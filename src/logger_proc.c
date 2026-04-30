/*
 * A separate process that reads from the named FIFO and persists log entries to a log file.
 * Run BEFORE the server: ./logger &
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include "logger.h"
#include "common.h"

static volatile int running = 1;

static void handle_sig(int s) { (void)s; running = 0; }

int main(void) {
    signal(SIGINT, handle_sig);
    signal(SIGTERM, handle_sig);

    printf("[logger] Waiting for server on FIFO %s ...\n", LOG_PIPE);
    if (logger_init_reader() < 0) return 1;
    FILE *log_file = fopen("logs/server.log", "a");
    if (!log_file) { perror("fopen log"); return 1; }
    printf("[logger] Reading log stream. Ctrl-C to stop.\n");
    char line[1024];
    while (running) {
        int n = logger_read_line(line, sizeof(line));
        if (n <= 0) break;
        fputs(line, log_file);
        fflush(log_file);
        fputs(line, stdout);
        fflush(stdout);
    }
    fclose(log_file);
    logger_close();
    printf("[logger] Stopped.\n");
    return 0;
}