/*
 * Code generation: the resolved AST to MLWSVM assembly.
 *
 * Values. A Lox value is a pair of registers (or stack words): an F register
 * holding a number and an R register holding a reference. The R half says
 * what the value is:
 *
 *   null                 a number (the F half)
 *   "\"nil" "\"true" "\"false"   nil and the booleans: three constant strings
 *                        that no Lox string can equal, since they contain '"'
 *   a string             a Lox string
 *   Fn, Native, Class, Inst, Bound   structs declared in the runtime
 *
 * so the type test on arithmetic is one null test of a register.
 *
 * Slots. Each function numbers its locals and temporaries as slots. Slot k is
 * (F<k>, R<k>) for k < NREG, and a pair of stack words LOC[...] above that.
 * Every instruction accepts memory operands, so spilled slots need no special
 * code. I registers, F30-F31 and R24-R31 are scratch.
 *
 * Calls. A Lox function compiles to `.func L<id>$name(f64, ref, ...) ->
 * (f64, ref)`, with the receiver as an extra first pair for methods. A
 * function value is an Fn struct holding a closure; a call site checks for an
 * Fn of the right arity and calls it with `calli`, and leaves everything else
 * (classes, bound methods, natives, errors) to rt$call<n>. A call of a global
 * function that is declared once and never assigned is a direct `call`.
 *
 * Closures. A captured variable that is never assigned after its declaration
 * is copied into each closure (two capture words). One that is assigned lives
 * in a heap Box, and the closures capture the box (one word).
 *
 * Slow paths and error reports are emitted out of line, after the function's
 * last instruction ("cold" code), so the fast path falls straight through.
 */
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "lox.h"

#define NREG 24          /* value slots held in F0-F23 / R0-R23 */
#define MAX_REG_ARGS 12  /* calls with more arguments pass them in a list */
#define MAX_DIRECT_UP 28 /* closures with more capture words keep them in a list */

#define NIL "\"\\\"nil\""
#define TRUE "\"\\\"true\""
#define FALSE "\"\\\"false\""

/* ---------------------------------------------------------------- output */

/* Lines of assembly, each with the Lox line it was generated for (0: none),
 * from which the runtime's line map is built. */
typedef struct {
    char *text;
    size_t len, cap;
    int *lines;
    int nlines, linecap;
} Buf;

static void buf_add(Buf *b, int loxline, const char *s) {
    size_t n = strlen(s);
    if (b->len + n + 2 > b->cap) {
        while (b->len + n + 2 > b->cap) b->cap = b->cap ? b->cap * 2 : 4096;
        b->text = xrealloc(b->text, b->cap);
    }
    memcpy(b->text + b->len, s, n);
    b->len += n;
    b->text[b->len++] = '\n';
    if (b->nlines == b->linecap) {
        b->linecap = b->linecap ? b->linecap * 2 : 256;
        b->lines = xrealloc(b->lines, (size_t)b->linecap * sizeof *b->lines);
    }
    b->lines[b->nlines++] = loxline;
}

static void buf_addf(Buf *b, int loxline, const char *fmt, ...) {
    char tmp[8192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    buf_add(b, loxline, tmp);
}

static void buf_cat(Buf *dst, Buf *src) {
    size_t start = 0;
    for (int i = 0; i < src->nlines; i++) {
        char *nl = memchr(src->text + start, '\n', src->len - start);
        *nl = '\0';
        buf_add(dst, src->lines[i], src->text + start);
        *nl = '\n';
        start = (size_t)(nl - src->text) + 1;
    }
}

static Buf out_funcs;    /* every generated function, in order of completion */
static Globals *globals;

/* Runtime helpers used by the generated code. */
static bool used_call[MAX_REG_ARGS + 1], used_invoke[MAX_REG_ARGS + 1];
static bool used_callp, used_invokep;

/* --------------------------------------------------------- function state */

typedef struct FS {
    Func *fn;
    struct FS *enclosing;
    Buf code, cold;
    Buf *cur;
    int next, max;       /* slot allocation */
    int labels;
    int line;
    int *up_off;         /* capture word offset of each upvalue */
    bool packed;         /* captures are in a list, UP[0] */
    struct { char msg[160]; int line, label; } errs[64];
    int nerrs;
    /* Which slots are known to hold a number at the current point of the
     * (hot) code, so that type checks can be left out; and, per label, what
     * is known on every jump to it seen so far. */
    bool *known;
    int known_cap;
    bool reach;          /* the current point can be reached by falling through */
    bool cold_called;    /* the cold block being generated calls Lox code */
    struct { bool *k; int n; bool has; } *rec;
    int rec_cap;
} FS;

static void forget_global_numbers(FS *fs);

static void emit(FS *fs, const char *fmt, ...) {
    char tmp[8192];
    tmp[0] = tmp[1] = tmp[2] = tmp[3] = ' ';
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp + 4, sizeof tmp - 4, fmt, ap);
    va_end(ap);
    buf_add(fs->cur, fs->line, tmp);
    const char *t = tmp + 4;
    if (!strncmp(t, "call L", 6) || !strncmp(t, "calli ", 6) || !strncmp(t, "call rt$call", 12) ||
        !strncmp(t, "call rt$invoke", 14)) {
        if (fs->cur == &fs->cold) fs->cold_called = true;
        else forget_global_numbers(fs);
    }
}

static int new_label(FS *fs) { return fs->labels++; }

/* ------------------------------------------------- what is known about slots
 *
 * known[k] means slot k holds a number at this point. It is set by a type
 * check of the slot and by storing a number in it, and cleared by storing
 * anything else; a newly allocated slot knows nothing. At a label, what is
 * known is what is known on every path to it: falling through, and each jump
 * recorded with jump(). A loop head forgets the variables the loop assigns.
 * Cold code (slow paths) is generated in the middle of the hot code, with the
 * hot code's knowledge at that point; a slow path that jumps back records that
 * knowledge minus the slot it writes. Only variables' knowledge is ever used
 * (read_var), and temporaries' knowledge only by check_number before any
 * other write, so missing a write to a temporary is harmless. */

/* Facts are numbered: global g is defined (g), global g holds a number
 * (NG + g), slot k holds a number (2 NG + k). */
static int NG;
#define F_DEF(g) (g)
#define F_NUM(g) (NG + (g))
#define F_SLOT(k) (2 * NG + (k))

static void fact_set(FS *fs, int k, bool v) {
    if (k < 0) return;
    if (k >= fs->known_cap) {
        int cap = fs->known_cap ? fs->known_cap * 2 : 64;
        while (cap <= k) cap *= 2;
        fs->known = xrealloc(fs->known, (size_t)cap * sizeof *fs->known);
        memset(fs->known + fs->known_cap, 0, (size_t)(cap - fs->known_cap) * sizeof *fs->known);
        fs->known_cap = cap;
    }
    fs->known[k] = v;
}

static bool fact(FS *fs, int k) { return k >= 0 && k < fs->known_cap && fs->known[k]; }

static void know(FS *fs, int slot, bool v) {
    if (slot >= 0) fact_set(fs, F_SLOT(slot), v);
}

static bool is_known(FS *fs, int slot) { return slot >= 0 && fact(fs, F_SLOT(slot)); }

/* After code that can run Lox code (and so assign any global). */
static void forget_global_numbers(FS *fs) {
    for (int g = 0; g < NG; g++) fact_set(fs, F_NUM(g), false);
}

static void forget_all(FS *fs) {
    if (fs->known) memset(fs->known, 0, (size_t)fs->known_cap * sizeof *fs->known);
}

/* Record a jump to label from here, where slot `except` (or -1) is about to
 * be written. */
static void record(FS *fs, int label, int except) {
    if (label >= fs->rec_cap) {
        int cap = fs->rec_cap ? fs->rec_cap * 2 : 64;
        while (cap <= label) cap *= 2;
        fs->rec = xrealloc(fs->rec, (size_t)cap * sizeof *fs->rec);
        memset(fs->rec + fs->rec_cap, 0, (size_t)(cap - fs->rec_cap) * sizeof *fs->rec);
        fs->rec_cap = cap;
    }
    if (!fs->reach && fs->cur == &fs->code) return; /* unreachable: contributes nothing */
    if (!fs->rec[label].has) {
        fs->rec[label].has = true;
        fs->rec[label].n = fs->known_cap;
        fs->rec[label].k = xmalloc((size_t)(fs->known_cap + 1) * sizeof(bool));
        for (int i = 0; i < fs->known_cap; i++) fs->rec[label].k[i] = fs->known[i];
    } else {
        for (int i = 0; i < fs->rec[label].n; i++) fs->rec[label].k[i] = fs->rec[label].k[i] && fact(fs, i);
    }
    /* what the jumping code is about to change */
    bool *k = fs->rec[label].k;
    int n = fs->rec[label].n;
    if (except >= 0 && F_SLOT(except) < n) k[F_SLOT(except)] = false;
    if (fs->cur == &fs->cold && fs->cold_called)
        for (int g = 0; g < NG && F_NUM(g) < n; g++) k[F_NUM(g)] = false;
}

/* Note a jump to label emitted here; `always` for an unconditional one. */
static void jump(FS *fs, int label, bool always) {
    record(fs, label, -1);
    if (always && fs->cur == &fs->code) fs->reach = false;
}

/* Note that execution cannot fall through past here (ret, halt). */
static void dead(FS *fs) {
    if (fs->cur == &fs->code) fs->reach = false;
}

