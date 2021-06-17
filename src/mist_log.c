#include <mist_log.h>

#include <time.h>

#ifdef _MSC_VER

#define LOG_WINDOWS
#include <Windows.h>
#include <sys/stat.h>

#elif defined(__clang__) || defined(__GNUC__)

#define LOG_GCC
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <glob.h>

#if __GNUC__ > 2 || (__GNUC__ == 2 && (__GNUC_MINOR__ >= 28))

#define LOG_STATX

#endif

#endif

// TODO: Max Archive Files

struct LogFormatTime {
    String format;
    bool is_utc;
};

struct LogLayoutRendererCreator {
    const char* name;
    void* ctx;
    struct LogLayoutRenderer* (*create)(char* text, size_t start, size_t count, void* ctx);
    void (*free)(void* ctx);
};

struct LogLayoutRendererFinder {
    struct LogLayoutRendererCreator** registered_creators;
    size_t count;
    size_t capacity;
};

struct LogFile {
    struct tm creation_time;
    String name;
    FILE* file;
    char* buffer;
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

static struct LogLayoutRendererFinder log_renderer_finder = { NULL, 0, 0 };

// ========================
// SECION: Layout Renderers
// ========================

LOG_EXPORT bool ___log_format_read_arg_name(char* text, size_t* start, size_t count, String* name) {
    for(size_t iter = 0; iter < count; iter++) {
        if(iter + 1 < count && text[*start] == '\\') {
            switch(text[*start + 1]) {
                case '\\':
                    if(!string_append_cstr(name, "\\"))
                        return false;
                    iter += 2;
                    continue;
                case ':':
                    if(!string_append_cstr(name, ":"))
                        return false;
                    iter += 2;
                    continue;
                case '=':
                    if(!string_append_cstr(name, "="))
                        return false;
                    iter += 2;
                    continue;
                case '}':
                    if(!string_append_cstr(name, "}"))
                        return false;
                    iter += 2;
                    continue;
            }
        }

        switch(text[*start]) {
            case ':':
            case '=':
                return true;
        }

        if(!string_append_cstr_part(name, text, *start, 1))
            return false;
        (*start)++;
    }

    return true;
}

LOG_EXPORT bool ___log_format_read_arg_value(char* text, size_t* start, size_t count, bool as_format, String* value, struct LogFormat** format) {
    if(as_format) {
        *format = ___log_parse_format(text, *start, count);
        if(!(*format))
            return false;
        (*start) += count;
        return true;
    } else {
        return ___log_format_read_arg_name(text, start, count, value);
    }
}

static bool log_message_append_uint(String* message, uint32_t number) {
    // The maximum number of characters in a uint is 10 (UINT_MAX).
    if(!string_reserve(message, string_size(message) + 10))
        return false;

    size_t current_size = string_size(message);
    size_t written = sprintf(string_cstr(message) + current_size, "%u", number);

    if(___sso_string_is_long(message))
        ___sso_string_long_set_size(message, current_size + written);
    else
        ___sso_string_short_set_size(message, current_size + written);

    return true;
}

static bool log_format_level(enum LogLevel log_level, const char* file, const char* function, uint32_t line, String* message, void *ctx, char* format, va_list args) {
    switch(log_level) {
        case LOG_TRACE: return string_append_cstr(message, "Trace");
        case LOG_DEBUG: return string_append_cstr(message, "Debug");
        case LOG_INFO: return string_append_cstr(message, "Info");
        case LOG_WARN: return string_append_cstr(message, "Warn");
        case LOG_ERROR: return string_append_cstr(message, "Error");
        case LOG_FATAL: return string_append_cstr(message, "Fatal");
        default: return true;
    }
}

static struct LogLayoutRenderer* log_renderer_create_level(char* text, size_t start, size_t count, void* ctx) {
    struct LogLayoutRenderer* renderer = malloc(sizeof(*renderer));
    if(!renderer)
        return NULL;

    renderer->append = log_format_level;
    renderer->free = NULL;
    renderer->ctx = NULL;

    return renderer;
}

static struct LogLayoutRendererCreator* log_layout_renderer_creator_level() {
    struct LogLayoutRendererCreator* creator = malloc(sizeof(*creator));
    if(!creator)
        return NULL;

    creator->name = "level";
    creator->create = log_renderer_create_level;
    creator->ctx = NULL;
    creator->free = NULL;

    return creator;
}

static bool log_format_text(enum LogLevel log_level, const char* file, const char* function, uint32_t line, String* message, void *ctx, char* format, va_list args) {
    return string_append_string(message, (String*)ctx);
}

static void log_format_text_free(void* ctx) {
    free(ctx);
}

static bool log_format_date_time(enum LogLevel log_level, const char* file, const char* function, uint32_t line, String* message, void *ctx, char* format, va_list args) {
    struct LogFormatTime* time_format = ctx;
    struct tm* time_info;
    time_t raw_time;
    time(&raw_time);

    if(time_format->is_utc) {
        time_info = gmtime(&raw_time);
    } else {
        time_info = localtime(&raw_time);
    }

    size_t written = 0;
    size_t reserve = 0;
    size_t current_size = string_size(message);
    do {
        // If the formatting fails, make sure the string is properly terminated.
        if(!string_reserve(message, string_size(message) + reserve)) {
            string_cstr(message)[string_size(message)] = '\0';
            return false;
        }

        // Offset the start by string_size so that it starts writing the date at the end of the string.
        // The remaining size is the difference of the capacity of the string (including the null-terminating 
        // character) and the current_size.
        written = strftime(
            string_cstr(message) + current_size, 
            string_capacity(message) + 1 - current_size, 
            string_data(&time_format->format), 
            time_info);

        // Makes sure the string grows at least one size up.
        reserve = string_capacity(message) + 1;
    }
    while(written == 0);

    if(___sso_string_is_long(message))
        ___sso_string_long_set_size(message, current_size + written);
    else
        ___sso_string_short_set_size(message, current_size + written);

    return true;
}

static log_format_date_time_free(void* ctx) {
    struct LogFormatTime* format_time = ctx;
    string_free_resources(&format_time->format);
    free(format_time);
}

static struct LogLayoutRenderer* log_renderer_create_date_time(char* text, size_t start, size_t count, void* ctx) {
    String arg_name = string_create("");
    String arg_value = string_create("");

    struct LogFormatTime* format_time = calloc(1, sizeof(*format_time));
    if(!format_time)
        goto error;

