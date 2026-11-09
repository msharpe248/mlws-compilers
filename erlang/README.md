# Erlang for MLWSVM: mlws-vm-erl

`mlws-vm-erlc` compiles Erlang to MLWSVM assembly. Erlang's processes are
MLWSVM processes, its messages MLWSVM messages, and its monitors and links
the VM's, so this is the compiler that puts the VM's concurrency to work.

```sh
make                                   # builds mlws-vm-erlc
./mlws-vm-erl program.erl [args...]    # compile and run
./mlws-vm-erl -S program.erl           # write program.masm, don't run
./mlws-vm-erl -b app program.erl       # write one executable, app
./mlws-vm-erl -d program.erl           # run it in the debugger, variables named
./mlws-vm-erl --modules out program.erl  # each module a file of its own in out/,
                                       # for hot code loading
./mlws-vm-erl --env app.env program.erl  # application:get_env from app.env
./mlws-vm-erlc -o out.masm program.erl # the compiler alone
make test                              # the tests (needs python3)
make bench                             # the benchmarks, against erl if installed
```

A program is a module with `main/0`, or `main/1`, which gets the
command-line arguments as a list of strings. Other modules it calls are
compiled with it when they are found as `NAME.erl` next to it, or can be
named on the compiler's command line. The output is one self-contained
`.masm` file holding the program, the runtime and the part of the library
that the program uses.

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
Bin (I2, R2) = <<"bin">>
M (I3, R3) = #{a => 1}
```

A variable is named (`.local X, I1, R1`) from where it is bound, in the
hot code; after a `case`, `receive` or `try`, only the variables every
clause binds keep their names. The formatter, `.debug_format rt$show`,
calls `io_lib:print/1` in a process of its own, on copies. Variables on the
stack (beyond the 29th slot) are not named, and the optimizer drops the
names of a function it changes, so debug code that is not optimized.
`-d` allows as many frames as a run does (`mlwsvm debug --max-frames`).

## Hot code loading

With `--modules DIR` (on `mlws-vm-erlc` and the driver), each module of
the program is written to DIR as an MLWSVM module of its own, `M.masm`,
with the runtime and the whole library beside them in `erlrt.masm`, and
the driver runs the program from there. A new version of a module can then
replace the old one while the program runs, as in Erlang:

```sh
./mlws-vm-erl --modules out server.erl &   # runs out/server.masm
./mlws-vm-erlc --modules out counter.erl   # a new out/counter.masm
                                           # ... and the program calls code:load_file(counter)
