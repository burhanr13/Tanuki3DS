CC=gcc

ifeq ($(shell uname),Darwin)
	CC := $(shell brew --prefix)/opt/llvm/bin/clang
endif

.PHONY: extractcode
extractcode: bin/extractcode
bin/extractcode: extractcode.c
	$(CC) -std=c23 -O3 -o $@ $^

.PHONY: clean
clean:
	rm -rf bin/*