    string_init(&format_time->format, "");

    struct LogLayoutRenderer* renderer = malloc(sizeof(*renderer));
    if(!renderer)
        goto error;

    renderer->ctx = format_time;
    renderer->free = log_format_date_time_free;
    renderer->append = log_format_date_time;

    size_t arg_start = start;
    while(arg_start - start < count) {
        string_clear(&arg_name);
        if(!___log_format_read_arg_name(text, &arg_start, count - (arg_start - start), &arg_name))
            break;

        arg_start++;

        if(text[arg_start - 1] == '=') {
            string_clear(&arg_value);
            if(!___log_format_read_arg_value(text, &arg_start, count - (arg_start - start), false, &arg_value, NULL))
                break;
        }

        if(string_equals_cstr(&arg_name, "utc")) {
            if(string_size(&arg_value) == 0) {
                format_time->is_utc = true;
            } else {
                format_time->is_utc = string_equals_cstr(&arg_value, "true");
            }
        } else if(string_equals_cstr(&arg_name, "format")) {
            if(string_size(&arg_value) == 0)
                continue;

            string_append_string(&format_time->format, &arg_value);
        }
    }

    // If no date time format was specified, add a default format.
    // The format is locale specific but looks something like:
    // 08/23/01 14:55:02
    if(string_size(&format_time->format) == 0) {
        if(!string_append_cstr(&format_time->format, "%x %X"))
            goto error;
    }

    string_free_resources(&arg_value);
    string_free_resources(&arg_name);

    return renderer;

    error:
        if(format_time != NULL) {
            string_free_resources(&format_time->format); 
            free(format_time);
        }

        if(renderer) {
            free(renderer);
        }

        string_free_resources(&arg_name);
        string_free_resources(&arg_value);

        return NULL;
}

static struct LogLayoutRendererCreator* log_layout_renderer_creator_date_time() {
    struct LogLayoutRendererCreator* creator = malloc(sizeof(*creator));
    if(!creator)
        return NULL;

    creator->name = "time";
    creator->create = log_renderer_create_date_time;
    creator->ctx = NULL;
    creator->free = NULL;

    return creator;
}

static bool log_format_counter(enum LogLevel log_level, const char* file, const char* function, uint32_t line, String* message, void *ctx, char* format, va_list args) {
    return log_message_append_uint(message, ++(*(uint32_t*)ctx));
}

static void log_format_counter_free(void* ctx) {
    free(ctx);
}

static struct LogLayoutRenderer* log_renderer_create_counter(char* text, size_t start, size_t count, void* ctx) {
    struct LogLayoutRenderer* renderer = malloc(sizeof(*renderer));
    if(!renderer)
        return NULL;

    uint32_t* counter = malloc(sizeof(*counter));
    if(!counter) {
        free(renderer);
        return NULL;
    }

    *counter = 1;
    renderer->append = log_format_counter;
    renderer->free = log_format_counter_free;
    renderer->ctx = counter;

    return renderer;
}

static struct LogLayoutRendererCreator* log_layout_renderer_creator_counter() {
    struct LogLayoutRendererCreator* creator = malloc(sizeof(*creator));
    if(!creator)
        return NULL;

    creator->name = "counter";
    creator->create = log_renderer_create_counter;
    creator->ctx = NULL;
    creator->free = NULL;

    return creator;
}

static bool log_format_file(enum LogLevel log_level, const char* file, const char* function, uint32_t line, String* message, void *ctx, char* format, va_list args) {
    return string_append_cstr(message, file);
}

static struct LogLayoutRenderer* log_renderer_create_file(char* text, size_t start, size_t count, void* ctx) {
    struct LogLayoutRenderer* renderer = malloc(sizeof(*renderer));
    if(!renderer)
        return NULL;
    
    renderer->append = log_format_file;
    renderer->free = NULL;
    renderer->ctx = NULL;

    return renderer;
}

static struct LogLayoutRendererCreator* log_layout_renderer_creator_file() {
    struct LogLayoutRendererCreator* creator = malloc(sizeof(*creator));
    if(!creator)
        return NULL;

    creator->name = "file";
    creator->create = log_renderer_create_file;
    creator->ctx = NULL;
    creator->free = NULL;

    return creator;
}

static bool log_format_function(enum LogLevel log_level, const char* file, const char* function, uint32_t line, String* message, void *ctx, char* format, va_list args) {
    return string_append_cstr(message, function);
}

static struct LogLayoutRenderer* log_renderer_create_function(char* text, size_t start, size_t count, void* ctx) {
    struct LogLayoutRenderer* renderer = malloc(sizeof(*renderer));
    if(!renderer)
        return NULL;
    
    renderer->append = log_format_function;
    renderer->free = NULL;
    renderer->ctx = NULL;

    return renderer;
}

static struct LogLayoutRendererCreator* log_layout_renderer_creator_function() {
    struct LogLayoutRendererCreator* creator = malloc(sizeof(*creator));
    if(!creator)
        return NULL;

    creator->name = "function";
    creator->create = log_renderer_create_function;
    creator->ctx = NULL;
    creator->free = NULL;

    return creator;
}

static bool log_format_line(enum LogLevel log_level, const char* file, const char* function, uint32_t line, String* message, void *ctx, char* format, va_list args) {
    return log_message_append_uint(message, line);
}

static struct LogLayoutRenderer* log_renderer_create_line(char* text, size_t start, size_t count, void* ctx) {
    struct LogLayoutRenderer* renderer = malloc(sizeof(*renderer));
    if(!renderer)
        return NULL;
    
    renderer->append = log_format_line;
    renderer->free = NULL;
    renderer->ctx = NULL;

    return renderer;
}

static struct LogLayoutRendererCreator* log_layout_renderer_creator_line() {
    struct LogLayoutRendererCreator* creator = malloc(sizeof(*creator));
    if(!creator)
        return NULL;

    creator->name = "line";
    creator->create = log_renderer_create_line;
    creator->ctx = NULL;
    creator->free = NULL;

    return creator;
}

static bool log_format_message(enum LogLevel log_level, const char* file, const char* function, uint32_t line, String* message, void *ctx, char* format, va_list args) {
    va_list copy;
    va_copy(copy, args);

    String* result = string_format_args_cstr(message, format, args);

    va_end(copy);

    return result != NULL;
}

static struct LogLayoutRenderer* log_renderer_create_message(char* text, size_t start, size_t count, void* ctx) {
    struct LogLayoutRenderer* renderer = malloc(sizeof(*renderer));
    if(!renderer)
        return NULL;
    
