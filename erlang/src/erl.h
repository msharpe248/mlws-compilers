/* mlws-vm-erlc: an Erlang compiler for MLWSVM. Shared declarations. */
#ifndef ERL_H
#define ERL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* ------------------------------------------------------------------ utility */

void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t size);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);
char *xprintf(const char *fmt, ...);
_Noreturn void fatal(const char *file, int line, const char *fmt, ...);

/* A growable array: VEC(Type) v; vec_push(v, x); v.n, v.v[i]. */
#define VEC(T) struct { T *v; int n, cap; }
#define vec_push(vec, x) do { \
        if ((vec).n == (vec).cap) { \
            (vec).cap = (vec).cap ? 2 * (vec).cap : 8; \
            (vec).v = xrealloc((vec).v, (size_t)(vec).cap * sizeof *(vec).v); \
        } \
        (vec).v[(vec).n++] = (x); \
    } while (0)

/* A growable text buffer. */
typedef struct {
    char *s;
    size_t n, cap;
} Buf;

void buf_add(Buf *b, const char *s, size_t n);
void buf_puts(Buf *b, const char *s);
void buf_printf(Buf *b, const char *fmt, ...);

/* Strings are interned: equal names are the same pointer. */
const char *intern(const char *s, size_t n);

/* ------------------------------------------------------------------ lexer */

typedef enum {
    /* reserved words, in the order of the table in lex.c */
    T_AFTER, T_AND, T_ANDALSO, T_BAND, T_BEGIN, T_BNOT, T_BOR, T_BSL, T_BSR,
    T_BXOR, T_CASE, T_CATCH, T_COND, T_DIV, T_ELSE, T_END, T_FUN, T_IF, T_LET,
    T_MAYBE, T_NOT, T_OF, T_OR, T_ORELSE, T_RECEIVE, T_REM, T_TRY, T_WHEN,
    T_XOR,
    /* multi-character symbols */
    T_ARROW,        /* -> */
    T_LARROW,       /* <- */
    T_LDARROW,      /* <= */
    T_DARROW,       /* => */
    T_ASSOC,        /* := */
    T_COLCOL,       /* :: */
    T_BARBAR,       /* || */
    T_LTLT,         /* << */
    T_GTGT,         /* >> */
    T_EQEQ,         /* == */
    T_NE,           /* /= */
    T_LE,           /* =< */
    T_GE,           /* >= */
    T_EXEQ,         /* =:= */
    T_EXNE,         /* =/= */
    T_PLUSPLUS,     /* ++ */
    T_MINUSMINUS,   /* -- */
    T_QQ,           /* ?? */
    T_MAYBEMATCH,   /* ?= */
    T_DOTDOTDOT,    /* ... */
    T_DOTDOT,       /* .. */
    /* the rest */
    T_EOF, T_DOT,   /* T_DOT ends a form */
    T_ATOM, T_VAR, T_INT, T_BIGINT, T_FLOAT, T_STRING, T_CHARLIT,
    /* single characters are their own codes, above this */
    T_CHAR = 256,
} TokType;

typedef struct {
    int type;           /* a TokType, or T_CHAR + the character */
    int line;
    const char *file;   /* the file it came from (interned) */
    const char *s;      /* atoms, variables (interned); strings and bigints (decimal digits) */
    int len;            /* length of s for strings (they may contain zeros) */
    int64_t i;          /* integers and character literals */
    double f;
} Token;

typedef VEC(Token) Tokens;

/* Tokenize a whole file. */
void lex_file(const char *file, const char *src, size_t len, Tokens *out);
const char *tok_name(int type);

/* ------------------------------------------------------------------ syntax tree */