static void place(FS *fs, int label) {
    buf_addf(fs->cur, fs->line, "L%d:", label);
    if (fs->cur != &fs->code) return;
    bool has = label < fs->rec_cap && fs->rec[label].has;
    if (!fs->reach) {
        forget_all(fs);
        if (has)
            for (int i = 0; i < fs->rec[label].n; i++) fact_set(fs, i, fs->rec[label].k[i]);
    } else if (has) {
        for (int i = 0; i < fs->known_cap; i++)
            fs->known[i] = fs->known[i] && i < fs->rec[label].n && fs->rec[label].k[i];
    }
    fs->reach = true;
}

/* Start a block of cold code; returns its label. End it with cold_end. */
static int cold_begin(FS *fs) {
    int l = new_label(fs);
    fs->cur = &fs->cold;
    fs->cold_called = false;
    place(fs, l);
    return l;
}

/* In cold code: jump back to the hot code at label, having written slot d. */
static void cold_back(FS *fs, int label, int d) {
    emit(fs, "jmp L%d", label);
    record(fs, label, d);
}

static void cold_end(FS *fs) { fs->cur = &fs->code; }

/* A cold block that reports a runtime error; one per message and line. */
static int cold_error(FS *fs, const char *msg) {
    for (int i = 0; i < fs->nerrs; i++)
        if (fs->errs[i].line == fs->line && strcmp(fs->errs[i].msg, msg) == 0)
            return fs->errs[i].label;
    int l = cold_begin(fs);
    emit(fs, "call rt$error (%s, %d)", msg, fs->line);
    emit(fs, "halt 70");
    cold_end(fs);
    if (fs->nerrs < 64) {
        snprintf(fs->errs[fs->nerrs].msg, sizeof fs->errs[0].msg, "%s", msg);
        fs->errs[fs->nerrs].line = fs->line;
        fs->errs[fs->nerrs].label = l;
        fs->nerrs++;
    }
    return l;
}

static int alloc_slot(FS *fs) {
    int k = fs->next++;
    if (fs->next > fs->max) fs->max = fs->next;
    know(fs, k, false);
    return k;
}

/* --------------------------------------------------------------- values */

typedef struct {
    char f[320], r[320]; /* operands for the two halves */
    bool num;            /* known to be a number (r is null) */
    bool konst;          /* r is a non-null constant: a string, nil, true or false */
    bool mut;            /* a variable that evaluating later code could change */
    int slot;            /* the slot it is in, or -1 */
    int nfact;           /* the fact "it holds a number" about its location, or -1 */
    bool lit;            /* a number literal, with value d */
    double d;
} Val;

static Val slot_val(int k) {
    Val v = {0};
    if (k < NREG) {
        snprintf(v.f, sizeof v.f, "F%d", k);
        snprintf(v.r, sizeof v.r, "R%d", k);
    } else {
        snprintf(v.f, sizeof v.f, "LOC[%d]", 2 * (k - NREG));
        snprintf(v.r, sizeof v.r, "LOC[%d]", 2 * (k - NREG) + 1);
    }
    v.slot = k;
    v.nfact = F_SLOT(k);
    return v;
}

static Val const_val(const char *r) {
    Val v = {0};
    strcpy(v.f, "0.0");
    snprintf(v.r, sizeof v.r, "%s", r);
    v.konst = true;
    v.slot = -1;
    v.nfact = -1;
    return v;
}

/* A float literal the assembler reads back exactly. */
static void format_number(double d, char *out, size_t n) {
    if (isinf(d)) { snprintf(out, n, d > 0 ? "inf" : "-inf"); return; }
    if (isnan(d)) { snprintf(out, n, "nan"); return; }
    for (int prec = 15; prec <= 17; prec++) {
        snprintf(out, n, "%.*g", prec, d);
        if (strtod(out, NULL) == d) break;
    }
    /* the assembler wants a float to have a '.' or an exponent, without '+' */
    char *e = strchr(out, 'e');
    if (e && e[1] == '+') memmove(e + 1, e + 2, strlen(e + 2) + 1);
    if (!strchr(out, '.') && !strchr(out, 'e')) strcat(out, ".0");
}

static Val num_val(double d) {
    Val v = {0};
    format_number(d, v.f, sizeof v.f);
    strcpy(v.r, "null");
    v.num = true;
    v.slot = -1;
    v.nfact = -1;
    v.lit = true;
    v.d = d;
    return v;
}

static Val bool_val(bool b) { return const_val(b ? TRUE : FALSE); }

/* Whether two constants (number literals or konst) are equal. Constant
 * strings are equal if their text is: the text is a deterministic escaping. */
static bool const_equal(Val *a, Val *b) {
    if (a->lit && b->lit) return a->d == b->d;
    if (a->konst && b->konst) return strcmp(a->r, b->r) == 0;
    return false;
}

static bool is_sentinel(Val *v) {
    return v->konst && (!strcmp(v->r, NIL) || !strcmp(v->r, TRUE) || !strcmp(v->r, FALSE));
}

/* Copy v into slot k. */
static void put(FS *fs, int k, Val v) {
    /* a number into a slot known to hold a number: the R half is null already */
    bool r_done = v.num && is_known(fs, k);
    know(fs, k, v.num);
    Val d = slot_val(k);
    if (v.slot == k) {
        /* a number computed into k leaves its R half unwritten */
        if (strcmp(v.r, d.r) != 0 && !r_done) emit(fs, "mov.r %s, %s", d.r, v.r);
        return;
    }
    if (!(v.konst && k < NREG)) emit(fs, "mov.f64 %s, %s", d.f, v.f);
    if (!r_done) emit(fs, "mov.r %s, %s", d.r, v.r);
}

/* Give a reference result in slot d a defined number half when the slot is
 * a stack word (registers need nothing): stack words keep the type last
 * written, and closures capture with that type. */
static void fix_f(FS *fs, int d) {
    if (d >= NREG) emit(fs, "mov.f64 %s, 0.0", slot_val(d).f);
}

/* An R register holding operand r: r itself, or scratch after a move. */
static const char *rreg(FS *fs, const char *r, const char *scratch) {
    if (r[0] == 'R' && r[1] >= '0' && r[1] <= '9') return r;
    emit(fs, "mov.r %s, %s", scratch, r);
    return scratch;
}

/* Copy a variable operand to a temporary if later code could change it. */
static void protect(FS *fs, Val *v) {
    if (!v->mut) return;
    int t = alloc_slot(fs);
    put(fs, t, *v);
    bool num = v->num;
    *v = slot_val(t);
    v->num = num;
}

/* Whether evaluating e could assign a variable (directly, or by calling code). */
static bool may_change(Expr *e) {
    if (!e) return false;
    switch (e->kind) {
    case E_CALL: case E_ASSIGN:
        return true;
    case E_NUMBER: case E_STRING: case E_NIL: case E_TRUE: case E_FALSE:
    case E_VAR: case E_THIS: case E_SUPER:
        return false;
    default:
        return may_change(e->a) || may_change(e->b);
    }
}

/* Jump to label unless v is a number. */
static void check_number(FS *fs, Val *v, int label) {
    if (v->num) return;
    if (fact(fs, v->nfact)) {
        v->num = true;
        return;
    }
    if (v->konst) {
        emit(fs, "jmp L%d", label);
        return;
    }
    emit(fs, "bnz.r %s, L%d", v->r, label);
    fact_set(fs, v->nfact, true);
    v->num = true;
}

/* The Lox string literal tok as an assembler string. */
static void string_literal(Token *tok, char *out, size_t n) {
    size_t j = 0;
    out[j++] = '"';
    for (int i = 1; i < tok->length - 1 && j + 6 < n; i++) {
        unsigned char c = (unsigned char)tok->start[i];
        if (c == '\\') { out[j++] = '\\'; out[j++] = '\\'; }
        else if (c == '\n') { out[j++] = '\\'; out[j++] = 'n'; }
        else if (c == '\t') { out[j++] = '\\'; out[j++] = 't'; }
        else if (c == '\r') { out[j++] = '\\'; out[j++] = 'r'; }
        else if (c < 0x20 || c >= 0x7f) j += (size_t)snprintf(out + j, n - j, "\\x%02x", c);
        else out[j++] = (char)c;
    }
    out[j++] = '"';
    out[j] = '\0';
}

static char *tok_str(Token *t) {
    static char buf[4][256];
    static int i;
    i = (i + 1) % 4;
    snprintf(buf[i], sizeof buf[i], "%.*s", t->length, t->start);
    return buf[i];
}

/* The message for an undefined variable, as an assembler string. */
static const char *undefined_msg(Token *name) {
    static char buf[300];
    snprintf(buf, sizeof buf, "\"Undefined variable '%.*s'.\"", name->length, name->start);
    return buf;
}

/* ------------------------------------------------------------- variables */

/* The operand of upvalue capture word `off` in the current function. With
 * packed captures, R27 is loaded with the capture list first. */
static void up_word(FS *fs, int off, char *out, size_t n) {
    if (fs->packed) {
        emit(fs, "mov.r R27, UP[0]");
        snprintf(out, n, "R27[%d]", off);
    } else {
        snprintf(out, n, "UP[%d]", off);
    }
}

static int global_index(Token *name) {
    return (int)(global_get(globals, name->start, name->length) - globals->v);
}

/* Check that a global is defined; once it is, it stays defined. */
static void check_defined(FS *fs, Token *name) {
    int g = global_index(name);
    if (fact(fs, F_DEF(g))) return;
    int l = cold_error(fs, undefined_msg(name));
    emit(fs, "bz.b @gd$%s, L%d", tok_str(name), l);
    fact_set(fs, F_DEF(g), true);
}

