# Lox for MLWSVM: mlws-vm-lox

A compiler for [Lox](https://craftinginterpreters.com/the-lox-language.html),
the language of *Crafting Interpreters*, that produces MLWSVM assembly.

```sh
make
./mlws-vm-lox program.lox         # compile and run
./mlws-vm-lox -S program.lox      # export the assembly: program.masm
mlwsvm run program.masm           # which runs on its own
```

`mlws-vm-lox` compiles a program and runs it:

```
mlws-vm-lox [options] program.lox [args...]
  -S             only compile: write the assembly and do not run it
  -k             keep the assembly after running the program
  -o FILE        the assembly file (default: program.masm next to the source);
                 with -S, '-' writes it to standard output
```

`mlws-vm-loxc` is the compiler itself; it only compiles:

```
mlws-vm-loxc [-o FILE] program.lox
  -o FILE        write the assembly to FILE (default: program.masm; '-' for stdout)
```

The assembly is one self-contained file: the program, the runtime
(`src/runtime.masm`) and a line map, with no imports. It can also be
assembled to a binary module (`mlwsvm asm program.masm -o program.mlwso`),
or run under `mlwsvm debug`, `--profile` or `--trace`; each function is named
`L<n>$name` after the Lox function it came from.

It implements the whole language (closures, classes, inheritance, `super`,
initializers, `clock()`) and behaves like jlox: it passes all 241 applicable
tests of the book's test suite, with jlox's compile error messages and error
recovery and the same runtime error messages. Compile errors exit with status
65 and runtime errors with 70, as in the book.

```
make test     # the book's tests (tests/craftinginterpreters), mlws-vm-loxc's own
              # (tests/compiler), and the mlws-vm-lox driver's -S, -o and -k options
make bench    # the book's benchmarks
```

## Differences from jlox

* **Numbers print** as integers when they are whole and below 10^18
  (`1000000`, `-0`), and otherwise in their shortest exact form with a C-style
  exponent (`0.30000000000000004`, `1e+20`, `inf`, `nan`). jlox prints
  `1.0E20`, `Infinity` and `NaN`.
* **`NaN == NaN` is false**, following IEEE 754 (and clox).
* **Recursion** may go 100,000 calls deep (the VM's default; raise it with
  `mlwsvm run --max-frames N`) before `Stack overflow.`
* **Runtime errors** end with `[line N] in script`, as in clox.
* `clock()` uses a monotonic clock: only differences are meaningful.

## How it works

`src/scanner.c` and `src/parser.c` follow jlox (recursive descent; `for`
desugars to `while`). `src/resolver.c` binds variables and reports jlox's
static errors, and also works out which locals nested functions capture and
which variables are ever assigned. `src/codegen.c` writes the assembly, and
`src/runtime.masm` (compiled into mlws-vm-loxc and copied into every output file) has
the struct types and the slow paths. The output is a single self-contained
`.masm` file.

### Values

MLWSVM registers are typed (I, F, R and P banks), so a dynamically typed Lox
value is a **pair**: an F register for a number and an R register for a
reference.

| Lox value | F half | R half |
|---|---|---|
| number | the number | `null` |
| `nil`, `true`, `false` | unused | the constant strings `"\"nil"`, `"\"true"`, `"\"false"` |
| string | unused | a string |
| function, class, instance, bound method | unused | an `Fn`, `Class`, `Inst` or `Bound` struct |

"Is it a number?" is therefore a single null test of a register, the VM's
fastest branch. The three sentinels contain a `"`, which no Lox string can,
so they are never equal to a string, and they are module constants, so
`nil` and the booleans compare by identity.

### Variables and functions

* **Locals and temporaries** are numbered slots: slot k is `(Fk, Rk)` for
  k < 24, and a pair of stack words (`LOC[...]`) beyond that. Since every
  instruction accepts memory operands, spilled slots need no special code.
* **Globals** are three data words each: number, reference and a "defined"
  byte (reading or assigning an undefined global is a runtime error).
* **Functions** compile to `.func L<id>$name(f64, ref, ...) -> (f64, ref)`.
  Methods take the receiver as an extra first pair. A function value is an
  `Fn` struct (closure, arity, name). Calls with more than 12 arguments pass
  them in a list instead.
* **Closures** use MLWSVM closures. A captured variable that is never
  assigned after its declaration is copied into each closure. One that is
  assigned lives in a heap `Box` that the closures share. Functions are
  created "Fn first", so a local recursive function can capture itself by
  value.
* **Instances** have a map from field names to `Box`es. Classes have a map of
  methods, with inherited ones copied in when the class is created, as clox
  does.

### Calls

A call site checks for an `Fn` of the right arity inline and calls its closure
with `calli`. Anything else (a class, a bound method, `clock`, a wrong arity,
or something that is not callable) goes to the generated helper
`rt$call<n>`. Three cases are faster:

* `f(...)` where `f` is a global function declared once and never assigned
  is a direct `call` (after checking that `f` has been defined).
* `C(...)` where `C` is such a class allocates the instance inline and calls
  its initializer directly.
* `obj.m(...)` does not create a bound method. Each such site has an
  **inline cache** of the last class it saw and that class's method, so a
  monomorphic call is an instance check, a class compare and a `calli`. The
  check for a field shadowing the method is left out when the program never
  assigns a property of that name, and the instance check when `obj` is
  `this`.

### Optimisations

* **Type facts.** The code generator tracks which local slots and globals
  are known to hold numbers (after a check, or after storing a number) and
  leaves out repeated checks. Facts merge at labels (intersection), loop
  heads forget the variables the loop assigns, and facts about globals are
  dropped at any call that could run Lox code. Once a global's "defined"
  check has passed, it is not repeated.
* **Constant folding** of arithmetic, comparisons and equality on literals.
  An expression statement that has no effect and cannot fail is dropped.
  This is why the `equality` and `string_equality` benchmarks, which only
  evaluate such statements, finish in no time.
* `a + b` where one side is known to be a number must be numeric addition
  (or an error), so its result is known to be a number too.
* **Cold code.** Slow paths and error reports are placed after the function's
  last instruction, so the fast paths fall straight through.
* **Line map.** Errors that the VM raises itself (a stack overflow) are
  reported with the Lox line: the output contains a table from assembly
  lines to Lox lines, which `rt$fault` applies to the VM's backtrace.

### Performance

The book's benchmarks (`make bench`), on the machine this was developed on:

| Benchmark | Seconds |
|---|---|
| fib | 1.07 |
| binary_trees | 2.75 |
| trees | 4.97 |
| properties | 1.17 |
| invocation | 0.86 |
| instantiation | 0.73 |
| method_call | 0.46 |
| zoo | 0.91 |
| zoo_batch | 1,877 batches in 10 s |

Field access is now the main cost (`trees`, `binary_trees`): each read is a
map lookup and a load from a `Box`. Giving classes a shared field layout,
with per-site caches of field positions, would be the next step.

## Tests

`tests/craftinginterpreters` is the test suite from
[munificent/craftinginterpreters](https://github.com/munificent/craftinginterpreters)
(MIT licence in that directory); its `SKIP` file lists what does not apply,
following the book's own configuration for jlox. `tests/compiler` covers what the
optimisations could get wrong: polymorphic and shadowed method calls, type
changes through calls, loops and closures, spilled slots, packed arguments
and captures, construction, and number formatting. Everything also passes
with the VM collecting garbage at every allocation (`MLWSVM_GC_STRESS=1`).