    renderer->append = log_format_message;
    renderer->free = NULL;
    renderer->ctx = NULL;

    return renderer;
}

static struct LogLayoutRendererCreator* log_layout_renderer_creator_message() {
    struct LogLayoutRendererCreator* creator = malloc(sizeof(*creator));
    if(!creator)
        return NULL;

    creator->name = "message";
    creator->create = log_renderer_create_message;
    creator->ctx = NULL;
    creator->free = NULL;

    return creator;
}

static bool log_format_finder_init() {
    if(log_renderer_finder.capacity != 0)
        return true;

    size_t capacity = 16;
    size_t count = 0;

    log_renderer_finder.registered_creators = calloc(capacity, sizeof(*log_renderer_finder.registered_creators));


    if(!log_renderer_finder.registered_creators)
        goto error;

    log_renderer_finder.capacity = capacity;

    log_renderer_finder.registered_creators[count] = log_layout_renderer_creator_level();
    if(!log_renderer_finder.registered_creators[count++])
        goto error;

    log_renderer_finder.registered_creators[count] = log_layout_renderer_creator_date_time();
    if(!log_renderer_finder.registered_creators[count++])
        goto error;

    log_renderer_finder.registered_creators[count] = log_layout_renderer_creator_counter();
    if(!log_renderer_finder.registered_creators[count++])
        goto error;

    log_renderer_finder.registered_creators[count] = log_layout_renderer_creator_file();
    if(!log_renderer_finder.registered_creators[count++])
        goto error;
        
    log_renderer_finder.registered_creators[count] = log_layout_renderer_creator_function();
    if(!log_renderer_finder.registered_creators[count++])
        goto error;
        
    log_renderer_finder.registered_creators[count] = log_layout_renderer_creator_line();
    if(!log_renderer_finder.registered_creators[count++])
        goto error;
        
    log_renderer_finder.registered_creators[count] = log_layout_renderer_creator_message();
    if(!log_renderer_finder.registered_creators[count++])
        goto error;

    log_renderer_finder.count = count;

    return true;

    error:
        for(int i = 0; i < count; i++) {
            struct LogLayoutRendererCreator* creator = log_renderer_finder.registered_creators[i];
            if(creator->free != NULL)
                creator->free(creator->ctx);
            free(creator);
        }

        free(log_renderer_finder.registered_creators);
        return false;
}

LOG_EXPORT bool ___log_register_log_format_creator(const char* name, struct LogLayoutRenderer* (*create)(char* text, size_t start, size_t count, void* ctx), void* ctx, void (*free)(void* ctx)) {
    struct LogLayoutRendererCreator* creator = malloc(sizeof(*creator));
    if(!creator)
        return false;

    creator->name = name;
    creator->create = create;
    creator->ctx = ctx;
    creator->free = free;

    if(log_renderer_finder.count == log_renderer_finder.capacity) {
        size_t capacity = log_renderer_finder.capacity * 2;
        void* buffer = realloc(log_renderer_finder.registered_creators, sizeof(*log_renderer_finder.registered_creators));
        if(!buffer) {
            free(creator);
            return false;
        }
        log_renderer_finder.registered_creators = buffer;
        log_renderer_finder.capacity = capacity;
    }

    log_renderer_finder.registered_creators[log_renderer_finder.count++] = creator;
    return true;
}

static struct LogLayoutRenderer* ___log_create_renderer(char* format, size_t start, size_t count) {
    size_t name_length = 0;
    for(int i = 0; i < count; i++) {
        if(format[i + start] == ':') {
            if(i != count && format[i + start + 1] == ':') {
                i += 2;
                continue;
            }

            break;
        }

        name_length++;
    }

    for(int i = 0; i < log_renderer_finder.count; i++) {
        struct LogLayoutRendererCreator* creator = log_renderer_finder.registered_creators[i];
        if(strncmp(format + start, creator->name, name_length) == 0) {
            return creator->create(format, start + name_length + 1, count - name_length - 1, creator->ctx);
        }
    }

    return NULL;
}

static struct LogLayoutRenderer* ___log_create_text_renderer(char* format, size_t start, size_t count) {
    String* part = string_create_ref("");
    if(!part)
        return NULL;

    if(!string_append_cstr_part(part, format, start, count)) {
        string_free(part);
        return NULL;
    }
    struct LogLayoutRenderer* step = malloc(sizeof(*step));
    if(!step) {
        string_free(part);
        return NULL;
    }

    step->ctx = part;
    step->append = log_format_text;
    step->free = log_format_text_free;

    return step;
}

// ===================
// SECTION: Formatting
// ===================

struct LogFormat* ___log_parse_format(char* format, size_t start, size_t count) {
    if (!log_format_finder_init())
        return NULL;
    // "${time:utc=true:format=%x %X} | ${upper:inner=${level}} ${level} | ${file} | ${message}";
    size_t capacity = 2;
    size_t renderer_count = 0;
    struct LogLayoutRenderer** renderers = malloc(sizeof(*renderers) * capacity);
    if(!renderers)
        goto error;

    size_t format_start = start;

    for(size_t iter = 0; iter < count; iter++) {
        size_t i = iter + start;
        
        // A layout renderer escape sequence has been detected. Start creating the renderer.
        if(iter + 1 < count && format[i] == '$' && format[i + 1] == '{') {
            // If there was any text before the layout renderer, create a raw
            // text renderer containing it.
            if(i != format_start) {

                // Resize the renderers list if the capacity has been reached.
                if(renderer_count == capacity) {
                    capacity *= 2;
                    void* buffer = realloc(renderers, sizeof(*renderers) * capacity);
                    if(!buffer)
                        goto error;
                    renderers = buffer;
                }

                struct LogLayoutRenderer* renderer = ___log_create_text_renderer(format, format_start, i - format_start);
                if (!renderer)
                    goto error;
                renderers[renderer_count++] = renderer;
            }

            iter += 2;
            format_start = i + 2;
            int brace_count = 1;
            for(; iter < count; iter++) {
                i = iter + start;
                // Some renderers have inner renderers that need to be accounted for.
                // This step makes sure braces are matched correctly accordingly.
                if(iter + 1 < count && format[i] == '$' && format[i + 1] == '{') {
                    brace_count++;
                }

                if(format[i] == '}') {
                    // If all braces have been matched for the current layer, create a layout renderer
                    // from the matched string.
                    if(--brace_count == 0) {
                        struct LogLayoutRenderer* renderer = ___log_create_renderer(format, format_start, i - format_start);
                        if(!renderer)
                            goto error;
                        
                        // Resize the renderers list if the capacity has been reached.
                        if(renderer_count == capacity) {
                            capacity *= 2;
                            void* buffer = realloc(renderers, sizeof(*renderers) * capacity);
                            if(!buffer)
                                goto error;
                            renderers = buffer;
                        }

                        renderers[renderer_count++] = renderer;
                        break;
                    }
                }

            }

            format_start = i + 1;
        }
    }