static Val read_var(FS *fs, Var *v, int up, Token *name, int dst) {
    if (!v) {
        check_defined(fs, name);
        Val g = {0};
        snprintf(g.f, sizeof g.f, "@gf$%s", tok_str(name));
        snprintf(g.r, sizeof g.r, "@gr$%s", tok_str(name));
        g.mut = true;
        g.slot = -1;
        g.nfact = F_NUM(global_index(name));
        g.num = fact(fs, g.nfact);
        return g;
    }
    if (up < 0) {
        if (!var_boxed(v)) {
            Val s = slot_val(v->slot);
            s.mut = v->assigned;
            s.num = is_known(fs, v->slot);
            return s;
        }
        const char *b = rreg(fs, slot_val(v->slot).r, "R28");
        int d = dst >= 0 ? dst : alloc_slot(fs);
        Val dv = slot_val(d);
        emit(fs, "mov.f64 %s, %s->Box.f", dv.f, b);
        emit(fs, "mov.r %s, %s->Box.r", dv.r, b);
        return dv;
    }
    int off = fs->up_off[up];
    char w[32];
    if (!var_boxed(v)) {
        if (!fs->packed) {
            Val u = {0};
            snprintf(u.f, sizeof u.f, "UP[%d]", off);
            snprintf(u.r, sizeof u.r, "UP[%d]", off + 1);
            u.slot = -1;
            u.nfact = -1;
            return u;
        }
        int d = dst >= 0 ? dst : alloc_slot(fs);
        Val dv = slot_val(d);
        emit(fs, "mov.r R27, UP[0]");
        emit(fs, "mov.f64 %s, R27[%d]", dv.f, off);
        emit(fs, "mov.r %s, R27[%d]", dv.r, off + 1);
        return dv;
    }
    up_word(fs, off, w, sizeof w);
    emit(fs, "mov.r R28, %s", w);
    int d = dst >= 0 ? dst : alloc_slot(fs);
    Val dv = slot_val(d);
    emit(fs, "mov.f64 %s, R28->Box.f", dv.f);
    emit(fs, "mov.r %s, R28->Box.r", dv.r);
    return dv;
}

/* Assign an existing variable. */
static void write_var(FS *fs, Var *v, int up, Token *name, Val val) {
    if (!v) {
        check_defined(fs, name);
        int g = global_index(name);
        emit(fs, "mov.f64 @gf$%s, %s", tok_str(name), val.f);
        if (!(val.num && fact(fs, F_NUM(g)))) emit(fs, "mov.r @gr$%s, %s", tok_str(name), val.r);
        fact_set(fs, F_NUM(g), val.num);
        return;
    }
    const char *b;
    if (up < 0) {
        if (!var_boxed(v)) { put(fs, v->slot, val); return; }
        b = rreg(fs, slot_val(v->slot).r, "R28");
    } else {
        char w[32];
        up_word(fs, fs->up_off[up], w, sizeof w);
        emit(fs, "mov.r R28, %s", w);
        b = "R28";
    }
    emit(fs, "mov.f64 %s->Box.f, %s", b, val.f);
    emit(fs, "mov.r %s->Box.r, %s", b, val.r);
}

/* Give a newly declared variable its first value. A local's slot must have
 * been allocated. */
static void define_var(FS *fs, Var *v, Token *name, Val val) {
    if (!v) {
        emit(fs, "mov.f64 @gf$%s, %s", tok_str(name), val.f);
        emit(fs, "mov.r @gr$%s, %s", tok_str(name), val.r);
        emit(fs, "mov.b @gd$%s, 1", tok_str(name));
        fact_set(fs, F_DEF(global_index(name)), true);
        fact_set(fs, F_NUM(global_index(name)), val.num);
        return;
    }
    if (!var_boxed(v)) { put(fs, v->slot, val); return; }
    Val s = slot_val(v->slot);
    emit(fs, "new R28, Box");
    emit(fs, "mov.f64 R28->Box.f, %s", val.f);
    emit(fs, "mov.r R28->Box.r, %s", val.r);
    emit(fs, "mov.r %s, R28", s.r);
    fix_f(fs, v->slot);
}

/* A global function that is declared once, at the top level, and never
 * assigned: once defined, its value never changes, so calls with the right
 * number of arguments can call its code directly. */
static Func *const_function(Token *name) {
    Global *g = global_get(globals, name->start, name->length);
    if (g->decls != 1 || g->assigns != 0 || !g->fun) return NULL;
    if (strcmp(g->name, "clock") == 0) return NULL; /* predefined as a native */
    return g->fun->fn;
}

static void func_label(Func *f, char *out, size_t n) {
    snprintf(out, n, "L%d$%.*s", f->id, f->name.length, f->name.start);
}

/* ----------------------------------------------------------- expressions */

static Val gen(FS *fs, Expr *e, int dst);
static void gen_branch(FS *fs, Expr *e, int label, bool when);
static void gen_function(Func *f, FS *enclosing);

/* Evaluate e into slot k. */
static void gen_into(FS *fs, Expr *e, int k) { put(fs, k, gen(fs, e, k)); }

/* A result that must not live in a temporary above `mark`: move it to d. */
static Val settle(FS *fs, Val v, int d, int mark) {
    if (v.slot >= mark) {
        put(fs, d, v);
        return slot_val(d);
    }
    return v;
}

/* Set I0 to whether a == b. */
static void gen_equal(FS *fs, Val *a, Val *b) {
    if (a->num && b->num) {
        emit(fs, "seq.f64 I0, %s, %s", a->f, b->f);
    } else if ((a->num && b->konst) || (a->konst && b->num)) {
        emit(fs, "mov I0, 0");
    } else if (is_sentinel(a) || is_sentinel(b)) {
        emit(fs, "seq.r I0, %s, %s", a->r, b->r);
    } else if (a->konst || b->konst) {
        emit(fs, "call rt$eq (%s, %s) -> (I0)", a->r, b->r);
    } else {
        int back = new_label(fs);
        emit(fs, "mov I0, 0");
        int ref = -1;
        if (!a->num) {
            ref = cold_begin(fs);
            emit(fs, "call rt$eq (%s, %s) -> (I0)", a->r, b->r);
            cold_back(fs, back, -1);
            cold_end(fs);
            emit(fs, "bnz.r %s, L%d", a->r, ref);
        }
        if (!b->num) {
            emit(fs, "bnz.r %s, L%d", b->r, back);
            jump(fs, back, false);
        }
        emit(fs, "seq.f64 I0, %s, %s", a->f, b->f);
        place(fs, back);
    }
}

/* Store the boolean in I0 (negated if `invert`) into slot d. */
static Val set_bool(FS *fs, int d, bool invert) {
    Val dv = slot_val(d);
    emit(fs, "sel.r %s, I0, %s, %s", dv.r, invert ? FALSE : TRUE, invert ? TRUE : FALSE);
    fix_f(fs, d);
    return dv;
}

static const char *compare_op(TokenType t) {
    switch (t) {
    case T_LESS: return "lt";
    case T_LESS_EQUAL: return "le";
    case T_GREATER: return "gt";
    case T_GREATER_EQUAL: return "ge";
    default: return NULL;
    }
}

static Val gen_binary(FS *fs, Expr *e, int dst) {
    int d = dst >= 0 ? dst : alloc_slot(fs);
    int mark = fs->next;
    Val a = gen(fs, e->a, -1);
    if (may_change(e->b)) protect(fs, &a);
    Val b = gen(fs, e->b, -1);
    fs->line = e->line;
    Val dv = slot_val(d);
    TokenType op = e->tok.type;

    /* constant folding */
    bool consts = (a.lit || a.konst) && (b.lit || b.konst);
    if (consts && (op == T_EQUAL_EQUAL || op == T_BANG_EQUAL)) {
        fs->next = mark;
        return bool_val(const_equal(&a, &b) == (op == T_EQUAL_EQUAL));
    }
    if (a.lit && b.lit) {
        fs->next = mark;
        switch (op) {
        case T_PLUS: return num_val(a.d + b.d);
        case T_MINUS: return num_val(a.d - b.d);
        case T_STAR: return num_val(a.d * b.d);
        case T_SLASH: return num_val(a.d / b.d);
        case T_LESS: return bool_val(a.d < b.d);
        case T_LESS_EQUAL: return bool_val(a.d <= b.d);
        case T_GREATER: return bool_val(a.d > b.d);
        case T_GREATER_EQUAL: return bool_val(a.d >= b.d);
        default: break;
        }
    }

    if (op == T_PLUS && (a.num || b.num)) {
        /* a number plus anything but a number is an error */
        int err = cold_error(fs, "\"Operands must be two numbers or two strings.\"");
        check_number(fs, &a, err);
        check_number(fs, &b, err);
        emit(fs, "add.f64 %s, %s, %s", dv.f, a.f, b.f);
        strcpy(dv.r, "null");
        dv.num = true;
    } else if (op == T_PLUS) {
        if (a.num && b.num) {
            emit(fs, "add.f64 %s, %s, %s", dv.f, a.f, b.f);
            strcpy(dv.r, "null");
            dv.num = true;
        } else if (a.konst || b.konst) {
            emit(fs, "call rt$add (%d, %s, %s, %s, %s) -> (%s, %s)", e->line, a.f, a.r, b.f, b.r, dv.f, dv.r);
        } else {
            int back = new_label(fs);
            int slow = cold_begin(fs);
            emit(fs, "call rt$add (%d, %s, %s, %s, %s) -> (%s, %s)", e->line, a.f, a.r, b.f, b.r, dv.f, dv.r);
            cold_back(fs, back, d);
            cold_end(fs);
            check_number(fs, &a, slow);
            check_number(fs, &b, slow);
            emit(fs, "add.f64 %s, %s, %s", dv.f, a.f, b.f);
            emit(fs, "mov.r %s, null", dv.r);
            place(fs, back);
        }
    } else if (op == T_MINUS || op == T_STAR || op == T_SLASH) {
        int err = cold_error(fs, "\"Operands must be numbers.\"");
        check_number(fs, &a, err);
        check_number(fs, &b, err);
        const char *ins = op == T_MINUS ? "sub" : op == T_STAR ? "mul" : "div";
        emit(fs, "%s.f64 %s, %s, %s", ins, dv.f, a.f, b.f);
        strcpy(dv.r, "null");
        dv.num = true;
    } else if (compare_op(op)) {
        int err = cold_error(fs, "\"Operands must be numbers.\"");
        check_number(fs, &a, err);
        check_number(fs, &b, err);
        emit(fs, "s%s.f64 I0, %s, %s", compare_op(op), a.f, b.f);
        dv = set_bool(fs, d, false);
    } else { /* == != */
        gen_equal(fs, &a, &b);
        dv = set_bool(fs, d, op == T_BANG_EQUAL);
    }
    fs->next = mark;
    return dv;
}

