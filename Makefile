# Build and test every compiler.
DIRS = bf lox lua

all:
	@for d in $(DIRS); do $(MAKE) -C $$d || exit 1; done

test: all
	@for d in $(DIRS); do echo "== $$d"; $(MAKE) -s -C $$d test || exit 1; done

clean:
	@for d in $(DIRS); do $(MAKE) -C $$d clean; done

.PHONY: all test clean
