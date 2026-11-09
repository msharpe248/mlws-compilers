# Changelog

Each compiler is versioned on its own (its `VERSION` file); a release of
this repository (`vX.Y.Z`, the top-level `VERSION`) collects them.

## Unreleased

### Repository

* The compilers target the latest MLWSVM release only, now 0.27.0: every
  compiler's output says `.requires mlwsvm, ">=0.27.0"`, and the drivers
  and tests no longer check the VM's version (`-g`, `-d` and `-b` are
  always tested, and Erlang's `-d` always passes `--max-frames`). The
  READMEs say what a feature uses, not which VM it first needs.

### Erlang

* `--modules DIR` (on `mlws-vm-erlc` and the driver): each module of the
  program is written to DIR as an MLWSVM module of its own, `M.masm`,
  importing `erlrt.masm`, the runtime and the whole library, which is
  written beside them. A call to another of the program's modules, and a
  fully qualified call to its own (`?MODULE:loop(S)`), goes through a
  `module.handle` (MLWSVM 0.18.1) kept for the process, which calls the
  module's current version (loading the module the first time), so modules
  may call each other both ways and a new version takes over as in
  Erlang: code already running stays on the old one. Each module says
  `.reload fresh`, so a new version starts its cached constants (funs of
  the old code among them) afresh, and atoms made at run time are
  `text.intern`'s, the same as every module's literals. A new process runs
  its fun from a frame that does not keep it, so `code:purge` does not
  kill a process that has moved to the new code. tests/modules has these
  programs, among them one whose new version drops a function that was
  called: calls to it are undef.
* Atoms made at run time are `text.intern`'s in every program, so the
  table of the program's atom literals is gone.
* `code`: `load_file/1` and `load_binary/3` (the module's assembly, as
  `--modules` writes it) on MLWSVM's `module.load`, `module.reload` and
  `module.reload_text`; `purge/1` and `soft_purge/1` on `module.purge_kill`
  and `module.purge`; `ensure_loaded/1`, `is_loaded/1`. A program compiled
  to one file has no module to replace: `load_file` says
  `{error, embedded}`.
* `file:read_file/1` and `file:write_file/2`, on `fs.read_file` and
  `fs.write_file`.
* Distribution, on MLWSVM's `std.node`, for programs compiled with
  `--modules`: `net_kernel:start/1,2` makes the VM a node, `name@host`,
  found through `mlwsvm names`; a registered `net_kernel` process keeps
  the cookie and connects to other nodes. Pids are the VM's global pids
  once it is a node. `node/0,1`, `nodes/0`, `is_alive/0`,
  `{Name, Node} ! Msg`, `spawn/2,4` and `spawn_link` on another node (the
  fun runs there in erlrt), `monitor_node/2` and
  `net_kernel:monitor_nodes/1` (the VM's `NodeEvent` as `nodeup` and
  `nodedown`), `net_adm:ping/1`, `rpc:call/4,5` and `cast/4`,
  `set_cookie` and `get_cookie`. A process on a node that went is
  `noconnection`, and a closure of code the other node does not have is
  `badfun`. Atoms and `[]` match across nodes, and any term crosses, of any
  size or depth, sharing kept (MLWSVM 0.22.0).
  tests/dist runs two nodes, with an `mlwsvm names` of its own, and sends
  terms of a million cells and 100,000 deep there and back.
* A message travels as the one value it is (an `i64`, an `f64`, or its
  `R`), not as a list of two made for each send, and the receiver tells
  which by `tagof` of the list of one it takes messages into: ring 0.22 s
  to 0.13 s (0.20 to 0.11 with the JIT). From the VM session's
  measurements. tests/lang/messages sends every kind of term.
* `{Name, Node} ! Msg` to another node sends to MLWSVM 0.24.0's pid for
  the name (`node.name_pid`), which the other node delivers by the name
  when the message arrives (or drops, if no one has it), as Erlang's
  REG_SEND: one way, about 480 ns a send (a pid: 270), where it was a
  `node.whereis` round trip and a call of net_kernel each time (80 us).
