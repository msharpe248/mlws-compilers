/* The Lua 5.5 parser: recursive descent, as in lparser.c. It builds the AST
   and resolves names as it goes (locals, upvalues, globals and their
   declarations), checks gotos and labels, and records which variables are
   captured by closures and which are assigned. */
#include "lua.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

enum { D_LOCAL, D_GLOBAL, D_GLOBAL_ALL, D_GLOBAL_FN };

typedef struct {
    int kind;
    Var *var;           /* D_LOCAL */
    Str name;           /* D_GLOBAL */
    int attrib;         /* D_GLOBAL, D_GLOBAL_ALL */
} Decl;

typedef struct {
    Str name;
    Stmt *stmt;
    int nactive;
    int level;          /* block nesting level in the function */
    int line;
} LabelInfo;

typedef struct BlockScope {
    struct BlockScope *prev;
    int ndecl;          /* declarations before the block */
    int nactive;        /* active locals before the block */
    bool loop;
    int level;
} BlockScope;

typedef struct FuncState {
    struct FuncState *prev;
    Func *f;
    BlockScope *bl;
    int ndecl;          /* declarations before the function */
    int nactive;
    int level;
    VEC(LabelInfo) labels;  /* visible labels */
    VEC(LabelInfo) gotos;   /* pending gotos (stmt, nactive, level) */
    VEC(Var *) active;      /* the active locals, in order */
} FuncState;

typedef struct {
    Lexer lx;
    FuncState *fs;
    VEC(Decl) decls;
    int *func_ids;
    int var_ids;
    bool prelude;
} Parser;

static Block *block(Parser *p);
static Expr *expr(Parser *p);
static Expr *subexpr(Parser *p, int limit);
static Func *funcbody(Parser *p, bool self, int line, const char *name);

/* ----------------------------------------------------------------- errors */

static char *near_text(Token *t) {
    char buf[64];
    if (t->type == T_EOF)
        return xstrdup("<eof>");
    if (t->type == T_NAME || t->type == T_STRING || t->type == T_INT || t->type == T_FLT)
        return xstrndup(t->raw, (size_t)(t->rawlen > 60 ? 60 : t->rawlen));
    return xstrdup(tok_text(t->type, buf, sizeof buf));
}

static _Noreturn void error_at(Parser *p, const char *fmt, ...) {
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    lex_error(&p->lx, p->lx.t.line, near_text(&p->lx.t), "%s", msg);
}

static _Noreturn void error_line(Parser *p, int line, const char *fmt, ...) {
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    lex_error(&p->lx, line, NULL, "%s", msg);
}

static int tok(Parser *p) { return p->lx.t.type; }
static void next(Parser *p) { lex_next(&p->lx); }

static bool test(Parser *p, int type) {
    if (tok(p) != type)
        return false;
    next(p);
    return true;
}

static void check(Parser *p, int type) {
    char buf[16];
    if (tok(p) != type)
        error_at(p, "'%s' expected", tok_text(type, buf, sizeof buf));
}

static void expect(Parser *p, int type) {
    check(p, type);
    next(p);
}

/* Expect the token closing what opened at line (if on another line). */
static void match(Parser *p, int what, int who, int line) {
    char b1[16], b2[16];
    if (tok(p) == what) {
        next(p);
        return;
    }
    if (line == p->lx.t.line)
        error_at(p, "'%s' expected", tok_text(what, b1, sizeof b1));
    error_at(p, "'%s' expected (to close '%s' at line %d)", tok_text(what, b1, sizeof b1),
             tok_text(who, b2, sizeof b2), line);
}

static Str name(Parser *p) {
    check(p, T_NAME);
    Str s = p->lx.t.s;
    next(p);
    return s;
}

static char *cstr(Str s) { return xstrndup(s.s, (size_t)s.len); }

/* ------------------------------------------------------------------- nodes */

static Expr *new_expr(XKind k, int line) {
    Expr *e = xcalloc(1, sizeof *e);
    e->k = k;
    e->line = line;
    return e;
}

static Stmt *new_stmt(SKind k, int line) {
    Stmt *s = xcalloc(1, sizeof *s);
    s->k = k;
    s->line = line;
    return s;
}

static Expr *str_expr(Str s, int line) {
    Expr *e = new_expr(X_STR, line);
    e->s = s;
    return e;
}

/* ----------------------------------------------------------------- scopes */

static Var *new_var(Parser *p, Str nm, int attrib) {
    Var *v = xcalloc(1, sizeof *v);
    v->name = nm;
    v->fn = p->fs->f;
    v->attrib = attrib;
    v->id = p->var_ids++;
    vec_push(p->fs->f->locals, v);
    return v;
}

