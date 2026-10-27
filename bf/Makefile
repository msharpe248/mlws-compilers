CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -pedantic

all: bfc

bfc: bfc.c
	$(CC) $(CFLAGS) -o $@ bfc.c

test: bfc
	../tools/run-tests.py bf tests

clean:
	rm -f bfc tests/*.masm examples/*.masm

.PHONY: all test clean
