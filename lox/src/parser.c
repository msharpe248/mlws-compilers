/* The parser: recursive descent, following jlox's grammar, error messages and
 * panic-mode recovery. 'for' loops are desugared into 'while' loops. */
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

#include "lox.h"

static Token *toks;
static int ntoks, pos;
static jmp_buf panic_buf;
static int next_func_id;

/* --------------------------------------------------------------- helpers */

static Token *peek(void) { return &toks[pos]; }
static Token *previous(void) { return &toks[pos - 1]; }
static bool at_end(void) { return peek()->type == T_EOF; }
static bool check(TokenType t) { return peek()->type == t; }

static Token *advance(void) {
    if (!at_end()) pos++;
    return previous();
}

static bool match(TokenType t) {
    if (!check(t)) return false;
    advance();
    return true;
}

static _Noreturn void panic(Token *tok, const char *msg) {
    error_at(tok, msg);
    longjmp(panic_buf, 1);
}

static Token *consume(TokenType t, const char *msg) {
    if (check(t)) return advance();
    panic(peek(), msg);
}

/* Discard tokens until a likely statement boundary. */
static void synchronize(void) {
    advance();
    while (!at_end()) {
        if (previous()->type == T_SEMICOLON) return;
        switch (peek()->type) {
        case T_CLASS: case T_FUN: case T_VAR: case T_FOR: case T_IF:
        case T_WHILE: case T_PRINT: case T_RETURN:
            return;
        default:
            advance();
        }
    }
}

static Expr *new_expr(ExprKind kind, Token *tok) {
    Expr *e = xmalloc(sizeof *e);
    memset(e, 0, sizeof *e);
    e->kind = kind;
    e->tok = *tok;
    e->line = tok->line;
    e->up = -1;
    e->this_up = -1;
    return e;
}

static Stmt *new_stmt(StmtKind kind, int line) {
    Stmt *s = xmalloc(sizeof *s);
    memset(s, 0, sizeof *s);
    s->kind = kind;
    s->line = line;
    return s;
}

typedef struct {
    void **v;
    int n, cap;
} Vec;

static void vec_push(Vec *v, void *p) {
    if (v->n == v->cap) {
        v->cap = v->cap ? v->cap * 2 : 8;
        v->v = xrealloc(v->v, (size_t)v->cap * sizeof *v->v);
    }
    v->v[v->n++] = p;
}

/* ----------------------------------------------------------- expressions */

static Expr *expression(void);

static Expr *finish_call(Expr *callee) {
    Vec args = {0};
    if (!check(T_RIGHT_PAREN)) {
        do {
            if (args.n >= 255) error_at(peek(), "Can't have more than 255 arguments.");
            vec_push(&args, expression());
        } while (match(T_COMMA));
    }
    Token *paren = consume(T_RIGHT_PAREN, "Expect ')' after arguments.");
    Expr *e = new_expr(E_CALL, paren);
    e->a = callee;
    e->args = (Expr **)args.v;
    e->nargs = args.n;
    return e;
}

static Expr *primary(void) {
    if (match(T_FALSE)) return new_expr(E_FALSE, previous());
    if (match(T_TRUE)) return new_expr(E_TRUE, previous());
    if (match(T_NIL)) return new_expr(E_NIL, previous());
    if (match(T_NUMBER)) {
        Expr *e = new_expr(E_NUMBER, previous());
        char *s = xstrndup(e->tok.start, (size_t)e->tok.length);
        e->num = strtod(s, NULL);
        free(s);
        return e;
    }
    if (match(T_STRING)) return new_expr(E_STRING, previous());
    if (match(T_SUPER)) {
        consume(T_DOT, "Expect '.' after 'super'.");
        Token *method = consume(T_IDENTIFIER, "Expect superclass method name.");
        return new_expr(E_SUPER, method);
    }
    if (match(T_THIS)) return new_expr(E_THIS, previous());
    if (match(T_IDENTIFIER)) return new_expr(E_VAR, previous());
    if (match(T_LEFT_PAREN)) {
        Token *paren = previous();
        Expr *inner = expression();
        consume(T_RIGHT_PAREN, "Expect ')' after expression.");
        Expr *e = new_expr(E_GROUP, paren);
        e->a = inner;
        return e;
    }
    panic(peek(), "Expect expression.");
}

static Expr *call(void) {
    Expr *e = primary();
    for (;;) {
        if (match(T_LEFT_PAREN)) {
            e = finish_call(e);
        } else if (match(T_DOT)) {
            Token *name = consume(T_IDENTIFIER, "Expect property name after '.'.");
            Expr *g = new_expr(E_GET, name);
            g->a = e;
            e = g;
        } else {
            break;
        }
    }
    return e;
}

static Expr *unary(void) {
    if (match(T_BANG) || match(T_MINUS)) {
        Expr *e = new_expr(E_UNARY, previous());
        e->a = unary();
        return e;
    }
    return call();
}

