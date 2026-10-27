CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -pedantic

SRCS = src/main.c src/scanner.c src/parser.c src/resolver.c src/codegen.c
OBJS = $(SRCS:.c=.o) src/runtime_masm.o

all: mlws-vm-loxc

mlws-vm-loxc: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) -lm

src/%.o: src/%.c src/lox.h
	$(CC) $(CFLAGS) -c -o $@ $<

# The runtime, as a C string.
src/runtime_masm.c: src/runtime.masm
	awk 'BEGIN { print "const char lox_runtime[] =" } \
	     { gsub(/\\/, "\\\\"); gsub(/"/, "\\\""); print "    \"" $$0 "\\n\"" } \
	     END { print ";" }' $< > $@

test: mlws-vm-loxc
	../tools/run-tests.py lox tests
	../tools/test-drivers.sh ./mlws-vm-lox tests/drivers/hello.lox tests/drivers/hello.out

clean:
	rm -f mlws-vm-loxc src/*.o src/runtime_masm.c

.PHONY: all test clean

src/runtime_masm.o: src/runtime_masm.c
	$(CC) $(CFLAGS) -Wno-overlength-strings -c -o $@ $<

# The benchmarks from the Crafting Interpreters test suite: each prints its
# own timing (zoo_batch: how many batches it ran in 10 seconds).
bench: mlws-vm-loxc
	@for f in tests/craftinginterpreters/benchmark/*.lox; do \
	    printf '%-18s ' $$(basename $$f .lox); ./mlws-vm-lox $$f | tr '\n' ' '; echo; done

.PHONY: bench
