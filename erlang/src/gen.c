/* The code generator: modules to MLWSVM assembly.
 *
 * An Erlang value lives in a pair of registers, Ik and Rk (a "slot"):
 *   Rk null      a small integer, in Ik
 *   Rk == 1d     a float, its bits in Ik
 *   Rk == 0d     []
 *   Rk a string  an atom (atom literals are VM-wide constants: compared by identity)
 *   Rk a BigInt  an integer outside 64 bits (never one that fits)
 *   Rk a list    a tuple: two words per element, R then I
 *   Rk a struct  Cons, Pid, Ref, Fun, Map, Bin
 * and Ik is 0 unless Rk is null or 1d. A function f/N of module m is
 *   .func E$m$f$N(i64 x N, ref x N) -> (i64, ref)
 * Slots 0..N-1 are the arguments, then the variables of the clause being
 * compiled, then temporaries, allocated as a stack. Slots from NR up live
 * on the data stack (LOC). */
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "erl.h"

#define NR 29           /* slots kept in registers (the 3 above are scratch) */

/* ------------------------------------------------------------------ program state */

typedef VEC(const char *) Names;

typedef struct {
    const char *name;   /* masm name */
    Module *mod;
    Function *fn;
} FnReq;

typedef struct {
    VEC(Module *) mods;
    const char *srcdir;
    VEC(FnReq) queue;
    Names emitted;      /* masm names queued so far */
    Buf out;
    Names atoms;        /* atom literals */
    int nconst;                     /* cached constant terms: globals K0, K1, ... */
    Buf consts;                     /* code that builds them: functions K$n */
    bool dispatch;                  /* apply/3 and M:F(...) with unknown M or F used */
    Names dyn_mods;     /* modules named by atoms (for dispatch) */
} Prog;

static Prog P;

/* ------------------------------------------------------------------ per-function state */

typedef struct {
    const char *i, *r;  /* operands: registers, constants or memory */
} Val;

typedef struct {
    const char *name;
    int slot;
    bool bound;
    bool flt;           /* bound to a value that is likely a float */
} Var;

typedef struct Fn Fn;
struct Fn {
    Module *mod;
    const char *ename;      /* the Erlang function, for messages: "f/2" */
    const char *mname;      /* the masm function */
    Buf code, cold;
    bool in_cold;
    int nparams;
    VEC(Var) vars;
    int top, maxslot;
    int want;               /* if > 0, the next temp() is slot want - 1 */
    int tbase;              /* the first temporary's slot (a clause's variables are below) */
    int label;
    int nfuns;              /* funs made so far in this function (for names) */
    int *funcount;          /* shared by the funs of one Erlang function */
    Names handlers; /* guard .try handlers: "Lh:Lfail" pairs to emit */
    int ncaps;              /* captures (UP words) of a fun */
    const char *file;
    int line;               /* last .loc emitted */
};

static Val ev(Fn *g, Node *e);
static void tail(Fn *g, Node *e);
static void match(Fn *g, Node *pat, Val v, const char *fail);
static void cond(Fn *g, Node *e, const char *fail);
static Val ev_body(Fn *g, Nodes *body);
static void tail_body(Fn *g, Nodes *body);
static const char *request(Module *m, Function *f);
static Module *get_module(const char *name);

/* ------------------------------------------------------------------ text helpers */

static const char *S(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    char *s = xmalloc((size_t)n + 1);
    va_start(ap, fmt);
    vsnprintf(s, (size_t)n + 1, fmt, ap);
    va_end(ap);
    return s;
}

static void emit(Fn *g, const char *fmt, ...) {
    Buf *b = g->in_cold ? &g->cold : &g->code;
    buf_puts(b, "    ");
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    char *s = xmalloc((size_t)n + 1);
    va_start(ap, fmt);
    vsnprintf(s, (size_t)n + 1, fmt, ap);
    va_end(ap);
    buf_add(b, s, (size_t)n);
    buf_add(b, "\n", 1);
    free(s);
}

static void label(Fn *g, const char *l) {
    Buf *b = g->in_cold ? &g->cold : &g->code;
    buf_printf(b, "%s:\n", l);
}

static const char *new_label(Fn *g) {
    return S("L%d", g->label++);
}

/* Cold code made inside a .try region must stay inside it: a region
 * keeps its own, put at its end (jumped over). */
static Buf begin_region(Fn *g) {
    Buf saved = g->cold;
    g->cold = (Buf){0};
    return saved;
}

static void end_region(Fn *g, Buf saved) {
    if (g->cold.n) {
        Buf cold = g->cold;
        g->cold = (Buf){0};
        const char *over = new_label(g);
        emit(g, "jmp %s", over);
        buf_add(&g->code, cold.s, cold.n);
        label(g, over);
        free(cold.s);
    }
    free(g->cold.s);
    g->cold = saved;
}

static void loc(Fn *g, Node *e) {
    if (e && e->line > 0 && (e->line != g->line || e->file != g->file) && !g->in_cold) {
        g->line = e->line;
        g->file = e->file;
        emit(g, ".loc \"%s\", %d", e->file, e->line);
    }
}

/* A masm string literal for bytes. */
static const char *quote(const char *s, int len) {
    Buf b = {0};
    buf_add(&b, "\"", 1);
    for (int k = 0; k < len; k++) {
        unsigned char c = (unsigned char)s[k];
        if (c == '"' || c == '\\')
            buf_printf(&b, "\\%c", c);
        else if (c < 32 || c >= 127)
            buf_printf(&b, "\\x%02X", c);
        else
            buf_add(&b, (const char *)&c, 1);
    }
    buf_add(&b, "\"", 1);
    buf_add(&b, "", 0);
    return b.s;
}

/* Mangle a name into masm identifier characters. */
static const char *mangle(const char *s) {
    Buf b = {0};
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_')
            buf_add(&b, (const char *)p, 1);
        else
            buf_printf(&b, "_%02X_", *p);
    }
    buf_add(&b, "", 0);
    return b.s;
}

static const char *fn_name(const char *mod, const char *name, int arity) {
    return S("E$%s$%s$%d", mangle(mod), mangle(name), arity);
}

static void note_atom(const char *a) {
    for (int k = 0; k < P.atoms.n; k++)
        if (P.atoms.v[k] == a)
            return;
    vec_push(P.atoms, a);
}

/* ------------------------------------------------------------------ slots and values */

static const char *ireg(int slot) {
    return slot < NR ? S("I%d", slot) : S("LOC[%d]", 2 * (slot - NR));
}

static const char *rreg(int slot) {
    return slot < NR ? S("R%d", slot) : S("LOC[%d]", 2 * (slot - NR) + 1);
}

static Val slot_val(int slot) {
    return (Val){ireg(slot), rreg(slot)};
}

static int temp(Fn *g) {
    if (g->want > 0) {
        int s = g->want - 1;
        g->want = 0;
        return s;
    }
    int s = g->top++;
    if (s > g->maxslot)
        g->maxslot = s;
    return s;
}

/* The scratch registers: just above the slots, filled in at the end. */
#define XI0 "\001I0"
#define XI1 "\001I1"
#define XI2 "\001I2"
#define XR0 "\001R0"
#define XR1 "\001R1"
#define XR2 "\001R2"

static Val vint(int64_t n) { return (Val){S("%lld", (long long)n), "null"}; }
static Val vatom(const char *a) { note_atom(a); return (Val){"0", quote(a, (int)strlen(a))}; }
static Val vnil(void) { return (Val){"0", "0d"}; }

static Val vfloat(double f) {
    int64_t bits;
    memcpy(&bits, &f, 8);
    return (Val){S("%lld", (long long)bits), "1d"};
}

static bool is_reg(const char *op) {
    return (op[0] == 'I' || op[0] == 'R') && op[1] >= '0' && op[1] <= '9';
}

static bool is_scratch(const char *op) {
    return op[0] == '\001';
}

static bool is_loc(const char *op) {
    return strncmp(op, "LOC[", 4) == 0;
}

static bool is_const(Val v) {
    return !is_reg(v.r) && !is_scratch(v.r) && !is_loc(v.r);
}

static bool same(const char *a, const char *b) {
    return strcmp(a, b) == 0;
}

/* Move a value into a slot. */
static void move(Fn *g, int slot, Val v) {
    const char *di = ireg(slot), *dr = rreg(slot);
    if (!same(di, v.i))
        emit(g, is_loc(di) && !is_reg(v.i) ? "mov.i64 %s, %s" : "mov %s, %s", di, v.i);
    if (!same(dr, v.r))
        emit(g, "mov.r %s, %s", dr, v.r);
}

static Val to_temp(Fn *g, Val v) {
    int t = temp(g);
    move(g, t, v);
    return slot_val(t);
}

/* An R operand usable as a base (a register): a scratch register if need be. */
static const char *rbase(Fn *g, Val v, const char *scratch) {
    if (is_reg(v.r) || is_scratch(v.r))
        return v.r;
    emit(g, "mov.r %s, %s", scratch, v.r);
    return scratch;
}

/* An R operand for lpush: never the literal null (which would be an i64). */
static const char *rpush(Fn *g, const char *r, const char *scratch) {
    if (same(r, "null")) {
        emit(g, "mov.r %s, null", scratch);
        return scratch;
    }
    return r;
}

/* ------------------------------------------------------------------ variables */

static Var *find_var(Fn *g, const char *name) {
    for (int k = g->vars.n - 1; k >= 0; k--)
        if (g->vars.v[k].name == name)
            return &g->vars.v[k];
    return NULL;
}

/* Collect the variables a clause binds (not those inside funs and
 * comprehensions, which have their own). */
static void collect_vars(Node *e, Names *out) {
    if (!e)
        return;
    switch (e->kind) {
    case N_VAR:
        if (strcmp(e->s, "_") != 0) {
            for (int k = 0; k < out->n; k++)
                if (out->v[k] == e->s)
                    return;
            vec_push(*out, e->s);
        }
        return;
    case N_FUN:
    case N_LC:
    case N_BC:
    case N_MC:
        return;
    default:
        break;
    }
    collect_vars(e->a, out);
    collect_vars(e->b, out);
    collect_vars(e->c, out);
    for (int k = 0; k < e->kids.n; k++)
        collect_vars(e->kids.v[k], out);
    for (int pass = 0; pass < 2; pass++) {
        Clauses *cs = pass ? &e->cclauses : &e->clauses;
        for (int k = 0; k < cs->n; k++) {
            Clause *c = cs->v[k];
            for (int j = 0; j < c->pats.n; j++)
                collect_vars(c->pats.v[j], out);
            for (int j = 0; j < c->guards.n; j++)
                for (int q = 0; q < c->guards.v[j].n; q++)
                    collect_vars(c->guards.v[j].v[q], out);
            for (int j = 0; j < c->body.n; j++)
                collect_vars(c->body.v[j], out);
        }
    }
}

static void clause_vars(Clause *c, Names *out) {
    for (int j = 0; j < c->pats.n; j++)
        collect_vars(c->pats.v[j], out);
    for (int j = 0; j < c->guards.n; j++)
        for (int q = 0; q < c->guards.v[j].n; q++)
            collect_vars(c->guards.v[j].v[q], out);
    for (int j = 0; j < c->body.n; j++)
        collect_vars(c->body.v[j], out);
}

static bool mentions(Node *e, const char *name) {
    Names vs = {0};
    collect_vars(e, &vs);
    bool r = false;
    for (int k = 0; k < vs.n; k++)
        if (vs.v[k] == name)
            r = true;
    free(vs.v);
    return r;
}

/* The argument slot a variable can use: the first head pattern that is
 * just that variable, unless an earlier pattern binds it. -1 for none. */
/* Whether pattern p is the variable name, or binds it to the whole value
 * with =, as [X | Xs] = A does, mentioning it nowhere else. */
static bool whole_var(Node *p, const char *name) {
    if (p->kind == N_VAR)
        return p->s == name;
    if (p->kind == N_MATCH)
        return (whole_var(p->a, name) && !mentions(p->b, name)) ||
               (whole_var(p->b, name) && !mentions(p->a, name));
    return false;
}

static int head_slot(Clause *c, const char *name) {
    for (int j = 0; j < c->pats.n; j++) {
        Node *p = c->pats.v[j];
        if (p && whole_var(p, name))
            return j;
        if (p && mentions(p, name))
            return -1;
    }
    return -1;
}

/* ---- live ranges: where in evaluation order each variable is first and
 * last mentioned. A pattern is one position (its parts are matched in an
 * order of their own); a fun or a comprehension is one position (the
 * variables it uses must live through all of it); a variable mentioned
 * inside a receive lives to its end (a receive loops). */

typedef struct {
    Names names;
    VEC(int) first, last;
    int pos;
    int body;           /* where the clause's body starts (-1 until known) */
} Live;

static void live_note(Live *lv, const char *name, int pos) {
    for (int k = 0; k < lv->names.n; k++)
        if (lv->names.v[k] == name) {
            if (pos < lv->first.v[k])
                lv->first.v[k] = pos;
            if (pos > lv->last.v[k])
                lv->last.v[k] = pos;
            return;
        }
}

/* Every variable in e, as mentioned at one position. */
static void live_flat(Live *lv, Node *e, int pos) {
    if (!e)
        return;
    if (e->kind == N_VAR) {
        live_note(lv, e->s, pos);
        return;
    }
    live_flat(lv, e->a, pos);
    live_flat(lv, e->b, pos);
    live_flat(lv, e->c, pos);
    for (int k = 0; k < e->kids.n; k++)
        live_flat(lv, e->kids.v[k], pos);
    for (int pass = 0; pass < 2; pass++) {
        Clauses *cs = pass ? &e->cclauses : &e->clauses;
        for (int k = 0; k < cs->n; k++) {
            Clause *c = cs->v[k];
            for (int j = 0; j < c->pats.n; j++)
                live_flat(lv, c->pats.v[j], pos);
            for (int j = 0; j < c->guards.n; j++)
                for (int q = 0; q < c->guards.v[j].n; q++)
                    live_flat(lv, c->guards.v[j].v[q], pos);
            for (int j = 0; j < c->body.n; j++)
                live_flat(lv, c->body.v[j], pos);
        }
    }
}

static void live_walk(Live *lv, Node *e);

/* A sequence of expressions (a body, a guard): a variable mentioned in one
 * lives to its end, since an operand may be the variable's own slot, read
 * after later parts of the expression have bound other variables. */
static void live_seq(Live *lv, Nodes *body) {
    for (int j = 0; j < body->n; j++) {
        int start = lv->pos + 1;
        live_walk(lv, body->v[j]);
        int end = ++lv->pos;
        for (int k = 0; k < lv->names.n; k++)
            if (lv->last.v[k] >= start && lv->last.v[k] < end)
                lv->last.v[k] = end;
    }
}

static void live_clause(Live *lv, Clause *c) {
    lv->pos++;
    for (int j = 0; j < c->pats.n; j++)
        live_flat(lv, c->pats.v[j], lv->pos);
    for (int j = 0; j < c->guards.n; j++)
        live_seq(lv, &c->guards.v[j]);
    if (lv->body < 0)
        lv->body = lv->pos + 1;
    live_seq(lv, &c->body);
}

static void live_walk(Live *lv, Node *e) {
    if (!e)
        return;
    switch (e->kind) {
    case N_VAR:
        live_note(lv, e->s, ++lv->pos);
        return;
    case N_FUN: case N_LC: case N_BC: case N_MC:
        live_flat(lv, e, ++lv->pos);
        return;
    case N_MATCH:
        live_walk(lv, e->b);
        live_flat(lv, e->a, ++lv->pos);
        return;
    case N_BLOCK:
        live_seq(lv, &e->kids);
        return;
    case N_TRY:
        live_seq(lv, &e->kids);
        for (int k = 0; k < e->clauses.n; k++)
            live_clause(lv, e->clauses.v[k]);
        for (int k = 0; k < e->cclauses.n; k++)
            live_clause(lv, e->cclauses.v[k]);
        if (e->b)
            live_seq(lv, &e->b->kids);
        return;
    case N_RECEIVE: {
        int start = lv->pos + 1;
        live_walk(lv, e->a);
        for (int k = 0; k < e->clauses.n; k++)
            live_clause(lv, e->clauses.v[k]);
        live_seq(lv, &e->kids);
        int end = ++lv->pos;
        for (int k = 0; k < lv->names.n; k++)
            if (lv->last.v[k] >= start && lv->last.v[k] < end)
                lv->last.v[k] = end;
        return;
    }
    default:
        break;
    }
    live_walk(lv, e->a);
    live_walk(lv, e->b);
    live_walk(lv, e->c);
    for (int k = 0; k < e->kids.n; k++)
        live_walk(lv, e->kids.v[k]);
    for (int k = 0; k < e->clauses.n; k++)
        live_clause(lv, e->clauses.v[k]);
    for (int k = 0; k < e->cclauses.n; k++)
        live_clause(lv, e->cclauses.v[k]);
}

/* Slots for a clause's variables: slots[k] >= 0 is fixed (an argument's
 * slot); the others share slots from next up where their live ranges do
 * not overlap. Returns one past the highest slot used. */
