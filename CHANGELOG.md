# Changelog

Each compiler is versioned on its own (its `VERSION` file); a release of
this repository (`vX.Y.Z`, the top-level `VERSION`) collects them.

## Unreleased

### Lua

* Faster strings: `string.find`, `match` and `gmatch` and the common
  `string.format` conversions (`%d`, `%s`, `%%`) are in assembly; patterns
  are compiled once and searches skip positions that cannot match; plain
  `gsub` replacements are done in one pass. The strings benchmark runs in
  a quarter of the time.
* Calls of library functions that take a count of their arguments (`type`,
  `tostring`, `pairs`, `table.insert`, ...) no longer go through an
  adapter.
* Faster objects: field reads and writes (`t.name`) cache the table's
  shape, so a hit is one compare; method calls (`obj:m()`) find the method
  in the class inline instead of in the runtime; table constructors with
  constant keys set up their shape inline. The methods benchmark runs in
  half the time.
* A call as the last argument (`f(a, g())`) calls `f` directly when `g`
  returns one value, instead of building a list of arguments.
* Table constructors of positional values (`{a, b}`, `{f(x), g()}`) set up
  their array part inline (binarytrees: 0.81s to 0.73s).
* Float arithmetic on values of unknown type takes a shorter path
  (nbody: 2.7s to 1.8s).
* Values are read from tables and lists with MLWSVM M30's `ldv`, without
  a branch on their type.
* Fixed: errors from a method call on a value that cannot be indexed name
  the variable (`attempt to index a number value (local 'o')`).
* Fixed: a generic `for` whose iterator came from a call and took two
  parameters called it twice per step.
* Fixed: `gmatch`'s iterator returns no values at the end, as Lua's does;
  `string.format`'s `%c` wraps values outside 0..255, `%g`/`%e`/`%f`/`%a`
  keep the sign of -0.0, `%q` writes floats in hex, `%a` takes `+` and
  space flags and converts strings, `%s` with a width or precision rejects
  strings containing zeros, and a nil argument is "got nil".

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
