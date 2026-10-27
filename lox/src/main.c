/* mlws-vm-loxc: compile a Lox program to MLWSVM assembly.
 *
 *   mlws-vm-loxc [-o FILE] program.lox     (FILE '-' is standard output)
 *
 * The output is one self-contained MLWSVM assembly file, which `mlwsvm run`
 * runs and `mlwsvm asm` turns into a binary module. Compile errors are
 * reported in jlox's format and exit with status 65. */
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "lox.h"

extern const char lox_runtime[];

bool had_error;
const char *source_path;

void error_at(Token *tok, const char *msg) {
    if (tok->type == T_EOF)
        fprintf(stderr, "[line %d] Error at end: %s\n", tok->line, msg);
    else
        fprintf(stderr, "[line %d] Error at '%.*s': %s\n", tok->line, tok->length, tok->start, msg);
    had_error = true;
}

void error_line(int line, const char *msg) {
    fprintf(stderr, "[line %d] Error: %s\n", line, msg);
    had_error = true;
}

void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) {
        fprintf(stderr, "mlws-vm-loxc: out of memory\n");
        exit(70);
    }
    return p;
}

void *xrealloc(void *p, size_t n) {
    p = realloc(p, n ? n : 1);
    if (!p) {
        fprintf(stderr, "mlws-vm-loxc: out of memory\n");
        exit(70);
    }
    return p;
}

char *xstrndup(const char *s, size_t n) {
    char *d = xmalloc(n + 1);
    memcpy(d, s, n);
    d[n] = '\0';
    return d;
}

Global *global_get(Globals *g, const char *name, int len) {
    for (int i = 0; i < g->n; i++)
        if ((int)strlen(g->v[i].name) == len && memcmp(g->v[i].name, name, (size_t)len) == 0)
            return &g->v[i];
    if (g->n == g->cap) {
        g->cap = g->cap ? g->cap * 2 : 32;
        g->v = xrealloc(g->v, (size_t)g->cap * sizeof *g->v);
    }
    g->v[g->n] = (Global){xstrndup(name, (size_t)len), 0, 0, NULL, NULL};
    return &g->v[g->n++];
}

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "mlws-vm-loxc: cannot open %s: %s\n", path, strerror(errno));
        exit(66);
    }
    size_t cap = 65536, len = 0, r;
    char *buf = xmalloc(cap);
    while ((r = fread(buf + len, 1, cap - len - 1, f)) > 0) {
        len += r;
        if (len + 1 == cap) buf = xrealloc(buf, cap *= 2);
    }
    fclose(f);
    buf[len] = '\0';
    return buf;
}

static void usage(void) {
    fprintf(stderr,
        "usage: mlws-vm-loxc [-o FILE] program.lox\n"
        "  -o FILE        write the assembly to FILE (default: program.masm; '-' for stdout)\n"
        "  --version      print the version\n");
    exit(64);
}

int main(int argc, char **argv) {
    const char *output = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) output = argv[++i];
        else if (strcmp(argv[i], "--version") == 0) {
            printf("mlws-vm-loxc %s\n", VERSION);
            return 0;
        }
        else if (argv[i][0] == '-' && argv[i][1]) usage();
        else if (!source_path) source_path = argv[i];
        else usage();
    }
    if (!source_path) usage();

    char *src = read_file(source_path);
    scanner_init(src);
    Func *script = parse_program();
    if (had_error) return 65;
    Globals globals = {0};
    resolve_program(script, &globals);
    if (had_error) return 65;

    char *outpath = NULL;
    if (!output) {
        const char *dot = strrchr(source_path, '.');
        const char *slash = strrchr(source_path, '/');
        size_t stem = (dot && (!slash || dot > slash)) ? (size_t)(dot - source_path) : strlen(source_path);
        outpath = xmalloc(stem + 6);
        memcpy(outpath, source_path, stem);
        strcpy(outpath + stem, ".masm");
        output = outpath;
    }
    FILE *out = strcmp(output, "-") == 0 ? stdout : fopen(output, "w");
    if (!out) {
        fprintf(stderr, "mlws-vm-loxc: cannot write %s: %s\n", output, strerror(errno));
        return 73;
    }
    generate(script, &globals, lox_runtime, out);
    if (out != stdout && fclose(out) != 0) {
        fprintf(stderr, "mlws-vm-loxc: cannot write %s\n", output);
        return 74;
    }
    return 0;
}