/* Bring a declared variable into scope. */
static void activate(Parser *p, Var *v) {
    Decl d = {D_LOCAL, v, v->name, 0};
    vec_push(p->decls, d);
    vec_push(p->fs->active, v);
    p->fs->nactive++;
}

static void enter_block(Parser *p, BlockScope *bl, bool loop) {
    FuncState *fs = p->fs;
    bl->prev = fs->bl;
    bl->ndecl = p->decls.n;
    bl->nactive = fs->nactive;
    bl->loop = loop;
    bl->level = ++fs->level;
    fs->bl = bl;
}

static void leave_block(Parser *p) {
    FuncState *fs = p->fs;
    BlockScope *bl = fs->bl;
    /* labels of the block go out of sight; its pending gotos move out */
    int k = 0;
    for (int i = 0; i < fs->labels.n; i++)
        if (fs->labels.v[i].level < bl->level)
            fs->labels.v[k++] = fs->labels.v[i];
    fs->labels.n = k;
    for (int i = 0; i < fs->gotos.n; i++)
        if (fs->gotos.v[i].level >= bl->level) {
            fs->gotos.v[i].level = bl->level - 1;
            if (fs->gotos.v[i].nactive > bl->nactive)
                fs->gotos.v[i].nactive = bl->nactive;
        }
    p->decls.n = bl->ndecl;
    fs->active.n = bl->nactive;
    fs->nactive = bl->nactive;
    fs->level--;
    fs->bl = bl->prev;
}

/* Mark v as used by the current function: an upvalue of each function
   between the current one and the one that declares v. */
static void use_var(Parser *p, Var *v) {
    for (FuncState *fs = p->fs; fs && fs->f != v->fn; fs = fs->prev) {
        v->captured = true;
        bool have = false;
        for (int i = 0; i < fs->f->upvals.n; i++)
            if (fs->f->upvals.v[i] == v)
                have = true;
        if (!have)
            vec_push(fs->f->upvals, v);
    }
}

/* Resolve a name: a local (or upvalue), or a global. For a global, *attrib
   gets its declared attribute; -1 means the name is undeclared under a
   global declaration (an error). */
static Var *find_local(Parser *p, Str nm, int *attrib) {
    bool voided = false;
    *attrib = ATTR_NONE;
    for (int i = p->decls.n - 1; i >= 0; i--) {
        Decl *d = &p->decls.v[i];
        if (d->kind == D_LOCAL) {
            if (str_same(d->var->name, nm))
                return d->var;
        } else if (d->kind == D_GLOBAL || d->kind == D_GLOBAL_FN) {
            if (str_same(d->name, nm)) {
                *attrib = d->attrib;
                return NULL;
            }
            voided = true;
        } else {
            *attrib = d->attrib;
            return NULL;
        }
    }
    if (voided)
        *attrib = -1;
    return NULL;
}

static Func *top_func(Parser *p) {
    FuncState *top = p->fs;
    while (top->prev)
        top = top->prev;
    return top->f;
}

static Expr *singlevar(Parser *p, Str nm, int line) {
    int attrib;
    Var *v = find_local(p, nm, &attrib);
    if (v) {
        /* the main chunk's _ENV lives in a global of the program, so
           closures need not capture it */
        Expr *e = new_expr(X_LOCAL, line);
        e->var = v;
        if (v != top_func(p)->env) {
            use_var(p, v);
            if (v->fn != p->fs->f) {
                UpRef r = {v, e};
                vec_push(p->fs->f->uprefs, r);
            }
        }
        return e;
    }
    if (attrib < 0)
        error_line(p, line, "variable '%.*s' not declared", nm.len, nm.s);
    Expr *e = new_expr(X_GLOBAL, line);
    e->s = nm;
    e->i = attrib;      /* ATTR_CONST: a read-only global */
    int a2;
    Var *env = find_local(p, (Str){"_ENV", 4}, &a2);
    if (env && env != top_func(p)->env) {
        use_var(p, env);
        e->env = new_expr(X_LOCAL, line);
        e->env->var = env;
        if (env->fn != p->fs->f) {
            UpRef r = {env, e->env};
            vec_push(p->fs->f->uprefs, r);
        }
    }
    return e;
}

/* ------------------------------------------------------------ expressions */

static void push_expr(Expr ***v, int *n, int *cap, Expr *e) {
    if (*n == *cap) {
        *cap = *cap ? 2 * *cap : 4;
        *v = xrealloc(*v, (size_t)*cap * sizeof **v);
    }
    (*v)[(*n)++] = e;
}