static int assign_slots(Clause *c, Names *names, int *slots, int next) {
    Live lv = {0};
    lv.body = -1;
    for (int k = 0; k < names->n; k++) {
        vec_push(lv.names, names->v[k]);
        vec_push(lv.first, 1 << 30);
        vec_push(lv.last, -1);
    }
    live_clause(&lv, c);
    /* by first mention; each takes the lowest slot free by then */
    int n = names->n;
    int *order = xmalloc(((size_t)n + 1) * sizeof *order);
    for (int k = 0; k < n; k++)
        order[k] = k;
    for (int k = 1; k < n; k++)
        for (int j = k; j > 0 && lv.first.v[order[j]] < lv.first.v[order[j - 1]]; j--) {
            int t = order[j];
            order[j] = order[j - 1];
            order[j - 1] = t;
        }
    /* argument slots whose pattern is not a plain variable are free once
     * the body starts (the clause can no longer fail to match) */
    VEC(int) argslot = {0}, argbusy = {0};
    for (int j = 0; j < c->pats.n; j++) {
        bool used = false;
        for (int k = 0; k < n; k++)
            if (slots[k] == j)
                used = true;
        if (!used) {
            vec_push(argslot, j);
            vec_push(argbusy, -1);
        }
    }
    VEC(int) busy = {0};        /* per slot from next: the last position of its variable */
    int top = next;
    for (int q = 0; q < n; q++) {
        int k = order[q];
        if (slots[k] >= 0)
            continue;
        int f = lv.first.v[k], l = lv.last.v[k] < f ? f : lv.last.v[k];
        if (lv.body >= 0 && f >= lv.body) {
            int j = 0;
            while (j < argslot.n && argbusy.v[j] >= f)
                j++;
            if (j < argslot.n) {
                slots[k] = argslot.v[j];
                argbusy.v[j] = l;
                continue;
            }
        }
        int s = -1;
        for (int j = 0; j < busy.n; j++)
            if (busy.v[j] < f) {
                s = j;
                break;
            }
        if (s < 0) {
            vec_push(busy, l);
            s = busy.n - 1;
        } else {
            busy.v[s] = l;
        }
        slots[k] = next + s;
        if (next + s + 1 > top)
            top = next + s + 1;
    }
    free(argslot.v);
    free(argbusy.v);
    free(order);
    free(busy.v);
    free(lv.names.v);
    free(lv.first.v);
    free(lv.last.v);
    return top;
}

/* Give each variable of a function or fun clause its slot. A head
 * pattern that is just a new variable uses the argument's own slot. */
static void setup_clause(Fn *g, Clause *c, int base) {
    g->vars.n = base;
    Names names = {0};
    clause_vars(c, &names);
    int *slots = xmalloc(((size_t)names.n + 1) * sizeof *slots);
    for (int k = 0; k < names.n; k++)
        slots[k] = head_slot(c, names.v[k]);
    int next = assign_slots(c, &names, slots, g->nparams + g->ncaps);
    for (int k = 0; k < names.n; k++) {
        Var v = {names.v[k], slots[k], false, false};
        vec_push(g->vars, v);
    }
    free(slots);
    free(names.v);
    g->top = next;
    g->tbase = next;
    if (next - 1 > g->maxslot)
        g->maxslot = next - 1;
}

/* The bound flags of every variable, to save and restore around branches. */
typedef VEC(bool) Bound;

static Bound save_bound(Fn *g) {
    Bound b = {0};
    for (int k = 0; k < g->vars.n; k++)
        vec_push(b, g->vars.v[k].bound);
    return b;
}

static void restore_bound(Fn *g, Bound *b) {
    for (int k = 0; k < g->vars.n && k < b->n; k++)
        g->vars.v[k].bound = b->v[k];
}

/* Union: after a case, a variable bound in some clause counts as bound. */
static void union_bound(Fn *g, Bound *acc) {
    for (int k = 0; k < g->vars.n && k < acc->n; k++)
        acc->v[k] = acc->v[k] || g->vars.v[k].bound;
}

/* ------------------------------------------------------------------ errors in the program */

_Noreturn static void error_at(Node *e, const char *fmt, ...) {
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    fatal(e ? e->file : "?", e ? e->line : 0, "%s", msg);
}

/* ------------------------------------------------------------------ constants */

/* A term with no variables, built once per process and kept in a global. */
static bool constant(Node *e) {
    switch (e->kind) {
    case N_INT: case N_BIGINT: case N_FLOAT: case N_ATOM: case N_NIL: case N_STRING:
        return true;
    case N_CONS:
        return constant(e->a) && constant(e->b);
    case N_TUPLE:
        for (int k = 0; k < e->kids.n; k++)
            if (!constant(e->kids.v[k]))
                return false;
        return true;
    default:
        return false;
    }
}

static bool simple_const(Node *e) {
    return e->kind == N_INT || e->kind == N_BIGINT || e->kind == N_FLOAT || e->kind == N_ATOM
        || e->kind == N_NIL || (e->kind == N_STRING && e->len == 0);
}

/* Decode a string literal's UTF-8 into code points. */
static int decode(const char *s, int len, uint32_t *out) {
    int n = 0;
    for (int k = 0; k < len;) {
        unsigned char c = (unsigned char)s[k];
        uint32_t v;
        int more = 0;
        if (c < 0x80) v = c;
        else if (c >= 0xF0) v = c & 7, more = 3;
        else if (c >= 0xE0) v = c & 15, more = 2;
        else v = c & 31, more = 1;
        k++;
        for (int j = 0; j < more && k < len; j++)
            v = v << 6 | ((unsigned char)s[k++] & 0x3F);
        out[n++] = v;
    }
    return n;
}

static Val build_const(Fn *g, Node *e);
static Val make_tuple(Fn *g, Val *vs, int n);

/* Put a new cell (h, acc) into acc's slot. */
static void push_cell(Fn *g, int acc, Val h) {
    emit(g, "new %s, Cons", XR1);
    emit(g, "mov %s->Cons.hi, %s", XR1, h.i);
    emit(g, "mov.r %s->Cons.hr, %s", XR1, h.r);
    emit(g, "mov %s->Cons.ti, %s", XR1, ireg(acc));
    emit(g, "mov.r %s->Cons.tr, %s", XR1, rreg(acc));
    emit(g, "mov.r %s, %s", rreg(acc), XR1);
    emit(g, "mov %s, 0", ireg(acc));
}

/* A list of characters from a string literal, from the end. */
static Val build_string(Fn *g, const char *s, int len, Val tail) {
    uint32_t *cs = xmalloc(((size_t)len + 1) * sizeof *cs);
    int n = decode(s, len, cs);
    int acc = temp(g);
    move(g, acc, tail);
    for (int k = n - 1; k >= 0; k--)
        push_cell(g, acc, vint(cs[k]));
    free(cs);
    return slot_val(acc);
}

static Val build_const(Fn *g, Node *e) {
    switch (e->kind) {
    case N_INT: return vint(e->i);
    case N_BIGINT: return (Val){"0", S("%sn", e->s)};
    case N_FLOAT: return vfloat(e->f);
    case N_ATOM: return vatom(e->s);
    case N_NIL: return vnil();
    case N_STRING: return build_string(g, e->s, e->len, vnil());
    case N_CONS: {
        Nodes elems = {0};
        Node *q = e;
        while (q->kind == N_CONS) {
            vec_push(elems, q->a);
            q = q->b;
        }
        int acc = temp(g);
        int mark = g->top;
        move(g, acc, build_const(g, q));
        g->top = mark;
        for (int k = elems.n - 1; k >= 0; k--) {
            Val h = build_const(g, elems.v[k]);
            push_cell(g, acc, h);
            g->top = mark;
        }
        free(elems.v);
        return slot_val(acc);
    }
    case N_TUPLE: {
        Val *vs = xmalloc(((size_t)e->kids.n + 1) * sizeof *vs);
        for (int k = 0; k < e->kids.n; k++)
            vs[k] = build_const(g, e->kids.v[k]);
        Val tv = make_tuple(g, vs, e->kids.n);
        free(vs);
        return tv;
    }
    default:
        error_at(e, "not a constant");
    }
}

/* A constant term: a global, built on first use in each process. */
static Val const_term(Fn *g, Node *e) {
    if (simple_const(e))
        return build_const(g, e);
    int k = P.nconst++;
    /* the builder, as a function of its own */
    Fn b = {0};
    b.mod = g->mod;
    b.mname = S("K$%d", k);
    b.top = 0;
    b.maxslot = 0;
    Val v = build_const(&b, e);
    buf_printf(&P.consts, ".func K$%d -> ref\n", k);
    /* patch scratch registers: the builder uses only its own temps */
    int sx = b.maxslot + 1;
    if (sx >= NR) {
        int words = 2 * (b.maxslot - NR + 1);
        buf_printf(&P.consts, "    enter %d\n", words);
        sx = NR;
    }
    for (size_t q = 0; q < b.code.n; q++) {
        if (b.code.s[q] == '\001') {
            buf_printf(&P.consts, "%c%d", b.code.s[q + 1], sx + (b.code.s[q + 2] - '0'));
            q += 2;
        } else {
            buf_add(&P.consts, &b.code.s[q], 1);
        }
    }
    buf_printf(&P.consts, "    mov.r @K%d, %s\n    ret %s\n.endf\n", k, v.r, v.r);
    int t = temp(g);
    Val tv = slot_val(t);
    const char *done = new_label(g);
    emit(g, "mov.r %s, @K%d", tv.r, k);
    emit(g, "bnz.r %s, %s", tv.r, done);
    emit(g, "call K$%d -> %s", k, tv.r);
    label(g, done);
    emit(g, "mov %s, 0", tv.i);
    return tv;
}

/* ------------------------------------------------------------------ calls */

/* Arguments in the order a call takes them: the I halves, then the R halves. */
static const char *arglist(Val *args, int n) {
    Buf b = {0};
    for (int k = 0; k < n; k++)
        buf_printf(&b, "%s%s", b.n ? ", " : "", args[k].i);
    for (int k = 0; k < n; k++)
        buf_printf(&b, "%s%s", b.n ? ", " : "", args[k].r);
    buf_add(&b, "", 0);
    return b.s ? b.s : "";
}

static const char *sig(int n) {
    Buf b = {0};
    for (int k = 0; k < n; k++)
        buf_printf(&b, "%si64", b.n ? ", " : "");
    for (int k = 0; k < n; k++)
        buf_printf(&b, "%sref", b.n ? ", " : "");
    buf_add(&b, "", 0);
    return b.s ? b.s : "";
}

/* Call a function with the value convention, into a new temporary. */
static Val call_fn(Fn *g, const char *name, Val *args, int n) {
    int t = temp(g);
    Val tv = slot_val(t);
    emit(g, "call %s (%s) -> (%s, %s)", name, arglist(args, n), tv.i, tv.r);
    return tv;
}

static void tcall_fn(Fn *g, const char *name, Val *args, int n) {
    emit(g, "tcall %s (%s)", name, arglist(args, n));
}

/* rt$ functions with the value convention. */
static Val rt1(Fn *g, const char *f, Val a) { Val v[1] = {a}; return call_fn(g, S("rt$%s", f), v, 1); }
static Val rt2(Fn *g, const char *f, Val a, Val b) { Val v[2] = {a, b}; return call_fn(g, S("rt$%s", f), v, 2); }
static Val rt3(Fn *g, const char *f, Val a, Val b, Val c) { Val v[3] = {a, b, c}; return call_fn(g, S("rt$%s", f), v, 3); }

/* An error that never returns: error(Reason). */
static void raise_error(Fn *g, Val reason) {
    Val v[1] = {reason};
    int t = temp(g);
    emit(g, "call rt$error (%s) -> (%s, %s)", arglist(v, 1), ireg(t), rreg(t));
    g->top--;
}

/* Where to build an object for a slot: its register, or scratch XR1 for a
 * slot on the stack (then moved there by built()). */
static const char *build_reg(int slot) {
    return is_reg(rreg(slot)) ? rreg(slot) : XR1;
}

static void built(Fn *g, int slot, const char *r) {
    if (!same(r, rreg(slot)))
        emit(g, "mov.r %s, %s", rreg(slot), r);
    emit(g, is_loc(ireg(slot)) ? "mov.i64 %s, 0" : "mov %s, 0", ireg(slot));
}

static Val make_tuple(Fn *g, Val *vs, int n) {
    int t = temp(g);
    const char *r = build_reg(t);
    emit(g, "lnew %s, %d", r, 2 * n);
    for (int k = 0; k < n; k++)
        emit(g, "lpush %s, %s, %s", r, rpush(g, vs[k].r, XR0), vs[k].i);
    built(g, t, r);
    return slot_val(t);
}

static Val make_cons(Fn *g, Val h, Val t) {
    int c = temp(g);
    const char *r = build_reg(c);
    emit(g, "new %s, Cons", r);
    emit(g, "mov %s->Cons.hi, %s", r, h.i);
    emit(g, "mov.r %s->Cons.hr, %s", r, h.r);
    emit(g, "mov %s->Cons.ti, %s", r, t.i);
    emit(g, "mov.r %s->Cons.tr, %s", r, t.r);
    built(g, c, r);
    return slot_val(c);
}

static Val make_list(Fn *g, Val *vs, int n, Val tail) {
    if (n == 0)
        return tail;
    /* a tail in the newest temporary (a call's result, say) is built on
     * where it is, unless that is a variable's (a comprehension's) */
    int acc = g->top - 1;
    bool own = acc >= g->tbase && same(tail.i, ireg(acc)) && same(tail.r, rreg(acc));
    for (int k = 0; own && k < g->vars.n; k++)
        if (g->vars.v[k].slot == acc)
            own = false;
    if (!own) {
        acc = temp(g);
        move(g, acc, tail);
    }
    for (int k = n - 1; k >= 0; k--)
        push_cell(g, acc, vs[k]);
    return slot_val(acc);
}

/* ------------------------------------------------------------------ records */

static Record *find_record(Fn *g, Node *e, const char *name) {
    for (int k = 0; k < g->mod->records.n; k++)
        if (g->mod->records.v[k]->name == name)
            return g->mod->records.v[k];
    error_at(e, "record %s undefined", name);
}

static int field_index(Record *r, Node *e, const char *field) {
    for (int k = 0; k < r->fields.n; k++)
        if (r->fields.v[k] == field)
            return k;
    error_at(e, "field %s undefined in record %s", field, r->name);
}

/* A record construction or pattern as a tuple node. */
static Node *record_tuple(Fn *g, Node *e, bool pattern) {
    Record *r = find_record(g, e, e->s);
    Node *t = new_node(N_TUPLE, e->line, e->file);
    Node *name = new_node(N_ATOM, e->line, e->file);
    name->s = r->name;
    vec_push(t->kids, name);
    Node *rest = NULL;
    for (int k = 0; k < e->kids.n; k++)
        if (strcmp(e->kids.v[k]->a->s, "_") == 0)
            rest = e->kids.v[k]->b;
    for (int f = 0; f < r->fields.n; f++) {
        Node *v = NULL;
        for (int k = 0; k < e->kids.n; k++)
            if (e->kids.v[k]->a->s == r->fields.v[f])
                v = e->kids.v[k]->b;
        if (!v)
            v = rest;
        if (!v) {
            if (pattern) {
                v = new_node(N_VAR, e->line, e->file);
                v->s = intern("_", 1);
            } else if (r->defaults.v[f]) {
                v = r->defaults.v[f];
            } else {
                v = new_node(N_ATOM, e->line, e->file);
                v->s = intern("undefined", 9);
            }
        }
        vec_push(t->kids, v);
    }
    for (int k = 0; k < e->kids.n; k++)
        if (strcmp(e->kids.v[k]->a->s, "_") != 0)
            field_index(r, e, e->kids.v[k]->a->s);
    return t;
}

/* ------------------------------------------------------------------ patterns */

/* Is v a tuple of n elements? */
static void test_tuple(Fn *g, const char *r, int n, const char *fail) {
    emit(g, "typeof %s, %s", XI0, r);
    emit(g, "bne %s, 5, %s", XI0, fail);
    emit(g, "len %s, %s", XI0, r);
    emit(g, "bne %s, %d, %s", XI0, 2 * n, fail);
}

/* Is this pattern nothing but a new variable (or _)? */
static bool binds_only(Fn *g, Node *p) {
    if (p->kind != N_VAR)
        return false;
    if (strcmp(p->s, "_") == 0)
        return true;
    Var *v = find_var(g, p->s);
    return v && !v->bound;
}

/* The first phase of matching a sub-value at memory operands (i, r):
 * bind a new variable or test a literal straight from memory, or load it
 * into a new temporary and return that slot, to be matched once every
 * sub-value is loaded (-1 if nothing is left to do). Uses no scratch
 * registers, so the base of mi and mr may be one. */
static int match_load(Fn *g, Node *p, const char *mi, const char *mr, const char *fail) {
    if (p->kind == N_VAR) {
        if (strcmp(p->s, "_") == 0)
            return -1;
        Var *v = find_var(g, p->s);
        if (v && !v->bound) {
            emit(g, "mov %s, %s", ireg(v->slot), mi);
            emit(g, "mov.r %s, %s", rreg(v->slot), mr);
            v->bound = true;
            return -1;
        }
    }
    if (strstr(mr, "->") && (p->kind == N_ATOM || p->kind == N_NIL || p->kind == N_INT
                             || (p->kind == N_STRING && p->len == 0))) {
        /* a compare on a field is the VM's general form; on a register, fast */
        emit(g, "mov.r %s, %s", XR0, mr);
        mr = XR0;
    }
    if (p->kind == N_ATOM) {
        note_atom(p->s);
        emit(g, "bne.r %s, %s, %s", mr, quote(p->s, (int)strlen(p->s)), fail);
        return -1;
    }
    if (p->kind == N_NIL || (p->kind == N_STRING && p->len == 0)) {
        emit(g, "bne.r %s, 0d, %s", mr, fail);
        return -1;
    }
    if (p->kind == N_INT) {
        emit(g, "bne.r %s, null, %s", mr, fail);
        emit(g, "mov %s, %s", XI0, mi);
        emit(g, "bne %s, %lld, %s", XI0, (long long)p->i, fail);
        return -1;
    }
    int t = temp(g);
    emit(g, "mov %s, %s", ireg(t), mi);
    emit(g, "mov.r %s, %s", rreg(t), mr);
    return t;
}

