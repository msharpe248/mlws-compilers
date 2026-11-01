/* The lexer: Erlang source text to tokens. */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "erl.h"

static const char *reserved[] = {
    "after", "and", "andalso", "band", "begin", "bnot", "bor", "bsl", "bsr",
    "bxor", "case", "catch", "cond", "div", "else", "end", "fun", "if", "let",
    "maybe", "not", "of", "or", "orelse", "receive", "rem", "try", "when",
    "xor",
};

/* Multi-character symbols, longest first. */
static const struct { const char *text; int type; } symbols[] = {
    {"=:=", T_EXEQ}, {"=/=", T_EXNE}, {"...", T_DOTDOTDOT},
    {"->", T_ARROW}, {"<-", T_LARROW}, {"<=", T_LDARROW}, {"=>", T_DARROW},
    {":=", T_ASSOC}, {"::", T_COLCOL}, {"||", T_BARBAR}, {"<<", T_LTLT},
    {">>", T_GTGT}, {"==", T_EQEQ}, {"/=", T_NE}, {"=<", T_LE}, {">=", T_GE},
    {"++", T_PLUSPLUS}, {"--", T_MINUSMINUS}, {"??", T_QQ}, {"?=", T_MAYBEMATCH},
    {"..", T_DOTDOT},
};

const char *tok_name(int type) {
    static char buf[8];
    if (type >= T_CHAR) {
        snprintf(buf, sizeof buf, "'%c'", type - T_CHAR);
        return buf;
    }
    if (type <= T_XOR)
        return reserved[type];
    for (size_t i = 0; i < sizeof symbols / sizeof *symbols; i++)
        if (symbols[i].type == type)
            return symbols[i].text;
    switch (type) {
    case T_EOF: return "end of file";
    case T_DOT: return "'.'";
    case T_ATOM: return "atom";
    case T_VAR: return "variable";
    case T_INT: case T_BIGINT: return "integer";
    case T_FLOAT: return "float";
    case T_STRING: return "string";
    case T_CHARLIT: return "character";
    }
    return "?";
}

typedef struct {
    const char *file;
    const char *p, *end;
    int line;
    Tokens *out;
} Lexer;

static void put_utf8(Buf *b, uint32_t c) {
    char t[4];
    size_t n;
    if (c < 0x80) {
        t[0] = (char)c;
        n = 1;
    } else if (c < 0x800) {
        t[0] = (char)(0xC0 | c >> 6);
        t[1] = (char)(0x80 | (c & 0x3F));
        n = 2;
    } else if (c < 0x10000) {
        t[0] = (char)(0xE0 | c >> 12);
        t[1] = (char)(0x80 | (c >> 6 & 0x3F));
        t[2] = (char)(0x80 | (c & 0x3F));
        n = 3;
    } else {
        t[0] = (char)(0xF0 | c >> 18);
        t[1] = (char)(0x80 | (c >> 12 & 0x3F));
        t[2] = (char)(0x80 | (c >> 6 & 0x3F));
        t[3] = (char)(0x80 | (c & 0x3F));
        n = 4;
    }
    buf_add(b, t, n);
}

/* The next source character, decoding UTF-8 (a byte that is not valid
 * UTF-8 stands for itself, as Latin-1). */
static uint32_t next_char(Lexer *lx) {
    const unsigned char *p = (const unsigned char *)lx->p;
    size_t left = (size_t)(lx->end - lx->p);
    uint32_t c = p[0];
    int n = 0;
    if (c >= 0xF0 && c < 0xF8) n = 3, c &= 7;
    else if (c >= 0xE0) n = 2, c &= 15;
    else if (c >= 0xC2 && c < 0xE0) n = 1, c &= 31;
    if (c >= 0x80 && n == 0) {
        lx->p++;
        return c;
    }
    if ((size_t)n >= left) {
        lx->p++;
        return p[0];
    }
    for (int i = 1; i <= n; i++) {
        if ((p[i] & 0xC0) != 0x80) {
            lx->p++;
            return p[0];
        }
        c = c << 6 | (p[i] & 0x3F);
    }
    lx->p += n + 1;
    return c;
}

static int digit_value(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return 99;
}

