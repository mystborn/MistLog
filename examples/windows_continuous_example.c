#include "../include/mist_log.h"

#include <process.h>
#include <Windows.h>

bool running = true;

static void log_periodically(void* ctx) {
    Logger* logger = ctx;
    while(running) {
        log_info(logger, "Running...");
        Sleep(5000);
    }
}

int main(void) {

    const char* format = "${time:format=%x %r} | ${level} | ${message}";
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

    log_file_target_context_archive_fname(file_ctx, "examples.archive.log");
    log_file_target_archive_on_date(file_ctx, FILE_ARCHIVE_MINUTE);
    log_file_target_context_archive_number_sequence(file_ctx);
    log_file_target_context_set_max_archive_files(file_ctx, 2);

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

    uintptr_t handle = _beginthread(log_periodically, 0, logger);

    puts("Press any key to exit...");
    getchar();
    running = false;

    log_logger_free(logger);

    return EXIT_SUCCESS;
}