/* A tuple pattern's elements, the tuple in register r having been tested:
 * load them, then match those that need more. */
static void match_elements(Fn *g, Node *p, const char *r, const char *fail) {
    int *pending = xmalloc(((size_t)p->kids.n + 1) * sizeof *pending);
    for (int k = 0; k < p->kids.n; k++)
        pending[k] = match_load(g, p->kids.v[k], S("%s[%d]", r, 2 * k + 1), S("%s[%d]", r, 2 * k), fail);
    for (int k = 0; k < p->kids.n; k++)
        if (pending[k] >= 0)
            match(g, p->kids.v[k], slot_val(pending[k]), fail);
    free(pending);
}

/* A register holding v's R, for reading fields: XR1 if need be. */
static const char *base_reg(Fn *g, Val v) {
    if (is_reg(v.r))
        return v.r;
    emit(g, "mov.r %s, %s", XR1, v.r);
    return XR1;
}

static void match_list(Fn *g, Node *p, Val v, const char *fail);

/* A binary pattern: the runtime walks the segments. */
static void match_bin(Fn *g, Node *p, Val v, const char *fail);

static void match_map(Fn *g, Node *p, Val v, const char *fail) {
    const char *r = rbase(g, v, XR0);
    emit(g, "bnisa %s, Map, %s", r, fail);
    int mark = g->top;
    Val m = to_temp(g, (Val){"0", r});
    for (int k = 0; k < p->kids.n; k++) {
        Node *a = p->kids.v[k];
        if (a->kind != N_EXACT)
            error_at(a, "illegal map pattern: use :=");
        Val key = ev(g, a->a);
        int t = temp(g);
        int found = temp(g);
        emit(g, "call rt$map_find (%s, %s, %s) -> (%s, %s, %s)", key.i, m.r, key.r, ireg(found), ireg(t), rreg(t));
        emit(g, "bz %s, %s", ireg(found), fail);
        match(g, a->b, slot_val(t), fail);
    }
    (void)mark;
}

static void match(Fn *g, Node *p, Val v, const char *fail) {
    switch (p->kind) {
    case N_VAR: {
        if (strcmp(p->s, "_") == 0)
            return;
        Var *var = find_var(g, p->s);
        if (!var)
            error_at(p, "variable '%s' is unbound", p->s);
        if (!var->bound) {
            move(g, var->slot, v);
            var->bound = true;
            return;
        }
        /* an already bound variable: the values must be equal */
        Val w = slot_val(var->slot);
        const char *slow = new_label(g), *ok = new_label(g);
        emit(g, "bne.r %s, %s, %s", v.r, w.r, slow);
        emit(g, "beq %s, %s, %s", v.i, w.i, ok);
        emit(g, "jmp %s", fail);
        label(g, slow);
        emit(g, "call rt$eqx (%s, %s, %s, %s) -> %s", v.i, w.i, v.r, w.r, XI0);
        emit(g, "bz %s, %s", XI0, fail);
        label(g, ok);
        return;
    }
    case N_INT:
        if (!same(v.r, "null"))
            emit(g, "bnz.r %s, %s", v.r, fail);
        emit(g, "bne %s, %lld, %s", v.i, (long long)p->i, fail);
        return;
    case N_BIGINT: {
        const char *r = rbase(g, v, XR0);
        emit(g, "typeof %s, %s", XI0, r);
        emit(g, "bne %s, 8, %s", XI0, fail);
        emit(g, "bne.big %s, %sn, %s", r, p->s, fail);
        return;
    }
    case N_FLOAT: {
        Val f = vfloat(p->f);
        emit(g, "bne.r %s, 1d, %s", v.r, fail);
        emit(g, "bne %s, %s, %s", v.i, f.i, fail);
        return;
    }
    case N_ATOM:
        note_atom(p->s);
        emit(g, "bne.r %s, %s, %s", v.r, quote(p->s, (int)strlen(p->s)), fail);
        return;
    case N_NIL:
        emit(g, "bne.r %s, 0d, %s", v.r, fail);
        return;
    case N_STRING:
    case N_CONS:
        match_list(g, p, v, fail);
        return;
    case N_TUPLE: {
        const char *r = base_reg(g, v);
        test_tuple(g, r, p->kids.n, fail);
        match_elements(g, p, r, fail);
        return;
    }
    case N_MATCH:
        match(g, p->a, v, fail);
        match(g, p->b, v, fail);
        return;
    case N_RECORD:
        if (p->a)
            error_at(p, "illegal pattern");
        match(g, record_tuple(g, p, true), v, fail);
        return;
    case N_RECINDEX: {
        Record *r = find_record(g, p, p->s);
        Node *n = new_node(N_INT, p->line, p->file);
        n->i = field_index(r, p, p->b->s) + 2;
        match(g, n, v, fail);
        return;
    }
    case N_MAP:
        if (p->a)
            error_at(p, "illegal pattern");
        match_map(g, p, v, fail);
        return;
    case N_BIN:
        match_bin(g, p, v, fail);
        return;
    case N_OP:
        if (strcmp(p->s, "++") == 0 && (p->a->kind == N_STRING || p->a->kind == N_NIL || p->a->kind == N_CONS)) {
            /* "abc" ++ T: a list whose tail is T */
            Node *l = p->a;
            if (l->kind == N_STRING) {
                uint32_t *cs = xmalloc(((size_t)l->len + 1) * sizeof *cs);
                int n = decode(l->s, l->len, cs);
                Node *acc = p->b;
                for (int k = n - 1; k >= 0; k--) {
                    Node *c = new_node(N_CONS, l->line, l->file);
                    c->a = new_node(N_INT, l->line, l->file);
                    c->a->i = cs[k];
                    c->b = acc;
                    acc = c;
                }
                free(cs);
                match(g, acc, v, fail);
                return;
            }
            /* [a, b] ++ T */
            Node *copy = NULL, **tailp = &copy;
            for (Node *q = l; q->kind == N_CONS; q = q->b) {
                Node *c = new_node(N_CONS, q->line, q->file);
                c->a = q->a;
                *tailp = c;
                tailp = &c->b;
            }
            *tailp = p->b;
            match(g, copy, v, fail);
            return;
        }
        /* an arithmetic expression of constants is a constant */
        error_at(p, "illegal pattern");
    case N_UNOP:
        error_at(p, "illegal pattern");
    default:
        error_at(p, "illegal pattern");
    }
}

static void match_list(Fn *g, Node *p, Val v, const char *fail) {
    if (p->kind == N_STRING) {
        if (p->len == 0) {
            emit(g, "bne.r %s, 0d, %s", v.r, fail);
            return;
        }
        uint32_t *cs = xmalloc(((size_t)p->len + 1) * sizeof *cs);
        int n = decode(p->s, p->len, cs);
        const char *r = rbase(g, v, XR0);
        if (!is_reg(r))
            r = to_temp(g, (Val){"0", r}).r;
        int cur = temp(g);
        emit(g, "mov.r %s, %s", rreg(cur), r);
        const char *c = is_reg(rreg(cur)) ? rreg(cur) : XR1;
        for (int k = 0; k < n; k++) {
            if (!is_reg(rreg(cur)))
                emit(g, "mov.r %s, %s", c, rreg(cur));
            emit(g, "bnisa %s, Cons, %s", c, fail);
            emit(g, "mov.r %s, %s->Cons.hr", XR0, c);
            emit(g, "bnz.r %s, %s", XR0, fail);
            emit(g, "mov %s, %s->Cons.hi", XI0, c);
            emit(g, "bne %s, %u, %s", XI0, cs[k], fail);
            emit(g, "mov.r %s, %s->Cons.tr", rreg(cur), c);
        }
        emit(g, "bne.r %s, 0d, %s", rreg(cur), fail);
        free(cs);
        return;
    }
    const char *r = base_reg(g, v);
    emit(g, "bnisa %s, Cons, %s", r, fail);
    int h = match_load(g, p->a, S("%s->Cons.hi", r), S("%s->Cons.hr", r), fail);
    int t = match_load(g, p->b, S("%s->Cons.ti", r), S("%s->Cons.tr", r), fail);
    if (h >= 0)
        match(g, p->a, slot_val(h), fail);
    if (t >= 0)
        match(g, p->b, slot_val(t), fail);
}

static void match_bin(Fn *g, Node *p, Val v, const char *fail) {
    /* Each segment: rt$bin_get (bin, offset, size, flags) -> (ok, value, next offset) */
    const char *r = rbase(g, v, XR0);
    emit(g, "bnisa %s, Bin, %s", r, fail);
    Val bin = to_temp(g, (Val){"0", r});
    int off = temp(g);
    emit(g, "mov %s, 0", ireg(off));
    for (int k = 0; k < p->kids.n; k++) {
        Node *seg = p->kids.v[k];
        const char *spec = seg->s ? seg->s : "";
        int type = 0;   /* 0 integer, 1 binary, 2 float, 3 utf8 */
        int flags = 0;  /* 1 signed, 2 little */
        int unit = -1;
        char *sp = xstrdup(spec);
        for (char *tok = strtok(sp, "-"); tok; tok = strtok(NULL, "-")) {
            if (strcmp(tok, "integer") == 0) type = 0;
            else if (strcmp(tok, "binary") == 0 || strcmp(tok, "bytes") == 0) type = 1;
            else if (strcmp(tok, "bits") == 0 || strcmp(tok, "bitstring") == 0) type = 1;
            else if (strcmp(tok, "float") == 0) type = 2;
            else if (strcmp(tok, "utf8") == 0) type = 3;
            else if (strcmp(tok, "signed") == 0) flags |= 1;
            else if (strcmp(tok, "unsigned") == 0) flags &= ~1;
            else if (strcmp(tok, "little") == 0) flags |= 2;
            else if (strcmp(tok, "big") == 0) flags &= ~2;
            else if (strncmp(tok, "unit:", 5) == 0) unit = atoi(tok + 5);
            else if (strcmp(tok, "native") == 0) flags |= 2;
            else error_at(seg, "unsupported binary type %s", tok);
        }
        free(sp);
        if (unit < 0)
            unit = type == 1 ? 8 : 1;
        /* the size in bits: -1 for the rest of a binary */
        Val size;
        if (seg->b) {
            size = ev(g, seg->b);
        } else if (type == 0) {
            size = vint(8);
        } else if (type == 2) {
            size = vint(64);
        } else if (type == 3) {
            size = vint(0);
        } else {
            size = vint(-1);
            unit = 8;
        }
        if (seg->a->kind == N_STRING) {
            /* <<"abc", ...>>: the bytes, each an 8-bit integer */
            uint32_t *cs = xmalloc(((size_t)seg->a->len + 1) * sizeof *cs);
            int n = decode(seg->a->s, seg->a->len, cs);
            for (int q = 0; q < n; q++) {
                int ok = temp(g), val = temp(g);
                emit(g, "call rt$bin_get (%s, 8, 1, 0, %s, null) -> (%s, %s, %s, %s)", ireg(off), bin.r,
                     ireg(ok), ireg(val), ireg(off), rreg(val));
                emit(g, "bz %s, %s", ireg(ok), fail);
                g->top -= 2;
                emit(g, "bne %s, %u, %s", ireg(val), cs[q] & 255, fail);
            }
            free(cs);
            continue;
        }
        int ok = temp(g), val = temp(g);
        emit(g, "call rt$bin_get (%s, %s, %d, %d, %s, %s) -> (%s, %s, %s, %s)",
             ireg(off), size.i, unit, type * 4 + flags, bin.r, size.r,
             ireg(ok), ireg(val), ireg(off), rreg(val));
        emit(g, "bz %s, %s", ireg(ok), fail);
        match(g, seg->a, slot_val(val), fail);
    }
    emit(g, "call rt$bin_end (%s, %s) -> %s", ireg(off), bin.r, XI0);
    emit(g, "bz %s, %s", XI0, fail);
}

/* ------------------------------------------------------------------ guards */

static int type_test(const char *name, int arity);

static bool can_raise(Node *e) {
    if (!e)
        return false;
    switch (e->kind) {
    case N_OP:
        /* comparisons and the short-circuit operators raise nothing themselves */
        if (strcmp(e->s, "==") && strcmp(e->s, "/=") && strcmp(e->s, "=:=") && strcmp(e->s, "=/=")
            && strcmp(e->s, "<") && strcmp(e->s, "=<") && strcmp(e->s, ">") && strcmp(e->s, ">=")
            && strcmp(e->s, "andalso") && strcmp(e->s, "orelse"))
            return true;
        break;
    case N_CALL:
        if (!(e->a->kind == N_ATOM && type_test(e->a->s, e->kids.n) >= 0 && e->kids.n == 1))
            return true;
        break;
    case N_UNOP: case N_RECFIELD:
        return true;
    default:
        break;
    }
    for (int k = 0; k < e->kids.n; k++)
        if (can_raise(e->kids.v[k]))
            return true;
    return can_raise(e->a) || can_raise(e->b);
}

/* Guards: alternatives separated by ';', each a list of tests. Anything
 * that raises makes the alternative fail. */
static void guards(Fn *g, Clause *c, const char *fail) {
    if (!c->guards.n)
        return;
    const char *ok = new_label(g);
    for (int a = 0; a < c->guards.n; a++) {
        Nodes *tests = &c->guards.v[a];
        bool last = a == c->guards.n - 1;
        const char *next = last ? fail : new_label(g);
        bool raise = false;
        for (int k = 0; k < tests->n; k++)
            raise = raise || can_raise(tests->v[k]);
        const char *ls = NULL, *le = NULL, *lh = NULL;
        if (raise) {
            ls = new_label(g);
            le = new_label(g);
            lh = new_label(g);
            emit(g, ".try %s, %s, %s", ls, le, lh);
            label(g, ls);
        }
        int mark = g->top;
        Buf saved = raise ? begin_region(g) : (Buf){0};
        for (int k = 0; k < tests->n; k++)
            cond(g, tests->v[k], next);
        g->top = mark;
        if (raise) {
            end_region(g, saved);
            label(g, le);
            vec_push(g->handlers, S("%s:%s", lh, next));
        }
        if (!last) {
            emit(g, "jmp %s", ok);
            label(g, next);
        }
    }
    label(g, ok);
}

/* ------------------------------------------------------------------ conditions */

static int type_test(const char *name, int arity);

/* Comparison of two values: branch to fail unless a op b holds. */
static void compare(Fn *g, const char *op, Val a, Val b, const char *fail) {
    const char *slow = new_label(g), *done = new_label(g);
    bool eq = strcmp(op, "=:=") == 0 || strcmp(op, "==") == 0;
    bool ne = strcmp(op, "=/=") == 0 || strcmp(op, "/=") == 0;
    bool exact = strcmp(op, "=:=") == 0 || strcmp(op, "=/=") == 0;
    if (eq || ne) {
        /* same R and same I: the same term (with I 0 for objects) */
        if (!(is_const(a) && is_const(b))) {
            emit(g, "bne.r %s, %s, %s", a.r, b.r, slow);
            if (eq) {
                if (exact || same(a.r, "null") || same(b.r, "null")) {
                    emit(g, "bne %s, %s, %s", a.i, b.i, fail);
                    emit(g, "jmp %s", done);
                } else {
                    emit(g, "beq %s, %s, %s", a.i, b.i, done);
                    emit(g, "bne.r %s, 1d, %s", a.r, fail);
                    emit(g, "jmp %s", slow);
                }
            } else {
                if (exact || same(a.r, "null") || same(b.r, "null")) {
                    emit(g, "beq %s, %s, %s", a.i, b.i, fail);
                    emit(g, "jmp %s", done);
                } else {
                    emit(g, "beq %s, %s, %s", a.i, b.i, fail);
                    emit(g, "bne.r %s, 1d, %s", a.r, done);
                    emit(g, "jmp %s", slow);
                }
            }
        }
        label(g, slow);
        emit(g, "call rt$%s (%s, %s, %s, %s) -> %s", exact ? "eqx" : "eq", a.i, b.i, a.r, b.r, XI0);
        emit(g, "%s %s, %s", eq ? "bz" : "bnz", XI0, fail);
        label(g, done);
        return;
    }
    /* < =< > >=: integers inline, the rest by the term order */
    const char *neg = strcmp(op, "<") == 0 ? "bge" : strcmp(op, "=<") == 0 ? "bgt"
                    : strcmp(op, ">") == 0 ? "ble" : "blt";
    if (!same(a.r, "null") || !same(b.r, "null")) {
        if (!same(a.r, "null"))
            emit(g, "bnz.r %s, %s", a.r, slow);
        if (!same(b.r, "null"))
            emit(g, "bnz.r %s, %s", b.r, slow);
    }
    emit(g, "%s %s, %s, %s", neg, a.i, b.i, fail);
    if (!same(a.r, "null") || !same(b.r, "null")) {
        g->in_cold = true;
        label(g, slow);
        emit(g, "call rt$cmp (%s, %s, %s, %s) -> %s", a.i, b.i, a.r, b.r, XI0);
        emit(g, "%s %s, 0, %s", neg, XI0, fail);
        emit(g, "jmp %s", done);
        g->in_cold = false;
    }
    label(g, done);
}

