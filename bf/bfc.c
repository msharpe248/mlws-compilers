/*
 * mlws-vm-bfc: a brainfuck compiler targeting MLWSVM assembly (.masm).
 *
 * The program is parsed into a small IR in which runs of + - < > are folded
 * into additions at offsets from the tape pointer, and common loop idioms are
 * replaced by straight-line code:
 *
 *   [-]  [+]           clear the cell
 *   [->+>++<<]         multiply-add: every touched cell gets cell0 * delta
 *   [>]  [<<]          scan for a zero cell
 *
 * Tape cells live in an i64 array and wrap lazily: arithmetic is done on the
 * full 64-bit value and the low 8 bits are taken only when a cell is tested,
 * printed or used as a multiplier. Since 2^64 is a multiple of 256 this is
 * exactly 8-bit wrapping arithmetic.
 */
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum op_kind {
    OP_ADD,    /* cell[off] += n */
    OP_MOVE,   /* ptr += n */
    OP_OUT,    /* putchar(cell[off]) */
    OP_IN,     /* cell[off] = getchar() */
    OP_CLEAR,  /* cell[off] = 0 */
    OP_MUL,    /* cell[off] += cell[0] * n  (part of a multiply loop; cell[0] cleared by a following OP_CLEAR) */
    OP_SCAN,   /* while (cell[0]) ptr += n */
    OP_LOOP,   /* while (cell[0]) { body } */
};

struct op {
    enum op_kind kind;
    long off;           /* cell offset from the pointer */
    long n;             /* amount, step or multiplier */
    int line;           /* source line, for comments */
    struct op *body;    /* OP_LOOP */
    int nbody;
};

struct oplist {
    struct op *v;
    int n, cap;
};

static const char *progname = "mlws-vm-bfc";
static const char *src_name;

static void die(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "%s: ", progname);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    exit(1);
}

static void push(struct oplist *l, struct op o) {
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 16;
        l->v = realloc(l->v, (size_t)l->cap * sizeof *l->v);
        if (!l->v) die("out of memory");
    }
    l->v[l->n++] = o;
}

/* ---------------------------------------------------------------- parsing */

static const char *src;
static size_t pos, len;
static int line = 1;

/* Parse until ']' (nested) or end of input (top level). Within a straight run
 * of + - < > the pointer movement is kept as a pending offset, so ops refer
 * to cells relative to where the pointer was at the start of the run. */
static struct oplist parse_block(int nested, int open_line) {
    struct oplist out = {0};
    long off = 0; /* pending pointer movement */

    for (;;) {
        if (pos >= len) {
            if (nested) {
                fprintf(stderr, "%s:%d: error: unmatched '['\n", src_name, open_line);
                exit(65);
            }
            break;
        }
        char c = src[pos++];
        switch (c) {
        case '\n': line++; break;
        case '+': case '-': {
            long n = (c == '+') ? 1 : -1;
            /* merge with a preceding ADD at the same offset */
            if (out.n && out.v[out.n - 1].kind == OP_ADD && out.v[out.n - 1].off == off) {
                out.v[out.n - 1].n += n;
                if (out.v[out.n - 1].n == 0) out.n--;
            } else {
                push(&out, (struct op){OP_ADD, off, n, line, NULL, 0});
            }
            break;
        }
        case '>': off++; break;
        case '<': off--; break;
        case '.': push(&out, (struct op){OP_OUT, off, 0, line, NULL, 0}); break;
        case ',': push(&out, (struct op){OP_IN, off, 0, line, NULL, 0}); break;
        case '[': {
            if (off) push(&out, (struct op){OP_MOVE, 0, off, line, NULL, 0});
            off = 0;
            int l = line;
            struct oplist body = parse_block(1, l);
            push(&out, (struct op){OP_LOOP, 0, 0, l, body.v, body.n});
            break;
        }
        case ']':
            if (!nested) {
                fprintf(stderr, "%s:%d: error: unmatched ']'\n", src_name, line);
                exit(65);
            }
            if (off) push(&out, (struct op){OP_MOVE, 0, off, line, NULL, 0});
            return out;
        default: break; /* everything else is a comment */
        }
    }
    if (off) push(&out, (struct op){OP_MOVE, 0, off, line, NULL, 0});
    return out;
}