/* A left-associative binary level. */
static Expr *binary_level(Expr *(*next)(void), ExprKind kind, const TokenType *ops, int nops) {
    Expr *e = next();
    for (;;) {
        bool found = false;
        for (int i = 0; i < nops && !found; i++) found = match(ops[i]);
        if (!found) return e;
        Expr *b = new_expr(kind, previous());
        b->a = e;
        b->b = next();
        e = b;
    }
}

static Expr *factor(void) {
    static const TokenType ops[] = {T_SLASH, T_STAR};
    return binary_level(unary, E_BINARY, ops, 2);
}

static Expr *term(void) {
    static const TokenType ops[] = {T_MINUS, T_PLUS};
    return binary_level(factor, E_BINARY, ops, 2);
}

static Expr *comparison(void) {
    static const TokenType ops[] = {T_GREATER, T_GREATER_EQUAL, T_LESS, T_LESS_EQUAL};
    return binary_level(term, E_BINARY, ops, 4);
}

static Expr *equality(void) {
    static const TokenType ops[] = {T_BANG_EQUAL, T_EQUAL_EQUAL};
    return binary_level(comparison, E_BINARY, ops, 2);
}

static Expr *and_expr(void) {
    static const TokenType ops[] = {T_AND};
    return binary_level(equality, E_LOGICAL, ops, 1);
}

static Expr *or_expr(void) {
    static const TokenType ops[] = {T_OR};
    return binary_level(and_expr, E_LOGICAL, ops, 1);
}

static Expr *assignment(void) {
    Expr *e = or_expr();
    if (match(T_EQUAL)) {
        Token *equals = previous();
        Expr *value = assignment();
        if (e->kind == E_VAR) {
            Expr *a = new_expr(E_ASSIGN, &e->tok);
            a->a = value;
            return a;
        }
        if (e->kind == E_GET) {
            Expr *s = new_expr(E_SET, &e->tok);
            s->a = e->a;
            s->b = value;
            return s;
        }
        error_at(equals, "Invalid assignment target.");
    }
    return e;
}

static Expr *expression(void) { return assignment(); }

/* ------------------------------------------------------------ statements */

static Stmt *declaration(void);
static Stmt *statement(void);

static Stmt *expression_statement(void) {
    Stmt *s = new_stmt(S_EXPR, peek()->line);
    s->expr = expression();
    consume(T_SEMICOLON, "Expect ';' after expression.");
    return s;
}

static Stmt *block_stmt(int line, Stmt **stmts, int n) {
    Stmt *s = new_stmt(S_BLOCK, line);
    s->stmts = stmts;
    s->n = n;
    return s;
}

/* The statements of a block whose '{' has been consumed. */
static Stmt *block(void) {
    int line = previous()->line;
    Vec v = {0};
    while (!check(T_RIGHT_BRACE) && !at_end()) {
        Stmt *s = declaration();
        if (s) vec_push(&v, s);
    }
    consume(T_RIGHT_BRACE, "Expect '}' after block.");
    return block_stmt(line, (Stmt **)v.v, v.n);
}

static Stmt *var_declaration(void) {
    Token *name = consume(T_IDENTIFIER, "Expect variable name.");
    Stmt *s = new_stmt(S_VAR, name->line);
    s->name = *name;
    if (match(T_EQUAL)) s->expr = expression();
    consume(T_SEMICOLON, "Expect ';' after variable declaration.");
    return s;
}

static Stmt *function(FuncKind kind) {
    const char *what = kind == FN_FUNCTION ? "function" : "method";
    char msg[64];
    snprintf(msg, sizeof msg, "Expect %s name.", what);
    Token *name = consume(T_IDENTIFIER, msg);
    snprintf(msg, sizeof msg, "Expect '(' after %s name.", what);
    consume(T_LEFT_PAREN, msg);

    Func *f = xmalloc(sizeof *f);
    memset(f, 0, sizeof *f);
    f->kind = kind;
    f->name = *name;
    f->id = next_func_id++;
    if (kind == FN_METHOD && name->length == 4 && memcmp(name->start, "init", 4) == 0)
        f->kind = FN_INITIALIZER;

    Token *params = NULL;
    int n = 0, cap = 0;
    if (!check(T_RIGHT_PAREN)) {
        do {
            if (n >= 255) error_at(peek(), "Can't have more than 255 parameters.");
            Token *p = consume(T_IDENTIFIER, "Expect parameter name.");
            if (n == cap) {
                cap = cap ? cap * 2 : 8;
                params = xrealloc(params, (size_t)cap * sizeof *params);
            }
            params[n++] = *p;
        } while (match(T_COMMA));
    }
    consume(T_RIGHT_PAREN, "Expect ')' after parameters.");
    snprintf(msg, sizeof msg, "Expect '{' before %s body.", what);
    consume(T_LEFT_BRACE, msg);
    Stmt *body = block();

    f->params = params;
    f->nparams = n;
    f->body = body->stmts;
    f->nbody = body->n;

    Stmt *s = new_stmt(S_FUN, name->line);
    s->name = *name;
    s->fn = f;
    return s;
}

