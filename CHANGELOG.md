# Changelog

Each compiler is versioned on its own (its `VERSION` file); a release of
this repository (`vX.Y.Z`, the top-level `VERSION`) collects them.

## Release 0.4.1

bf 0.4.1, Lox 0.4.1 and Lua 0.4.1, for MLWSVM 0.4.1. Lua's runtime can
be an MLWSVM library, which needs MLWSVM 0.4.1 or later (one object per
constant across modules); self-contained programs still run on 0.3.0.

### bf 0.4.1

* No changes; the version follows MLWSVM 0.4.1, on which it is tested.

### Lox 0.4.1

* No changes; the version follows MLWSVM 0.4.1, on which it is tested.

### Lua 0.4.1

* The runtime as an MLWSVM library: `make lib` builds `lua/lib/lua-VERSION.mlwsl`
  (from `mlws-vm-luac --library DIR`), and `--shared` (on `mlws-vm-luac`
  and `mlws-vm-lua`) compiles a program that imports it instead of
  containing it: 10 KB instead of 790 KB for a small program, assembled in
  a few milliseconds instead of about 30, and as fast. `make test` runs the
  Lua tests both ways.
* Error positions come from the `.loc` positions in the VM's backtraces;
  the table from assembly lines to Lua lines (`rt$linemap`) is gone.
* The prelude's code no longer depends on the program's (its float hints
  come from the prelude alone), so the library and the programs compiled
  against it agree.

## Release 0.3.0

bf 0.3.0, Lox 0.3.0 and Lua 0.3.0, for MLWSVM 0.3.0. Lua's assembly needs
MLWSVM 0.3.0 or later; bf's and Lox's still run on MLWSVM 0.2.0.

### bf 0.3.0

* No changes; the version follows MLWSVM 0.3.0, on which it is tested.

### Lox 0.3.0

* No changes; the version follows MLWSVM 0.3.0, on which it is tested.

### Lua 0.3.0

* `string.upper` and `string.lower` change ASCII letters only, byte for
  byte, as Lua does in the C locale (`("straße"):upper()` is `"STRAßE"`),
  with MLWSVM M37's `text.ascii_upper`/`ascii_lower`; M37's `text.upper`
  maps all of Unicode. It needs MLWSVM M37 or later.

## Release 0.2.0

bf 0.2.0, Lox 0.2.0 and Lua 0.2.0, for MLWSVM 0.2.0. The assembly of all
three uses M34's `.loc` source positions, so it needs MLWSVM 0.2.0 or later.

### bf 0.2.0

* The assembly gives each stretch of code the line of the program it came
  from (`.loc`, MLWSVM M34), so the VM's profiler, tracer and debugger name
  lines of the brainfuck program. It needs MLWSVM M34 or later.

### Lox 0.2.0

* The assembly gives each stretch of code the Lox line it came from
  (`.loc`, MLWSVM M34), so the VM's backtraces, profiler, tracer and
  debugger name lines of the Lox program. It needs MLWSVM M34 or later.

### Lua 0.2.0

* The assembly gives each stretch of code the Lua line it came from
  (`.loc`, MLWSVM M34), so the VM's backtraces, profiler (hot program
  lines), tracer and debugger (`break program.lua:12`) name lines of the
  Lua program. It needs MLWSVM M34 or later.
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
* Each function's scratch registers are placed just above the registers
  it uses, instead of at the top of each bank, so a call has a smaller
  window to clear (fib: 0.15s to 0.13s; binarytrees: 0.72s to 0.67s).
* Jumps to the next instruction are removed, a branch over a jump becomes
  the opposite branch, and `x * x` checks `x`'s type once (nbody: 1.68s
  to 1.58s).
* Arithmetic on values whose type is not known puts the float path inline
  when the operands are likely floats: a guess from float constants, `/`,
  `^` and math functions, carried through locals, parameters and results
  of local functions, field names and the elements of tables (nbody: 1.58s
  to 1.37s).
* An expression of several arithmetic operators (`dx*dx + dy*dy`) checks
  each variable's type once, then runs its operators as straight-line
  code that writes only the result's tag (spectralnorm: 0.43s to 0.34s).
* Functions check their parameters once on entry (a table, or an integer
  or float, from how the body uses them) and have a second, generic copy
  for other arguments; in a loop, a local used as a table twice or more is
  checked once and the rest of the block has a generic copy. The facts
  then hold through the whole body, where a check's slow path would
  otherwise erase them (fannkuch: 2.72s to 2.36s; nbody: 1.31s to 1.19s).
* A loop that indexes a table in a local twice or more keeps the table's
  array length and list in registers, loaded on entry and after each call
  (only calls change them), unless that would push the loop's variables
  out of registers (fannkuch: 2.36s to 2.06s).
* A `for` loop whose limit was already known to be an integer (a constant,
  say) entered its body knowing nothing about any variable.
* Errors name `t[i]` as `field '?'` and `t[1]` as `field 'integer index'`,
  as Lua does.
* Uses MLWSVM M32's element offsets (`R[I+1]`) for the number half of a
  value in a list, and `bnisa` for "is it a table / function" checks; the
  output needs MLWSVM M32 (methods: 0.78s to 0.74s; nbody: 1.18s to
  1.12s).
* `setmetatable(t, mt)` is made inline when `setmetatable` is still the
  library's function, and a table constructor pushes each key with its
  value in one instruction (methods: 13% fewer instructions, 0.74s to
  0.72s).
* A table in a local whose fields are used twice or more (in a loop, after
  `local o = t[i]`, or as a parameter) keeps its shape and hash slots in
  registers, reloaded after calls; numeric `for` loops with a constant
  step use one register fewer (nbody: 1.13s to 1.04s).
* Typed copies of local functions: arguments of known types are passed
  in one register each, and a function whose returns always give one
  value of one type returns just that value (fib: 0.13s to 0.09s;
  spectralnorm: 0.31s to 0.27s; binarytrees: 0.66s to 0.62s). Tag writes
  that nothing reads are removed.
* Calls and returns pass a constant for a half of a value that its type
  leaves unused (an integer's float half, a table's number halves), so the
  register is not read before it is written.
* Fixed: unary minus on a string that converts to a number (`-"10"`)
  raised "number has no integer representation".
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
