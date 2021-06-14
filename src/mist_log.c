#include <mist_log.h>

#include <time.h>

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

static struct LogLayoutRendererFinder log_renderer_finder = { NULL, 0, 0 };

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

static bool log_format_counter(enum LogLevel log_level, const char* file, const char* function, uint32_t line, String* message, void *ctx, char* format, va_list args) {
    return log_message_append_uint(message, ++(*(uint32_t*)ctx));
}

static void log_format_counter_free(void* ctx) {
    free(ctx);
}

static bool log_format_file(enum LogLevel log_level, const char* file, const char* function, uint32_t line, String* message, void *ctx, char* format, va_list args) {
    return string_append_cstr(message, file);
}

static bool log_format_function(enum LogLevel log_level, const char* file, const char* function, uint32_t line, String* message, void *ctx, char* format, va_list args) {
    return string_append_cstr(message, function);
}

static bool log_format_line(enum LogLevel log_level, const char* file, const char* function, uint32_t line, String* message, void *ctx, char* format, va_list args) {
    return log_message_append_uint(message, line);
}

static bool log_format_message(enum LogLevel log_level, const char* file, const char* function, uint32_t line, String* message, void *ctx, char* format, va_list args) {
    va_list copy;
    va_copy(copy, args);

    String* result = string_format_args_cstr(message, format, args);

    va_end(copy);

    return result != NULL;
}

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

static void ___log_console_log(enum LogLevel log_level, const char* file, const char* function, uint32_t line, String* msg, void* ctx) {
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
    target->log = ___log_console_log;
    target->min_level = min_level;
    target->max_level = max_level;

    return target;
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


static bool ___log_log_impl(Logger* logger, enum LogLevel log_level, const char* file, const char* function, int line, const char* message, va_list args) {
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

    bool result = ___log_log_impl(logger, log_level, file, "", line, string_data(message), args);

    va_end(args);

    return result;
}

LOG_EXPORT bool ___log_func_string(Logger* logger, enum LogLevel log_level, const char* file, const char* function, int line, const String* message, ...) {
    va_list args;
    va_start(args, message);

    bool result = ___log_log_impl(logger, log_level, file, function, line, string_data(message), args);

    va_end(args);

    return result;
}

LOG_EXPORT bool ___log_cstr(Logger* logger, enum LogLevel log_level, const char* file, int line, const char* message, ...) {
    va_list args;
    va_start(args, message);

    bool result = ___log_log_impl(logger, log_level, file, "", line, message, args);

    va_end(args);

    return result;
}

LOG_EXPORT bool ___log_func_cstr(Logger* logger, enum LogLevel log_level, const char* file, const char* function, int line, const char* message, ...) {
    va_list args;
    va_start(args, message);

    bool result = ___log_log_impl(logger, log_level, file, function, line, message, args);

    va_end(args);

    return result;
}