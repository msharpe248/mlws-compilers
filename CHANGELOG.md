# Changelog

Each compiler is versioned on its own (its `VERSION` file); a release of
this repository (`vX.Y.Z`, the top-level `VERSION`) collects them.

## Release 0.1.0

The first release: bf 0.1.0, Lox 0.1.0 and Lua 0.1.0, for MLWSVM 0.1.0
(M26).

### bf 0.1.0

* A brainfuck compiler with run-length folding, offset folding and the
  clear, multiply and scan idioms; a lazily wrapped 64-bit tape; buffered
  output; configurable tape size and end-of-file behaviour.

### Lox 0.1.0

* The whole Lox language of *Crafting Interpreters*, passing every
  applicable test of the book's suite with jlox's messages.
* Type facts, constant folding, direct calls of global functions, inline
  instance construction and per-site inline caches for method calls.

### Lua 0.1.0

* Lua 5.5: the whole language (integers and floats, metatables, closures,
  varargs, goto, proper tail calls, `<const>`/`<close>`, `global`
  declarations, named vararg tables), coroutines on the VM's stackful
  coroutines, and most of the standard library, including Lua patterns.
* Type facts with speculative loop facts, shared table shapes, per-site
  inline caches, inline pairs/ipairs loops and direct calls of local
  functions.

### All

* Each driver (`mlws-vm-bf`, `mlws-vm-lox`, `mlws-vm-lua`) can export the
  assembly (`-S`, `-k`, `-o FILE`) and prints its version (`--version`).
