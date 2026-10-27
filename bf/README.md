# bfc: brainfuck for MLWSVM

```sh
make
./bf tests/hello.b                      # compile and run
./bfc -o hello.masm tests/hello.b       # just compile
mlwsvm run hello.masm
```

```
bfc [options] program.b
  -o FILE        write the assembly to FILE (default: program.masm; '-' for stdout)
  --tape N       number of tape cells (default 30000)
  --eof MODE     what ',' does at end of input: unchanged (default), zero, minus1
  -O0            disable optimisations
```

`bf` accepts the same options before the program name.

## Semantics

* 8-bit cells that wrap, on a tape of 30,000 cells (`--tape`). Moving the
  pointer off either end is allowed, but touching a cell there stops the
  program with `bf: tape pointer out of range` and exit status 70.
* `,` reads a byte from standard input. At the end of input the cell is left
  unchanged by default, which suits most programs; `--eof zero` and
  `--eof minus1` give the other common conventions.
* `.` writes a byte. Output is buffered and flushed when the program ends,
  when the buffer reaches 8 KiB, and before every `,` (so prompts appear
  before input is read).
* Characters other than the eight commands are comments. An unmatched `[` or
  `]` is a compile error (exit status 65).

## How it compiles

The program is parsed into a small IR in which runs of `+ - < >` become
additions at offsets from the tape pointer, so `>>+++<<-` touches two cells
and does not move the pointer at all. Loops are then matched against common
idioms:

| Idiom | Becomes |
|---|---|
| `[-]`, `[+]` | clear the cell |
| `[->+>++<<]` (only additions, pointer balanced, cell 0 changes by ±1) | for each touched cell, `cell += cell0 * k`, then clear cell 0 |
| `[>]`, `[<<]` | a tight scan loop |

The tape is an `i64` array (`anew.i64`) in `R0` with the pointer in `I1`.
Cells are updated with 64-bit arithmetic in registers (`mov I2, R0[I1]`,
`add I2, 3`, `mov R0[I1], I2`), which takes the VM's fast paths, and are
reduced to 8 bits (`and I2, 255`) only when a cell is tested or printed.
Since 2^64 is a multiple of 256 this is exactly 8-bit wrapping arithmetic;
measured on MLWSVM it is about three times faster than byte-sized element
arithmetic.

## Tests

`make test` compiles and runs each `tests/NAME.b`, comparing its output with
`NAME.out` (with optional `NAME.in`, `NAME.opts`, `NAME.err` and `NAME.exit`;
see `tools/run-tests.py`). The optimiser was also checked against a
reference interpreter on about 900 random programs built from the idioms
above.
