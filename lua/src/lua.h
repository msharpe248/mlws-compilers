/* mlws-vm-luac: a Lua 5.5 compiler for MLWSVM. Shared declarations. */
#ifndef LUA_H
#define LUA_H

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

/* A growable array: VEC(Type) v; vec_push(v, x); v.n, v.v[i]. */
#define VEC(T) struct { T *v; int n, cap; }
#define vec_push(vec, x) do { \
        if ((vec).n == (vec).cap) { \
            (vec).cap = (vec).cap ? 2 * (vec).cap : 8; \
            (vec).v = xrealloc((vec).v, (size_t)(vec).cap * sizeof *(vec).v); \
        } \
        (vec).v[(vec).n++] = (x); \
    } while (0)

/* A byte string (Lua strings may contain zeros). */
typedef struct {
    const char *s;
    int len;
} Str;

bool str_eq(Str a, const char *b);
bool str_same(Str a, Str b);

/* ------------------------------------------------------------------ lexer */

typedef enum {
    /* reserved words, in the order of the table in lex.c */
    T_AND, T_BREAK, T_DO, T_ELSE, T_ELSEIF, T_END, T_FALSE, T_FOR, T_FUNCTION,
    T_GLOBAL, T_GOTO, T_IF, T_IN, T_LOCAL, T_NIL, T_NOT, T_OR, T_REPEAT,
    T_RETURN, T_THEN, T_TRUE, T_UNTIL, T_WHILE,
    /* multi-character symbols */
    T_IDIV, T_CONCAT, T_DOTS, T_EQ, T_GE, T_LE, T_NE, T_SHL, T_SHR, T_DBCOLON,
    /* the rest */
    T_EOF, T_FLT, T_INT, T_NAME, T_STRING,
    /* single characters are their own codes, above this */
    T_CHAR = 256,
} TokType;

typedef struct {
    int type;           /* a TokType, or T_CHAR + the character */
    int line;
    Str s;              /* names and strings (strings are unescaped) */
    int64_t i;
    double f;
    const char *raw;    /* the token's source text, for messages */
    int rawlen;
} Token;

typedef struct {
    const char *src, *p, *end;
    const char *chunk;  /* chunk name for messages */
    int line;
    Token t, ahead;     /* current token and one lookahead */
    bool has_ahead;
} Lexer;

void lex_init(Lexer *lx, const char *chunk, const char *src, size_t len);
void lex_next(Lexer *lx);
int lex_peek(Lexer *lx);
const char *tok_text(int type, char *buf, size_t n);
_Noreturn void lex_error(Lexer *lx, int line, const char *near, const char *fmt, ...);

/* --------------------------------------------------------------------- AST */

typedef struct Expr Expr;
typedef struct Stmt Stmt;
typedef struct Func Func;
typedef struct Var Var;
typedef struct Block Block;

enum { ATTR_NONE, ATTR_CONST, ATTR_CLOSE };

struct Var {
    Str name;
    Func *fn;           /* the function that declares it */
    int attrib;
    bool captured;      /* used by a nested function */
    bool assigned;      /* assigned anywhere after its declaration */
    bool param;
    bool vatable;       /* a named vararg table (Lua 5.5) */
    int id;
    Func *localfn;      /* local function f: its function */
    Expr *init;         /* local x = e: e (when it gives x its value alone) */
    int sty;            /* code generation: its type, if never assigned and known */
    /* code generation */
    int slot;
    bool boxed;         /* lives in a Box, shared with closures */
};

typedef enum {
    X_NIL, X_TRUE, X_FALSE, X_INT, X_FLT, X_STR, X_VARARG, X_FUNC,
    X_LOCAL,            /* var (an upvalue when var->fn is not the current function) */
    X_GLOBAL,           /* name; env is NULL for the default _ENV */
    X_INDEX,            /* a[b] */
    X_CALL,             /* a(args) */
    X_METHOD,           /* a:name(args) */
    X_BIN, X_UN, X_AND, X_OR,
    X_TABLE,
    X_PAREN,            /* (a): truncates to one value */
} XKind;