static int explist(Parser *p, Expr ***out) {
    Expr **v = NULL;
    int n = 0, cap = 0;
    push_expr(&v, &n, &cap, expr(p));
    while (test(p, T_CHAR + ','))
        push_expr(&v, &n, &cap, expr(p));
    *out = v;
    return n;
}

static Expr *table_cons(Parser *p) {
    int line = p->lx.t.line;
    Expr *e = new_expr(X_TABLE, line);
    int cap = 0;
    expect(p, T_CHAR + '{');
    while (tok(p) != T_CHAR + '}') {
        Field f = {0};
        if (tok(p) == T_NAME && lex_peek(&p->lx) == T_CHAR + '=') {
            f.key = str_expr(p->lx.t.s, p->lx.t.line);
            next(p);
            next(p);
            f.val = expr(p);
        } else if (tok(p) == T_CHAR + '[') {
            next(p);
            f.key = expr(p);
            expect(p, T_CHAR + ']');
            expect(p, T_CHAR + '=');
            f.val = expr(p);
        } else {
            f.val = expr(p);
        }
        if (e->nfields == cap) {
            cap = cap ? 2 * cap : 4;
            e->fields = xrealloc(e->fields, (size_t)cap * sizeof *e->fields);
        }
        e->fields[e->nfields++] = f;
        if (!test(p, T_CHAR + ',') && !test(p, T_CHAR + ';'))
            break;
    }
    match(p, T_CHAR + '}', T_CHAR + '{', line);
    return e;
}

static void call_args(Parser *p, Expr *call) {
    int line = p->lx.t.line;
    if (tok(p) == T_STRING) {
        call->args = xmalloc(sizeof *call->args);
        call->args[0] = str_expr(p->lx.t.s, line);
        call->nargs = 1;
        next(p);
    } else if (tok(p) == T_CHAR + '{') {
        call->args = xmalloc(sizeof *call->args);
        call->args[0] = table_cons(p);
        call->nargs = 1;
    } else if (tok(p) == T_CHAR + '(') {
        next(p);
        if (tok(p) != T_CHAR + ')')
            call->nargs = explist(p, &call->args);
        match(p, T_CHAR + ')', T_CHAR + '(', line);
    } else {
        error_at(p, "function arguments expected");
    }
}

static Expr *primary(Parser *p) {
    int line = p->lx.t.line;
    if (tok(p) == T_NAME)
        return singlevar(p, name(p), line);
    if (tok(p) == T_CHAR + '(') {
        next(p);
        Expr *e = new_expr(X_PAREN, line);
        e->a = expr(p);
        match(p, T_CHAR + ')', T_CHAR + '(', line);
        return e;
    }
    error_at(p, "unexpected symbol");
}

static Expr *suffixed(Parser *p) {
    Expr *e = primary(p);
    for (;;) {
        int line = p->lx.t.line;
        switch (tok(p)) {
        case T_CHAR + '.': {
            next(p);
            Expr *x = new_expr(X_INDEX, line);
            x->a = e;
            x->b = str_expr(name(p), line);
            e = x;
            break;
        }
        case T_CHAR + '[': {
            next(p);
            Expr *x = new_expr(X_INDEX, line);
            x->a = e;
            x->b = expr(p);
            expect(p, T_CHAR + ']');
            e = x;
            break;
        }
        case T_CHAR + ':': {
            next(p);
            Expr *x = new_expr(X_METHOD, line);
            x->a = e;
            x->s = name(p);
            call_args(p, x);
            e = x;
            break;
        }
        case T_CHAR + '(': case T_STRING: case T_CHAR + '{': {
            Expr *x = new_expr(X_CALL, line);
            x->a = e;
            call_args(p, x);
            if (e->k == X_LOCAL) {
                /* remembered for calling local functions directly */
                e->callee = true;
                e->i = x->nargs;
                XKind last = x->nargs ? x->args[x->nargs - 1]->k : X_NIL;
                e->op = last == X_CALL || last == X_METHOD || last == X_VARARG;
            }
            e = x;
            break;
        }
        default:
            return e;
        }
    }
}

static Expr *simple(Parser *p) {
    Token *t = &p->lx.t;
    Expr *e;
    switch (t->type) {
    case T_FLT:
        e = new_expr(X_FLT, t->line);
        e->f = t->f;
        break;
    case T_INT:
        e = new_expr(X_INT, t->line);
        e->i = t->i;
        break;
    case T_STRING:
        e = str_expr(t->s, t->line);
        break;
    case T_NIL: e = new_expr(X_NIL, t->line); break;
    case T_TRUE: e = new_expr(X_TRUE, t->line); break;
    case T_FALSE: e = new_expr(X_FALSE, t->line); break;
    case T_DOTS:
        if (!p->fs->f->vararg)
            error_at(p, "cannot use '...' outside a vararg function");
        p->fs->f->uses_vararg = true;
        e = new_expr(X_VARARG, t->line);
        break;
    case T_CHAR + '{':
        return table_cons(p);
    case T_FUNCTION: {
        int line = t->line;
        next(p);
        e = new_expr(X_FUNC, line);
        e->fn = funcbody(p, false, line, "anon");
        return e;
    }
    default:
        return suffixed(p);
    }
    next(p);
    return e;
}

