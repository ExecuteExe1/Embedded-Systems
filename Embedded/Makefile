CC      := gcc
CFLAGS  := -Wall -Wextra -O2 -pthread -std=gnu11 $(shell pkg-config --cflags libcjson 2>/dev/null)
LDLIBS  := -lwebsockets -lssl -lcrypto -lm -pthread $(shell pkg-config --libs libcjson 2>/dev/null || echo -lcjson)

BIN         := jetstream_rt
ANALYZE_BIN := analyze

SRCS := main.c common.c producer.c consumer.c logger.c network_reset.c
OBJS := $(SRCS:.c=.o)

LOG_FILE ?= metrics_log.txt

.PHONY: all clean deps run-analyze

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Only built if analyze.c is present in this same folder.
ifneq ("$(wildcard analyze.c)","")
all: $(ANALYZE_BIN)

$(ANALYZE_BIN): analyze.c
	$(CC) -Wall -Wextra -O2 -std=gnu11 -o $@ $< -lm

run-analyze: $(ANALYZE_BIN)
	./$(ANALYZE_BIN) $(LOG_FILE)
endif

# Convenience target for a fresh Raspberry Pi OS install.
deps:
	sudo apt-get update
	sudo apt-get install -y build-essential libwebsockets-dev libcjson-dev pkg-config gnuplot

clean:
	rm -f $(OBJS) $(BIN) $(ANALYZE_BIN) *.gp jitter.csv rate_buffer.csv cpu.csv summary.txt jitter.png load_buffer.png cpu.png