```

* A call to another of the program's modules, and a fully qualified call
  to its own (`?MODULE:loop(State)`), goes to the module's current
  version: a server loop that calls itself so moves to the new code at its
  next message, while one that calls itself locally stays on the old. The
  calls go through MLWSVM's `module.handle`, kept for each process,
  so modules may call each other both ways, and a module is loaded the
  first time it is called.
* `code:load_file/1` loads the new version from its file
  (`module.reload`), and `code:load_binary/3` from its assembly
  (`module.reload_text`), as `--modules` writes it; `code:purge/1` frees
  the old version, killing the processes still running it
  (`module.purge_kill`), and `code:soft_purge/1` frees it only if none is
  (`module.purge`).
* A new version keeps none of the old one's globals (`.reload fresh`): an
  Erlang module has no state of its own, only its processes do.
* A program compiled to one file has no modules to replace:
  `code:load_file` says `{error, embedded}`.

`tests/modules` has such programs: `hot` replaces a running server's
module and purges a process left in the old code.

## Distribution

A program compiled with `--modules` can be a node, and its processes talk
to processes on other nodes as in Erlang. Nodes find each other through
`mlwsvm names`, which must be running on each machine (port 4368, or
`$MLWSVM_NAMES_PORT`), and every node must run the same `erlrt.masm`
(the same `mlws-vm-erlc`), since terms cross as its structs and funs as
its closures:

```sh
mlwsvm names &                            # once per machine
./mlws-vm-erl --modules out b.erl &       # net_kernel:start(b@localhost, #{})
./mlws-vm-erl --modules out a.erl         # ... rpc:call(b@localhost, b, square, [7])
```

* `net_kernel:start/1,2` makes the VM the node `name@host` (MLWSVM's
  `std.node`, `start_named`, its number given by `mlwsvm names`); a
  registered `net_kernel` process keeps the cookie (`erlang:set_cookie`,
  or `~/.erlang.cookie`) and connects to other nodes when a process first
  sends to them. Pids are then the VM's global pids, which work with `!`,
  `link`, `monitor` and `exit` on any node, and print as `<N.x.0>`.
* `node/0,1`, `nodes/0`, `is_alive/0`, `{Name, Node} ! Msg`, `spawn/2,4`
  and `spawn_link` on another node (the fun, closure and all, runs there),
  `monitor_node/2` and `net_kernel:monitor_nodes/1` (`{nodeup, N}` and
  `{nodedown, N}`, from the VM's `NodeEvent`), `net_adm:ping/1`,
  `rpc:call/4,5` and `rpc:cast/4`. A process on a node that went is
  `noconnection`. `{Name, Node} ! Msg` goes one way, to the VM's pid for
  the name there (`node.name_pid`), which the other node delivers to
  whoever has the name when the message arrives, as Erlang does.
* `global:register_name/2`, `whereis_name/1`, `send/2`, `unregister_name/1`
  and `registered_names/0`, on the VM's names across the cluster: every
  node connected agrees to a registration, a name goes when its process or
  its node does, and when two nodes that each have a name for a different
  process connect, the lower pid keeps it and the other process is killed,
  as `global`'s default resolver does. Names are atoms, and the VM must be
  a node.
* `net_kernel:get_net_ticktime/0` and `set_net_ticktime/1`: a connection
  that hears nothing for the ticktime (60 s at first) is down, as in
  Erlang. `erlang:disconnect_node/1` closes one; a message to a node that
  was connected, to a pid or a name, connects to it again (the VM's
  auto-connect), so only the first contact goes through net_kernel.
* Atoms and `[]` are the same on every node (MLWSVM sends interned
  strings as interned, and numeric literals as literals), so they match
  and compare as they do on one; a module a message needs is loaded on
  the node that gets it. A term of any size or depth crosses, its shared
  parts still shared: a list of a million cells, a tuple nested 100,000
  deep.

`tests/dist` runs two nodes on one machine, with an `mlwsvm names` of its
own: messages, rpc, a fun spawned on the other node, big terms there and
back, a remote kill, global names, a disconnect and a reconnect, and
`monitor_node`.

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
  `timer`, `unicode`, `re` (`run`, `replace`, `split`, `compile` and
  `inspect`, on MLWSVM's regular expressions), `code` (above), `file`
  (`read_file`, `write_file`), `net_kernel`, `net_adm`, `rpc` and
  `global` (Distribution, above), `application`'s environment (below),
  and the BIFs of `erlang`.
* `application:get_env/2,3`, `set_env/1-4`, `unset_env/2,3` and
  `get_all_env/1`, on MLWSVM's environments: App's Key is the VM's value
  `App.Key`, which every process of the program sees at once, and a term
  set reads back as it was. `mlws-vm-erl --env FILE` fills it before the
  program starts, as Erlang's `-config` does, from lines of
  `app.key = JSON` (`${VAR}`: an OS environment variable): a string is an
  atom, an array a list, an object a map with atom keys, and `true`,
  `false` and `null` atoms, except that `true` and `false` alone read as 1
  and 0. Apps and keys are atoms.

Not implemented: `maybe`, bit strings that are not whole bytes, bitwise
operations on BigInts, ports,
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
  linear in the subject. Named groups (`(?<name>...)`, `(?P<name>...)`)
  are captured by name and with `all_names` (MLWSVM's `regex.names`), and `inspect(MP, namelist)` lists them; a replacement
  cannot be a fun, and `{return, iodata}` gives a binary. A subject or
  pattern is matched as UTF-8 text with or without `unicode`.

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

A message travels as the one value it is: an `i64` for a small integer,
an `f64` for a float, and its `R` for anything else, so sending one makes
nothing new. The receiver takes it into a list of one it keeps, which
holds the message's type, and `tagof` tells it which of the three it got
(and a `Down` or `NodeEvent` from the VM from a term). `receive`
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
three runs, on MLWSVM 0.22.0. MLWSVM is run interpreting, and with its JIT
(`MLWSVM_JIT=on`):

| Benchmark | MLWSVM | MLWSVM JIT | erl |
|---|---|---|---|
| fib (fib(30)) | 0.04 | 0.01 | 0.08 |
| ring (1,000 processes, 1,000,000 messages) | 0.13 | 0.11 | 0.25 |
| binarytrees (depth 16) | 0.56 | 0.19 | 0.12 |
| nbody (100,000 steps, floats in tuples) | 0.48 | 0.18 | 0.14 |
| sorting (200,000 integers, maps) | 0.37 | 0.21 | 0.11 |

MLWSVM's fast forms for `bitcast`, `typeof` and comparisons of list
elements came from these programs' profiles. Calls and integer arithmetic are as fast as BEAM's; tuples and
allocation are where the time goes. With the JIT, `--jit-report -` shows
what its machine code could not run itself: here, only `send` and `recv`.

## Tests

`tests/lang` and `tests/programs` hold modules with a `main/0`; each
`NAME.out` is what Erlang/OTP prints for it, made by `tests/expect.sh`
(which needs `erlc` and `erl`). A module without `main/0` is a helper that
a test calls. `tests/modules` (run by `tests/modules/run.sh`) holds
programs compiled with `--modules`, their new versions of modules in
`v2/`, and what each prints (`main.out`); `tests/dist` (run by
`tests/dist/run.sh`) two nodes, above. The tests pass with `MLWSVM_GC_STRESS=1` and `MLWSVM_OPT=3`, and with
`MLWSVM_GC_VERIFY=1`.