/* An escape sequence after a backslash, in a string, quoted atom or $\c. */
static uint32_t escape(Lexer *lx) {
    if (lx->p >= lx->end)
        fatal(lx->file, lx->line, "unterminated escape sequence");
    char c = *lx->p++;
    switch (c) {
    case 'b': return 8;
    case 'd': return 127;
    case 'e': return 27;
    case 'f': return 12;
    case 'n': return 10;
    case 'r': return 13;
    case 's': return 32;
    case 't': return 9;
    case 'v': return 11;
    case '^':
        if (lx->p >= lx->end)
            fatal(lx->file, lx->line, "unterminated escape sequence");
        return (uint32_t)(*lx->p++ & 31);
    case 'x':
        if (lx->p < lx->end && *lx->p == '{') {
            uint32_t v = 0;
            lx->p++;
            while (lx->p < lx->end && *lx->p != '}') {
                int d = digit_value(*lx->p++);
                if (d > 15)
                    fatal(lx->file, lx->line, "bad \\x{...} escape");
                v = v * 16 + (uint32_t)d;
            }
            if (lx->p < lx->end)
                lx->p++;
            return v;
        } else {
            uint32_t v = 0;
            for (int i = 0; i < 2 && lx->p < lx->end && digit_value(*lx->p) < 16; i++)
                v = v * 16 + (uint32_t)digit_value(*lx->p++);
            return v;
        }
    default:
        if (c >= '0' && c <= '7') {
            uint32_t v = (uint32_t)(c - '0');
            for (int i = 0; i < 2 && lx->p < lx->end && *lx->p >= '0' && *lx->p <= '7'; i++)
                v = v * 8 + (uint32_t)(*lx->p++ - '0');
            return v;
        }
        if (c == '\n')
            lx->line++;
        lx->p--;
        return next_char(lx);
    }
}

static Token *emit(Lexer *lx, int type) {
    Token t = {0};
    t.type = type;
    t.line = lx->line;
    t.file = lx->file;
    vec_push(*lx->out, t);
    return &lx->out->v[lx->out->n - 1];
}

/* A quoted string or atom: the text as UTF-8. */
static const char *quoted(Lexer *lx, char q, int *len) {
    Buf b = {0};
    int line = lx->line;
    for (;;) {
        if (lx->p >= lx->end)
            fatal(lx->file, line, "unterminated %s", q == '"' ? "string" : "quoted atom");
        char c = *lx->p;
        if (c == q) {
            lx->p++;
            break;
        }
        if (c == '\\') {
            lx->p++;
            put_utf8(&b, escape(lx));
            continue;
        }
        if (c == '\n')
            lx->line++;
        put_utf8(&b, next_char(lx));
    }
    buf_add(&b, "", 0);
    *len = (int)b.n;
    return b.s ? b.s : "";
}

/* Decimal digits of a non-negative integer written in some base, for
 * integers too big for 64 bits. */
static const char *big_digits(const char *digits, int base) {
    int cap = (int)strlen(digits) * 2 + 2, n = 1;
    int *d = xcalloc((size_t)cap, sizeof *d);       /* little-endian decimal digits */
    for (const char *p = digits; *p; p++) {
        if (*p == '_')
            continue;
        int carry = digit_value(*p);
        for (int i = 0; i < n; i++) {
            int v = d[i] * base + carry;
            d[i] = v % 10;
            carry = v / 10;
        }
        while (carry) {
            d[n++] = carry % 10;
            carry /= 10;
        }
    }
    char *s = xmalloc((size_t)n + 1);
    for (int i = 0; i < n; i++)
        s[i] = (char)('0' + d[n - 1 - i]);
    s[n] = 0;
    free(d);
    return s;
}

static void number(Lexer *lx) {
    const char *start = lx->p;
    while (lx->p < lx->end && (isdigit((unsigned char)*lx->p) || *lx->p == '_'))
        lx->p++;
    /* Base#digits */
    if (lx->p < lx->end && *lx->p == '#') {
        int base = atoi(start);
        if (base < 2 || base > 36)
            fatal(lx->file, lx->line, "bad base %d", base);
        lx->p++;
        const char *ds = lx->p;
        while (lx->p < lx->end && (digit_value(*lx->p) < base || *lx->p == '_'))
            lx->p++;
        char *digits = xstrndup(ds, (size_t)(lx->p - ds));
        uint64_t v = 0;
        bool big = false;
        for (char *q = digits; *q; q++) {
            if (*q == '_')
                continue;
            uint64_t d = (uint64_t)digit_value(*q);
            if (v > (UINT64_MAX - d) / (uint64_t)base) {
                big = true;
                break;
            }
            v = v * (uint64_t)base + d;
        }
        if (big || v > INT64_MAX) {
            Token *t = emit(lx, T_BIGINT);
            t->s = big_digits(digits, base);
        } else {
            emit(lx, T_INT)->i = (int64_t)v;
        }
        free(digits);
        return;
    }
    bool is_float = false;
    if (lx->p + 1 < lx->end && *lx->p == '.' && isdigit((unsigned char)lx->p[1])) {
        is_float = true;
        lx->p++;
        while (lx->p < lx->end && (isdigit((unsigned char)*lx->p) || *lx->p == '_'))
            lx->p++;
        if (lx->p < lx->end && (*lx->p == 'e' || *lx->p == 'E')) {
            const char *q = lx->p + 1;
            if (q < lx->end && (*q == '+' || *q == '-'))
                q++;
            if (q < lx->end && isdigit((unsigned char)*q)) {
                lx->p = q;
                while (lx->p < lx->end && isdigit((unsigned char)*lx->p))
                    lx->p++;
            }
        }
    }
    Buf b = {0};
    for (const char *q = start; q < lx->p; q++)
        if (*q != '_')
            buf_add(&b, q, 1);
    buf_add(&b, "", 0);
    if (is_float) {
        emit(lx, T_FLOAT)->f = strtod(b.s, NULL);
    } else {
        const char *s = b.s;
        while (s[0] == '0' && s[1])
            s++;
        if (strlen(s) > 19 || (strlen(s) == 19 && strcmp(s, "9223372036854775807") > 0)) {
            emit(lx, T_BIGINT)->s = xstrdup(s);
        } else {
            emit(lx, T_INT)->i = strtoll(s, NULL, 10);
        }
    }
    free(b.s);
}