* `global`: `register_name/2,3`, `re_register_name/2,3`,
  `unregister_name/1`, `whereis_name/1`, `send/2` and
  `registered_names/0`, on MLWSVM 0.25.0's names across the cluster
  (`std.node`'s `register_global`): a registration is agreed by every node
  connected, a name goes with its process or its node, and of two
  processes with one name when nodes connect, the lower pid keeps it and
  the other is killed. Names are atoms.
* `net_kernel:get_net_ticktime/0` and `set_net_ticktime/1,2`, on the VM's
  ticks (a node that is silent for the ticktime is down), and
  `erlang:disconnect_node/1`. A message to a node that was connected, by
  `!` to a pid or to `{Name, Node}`, connects to it again (MLWSVM's
  auto-connect); only the first contact with a node goes through
  net_kernel. tests/dist registers names both ways, disconnects b and
  sends to it again.
* `application:get_env/2,3`, `set_env/1-4`, `unset_env/2,3` and
  `get_all_env/1`, on MLWSVM 0.26.0's environments (`env.*`): App's Key is
  the VM's value `App.Key`, shared by every process of the program, and a
  term set reads back as it was (an integer, a float and an atom as the
  VM's own values). `mlws-vm-erl --env FILE` (the VM's `run --env`) fills
  it from lines of `app.key = JSON`, in which a string is an atom, an array
  a list and an object a map with atom keys. tests/lang/appenv (against
  OTP) and appfile (with appfile.env, which the test runner passes as
  NAME.env).
  A value is read with MLWSVM 0.27.0's `env.get_any`, keeping its type,
  so a file's `true` and `false` are atoms wherever they are.
* Fixed: `monitor_node(N, true)` acted as `false` (its flag's register
  held the runtime's map), and the same kind of slip in `demonitor/2`'s
  `flush`, a `monitor` of a process that is gone, `code:load_binary`, and
  the undef of a call to a module that is not there.

## Release 0.8.0

Lox 0.6.0, Lua 0.5.0, Erlang 0.3.0 and bf 0.5.0. Every driver can bundle a
program into one executable (`-b`, MLWSVM 0.17.0); Lox, Lua and Erlang
name a program's variables for MLWSVM's debuggers (`-g`, and `-d` to run
in `mlwsvm debug`, MLWSVM 0.17.3); Lua has `__gc` and weak tables, and
Erlang the `re` module. Lua programs need MLWSVM 0.17.3 or later; Erlang
programs that use `re`, and the output of `-g`, too; Lox, bf and other
Erlang programs still run on MLWSVM 0.6.0 or later (tested on 0.6.1).
Every suite passes on MLWSVM 0.17.3 and 0.17.4 with the JIT off, on and
compiling everything, with `MLWSVM_OPT=3`, `MLWSVM_GC_STRESS=1` and
`MLWSVM_GC_VERIFY=1`. Speed on MLWSVM 0.17.4 is as 0.7.1's, but for
Lua's methods benchmark (0.53 s to 0.56 s interpreted; tables are a word
larger).

### Repository

* Every driver takes `-b FILE` (`--bundle FILE`): compile the program and
  write one executable, FILE, with MLWSVM 0.17.0's `mlwsvm bundle`, instead
  of running it. The program is optimized at `--opt 3` when it is bundled,
  so the bundle runs it at that level without the optimizer's start-up
  cost (spectralnorm: 0.08 s run with the JIT, 0.04 s bundled), and the JIT
  is on. tools/test-drivers.sh checks it on an MLWSVM that has `bundle`.
* The Lox, Lua and Erlang compilers and drivers take `-g`: the output names
  the program's local variables for MLWSVM's debuggers (`.local` of several
  registers, MLWSVM 0.17.3) and gives them a formatter (`.debug_format`)
  that shows a variable's registers in the language's own terms, so
  `locals` and `print X` in `mlwsvm debug`, and an editor's Locals through
  `mlwsvm dap`, show `x = 1.5`, `t = {1, 2, x = 3}` or `L = [{a,1}]`. The
  output of `-g` requires MLWSVM 0.17.3; without it nothing changes. The
  drivers' `-d` compiles with `-g` and runs the program in `mlwsvm debug`.
  tools/test-drivers.sh checks both on MLWSVM 0.17.3 or later.

### bf 0.5.0

* The driver takes `-b FILE` (above).

### Lox 0.6.0

* `-g`: parameters, `this` and locals are named where they are in scope
  (a block's end ends its names and shows a variable it hid again), shown
  by `rt$show` as `print` shows them, with strings in quotes and a captured
  variable's box opened.

### Lua 0.5.0

* `__gc`: a table whose metatable has a `__gc` field when `setmetatable`
  is called is finalized after a collection finds nothing holds it, and
  the ones left when the program ends are finalized then, the last marked
  first (MLWSVM 0.17.0's `gc.finalize` and weak maps). The program holds
  its finalizers (MLWSVM 0.17.3's `gc.hold_finalizers`), so `__gc` runs
  only when the runtime calls `gc.run_finalizers`: at `collectgarbage()`,
  at the next `setmetatable` that marks a table and at exit, where Lua
  code may run without breaking a table the runtime is in the middle of
  changing.
* Weak tables: `setmetatable` with a metatable whose `__mode` has `k`,
  `v` or both moves the table's entries into MLWSVM weak maps (one for
  references, made weak in that mode, and one for numbers, whose keys are
  weak with `k`), so a collection removes the entries whose weak key or
  value nothing else holds; weak keys are ephemerons, as in Lua.
  `setmetatable` with a metatable without `__mode` moves them back. The
  array and hash parts of a weak table stay empty, so the compiler's
  inline caches miss and its accesses go to the runtime; `next` and
  `pairs` walk a snapshot of the keys that holds objects by weak
  references. Tables are one word larger (`Tab.wk`).
* Programs say they need MLWSVM 0.17.3 (`.requires mlwsvm, ">=0.17.3"`),
  so an older VM says so instead of failing to link a native.
* `-g`: parameters, locals, loop variables and the upvalues a function with
  a loop copies to registers are named where they are in scope, shown by
  the runtime's `rt$show` as `tostring` shows them, with strings in quotes
  and a table by its first entries (`{1, 2, x = 3, ...}`); it calls no
  `__tostring`. With `--shared` the formatter is the library's.

### Erlang 0.3.0

* `re`: `run/2,3` (`global`, `{capture, Spec, Type}` with `all`,
  `all_but_first`, `first`, `none`, `all_names` or group numbers and
  names, and `index`, `list` or `binary`; `caseless`, `multiline`,
  `dotall`, `unicode`, `{offset, N}`), `replace/3,4` (`&`, `\N`, `\g{N}`;
  `global`, `{return, T}`), `split/2,3` (groups, `group`, `trim`, `{parts,
  N}`), `compile/1,2` and `inspect/2` (`namelist`), in Erlang on runtime
  functions over MLWSVM's `regex.find` and 0.17.3's `regex.names`. Only a
  program that uses `re` gets that part of the runtime, and needs MLWSVM
  0.17.3 (its `.requires` says so); others still need 0.6.0.
* `-g`: variables are named from where they are bound; after a `case`,
  `receive` or `try`, only those every clause binds keep their names. They
  are shown by `io_lib:print/1`, as `~p` prints them. On MLWSVM 0.17.4 and
  later, `-d` passes `--max-frames 50000000` to `mlwsvm debug`, as a run
  does, so deep recursion works under the debugger too.

## Release 0.7.1

Lua 0.4.5, Erlang 0.2.3 and Lox 0.5.1; bf 0.4.1 is unchanged. Smaller and
cheaper generated code, with no change to what programs do or to the
MLWSVM they need: Lua programs need MLWSVM 0.14.0 or later, and Erlang,
Lox and bf MLWSVM 0.6.0 or later (tested on 0.6.1). Every suite passes on
MLWSVM 0.15.0 with the JIT off, on and compiling everything, with
`MLWSVM_OPT=3`, `MLWSVM_GC_STRESS=1` and `MLWSVM_GC_VERIFY=1`. Numbers on
MLWSVM 0.15.0.

### Repository

* README, lua/README, lox/README, erlang/README: benchmark times with
  these compilers on MLWSVM 0.15.0.

### Lua 0.4.5

* A multiple assignment uses its targets' tables and keys where they are
  when they are locals that nothing in it can change (no closure captures
  them, and no target assigns them), instead of copying them first; the
  stores then use a loop's cached array part. fannkuch runs 12% fewer
  instructions: 1.99 to 1.73 s interpreted, 0.65 to 0.57 with the JIT.
* An integer `for` loop with a step of 1 or -1 ends in one branch back to
  its head instead of a branch, a step and a jump (spectralnorm: 3.5%
  fewer instructions).
* A `while` loop whose condition is small (no calls, functions or tables)
  tests it before the loop and at its end, one branch an iteration
  instead of two (loop: 7% fewer instructions). An error in the second
  test reports the `while`'s line.
* The runtime branches on a value's type in one instruction (`bisa`,
  `bnisa`) where it used `isa` and a branch.

### Erlang 0.2.3

* A tuple, list cell or constant term returned by a function is returned
  from the register it was built in, without moving it into the result's
  registers first: binarytrees runs 3.7% fewer instructions, sorting
  4.2%.

### Lox 0.5.1

* A function's scratch registers are the ones just above its slots
  instead of R24-R28, so its calls clear a smaller register window
  (`Tree.walk`: 12 words instead of 27). With the JIT, properties,
  method_call and zoo run 10% faster and trees 6%, and zoo_batch does
  13% more batches.
* Property gets, calls and invokes test an object's type and branch in
  one instruction (`bnisa`) instead of two.

## Release 0.7.0

Lua 0.4.4 and Erlang 0.2.2; Lox 0.5.0 and bf 0.4.1 are unchanged. Lua
programs now need MLWSVM 0.14.0 or later; Erlang, Lox and bf still run on
MLWSVM 0.6.0 or later (tested on 0.6.1). Every suite passes on MLWSVM
0.14.0 and 0.15.0 with the JIT off, on and compiling everything, with
`MLWSVM_OPT=3`, `MLWSVM_GC_STRESS=1` and `MLWSVM_GC_VERIFY=1`.

### Repository

* README, lua/README, lox/README, erlang/README: benchmark times on MLWSVM
  0.15.0, interpreting and with the JIT; with the JIT, all nine Lua
  benchmarks run faster than Lua 5.5.

### Lua 0.4.4

* `gsub` of a plain pattern by a plain string is one call to MLWSVM
  0.14.0's `text.replace_n`, instead of a loop in the runtime, and an
  empty pattern takes that path too. Lua programs now need MLWSVM 0.14.0
  or later.

### Erlang 0.2.2

* Map keys that are both small integers are compared inline, in the
  lookups of small and large maps and in the tree's inserts, without
  ranking the two terms.
* Replacing the value of a key that a large map (a tree) already has
  rebuilds the path to it without rebalancing, since the tree keeps its
  shape.
* `maps:update_with/4` looks the key up once, not twice, when it is there.
* A head pattern that binds the whole argument as well (`[X | Xs] = A`)
  uses the argument's own slot, as a plain variable does, instead of a
  copy; and a list cell built on a call's result is built where the
  result is. `lists:merge2`, which `lists:sort` spends its time in, runs
  21% fewer instructions.
* `bench/sorting.erl` (`lists:sort` and 200,000 `maps:update_with` of 200,000
  integers) runs in 0.36 s instead of 0.64 interpreting, and 0.22 s instead
  of 0.32 with the JIT, on MLWSVM 0.14.0.

## Release 0.6.1

Erlang 0.2.1; Lox 0.5.0, Lua 0.4.3 and bf 0.4.1 are unchanged. For MLWSVM
0.6.0 or later; tested on MLWSVM 0.6.1, 0.7.0, 0.8.0, 0.9.0 and 0.9.1, and
with the JIT, optimizer and generational collector of 0.9.x
(`MLWSVM_GC_VERIFY=1` checks it at every minor collection). Use MLWSVM
0.9.1 or later with the JIT: 0.9.0's can crash the VM while it compiles a
large function (seen with `MLWSVM_JIT=all` and `MLWSVM_OPT=3`, but any
large function it compiles could hit it).

### Repository

* README, lua/README, lox/README, erlang/README: benchmark times on MLWSVM
  0.9.0, interpreting and with the JIT.

### Erlang 0.2.1

* A new message stays in the `receive`'s registers until a clause takes
  it or none does, instead of in the process's mailbox struct: taking it
  stores nothing into a long-lived object (which MLWSVM 0.9.0's
  generational collector would have to remember) and calls no runtime
  function. The mailbox struct is just the saved messages and the scan
  position.

## Release 0.6.0

Erlang 0.2.0, Lox 0.5.0, Lua 0.4.3; bf stays at 0.4.1. For MLWSVM 0.6.0
or later; tested on MLWSVM 0.6.1, 0.7.0 and 0.8.0, with 0.8.0's JIT
(`MLWSVM_JIT=on`) on and off. The changes make faster code for the
interpreter and the JIT alike, steering clear of the instruction forms the
JIT cannot compile (`mlwsvm run --jit-report -`).

### Repository

* README: performance tables for Lua and Erlang, MLWSVM interpreting and
  with its JIT, against `lua` 5.5 and Erlang/OTP; lox/README has the Lox
  benchmarks.
* A stray `erl_crash.dump` is gone from the Erlang tests, and ignored.

### Erlang 0.2.0

* An operation, tuple or call takes its result's slot before its operands,
  so temporaries are free again at once: nbody's hot function no longer
  spills to the stack (0.78 s to 0.50 s; 0.23 s with the JIT).
* A received message joins the saved ones only if the `receive` does not
  take it; a process's mailbox state is a struct.
* A fun that captures nothing is made once; constant patterns on a list
  cell's fields are compared from a register; clauses in a row beginning
  with tuples of one size share the tuple test.

### Lox 0.5.0

* Instances keep their fields in shapes (field names to places, shared by
  instances whose fields were added in the same order), with an inline
  cache of two shapes at each field read and set: making an instance
  allocates no map. binary_trees 1.74 s to 0.85 s (0.54 s with the JIT).
* `super.m(...)` caches its method; a global read as an operand, and a
  global's "defined" check, go through a register; a comparison's value is
  made with a branch, not `sel`.

### Lua 0.4.3

* A library function's argument error names the function as Lua does (by
  its call site, `'rep'` or `calling 'rep' on bad self`, else its library
  name, `'string.rep'`) and says `got no value` for an argument not passed;
  `coroutine.resume` and `status` expect a `thread`, `rawlen` and
  `table.unpack` say what they got, and `ipairs` takes any value. The
  library made by `make lib` must be made again.
* `pairs` steps through a table's hash part inline; `t[i]` past the array
  part of a plain table is nil inline; `math.sqrt` is inline when it is the
  library's. iterate 0.09 s to 0.07 s, binarytrees 0.55 s to 0.47 s, nbody
  0.96 s to 0.85 s (0.41 s with the JIT, as fast as `lua`).
* `setmetatable`'s inline path no longer leaves a constant argument unset
  for the call it makes when the function has been replaced.

## Release 0.5.0

Erlang 0.1.0, new; bf 0.4.1, Lox 0.4.1 and Lua 0.4.2 are unchanged. For
MLWSVM 0.6.0 or later; tested on MLWSVM 0.6.1 and 0.7.0, whose fast forms for
`bitcast`, `typeof` and list-element compares (made from the Erlang
compiler's profiles) the Erlang benchmark times in erlang/README.md need.

### Repository

* README: running any compiler's output with MLWSVM's optimizer
  (`MLWSVM_OPT=3`, or `mlwsvm run --opt 3`), and why not to add the
  `tailcall` pass.
* `lua/Makefile`: the Lua library is made again when the VM changes, since
  its module format may have.

### Erlang 0.1.0 (new)

* `mlws-vm-erlc` and `mlws-vm-erl`: a compiler for Erlang, whose processes,
  messages, monitors and links are MLWSVM's. Modules, records, the
  preprocessor, pattern matching, funs, comprehensions, maps, binaries,
  BigInts, exceptions, selective `receive`, and a library (`io`, `io_lib`,
  `lists`, `maps`, `string`, ...) written in Erlang. See
  [erlang/README.md](erlang/README.md). Needs MLWSVM 0.6.0 or later.

## Release 0.4.2

Lua 0.4.2; bf and Lox stay at 0.4.1. For MLWSVM 0.4.1 or later.

### Lua 0.4.2

* `io.flush()` exists, and `file:flush()` writes out standard output's
  unfinished line (MLWSVM after 0.4.1 writes each process's output a whole
  line at a time); both return `true`, as lua does.
* `tools/run-tests.py` stops a timed-out test's VM along with its driver.

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
