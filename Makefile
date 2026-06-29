CC       ?= clang
CFLAGS   ?= -Wall -Wextra -Wpedantic -std=c11 -O2 -g
ASAN     ?= -fsanitize=address,undefined -fno-omit-frame-pointer

SRC_DIRS  = src/core src/checksum src/buffer src/frame src/session \
            src/channel src/reassembly src/flow src/receipt src/route \
            src/compression src/crypto src/scheduler src/transcript \
            src/diagnostics src/stream src/cli src/api
SRCS      = $(foreach d,$(SRC_DIRS),$(wildcard $(d)/*.c))
OBJS      = $(SRCS:.c=.o)
LIB       = libstreamux.a

TEST_SRCS = $(wildcard tests/*.c)
TEST_BINS = $(TEST_SRCS:.c=)

.PHONY: all clean test asan harnesses

all: $(LIB)

$(LIB): $(OBJS)
	ar rcs $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -Isrc -c -o $@ $<

test: $(LIB) $(TEST_BINS)
	@echo "Running tests..."
	@for t in $(TEST_BINS); do \
		echo "  $$t"; \
		./$$t || exit 1; \
	done
	@echo "All tests passed."

tests/%: tests/%.c $(LIB)
	$(CC) $(CFLAGS) -Isrc -o $@ $< -L. -lstreamux -lm

asan: CFLAGS += $(ASAN)
asan: clean all

harnesses: $(LIB)
	$(CC) $(CFLAGS) $(ASAN) -Isrc -o out/channel_fuzzer fuzz/channel_fuzzer.cc -L. -lstreamux -lm -lstdc++
	$(CC) $(CFLAGS) $(ASAN) -Isrc -o out/session_fuzzer fuzz/session_fuzzer.cc -L. -lstreamux -lm -lstdc++
	$(CC) $(CFLAGS) $(ASAN) -Isrc -o out/stream_fuzzer  fuzz/stream_fuzzer.cc  -L. -lstreamux -lm -lstdc++
	$(CC) $(CFLAGS) $(ASAN) -Isrc -o out/migrate_fuzzer fuzz/migrate_fuzzer.cc -L. -lstreamux -lm -lstdc++

clean:
	rm -f $(OBJS) $(LIB) $(TEST_BINS)
	rm -f out/channel_fuzzer out/session_fuzzer out/stream_fuzzer out/migrate_fuzzer