static Val gen_unary(FS *fs, Expr *e, int dst) {
    int d = dst >= 0 ? dst : alloc_slot(fs);
    int mark = fs->next;
    Val a = gen(fs, e->a, -1);
    fs->line = e->line;
    Val dv = slot_val(d);
    if (e->tok.type == T_MINUS && a.lit) {
        dv = num_val(-a.d);
    } else if (e->tok.type == T_MINUS) {
        check_number(fs, &a, cold_error(fs, "\"Operand must be a number.\""));
        emit(fs, "neg.f64 %s, %s", dv.f, a.f);
        strcpy(dv.r, "null");
        dv.num = true;
    } else if (a.num) {
        dv = const_val(FALSE);
    } else if (a.konst) {
        dv = const_val(!strcmp(a.r, NIL) || !strcmp(a.r, FALSE) ? TRUE : FALSE);
    } else {
        emit(fs, "seq.r I0, %s, %s", a.r, NIL);
        emit(fs, "seq.r I1, %s, %s", a.r, FALSE);
        emit(fs, "or I0, I1");
        dv = set_bool(fs, d, false);
    }
    fs->next = mark;
    return dv;
}

static Val gen_logical(FS *fs, Expr *e) {
    /* The left operand is written to the result before the right one is
     * evaluated, so the result is always a fresh temporary. */
    int d = alloc_slot(fs);
    int mark = fs->next;
    int end = new_label(fs);
    gen_into(fs, e->a, d);
    fs->next = mark;
    Val dv = slot_val(d);
    fs->line = e->line;
    /* skip the right operand if the left one decides */
    if (e->tok.type == T_AND) {
        emit(fs, "beq.r %s, %s, L%d", dv.r, NIL, end);
        jump(fs, end, false);
        emit(fs, "beq.r %s, %s, L%d", dv.r, FALSE, end);
        jump(fs, end, false);
    } else {
        int right = new_label(fs);
        emit(fs, "beq.r %s, %s, L%d", dv.r, NIL, right);
        jump(fs, right, false);
        emit(fs, "bne.r %s, %s, L%d", dv.r, FALSE, end);
        jump(fs, end, false);
        place(fs, right);
    }
    gen_into(fs, e->b, d);
    fs->next = mark;
    place(fs, end);
    return dv;
}

/* Evaluate the arguments of a call. Each is protected from the ones after it. */
static Val *gen_args(FS *fs, Expr *e, bool protect_callee, Val *callee) {
    int n = e->nargs;
    Val *args = xmalloc((size_t)(n + 1) * sizeof *args);
    bool *later = xmalloc((size_t)(n + 1) * sizeof *later);
    later[n] = false;
    for (int i = n - 1; i >= 0; i--) later[i] = later[i + 1] || may_change(e->args[i]);
    if (protect_callee && later[0]) protect(fs, callee);
    for (int i = 0; i < n; i++) {
        args[i] = gen(fs, e->args[i], -1);
        if (later[i + 1]) protect(fs, &args[i]);
    }
    free(later);
    return args;
}

/* "a.f, a.r, b.f, b.r, ..." */
static char *arg_list(Val *args, int n) {
    size_t cap = 64 + (size_t)n * 100;
    char *s = xmalloc(cap);
    s[0] = '\0';
    size_t len = 0;
    for (int i = 0; i < n; i++)
        len += (size_t)snprintf(s + len, cap - len, "%s%s, %s", i ? ", " : "", args[i].f, args[i].r);
    return s;
}

/* Put the arguments in a list in R26, for calls with many arguments. */
static void pack_args(FS *fs, Val *args, int n) {
    emit(fs, "lnew R26, %d", 2 * n);
    for (int i = 0; i < n; i++) {
        emit(fs, "lpush.f64 R26, %s", args[i].f);
        emit(fs, "lpush.r R26, %s", args[i].r);
    }
}

/* "(x, " prefix helpers: the argument text that follows fixed operands. */
static const char *comma(const char *list) { return list[0] ? ", " : ""; }

static bool is_this(Expr *e) {
    while (e->kind == E_GROUP) e = e->a;
    return e->kind == E_THIS;
}

/* The property names the program assigns (obj.name = ...) anywhere. A name
 * that is never assigned can never be a field. */
static char **set_names;
static int n_set_names;

static bool field_name_assigned(Token *name) {
    for (int i = 0; i < n_set_names; i++)
        if ((int)strlen(set_names[i]) == name->length && !memcmp(set_names[i], name->start, (size_t)name->length))
            return true;
    return false;
}

static void collect_set_names_stmts(Stmt **v, int n);

static void collect_set_names_expr(Expr *e) {
    if (!e) return;
    if (e->kind == E_SET && !field_name_assigned(&e->tok)) {
        set_names = xrealloc(set_names, (size_t)(n_set_names + 1) * sizeof *set_names);
        set_names[n_set_names++] = xstrndup(e->tok.start, (size_t)e->tok.length);
    }
    collect_set_names_expr(e->a);
    collect_set_names_expr(e->b);
    for (int i = 0; i < e->nargs; i++) collect_set_names_expr(e->args[i]);
}

static void collect_set_names_stmts(Stmt **v, int n) {
    for (int i = 0; i < n; i++) {
        Stmt *s = v[i];
        if (!s) continue;
        collect_set_names_expr(s->expr);
        collect_set_names_stmts(&s->then, 1);
        collect_set_names_stmts(&s->els, 1);
        if (s->kind == S_BLOCK || s->kind == S_CLASS) collect_set_names_stmts(s->stmts, s->n);
        if (s->fn) collect_set_names_stmts(s->fn->body, s->fn->nbody);
    }
}

/* A global class that is declared once, at the top level, and never assigned. */
static Stmt *const_class(Token *name) {
    Global *g = global_get(globals, name->start, name->length);
    if (g->decls != 1 || g->assigns != 0 || !g->cls) return NULL;
    if (strcmp(g->name, "clock") == 0) return NULL;
    return g->cls;
}

/* The initializer instances of class cls run, in *init (NULL if none);
 * false if that cannot be known at compile time. */
static bool class_init(Stmt *cls, int depth, Func **init) {
    for (int i = 0; i < cls->n; i++) {
        if (cls->stmts[i]->fn->kind == FN_INITIALIZER) {
            *init = cls->stmts[i]->fn;
            return true;
        }
    }
    if (!cls->expr) {
        *init = NULL;
        return true;
    }
    Stmt *super = const_class(&cls->expr->tok);
    if (!super || super == cls || depth > 100) return false;
    return class_init(super, depth + 1, init);
}

static int n_inline_caches;

