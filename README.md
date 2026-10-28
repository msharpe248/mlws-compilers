# Compilers for MLWSVM

Compilers from other languages to [MLWSVM](../mlws-vm) assembly (`.masm`).
Each compiler lives in its own directory:

| Directory | Language | Compiler | Run a program | Export the assembly |
|---|---|---|---|---|
| [`bf/`](bf/README.md) | brainfuck | `bf/mlws-vm-bfc` | `bf/mlws-vm-bf program.b` | `bf/mlws-vm-bf -S program.b` |
| [`lox/`](lox/README.md) | [Lox](https://craftinginterpreters.com/the-lox-language.html) | `lox/mlws-vm-loxc` | `lox/mlws-vm-lox program.lox` | `lox/mlws-vm-lox -S program.lox` |
| [`lua/`](lua/README.md) | [Lua 5.5](https://www.lua.org/manual/5.5/) | `lua/mlws-vm-luac` | `lua/mlws-vm-lua program.lua` | `lua/mlws-vm-lua -S program.lua` |

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
```

(and the same for `bf/mlws-vm-bf` and `lua/mlws-vm-lua`). `-S` needs no VM.
The compilers themselves take `-o FILE`, defaulting to `program.masm` next
to the source: `lox/mlws-vm-loxc program.lox`, `lua/mlws-vm-luac
program.lua` and `bf/mlws-vm-bfc program.b` only compile.

## Versions

Each compiler has its own version, in its `VERSION` file (`bf/VERSION`,
`lox/VERSION`, `lua/VERSION`), printed by `--version` (on the compiler and
its driver) and written into the header of every `.masm` file it produces.
The compilers are released together as releases of this repository, tagged
`vX.Y.Z` (the top-level `VERSION`); a compiler's own versions are tagged
`bf-vX.Y.Z`, `lox-vX.Y.Z` and `lua-vX.Y.Z`. [CHANGELOG.md](CHANGELOG.md)
says what changed in each.

| Release | bf | Lox | Lua |
|---|---|---|---|
| 0.1.0 | 0.1.0 | 0.1.0 | 0.1.0 |
| 0.2.0 | 0.2.0 | 0.2.0 | 0.2.0 |
| 0.3.0 | 0.3.0 | 0.3.0 | 0.3.0 |

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
make            # builds bf/mlws-vm-bfc, lox/mlws-vm-loxc and lua/mlws-vm-luac
make test       # runs every test suite (needs python3)
make -C lox bench   # the Lox benchmarks
make -C lua bench   # the Lua benchmarks (with the reference lua's times, if installed)
```

The test runner is `tools/run-tests.py`. The Lox tests include the
complete test suite from *Crafting Interpreters*
(`lox/tests/craftinginterpreters`, MIT licensed), which mlws-vm-loxc passes
in full (241 tests, skipping only the scanner/parser-chapter and clox-limit
tests that jlox also skips). The Lua tests' expected output comes from the
reference `lua` 5.5 (`lua/tests/expect.sh`), so running them needs no Lua.

## Finding the VM

The driver scripts (`bf/mlws-vm-bf`, `lox/mlws-vm-lox`, `lua/mlws-vm-lua`) and the test runner
use the `mlwsvm` executable given by `$MLWSVM`, else `mlwsvm` on the `PATH`, else
`../mlws-vm/mlwsvm` next to this repository.

## Layout

```
bf/        the brainfuck compiler (one C file), tests and an example
lox/       the Lox compiler: src/ (scanner, parser, resolver, code generator,
           and runtime.masm, which is compiled into mlws-vm-loxc), tests/
lua/       the Lua 5.5 compiler: src/ (lexer, parser, code generator; runtime.masm,
           lib.masm and match.masm, the runtime and library in assembly; and
           prelude.lua, the library written in Lua), tests/, bench/
tools/     the test runners and the VM locator shared by the driver scripts
```

## License

MIT; see [LICENSE](LICENSE). The test suite in
`lox/tests/craftinginterpreters` comes from
[munificent/craftinginterpreters](https://github.com/munificent/craftinginterpreters)
and keeps its own MIT license (`lox/tests/craftinginterpreters/LICENSE`).
