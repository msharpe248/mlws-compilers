/* The preprocessor (macros, conditionals, includes) and the parser: tokens
 * to a module's syntax tree. Records are kept as N_RECORD nodes and
 * expanded by the code generator, which knows patterns from expressions. */
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "erl.h"

/* ------------------------------------------------------------------ preprocessor */

typedef struct {
    const char *name;
    int nparams;            /* -1: no parameter list */
    const char **params;
    Tokens body;
} Macro;

typedef struct {
    VEC(Macro) macros;
    const char *module;
    const char *func_name;  /* the function whose clause is being read, for ?FUNCTION_NAME */
    int func_arity;
    VEC(const char *) incdirs;
    int depth;              /* include nesting */
} PP;

static Macro *find_macro(PP *pp, const char *name, int nargs) {
    Macro *any = NULL;
    for (int i = pp->macros.n - 1; i >= 0; i--) {
        Macro *m = &pp->macros.v[i];
        if (m->name == name) {
            if (m->nparams == nargs)
                return m;
            if (!any)
                any = m;
        }
    }
    return nargs < 0 ? any : NULL;
}

static bool is_defined(PP *pp, const char *name) {
    if (strcmp(name, "MODULE") == 0 || strcmp(name, "MODULE_STRING") == 0
        || strcmp(name, "FILE") == 0 || strcmp(name, "LINE") == 0
        || strcmp(name, "MACHINE") == 0 || strcmp(name, "OTP_RELEASE") == 0
        || strcmp(name, "BEAM") == 0)
        return true;
    for (int i = 0; i < pp->macros.n; i++)
        if (pp->macros.v[i].name == name)
            return true;
    return false;
}

static void undefine(PP *pp, const char *name) {
    for (int i = 0; i < pp->macros.n; i++)
        if (pp->macros.v[i].name == name)
            pp->macros.v[i].name = intern("", 0);
}

static char *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
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

static void preprocess(PP *pp, Tokens *in, Tokens *out);

static bool is_open(int t) {
    return t == T_CHAR + '(' || t == T_CHAR + '[' || t == T_CHAR + '{' || t == T_LTLT
        || t == T_CASE || t == T_IF || t == T_RECEIVE || t == T_TRY || t == T_BEGIN;
}

static bool is_close(int t) {
    return t == T_CHAR + ')' || t == T_CHAR + ']' || t == T_CHAR + '}' || t == T_GTGT || t == T_END;
}

/* Expand macro uses in toks[from, to) onto out. */
static void expand(PP *pp, Token *toks, int from, int to, Tokens *out, int level) {
    if (level > 100)
        fatal(toks[from].file, toks[from].line, "macro expansion too deep");
    for (int i = from; i < to; i++) {
        Token *t = &toks[i];
        if (t->type != T_CHAR + '?' || i + 1 >= to
            || (toks[i + 1].type != T_ATOM && toks[i + 1].type != T_VAR)) {
            if (t->type == T_QQ)
                fatal(t->file, t->line, "??Arg is not supported");
            vec_push(*out, *t);
            continue;
        }
        Token *nt = &toks[i + 1];
        const char *name = nt->s;
        Token r = *t;
        i++;
        if (strcmp(name, "MODULE") == 0 && !find_macro(pp, name, -1)) {
            r.type = T_ATOM;
            r.s = pp->module ? pp->module : intern("", 0);
            vec_push(*out, r);
            continue;
        }
        if (strcmp(name, "MODULE_STRING") == 0 && !find_macro(pp, name, -1)) {
            r.type = T_STRING;
            r.s = pp->module ? pp->module : "";
            r.len = (int)strlen(r.s);
            vec_push(*out, r);
            continue;
        }
        if (strcmp(name, "FILE") == 0 && !find_macro(pp, name, -1)) {
            r.type = T_STRING;
            r.s = t->file;
            r.len = (int)strlen(r.s);
            vec_push(*out, r);
            continue;
        }
        if (strcmp(name, "LINE") == 0 && !find_macro(pp, name, -1)) {
            r.type = T_INT;
            r.i = t->line;
            vec_push(*out, r);
            continue;
        }
        if (strcmp(name, "FUNCTION_NAME") == 0 && pp->func_name) {
            r.type = T_ATOM;
            r.s = pp->func_name;
            vec_push(*out, r);
            continue;
        }
        if (strcmp(name, "FUNCTION_ARITY") == 0 && pp->func_name) {
            r.type = T_INT;
            r.i = pp->func_arity;
            vec_push(*out, r);
            continue;
        }
        if (strcmp(name, "MACHINE") == 0 && !find_macro(pp, name, -1)) {
            r.type = T_ATOM;
            r.s = intern("BEAM", 4);
            vec_push(*out, r);
            continue;
        }
        if (strcmp(name, "OTP_RELEASE") == 0 && !find_macro(pp, name, -1)) {
            r.type = T_INT;
            r.i = 29;
            vec_push(*out, r);
            continue;
        }
        /* arguments? */
        VEC(int) argstart = {0};
        VEC(int) argend = {0};
        int after = i + 1;
        bool has_args = false;
        if (i + 1 < to && toks[i + 1].type == T_CHAR + '(') {
            int depth = 0, j = i + 2, start = j;
            if (j < to && toks[j].type == T_CHAR + ')') {
                has_args = true;
                after = j + 1;
            } else {
                for (; j < to; j++) {
                    int ty = toks[j].type;
                    if (ty == T_FUN && j + 1 < to && toks[j + 1].type == T_CHAR + '(')
                        depth++;
                    else if (is_open(ty))
                        depth++;
                    else if (is_close(ty)) {
                        if (depth == 0) {
                            vec_push(argstart, start);
                            vec_push(argend, j);
                            break;
                        }
                        depth--;
                    } else if (ty == T_CHAR + ',' && depth == 0) {
                        vec_push(argstart, start);
                        vec_push(argend, j);
                        start = j + 1;
                    }
                }
                if (j < to && toks[j].type == T_CHAR + ')') {
                    has_args = true;
                    after = j + 1;
                }
            }
        }
        Macro *m = has_args ? find_macro(pp, name, argstart.n) : NULL;
        if (!m)
            m = find_macro(pp, name, -1);
        if (!m || (m->nparams >= 0 && !has_args))
            fatal(t->file, t->line, "undefined macro '%s'", name);
        if (m->nparams < 0) {
            /* no parameters: the arguments, if any, are the program's */
            Tokens tmp = {0};
            for (int k = 0; k < m->body.n; k++) {
                Token b = m->body.v[k];
                b.line = t->line;
                vec_push(tmp, b);
            }
            expand(pp, tmp.v, 0, tmp.n, out, level + 1);
            free(tmp.v);
        } else {
            Tokens tmp = {0};
            for (int k = 0; k < m->body.n; k++) {
                Token b = m->body.v[k];
                int p = -1;
                if (b.type == T_VAR)
                    for (int q = 0; q < m->nparams; q++)
                        if (m->params[q] == b.s)
                            p = q;
                if (p >= 0) {
                    for (int q = argstart.v[p]; q < argend.v[p]; q++)
                        vec_push(tmp, toks[q]);
                } else {
                    b.line = t->line;
                    vec_push(tmp, b);
                }
            }
            expand(pp, tmp.v, 0, tmp.n, out, level + 1);
            free(tmp.v);
            i = after - 1;
        }
        free(argstart.v);
        free(argend.v);
    }
}