/* Branch to fail unless e is true (any other value counts as false). */
static void cond(Fn *g, Node *e, const char *fail) {
    int mark = g->top;
    loc(g, e);
    if (e->kind == N_ATOM && strcmp(e->s, "true") == 0)
        return;
    if (e->kind == N_OP) {
        const char *op = e->s;
        if (!strcmp(op, "==") || !strcmp(op, "/=") || !strcmp(op, "=:=") || !strcmp(op, "=/=")
            || !strcmp(op, "<") || !strcmp(op, "=<") || !strcmp(op, ">") || !strcmp(op, ">=")) {
            Val a = ev(g, e->a);
            Val b = ev(g, e->b);
            compare(g, op, a, b, fail);
            g->top = mark;
            return;
        }
        if (!strcmp(op, "andalso")) {
            cond(g, e->a, fail);
            cond(g, e->b, fail);
            return;
        }
        if (!strcmp(op, "orelse")) {
            const char *next = new_label(g), *ok = new_label(g);
            cond(g, e->a, next);
            emit(g, "jmp %s", ok);
            label(g, next);
            cond(g, e->b, fail);
            label(g, ok);
            return;
        }
    }
    if (e->kind == N_UNOP && strcmp(e->s, "not") == 0) {
        /* not X: X must be false */
        Val v = ev(g, e->a);
        note_atom(intern("false", 5));
        emit(g, "bne.r %s, \"false\", %s", v.r, fail);
        g->top = mark;
        return;
    }
    const char *tt = NULL;
    if (e->kind == N_CALL && e->a->kind == N_ATOM && !find_function(g->mod, e->a->s, e->kids.n))
        tt = e->a->s;
    if (e->kind == N_CALL && e->a->kind == N_REMOTE && e->a->a->kind == N_ATOM
        && e->a->b->kind == N_ATOM && !strcmp(e->a->a->s, "erlang"))
        tt = e->a->b->s;
    if (tt && type_test(tt, e->kids.n) >= 0) {
        Val args[3];
        for (int k = 0; k < e->kids.n && k < 3; k++)
            args[k] = ev(g, e->kids.v[k]);
        const char *t = tt;
        const char *r = args[0].r;
        if (!strcmp(t, "is_integer")) {
            const char *ok = new_label(g);
            emit(g, "bz.r %s, %s", r, ok);
            emit(g, "typeof %s, %s", XI0, r);
            emit(g, "bne %s, 8, %s", XI0, fail);
            label(g, ok);
        } else if (!strcmp(t, "is_float")) {
            emit(g, "bne.r %s, 1d, %s", r, fail);
        } else if (!strcmp(t, "is_number")) {
            const char *ok = new_label(g);
            emit(g, "bz.r %s, %s", r, ok);
            emit(g, "beq.r %s, 1d, %s", r, ok);
            emit(g, "typeof %s, %s", XI0, r);
            emit(g, "bne %s, 8, %s", XI0, fail);
            label(g, ok);
        } else if (!strcmp(t, "is_atom")) {
            emit(g, "typeof %s, %s", XI0, r);
            emit(g, "bne %s, 1, %s", XI0, fail);
        } else if (!strcmp(t, "is_boolean")) {
            const char *ok = new_label(g);
            note_atom(intern("true", 4));
            note_atom(intern("false", 5));
            emit(g, "beq.r %s, \"true\", %s", r, ok);
            emit(g, "bne.r %s, \"false\", %s", r, fail);
            label(g, ok);
        } else if (!strcmp(t, "is_list")) {
            const char *ok = new_label(g);
            emit(g, "beq.r %s, 0d, %s", r, ok);
            emit(g, "bnisa %s, Cons, %s", rbase(g, args[0], XR0), fail);
            label(g, ok);
        } else if (!strcmp(t, "is_tuple")) {
            emit(g, "typeof %s, %s", XI0, r);
            emit(g, "bne %s, 5, %s", XI0, fail);
        } else if (!strcmp(t, "is_pid")) {
            emit(g, "bnisa %s, Pid, %s", rbase(g, args[0], XR0), fail);
        } else if (!strcmp(t, "is_reference")) {
            emit(g, "bnisa %s, Ref, %s", rbase(g, args[0], XR0), fail);
        } else if (!strcmp(t, "is_map")) {
            emit(g, "bnisa %s, Map, %s", rbase(g, args[0], XR0), fail);
        } else if (!strcmp(t, "is_binary") || !strcmp(t, "is_bitstring")) {
            emit(g, "bnisa %s, Bin, %s", rbase(g, args[0], XR0), fail);
        } else if (!strcmp(t, "is_function") && e->kids.n == 1) {
            emit(g, "bnisa %s, Fun, %s", rbase(g, args[0], XR0), fail);
        } else if (!strcmp(t, "is_function")) {
            const char *b = rbase(g, args[0], XR0);
            emit(g, "bnisa %s, Fun, %s", b, fail);
            emit(g, "bnz.r %s, %s", args[1].r, fail);
            emit(g, "mov %s, %s->Fun.arity", XI0, b);
            emit(g, "bne %s, %s, %s", XI0, args[1].i, fail);
        } else if (!strcmp(t, "is_record")) {
            /* is_record(X, name) or is_record(X, name, size) */
            const char *b = rbase(g, args[0], XR0);
            int size;
            if (e->kids.n == 3) {
                if (e->kids.v[2]->kind != N_INT)
                    error_at(e, "is_record/3 needs a literal size");
                size = (int)e->kids.v[2]->i;
            } else {
                if (e->kids.v[1]->kind != N_ATOM)
                    error_at(e, "is_record/2 needs a literal record name");
                size = find_record(g, e, e->kids.v[1]->s)->fields.n + 1;
            }
            test_tuple(g, b, size, fail);
            emit(g, "bne.r %s[0], %s, %s", b, args[1].r, fail);
        }
        g->top = mark;
        return;
    }
    Val v = ev(g, e);
    note_atom(intern("true", 4));
    emit(g, "bne.r %s, \"true\", %s", v.r, fail);
    g->top = mark;
}

static int type_test(const char *name, int arity) {
    static const struct { const char *name; int arity; } tests[] = {
        {"is_integer", 1}, {"is_float", 1}, {"is_number", 1}, {"is_atom", 1},
        {"is_boolean", 1}, {"is_list", 1}, {"is_tuple", 1}, {"is_pid", 1},
        {"is_reference", 1}, {"is_map", 1}, {"is_binary", 1}, {"is_bitstring", 1},
        {"is_function", 1}, {"is_function", 2}, {"is_record", 2}, {"is_record", 3},
    };
    for (size_t k = 0; k < sizeof tests / sizeof *tests; k++)
        if (strcmp(tests[k].name, name) == 0 && tests[k].arity == arity)
            return (int)k;
    return -1;
}

/* A condition's value: true or false. */
static Val bool_val(Fn *g, Node *e) {
    int t = temp(g);
    int mark = g->top;
    const char *no = new_label(g), *done = new_label(g);
    cond(g, e, no);
    g->top = mark;
    note_atom(intern("true", 4));
    note_atom(intern("false", 5));
    emit(g, "mov.r %s, \"true\"", rreg(t));
    emit(g, "jmp %s", done);
    label(g, no);
    emit(g, "mov.r %s, \"false\"", rreg(t));
    label(g, done);
    emit(g, "mov %s, 0", ireg(t));
    return slot_val(t);
}

/* ------------------------------------------------------------------ arithmetic */

/* Two floats: the operation inline, into tv; to fail if either is not a
 * float or the result is not finite (Erlang's badarith). */
static void float_op(Fn *g, const char *fop, Val a, Val b, Val tv, const char *fail) {
    if (!same(a.r, "1d"))
        emit(g, "bne.r %s, 1d, %s", a.r, fail);
    bool one = same(a.i, b.i) && same(a.r, b.r);   /* X * X */
    if (!same(b.r, "1d") && !one)
        emit(g, "bne.r %s, 1d, %s", b.r, fail);
    emit(g, "bitcast.f64.i64 F0, %s", a.i);
    if (!one)
        emit(g, "bitcast.f64.i64 F1, %s", b.i);
    if (!strcmp(fop, "div"))
        emit(g, "beq %s, 0.0, %s", one ? "F0" : "F1", fail);
    emit(g, "%s F2, F0, %s", fop, one ? "F0" : "F1");
    emit(g, "sub F3, F2, F2");
    emit(g, "bne F3, 0.0, %s", fail);
    emit(g, "bitcast.i64.f64 %s, F2", tv.i);
    emit(g, "mov.r %s, 1d", tv.r);
}

/* Is e likely to be a float? Then arithmetic tries floats first. */
static bool likely_float(Fn *g, Node *e) {
    switch (e->kind) {
    case N_FLOAT:
        return true;
    case N_VAR: {
        Var *v = find_var(g, e->s);
        return v && v->flt;
    }
    case N_OP:
        if (!strcmp(e->s, "/"))
            return true;
        if (!strcmp(e->s, "+") || !strcmp(e->s, "-") || !strcmp(e->s, "*"))
            return likely_float(g, e->a) || likely_float(g, e->b);
        return false;
    case N_UNOP:
        return !strcmp(e->s, "-") && likely_float(g, e->a);
    case N_CALL:
        if (e->a->kind == N_REMOTE && e->a->a->kind == N_ATOM && !strcmp(e->a->a->s, "math"))
            return true;
        return e->a->kind == N_ATOM && !strcmp(e->a->s, "float") && e->kids.n == 1;
    case N_MATCH:
        return likely_float(g, e->b);
    default:
        return false;
    }
}

static Val arith_f(Fn *g, const char *op, Val a, Val b, bool flt);

static Val arith(Fn *g, const char *op, Val a, Val b) {
    return arith_f(g, op, a, b, false);
}

static Val arith_f(Fn *g, const char *op, Val a, Val b, bool flt) {
    /* a constant second, where the order does not matter: the VM's fast form */
    if (is_const(a) && !is_const(b) && (!strcmp(op, "+") || !strcmp(op, "*") || !strcmp(op, "band")
                                        || !strcmp(op, "bor") || !strcmp(op, "bxor"))) {
        Val t = a;
        a = b;
        b = t;
    }
    int t = temp(g);
    Val tv = slot_val(t);
    const char *slow = new_label(g), *done = new_label(g);
    const char *rt = NULL;
    bool fast = true;
    if (!strcmp(op, "+")) rt = "add";
    else if (!strcmp(op, "-")) rt = "sub";
    else if (!strcmp(op, "*")) rt = "mul";
    else if (!strcmp(op, "/")) rt = "fdiv", fast = false;
    else if (!strcmp(op, "div")) rt = "idiv";
    else if (!strcmp(op, "rem")) rt = "irem";
    else if (!strcmp(op, "band")) rt = "band";
    else if (!strcmp(op, "bor")) rt = "bor";
    else if (!strcmp(op, "bxor")) rt = "bxor";
    else if (!strcmp(op, "bsl")) rt = "bsl", fast = false;
    else if (!strcmp(op, "bsr")) rt = "bsr";
    const char *fop = !strcmp(op, "+") ? "add" : !strcmp(op, "-") ? "sub" : !strcmp(op, "*") ? "mul"
                    : !strcmp(op, "/") ? "div" : NULL;
    bool fconst = same(a.r, "1d") || same(b.r, "1d");
    bool nonnum_const = (is_const(a) && !same(a.r, "null") && !same(a.r, "1d"))
                     || (is_const(b) && !same(b.r, "null") && !same(b.r, "1d"));
    if (fop && !nonnum_const && (fconst || flt || !strcmp(fop, "div"))
        && !(same(a.r, "null") && same(b.r, "null"))) {
        /* a float constant, or /: floats inline, the rest by the runtime */
        float_op(g, fop, a, b, tv, slow);
        g->in_cold = true;
        label(g, slow);
        emit(g, "call rt$%s (%s, %s, %s, %s) -> (%s, %s)", rt, a.i, b.i, a.r, b.r, tv.i, tv.r);
        emit(g, "jmp %s", done);
        g->in_cold = false;
        label(g, done);
        return tv;
    }
    if (fast && !(same(a.r, "1d") || same(b.r, "1d") || (is_const(a) && !same(a.r, "null"))
                  || (is_const(b) && !same(b.r, "null")))) {
        if (!same(a.r, "null"))
            emit(g, "bnz.r %s, %s", a.r, slow);
        bool one = same(a.i, b.i) && same(a.r, b.r);   /* X * X */
        if (!same(b.r, "null") && !one)
            emit(g, "bnz.r %s, %s", b.r, slow);
        bool bconst = same(b.r, "null") && !is_reg(b.i) && !is_loc(b.i) && !is_scratch(b.i);
        long long bc = bconst ? atoll(b.i) : 0;
        if (!strcmp(rt, "add") || !strcmp(rt, "sub")) {
            bool add = !strcmp(rt, "add");
            if (bconst && bc != INT64_MIN) {
                long long d = add ? bc : -bc;
                if (d > 0)
                    emit(g, "bgt %s, %lld, %s", a.i, (long long)(INT64_MAX - d), slow);
                else if (d < 0)
                    emit(g, "blt %s, %lld, %s", a.i, (long long)(INT64_MIN - d), slow);
                emit(g, "%s %s, %s, %s", rt, tv.i, a.i, b.i);
            } else {
                emit(g, "%s %s, %s, %s", rt, tv.i, a.i, b.i);
                /* signed overflow: the result's sign is wrong */
                if (add) {
                    emit(g, "xor %s, %s, %s", XI0, a.i, tv.i);
                    emit(g, "xor %s, %s, %s", XI1, b.i, tv.i);
                } else {
                    emit(g, "xor %s, %s, %s", XI0, a.i, b.i);
                    emit(g, "xor %s, %s, %s", XI1, a.i, tv.i);
                }
                emit(g, "and %s, %s", XI0, XI1);
                emit(g, "blt %s, 0, %s", XI0, slow);
            }
        } else if (!strcmp(rt, "mul")) {
            /* both within 32 bits: the product fits */
            emit(g, "add %s, %s, 2147483648", XI0, a.i);
            emit(g, "bgt.u64 %s, 4294967295, %s", XI0, slow);
            if (!one && !(bconst && bc >= -2147483648LL && bc <= 2147483647LL)) {
                emit(g, "add %s, %s, 2147483648", XI0, b.i);
                emit(g, "bgt.u64 %s, 4294967295, %s", XI0, slow);
            }
            emit(g, "mul %s, %s, %s", tv.i, a.i, b.i);
        } else if (!strcmp(rt, "idiv") || !strcmp(rt, "irem")) {
            if (!bconst || bc == 0 || bc == -1) {
                emit(g, "beq %s, 0, %s", b.i, slow);
                emit(g, "beq %s, -1, %s", b.i, slow);
            }
            emit(g, "%s %s, %s, %s", !strcmp(rt, "idiv") ? "div" : "rem", tv.i, a.i, b.i);
        } else if (!strcmp(rt, "band") || !strcmp(rt, "bor") || !strcmp(rt, "bxor")) {
            emit(g, "%s %s, %s, %s", !strcmp(rt, "band") ? "and" : !strcmp(rt, "bor") ? "or" : "xor",
                 tv.i, a.i, b.i);
        } else if (!strcmp(rt, "bsr")) {
            if (!bconst || bc < 0 || bc > 63) {
                emit(g, "blt %s, 0, %s", b.i, slow);
                emit(g, "bgt %s, 63, %s", b.i, slow);
            }
            emit(g, "sar %s, %s, %s", tv.i, a.i, b.i);
        }
        emit(g, "mov.r %s, null", tv.r);
        g->in_cold = true;
        label(g, slow);
        if (fop && !same(a.r, "null") && !same(b.r, "null")) {
            /* not two small integers: perhaps two floats */
            const char *rt_call = new_label(g);
            float_op(g, fop, a, b, tv, rt_call);
            emit(g, "jmp %s", done);
            label(g, rt_call);
        }
        emit(g, "call rt$%s (%s, %s, %s, %s) -> (%s, %s)", rt, a.i, b.i, a.r, b.r, tv.i, tv.r);
        emit(g, "jmp %s", done);
        g->in_cold = false;
        label(g, done);
        return tv;
    }
    emit(g, "call rt$%s (%s, %s, %s, %s) -> (%s, %s)", rt, a.i, b.i, a.r, b.r, tv.i, tv.r);
    return tv;
}

/* ------------------------------------------------------------------ BIFs */

