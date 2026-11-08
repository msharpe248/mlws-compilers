# Compilers for MLWSVM

Compilers from other languages to [MLWSVM](../mlws-vm) assembly (`.masm`).
Each compiler lives in its own directory:

| Directory | Language | Compiler | Run a program | Export the assembly |
|---|---|---|---|---|
| [`bf/`](bf/README.md) | brainfuck | `bf/mlws-vm-bfc` | `bf/mlws-vm-bf program.b` | `bf/mlws-vm-bf -S program.b` |
| [`lox/`](lox/README.md) | [Lox](https://craftinginterpreters.com/the-lox-language.html) | `lox/mlws-vm-loxc` | `lox/mlws-vm-lox program.lox` | `lox/mlws-vm-lox -S program.lox` |
| [`lua/`](lua/README.md) | [Lua 5.5](https://www.lua.org/manual/5.5/) | `lua/mlws-vm-luac` | `lua/mlws-vm-lua program.lua` | `lua/mlws-vm-lua -S program.lua` |
| [`erlang/`](erlang/README.md) | [Erlang](https://www.erlang.org/doc/system/reference_manual.html) | `erlang/mlws-vm-erlc` | `erlang/mlws-vm-erl program.erl` | `erlang/mlws-vm-erl -S program.erl` |

The compilers are written in C11 with no dependencies beyond a C compiler and
`make`. They emit `.masm` text, which `mlwsvm run` assembles and runs directly,
as [TARGETING.md](../mlws-vm/docs/TARGETING.md) recommends.

## Exporting the assembly

Every compiler writes a single, self-contained `.masm` file: the compiled
program together with any runtime support it needs, with no imports. It can
be run, inspected, assembled to a binary module or debugged with the usual
MLWSVM tools. Each driver script takes the same options:

```sh
lox/mlws-vm-lox -S program.lox              # write program.masm next to the source; don't run
lox/mlws-vm-lox -S -o out.masm program.lox  # ... to out.masm
lox/mlws-vm-lox -S -o - program.lox         # ... to standard output
lox/mlws-vm-lox -k program.lox              # run it, and keep program.masm
lox/mlws-vm-lox -o out.masm program.lox     # run it, and keep out.masm
lox/mlws-vm-lox -b app program.lox          # write one executable, app; don't run
```

(and the same for `bf/mlws-vm-bf`, `lua/mlws-vm-lua` and `erlang/mlws-vm-erl`). `-S` needs no VM.
`-b` (`mlwsvm bundle`) writes a copy of `mlwsvm` with the
program after it, optimized at `--opt 3` and with the JIT on, so it runs
anywhere the same kind of machine runs, with no MLWSVM installed. On macOS,
don't give it a name ending in `.app`: macOS kills any plain executable
with such a name as it starts (`Killed: 9`).
For Lox, Lua and Erlang, `-g` names the program's local variables for
MLWSVM's debuggers, which then show them in the language's own terms,
and `-d` runs the program in `mlwsvm debug` with them:

```sh
lua/mlws-vm-lua -d program.lua              # (mdb) break program.lua:12, run, locals
```

The compilers themselves take `-o FILE`, defaulting to `program.masm` next
to the source: `lox/mlws-vm-loxc program.lox`, `lua/mlws-vm-luac
program.lua` and `bf/mlws-vm-bfc program.b` only compile.

## Versions

Each compiler has its own version, in its `VERSION` file (`bf/VERSION`,
`lox/VERSION`, `lua/VERSION`, `erlang/VERSION`), printed by `--version` (on the compiler and
its driver) and written into the header of every `.masm` file it produces.
The compilers are released together as releases of this repository, tagged
`vX.Y.Z` (the top-level `VERSION`); a compiler's own versions are tagged
`bf-vX.Y.Z`, `lox-vX.Y.Z`, `lua-vX.Y.Z` and `erlang-vX.Y.Z`. [CHANGELOG.md](CHANGELOG.md)
says what changed in each.

| Release | bf | Lox | Lua | Erlang |
|---|---|---|---|---|
| 0.1.0 | 0.1.0 | 0.1.0 | 0.1.0 | |
| 0.2.0 | 0.2.0 | 0.2.0 | 0.2.0 | |
| 0.3.0 | 0.3.0 | 0.3.0 | 0.3.0 | |
| 0.4.1 | 0.4.1 | 0.4.1 | 0.4.1 | |
| 0.4.2 | 0.4.1 | 0.4.1 | 0.4.2 | |
| 0.5.0 | 0.4.1 | 0.4.1 | 0.4.2 | 0.1.0 |
| 0.6.0 | 0.4.1 | 0.5.0 | 0.4.3 | 0.2.0 |
| 0.6.1 | 0.4.1 | 0.5.0 | 0.4.3 | 0.2.1 |
| 0.7.0 | 0.4.1 | 0.5.0 | 0.4.4 | 0.2.2 |
| 0.7.1 | 0.4.1 | 0.5.1 | 0.4.5 | 0.2.3 |
| 0.8.0 | 0.5.0 | 0.6.0 | 0.5.0 | 0.3.0 |

## Naming

Every executable is named `mlws-vm-LANG` (the driver: compile and run, or
export the assembly with `-S`) and `mlws-vm-LANGc` (the compiler alone), so
they never clash with a language's own tools (`lua`, `luac`, ...) on the
`PATH`.

```sh
mlwsvm run program.masm                     # run the exported assembly
mlwsvm asm program.masm -o program.mlwso    # assemble it to a binary module
mlwsvm run program.mlwso
mlwsvm debug program.masm                   # or debug, profile, trace, disassemble it
```

## Building and testing

```sh
make            # builds bf/mlws-vm-bfc, lox/mlws-vm-loxc, lua/mlws-vm-luac and erlang/mlws-vm-erlc
make test       # runs every test suite (needs python3)
make -C lox bench   # the Lox benchmarks
make -C lua bench   # the Lua benchmarks (with the reference lua's times, if installed)
make -C erlang bench   # the Erlang benchmarks (with erl's times, if installed)
```

The test runner is `tools/run-tests.py`. The Lox tests include the
complete test suite from *Crafting Interpreters*
(`lox/tests/craftinginterpreters`, MIT licensed), which mlws-vm-loxc passes
in full (241 tests, skipping only the scanner/parser-chapter and clox-limit
tests that jlox also skips). The Lua tests' expected output comes from the
reference `lua` 5.5 (`lua/tests/expect.sh`), so running them needs no Lua;
the Erlang tests' from Erlang/OTP (`erlang/tests/expect.sh`).

## Finding the VM

The compilers target the latest MLWSVM release (now 0.21.0): what they
write says so (`.requires mlwsvm, ">=0.21.0"`), and older VMs are not
supported.

The driver scripts (`bf/mlws-vm-bf`, `lox/mlws-vm-lox`, `lua/mlws-vm-lua`, `erlang/mlws-vm-erl`) and the test runner
use the `mlwsvm` executable given by `$MLWSVM`, else `mlwsvm` on the `PATH`, else
`../mlws-vm/mlwsvm` next to this repository.

## Optimizing

MLWSVM has an optimizer, off by default, which works on any
of these compilers' output: `mlwsvm run --opt 3 program.masm`, or
`MLWSVM_OPT=3` in the environment, which the driver scripts and the test
runner pass on (`MLWSVM_OPT=3 lua/mlws-vm-lua program.lua`,
`MLWSVM_OPT=3 make test`). Every module is optimized once as it loads, and the
result is kept in the module cache. Every test suite passes at `-O3`. Do not
add the `tailcall` pass (`--opt 3,tailcall`, or `all`): it removes the frames
Lua counts to find an error's position. See the VM's
[OPTIMIZER.md](../mlws-vm/docs/OPTIMIZER.md).

## Performance

The Lua and Erlang benchmarks (`make -C lua bench`, `make -C erlang bench`),
in seconds, the best of three runs, on an Apple M5 Pro with MLWSVM 0.15.0.
MLWSVM is run interpreting, and with its JIT (`mlwsvm run
--jit on`, or `MLWSVM_JIT=on` in the environment, which works
with the driver scripts and `make test` too). The references are the
interpreter `lua` 5.5.1, and Erlang/OTP 29.1.2, whose BEAM compiles to
machine code (its times include starting it, about 0.07 s):

| Lua | MLWSVM | MLWSVM JIT | lua 5.5 |
|---|---|---|---|
| fib (32) | 0.05 | 0.02 | 0.05 |
| loop (30,000,000 iterations) | 0.18 | 0.04 | 0.21 |
| methods (2M method calls and objects) | 0.52 | 0.20 | 0.34 |
| strings (format, gmatch, gsub) | 0.18 | 0.09 | 0.13 |
| iterate (pairs, ipairs) | 0.06 | 0.03 | 0.05 |
| binarytrees (15) | 0.32 | 0.15 | 0.37 |
| nbody (500,000) | 0.83 | 0.34 | 0.39 |
| spectralnorm (500) | 0.22 | 0.08 | 0.18 |
| fannkuch (10) | 1.73 | 0.57 | 0.79 |

| Erlang | MLWSVM | MLWSVM JIT | erl (JIT) |
|---|---|---|---|
| fib (30) | 0.03 | 0.01 | 0.08 |
| ring (1,000 processes, 1,000,000 messages) | 0.22 | 0.20 | 0.26 |
| binarytrees (16) | 0.54 | 0.18 | 0.12 |
| nbody (100,000 steps) | 0.46 | 0.17 | 0.16 |
| sorting (200,000 integers, maps) | 0.35 | 0.20 | 0.11 |

The Lox benchmarks, with and without the JIT, are in
[lox/README.md](lox/README.md#performance); there is no reference Lox
to compare with.

Every test suite passes with the JIT, and prints the same as without it,
and with the generational collector checked against a full heap walk at
every minor collection (`MLWSVM_GC_VERIFY=1`).
The JIT does least for code that spends its time in the runtime: string
operations, and sending and receiving messages.

## Layout

```
bf/        the brainfuck compiler (one C file), tests and an example
lox/       the Lox compiler: src/ (scanner, parser, resolver, code generator,
           and runtime.masm, which is compiled into mlws-vm-loxc), tests/
lua/       the Lua 5.5 compiler: src/ (lexer, parser, code generator; runtime.masm,
           lib.masm and match.masm, the runtime and library in assembly; and
           prelude.lua, the library written in Lua), tests/, bench/
erlang/    the Erlang compiler: src/ (lexer, preprocessor and parser, code generator;
           runtime.masm, the runtime in assembly; lib/, the library in Erlang),
           tests/, bench/
tools/     the test runners and the VM locator shared by the driver scripts
```

## License

MIT; see [LICENSE](LICENSE). The test suite in
`lox/tests/craftinginterpreters` comes from
[munificent/craftinginterpreters](https://github.com/munificent/craftinginterpreters)
and keeps its own MIT license (`lox/tests/craftinginterpreters/LICENSE`).