static int unop(int t) {
    switch (t) {
    case T_NOT: return OP_NOT;
    case T_CHAR + '-': return OP_UNM;
    case T_CHAR + '~': return OP_BNOT;
    case T_CHAR + '#': return OP_LEN;
    default: return -1;
    }
}

static int binop(int t) {
    switch (t) {
    case T_CHAR + '+': return OP_ADD;
    case T_CHAR + '-': return OP_SUB;
    case T_CHAR + '*': return OP_MUL;
    case T_CHAR + '%': return OP_MOD;
    case T_CHAR + '^': return OP_POW;
    case T_CHAR + '/': return OP_DIV;
    case T_IDIV: return OP_IDIV;
    case T_CHAR + '&': return OP_BAND;
    case T_CHAR + '|': return OP_BOR;
    case T_CHAR + '~': return OP_BXOR;
    case T_SHL: return OP_SHL;
    case T_SHR: return OP_SHR;
    case T_CONCAT: return OP_CONCAT;
    case T_NE: return OP_NE;
    case T_EQ: return OP_EQ;
    case T_CHAR + '<': return OP_LT;
    case T_LE: return OP_LE;
    case T_CHAR + '>': return OP_GT;
    case T_GE: return OP_GE;
    case T_AND: return 100;
    case T_OR: return 101;
    default: return -1;
    }
}

static const struct { int left, right; } priority[] = {
    [OP_ADD] = {10, 10}, [OP_SUB] = {10, 10}, [OP_MUL] = {11, 11}, [OP_MOD] = {11, 11},
    [OP_POW] = {14, 13}, [OP_DIV] = {11, 11}, [OP_IDIV] = {11, 11},
    [OP_BAND] = {6, 6}, [OP_BOR] = {4, 4}, [OP_BXOR] = {5, 5}, [OP_SHL] = {7, 7}, [OP_SHR] = {7, 7},
    [OP_CONCAT] = {9, 8},
    [OP_EQ] = {3, 3}, [OP_NE] = {3, 3}, [OP_LT] = {3, 3}, [OP_LE] = {3, 3}, [OP_GT] = {3, 3}, [OP_GE] = {3, 3},
};
#define UNARY_PRIORITY 12

static void prio(int op, int *l, int *r) {
    if (op == 100) { *l = *r = 2; return; }
    if (op == 101) { *l = *r = 1; return; }
    *l = priority[op].left;
    *r = priority[op].right;
}

static Expr *subexpr(Parser *p, int limit) {
    Expr *e;
    int u = unop(tok(p));
    if (u >= 0) {
        int line = p->lx.t.line;
        next(p);
        e = new_expr(X_UN, line);
        e->op = u;
        e->a = subexpr(p, UNARY_PRIORITY);
    } else {
        e = simple(p);
    }
    for (;;) {
        int op = binop(tok(p)), l, r;
        if (op < 0)
            break;
        prio(op, &l, &r);
        if (l <= limit)
            break;
        int line = p->lx.t.line;
        next(p);
        Expr *b = subexpr(p, r);
        Expr *x = new_expr(op == 100 ? X_AND : op == 101 ? X_OR : X_BIN, line);
        x->op = op;
        x->a = e;
        x->b = b;
        e = x;
    }
    return e;
}

static Expr *expr(Parser *p) { return subexpr(p, 0); }

/* -------------------------------------------------------------- functions */

static void open_func(Parser *p, FuncState *fs, Func *f) {
    memset(fs, 0, sizeof *fs);
    fs->prev = p->fs;
    fs->f = f;
    fs->ndecl = p->decls.n;
    f->parent = p->fs ? p->fs->f : NULL;
    if (f->parent)
        vec_push(f->parent->children, f);
    f->id = (*p->func_ids)++;
    f->prelude = p->prelude;
    p->fs = fs;
}

static void close_func(Parser *p) {
    FuncState *fs = p->fs;
    if (fs->gotos.n) {
        LabelInfo *g = &fs->gotos.v[0];
        if (str_eq(g->name, "break"))
            error_line(p, g->line, "break outside a loop at line %d", g->line);
        error_line(p, g->line, "no visible label '%.*s' for goto at line %d", g->name.len, g->name.s,
                   g->line);
    }
    p->decls.n = fs->ndecl;
    p->fs = fs->prev;
}

