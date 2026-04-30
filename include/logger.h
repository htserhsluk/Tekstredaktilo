#ifndef LOGGER_H
#define LOGGER_H

// Open the named FIFO for writing (server side)
int  logger_init_writer(void);

// Write a log line to the FIFO.
void logger_write(const char *fmt, ...);

// Open the named FIFO for reading (logger process side)
int  logger_init_reader(void);

// Block-read one log line. Returns bytes read or -1.
int  logger_read_line(char *buf, int buf_size);

void logger_close(void);

#endif