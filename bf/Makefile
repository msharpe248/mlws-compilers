CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -pedantic

all: mlws-vm-bfc

mlws-vm-bfc: bfc.c
	$(CC) $(CFLAGS) -o $@ bfc.c

test: mlws-vm-bfc
	../tools/run-tests.py bf tests
	../tools/test-drivers.sh ./mlws-vm-bf tests/hello.b tests/hello.out

clean:
	rm -f mlws-vm-bfc tests/*.masm examples/*.masm

.PHONY: all test clean