/* -define(Name, Body). or -define(Name(P1, ...), Body). toks[from..to)
 * are the tokens between the parentheses. */
static void define(PP *pp, Token *toks, int from, int to) {
    if (from >= to || (toks[from].type != T_ATOM && toks[from].type != T_VAR))
        fatal(toks[from].file, toks[from].line, "bad -define");
    Macro m = {0};
    m.name = toks[from].s;
    m.nparams = -1;
    int i = from + 1;
    if (i < to && toks[i].type == T_CHAR + '(') {
        VEC(const char *) ps = {0};
        i++;
        while (i < to && toks[i].type != T_CHAR + ')') {
            if (toks[i].type == T_VAR)
                vec_push(ps, toks[i].s);
            i++;
        }
        i++;
        m.nparams = ps.n;
        m.params = ps.v;
    }
    if (i >= to || toks[i].type != T_CHAR + ',') {
        if (i == to) {
            vec_push(pp->macros, m);
            return;
        }
        fatal(toks[i].file, toks[i].line, "bad -define");
    }
    for (i++; i < to; i++)
        vec_push(m.body, toks[i]);
    vec_push(pp->macros, m);
}

static void include_file(PP *pp, Token *at, const char *name, bool lib) {
    const char *dir = at->file;
    const char *slash = strrchr(dir, '/');
    char *path = slash ? xprintf("%.*s/%s", (int)(slash - dir), dir, name) : xstrdup(name);
    size_t len;
    char *src = read_file(path, &len);
    for (int i = 0; !src && i < pp->incdirs.n; i++) {
        free(path);
        path = xprintf("%s/%s", pp->incdirs.v[i], name);
        src = read_file(path, &len);
    }
    if (!src) {
        /* include_lib("eunit/include/eunit.hrl") and the like: nothing we know */
        if (lib)
            return;
        fatal(at->file, at->line, "can't find include file \"%s\"", name);
    }
    if (++pp->depth > 50)
        fatal(at->file, at->line, "include nesting too deep");
    Tokens toks = {0};
    lex_file(path, src, len, &toks);
    toks.n--;   /* the T_EOF */
    Tokens *out = xcalloc(1, sizeof *out);
    preprocess(pp, &toks, out);
    pp->depth--;
    /* the included forms go into the stream at this point */
    extern Tokens *pp_out;
    for (int i = 0; i < out->n; i++)
        vec_push(*pp_out, out->v[i]);
}

Tokens *pp_out;