static Val gen_call(FS *fs, Expr *e, int dst) {
    int d = dst >= 0 ? dst : alloc_slot(fs);
    int mark = fs->next;
    Val dv = slot_val(d);
    int argc = e->nargs;
    bool packed = argc > MAX_REG_ARGS;
    Expr *c = e->a;
    while (c->kind == E_GROUP) c = c->a;

    if (c->kind == E_GET) {
        /* obj.name(args): call the method without making a bound method,
         * unless a field of that name shadows it. Each call site caches the
         * class it last saw and that class's method. */
        Val o = gen(fs, c->a, -1);
        Val *args = gen_args(fs, e, true, &o);
        fs->line = e->line;
        char *al = arg_list(args, argc);
        const char *name = tok_str(&c->tok);
        int ic = n_inline_caches++;
        int back = new_label(fs), hit = new_label(fs);
        int slow = cold_begin(fs);
        if (packed) {
            pack_args(fs, args, argc);
            emit(fs, "call rt$invokep (%d, %s, %s, \"%s\", R26) -> (%s, %s)", e->line, o.f, o.r, name, dv.f, dv.r);
            used_invokep = used_callp = true;
        } else {
            emit(fs, "call rt$invoke%d (%d, %s, %s, \"%s\"%s%s) -> (%s, %s)", argc, e->line, o.f, o.r, name,
                 comma(al), al, dv.f, dv.r);
            used_invoke[argc] = used_call[argc] = true;
        }
        cold_back(fs, back, d);
        cold_end(fs);
        /* a cache miss: look the method up in the class (in R25) */
        int miss = cold_begin(fs);
        emit(fs, "mov.r R26, R25->Class.methods");
        emit(fs, "mget.r R26, R26, \"%s\", null", name);
        emit(fs, "bz.r R26, L%d", slow);
        emit(fs, "mov.i64 I1, R26->Fn.arity");
        emit(fs, "bne I1, %d, L%d", argc, slow);
        emit(fs, "mov.r @ic$%d, R25", ic);
        emit(fs, "mov.r R26, R26->Fn.clo");
        emit(fs, "mov.r @icf$%d, R26", ic);
        cold_back(fs, hit, -1);
        cold_end(fs);

        const char *ro = rreg(fs, o.r, "R24");
        if (!is_this(c->a)) {
            emit(fs, "isa I0, %s, Inst", ro);
            emit(fs, "bz I0, L%d", slow);
        }
        if (field_name_assigned(&c->tok)) {
            emit(fs, "mov.r R25, %s->Inst.fields", ro);
            emit(fs, "mhas I0, R25, \"%s\"", name);
            emit(fs, "bnz I0, L%d", slow);
        }
        emit(fs, "mov.r R25, %s->Inst.cls", ro);
        emit(fs, "bne.r R25, @ic$%d, L%d", ic, miss);
        place(fs, hit);
        emit(fs, "mov.r R25, @icf$%d", ic);
        if (packed) {
            pack_args(fs, args, argc);
            emit(fs, "calli R25 (0.0, %s, R26) -> (%s, %s)", ro, dv.f, dv.r);
        } else {
            emit(fs, "calli R25 (0.0, %s%s%s) -> (%s, %s)", ro, comma(al), al, dv.f, dv.r);
        }
        place(fs, back);
        free(al);
        free(args);
        fs->next = mark;
        return dv;
    }

    if (c->kind == E_SUPER) {
        /* super.name(args): look the method up (which reports an undefined
         * one before the arguments are evaluated, as jlox does), then call it
         * with this. */
        Val s = read_var(fs, c->var, c->up, &c->tok, -1);
        Val th = read_var(fs, c->this_var, c->this_up, &c->tok, -1);
        int m = alloc_slot(fs);
        Val mv = slot_val(m);
        fs->line = c->line;
        emit(fs, "call rt$super_method (%d, %s, \"%s\") -> (%s)", c->line, s.r, tok_str(&c->tok), mv.r);
        fix_f(fs, m);
        Val *args = gen_args(fs, e, false, NULL);
        fs->line = e->line;
        char *al = arg_list(args, argc);
        const char *rm = rreg(fs, mv.r, "R25");
        emit(fs, "mov.i64 I1, %s->Fn.arity", rm);
        int err = cold_begin(fs);
        emit(fs, "call rt$arity_error (%d, I1, %d)", e->line, argc);
        emit(fs, "halt 70");
        cold_end(fs);
        emit(fs, "bne I1, %d, L%d", argc, err);
        emit(fs, "mov.r R25, %s->Fn.clo", rm);
        if (packed) {
            pack_args(fs, args, argc);
            emit(fs, "calli R25 (0.0, %s, R26) -> (%s, %s)", th.r, dv.f, dv.r);
        } else {
            emit(fs, "calli R25 (0.0, %s%s%s) -> (%s, %s)", th.r, comma(al), al, dv.f, dv.r);
        }
        free(al);
        free(args);
        fs->next = mark;
        return dv;
    }

    Func *direct = NULL;
    if (c->kind == E_VAR && !c->var) {
        direct = const_function(&c->tok);
        if (direct && (direct->nparams != argc || packed)) direct = NULL;
    }
    if (direct) {
        Val *args = gen_args(fs, e, false, NULL);
        fs->line = e->line;
        char *al = arg_list(args, argc);
        char fl[300];
        func_label(direct, fl, sizeof fl);
        /* a function that is running has been defined */
        if (direct != fs->fn) check_defined(fs, &c->tok);
        emit(fs, "call %s (%s) -> (%s, %s)", fl, al, dv.f, dv.r);
        free(al);
        free(args);
        fs->next = mark;
        return dv;
    }

    Stmt *cls = NULL;
    Func *init = NULL;
    if (c->kind == E_VAR && !c->var && !packed && (cls = const_class(&c->tok))) {
        bool ok = class_init(cls, 0, &init);
        if (!ok || (init ? init->nparams : 0) != argc) cls = NULL;
    }
    if (cls) {
        /* Name(args) for a class that never changes: make the instance and
         * call its initializer here. */
        Val *args = gen_args(fs, e, false, NULL);
        fs->line = e->line;
        char *al = arg_list(args, argc);
        check_defined(fs, &c->tok);
        emit(fs, "mov.r R24, @gr$%s", tok_str(&c->tok));
        emit(fs, "new R25, Inst");
        emit(fs, "mov.r R25->Inst.cls, R24");
        emit(fs, "mnew R26");
        emit(fs, "mov.r R25->Inst.fields, R26");
        if (init && init->nups == 0) {
            char fl[300];
            func_label(init, fl, sizeof fl);
            emit(fs, "call %s (0.0, R25%s%s)", fl, comma(al), al);
        } else if (init) {
            emit(fs, "mov.r R26, R24->Class.init");
            emit(fs, "mov.r R26, R26->Fn.clo");
            emit(fs, "calli R26 (0.0, R25%s%s)", comma(al), al);
        }
        emit(fs, "mov.r %s, R25", dv.r);
        fix_f(fs, d);
        free(al);
        free(args);
        fs->next = mark;
        return dv;
    }

    Val cv = gen(fs, e->a, -1);
    Val *args = gen_args(fs, e, true, &cv);
    fs->line = e->line;
    char *al = arg_list(args, argc);
    int back = new_label(fs);
    int slow = cold_begin(fs);
    if (packed) {
        pack_args(fs, args, argc);
        emit(fs, "call rt$callp (%d, %s, %s, R26) -> (%s, %s)", e->line, cv.f, cv.r, dv.f, dv.r);
        used_callp = true;
    } else {
        emit(fs, "call rt$call%d (%d, %s, %s%s%s) -> (%s, %s)", argc, e->line, cv.f, cv.r, comma(al), al,
             dv.f, dv.r);
        used_call[argc] = true;
    }
    cold_back(fs, back, d);
    cold_end(fs);
    const char *rc = rreg(fs, cv.r, "R24");
    emit(fs, "isa I0, %s, Fn", rc);
    emit(fs, "bz I0, L%d", slow);
    emit(fs, "mov.i64 I1, %s->Fn.arity", rc);
    emit(fs, "bne I1, %d, L%d", argc, slow);
    emit(fs, "mov.r R25, %s->Fn.clo", rc);
    if (packed) {
        pack_args(fs, args, argc);
        emit(fs, "calli R25 (R26) -> (%s, %s)", dv.f, dv.r);
    } else {
        emit(fs, "calli R25 (%s) -> (%s, %s)", al, dv.f, dv.r);
    }
    place(fs, back);
    free(al);
    free(args);
    fs->next = mark;
    return dv;
}

static Val gen_get(FS *fs, Expr *e, int dst) {
    int d = dst >= 0 ? dst : alloc_slot(fs);
    int mark = fs->next;
    Val dv = slot_val(d);
    Val o = gen(fs, e->a, -1);
    fs->line = e->line;
    const char *name = tok_str(&e->tok);
    int back = new_label(fs);
    int slow = cold_begin(fs);
    emit(fs, "call rt$get (%d, %s, \"%s\") -> (%s, %s)", e->line, o.r, name, dv.f, dv.r);
    cold_back(fs, back, d);
    cold_end(fs);
    const char *ro = rreg(fs, o.r, "R24");
    if (!is_this(e->a)) {
        emit(fs, "isa I0, %s, Inst", ro);
        emit(fs, "bz I0, L%d", slow);
    }
    emit(fs, "mov.r R25, %s->Inst.fields", ro);
    emit(fs, "mget.r R25, R25, \"%s\", null", name);
    emit(fs, "bz.r R25, L%d", slow);
    emit(fs, "mov.f64 %s, R25->Box.f", dv.f);
    emit(fs, "mov.r %s, R25->Box.r", dv.r);
    place(fs, back);
    fs->next = mark;
    return dv;
}

static Val gen_set(FS *fs, Expr *e, int dst) {
    int d = dst >= 0 ? dst : alloc_slot(fs);
    int mark = fs->next;
    Val o = gen(fs, e->a, -1);
    if (may_change(e->b)) protect(fs, &o);
    fs->line = e->line;
    /* As in jlox, the object is checked before the value is evaluated. */
    const char *ro;
    if (!is_this(e->a)) {
        int err = cold_error(fs, "\"Only instances have fields.\"");
        ro = rreg(fs, o.r, "R24");
        emit(fs, "isa I0, %s, Inst", ro);
        emit(fs, "bz I0, L%d", err);
    }
    Val v = settle(fs, gen(fs, e->b, d), d, mark);
    fs->line = e->line;
    const char *name = tok_str(&e->tok);
    ro = rreg(fs, o.r, "R24");
    emit(fs, "mov.r R25, %s->Inst.fields", ro);
    emit(fs, "mget.r R26, R25, \"%s\", null", name);
    int back = new_label(fs);
    int add = cold_begin(fs);
    emit(fs, "new R26, Box");
    emit(fs, "mset R25, \"%s\", R26", name);
    cold_back(fs, back, -1);
    cold_end(fs);
    emit(fs, "bz.r R26, L%d", add);
    place(fs, back);
    emit(fs, "mov.f64 R26->Box.f, %s", v.f);
    emit(fs, "mov.r R26->Box.r, %s", v.r);
    fs->next = mark;
    return v;
}

