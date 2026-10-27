/* The resolver. As in jlox it binds each variable reference to a local
 * variable or a global and reports the static errors. It also works out what
 * code generation needs about closures: which locals are captured by nested
 * functions and which are assigned after their declaration, and for every
 * function the list of variables it captures (its upvalues), as clox does. */
#include <string.h>

#include "lox.h"

typedef struct {
    Var *var;
    bool defined;
} Entry;

typedef struct {
    Entry *v;
    int n, cap;
} Scope;

enum { CLASS_NONE, CLASS_CLASS, CLASS_SUBCLASS };

static Scope *scopes;
static int nscopes, scopes_cap;
static Func *cur_fn;
static int class_type;
static Globals *globals;

static void resolve_stmt(Stmt *s);
static void resolve_expr(Expr *e);

static bool same(Token *a, const char *s, int n) {
    return a->length == n && memcmp(a->start, s, (size_t)n) == 0;
}

static void begin_scope(void) {
    if (nscopes == scopes_cap) {
        scopes_cap = scopes_cap ? scopes_cap * 2 : 16;
        scopes = xrealloc(scopes, (size_t)scopes_cap * sizeof *scopes);
    }
    scopes[nscopes++] = (Scope){0};
}

static void end_scope(void) { nscopes--; }

static Var *new_var(Token name) {
    Var *v = xmalloc(sizeof *v);
    memset(v, 0, sizeof *v);
    v->name = name;
    v->fn = cur_fn;
    v->slot = -1;
    return v;
}

static Var *add_to_scope(Var *v, bool defined) {
    Scope *s = &scopes[nscopes - 1];
    if (s->n == s->cap) {
        s->cap = s->cap ? s->cap * 2 : 8;
        s->v = xrealloc(s->v, (size_t)s->cap * sizeof *s->v);
    }
    s->v[s->n++] = (Entry){v, defined};
    return v;
}

/* Declare a name in the innermost scope; NULL for a global. */
static Var *declare(Token *name) {
    if (nscopes == 0) {
        global_get(globals, name->start, name->length)->decls++;
        return NULL;
    }
    Scope *s = &scopes[nscopes - 1];
    for (int i = 0; i < s->n; i++)
        if (same(&s->v[i].var->name, name->start, name->length))
            error_at(name, "Already a variable with this name in this scope.");
    return add_to_scope(new_var(*name), false);
}

static void define(Var *v) {
    if (!v) return;
    Scope *s = &scopes[nscopes - 1];
    for (int i = s->n - 1; i >= 0; i--)
        if (s->v[i].var == v) s->v[i].defined = true;
}

static int add_upvalue(Func *f, Var *v, bool from_local, int index) {
    for (int i = 0; i < f->nups; i++)
        if (f->ups[i].var == v) return i;
    f->ups = xrealloc(f->ups, (size_t)(f->nups + 1) * sizeof *f->ups);
    f->ups[f->nups] = (Upval){v, from_local, index};
    return f->nups++;
}

/* The index of v among f's upvalues, adding it to f and to every function
 * between f and the one that declares v. */
static int resolve_upvalue(Func *f, Var *v) {
    Func *enc = f->enclosing;
    if (v->fn == enc) return add_upvalue(f, v, true, -1);
    return add_upvalue(f, v, false, resolve_upvalue(enc, v));
}

/* Bind a reference: the variable, with *up set to -1 for a local of the
 * current function or its upvalue index. NULL means a global. */
static Var *resolve_local(Token *name, int *up) {
    *up = -1;
    for (int i = nscopes - 1; i >= 0; i--) {
        Scope *s = &scopes[i];
        for (int j = s->n - 1; j >= 0; j--) {
            Var *v = s->v[j].var;
            if (!same(&v->name, name->start, name->length)) continue;
            if (v->fn != cur_fn) {
                v->captured = true;
                *up = resolve_upvalue(cur_fn, v);
            }
            return v;
        }
    }
    global_get(globals, name->start, name->length);
    return NULL;
}

static void resolve_function(Func *f) {
    Func *enclosing = cur_fn;
    f->enclosing = enclosing;
    cur_fn = f;
    begin_scope();
    if (f->kind == FN_METHOD || f->kind == FN_INITIALIZER) {
        Token t = {T_THIS, "this", 4, f->name.line};
        f->this_var = add_to_scope(new_var(t), true);
    }
    f->param_vars = xmalloc((size_t)(f->nparams + 1) * sizeof *f->param_vars);
    for (int i = 0; i < f->nparams; i++) {
        f->param_vars[i] = declare(&f->params[i]);
        define(f->param_vars[i]);
    }
    for (int i = 0; i < f->nbody; i++) resolve_stmt(f->body[i]);
    end_scope();
    cur_fn = enclosing;
}