/* Preprocess a token stream form by form. */
static void preprocess(PP *pp, Tokens *in, Tokens *out) {
    Tokens *saved_out = pp_out;
    pp_out = out;
    VEC(int) skip = {0};    /* per open -if: 0 taking, 1 skipping, 2 skipping (a branch was taken) */
    int i = 0;
    while (i < in->n) {
        int start = i;
        while (i < in->n && in->v[i].type != T_DOT)
            i++;
        int end = i;        /* the T_DOT, or n */
        if (i < in->n)
            i++;
        Token *t = &in->v[start];
        bool skipping = false;
        for (int k = 0; k < skip.n; k++)
            if (skip.v[k])
                skipping = true;
        /* -directive(...) */
        if (end - start >= 2 && t[0].type == T_CHAR + '-'
            && (t[1].type == T_ATOM || t[1].type == T_IF || t[1].type == T_ELSE)) {
            const char *d = t[1].type == T_IF ? "if" : t[1].type == T_ELSE ? "else" : t[1].s;
            int a0 = start + 3, a1 = end - 1;   /* between the parentheses */
            bool paren = end - start >= 4 && t[2].type == T_CHAR + '(' && in->v[end - 1].type == T_CHAR + ')';
            if (strcmp(d, "ifdef") == 0 || strcmp(d, "ifndef") == 0) {
                if (skipping) {
                    vec_push(skip, 2);
                } else {
                    bool def = paren && is_defined(pp, in->v[a0].s);
                    vec_push(skip, (strcmp(d, "ifdef") == 0) == def ? 0 : 1);
                }
                continue;
            }
            if (strcmp(d, "if") == 0) {
                /* only literal true/false and defined() checks are evaluated */
                int v = 0;
                if (skipping) {
                    vec_push(skip, 2);
                    continue;
                }
                if (paren && a1 - a0 == 1 && in->v[a0].type == T_ATOM)
                    v = strcmp(in->v[a0].s, "true") == 0;
                else if (paren && a1 - a0 == 4 && in->v[a0].type == T_ATOM
                         && strcmp(in->v[a0].s, "defined") == 0)
                    v = is_defined(pp, in->v[a0 + 2].s);
                else if (paren && a1 - a0 >= 3 && in->v[a0].type == T_CHAR + '?'
                         && strcmp(in->v[a0 + 1].s, "OTP_RELEASE") == 0
                         && in->v[a0 + 3].type == T_INT) {
                    int rel = 29;
                    int64_t n = in->v[a0 + 3].i;
                    int op = in->v[a0 + 2].type;
                    v = op == T_GE ? rel >= n : op == T_CHAR + '>' ? rel > n : op == T_LE ? rel <= n
                        : op == T_CHAR + '<' ? rel < n : op == T_EQEQ ? rel == n : 0;
                }
                vec_push(skip, v ? 0 : 1);
                continue;
            }
            if (strcmp(d, "elif") == 0) {
                if (!skip.n)
                    fatal(t->file, t->line, "-elif without -if");
                if (skip.v[skip.n - 1] == 0)
                    skip.v[skip.n - 1] = 2;
                else if (skip.v[skip.n - 1] == 1) {
                    int v = paren && a1 - a0 == 1 && in->v[a0].type == T_ATOM
                            && strcmp(in->v[a0].s, "true") == 0;
                    skip.v[skip.n - 1] = v ? 0 : 1;
                }
                continue;
            }
            if (strcmp(d, "else") == 0) {
                if (!skip.n)
                    fatal(t->file, t->line, "-else without -ifdef");
                skip.v[skip.n - 1] = skip.v[skip.n - 1] == 1 ? 0 : 2;
                continue;
            }
            if (strcmp(d, "endif") == 0) {
                if (!skip.n)
                    fatal(t->file, t->line, "-endif without -ifdef");
                skip.n--;
                continue;
            }
            if (skipping)
                continue;
            if (strcmp(d, "define") == 0 && paren) {
                define(pp, in->v, a0, a1);
                continue;
            }
            if (strcmp(d, "undef") == 0 && paren) {
                undefine(pp, in->v[a0].s);
                continue;
            }
            if ((strcmp(d, "include") == 0 || strcmp(d, "include_lib") == 0) && paren
                && in->v[a0].type == T_STRING) {
                include_file(pp, t, in->v[a0].s, strcmp(d, "include_lib") == 0);
                continue;
            }
            if (strcmp(d, "module") == 0 && paren && in->v[a0].type == T_ATOM)
                pp->module = in->v[a0].s;
        }
        if (skipping)
            continue;
        /* a function clause: note its name and arity for ?FUNCTION_NAME */
        if (t[0].type == T_ATOM && end - start > 1 && t[1].type == T_CHAR + '(') {
            pp->func_name = t[0].s;
            int depth = 0, n = 0;
            bool any = false;
            for (int k = start + 2; k < end; k++) {
                int ty = in->v[k].type;
                if (is_open(ty) || (ty == T_FUN && k + 1 < end && in->v[k + 1].type == T_CHAR + '('))
                    depth++;
                else if (is_close(ty)) {
                    if (depth == 0)
                        break;
                    depth--;
                } else if (ty == T_CHAR + ',' && depth == 0)
                    n++;
                if (depth > 0 || ty != T_CHAR + ')')
                    any = true;
            }
            pp->func_arity = any ? n + 1 : 0;
        } else {
            pp->func_name = NULL;
        }
        expand(pp, in->v, start, end, out, 0);
        if (end < in->n)
            vec_push(*out, in->v[end]);
    }
    if (skip.n)
        fatal(in->v[in->n - 1].file, in->v[in->n - 1].line, "missing -endif");
    free(skip.v);
    pp_out = saved_out;
}

/* ------------------------------------------------------------------ parser */

typedef struct {
    Token *t;
    int n, pos;
    Module *mod;
    bool no_remote;     /* in a catch clause's head: ':' separates class, reason and stack */
} Parser;

Node *new_node(NodeKind kind, int line, const char *file) {
    Node *n = xcalloc(1, sizeof *n);
    n->kind = kind;
    n->line = line;
    n->file = file;
    return n;
}

static Token *peek(Parser *p) { return &p->t[p->pos]; }
static Token *peek2(Parser *p) { return &p->t[p->pos + 1 < p->n ? p->pos + 1 : p->pos]; }
static Token *advance(Parser *p) { Token *t = &p->t[p->pos]; if (p->pos < p->n - 1) p->pos++; return t; }
static bool check(Parser *p, int type) { return peek(p)->type == type; }

static bool accept(Parser *p, int type) {
    if (check(p, type)) {
        advance(p);
        return true;
    }
    return false;
}

_Noreturn static void syntax_error(Parser *p, const char *what) {
    Token *t = peek(p);
    if (t->type == T_EOF)
        fatal(t->file, t->line, "syntax error before: end of file%s%s", what ? ", expected " : "", what ? what : "");
    if (t->type == T_ATOM || t->type == T_VAR)
        fatal(t->file, t->line, "syntax error before: %s", t->s);
    fatal(t->file, t->line, "syntax error before: %s", tok_name(t->type));
}

