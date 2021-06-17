#include "../include/mist_log.h"

int main(void) {
    puts("Starting");

    const char* format = "${time:format=%X} | ${level} | ${message}";
    Logger* logger = NULL;
    LogTarget* console = NULL;
    LogTarget* file = NULL;
    struct LogFileTargetContext* file_ctx = NULL;

    logger = log_logger_create();
    if(!logger) {
        return EXIT_FAILURE;
    }

    console = log_target_console_create(format, LOG_TRACE, LOG_FATAL);
    if(!console) {
        log_logger_free(logger);
        return EXIT_FAILURE;
    }

    file_ctx = log_file_target_context_create("examples.log");
    if(!file_ctx) {
        log_logger_free(logger);
        log_target_free(console);
        return EXIT_FAILURE;
    }

    file = log_target_file_create(format, LOG_TRACE, LOG_FATAL, file_ctx);
    if(!file) {
        log_logger_free(logger);
        log_target_free(console);
        log_file_target_context_free(file_ctx);
        return EXIT_FAILURE;
    }

    if(!log_add_target(logger, console)) {
        log_logger_free(logger);
        log_target_free(console);
        log_target_free(file);
        return EXIT_FAILURE;
    }

    if(!log_add_target(logger, file)) {
        log_logger_free(logger);
        log_target_free(file);
        return EXIT_FAILURE;
    }

    log_debug(logger, "This is a simple debug message.");
    log_info(logger, "This is a formatted message: %d", 152);

    // log_logger_free(logger);

    puts("Finished");

    return EXIT_SUCCESS;
}