static Val gen(FS *fs, Expr *e, int dst) {
    int saved_line = fs->line;
    fs->line = e->line;
    Val v;
    switch (e->kind) {
    case E_NUMBER:
        v = num_val(e->num);
        break;
    case E_STRING: {
        char *s = xmalloc((size_t)e->tok.length * 4 + 8);
        string_literal(&e->tok, s, (size_t)e->tok.length * 4 + 8);
        if (strlen(s) < sizeof v.r) {
            v = const_val(s);
        } else {
            /* too long for a Val: load it into a slot */
            int d = dst >= 0 ? dst : alloc_slot(fs);
            v = slot_val(d);
            emit(fs, "mov.r %s, %s", v.r, s);
            fix_f(fs, d);
        }
        free(s);
        break;
    }
    case E_NIL: v = const_val(NIL); break;
    case E_TRUE: v = const_val(TRUE); break;
    case E_FALSE: v = const_val(FALSE); break;
    case E_GROUP: v = gen(fs, e->a, dst); break;
    case E_UNARY: v = gen_unary(fs, e, dst); break;
    case E_BINARY: v = gen_binary(fs, e, dst); break;
    case E_LOGICAL: v = gen_logical(fs, e); break;
    case E_VAR: case E_THIS:
        v = read_var(fs, e->var, e->up, &e->tok, dst);
        break;
    case E_ASSIGN:
        if (e->var && e->up < 0 && !var_boxed(e->var)) {
            gen_into(fs, e->a, e->var->slot);
            v = slot_val(e->var->slot);
            v.mut = true;
        } else {
            v = gen(fs, e->a, dst);
            fs->line = e->line;
            write_var(fs, e->var, e->up, &e->tok, v);
        }
        break;
    case E_CALL: v = gen_call(fs, e, dst); break;
    case E_GET: v = gen_get(fs, e, dst); break;
    case E_SET: v = gen_set(fs, e, dst); break;
    case E_SUPER: {
        int d = dst >= 0 ? dst : alloc_slot(fs);
        Val s = read_var(fs, e->var, e->up, &e->tok, -1);
        Val th = read_var(fs, e->this_var, e->this_up, &e->tok, -1);
        v = slot_val(d);
        emit(fs, "call rt$super_get (%d, %s, %s, \"%s\") -> (%s, %s)", e->line, s.r, th.r, tok_str(&e->tok),
             v.f, v.r);
        break;
    }
    default:
        abort();
    }
    fs->line = saved_line;
    return v;
}

static bool literal(Expr *e) {
    while (e->kind == E_GROUP) e = e->a;
    return e->kind == E_NUMBER || e->kind == E_STRING || e->kind == E_NIL || e->kind == E_TRUE ||
           e->kind == E_FALSE;
}

/* Jump to label if the truthiness of v is `when`. */
static void branch_value(FS *fs, Val *v, int label, bool when) {
    if (v->num || v->konst) {
        bool truthy = v->num || (strcmp(v->r, NIL) && strcmp(v->r, FALSE));
        if (truthy == when) {
            emit(fs, "jmp L%d", label);
            jump(fs, label, true);
        }
        return;
    }
    if (!when) {
        emit(fs, "beq.r %s, %s, L%d", v->r, NIL, label);
        jump(fs, label, false);
        emit(fs, "beq.r %s, %s, L%d", v->r, FALSE, label);
        jump(fs, label, false);
    } else {
        int skip = new_label(fs);
        emit(fs, "beq.r %s, %s, L%d", v->r, NIL, skip);
        jump(fs, skip, false);
        emit(fs, "bne.r %s, %s, L%d", v->r, FALSE, label);
        jump(fs, label, false);
        place(fs, skip);
    }
}

/* Jump to label if the truthiness of e is `when`; otherwise fall through. */
static void gen_branch(FS *fs, Expr *e, int label, bool when) {
    int saved_line = fs->line;
    fs->line = e->line;
    switch (e->kind) {
    case E_GROUP:
        gen_branch(fs, e->a, label, when);
        break;
    case E_UNARY:
        if (e->tok.type == T_BANG) {
            gen_branch(fs, e->a, label, !when);
            break;
        }
        goto value;
    case E_LOGICAL: {
        bool is_and = e->tok.type == T_AND;
        if (is_and != when) {
            /* and/false, or/true: either operand decides */
            gen_branch(fs, e->a, label, when);
            gen_branch(fs, e->b, label, when);
        } else {
            int skip = new_label(fs);
            gen_branch(fs, e->a, skip, !when);
            gen_branch(fs, e->b, label, when);
            place(fs, skip);
        }
        break;
    }
    case E_BINARY: {
        TokenType op = e->tok.type;
        const char *cmp = compare_op(op);
        if (!cmp && op != T_EQUAL_EQUAL && op != T_BANG_EQUAL) goto value;
        if (literal(e->a) && literal(e->b)) goto value; /* folded */
        int mark = fs->next;
        Val a = gen(fs, e->a, -1);
        if (may_change(e->b)) protect(fs, &a);
        Val b = gen(fs, e->b, -1);
        fs->line = e->line;
        if (cmp) {
            int err = cold_error(fs, "\"Operands must be numbers.\"");
            check_number(fs, &a, err);
            check_number(fs, &b, err);
            if (when) {
                emit(fs, "b%s.f64 %s, %s, L%d", cmp, a.f, b.f, label);
                jump(fs, label, false);
            } else {
                /* not the inverse comparison, which differs for NaN */
                emit(fs, "s%s.f64 I0, %s, %s", cmp, a.f, b.f);
                emit(fs, "bz I0, L%d", label);
                jump(fs, label, false);
            }
        } else {
            gen_equal(fs, &a, &b);
            emit(fs, "%s I0, L%d", (op == T_EQUAL_EQUAL) == when ? "bnz" : "bz", label);
            jump(fs, label, false);
        }
        fs->next = mark;
        break;
    }
    default:
    value: {
        int mark = fs->next;
        Val v = gen(fs, e, -1);
        fs->line = e->line;
        branch_value(fs, &v, label, when);
        fs->next = mark;
    }
    }
    fs->line = saved_line;
}

/* ------------------------------------------------------------ statements */

static void gen_stmt(FS *fs, Stmt *s);

/* Forget what is known about the local variables that e or s assign. */
static void forget_assigned_expr(FS *fs, Expr *e) {
    if (!e) return;
    if (e->kind == E_ASSIGN && e->var && e->up < 0 && e->var->slot >= 0) know(fs, e->var->slot, false);
    if (e->kind == E_ASSIGN && !e->var) fact_set(fs, F_NUM(global_index(&e->tok)), false);
    if (e->kind == E_CALL) forget_global_numbers(fs);
    forget_assigned_expr(fs, e->a);
    forget_assigned_expr(fs, e->b);
    for (int i = 0; i < e->nargs; i++) forget_assigned_expr(fs, e->args[i]);
}

static void forget_assigned_stmt(FS *fs, Stmt *s) {
    if (!s) return;
    forget_assigned_expr(fs, s->expr);
    forget_assigned_stmt(fs, s->then);
    forget_assigned_stmt(fs, s->els);
    if (s->kind == S_BLOCK)
        for (int i = 0; i < s->n; i++) forget_assigned_stmt(fs, s->stmts[i]);
}

/* The value of the receiver: slot 0 of a method. */
static Val this_val(FS *fs) {
    (void)fs;
    return slot_val(0);
}

/* Create the closure for f, in the enclosing function fs, into register dst. */
static void emit_closure(FS *fs, Func *f, const char *dst) {
    char fl[300];
    func_label(f, fl, sizeof fl);
    int words = 0;
    for (int i = 0; i < f->nups; i++) words += var_boxed(f->ups[i].var) ? 1 : 2;
    bool packed = words > MAX_DIRECT_UP;
    size_t cap = 64 + (size_t)words * 24;
    char *caps = xmalloc(cap);
    size_t len = 0;
    caps[0] = '\0';
    if (fs->packed) {
        /* captures of our own captures are elements of our list */
        bool any = false;
        for (int i = 0; i < f->nups; i++) any |= !f->ups[i].from_local;
        if (any) emit(fs, "mov.r R27, UP[0]");
    }
    if (packed) emit(fs, "lnew R26, %d", words);
    for (int i = 0; i < f->nups; i++) {
        Upval *u = &f->ups[i];
        bool boxed = var_boxed(u->var);
        char w[2][320];
        if (u->from_local) {
            Val s = slot_val(u->var->slot);
            strcpy(w[0], boxed ? s.r : s.f);
            strcpy(w[1], s.r);
        } else {
            int off = fs->up_off[u->index];
            const char *fmt = fs->packed ? "R27[%d]" : "UP[%d]";
            snprintf(w[0], sizeof w[0], fmt, off);
            snprintf(w[1], sizeof w[1], fmt, off + 1);
        }
        if (packed) {
            if (boxed) {
                emit(fs, "lpush.r R26, %s", w[0]);
            } else {
                emit(fs, "lpush.f64 R26, %s", w[0]);
                emit(fs, "lpush.r R26, %s", w[1]);
            }
        } else if (boxed) {
            len += (size_t)snprintf(caps + len, cap - len, "%s%s", len ? ", " : "", w[0]);
        } else {
            len += (size_t)snprintf(caps + len, cap - len, "%s%s, %s", len ? ", " : "", w[0], w[1]);
        }
    }
    if (packed) emit(fs, "closure %s (R26) -> (%s)", fl, dst);
    else emit(fs, "closure %s (%s) -> (%s)", fl, caps, dst);
    free(caps);
}

/* Make an Fn for f (generating its code) in R24. Call `then` between
 * allocating the Fn and making its closure, so that a function can capture
 * the variable that holds it. */
static void make_function(FS *fs, Func *f, Stmt *decl) {
    gen_function(f, fs);
    emit(fs, "new R24, Fn");
    emit(fs, "mov.i64 R24->Fn.arity, %d", f->nparams);
    emit(fs, "mov.r R24->Fn.name, \"%s\"", tok_str(&f->name));
    if (decl) {
        Val fv = const_val("R24");
        fv.konst = false;
        define_var(fs, decl->var, &decl->name, fv);
    }
    emit_closure(fs, f, "R25");
    emit(fs, "mov.r R24->Fn.clo, R25");
}