static Token *expect(Parser *p, int type) {
    if (!check(p, type))
        syntax_error(p, tok_name(type));
    return advance(p);
}

static Node *node(Parser *p, NodeKind kind) {
    return new_node(kind, peek(p)->line, peek(p)->file);
}

static Node *expr(Parser *p);
static Node *expr_max(Parser *p);
static void body(Parser *p, Nodes *out);
static Node *expr_700(Parser *p);

static Node *atom_node(Parser *p, const char *s, int line) {
    Node *n = new_node(N_ATOM, line, peek(p)->file);
    n->s = s;
    return n;
}

/* Guards: G1, G2; G3 ... (alternatives separated by ';'). */
static void guards(Parser *p, Clause *c) {
    do {
        Nodes g = {0};
        do {
            vec_push(g, expr(p));
        } while (accept(p, T_CHAR + ','));
        vec_push(c->guards, g);
    } while (accept(p, T_CHAR + ';'));
}

/* Pattern [when Guards] -> Body (case, receive, try-of clauses). */
static Clause *cr_clause(Parser *p) {
    Clause *c = xcalloc(1, sizeof *c);
    c->line = peek(p)->line;
    vec_push(c->pats, expr(p));
    if (accept(p, T_WHEN))
        guards(p, c);
    expect(p, T_ARROW);
    body(p, &c->body);
    return c;
}

static void cr_clauses(Parser *p, Clauses *out) {
    do {
        vec_push(*out, cr_clause(p));
    } while (accept(p, T_CHAR + ';'));
}

/* (Pats) [when Guards] -> Body: function and fun clauses. */
static Clause *fun_clause(Parser *p) {
    Clause *c = xcalloc(1, sizeof *c);
    c->line = peek(p)->line;
    expect(p, T_CHAR + '(');
    if (!check(p, T_CHAR + ')')) {
        do {
            vec_push(c->pats, expr(p));
        } while (accept(p, T_CHAR + ','));
    }
    expect(p, T_CHAR + ')');
    if (accept(p, T_WHEN))
        guards(p, c);
    expect(p, T_ARROW);
    body(p, &c->body);
    return c;
}

static void body(Parser *p, Nodes *out) {
    do {
        vec_push(*out, expr(p));
    } while (accept(p, T_CHAR + ','));
}

/* A binary's segment: Value[:Size][/Type-Spec-...]. */
static Node *bin_element(Parser *p) {
    Node *seg = node(p, N_BINSEG);
    if (check(p, T_CHAR + '-') || check(p, T_CHAR + '+') || check(p, T_BNOT) || check(p, T_NOT)) {
        Token *op = advance(p);
        Node *u = new_node(N_UNOP, op->line, op->file);
        u->s = op->type == T_BNOT ? "bnot" : op->type == T_NOT ? "not" : op->type == T_CHAR + '-' ? "-" : "+";
        u->a = expr_max(p);
        seg->a = u;
    } else {
        seg->a = expr_max(p);
    }
    if (accept(p, T_CHAR + ':'))
        seg->b = expr_max(p);
    if (accept(p, T_CHAR + '/')) {
        Buf b = {0};
        do {
            Token *t = expect(p, T_ATOM);
            if (b.n)
                buf_puts(&b, "-");
            buf_puts(&b, t->s);
            if (accept(p, T_CHAR + ':')) {
                Token *u = expect(p, T_INT);
                buf_printf(&b, ":%lld", (long long)u->i);
            }
        } while (accept(p, T_CHAR + '-'));
        seg->s = b.s;
    }
    return seg;
}

/* Qualifiers of a comprehension, after '||'. */
static void qualifiers(Parser *p, Nodes *out) {
    do {
        Node *e = expr(p);
        if (accept(p, T_ASSOC)) {
            Node *v = expr(p);
            Node *g = new_node(N_MGEN, e->line, e->file);
            Node *kv = new_node(N_ASSOC, e->line, e->file);
            kv->a = e;
            kv->b = v;
            expect(p, T_LARROW);
            g->a = kv;
            g->b = expr(p);
            vec_push(*out, g);
        } else if (accept(p, T_LARROW)) {
            Node *g = new_node(N_GEN, e->line, e->file);
            g->a = e;
            g->b = expr(p);
            vec_push(*out, g);
        } else if (accept(p, T_LDARROW)) {
            Node *g = new_node(N_BGEN, e->line, e->file);
            g->a = e;
            g->b = expr(p);
            vec_push(*out, g);
        } else {
            vec_push(*out, e);
        }
    } while (accept(p, T_CHAR + ','));
}

/* Map associations inside #{ ... }. */
static void map_fields(Parser *p, Node *m) {
    expect(p, T_CHAR + '{');
    if (!check(p, T_CHAR + '}')) {
        do {
            Node *k = expr(p);
            Node *a;
            if (accept(p, T_DARROW))
                a = new_node(N_ASSOC, k->line, k->file);
            else {
                expect(p, T_ASSOC);
                a = new_node(N_EXACT, k->line, k->file);
            }
            a->a = k;
            a->b = expr(p);
            vec_push(m->kids, a);
        } while (accept(p, T_CHAR + ','));
    }
    expect(p, T_CHAR + '}');
}

/* Record fields inside #name{ ... }. */
static void record_fields(Parser *p, Node *r) {
    expect(p, T_CHAR + '{');
    if (!check(p, T_CHAR + '}')) {
        do {
            Node *a = node(p, N_ASSOC);
            if (check(p, T_VAR) && strcmp(peek(p)->s, "_") == 0) {
                advance(p);
                a->a = atom_node(p, intern("_", 1), a->line);
            } else {
                Token *f = expect(p, T_ATOM);
                a->a = atom_node(p, f->s, f->line);
            }
            expect(p, T_CHAR + '=');
            a->b = expr(p);
            vec_push(r->kids, a);
        } while (accept(p, T_CHAR + ','));
    }
    expect(p, T_CHAR + '}');
}

