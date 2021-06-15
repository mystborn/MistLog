#include <mist_log.h>
#include <time.h>

#ifdef _MSC_VER

#define LOG_WINDOWS
#include <fileapi.h>
#include <sys/stat.h>

#elif defined(__clang__) || defined(__GNUC__)

#define LOG_GCC
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

#endif

struct LogFile {
    String name;
    FILE* file;
    struct tm creation_time;
    int sequence;
};

struct LogFileTargetContext {
    struct LogFormat* file_name;
    struct LogFormat* archive_file_name;

    struct LogFile* files;
    int files_count;
    int files_capacity;

    String archive_date_format;

    char* buffer;
    size_t buffer_size;

    enum FileArchiveNumbering archive_numbering;
    enum FileArchiveTiming archive_timing;

    size_t archive_above_size;

    int max_archive_files;
    int max_archive_days;

    int buffer_mode;

    bool keep_files_open;
    bool custom_buffering;
};

struct LogFileTargetContext* log_file_target_context_create(char* fname) {
    struct LogFileTargetContext* ctx = calloc(1, sizeof(*ctx));
    if(!ctx)
        return NULL;

    ctx->file_name = ___log_parse_format(fname, 0, strlen(fname));
    if(!ctx->file_name) {
        free(ctx);
        return NULL;
    }

    string_init(&ctx->archive_date_format, "");
    return ctx;
}

void log_file_target_context_free(void* ptr) {
    struct LogFileTargetContext* ctx = ptr;
    ___log_free_format(ctx->file_name);

    if(ctx->archive_file_name)
        ___log_free_format(ctx->archive_file_name);

    if(ctx->buffer)
        free(ctx->buffer);

    if(ctx->files)
        free(ctx->files);

    string_free_resources(&ctx->archive_date_format);

    free(ctx);
}

bool log_file_target_context_archive_fname(struct LogFileTargetContext* ctx, char* archive_fname) {
    struct LogFormat* format = ___log_parse_format(archive_fname, 0, strlen(archive_fname));
    if(!format)
        return false;

    ctx->archive_file_name = format;
    return true;
}

bool log_file_target_context_set_buffering(struct LogFileTargetContext* ctx, size_t size, int mode) {
    if(ctx->custom_buffering && ctx->buffer_mode != _IONBF) {
        free(ctx->buffer);
        ctx->buffer = NULL;
    }
    
    if(mode != _IONBF) {

        if(size == 0)
        {
            ctx->custom_buffering = false;
            return true;
        } else {
            char* buffer = malloc(size);
            if(!buffer)
                return false;

            ctx->buffer = buffer;
            ctx->buffer_size = size;
        }
    }

    ctx->buffer_mode = mode;
    ctx->custom_buffering = true;
    return true;
}

void log_file_target_context_set_max_archive_files(struct LogFileTargetContext* ctx, int max_file_count) {
    ctx->max_archive_files = max_file_count;
}

void log_file_target_context_set_max_archive_days(struct LogFileTargetContext* ctx, int max_file_days) {
    ctx->max_archive_days = max_file_days;
}

void log_file_target_context_archive_on_size(struct LogFileTargetContext* ctx, size_t max_size) {
    ctx->archive_timing = FILE_ARCHIVE_SIZE;
    ctx->archive_above_size = max_size;
}

void log_file_target_archive_on_date(struct LogFileTargetContext* ctx, enum FileArchiveTiming timing) {
    ctx->archive_timing = timing;
}

void log_file_target_context_archive_number_sequence(struct LogFileTargetContext* ctx) {
    ctx->archive_numbering = FILE_ARCHIVE_NUMBER_SEQUENCE;
}

bool log_file_target_context_archive_number_date(struct LogFileTargetContext* ctx, enum FileArchiveNumbering numbering, char* date_string) {
    ctx->archive_numbering = FILE_ARCHIVE_NUMBER_DATE;
    string_clear(&ctx->archive_date_format);
    return string_append_cstr(&ctx->archive_date_format, date_string);
}