static void gen_class(FS *fs, Stmt *s) {
    if (s->var) s->var->slot = alloc_slot(fs);
    int saved = fs->next;
    Val sup = const_val("null");
    sup.konst = false;
    if (s->expr) {
        int k = alloc_slot(fs);
        s->super_var->slot = k;
        gen_into(fs, s->expr, k);
        fs->line = s->expr->line;
        sup = slot_val(k);
        const char *rs = rreg(fs, sup.r, "R24");
        emit(fs, "isa I0, %s, Class", rs);
        emit(fs, "bz I0, L%d", cold_error(fs, "\"Superclass must be a class.\""));
        fs->line = s->line;
    }
    int t = alloc_slot(fs);
    Val tv = slot_val(t);
    emit(fs, "call rt$class (\"%s\", %s) -> (%s)", tok_str(&s->name), sup.r, tv.r);
    fix_f(fs, t);
    Val cls = tv;
    cls.konst = false;
    strcpy(cls.f, "0.0");
    define_var(fs, s->var, &s->name, cls);
    for (int i = 0; i < s->n; i++) {
        Func *m = s->stmts[i]->fn;
        fs->line = m->name.line;
        make_function(fs, m, NULL);
        const char *rc = rreg(fs, tv.r, "R25");
        emit(fs, "mov.r R26, %s->Class.methods", rc);
        emit(fs, "mset R26, \"%s\", R24", tok_str(&m->name));
        if (m->kind == FN_INITIALIZER) emit(fs, "mov.r %s->Class.init, R24", rc);
    }
    fs->next = saved;
}

/* Whether evaluating e can have no effect: no side effects and no errors. */
static bool pure(FS *fs, Expr *e) {
    switch (e->kind) {
    case E_NUMBER: case E_STRING: case E_NIL: case E_TRUE: case E_FALSE: case E_THIS:
        return true;
    case E_GROUP:
        return pure(fs, e->a);
    case E_VAR:
        return e->var || fact(fs, F_DEF(global_index(&e->tok)));
    case E_LOGICAL:
        return pure(fs, e->a) && pure(fs, e->b);
    case E_UNARY:
        return e->tok.type == T_BANG ? pure(fs, e->a) : e->a->kind == E_NUMBER;
    case E_BINARY:
        if (e->tok.type == T_EQUAL_EQUAL || e->tok.type == T_BANG_EQUAL)
            return pure(fs, e->a) && pure(fs, e->b);
        return e->a->kind == E_NUMBER && e->b->kind == E_NUMBER;
    default:
        return false;
    }
}

static void gen_stmt(FS *fs, Stmt *s) {
    fs->line = s->line;
    int mark = fs->next;
    switch (s->kind) {
    case S_EXPR:
        if (!pure(fs, s->expr)) gen(fs, s->expr, -1);
        break;
    case S_PRINT: {
        Val v = gen(fs, s->expr, -1);
        fs->line = s->line;
        emit(fs, "call rt$print (%s, %s)", v.f, v.r);
        break;
    }
    case S_VAR: {
        if (s->var) {
            int k = alloc_slot(fs);
            s->var->slot = k;
            mark = fs->next;
            Val v = s->expr ? gen(fs, s->expr, k) : const_val(NIL);
            fs->line = s->line;
            if (var_boxed(s->var)) {
                put(fs, k, v);
                define_var(fs, s->var, &s->name, slot_val(k));
            } else {
                put(fs, k, v);
            }
        } else {
            Val v = s->expr ? gen(fs, s->expr, -1) : const_val(NIL);
            fs->line = s->line;
            define_var(fs, NULL, &s->name, v);
        }
        break;
    }
    case S_BLOCK:
        for (int i = 0; i < s->n; i++) gen_stmt(fs, s->stmts[i]);
        break;
    case S_IF: {
        int els = new_label(fs);
        gen_branch(fs, s->expr, els, false);
        gen_stmt(fs, s->then);
        if (s->els) {
            int end = new_label(fs);
            emit(fs, "jmp L%d", end);
            jump(fs, end, true);
            place(fs, els);
            gen_stmt(fs, s->els);
            place(fs, end);
        } else {
            place(fs, els);
        }
        break;
    }
    case S_WHILE: {
        /* test at the top once, then at the bottom */
        int end = new_label(fs), body = new_label(fs);
        gen_branch(fs, s->expr, end, false);
        place(fs, body);
        /* the loop head is also reached from the end of the body */
        forget_assigned_stmt(fs, s->then);
        forget_assigned_expr(fs, s->expr);
        gen_stmt(fs, s->then);
        fs->line = s->line;
        gen_branch(fs, s->expr, body, true);
        place(fs, end);
        break;
    }
    case S_FUN:
        if (s->var) {
            s->var->slot = alloc_slot(fs);
            mark = fs->next;
        }
        make_function(fs, s->fn, s);
        break;
    case S_RETURN:
        if (fs->fn->kind == FN_INITIALIZER) {
            emit(fs, "ret 0.0, %s", this_val(fs).r);
            dead(fs);
        } else {
            Val v = s->expr ? gen(fs, s->expr, -1) : const_val(NIL);
            fs->line = s->line;
            emit(fs, "ret %s, %s", v.f, v.r);
            dead(fs);
        }
        break;
    case S_CLASS:
        gen_class(fs, s);
        mark = fs->next;
        break;
    }
    fs->next = mark;
}

/* ------------------------------------------------------------- functions */

static void finish_function(FS *fs, const char *header, int line) {
    Buf b = {0};
    buf_add(&b, line, header);
    if (fs->max > NREG) buf_addf(&b, line, "    enter %d", 2 * (fs->max - NREG));
    buf_cat(&b, &fs->code);
    buf_cat(&b, &fs->cold);
    buf_add(&b, 0, ".endf");
    buf_add(&b, 0, "");
    buf_cat(&out_funcs, &b);
}

static void gen_function(Func *f, FS *enclosing) {
    FS fs = {0};
    fs.fn = f;
    fs.enclosing = enclosing;
    fs.cur = &fs.code;
    fs.reach = true;
    fs.line = f->name.line;

    /* capture word offsets */
    fs.up_off = xmalloc((size_t)(f->nups + 1) * sizeof *fs.up_off);
    int words = 0;
    for (int i = 0; i < f->nups; i++) {
        fs.up_off[i] = words;
        words += var_boxed(f->ups[i].var) ? 1 : 2;
    }
    fs.packed = words > MAX_DIRECT_UP;

    bool method = f->kind == FN_METHOD || f->kind == FN_INITIALIZER;
    int base = method ? 1 : 0;
    if (method) f->this_var->slot = 0;
    for (int i = 0; i < f->nparams; i++) f->param_vars[i]->slot = base + i;
    fs.next = fs.max = base + f->nparams;

    bool packed_args = f->nparams > MAX_REG_ARGS;
    if (packed_args) {
        emit(&fs, "mov.r R31, R%d", base);
        for (int i = 0; i < f->nparams; i++) {
            Val p = slot_val(base + i);
            emit(&fs, "mov.f64 %s, R31[%d]", p.f, 2 * i);
            emit(&fs, "mov.r %s, R31[%d]", p.r, 2 * i + 1);
        }
    }
    for (int i = 0; i < f->nparams; i++) {
        Var *v = f->param_vars[i];
        if (var_boxed(v)) define_var(&fs, v, &v->name, slot_val(v->slot));
    }

    for (int i = 0; i < f->nbody; i++) gen_stmt(&fs, f->body[i]);
    fs.line = 0;
    if (f->kind == FN_INITIALIZER) emit(&fs, "ret 0.0, %s", this_val(&fs).r);
    else emit(&fs, "ret 0.0, %s", NIL);

    /* the signature */
    char fl[300];
    func_label(f, fl, sizeof fl);
    size_t cap = 128 + (size_t)f->nparams * 10;
    char *sig = xmalloc(cap);
    size_t len = (size_t)snprintf(sig, cap, ".func %s(", fl);
    if (method) len += (size_t)snprintf(sig + len, cap - len, "f64, ref");
    if (packed_args) {
        len += (size_t)snprintf(sig + len, cap - len, "%sref", method ? ", " : "");
    } else {
        for (int i = 0; i < f->nparams; i++)
            len += (size_t)snprintf(sig + len, cap - len, "%sf64, ref", (method || i) ? ", " : "");
    }
    snprintf(sig + len, cap - len, ") -> (f64, ref)");
    finish_function(&fs, sig, f->name.line);
    free(sig);
}

static void gen_script(Func *script) {
    FS fs = {0};
    fs.fn = script;
    fs.cur = &fs.code;
    fs.reach = true;
    fs.up_off = xmalloc(sizeof *fs.up_off);
    fs.line = 0;

    int try_start = new_label(&fs), try_end = new_label(&fs), fault = new_label(&fs);
    emit(&fs, ".try L%d, L%d, L%d", try_start, try_end, fault);
    place(&fs, try_start);
    for (int i = 0; i < globals->n; i++) {
        if (strcmp(globals->v[i].name, "clock") == 0) {
            emit(&fs, "new R24, Native");
            emit(&fs, "mov.r R24->Native.name, \"clock\"");
            emit(&fs, "mov.f64 @gf$clock, 0.0");
            emit(&fs, "mov.r @gr$clock, R24");
            emit(&fs, "mov.b @gd$clock, 1");
        }
    }
    for (int i = 0; i < script->nbody; i++) gen_stmt(&fs, script->body[i]);
    fs.line = 0;
    emit(&fs, "ret 0");
    /* the handler region covers the cold code too */
    fs.cur = &fs.cold;
    place(&fs, try_end);
    place(&fs, fault);
    emit(&fs, "catch R0");
    emit(&fs, "call rt$fault (R0)");
    emit(&fs, "halt 70");
    finish_function(&fs, ".func main -> i64", 0);
}

/* ------------------------------------------------------- runtime helpers */

