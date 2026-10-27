# Compilers for MLWSVM

Compilers from other languages to [MLWSVM](../mlws-vm) assembly (`.masm`).
Each compiler lives in its own directory:

| Directory | Language | Compiler | Run a program |
|---|---|---|---|
| [`bf/`](bf/README.md) | brainfuck | `bf/bfc` | `bf/bf program.b` |
| [`lox/`](lox/README.md) | [Lox](https://craftinginterpreters.com/the-lox-language.html) | `lox/loxc` | `lox/lox program.lox` |

Both compilers are written in C11 with no dependencies beyond a C compiler and
`make`. They emit `.masm` text, which `mlwsvm run` assembles and runs directly,
as [TARGETING.md](../mlws-vm/docs/TARGETING.md) recommends.

## Building and testing

```sh
make            # builds bf/bfc and lox/loxc
make test       # runs both test suites (needs python3)
make -C lox bench   # the Lox benchmarks
```

The test runner is `tools/run-tests.py`. The Lox tests include the
complete test suite from *Crafting Interpreters*
(`lox/tests/craftinginterpreters`, MIT licensed), which loxc passes in full
(241 tests, skipping only the scanner/parser-chapter and clox-limit tests that
jlox also skips).

## Finding the VM

The driver scripts (`bf/bf`, `lox/lox`) and the test runner use the `mlwsvm`
executable given by `$MLWSVM`, else `mlwsvm` on the `PATH`, else
`../mlws-vm/mlwsvm` next to this repository.

## Layout

```
bf/        the brainfuck compiler (one C file), tests and an example
lox/       the Lox compiler: src/ (scanner, parser, resolver, code generator,
           and runtime.masm, which is compiled into loxc), tests/
tools/     the test runner and the VM locator shared by the driver scripts
```