/* Inline BIFs: return true if handled (with the result in *out). */
static bool inline_bif(Fn *g, Node *e, const char *name, int n, Val *out) {
    Node **k = e->kids.v;
    if (type_test(name, n) >= 0) {
        *out = bool_val(g, e);
        return true;
    }
    if (!strcmp(name, "hd") && n == 1) {
        Val v = ev(g, k[0]);
        const char *r = rbase(g, v, XR0);
        int t = temp(g);
        const char *bad = new_label(g), *ok = new_label(g);
        emit(g, "bnisa %s, Cons, %s", r, bad);
        emit(g, "mov %s, %s->Cons.hi", ireg(t), r);
        emit(g, "mov.r %s, %s->Cons.hr", rreg(t), r);
        g->in_cold = true;
        label(g, bad);
        raise_error(g, vatom(intern("badarg", 6)));
        emit(g, "jmp %s", ok);
        g->in_cold = false;
        label(g, ok);
        *out = slot_val(t);
        return true;
    }
    if (!strcmp(name, "tl") && n == 1) {
        Val v = ev(g, k[0]);
        const char *r = rbase(g, v, XR0);
        int t = temp(g);
        const char *bad = new_label(g), *ok = new_label(g);
        emit(g, "bnisa %s, Cons, %s", r, bad);
        emit(g, "mov %s, %s->Cons.ti", ireg(t), r);
        emit(g, "mov.r %s, %s->Cons.tr", rreg(t), r);
        g->in_cold = true;
        label(g, bad);
        raise_error(g, vatom(intern("badarg", 6)));
        emit(g, "jmp %s", ok);
        g->in_cold = false;
        label(g, ok);
        *out = slot_val(t);
        return true;
    }
    if (!strcmp(name, "element") && n == 2) {
        Val i = ev(g, k[0]);
        Val v = ev(g, k[1]);
        int t = temp(g);
        if (k[0]->kind == N_INT && k[0]->i >= 1) {
            const char *r = rbase(g, v, XR0);
            const char *bad = new_label(g), *ok = new_label(g);
            emit(g, "typeof %s, %s", XI0, r);
            emit(g, "bne %s, 5, %s", XI0, bad);
            emit(g, "len %s, %s", XI0, r);
            emit(g, "blt %s, %lld, %s", XI0, (long long)(2 * k[0]->i), bad);
            emit(g, "mov %s, %s[%lld]", ireg(t), r, (long long)(2 * k[0]->i - 1));
            emit(g, "mov.r %s, %s[%lld]", rreg(t), r, (long long)(2 * k[0]->i - 2));
            g->in_cold = true;
            label(g, bad);
            raise_error(g, vatom(intern("badarg", 6)));
            emit(g, "jmp %s", ok);
            g->in_cold = false;
            label(g, ok);
        } else {
            emit(g, "call rt$element (%s, %s, %s, %s) -> (%s, %s)", i.i, v.i, i.r, v.r, ireg(t), rreg(t));
        }
        *out = slot_val(t);
        return true;
    }
    if (!strcmp(name, "self") && n == 0) {
        *out = call_fn(g, "rt$self", NULL, 0);
        return true;
    }
    if ((!strcmp(name, "throw") || !strcmp(name, "exit")) && n == 1) {
        Val v = ev(g, k[0]);
        *out = rt1(g, name, v);
        return true;
    }
    if (!strcmp(name, "error") && (n == 1 || n == 2)) {
        Val v = ev(g, k[0]);
        *out = rt1(g, "error", v);
        return true;
    }
    if (!strcmp(name, "apply") && n == 2) {
        Val f = ev(g, k[0]);
        Val a = ev(g, k[1]);
        *out = rt2(g, "apply_fun", f, a);
        return true;
    }
    if (!strcmp(name, "apply") && n == 3) {
        P.dispatch = true;
        Val m = ev(g, k[0]);
        Val f = ev(g, k[1]);
        Val a = ev(g, k[2]);
        *out = rt3(g, "apply", m, f, a);
        return true;
    }
    return false;
}

/* BIFs written in assembly, with the value convention. */
static const char *rt_bif(const char *name, int n) {
    static const struct { const char *name; int arity; const char *rt; } bifs[] = {
        {"length", 1, "length"}, {"tuple_size", 1, "tuple_size"}, {"size", 1, "size"},
        {"setelement", 3, "setelement"}, {"element", 2, "element"},
        {"abs", 1, "abs"}, {"float", 1, "float"}, {"trunc", 1, "trunc"}, {"round", 1, "round"},
        {"floor", 1, "floor"}, {"ceil", 1, "ceil"},
        {"atom_to_list", 1, "atom_to_list"}, {"list_to_atom", 1, "list_to_atom"},
        {"list_to_existing_atom", 1, "list_to_atom"},
        {"integer_to_list", 1, "integer_to_list"},
        {"list_to_integer", 1, "list_to_integer"},
        {"float_to_list", 1, "float_to_list"},
        {"list_to_float", 1, "list_to_float"},
        {"tuple_to_list", 1, "tuple_to_list"}, {"list_to_tuple", 1, "list_to_tuple"},
        {"make_tuple", 2, "make_tuple"},
        {"spawn", 1, "spawn"}, {"spawn_link", 1, "spawn_link"}, {"spawn_monitor", 1, "spawn_monitor"},
        {"spawn", 3, "spawn3"}, {"spawn_link", 3, "spawn_link3"},
        {"link", 1, "link"}, {"unlink", 1, "unlink"}, {"monitor", 2, "monitor"},
        {"demonitor", 1, "demonitor"},
        {"exit", 2, "exit2"}, {"process_flag", 2, "process_flag"},
        {"register", 2, "register"}, {"unregister", 1, "unregister"}, {"whereis", 1, "whereis"},
        {"is_process_alive", 1, "is_process_alive"},
        {"make_ref", 0, "make_ref"}, {"node", 0, "node"}, {"node", 1, "node1"},
        {"atom_to_binary", 1, "atom_to_binary"},
        {"binary_to_atom", 1, "binary_to_atom"},
        {"binary_to_list", 1, "binary_to_list"}, {"list_to_binary", 1, "list_to_binary"},
        {"iolist_to_binary", 1, "list_to_binary"}, {"byte_size", 1, "byte_size"},
        {"bit_size", 1, "bit_size"}, {"iolist_size", 1, "iolist_size"},
        {"binary_part", 3, "binary_part"},
        {"integer_to_binary", 1, "integer_to_binary"}, {"binary_to_integer", 1, "binary_to_integer"},
        {"binary_to_float", 1, "binary_to_float"},
        {"system_time", 0, "system_time"},
        {"monotonic_time", 0, "monotonic_time"},
        {"garbage_collect", 0, "garbage_collect"}, {"processes", 0, "processes"},
        {"halt", 0, "halt"}, {"halt", 1, "halt1"},
        {"put_chars", 1, "put_chars"},
        {"unique_integer", 0, "unique_integer"},
    };
    for (size_t k = 0; k < sizeof bifs / sizeof *bifs; k++)
        if (strcmp(bifs[k].name, name) == 0 && bifs[k].arity == n)
            return bifs[k].rt;
    return NULL;
}

/* ------------------------------------------------------------------ calls */

static bool exported(Module *m, const char *name, int arity) {
    if (m->export_all)
        return true;
    char *fa = xprintf("%s/%d", name, arity);
    const char *s = intern(fa, strlen(fa));
    free(fa);
    for (int k = 0; k < m->exports.n; k++)
        if (m->exports.v[k] == s)
            return true;
    return false;
}

/* The masm function for module:name/arity, or NULL if there is none. */
static const char *resolve(const char *mod, const char *name, int arity) {
    Module *m = get_module(mod);
    if (!m)
        return NULL;
    Function *f = find_function(m, name, arity);
    if (!f)
        return NULL;
    return request(m, f);
}

typedef struct {
    const char *fn;     /* a masm function with the value convention */
    bool inl;           /* an inline BIF, done by inline_bif */
} Target;

/* What a call of erlang:name/n means. */
static Target erlang_bif(const char *name, int n) {
    Target t = {0};
    /* prelude Erlang code first: erlang.erl implements some BIFs */
    const char *f = resolve("erlang", name, n);
    if (f) {
        t.fn = f;
        return t;
    }
    const char *rt = rt_bif(name, n);
    if (rt) {
        t.fn = S("rt$%s", rt);
        return t;
    }
    return t;
}

static bool is_inline_bif(const char *name, int n) {
    if (type_test(name, n) >= 0)
        return true;
    static const struct { const char *name; int n; } l[] = {
        {"hd", 1}, {"tl", 1}, {"element", 2}, {"self", 0}, {"throw", 1}, {"exit", 1},
        {"error", 1}, {"error", 2}, {"apply", 2}, {"apply", 3},
    };
    for (size_t k = 0; k < sizeof l / sizeof *l; k++)
        if (!strcmp(l[k].name, name) && l[k].n == n)
            return true;
    return false;
}

static Val call_fun_value(Fn *g, Val f, Val *args, int n, bool tailpos);

/* A call. In tail position it is a tail call, and returns nothing useful. */
static Val call(Fn *g, Node *e, bool tailpos) {
    Node *callee = e->a;
    int n = e->kids.n;
    const char *target = NULL;
    const char *mod = NULL, *name = NULL;
    if (callee->kind == N_ATOM) {
        name = callee->s;
        Function *f = find_function(g->mod, name, n);
        if (f) {
            target = request(g->mod, f);
        } else {
            /* -import */
            char *fa = xprintf("%s/%d", name, n);
            const char *s = intern(fa, strlen(fa));
            free(fa);
            for (int k = 0; k < g->mod->import_fa.n; k++)
                if (g->mod->import_fa.v[k] == s)
                    mod = g->mod->import_mod.v[k];
            if (!mod)
                mod = intern("erlang", 6);
        }
    } else if (callee->kind == N_REMOTE && callee->a->kind == N_ATOM && callee->b->kind == N_ATOM) {
        mod = callee->a->s;
        name = callee->b->s;
        note_atom(mod);
    }
    if (!target && mod) {
        if (!strcmp(mod, "erlang") || (callee->kind == N_ATOM)) {
            if (!strcmp(mod, "erlang") && is_inline_bif(name, n)) {
                Val v;
                if (inline_bif(g, e, name, n, &v))
                    return v;
            }
            if (!strcmp(mod, "erlang")) {
                Target t = erlang_bif(name, n);
                target = t.fn;
                if (!target && callee->kind == N_ATOM)
                    error_at(e, "function %s/%d undefined", name, n);
            }
        }
        if (!target && !strcmp(mod, "$rt"))
            target = S("rt$%s", mangle(name));
        if (!target) {
            target = resolve(mod, name, n);
            if (target) {
                Module *m = get_module(mod);
                if (!exported(m, name, n) && m != g->mod)
                    target = NULL;
            }
        }
        if (!target) {
            /* unknown: undef at run time */
            Val args[3] = {vatom(mod), vatom(name), vint(n)};
            return rt3(g, "undef", args[0], args[1], args[2]);
        }
    }
    Val *args = xmalloc(((size_t)n + 1) * sizeof *args);
    if (target) {
        int res = tailpos ? -1 : temp(g);
        for (int k = 0; k < n; k++)
            args[k] = ev(g, e->kids.v[k]);
        loc(g, e);
        if (tailpos) {
            tcall_fn(g, target, args, n);
            return vnil();
        }
        g->want = res + 1;
        Val v = call_fn(g, target, args, n);
        g->want = 0;
        g->top = res + 1;
        return v;
    }
    if (callee->kind == N_REMOTE) {
        /* M:F(Args) with M or F computed */
        P.dispatch = true;
        Val m = ev(g, callee->a);
        Val f = ev(g, callee->b);
        for (int k = 0; k < n; k++)
            args[k] = ev(g, e->kids.v[k]);
        Val l = make_list(g, args, n, vnil());
        loc(g, e);
        if (tailpos) {
            Val v3[3] = {m, f, l};
            tcall_fn(g, "rt$apply", v3, 3);
            return vnil();
        }
        return rt3(g, "apply", m, f, l);
    }
    /* a fun */
    Val f = ev(g, callee);
    for (int k = 0; k < n; k++)
        args[k] = ev(g, e->kids.v[k]);
    loc(g, e);
    return call_fun_value(g, f, args, n, tailpos);
}

static Val call_fun_value(Fn *g, Val f, Val *args, int n, bool tailpos) {
    const char *r = rbase(g, f, XR0);
    const char *bad = new_label(g);
    emit(g, "bnisa %s, Fun, %s", r, bad);
    emit(g, "mov %s, %s->Fun.arity", XI0, r);
    emit(g, "bne %s, %d, %s", XI0, n, bad);
    emit(g, "mov.r %s, %s->Fun.clo", XR1, r);
    Val res = {0};
    if (tailpos) {
        emit(g, "tcalli %s (%s)", XR1, arglist(args, n));
    } else {
        int t = temp(g);
        res = slot_val(t);
        emit(g, "calli %s (%s) -> (%s, %s)", XR1, arglist(args, n), res.i, res.r);
    }
    g->in_cold = true;
    label(g, bad);
    Val l = make_list(g, args, n, vnil());
    Val fv = {f.i, f.r};
    emit(g, "call rt$badfun (%s, %s, %s, %s) -> (%s, %s)", fv.i, l.i, fv.r, l.r, XI0, XR0);
    emit(g, "ret 0, null");
    g->in_cold = false;
    return tailpos ? vnil() : res;
}

/* ------------------------------------------------------------------ funs */

static void gen_fun_body(Fn *parent, Node *e, const char *mname, Names *caps, const char *selfname);

/* Free variables of a fun that are bound outside it at this point. */
static void fun_captures(Fn *g, Node *e, Names *caps) {
    Names all = {0};
    for (int k = 0; k < e->clauses.n; k++) {
        Clause *c = e->clauses.v[k];
        Names names = {0};
        clause_vars(c, &names);
        /* nested funs and comprehensions mention variables too */
        for (int j = 0; j < names.n; j++)
            vec_push(all, names.v[j]);
        free(names.v);
    }
    /* deeper: any variable occurring anywhere inside (including nested funs) */
    Names deep = {0};
    VEC(Node *) stack = {0};
    for (int k = 0; k < e->clauses.n; k++) {
        Clause *c = e->clauses.v[k];
        for (int j = 0; j < c->pats.n; j++) vec_push(stack, c->pats.v[j]);
        for (int j = 0; j < c->guards.n; j++)
            for (int q = 0; q < c->guards.v[j].n; q++) vec_push(stack, c->guards.v[j].v[q]);
        for (int j = 0; j < c->body.n; j++) vec_push(stack, c->body.v[j]);
    }
    while (stack.n) {
        Node *x = stack.v[--stack.n];
        if (!x)
            continue;
        if (x->kind == N_VAR) {
            vec_push(deep, x->s);
            continue;
        }
        if (x->a) vec_push(stack, x->a);
        if (x->b) vec_push(stack, x->b);
        if (x->c) vec_push(stack, x->c);
        for (int k = 0; k < x->kids.n; k++) vec_push(stack, x->kids.v[k]);
        for (int pass = 0; pass < 2; pass++) {
            Clauses *cs = pass ? &x->cclauses : &x->clauses;
            for (int k = 0; k < cs->n; k++) {
                Clause *c = cs->v[k];
                for (int j = 0; j < c->pats.n; j++) vec_push(stack, c->pats.v[j]);
                for (int j = 0; j < c->guards.n; j++)
                    for (int q = 0; q < c->guards.v[j].n; q++) vec_push(stack, c->guards.v[j].v[q]);
                for (int j = 0; j < c->body.n; j++) vec_push(stack, c->body.v[j]);
            }
        }
    }
    for (int k = 0; k < deep.n; k++) {
        Var *v = find_var(g, deep.v[k]);
        if (!v || !v->bound || (e->s && deep.v[k] == e->s))
            continue;
        bool have = false;
        for (int j = 0; j < caps->n; j++)
            if (caps->v[j] == deep.v[k])
                have = true;
        if (!have)
            vec_push(*caps, deep.v[k]);
    }
    free(all.v);
    free(deep.v);
    free(stack.v);
}

/* A value made once (per process) and kept in a constant's global: the code
 * between cached_begin and cached_end makes it into tv the first time. */
static const char *cached_begin(Fn *g, Val tv) {
    int k = P.nconst++;
    const char *have = new_label(g);
    emit(g, "mov.r %s, @K%d", tv.r, k);
    emit(g, "bnz.r %s, %s", tv.r, have);
    return S("%d:%s", k, have);
}

static void cached_end(Fn *g, Val tv, const char *have) {
    emit(g, "mov.r @K%d, %s", atoi(have), tv.r);
    label(g, strchr(have, ':') + 1);
    emit(g, is_loc(tv.i) ? "mov.i64 %s, 0" : "mov %s, 0", tv.i);
}

static Val fun_expr(Fn *g, Node *e) {
    int arity = e->clauses.v[0]->pats.n;
    Names caps = {0};
    fun_captures(g, e, &caps);
    int k = (*g->funcount)++;
    const char *mname = S("%s$fun%d", g->mname, k);
    /* captures: each variable's I and R, then the box of a named fun */
    Buf a = {0};
    for (int j = 0; j < caps.n; j++) {
        Var *v = find_var(g, caps.v[j]);
        buf_printf(&a, "%s%s, %s", a.n ? ", " : "", ireg(v->slot), rreg(v->slot));
    }
    int box = -1;
    if (e->s) {
        box = temp(g);
        emit(g, "lnew %s, 1", XR1);
        emit(g, "mov.r %s, null", XR0);
        emit(g, "lpush %s, %s", XR1, XR0);
        emit(g, "mov.r %s, %s", rreg(box), XR1);
        buf_printf(&a, "%s%s", a.n ? ", " : "", rreg(box));
    }
    buf_add(&a, "", 0);
    int t = temp(g);
    Val tv = slot_val(t);
    /* a fun that captures nothing is made once, and kept like a constant */
    const char *have = caps.n == 0 && box < 0 ? cached_begin(g, tv) : NULL;
    emit(g, "closure %s (%s) -> %s", mname, a.s ? a.s : "", XR0);
    const char *r = build_reg(t);
    emit(g, "new %s, Fun", r);
    emit(g, "mov %s->Fun.arity, %d", r, arity);
    emit(g, "mov.r %s->Fun.clo, %s", r, XR0);
    emit(g, "mov.r %s->Fun.name, %s", r, quote(mname, (int)strlen(mname)));
    if (box >= 0) {
        emit(g, "mov.r %s, %s", XR2, rreg(box));
        emit(g, "mov.r %s[0], %s", XR2, r);
    }
    built(g, t, r);
    if (have)
        cached_end(g, tv, have);
    gen_fun_body(g, e, mname, &caps, e->s);
    free(caps.v);
    return tv;
}