static Node *string_node(Parser *p) {
    Token *t = advance(p);
    Node *n = new_node(N_STRING, t->line, t->file);
    if (!check(p, T_STRING)) {
        n->s = t->s;
        n->len = t->len;
        return n;
    }
    /* adjacent strings are one */
    Buf b = {0};
    buf_add(&b, t->s, (size_t)t->len);
    while (check(p, T_STRING)) {
        Token *u = advance(p);
        buf_add(&b, u->s, (size_t)u->len);
    }
    n->s = b.s;
    n->len = (int)b.n;
    return n;
}

static Node *fun_expr(Parser *p) {
    Token *ft = expect(p, T_FUN);
    /* fun Name/Arity, fun Mod:Name/Arity */
    if ((check(p, T_ATOM) || check(p, T_VAR)) && (peek2(p)->type == T_CHAR + '/' || peek2(p)->type == T_CHAR + ':')) {
        Node *r = new_node(N_FUNREF, ft->line, ft->file);
        Node *first = expr_max(p);
        if (accept(p, T_CHAR + ':')) {
            r->a = first;
            r->b = expr_max(p);
        } else {
            r->b = first;
        }
        expect(p, T_CHAR + '/');
        if (check(p, T_INT)) {
            r->i = advance(p)->i;
        } else {
            r->c = expr_max(p);     /* a variable arity */
        }
        return r;
    }
    Node *f = new_node(N_FUN, ft->line, ft->file);
    if (check(p, T_VAR) && peek2(p)->type == T_CHAR + '(') {
        f->s = peek(p)->s;
        do {
            Token *v = expect(p, T_VAR);
            if (v->s != f->s)
                fatal(v->file, v->line, "named fun clauses must all use the name %s", f->s);
            vec_push(f->clauses, fun_clause(p));
        } while (accept(p, T_CHAR + ';'));
    } else {
        do {
            vec_push(f->clauses, fun_clause(p));
        } while (accept(p, T_CHAR + ';'));
    }
    expect(p, T_END);
    return f;
}

/* A catch clause: [Class:]Reason[:Stack] [when Guards] -> Body, as a clause
 * whose patterns are class, reason and stack (NULL for none). */
static Clause *catch_clause(Parser *p) {
    Clause *c = xcalloc(1, sizeof *c);
    c->line = peek(p)->line;
    bool saved = p->no_remote;
    p->no_remote = true;
    Node *a = expr(p), *b = NULL, *s = NULL;
    if (accept(p, T_CHAR + ':')) {
        b = expr(p);
        if (accept(p, T_CHAR + ':'))
            s = expr(p);
    }
    p->no_remote = saved;
    if (!b) {
        b = a;
        a = atom_node(p, intern("throw", 5), c->line);
    }
    vec_push(c->pats, a);
    vec_push(c->pats, b);
    vec_push(c->pats, s);
    if (accept(p, T_WHEN))
        guards(p, c);
    expect(p, T_ARROW);
    body(p, &c->body);
    return c;
}

