# Erlang for MLWSVM: mlws-vm-erl

`mlws-vm-erlc` compiles Erlang to MLWSVM assembly. Erlang's processes are
MLWSVM processes, its messages MLWSVM messages, and its monitors and links
the VM's, so this is the compiler that puts the VM's concurrency to work.

```sh
make                                   # builds mlws-vm-erlc
./mlws-vm-erl program.erl [args...]    # compile and run
./mlws-vm-erl -S program.erl           # write program.masm, don't run
./mlws-vm-erl -b app program.erl       # write one executable, app (MLWSVM 0.17.0)
./mlws-vm-erl -d program.erl           # run it in the debugger, variables named (0.17.3)
./mlws-vm-erlc -o out.masm program.erl # the compiler alone
make test                              # the tests (needs python3)
make bench                             # the benchmarks, against erl if installed
```

A program is a module with `main/0`, or `main/1`, which gets the
command-line arguments as a list of strings. Other modules it calls are
compiled with it when they are found as `NAME.erl` next to it, or can be
named on the compiler's command line. The output is one self-contained
`.masm` file holding the program, the runtime and the part of the library
that the program uses. It needs MLWSVM 0.6.0 or later.

With `-g` (on `mlws-vm-erlc` and the driver) the debuggers (`mlwsvm debug`,
and editors through `mlwsvm dap`) show the program's variables by name, as
`~p` prints them; `-d` compiles with `-g` and runs the program in `mlwsvm
debug`, which reads its commands from standard input:

```
$ ./mlws-vm-erl -d program.erl
(mdb) break program.erl:16
(mdb) run
(mdb) locals
Shapes (I0, R0) = [{rect,2,3},{circle,1.5}]
Name (I1, R1) = "shapes"
M (I3, R3) = #{a => 1}
```

A variable is named (`.local X, I1, R1`) from where it is bound, in the
hot code; after a `case`, `receive` or `try`, only the variables every
clause binds keep their names. The formatter, `.debug_format rt$show`,
calls `io_lib:print/1` in a process of its own, on copies. Variables on the
stack (beyond the 29th slot) are not named, and the optimizer drops the
names of a function it changes, so debug code that is not optimized. The
output of `-g` says it needs MLWSVM 0.17.3 (`.requires`); `mlwsvm debug`
takes no `--max-frames`, so a program run with `-d` has the VM's default
limit on recursion.

## What it implements

* Modules, `-export`, `-import`, `-record` (and `#r{}`, `R#r.f`,
  `R#r{f = V}`, `#r.f`, `is_record`), `-compile(export_all)`; the
  preprocessor: `-define` with and without arguments, `-undef`, `-ifdef`,
  `-ifndef`, `-if`, `-elif`, `-else`, `-endif`, `-include`, and `?MODULE`,
  `?MODULE_STRING`, `?FILE`, `?LINE`, `?FUNCTION_NAME`, `?FUNCTION_ARITY`.
  Other attributes (`-spec`, `-type`, `-behaviour`, ...) are skipped.
* Function clauses with patterns and guards, `case`, `if`, `receive ...
  after`, `try ... of ... catch ... after`, `catch`, `begin`, funs (with
  closures and named funs), `fun f/1`, `fun m:f/1`, `M:F(...)` and
  `apply/2,3` with computed names, list, binary and map comprehensions,
  maps (`#{}`, `M#{K => V}`, `M#{K := V}`, map patterns), binaries
  (integer, float, binary and utf8 segments of whole bytes, in patterns
  and constructions), `++`, `--`, and every operator.
* Integers of any size (64-bit until they overflow, then BigInts), floats
  (with `badarith` for a result that is not finite), atoms, tuples,
  lists, strings as lists, binaries, maps, funs, pids and references, all
  compared in Erlang's term order.
* Processes: `spawn`, `spawn_link`, `spawn_monitor`, `spawn/3`, `!`,
  `receive` with selective matching and `after`, `self`, `register`,
  `whereis`, `link`, `monitor`, `demonitor`, `exit/1,2`,
  `process_flag(trap_exit, true)` (an `'EXIT'` message for processes
  linked after it), `make_ref`, the process dictionary. Exit reasons
  travel to monitors and to `'DOWN'` and `'EXIT'` messages.
* Errors as Erlang raises them: `badarith`, `badarg`, `{badmatch, V}`,
  `function_clause`, `{case_clause, V}`, `if_clause`, `{try_clause, V}`,
  `{badkey, K}`, `{badmap, M}`, `{badfun, F}`, `{badarity, ...}`,
  `undef`.