/* fun f/N, fun m:f/N */
static Val funref(Fn *g, Node *e) {
    if (e->c || (e->a && e->a->kind != N_ATOM) || e->b->kind != N_ATOM) {
        /* computed: erlang:make_fun(M, F, A) */
        P.dispatch = true;
        Val m = e->a ? ev(g, e->a) : vatom(g->mod->name);
        Val f = ev(g, e->b);
        Val a = e->c ? ev(g, e->c) : vint(e->i);
        return rt3(g, "make_fun", m, f, a);
    }
    const char *mod = e->a ? e->a->s : g->mod->name;
    const char *name = e->b->s;
    int n = (int)e->i;
    const char *target = NULL;
    if (!e->a) {
        Function *f = find_function(g->mod, name, n);
        if (f)
            target = request(g->mod, f);
        else
            mod = intern("erlang", 6);
    }
    if (!target) {
        if (!strcmp(mod, "erlang")) {
            Target t = erlang_bif(name, n);
            target = t.fn;
            if (!target && is_inline_bif(name, n)) {
                /* a wrapper: generated from erlang.erl if present */
                target = resolve("erlang", name, n);
            }
        } else {
            target = resolve(mod, name, n);
        }
    }
    if (!target) {
        P.dispatch = true;
        return rt3(g, "make_fun", vatom(mod), vatom(name), vint(n));
    }
    int t = temp(g);
    const char *have = cached_begin(g, slot_val(t));
    emit(g, "closure %s -> %s", target, XR0);
    const char *r = build_reg(t);
    emit(g, "new %s, Fun", r);
    emit(g, "mov %s->Fun.arity, %d", r, n);
    emit(g, "mov.r %s->Fun.clo, %s", r, XR0);
    const char *fname = S("%s:%s/%d", mod, name, n);
    emit(g, "mov.r %s->Fun.name, %s", r, quote(fname, (int)strlen(fname)));
    built(g, t, r);
    cached_end(g, slot_val(t), have);
    return slot_val(t);
}

/* ------------------------------------------------------------------ comprehensions */

typedef struct {
    Val res;            /* the result list */
    int last;           /* slot whose R is the last cell, or null */
    int kind;           /* 0 list, 1 binary, 2 map */
} Comp;

static void comp_quals(Fn *g, Node *e, int qi, const char *next, Comp *c);

static void comp_emit(Fn *g, Node *e, Comp *c) {
    int mark = g->top;
    if (c->kind == 0) {
        Val v = ev(g, e->a);
        Val cell = make_cons(g, v, vnil());
        const char *first = new_label(g), *done = new_label(g);
        emit(g, "bz.r %s, %s", rreg(c->last), first);
        emit(g, "mov.r %s, %s", XR0, rreg(c->last));
        emit(g, "mov.r %s->Cons.tr, %s", XR0, cell.r);
        emit(g, "jmp %s", done);
        label(g, first);
        emit(g, "mov.r %s, %s", c->res.r, cell.r);
        label(g, done);
        emit(g, "mov.r %s, %s", rreg(c->last), cell.r);
    } else if (c->kind == 1) {
        Val v = ev(g, e->a);
        emit(g, "call rt$bin_append (%s, %s, %s, %s) -> (%s, %s)", c->res.i, v.i, c->res.r, v.r, c->res.i, c->res.r);
    } else {
        Val k = ev(g, e->a->a);
        Val v = ev(g, e->a->b);
        Val args[3] = {k, v, c->res};
        emit(g, "call rt$map_put (%s) -> (%s, %s)", arglist(args, 3), c->res.i, c->res.r);
    }
    g->top = mark;
}

static void comp_quals(Fn *g, Node *e, int qi, const char *next, Comp *c) {
    if (qi == e->kids.n) {
        comp_emit(g, e, c);
        return;
    }
    Node *q = e->kids.v[qi];
    int mark = g->top;
    if (q->kind == N_GEN) {
        Val l = ev(g, q->b);
        int cur = temp(g);
        move(g, cur, l);
        const char *top = new_label(g), *end = new_label(g), *bad = new_label(g);
        label(g, top);
        Bound b = save_bound(g);
        emit(g, "beq.r %s, 0d, %s", rreg(cur), end);
        const char *r = is_reg(rreg(cur)) ? rreg(cur) : XR0;
        if (!is_reg(rreg(cur)))
            emit(g, "mov.r %s, %s", r, rreg(cur));
        emit(g, "bnisa %s, Cons, %s", r, bad);
        int h = temp(g);
        emit(g, "mov %s, %s->Cons.hi", ireg(h), r);
        emit(g, "mov.r %s, %s->Cons.hr", rreg(h), r);
        emit(g, "mov %s, %s->Cons.ti", ireg(cur), r);
        emit(g, "mov.r %s, %s->Cons.tr", rreg(cur), r);
        /* generator variables are new in each round */
        Names pv = {0};
        collect_vars(q->a, &pv);
        for (int k = 0; k < pv.n; k++) {
            Var *v = find_var(g, pv.v[k]);
            if (v)
                v->bound = false;
        }
        free(pv.v);
        match(g, q->a, slot_val(h), top);
        comp_quals(g, e, qi + 1, top, c);
        restore_bound(g, &b);
        free(b.v);
        emit(g, "jmp %s", top);
        g->in_cold = true;
        label(g, bad);
        Val reason = make_tuple(g, (Val[]){vatom(intern("bad_generator", 13)), slot_val(cur)}, 2);
        raise_error(g, reason);
        emit(g, "jmp %s", end);
        g->in_cold = false;
        label(g, end);
    } else if (q->kind == N_BGEN) {
        /* <<P>> <= Bin: match P against the start of the rest, again and again */
        Val bv = ev(g, q->b);
        int cur = temp(g);
        move(g, cur, bv);
        const char *top = new_label(g), *end = new_label(g);
        label(g, top);
        Bound b = save_bound(g);
        if (q->a->kind != N_BIN)
            error_at(q, "a binary generator needs a binary pattern");
        /* match the segments then a /binary rest into cur */
        Node *pat = new_node(N_BIN, q->line, q->file);
        for (int k = 0; k < q->a->kids.n; k++)
            vec_push(pat->kids, q->a->kids.v[k]);
        Node *rest = new_node(N_BINSEG, q->line, q->file);
        rest->a = new_node(N_VAR, q->line, q->file);
        rest->a->s = intern("$rest", 5);
        rest->s = "binary";
        vec_push(pat->kids, rest);
        Var rv = {rest->a->s, cur, false, false};
        vec_push(g->vars, rv);
        Names pv = {0};
        collect_vars(q->a, &pv);
        for (int k = 0; k < pv.n; k++) {
            Var *v = find_var(g, pv.v[k]);
            if (v)
                v->bound = false;
        }
        free(pv.v);
        /* the rest must be matched into a fresh slot, then copied into cur */
        int nr = temp(g);
        g->vars.v[g->vars.n - 1].slot = nr;
        match(g, pat, slot_val(cur), end);
        move(g, cur, slot_val(nr));
        g->vars.n--;
        comp_quals(g, e, qi + 1, top, c);
        restore_bound(g, &b);
        free(b.v);
        emit(g, "jmp %s", top);
        label(g, end);
    } else if (q->kind == N_MGEN) {
        /* K := V <- Map: over maps:to_list(Map) */
        Val m = ev(g, q->b);
        Val l = rt1(g, "map_to_list", m);
        int cur = temp(g);
        move(g, cur, l);
        const char *top = new_label(g), *end = new_label(g);
        label(g, top);
        Bound b = save_bound(g);
        emit(g, "beq.r %s, 0d, %s", rreg(cur), end);
        const char *r = is_reg(rreg(cur)) ? rreg(cur) : XR0;
        if (!is_reg(rreg(cur)))
            emit(g, "mov.r %s, %s", r, rreg(cur));
        int h = temp(g);
        emit(g, "mov %s, %s->Cons.hi", ireg(h), r);
        emit(g, "mov.r %s, %s->Cons.hr", rreg(h), r);
        emit(g, "mov %s, %s->Cons.ti", ireg(cur), r);
        emit(g, "mov.r %s, %s->Cons.tr", rreg(cur), r);
        Node *tp = new_node(N_TUPLE, q->line, q->file);
        vec_push(tp->kids, q->a->a);
        vec_push(tp->kids, q->a->b);
        Names pv = {0};
        collect_vars(tp, &pv);
        for (int k = 0; k < pv.n; k++) {
            Var *v = find_var(g, pv.v[k]);
            if (v)
                v->bound = false;
        }
        free(pv.v);
        match(g, tp, slot_val(h), top);
        comp_quals(g, e, qi + 1, top, c);
        restore_bound(g, &b);
        free(b.v);
        emit(g, "jmp %s", top);
        label(g, end);
    } else {
        /* a filter: a guard-like test, or any expression that must give true */
        cond(g, q, next);
        comp_quals(g, e, qi + 1, next, c);
    }
    g->top = mark;
}

static Val comprehension(Fn *g, Node *e, int kind) {
    /* the comprehension's own variables: those its generators bind */
    Names names = {0};
    for (int k = 0; k < e->kids.n; k++) {
        Node *q = e->kids.v[k];
        if (q->kind == N_GEN || q->kind == N_BGEN || q->kind == N_MGEN)
            collect_vars(q->a, &names);
        else
            collect_vars(q, &names);
    }
    collect_vars(e->a, &names);
    int res = temp(g);
    int last = temp(g);
    int base = g->vars.n;
    for (int k = 0; k < names.n; k++) {
        Var *outer = find_var(g, names.v[k]);
        bool is_gen_var = false;
        for (int j = 0; j < e->kids.n; j++) {
            Node *q = e->kids.v[j];
            if (q->kind == N_GEN || q->kind == N_BGEN || q->kind == N_MGEN) {
                Names pv = {0};
                collect_vars(q->a, &pv);
                for (int i = 0; i < pv.n; i++)
                    if (pv.v[i] == names.v[k])
                        is_gen_var = true;
                free(pv.v);
            }
        }
        if (outer && outer->bound && !is_gen_var)
            continue;
        Var v = {names.v[k], temp(g), false, false};
        vec_push(g->vars, v);
    }
    free(names.v);
    Comp c = {slot_val(res), last, kind};
    if (kind == 0) {
        emit(g, "mov %s, 0", ireg(res));
        emit(g, "mov.r %s, 0d", rreg(res));
        emit(g, "mov.r %s, null", rreg(last));
    } else if (kind == 1) {
        emit(g, "call rt$bin_new () -> (%s, %s)", ireg(res), rreg(res));
    } else {
        emit(g, "call rt$map_new () -> (%s, %s)", ireg(res), rreg(res));
    }
    const char *done = new_label(g);
    if (kind == 2) {
        /* map comprehension: the emit puts into res */
        Comp *cp = &c;
        (void)cp;
    }
    comp_quals(g, e, 0, done, &c);
    label(g, done);
    g->vars.n = base;
    int t = temp(g);
    move(g, t, slot_val(res));
    return slot_val(t);
}

/* ------------------------------------------------------------------ case, if, receive, try */

/* Each clause's body either returns (tail) or leaves its value in res and
 * jumps to done. */
static void clause_bodies(Fn *g, Clauses *cs, Val scrut, int res, bool tailpos, const char *done,
                          const char *nomatch_kind) {
    Bound entry = save_bound(g);
    Bound acc = save_bound(g);
    for (int k = 0; k < cs->n; k++) {
        Clause *c = cs->v[k];
        restore_bound(g, &entry);
        const char *next = new_label(g);
        int mark = g->top;
        if (c->pats.n)
            match(g, c->pats.v[0], scrut, next);
        guards(g, c, next);
        g->top = mark;
        if (tailpos) {
            tail_body(g, &c->body);
        } else {
            Val v = ev_body(g, &c->body);
            move(g, res, v);
            emit(g, "jmp %s", done);
        }
        g->top = mark;
        union_bound(g, &acc);
        label(g, next);
    }
    restore_bound(g, &acc);
    free(entry.v);
    free(acc.v);
    /* no clause matched */
    if (!strcmp(nomatch_kind, "if_clause")) {
        raise_error(g, vatom(intern("if_clause", 9)));
    } else {
        Val reason = make_tuple(g, (Val[]){vatom(intern(nomatch_kind, strlen(nomatch_kind))), scrut}, 2);
        raise_error(g, reason);
    }
    if (tailpos)
        emit(g, "ret 0, null");
    else
        emit(g, "jmp %s", done);
}

static Val case_expr(Fn *g, Node *e, bool tailpos) {
    int res = tailpos ? -1 : temp(g);
    Val s = ev(g, e->a);
    if (is_const(s) && !same(s.r, "null"))
        s = to_temp(g, s);
    const char *done = new_label(g);
    clause_bodies(g, &e->clauses, s, res, tailpos, done, "case_clause");
    label(g, done);
    if (tailpos)
        return vnil();
    g->top = res + 1;
    return slot_val(res);
}

static Val if_expr(Fn *g, Node *e, bool tailpos) {
    int res = tailpos ? -1 : temp(g);
    const char *done = new_label(g);
    clause_bodies(g, &e->clauses, vnil(), res, tailpos, done, "if_clause");
    label(g, done);
    if (tailpos)
        return vnil();
    g->top = res + 1;
    return slot_val(res);
}

static Val receive_expr(Fn *g, Node *e, bool tailpos) {
    int res = tailpos ? -1 : temp(g);
    int mark0 = g->top;
    int deadline = temp(g);
    if (e->a) {
        Val t = ev(g, e->a);
        emit(g, "call rt$recv_deadline (%s, %s) -> %s", t.i, t.r, ireg(deadline));
    } else {
        emit(g, "mov %s, -1", ireg(deadline));
    }
    const char *loop = new_label(g), *after = new_label(g), *done = new_label(g);
    int msg = temp(g);
    int st = temp(g);
    /* rt$recv_next gets back what it returned last: st 2 is a new message,
     * which it saves only if no clause took it. */
    emit(g, is_loc(ireg(st)) ? "mov.i64 %s, -1" : "mov %s, -1", ireg(st));
    emit(g, is_loc(ireg(msg)) ? "mov.i64 %s, 0" : "mov %s, 0", ireg(msg));
    emit(g, "mov.r %s, null", rreg(msg));
    label(g, loop);
    emit(g, "call rt$recv_next (%s, %s, %s, %s) -> (%s, %s, %s)", ireg(deadline), ireg(st), ireg(msg), rreg(msg),
         ireg(st), ireg(msg), rreg(msg));
    emit(g, "bz %s, %s", ireg(st), after);
    Bound entry = save_bound(g);
    Bound acc = save_bound(g);
    Val m = slot_val(msg);
    for (int k = 0; k < e->clauses.n; k++) {
        Clause *c = e->clauses.v[k];
        restore_bound(g, &entry);
        const char *next = new_label(g);
        int mark = g->top;
        match(g, c->pats.v[0], m, next);
        guards(g, c, next);
        g->top = mark;
        const char *taken = new_label(g);
        emit(g, "bne %s, 1, %s", ireg(st), taken);
        emit(g, "call rt$recv_accept ()");
        label(g, taken);
        if (tailpos) {
            tail_body(g, &c->body);
        } else {
            Val v = ev_body(g, &c->body);
            move(g, res, v);
            emit(g, "jmp %s", done);
        }
        g->top = mark;
        union_bound(g, &acc);
        label(g, next);
    }
    emit(g, "jmp %s", loop);
    label(g, after);
    restore_bound(g, &entry);
    if (e->a) {
        if (tailpos) {
            tail_body(g, &e->kids);
        } else {
            Val v = ev_body(g, &e->kids);
            move(g, res, v);
            emit(g, "jmp %s", done);
        }
        union_bound(g, &acc);
    } else {
        emit(g, "jmp %s", loop);
    }
    restore_bound(g, &acc);
    free(entry.v);
    free(acc.v);
    label(g, done);
    g->top = mark0;
    if (tailpos)
        return vnil();
    return slot_val(res);
}