static Node *expr_max(Parser *p) {
    Token *t = peek(p);
    Node *n;
    switch (t->type) {
    case T_VAR:
        advance(p);
        n = new_node(N_VAR, t->line, t->file);
        n->s = t->s;
        return n;
    case T_ATOM:
        advance(p);
        return atom_node(p, t->s, t->line);
    case T_INT:
    case T_CHARLIT:
        advance(p);
        n = new_node(N_INT, t->line, t->file);
        n->i = t->i;
        return n;
    case T_BIGINT:
        advance(p);
        n = new_node(N_BIGINT, t->line, t->file);
        n->s = t->s;
        return n;
    case T_FLOAT:
        advance(p);
        n = new_node(N_FLOAT, t->line, t->file);
        n->f = t->f;
        return n;
    case T_STRING:
        return string_node(p);
    case T_CHAR + '(': {
        advance(p);
        Node *e = expr(p);
        expect(p, T_CHAR + ')');
        return e;
    }
    case T_CHAR + '{':
        advance(p);
        n = new_node(N_TUPLE, t->line, t->file);
        if (!check(p, T_CHAR + '}')) {
            do {
                vec_push(n->kids, expr(p));
            } while (accept(p, T_CHAR + ','));
        }
        expect(p, T_CHAR + '}');
        return n;
    case T_CHAR + '[': {
        advance(p);
        if (accept(p, T_CHAR + ']'))
            return new_node(N_NIL, t->line, t->file);
        Node *first = expr(p);
        if (accept(p, T_BARBAR)) {
            n = new_node(N_LC, t->line, t->file);
            n->a = first;
            qualifiers(p, &n->kids);
            expect(p, T_CHAR + ']');
            return n;
        }
        Nodes elems = {0};
        vec_push(elems, first);
        while (accept(p, T_CHAR + ','))
            vec_push(elems, expr(p));
        Node *tail;
        if (accept(p, T_CHAR + '|'))
            tail = expr(p);
        else
            tail = new_node(N_NIL, peek(p)->line, peek(p)->file);
        expect(p, T_CHAR + ']');
        for (int i = elems.n - 1; i >= 0; i--) {
            Node *c = new_node(N_CONS, elems.v[i]->line, elems.v[i]->file);
            c->a = elems.v[i];
            c->b = tail;
            tail = c;
        }
        free(elems.v);
        return tail;
    }
    case T_LTLT:
        advance(p);
        n = new_node(N_BIN, t->line, t->file);
        if (accept(p, T_GTGT))
            return n;
        {
            Node *first = bin_element(p);
            if (accept(p, T_BARBAR)) {
                Node *bc = new_node(N_BC, t->line, t->file);
                /* <<E || ...>>: E is a whole binary expression, parsed as a segment */
                Node *b = new_node(N_BIN, t->line, t->file);
                vec_push(b->kids, first);
                bc->a = first->b == NULL && first->s == NULL ? first->a : b;
                qualifiers(p, &bc->kids);
                expect(p, T_GTGT);
                return bc;
            }
            vec_push(n->kids, first);
            while (accept(p, T_CHAR + ','))
                vec_push(n->kids, bin_element(p));
        }
        expect(p, T_GTGT);
        return n;
    case T_BEGIN:
        advance(p);
        n = new_node(N_BLOCK, t->line, t->file);
        body(p, &n->kids);
        expect(p, T_END);
        return n;
    case T_IF:
        advance(p);
        n = new_node(N_IF, t->line, t->file);
        do {
            Clause *c = xcalloc(1, sizeof *c);
            c->line = peek(p)->line;
            guards(p, c);
            expect(p, T_ARROW);
            body(p, &c->body);
            vec_push(n->clauses, c);
        } while (accept(p, T_CHAR + ';'));
        expect(p, T_END);
        return n;
    case T_CASE:
        advance(p);
        n = new_node(N_CASE, t->line, t->file);
        n->a = expr(p);
        expect(p, T_OF);
        cr_clauses(p, &n->clauses);
        expect(p, T_END);
        return n;
    case T_RECEIVE:
        advance(p);
        n = new_node(N_RECEIVE, t->line, t->file);
        if (!check(p, T_AFTER))
            cr_clauses(p, &n->clauses);
        if (accept(p, T_AFTER)) {
            n->a = expr(p);
            expect(p, T_ARROW);
            body(p, &n->kids);
        }
        expect(p, T_END);
        return n;
    case T_FUN:
        return fun_expr(p);
    case T_TRY:
        advance(p);
        n = new_node(N_TRY, t->line, t->file);
        body(p, &n->kids);
        if (accept(p, T_OF))
            cr_clauses(p, &n->clauses);
        if (accept(p, T_CATCH)) {
            do {
                vec_push(n->cclauses, catch_clause(p));
            } while (accept(p, T_CHAR + ';'));
        }
        if (accept(p, T_AFTER)) {
            Node *b = new_node(N_BLOCK, peek(p)->line, peek(p)->file);
            body(p, &b->kids);
            n->b = b;
        }
        if (!n->cclauses.n && !n->b)
            syntax_error(p, "catch or after");
        expect(p, T_END);
        return n;
    case T_CHAR + '#': {
        advance(p);
        if (check(p, T_CHAR + '{')) {
            /* #{K => V || Qualifiers}: a map comprehension */
            int save = p->pos;
            advance(p);
            if (!check(p, T_CHAR + '}')) {
                Node *k = expr(p);
                if (accept(p, T_DARROW)) {
                    Node *v = expr(p);
                    if (accept(p, T_BARBAR)) {
                        n = new_node(N_MC, t->line, t->file);
                        Node *a = new_node(N_ASSOC, k->line, k->file);
                        a->a = k;
                        a->b = v;
                        n->a = a;
                        qualifiers(p, &n->kids);
                        expect(p, T_CHAR + '}');
                        return n;
                    }
                }
            }
            p->pos = save;
            n = new_node(N_MAP, t->line, t->file);
            map_fields(p, n);
            return n;
        }
        Token *name = expect(p, T_ATOM);
        if (accept(p, T_CHAR + '.')) {
            Token *f = expect(p, T_ATOM);
            n = new_node(N_RECINDEX, t->line, t->file);
            n->s = name->s;
            n->b = atom_node(p, f->s, f->line);
            return n;
        }
        n = new_node(N_RECORD, t->line, t->file);
        n->s = name->s;
        record_fields(p, n);
        return n;
    }
    default:
        if (t->type == T_ATOM && strcmp(t->s, "maybe") == 0)
            fatal(t->file, t->line, "maybe expressions are not supported");
        syntax_error(p, NULL);
    }
}

/* Calls, remote names, and record and map postfixes. */
static Node *expr_700(Parser *p) {
    Node *e = expr_max(p);
    for (;;) {
        Token *t = peek(p);
        if (t->type == T_CHAR + ':' && !p->no_remote) {
            advance(p);
            Node *r = new_node(N_REMOTE, t->line, t->file);
            r->a = e;
            r->b = expr_max(p);
            e = r;
        } else if (t->type == T_CHAR + '(') {
            advance(p);
            Node *c = new_node(N_CALL, e->line, e->file);
            c->a = e;
            if (!check(p, T_CHAR + ')')) {
                do {
                    vec_push(c->kids, expr(p));
                } while (accept(p, T_CHAR + ','));
            }
            expect(p, T_CHAR + ')');
            e = c;
        } else if (t->type == T_CHAR + '#') {
            advance(p);
            if (check(p, T_CHAR + '{')) {
                Node *m = new_node(N_MAP, t->line, t->file);
                m->a = e;
                map_fields(p, m);
                e = m;
                continue;
            }
            Token *name = expect(p, T_ATOM);
            if (accept(p, T_CHAR + '.')) {
                Token *f = expect(p, T_ATOM);
                Node *r = new_node(N_RECFIELD, t->line, t->file);
                r->a = e;
                r->s = name->s;
                r->b = atom_node(p, f->s, f->line);
                e = r;
            } else {
                Node *r = new_node(N_RECORD, t->line, t->file);
                r->a = e;
                r->s = name->s;
                record_fields(p, r);
                e = r;
            }
        } else {
            return e;
        }
    }
}