    // If there was any text at the end of the string, create a raw text
    // renderer containing it.
    if(format_start != start + count) {
        // Resize the renderers list if the capacity has been reached.
        if(renderer_count == capacity) {
            capacity *= 2;
            void* buffer = realloc(renderers, sizeof(*renderers) * capacity);
            if(!buffer)
                goto error;
            renderers = buffer;
        }

        struct LogLayoutRenderer* renderer = ___log_create_text_renderer(format, format_start, start + count - format_start);
        if(!renderer)
            goto error;
        renderers[renderer_count++] = renderer;
    }

    // Create the actual log format structure that will contain the layout renderers.
    struct LogFormat* log_format = malloc(sizeof(*log_format));
    if(!log_format)
        goto error;

    log_format->steps = renderers;
    log_format->step_count = renderer_count;

    return log_format;

    error:
        if(renderers) {
            for(int i = 0; i < renderer_count; i++) {
                if(renderers[i]->free != NULL)
                    renderers[i]->free(renderers[i]->ctx);
                free(renderers[i]);
            }

            free(renderers);
        }

        return NULL;
}

LOG_EXPORT void ___log_format_free(struct LogFormat* log_format) {
    if(!log_format)
        return;

    for(size_t i = 0; i < log_format->step_count; i++) {
        struct LogLayoutRenderer* renderer = log_format->steps[i];
        if(renderer->free)
            renderer->free(renderer->ctx);
        free(renderer);
    }
    free(log_format);
}

LOG_EXPORT bool ___log_format(struct LogFormat* log_format, enum LogLevel level, const char* file, const char* function, uint32_t line, String* message, char* format_string, va_list args) {
    for(int i = 0; i < log_format->step_count; i++) {
        struct LogLayoutRenderer* step = log_format->steps[i];
        if(!step->append(level, file, function, line, message, step->ctx, format_string, args)) {
            string_clear(message);
            return false;
        }
    }

    return true;
}

LOG_EXPORT Logger* log_logger_create() {
    Logger* logger = calloc(1, sizeof(*logger));
    if(!logger)
        return NULL;

    return logger;
}

LOG_EXPORT void log_logger_free(Logger* logger) {
    for(int i = 0; i < logger->target_count; i++) {
        log_target_free(logger->targets[i]);
    }

    free(logger);
}

LOG_EXPORT void log_target_free(LogTarget* target) {
    if(!target)
        return;

    struct LogFormat* fmt = target->format;
    for(int i = 0; i < fmt->step_count; i++) {
        struct LogLayoutRenderer* renderer = fmt->steps[i];
        if(renderer->free)
            renderer->free(renderer->ctx);
        free(renderer);
    }
    free(fmt->steps);
    free(fmt);

    if(target->free)
        target->free(target->ctx);

    free(target);
}

LOG_EXPORT bool log_add_target(Logger* logger, LogTarget* target) {
    if(!logger)
        return false;
    
    if(logger->target_count == logger->target_capacity) {
        int capacity = logger->target_capacity == 0 ? 2 : logger->target_capacity * 2;
        void* buffer = realloc(logger->targets, sizeof(*logger->targets) * capacity);
        if(!buffer)
            return false;
        
        logger->targets = buffer;
        logger->target_capacity = capacity;
    }

    logger->targets[logger->target_count++] = target;
    return true;
}

LOG_EXPORT void log_set_lock(Logger* logger, void* mutex, void (*lock)(void* mtx, bool lock)) {
    if(!logger)
        return;

    logger->mutex = mutex;
    logger->lock = lock;
}


static bool log_log_impl(Logger* logger, enum LogLevel log_level, const char* file, const char* function, int line, const char* message, va_list args) {
    if(!logger)
        return false;

    if(logger->mutex && logger->lock)
        logger->lock(logger->mutex, true);

    String output = string_create("");

    for(int i = 0; i < logger->target_count; i++) {
        LogTarget* target = logger->targets[i];
        if(log_level < target->min_level || log_level > target->max_level)
            continue;

        string_clear(&output);
        if(!___log_format(target->format, log_level, file, function, line, &output, message, args))
            return false;

        target->log(log_level, file, function, line, &output, target->ctx);
    }

    string_free_resources(&output);

    if(logger->mutex && logger->lock)
        logger->lock(logger->mutex, false);

    return true;
}

LOG_EXPORT bool ___log_string(Logger* logger, enum LogLevel log_level, const char* file, int line, const String* message, ...) {
    va_list args;
    va_start(args, message);

    bool result = log_log_impl(logger, log_level, file, "", line, string_data(message), args);

    va_end(args);

    return result;
}

LOG_EXPORT bool ___log_func_string(Logger* logger, enum LogLevel log_level, const char* file, const char* function, int line, const String* message, ...) {
    va_list args;
    va_start(args, message);

    bool result = log_log_impl(logger, log_level, file, function, line, string_data(message), args);

    va_end(args);

    return result;
}

LOG_EXPORT bool ___log_cstr(Logger* logger, enum LogLevel log_level, const char* file, int line, const char* message, ...) {
    va_list args;
    va_start(args, message);

    bool result = log_log_impl(logger, log_level, file, "", line, message, args);

    va_end(args);

    return result;
}

LOG_EXPORT bool ___log_func_cstr(Logger* logger, enum LogLevel log_level, const char* file, const char* function, int line, const char* message, ...) {
    va_list args;
    va_start(args, message);

    bool result = log_log_impl(logger, log_level, file, function, line, message, args);

    va_end(args);

    return result;
}

// ====================
// SECTION: Log Targets
// ====================

static void log_console_log(enum LogLevel log_level, const char* file, const char* function, uint32_t line, String* msg, void* ctx) {
    puts(string_data(msg));
}

LogTarget* log_target_console_create(const char* layout, enum LogLevel min_level, enum LogLevel max_level) {
    LogTarget* target = malloc(sizeof(*target));
    if(!target)
        return NULL;

    struct LogFormat* fmt = ___log_parse_format(layout, 0, strlen(layout));
    if(!fmt) {
        free(target);
        return NULL;
    }

    target->format = fmt;
    target->free = NULL;
    target->ctx = NULL;
    target->log = log_console_log;
    target->min_level = min_level;
    target->max_level = max_level;

    return target;
}

LOG_EXPORT struct LogFileTargetContext* log_file_target_context_create(char* fname) {
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

static void log_file_target_context_free_generic(void* ptr) {
    struct LogFileTargetContext* ctx = ptr;
    log_file_target_context_free(ctx);
}

LOG_EXPORT void log_file_target_context_free(struct LogFileTargetContext* ctx) {
    ___log_format_free(ctx->file_name);

    if(ctx->archive_file_name)
        ___log_format_free(ctx->archive_file_name);

    if(ctx->files) {
        for(size_t i = 0; i < ctx->files_count; i++) {
            struct LogFile* file = ctx->files + i;
            if(file->file)
                fclose(file->file);
            string_free_resources(&file->name);
            free(file->buffer);
        }

        free(ctx->files);
    }

    string_free_resources(&ctx->archive_date_format);
    
    free(ctx);
}

LOG_EXPORT bool log_file_target_context_archive_fname(struct LogFileTargetContext* ctx, char* archive_fname) {
    struct LogFormat* format = ___log_parse_format(archive_fname, 0, strlen(archive_fname));
    if(!format)
        return false;

    ctx->archive_file_name = format;
    return true;
}

LOG_EXPORT bool log_file_target_context_set_buffering(struct LogFileTargetContext* ctx, size_t size, int mode) {
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

LOG_EXPORT void log_file_target_context_set_max_archive_files(struct LogFileTargetContext* ctx, int max_file_count) {
    ctx->max_archive_files = max_file_count;
}

LOG_EXPORT void log_file_target_context_set_max_archive_days(struct LogFileTargetContext* ctx, int max_file_days) {
    ctx->max_archive_days = max_file_days;
}

LOG_EXPORT void log_file_target_context_archive_on_size(struct LogFileTargetContext* ctx, size_t max_size) {
    ctx->archive_timing = FILE_ARCHIVE_SIZE;
    ctx->archive_above_size = max_size;
}

LOG_EXPORT void log_file_target_archive_on_date(struct LogFileTargetContext* ctx, enum FileArchiveTiming timing) {
    ctx->archive_timing = timing;
}

LOG_EXPORT void log_file_target_context_archive_number_sequence(struct LogFileTargetContext* ctx) {
    ctx->archive_numbering = FILE_ARCHIVE_NUMBER_SEQUENCE;
}

LOG_EXPORT bool log_file_target_context_archive_number_date(struct LogFileTargetContext* ctx, enum FileArchiveNumbering numbering, char* date_string) {
    ctx->archive_numbering = FILE_ARCHIVE_NUMBER_DATE;
    string_clear(&ctx->archive_date_format);
    return string_append_cstr(&ctx->archive_date_format, date_string);
}

LOG_EXPORT bool log_file_target_context_keep_files_open(struct LogFileTargetContext* ctx) {
    ctx->keep_files_open = true;

    int capacity = 2;
    ctx->files = malloc(sizeof(*ctx->files) * capacity);
    if(!ctx->files)
        return false;

    ctx->files_capacity = capacity;

    return true;
}

static bool log_file_exists(String* fname) {
#if defined(LOG_WINDOWS)

    DWORD attribs = GetFileAttributesA(string_data(fname));
    return attribs != INVALID_FILE_ATTRIBUTES &&
           !(attribs & FILE_ATTRIBUTE_DIRECTORY);

#elif defined(LOG_GCC)

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

    bool result = false;

    while((line_size = getLine(&line, &line_buf_size, log_info_file)) != -1) {
        if(strncmp(line, attrib, attrib_length) == 0) {
            if(line_size >= attrib_length + 1) {
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

static bool log_file_info_handle_attribute(
    struct LogFile* file, 
    char* attrib, 
    void* ctx, 
    void (*handle_attrib)(FILE* file, String* current_line, char* attrib, void* ctx))
{
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

    bool result = true;

    // Create a temporary file to copy the old data over to. This is the only
    // way to replace existing data unfortunately.
    String temp_name;
    if(!string_copy(&log_info, &temp_name) || !string_append_cstr(&temp_name, ".tmp")) {
        string_free_resources(&log_info);
        fclose(log_info_file);
        return false;
    }

    FILE* temp = fopen(string_data(&temp_name), "w");
    if(!temp) {
        string_free_resources(&log_info);
        string_free_resources(&temp_name);
        fclose(log_info_file);
        return false;
    }

    bool found = false;

    // Loop through all lines in the file.
    while((line_size = getLine(&line, &line_buf_size, log_info_file)) != -1) {
        // The attribute was found. Overwrite it with the new value.
        if(strncmp(line, attrib, attrib_length) == 0) {
            String str = string_create(line);
            handle_attrib(temp, &str, attrib, ctx);

            found = true;
        } else {
            // Not the attribute. Copy it over to the temp file.
            if(fputs(line, temp) < 0) {
                result = false;
                break;
            }
        }
    }

    // If the result wasn't found, append it to the end of the file.
    if(result && !found) {
        if(fprintf(temp, "%s=%s", attrib, string_data(value)) < 0) {
            result = false;
        }
    }

    // Close the files before anything else so that they can be removed/renamed.
    fclose(log_info_file);
    fclose(temp);

    // Replace the old log file with the new one if everything has been successful so far.
    if(result)
        result = remove(string_data(&log_info)) == 0 && rename(string_data(&temp_name), string_data(&log_info));

    // free the remaining resources.
    free(line);
    string_free_resources(&log_info);
    string_free_resources(&temp_name);
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
        String* lines = string_split(&file_contents, &new_line, NULL, -1, &line_count, true, true);
        string_free_resources(&file_contents);
        if(!lines) {
            string_free_resources(&log_info);
            fclose(log_info_file);
            return false;
        }

        log_info_file = freopen(string_data(&log_info), "w", log_info_file);
        if(!log_info_file) {
            string_free_resources(&log_info);
            return false;
        }

        bool result = true;
        bool found = false;

        for(size_t i = 0; i < line_count; i++) {
            if(string_starts_with_cstr(lines + i, attrib)) {
                handle_attrib(log_info_file, lines + i, attrib, ctx);
                found = true;
            } else {
                if(fprintf(log_info_file, "%s", string_data(lines + i)) < 0) {
                    result = false;
                    break;
                }
            }
        }

        if(result && !found) {
            handle_attrib(log_info_file, NULL, attrib, ctx);
        }

        free(lines);
        string_free_resources(&log_info);
        fclose(log_info_file);

        return result;
    }

    return false;
#endif
}

static void log_file_info_write_attribute_impl(FILE* file, char* current_line, char* attrib, void* ctx) {
    String* str = ctx;
    fprintf(file, "%s=%s", attrib, string_data(str));
}

static void log_file_info_append_attribute_impl(FILE* file, char* current_line, char* attrib, void* ctx) {
    String* str = ctx;
    fprintf(file, "%s%s", current_line, string_data(str));
}

static bool log_file_info_write_attribute(struct LogFile* file, char* attrib, String* value) {
    return log_file_info_handle_attribute(file, attrib, value, log_file_info_write_attribute_impl);
}

static bool log_file_info_append_attribute(struct LogFile* file, char* attrib, String* value) {
    return log_file_info_handle_attribute(file, attrib, value, log_file_info_append_attribute_impl);
}

#ifdef LOG_WINDOWS

static time_t log_file_time_to_time(FILETIME* ft) {
    ULARGE_INTEGER ull; 
    ull.LowPart = ft->dwLowDateTime;
    ull.HighPart = ft->dwHighDateTime;
    return ull.QuadPart / 10000000ULL - 11644473600ULL;
}

#endif

static void log_file_creation_time(struct LogFile* file) {
#if defined(LOG_WINDOWS)

    WIN32_FILE_ATTRIBUTE_DATA file_data;
    if(GetFileAttributesExA(string_data(&file->name), GetFileExInfoStandard, &file_data)) {
        time_t t = log_file_time_to_time(&file_data.ftCreationTime);
        file->creation_time = *localtime(&t);
        return;
    }

#elif defined(LOG_GCC) && defined(LOG_STATX)

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
            file->sequence = sequence_num + 1;
        }
        string_free_resources(&sequence);
    }

    file->sequence = 1;
}

static uint64_t log_file_size(struct LogFile* file) {
#if defined(LOG_WINDOWS)

    uint64_t size = GetFileSize(file->file, NULL);
    if(size == INVALID_FILE_SIZE)
        return 0;

    return size;

#elif defined(LOG_GCC) && defined(LOG_STATX)
    struct statx buffer;
    if(statx(AT_FDCWD, string_data(&file->fname), AT_STATX_SYNC_AS_STAT, STATX_SIZE, &buffer) == 0) {
        return buffer.stx_size;
    }
#else

    fseek(file->file, 0, SEEK_END);
    size_t size = ftell(file->file);
    fseek(file->file, 0, SEEK_SET);

    return size;

#endif
}

static struct LogFile* log_file_open(struct LogFileTargetContext* ctx, String* fname) {
    for(int i = 0; i < ctx->files; i++) {
        if(string_equals_string(&ctx->files[i].name, fname)) {
            if(!ctx->keep_files_open) {
                ctx->files[i].file = fopen(string_data(fname), "a");
                if(!ctx->files[i].file)
                    return NULL;
            }

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
        file->file = fopen(string_data(fname), "a");
        if(!file->file)
            return NULL;

        string_copy(fname, &file->name);

        if(ctx->archive_timing != FILE_ARCHIVE_NONE && ctx->archive_timing != FILE_ARCHIVE_SIZE) {
            log_file_creation_time(file);
        }

        if(ctx->archive_numbering == FILE_ARCHIVE_NUMBER_SEQUENCE) {
            log_file_sequence(file);
        }
    } else {
        file->file = fopen(string_data(fname), "a");
        if(!file->file)
            return NULL;

        string_copy(fname, &file->name);
        if(ctx->archive_timing != FILE_ARCHIVE_NONE && ctx->archive_timing != FILE_ARCHIVE_SIZE) {
            time_t t = time(NULL);
            file->creation_time = *localtime(&t);
        }

        if(ctx->archive_numbering == FILE_ARCHIVE_NUMBER_SEQUENCE) {
            log_file_sequence(file);
        }
    }

    return file;
}

static bool string_strip_extension(String* value, String* ext) {
    size_t position = string_rfind_cstr(value, string_size(value), ".");
    if(position == SIZE_MAX)
        return false;

    if(!string_append_string_part(ext, value, position, string_size(value) - position))
        return false;

    string_erase(value, position, string_size(value) - position);
    return true;
}

struct LogArchiveDeleteContext {
    // Borrowed pointer to the new file name.
    String* new_file;

    // Either max_archive_days or max_archive_files.
    int value;
};

static void log_info_delete_files_on_days_impl(FILE* file, String* current_line, char* attrib, void* ptr) {
    struct LogArchiveDeleteContext* ctx = ptr;
    time_t current_time = time(NULL);

    if(!current_line) {
        fprintf(file, "%s=%lld?%s\n", attrib, (int64_t)current_time, string_data(ctx->new_file));
        return;
    }

    size_t attrib_length = strlen(attrib) + 1;
    String value = string_create("");
    String fname = string_create("");
    String* files = NULL;
    bool processing = false;

    if(!string_append_string_part(&value, current_line, attrib_length, string_size(current_line) - attrib_length))
        goto end;

    String separator = string_create("|");
    size_t count;
    String* files = string_split(&value, &separator, NULL, STRING_SPLIT_ALLOCATE, &count, true, true);
    if(!files)
        goto end;

    string_clear(&value);

    processing = true;

    for(size_t i = 0; i < count; i++) {
        if(processing) {
            string_clear(&fname);
            int64_t arg;
            size_t index = string_find_cstr(files + i, 0, "?");
            if(index != SIZE_MAX) {
                if(string_append_string_part(&fname, files + i, 0, index)) {
                    if(sscanf(string_data(files + i) + index + 1, "%lld", &arg) == 1) {
                        time_t t = arg;
                        double diff = difftime(current_time, t);
                        if(diff >= 86400 * ctx->value)
                            remove(string_data(&fname));
                        else {
                            processing = string_append_string(&value, files + i) && string_append_cstr(&value, "|");
                        }
                    } else {
                        processing = false;
                    }
                } else {
                    processing = false;
                }
            }
        }

        string_free_resources(files + i);
    }

    end:
        if(processing)
            fputs(string_data(&value), file);
        else
            fputs(string_data(current_line), file);

        string_free_resources(&fname);
        string_free_resources(&value);
        free(files);
}

static void log_info_delete_files_on_count_impl(FILE* file, String* current_line, char* attrib, void* ptr) {
    struct LogArchiveDeleteContext* ctx = ptr;
    if(!current_line) {
        fprintf(file, "%s=%s\n", attrib, string_data(ctx->new_file));
        return;
    }

    size_t attrib_length = strlen(attrib) + 1;
    String value = string_create("");
    String fname = string_create("");
    String* files = NULL;
    bool processing = false;

    if(!string_append_string_part(&value, current_line, attrib_length, string_size(current_line) - attrib_length))
        goto end;

    String separator = string_create("|");
    size_t count;
    String* files = string_split(&value, &separator, NULL, STRING_SPLIT_ALLOCATE, &count, true, true);
    if(!files)
        goto end;

    string_clear(&value);
    processing = true;
    int i = 0;
    int files_to_delete = count - ctx->value;

    for(; i < files_to_delete; i++) {
        remove(string_data(files + i));
        string_free_resources(files + i);
    }

    string_clear(&value);
    bool processing = true;
    for(; i < count; i++) {
        if(processing) {
            processing = string_append_string(&value, files + i) && string_append_cstr(&value, "|");
        }
        string_free_resources(files + i);
    }

    end:
        if(processing)
            fputs(string_data(&value), file);
        else
            fputs(string_data(current_line), file);
        string_free_resources(&fname);
        string_free_resources(&value);
        free(files);
}

static bool log_info_delete_files_on_days(struct LogFile* file, struct LogArchiveDeleteContext* ctx) {
    return log_file_info_handle_attribute(file, "archives", ctx, log_info_delete_files_on_days_impl);
}

static bool log_info_delete_files_on_count(struct LogFile* file, struct LogArchiveDeleteContext* ctx) {
    return log_file_info_handle_attribute(file, "archives", ctx, log_info_delete_files_on_count_impl);
}

static void log_file_delete_old_archives(
    struct LogFileTargetContext* ctx, 
    struct LogFile* file,
    String* log_archive_name,
    String* archive_file_pattern,
    time_t t)
{
    if(ctx->max_archive_days > 0) {
#if defined(LOG_WINDOWS)
        WIN32_FIND_DATAA find_data;
        HANDLE handle = FindFirstFileA(string_data(archive_file_pattern), &find_data);
        if(handle != INVALID_HANDLE_VALUE) {
            do {
                time_t archive_time = log_file_time_to_time(&find_data.ftCreationTime);
                double time_diff = difftime(t, archive_time);
                if(time_diff >= 86400 * ctx->max_archive_days)
                    DeleteFileA(find_data.cFileName);
            }
            while(FindNextFileA(handle, &find_data));

            FindClose(handle);
            return;
        }
#elif defined(LOG_STATX)
        glob_t pattern;
        struct statx buffer;
        if(glob(string_data(&archive_file_pattern), 0, NULL, &pattern) == 0) {
            for(int i = 0; i < pattern.gl_pathc; i++) {
                if(statx(AT_FDCWD, pattern.gl_pathv[i], AT_STATX_SYNC_AS_STAT, STATX_BTIME, &buffer) == 0) {
                    double time_diff = difftime(t, (time_t)buffer.stx_btime.tv_sec);
                    if(time_diff >= 86400 * ctx->max_archive_days)
                        remove(pattern.gl_pathv[i]);
                }
            }
            globfree(&pattern);
            return;
        }
        globfree(&pattern);
#endif
        struct LogArchiveDeleteContext delete_ctx;
        delete_ctx.value = ctx->max_archive_days;
        delete_ctx.new_file = log_archive_name;
        log_info_delete_files_on_days(file, &delete_ctx);
    } else {
        // Todo: Optimize this using FindNextFile/glob.
        struct LogArchiveDeleteContext delete_ctx;
        delete_ctx.value = ctx->max_archive_files;
        delete_ctx.new_file = log_archive_name;
        log_info_delete_files_on_count(file, &delete_ctx);
    }
}

static void log_file_archive(
    struct LogFileTargetContext* ctx, 
    struct LogFile* file,
    enum LogLevel log_level,
    const char* calling_file,
    const char* function,
    uint32_t line,
    String* msg)
{
    String log_file_name = string_create("");
    if(!___log_format(ctx->archive_file_name, log_level, calling_file, function, line, &log_file_name, "%s", msg))
        return;

    String ext = string_create("");
    String archive_file_pattern = string_create("");
    String number = string_create("");
    String log_files = string_create("");
    bool has_ext = false;
    time_t t = time(NULL);
    bool result = false;

    switch(ctx->archive_numbering) {
        case FILE_ARCHIVE_NUMBER_NONE:
            remove(string_data(&log_file_name));
            result = true;
            break;
        case FILE_ARCHIVE_NUMBER_SEQUENCE:
            has_ext = string_strip_extension(&log_file_name, &ext);

            if (!string_append_string(&archive_file_pattern, &log_file_name) ||
                !string_append_cstr(&archive_file_pattern, "*"))
            {
                break;
            } else if(has_ext && !string_append_string(&archive_file_pattern, &ext)) {
                break;
            }
            
            if(!string_format_cstr(&number, "%d", file->sequence))
                break;

            if(!string_format_cstr(&log_file_name, ".%s", string_data(&number)))
                break;
            
            if(has_ext && !string_append_string(&log_file_name, &ext))
                break;

            log_file_info_write_attribute(file, "sequence", &number, false);
            if(ctx->max_archive_files != 0 || ctx->max_archive_days != 0)
            {
                t = time(NULL);
                if(!string_format_cstr(
                    &log_files,
                    "%s?%lld|",
                    string_data(&log_file_name),
                    (int64_t)t))
                {
                    break;
                }

                log_file_info_write_attribute(file, "log_files", &log_files, true);
            }
            result = true;
            break;
        case FILE_ARCHIVE_NUMBER_DATE:
            char datetime[256];
            struct tm time_value = *localtime(&t);
            size_t count = strftime(datetime, 256, string_date(&ctx->archive_date_format), &time_value);
            if(count == 0)
                break;

            has_ext = string_strip_extension(&log_file_name, &ext);
            if( !string_append_cstr(&log_file_name, ".") || 
                !string_append_cstr_part(&log_file_name, datetime, 0, count)) 
            {
                break;
            }

            if(has_ext && !string_append_string(&log_file_name, &ext))
                break;

            result = true;
            break;
    }

    if(result) {
        bool was_open = file->file != NULL;
        if(was_open)
            fclose(file->file);

        rename(string_data(&file->name), string_data(&log_file_name));

        if(was_open) {
            file->file = fopen(string_data(&file->name), "w+");
            if(!file->file)
                goto end;
        }

        if(ctx->archive_timing == FILE_ARCHIVE_NUMBER_SEQUENCE)
            file->sequence++;

        if(ctx->archive_numbering != FILE_ARCHIVE_NUMBER_NONE)
            log_file_delete_old_archives(ctx, file, &log_file_name, &archive_file_pattern, t);
    }

    end:
    string_free_resources(&log_file_name);
    string_free_resources(&ext);
    string_free_resources(&number);
    string_free_resources(&archive_file_pattern);
}

static bool log_file_day_passed(struct tm* creation_time, struct tm* current_time, int day) {
    int last_day = creation_time->tm_yday - creation_time->tm_wday + day;

    // Make sure to account for leap year.
    int max_days = 365;
    if(creation_time->tm_year % 4 == 0) {
        if(creation_time->tm_year % 100 == 0) {
            if(creation_time->tm_year % 400 != 0) {
                max_days = 366;
            }
        } else {
            max_days = 366;
        }
    }

    if(last_day >= max_days - 7) {
        return current_time->tm_yday >= (7 - (max_days - last_day));
    } else if(current_time->tm_year > creation_time->tm_year) {
        return current_time->tm_yday >= last_day + 7;
    }

    return false;
}

static void log_file_archive_if_needed(
    struct LogFileTargetContext* ctx, 
    struct LogFile* file,
    enum LogLevel log_level,
    const char* calling_file,
    const char* function,
    uint32_t line,
    String* msg) 
{
    if(ctx->archive_timing == FILE_ARCHIVE_NONE)
        return;

    if(ctx->archive_timing == FILE_ARCHIVE_SIZE) {
        size_t fsize = log_file_size(file);
        if(fsize >= ctx->archive_above_size) {

        }
    } else {
        time_t current_time = time(NULL);
        struct tm* datetime = localtime(&current_time);
        time_t file_time;
        double difference;
        switch(ctx->archive_timing) {
            case FILE_ARCHIVE_DAY:
                time_t file_time = mktime(&file->creation_time);
                double difference = difftime(current_time, file_time);
                if(difference >= 86400)
                    log_file_archive(ctx, file, log_level, calling_file, function, line, msg);
                break;
            case FILE_ARCHIVE_HOUR:
                time_t file_time = mktime(&file->creation_time);
                double difference = difftime(current_time, file_time);
                if(difference >= 3600)
                    log_file_archive(ctx, file, log_level, calling_file, function, line, msg);
                break;
            case FILE_ARCHIVE_MINUTE:
                time_t file_time = mktime(&file->creation_time);
                double difference = difftime(current_time, file_time);
                if(difference >= 60)
                    log_file_archive(ctx, file, log_level, calling_file, function, line, msg);
                break;
            case FILE_ARCHIVE_MONTH:
                if (datetime->tm_mon > file->creation_time.tm_mon ||
                    datetime->tm_year > file->creation_time.tm_year)
                    {
                        log_file_archive(ctx, file, log_level, calling_file, function, line, msg);
                    }
                    break;
            case FILE_ARCHIVE_YEAR:
                if(datetime->tm_year > file->creation_time.tm_year)
                    log_file_archive(ctx, file, log_level, calling_file, function, line, msg);
                break;
            case FILE_ARCHIVE_SUNDAY:
                if(log_file_day_passed(&file->creation_time, datetime, 0))
                    log_file_archive(ctx, file, log_level, calling_file, function, line, msg);
                break;
            case FILE_ARCHIVE_MONDAY:
                if(log_file_day_passed(&file->creation_time, datetime, 1))
                    log_file_archive(ctx, file, log_level, calling_file, function, line, msg);
                break;
            case FILE_ARCHIVE_TUESDAY:
                if(log_file_day_passed(&file->creation_time, datetime, 2))
                    log_file_archive(ctx, file, log_level, calling_file, function, line, msg);
                break;
            case FILE_ARCHIVE_WEDNESDAY:
                if(log_file_day_passed(&file->creation_time, datetime, 3))
                    log_file_archive(ctx, file, log_level, calling_file, function, line, msg);
                break;
            case FILE_ARCHIVE_THURSDAY:
                if(log_file_day_passed(&file->creation_time, datetime, 4))
                    log_file_archive(ctx, file, log_level, calling_file, function, line, msg);
                break;
            case FILE_ARCHIVE_FRIDAY:
                if(log_file_day_passed(&file->creation_time, datetime, 5))
                    log_file_archive(ctx, file, log_level, calling_file, function, line, msg);
                break;
            case FILE_ARCHIVE_SATURDAY:
                if(log_file_day_passed(&file->creation_time, datetime, 6))
                    log_file_archive(ctx, file, log_level, calling_file, function, line, msg);
                break;
        }
    }
}

static void log_file_log(enum LogLevel log_level, const char* file, const char* function, uint32_t line, String* msg, void* ptr) {
    struct LogFileTargetContext* ctx = ptr;
    String fname = string_create("");

    if(!___log_format(ctx->file_name, log_level, file, function, line, &fname, "%s", string_data(msg)))
        return;

    struct LogFile* log_file = log_file_open(ctx, &fname);
    if(!log_file) {
        string_free_resources(&fname);
        return;
    }

    log_file_archive_if_needed(ctx, log_file, log_level, file, function, line, msg);

    fputs(string_data(msg), log_file->file);

    if(!ctx->keep_files_open)
        fclose(log_file->file);
}

LogTarget* log_target_file_create(const char* layout, enum LogLevel min_level, enum LogLevel max_level, struct LogFileTargetContext* ctx) {
    LogTarget* target = malloc(sizeof(*target));
    if(!target)
        return NULL;

    struct LogFormat* fmt = ___log_parse_format(layout, 0, strlen(layout));
    if(!fmt) {
        free(target);
        return NULL;
    }

    target->format = fmt;
    target->free = log_file_target_context_free;
    target->ctx = ctx;
    target->log = log_file_log;
    target->min_level = min_level;
    target->max_level = max_level;

    return target;
}