static void resolve_expr(Expr *e) {
    switch (e->kind) {
    case E_NUMBER: case E_STRING: case E_NIL: case E_TRUE: case E_FALSE:
        break;
    case E_GROUP: case E_UNARY:
        resolve_expr(e->a);
        break;
    case E_BINARY: case E_LOGICAL:
        resolve_expr(e->a);
        resolve_expr(e->b);
        break;
    case E_VAR:
        if (nscopes > 0) {
            Scope *s = &scopes[nscopes - 1];
            for (int i = 0; i < s->n; i++)
                if (!s->v[i].defined && same(&s->v[i].var->name, e->tok.start, e->tok.length))
                    error_at(&e->tok, "Can't read local variable in its own initializer.");
        }
        e->var = resolve_local(&e->tok, &e->up);
        break;
    case E_ASSIGN:
        resolve_expr(e->a);
        e->var = resolve_local(&e->tok, &e->up);
        if (e->var) e->var->assigned = true;
        else global_get(globals, e->tok.start, e->tok.length)->assigns++;
        break;
    case E_CALL:
        resolve_expr(e->a);
        for (int i = 0; i < e->nargs; i++) resolve_expr(e->args[i]);
        break;
    case E_GET:
        resolve_expr(e->a);
        break;
    case E_SET:
        resolve_expr(e->b);
        resolve_expr(e->a);
        break;
    case E_THIS:
        if (class_type == CLASS_NONE) {
            error_at(&e->tok, "Can't use 'this' outside of a class.");
            break;
        }
        e->var = resolve_local(&e->tok, &e->up);
        break;
    case E_SUPER: {
        /* The error is reported at the 'super' keyword, which is not kept;
         * its lexeme and line are those of the expression. */
        Token kw = {T_SUPER, "super", 5, e->line};
        if (class_type == CLASS_NONE) {
            error_at(&kw, "Can't use 'super' outside of a class.");
            break;
        }
        if (class_type != CLASS_SUBCLASS) {
            error_at(&kw, "Can't use 'super' in a class with no superclass.");
            break;
        }
        e->var = resolve_local(&kw, &e->up);
        Token th = {T_THIS, "this", 4, e->line};
        e->this_var = resolve_local(&th, &e->this_up);
        break;
    }
    }
}

static void resolve_stmt(Stmt *s) {
    switch (s->kind) {
    case S_EXPR: case S_PRINT:
        resolve_expr(s->expr);
        break;
    case S_VAR:
        s->var = declare(&s->name);
        if (s->expr) resolve_expr(s->expr);
        define(s->var);
        break;
    case S_BLOCK:
        begin_scope();
        for (int i = 0; i < s->n; i++) resolve_stmt(s->stmts[i]);
        end_scope();
        break;
    case S_IF:
        resolve_expr(s->expr);
        resolve_stmt(s->then);
        if (s->els) resolve_stmt(s->els);
        break;
    case S_WHILE:
        resolve_expr(s->expr);
        resolve_stmt(s->then);
        break;
    case S_FUN: {
        bool top = nscopes == 0;
        s->var = declare(&s->name);
        define(s->var);
        if (top) {
            Global *g = global_get(globals, s->name.start, s->name.length);
            g->fun = s;
        }
        resolve_function(s->fn);
        break;
    }
    case S_RETURN:
        if (cur_fn->kind == FN_SCRIPT) error_at(&s->name, "Can't return from top-level code.");
        if (s->expr) {
            if (cur_fn->kind == FN_INITIALIZER)
                error_at(&s->name, "Can't return a value from an initializer.");
            resolve_expr(s->expr);
        }
        break;
    case S_CLASS: {
        int enclosing_class = class_type;
        class_type = CLASS_CLASS;
        if (nscopes == 0) global_get(globals, s->name.start, s->name.length)->cls = s;
        s->var = declare(&s->name);
        define(s->var);
        if (s->expr) {
            if (same(&s->expr->tok, s->name.start, s->name.length))
                error_at(&s->expr->tok, "A class can't inherit from itself.");
            class_type = CLASS_SUBCLASS;
            resolve_expr(s->expr);
            begin_scope();
            Token t = {T_SUPER, "super", 5, s->line};
            s->super_var = add_to_scope(new_var(t), true);
        }
        for (int i = 0; i < s->n; i++) {
            /* methods are not variables, so declare nothing */
            resolve_function(s->stmts[i]->fn);
        }
        if (s->expr) end_scope();
        class_type = enclosing_class;
        break;
    }
    }
}

void resolve_program(Func *script, Globals *g) {
    globals = g;
    cur_fn = script;
    class_type = CLASS_NONE;
    nscopes = 0;
    for (int i = 0; i < script->nbody; i++) resolve_stmt(script->body[i]);
}
