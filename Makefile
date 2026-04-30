CC      = gcc
CFLAGS  = -Wall -Wextra -g -Iinclude
LDFLAGS = -lpthread -lncurses

SRC_DIR = src

COMMON_SRCS = $(SRC_DIR)/ot_engine.c $(SRC_DIR)/auth.c \
              $(SRC_DIR)/storage.c  $(SRC_DIR)/logger.c

SERVER_SRCS  = $(COMMON_SRCS) $(SRC_DIR)/server.c
CLIENT_SRCS  = $(COMMON_SRCS) $(SRC_DIR)/client.c
LOGGER_SRCS  = $(COMMON_SRCS) $(SRC_DIR)/logger_proc.c

.PHONY: all clean dirs

all: dirs server client logger

dirs:
	@mkdir -p data logs

server: $(SERVER_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

client: $(CLIENT_SRCS)
	$(CC) $(CFLAGS) -o $@ $^  $(LDFLAGS)

logger: $(LOGGER_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f server client logger