bool log_file_target_context_keep_files_open(struct LogFileTargetContext* ctx) {
    ctx->keep_files_open = true;

    int capacity = 2;
    ctx->files = malloc(sizeof(*ctx->files) * capacity);
    if(!ctx->files)
        return false;

    ctx->files_capacity = capacity;

    return true;
}

static bool log_file_exists(String* fname) {
#ifdef LOG_WINDOWS

    DWORD attribs = GetFileAttributesA(string_data(fname));
    return attribs != INVALID_FILE_ATTRIBUTES &&
           !(attribs & FILE_ATTRIBUTE_DIRECTORY);

#elif LOG_GCC

    return access(string_data(fname), F_OK) == 0;

#else
    FILE* file = fopen(string_data(fname), "r");
    if(file) {
        fclose(file);
        return true;
    }

    return false;
#endif
}

static bool log_file_info_attribute(struct LogFile* file, char* attrib, String* value) {
    size_t ext_start = string_rfind_cstr(&file->name, string_size(&file->name), ".");
    String log_info = string_create("");
    if(!string_append_string(&log_info, &file->name))
        return false;
    
    if(ext_start != SIZE_MAX) {
        string_erase(&log_info, ext_start, string_size(&log_info) - ext_start);
    }

    if(!string_append_cstr(&log_info, ".li")) {
        string_free_resources(&log_info);
        return false;
    }

    FILE* log_info_file = fopen(string_data(&log_info), "r");
    if(!log_info_file) {
        string_free_resources(&log_info);
        return false;
    }

    size_t attrib_length = strlen(attrib);

    // Prefer getLine implementation if possible to avoid reading whole file into program.
#ifdef LOG_GCC

    char* line;
    size_t line_buf_size;
    ssize_t line_size;

    size_t attrib_length = strlen(attrib);
    bool result = false;

    while((line_size = getLine(&line, &line_buf_size, log_info_file)) != -1) {
        if(strncmp(line, attrib, attrib_length) == 0) {
            if(line_size > attrib_length + 1) {
                if(!string_append_cstr_part(value, line, attrib_length + 1, line_size - attrib_length - 1))
                    break;
                
                result = true;
                break;
            }
        }
    }

    free(line);
    string_free_resources(&log_info);
    fclose(log_info_file);
    return result;

#else

    if(fseek(log_info_file, 0, SEEK_END) == 0) {
        int64_t fsize = ftell(log_info_file);
        if(fsize == -1) {
            string_free_resources(&log_info);
            fclose(log_info_file);
            return false;
        }

        String file_contents = string_create("");
        if(!string_reserve(&file_contents, fsize)) {
            string_free_resources(&log_info);
            fclose(log_info_file);
            return false;
        }

        if(fseek(log_info_file, 0, SEEK_SET) != 0) {
            string_free_resources(&log_info);
            string_free_resources(&file_contents);
            fclose(log_info_file);
            return false;
        }

        size_t file_read_length = fread(string_cstr(&file_contents), sizeof(char), fsize, log_info_file);
        if(ferror(log_info_file) != 0) {
            string_free_resources(&log_info);
            string_free_resources(&file_contents);
            fclose(log_info_file);
            return false;
        }

        string_cstr(&file_contents)[file_read_length] = '\0';
        if(___sso_string_is_long(&file_contents))
            ___sso_string_long_set_size(&file_contents, file_read_length);
        else
            ___sso_string_short_set_size(&file_contents, file_read_length);

        String new_line = string_create("\n");
        size_t line_count;
        String* lines = lstring_split(&file_contents, &new_line, NULL, -1, &line_count, true, true);
        if(!lines) {
            string_free_resources(&log_info);
            string_free_resources(&file_contents);
            fclose(log_info_file);
            return false;
        }

        for(size_t i = 0; i < line_count; i++) {
            if(string_starts_with_cstr(lines + i, attrib)) {
                bool result = string_append_string_part(value, lines + i, attrib_length + 1, string_size(lines + i) - attrib_length - 1);
                free(lines);
                string_free_resources(&log_info);
                string_free_resources(&file_contents);
                fclose(log_info_file);
                return result;
            }
        }

        free(lines);
        string_free_resources(&log_info);
        string_free_resources(&file_contents);
        fclose(log_info_file);
        return false;
    }

    string_free_resources(&log_info);
    fclose(log_info_file);
    return false;

#endif
}