typedef enum {
    /* terms */
    N_INT,          /* i */
    N_BIGINT,       /* s: decimal digits, with a leading '-' if negative */
    N_FLOAT,        /* f */
    N_ATOM,         /* s */
    N_VAR,          /* s ("_" is the anonymous variable) */
    N_STRING,       /* s, len: a list of characters (code points) */
    N_NIL,          /* [] */
    N_CONS,         /* a = head, b = tail */
    N_TUPLE,        /* kids */
    N_BIN,          /* kids: N_BINSEG */
    N_BINSEG,       /* a = value, b = size (or NULL), s = type specifiers ("integer-big-..."), */
    N_MAP,          /* a = base map (or NULL), kids: N_ASSOC / N_EXACT */
    N_ASSOC,        /* a => b */
    N_EXACT,        /* a := b */
    /* expressions */
    N_MATCH,        /* a = b */
    N_MAYBEMATCH,   /* a ?= b */
    N_OP,           /* a op b, op in s ("+", "andalso", ...) */
    N_UNOP,         /* op a */
    N_CALL,         /* a = callee (N_ATOM local name, N_REMOTE or any expression), kids = args */
    N_REMOTE,       /* a : b */
    N_CASE,         /* a = scrutinee, clauses */
    N_IF,           /* clauses (no patterns) */
    N_RECEIVE,      /* clauses; a = after timeout (or NULL), kids = after body */
    N_TRY,          /* kids = body, clauses = of clauses, cclauses = catch clauses, b = after (N_BLOCK) or NULL */
    N_CATCH,        /* catch a */
    N_BLOCK,        /* begin kids end */
    N_FUN,          /* clauses; s = name of a named fun or NULL */
    N_FUNREF,       /* fun a:b/i or fun b/i (a NULL): a, b are atoms or variables */
    N_LC,           /* [a || kids] */
    N_BC,           /* <<a || kids>> */
    N_MC,           /* #{a => b || kids} with a = N_ASSOC */
    N_GEN,          /* a <- b */
    N_BGEN,         /* a <= b */
    N_MGEN,         /* a := b <- c, with a = N_ASSOC(key, value) and b = the map */
    N_MAYBE,        /* maybe kids else clauses end */
    N_RECORD,       /* #s{kids}, a = the record updated (or NULL); kids: N_ASSOC(field atom or '_', value) */
    N_RECFIELD,     /* a#s.b */
    N_RECINDEX,     /* #s.b */
} NodeKind;

typedef struct Node Node;
typedef struct Clause Clause;
typedef VEC(Node *) Nodes;
typedef VEC(Clause *) Clauses;

/* A clause: head patterns (function and fun clauses; one for case,
 * receive and the catch clauses' Class:Reason:Stack, none for if), guards
 * (a list of alternatives, each a list of tests that must all hold) and
 * the body. */
struct Clause {
    Nodes pats;
    VEC(Nodes) guards;
    Nodes body;
    int line;
};

struct Node {
    NodeKind kind;
    int line;
    const char *file;
    const char *s;
    int len;
    int64_t i;
    double f;
    Node *a, *b, *c;
    Nodes kids;
    Clauses clauses, cclauses;
};

/* A function definition in a module. */
typedef struct {
    const char *name;
    int arity;
    Clauses clauses;
    int line;
    const char *file;
} Function;

/* A record definition: field names and default values (NULL: undefined). */
typedef struct {
    const char *name;
    VEC(const char *) fields;
    Nodes defaults;
} Record;

typedef struct {
    const char *name;
    const char *file;
    VEC(Function *) funcs;
    VEC(const char *) exports;      /* "name/arity", interned */
    bool export_all;
    /* -import(Mod, [f/1]): "f/1" -> module */
    VEC(const char *) import_fa;
    VEC(const char *) import_mod;
    VEC(Record *) records;
} Module;

/* Parse a file (after preprocessing it) into a module. Include files are
 * found relative to the file, then in the include directories. */
Module *parse_module(const char *path, const char *src, size_t len);
Function *find_function(Module *m, const char *name, int arity);
Node *new_node(NodeKind kind, int line, const char *file);

/* ------------------------------------------------------------------ code generation */

/* Generate a program from its modules (the first one's main/0 or main/1 is
 * run), writing assembly to out. */
void generate(Module **mods, int nmods, FILE *out, const char *source_name);

extern const char *runtime_masm;      /* runtime.masm, built in */
extern bool debug_info;               /* -g: .local names and a .debug_format formatter */
extern const char *runtime_re_masm;   /* runtime_re.masm: for programs that use re */
extern const char *prelude_files[];   /* the library modules' Erlang source, NULL-terminated */
extern const char *prelude_names[];

#endif