* The library, written in Erlang (`src/lib/*.erl`) and compiled with the
  program: `io` (`format`, `fwrite`, `put_chars`, `nl`, `write`,
  `get_line`), `io_lib` (`format` with every control sequence, `~p` laid
  out as OTP's pretty printer does, `write`, `print`), `lists`, `maps`,
  `string` (both the old and the new functions), `proplists`, `math`,
  `timer`, `unicode`, `re` (`run`, `replace`, `split` and `compile`, on
  MLWSVM 0.17.0's regular expressions), and the BIFs of `erlang`.

Not implemented: `maybe`, bit strings that are not whole bytes, bitwise
operations on BigInts, hot code loading, distribution (nodes), ports,
OTP's behaviours (`gen_server`, `supervisor`, ...), `ets`, and most of
the library beyond the modules above.

### Differences from Erlang/OTP

* Stack traces are empty lists: `catch` of an error gives
  `{'EXIT', {Reason, []}}`.
* Maps print with their keys in term order; OTP 26 and later print small
  maps in an internal order.
* `~p` does not shorten deep terms, and does not know records.
* Pids print as `<0.N.0>` with the VM's process number; a crash report
  (`=ERROR REPORT====`) goes to standard error, more briefly than OTP's.
* A process linked to one that fails is killed with reason `killed`,
  where Erlang passes the original reason on. Links made while trapping
  exits are one-way monitors: the trapping process is told of the other's
  end, but its own failure does not kill the other.
* When `main` returns, the program ends and any processes still running
  are stopped (as `erl -eval '...main(), init:stop().'` does).
* `re` patterns are RE2's syntax, which is Perl's without backreferences,
  lookaround, possessive quantifiers and recursion, and match in time
  linear in the subject. Named groups cannot be captured by name, a
  replacement cannot be a fun, and `{return, iodata}` gives a binary. A
  subject or pattern is matched as UTF-8 text with or without `unicode`.
  A program that uses `re` needs MLWSVM 0.17.0; others still run on
  0.6.0.

## How it works

`src/lex.c` turns source into tokens, `src/parse.c` preprocesses them and
parses each module, and `src/gen.c` writes assembly. The runtime is
`src/runtime.masm`; the library modules are compiled from Erlang like
the program, and only the functions the program reaches are written out.

### Values

A term is a pair of registers, `Ik` and `Rk`:

| `Rk` | The term |
|---|---|
| `null` | a small integer, in `Ik` |
| `1d` | a float, its bits in `Ik` |
| `0d` | `[]` |
| a string | an atom (an atom literal is one VM-wide object, so `beq.r` compares atoms) |
| a BigInt | an integer that does not fit in 64 bits |
| a list | a tuple: two words per element, `R` then `I` |
| a `Cons` struct | a list cell (head and tail, each two words) |
| a `Map`, `Bin`, `Fun`, `Pid` or `Ref` struct | a map, binary, fun, pid or reference |

`Ik` is 0 when `Rk` is an object, so two terms with the same `Rk` and
`Ik` are equal, and `=:=` and pattern matches only call the runtime when
the `R`s differ. Arithmetic on two small integers is done inline, with an
overflow check that goes to BigInts; arithmetic on two floats is inline
too, first when an operand is likely a float (a float literal, `/`,
`math:`, or a variable bound to one of those).

Maps of up to 32 keys keep a sorted list of keys and values; larger ones
a persistent red-black tree (`src/lib/erts_map.erl`), as BEAM has a flat
map and a hash trie. Binaries are a byte buffer with an offset and a
length, so matching `<<X, Rest/binary>>` shares the bytes.

### Functions

`m:f/N` is `.func E$m$f$N(i64 x N, ref x N) -> (i64, ref)`. The arguments
are the first slots (pairs of registers); a clause's variables get the
next ones (a head pattern that is a new variable uses its argument's own
slot), then temporaries. Variables whose live ranges do not overlap share
a slot (a variable lives at least to the end of the expression that
mentions it, and through a whole `receive`), and once a clause's body
starts, its arguments' slots are free for the body's variables. An
operation, tuple or call takes its result's slot before its operands are
evaluated, so their temporaries are free again once it is done. Slots
past 29 live on the data stack. Calls in
tail position are `tcall`s, so loops and server processes run in constant
space; other calls recurse on the VM's frame stack, which the driver
allows to grow to 50 million frames. A fun is a `Fun` struct holding its
arity and a closure whose captures are the variables it uses.

### Processes and messages

A message travels as a two-element list (its `R` and `I`). `receive`
looks first at the messages an earlier `receive` left (each process keeps
them in a list), then waits with `recv` or `recvt` until a message
matches or the timeout passes. A new message joins that list only if the
`receive` does not take it. A process runs its fun inside a handler;
if the fun fails, the process ends with an `Error` whose message is the
exit reason written as a term, which the VM hands to the monitors in its
`Down`, where the runtime reads it back.

## Performance

`bench/run.sh` (`make bench`) times five programs on MLWSVM and on
Erlang/OTP 29 (whose BEAM compiles to machine code, and whose time includes
starting it, about 0.07 s), on an Apple M5 Pro, in seconds, the best of
three runs, on MLWSVM 0.15.0. MLWSVM is run interpreting, and with its JIT
(MLWSVM 0.8.0 and later, `MLWSVM_JIT=on`):

| Benchmark | MLWSVM | MLWSVM JIT | erl |
|---|---|---|---|
| fib (fib(30)) | 0.03 | 0.01 | 0.08 |
| ring (1,000 processes, 1,000,000 messages) | 0.22 | 0.20 | 0.26 |
| binarytrees (depth 16) | 0.54 | 0.18 | 0.12 |
| nbody (100,000 steps, floats in tuples) | 0.46 | 0.17 | 0.16 |
| sorting (200,000 integers, maps) | 0.35 | 0.20 | 0.11 |

These need MLWSVM 0.7.0 or later, whose fast forms for `bitcast`,
`typeof` and comparisons of list elements came from these programs'
profiles. Calls and integer arithmetic are as fast as BEAM's; tuples and
allocation are where the time goes. With the JIT, `--jit-report -` shows
what its machine code could not run itself: here, only `send` and `recv`.

## Tests

`tests/lang` and `tests/programs` hold modules with a `main/0`; each
`NAME.out` is what Erlang/OTP prints for it, made by `tests/expect.sh`
(which needs `erlc` and `erl`). A module without `main/0` is a helper that
a test calls. The tests pass with `MLWSVM_GC_STRESS=1` and `MLWSVM_OPT=3`, and with
MLWSVM 0.9.0's `MLWSVM_GC_VERIFY=1`.
