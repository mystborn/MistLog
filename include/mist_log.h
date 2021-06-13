#ifndef MIST_LOG_MIST_LOG_H
#define MIST_LOG_MIST_LOG_H

#include <stdarg.h>
#include <sso_string.h>

enum LogLevel {
    LOG_TRACE,
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
    LOG_FATAL
};

enum FileArchiveNumbering {
    FILE_ARCHIVE_SEQUENCE,
    FILE_ARCHIVE_DATE
};

enum FileArchiveTiming {
    FILE_ARCHIVE_NONE,
    FILE_ARCHIVE_SIZE,
    FILE_ARCHIVE_DAY,
    FILE_ARCHIVE_HOUR,
    FILE_ARCHIVE_MINUTE,
    FILE_ARCHIVE_MONTH,
    FILE_ARCHIVE_YEAR,
    FILE_ARCHIVE_SUNDAY,
    FILE_ARCHIVE_MONDAY,
    FILE_ARCHIVE_TUESDAY,
    FILE_ARCHIVE_WEDNESDAY,
    FILE_ARCHIVE_THURSDAY,
    FILE_ARCHIVE_FRIDAY,
    FILE_ARCHIVE_SATURDAY
};

/**
 * Renders a layout to a log message.
 */
struct LogLayoutRenderer {
    /**
     * A generic context value that can be used to store information about the LogLayoutRenderer.
     */
    void *ctx;

    /**
     * A method used to append a string determined by the LogLayoutRenderer to the output messasge.
     */
    bool (*append)(enum LogLevel log_level, const char* file, const char* function, uint32_t line, String* message, void *ctx, char* format, va_list args);

    /**
     * A method used to destroy the context value if needed.
     */ 
    void (*free)(void* ctx);
};

/**
 * Describes the format of a log message.
 * 
 * @see ___log_format()
 */
struct LogFormat {
    /**
     * An array of the layout renderers that are used to build a log message.
     */
    struct LogLayoutRenderer **steps;

    /**
     * The number of steps used to build a log message.
     */
    int step_count;
};

/**
 * An output target for log messages (i.e. console, file, etc).
 */
typedef struct LogTarget {
    /**
     * The format of the log message produced by this target.
     */
    struct LogFormat* format;

    /**
     * The method used by this target to actually log the log message.
     */
    void (*log)(String* msg, void* ctx);

    /**
     * A method that can optionally free the context of this target if needed.
     */
    void (*free)(void* ctx);

    /**
     * A generic context value that can be used to store information about the log target if needed.
     */
    void* ctx;

    /**
     * The minimum log level needed to log a message to this target.
     */
    enum LogLevel min_level;

    /**
     * The maximum log level allowed to log a message to this target.
     */
    enum LogLevel max_level;
} LogTarget;

/**
 * Used to log formatted messages to various log targets.
 */
typedef struct Logger {
    /**
     * An array of targets that log messages are sent to.
     */
    struct LogTarget** targets;

    /**
     * A mutex that can be used to make the logger thread-safe. Optional.
     */
    void* mutex;

    /**
     * A user-defined method to lock/unlock the mutex.
     */
    void (*lock)(void* mtx, bool lock);

    /**
     * The number of items in the targets array.
     */
    int target_count;

    /**
     * The maximum number of items allowed in the targets array before it has to resize.
     */
    int target_capacity;
} Logger;

/**
 * Creates and initializes a new Logger.
 */
Logger* log_logger_create();

/**
 * Frees all the resources used by a logger, than frees the logger.
 */
void log_logger_free(Logger* logger);

/**
 * Creates a log target that outputs to the console.
 * 
 * @param layout The layout format of the log message output to the console.
 * @param min_level The minimum level of log messages to allow to this target.
 * @param max_level The maximum level of log messages to allow to this target.
 */
LogTarget* log_target_console_create(const char* layout, enum LogLevel min_level, enum LogLevel max_level);

/**
 * Frees the resources used by a log target, than frees the log target.
 */
void log_target_free(LogTarget* target);

/**
 * Adds a log target to a logger.
 */
bool log_add_target(Logger* logger, LogTarget* target);

/**
 * Sets the lock method and mutex value used by a logger. If either is NULL, the logger will never lock and will not be thread-safe.
 */
void log_set_lock(Logger* logger, void* mutex, void (*lock)(void* mtx, bool lock));