static char *func_name_of(Expr *target) {
    if (!target)
        return xstrdup("anon");
    if (target->k == X_LOCAL)
        return cstr(target->var->name);
    if (target->k == X_GLOBAL)
        return cstr(target->s);
    if (target->k == X_INDEX && target->b->k == X_STR) {
        char *a = func_name_of(target->a), *b = cstr(target->b->s);
        char *r = xmalloc(strlen(a) + strlen(b) + 2);
        sprintf(r, "%s.%s", a, b);
        free(a);
        free(b);
        return r;
    }
    return xstrdup("anon");
}

static Func *funcbody(Parser *p, bool self, int line, const char *fname) {
    Func *f = xcalloc(1, sizeof *f);
    FuncState fs;
    open_func(p, &fs, f);
    f->line = line;
    f->name = xstrdup(fname);
    BlockScope bl;
    enter_block(p, &bl, false);
    VEC(Var *) params = {0};
    if (self) {
        Var *v = new_var(p, (Str){"self", 4}, ATTR_NONE);
        v->param = true;
        vec_push(params, v);
        f->has_self = true;
    }
    expect(p, T_CHAR + '(');
    if (tok(p) != T_CHAR + ')') {
        do {
            if (tok(p) == T_NAME) {
                Var *v = new_var(p, name(p), ATTR_NONE);
                v->param = true;
                vec_push(params, v);
            } else if (tok(p) == T_DOTS) {
                next(p);
                f->vararg = true;
                if (tok(p) == T_NAME) {
                    f->vaname = new_var(p, name(p), ATTR_CONST);
                    f->vaname->vatable = true;
                }
                break;
            } else {
                error_at(p, "<name> expected");
            }
        } while (test(p, T_CHAR + ','));
    }
    f->params = params.v;
    f->np = params.n;
    for (int i = 0; i < params.n; i++)
        activate(p, params.v[i]);
    if (f->vaname)
        activate(p, f->vaname);
    expect(p, T_CHAR + ')');
    f->body = block(p);
    f->lastline = p->lx.t.line;
    match(p, T_END, T_FUNCTION, line);
    leave_block(p);
    close_func(p);
    return f;
}

/* ------------------------------------------------------------- statements */

static bool block_follow(Parser *p, bool with_until) {
    switch (tok(p)) {
    case T_ELSE: case T_ELSEIF: case T_END: case T_EOF:
        return true;
    case T_UNTIL:
        return with_until;
    default:
        return false;
    }
}

static void check_assignable(Parser *p, Expr *e) {
    if (e->k == X_LOCAL) {
        if (e->var->attrib != ATTR_NONE)
            error_line(p, e->line, "attempt to assign to const variable '%.*s'", e->var->name.len,
                       e->var->name.s);
        e->var->assigned = true;
    } else if (e->k == X_GLOBAL) {
        if (e->i == ATTR_CONST)
            error_line(p, e->line, "attempt to assign to const variable '%.*s'", e->s.len, e->s.s);
        if (!e->env) {
            /* the default _ENV is only read */
        }
    } else if (e->k != X_INDEX) {
        error_at(p, "syntax error");
    }
}

static Stmt *exprstat(Parser *p) {
    int line = p->lx.t.line;
    Expr *e = suffixed(p);
    if (tok(p) == T_CHAR + '=' || tok(p) == T_CHAR + ',') {
        Stmt *s = new_stmt(S_ASSIGN, line);
        Expr **t = NULL;
        int n = 0, cap = 0;
        push_expr(&t, &n, &cap, e);
        while (test(p, T_CHAR + ','))
            push_expr(&t, &n, &cap, suffixed(p));
        expect(p, T_CHAR + '=');
        for (int i = 0; i < n; i++)
            check_assignable(p, t[i]);
        s->targets = t;
        s->ntargets = n;
        s->nexprs = explist(p, &s->exprs);
        return s;
    }
    if (e->k != X_CALL && e->k != X_METHOD)
        error_at(p, "syntax error");
    Stmt *s = new_stmt(S_EXPR, line);
    s->exprs = xmalloc(sizeof *s->exprs);
    s->exprs[0] = e;
    s->nexprs = 1;
    return s;
}

static int attrib(Parser *p) {
    if (!test(p, T_CHAR + '<'))
        return -1;
    Str a = name(p);
    expect(p, T_CHAR + '>');
    if (str_eq(a, "const"))
        return ATTR_CONST;
    if (str_eq(a, "close"))
        return ATTR_CLOSE;
    error_at(p, "unknown attribute '%.*s'", a.len, a.s);
}