/* ----------------------------------------------------------- optimisation */

/* Replace recognised loop idioms, recursively. */
static void optimise(struct oplist *l) {
    struct oplist out = {0};
    for (int i = 0; i < l->n; i++) {
        struct op o = l->v[i];
        if (o.kind != OP_LOOP) { push(&out, o); continue; }

        struct oplist body = {o.body, o.nbody, o.nbody};
        optimise(&body);
        o.body = body.v;
        o.nbody = body.n;

        /* A loop that only moves the pointer: a scan. */
        if (o.nbody == 1 && o.body[0].kind == OP_MOVE) {
            push(&out, (struct op){OP_SCAN, 0, o.body[0].n, o.line, NULL, 0});
            continue;
        }

        /* A loop of additions only (no moves, I/O or inner loops) whose cell 0
         * changes by -1 or +1: a clear, or a multiply-add loop. */
        int simple = 1;
        long d0 = 0;
        for (int k = 0; k < o.nbody; k++) {
            if (o.body[k].kind != OP_ADD) { simple = 0; break; }
            if (o.body[k].off == 0) d0 += o.body[k].n;
        }
        if (simple && (d0 == -1 || d0 == 1)) {
            /* With d0 = -1 the loop runs v times; with +1 it runs (256 - v) % 256
             * times, i.e. -v mod 256, so the multiplier is negated. */
            for (int k = 0; k < o.nbody; k++) {
                if (o.body[k].off == 0) continue;
                long m = (d0 == -1) ? o.body[k].n : -o.body[k].n;
                push(&out, (struct op){OP_MUL, o.body[k].off, m, o.line, NULL, 0});
            }
            push(&out, (struct op){OP_CLEAR, 0, 0, o.line, NULL, 0});
            continue;
        }
        push(&out, o);
    }
    *l = out;
}

/* --------------------------------------------------------- code generation */

/*
 * Register use in main:
 *   R0  the tape (array of i64)
 *   R1  the output buffer
 *   I1  the tape pointer
 *   I2  a cell value, I3 an index, I4/I5 temporaries
 */
static FILE *out;
static int label_counter;
static int eof_mode; /* 0 unchanged, 1 zero, 2 minus one */

static void emit(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fputs("    ", out);
    vfprintf(out, fmt, ap);
    fputc('\n', out);
    va_end(ap);
}

/* The operand for cell[off]: R0[I1], or R0[I3] after computing the index. */
static const char *cell(long off) {
    if (off == 0) return "R0[I1]";
    emit("add I3, I1, %ld", off);
    return "R0[I3]";
}

static void gen_ops(struct op *ops, int n);

static void gen_op(struct op *o) {
    const char *c;
    int l;
    switch (o->kind) {
    case OP_ADD:
        c = cell(o->off);
        emit("mov I2, %s", c);
        emit("add I2, %ld", o->n);
        emit("mov %s, I2", c);
        break;
    case OP_MOVE:
        emit("add I1, %ld", o->n);
        break;
    case OP_OUT:
        c = cell(o->off);
        emit("mov I2, %s", c);
        emit("bufpush R1, I2");
        emit("len I4, R1");
        l = label_counter++;
        emit("blt I4, 8192, .Lo%d", l);
        emit("call flush (R1)");
        fprintf(out, ".Lo%d:\n", l);
        break;
    case OP_IN:
        emit("call flush (R1)");
        emit("call getbyte -> (I2)");
        c = cell(o->off);
        if (eof_mode == 0) {
            l = label_counter++;
            emit("blt I2, 0, .Li%d", l);
            emit("mov %s, I2", c);
            fprintf(out, ".Li%d:\n", l);
        } else {
            if (eof_mode == 1) emit("max I2, 0");
            emit("mov %s, I2", c);
        }
        break;
    case OP_CLEAR:
        emit("mov %s, 0", cell(o->off));
        break;
    case OP_MUL:
        /* Only reached as part of a run of MULs followed by a CLEAR; the run is
         * generated together in gen_ops. */
        abort();
    case OP_SCAN:
        l = label_counter++;
        fprintf(out, ".Ls%d:\n", l);
        emit("mov I2, R0[I1]");
        emit("and I2, 255");
        emit("bz I2, .Lse%d", l);
        emit("add I1, %ld", o->n);
        emit("jmp .Ls%d", l);
        fprintf(out, ".Lse%d:\n", l);
        break;
    case OP_LOOP:
        l = label_counter++;
        emit("mov I2, R0[I1]");
        emit("and I2, 255");
        emit("bz I2, .Le%d", l);
        fprintf(out, ".Lb%d:                ; [ line %d\n", l, o->line);
        gen_ops(o->body, o->nbody);
        emit("mov I2, R0[I1]");
        emit("and I2, 255");
        emit("bnz I2, .Lb%d", l);
        fprintf(out, ".Le%d:\n", l);
        break;
    }
}