/**
 * Registers a custom LogLayoutRenderer.
 * 
 * @param name The name of the layout renderer used inside of layout formats to be used to create the LogFormatRenderer (e.g. The "level" part of "${level}").
 * @param create A function that will create a LogLayoutRenderer. Accepts a string section that contains any potential arguments in the layout format.
 *               Accepts the context value used when calling this function.
 * @param ctx The context passed to the create function.
 * @param free A function that can optionally free the context value passed to this function.
 * 
 * @remarks The arguments passed to the create function can be easily parsed using ___log_format_read_arg_name and ___log_format_read_arg_value.
 */
bool ___log_register_log_format_creator(const char* name, struct LogLayoutRenderer* (*create)(char* text, size_t start, size_t count, void* ctx), void* ctx, void (*free)(void*));

/**
 * Converts a layout format string into a LogFormat value.
 * 
 * @remarks This function is mostly meant to be used by custom LogTarget constructors.
 */
struct LogFormat* ___log_parse_format(char* format, size_t start, size_t count);

/**
 * Reads the name of the next argument in the argument list of a layout renderer format string.
 */
bool ___log_format_read_arg_name(char* text, size_t* start, size_t count, String* name);

/**
 * Reads the value of an argument in the argument list of a layout renderer format string. Can either append the result to a string or return a nested LogFormat value.
 */
bool ___log_format_read_arg_value(char* text, size_t* start, size_t count, bool as_format, String* value, struct LogFormat** format);

/**
 * Given a LogFormat, converts a raw log message into a formatted log message.
 * 
 * @remarks Mostly intended to make testing custom LogLayoutRenderers easier.
 */
bool ___log_format(struct LogFormat* log_format, enum LogLevel level, const char* file, const char* function, uint32_t line, String* message, char* format_string, va_list args);

/**
 * Logs a string value using the specified logger. Does not support logging the calling function name.
 */
bool ___log_string(Logger* logger, enum LogLevel log_level, const char* file, int line, const String* message, ...);

/**
 * Logs a string value using the specified logger.
 */
bool ___log_func_string(Logger* logger, enum LogLevel log_level, const char* file, const char* function, int line, const String* message, ...);

/**
 * Logs a c-string using the specified logger. Does not support logging the calling function name.
 */
bool ___log_cstr(Logger* logger, enum LogLevel log_level, const char* file, int line, const char* message, ...);

/**
 * Logs a c-string using the specified logger.
 */
bool ___log_func_cstr(Logger* logger, enum LogLevel log_level, const char* file, const char* function, int line, const char* message, ...);


#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L

#if __STDC_VERSION__ >= 201112L

// If _Generic is available, prefer that over just using c-strings for the default
// log message call.