static Stmt *localstat(Parser *p, int line) {
    Stmt *s = new_stmt(S_LOCAL, line);
    VEC(Var *) vars = {0};
    int pre = attrib(p), nclose = 0;
    do {
        Str nm = name(p);
        int a = attrib(p);
        if (a < 0)
            a = pre < 0 ? ATTR_NONE : pre;
        if (a == ATTR_CLOSE && ++nclose > 1)
            error_at(p, "multiple to-be-closed variables in local list");
        vec_push(vars, new_var(p, nm, a));
    } while (test(p, T_CHAR + ','));
    if (test(p, T_CHAR + '='))
        s->nexprs = explist(p, &s->exprs);
    s->vars = vars.v;
    s->nvars = vars.n;
    for (int i = 0; i < vars.n && i < s->nexprs; i++) {
        XKind k = s->exprs[i]->k;
        bool multi = k == X_CALL || k == X_METHOD || k == X_VARARG;
        if (!(multi && i == s->nexprs - 1))
            vars.v[i]->init = s->exprs[i];
    }
    for (int i = 0; i < vars.n; i++)
        activate(p, vars.v[i]);
    return s;
}

/* global [attrib] '*' | global attnamelist ['=' explist] | global function */
static Stmt *globalstat(Parser *p, int line) {
    if (test(p, T_FUNCTION)) {
        Str nm = name(p);
        Decl d = {D_GLOBAL_FN, NULL, nm, ATTR_NONE};
        vec_push(p->decls, d);
        Stmt *s = new_stmt(S_GLOBALFN, line);
        s->name = nm;
        Expr *target = singlevar(p, nm, line);
        s->targets = xmalloc(sizeof *s->targets);
        s->targets[0] = target;
        s->ntargets = 1;
        s->fn = funcbody(p, false, line, cstr(nm));
        return s;
    }
    int pre = attrib(p);
    if (pre == ATTR_CLOSE)
        error_at(p, "global variables cannot be to-be-closed");
    if (test(p, T_CHAR + '*')) {
        Decl d = {D_GLOBAL_ALL, NULL, {0}, pre < 0 ? ATTR_NONE : pre};
        vec_push(p->decls, d);
        return NULL;
    }
    VEC(Decl) names = {0};
    do {
        Str nm = name(p);
        int a = attrib(p);
        if (a < 0)
            a = pre < 0 ? ATTR_NONE : pre;
        if (a == ATTR_CLOSE)
            error_at(p, "global variables cannot be to-be-closed");
        Decl d = {D_GLOBAL, NULL, nm, a};
        vec_push(names, d);
    } while (test(p, T_CHAR + ','));
    Stmt *s = NULL;
    if (test(p, T_CHAR + '=')) {
        s = new_stmt(S_ASSIGN, line);
        s->check_undef = true;
        s->nexprs = explist(p, &s->exprs);
    }
    for (int i = 0; i < names.n; i++)
        vec_push(p->decls, names.v[i]);
    if (s) {
        s->targets = xmalloc((size_t)names.n * sizeof *s->targets);
        for (int i = 0; i < names.n; i++) {
            Expr *t = singlevar(p, names.v[i].name, line);
            t->i = ATTR_NONE;       /* the initialisation may assign a const */
            s->targets[i] = t;
        }
        s->ntargets = names.n;
    }
    return s;
}

static Stmt *funcstat(Parser *p, int line) {
    next(p);
    int nline = p->lx.t.line;
    Expr *target = singlevar(p, name(p), nline);
    bool self = false;
    while (tok(p) == T_CHAR + '.' || tok(p) == T_CHAR + ':') {
        bool colon = tok(p) == T_CHAR + ':';
        next(p);
        Expr *x = new_expr(X_INDEX, nline);
        x->a = target;
        x->b = str_expr(name(p), nline);
        target = x;
        if (colon) {
            self = true;
            break;
        }
    }
    check_assignable(p, target);
    char *fname = func_name_of(target);
    Stmt *s = new_stmt(S_ASSIGN, line);
    s->targets = xmalloc(sizeof *s->targets);
    s->targets[0] = target;
    s->ntargets = 1;
    Expr *fe = new_expr(X_FUNC, line);
    fe->fn = funcbody(p, self, line, fname);
    s->exprs = xmalloc(sizeof *s->exprs);
    s->exprs[0] = fe;
    s->nexprs = 1;
    return s;
}

static Block *loop_body(Parser *p) {
    BlockScope bl;
    enter_block(p, &bl, true);
    Block *b = block(p);
    leave_block(p);
    return b;
}

