#include "../include/mist_log.h"

int main(void) {

    const char* format = "${time:format=%x %r} | ${level} | ${message}";
    Logger* logger = NULL;
    LogTarget* console = NULL;

    logger = log_logger_create();
    if(!logger) {
        return EXIT_FAILURE;
    }

    console = log_target_console_create(format, LOG_TRACE, LOG_FATAL);
    if(!console) {
        log_logger_free(logger);
        return EXIT_FAILURE;
    }

    if(!log_add_target(logger, console)) {
        log_logger_free(logger);
        log_target_free(console);
        return EXIT_FAILURE;
    }

    log_debug(logger, "This is a simple debug message.");
    log_info(logger, "This log has %d arguments: %s", 2, "Hello MistLog");

    log_logger_free(logger);

    return EXIT_SUCCESS;
}