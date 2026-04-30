#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include "logger.h"
#include "common.h"

/*
 * A named pipe (FIFO) is used to decouple logging from the main
 * server process.  The server writes log lines to LOG_PIPE; a
 * separate logger process reads them and writes to a log file.
*/

static int pipe_fd = -1;

// Server side (writer)
int logger_init_writer(void) {
    mkfifo(LOG_PIPE, 0666);
    pipe_fd = open(LOG_PIPE, O_WRONLY | O_NONBLOCK);
    if (pipe_fd < 0) {
        /* No reader attached yet – open in non-blocking mode is fine;
         * writes will silently fail until logger process connects.    */
        pipe_fd = -1;
    }
    return 0;
}

void logger_write(const char *fmt, ...) {
    if (pipe_fd < 0) {
        /* Try to (re)open non-blocking – reader may have started */
        pipe_fd = open(LOG_PIPE, O_WRONLY | O_NONBLOCK);
    }
    char msg[1024];
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    char ts[32];
    strftime(ts, sizeof(ts), "%H:%M:%S", tm_info);
    char body[900];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);
    int len = snprintf(msg, sizeof(msg), "[%s] %s\n", ts, body);
    // Also echo to stdout
    fputs(msg, stdout);
    fflush(stdout);
    if (pipe_fd >= 0)
        write(pipe_fd, msg, len); // best-effort, ignore EAGAIN
}

// Logger process side (reader)
int logger_init_reader(void) {
    mkfifo(LOG_PIPE, 0666);
    pipe_fd = open(LOG_PIPE, O_RDONLY); // blocks until writer opens
    if (pipe_fd < 0) { perror("[logger] open FIFO"); return -1; }
    return 0;
}

int logger_read_line(char *buf, int buf_size) {
    if (pipe_fd < 0) return -1;
    int total = 0;
    char c;
    while (total < buf_size - 1) {
        int n = (int)read(pipe_fd, &c, 1);
        if (n <= 0) return n;
        buf[total++] = c;
        if (c == '\n') break;
    }
    buf[total] = '\0';
    return total;
}

void logger_close(void) {
    if (pipe_fd >= 0) { close(pipe_fd); pipe_fd = -1; }
}