static void log_file_creation_time(struct LogFile* file) {
#ifdef LOG_WINDOWS

    WIN32_FILE_ATTRIBUTE_DATA file_data;
    if(GetFileAttributesExA(string_data(&file->name), GetFileExInfoStandard, &file_data)) {

        ULARGE_INTEGER ull; 
        ull.LowPart = file_data.ftCreationTime.dwLowDateTime;
        ull.HighPart = file_data.ftCreationTime.dwHighDateTime;
        time_t t = ull.QuadPart / 10000000ULL - 11644473600ULL;

        file->creation_time = *localtime(&t);
        return;
    }


#elif LOG_GCC
    struct statx buffer;
    if(statx(AT_FDCWD, string_data(&file->fname), AT_STATX_SYNC_AS_STAT, STATX_BTIME, &buffer) == 0) {
        time_t t = buffer.stx_btime.tv_sec;
        file->creation_time = *localtime(&t);
        return;
    }
#endif

    String create_time = string_create("");
    if(log_file_info_attribute(file, "creation_time", &create_time)) {
        time_t t;
        if(sscanf(string_data(&create_time), "%lld", &t) == 1) {
            file->creation_time = *localtime(&t);
        }

        string_free_resources(&create_time);
    }
}

static void log_file_sequence(struct LogFile* file) {
    String sequence = string_create("");
    if(log_file_info_attribute(file, "sequence", &sequence)) {
        int sequence_num;
        
        if(sscanf(string_data(&sequence), "%d", &sequence_num) == 1) {
            file->sequence = sequence_num;
        }
        string_free_resources(&sequence);
    }

    file->sequence = 1;
}

static struct LogFile* log_file_open(struct LogFileTargetContext* ctx, String* fname) {
    for(int i = 0; i < ctx->files; i++) {
        if(string_equals_string(&ctx->files[i].name, fname)) {
            return ctx->files + i;
        }
    }

    if(ctx->files_count == ctx->files_capacity) {
        size_t capacity = ctx->files_capacity * 2;
        void* buffer = realloc(ctx->files, sizeof(*ctx->files) * capacity);
        if(!buffer)
            return NULL;
        
        ctx->files = buffer;
        ctx->files_capacity = capacity;
    }

    struct LogFile* file = ctx->files + ctx->files_count;

    if(log_file_exists(fname)) {
        string_copy(fname, &file->name);
        file->file = fopen(string_data(fname), "a");
        if(!file->file)
            return NULL;

        if(ctx->archive_timing != FILE_ARCHIVE_NONE && ctx->archive_timing != FILE_ARCHIVE_SIZE) {
            log_file_creation_time(file);
        }

        if(ctx->archive_numbering == FILE_ARCHIVE_NUMBER_SEQUENCE) {
            log_file_sequence(file);
        }
    } else {
    }

    return file;
}

static void log_file_archive_if_needed(struct LogFileTargetContext* ctx, struct LogFile* file) {
    
}

static void ___log_file_log(enum LogLevel log_level, const char* file, const char* function, uint32_t line, String* msg, void* ptr) {
    struct LogFileTargetContext* ctx = ptr;
    String fname = string_create("");

    if(!___log_format(ctx->file_name, log_level, file, function, line, &fname, "%s", string_data(msg)))
        return;
}