void lex_file(const char *file, const char *src, size_t len, Tokens *out) {
    Lexer lx = {intern(file, strlen(file)), src, src + len, 1, out};
    while (lx.p < lx.end) {
        char c = *lx.p;
        if (c == '\n') {
            lx.line++;
            lx.p++;
            continue;
        }
        if (isspace((unsigned char)c)) {
            lx.p++;
            continue;
        }
        if (c == '%') {
            while (lx.p < lx.end && *lx.p != '\n')
                lx.p++;
            continue;
        }
        if (isdigit((unsigned char)c)) {
            number(&lx);
            continue;
        }
        if (islower((unsigned char)c) || isupper((unsigned char)c) || c == '_'
            || (unsigned char)c >= 0xC0) {
            const char *start = lx.p;
            uint32_t first = next_char(&lx);
            lx.p = start;
            Buf b = {0};
            for (;;) {
                if (lx.p >= lx.end)
                    break;
                const char *save = lx.p;
                uint32_t ch = next_char(&lx);
                if (ch < 128 ? (isalnum((int)ch) || ch == '_' || ch == '@')
                             : (ch >= 0xC0 && ch <= 0xFF && ch != 0xD7 && ch != 0xF7)) {
                    put_utf8(&b, ch);
                } else {
                    lx.p = save;
                    break;
                }
            }
            const char *name = intern(b.s, b.n);
            free(b.s);
            bool var = first == '_' || (first < 128 ? isupper((int)first) : (first >= 0xC0 && first <= 0xDE));
            if (var) {
                emit(&lx, T_VAR)->s = name;
                continue;
            }
            int kw = -1;
            for (int i = 0; i <= T_XOR; i++)
                if (strcmp(name, reserved[i]) == 0)
                    kw = i;
            /* cond and let are reserved but unused; maybe and else are
             * keywords only where the maybe feature is on: treat as atoms. */
            if (kw == T_COND || kw == T_LET || kw == T_MAYBE || kw == T_ELSE)
                kw = -1;
            if (kw >= 0)
                emit(&lx, kw);
            else
                emit(&lx, T_ATOM)->s = name;
            continue;
        }
        if (c == '\'') {
            lx.p++;
            int n;
            const char *s = quoted(&lx, '\'', &n);
            emit(&lx, T_ATOM)->s = intern(s, (size_t)n);
            continue;
        }
        if (c == '"') {
            lx.p++;
            int line = lx.line, n;
            const char *s = quoted(&lx, '"', &n);
            Token *t = emit(&lx, T_STRING);
            t->s = s;
            t->len = n;
            t->line = line;
            continue;
        }
        if (c == '$') {
            lx.p++;
            if (lx.p >= lx.end)
                fatal(lx.file, lx.line, "unterminated character");
            uint32_t ch;
            if (*lx.p == '\\') {
                lx.p++;
                ch = escape(&lx);
            } else {
                if (*lx.p == '\n')
                    lx.line++;
                ch = next_char(&lx);
            }
            emit(&lx, T_CHARLIT)->i = ch;
            continue;
        }
        if (c == '.') {
            const char *q = lx.p + 1;
            if (q >= lx.end || isspace((unsigned char)*q) || *q == '%') {
                lx.p++;
                emit(&lx, T_DOT);
                continue;
            }
        }
        bool found = false;
        for (size_t i = 0; i < sizeof symbols / sizeof *symbols; i++) {
            size_t n = strlen(symbols[i].text);
            if ((size_t)(lx.end - lx.p) >= n && memcmp(lx.p, symbols[i].text, n) == 0) {
                emit(&lx, symbols[i].type);
                lx.p += n;
                found = true;
                break;
            }
        }
        if (found)
            continue;
        if (strchr("()[]{},;:|#!=<>+-*/?.", c) && c) {
            emit(&lx, T_CHAR + c);
            lx.p++;
            continue;
        }
        fatal(lx.file, lx.line, "illegal character '%c'", c);
    }
    emit(&lx, T_EOF);
}
