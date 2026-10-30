CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -pedantic
VERSION := $(shell cat VERSION)

SRCS = src/main.c src/lex.c src/parse.c src/gen.c
OBJS = $(SRCS:.c=.o) src/embedded.o

# The VM, for the library and the tests: $MLWSVM, else mlwsvm on the PATH,
# else ../mlws-vm/mlwsvm next to this repository.
MLWSVM ?= $(shell command -v mlwsvm || echo ../../mlws-vm/mlwsvm)
LIB = lib/lua-$(VERSION).mlwsl

all: mlws-vm-luac

mlws-vm-luac: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) -lm

src/%.o: src/%.c src/lua.h
	$(CC) $(CFLAGS) -c -o $@ $<

# The version, from VERSION.
src/main.o src/gen.o: VERSION
src/main.o src/gen.o: CFLAGS += -DVERSION='"$(VERSION)"'

src/gen.o: src/builtins.h

# The runtime (runtime.masm, lib.masm) and the prelude, as C strings.
src/embedded.c: src/runtime.masm src/lib.masm src/match.masm src/prelude.lua
	{ awk 'BEGIN { print "const char *runtime_masm =" } \
	       { gsub(/\\/, "\\\\"); gsub(/"/, "\\\""); print "    \"" $$0 "\\n\"" } \
	       END { print ";" }' src/runtime.masm src/lib.masm src/match.masm; \
	  awk 'BEGIN { print "const char *prelude_lua =" } \
	       { gsub(/\\/, "\\\\"); gsub(/"/, "\\\""); print "    \"" $$0 "\\n\"" } \
	       END { print ";" }' src/prelude.lua; } > $@

src/embedded.o: src/embedded.c
	$(CC) $(CFLAGS) -Wno-overlength-strings -c -o $@ $<

# The runtime and the library as an MLWSVM library, for programs compiled
# with --shared (needs the VM). It is made again when the VM changes, whose
# module format may have.
lib: $(LIB)

$(LIB): mlws-vm-luac $(wildcard $(MLWSVM))
	rm -rf lib && mkdir -p lib/src
	./mlws-vm-luac --library lib/src
	"$(MLWSVM)" lib build lib/src -o $@

test: mlws-vm-luac $(LIB)
	../tools/run-tests.py lua tests
	LUA_FLAGS=--shared ../tools/run-tests.py lua tests
	../tools/test-drivers.sh ./mlws-vm-lua tests/drivers/hello.lua tests/drivers/hello.out 4
	./mlws-vm-lua --shared tests/drivers/hello.lua | cmp - tests/drivers/hello.out

clean:
	rm -f mlws-vm-luac src/*.o src/embedded.c
	rm -rf lib

.PHONY: all lib test clean

# The benchmarks, with the reference lua's times if it is installed.
bench: mlws-vm-luac
	bench/run.sh

.PHONY: bench