static Node *expr_600(Parser *p) {
    Token *t = peek(p);
    const char *op = NULL;
    switch (t->type) {
    case T_CHAR + '-': op = "-"; break;
    case T_CHAR + '+': op = "+"; break;
    case T_BNOT: op = "bnot"; break;
    case T_NOT: op = "not"; break;
    }
    if (op) {
        advance(p);
        Node *a = expr_600(p);
        /* fold negative literals, which patterns need */
        if (strcmp(op, "-") == 0 && a->kind == N_INT && a->i != INT64_MIN) {
            a->i = -a->i;
            return a;
        }
        if (strcmp(op, "-") == 0 && a->kind == N_FLOAT) {
            a->f = -a->f;
            return a;
        }
        if (strcmp(op, "-") == 0 && a->kind == N_BIGINT) {
            a->s = a->s[0] == '-' ? a->s + 1 : xprintf("-%s", a->s);
            return a;
        }
        if (strcmp(op, "+") == 0 && (a->kind == N_INT || a->kind == N_FLOAT || a->kind == N_BIGINT))
            return a;
        Node *u = new_node(N_UNOP, t->line, t->file);
        u->s = op;
        u->a = a;
        return u;
    }
    return expr_700(p);
}

static Node *binop(Token *t, const char *op, Node *a, Node *b) {
    Node *n = new_node(N_OP, t->line, t->file);
    n->s = op;
    n->a = a;
    n->b = b;
    return n;
}

static const char *mult_op(int type) {
    switch (type) {
    case T_CHAR + '/': return "/";
    case T_CHAR + '*': return "*";
    case T_DIV: return "div";
    case T_REM: return "rem";
    case T_BAND: return "band";
    case T_AND: return "and";
    }
    return NULL;
}

static const char *add_op(int type) {
    switch (type) {
    case T_CHAR + '+': return "+";
    case T_CHAR + '-': return "-";
    case T_BOR: return "bor";
    case T_BXOR: return "bxor";
    case T_BSL: return "bsl";
    case T_BSR: return "bsr";
    case T_OR: return "or";
    case T_XOR: return "xor";
    }
    return NULL;
}

static const char *comp_op(int type) {
    switch (type) {
    case T_EQEQ: return "==";
    case T_NE: return "/=";
    case T_LE: return "=<";
    case T_CHAR + '<': return "<";
    case T_GE: return ">=";
    case T_CHAR + '>': return ">";
    case T_EXEQ: return "=:=";
    case T_EXNE: return "=/=";
    }
    return NULL;
}

static Node *expr_500(Parser *p) {
    Node *a = expr_600(p);
    const char *op;
    while ((op = mult_op(peek(p)->type))) {
        Token *t = advance(p);
        a = binop(t, op, a, expr_600(p));
    }
    return a;
}

static Node *expr_400(Parser *p) {
    Node *a = expr_500(p);
    const char *op;
    while ((op = add_op(peek(p)->type))) {
        Token *t = advance(p);
        a = binop(t, op, a, expr_500(p));
    }
    return a;
}

static Node *expr_300(Parser *p) {
    Node *a = expr_400(p);
    if (check(p, T_PLUSPLUS) || check(p, T_MINUSMINUS)) {
        Token *t = advance(p);
        return binop(t, t->type == T_PLUSPLUS ? "++" : "--", a, expr_300(p));
    }
    return a;
}

static Node *expr_200(Parser *p) {
    Node *a = expr_300(p);
    const char *op = comp_op(peek(p)->type);
    if (op) {
        Token *t = advance(p);
        return binop(t, op, a, expr_300(p));
    }
    return a;
}

static Node *expr_160(Parser *p) {
    Node *a = expr_200(p);
    if (check(p, T_ANDALSO)) {
        Token *t = advance(p);
        return binop(t, "andalso", a, expr_160(p));
    }
    return a;
}

static Node *expr_150(Parser *p) {
    Node *a = expr_160(p);
    if (check(p, T_ORELSE)) {
        Token *t = advance(p);
        return binop(t, "orelse", a, expr_150(p));
    }
    return a;
}

static Node *expr_100(Parser *p) {
    Node *a = expr_150(p);
    if (check(p, T_CHAR + '=')) {
        Token *t = advance(p);
        Node *m = new_node(N_MATCH, t->line, t->file);
        m->a = a;
        m->b = expr(p);
        return m;
    }
    if (check(p, T_CHAR + '!')) {
        Token *t = advance(p);
        return binop(t, "!", a, expr(p));
    }
    return a;
}

static Node *expr(Parser *p) {
    if (check(p, T_CATCH)) {
        Token *t = advance(p);
        Node *c = new_node(N_CATCH, t->line, t->file);
        c->a = expr(p);
        return c;
    }
    return expr_100(p);
}

/* ------------------------------------------------------------------ forms */

/* Skip a type (in a record field or a spec) up to a ',' or '}' (or '.') at depth 0. */
static void skip_type(Parser *p) {
    int depth = 0;
    for (;;) {
        int ty = peek(p)->type;
        if (ty == T_EOF || ty == T_DOT)
            return;
        if (depth == 0 && (ty == T_CHAR + ',' || ty == T_CHAR + '}' || ty == T_CHAR + ')'))
            return;
        if (ty == T_CHAR + '(' || ty == T_CHAR + '[' || ty == T_CHAR + '{' || ty == T_LTLT)
            depth++;
        else if (ty == T_CHAR + ')' || ty == T_CHAR + ']' || ty == T_CHAR + '}' || ty == T_GTGT)
            depth--;
        advance(p);
    }
}