static void gen_ops(struct op *ops, int n) {
    for (int i = 0; i < n; i++) {
        if (ops[i].kind != OP_MUL) { gen_op(&ops[i]); continue; }
        /* A multiply-add loop: MUL... CLEAR. Skip it all if cell 0 is zero, so
         * that cells the loop would never have reached are not touched. */
        int l = label_counter++;
        emit("mov I5, R0[I1]");
        emit("and I5, 255");
        emit("bz I5, .Lm%d", l);
        for (; i < n && ops[i].kind == OP_MUL; i++) {
            const char *c = cell(ops[i].off);
            emit("mov I2, %s", c);
            if (ops[i].n == 1) {
                emit("add I2, I5");
            } else if (ops[i].n == -1) {
                emit("sub I2, I5");
            } else {
                emit("mul I4, I5, %ld", ops[i].n);
                emit("add I2, I4");
            }
            emit("mov %s, I2", c);
        }
        emit("mov R0[I1], 0"); /* the CLEAR */
        fprintf(out, ".Lm%d:\n", l);
    }
}

static void gen_program(struct oplist *prog, long tape_size) {
    fprintf(out,
        "; Generated by mlws-vm-bfc from %s\n\n"
        "stdin:  .zero 8                 ; fs.stdin handle\n"
        "inbuf:  .zero 8                 ; input buffer (byte buffer)\n"
        "inpos:  .i64 0                  ; next byte in inbuf\n\n"
        "; Write and empty the output buffer.\n"
        ".func flush(ref buf)\n"
        "    len I0, buf\n"
        "    bz I0, 1f\n"
        "    ncall \"fs.stdout\" -> (R1)\n"
        "    ncall \"fs.write\" (R1, buf)\n"
        "    resize buf, 0\n"
        "1:  ret\n"
        ".endf\n\n"
        "; The next input byte, or -1 at the end of input.\n"
        ".func getbyte -> i64\n"
        "    mov R0, @inbuf\n"
        "    bz R0, 2f\n"
        "    len I1, R0\n"
        "    mov I0, @inpos\n"
        "    bge I0, I1, 2f\n"
        "1:  mov.u8 I2, R0[I0]\n"
        "    inc I0\n"
        "    mov @inpos, I0\n"
        "    ret I2\n"
        "2:  mov R1, @stdin\n"
        "    bnz R1, 3f\n"
        "    ncall \"fs.stdin\" -> (R1)\n"
        "    mov @stdin, R1\n"
        "3:  ncall \"fs.read\" (R1, 65536) -> (R0)\n"
        "    mov @inbuf, R0\n"
        "    mov I0, 0\n"
        "    len I1, R0\n"
        "    bnz I1, 1b\n"
        "    mov @inpos, 0\n"
        "    ret -1\n"
        ".endf\n\n"
        ".func main -> i64\n"
        "    anew.i64 R0, %ld\n"
        "    bufnew R1, 0\n"
        "    mov I1, 0\n"
        ".try fault\n",
        src_name, tape_size);
    gen_ops(prog->v, prog->n);
    fprintf(out,
        ".endtry\n"
        "    call flush (R1)\n"
        "    ret 0\n"
        "fault:\n"
        "    catch R5\n"
        "    call flush (R1)\n"
        "    ncall \"fs.stderr\" -> (R6)\n"
        "    mov R7, R5->Error.code\n"
        "    strcmp I6, R7, \"E_BOUNDS\"\n"
        "    bz I6, 1f\n"
        "    strcat R8, \"bf: runtime error: \", R7, \": \", R5->Error.message, \"\\n\"\n"
        "    jmp 2f\n"
        "1:  tostr R9, I1\n"
        "    strcat R8, \"bf: tape pointer out of range (cell \", R9, \"; the tape has %ld cells)\\n\"\n"
        "2:  ncall \"fs.write\" (R6, R8)\n"
        "    ret 70\n"
        ".endf\n",
        tape_size);
}

