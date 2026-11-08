CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -pedantic
VERSION := $(shell cat VERSION)

SRCS = src/main.c src/lex.c src/parse.c src/gen.c
OBJS = $(SRCS:.c=.o) src/embedded.o
LIBS = $(sort $(wildcard src/lib/*.erl))

all: mlws-vm-erlc

mlws-vm-erlc: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) -lm

src/%.o: src/%.c src/erl.h
	$(CC) $(CFLAGS) -c -o $@ $<

# The version, from VERSION.
src/main.o src/gen.o: VERSION
src/main.o src/gen.o: CFLAGS += -DVERSION='"$(VERSION)"'

# The runtime (runtime.masm, runtime_re.masm for re and runtime_mod.masm for
# --modules) and the library's modules (lib/*.erl), as C strings.
src/embedded.c: src/runtime.masm src/runtime_re.masm src/runtime_mod.masm $(LIBS)
	{ awk 'BEGIN { print "const char *runtime_masm =" } \
	       { gsub(/\\/, "\\\\"); gsub(/"/, "\\\""); print "    \"" $$0 "\\n\"" } \
	       END { print ";" }' src/runtime.masm; \
	  awk 'BEGIN { print "const char *runtime_re_masm =" } \
	       { gsub(/\\/, "\\\\"); gsub(/"/, "\\\""); print "    \"" $$0 "\\n\"" } \
	       END { print ";" }' src/runtime_re.masm; \
	  awk 'BEGIN { print "const char *runtime_mod_masm =" } \
	       { gsub(/\\/, "\\\\"); gsub(/"/, "\\\""); print "    \"" $$0 "\\n\"" } \
	       END { print ";" }' src/runtime_mod.masm; \
	  for f in $(LIBS); do \
	    n=$$(basename $$f .erl); \
	    awk -v n=$$n 'BEGIN { print "static const char lib_" n "[] =" } \
	       { gsub(/\\/, "\\\\"); gsub(/"/, "\\\""); print "    \"" $$0 "\\n\"" } \
	       END { print ";" }' $$f; \
	  done; \
	  echo 'const char *prelude_files[] = {'; \
	  for f in $(LIBS); do echo "    lib_$$(basename $$f .erl),"; done; \
	  echo '    0 };'; \
	  echo 'const char *prelude_names[] = {'; \
	  for f in $(LIBS); do echo "    \"$$(basename $$f .erl)\","; done; \
	  echo '    0 };'; } > $@

src/embedded.o: src/embedded.c
	$(CC) $(CFLAGS) -Wno-overlength-strings -c -o $@ $<

test: mlws-vm-erlc
	../tools/run-tests.py erlang tests
	../tools/test-drivers.sh ./mlws-vm-erl tests/drivers/hello.erl tests/drivers/hello.out 5
	tests/modules/run.sh

bench: mlws-vm-erlc
	bench/run.sh

clean:
	rm -f mlws-vm-erlc src/*.o src/embedded.c

.PHONY: all test bench clean
