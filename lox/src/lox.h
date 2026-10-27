/* loxc: a compiler from Lox (https://craftinginterpreters.com) to MLWSVM assembly. */
#ifndef LOX_H
#define LOX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* ------------------------------------------------------------------ tokens */

typedef enum {
    /* single-character tokens */
    T_LEFT_PAREN, T_RIGHT_PAREN, T_LEFT_BRACE, T_RIGHT_BRACE,
    T_COMMA, T_DOT, T_MINUS, T_PLUS, T_SEMICOLON, T_SLASH, T_STAR,
    /* one or two character tokens */
    T_BANG, T_BANG_EQUAL, T_EQUAL, T_EQUAL_EQUAL,
    T_GREATER, T_GREATER_EQUAL, T_LESS, T_LESS_EQUAL,
    /* literals */
    T_IDENTIFIER, T_STRING, T_NUMBER,
    /* keywords */
    T_AND, T_CLASS, T_ELSE, T_FALSE, T_FUN, T_FOR, T_IF, T_NIL, T_OR,
    T_PRINT, T_RETURN, T_SUPER, T_THIS, T_TRUE, T_VAR, T_WHILE,
    T_EOF
} TokenType;

typedef struct {
    TokenType type;
    const char *start; /* the lexeme (for strings: including the quotes) */
    int length;
    int line;
} Token;

/* -------------------------------------------------------------------- AST */

typedef struct Var Var;
typedef struct Func Func;
typedef struct Stmt Stmt;

typedef enum {
    E_NUMBER, E_STRING, E_NIL, E_TRUE, E_FALSE,
    E_GROUP,    /* (a) */
    E_UNARY,    /* op a */
    E_BINARY,   /* a op b */
    E_LOGICAL,  /* a and/or b */
    E_VAR,      /* name */
    E_ASSIGN,   /* name = a */
    E_CALL,     /* a(args) */
    E_GET,      /* a.name */
    E_SET,      /* a.name = b */
    E_THIS,
    E_SUPER,    /* super.name */
} ExprKind;

typedef struct Expr {
    ExprKind kind;
    int line;
    Token tok;              /* operator, name, 'this', or the call's ')' */
    struct Expr *a, *b;
    struct Expr **args;
    int nargs;
    double num;
    /* filled in by the resolver for E_VAR, E_ASSIGN, E_THIS, E_SUPER: */
    Var *var;               /* the local variable, or NULL for a global */
    int up;                 /* -1: a local of the current function; else its upvalue index */
    Var *this_var;          /* E_SUPER: 'this' */
    int this_up;
} Expr;

typedef enum {
    S_EXPR, S_PRINT, S_VAR, S_BLOCK, S_IF, S_WHILE, S_FUN, S_RETURN, S_CLASS,
} StmtKind;

struct Stmt {
    StmtKind kind;
    int line;
    Token name;             /* S_VAR, S_FUN, S_CLASS; 'return' for S_RETURN */
    Expr *expr;             /* expression, initializer, condition, return value, superclass */
    Stmt *then, *els;       /* S_IF; S_WHILE body is 'then' */
    Stmt **stmts;           /* S_BLOCK; S_CLASS methods (S_FUN) */
    int n;
    Func *fn;               /* S_FUN */
    Var *var;               /* the declared local, or NULL for a global */
    Var *super_var;         /* S_CLASS with a superclass: the hidden 'super' local */
};

typedef enum { FN_SCRIPT, FN_FUNCTION, FN_METHOD, FN_INITIALIZER } FuncKind;

typedef struct {
    Var *var;
    bool from_local;        /* captured from the enclosing function's local, else its upvalue */
    int index;              /* that upvalue's index (when !from_local) */
} Upval;

struct Func {
    FuncKind kind;
    Token name;
    Token *params;
    int nparams;
    Stmt **body;
    int nbody;
    int id;
    Func *enclosing;
    /* resolver results */
    Var **param_vars;
    Var *this_var;          /* methods */
    Upval *ups;
    int nups;
};

struct Var {
    Token name;
    Func *fn;               /* the function the variable belongs to */
    bool captured;          /* referenced from a nested function */
    bool assigned;          /* assigned after its declaration */
    int slot;               /* codegen: its register/stack slot */
};

/* Captured variables that are assigned live in a heap Box shared by every
 * closure; the others are copied into each closure by value. */
static inline bool var_boxed(const Var *v) { return v->captured && v->assigned; }

/* --------------------------------------------------------------- globals */

typedef struct {
    char *name;
    int decls;              /* top-level declarations (var, fun, class) */
    int assigns;            /* assignments anywhere */
    Stmt *fun;              /* the declaration, if it is a 'fun' */
    Stmt *cls;              /* the declaration, if it is a class */
} Global;

typedef struct {
    Global *v;
    int n, cap;
} Globals;

Global *global_get(Globals *g, const char *name, int len);

/* ----------------------------------------------------------------- phases */

extern bool had_error;
extern const char *source_path;

void error_at(Token *tok, const char *msg);
void error_line(int line, const char *msg);

void *xmalloc(size_t n);
void *xrealloc(void *p, size_t n);
char *xstrndup(const char *s, size_t n);

void scanner_init(const char *src);
Token scan_token(void);

Func *parse_program(void);                       /* the script as a FN_SCRIPT function */
void resolve_program(Func *script, Globals *g);
void generate(Func *script, Globals *g, const char *runtime, FILE *out);

#endif