/* try Body [of Clauses] [catch Clauses] [after After] end */
static Val try_expr(Fn *g, Node *e, bool tailpos) {
    if (e->b)
        tailpos = false;
    int res = temp(g);
    const char *done = new_label(g);
    /* with after: everything below is protected by an outer region whose
     * handler runs After and rethrows */
    const char *as = NULL, *ae = NULL, *ah = NULL;
    Buf outer = {0};
    if (e->b) {
        as = new_label(g);
        ae = new_label(g);
        ah = new_label(g);
        emit(g, ".try %s, %s, %s", as, ae, ah);
        label(g, as);
        outer = begin_region(g);
    }
    const char *ls = new_label(g), *le = new_label(g), *lh = new_label(g);
    int mark = g->top;
    if (e->cclauses.n) {
        emit(g, ".try %s, %s, %s", ls, le, lh);
    }
    label(g, ls);
    Buf inner = begin_region(g);
    Val v = ev_body(g, &e->kids);
    int bodyval = res;
    move(g, bodyval, v);
    end_region(g, inner);
    label(g, le);
    g->top = mark;
    if (e->clauses.n) {
        const char *after_of = new_label(g);
        clause_bodies(g, &e->clauses, slot_val(bodyval), res, tailpos, after_of, "try_clause");
        label(g, after_of);
    } else if (tailpos) {
        emit(g, "ret %s, %s", ireg(res), rreg(res));
    }
    emit(g, "jmp %s", done);
    if (e->cclauses.n) {
        label(g, lh);
        int exc = temp(g);
        int cls = temp(g), reason = temp(g), stack = temp(g);
        emit(g, "catch %s", rreg(exc));
        emit(g, "call rt$exc_info (%s) -> (%s, %s, %s, %s)", rreg(exc), ireg(reason), rreg(cls), rreg(reason), rreg(stack));
        emit(g, "mov %s, 0", ireg(cls));
        emit(g, "mov %s, 0", ireg(stack));
        Bound entry = save_bound(g);
        Bound acc = save_bound(g);
        for (int k = 0; k < e->cclauses.n; k++) {
            Clause *c = e->cclauses.v[k];
            restore_bound(g, &entry);
            const char *next = new_label(g);
            int m2 = g->top;
            match(g, c->pats.v[0], slot_val(cls), next);
            match(g, c->pats.v[1], slot_val(reason), next);
            if (c->pats.v[2])
                match(g, c->pats.v[2], slot_val(stack), next);
            guards(g, c, next);
            g->top = m2;
            if (tailpos) {
                tail_body(g, &c->body);
            } else {
                Val bv = ev_body(g, &c->body);
                move(g, res, bv);
                emit(g, "jmp %s", done);
            }
            g->top = m2;
            union_bound(g, &acc);
            label(g, next);
        }
        restore_bound(g, &acc);
        free(entry.v);
        free(acc.v);
        emit(g, "throw %s", rreg(exc));
    }
    label(g, done);
    if (e->b) {
        end_region(g, outer);
        label(g, ae);
        /* the normal way out: run After, keep the value */
        int m3 = g->top;
        ev_body(g, &e->b->kids);
        g->top = m3;
        const char *out = new_label(g);
        emit(g, "jmp %s", out);
        label(g, ah);
        int exc = temp(g);
        emit(g, "catch %s", rreg(exc));
        ev_body(g, &e->b->kids);
        emit(g, "throw %s", rreg(exc));
        label(g, out);
        g->top = m3;
    }
    g->top = res + 1;
    return slot_val(res);
}

static Val catch_expr(Fn *g, Node *e) {
    int res = temp(g);
    const char *ls = new_label(g), *le = new_label(g), *lh = new_label(g), *done = new_label(g);
    emit(g, ".try %s, %s, %s", ls, le, lh);
    label(g, ls);
    int mark = g->top;
    Buf saved = begin_region(g);
    Val v = ev(g, e->a);
    move(g, res, v);
    end_region(g, saved);
    label(g, le);
    g->top = mark;
    emit(g, "jmp %s", done);
    label(g, lh);
    emit(g, "catch %s", XR0);
    emit(g, "call rt$catch_value (%s) -> (%s, %s)", XR0, ireg(res), rreg(res));
    label(g, done);
    return slot_val(res);
}

/* ------------------------------------------------------------------ maps and binaries */

static Val map_expr(Fn *g, Node *e) {
    Val m;
    if (e->a) {
        Val base = ev(g, e->a);
        m = to_temp(g, base);
        const char *r = rbase(g, m, XR0);
        const char *bad = new_label(g), *ok = new_label(g);
        emit(g, "bnisa %s, Map, %s", r, bad);
        g->in_cold = true;
        label(g, bad);
        raise_error(g, make_tuple(g, (Val[]){vatom(intern("badmap", 6)), m}, 2));
        emit(g, "jmp %s", ok);
        g->in_cold = false;
        label(g, ok);
    } else {
        int t = temp(g);
        m = slot_val(t);
        emit(g, "call rt$map_new () -> (%s, %s)", m.i, m.r);
    }
    for (int k = 0; k < e->kids.n; k++) {
        Node *a = e->kids.v[k];
        int mark = g->top;
        Val key = ev(g, a->a);
        Val val = ev(g, a->b);
        Val args[3] = {key, val, m};
        emit(g, "call rt$%s (%s) -> (%s, %s)", a->kind == N_EXACT ? "map_update" : "map_put",
             arglist(args, 3), m.i, m.r);
        g->top = mark;
    }
    return m;
}

static Val bin_expr(Fn *g, Node *e) {
    int t = temp(g);
    Val b = slot_val(t);
    emit(g, "call rt$bin_new () -> (%s, %s)", b.i, b.r);
    for (int k = 0; k < e->kids.n; k++) {
        Node *seg = e->kids.v[k];
        int mark = g->top;
        const char *spec = seg->s ? seg->s : "";
        int type = 0, flags = 0;
        char *sp = xstrdup(spec);
        for (char *tok = strtok(sp, "-"); tok; tok = strtok(NULL, "-")) {
            if (strcmp(tok, "integer") == 0) type = 0;
            else if (strcmp(tok, "binary") == 0 || strcmp(tok, "bytes") == 0) type = 1;
            else if (strcmp(tok, "bits") == 0 || strcmp(tok, "bitstring") == 0) type = 1;
            else if (strcmp(tok, "float") == 0) type = 2;
            else if (strcmp(tok, "utf8") == 0) type = 3;
            else if (strcmp(tok, "signed") == 0) flags |= 1;
            else if (strcmp(tok, "unsigned") == 0) flags &= ~1;
            else if (strcmp(tok, "little") == 0 || strcmp(tok, "native") == 0) flags |= 2;
            else if (strcmp(tok, "big") == 0) flags &= ~2;
            else if (strncmp(tok, "unit:", 5) == 0) ;
            else error_at(seg, "unsupported binary type %s", tok);
        }
        free(sp);
        if (seg->a->kind == N_STRING && !seg->b && type == 0) {
            uint32_t *cs = xmalloc(((size_t)seg->a->len + 1) * sizeof *cs);
            int n = decode(seg->a->s, seg->a->len, cs);
            Buf bytes = {0};
            for (int q = 0; q < n; q++) {
                char c = (char)(cs[q] & 255);
                buf_add(&bytes, &c, 1);
            }
            free(cs);
            emit(g, "call rt$bin_put_bytes (%s, %s) -> (%s, %s)", b.r, quote(bytes.s ? bytes.s : "", (int)bytes.n), b.i, b.r);
            free(bytes.s);
            continue;
        }
        if (seg->a->kind == N_STRING && type == 3) {
            emit(g, "call rt$bin_put_bytes (%s, %s) -> (%s, %s)", b.r, quote(seg->a->s, seg->a->len), b.i, b.r);
            continue;
        }
        Val v = ev(g, seg->a);
        Val size;
        if (seg->b)
            size = ev(g, seg->b);
        else
            size = type == 0 ? vint(8) : type == 2 ? vint(64) : vint(-1);
        emit(g, "call rt$bin_put (%s, %s, %d, %s, %s, %s) -> (%s, %s)", v.i, size.i, type * 4 + flags,
             b.r, v.r, size.r, b.i, b.r);
        g->top = mark;
    }
    return b;
}

/* ------------------------------------------------------------------ expressions */

static Val ev(Fn *g, Node *e) {
    switch (e->kind) {
    case N_INT: case N_BIGINT: case N_FLOAT: case N_ATOM: case N_NIL:
        return build_const(g, e);
    case N_STRING:
        if (e->len == 0)
            return vnil();
        return const_term(g, e);
    case N_VAR: {
        if (strcmp(e->s, "_") == 0)
            error_at(e, "variable '_' is unbound");
        Var *v = find_var(g, e->s);
        if (!v || !v->bound)
            error_at(e, "variable '%s' is unbound", e->s);
        return slot_val(v->slot);
    }
    case N_CONS: {
        if (constant(e))
            return const_term(g, e);
        /* evaluate the elements left to right, then build from the end */
        Nodes elems = {0};
        Node *q = e;
        while (q->kind == N_CONS) {
            vec_push(elems, q->a);
            q = q->b;
        }
        Val *vs = xmalloc(((size_t)elems.n + 1) * sizeof *vs);
        for (int k = 0; k < elems.n; k++)
            vs[k] = ev(g, elems.v[k]);
        Val t = q->kind == N_NIL ? vnil() : ev(g, q);
        Val l = make_list(g, vs, elems.n, t);
        free(vs);
        free(elems.v);
        return l;
    }
    case N_TUPLE: {
        if (constant(e))
            return const_term(g, e);
        int res = temp(g);
        Val *vs = xmalloc(((size_t)e->kids.n + 1) * sizeof *vs);
        for (int k = 0; k < e->kids.n; k++)
            vs[k] = ev(g, e->kids.v[k]);
        g->want = res + 1;
        Val t = make_tuple(g, vs, e->kids.n);
        g->want = 0;
        g->top = res + 1;
        free(vs);
        return t;
    }
    case N_MATCH: {
        Val v = ev(g, e->b);
        const char *fail = new_label(g), *ok = new_label(g);
        if (e->a->kind == N_VAR && binds_only(g, e->a)) {
            bool flt = likely_float(g, e->b);
            match(g, e->a, v, fail);
            if (strcmp(e->a->s, "_") != 0) {
                Var *var = find_var(g, e->a->s);
                var->flt = flt;
                v = slot_val(var->slot);
            }
            label(g, fail);
            return v;
        }
        if (is_const(v) && !same(v.r, "null"))
            v = to_temp(g, v);
        match(g, e->a, v, fail);
        g->in_cold = true;
        label(g, fail);
        raise_error(g, make_tuple(g, (Val[]){vatom(intern("badmatch", 8)), v}, 2));
        emit(g, "jmp %s", ok);
        g->in_cold = false;
        label(g, ok);
        return v;
    }
    case N_OP: {
        const char *op = e->s;
        if (!strcmp(op, "andalso") || !strcmp(op, "orelse")) {
            /* the second operand is returned as it is (it may be any value) */
            int t = temp(g);
            const char *done = new_label(g), *other = new_label(g);
            Val a = ev(g, e->a);
            bool and = !strcmp(op, "andalso");
            note_atom(intern("true", 4));
            note_atom(intern("false", 5));
            emit(g, "mov %s, 0", ireg(t));
            if (and) {
                emit(g, "beq.r %s, \"true\", %s", a.r, other);
                emit(g, "bne.r %s, \"false\", %s", a.r, S("%s_bad", other));
                emit(g, "mov.r %s, \"false\"", rreg(t));
            } else {
                emit(g, "beq.r %s, \"false\", %s", a.r, other);
                emit(g, "bne.r %s, \"true\", %s", a.r, S("%s_bad", other));
                emit(g, "mov.r %s, \"true\"", rreg(t));
            }
            emit(g, "jmp %s", done);
            g->in_cold = true;
            label(g, S("%s_bad", other));
            raise_error(g, make_tuple(g, (Val[]){vatom(intern("badarg", 6)), a}, 2));
            emit(g, "jmp %s", done);
            g->in_cold = false;
            label(g, other);
            int mark = g->top;
            Val b = ev(g, e->b);
            move(g, t, b);
            g->top = mark;
            label(g, done);
            return slot_val(t);
        }
        if (!strcmp(op, "==") || !strcmp(op, "/=") || !strcmp(op, "=:=") || !strcmp(op, "=/=")
            || !strcmp(op, "<") || !strcmp(op, "=<") || !strcmp(op, ">") || !strcmp(op, ">="))
            return bool_val(g, e);
        if (!strcmp(op, "!")) {
            Val a = ev(g, e->a);
            Val b = ev(g, e->b);
            loc(g, e);
            return rt2(g, "send", a, b);
        }
        if (!strcmp(op, "++")) {
            Val a = ev(g, e->a);
            Val b = ev(g, e->b);
            return rt2(g, "append", a, b);
        }
        if (!strcmp(op, "--")) {
            Val a = ev(g, e->a);
            Val b = ev(g, e->b);
            const char *f = resolve(intern("erlang", 6), intern("--", 2), 2);
            Val v[2] = {a, b};
            return call_fn(g, f, v, 2);
        }
        if (!strcmp(op, "and") || !strcmp(op, "or") || !strcmp(op, "xor")) {
            Val a = ev(g, e->a);
            Val b = ev(g, e->b);
            return rt2(g, op, a, b);
        }
        bool flt = likely_float(g, e);
        /* the result's slot is taken first, so the operands' temporaries
         * are free again after it */
        int res = temp(g);
        Val a = ev(g, e->a);
        Val b = ev(g, e->b);
        loc(g, e);
        g->want = res + 1;
        Val v = arith_f(g, op, a, b, flt);
        g->want = 0;
        g->top = res + 1;
        return v;
    }
    case N_UNOP: {
        Val a = ev(g, e->a);
        if (!strcmp(e->s, "-") && likely_float(g, e->a))
            return arith_f(g, "-", vfloat(0.0), a, true);
        if (!strcmp(e->s, "-"))
            return arith(g, "-", vint(0), a);
        if (!strcmp(e->s, "+"))
            return rt1(g, "plus", a);
        if (!strcmp(e->s, "bnot"))
            return rt1(g, "bnot", a);
        return rt1(g, "not", a);
    }
    case N_CALL:
        return call(g, e, false);
    case N_REMOTE:
        error_at(e, "illegal expression");
    case N_CASE:
        return case_expr(g, e, false);
    case N_IF:
        return if_expr(g, e, false);
    case N_RECEIVE:
        return receive_expr(g, e, false);
    case N_TRY:
        return try_expr(g, e, false);
    case N_CATCH:
        return catch_expr(g, e);
    case N_BLOCK:
        return ev_body(g, &e->kids);
    case N_FUN:
        return fun_expr(g, e);
    case N_FUNREF:
        return funref(g, e);
    case N_LC:
        return comprehension(g, e, 0);
    case N_BC:
        return comprehension(g, e, 1);
    case N_MC:
        return comprehension(g, e, 2);
    case N_MAP:
        return map_expr(g, e);
    case N_BIN:
        return bin_expr(g, e);
    case N_RECORD: {
        if (!e->a)
            return ev(g, record_tuple(g, e, false));
        /* R#rec{f = V}: setelement for each field, after checking R */
        Record *r = find_record(g, e, e->s);
        Val base = ev(g, e->a);
        Val cur = to_temp(g, base);
        const char *rb = rbase(g, cur, XR0);
        const char *bad = new_label(g), *ok = new_label(g);
        note_atom(r->name);
        test_tuple(g, rb, r->fields.n + 1, bad);
        emit(g, "bne.r %s[0], %s, %s", rb, quote(r->name, (int)strlen(r->name)), bad);
        g->in_cold = true;
        label(g, bad);
        raise_error(g, make_tuple(g, (Val[]){vatom(intern("badrecord", 9)), cur}, 2));
        emit(g, "jmp %s", ok);
        g->in_cold = false;
        label(g, ok);
        for (int k = 0; k < e->kids.n; k++) {
            int idx = field_index(r, e, e->kids.v[k]->a->s) + 2;
            int mark = g->top;
            Val v = ev(g, e->kids.v[k]->b);
            Val nv = rt3(g, "setelement", vint(idx), cur, v);
            g->top = mark;
            emit(g, "mov %s, %s", cur.i, nv.i);
            emit(g, "mov.r %s, %s", cur.r, nv.r);
        }
        return cur;
    }
    case N_RECFIELD: {
        Record *r = find_record(g, e, e->s);
        int idx = field_index(r, e, e->b->s);
        Val v = ev(g, e->a);
        const char *rb = rbase(g, v, XR0);
        int t = temp(g);
        const char *bad = new_label(g), *ok = new_label(g);
        note_atom(r->name);
        test_tuple(g, rb, r->fields.n + 1, bad);
        emit(g, "bne.r %s[0], %s, %s", rb, quote(r->name, (int)strlen(r->name)), bad);
        emit(g, "mov %s, %s[%d]", ireg(t), rb, 2 * idx + 3);
        emit(g, "mov.r %s, %s[%d]", rreg(t), rb, 2 * idx + 2);
        g->in_cold = true;
        label(g, bad);
        raise_error(g, make_tuple(g, (Val[]){vatom(intern("badrecord", 9)), v}, 2));
        emit(g, "jmp %s", ok);
        g->in_cold = false;
        label(g, ok);
        return slot_val(t);
    }
    case N_RECINDEX: {
        Record *r = find_record(g, e, e->s);
        return vint(field_index(r, e, e->b->s) + 2);
    }
    default:
        error_at(e, "unsupported expression");
    }
}

static Val ev_body(Fn *g, Nodes *body) {
    Val v = vnil();
    for (int k = 0; k < body->n; k++) {
        int mark = g->top;
        loc(g, body->v[k]);
        v = ev(g, body->v[k]);
        if (k < body->n - 1)
            g->top = mark;
    }
    return v;
}

/* Code that returns the value of e. */
static void tail(Fn *g, Node *e) {
    loc(g, e);
    switch (e->kind) {
    case N_CALL: {
        /* a local or remote call in tail position is a tail call; BIFs
         * that are done inline are not calls */
        Node *c = e->a;
        bool inl = false;
        if (c->kind == N_ATOM && !find_function(g->mod, c->s, e->kids.n) && is_inline_bif(c->s, e->kids.n))
            inl = true;
        if (c->kind == N_REMOTE && c->a->kind == N_ATOM && c->b->kind == N_ATOM
            && !strcmp(c->a->s, "erlang") && is_inline_bif(c->b->s, e->kids.n))
            inl = true;
        if (!inl) {
            int mark = g->top;
            call(g, e, true);
            g->top = mark;
            return;
        }
        break;
    }
    case N_CASE:
        case_expr(g, e, true);
        return;
    case N_IF:
        if_expr(g, e, true);
        return;
    case N_RECEIVE:
        receive_expr(g, e, true);
        return;
    case N_BLOCK:
        tail_body(g, &e->kids);
        return;
    default:
        break;
    }
    int mark = g->top;
    Val v = ev(g, e);
    emit(g, "ret %s, %s", v.i, v.r);
    g->top = mark;
}

