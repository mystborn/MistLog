#ifndef MIST_LOG_MIST_LOG_H
#define MIST_LOG_MIST_LOG_H

#include <stdarg.h>
#include <sso_string.h>

#ifdef MIST_LOG_BUILD
    #if defined(_WIN32)
        #define LOG_EXPORT __declspec(dllexport)
    #elif defined(__ELF__)
        #define LOG_EXPORT __attribute__((visibility ("default")))
    #else
        #define LOG_EXPORT
    #endif
#else
    #if defined(_WIN32)
        #define LOG_EXPORT __declspec(dllimport)
    #else
        #define LOG_EXPORT
    #endif
#endif

enum LogLevel {
    LOG_TRACE,
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
    LOG_FATAL
};

enum FileArchiveNumbering {
    FILE_ARCHIVE_NUMBER_NONE,
    FILE_ARCHIVE_NUMBER_SEQUENCE,
    FILE_ARCHIVE_NUMBER_DATE
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
    void (*log)(enum LogLevel log_level, const char* file, const char* function, uint32_t line, String* msg, void* ctx);

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

struct LogFileTargetContext;

/**
 * Creates and initializes a new Logger.
 */
LOG_EXPORT Logger* log_logger_create();

/**
 * Frees all the resources used by a logger, than frees the logger.
 */
LOG_EXPORT void log_logger_free(Logger* logger);

/**
 * Frees the resources used by a log target, than frees the log target.
 */
LOG_EXPORT void log_target_free(LogTarget* target);

/**
 * Adds a log target to a logger.
 */
LOG_EXPORT bool log_add_target(Logger* logger, LogTarget* target);

/**
 * Sets the lock method and mutex value used by a logger. If either is NULL, the logger will never lock and will not be thread-safe.
 */
LOG_EXPORT void log_set_lock(Logger* logger, void* mutex, void (*lock)(void* mtx, bool lock));

/**
 * Creates a log target that outputs to the console.
 * 
 * @param layout The layout format of the log message output to the console.
 * @param min_level The minimum level of log messages to allow to this target.
 * @param max_level The maximum level of log messages to allow to this target.
 */
LOG_EXPORT LogTarget* log_target_console_create(const char* layout, enum LogLevel min_level, enum LogLevel max_level);

LOG_EXPORT struct LogFileTargetContext* log_file_target_context_create(char* fname);

LOG_EXPORT void log_file_target_context_free(struct LogFileTargetContext* ctx);

LOG_EXPORT bool log_file_target_context_archive_fname(struct LogFileTargetContext* ctx, char* archive_fname);

LOG_EXPORT bool log_file_target_context_set_buffering(struct LogFileTargetContext* ctx, size_t size, int mode);

LOG_EXPORT void log_file_target_context_set_max_archive_files(struct LogFileTargetContext* ctx, int max_file_count);

LOG_EXPORT void log_file_target_context_set_max_archive_days(struct LogFileTargetContext* ctx, int max_file_days);

LOG_EXPORT void log_file_target_context_archive_on_size(struct LogFileTargetContext* ctx, size_t max_size);

LOG_EXPORT void log_file_target_archive_on_date(struct LogFileTargetContext* ctx, enum FileArchiveTiming timing);

LOG_EXPORT void log_file_target_context_archive_number_sequence(struct LogFileTargetContext* ctx);

LOG_EXPORT bool log_file_target_context_archive_number_date(struct LogFileTargetContext* ctx, enum FileArchiveNumbering numbering, char* date_string);

LOG_EXPORT bool log_file_target_context_keep_files_open(struct LogFileTargetContext* ctx);

LOG_EXPORT LogTarget* log_target_file_create(const char* layout, enum LogLevel min_level, enum LogLevel max_level, struct LogFileTargetContext* ctx);

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
LOG_EXPORT bool mist_log_register_log_format_creator(const char* name, struct LogLayoutRenderer* (*create)(char* text, size_t start, size_t count, void* ctx), void* ctx, void (*free)(void*));

/**
 * Converts a layout format string into a LogFormat value.
 * 
 * @remarks This function is mostly meant to be used by custom LogTarget constructors.
 */
LOG_EXPORT struct LogFormat* mist_log_parse_format(char* format, size_t start, size_t count);

/**
 * Frees a LogFormat value.
 */
LOG_EXPORT void mist_log_format_free(struct LogFormat* format);

/**
 * Reads the name of the next argument in the argument list of a layout renderer format string.
 */
LOG_EXPORT bool mist_log_format_read_arg_name(char* text, size_t* start, size_t count, String* name);

/**
 * Reads the value of an argument in the argument list of a layout renderer format string. Can either append the result to a string or return a nested LogFormat value.
 */
LOG_EXPORT bool mist_log_format_read_arg_value(char* text, size_t* start, size_t count, bool as_format, String* value, struct LogFormat** format);

/**
 * Given a LogFormat, converts a raw log message into a formatted log message.
 * 
 * @remarks Mostly intended to make testing custom LogLayoutRenderers easier.
 */
LOG_EXPORT bool mist_log_format(struct LogFormat* log_format, enum LogLevel level, const char* file, const char* function, uint32_t line, String* message, char* format_string, va_list args);

/**
 * Logs a string value using the specified logger. Does not support logging the calling function name.
 */
LOG_EXPORT bool mist_log_string(Logger* logger, enum LogLevel log_level, const char* file, int line, const String* message, ...);

/**
 * Logs a string value using the specified logger.
 */
LOG_EXPORT bool mist_log_func_string(Logger* logger, enum LogLevel log_level, const char* file, const char* function, int line, const String* message, ...);

/**
 * Logs a c-string using the specified logger. Does not support logging the calling function name.
 */
LOG_EXPORT bool mist_log_cstr(Logger* logger, enum LogLevel log_level, const char* file, int line, const char* message, ...);

/**
 * Logs a c-string using the specified logger.
 */
LOG_EXPORT bool mist_log_func_cstr(Logger* logger, enum LogLevel log_level, const char* file, const char* function, int line, const char* message, ...);


#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L

#if __STDC_VERSION__ >= 201112L

// If _Generic is available, prefer that over just using c-strings for the default
// log message call.

#define mist_log_generic(logger, level, message, ...) \
    _Generic(message, \
        char*: mist_log_func_cstr, \
        const char*: mist_log_func_cstr, \
        String*: mist_log_func_string, \
        const String*: mist_log_func_string)((logger), (level), __FILE__, __func__, __LINE__, message, ## __VA_ARGS__)

#define log_trace(logger, message, ...) mist_log_generic(logger, LOG_TRACE, message, __VA_ARGS__)
#define log_debug(logger, message, ...) mist_log_generic(logger, LOG_DEBUG, message, __VA_ARGS__)
#define log_info(logger, message, ...) mist_log_generic(logger, LOG_INFO, message, __VA_ARGS__)
#define log_warn(logger, message, ...) mist_log_generic(logger, LOG_WARN, message, __VA_ARGS__)
#define log_error(logger, message, ...) mist_log_generic(logger, LOG_ERROR, message, __VA_ARGS__)
#define log_fatal(logger, message, ...) mist_log_generic(logger, LOG_FATAL, message, __VA_ARGS__)
    

#else // __STDC_VERSION__ >= 201112L

#define log_trace(logger, ...) mist_log_func_cstr((logger), LOG_TRACE, __FILE__, __func__, __LINE__, __VA_ARGS__)
#define log_debug(logger, ...) mist_log_func_cstr((logger), LOG_DEBUG, __FILE__, __func__, __LINE__, __VA_ARGS__)
#define log_info(logger, ...) mist_log_func_cstr((logger), LOG_INFO, __FILE__, __func__, __LINE__, __VA_ARGS__)
#define log_warn(logger, ...) mist_log_func_cstr((logger), LOG_WARN, __FILE__, __func__, __LINE__, __VA_ARGS__)
#define log_error(logger, ...) mist_log_func_cstr((logger), LOG_ERROR, __FILE__, __func__, __LINE__, __VA_ARGS__)
#define log_fatal(logger, ...) mist_log_func_cstr((logger), LOG_FATAL, __FILE__, __func__, __LINE__, __VA_ARGS__)

#endif

#define log_cstr_trace(logger, ...) mist_log_func_cstr((logger), LOG_TRACE, __FILE__, __func__, __LINE__, __VA_ARGS__)
#define log_cstr_debug(logger, ...) mist_log_func_cstr((logger), LOG_DEBUG, __FILE__, __func__, __LINE__, __VA_ARGS__)
#define log_cstr_info(logger, ...) mist_log_func_cstr((logger), LOG_INFO, __FILE__, __func__, __LINE__, __VA_ARGS__)
#define log_cstr_warn(logger, ...) mist_log_func_cstr((logger), LOG_WARN, __FILE__, __func__, __LINE__, __VA_ARGS__)
#define log_cstr_error(logger, ...) mist_log_func_cstr((logger), LOG_ERROR, __FILE__, __func__, __LINE__, __VA_ARGS__)
#define log_cstr_fatal(logger, ...) mist_log_func_cstr((logger), LOG_FATAL, __FILE__, __func__, __LINE__, __VA_ARGS__)

#define log_string_trace(logger, ...) mist_log_func_string((logger), LOG_TRACE, __FILE__, __func__, __LINE__, __VA_ARGS__)
#define log_string_debug(logger, ...) mist_log_func_string((logger), LOG_DEBUG, __FILE__, __func__, __LINE__, __VA_ARGS__)
#define log_string_info(logger, ...) mist_log_func_string((logger), LOG_INFO, __FILE__, __func__, __LINE__, __VA_ARGS__)
#define log_string_warn(logger, ...) mist_log_func_string((logger), LOG_WARN, __FILE__, __func__, __LINE__, __VA_ARGS__)
#define log_string_error(logger, ...) mist_log_func_string((logger), LOG_ERROR, __FILE__, __func__, __LINE__, __VA_ARGS__)
#define log_string_fatal(logger, ...) mist_log_func_string((logger), LOG_FATAL, __FILE__, __func__, __LINE__, __VA_ARGS__)

#else // __STDC_VERSION__ >= 199901L

#define log_trace(logger, ...) mist_log_cstr((logger), LOG_TRACE, __FILE__, __LINE__, __VA_ARGS__)
#define log_debug(logger, ...) mist_log_cstr((logger), LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define log_info(logger, ...) mist_log_cstr((logger), LOG_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define log_warn(logger, ...) mist_log_cstr((logger), LOG_WARN, __FILE__, __LINE__, __VA_ARGS__)
#define log_error(logger, ...) mist_log_cstr((logger), LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define log_fatal(logger, ...) mist_log_cstr((logger), LOG_FATAL, __FILE__, __LINE__, __VA_ARGS__)

#define log_cstr_trace(logger, ...) mist_log_cstr((logger), LOG_TRACE, __FILE__, __LINE__, __VA_ARGS__)
#define log_cstr_debug(logger, ...) mist_log_cstr((logger), LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define log_cstr_info(logger, ...) mist_log_cstr((logger), LOG_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define log_cstr_warn(logger, ...) mist_log_cstr((logger), LOG_WARN, __FILE__, __LINE__, __VA_ARGS__)
#define log_cstr_error(logger, ...) mist_log_cstr((logger), LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define log_cstr_fatal(logger, ...) mist_log_cstr((logger), LOG_FATAL, __FILE__, __LINE__, __VA_ARGS__)

#define log_string_trace(logger, ...) mist_log_string((logger), LOG_TRACE, __FILE__, __LINE__, __VA_ARGS__)
#define log_string_debug(logger, ...) mist_log_string((logger), LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define log_string_info(logger, ...) mist_log_string((logger), LOG_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define log_string_warn(logger, ...) mist_log_string((logger), LOG_WARN, __FILE__, __LINE__, __VA_ARGS__)
#define log_string_error(logger, ...) mist_log_string((logger), LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define log_string_fatal(logger, ...) mist_log_string((logger), LOG_FATAL, __FILE__, __LINE__, __VA_ARGS__)

#endif

#endif