/* Binary and unary operators, in Lua's priority classes. */
enum {
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_POW, OP_IDIV,
    OP_BAND, OP_BOR, OP_BXOR, OP_SHL, OP_SHR,
    OP_CONCAT,
    OP_EQ, OP_NE, OP_LT, OP_LE, OP_GT, OP_GE,
    OP_UNM, OP_BNOT, OP_NOT, OP_LEN,
};

typedef struct {
    Expr *key;          /* NULL for a positional item */
    Expr *val;
} Field;

struct Expr {
    XKind k;
    int line;
    int64_t i;
    double f;
    Str s;              /* X_STR, X_GLOBAL and X_METHOD names */
    Var *var;           /* X_LOCAL */
    Expr *env;          /* X_GLOBAL: a local _ENV in scope, or NULL */
    int op;
    Expr *a, *b;
    Expr **args;
    int nargs;
    Func *fn;
    Field *fields;
    int nfields;
    bool callee;        /* X_LOCAL: the function of a call */
};

typedef enum {
    S_EXPR, S_LOCAL, S_ASSIGN, S_IF, S_WHILE, S_REPEAT, S_NUMFOR, S_GENFOR,
    S_DO, S_RETURN, S_BREAK, S_GOTO, S_LABEL, S_LOCALFN, S_GLOBALFN,
} SKind;

struct Block {
    Stmt **stmts;
    int n;
    bool has_close;     /* declares a to-be-closed variable */
};

typedef struct {
    Var *var;
    Expr *e;
} UpRef;

struct Stmt {
    SKind k;
    int line;
    /* S_LOCAL: vars and exprs; S_ASSIGN: targets and exprs (global
       declarations with values are S_ASSIGN with check_undef) */
    Var **vars;
    int nvars;
    Expr **targets;
    int ntargets;
    Expr **exprs;
    int nexprs;
    bool check_undef;   /* global x = ...: x must not be defined yet */
    Expr *cond;         /* S_WHILE, S_REPEAT; S_IF uses conds */
    Expr **conds;
    Block **blocks;     /* S_IF: one per cond, plus the else block (or NULL) */
    int nconds;
    Block *body;
    Var *var;           /* S_NUMFOR, S_LOCALFN */
    Expr *start, *limit, *step;
    Func *fn;           /* S_LOCALFN, S_GLOBALFN */
    Str name;           /* S_GOTO, S_LABEL, S_GLOBALFN */
    Stmt *label;        /* S_GOTO: its target */
    int label_id;
    int nactive;        /* S_LABEL, S_GOTO: locals active in the enclosing function */
    bool backward;      /* S_LABEL: the target of a backward goto */
};

struct Func {
    Func *parent;
    Var **params;
    int np;
    bool vararg;
    Var *vaname;        /* function f(...t) */
    bool has_self;
    Block *body;
    VEC(Var *) upvals;  /* variables of enclosing functions that it uses */
    VEC(Var *) locals;  /* every variable it declares */
    VEC(UpRef) uprefs;  /* its references to upvalues */
    VEC(Func *) children;
    char *name;         /* for the assembly label */
    int line, lastline;
    int id;
    bool uses_vararg;
    bool is_main;
    bool prelude;
    Var *env;           /* main chunk: the _ENV upvalue every free name goes through */
    /* code generation */
    VEC(Var *) caps;    /* the variables its closures capture */
    int capwords;
    bool packed;        /* captures packed in a list (UP[0]) */
    bool direct;        /* called with call, not calli (no captures) */
    bool generated;
    int sig;
};

typedef struct {
    Func *main;
    VEC(Func *) funcs;
} Program;

Func *parse_chunk(const char *chunk, const char *src, size_t len, bool prelude, int *func_ids);

/* ---------------------------------------------------------------- codegen */

typedef struct {
    const char *chunk;          /* the chunk name for messages */
    Func *main, *prelude;
    const char *runtime;        /* src/runtime.masm */
} GenInput;

void generate(GenInput *in, FILE *out);

extern const char *runtime_masm;
extern const char *prelude_lua;

/* The compiler's version (the Makefile passes lua/VERSION). */
#ifndef VERSION
#define VERSION "unknown"
#endif

#endif
