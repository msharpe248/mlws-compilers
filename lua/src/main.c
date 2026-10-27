/* mlws-vm-luac: compile a Lua 5.5 program to MLWSVM assembly.
 *
 *   mlws-vm-luac [-o FILE] [-n NAME] program.lua    (FILE '-' is standard output)
 *
 * The output is one self-contained MLWSVM assembly file: the program, the
 * runtime and the library. `mlwsvm run` runs it and `mlwsvm asm` turns it
 * into a binary module. Errors in the program are reported as lua does
 * ("program.lua:3: ... near ..."), with exit status 1. NAME is the chunk
 * name used in messages (default: the path as given). */
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"

static char *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "mlws-vm-luac: cannot open %s: %s\n", path, strerror(errno));
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
    fprintf(stderr, "usage: mlws-vm-luac [-o FILE] [-n NAME] program.lua\n"
                    "  -o FILE   write the assembly to FILE (default: program.masm; '-' for stdout)\n"
                    "  -n NAME   the chunk name in error messages (default: the path)\n"
                    "  --version print the version\n");
    exit(2);
}

int main(int argc, char **argv) {
    const char *out = NULL, *path = NULL, *name = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
            out = argv[++i];
        else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc)
            name = argv[++i];
        else if (strcmp(argv[i], "--version") == 0) {
            printf("mlws-vm-luac %s\n", VERSION);
            return 0;
        }
        else if (argv[i][0] == '-' && argv[i][1])
            usage();
        else if (!path)
            path = argv[i];
        else
            usage();
    }
    if (!path)
        usage();
    if (!name)
        name = path;
    size_t len;
    char *src = read_file(path, &len);
    int ids = 0;
    Func *prelude = parse_chunk("prelude", prelude_lua, strlen(prelude_lua), true, &ids);
    Func *main_fn = parse_chunk(name, src, len, false, &ids);
    char *outpath = NULL;
    if (!out) {
        size_t n = strlen(path);
        outpath = xmalloc(n + 6);
        strcpy(outpath, path);
        char *dot = strrchr(outpath, '.');
        char *slash = strrchr(outpath, '/');
        if (dot && (!slash || dot > slash))
            *dot = 0;
        strcat(outpath, ".masm");
        out = outpath;
    }
    FILE *f = strcmp(out, "-") == 0 ? stdout : fopen(out, "w");
    if (!f) {
        fprintf(stderr, "mlws-vm-luac: cannot write %s: %s\n", out, strerror(errno));
        return 1;
    }
    GenInput in = {name, main_fn, prelude, runtime_masm};
    generate(&in, f);
    if (f != stdout && fclose(f) != 0) {
        fprintf(stderr, "mlws-vm-luac: cannot write %s: %s\n", out, strerror(errno));
        return 1;
    }
    return 0;
}