static void emit_line(Buf *b, const char *fmt, ...) {
    char tmp[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    buf_add(b, 0, tmp);
}

static void gen_helpers(Buf *b) {
    for (int n = 0; n <= MAX_REG_ARGS; n++) {
        if (!used_call[n]) continue;
        char params[512] = "", args[512] = "";
        size_t pl = 0, al = 0;
        for (int i = 1; i <= n; i++) {
            pl += (size_t)snprintf(params + pl, sizeof params - pl, ", f64, ref");
            al += (size_t)snprintf(args + al, sizeof args - al, "%sF%d, R%d", i > 1 ? ", " : "", i, i);
        }
        const char *c = n ? ", " : "";
        emit_line(b, "; Call any value with %d arguments (F1/R1, ...).", n);
        emit_line(b, ".func rt$call%d(i64, f64, ref%s) -> (f64, ref)", n, params);
        emit_line(b, "    isa I1, R0, Fn");
        emit_line(b, "    bz I1, 1f");
        emit_line(b, "    mov.i64 I2, R0->Fn.arity");
        emit_line(b, "    bne I2, %d, 9f", n);
        emit_line(b, "    mov.r R30, R0->Fn.clo");
        emit_line(b, "    tcalli R30 (%s)", args);
        emit_line(b, "1:  isa I1, R0, Bound");
        emit_line(b, "    bz I1, 2f");
        emit_line(b, "    mov.r R29, R0->Bound.fn");
        emit_line(b, "    mov.i64 I2, R29->Fn.arity");
        emit_line(b, "    bne I2, %d, 9f", n);
        emit_line(b, "    mov.r R30, R29->Fn.clo");
        emit_line(b, "    mov.r R28, R0->Bound.recv");
        emit_line(b, "    mov.f64 F30, 0.0");
        emit_line(b, "    tcalli R30 (F30, R28%s%s)", c, args);
        emit_line(b, "2:  isa I1, R0, Class");
        emit_line(b, "    bz I1, 3f");
        emit_line(b, "    call rt$new_instance (R0) -> (R28)");
        emit_line(b, "    mov.r R29, R0->Class.init");
        emit_line(b, "    bz.r R29, 4f");
        emit_line(b, "    mov.i64 I2, R29->Fn.arity");
        emit_line(b, "    bne I2, %d, 9f", n);
        emit_line(b, "    mov.r R30, R29->Fn.clo");
        emit_line(b, "    calli R30 (0.0, R28%s%s) -> (F31, R27)", c, args);
        emit_line(b, "    ret 0.0, R28");
        if (n == 0) {
            emit_line(b, "4:  ret 0.0, R28");
        } else {
            emit_line(b, "4:  mov I2, 0");
            emit_line(b, "    jmp 9f");
        }
        emit_line(b, "3:  isa I1, R0, Native");
        emit_line(b, "    bz I1, 5f");
        if (n == 0) {
            emit_line(b, "    tcall rt$native (R0)");
        } else {
            emit_line(b, "    mov I2, 0");
            emit_line(b, "    jmp 9f");
        }
        emit_line(b, "5:  call rt$error (\"Can only call functions and classes.\", I0)");
        emit_line(b, "    halt 70");
        emit_line(b, "9:  call rt$arity_error (I0, I2, %d)", n);
        emit_line(b, "    halt 70");
        emit_line(b, ".endf");
        emit_line(b, "");
    }
    for (int n = 0; n <= MAX_REG_ARGS; n++) {
        if (!used_invoke[n]) continue;
        char params[512] = "", args[512] = "";
        size_t pl = 0, al = 0;
        for (int i = 1; i <= n; i++) {
            pl += (size_t)snprintf(params + pl, sizeof params - pl, ", f64, ref");
            al += (size_t)snprintf(args + al, sizeof args - al, ", F%d, R%d", i, i + 1);
        }
        emit_line(b, "; Call property name of an object with %d arguments.", n);
        emit_line(b, ".func rt$invoke%d(i64, f64, ref, ref%s) -> (f64, ref)", n, params);
        emit_line(b, "    call rt$get (I0, R0, R1) -> (F31, R31)");
        emit_line(b, "    tcall rt$call%d (I0, F31, R31%s)", n, args);
        emit_line(b, ".endf");
        emit_line(b, "");
    }
    if (used_callp) {
        emit_line(b, "; Call any value with the arguments in a list (f, r, f, r, ...).");
        emit_line(b, ".func rt$callp(i64, f64, ref, ref) -> (f64, ref)");
        emit_line(b, "    len I3, R1");
        emit_line(b, "    shr I3, 1");
        emit_line(b, "    isa I1, R0, Fn");
        emit_line(b, "    bz I1, 1f");
        emit_line(b, "    mov.i64 I2, R0->Fn.arity");
        emit_line(b, "    bne I2, I3, 9f");
        emit_line(b, "    mov.r R30, R0->Fn.clo");
        emit_line(b, "    tcalli R30 (R1)");
        emit_line(b, "1:  isa I1, R0, Bound");
        emit_line(b, "    bz I1, 2f");
        emit_line(b, "    mov.r R29, R0->Bound.fn");
        emit_line(b, "    mov.i64 I2, R29->Fn.arity");
        emit_line(b, "    bne I2, I3, 9f");
        emit_line(b, "    mov.r R30, R29->Fn.clo");
        emit_line(b, "    mov.r R28, R0->Bound.recv");
        emit_line(b, "    mov.f64 F30, 0.0");
        emit_line(b, "    tcalli R30 (F30, R28, R1)");
        emit_line(b, "2:  isa I1, R0, Class");
        emit_line(b, "    bz I1, 3f");
        emit_line(b, "    call rt$new_instance (R0) -> (R28)");
        emit_line(b, "    mov.r R29, R0->Class.init");
        emit_line(b, "    mov I2, 0");
        emit_line(b, "    bz.r R29, 9f");
        emit_line(b, "    mov.i64 I2, R29->Fn.arity");
        emit_line(b, "    bne I2, I3, 9f");
        emit_line(b, "    mov.r R30, R29->Fn.clo");
        emit_line(b, "    calli R30 (0.0, R28, R1) -> (F31, R27)");
        emit_line(b, "    ret 0.0, R28");
        emit_line(b, "3:  isa I1, R0, Native");
        emit_line(b, "    mov I2, 0");
        emit_line(b, "    bnz I1, 9f");
        emit_line(b, "    call rt$error (\"Can only call functions and classes.\", I0)");
        emit_line(b, "    halt 70");
        emit_line(b, "9:  call rt$arity_error (I0, I2, I3)");
        emit_line(b, "    halt 70");
        emit_line(b, ".endf");
        emit_line(b, "");
    }
    if (used_invokep) {
        emit_line(b, ".func rt$invokep(i64, f64, ref, ref, ref) -> (f64, ref)");
        emit_line(b, "    call rt$get (I0, R0, R1) -> (F31, R31)");
        emit_line(b, "    tcall rt$callp (I0, F31, R31, R2)");
        emit_line(b, ".endf");
        emit_line(b, "");
    }
}

/* ------------------------------------------------------------------ main */

void generate(Func *script, Globals *g, const char *runtime, FILE *out) {
    globals = g;
    NG = g->n;
    collect_set_names_stmts(script->body, script->nbody);
    gen_script(script);

    Buf all = {0};
    buf_addf(&all, 0, "; Generated by mlws-vm-loxc %s from %s", VERSION, source_path);
    buf_add(&all, 0, "");
    for (const char *p = runtime; *p;) {
        const char *nl = strchr(p, '\n');
        size_t n = nl ? (size_t)(nl - p) : strlen(p);
        char *line = xstrndup(p, n);
        buf_add(&all, 0, line);
        free(line);
        p += n + (nl ? 1 : 0);
    }
    gen_helpers(&all);

    buf_add(&all, 0, "; Globals: value (number, reference) and whether defined.");
    buf_add(&all, 0, "    .align 8");
    for (int i = 0; i < g->n; i++) {
        buf_addf(&all, 0, "gf$%s: .f64 0", g->v[i].name);
        buf_addf(&all, 0, "gr$%s: .zero 8", g->v[i].name);
    }
    for (int i = 0; i < g->n; i++) buf_addf(&all, 0, "gd$%s: .b 0", g->v[i].name);
    buf_add(&all, 0, "; Method call inline caches: the class last seen, and its method's closure.");
    buf_add(&all, 0, "    .align 8");
    for (int i = 0; i < n_inline_caches; i++) {
        buf_addf(&all, 0, "ic$%d: .zero 8", i);
        buf_addf(&all, 0, "icf$%d: .zero 8", i);
    }
    buf_add(&all, 0, "");

    buf_cat(&all, &out_funcs);

    /* The map from assembly lines to Lox lines, for errors the VM raises
     * (such as a stack overflow). Its own lines map to nothing. */
    int nl = all.nlines;
    int *map = all.lines;
    Buf tail = {0};
    buf_add(&tail, 0, "; Lox line of each line of this file");
    buf_add(&tail, 0, "    .align 8");
    buf_add(&tail, 0, "rt$nlines: .i64 0"); /* patched below */
    buf_add(&tail, 0, "rt$linemap:");
    char line[512];
    for (int i = 0; i < nl; i += 32) {
        size_t len = (size_t)snprintf(line, sizeof line, "    .i32 ");
        for (int j = i; j < i + 32 && j < nl; j++)
            len += (size_t)snprintf(line + len, sizeof line - len, "%s%d", j > i ? "," : "", map[j]);
        buf_add(&tail, 0, line);
    }

    fwrite(all.text, 1, all.len, out);
    /* write the tail, with the line count filled in */
    char *t = tail.text;
    char *mark = strstr(t, "rt$nlines: .i64 0");
    fwrite(t, 1, (size_t)(mark - t), out);
    fprintf(out, "rt$nlines: .i64 %d", nl);
    mark += strlen("rt$nlines: .i64 0");
    fwrite(mark, 1, tail.len - (size_t)(mark - t), out);
}
