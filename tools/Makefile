CC=gcc

ifeq ($(shell uname),Darwin)
	CC := $(shell brew --prefix)/opt/llvm/bin/clang
else ifeq ($(shell uname),Linux)
	CC := clang-19
endif

EXECS := extractcode extractcxi

EXECS := $(EXECS:%=bin/%)

all: $(EXECS)

bin/%: %.c
	$(CC) -I../src -std=c23 -O3 -o $@ $^

.PHONY: clean
clean:
	rm -rf bin/*
