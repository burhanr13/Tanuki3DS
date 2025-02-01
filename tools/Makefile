CC=gcc

ifeq ($(shell uname),Darwin)
	CC := $(shell brew --prefix)/opt/llvm/bin/clang
endif

EXECS := extractcode extractcxi

EXECS := $(EXECS:%=bin/%)

all: $(EXECS)

bin/%: %.c
	$(CC) -std=c23 -O3 -o $@ $^

.PHONY: clean
clean:
	rm -rf bin/*