static void tail_body(Fn *g, Nodes *body) {
    for (int k = 0; k < body->n - 1; k++) {
        int mark = g->top;
        loc(g, body->v[k]);
        ev(g, body->v[k]);
        g->top = mark;
    }
    tail(g, body->v[body->n - 1]);
}

/* ------------------------------------------------------------------ functions */

/* Write a finished function: its header, the stack it needs, the code
 * with the scratch registers numbered, and its cold code. */
static void finish(Fn *g, const char *header) {
    int sx = g->maxslot + 1;
    int locwords = 0;
    if (sx > NR) {
        locwords = 2 * (g->maxslot - NR + 1);
        sx = NR;
    }
    if (sx + 3 > 32)
        sx = 29;
    buf_puts(&P.out, header);
    if (locwords)
        buf_printf(&P.out, "    enter %d\n", locwords);
    for (int pass = 0; pass < 2; pass++) {
        Buf *b = pass ? &g->cold : &g->code;
        for (size_t q = 0; q < b->n; q++) {
            if (b->s[q] == '\001') {
                buf_printf(&P.out, "%c%d", b->s[q + 1], sx + (b->s[q + 2] - '0'));
                q += 2;
            } else {
                buf_add(&P.out, &b->s[q], 1);
            }
        }
    }
    /* the handlers of guards that may raise */
    for (int k = 0; k < g->handlers.n; k++) {
        const char *h = g->handlers.v[k];
        const char *colon = strchr(h, ':');
        buf_printf(&P.out, "%.*s:\n    catch R%d\n    jmp %s\n", (int)(colon - h), h, sx, colon + 1);
    }
    buf_puts(&P.out, ".endf\n\n");
}

/* The clauses of a function or fun, matched against the arguments in
 * slots 0..n-1. */
/* The arity of the clause's first pattern if it is a tuple, else -1. */
static int first_tuple(Clause *c) {
    return c->pats.n && c->pats.v[0]->kind == N_TUPLE ? c->pats.v[0]->kids.n : -1;
}

static void gen_clauses(Fn *g, Clauses *cs, int nvars_base) {
    /* Where clauses in a row begin with tuples of one size, a clause that
     * fails after its tuple test goes to the next one's elements, past its
     * tuple test (shared_in: the label there). */
    const char *shared_in = NULL;
    for (int k = 0; k < cs->n; k++) {
        Clause *c = cs->v[k];
        setup_clause(g, c, nvars_base);
        const char *next = new_label(g), *fail = next;
        int tn = first_tuple(c);
        const char *shared_out = tn >= 0 && k + 1 < cs->n && first_tuple(cs->v[k + 1]) == tn ? new_label(g) : NULL;
        g->line = 0;
        if (c->body.n)
            loc(g, c->body.v[0]);
        for (int j = 0; j < c->pats.n; j++) {
            Node *p = c->pats.v[j];
            if (j == 0 && (shared_in || shared_out)) {
                const char *r = base_reg(g, slot_val(0));
                test_tuple(g, r, tn, next);
                if (shared_in)
                    label(g, shared_in);
                if (shared_out)
                    fail = shared_out;
                match_elements(g, p, r, fail);
                continue;
            }
            Var *v = p->kind == N_VAR ? find_var(g, p->s) : NULL;
            if (v && v->slot == j && !v->bound) {
                v->bound = true;
                continue;
            }
            match(g, p, slot_val(j), fail);
        }
        guards(g, c, fail);
        g->top = g->nparams + g->ncaps + (g->vars.n - nvars_base);
        if (g->top < g->nparams + g->ncaps)
            g->top = g->nparams + g->ncaps;
        /* temporaries go above every variable */
        int maxv = g->nparams + g->ncaps - 1;
        for (int q = nvars_base; q < g->vars.n; q++)
            if (g->vars.v[q].slot > maxv)
                maxv = g->vars.v[q].slot;
        g->top = maxv + 1;
        tail_body(g, &c->body);
        label(g, next);
        shared_in = shared_out;
    }
    /* no clause matched: function_clause */
    Val *args = xmalloc(((size_t)g->nparams + 1) * sizeof *args);
    for (int j = 0; j < g->nparams; j++)
        args[j] = slot_val(j);
    g->top = g->nparams + g->ncaps;
    Val l = make_list(g, args, g->nparams, vnil());
    Val name = vatom(intern(g->ename, strlen(g->ename)));
    emit(g, "call rt$function_clause (%s, %s, %s, %s) -> (%s, %s)", name.i, l.i, name.r, l.r, XI0, XR0);
    emit(g, "ret 0, null");
}

static void gen_function(Module *m, Function *f, const char *mname) {
    Fn g = {0};
    int funcount = 0;
    g.mod = m;
    g.ename = S("%s:%s/%d", m->name, f->name, f->arity);
    g.mname = mname;
    g.nparams = f->arity;
    g.funcount = &funcount;
    g.maxslot = f->arity - 1;
    gen_clauses(&g, &f->clauses, 0);
    finish(&g, S(".func %s(%s) -> (i64, ref)\n", mname, sig(f->arity)));
}

/* A fun's body: arguments, then its captures (loaded from UP), then its
 * own variables. A named fun finds itself in the box it captures last. */
static void gen_fun_body(Fn *parent, Node *e, const char *mname, Names *caps, const char *selfname) {
    Fn g = {0};
    g.mod = parent->mod;
    g.ename = parent->ename;
    g.mname = mname;
    int arity = e->clauses.v[0]->pats.n;
    g.nparams = arity;
    g.funcount = parent->funcount;
    g.ncaps = caps->n + (selfname ? 1 : 0);
    g.maxslot = arity + g.ncaps - 1;
    /* the captured variables keep their names, bound, in slots after the arguments */
    for (int k = 0; k < caps->n; k++) {
        Var v = {caps->v[k], arity + k, true, false};
        vec_push(g.vars, v);
        emit(&g, "mov %s, UP[%d]", ireg(arity + k), 2 * k);
        emit(&g, "mov.r %s, UP[%d]", rreg(arity + k), 2 * k + 1);
    }
    if (selfname) {
        int s = arity + caps->n;
        Var v = {selfname, s, true, false};
        vec_push(g.vars, v);
        emit(&g, "mov.r %s, UP[%d]", XR0, 2 * caps->n);
        emit(&g, "mov.r %s, %s[0]", rreg(s), XR0);
        emit(&g, "mov %s, 0", ireg(s));
    }
    int base = g.vars.n;
    /* fun clauses' variables shadow nothing they capture: setup_clause
     * gives the clause's own variables new slots; captured names stay bound */
    for (int k = 0; k < e->clauses.n; k++) {
        Clause *c = e->clauses.v[k];
        Clauses one = {0};
        vec_push(one, c);
        /* set up with the captures still visible */
        g.vars.n = base;
        Names names = {0};
        clause_vars(c, &names);
        int next = arity + g.ncaps;
        /* head variables shadow captures */
        Names headv = {0};
        for (int j = 0; j < c->pats.n; j++)
            collect_vars(c->pats.v[j], &headv);
        Names own = {0};
        for (int q = 0; q < names.n; q++) {
            bool captured = false, inhead = false;
            for (int j = 0; j < base; j++)
                if (g.vars.v[j].name == names.v[q])
                    captured = true;
            for (int j = 0; j < headv.n; j++)
                if (headv.v[j] == names.v[q])
                    inhead = true;
            if (captured && !inhead)
                continue;
            vec_push(own, names.v[q]);
        }
        int *slots = xmalloc(((size_t)own.n + 1) * sizeof *slots);
        for (int q = 0; q < own.n; q++)
            slots[q] = head_slot(c, own.v[q]);
        next = assign_slots(c, &own, slots, next);
        for (int q = 0; q < own.n; q++) {
            Var v = {own.v[q], slots[q], false, false};
            vec_push(g.vars, v);
        }
        free(slots);
        free(own.v);
        free(names.v);
        free(headv.v);
        g.top = next;
        if (next - 1 > g.maxslot)
            g.maxslot = next - 1;
        const char *nextl = new_label(&g);
        g.line = 0;
        if (c->body.n)
            loc(&g, c->body.v[0]);
        for (int j = 0; j < c->pats.n; j++) {
            Node *p = c->pats.v[j];
            Var *v = p->kind == N_VAR ? find_var(&g, p->s) : NULL;
            if (v && v->slot == j && !v->bound) {
                v->bound = true;
                continue;
            }
            match(&g, p, slot_val(j), nextl);
        }
        guards(&g, c, nextl);
        g.top = next;
        tail_body(&g, &c->body);
        label(&g, nextl);
        free(one.v);
    }
    g.vars.n = base;
    Val *args = xmalloc(((size_t)arity + 1) * sizeof *args);
    for (int j = 0; j < arity; j++)
        args[j] = slot_val(j);
    g.top = arity + g.ncaps;
    Val l = make_list(&g, args, arity, vnil());
    Val fn = vatom(intern("fun", 3));
    emit(&g, "call rt$function_clause (%s, %s, %s, %s) -> (%s, %s)", fn.i, l.i, fn.r, l.r, XI0, XR0);
    emit(&g, "ret 0, null");
    finish(&g, S(".func %s(%s) -> (i64, ref)\n", mname, sig(arity)));
}

/* ------------------------------------------------------------------ modules */

static Module *load_module(const char *path, const char *src, size_t len) {
    Module *m = parse_module(path, src, len);
    vec_push(P.mods, m);
    return m;
}

static char *slurp(const char *path, size_t *len) {
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

static Module *get_module(const char *name) {
    for (int k = 0; k < P.mods.n; k++)
        if (P.mods.v[k]->name == name)
            return P.mods.v[k];
    /* a source file next to the program */
    if (P.srcdir) {
        char *path = xprintf("%s/%s.erl", P.srcdir, name);
        size_t len;
        char *src = slurp(path, &len);
        if (src) {
            Module *m = load_module(path, src, len);
            if (m->name != name)
                fatal(path, 1, "module name '%s' does not match file name '%s'", m->name, name);
            return m;
        }
        free(path);
    }
    /* the library */
    for (int k = 0; prelude_names[k]; k++)
        if (!strcmp(prelude_names[k], name)) {
            char *path = xprintf("%s.erl", name);
            return load_module(path, prelude_files[k], strlen(prelude_files[k]));
        }
    return NULL;
}

static const char *request(Module *m, Function *f) {
    const char *name = fn_name(m->name, f->name, f->arity);
    for (int k = 0; k < P.emitted.n; k++)
        if (!strcmp(P.emitted.v[k], name))
            return P.emitted.v[k];
    vec_push(P.emitted, name);
    FnReq r = {name, m, f};
    vec_push(P.queue, r);
    return name;
}

/* apply/3 and friends: a table from module, name and arity to a closure
 * taking the arguments as a list, built on first use in each process. */
static void gen_dispatch(void) {
    Buf b = {0};
    buf_puts(&b, ".func rt$dispatch_init -> ref\n    mnew R0\n");
    int nadapt = 0;
    Buf adapters = {0};
    for (int k = 0; k < P.mods.n; k++) {
        Module *m = P.mods.v[k];
        bool user = true;
        for (int j = 0; prelude_names[j]; j++)
            if (!strcmp(prelude_names[j], m->name))
                user = false;
        if (!user) {
            bool named = false;
            for (int j = 0; j < P.atoms.n; j++)
                if (P.atoms.v[j] == m->name)
                    named = true;
            if (!named)
                continue;
        }
        for (int j = 0; j < m->funcs.n; j++) {
            Function *f = m->funcs.v[j];
            if (!exported(m, f->name, f->arity))
                continue;
            const char *target = request(m, f);
            const char *ad = S("A$%d", nadapt++);
            const char *key = S("%s:%s/%d", m->name, f->name, f->arity);
            buf_printf(&b, "    closure %s -> R1\n    mset R0, %s, R1\n", ad, quote(key, (int)strlen(key)));
            /* the adapter: unpack the list into arguments */
            buf_printf(&adapters, ".func %s(ref args) -> (i64, ref)\n", ad);
            int n = f->arity;
            for (int q = 0; q < n; q++) {
                buf_printf(&adapters, "    mov I%d, R0->Cons.hi\n    mov.r R%d, R0->Cons.hr\n    mov.r R0, R0->Cons.tr\n",
                           q, q + 1, q + 1);
            }
            /* move R1..Rn down to R0..R(n-1) */
            for (int q = 0; q < n; q++)
                buf_printf(&adapters, "    mov.r R%d, R%d\n", q, q + 1);
            Buf a = {0};
            for (int q = 0; q < n; q++)
                buf_printf(&a, "%sI%d", a.n ? ", " : "", q);
            for (int q = 0; q < n; q++)
                buf_printf(&a, "%sR%d", a.n ? ", " : "", q);
            buf_add(&a, "", 0);
            buf_printf(&adapters, "    tcall %s (%s)\n.endf\n", target, a.s ? a.s : "");
            free(a.s);
        }
    }
    buf_puts(&b, "    ret R0\n.endf\n");
    buf_add(&b, "", 0);
    buf_add(&adapters, "", 0);
    buf_puts(&P.out, b.s);
    buf_puts(&P.out, adapters.s ? adapters.s : "");
}

static void gen_atoms(void) {
    buf_puts(&P.out, ".func rt$atoms_init -> ref\n    mnew R0\n");
    for (int k = 0; k < P.atoms.n; k++) {
        const char *q = quote(P.atoms.v[k], (int)strlen(P.atoms.v[k]));
        buf_printf(&P.out, "    mset R0, %s, %s\n", q, q);
    }
    buf_puts(&P.out, "    ret R0\n.endf\n");
}

void generate(Module **mods, int nmods, FILE *out, const char *source_name) {
    (void)source_name;
    for (int k = 0; k < nmods; k++)
        vec_push(P.mods, mods[k]);
    const char *slash = strrchr(mods[0]->file, '/');
    P.srcdir = slash ? xstrndup(mods[0]->file, (size_t)(slash - mods[0]->file)) : ".";
    Module *m = mods[0];
    Function *mainf = find_function(m, intern("main", 4), 0);
    int main_arity = 0;
    if (!mainf) {
        mainf = find_function(m, intern("main", 4), 1);
        main_arity = 1;
    }
    if (!mainf)
        fatal(m->file, 1, "module %s has no main/0 or main/1", m->name);
    const char *entry = request(m, mainf);
    /* functions the runtime calls */
    const char *rt_roots[][3] = {
        {"erts", "main_error", "2"},
        {"erts_map", "insert", "3"},
        {"erts_map", "from_kv", "1"},
        {"erts", "ser", "1"},
        {"erts", "deser", "1"},
        {"erts", "report", "2"},
    };
    for (size_t k = 0; k < sizeof rt_roots / sizeof *rt_roots; k++) {
        Module *rm = get_module(intern(rt_roots[k][0], strlen(rt_roots[k][0])));
        if (!rm)
            continue;
        Function *f = find_function(rm, intern(rt_roots[k][1], strlen(rt_roots[k][1])), atoi(rt_roots[k][2]));
        if (f)
            request(rm, f);
    }
    for (int k = 0; k < P.queue.n; k++) {
        FnReq r = P.queue.v[k];
        gen_function(r.mod, r.fn, r.name);
        if (k == P.queue.n - 1 && P.dispatch) {
            static bool done = false;
            if (!done) {
                done = true;
                gen_dispatch();
            }
        }
    }
    if (!P.dispatch)
        buf_puts(&P.out, ".func rt$dispatch_init -> ref\n    mnew R0\n    ret R0\n.endf\n");
    note_atom(intern("true", 4));
    note_atom(intern("false", 5));
    gen_atoms();

    fprintf(out, "; generated by mlws-vm-erlc %s from %s\n", VERSION, mods[0]->file);
    fprintf(out, ".requires mlwsvm, \">=0.6.0\"\n\n");
    fputs(runtime_masm, out);
    fprintf(out, "\n; ---------------------------------------------------------------- constants\n");
    for (int k = 0; k < P.nconst; k++)
        fprintf(out, "K%d: .zero 8\n", k);
    if (P.consts.n)
        fwrite(P.consts.s, 1, P.consts.n, out);
    fprintf(out, "\n; ---------------------------------------------------------------- program\n");
    fprintf(out, ".func main -> i64\n");
    fprintf(out, "    call rt$main_start ()\n");
    fprintf(out, ".try 1f\n");
    if (main_arity == 0) {
        fprintf(out, "    call %s () -> (I0, R0)\n", entry);
    } else {
        fprintf(out, "    call rt$args () -> (I1, R1)\n");
        fprintf(out, "    call %s (I1, R1) -> (I0, R0)\n", entry);
    }
    fprintf(out, ".endtry\n    call rt$main_end ()\n    ret 0\n");
    fprintf(out, "1:  catch R0\n    call rt$main_uncaught (R0) -> I0\n    ret I0\n.endf\n\n");
    fwrite(P.out.s, 1, P.out.n, out);
}
