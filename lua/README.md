# Lua 5.5 for MLWSVM: mlws-vm-lua

A compiler for [Lua 5.5](https://www.lua.org/manual/5.5/) that produces MLWSVM
assembly.

```sh
make
./mlws-vm-lua program.lua [args...]   # compile and run
./mlws-vm-lua -S program.lua          # export the assembly: program.masm
mlwsvm run program.masm [args...]     # which runs on its own

make lib                              # the runtime as an MLWSVM library
./mlws-vm-lua --shared program.lua    # a program that imports it
```

`mlws-vm-lua` compiles a program and runs it:

```
mlws-vm-lua [options] program.lua [args...]
  -S             only compile: write the assembly and do not run it
  -k             keep the assembly after running the program
  -o FILE        the assembly file (default: program.masm next to the source);
                 with -S, '-' writes it to standard output
  --shared       compile the program alone, importing the runtime from the
                 library lib/lua-VERSION.mlwsl (see below)
  --version      print the compiler's version (lua/VERSION)
```

`mlws-vm-luac` is the compiler itself; it only compiles:

```
mlws-vm-luac [--shared] [-o FILE] [-n NAME] program.lua
mlws-vm-luac --library DIR
  -o FILE        write the assembly to FILE (default: program.masm; '-' for stdout)
  -n NAME        the chunk name in error messages (default: the path as given)
  --shared       import the runtime from the MLWSVM library lua (see below)
  --library DIR  write that library's source to DIR
  --version      print the version
```

The assembly is one self-contained file: the program, the runtime and the
library, with no imports. It uses MLWSVM M37's `text.ascii_upper` and
`text.ascii_lower` (and M34's `.loc` source positions, M32's element
offsets and `bisa`/`bnisa`), so it needs M37 or a later VM. It can be assembled to a binary module
(`mlwsvm asm program.masm -o program.mlwso`), or run under `mlwsvm debug`,
`--profile` or `--trace`; each function is named `L<n>$name` after the Lua
function it came from, and its code carries the Lua lines it came from
(`.loc`), so backtraces, the profiler's hot program lines and the
debugger's `break program.lua:12` name lines of the Lua program.

### The runtime as a library

The runtime, the library and the prelude (the part of the library written
in Lua) are the same for every program, about 780 KB of assembly. They can
instead be one MLWSVM library, `lua`, that programs import
([MLWSVM libraries](../../mlws-vm/docs/ASSEMBLY.md#81-libraries)):

```sh
make lib                                   # lib/lua-VERSION.mlwsl (needs mlwsvm)
./mlws-vm-luac --shared -o program.masm program.lua
mlwsvm run -L lib program.masm             # -L: where the library is
./mlws-vm-lua --shared program.lua         # the same, in one step
```

`make lib` writes the library's source with `mlws-vm-luac --library
lib/src` (`library.masm`, and `lua.masm`, the module `lua`, which exports
the runtime's functions, data and structs) and builds it with `mlwsvm lib
build`. A program compiled with `--shared` holds only its own code (10 KB
for `tests/drivers/hello.lua`, against 790 KB), names the library's
functions as `lua.name`, and says `.requires lua, "=VERSION"`: it relies
on the runtime's internals, so it needs the library of the same compiler
version, and checks at start-up that the library pins the globals and
makes the table constructors it expects. Such a program is assembled in
a few milliseconds instead of about 30, and runs as fast. Its output must
not be named `lua.masm`, which would be found instead of the library.
The runtime tells nil, false and true apart by the identity of constants
(`1n`, `2n`, `3n`), so the VM must make one object of each constant for
all modules: MLWSVM 0.4.1 or later, which the library requires.

## What it implements

The whole language of Lua 5.5:

* integers and floats as distinct subtypes, with Lua's arithmetic (wrapping
  integers, floor division and modulo, exact integer/float comparison,
  string coercion), bitwise operators and number formatting;
* strings, tables with array and hash parts, metatables and every
  metamethod (`__index`, `__newindex`, `__call`, arithmetic and bitwise ones,
  `__eq`, `__lt`, `__le`, `__concat`, `__len`, `__unm`, `__close`,
  `__tostring`, `__name`, `__metatable`, `__pairs`);
* closures with shared upvalues, varargs, multiple results and assignment,
  proper tail calls, `goto` and labels, integer and float `for` loops;
* `local <const>` and `<close>` variables; Lua 5.5's `global` declarations
  (`global x`, `global<const> *`, `global function f`), named vararg tables
  (`function f(...t)`) and read-only loop variables;
* coroutines (`create`, `resume`, `yield`, `wrap`, `status`, `running`,
  `isyieldable`, `close`), on the VM's stackful coroutines, so a yield can
  happen at any depth, including inside `pcall`, metamethods and `table.sort`;
* errors as Lua reports them: `error` with levels, `pcall`, `xpcall`,
  messages with positions and variable names
  (`prog.lua:3: attempt to index a nil value (field 'x')`), and stack
  overflow as an error;
* the standard library: the basic functions, `string` (with Lua patterns,
  `format`, `gsub`, ...), `table`, `math`, `utf8`, `coroutine`, `io` (files,
  `read` formats, `lines`), `os` (`time`, `date`, `clock`, `getenv`, `exit`,
  `remove`, `rename`) and a little of `debug`.

Compile errors and uncaught runtime errors are reported as `lua` reports
them, prefixed `mlws-vm-lua:`, with exit status 1.

### Differences from PUC Lua

* **No compiler at run time**: programs are compiled ahead of time, so
  `load` returns an error and `dofile`, `loadfile` and `require` are not
  available. `string.dump`, `string.pack` and `string.unpack` are missing.
* **`debug`** has only `traceback` (without the frames), `getinfo` (without
  details), `sethook`, `gethook`, `getmetatable` and `setmetatable`.
  Uncaught errors print the message without a stack traceback.
* **No weak tables or finalizers**: `__mode` and `__gc` are ignored (the
  VM's collector is precise but has neither).
* **To-be-closed variables** are closed when their block exits normally or
  by `break`, `goto` or `return`, but not when an error unwinds it.
* **Messages from library functions** name the function by its own name
  (`bad argument #1 to 'rep'`) and say `got nil` for a missing argument,
  where Lua may say `'string.rep'` or `got no value`.
* **`#t` for a table with holes** returns a border, as Lua does, but not
  always the same one.
* **`os.clock`** measures wall time since the program started.
* **Recursion** may go 100,000 calls deep (the VM's default; raise it with
  `mlwsvm run --max-frames N`) before `stack overflow`.
* Object addresses printed by `tostring` are numbers made up per object.

```
make test     # the tests (tests/lang, tests/programs) and the driver's -S, -o and -k
make bench    # the benchmarks, with the reference lua's times if it is installed
```

## How it works

`src/lex.c` and `src/parse.c` follow Lua's own `llex.c` and `lparser.c`: a
recursive-descent parser that builds an AST and resolves names as it goes
(locals, upvalues, globals and their declarations, gotos and labels, and
which variables closures capture or assign). `src/gen.c` writes the
assembly. The runtime is in assembly (`src/runtime.masm`; the library in
`src/lib.masm`; Lua patterns in `src/match.masm`), and the parts of the
library that are easier in Lua (`string.format` beyond `%d`, `%s` and
`%%`, `gsub`, `table.sort`, `utf8`, `io`, `os.date`) are in `src/prelude.lua`,
which mlws-vm-luac compiles into every program with the program itself.

### Values

MLWSVM registers are typed (I, F, R and P banks), so a Lua value is a
**triple** of registers: an I register for an integer, an F register for a
float, and an R register that says what the value is:

| Lua value | R register | Payload |
|---|---|---|
| integer | `null` | the I register |
| float | `4n` | the F register |
| `nil`, `false`, `true` | `1n`, `2n`, `3n` | none |
| string | the string | none |
| table, function, coroutine | a `Tab`, `Fn` or `Thread` struct | none |

`1n` to `4n` are BigInt constants: no Lua value is a BigInt, so they can
never be confused with a string or a table key. "Is it an integer?" is a
null test of a register, the VM's fastest branch. In lists (a table's
array part, argument lists) a value takes two words: the R half, then the
integer or the float.

### Functions and calls

A Lua function with up to seven parameters compiles to
`.func L<n>$name(i64, f64, ref, ...) -> (i64, i64, f64, ref)`: the
parameters as triples, and the number of results with the first one.
Further results go in a list, `@rt$mv`, which the caller reads at once.
A function value is an `Fn` struct holding the MLWSVM closure and a
signature code (exact arity, varargs, or everything in a list). A call
checks for an `Fn` of the right arity inline and calls the closure with
`calli`; anything else (a different arity, a vararg function, `__call`,
not a function) goes to a helper `rt$call<n>` that adapts the arguments.
A local function that captures nothing is called with a direct `call`, so
recursion such as `fib` needs no closure at all.

Upvalues follow the usual scheme: a variable that is captured and assigned
lives in a heap `Box`; one that is never assigned after its declaration is
copied into the closure, and if its type is known (a local table,
function, string or number constant), only the one register that holds it.

### Tables

A table has an array part (values `1..n` in a list) and a hash part (a list
of slots and a map from each key to its slot). Tables whose string keys
were added in the same order share their key map (a **shape**), which is
never changed: adding a key moves the table to the next shape. Each
`t.name` in the program has an **inline cache** of the shape it saw last
time and where `name` is in it, so objects of one "class" are read with
one compare and no hash lookup; for a table with a map of its own (more
than 16 keys, as a library's), the cache is used if the slot it names
holds that very key. Table constructors with constant keys build the
slots directly in their shape, set up inline. Integer keys in the array
part are read and written inline.

Each table that is used as a metatable caches its `__index` and
`__newindex`. Each `obj:method()` caches a shape that does not have the
method, and for an object of that shape reads the method from the
metatable's `__index` table through an inline cache, with no runtime call.

### Optimisations

* **Type facts.** The code generator tracks which slots hold integers,
  floats, strings, tables and functions, and leaves out the checks and
  register moves that facts make unnecessary. Loops are generated with the
  facts at their entry, and regenerated with weaker ones if the loop body
  changes them, so a counter or accumulator stays an integer.
* **Arithmetic** has an inline path for integers, or for floats when an
  operand is known or likely to be one, and in out-of-line code a path for
  the other kind, then one that converts mixed integer and float operands;
  only strings and metamethods call the runtime. Constant expressions are
  folded. "Likely" is a guess made before code generation: float
  constants, `/`, `^` and math functions make floats, and the guess
  follows them through locals, the parameters and results of local
  functions, field names (`p.x`, across the program) and the elements of
  tables held in locals.
* **Arithmetic expressions** of several operators (`+ - * / ^` and unary
  minus) are generated as a unit: each variable's type is checked once,
  then the operators run as straight-line code that writes only the
  result's tag; if a check fails, cold code does them one at a time.
* **Versions.** A function whose parameters are used as tables or numbers
  checks them once on entry and is generated twice: with those facts, and
  as a generic copy for other arguments. In a loop, a local that the rest
  of the block uses as a table twice or more is checked once, and the rest
  of the block has a generic copy in cold code. A fact that holds through
  a whole body survives its joins, where one learnt from a check inside it
  would not, since the check's slow path joins it again without it.
* **Array caches.** A loop that indexes a table held in a local (known to
  be a table and never assigned) twice or more keeps the table's array
  length and list in a register pair, loaded on entry and after every
  call, since only a call can change them. If the extra registers would
  push the loop's variables into memory, the loop is generated again
  without them.
* **Shape caches.** Where a table held in a local (known to be a table
  and never assigned) has its fields read or written twice or more, as in
  a loop, the rest of a block after `local o = t[i]`, or a function whose
  parameter is a table, its map and hash slots (`Tab.hm`, `Tab.hs`) are
  kept in registers, loaded on entry and after every call. A field access
  is then the inline cache's compare and the load. If the registers would
  push variables into memory, only the hash slots are kept, or nothing.
* **Numeric `for`** with a constant step keeps no step register, and a
  limit in an integer local that is never assigned is used where it is.
* **Typed copies.** A local function that captures nothing gets a copy
  for each combination of argument types its direct calls use: each
  argument is passed in one register (an integer in I, a float in F, a
  table or string in R). If every return of the copy gives one value of
  one type, it returns just that, with no count and no tag (`fib(n)` is
  `(i64) -> i64`). The return type comes from an inference over the
  source before code generation: each local has one type for its whole
  life, and a recursive call's result is iterated to a fixpoint. A tail
  call stays a tail call when both sides return the same way.
* **Dead tag writes.** A tag write (`mov.r R, null`) that is overwritten
  or followed by a return on every path before anything reads it is
  removed, in registers that never hold a reference (where it may be what
  lets the collector free an object).
* **Calls** whose last argument is a call (`f(a, g())`) are made directly
  when `g` returns one value; any other count builds a list of arguments.
  A constructor of positional values (`{a, g()}`) builds its array part
  inline, the same way.
* **`setmetatable(t, mt)`** is done inline when the function called is
  the library's (one compare with the value saved at start-up): it checks
  `t` and `mt` and that `t`'s metatable is not protected, and otherwise
  makes the call, so errors are the library's.
* **Table constructors** push each constant key and its value with one
  `lpush`.
* **Generic `for`** over `pairs` and `ipairs` is recognised at run time:
  `pairs` walks the table by position and `ipairs` reads the array part
  inline, instead of calling `next` or the iterator.
* **Patterns** are compiled as they are used, once per pattern: each item's
  end and a 256-entry set of the characters it matches. A search skips
  positions where the pattern's first item cannot match, backtracking
  skips positions where the rest of the pattern cannot begin, and a search
  for a pattern that starts with `x+` or `x*` and has no captures skips
  the rest of a run of `x`s after a failure there.
* Slow paths are placed after the function's last instruction, so the fast
  paths fall straight through; upvalues are copied to registers at the
  start of a function that loops.
* **Register windows.** Scratch registers are generated at the top of each
  bank and then moved down to just above the registers the function uses,
  since a call zeroes the callee's whole window.

### Errors

A Lua error is an MLWSVM exception carrying a `LuaErr` struct; `pcall` is a
`.try` around the call, and faults the VM raises itself (stack overflow,
running out of memory) become Lua errors. Positions come from the VM's
backtrace: a compiled Lua function's frame shows its Lua line (from
`.loc`), the library's functions are recognised by name (`B$...`), so that
an error in one is reported at the line that called it, as in Lua, and
`error`'s levels count frames the way Lua does.

## Performance

`make bench` (`bench/run.sh`), on the machine this was developed on (an
Apple M5 Pro, MLWSVM v0.2.0, whose module cache was warm, so a run does not
assemble the runtime again), in seconds, with Lua 5.5.1 for comparison:

| Benchmark | mlws-vm-lua | lua 5.5 |
|---|---|---|
| fib (32) | 0.06 | 0.05 |
| loop | 0.18 | 0.21 |
| methods (2M method calls and objects) | 0.57 | 0.34 |
| strings (format, gmatch, gsub) | 0.36 | 0.13 |
| iterate (pairs, ipairs) | 0.09 | 0.05 |
| binarytrees (15) | 0.56 | 0.37 |
| nbody (500,000) | 0.97 | 0.40 |
| spectralnorm (500) | 0.22 | 0.18 |
| fannkuch (10) | 1.92 | 0.79 |

The profiles (`mlwsvm run --profile`) show almost every instruction on the
VM's fast paths; what remains is instruction count, mostly type checks on
values whose type is not known statically (anything read from a table or
passed as an argument), bounds checks on array accesses and allocation.

With MLWSVM's optimizer (`MLWSVM_OPT=3`, MLWSVM 0.6.0 or later; see the
top-level README), the same benchmarks on MLWSVM d7cdc6f, cache warm, run
from 0% to 27% faster: spectralnorm 0.22 → 0.16, loop 0.19 → 0.17,
iterate 0.09 → 0.08, binarytrees 0.57 → 0.54, nbody 0.99 → 0.95,
fannkuch 1.94 → 1.90; fib, methods and strings are unchanged.

## Tests

`tests/lang` has a test per area of the language and library, and
`tests/programs` larger programs (benchmarks, a JSON encoder and decoder,
an object system, coroutine pipelines, text processing). Each `NAME.lua`
has its expected output in `NAME.out` (and `NAME.err` for the first line of
an expected error), made by running the reference `lua` 5.5 with
`tests/expect.sh`, so the tests need only python3 to run. `make test` runs
them twice, as self-contained programs and with `--shared` (the library is
built first). The tests also pass with the VM collecting garbage at every
allocation (`MLWSVM_GC_STRESS=1`).