static void skip_form(Parser *p) {
    while (!check(p, T_DOT) && !check(p, T_EOF))
        advance(p);
    accept(p, T_DOT);
}

/* [f/1, g/2] */
static void fa_list(Parser *p, void (*add)(Module *, const char *, void *), void *arg) {
    expect(p, T_CHAR + '[');
    if (!check(p, T_CHAR + ']')) {
        do {
            Token *f = expect(p, T_ATOM);
            expect(p, T_CHAR + '/');
            Token *a = expect(p, T_INT);
            char *s = xprintf("%s/%lld", f->s, (long long)a->i);
            add(p->mod, intern(s, strlen(s)), arg);
            free(s);
        } while (accept(p, T_CHAR + ','));
    }
    expect(p, T_CHAR + ']');
}

static void add_export(Module *m, const char *fa, void *arg) {
    (void)arg;
    vec_push(m->exports, fa);
}

static void add_import(Module *m, const char *fa, void *arg) {
    vec_push(m->import_fa, fa);
    vec_push(m->import_mod, (const char *)arg);
}

static void attribute(Parser *p) {
    Token *dash = expect(p, T_CHAR + '-');
    Token *name = peek(p);
    if (name->type != T_ATOM) {
        skip_form(p);
        return;
    }
    advance(p);
    const char *a = name->s;
    if (strcmp(a, "module") == 0) {
        expect(p, T_CHAR + '(');
        p->mod->name = expect(p, T_ATOM)->s;
        expect(p, T_CHAR + ')');
    } else if (strcmp(a, "export") == 0) {
        expect(p, T_CHAR + '(');
        fa_list(p, add_export, NULL);
        expect(p, T_CHAR + ')');
    } else if (strcmp(a, "import") == 0) {
        expect(p, T_CHAR + '(');
        const char *mod = expect(p, T_ATOM)->s;
        expect(p, T_CHAR + ',');
        fa_list(p, add_import, (void *)mod);
        expect(p, T_CHAR + ')');
    } else if (strcmp(a, "compile") == 0) {
        while (!check(p, T_DOT) && !check(p, T_EOF)) {
            Token *t = advance(p);
            if (t->type == T_ATOM && strcmp(t->s, "export_all") == 0)
                p->mod->export_all = true;
        }
    } else if (strcmp(a, "record") == 0) {
        expect(p, T_CHAR + '(');
        Record *r = xcalloc(1, sizeof *r);
        r->name = expect(p, T_ATOM)->s;
        expect(p, T_CHAR + ',');
        expect(p, T_CHAR + '{');
        if (!check(p, T_CHAR + '}')) {
            do {
                vec_push(r->fields, expect(p, T_ATOM)->s);
                Node *def = NULL;
                if (accept(p, T_CHAR + '='))
                    def = expr(p);
                if (accept(p, T_COLCOL))
                    skip_type(p);
                vec_push(r->defaults, def);
            } while (accept(p, T_CHAR + ','));
        }
        expect(p, T_CHAR + '}');
        expect(p, T_CHAR + ')');
        vec_push(p->mod->records, r);
    } else {
        /* -spec, -type, -behaviour, -author, ...: nothing to do */
        (void)dash;
        skip_form(p);
        return;
    }
    expect(p, T_DOT);
}

Function *find_function(Module *m, const char *name, int arity) {
    for (int i = 0; i < m->funcs.n; i++)
        if (m->funcs.v[i]->name == name && m->funcs.v[i]->arity == arity)
            return m->funcs.v[i];
    return NULL;
}

static void function(Parser *p) {
    Token *first = peek(p);
    const char *name = first->s;
    Function *f = xcalloc(1, sizeof *f);
    f->name = name;
    f->line = first->line;
    f->file = first->file;
    f->arity = -1;
    do {
        Token *t = expect(p, T_ATOM);
        if (t->s != name)
            fatal(t->file, t->line, "head mismatch: %s and %s", name, t->s);
        Clause *c = fun_clause(p);
        if (f->arity < 0)
            f->arity = c->pats.n;
        else if (f->arity != c->pats.n)
            fatal(t->file, t->line, "head mismatch: %s/%d and %s/%d", name, f->arity, name, c->pats.n);
        vec_push(f->clauses, c);
    } while (accept(p, T_CHAR + ';'));
    expect(p, T_DOT);
    if (find_function(p->mod, name, f->arity))
        fatal(first->file, first->line, "function %s/%d already defined", name, f->arity);
    vec_push(p->mod->funcs, f);
}

Module *parse_module(const char *path, const char *src, size_t len) {
    Tokens raw = {0};
    lex_file(path, src, len, &raw);
    raw.n--;    /* T_EOF */
    PP pp = {0};
    const char *slash = strrchr(path, '/');
    if (slash) {
        char *inc = xprintf("%.*s/../include", (int)(slash - path), path);
        vec_push(pp.incdirs, inc);
    }
    Tokens toks = {0};
    preprocess(&pp, &raw, &toks);
    Token eof = {0};
    eof.type = T_EOF;
    eof.file = intern(path, strlen(path));
    eof.line = raw.n ? raw.v[raw.n - 1].line : 1;
    vec_push(toks, eof);

    Module *m = xcalloc(1, sizeof *m);
    m->file = intern(path, strlen(path));
    Parser p = {toks.v, toks.n, 0, m, false};
    while (!check(&p, T_EOF)) {
        if (check(&p, T_CHAR + '-'))
            attribute(&p);
        else if (check(&p, T_ATOM))
            function(&p);
        else
            syntax_error(&p, NULL);
    }
    if (!m->name)
        fatal(path, 1, "no -module attribute");
    return m;
}
