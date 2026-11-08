/* mlws-vm-erlc: compile an Erlang program to MLWSVM assembly.
 *
 *   mlws-vm-erlc [-o FILE] program.erl [module.erl...]    (FILE '-' is standard output)
 *
 * The output is one self-contained MLWSVM assembly file: the program's
 * modules, the runtime and the part of the library the program uses.
 * The first module's main/0 (or main/1, given the command-line arguments
 * as strings) runs. Other modules are found as NAME.erl next to the
 * program. Errors in the program are reported as erlc does
 * ("program.erl:3: ..."), with exit status 1. */
#define _POSIX_C_SOURCE 200809L
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "erl.h"

void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) {
        fprintf(stderr, "mlws-vm-erlc: out of memory\n");
        exit(1);
    }
    return p;
}

void *xcalloc(size_t n, size_t size) {
    void *p = calloc(n ? n : 1, size ? size : 1);
    if (!p) {
        fprintf(stderr, "mlws-vm-erlc: out of memory\n");
        exit(1);
    }
    return p;
}

void *xrealloc(void *p, size_t n) {
    p = realloc(p, n ? n : 1);
    if (!p) {
        fprintf(stderr, "mlws-vm-erlc: out of memory\n");
        exit(1);
    }
    return p;
}

char *xstrdup(const char *s) {
    return xstrndup(s, strlen(s));
}

char *xstrndup(const char *s, size_t n) {
    char *p = xmalloc(n + 1);
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

char *xprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    char *s = xmalloc((size_t)n + 1);
    va_start(ap, fmt);
    vsnprintf(s, (size_t)n + 1, fmt, ap);
    va_end(ap);
    return s;
}

void fatal(const char *file, int line, const char *fmt, ...) {
    fprintf(stderr, "%s:%d: ", file, line);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

void buf_add(Buf *b, const char *s, size_t n) {
    if (b->n + n + 1 > b->cap) {
        b->cap = b->cap ? b->cap * 2 : 256;
        while (b->n + n + 1 > b->cap)
            b->cap *= 2;
        b->s = xrealloc(b->s, b->cap);
    }
    memcpy(b->s + b->n, s, n);
    b->n += n;
    b->s[b->n] = 0;
}

void buf_puts(Buf *b, const char *s) {
    buf_add(b, s, strlen(s));
}

void buf_printf(Buf *b, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    char *s = xmalloc((size_t)n + 1);
    va_start(ap, fmt);
    vsnprintf(s, (size_t)n + 1, fmt, ap);
    va_end(ap);
    buf_add(b, s, (size_t)n);
    free(s);
}

/* The intern table: a simple open-addressing hash set. */
static struct {
    char **v;
    size_t n, cap;
} names;

const char *intern(const char *s, size_t n) {
    if (names.n * 2 >= names.cap) {
        size_t cap = names.cap ? names.cap * 2 : 1024;
        char **v = xcalloc(cap, sizeof *v);
        for (size_t k = 0; k < names.cap; k++) {
            char *q = names.v[k];
            if (!q)
                continue;
            size_t h = 5381;
            for (const char *p = q; *p; p++)
                h = h * 33 + (unsigned char)*p;
            while (v[h & (cap - 1)])
                h++;
            v[h & (cap - 1)] = q;
        }
        free(names.v);
        names.v = v;
        names.cap = cap;
    }
    size_t h = 5381;
    for (size_t k = 0; k < n; k++)
        h = h * 33 + (unsigned char)s[k];
    for (;; h++) {
        char *q = names.v[h & (names.cap - 1)];
        if (!q)
            break;
        if (strlen(q) == n && memcmp(q, s, n) == 0)
            return q;
    }
    char *q = xstrndup(s, n);
    names.v[h & (names.cap - 1)] = q;
    names.n++;
    return q;
}

static char *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "mlws-vm-erlc: cannot open %s\n", path);
        exit(1);
    }
    size_t cap = 65536, n = 0, r;
    char *buf = xmalloc(cap);
    while ((r = fread(buf + n, 1, cap - n - 1, f)) > 0) {
        n += r;
        if (n + 1 == cap)
            buf = xrealloc(buf, cap *= 2);
    }
    fclose(f);
    buf[n] = 0;
    *len = n;
    return buf;
}

static void usage(void) {
    fprintf(stderr, "usage: mlws-vm-erlc [--version] [-g] [-o FILE | --modules DIR] program.erl [module.erl...]\n"
                    "  -g        name the variables for debuggers (needs MLWSVM 0.17.3)\n"
                    "  --modules DIR\n"
                    "            write each module to DIR as an MLWSVM module of its own, M.masm,\n"
                    "            with the runtime and library as erlrt.masm, so that code:load_file\n"
                    "            can replace one while the program runs (needs MLWSVM 0.18.0)\n");
    exit(64);
}

int main(int argc, char **argv) {
    const char *out = NULL, *moddir = NULL;
    VEC(const char *) files = {0};
    for (int k = 1; k < argc; k++) {
        if (!strcmp(argv[k], "--version")) {
            printf("mlws-vm-erlc %s\n", VERSION);
            return 0;
        } else if (!strcmp(argv[k], "-g")) {
            debug_info = true;
        } else if (!strcmp(argv[k], "-o")) {
            if (++k >= argc)
                usage();
            out = argv[k];
        } else if (!strcmp(argv[k], "--modules")) {
            if (++k >= argc)
                usage();
            moddir = argv[k];
        } else if (argv[k][0] == '-' && argv[k][1]) {
            usage();
        } else {
            vec_push(files, argv[k]);
        }
    }
    if (!files.n || (out && moddir))
        usage();
    Module **mods = xcalloc((size_t)files.n, sizeof *mods);
    for (int k = 0; k < files.n; k++) {
        size_t len;
        char *src = read_file(files.v[k], &len);
        mods[k] = parse_module(files.v[k], src, len);
    }
    if (moddir) {
        generate_modules(mods, files.n, moddir);
        return 0;
    }
    char *outpath = NULL;
    if (!out) {
        const char *f = files.v[0];
        size_t n = strlen(f);
        if (n > 4 && !strcmp(f + n - 4, ".erl"))
            n -= 4;
        outpath = xprintf("%.*s.masm", (int)n, f);
        out = outpath;
    }
    /* generate into memory first, so a compile error leaves no file */
    char *text = NULL;
    size_t textlen = 0;
    FILE *mem = open_memstream(&text, &textlen);
    generate(mods, files.n, mem, files.v[0]);
    fclose(mem);
    FILE *f = strcmp(out, "-") ? fopen(out, "w") : stdout;
    if (!f) {
        fprintf(stderr, "mlws-vm-erlc: cannot write %s\n", out);
        return 1;
    }
    fwrite(text, 1, textlen, f);
    if (f != stdout)
        fclose(f);
    return 0;
}