/* ------------------------------------------------------------------ main */

static void usage(void) {
    fprintf(stderr,
        "usage: %s [options] program.b\n"
        "  -o FILE        write the assembly to FILE (default: program.masm; '-' for stdout)\n"
        "  --tape N       number of tape cells (default 30000)\n"
        "  --eof MODE     what ',' does at end of input: unchanged (default), zero, minus1\n"
        "  -O0            disable optimisations\n",
        progname);
    exit(2);
}

static char *read_file(const char *path, size_t *n) {
    FILE *f = strcmp(path, "-") == 0 ? stdin : fopen(path, "rb");
    if (!f) die("cannot open %s: %s", path, strerror(errno));
    size_t cap = 65536, used = 0;
    char *buf = malloc(cap);
    size_t r;
    while ((r = fread(buf + used, 1, cap - used, f)) > 0) {
        used += r;
        if (used == cap) buf = realloc(buf, cap *= 2);
    }
    if (f != stdin) fclose(f);
    *n = used;
    return buf;
}

int main(int argc, char **argv) {
    const char *input = NULL, *output = NULL;
    long tape = 30000;
    int optimize = 1;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "-o") == 0 && i + 1 < argc) output = argv[++i];
        else if (strcmp(a, "--tape") == 0 && i + 1 < argc) {
            tape = strtol(argv[++i], NULL, 10);
            if (tape <= 0 || tape >= (1L << 31)) die("bad tape size");
        } else if (strcmp(a, "--eof") == 0 && i + 1 < argc) {
            const char *m = argv[++i];
            if (strcmp(m, "unchanged") == 0) eof_mode = 0;
            else if (strcmp(m, "zero") == 0 || strcmp(m, "0") == 0) eof_mode = 1;
            else if (strcmp(m, "minus1") == 0 || strcmp(m, "-1") == 0) eof_mode = 2;
            else die("bad --eof mode '%s'", m);
        } else if (strcmp(a, "-O0") == 0) optimize = 0;
        else if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) usage();
        else if (a[0] == '-' && a[1]) usage();
        else if (!input) input = a;
        else usage();
    }
    if (!input) usage();
    src_name = input;

    src = read_file(input, &len);
    struct oplist prog = parse_block(0, 0);
    if (optimize) optimise(&prog);

    char *outpath = NULL;
    if (!output) {
        const char *dot = strrchr(input, '.');
        const char *slash = strrchr(input, '/');
        size_t stem = (dot && (!slash || dot > slash)) ? (size_t)(dot - input) : strlen(input);
        outpath = malloc(stem + 6);
        memcpy(outpath, input, stem);
        strcpy(outpath + stem, ".masm");
        output = outpath;
    }
    out = strcmp(output, "-") == 0 ? stdout : fopen(output, "w");
    if (!out) die("cannot write %s: %s", output, strerror(errno));
    gen_program(&prog, tape);
    if (out != stdout && fclose(out) != 0) die("cannot write %s", output);
    return 0;
}
