CC      = gcc
CFLAGS  = -Wall -Wextra -g -Iinclude
LDFLAGS = -lpthread -lncurses

SRC_DIR = src
TEST_DIR = test

COMMON_SRCS = $(SRC_DIR)/ot_engine.c $(SRC_DIR)/auth.c \
              $(SRC_DIR)/storage.c  $(SRC_DIR)/logger.c

SERVER_SRCS  = $(COMMON_SRCS) $(SRC_DIR)/server.c
CLIENT_SRCS  = $(COMMON_SRCS) $(SRC_DIR)/client.c
LOGGER_SRCS  = $(COMMON_SRCS) $(SRC_DIR)/logger_proc.c

CONCUR_SRCS  = $(TEST_DIR)/concurrency_tester.c $(COMMON_SRCS)

.PHONY: all clean dirs test-concurrency

all: dirs server client logger

#dirs:
#	@mkdir -p data logs

server: $(SERVER_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

client: $(CLIENT_SRCS)
	$(CC) $(CFLAGS) -o $@ $^  $(LDFLAGS)

logger: $(LOGGER_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Test targets
test-concurrency: $(TEST_DIR)/concurrency_tester
	@echo "Running concurrency tests..."
	@$(TEST_DIR)/concurrency_tester

$(TEST_DIR)/concurrency_tester: $(CONCUR_SRCS)
	@mkdir -p $(TEST_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f server client logger
	rm -f $(TEST_DIR)/tester $(TEST_DIR)/concurrency_tester
	rm -f /tmp/test_*.txt /tmp/storage_contention_test.txt