static Stmt *forstat(Parser *p, int line) {
    next(p);
    Str n1 = name(p);
    BlockScope outer;
    if (tok(p) == T_CHAR + '=') {
        next(p);
        Stmt *s = new_stmt(S_NUMFOR, line);
        s->start = expr(p);
        expect(p, T_CHAR + ',');
        s->limit = expr(p);
        if (test(p, T_CHAR + ','))
            s->step = expr(p);
        expect(p, T_DO);
        enter_block(p, &outer, true);
        s->var = new_var(p, n1, ATTR_CONST);
        activate(p, s->var);
        BlockScope inner;
        enter_block(p, &inner, false);
        s->body = block(p);
        leave_block(p);
        leave_block(p);
        match(p, T_END, T_FOR, line);
        return s;
    }
    if (tok(p) != T_CHAR + ',' && tok(p) != T_IN)
        error_at(p, "'=' or 'in' expected");
    Stmt *s = new_stmt(S_GENFOR, line);
    VEC(Str) names = {0};
    vec_push(names, n1);
    while (test(p, T_CHAR + ','))
        vec_push(names, name(p));
    expect(p, T_IN);
    s->nexprs = explist(p, &s->exprs);
    expect(p, T_DO);
    enter_block(p, &outer, true);
    s->vars = xmalloc((size_t)names.n * sizeof *s->vars);
    s->nvars = names.n;
    for (int i = 0; i < names.n; i++) {
        s->vars[i] = new_var(p, names.v[i], ATTR_CONST);
        activate(p, s->vars[i]);
    }
    BlockScope inner;
    enter_block(p, &inner, false);
    s->body = block(p);
    leave_block(p);
    leave_block(p);
    match(p, T_END, T_FOR, line);
    return s;
}

static void new_goto(Parser *p, Stmt *s) {
    FuncState *fs = p->fs;
    /* a backward jump to a visible label */
    for (int i = fs->labels.n - 1; i >= 0; i--)
        if (str_same(fs->labels.v[i].name, s->name)) {
            s->label = fs->labels.v[i].stmt;
            s->label->backward = true;
            s->nactive = fs->nactive;
            return;
        }
    LabelInfo g = {s->name, s, fs->nactive, fs->level, s->line};
    s->nactive = fs->nactive;
    vec_push(fs->gotos, g);
}

static void new_label(Parser *p, Stmt *s, bool last) {
    FuncState *fs = p->fs;
    for (int i = 0; i < fs->labels.n; i++)
        if (str_same(fs->labels.v[i].name, s->name))
            error_line(p, s->line, "label '%.*s' already defined on line %d", s->name.len, s->name.s,
                       fs->labels.v[i].line);
    s->nactive = last ? fs->bl->nactive : fs->nactive;
    LabelInfo l = {s->name, s, s->nactive, fs->level, s->line};
    vec_push(fs->labels, l);
    /* pending gotos of this block (or moved out to it) that jump here */
    int k = 0;
    for (int i = 0; i < fs->gotos.n; i++) {
        LabelInfo *g = &fs->gotos.v[i];
        if (g->level == fs->level && str_same(g->name, s->name)) {
            if (g->nactive < s->nactive) {
                Var *v = fs->active.v[g->nactive];
                error_line(p, p->lx.t.line, "<goto %.*s> at line %d jumps into the scope of '%.*s'",
                           g->name.len, g->name.s, g->line, v->name.len, v->name.s);
            }
            g->stmt->label = s;
        } else {
            fs->gotos.v[k++] = *g;
        }
    }
    fs->gotos.n = k;
}

