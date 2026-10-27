/* The Lua 5.5 lexer. */
#include "lua.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) {
        fprintf(stderr, "mlws-vm-luac: out of memory\n");
        exit(70);
    }
    return p;
}

void *xcalloc(size_t n, size_t size) {
    void *p = calloc(n ? n : 1, size ? size : 1);
    if (!p) {
        fprintf(stderr, "mlws-vm-luac: out of memory\n");
        exit(70);
    }
    return p;
}

void *xrealloc(void *p, size_t n) {
    p = realloc(p, n ? n : 1);
    if (!p) {
        fprintf(stderr, "mlws-vm-luac: out of memory\n");
        exit(70);
    }
    return p;
}

char *xstrdup(const char *s) { return xstrndup(s, strlen(s)); }

char *xstrndup(const char *s, size_t n) {
    char *p = xmalloc(n + 1);
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

bool str_eq(Str a, const char *b) {
    return (size_t)a.len == strlen(b) && memcmp(a.s, b, (size_t)a.len) == 0;
}

bool str_same(Str a, Str b) { return a.len == b.len && memcmp(a.s, b.s, (size_t)a.len) == 0; }

static const char *const reserved[] = {
    "and", "break", "do", "else", "elseif", "end", "false", "for", "function",
    "global", "goto", "if", "in", "local", "nil", "not", "or", "repeat",
    "return", "then", "true", "until", "while",
};
static const char *const symbols[] = {
    "//", "..", "...", "==", ">=", "<=", "~=", "<<", ">>", "::",
    "<eof>", "<number>", "<integer>", "<name>", "<string>",
};

const char *tok_text(int type, char *buf, size_t n) {
    if (type >= T_CHAR) {
        snprintf(buf, n, "%c", type - T_CHAR);
        return buf;
    }
    if (type <= T_WHILE)
        return reserved[type];
    return symbols[type - T_IDIV];
}

_Noreturn void lex_error(Lexer *lx, int line, const char *near, const char *fmt, ...) {
    va_list ap;
    fprintf(stderr, "mlws-vm-luac: %s:%d: ", lx->chunk, line);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    if (near)
        fprintf(stderr, " near '%s'", near);
    fprintf(stderr, "\n");
    exit(1);
}

void lex_init(Lexer *lx, const char *chunk, const char *src, size_t len) {
    memset(lx, 0, sizeof *lx);
    lx->src = lx->p = src;
    lx->end = src + len;
    lx->chunk = chunk;
    lx->line = 1;
    if (lx->p < lx->end && *lx->p == '#')        /* a #! line */
        while (lx->p < lx->end && *lx->p != '\n')
            lx->p++;
    lex_next(lx);
}

static int cur(Lexer *lx) { return lx->p < lx->end ? (unsigned char)*lx->p : -1; }
static int at(Lexer *lx, int k) { return lx->p + k < lx->end ? (unsigned char)lx->p[k] : -1; }

/* Skip a newline sequence (\n, \r, \n\r or \r\n). */
static void newline(Lexer *lx) {
    int c = cur(lx);
    lx->p++;
    if ((cur(lx) == '\n' || cur(lx) == '\r') && cur(lx) != c)
        lx->p++;
    lx->line++;
}

typedef VEC(char) CharBuf;

static void cb_add(CharBuf *b, int c) { vec_push(*b, (char)c); }

static void utf8_add(CharBuf *b, unsigned long x) {
    if (x < 0x80) {
        cb_add(b, (int)x);
        return;
    }
    char tmp[8];
    int n = 0;
    unsigned long mfb = 0x3f;       /* the largest that fits in the first byte */
    do {
        tmp[n++] = (char)(0x80 | (x & 0x3f));
        x >>= 6;
        mfb >>= 1;
    } while (x > mfb);
    tmp[n++] = (char)((~mfb << 1) | x);
    while (n > 0)
        cb_add(b, (unsigned char)tmp[--n]);
}

/* The level of a long bracket at p ('[' or ']' followed by '='s and the
   same bracket), or -1. */
static int long_level(Lexer *lx) {
    int c = cur(lx), k = 1;
    while (at(lx, k) == '=')
        k++;
    return at(lx, k) == c ? k - 1 : -1;
}

static Str long_string(Lexer *lx, int level, bool keep) {
    int line = lx->line;
    CharBuf b = {0};
    lx->p += level + 2;
    if (cur(lx) == '\n' || cur(lx) == '\r')
        newline(lx);
    for (;;) {
        int c = cur(lx);
        if (c < 0)
            lex_error(lx, line, "<eof>", keep ? "unfinished long string" : "unfinished long comment");
        if (c == ']' && long_level(lx) == level) {
            lx->p += level + 2;
            break;
        }
        if (c == '\n' || c == '\r') {
            newline(lx);
            if (keep)
                cb_add(&b, '\n');
        } else {
            if (keep)
                cb_add(&b, c);
            lx->p++;
        }
    }
    return (Str){b.v ? b.v : "", b.n};
}

static int hexval(int c) {
    return isdigit(c) ? c - '0' : (tolower(c) - 'a' + 10);
}

static Str quoted_string(Lexer *lx) {
    int q = cur(lx), line = lx->line;
    CharBuf b = {0};
    lx->p++;
    for (;;) {
        int c = cur(lx);
        if (c < 0)
            lex_error(lx, line, "<eof>", "unfinished string");
        if (c == '\n' || c == '\r') {
            cb_add(&b, 0);
            lex_error(lx, line, b.v, "unfinished string");
        }
        if (c == q) {
            lx->p++;
            break;
        }
        if (c != '\\') {
            cb_add(&b, c);
            lx->p++;
            continue;
        }
        lx->p++;
        c = cur(lx);
        switch (c) {
        case 'a': cb_add(&b, '\a'); lx->p++; break;
        case 'b': cb_add(&b, '\b'); lx->p++; break;
        case 'f': cb_add(&b, '\f'); lx->p++; break;
        case 'n': cb_add(&b, '\n'); lx->p++; break;
        case 'r': cb_add(&b, '\r'); lx->p++; break;
        case 't': cb_add(&b, '\t'); lx->p++; break;
        case 'v': cb_add(&b, '\v'); lx->p++; break;
        case '\\': case '"': case '\'': cb_add(&b, c); lx->p++; break;
        case '\n': case '\r': newline(lx); cb_add(&b, '\n'); break;
        case 'x': {
            lx->p++;
            if (!isxdigit(cur(lx)) || !isxdigit(at(lx, 1)))
                lex_error(lx, lx->line, NULL, "hexadecimal digit expected");
            cb_add(&b, hexval(cur(lx)) * 16 + hexval(at(lx, 1)));
            lx->p += 2;
            break;
        }
        case 'z':
            lx->p++;
            while (cur(lx) >= 0 && isspace(cur(lx))) {
                if (cur(lx) == '\n' || cur(lx) == '\r')
                    newline(lx);
                else
                    lx->p++;
            }
            break;
        case 'u': {
            lx->p++;
            if (cur(lx) != '{')
                lex_error(lx, lx->line, NULL, "missing '{' in \\u{xxxx}");
            lx->p++;
            unsigned long x = 0;
            if (!isxdigit(cur(lx)))
                lex_error(lx, lx->line, NULL, "hexadecimal digit expected");
            while (isxdigit(cur(lx))) {
                x = x * 16 + (unsigned long)hexval(cur(lx));
                if (x > 0x7FFFFFFFul)
                    lex_error(lx, lx->line, NULL, "UTF-8 value too large");
                lx->p++;
            }
            if (cur(lx) != '}')
                lex_error(lx, lx->line, NULL, "missing '}' in \\u{xxxx}");
            lx->p++;
            utf8_add(&b, x);
            break;
        }
        default:
            if (c >= 0 && isdigit(c)) {
                int v = 0, k = 0;
                while (k < 3 && isdigit(cur(lx))) {
                    v = v * 10 + cur(lx) - '0';
                    lx->p++;
                    k++;
                }
                if (v > 255)
                    lex_error(lx, lx->line, NULL, "decimal escape too large");
                cb_add(&b, v);
            } else {
                lex_error(lx, lx->line, NULL, "invalid escape sequence '\\%c'", c < 0 ? '?' : c);
            }
        }
    }
    return (Str){b.v ? b.v : "", b.n};
}

/* Convert a Lua numeral; false if it is malformed. */
static bool numeral(const char *s, Token *t) {
    const char *p = s;
    bool hex = p[0] == '0' && (p[1] == 'x' || p[1] == 'X');
    bool isfloat = false;
    for (const char *q = hex ? p + 2 : p; *q; q++)
        if (*q == '.' || (hex ? (*q == 'p' || *q == 'P') : (*q == 'e' || *q == 'E')))
            isfloat = true;
    if (!isfloat) {
        if (hex) {
            uint64_t v = 0;
            const char *q = p + 2;
            if (!*q)
                return false;
            for (; *q; q++) {
                if (!isxdigit((unsigned char)*q))
                    return false;
                v = v * 16 + (uint64_t)hexval((unsigned char)*q);   /* wraps around */
            }
            t->type = T_INT;
            t->i = (int64_t)v;
            return true;
        }
        uint64_t v = 0;
        bool overflow = false;
        for (const char *q = p; *q; q++) {
            if (!isdigit((unsigned char)*q))
                return false;
            int d = *q - '0';
            if (v > (UINT64_MAX - (uint64_t)d) / 10)
                overflow = true;
            else
                v = v * 10 + (uint64_t)d;
        }
        if (!overflow && v <= (uint64_t)INT64_MAX) {
            t->type = T_INT;
            t->i = (int64_t)v;
            return true;
        }
        /* too large for an integer: a float */
    }
    char *end;
    errno = 0;
    double d = strtod(p, &end);
    if (*end || end == p)
        return false;
    /* strtod accepts things Lua does not */
    for (const char *q = p; *q; q++)
        if (strchr("nNiI", *q) && !hex)
            return false;
    t->type = T_FLT;
    t->f = d;
    return true;
}

static void scan_token(Lexer *lx, Token *t);

static void scan(Lexer *lx, Token *t) {
    scan_token(lx, t);
    t->rawlen = (int)(lx->p - t->raw);
}

static void scan_token(Lexer *lx, Token *t) {
    for (;;) {
        int c = cur(lx);
        t->line = lx->line;
        t->raw = lx->p;
        t->s = (Str){0};
        switch (c) {
        case -1:
            t->type = T_EOF;
            return;
        case '\n': case '\r':
            newline(lx);
            continue;
        case ' ': case '\t': case '\f': case '\v':
            lx->p++;
            continue;
        case '-':
            if (at(lx, 1) != '-') {
                lx->p++;
                t->type = T_CHAR + '-';
                return;
            }
            lx->p += 2;
            if (cur(lx) == '[' && long_level(lx) >= 0) {
                long_string(lx, long_level(lx), false);
                continue;
            }
            while (cur(lx) >= 0 && cur(lx) != '\n' && cur(lx) != '\r')
                lx->p++;
            continue;
        case '[':
            if (long_level(lx) >= 0) {
                t->s = long_string(lx, long_level(lx), true);
                t->type = T_STRING;
                return;
            }
            if (at(lx, 1) == '=') {
                lex_error(lx, lx->line, "[=", "invalid long string delimiter");
            }
            lx->p++;
            t->type = T_CHAR + '[';
            return;
        case '=':
            lx->p++;
            if (cur(lx) == '=') { lx->p++; t->type = T_EQ; } else t->type = T_CHAR + '=';
            return;
        case '<':
            lx->p++;
            if (cur(lx) == '=') { lx->p++; t->type = T_LE; }
            else if (cur(lx) == '<') { lx->p++; t->type = T_SHL; }
            else t->type = T_CHAR + '<';
            return;
        case '>':
            lx->p++;
            if (cur(lx) == '=') { lx->p++; t->type = T_GE; }
            else if (cur(lx) == '>') { lx->p++; t->type = T_SHR; }
            else t->type = T_CHAR + '>';
            return;
        case '/':
            lx->p++;
            if (cur(lx) == '/') { lx->p++; t->type = T_IDIV; } else t->type = T_CHAR + '/';
            return;
        case '~':
            lx->p++;
            if (cur(lx) == '=') { lx->p++; t->type = T_NE; } else t->type = T_CHAR + '~';
            return;
        case ':':
            lx->p++;
            if (cur(lx) == ':') { lx->p++; t->type = T_DBCOLON; } else t->type = T_CHAR + ':';
            return;
        case '"': case '\'':
            t->s = quoted_string(lx);
            t->type = T_STRING;
            return;
        case '.':
            if (at(lx, 1) == '.') {
                if (at(lx, 2) == '.') {
                    lx->p += 3;
                    t->type = T_DOTS;
                } else {
                    lx->p += 2;
                    t->type = T_CONCAT;
                }
                return;
            }
            if (!isdigit(at(lx, 1))) {
                lx->p++;
                t->type = T_CHAR + '.';
                return;
            }
            /* fall through: a number */
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9': {
            const char *start = lx->p;
            const char *expo = "Ee";
            if (cur(lx) == '0' && (at(lx, 1) == 'x' || at(lx, 1) == 'X')) {
                expo = "Pp";
                lx->p += 2;
            }
            for (;;) {
                int d = cur(lx);
                if (d >= 0 && strchr(expo, d) && d) {
                    lx->p++;
                    if (cur(lx) == '+' || cur(lx) == '-')
                        lx->p++;
                } else if (d >= 0 && (isxdigit(d) || d == '.')) {
                    lx->p++;
                } else {
                    break;
                }
            }
            /* Lua reads alphanumerics glued to a numeral as part of it */
            while (cur(lx) >= 0 && (isalnum(cur(lx)) || cur(lx) == '_'))
                lx->p++;
            char *text = xstrndup(start, (size_t)(lx->p - start));
            if (!numeral(text, t))
                lex_error(lx, lx->line, text, "malformed number");
            free(text);
            return;
        }
        default:
            if (isalpha(c) || c == '_') {
                const char *start = lx->p;
                while (cur(lx) >= 0 && (isalnum(cur(lx)) || cur(lx) == '_'))
                    lx->p++;
                t->s = (Str){start, (int)(lx->p - start)};
                for (int k = 0; k <= T_WHILE; k++)
                    if (str_eq(t->s, reserved[k])) {
                        t->type = k;
                        return;
                    }
                t->type = T_NAME;
                return;
            }
            lx->p++;
            t->type = T_CHAR + c;
            return;
        }
    }
}

void lex_next(Lexer *lx) {
    if (lx->has_ahead) {
        lx->t = lx->ahead;
        lx->has_ahead = false;
    } else {
        scan(lx, &lx->t);
    }
}

int lex_peek(Lexer *lx) {
    if (!lx->has_ahead) {
        scan(lx, &lx->ahead);
        lx->has_ahead = true;
    }
    return lx->ahead.type;
}