static Stmt *class_declaration(void) {
    Token *name = consume(T_IDENTIFIER, "Expect class name.");
    Stmt *s = new_stmt(S_CLASS, name->line);
    s->name = *name;
    if (match(T_LESS)) {
        consume(T_IDENTIFIER, "Expect superclass name.");
        s->expr = new_expr(E_VAR, previous());
    }
    consume(T_LEFT_BRACE, "Expect '{' before class body.");
    Vec methods = {0};
    while (!check(T_RIGHT_BRACE) && !at_end()) vec_push(&methods, function(FN_METHOD));
    consume(T_RIGHT_BRACE, "Expect '}' after class body.");
    s->stmts = (Stmt **)methods.v;
    s->n = methods.n;
    return s;
}

static Stmt *for_statement(void) {
    int line = previous()->line;
    consume(T_LEFT_PAREN, "Expect '(' after 'for'.");
    Stmt *init = NULL;
    if (match(T_SEMICOLON)) init = NULL;
    else if (match(T_VAR)) init = var_declaration();
    else init = expression_statement();

    Expr *cond = NULL;
    if (!check(T_SEMICOLON)) cond = expression();
    consume(T_SEMICOLON, "Expect ';' after loop condition.");
    Expr *incr = NULL;
    if (!check(T_RIGHT_PAREN)) incr = expression();
    consume(T_RIGHT_PAREN, "Expect ')' after for clauses.");
    Stmt *body = statement();

    if (incr) {
        Stmt **v = xmalloc(2 * sizeof *v);
        v[0] = body;
        v[1] = new_stmt(S_EXPR, incr->line);
        v[1]->expr = incr;
        body = block_stmt(body->line, v, 2);
    }
    if (!cond) {
        Token t = {T_TRUE, "true", 4, line};
        cond = new_expr(E_TRUE, &t);
    }
    Stmt *loop = new_stmt(S_WHILE, line);
    loop->expr = cond;
    loop->then = body;
    if (init) {
        Stmt **v = xmalloc(2 * sizeof *v);
        v[0] = init;
        v[1] = loop;
        return block_stmt(line, v, 2);
    }
    return loop;
}

static Stmt *statement(void) {
    if (match(T_FOR)) return for_statement();
    if (match(T_IF)) {
        Stmt *s = new_stmt(S_IF, previous()->line);
        consume(T_LEFT_PAREN, "Expect '(' after 'if'.");
        s->expr = expression();
        consume(T_RIGHT_PAREN, "Expect ')' after if condition.");
        s->then = statement();
        if (match(T_ELSE)) s->els = statement();
        return s;
    }
    if (match(T_PRINT)) {
        Stmt *s = new_stmt(S_PRINT, previous()->line);
        s->expr = expression();
        consume(T_SEMICOLON, "Expect ';' after value.");
        return s;
    }
    if (match(T_RETURN)) {
        Stmt *s = new_stmt(S_RETURN, previous()->line);
        s->name = *previous();
        if (!check(T_SEMICOLON)) s->expr = expression();
        consume(T_SEMICOLON, "Expect ';' after return value.");
        return s;
    }
    if (match(T_WHILE)) {
        Stmt *s = new_stmt(S_WHILE, previous()->line);
        consume(T_LEFT_PAREN, "Expect '(' after 'while'.");
        s->expr = expression();
        consume(T_RIGHT_PAREN, "Expect ')' after condition.");
        s->then = statement();
        return s;
    }
    if (match(T_LEFT_BRACE)) return block();
    return expression_statement();
}

/* A declaration, or NULL after a syntax error (once recovered). */
static Stmt *declaration(void) {
    /* Nested declarations each set their own recovery point; restore the
     * enclosing one afterwards. */
    jmp_buf saved;
    memcpy(saved, panic_buf, sizeof saved);
    Stmt *s = NULL;
    if (setjmp(panic_buf) == 0) {
        if (match(T_CLASS)) s = class_declaration();
        else if (match(T_FUN)) s = function(FN_FUNCTION);
        else if (match(T_VAR)) s = var_declaration();
        else s = statement();
    } else {
        synchronize();
        s = NULL;
    }
    memcpy(panic_buf, saved, sizeof saved);
    return s;
}

Func *parse_program(void) {
    int cap = 1024;
    toks = xmalloc((size_t)cap * sizeof *toks);
    ntoks = 0;
    for (;;) {
        if (ntoks == cap) toks = xrealloc(toks, (size_t)(cap *= 2) * sizeof *toks);
        toks[ntoks] = scan_token();
        if (toks[ntoks++].type == T_EOF) break;
    }
    pos = 0;

    Vec v = {0};
    while (!at_end()) {
        Stmt *s = declaration();
        if (s) vec_push(&v, s);
    }

    Func *script = xmalloc(sizeof *script);
    memset(script, 0, sizeof *script);
    script->kind = FN_SCRIPT;
    script->name = (Token){T_IDENTIFIER, "script", 6, 0};
    script->id = next_func_id++;
    script->body = (Stmt **)v.v;
    script->nbody = v.n;
    return script;
}