static Stmt *statement(Parser *p) {
    int line = p->lx.t.line;
    switch (tok(p)) {
    case T_CHAR + ';':
        next(p);
        return NULL;
    case T_IF: {
        Stmt *s = new_stmt(S_IF, line);
        int cap = 0;
        do {
            next(p);    /* 'if' or 'elseif' */
            Expr *c = expr(p);
            expect(p, T_THEN);
            BlockScope bl;
            enter_block(p, &bl, false);
            Block *b = block(p);
            leave_block(p);
            if (s->nconds + 1 >= cap) {
                cap = cap ? 2 * cap : 4;
                s->conds = xrealloc(s->conds, (size_t)cap * sizeof *s->conds);
                s->blocks = xrealloc(s->blocks, (size_t)cap * sizeof *s->blocks);
            }
            s->conds[s->nconds] = c;
            s->blocks[s->nconds++] = b;
        } while (tok(p) == T_ELSEIF);
        s->blocks[s->nconds] = NULL;
        if (test(p, T_ELSE)) {
            BlockScope bl;
            enter_block(p, &bl, false);
            s->blocks[s->nconds] = block(p);
            leave_block(p);
        }
        match(p, T_END, T_IF, line);
        return s;
    }
    case T_WHILE: {
        next(p);
        Stmt *s = new_stmt(S_WHILE, line);
        s->cond = expr(p);
        expect(p, T_DO);
        s->body = loop_body(p);
        match(p, T_END, T_WHILE, line);
        return s;
    }
    case T_DO: {
        next(p);
        Stmt *s = new_stmt(S_DO, line);
        BlockScope bl;
        enter_block(p, &bl, false);
        s->body = block(p);
        leave_block(p);
        match(p, T_END, T_DO, line);
        return s;
    }
    case T_FOR:
        return forstat(p, line);
    case T_REPEAT: {
        next(p);
        Stmt *s = new_stmt(S_REPEAT, line);
        BlockScope bl;
        enter_block(p, &bl, true);
        s->body = block(p);
        match(p, T_UNTIL, T_REPEAT, line);
        s->cond = expr(p);     /* sees the body's locals */
        leave_block(p);
        return s;
    }
    case T_FUNCTION:
        return funcstat(p, line);
    case T_LOCAL:
        next(p);
        if (test(p, T_FUNCTION)) {
            Stmt *s = new_stmt(S_LOCALFN, line);
            Str nm = name(p);
            s->var = new_var(p, nm, ATTR_NONE);
            activate(p, s->var);
            s->fn = funcbody(p, false, line, cstr(nm));
            s->var->localfn = s->fn;
            return s;
        }
        return localstat(p, line);
    case T_GLOBAL:
        next(p);
        return globalstat(p, line);
    case T_DBCOLON: {
        next(p);
        Stmt *s = new_stmt(S_LABEL, line);
        s->name = name(p);
        expect(p, T_DBCOLON);
        /* skip other no-op statements, to see if the label ends the block */
        while (tok(p) == T_CHAR + ';' || tok(p) == T_DBCOLON) {
            if (tok(p) == T_CHAR + ';') {
                next(p);
                continue;
            }
            break;
        }
        new_label(p, s, block_follow(p, false));
        return s;
    }
    case T_RETURN: {
        next(p);
        Stmt *s = new_stmt(S_RETURN, line);
        if (!block_follow(p, true) && tok(p) != T_CHAR + ';')
            s->nexprs = explist(p, &s->exprs);
        test(p, T_CHAR + ';');
        if (!block_follow(p, true))
            error_at(p, "'<eof>' expected");
        return s;
    }
    case T_BREAK: {
        next(p);
        Stmt *s = new_stmt(S_BREAK, line);
        BlockScope *bl = p->fs->bl;
        while (bl && !bl->loop)
            bl = bl->prev;
        if (!bl)
            lex_error(&p->lx, line, "break", "break outside loop");
        return s;
    }
    case T_GOTO: {
        next(p);
        Stmt *s = new_stmt(S_GOTO, line);
        s->name = name(p);
        new_goto(p, s);
        return s;
    }
    default:
        return exprstat(p);
    }
}

static Block *block(Parser *p) {
    Block *b = xcalloc(1, sizeof *b);
    int cap = 0;
    while (!block_follow(p, true)) {
        bool ret = tok(p) == T_RETURN;
        Stmt *s = statement(p);
        if (!s)
            continue;
        if (b->n == cap) {
            cap = cap ? 2 * cap : 8;
            b->stmts = xrealloc(b->stmts, (size_t)cap * sizeof *b->stmts);
        }
        b->stmts[b->n++] = s;
        if (s->k == S_LOCAL)
            for (int i = 0; i < s->nvars; i++)
                if (s->vars[i]->attrib == ATTR_CLOSE)
                    b->has_close = true;
        if (ret)
            break;
    }
    return b;
}

Func *parse_chunk(const char *chunk, const char *src, size_t len, bool prelude, int *func_ids) {
    Parser p = {0};
    p.func_ids = func_ids;
    p.prelude = prelude;
    lex_init(&p.lx, chunk, src, len);
    Func *f = xcalloc(1, sizeof *f);
    FuncState fs;
    open_func(&p, &fs, f);
    f->name = xstrdup(prelude ? "prelude" : "main");
    f->is_main = true;
    f->vararg = true;
    f->line = 0;
    BlockScope bl;
    enter_block(&p, &bl, false);
    f->env = new_var(&p, (Str){"_ENV", 4}, ATTR_NONE);
    activate(&p, f->env);
    f->body = block(&p);
    f->lastline = p.lx.t.line;
    if (tok(&p) != T_EOF)
        error_at(&p, "'<eof>' expected");
    leave_block(&p);
    close_func(&p);
    return f;
}