#define ___log_generic(logger, level, message, ...) \
    _Generic(message, \
        char*: ___log_func_cstr, \
        const char*: ___log_func_cstr, \
        String*: ___log_func_string, \
        const String*: ___log_func_string)((logger), (level), __FILE__, __func__, __LINE__, message, ## __VA_ARGS__)

#define log_trace(logger, message, ...) ___log_generic(logger, LOG_TRACE, message, __VA_ARGS__)
#define log_debug(logger, message, ...) ___log_generic(logger, LOG_DEBUG, message, __VA_ARGS__)
#define log_info(logger, message, ...) ___log_generic(logger, LOG_INFO, message, __VA_ARGS__)
#define log_warn(logger, message, ...) ___log_generic(logger, LOG_WARN, message, __VA_ARGS__)
#define log_error(logger, message, ...) ___log_generic(logger, LOG_ERROR, message, __VA_ARGS__)
#define log_fatal(logger, message, ...) ___log_generic(logger, LOG_FATAL, message, __VA_ARGS__)
    

#else // __STDC_VERSION__ >= 201112L

#define log_trace(logger, ...) ___log_func_cstr((logger), LOG_TRACE, __FILE__, __func__, __LINE__, __VA_ARGS__)
#define log_debug(logger, ...) ___log_func_cstr((logger), LOG_DEBUG, __FILE__, __func__, __LINE__, __VA_ARGS__)
#define log_info(logger, ...) ___log_func_cstr((logger), LOG_INFO, __FILE__, __func__, __LINE__, __VA_ARGS__)
#define log_warn(logger, ...) ___log_func_cstr((logger), LOG_WARN, __FILE__, __func__, __LINE__, __VA_ARGS__)
#define log_error(logger, ...) ___log_func_cstr((logger), LOG_ERROR, __FILE__, __func__, __LINE__, __VA_ARGS__)
#define log_fatal(logger, ...) ___log_func_cstr((logger), LOG_FATAL, __FILE__, __func__, __LINE__, __VA_ARGS__)

#endif

#define log_cstr_trace(logger, ...) ___log_func_cstr((logger), LOG_TRACE, __FILE__, __func__, __LINE__, __VA_ARGS__)
#define log_cstr_debug(logger, ...) ___log_func_cstr((logger), LOG_DEBUG, __FILE__, __func__, __LINE__, __VA_ARGS__)
#define log_cstr_info(logger, ...) ___log_func_cstr((logger), LOG_INFO, __FILE__, __func__, __LINE__, __VA_ARGS__)
#define log_cstr_warn(logger, ...) ___log_func_cstr((logger), LOG_WARN, __FILE__, __func__, __LINE__, __VA_ARGS__)
#define log_cstr_error(logger, ...) ___log_func_cstr((logger), LOG_ERROR, __FILE__, __func__, __LINE__, __VA_ARGS__)
#define log_cstr_fatal(logger, ...) ___log_func_cstr((logger), LOG_FATAL, __FILE__, __func__, __LINE__, __VA_ARGS__)

#define log_string_trace(logger, ...) ___log_func_string((logger), LOG_TRACE, __FILE__, __func__, __LINE__, __VA_ARGS__)
#define log_string_debug(logger, ...) ___log_func_string((logger), LOG_DEBUG, __FILE__, __func__, __LINE__, __VA_ARGS__)
#define log_string_info(logger, ...) ___log_func_string((logger), LOG_INFO, __FILE__, __func__, __LINE__, __VA_ARGS__)
#define log_string_warn(logger, ...) ___log_func_string((logger), LOG_WARN, __FILE__, __func__, __LINE__, __VA_ARGS__)
#define log_string_error(logger, ...) ___log_func_string((logger), LOG_ERROR, __FILE__, __func__, __LINE__, __VA_ARGS__)
#define log_string_fatal(logger, ...) ___log_func_string((logger), LOG_FATAL, __FILE__, __func__, __LINE__, __VA_ARGS__)

#else // __STDC_VERSION__ >= 199901L

#define log_trace(logger, ...) ___log_cstr((logger), LOG_TRACE, __FILE__, __LINE__, __VA_ARGS__)
#define log_debug(logger, ...) ___log_cstr((logger), LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define log_info(logger, ...) ___log_cstr((logger), LOG_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define log_warn(logger, ...) ___log_cstr((logger), LOG_WARN, __FILE__, __LINE__, __VA_ARGS__)
#define log_error(logger, ...) ___log_cstr((logger), LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define log_fatal(logger, ...) ___log_cstr((logger), LOG_FATAL, __FILE__, __LINE__, __VA_ARGS__)

#define log_cstr_trace(logger, ...) ___log_cstr((logger), LOG_TRACE, __FILE__, __LINE__, __VA_ARGS__)
#define log_cstr_debug(logger, ...) ___log_cstr((logger), LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define log_cstr_info(logger, ...) ___log_cstr((logger), LOG_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define log_cstr_warn(logger, ...) ___log_cstr((logger), LOG_WARN, __FILE__, __LINE__, __VA_ARGS__)
#define log_cstr_error(logger, ...) ___log_cstr((logger), LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define log_cstr_fatal(logger, ...) ___log_cstr((logger), LOG_FATAL, __FILE__, __LINE__, __VA_ARGS__)

#define log_string_trace(logger, ...) ___log_string((logger), LOG_TRACE, __FILE__, __LINE__, __VA_ARGS__)
#define log_string_debug(logger, ...) ___log_string((logger), LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define log_string_info(logger, ...) ___log_string((logger), LOG_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define log_string_warn(logger, ...) ___log_string((logger), LOG_WARN, __FILE__, __LINE__, __VA_ARGS__)
#define log_string_error(logger, ...) ___log_string((logger), LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define log_string_fatal(logger, ...) ___log_string((logger), LOG_FATAL, __FILE__, __LINE__, __VA_ARGS__)

#endif

#endif