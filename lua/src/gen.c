/* The code generator: Lua AST to MLWSVM assembly.

   A Lua value is a triple of registers: an I register (an integer), an F
   register (a float) and an R register, which says what the value is:

       null            an integer, in the I register
       4n              a float, in the F register
       1n, 2n, 3n      nil, false, true
       anything else   a string, table (Tab), function (Fn) or coroutine (Thread)

   (1n to 4n are BigInt constants, which no Lua value can be, so they never
   collide with a Lua string or a table key.)

   Each local variable or temporary is a slot k: (Ik, Fk, Rk) for k < NREG,
   and three stack words beyond. I/F/R registers NREG..31 are scratch.

   A Lua function compiles to .func L<id>$name(i64, f64, ref, ...) ->
   (i64 n, i64, f64, ref): its parameters as triples, and its number of
   results with the first one. Results after the first are left in the
   list @rt$mv. See runtime.masm for the rest of the conventions. */
#include "lua.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define NREG 28             /* slots that live in registers (28..31 are scratch) */
#define MAXARGS 7           /* arguments passed in registers (a call has at most 30 operands) */
#define MAXCAPWORDS 28      /* capture words before they are packed in a list */

#define NIL "1n"
#define FALSE "2n"
#define TRUE "3n"
#define FLT "4n"

/* ---------------------------------------------------------------- buffers */

typedef struct {
    char *text;
    int line;           /* the Lua line, 0 for none */
} Line;

typedef struct {
    Line *v;
    int n, cap;
} Buf;

static void buf_add(Buf *b, int line, const char *s) {
    if (b->n == b->cap) {
        b->cap = b->cap ? 2 * b->cap : 64;
        b->v = xrealloc(b->v, (size_t)b->cap * sizeof *b->v);
    }
    b->v[b->n].text = xstrdup(s);
    b->v[b->n].line = line;
    b->n++;
}

static void buf_truncate(Buf *b, int n) {
    while (b->n > n)
        free(b->v[--b->n].text);
}

static void buf_cat(Buf *dst, Buf *src) {
    for (int i = 0; i < src->n; i++)
        buf_add(dst, src->v[i].line, src->v[i].text);
}

/* ------------------------------------------------------------------ state */

typedef enum { TY_ANY, TY_INT, TY_FLT, TY_STR, TY_TAB, TY_NIL, TY_BOOL, TY_FN } Ty;

typedef struct {
    bool has;
    Ty *ty;
    int n;
} LabelFacts;

typedef struct {
    int brk;            /* the label that break jumps to */
    int nclose;         /* to-be-closed variables active outside the loop */
} Loop;

typedef struct {
    int slot;
    int level;          /* active locals before it */
} CloseVar;

typedef struct FS {
    struct FS *parent;
    Func *f;
    Buf code, cold, *cur;
    int labels;
    int nslots, maxslots;
    int nlocals;        /* slots held by active locals (temporaries are above) */
    Ty *ty;
    int tycap;
    bool dead;
    LabelFacts *lf;
    int lfcap;
    VEC(Loop) loops;
    VEC(CloseVar) closes;
    int line;
    int vaslot;         /* the slot whose R register holds the vararg list, or -1 */
    int *label_ids;     /* goto labels: label statement -> label id (by Stmt pointer) */
    VEC(Stmt *) gotolabels;
    VEC(int) gotolabel_ids;
    int nactive;        /* active local variables, counted as the parser does */
    int *upslot;        /* capture i copied into this slot at entry, or -1 */
    struct { Ty *ty; bool dead; Buf *cur; int skip; } saved[16];  /* open cold sections */
    int nsaved;
} FS;

typedef struct {
    const char *chunk;
    Buf out;            /* the functions */
    Buf data;           /* module data */
    VEC(char *) pinned; /* global names with fixed slots in _G */
    bool env_fast;      /* free names use the pinned slots of _G */
    bool used_call[MAXARGS + 2];
    int nic;            /* inline cache sites */
    int nmc;            /* method cache sites */
    int nctor;          /* table constructors that use a shape */
} Gen;

static Gen G;

static int line_of(FS *fs) { return fs->f->prelude ? -2 : fs->line; }

static void emit(FS *fs, const char *fmt, ...) {
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    /* a null test of a spilled R half needs its type: a memory operand
       would be read as an i64 (LOC words 3k are the R halves) */
    int w;
    char *p = buf + 4;
    if ((strncmp(p, "bnz LOC[", 8) == 0 && sscanf(p + 8, "%d", &w) == 1 && w % 3 == 0) ||
        (strncmp(p, "bz LOC[", 7) == 0 && sscanf(p + 7, "%d", &w) == 1 && w % 3 == 0)) {
        char tmp[4096];
        char *sp = strchr(p, ' ');
        snprintf(tmp, sizeof tmp, "    %.*s.r%s", (int)(sp - p), p, sp);
        buf_add(fs->cur, line_of(fs), tmp);
        return;
    }
    buf_add(fs->cur, line_of(fs), buf);
}

/* A dynamically built string. */
typedef struct {
    char *s;
    size_t n, cap;
} SB;

static void sb_add(SB *b, const char *fmt, ...) {
    char tmp[2048];
    va_list ap;
    va_start(ap, fmt);
    int k = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (b->n + (size_t)k + 1 > b->cap) {
        b->cap = (b->n + (size_t)k + 1) * 2;
        b->s = xrealloc(b->s, b->cap);
    }
    memcpy(b->s + b->n, tmp, (size_t)k + 1);
    b->n += (size_t)k;
}

static const char *sb_str(SB *b) { return b->s ? b->s : ""; }

/* ---------------------------------------------------------------- strings */

/* A masm string literal for bytes s. */
static char *masm_string(Str s) {
    SB b = {0};
    sb_add(&b, "\"");
    for (int i = 0; i < s.len; i++) {
        unsigned char c = (unsigned char)s.s[i];
        if (c == '"' || c == '\\')
            sb_add(&b, "\\%c", c);
        else if (c >= 32 && c < 127)
            sb_add(&b, "%c", c);
        else
            sb_add(&b, "\\x%02x", c);
    }
    sb_add(&b, "\"");
    return b.s;
}

static char *cstr_lit(const char *s) { return masm_string((Str){s, (int)strlen(s)}); }

static char *masm_float(double d) {
    char buf[64];
    if (isnan(d))
        return xstrdup("nan");
    if (isinf(d))
        return xstrdup(d > 0 ? "inf" : "-inf");
    snprintf(buf, sizeof buf, "%.17g", d);
    if (!strpbrk(buf, ".e"))
        strcat(buf, ".0");
    return xstrdup(buf);
}

/* ---------------------------------------------------------------- facts */

static void ty_ensure(FS *fs, int n) {
    if (n <= fs->tycap)
        return;
    int cap = fs->tycap ? fs->tycap : 32;
    while (cap < n)
        cap *= 2;
    fs->ty = xrealloc(fs->ty, (size_t)cap * sizeof *fs->ty);
    for (int i = fs->tycap; i < cap; i++)
        fs->ty[i] = TY_ANY;
    fs->tycap = cap;
}

static Ty ty_get(FS *fs, int k) { return k < fs->tycap ? fs->ty[k] : TY_ANY; }

static void ty_set(FS *fs, int k, Ty t) {
    ty_ensure(fs, k + 1);
    fs->ty[k] = t;
}

static Ty *ty_copy(FS *fs) {
    Ty *c = xmalloc((size_t)fs->tycap * sizeof *c);
    memcpy(c, fs->ty, (size_t)fs->tycap * sizeof *c);
    return c;
}

static void forget_all(FS *fs) {
    for (int i = 0; i < fs->tycap; i++)
        fs->ty[i] = TY_ANY;
}

static LabelFacts *label_facts(FS *fs, int label) {
    if (label >= fs->lfcap) {
        int cap = fs->lfcap ? fs->lfcap : 64;
        while (cap <= label)
            cap *= 2;
        fs->lf = xrealloc(fs->lf, (size_t)cap * sizeof *fs->lf);
        memset(fs->lf + fs->lfcap, 0, (size_t)(cap - fs->lfcap) * sizeof *fs->lf);
        fs->lfcap = cap;
    }
    return &fs->lf[label];
}

static int new_label(FS *fs) {
    int l = fs->labels++;
    LabelFacts *f = label_facts(fs, l);
    f->has = false;
    free(f->ty);
    f->ty = NULL;
    f->n = 0;
    return l;
}

/* The current facts flow to label. */
static void record(FS *fs, int label) {
    if (fs->dead)
        return;
    LabelFacts *f = label_facts(fs, label);
    if (!f->has) {
        f->has = true;
        f->ty = ty_copy(fs);
        f->n = fs->tycap;
        return;
    }
    for (int i = 0; i < f->n; i++)
        if (f->ty[i] != ty_get(fs, i))
            f->ty[i] = TY_ANY;
}

static void jump(FS *fs, int label) {
    if (fs->dead)
        return;
    emit(fs, "    jmp .L%d", label);
    record(fs, label);
    fs->dead = true;
}

/* A conditional branch: emit it and record the facts. */
static void branch(FS *fs, int label, const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    if (fs->dead)
        return;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    emit(fs, "    %s, .L%d", buf, label);
    record(fs, label);
}

static void place(FS *fs, int label) {
    LabelFacts *f = label_facts(fs, label);
    emit(fs, ".L%d:", label);
    if (fs->dead) {
        if (f->has) {
            ty_ensure(fs, f->n);
            for (int i = 0; i < fs->tycap; i++)
                fs->ty[i] = i < f->n ? f->ty[i] : TY_ANY;
        } else {
            forget_all(fs);
        }
    } else if (f->has) {
        for (int i = 0; i < fs->tycap; i++)
            if (i >= f->n || f->ty[i] != fs->ty[i])
                fs->ty[i] = TY_ANY;
    }
    fs->dead = false;
}

/* Cold code: slow paths, placed after the function's last instruction. */
static void cold_begin(FS *fs) {
    if (fs->nsaved == 16) {
        fprintf(stderr, "mlws-vm-luac: internal error: cold sections nested too deeply\n");
        exit(70);
    }
    fs->saved[fs->nsaved].ty = ty_copy(fs);
    fs->saved[fs->nsaved].dead = fs->dead;
    fs->saved[fs->nsaved].cur = fs->cur;
    fs->saved[fs->nsaved].skip = -1;
    if (fs->cur == &fs->cold && !fs->dead) {
        /* a cold section inside one: its code would follow this code, so
           this code jumps over it */
        int skip = new_label(fs);
        jump(fs, skip);
        fs->saved[fs->nsaved].skip = skip;
    }
    fs->nsaved++;
    fs->dead = true;
    fs->cur = &fs->cold;
}

static void cold_end(FS *fs) {
    fs->nsaved--;
    memcpy(fs->ty, fs->saved[fs->nsaved].ty, (size_t)fs->tycap * sizeof *fs->ty);
    free(fs->saved[fs->nsaved].ty);
    fs->dead = fs->saved[fs->nsaved].dead;
    fs->cur = fs->saved[fs->nsaved].cur;
    if (fs->saved[fs->nsaved].skip >= 0)
        place(fs, fs->saved[fs->nsaved].skip);
}

/* ------------------------------------------------------------------ slots */

static int alloc_slot(FS *fs) {
    int k = fs->nslots++;
    if (fs->nslots > fs->maxslots)
        fs->maxslots = fs->nslots;
    ty_set(fs, k, TY_ANY);
    return k;
}

static int alloc_slots(FS *fs, int n) {
    int k = fs->nslots;
    for (int i = 0; i < n; i++)
        alloc_slot(fs);
    return k;
}

static bool is_temp(FS *fs, int k) { return k >= fs->nlocals; }

/* Operand text: a ring of buffers, valid until 256 more are made. */
static char *ring(void) {
    static char bufs[256][96];
    static int k;
    k = (k + 1) & 255;
    return bufs[k];
}

static const char *slot_op(int k, char bank) {
    char *s = ring();
    if (k < NREG) {
        snprintf(s, 96, "%c%d", bank, k);
    } else {
        int w = 3 * (k - NREG) + (bank == 'R' ? 0 : bank == 'I' ? 1 : 2);
        snprintf(s, 96, "LOC[%d]", w);
    }
    return s;
}

static const char *SI(int k) { return slot_op(k, 'I'); }
static const char *SF(int k) { return slot_op(k, 'F'); }
static const char *SR(int k) { return slot_op(k, 'R'); }

/* The R register of slot k, as the base of a field or element operand
   (which must be a register): a spilled slot is loaded into scratch. */
static const char *RB(FS *fs, int k, const char *scratch);

static const char *RB(FS *fs, int k, const char *scratch) {
    if (k < NREG)
        return SR(k);
    emit(fs, "    mov %s, %s", scratch, SR(k));
    return scratch;
}

/* ----------------------------------------------------------------- values */

typedef enum { V_SLOT, V_INT, V_FLT, V_STR, V_NIL, V_TRUE, V_FALSE } VK;

typedef struct {
    VK k;
    int slot;
    int64_t i;
    double f;
    Str s;
} Val;

static Val vslot(int k) { return (Val){V_SLOT, k, 0, 0, {0}}; }
static Val vint(int64_t i) { return (Val){V_INT, -1, i, 0, {0}}; }
static Val vflt(double f) { return (Val){V_FLT, -1, 0, f, {0}}; }
static Val vstr(Str s) { return (Val){V_STR, -1, 0, 0, s}; }
static Val vk(VK k) { return (Val){k, -1, 0, 0, {0}}; }

static Ty vty(FS *fs, Val *v) {
    switch (v->k) {
    case V_SLOT: return ty_get(fs, v->slot);
    case V_INT: return TY_INT;
    case V_FLT: return TY_FLT;
    case V_STR: return TY_STR;
    case V_NIL: return TY_NIL;
    default: return TY_BOOL;
    }
}

static bool is_const(Val *v) { return v->k != V_SLOT; }

static const char *VI(Val *v) {
    if (v->k == V_SLOT)
        return SI(v->slot);
    char *s = ring();
    snprintf(s, 96, "%lld", (long long)(v->k == V_INT ? v->i : 0));
    return s;
}

static const char *VF(Val *v) {
    if (v->k == V_SLOT)
        return SF(v->slot);
    char *s = ring();
    char *f = masm_float(v->k == V_FLT ? v->f : 0.0);
    snprintf(s, 96, "%s", f);
    free(f);
    return s;
}

/* The R operand. A long string constant is returned in a fresh buffer. */
static const char *VR(Val *v) {
    switch (v->k) {
    case V_SLOT: return SR(v->slot);
    case V_INT: return "null";
    case V_FLT: return FLT;
    case V_NIL: return NIL;
    case V_TRUE: return TRUE;
    case V_FALSE: return FALSE;
    case V_STR: return masm_string(v->s);
    }
    return "null";
}

/* Does the R half need writing when a slot of type `have` gets a value of
   type t? (INT means R is null, FLT means it is 4n.) */
static void put(FS *fs, int d, Val v) {
    Ty have = ty_get(fs, d);
    switch (v.k) {
    case V_INT:
        emit(fs, "    mov %s, %lld", SI(d), (long long)v.i);
        if (have != TY_INT)
            emit(fs, "    mov.r %s, null", SR(d));
        ty_set(fs, d, TY_INT);
        return;
    case V_FLT:
        emit(fs, "    mov %s, %s", SF(d), VF(&v));
        if (have != TY_FLT)
            emit(fs, "    mov.r %s, " FLT, SR(d));
        ty_set(fs, d, TY_FLT);
        return;
    case V_STR:
        emit(fs, "    mov.r %s, %s", SR(d), VR(&v));
        ty_set(fs, d, TY_STR);
        return;
    case V_NIL: case V_TRUE: case V_FALSE:
        emit(fs, "    mov.r %s, %s", SR(d), VR(&v));
        ty_set(fs, d, v.k == V_NIL ? TY_NIL : TY_BOOL);
        return;
    case V_SLOT:
        break;
    }
    int s = v.slot;
    if (s == d)
        return;
    Ty t = ty_get(fs, s);
    switch (t) {
    case TY_INT:
        emit(fs, "    mov %s, %s", SI(d), SI(s));
        if (have != TY_INT)
            emit(fs, "    mov.r %s, null", SR(d));
        break;
    case TY_FLT:
        emit(fs, "    mov %s, %s", SF(d), SF(s));
        if (have != TY_FLT)
            emit(fs, "    mov.r %s, " FLT, SR(d));
        break;
    case TY_ANY:
        emit(fs, "    mov %s, %s", SI(d), SI(s));
        emit(fs, "    mov %s, %s", SF(d), SF(s));
        emit(fs, "    mov %s, %s", SR(d), SR(s));
        break;
    default:
        emit(fs, "    mov %s, %s", SR(d), SR(s));
        break;
    }
    ty_set(fs, d, t);
}

/* A Val in a slot (allocating a temporary for a constant). */
static int in_slot(FS *fs, Val v) {
    if (v.k == V_SLOT)
        return v.slot;
    int k = alloc_slot(fs);
    put(fs, k, v);
    return k;
}

/* Set the R half of slot d for a result of type t computed into its I or
   F half. */
static void set_tag(FS *fs, int d, Ty t) {
    Ty have = ty_get(fs, d);
    if (t == TY_INT && have != TY_INT)
        emit(fs, "    mov.r %s, null", SR(d));
    else if (t == TY_FLT && have != TY_FLT)
        emit(fs, "    mov.r %s, " FLT, SR(d));
    ty_set(fs, d, t);
}

/* Push a value onto the list in R register / operand `list`. */
/* Push a value onto the list in R register / operand `list`, as two words.
   lpush of registers is the VM's fast form (constants and memory operands
   go through scratch registers first). */
static void push_val(FS *fs, const char *list, Val *v) {
    Ty t = vty(fs, v);
    char lst[96];
    snprintf(lst, sizeof lst, "%s", list);
    if (strncmp(lst, "LOC", 3) == 0) {
        emit(fs, "    mov R29, %s", lst);
        snprintf(lst, sizeof lst, "R29");
    }
    switch (v->k) {
    case V_INT:
        emit(fs, "    mov I30, %lld", (long long)v->i);
        emit(fs, "    mov.r R30, null");
        emit(fs, "    lpush %s, R30, I30", lst);
        return;
    case V_FLT:
        emit(fs, "    mov F30, %s", VF(v));
        emit(fs, "    mov.r R30, " FLT);
        emit(fs, "    lpush %s, R30, F30", lst);
        return;
    case V_SLOT:
        break;
    default:
        emit(fs, "    mov.r R30, %s", VR(v));
        emit(fs, "    lpush %s, R30, F30", lst);
        return;
    }
    int s = v->slot;
    const char *ri = SI(s), *rf = SF(s), *rr = SR(s);
    if (s >= NREG) {
        emit(fs, "    mov R30, %s", rr);
        rr = "R30";
        if (t == TY_INT || t == TY_ANY) {
            emit(fs, "    mov I30, %s", ri);
            ri = "I30";
        }
        if (t != TY_INT) {
            emit(fs, "    mov F30, %s", rf);
            rf = "F30";
        }
    }
    if (t == TY_INT) {
        emit(fs, "    lpush %s, %s, %s", lst, rr, ri);
    } else if (t == TY_ANY) {
        int l1 = new_label(fs), l2 = new_label(fs);
        emit(fs, "    bnz %s, .L%d", rr, l1);
        emit(fs, "    lpush %s, %s, %s", lst, rr, ri);
        emit(fs, "    jmp .L%d", l2);
        emit(fs, ".L%d:", l1);
        emit(fs, "    lpush %s, %s, %s", lst, rr, rf);
        emit(fs, ".L%d:", l2);
    } else {
        emit(fs, "    lpush %s, %s, %s", lst, rr, rf);
    }
}

/* ------------------------------------------------------------ descriptions */

/* How a runtime error names the value of e, as a masm operand: a string
   such as "local 'x'", or null. */
static char *describe(FS *fs, Expr *e) {
    char buf[300];
    switch (e->k) {
    case X_LOCAL:
        if (e->var->vatable) {
            snprintf(buf, sizeof buf, "vararg table '%.*s'", e->var->name.len, e->var->name.s);
            break;
        }
        snprintf(buf, sizeof buf, "%s '%.*s'", e->var->fn == fs->f ? "local" : "upvalue",
                 e->var->name.len, e->var->name.s);
        break;
    case X_GLOBAL:
        snprintf(buf, sizeof buf, "global '%.*s'", e->s.len, e->s.s);
        break;
    case X_INDEX:
        if (e->b->k != X_STR)
            return xstrdup("null");
        snprintf(buf, sizeof buf, "field '%.*s'", e->b->s.len > 200 ? 200 : e->b->s.len, e->b->s.s);
        break;
    case X_STR:
        snprintf(buf, sizeof buf, "constant '%.*s'", e->s.len > 200 ? 200 : e->s.len, e->s.s);
        break;
    default:
        return xstrdup("null");
    }
    return masm_string((Str){buf, (int)strlen(buf)});
}

/* ----------------------------------------------------------- declarations */

static Val gen(FS *fs, Expr *e, int dst);
static void gen_branch(FS *fs, Expr *e, int label, bool when);
static void gen_block(FS *fs, Block *b);
static void gen_closure(FS *fs, Func *f, int d, bool self_first);
static int gen_multi_call(FS *fs, Expr *e, int d);
static void gen_function(Func *f, FS *parent);
static bool has_loop(Block *b);
static void pack_scratch(FS *fs);

static bool is_multi(Expr *e) { return e->k == X_CALL || e->k == X_METHOD || e->k == X_VARARG; }

/* --------------------------------------------------------------- captures */

/* The capture words of variable v in f's closures: its offset, or -1. */
/* Words a captured variable takes: a Box, the one half of a variable
   whose type is known, or all three. */
static int cap_width(Var *v) { return v->boxed || v->sty ? 1 : 3; }

static int cap_offset(Func *f, Var *v) {
    int w = 0;
    for (int i = 0; i < f->caps.n; i++) {
        if (f->caps.v[i] == v)
            return w;
        w += cap_width(f->caps.v[i]);
    }
    return -1;
}

/* The half of a value of type t that holds it. */
static char half_of(int t) { return t == TY_INT ? 'I' : t == TY_FLT ? 'F' : 'R'; }

static int up_index(FS *fs, Var *v) {
    for (int i = 0; i < fs->f->caps.n; i++)
        if (fs->f->caps.v[i] == v)
            return i;
    return -1;
}

/* An upvalue word of the running function, as an operand. For packed
   captures this loads the list into R31 first. */
static const char *up_word(FS *fs, int w) {
    char *s = ring();
    if (fs->f->packed) {
        emit(fs, "    mov R31, UP[0]");
        snprintf(s, 96, "R31[%d]", w);
    } else {
        snprintf(s, 96, "UP[%d]", w);
    }
    return s;
}

/* ------------------------------------------------------------- variables */

static bool env_default(Expr *e) { return e->env == NULL; }

static int pinned_index(const char *name, int len) {
    for (int i = 0; i < G.pinned.n; i++)
        if ((int)strlen(G.pinned.v[i]) == len && memcmp(G.pinned.v[i], name, (size_t)len) == 0)
            return i;
    return -1;
}

static void pin_name(Str s) {
    if (pinned_index(s.s, s.len) < 0)
        vec_push(G.pinned, xstrndup(s.s, (size_t)s.len));
}

/* Read a local variable (or upvalue) into a Val. */
static Val read_var(FS *fs, Var *v, int dst) {
    if (v->fn == fs->f) {
        if (!v->boxed)
            return vslot(v->slot);
        int d = dst >= 0 ? dst : alloc_slot(fs);
        emit(fs, "    mov R31, %s", SR(v->slot));
        emit(fs, "    mov %s, R31->Box.i", SI(d));
        emit(fs, "    mov %s, R31->Box.f", SF(d));
        emit(fs, "    mov %s, R31->Box.r", SR(d));
        ty_set(fs, d, TY_ANY);
        return vslot(d);
    }
    int u = up_index(fs, v);
    if (u >= 0 && fs->upslot && fs->upslot[u] >= 0) {
        int k = fs->upslot[u];
        if (!v->boxed)
            return vslot(k);    /* (its type is set at entry) */
        int d = dst >= 0 ? dst : alloc_slot(fs);
        emit(fs, "    mov R31, %s", SR(k));
        emit(fs, "    mov %s, R31->Box.i", SI(d));
        emit(fs, "    mov %s, R31->Box.f", SF(d));
        emit(fs, "    mov %s, R31->Box.r", SR(d));
        ty_set(fs, d, TY_ANY);
        return vslot(d);
    }
    int w = cap_offset(fs->f, v);
    int d = dst >= 0 ? dst : alloc_slot(fs);
    if (v->sty) {
        emit(fs, "    mov %s, %s", slot_op(d, half_of(v->sty)), up_word(fs, w));
        ty_set(fs, d, TY_ANY);
        set_tag(fs, d, (Ty)v->sty);
        ty_set(fs, d, (Ty)v->sty);
        return vslot(d);
    }
    if (v->boxed) {
        emit(fs, "    mov R31, %s", up_word(fs, w));
        emit(fs, "    mov %s, R31->Box.i", SI(d));
        emit(fs, "    mov %s, R31->Box.f", SF(d));
        emit(fs, "    mov %s, R31->Box.r", SR(d));
    } else {
        emit(fs, "    mov %s, %s", SI(d), up_word(fs, w));
        emit(fs, "    mov %s, %s", SF(d), up_word(fs, w + 1));
        emit(fs, "    mov %s, %s", SR(d), up_word(fs, w + 2));
    }
    ty_set(fs, d, TY_ANY);
    return vslot(d);
}

static void box_store(FS *fs, const char *box, Val *val) {
    Ty t = vty(fs, val);
    char b[96];
    snprintf(b, sizeof b, "%s", box);
    if (val->k == V_SLOT) {
        emit(fs, "    mov %s->Box.i, %s", b, SI(val->slot));
        emit(fs, "    mov %s->Box.f, %s", b, SF(val->slot));
        emit(fs, "    mov %s->Box.r, %s", b, SR(val->slot));
        return;
    }
    (void)t;
    if (val->k == V_INT)
        emit(fs, "    mov %s->Box.i, %s", b, VI(val));
    if (val->k == V_FLT)
        emit(fs, "    mov %s->Box.f, %s", b, VF(val));
    emit(fs, "    mov.r %s->Box.r, %s", b, VR(val));
}

static void write_var(FS *fs, Var *v, Val val) {
    if (v->fn == fs->f) {
        if (!v->boxed) {
            put(fs, v->slot, val);
            return;
        }
        emit(fs, "    mov R31, %s", SR(v->slot));
        box_store(fs, "R31", &val);
        return;
    }
    int u = up_index(fs, v);
    if (u >= 0 && fs->upslot && fs->upslot[u] >= 0) {
        emit(fs, "    mov R31, %s", SR(fs->upslot[u]));
        box_store(fs, "R31", &val);
        return;
    }
    int w = cap_offset(fs->f, v);
    emit(fs, "    mov R31, %s", up_word(fs, w));
    box_store(fs, "R31", &val);
}

/* Give a newly declared variable its slot and value. */
static void define_var(FS *fs, Var *v, int slot) {
    v->slot = slot;
    if (v->boxed) {
        emit(fs, "    new R31, Box");
        Val val = vslot(slot);
        box_store(fs, "R31", &val);
        emit(fs, "    mov %s, R31", SR(slot));
        ty_set(fs, slot, TY_ANY);
    }
}

/* ------------------------------------------------------------- globals */

static Val gen_index_val(FS *fs, Val t, Val k, int dst, const char *desc);
static void gen_setindex(FS *fs, Val t, Val k, Val v, const char *desc);

static void env_val(FS *fs, int d) {
    emit(fs, "    mov %s, @rt$env", SR(d));
    ty_set(fs, d, TY_ANY);
    emit(fs, "    mov %s, 0", SI(d));
}

static Val read_global(FS *fs, Expr *e, int dst) {
    char *desc = describe(fs, e);
    if (!env_default(e)) {
        Val env = gen(fs, e->env, -1);
        Val r = gen_index_val(fs, env, vstr(e->s), dst, desc);
        free(desc);
        return r;
    }
    int d = dst >= 0 ? dst : alloc_slot(fs);
    if (!G.env_fast) {
        int ek = alloc_slot(fs);
        env_val(fs, ek);
        ty_set(fs, ek, TY_ANY);
        Val r = gen_index_val(fs, vslot(ek), vstr(e->s), d, desc);
        free(desc);
        return r;
    }
    int j = pinned_index(e->s.s, e->s.len);
    int w = 4 * j + 2;
    int miss = new_label(fs);
    emit(fs, "    mov R31, @rt$Ghs");
    emit(fs, "    mov %s, R31[%d]", SR(d), w);
    emit(fs, "    ldv %s, %s, R31[%d]", SI(d), SF(d), w + 1);
    ty_set(fs, d, TY_ANY);
    branch(fs, miss, "beq.r %s, " NIL, SR(d));
    int cont = new_label(fs);
    cold_begin(fs);
    place(fs, miss);
    char *name = masm_string(e->s);
    emit(fs, "    call rt$gget (%s) -> (%s, %s, %s)", name, SI(d), SF(d), SR(d));
    free(name);
    jump(fs, cont);
    cold_end(fs);
    place(fs, cont);
    free(desc);
    return vslot(d);
}

static void write_global(FS *fs, Expr *e, Val v) {
    if (!env_default(e)) {
        Val env = gen(fs, e->env, -1);
        char *desc = describe(fs, e);
        gen_setindex(fs, env, vstr(e->s), v, desc);
        free(desc);
        return;
    }
    if (!G.env_fast) {
        int ek = alloc_slot(fs);
        env_val(fs, ek);
        char *desc = describe(fs, e);
        gen_setindex(fs, vslot(ek), vstr(e->s), v, desc);
        free(desc);
        return;
    }
    int j = pinned_index(e->s.s, e->s.len);
    int w = 4 * j + 2;
    int slow = new_label(fs), done = new_label(fs);
    emit(fs, "    mov R31, @rt$G");
    emit(fs, "    mov R31, R31->Tab.meta");
    branch(fs, slow, "bnz R31");
    emit(fs, "    mov R31, @rt$Ghs");
    emit(fs, "    mov I31, %d", w);
    Ty t = vty(fs, &v);
    if (v.k == V_SLOT) {
        emit(fs, "    mov R31[I31], %s", SR(v.slot));
        emit(fs, "    inc I31");
        if (t == TY_INT) {
            emit(fs, "    mov R31[I31], %s", SI(v.slot));
        } else if (t == TY_ANY) {
            int l1 = new_label(fs), l2 = new_label(fs);
            emit(fs, "    bnz %s, .L%d", SR(v.slot), l1);
            emit(fs, "    mov R31[I31], %s", SI(v.slot));
            emit(fs, "    jmp .L%d", l2);
            emit(fs, ".L%d:", l1);
            emit(fs, "    mov R31[I31], %s", SF(v.slot));
            emit(fs, ".L%d:", l2);
        } else {
            emit(fs, "    mov R31[I31], %s", SF(v.slot));
        }
    } else {
        const char *r = VR(&v);
        emit(fs, "    mov.r R31[I31], %s", r);
        emit(fs, "    inc I31");
        if (v.k == V_INT)
            emit(fs, "    mov.i64 R31[I31], %s", VI(&v));
        else
            emit(fs, "    mov.f64 R31[I31], %s", VF(&v));
    }
    emit(fs, ".L%d:", done);
    int cont = new_label(fs);
    cold_begin(fs);
    place(fs, slow);
    int vs = v.k == V_SLOT ? v.slot : -1;
    char *name = masm_string(e->s);
    if (vs >= 0) {
        emit(fs, "    call rt$gset (%s, %s, %s, %s)", name, SI(vs), SF(vs), SR(vs));
    } else {
        emit(fs, "    call rt$gset (%s, %s, %s, %s)", name, VI(&v), VF(&v), VR(&v));
    }
    free(name);
    jump(fs, cont);
    cold_end(fs);
    place(fs, cont);
}

/* ------------------------------------------------------- table access */

/* Find constant string key k in the hash part of the table in slot ts:
   leave its hash slots in R31 and the index of its value word in I31, or
   branch to notfound if it is not there. The table is in register t
   (not R29..R31). Each site has an inline cache of
   the shape it saw last time and the word it found the key at (globals
   ic$<site>s and ic$<site>w): a table of that shape has the key there, so
   a hit costs one compare. Tables with a map of their own (more than 16
   slots, as a library's) miss, and then use the cached word if it holds
   this very key, before looking the key up. */
static void ic_lookup(FS *fs, const char *t, Str k, int notfound) {
    int site = G.nic++;
    int miss = new_label(fs), load = new_label(fs), look = new_label(fs), own = new_label(fs),
        set = new_label(fs);
    char *ks = masm_string(k);
    emit(fs, "    mov R31, %s->Tab.hm", t);
    branch(fs, miss, "bne.r R31, @ic$%ds", site);
    emit(fs, "    mov R31, %s->Tab.hs", t);
    emit(fs, "    mov I31, @ic$%dw", site);
    emit(fs, ".L%d:", load);
    cold_begin(fs);
    emit(fs, ".L%d:", miss);
    emit(fs, "    mov I29, @ic$%dw", site);
    emit(fs, "    mov I28, %s->Tab.hw", t);
    emit(fs, "    bge I29, I28, .L%d", look);
    emit(fs, "    mov R30, %s->Tab.hs", t);
    emit(fs, "    sub I28, I29, 2");
    emit(fs, "    mov R29, R30[I28]");
    emit(fs, "    bne.r R29, %s, .L%d", ks, look);
    emit(fs, "    mov I31, I29");
    emit(fs, "    mov R31, R30");
    emit(fs, "    jmp .L%d", load);
    emit(fs, ".L%d:", look);
    emit(fs, "    bz R31, .L%d", notfound);
    emit(fs, "    mget I31, R31, %s, -1", ks);
    emit(fs, "    blt I31, 0, .L%d", notfound);
    emit(fs, "    mov @ic$%dw, I31", site);
    emit(fs, "    mov I30, %s->Tab.hmshared", t);
    emit(fs, "    bz I30, .L%d", own);
    emit(fs, "    mov.r @ic$%ds, R31", site);
    emit(fs, "    jmp .L%d", set);
    emit(fs, ".L%d:", own);
    emit(fs, "    mov.r @ic$%ds, " NIL, site);
    emit(fs, ".L%d:", set);
    emit(fs, "    mov R31, %s->Tab.hs", t);
    emit(fs, "    jmp .L%d", load);
    cold_end(fs);
    record(fs, notfound);
    free(ks);
}

/* t[k] for Vals: inline paths for a table with an integer or constant
   string key, else rt$index. */
static Val gen_index_val(FS *fs, Val t, Val k, int dst, const char *desc) {
    int ts = in_slot(fs, t);
    Ty tt = ty_get(fs, ts), kt = vty(fs, &k);
    int d = dst;
    if (d < 0 || d == ts || (k.k == V_SLOT && d == k.slot))
        d = alloc_slot(fs);
    int slow = new_label(fs), cont = new_label(fs);
    bool inl = false;
    if (((kt == TY_INT || (kt == TY_ANY && k.k == V_SLOT)) && (k.k != V_INT || (k.i >= 1 && k.i < (1ll << 40)))) || (k.k == V_STR && !(k.s.len >= 2 && k.s.s[0] == '_' && k.s.s[1] == '_'))) {
        inl = true;
        
        if (tt != TY_TAB) {
            emit(fs, "    isa I31, %s, Tab", SR(ts));
            branch(fs, slow, "bz I31");
        }
        if (kt == TY_INT || kt == TY_ANY) {
            if (kt == TY_ANY)
                branch(fs, slow, "bnz %s", SR(k.slot));
            emit(fs, "    mov I30, %s->Tab.n", RB(fs, ts, "R28"));
            if (k.k == V_INT)
                emit(fs, "    mov I31, %lld", (long long)(k.i - 1));
            else
                emit(fs, "    sub I31, %s, 1", VI(&k));
            /* 0 <= k - 1 < n, as one unsigned compare */
            branch(fs, slow, "bge.u64 I31, I30");
            emit(fs, "    add I31, I31");
            emit(fs, "    mov R31, %s->Tab.arr", RB(fs, ts, "R28"));
        } else {
            ic_lookup(fs, RB(fs, ts, "R28"), k.s, slow);
        }
        emit(fs, "    mov %s, R31[I31]", SR(d));
        emit(fs, "    inc I31");
        emit(fs, "    ldv %s, %s, R31[I31]", SI(d), SF(d));
        ty_set(fs, d, TY_ANY);
        branch(fs, slow, "beq.r %s, " NIL, SR(d));
        cold_begin(fs);
        place(fs, slow);
    }
    const char *kr = VR(&k);
    emit(fs, "    call rt$index (%s, %s, %s, %s, %s, %s, %s) -> (%s, %s, %s)", SI(ts), SF(ts), SR(ts),
         VI(&k), VF(&k), kr, desc, SI(d), SF(d), SR(d));
    ty_set(fs, d, TY_ANY);
    if (inl) {
        jump(fs, cont);
        cold_end(fs);
        place(fs, cont);
        ty_set(fs, d, TY_ANY);
    }
    return vslot(d);
}

static void gen_setindex(FS *fs, Val t, Val k, Val v, const char *desc) {
    int ts = in_slot(fs, t);
    Ty tt = ty_get(fs, ts), kt = vty(fs, &k), vt = vty(fs, &v);
    int slow = new_label(fs), cont = new_label(fs);
    bool inl = false;
    bool knil = vt == TY_NIL;
    if (!knil && (((kt == TY_INT || (kt == TY_ANY && k.k == V_SLOT)) && (k.k != V_INT || (k.i >= 1 && k.i < (1ll << 40)))) || (k.k == V_STR && !(k.s.len >= 2 && k.s.s[0] == '_' && k.s.s[1] == '_')))) {
        inl = true;
        if (tt != TY_TAB) {
            emit(fs, "    isa I31, %s, Tab", SR(ts));
            branch(fs, slow, "bz I31");
        }
        if (kt == TY_INT || kt == TY_ANY) {
            if (kt == TY_ANY)
                branch(fs, slow, "bnz %s", SR(k.slot));
            emit(fs, "    mov I30, %s->Tab.n", RB(fs, ts, "R28"));
            if (k.k == V_INT)
                emit(fs, "    mov I31, %lld", (long long)(k.i - 1));
            else
                emit(fs, "    sub I31, %s, 1", VI(&k));
            /* 0 <= k - 1 < n, as one unsigned compare */
            branch(fs, slow, "bge.u64 I31, I30");
            emit(fs, "    add I31, I31");
            emit(fs, "    mov R31, %s->Tab.arr", RB(fs, ts, "R28"));
        } else {
            ic_lookup(fs, RB(fs, ts, "R28"), k.s, slow);
        }
        /* only an existing non-nil value is overwritten inline */
        emit(fs, "    mov R30, R31[I31]");
        branch(fs, slow, "beq.r R30, " NIL);
        if (v.k == V_SLOT && vt != TY_INT && vt != TY_FLT && vt != TY_STR && vt != TY_TAB &&
            vt != TY_FN)
            branch(fs, slow, "beq.r %s, " NIL, SR(v.slot));
        if (v.k == V_SLOT) {
            emit(fs, "    mov R31[I31], %s", SR(v.slot));
            emit(fs, "    inc I31");
            if (vt == TY_INT) {
                emit(fs, "    mov R31[I31], %s", SI(v.slot));
            } else if (vt == TY_ANY) {
                int l1 = new_label(fs), l2 = new_label(fs);
                emit(fs, "    bnz %s, .L%d", SR(v.slot), l1);
                emit(fs, "    mov R31[I31], %s", SI(v.slot));
                emit(fs, "    jmp .L%d", l2);
                emit(fs, ".L%d:", l1);
                emit(fs, "    mov R31[I31], %s", SF(v.slot));
                emit(fs, ".L%d:", l2);
            } else {
                emit(fs, "    mov R31[I31], %s", SF(v.slot));
            }
        } else {
            emit(fs, "    mov.r R31[I31], %s", VR(&v));
            emit(fs, "    inc I31");
            if (v.k == V_INT)
                emit(fs, "    mov.i64 R31[I31], %s", VI(&v));
            else
                emit(fs, "    mov.f64 R31[I31], %s", VF(&v));
        }
        cold_begin(fs);
        place(fs, slow);
    }
    const char *kr = VR(&k);
    const char *vr = VR(&v);
    emit(fs, "    call rt$setindex (%s, %s, %s, %s, %s, %s, %s, %s, %s, %s)", SI(ts), SF(ts), SR(ts),
         VI(&k), VF(&k), kr, VI(&v), VF(&v), vr, desc);
    if (inl) {
        jump(fs, cont);
        cold_end(fs);
        place(fs, cont);
    }
}

/* ------------------------------------------------------------ arithmetic */

enum { A_ADD, A_SUB, A_MUL, A_DIV, A_MOD, A_POW, A_IDIV, A_BAND, A_BOR, A_BXOR, A_SHL, A_SHR,
       A_UNM, A_BNOT };

static bool is_bitwise(int op) { return (op >= A_BAND && op <= A_SHR) || op == A_BNOT; }

static int64_t shift_left(int64_t x, int64_t n) {
    if (n <= -64 || n >= 64)
        return 0;
    if (n >= 0)
        return (int64_t)((uint64_t)x << n);
    return (int64_t)((uint64_t)x >> -n);
}

static bool flt_to_int(double d, int64_t *out) {
    if (d != floor(d) || !(d >= -9223372036854775808.0 && d < 9223372036854775808.0))
        return false;
    *out = (int64_t)d;
    return true;
}

static double lua_fmod(double a, double b) {
    double m = fmod(a, b);
    if ((m > 0) ? b < 0 : (m < 0 && b != m))
        m += b;
    return m;
}

/* Fold op on two numeric constants. */
static bool fold(int op, Val *a, Val *b, Val *out) {
    if ((a->k != V_INT && a->k != V_FLT) || (b->k != V_INT && b->k != V_FLT))
        return false;
    if (is_bitwise(op)) {
        int64_t x, y;
        if (a->k == V_INT) x = a->i; else if (!flt_to_int(a->f, &x)) return false;
        if (b->k == V_INT) y = b->i; else if (!flt_to_int(b->f, &y)) return false;
        switch (op) {
        case A_BAND: *out = vint(x & y); return true;
        case A_BOR: *out = vint(x | y); return true;
        case A_BXOR: *out = vint(x ^ y); return true;
        case A_SHL: *out = vint(shift_left(x, y)); return true;
        case A_SHR: *out = vint(y == INT64_MIN ? 0 : shift_left(x, -y)); return true;
        case A_BNOT: *out = vint(~x); return true;
        }
        return false;
    }
    if (a->k == V_INT && b->k == V_INT && op != A_DIV && op != A_POW) {
        uint64_t x = (uint64_t)a->i, y = (uint64_t)b->i;
        switch (op) {
        case A_ADD: *out = vint((int64_t)(x + y)); return true;
        case A_SUB: *out = vint((int64_t)(x - y)); return true;
        case A_MUL: *out = vint((int64_t)(x * y)); return true;
        case A_UNM: *out = vint((int64_t)(0 - x)); return true;
        case A_IDIV: case A_MOD: {
            int64_t p = a->i, q = b->i;
            if (q == 0)
                return false;
            if (q == -1) {
                *out = vint(op == A_MOD ? 0 : (int64_t)(0 - (uint64_t)p));
                return true;
            }
            int64_t d = p / q, r = p % q;
            if (r != 0 && ((r ^ q) < 0)) {
                d--;
                r += q;
            }
            *out = vint(op == A_MOD ? r : d);
            return true;
        }
        }
        return false;
    }
    double x = a->k == V_INT ? (double)a->i : a->f, y = b->k == V_INT ? (double)b->i : b->f;
    switch (op) {
    case A_ADD: *out = vflt(x + y); return true;
    case A_SUB: *out = vflt(x - y); return true;
    case A_MUL: *out = vflt(x * y); return true;
    case A_DIV: *out = vflt(x / y); return true;
    case A_POW: *out = vflt(pow(x, y)); return true;
    case A_UNM: *out = vflt(-x); return true;
    case A_IDIV: *out = vflt(floor(x / y)); return true;
    case A_MOD:
        if (isinf(y) && isfinite(x))
            *out = vflt((x >= 0) == (y > 0) ? x : y);
        else
            *out = vflt(lua_fmod(x, y));
        return true;
    }
    return false;
}

/* An integer operation on I operands x and y into slot d. */
static bool is_literal(const char *op);

static void int_op(FS *fs, int op, int d, const char *x, const char *y, Val *b) {
    int l;
    /* commutative: the register operand first */
    if ((op == A_ADD || op == A_MUL || op == A_BAND || op == A_BOR || op == A_BXOR) && is_literal(x) &&
        !is_literal(y)) {
        const char *t = x;
        x = y;
        y = t;
    }
    switch (op) {
    case A_ADD: emit(fs, "    add %s, %s, %s", SI(d), x, y); break;
    case A_SUB: emit(fs, "    sub %s, %s, %s", SI(d), x, y); break;
    case A_MUL: emit(fs, "    mul %s, %s, %s", SI(d), x, y); break;
    case A_BAND: emit(fs, "    and %s, %s, %s", SI(d), x, y); break;
    case A_BOR: emit(fs, "    or %s, %s, %s", SI(d), x, y); break;
    case A_BXOR: emit(fs, "    xor %s, %s, %s", SI(d), x, y); break;
    case A_UNM: emit(fs, "    neg %s, %s", SI(d), x); break;
    case A_BNOT: emit(fs, "    xor %s, %s, -1", SI(d), x); break;
    case A_IDIV:
        l = new_label(fs);
        emit(fs, "    div I30, %s, %s", x, y);
        emit(fs, "    rem I29, %s, %s", x, y);
        emit(fs, "    bz I29, .L%d", l);
        emit(fs, "    xor I28, %s, %s", x, y);
        emit(fs, "    bge I28, 0, .L%d", l);
        emit(fs, "    dec I30");
        emit(fs, ".L%d:", l);
        emit(fs, "    mov %s, I30", SI(d));
        break;
    case A_MOD:
        l = new_label(fs);
        emit(fs, "    rem I30, %s, %s", x, y);
        emit(fs, "    bz I30, .L%d", l);
        emit(fs, "    xor I29, I30, %s", y);
        emit(fs, "    bge I29, 0, .L%d", l);
        emit(fs, "    add I30, %s", y);
        emit(fs, ".L%d:", l);
        emit(fs, "    mov %s, I30", SI(d));
        break;
    case A_SHL: case A_SHR:
        if (b && b->k == V_INT) {
            int64_t n = op == A_SHL ? b->i : (b->i == INT64_MIN ? 64 : -b->i);
            if (n <= -64 || n >= 64)
                emit(fs, "    mov %s, 0", SI(d));
            else if (n >= 0)
                emit(fs, "    shl %s, %s, %lld", SI(d), x, (long long)n);
            else
                emit(fs, "    shr %s, %s, %lld", SI(d), x, (long long)-n);
        } else {
            emit(fs, "    call rt$shl (%s, %s, %d) -> (%s)", x, y, op == A_SHR, SI(d));
        }
        break;
    }
    set_tag(fs, d, TY_INT);
}

static void flt_op(FS *fs, int op, int d, const char *x, const char *y) {
    switch (op) {
    case A_ADD: emit(fs, "    add.f64 %s, %s, %s", SF(d), x, y); break;
    case A_SUB: emit(fs, "    sub.f64 %s, %s, %s", SF(d), x, y); break;
    case A_MUL: emit(fs, "    mul.f64 %s, %s, %s", SF(d), x, y); break;
    case A_DIV: emit(fs, "    div.f64 %s, %s, %s", SF(d), x, y); break;
    case A_POW: emit(fs, "    ncall \"math.pow\" (%s, %s) -> (%s)", x, y, SF(d)); break;
    case A_MOD: emit(fs, "    call rt$fmod (%s, %s) -> (%s)", x, y, SF(d)); break;
    case A_IDIV:
        emit(fs, "    div.f64 F30, %s, %s", x, y);
        emit(fs, "    floor.f64 %s, F30", SF(d));
        break;
    case A_UNM: emit(fs, "    neg.f64 %s, %s", SF(d), x); break;
    }
    set_tag(fs, d, TY_FLT);
}

/* The F operand of a numeric Val, converting an integer into scratch. */
static const char *as_float(FS *fs, Val *v, const char *scratch) {
    if (vty(fs, v) == TY_FLT)
        return VF(v);
    if (v->k == V_INT) {
        char *s = ring();
        char *f = masm_float((double)v->i);
        snprintf(s, 96, "%s", f);
        free(f);
        return s;
    }
    emit(fs, "    cvt.f64.i64 %s, %s", scratch, VI(v));
    return scratch;
}

static bool numeric(Ty t) { return t == TY_INT || t == TY_FLT; }

static void call_arith(FS *fs, int op, Val *a, Val *b, int d, const char *da, const char *db) {
    const char *ar = VR(a), *br = VR(b);
    emit(fs, "    call rt$arith (%d, %s, %s, %s, %s, %s, %s, %s, %s) -> (%s, %s, %s)", op, VI(a), VF(a), ar,
         VI(b), VF(b), br, da, db, SI(d), SF(d), SR(d));
    ty_set(fs, d, TY_ANY);
}

static Val gen_arith(FS *fs, int op, Val a, Val b, int dst, const char *da, const char *db) {
    Val folded;
    if (fold(op, &a, &b, &folded))
        return folded;
    Ty ta = vty(fs, &a), tb = vty(fs, &b);
    int d = dst >= 0 ? dst : alloc_slot(fs);
    bool zero_div = (op == A_IDIV || op == A_MOD) && b.k == V_INT && b.i == 0;
    if (numeric(ta) && numeric(tb) && !zero_div) {
        if (ta == TY_INT && tb == TY_INT && op != A_DIV && op != A_POW) {
            if (op == A_IDIV || op == A_MOD) {
                if (b.k != V_INT) {
                    int slow = new_label(fs), cont = new_label(fs);
                    branch(fs, slow, "bz %s", VI(&b));
                    int_op(fs, op, d, VI(&a), VI(&b), &b);
                    cold_begin(fs);
                    place(fs, slow);
                    call_arith(fs, op, &a, &b, d, da, db);
                    jump(fs, cont);
                    cold_end(fs);
                    place(fs, cont);
                    ty_set(fs, d, TY_INT);
                    return vslot(d);
                }
            }
            int_op(fs, op, d, VI(&a), VI(&b), &b);
            return vslot(d);
        }
        if (!is_bitwise(op)) {
            const char *x = as_float(fs, &a, "F30"), *y = as_float(fs, &b, "F29");
            flt_op(fs, op, d, x, y);
            return vslot(d);
        }
    }
    if (!(ta == TY_ANY || numeric(ta)) || !(tb == TY_ANY || numeric(tb)) || zero_div) {
        call_arith(fs, op, &a, &b, d, da, db);
        return vslot(d);
    }
    /* dynamic: an inline path for integers (or for floats, when an operand
       is known to be a float or the result always is), and in cold code a
       second chance that converts integers to floats, then rt$arith */
    int slow = new_label(fs), cont = new_label(fs), gen = new_label(fs);
    bool fpref = ta == TY_FLT || tb == TY_FLT || op == A_DIV || op == A_POW;
    if (is_bitwise(op))
        fpref = false;
    if (!fpref) {
        if (ta != TY_INT)
            branch(fs, slow, "bnz %s", VR(&a));
        if (tb != TY_INT)
            branch(fs, slow, "bnz %s", VR(&b));
        if ((op == A_IDIV || op == A_MOD) && b.k != V_INT)
            branch(fs, gen, "bz %s", VI(&b));     /* rt$arith reports it */
        int_op(fs, op, d, VI(&a), VI(&b), &b);
    } else {
        const char *x = ta == TY_ANY ? VF(&a) : as_float(fs, &a, "F30");
        const char *y = tb == TY_ANY ? VF(&b) : as_float(fs, &b, "F29");
        if (ta == TY_ANY)
            branch(fs, slow, "bne.r %s, " FLT, VR(&a));
        if (tb == TY_ANY)
            branch(fs, slow, "bne.r %s, " FLT, VR(&b));
        flt_op(fs, op, d, x, y);
    }
    cold_begin(fs);
    place(fs, slow);
    if (!is_bitwise(op)) {
        if (!fpref && ta == TY_ANY && tb == TY_ANY) {
            /* two floats, first: the common case that is not two integers */
            int mixed = new_label(fs);
            branch(fs, mixed, "bne.r %s, " FLT, VR(&a));
            branch(fs, mixed, "bne.r %s, " FLT, VR(&b));
            flt_op(fs, op, d, VF(&a), VF(&b));
            ty_set(fs, d, TY_ANY);
            jump(fs, cont);
            place(fs, mixed);
        }
        /* both numbers: as floats */
        const char *xs[2] = {"F30", "F29"};
        Val *vs[2] = {&a, &b};
        Ty ts[2] = {ta, tb};
        for (int k = 0; k < 2; k++) {
            if (ts[k] == TY_FLT) {
                emit(fs, "    mov %s, %s", xs[k], VF(vs[k]));
            } else if (ts[k] == TY_INT) {
                emit(fs, "    cvt.f64.i64 %s, %s", xs[k], VI(vs[k]));
            } else {
                int isf = new_label(fs), done = new_label(fs);
                branch(fs, isf, "beq.r %s, " FLT, VR(vs[k]));
                branch(fs, gen, "bnz %s", VR(vs[k]));
                emit(fs, "    cvt.f64.i64 %s, %s", xs[k], VI(vs[k]));
                emit(fs, "    jmp .L%d", done);
                place(fs, isf);
                emit(fs, "    mov %s, %s", xs[k], VF(vs[k]));
                emit(fs, ".L%d:", done);
            }
        }
        ty_set(fs, d, TY_ANY);
        flt_op(fs, op, d, "F30", "F29");
        jump(fs, cont);
    }
    place(fs, gen);
    call_arith(fs, op, &a, &b, d, da, db);
    jump(fs, cont);
    cold_end(fs);
    place(fs, cont);
    return vslot(d);
}

/* ---------------------------------------------------------- comparisons */

static bool truthy_const(Val *v, bool *out) {
    switch (v->k) {
    case V_NIL: case V_FALSE: *out = false; return true;
    case V_SLOT: return false;
    default: *out = true; return true;
    }
}

/* Jump to label if the value's truthiness is `when`. */
static void branch_value(FS *fs, Val *v, int label, bool when) {
    bool c;
    if (truthy_const(v, &c)) {
        if (c == when)
            jump(fs, label);
        return;
    }
    Ty t = vty(fs, v);
    const char *r = SR(v->slot);
    if (t == TY_INT || t == TY_FLT || t == TY_STR || t == TY_TAB || t == TY_FN) {
        if (when)
            jump(fs, label);
        return;
    }
    if (t == TY_NIL) {
        if (!when)
            jump(fs, label);
        return;
    }
    if (when) {
        if (t == TY_BOOL) {
            branch(fs, label, "beq.r %s, " TRUE, r);
            return;
        }
        int skip = new_label(fs);
        branch(fs, skip, "beq.r %s, " NIL, r);
        branch(fs, label, "bne.r %s, " FALSE, r);
        place(fs, skip);
    } else {
        if (t != TY_BOOL)
            branch(fs, label, "beq.r %s, " NIL, r);
        branch(fs, label, "beq.r %s, " FALSE, r);
    }
}

static bool is_literal(const char *op) {
    return isdigit((unsigned char)op[0]) || (op[0] == '-' && isdigit((unsigned char)op[1]));
}

/* An integer compare-and-branch, with the register operand first (the
   VM's fast form): a constant on the left is moved to the right. */
static void int_cmp(FS *fs, int label, const char *mn, const char *x, const char *y) {
    if (is_literal(x) && !is_literal(y)) {
        const char *m = strcmp(mn, "blt") == 0 ? "bgt" : strcmp(mn, "bgt") == 0 ? "blt" :
                        strcmp(mn, "ble") == 0 ? "bge" : strcmp(mn, "bge") == 0 ? "ble" : mn;
        branch(fs, label, "%s %s, %s", m, y, x);
        return;
    }
    branch(fs, label, "%s %s, %s", mn, x, y);
}

static const char *int_branch(int op, bool when) {
    /* op: OP_LT, OP_LE, OP_EQ, OP_NE */
    switch (op) {
    case OP_LT: return when ? "blt" : "bge";
    case OP_LE: return when ? "ble" : "bgt";
    case OP_EQ: return when ? "beq" : "bne";
    default: return when ? "bne" : "beq";
    }
}

/* Jump to label when (a op b) == when, op one of OP_LT, OP_LE, OP_EQ, OP_NE. */
static void compare_branch(FS *fs, int op, Val a, Val b, int label, bool when) {
    Ty ta = vty(fs, &a), tb = vty(fs, &b);
    if (op == OP_NE) {
        op = OP_EQ;
        when = !when;
    }
    if (op == OP_EQ) {
        /* constants */
        if (is_const(&a) && is_const(&b)) {
            bool eq;
            if (a.k == V_INT && b.k == V_INT) eq = a.i == b.i;
            else if ((a.k == V_INT || a.k == V_FLT) && (b.k == V_INT || b.k == V_FLT))
                eq = (a.k == V_INT ? (double)a.i : a.f) == (b.k == V_INT ? (double)b.i : b.f);
            else if (a.k == V_STR && b.k == V_STR) eq = str_same(a.s, b.s);
            else eq = a.k == b.k && a.k != V_INT && a.k != V_FLT && a.k != V_STR;
            if (eq == when)
                jump(fs, label);
            return;
        }
        if (is_const(&a)) {
            Val t = a;
            a = b;
            b = t;
            Ty tt = ta;
            ta = tb;
            tb = tt;
        }
        if (b.k == V_NIL || b.k == V_TRUE || b.k == V_FALSE) {
            branch(fs, label, "%s.r %s, %s", when ? "beq" : "bne", SR(a.slot), VR(&b));
            return;
        }
        if (ta == TY_INT && tb == TY_INT) {
            int_cmp(fs, label, int_branch(op, when), VI(&a), VI(&b));
            return;
        }
        if (ta == TY_FLT && tb == TY_FLT) {
            branch(fs, label, "%s.f64 %s, %s", when ? "beq" : "bne", VF(&a), VF(&b));
            return;
        }
        if (b.k == V_STR && ta != TY_INT && ta != TY_FLT) {
            const char *s = VR(&b);
            int cont = new_label(fs);
            if (when) {
                branch(fs, label, "beq.r %s, %s", SR(a.slot), s);
                if (ta != TY_STR) {
                    emit(fs, "    typeof I31, %s", SR(a.slot));
                    branch(fs, cont, "bne I31, 1");
                }
                emit(fs, "    strcmp I31, %s, %s", SR(a.slot), s);
                branch(fs, label, "bz I31");
            } else {
                branch(fs, cont, "beq.r %s, %s", SR(a.slot), s);
                if (ta != TY_STR) {
                    emit(fs, "    typeof I31, %s", SR(a.slot));
                    branch(fs, label, "bne I31, 1");
                }
                emit(fs, "    strcmp I31, %s, %s", SR(a.slot), s);
                branch(fs, label, "bnz I31");
            }
            place(fs, cont);
            return;
        }
        int slow = new_label(fs), cont = new_label(fs);
        bool inl = false;
        if ((ta == TY_INT || ta == TY_ANY) && (tb == TY_INT || tb == TY_ANY)) {
            inl = true;
            if (ta != TY_INT)
                branch(fs, slow, "bnz %s", VR(&a));
            if (tb != TY_INT)
                branch(fs, slow, "bnz %s", VR(&b));
            int_cmp(fs, label, int_branch(op, when), VI(&a), VI(&b));
            jump(fs, cont);
            cold_begin(fs);
            place(fs, slow);
        }
        const char *ar = VR(&a), *br = VR(&b);
        emit(fs, "    call rt$eq (%s, %s, %s, %s, %s, %s) -> (I31)", VI(&a), VF(&a), ar, VI(&b), VF(&b), br);
        branch(fs, label, "%s I31", when ? "bnz" : "bz");
        jump(fs, cont);
        if (inl)
            cold_end(fs);
        place(fs, cont);
        return;
    }
    /* order */
    if (ta == TY_INT && tb == TY_INT) {
        int_cmp(fs, label, int_branch(op, when), VI(&a), VI(&b));
        return;
    }
    if (ta == TY_FLT && tb == TY_FLT) {
        if (when) {
            branch(fs, label, "%s.f64 %s, %s", op == OP_LT ? "blt" : "ble", VF(&a), VF(&b));
        } else {
            int skip = new_label(fs);
            branch(fs, skip, "%s.f64 %s, %s", op == OP_LT ? "blt" : "ble", VF(&a), VF(&b));
            jump(fs, label);
            place(fs, skip);
        }
        return;
    }
    if (ta == TY_STR && tb == TY_STR) {
        const char *ar = VR(&a), *br = VR(&b);
        emit(fs, "    strcmp I31, %s, %s", ar, br);
        branch(fs, label, "%s I31, 0", int_branch(op, when));
        return;
    }
    int slow = new_label(fs), cont = new_label(fs);
    bool inl = false;
    if ((ta == TY_INT || ta == TY_ANY) && (tb == TY_INT || tb == TY_ANY)) {
        inl = true;
        if (ta != TY_INT)
            branch(fs, slow, "bnz %s", VR(&a));
        if (tb != TY_INT)
            branch(fs, slow, "bnz %s", VR(&b));
        int_cmp(fs, label, int_branch(op, when), VI(&a), VI(&b));
        jump(fs, cont);
        cold_begin(fs);
        place(fs, slow);
    }
    const char *ar = VR(&a), *br = VR(&b);
    emit(fs, "    call rt$%s (%s, %s, %s, %s, %s, %s) -> (I31)", op == OP_LT ? "lt" : "le", VI(&a), VF(&a),
         ar, VI(&b), VF(&b), br);
    branch(fs, label, "%s I31", when ? "bnz" : "bz");
    jump(fs, cont);
    if (inl)
        cold_end(fs);
    place(fs, cont);
}

static bool is_compare(int op) { return op >= OP_EQ && op <= OP_GE; }

static void gen_compare(FS *fs, Expr *e, int label, bool when) {
    int mark = fs->nslots;
    Val a = gen(fs, e->a, -1);
    Val b = gen(fs, e->b, -1);
    int op = e->op;
    if (op == OP_GT) { Val t = a; a = b; b = t; op = OP_LT; }
    else if (op == OP_GE) { Val t = a; a = b; b = t; op = OP_LE; }
    compare_branch(fs, op, a, b, label, when);
    fs->nslots = mark;
}

static void gen_branch(FS *fs, Expr *e, int label, bool when) {
    switch (e->k) {
    case X_NIL: case X_FALSE:
        if (!when) jump(fs, label);
        return;
    case X_TRUE: case X_INT: case X_FLT: case X_STR: case X_FUNC: case X_TABLE:
        if (e->k == X_TABLE || e->k == X_FUNC) {
            int mark = fs->nslots;
            gen(fs, e, -1);
            fs->nslots = mark;
        }
        if (when) jump(fs, label);
        return;
    case X_UN:
        if (e->op == OP_NOT) {
            gen_branch(fs, e->a, label, !when);
            return;
        }
        break;
    case X_AND:
        if (!when) {
            gen_branch(fs, e->a, label, false);
            gen_branch(fs, e->b, label, false);
        } else {
            int skip = new_label(fs);
            gen_branch(fs, e->a, skip, false);
            gen_branch(fs, e->b, label, true);
            place(fs, skip);
        }
        return;
    case X_OR:
        if (when) {
            gen_branch(fs, e->a, label, true);
            gen_branch(fs, e->b, label, true);
        } else {
            int skip = new_label(fs);
            gen_branch(fs, e->a, skip, true);
            gen_branch(fs, e->b, label, false);
            place(fs, skip);
        }
        return;
    case X_BIN:
        if (is_compare(e->op)) {
            gen_compare(fs, e, label, when);
            return;
        }
        break;
    case X_PAREN:
        gen_branch(fs, e->a, label, when);
        return;
    default:
        break;
    }
    int mark = fs->nslots;
    Val v = gen(fs, e, -1);
    branch_value(fs, &v, label, when);
    fs->nslots = mark;
}

/* A boolean value from a condition. */
static Val gen_bool(FS *fs, Expr *e, int dst) {
    int d = dst >= 0 ? dst : alloc_slot(fs);
    int t = new_label(fs), done = new_label(fs);
    int mark = fs->nslots;
    gen_branch(fs, e, t, true);
    fs->nslots = mark;
    emit(fs, "    mov.r %s, " FALSE, SR(d));
    ty_set(fs, d, TY_BOOL);
    jump(fs, done);
    place(fs, t);
    emit(fs, "    mov.r %s, " TRUE, SR(d));
    ty_set(fs, d, TY_BOOL);
    place(fs, done);
    return vslot(d);
}

/* --------------------------------------------------------------- concat */

static void concat_parts(Expr *e, VEC(Expr *) *parts) {
    if (e->k == X_BIN && e->op == OP_CONCAT) {
        concat_parts(e->a, parts);
        concat_parts(e->b, parts);
    } else {
        vec_push(*parts, e);
    }
}

static Val gen_concat(FS *fs, Expr *e, int dst) {
    VEC(Expr *) parts = {0};
    concat_parts(e, (void *)&parts);
    int n = parts.n;
    Val *vals = xmalloc((size_t)n * sizeof *vals);
    char **descs = xmalloc((size_t)n * sizeof *descs);
    /* constant folding of strings and integers */
    bool allconst = true;
    for (int i = 0; i < n; i++)
        if (parts.v[i]->k != X_STR && parts.v[i]->k != X_INT)
            allconst = false;
    if (allconst) {
        SB b = {0};
        size_t total = 0;
        char *acc = NULL;
        for (int i = 0; i < n; i++) {
            if (parts.v[i]->k == X_STR) {
                acc = xrealloc(acc, total + (size_t)parts.v[i]->s.len + 1);
                memcpy(acc + total, parts.v[i]->s.s, (size_t)parts.v[i]->s.len);
                total += (size_t)parts.v[i]->s.len;
            } else {
                char tmp[32];
                int k = snprintf(tmp, sizeof tmp, "%lld", (long long)parts.v[i]->i);
                acc = xrealloc(acc, total + (size_t)k + 1);
                memcpy(acc + total, tmp, (size_t)k);
                total += (size_t)k;
            }
        }
        (void)b;
        return vstr((Str){acc ? acc : "", (int)total});
    }
    for (int i = 0; i < n; i++) {
        vals[i] = gen(fs, parts.v[i], -1);
        descs[i] = describe(fs, parts.v[i]);
    }
    int d = dst;
    for (int i = 0; i < n; i++)
        if (vals[i].k == V_SLOT && vals[i].slot == d)
            d = -1;
    if (d < 0)
        d = alloc_slot(fs);
    bool direct = true;     /* only strings and integers */
    bool impossible = false;
    for (int i = 0; i < n; i++) {
        Ty t = vty(fs, &vals[i]);
        if (t != TY_STR && t != TY_INT)
            direct = false;
        if (t == TY_TAB || t == TY_NIL || t == TY_BOOL || t == TY_FN || t == TY_FLT)
            impossible = true;
    }
    int slow = new_label(fs), cont = new_label(fs);
    if (!impossible) {
        for (int i = 0; i < n; i++)
            if (vty(fs, &vals[i]) == TY_ANY) {
                emit(fs, "    typeof I31, %s", SR(vals[i].slot));
                branch(fs, slow, "bne I31, 1");
            }
        /* integers to strings */
        SB ops = {0};
        int nops = 0;
        int acc = -1;
        for (int i = 0; i < n; i++) {
            const char *op;
            if (vty(fs, &vals[i]) == TY_INT) {
                int k = alloc_slot(fs);
                emit(fs, "    tostr %s, %s", SR(k), VI(&vals[i]));
                op = SR(k);
            } else {
                op = VR(&vals[i]);
            }
            if (nops == 30) {
                int k = alloc_slot(fs);
                emit(fs, "    strcat %s%s", SR(k), sb_str(&ops));
                free(ops.s);
                ops = (SB){0};
                sb_add(&ops, ", %s", SR(k));
                nops = 1;
                acc = k;
            }
            sb_add(&ops, ", %s", op);
            nops++;
        }
        (void)acc;
        emit(fs, "    strcat %s%s", SR(d), sb_str(&ops));
        ty_set(fs, d, TY_STR);
        free(ops.s);
        if (direct) {
            free(vals);
            return vslot(d);
        }
        cold_begin(fs);
        place(fs, slow);
    }
    /* the general case, from the right */
    int acc = alloc_slot(fs);
    put(fs, acc, vals[n - 1]);
    const char *accdesc = descs[n - 1];
    for (int i = n - 2; i >= 0; i--) {
        const char *r = VR(&vals[i]);
        emit(fs, "    call rt$concat (%s, %s, %s, %s, %s, %s, %s, %s) -> (%s, %s, %s)", VI(&vals[i]),
             VF(&vals[i]), r, SI(acc), SF(acc), SR(acc), descs[i], accdesc, SI(acc), SF(acc), SR(acc));
        ty_set(fs, acc, TY_ANY);
        accdesc = "null";
    }
    put(fs, d, vslot(acc));
    ty_set(fs, d, TY_ANY);
    if (!impossible) {
        jump(fs, cont);
        cold_end(fs);
        place(fs, cont);
    }
    free(vals);
    return vslot(d);
}

/* ------------------------------------------------------------------ calls */

static void call_list(Val *vals, int n, SB *ops) {
    for (int i = 0; i < n; i++) {
        const char *r = VR(&vals[i]);
        sb_add(ops, "%s%s, %s, %s", ops->n ? ", " : "", VI(&vals[i]), VF(&vals[i]), r);
    }
}

static bool spilled(Val *v) { return v->k == V_SLOT && v->slot >= NREG; }

/* Append the values of a multi-valued expression (a call or ...) to the
   list in slot l. */
static void append_multi(FS *fs, int l, Expr *e) {
    if (e->k == X_VARARG) {
        emit(fs, "    call rt$appendlist (%s, %s)", SR(l), SR(fs->vaslot));
        return;
    }
    int t = alloc_slot(fs);
    gen_multi_call(fs, e, t);
    emit(fs, "    call rt$appendmv (%s, I31, %s, %s, %s)", SR(l), SI(t), SF(t), SR(t));
}

static Func *known_func(FS *fs, Expr *callee) {
    (void)fs;
    if (callee->k == X_LOCAL && callee->var->localfn && !callee->var->assigned)
        return callee->var->localfn;
    return NULL;
}

/* obj:name, the method, from the object in slot ss into slot d. Each site
   caches a shape that lacks name (global mc$<site>; a table with no hash
   part has none): for an object of that shape, the method is looked up in
   its metatable's __index table, with an inline cache, if that is a table.
   Anything else calls rt$methodc, which also says what to cache. */
static void gen_method(FS *fs, int ss, Str name, const char *desc, int d) {
    int site = G.nmc++;
    int slow = new_label(fs), cont = new_label(fs);
    ty_set(fs, d, TY_ANY);
    if (ty_get(fs, ss) != TY_TAB) {
        emit(fs, "    isa I31, %s, Tab", SR(ss));
        branch(fs, slow, "bz I31");
    }
    emit(fs, "    mov R31, %s->Tab.hm", RB(fs, ss, "R28"));
    branch(fs, slow, "bne.r R31, @mc$%d", site);
    emit(fs, "    mov R31, %s->Tab.meta", RB(fs, ss, "R28"));
    branch(fs, slow, "bz R31");
    emit(fs, "    mov R28, R31->Tab.idx");
    emit(fs, "    isa I31, R28, Tab");
    branch(fs, slow, "bz I31");
    ic_lookup(fs, "R28", name, slow);
    emit(fs, "    mov %s, R31[I31]", SR(d));
    emit(fs, "    inc I31");
    emit(fs, "    ldv %s, %s, R31[I31]", SI(d), SF(d));
    branch(fs, slow, "beq.r %s, " NIL, SR(d));
    cold_begin(fs);
    place(fs, slow);
    char *nm = masm_string(name);
    emit(fs, "    call rt$methodc (%s, %s, %s, %s, %s) -> (%s, %s, %s, R31)", SI(ss), SF(ss), SR(ss), nm,
         desc, SI(d), SF(d), SR(d));
    free(nm);
    emit(fs, "    mov.r @mc$%d, R31", site);
    ty_set(fs, d, TY_ANY);
    jump(fs, cont);
    cold_end(fs);
    place(fs, cont);
    ty_set(fs, d, TY_ANY);
}

/* A call. The results go to slot d: the first value, with the count in
   I31 and the rest in @rt$mv. With tail, a tail call is made instead. */
static void gen_call_into(FS *fs, Expr *e, int d, bool tail) {
    int line = fs->line;
    fs->line = e->line;
    int fslot;
    Val fval;
    Func *known = NULL;
    char *desc;
    VEC(Expr *) args = {0};
    Val self = {0};
    bool method = e->k == X_METHOD;
    if (method) {
        self = gen(fs, e->a, -1);
        int ss = in_slot(fs, self);
        self = vslot(ss);
        fslot = alloc_slot(fs);
        char *odesc = describe(fs, e->a);
        gen_method(fs, ss, e->s, odesc, fslot);
        free(odesc);
        fval = vslot(fslot);
        char buf[300];
        snprintf(buf, sizeof buf, "method '%.*s'", e->s.len > 200 ? 200 : e->s.len, e->s.s);
        desc = masm_string((Str){buf, (int)strlen(buf)});
    } else {
        known = known_func(fs, e->a);
        desc = describe(fs, e->a);
        int nargs0 = e->nargs;
        bool multi0 = nargs0 && is_multi(e->args[nargs0 - 1]);
        if (known && known->direct && !known->vararg && known->np == nargs0 && !multi0) {
            fslot = -1;
            fval = vint(0);
        } else {
            fval = gen(fs, e->a, -1);
            fslot = in_slot(fs, fval);
            fval = vslot(fslot);
        }
    }
    if (method)
        vec_push(args, NULL);
    for (int i = 0; i < e->nargs; i++)
        vec_push(args, e->args[i]);
    int n = args.n;
    bool multi = n && args.v[n - 1] && is_multi(args.v[n - 1]);
    /* f(a, g()): if g returns one value, as it usually does, f is called
       as with a fixed number of arguments, else with a list */
    bool one = multi && n <= MAXARGS && args.v[n - 1]->k != X_VARARG;
    bool tail0 = tail;
    if (one)
        tail = false;
    if ((multi || n > MAXARGS) && !one) {
        int l = alloc_slot(fs);
        emit(fs, "    lnew %s, %d", SR(l), 2 * n + 4);
        ty_set(fs, l, TY_TAB);
        for (int i = 0; i < n; i++) {
            if (!args.v[i]) {
                push_val(fs, SR(l), &self);
            } else if (i == n - 1 && multi) {
                append_multi(fs, l, args.v[i]);
            } else {
                int mark = fs->nslots;
                Val v = gen(fs, args.v[i], -1);
                push_val(fs, SR(l), &v);
                fs->nslots = mark;
            }
        }
        if (fslot < 0) {
            /* a direct function called with a list: make its value */
            fslot = alloc_slot(fs);
            fval = read_var(fs, e->a->var, fslot);
            fslot = in_slot(fs, fval);
        }
        /* (not a tail call: Lua keeps the caller's frame when it calls a
           library function, and errors report its line) */
        emit(fs, "    call rt$apply (%s, %s, %s, %s, %s) -> (I31, %s, %s, %s)", SI(fslot), SF(fslot),
             SR(fslot), SR(l), desc, SI(d), SF(d), SR(d));
        if (tail)
            emit(fs, "    ret I31, %s, %s, %s", SI(d), SF(d), SR(d));
        if (tail)
            fs->dead = true;
        ty_set(fs, d, TY_ANY);
        free(desc);
        fs->line = line;
        return;
    }
    Val *vals = xmalloc((size_t)(n + 1) * sizeof *vals);
    for (int i = 0; i < n - one; i++)
        vals[i] = args.v[i] ? gen(fs, args.v[i], -1) : self;
    int many = -1, last = -1;
    if (one) {
        last = alloc_slot(fs);
        gen_multi_call(fs, args.v[n - 1], last);
        vals[n - 1] = vslot(last);
        many = new_label(fs);
        branch(fs, many, "bne I31, 1");
    }
    bool any_spilled = false;
    for (int i = 0; i < n; i++)
        if (spilled(&vals[i]))
            any_spilled = true;
    if (fslot >= NREG)
        any_spilled = true;
    if (tail && any_spilled)
        tail = false;   /* tail calls take register arguments only */
    SB ops = {0};
    call_list(vals, n, &ops);
    const char *argl = sb_str(&ops);
    char res[200];
    snprintf(res, sizeof res, "(I31, %s, %s, %s)", SI(d), SF(d), SR(d));
    G.used_call[n] = true;
    if (fslot < 0) {
        /* a local function with no captures */
        char lbl[256];
        snprintf(lbl, sizeof lbl, "L%d$%s", known->id, known->name);
        if (tail)
            emit(fs, "    tcall %s (%s)", lbl, argl);
        else
            emit(fs, "    call %s (%s) -> %s", lbl, argl, res);
    } else if (known && !known->vararg && known->np == n) {
        emit(fs, "    mov R30, %s->Fn.clo", RB(fs, fslot, "R28"));
        if (tail)
            emit(fs, "    tcalli R30 (%s)", argl);
        else
            emit(fs, "    calli R30 (%s) -> %s", argl, res);
    } else {
        int slow = new_label(fs), cont = new_label(fs);
        if (ty_get(fs, fslot) != TY_FN) {
            emit(fs, "    isa I30, %s, Fn", SR(fslot));
            branch(fs, slow, "bz I30");
        }
        emit(fs, "    mov I30, %s->Fn.sig", RB(fs, fslot, "R28"));
        int counted = new_label(fs);
        branch(fs, counted, "bne I30, %d", n);
        emit(fs, "    mov R30, %s->Fn.clo", RB(fs, fslot, "R28"));
        int builtin = new_label(fs);
        if (tail) {
            /* a proper tail call to a Lua function; a library function
               (which has a name) is called normally, as Lua does */
            emit(fs, "    mov R29, %s->Fn.name", RB(fs, fslot, "R28"));
            branch(fs, builtin, "bnz R29");
            emit(fs, "    tcalli R30 (%s)", argl);
            fs->dead = true;
        } else {
            emit(fs, "    calli R30 (%s) -> %s", argl, res);
        }
        ty_set(fs, d, TY_ANY);
        cold_begin(fs);
        if (tail) {
            place(fs, builtin);
            emit(fs, "    calli R30 (%s) -> %s", argl, res);
            emit(fs, "    ret I31, %s, %s, %s", SI(d), SF(d), SR(d));
            fs->dead = true;
        }
        /* a library function that takes the number of arguments first
           (Fn.sig 300 + n), as type and tostring do */
        place(fs, counted);
        branch(fs, slow, "bne I30, %d", 300 + n);
        emit(fs, "    mov R30, %s->Fn.clo", RB(fs, fslot, "R28"));
        emit(fs, "    calli R30 (%d%s%s) -> %s", n, n ? ", " : "", argl, res);
        ty_set(fs, d, TY_ANY);
        if (tail) {
            emit(fs, "    ret I31, %s, %s, %s", SI(d), SF(d), SR(d));
            fs->dead = true;
        } else {
            jump(fs, cont);
        }
        place(fs, slow);
        emit(fs, "    call rt$call%d (%s, %s, %s, %s%s%s) -> %s", n, desc, SI(fslot), SF(fslot), SR(fslot),
             n ? ", " : "", argl, res);
        ty_set(fs, d, TY_ANY);
        if (tail) {
            emit(fs, "    ret I31, %s, %s, %s", SI(d), SF(d), SR(d));
            fs->dead = true;
        } else {
            jump(fs, cont);
        }
        cold_end(fs);
        if (!tail)
            place(fs, cont);
    }
    if (tail)
        fs->dead = true;
    ty_set(fs, d, TY_ANY);
    if (one) {
        int done = new_label(fs);
        cold_begin(fs);
        place(fs, many);
        int l = alloc_slot(fs);
        emit(fs, "    lnew %s, %d", SR(l), 2 * n + 4);
        ty_set(fs, l, TY_TAB);
        for (int i = 0; i < n - 1; i++)
            push_val(fs, SR(l), &vals[i]);
        emit(fs, "    call rt$appendmv (%s, I31, %s, %s, %s)", SR(l), SI(last), SF(last), SR(last));
        if (fslot < 0) {
            fslot = alloc_slot(fs);
            fval = read_var(fs, e->a->var, fslot);
            fslot = in_slot(fs, fval);
        }
        emit(fs, "    call rt$apply (%s, %s, %s, %s, %s) -> (I31, %s, %s, %s)", SI(fslot), SF(fslot),
             SR(fslot), SR(l), desc, SI(d), SF(d), SR(d));
        jump(fs, done);
        cold_end(fs);
        place(fs, done);
        ty_set(fs, d, TY_ANY);
        if (tail0) {
            emit(fs, "    ret I31, %s, %s, %s", SI(d), SF(d), SR(d));
            fs->dead = true;
        }
    }
    free(ops.s);
    free(vals);
    free(desc);
    fs->line = line;
}

/* A call whose results are all wanted: the first in slot d, the count in I31. */
static int gen_multi_call(FS *fs, Expr *e, int d) {
    gen_call_into(fs, e, d, false);
    return d;
}

/* ----------------------------------------------------- table constructor */

/* A constructor of 1..16 distinct constant string keys (not metamethod
   names) and nothing else: its tables share a shape. */
static bool shaped_ctor(Expr *e) {
    if (e->nfields < 1 || e->nfields > 16)
        return false;
    for (int i = 0; i < e->nfields; i++) {
        Expr *k = e->fields[i].key;
        if (!k || k->k != X_STR || (k->s.len >= 2 && k->s.s[0] == '_' && k->s.s[1] == '_'))
            return false;
        for (int j = 0; j < i; j++)
            if (str_same(e->fields[j].key->s, k->s))
                return false;
    }
    return true;
}

static Val gen_table(FS *fs, Expr *e, int dst) {
    int d = dst >= 0 && is_temp(fs, dst) ? dst : alloc_slot(fs);
    emit(fs, "    new %s, Tab", SR(d));
    ty_set(fs, d, TY_TAB);
    if (shaped_ctor(e)) {
        int l = alloc_slot(fs);
        emit(fs, "    lnew %s, %d", SR(l), 4 * e->nfields);
        ty_set(fs, l, TY_TAB);
        /* which values may be nil: the table counts them (Tab.dead) */
        int ndead = 0;
        bool *maybe = xmalloc((size_t)e->nfields * sizeof *maybe);
        for (int i = 0; i < e->nfields; i++) {
            int mark = fs->nslots;
            Val v = gen(fs, e->fields[i].val, -1);
            Ty vt = vty(fs, &v);
            maybe[i] = vt == TY_ANY;
            if (vt == TY_NIL)
                ndead++;
            char *ks = masm_string(e->fields[i].key->s);
            const char *lr = l < NREG ? SR(l) : "R29";
            if (l >= NREG)
                emit(fs, "    mov R29, %s", SR(l));
            emit(fs, "    mov.r R30, %s", ks);
            emit(fs, "    lpush %s, R30, F30", lr);
            free(ks);
            push_val(fs, SR(l), &v);
            fs->nslots = mark;
        }
        /* the constructor's shape (global ct$<site>), made the first time
           by rt$tmpl */
        int site = G.nctor++, slow = new_label(fs), done = new_label(fs);
        emit(fs, "    mov R31, @ct$%d", site);
        branch(fs, slow, "bz R31");
        const char *t = RB(fs, d, "R28");
        emit(fs, "    mov %s->Tab.hm, R31", t);
        emit(fs, "    mov.i64 %s->Tab.hmshared, 1", t);
        emit(fs, "    mov %s->Tab.hs, %s", t, SR(l));
        emit(fs, "    mov.i64 %s->Tab.hw, %d", t, 4 * e->nfields);
        bool anymaybe = false;
        for (int i = 0; i < e->nfields; i++)
            anymaybe |= maybe[i];
        if (ndead || anymaybe) {
            emit(fs, "    mov I31, %d", ndead);
            const char *lr = RB(fs, l, "R29");
            for (int i = 0; i < e->nfields; i++) {
                if (!maybe[i])
                    continue;
                int skip = new_label(fs);
                emit(fs, "    mov R30, %s[%d]", lr, 4 * i + 2);
                emit(fs, "    bne.r R30, " NIL ", .L%d", skip);
                emit(fs, "    inc I31");
                emit(fs, ".L%d:", skip);
            }
            emit(fs, "    mov %s->Tab.dead, I31", t);
        }
        free(maybe);
        cold_begin(fs);
        place(fs, slow);
        emit(fs, "    call rt$tmpl (%s, %s, %d) -> (R31)", SR(d), SR(l), site);
        emit(fs, "    mov.r @ct$%d, R31", site);
        jump(fs, done);
        cold_end(fs);
        place(fs, done);
        ty_set(fs, d, TY_TAB);
        return vslot(d);
    }
    int npos = 0;
    for (int i = 0; i < e->nfields; i++)
        if (!e->fields[i].key)
            npos++;
    int l = -1;
    if (npos) {
        l = alloc_slot(fs);
        emit(fs, "    lnew %s, %d", SR(l), 2 * npos);
        ty_set(fs, l, TY_TAB);
    }
    int seen = 0;
    for (int i = 0; i < e->nfields; i++) {
        Field *f = &e->fields[i];
        int mark = fs->nslots;
        if (f->key) {
            Val k = gen(fs, f->key, -1);
            Val v = gen(fs, f->val, -1);
            const char *kr = VR(&k), *vr = VR(&v);
            emit(fs, "    call rt$rawset (%s, %s, %s, %s, %s, %s, %s)", SR(d), VI(&k), VF(&k), kr, VI(&v),
                 VF(&v), vr);
        } else {
            seen++;
            if (seen == npos && is_multi(f->val) && f->val->k != X_VARARG && npos == e->nfields) {
                /* {..., g()}: usually one value */
                int t = alloc_slot(fs), many = new_label(fs), got = new_label(fs);
                gen_multi_call(fs, f->val, t);
                branch(fs, many, "bne I31, 1");
                Val tv = vslot(t);
                push_val(fs, SR(l), &tv);
                cold_begin(fs);
                place(fs, many);
                emit(fs, "    call rt$appendmv (%s, I31, %s, %s, %s)", SR(l), SI(t), SF(t), SR(t));
                jump(fs, got);
                cold_end(fs);
                place(fs, got);
            } else if (seen == npos && is_multi(f->val)) {
                append_multi(fs, l, f->val);
            } else {
                Val v = gen(fs, f->val, -1);
                push_val(fs, SR(l), &v);
            }
        }
        fs->nslots = mark;
    }
    if (npos && npos == e->nfields) {
        /* only positional values: the list is the array part, less any
           nils at its end */
        int done = new_label(fs), trim = new_label(fs);
        const char *t = RB(fs, d, "R28");
        const char *lr = RB(fs, l, "R29");
        emit(fs, "    mov %s->Tab.arr, %s", t, lr);
        emit(fs, "    len I31, %s", lr);
        emit(fs, "    shr I31, I31, 1");
        emit(fs, "    mov %s->Tab.n, I31", t);
        branch(fs, done, "bz I31");
        emit(fs, "    add I30, I31, I31");
        emit(fs, "    sub I30, I30, 2");
        emit(fs, "    mov R30, %s[I30]", lr);
        branch(fs, trim, "beq.r R30, " NIL);
        cold_begin(fs);
        place(fs, trim);
        emit(fs, "    call rt$trimarr (%s)", SR(d));
        jump(fs, done);
        cold_end(fs);
        place(fs, done);
    } else if (npos) {
        emit(fs, "    call rt$setarr (%s, %s)", SR(d), SR(l));
    }
    ty_set(fs, d, TY_TAB);
    return vslot(d);
}

/* -------------------------------------------------------------- closures */

static const char *sig_params(Func *f, char *buf, size_t n) {
    buf[0] = 0;
    if (f->np > MAXARGS) {
        snprintf(buf, n, "ref");
        return buf;
    }
    size_t k = 0;
    for (int i = 0; i < f->np; i++)
        k += (size_t)snprintf(buf + k, n - k, "%si64, f64, ref", i ? ", " : "");
    if (f->vararg)
        snprintf(buf + k, n - k, "%sref", f->np ? ", " : "");
    return buf;
}

static int func_sig(Func *f) {
    if (f->np > MAXARGS)
        return 200;
    return f->vararg ? 100 + f->np : f->np;
}

static void capture_ops(FS *fs, Func *f, SB *ops) {
    for (int i = 0; i < f->caps.n; i++) {
        Var *v = f->caps.v[i];
        if (v->fn == fs->f) {
            if (v->sty && !v->boxed)
                sb_add(ops, "%s%s", ops->n ? ", " : "", slot_op(v->slot, half_of(v->sty)));
            else if (v->boxed)
                sb_add(ops, "%s%s", ops->n ? ", " : "", SR(v->slot));
            else
                sb_add(ops, "%s%s, %s, %s", ops->n ? ", " : "", SI(v->slot), SF(v->slot), SR(v->slot));
        } else if (fs->upslot && fs->upslot[up_index(fs, v)] >= 0) {
            int k = fs->upslot[up_index(fs, v)];
            if (v->sty)
                sb_add(ops, "%s%s", ops->n ? ", " : "", slot_op(k, half_of(v->sty)));
            else if (v->boxed)
                sb_add(ops, "%s%s", ops->n ? ", " : "", SR(k));
            else
                sb_add(ops, "%s%s, %s, %s", ops->n ? ", " : "", SI(k), SF(k), SR(k));
        } else {
            int w = cap_offset(fs->f, v);
            if (fs->f->packed) {
                if (v->boxed || v->sty)
                    sb_add(ops, "%sR31[%d]", ops->n ? ", " : "", w);
                else
                    sb_add(ops, "%sR31[%d], R31[%d], R31[%d]", ops->n ? ", " : "", w, w + 1, w + 2);
            } else {
                if (v->boxed || v->sty)
                    sb_add(ops, "%sUP[%d]", ops->n ? ", " : "", w);
                else
                    sb_add(ops, "%sUP[%d], UP[%d], UP[%d]", ops->n ? ", " : "", w, w + 1, w + 2);
            }
        }
    }
}

/* Make the closure of f; its Fn goes to slot d (with self_first, the Fn is
   already in d and only gets its closure). */
static void gen_closure(FS *fs, Func *f, int d, bool self_first) {
    gen_function(f, fs);
    if (!self_first) {
        emit(fs, "    new R30, Fn");
        emit(fs, "    mov %s, R30", SR(d));
        ty_set(fs, d, TY_FN);
    }
    if (fs->f->packed && f->caps.n)
        emit(fs, "    mov R31, UP[0]");
    SB ops = {0};
    capture_ops(fs, f, &ops);
    if (f->packed) {
        emit(fs, "    lnew R29, %d", f->capwords);
        /* lpush takes the words one by one, keeping their types */
        char *s = ops.s, *tok = strtok(s, ",");
        while (tok) {
            while (*tok == ' ')
                tok++;
            emit(fs, "    lpush R29, %s", tok);
            tok = strtok(NULL, ",");
        }
        emit(fs, "    closure L%d$%s (R29) -> (R29)", f->id, f->name);
    } else if (ops.n) {
        emit(fs, "    closure L%d$%s (%s) -> (R29)", f->id, f->name, ops.s);
    } else {
        emit(fs, "    closure L%d$%s -> (R29)", f->id, f->name);
    }
    free(ops.s);
    emit(fs, "    mov R30, %s", SR(d));
    emit(fs, "    mov R30->Fn.clo, R29");
    emit(fs, "    mov R30->Fn.sig, %d", func_sig(f));
}

/* ----------------------------------------------------------- expressions */

static bool is_top_env(FS *fs, Var *v) {
    Func *f = fs->f;
    while (f->parent)
        f = f->parent;
    return v == f->env;
}

static Val gen_local(FS *fs, Expr *e, int dst) {
    if (is_top_env(fs, e->var)) {
        int d = dst >= 0 ? dst : alloc_slot(fs);
        env_val(fs, d);
        return vslot(d);
    }
    return read_var(fs, e->var, dst);
}

static Val gen_vararg1(FS *fs, int dst) {
    int d = dst >= 0 ? dst : alloc_slot(fs);
    emit(fs, "    call rt$listget (%s, 0) -> (%s, %s, %s)", SR(fs->vaslot), SI(d), SF(d), SR(d));
    ty_set(fs, d, TY_ANY);
    return vslot(d);
}

static Val gen_len(FS *fs, Val a, int dst, const char *desc) {
    int d = dst >= 0 ? dst : alloc_slot(fs);
    Ty t = vty(fs, &a);
    if (t == TY_STR) {
        emit(fs, "    len %s, %s", SI(d), VR(&a));
        set_tag(fs, d, TY_INT);
        return vslot(d);
    }
    int as = in_slot(fs, a);
    if (d == as)
        d = alloc_slot(fs);
    int slow = new_label(fs), cont = new_label(fs);
    if (t == TY_ANY || t == TY_TAB) {
        if (t != TY_TAB) {
            emit(fs, "    isa I31, %s, Tab", SR(as));
            branch(fs, slow, "bz I31");
        }
        emit(fs, "    mov R31, %s->Tab.meta", RB(fs, as, "R28"));
        branch(fs, slow, "bnz R31");
        emit(fs, "    mov %s, %s->Tab.n", SI(d), RB(fs, as, "R28"));
        set_tag(fs, d, TY_INT);
        cold_begin(fs);
        place(fs, slow);
    }
    emit(fs, "    call rt$len (%s, %s, %s, %s) -> (%s, %s, %s)", SI(as), SF(as), SR(as), desc, SI(d), SF(d),
         SR(d));
    ty_set(fs, d, TY_ANY);
    if (t == TY_ANY || t == TY_TAB) {
        jump(fs, cont);
        cold_end(fs);
        place(fs, cont);
    }
    return vslot(d);
}

static int arith_of(int op) {
    if (op <= OP_SHR)
        return op;
    if (op == OP_UNM)
        return A_UNM;
    return A_BNOT;
}

/* A destination for an expression that writes it before it has read
   everything: a temporary if dst is a variable's slot. */
static int fresh_dst(FS *fs, int dst) { return dst >= 0 && is_temp(fs, dst) ? dst : alloc_slot(fs); }

static Val gen(FS *fs, Expr *e, int dst) {
    int line = fs->line;
    if (e->line)
        fs->line = e->line;
    Val r;
    switch (e->k) {
    case X_NIL: r = vk(V_NIL); break;
    case X_TRUE: r = vk(V_TRUE); break;
    case X_FALSE: r = vk(V_FALSE); break;
    case X_INT: r = vint(e->i); break;
    case X_FLT: r = vflt(e->f); break;
    case X_STR: r = vstr(e->s); break;
    case X_VARARG: r = gen_vararg1(fs, dst); break;
    case X_PAREN: r = gen(fs, e->a, dst); break;
    case X_LOCAL: r = gen_local(fs, e, dst); break;
    case X_GLOBAL: r = read_global(fs, e, dst); break;
    case X_FUNC: {
        int d = fresh_dst(fs, dst);
        gen_closure(fs, e->fn, d, false);
        r = vslot(d);
        break;
    }
    case X_TABLE: r = gen_table(fs, e, dst); break;
    case X_CALL: case X_METHOD: {
        int d = fresh_dst(fs, dst);
        gen_call_into(fs, e, d, false);
        r = vslot(d);
        break;
    }
    case X_INDEX: {
        Val t = gen(fs, e->a, -1);
        Val k = gen(fs, e->b, -1);
        char *desc = describe(fs, e->a);
        r = gen_index_val(fs, t, k, dst, desc);
        free(desc);
        break;
    }
    case X_AND: case X_OR: {
        int d = fresh_dst(fs, dst);
        int done = new_label(fs);
        Val a = gen(fs, e->a, d);
        put(fs, d, a);
        branch_value(fs, &a, done, e->k == X_OR);
        int mark = fs->nslots;
        Val b = gen(fs, e->b, d);
        put(fs, d, b);
        fs->nslots = mark;
        place(fs, done);
        r = vslot(d);
        break;
    }
    case X_UN: {
        if (e->op == OP_NOT) {
            /* constant? */
            Expr *a = e->a;
            if (a->k == X_NIL || a->k == X_FALSE) { r = vk(V_TRUE); break; }
            if (a->k == X_TRUE || a->k == X_INT || a->k == X_FLT || a->k == X_STR) { r = vk(V_FALSE); break; }
            r = gen_bool(fs, e, dst);
            break;
        }
        Val a = gen(fs, e->a, -1);
        char *desc = describe(fs, e->a);
        if (e->op == OP_LEN) {
            if (a.k == V_STR) {
                r = vint(a.s.len);
            } else {
                r = gen_len(fs, a, dst, desc);
            }
        } else {
            r = gen_arith(fs, arith_of(e->op), a, a, dst, desc, desc);
        }
        free(desc);
        break;
    }
    case X_BIN: {
        if (is_compare(e->op)) {
            r = gen_bool(fs, e, dst);
            break;
        }
        if (e->op == OP_CONCAT) {
            r = gen_concat(fs, e, dst);
            break;
        }
        Val a = gen(fs, e->a, -1);
        /* the right operand may use the destination as its temporary */
        bool free_dst = dst >= 0 && !(a.k == V_SLOT && a.slot == dst);
        Val b = gen(fs, e->b, free_dst ? dst : -1);
        char *da = describe(fs, e->a), *db = describe(fs, e->b);
        r = gen_arith(fs, arith_of(e->op), a, b, dst, da, db);
        free(da);
        free(db);
        break;
    }
    default:
        r = vk(V_NIL);
    }
    fs->line = line;
    return r;
}

/* Evaluate exprs adjusted to n values, into slots base..base+n-1. */
static void gen_explist(FS *fs, Expr **exprs, int nexprs, int base, int n) {
    for (int i = 0; i < nexprs; i++) {
        Expr *e = exprs[i];
        bool last = i == nexprs - 1;
        if (last && is_multi(e) && n > i) {
            int want = n - i;
            int d = base + i;
            if (e->k == X_VARARG) {
                for (int j = 0; j < want; j++) {
                    emit(fs, "    call rt$listget (%s, %d) -> (%s, %s, %s)", SR(fs->vaslot), j, SI(d + j), SF(d + j),
                         SR(d + j));
                    ty_set(fs, d + j, TY_ANY);
                }
            } else {
                int mark = fs->nslots;
                int t = d < fs->nslots ? d : alloc_slot(fs);
                gen_multi_call(fs, e, t);
                if (t != d)
                    put(fs, d, vslot(t));
                for (int j = 1; j < want; j++) {
                    emit(fs, "    call rt$mvget (I31, %d) -> (%s, %s, %s)", j + 1, SI(d + j), SF(d + j), SR(d + j));
                    ty_set(fs, d + j, TY_ANY);
                }
                fs->nslots = mark > fs->nslots ? mark : fs->nslots;
            }
            return;
        }
        int mark = fs->nslots;
        if (i < n) {
            Val v = gen(fs, e, base + i);
            put(fs, base + i, v);
        } else {
            gen(fs, e, -1);    /* evaluated for its effects */
        }
        fs->nslots = mark;
    }
    for (int i = nexprs; i < n; i++)
        put(fs, base + i, vk(V_NIL));
}

/* ------------------------------------------------------------ statements */

static int goto_label(FS *fs, Stmt *label) {
    for (int i = 0; i < fs->gotolabels.n; i++)
        if (fs->gotolabels.v[i] == label)
            return fs->gotolabel_ids.v[i];
    int id = new_label(fs);
    vec_push(fs->gotolabels, label);
    vec_push(fs->gotolabel_ids, id);
    return id;
}

/* Close the to-be-closed variables above level n of the closes stack. */
static void close_to(FS *fs, int n) {
    for (int i = fs->closes.n - 1; i >= n; i--) {
        int k = fs->closes.v[i].slot;
        emit(fs, "    call rt$close (%s, %s, %s, 0, 0.0, " NIL ")", SI(k), SF(k), SR(k));
    }
}

typedef struct {
    int code_n, cold_n;
    int nslots;
    int closes_n, loops_n, gl_n;
    bool dead;
} Snap;

static Snap snap(FS *fs) {
    return (Snap){fs->code.n, fs->cold.n, fs->nslots, fs->closes.n, fs->loops.n, fs->gotolabels.n, fs->dead};
}

static void restore(FS *fs, Snap s) {
    buf_truncate(&fs->code, s.code_n);
    buf_truncate(&fs->cold, s.cold_n);
    fs->nslots = s.nslots;
    fs->closes.n = s.closes_n;
    fs->loops.n = s.loops_n;
    fs->gotolabels.n = s.gl_n;
    fs->gotolabel_ids.n = s.gl_n;
    fs->dead = s.dead;
}

typedef void (*LoopBody)(FS *fs, void *ctx, int head);

/* Generate a loop with speculated facts at its head: start from the facts
   on entry, and if the back edges do not keep them, weaken them and
   generate the loop again. */
static void speculate(FS *fs, LoopBody body, void *ctx) {
    int active = fs->nslots;
    ty_ensure(fs, active + 1);
    int hn = fs->tycap;
    Ty *h = ty_copy(fs);
    for (int i = active; i < hn; i++)
        h[i] = TY_ANY;
    for (int iter = 0;; iter++) {
        Snap s = snap(fs);
        int head = new_label(fs);
        ty_ensure(fs, hn);
        for (int i = 0; i < fs->tycap; i++)
            fs->ty[i] = i < hn ? h[i] : TY_ANY;
        emit(fs, ".L%d:", head);
        fs->dead = false;
        body(fs, ctx, head);
        LabelFacts *lf = label_facts(fs, head);
        bool ok = true;
        if (lf->has)
            for (int i = 0; i < active; i++) {
                Ty b = i < lf->n ? lf->ty[i] : TY_ANY;
                if (h[i] != TY_ANY && b != h[i]) {
                    h[i] = TY_ANY;
                    ok = false;
                }
            }
        if (ok)
            break;
        restore(fs, s);
        if (iter >= 3)
            for (int i = 0; i < hn; i++)
                h[i] = TY_ANY;
    }
    free(h);
}

typedef struct {
    int nslots, nlocals, nactive, closes;
} Scope;

static Scope scope_open(FS *fs) { return (Scope){fs->nslots, fs->nlocals, fs->nactive, fs->closes.n}; }

static void scope_close(FS *fs, Scope sc) {
    if (!fs->dead)
        close_to(fs, sc.closes);
    fs->nslots = sc.nslots;
    fs->nlocals = sc.nlocals;
    fs->nactive = sc.nactive;
    fs->closes.n = sc.closes;
}

static void gen_stmt(FS *fs, Stmt *s);

static void gen_stmts(FS *fs, Block *b) {
    for (int i = 0; i < b->n; i++) {
        gen_stmt(fs, b->stmts[i]);
        fs->nslots = fs->nlocals;
    }
}

static void gen_block(FS *fs, Block *b) {
    Scope sc = scope_open(fs);
    gen_stmts(fs, b);
    scope_close(fs, sc);
}

/* Declare var in a new slot holding (or becoming) its value. */
static int declare(FS *fs, Var *v, int slot) {
    define_var(fs, v, slot);
    fs->nactive++;
    if (v->attrib == ATTR_CLOSE) {
        char *nm = masm_string(v->name);
        emit(fs, "    call rt$checkclose (%s, %s, %s, %s)", SI(slot), SF(slot), SR(slot), nm);
        free(nm);
        CloseVar c = {slot, fs->nactive - 1};
        vec_push(fs->closes, c);
    }
    return slot;
}

static void gen_localstat(FS *fs, Stmt *s) {
    int base = alloc_slots(fs, s->nvars);
    gen_explist(fs, s->exprs, s->nexprs, base, s->nvars);
    fs->nslots = base + s->nvars;
    for (int i = 0; i < s->nvars; i++)
        declare(fs, s->vars[i], base + i);
    fs->nlocals = fs->nslots;
}

static void gen_localfn(FS *fs, Stmt *s) {
    Var *v = s->var;
    int k = alloc_slot(fs);
    v->slot = k;
    fs->nactive++;
    fs->nlocals = fs->nslots;
    emit(fs, "    new R30, Fn");
    emit(fs, "    mov %s, R30", SR(k));
    ty_set(fs, k, TY_FN);
    if (v->boxed) {
        emit(fs, "    new R31, Box");
        emit(fs, "    mov R31->Box.r, R30");
        emit(fs, "    mov %s, R31", SR(k));
        ty_set(fs, k, TY_ANY);
    }
    gen_function(s->fn, fs);
    Func *f = s->fn;
    if (fs->f->packed && f->caps.n)
        emit(fs, "    mov R31, UP[0]");
    SB ops = {0};
    capture_ops(fs, f, &ops);
    if (f->packed) {
        emit(fs, "    lnew R29, %d", f->capwords);
        char *tok = strtok(ops.s, ",");
        while (tok) {
            while (*tok == ' ')
                tok++;
            emit(fs, "    lpush R29, %s", tok);
            tok = strtok(NULL, ",");
        }
        emit(fs, "    closure L%d$%s (R29) -> (R29)", f->id, f->name);
    } else if (ops.n) {
        emit(fs, "    closure L%d$%s (%s) -> (R29)", f->id, f->name, ops.s);
    } else {
        emit(fs, "    closure L%d$%s -> (R29)", f->id, f->name);
    }
    free(ops.s);
    emit(fs, "    mov R30, %s", SR(k));
    if (v->boxed)
        emit(fs, "    mov R30, R30->Box.r");
    emit(fs, "    mov R30->Fn.clo, R29");
    emit(fs, "    mov R30->Fn.sig, %d", func_sig(f));
}

static bool simple_target(FS *fs, Expr *t) {
    return t->k == X_LOCAL && t->var->fn == fs->f && !t->var->boxed && !is_top_env(fs, t->var);
}

static void assign_to(FS *fs, Expr *t, Val v, Val *tv, Val *kv) {
    switch (t->k) {
    case X_LOCAL:
        if (is_top_env(fs, t->var)) {
            int k = in_slot(fs, v);
            emit(fs, "    mov @rt$env, %s", SR(k));
            return;
        }
        write_var(fs, t->var, v);
        return;
    case X_GLOBAL:
        write_global(fs, t, v);
        return;
    case X_INDEX: {
        char *desc = describe(fs, t->a);
        gen_setindex(fs, *tv, *kv, v, desc);
        free(desc);
        return;
    }
    default:
        return;
    }
}

static void check_undef(FS *fs, Expr *t) {
    int mark = fs->nslots;
    Val cur = gen(fs, t, -1);
    int k = in_slot(fs, cur);
    char *nm = masm_string(t->s);
    emit(fs, "    call rt$gcheck (%s, %s)", SR(k), nm);
    free(nm);
    fs->nslots = mark;
}

static void gen_assign(FS *fs, Stmt *s) {
    if (s->check_undef)
        for (int i = 0; i < s->ntargets; i++)
            check_undef(fs, s->targets[i]);
    if (s->ntargets == 1 && s->nexprs == 1) {
        Expr *t = s->targets[0];
        Val tv = {0}, kv = {0};
        if (t->k == X_INDEX) {
            tv = gen(fs, t->a, -1);
            kv = gen(fs, t->b, -1);
        }
        Val v;
        if (simple_target(fs, t)) {
            v = gen(fs, s->exprs[0], t->var->slot);
        } else {
            v = gen(fs, s->exprs[0], -1);
            if (is_multi(s->exprs[0]) && s->exprs[0]->k != X_VARARG)
                ; /* the call is truncated to one value */
        }
        assign_to(fs, t, v, &tv, &kv);
        return;
    }
    Val *tv = xcalloc((size_t)s->ntargets, sizeof *tv), *kv = xcalloc((size_t)s->ntargets, sizeof *kv);
    for (int i = 0; i < s->ntargets; i++) {
        Expr *t = s->targets[i];
        if (t->k == X_INDEX) {
            tv[i] = gen(fs, t->a, -1);
            kv[i] = gen(fs, t->b, -1);
            /* keep them in temporaries: a local may be assigned first */
            if (tv[i].k == V_SLOT && !is_temp(fs, tv[i].slot)) {
                int k = alloc_slot(fs);
                put(fs, k, tv[i]);
                tv[i] = vslot(k);
            }
            if (kv[i].k == V_SLOT && !is_temp(fs, kv[i].slot)) {
                int k = alloc_slot(fs);
                put(fs, k, kv[i]);
                kv[i] = vslot(k);
            }
        }
    }
    int base = alloc_slots(fs, s->ntargets);
    gen_explist(fs, s->exprs, s->nexprs, base, s->ntargets);
    fs->nslots = base + s->ntargets;
    for (int i = 0; i < s->ntargets; i++)
        assign_to(fs, s->targets[i], vslot(base + i), &tv[i], &kv[i]);
    free(tv);
    free(kv);
}

static void gen_return(FS *fs, Stmt *s) {
    int n = s->nexprs;
    bool closes = fs->closes.n > 0;
    if (n == 0) {
        close_to(fs, 0);
        emit(fs, "    ret 0, 0, 0.0, " NIL);
        fs->dead = true;
        return;
    }
    Expr *last = s->exprs[n - 1];
    if (n == 1 && (last->k == X_CALL || last->k == X_METHOD) && !closes) {
        int d = alloc_slot(fs);
        gen_call_into(fs, last, d, true);
        if (!fs->dead) {
            emit(fs, "    ret I31, %s, %s, %s", SI(d), SF(d), SR(d));
            fs->dead = true;
        }
        return;
    }
    if (is_multi(last)) {
        int l = alloc_slot(fs);
        emit(fs, "    lnew %s, %d", SR(l), 2 * n + 4);
        for (int i = 0; i < n - 1; i++) {
            int mark = fs->nslots;
            Val v = gen(fs, s->exprs[i], -1);
            push_val(fs, SR(l), &v);
            fs->nslots = mark;
        }
        append_multi(fs, l, last);
        close_to(fs, 0);
        if (spilled(&(Val){V_SLOT, l, 0, 0, {0}})) {
            emit(fs, "    mov R30, %s", SR(l));
            emit(fs, "    tcall rt$retlist (R30)");
        } else {
            emit(fs, "    tcall rt$retlist (%s)", SR(l));
        }
        fs->dead = true;
        return;
    }
    Val *vals = xmalloc((size_t)n * sizeof *vals);
    for (int i = 0; i < n; i++) {
        vals[i] = gen(fs, s->exprs[i], -1);
        /* a later expression must not see an earlier value change */
        if (n > 1 && vals[i].k == V_SLOT && !is_temp(fs, vals[i].slot) && i < n - 1) {
            int k = alloc_slot(fs);
            put(fs, k, vals[i]);
            vals[i] = vslot(k);
        }
    }
    close_to(fs, 0);
    if (n > 1) {
        if (n > 32)
            emit(fs, "    call rt$mvreserve (%d)", 2 * (n - 1));
        emit(fs, "    mov R31, @rt$mv");
        emit(fs, "    mov I31, 0");
        for (int i = 1; i < n; i++) {
            Val *v = &vals[i];
            Ty t = vty(fs, v);
            if (i > 1)
                emit(fs, "    inc I31");
            if (v->k != V_SLOT) {
                emit(fs, "    mov.r R31[I31], %s", VR(v));
                emit(fs, "    inc I31");
                if (v->k == V_INT)
                    emit(fs, "    mov.i64 R31[I31], %s", VI(v));
                else
                    emit(fs, "    mov.f64 R31[I31], %s", VF(v));
            } else {
                emit(fs, "    mov R31[I31], %s", SR(v->slot));
                emit(fs, "    inc I31");
                if (t == TY_INT) {
                    emit(fs, "    mov R31[I31], %s", SI(v->slot));
                } else if (t == TY_ANY) {
                    int l1 = new_label(fs), l2 = new_label(fs);
                    emit(fs, "    bnz %s, .L%d", SR(v->slot), l1);
                    emit(fs, "    mov R31[I31], %s", SI(v->slot));
                    emit(fs, "    jmp .L%d", l2);
                    emit(fs, ".L%d:", l1);
                    emit(fs, "    mov R31[I31], %s", SF(v->slot));
                    emit(fs, ".L%d:", l2);
                } else {
                    emit(fs, "    mov R31[I31], %s", SF(v->slot));
                }
            }
        }
    }
    const char *r = VR(&vals[0]);
    emit(fs, "    ret %d, %s, %s, %s", n, VI(&vals[0]), VF(&vals[0]), r);
    fs->dead = true;
    free(vals);
}

/* ------------------------------------------------------------------ loops */

typedef struct {
    Stmt *s;
    int exit;
    int var, lim, step, count;
    int64_t cstep;
    bool const_step;
} ForCtx;

static void numfor_body(FS *fs, void *c, int head) {
    ForCtx *x = c;
    Loop lp = {x->exit, fs->closes.n};
    vec_push(fs->loops, lp);
    gen_block(fs, x->s->body);
    fs->loops.n--;
    if (!fs->dead) {
        if (x->const_step && x->cstep == 1) {
            branch(fs, x->exit, "bge %s, %s", SI(x->var), SI(x->lim));
            emit(fs, "    inc %s", SI(x->var));
        } else if (x->const_step && x->cstep == -1) {
            branch(fs, x->exit, "ble %s, %s", SI(x->var), SI(x->lim));
            emit(fs, "    dec %s", SI(x->var));
        } else {
            branch(fs, x->exit, "bz %s", SI(x->count));
            emit(fs, "    dec %s", SI(x->count));
            if (x->const_step)
                emit(fs, "    add %s, %lld", SI(x->var), (long long)x->cstep);
            else
                emit(fs, "    add %s, %s", SI(x->var), SI(x->step));
        }
        jump(fs, head);
    }
    place(fs, x->exit);
}

/* The general numeric loop: the loop variable's type is only known at run
   time. rt$forprep and rt$forloop keep the state in five slots. */
static void numfor_generic_body(FS *fs, void *c, int head) {
    ForCtx *x = c;
    Loop lp = {x->exit, fs->closes.n};
    vec_push(fs->loops, lp);
    gen_block(fs, x->s->body);
    fs->loops.n--;
    if (!fs->dead) {
        /* state: var (the value), lim (limit or count), step */
        emit(fs, "    call rt$forloop (%s, %s, %s, %s, %s, %s, %s, %s, %s) -> (I31, %s, %s, %s, %s)", SI(x->var),
             SF(x->var), SR(x->var), SI(x->lim), SF(x->lim), SR(x->lim), SI(x->step), SF(x->step), SR(x->step),
             SI(x->var), SF(x->var), SR(x->var), SI(x->lim));
        ty_set(fs, x->var, TY_ANY);
        branch(fs, x->exit, "bz I31");
        jump(fs, head);
    }
    place(fs, x->exit);
}

static void gen_numfor(FS *fs, Stmt *s) {
    Scope sc = scope_open(fs);
    ForCtx x = {s, new_label(fs), 0, 0, 0, 0, 1, true};
    /* the loop's slots, which the start, limit and step are evaluated into */
    x.var = alloc_slot(fs);
    x.lim = alloc_slot(fs);
    x.step = alloc_slot(fs);
    int mark = fs->nslots;
    Val start = gen(fs, s->start, x.var);
    put(fs, x.var, start);
    fs->nslots = mark;
    Val limit = gen(fs, s->limit, x.lim);
    put(fs, x.lim, limit);
    fs->nslots = mark;
    Val step = s->step ? gen(fs, s->step, x.step) : vint(1);
    put(fs, x.step, step);
    fs->nslots = mark;
    s->var->slot = x.var;
    Ty ts = ty_get(fs, x.var), tst = ty_get(fs, x.step);
    x.const_step = step.k == V_INT;
    x.cstep = step.k == V_INT ? step.i : 0;
    bool unit = ts == TY_INT && x.const_step && (x.cstep == 1 || x.cstep == -1);
    if (!unit)
        x.count = alloc_slot(fs);
    if (ts == TY_INT && tst == TY_INT) {
        if (x.const_step && x.cstep == 0)
            emit(fs, "    call rt$forzero");
        int slow = new_label(fs), cont = new_label(fs);
        if (unit) {
            /* an integer limit: inline */
            if (ty_get(fs, x.lim) != TY_INT) {
                branch(fs, slow, "bnz %s", SR(x.lim));
                set_tag(fs, x.lim, TY_INT);
            }
            cold_begin(fs);
            place(fs, slow);
        }
        /* the limit, clipped to an integer; I31 = 0 when the loop does not run */
        emit(fs, "    call rt$forprepi (%s, %s, %s, %s, %s) -> (I31, %s, %s)", SI(x.var), SI(x.step), SI(x.lim),
             SF(x.lim), SR(x.lim), SI(x.lim), unit ? "I30" : SI(x.count));
        set_tag(fs, x.lim, TY_INT);
        if (!unit)
            set_tag(fs, x.count, TY_INT);
        branch(fs, x.exit, "bz I31");
        if (unit) {
            jump(fs, cont);
            cold_end(fs);
            branch(fs, x.exit, "%s %s, %s", x.cstep == 1 ? "bgt" : "blt", SI(x.var), SI(x.lim));
            place(fs, cont);
        }
        fs->nlocals = fs->nslots;
        fs->nactive++;
        speculate(fs, numfor_body, &x);
    } else {
        emit(fs, "    call rt$forprep (%s, %s, %s, %s, %s, %s, %s, %s, %s) -> (I31, %s, %s, %s, %s, %s, %s, %s, %s, %s)",
             SI(x.var), SF(x.var), SR(x.var), SI(x.lim), SF(x.lim), SR(x.lim), SI(x.step), SF(x.step), SR(x.step),
             SI(x.var), SF(x.var), SR(x.var), SI(x.lim), SF(x.lim), SR(x.lim), SI(x.step), SF(x.step), SR(x.step));
        ty_set(fs, x.var, TY_ANY);
        ty_set(fs, x.lim, TY_ANY);
        ty_set(fs, x.step, TY_ANY);
        branch(fs, x.exit, "bz I31");
        fs->nlocals = fs->nslots;
        fs->nactive++;
        speculate(fs, numfor_generic_body, &x);
    }
    scope_close(fs, sc);
}

typedef struct {
    Stmt *s;
    int exit;
    int f, st, ctl;
    int pos;            /* the position of a pairs loop */
    int vars;
} GenForCtx;

static void emit_call(FS *fs, int fslot, Val *vals, int n, int d, const char *desc);

static void genfor_body(FS *fs, void *c, int head) {
    GenForCtx *x = c;
    Stmt *s = x->s;
    int k = x->vars, v = s->nvars > 1 ? x->vars + 1 : -1;
    int notnext = new_label(fs), generic = new_label(fs), got = new_label(fs);
    /* pairs: iterate by position, without calling next */
    branch(fs, notnext, "bne.r %s, @rt$next_fn", SR(x->f));
    if (ty_get(fs, x->st) != TY_TAB) {
        emit(fs, "    isa I30, %s, Tab", SR(x->st));
        branch(fs, generic, "bz I30");
    }
    emit(fs, "    call rt$nextpos (%s, %s) -> (%s, %s, %s, %s, %s, %s, %s)", SR(x->st), SI(x->pos), SI(x->pos),
         SI(k), SF(k), SR(k), v >= 0 ? SI(v) : "I30", v >= 0 ? SF(v) : "F30", v >= 0 ? SR(v) : "R30");
    ty_set(fs, k, TY_ANY);
    if (v >= 0)
        ty_set(fs, v, TY_ANY);
    for (int j = 2; j < s->nvars; j++)
        put(fs, x->vars + j, vk(V_NIL));
    jump(fs, got);
    cold_begin(fs);
    place(fs, notnext);
    /* ipairs: the array part inline */
    if (v >= 0) {
        branch(fs, generic, "bne.r %s, @rt$ipairs_aux", SR(x->f));
        if (ty_get(fs, x->st) != TY_TAB) {
            emit(fs, "    isa I30, %s, Tab", SR(x->st));
            branch(fs, generic, "bz I30");
        }
        branch(fs, generic, "bnz %s", SR(x->ctl));
        emit(fs, "    mov R31, %s", SR(x->st));
        emit(fs, "    mov R30, R31->Tab.meta");
        branch(fs, generic, "bnz R30");
        emit(fs, "    mov I30, R31->Tab.n");
        branch(fs, generic, "bge %s, I30", SI(x->ctl));
        emit(fs, "    add I31, %s, %s", SI(x->ctl), SI(x->ctl));
        emit(fs, "    mov R30, R31->Tab.arr");
        emit(fs, "    mov %s, R30[I31]", SR(v));
        emit(fs, "    inc I31");
        emit(fs, "    ldv %s, %s, R30[I31]", SI(v), SF(v));
        ty_set(fs, v, TY_ANY);
        emit(fs, "    add %s, %s, 1", SI(k), SI(x->ctl));
        emit(fs, "    mov.r %s, null", SR(k));
        ty_set(fs, k, TY_INT);
        /* a nil value ends the loop */
        int notnil = new_label(fs);
        branch(fs, notnil, "bne.r %s, " NIL, SR(v));
        emit(fs, "    mov.r %s, " NIL, SR(k));
        ty_set(fs, k, TY_ANY);
        place(fs, notnil);
        ty_set(fs, k, TY_ANY);
        for (int j = 2; j < s->nvars; j++)
            put(fs, x->vars + j, vk(V_NIL));
        jump(fs, got);
    }
    place(fs, generic);
    Val args[2] = {vslot(x->st), vslot(x->ctl)};
    emit_call(fs, x->f, args, 2, x->vars, "\"for iterator 'for iterator'\"");
    for (int j = 1; j < s->nvars; j++) {
        emit(fs, "    call rt$mvget (I31, %d) -> (%s, %s, %s)", j + 1, SI(x->vars + j), SF(x->vars + j),
             SR(x->vars + j));
        ty_set(fs, x->vars + j, TY_ANY);
    }
    jump(fs, got);
    cold_end(fs);
    place(fs, got);
    branch(fs, x->exit, "beq.r %s, " NIL, SR(x->vars));
    put(fs, x->ctl, vslot(x->vars));
    Loop lp = {x->exit, fs->closes.n};
    vec_push(fs->loops, lp);
    gen_block(fs, s->body);
    fs->loops.n--;
    jump(fs, head);
    place(fs, x->exit);
}

static void gen_genfor(FS *fs, Stmt *s) {
    Scope sc = scope_open(fs);
    GenForCtx x = {s, new_label(fs), 0, 0, 0, 0, 0};
    int base = alloc_slots(fs, 3);
    gen_explist(fs, s->exprs, s->nexprs, base, 3);
    fs->nslots = base + 3;
    x.f = base;
    x.st = base + 1;
    x.ctl = base + 2;
    x.pos = alloc_slot(fs);
    put(fs, x.pos, vint(0));
    x.vars = alloc_slots(fs, s->nvars);
    for (int i = 0; i < s->nvars; i++) {
        s->vars[i]->slot = x.vars + i;
        fs->nactive++;
    }
    fs->nlocals = fs->nslots;
    speculate(fs, genfor_body, &x);
    scope_close(fs, sc);
}

typedef struct {
    Stmt *s;
} WhileCtx;

static void while_body(FS *fs, void *c, int head) {
    Stmt *s = ((WhileCtx *)c)->s;
    int exit = new_label(fs);
    gen_branch(fs, s->cond, exit, false);
    Loop lp = {exit, fs->closes.n};
    vec_push(fs->loops, lp);
    gen_block(fs, s->body);
    fs->loops.n--;
    jump(fs, head);
    place(fs, exit);
}

static void repeat_body(FS *fs, void *c, int head) {
    Stmt *s = ((WhileCtx *)c)->s;
    int exit = new_label(fs);
    Loop lp = {exit, fs->closes.n};
    vec_push(fs->loops, lp);
    Scope sc = scope_open(fs);
    gen_stmts(fs, s->body);
    fs->loops.n--;
    if (!fs->dead) {
        if (fs->closes.n > sc.closes) {
            int again = new_label(fs);
            gen_branch(fs, s->cond, again, false);
            close_to(fs, sc.closes);
            jump(fs, exit);
            place(fs, again);
            close_to(fs, sc.closes);
            jump(fs, head);
        } else {
            gen_branch(fs, s->cond, head, false);
        }
    }
    fs->nslots = sc.nslots;
    fs->nlocals = sc.nlocals;
    fs->nactive = sc.nactive;
    fs->closes.n = sc.closes;
    if (!fs->dead)
        jump(fs, exit);
    place(fs, exit);
}

static void gen_stmt(FS *fs, Stmt *s) {
    fs->line = s->line;
    if (fs->dead && s->k != S_LABEL)
        return;     /* unreachable */
    switch (s->k) {
    case S_EXPR: {
        int d = alloc_slot(fs);
        gen_call_into(fs, s->exprs[0], d, false);
        break;
    }
    case S_LOCAL: gen_localstat(fs, s); break;
    case S_LOCALFN: gen_localfn(fs, s); break;
    case S_ASSIGN: gen_assign(fs, s); break;
    case S_GLOBALFN: {
        check_undef(fs, s->targets[0]);
        int d = alloc_slot(fs);
        gen_closure(fs, s->fn, d, false);
        assign_to(fs, s->targets[0], vslot(d), NULL, NULL);
        break;
    }
    case S_IF: {
        int end = new_label(fs);
        for (int i = 0; i < s->nconds; i++) {
            int next = new_label(fs);
            gen_branch(fs, s->conds[i], next, false);
            gen_block(fs, s->blocks[i]);
            jump(fs, end);
            place(fs, next);
        }
        if (s->blocks[s->nconds])
            gen_block(fs, s->blocks[s->nconds]);
        place(fs, end);
        break;
    }
    case S_WHILE: {
        WhileCtx c = {s};
        speculate(fs, while_body, &c);
        break;
    }
    case S_REPEAT: {
        WhileCtx c = {s};
        speculate(fs, repeat_body, &c);
        break;
    }
    case S_NUMFOR: gen_numfor(fs, s); break;
    case S_GENFOR: gen_genfor(fs, s); break;
    case S_DO: gen_block(fs, s->body); break;
    case S_RETURN: gen_return(fs, s); break;
    case S_BREAK: {
        Loop *lp = &fs->loops.v[fs->loops.n - 1];
        close_to(fs, lp->nclose);
        jump(fs, lp->brk);
        break;
    }
    case S_GOTO: {
        int keep = 0;
        while (keep < fs->closes.n && fs->closes.v[keep].level < s->label->nactive)
            keep++;
        close_to(fs, keep);
        jump(fs, goto_label(fs, s->label));
        break;
    }
    case S_LABEL:
        place(fs, goto_label(fs, s));
        if (s->backward)
            forget_all(fs);
        break;
    }
}

/* A call with arguments already evaluated (the generic for's iterator). */
static void emit_call(FS *fs, int fslot, Val *vals, int n, int d, const char *desc) {
    SB ops = {0};
    call_list(vals, n, &ops);
    const char *argl = sb_str(&ops);
    char res[200];
    snprintf(res, sizeof res, "(I31, %s, %s, %s)", SI(d), SF(d), SR(d));
    G.used_call[n] = true;
    int slow = new_label(fs), cont = new_label(fs);
    emit(fs, "    isa I30, %s, Fn", SR(fslot));
    branch(fs, slow, "bz I30");
    emit(fs, "    mov I30, %s->Fn.sig", RB(fs, fslot, "R28"));
    branch(fs, slow, "bne I30, %d", n);
    emit(fs, "    mov R30, %s->Fn.clo", RB(fs, fslot, "R28"));
    emit(fs, "    calli R30 (%s) -> %s", argl, res);
    ty_set(fs, d, TY_ANY);
    cold_begin(fs);
    place(fs, slow);
    emit(fs, "    call rt$call%d (%s, %s, %s, %s%s%s) -> %s", n, desc, SI(fslot), SF(fslot), SR(fslot),
         n ? ", " : "", argl, res);
    ty_set(fs, d, TY_ANY);
    jump(fs, cont);
    cold_end(fs);
    place(fs, cont);
    free(ops.s);
}

/* -------------------------------------------------------------- functions */

static void sanitize(char *s) {
    for (; *s; s++)
        if (!isalnum((unsigned char)*s) && *s != '_')
            *s = '_';
}

static void gen_function(Func *f, FS *parent) {
    if (f->generated)
        return;
    f->generated = true;
    FS fs = {0};
    fs.parent = parent;
    fs.f = f;
    fs.cur = &fs.code;
    fs.vaslot = -1;
    fs.line = f->line;
    int np = f->np;
    if (np > MAXARGS) {
        alloc_slots(&fs, np);
        fs.vaslot = alloc_slot(&fs);
        emit(&fs, "    mov %s, R0", SR(fs.vaslot));
        for (int j = 0; j < np; j++)
            emit(&fs, "    call rt$listget (%s, %d) -> (%s, %s, %s)", SR(fs.vaslot), j, SI(j), SF(j), SR(j));
        if (f->vararg)
            emit(&fs, "    call rt$sublist (%s, %d) -> (%s)", SR(fs.vaslot), np, SR(fs.vaslot));
    } else {
        alloc_slots(&fs, np);
        if (f->vararg)
            fs.vaslot = alloc_slot(&fs);
    }
    if (fs.vaslot >= 0)
        ty_set(&fs, fs.vaslot, TY_TAB);
    for (int j = 0; j < np; j++) {
        f->params[j]->slot = j;
        declare(&fs, f->params[j], j);
    }
    if (f->is_main)
        fs.nactive++;   /* _ENV */
    if (f->caps.n && has_loop(f->body)) {
        /* upvalues (UP[n]) are slow to read: copy them to registers once */
        fs.upslot = xmalloc((size_t)f->caps.n * sizeof *fs.upslot);
        if (f->packed)
            emit(&fs, "    mov R31, UP[0]");
        int w = 0;
        for (int i = 0; i < f->caps.n; i++) {
            Var *v = f->caps.v[i];
            int k = alloc_slot(&fs);
            fs.upslot[i] = k;
            char words[3][32];
            for (int j = 0; j < 3; j++)
                snprintf(words[j], sizeof words[j], f->packed ? "R31[%d]" : "UP[%d]", w + j);
            if (v->sty) {
                emit(&fs, "    mov %s, %s", slot_op(k, half_of(v->sty)), words[0]);
                set_tag(&fs, k, (Ty)v->sty);
                ty_set(&fs, k, (Ty)v->sty);
                w += 1;
            } else if (v->boxed) {
                emit(&fs, "    mov %s, %s", SR(k), words[0]);
                w += 1;
            } else {
                emit(&fs, "    mov %s, %s", SI(k), words[0]);
                emit(&fs, "    mov %s, %s", SF(k), words[1]);
                emit(&fs, "    mov %s, %s", SR(k), words[2]);
                w += 3;
            }
        }
    }
    if (f->vaname) {
        int k = alloc_slot(&fs);
        emit(&fs, "    call rt$vatable (%s) -> (%s)", SR(fs.vaslot), SR(k));
        ty_set(&fs, k, TY_TAB);
        declare(&fs, f->vaname, k);
    }
    fs.nlocals = fs.nslots;
    gen_stmts(&fs, f->body);
    if (!fs.dead) {
        fs.line = f->lastline;
        close_to(&fs, 0);
        emit(&fs, "    ret 0, 0, 0.0, " NIL);
    }
    pack_scratch(&fs);
    char params[1024];
    sig_params(f, params, sizeof params);
    char head[1200];
    snprintf(head, sizeof head, ".func L%d$%s(%s) -> (i64, i64, f64, ref)", f->id, f->name, params);
    buf_add(&G.out, f->prelude ? -2 : f->line, head);
    if (fs.maxslots > NREG) {
        int words = 3 * (fs.maxslots - NREG);
        snprintf(head, sizeof head, "    enter %d", words);
        buf_add(&G.out, f->prelude ? -2 : f->line, head);
        /* give the words their types: closures and lists copy a word with
           its type, and an unwritten word would read back as an i64 */
        for (int w = 0; w < words; w += 3) {
            snprintf(head, sizeof head, "    mov.r LOC[%d], " NIL, w);
            buf_add(&G.out, 0, head);
            snprintf(head, sizeof head, "    mov.i64 LOC[%d], 0", w + 1);
            buf_add(&G.out, 0, head);
            snprintf(head, sizeof head, "    mov.f64 LOC[%d], 0.0", w + 2);
            buf_add(&G.out, 0, head);
        }
    }
    buf_cat(&G.out, &fs.code);
    buf_cat(&G.out, &fs.cold);
    buf_add(&G.out, 0, ".endf");
    buf_add(&G.out, 0, "");
    buf_truncate(&fs.code, 0);
    buf_truncate(&fs.cold, 0);
}

/* Scratch registers are written as NREG..31 of each bank while a function
   is generated. A call zeroes the callee's whole window, which runs to the
   highest register of each bank, so afterwards the scratch registers a
   function uses are moved down to just above the other registers of their
   bank (and its parameters). */
static bool reg_char(char c) { return isalnum((unsigned char)c) || c == '_' || c == '$' || c == '.' || c == '@'; }

static void each_reg(Buf *b, int map[4][32], bool used[4][32]) {
    static const char banks[] = "IFRP";
    for (int i = 0; i < b->n; i++) {
        char *t = b->v[i].text, out[4096];
        size_t o = 0;
        bool changed = false;
        for (char *p = t; *p;) {
            if (*p == '"') {                       /* a string operand */
                char *q = p + 1;
                while (*q && *q != '"')
                    q += q[0] == '\\' && q[1] ? 2 : 1;
                if (*q)
                    q++;
                memcpy(out + o, p, (size_t)(q - p));
                o += (size_t)(q - p);
                p = q;
                continue;
            }
            const char *bk = *p ? strchr(banks, *p) : NULL;
            if (bk && (p == t || !reg_char(p[-1])) && isdigit((unsigned char)p[1])) {
                char *q = p + 1;
                int r = 0;
                while (isdigit((unsigned char)*q))
                    r = r * 10 + (*q++ - '0');
                if (!reg_char(*q) && r < 32) {
                    int bank = (int)(bk - banks);
                    if (used)
                        used[bank][r] = true;
                    o += (size_t)sprintf(out + o, "%c%d", *p, map ? map[bank][r] : r);
                    changed |= map && map[bank][r] != r;
                    p = q;
                    continue;
                }
            }
            out[o++] = *p++;
        }
        out[o] = 0;
        if (changed) {
            free(b->v[i].text);
            b->v[i].text = xstrdup(out);
        }
    }
}

static void pack_scratch(FS *fs) {
    bool used[4][32] = {{false}};
    each_reg(&fs->code, NULL, used);
    each_reg(&fs->cold, NULL, used);
    Func *f = fs->f;
    int np = f->np > MAXARGS ? 0 : f->np;
    int floor[4] = {np, np, f->np > MAXARGS ? 1 : np + (f->vararg ? 1 : 0), 0};
    int map[4][32];
    bool any = false;
    for (int b = 0; b < 4; b++) {
        int lo = floor[b];
        for (int r = 0; r < NREG; r++) {
            map[b][r] = r;
            if (used[b][r] && r + 1 > lo)
                lo = r + 1;
        }
        for (int r = NREG; r < 32; r++) {
            map[b][r] = r;
            if (used[b][r]) {
                map[b][r] = lo++;
                any |= map[b][r] != r;
            }
        }
    }
    if (any) {
        each_reg(&fs->code, map, NULL);
        each_reg(&fs->cold, map, NULL);
    }
}

static bool has_loop(Block *b) {
    if (!b)
        return false;
    for (int i = 0; i < b->n; i++) {
        Stmt *s = b->stmts[i];
        if (s->k == S_WHILE || s->k == S_REPEAT || s->k == S_NUMFOR || s->k == S_GENFOR ||
            (s->k == S_LABEL && s->backward))
            return true;
        if (has_loop(s->body))
            return true;
        if (s->blocks)
            for (int j = 0; j <= s->nconds; j++)
                if (has_loop(s->blocks[j]))
                    return true;
    }
    return false;
}

/* --------------------------------------------------------------- analysis */

typedef VEC(Func *) FuncVec;

static void collect_funcs(Func *f, FuncVec *all) {
    vec_push(*all, f);
    sanitize(f->name);
    for (int i = 0; i < f->children.n; i++)
        collect_funcs(f->children.v[i], all);
}

static bool has_var(VEC(Var *) *set, Var *v) {
    for (int i = 0; i < set->n; i++)
        if (set->v[i] == v)
            return true;
    return false;
}

/* The variables f's closures must capture, given which local functions are
   called directly. */
static void compute_caps(Func *f) {
    for (int i = 0; i < f->children.n; i++)
        compute_caps(f->children.v[i]);
    f->caps.n = 0;
    for (int i = 0; i < f->uprefs.n; i++) {
        UpRef *r = &f->uprefs.v[i];
        Var *v = r->var;
        Func *lf = v->localfn;
        bool direct_call = r->e->callee && lf && !v->assigned && lf->direct && !lf->vararg &&
                           r->e->i == lf->np && !r->e->op;
        if (!direct_call && !has_var((void *)&f->caps, v))
            vec_push(f->caps, v);
    }
    for (int i = 0; i < f->children.n; i++) {
        Func *c = f->children.v[i];
        for (int j = 0; j < c->caps.n; j++) {
            Var *v = c->caps.v[j];
            if (v->fn != f && !has_var((void *)&f->caps, v))
                vec_push(f->caps, v);
        }
    }
}

static void analyze(Func *top, FuncVec *all) {
    collect_funcs(top, all);
    for (int i = 0; i < all->n; i++) {
        Func *f = all->v[i];
        for (int j = 0; j < f->locals.n; j++) {
            Var *v = f->locals.v[j];
            v->boxed = v->captured && v->assigned;
            v->sty = TY_ANY;
            if (v->assigned || v->attrib == ATTR_CLOSE)
                continue;
            if (v->localfn)
                v->sty = TY_FN;
            else if (v->init) {
                switch (v->init->k) {
                case X_TABLE: v->sty = TY_TAB; break;
                case X_FUNC: v->sty = TY_FN; break;
                case X_STR: v->sty = TY_STR; break;
                case X_INT: v->sty = TY_INT; break;
                case X_FLT: v->sty = TY_FLT; break;
                default: break;
                }
            }
        }
    }
    /* functions called directly: start optimistic and drop those that
       turn out to need captures */
    for (int i = 0; i < all->n; i++) {
        Func *f = all->v[i];
        f->direct = false;
    }
    for (int i = 0; i < all->n; i++)
        for (int j = 0; j < all->v[i]->locals.n; j++) {
            Var *v = all->v[i]->locals.v[j];
            if (v->localfn && !v->assigned)
                v->localfn->direct = true;
        }
    for (bool changed = true; changed;) {
        changed = false;
        compute_caps(top);
        for (int i = 0; i < all->n; i++) {
            Func *f = all->v[i];
            if (f->direct && f->caps.n) {
                f->direct = false;
                changed = true;
            }
        }
    }
    for (int i = 0; i < all->n; i++) {
        Func *f = all->v[i];
        int w = 0;
        for (int j = 0; j < f->caps.n; j++)
            w += cap_width(f->caps.v[j]);
        f->capwords = w;
        f->packed = w > MAXCAPWORDS;
    }
}

static void pin_expr(Expr *e);
static void pin_block(Block *b);

static void pin_exprs(Expr **v, int n) {
    for (int i = 0; i < n; i++)
        pin_expr(v[i]);
}

static void pin_expr(Expr *e) {
    if (!e)
        return;
    if (e->k == X_GLOBAL && !e->env)
        pin_name(e->s);
    pin_expr(e->a);
    pin_expr(e->b);
    pin_expr(e->env);
    pin_exprs(e->args, e->nargs);
    for (int i = 0; i < e->nfields; i++) {
        pin_expr(e->fields[i].key);
        pin_expr(e->fields[i].val);
    }
    if (e->fn)
        pin_block(e->fn->body);
}

static void pin_block(Block *b) {
    if (!b)
        return;
    for (int i = 0; i < b->n; i++) {
        Stmt *s = b->stmts[i];
        pin_exprs(s->targets, s->ntargets);
        pin_exprs(s->exprs, s->nexprs);
        pin_expr(s->cond);
        pin_exprs(s->conds, s->nconds);
        if (s->blocks)
            for (int j = 0; j <= s->nconds; j++)
                pin_block(s->blocks[j]);
        pin_block(s->body);
        pin_expr(s->start);
        pin_expr(s->limit);
        pin_expr(s->step);
        if (s->fn)
            pin_block(s->fn->body);
    }
}

/* ---------------------------------------------------------------- helpers */

/* The library: Lua names, the runtime function and its signature. A NULL
   table is _G; "P" is the table of primitives handed to the prelude. */
typedef struct {
    const char *lib, *name, *fn;
    int sig;
} Builtin;

#include "builtins.h"

static void out(FILE *f, int *nlines, VEC(int) *map, int line, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    (*nlines)++;
    vec_push(*map, line);
}

static void gen_helpers(Buf *b) {
    char line[4096];
#define L(...) do { snprintf(line, sizeof line, __VA_ARGS__); buf_add(b, 0, line); } while (0)
    /* rt$call<N>: call any value with N arguments */
    for (int n = 0; n <= MAXARGS; n++) {
        SB params = {0};
        for (int j = 0; j < n; j++)
            sb_add(&params, ", i64, f64, ref");
        L(".func rt$call%d(ref, i64, f64, ref%s) -> (i64, i64, f64, ref)", n, sb_str(&params));
        L("    isa I20, R1, Fn");
        L("    bz I20, 90f");
        L("    mov I21, R1->Fn.sig");
        L("    mov R20, R1->Fn.clo");
        L("    mov.r R21, " NIL);
        L("    blt I21, 100, 10f");
        L("    blt I21, 200, 20f");
        L("    beq I21, 200, 30f");
        L("    sub I22, I21, 300");
        SB jt = {0};
        for (int k = 0; k <= MAXARGS; k++)
            sb_add(&jt, ", .Lp%d", k);
        L("    jtab I22%s", sb_str(&jt));
        L("    halt 70");
        free(jt.s);
        jt = (SB){0};
        for (int k = 0; k <= MAXARGS; k++)
            sb_add(&jt, ", .Lc%d", k);
        L("10: jtab I21%s", sb_str(&jt));
        L("    halt 70");
        free(jt.s);
        jt = (SB){0};
        for (int k = 0; k <= MAXARGS; k++)
            sb_add(&jt, ", .Lv%d", k);
        L("20: sub I22, I21, 100");
        L("    jtab I22%s", sb_str(&jt));
        L("    halt 70");
        free(jt.s);
        for (int k = 0; k <= MAXARGS; k++) {
            /* exactly k parameters: drop extra arguments, pad with nil */
            SB a = {0};
            for (int j = 0; j < k; j++) {
                if (j < n)
                    sb_add(&a, "%sI%d, F%d, R%d", j ? ", " : "", j + 1, j + 1, j + 2);
                else
                    sb_add(&a, "%sI0, F0, R21", j ? ", " : "");
            }
            L(".Lc%d: tcalli R20 (%s)", k, sb_str(&a));
            L(".Lp%d: tcalli R20 (%d%s%s)", k, n, k ? ", " : "", sb_str(&a));
            /* k fixed parameters and a vararg list */
            L(".Lv%d: lnew R22", k);
            for (int j = k; j < n; j++) {
                L("    lpush R22, R%d", j + 2);
                L("    bnz R%d, 1f", j + 2);
                L("    lpush R22, I%d", j + 1);
                L("    jmp 2f");
                L("1:  lpush R22, F%d", j + 1);
                L("2:");
            }
            L("    tcalli R20 (%s%sR22)", sb_str(&a), k ? ", " : "");
            free(a.s);
        }
        /* all arguments in one list */
        L("30: lnew R22");
        for (int j = 0; j < n; j++) {
            L("    lpush R22, R%d", j + 2);
            L("    bnz R%d, 1f", j + 2);
            L("    lpush R22, I%d", j + 1);
            L("    jmp 2f");
            L("1:  lpush R22, F%d", j + 1);
            L("2:");
        }
        L("    tcalli R20 (R22)");
        /* not a function: its __call metamethod, with the arguments in a list */
        L("90: lnew R22");
        for (int j = 0; j < n; j++) {
            L("    lpush R22, R%d", j + 2);
            L("    bnz R%d, 1f", j + 2);
            L("    lpush R22, I%d", j + 1);
            L("    jmp 2f");
            L("1:  lpush R22, F%d", j + 1);
            L("2:");
        }
        L("    tcall rt$apply (I0, F0, R1, R22, R0)");
        L(".endf");
        L("%s", "");
        free(params.s);
    }
    /* rt$apply: call f with the values in a list */
    L(".func rt$apply(i64, f64, ref, ref, ref) -> (i64, i64, f64, ref)");
    L("    isa I20, R0, Fn");
    L("    bz I20, 90f");
    L("    mov I21, R0->Fn.sig");
    L("    mov R20, R0->Fn.clo");
    L("    len I23, R1");
    L("    div I23, 2");
    L("    blt I21, 100, 10f");
    L("    blt I21, 200, 20f");
    L("    beq I21, 200, 30f");
    L("    sub I22, I21, 300");
    {
        SB jt = {0};
        for (int k = 0; k <= MAXARGS; k++)
            sb_add(&jt, ", .Lp%d", k);
        L("    jtab I22%s", sb_str(&jt));
        L("    halt 70");
        free(jt.s);
        jt = (SB){0};
        for (int k = 0; k <= MAXARGS; k++)
            sb_add(&jt, ", .Lc%d", k);
        L("10: jtab I21%s", sb_str(&jt));
        L("    halt 70");
        free(jt.s);
        jt = (SB){0};
        for (int k = 0; k <= MAXARGS; k++)
            sb_add(&jt, ", .Lv%d", k);
        L("20: sub I22, I21, 100");
        L("    jtab I22%s", sb_str(&jt));
        L("    halt 70");
        free(jt.s);
    }
    for (int k = 0; k <= MAXARGS; k++) {
        SB a = {0};
        for (int j = 0; j < k; j++)
            sb_add(&a, "%sI%d, F%d, R%d", j ? ", " : "", j + 1, j + 1, j + 3);
        L(".Lc%d:", k);
        for (int j = 0; j < k; j++)
            L("    call rt$listget (R1, %d) -> (I%d, F%d, R%d)", j, j + 1, j + 1, j + 3);
        L("    tcalli R20 (%s)", sb_str(&a));
        L(".Lp%d:", k);
        for (int j = 0; j < k; j++)
            L("    call rt$listget (R1, %d) -> (I%d, F%d, R%d)", j, j + 1, j + 1, j + 3);
        L("    tcalli R20 (I23%s%s)", k ? ", " : "", sb_str(&a));
        L(".Lv%d:", k);
        for (int j = 0; j < k; j++)
            L("    call rt$listget (R1, %d) -> (I%d, F%d, R%d)", j, j + 1, j + 1, j + 3);
        L("    call rt$sublist (R1, %d) -> (R22)", k);
        L("    tcalli R20 (%s%sR22)", sb_str(&a), k ? ", " : "");
        free(a.s);
    }
    L("30: tcalli R20 (R1)");
    L("90: call rt$getmm (I0, F0, R0, \"__call\") -> (I24, F24, R24)");
    L("    beq.r R24, " NIL ", 99f");
    L("    lnew R22");
    L("    lpush R22, R0");
    L("    bnz R0, 1f");
    L("    lpush R22, I0");
    L("    jmp 2f");
    L("1:  lpush R22, F0");
    L("2:  call rt$appendlist (R22, R1)");
    L("    tcall rt$apply (I24, F24, R24, R22, R2)");
    L("99: call rt$err_call (I0, F0, R0, R2)");
    L("    halt 70");
    L(".endf");
    L("%s", "");
#undef L
}

/* -------------------------------------------------------------- program */

static void gen_chunk(Func *top, Gen *g) {
    (void)g;
    G.env_fast = !top->env->assigned;
    gen_function(top, NULL);
}

void generate(GenInput *in, FILE *f) {
    memset(&G, 0, sizeof G);
    G.chunk = in->chunk;
    FuncVec all = {0};
    analyze(in->prelude, &all);
    analyze(in->main, &all);
    /* the library's globals, then the program's */
    for (size_t i = 0; i < sizeof builtins / sizeof *builtins; i++) {
        const char *lib = builtins[i].lib ? builtins[i].lib : builtins[i].name;
        if (strcmp(lib, "P") != 0)
            pin_name((Str){lib, (int)strlen(lib)});
    }
    const char *extra[] = {"_G", "_VERSION", "arg", "math", "string", "table", "io", "os", "coroutine", "utf8",
                           "debug"};
    for (size_t i = 0; i < sizeof extra / sizeof *extra; i++)
        pin_name((Str){extra[i], (int)strlen(extra[i])});
    pin_block(in->prelude->body);
    pin_block(in->main->body);

    gen_chunk(in->prelude, &G);
    gen_chunk(in->main, &G);

    int nlines = 0;
    VEC(int) map = {0};
    char *chunk = cstr_lit(in->chunk);
    out(f, &nlines, (void *)&map, 0, "; Generated by mlws-vm-luac %s from %s", VERSION, chunk);
    out(f, &nlines, (void *)&map, 0, "%s", "");
    /* the runtime; the library's functions (B$, P$) are -1 in the line map */
    int kind = 0;
    for (const char *p = in->runtime; *p;) {
        const char *e = strchr(p, '\n');
        int len = e ? (int)(e - p) : (int)strlen(p);
        if (strncmp(p, ".func B$", 8) == 0 || strncmp(p, ".func P$", 8) == 0)
            kind = -1;
        out(f, &nlines, (void *)&map, kind, "%.*s", len, p);
        if (strncmp(p, ".endf", 5) == 0)
            kind = 0;
        p += len + (e ? 1 : 0);
    }
    /* initialisation */
    out(f, &nlines, (void *)&map, 0, ".func rt$init_globals");
    for (int i = 0; i < G.pinned.n; i++) {
        char *nm = cstr_lit(G.pinned.v[i]);
        out(f, &nlines, (void *)&map, 0, "    call rt$pin (%s)", nm);
        free(nm);
    }
    out(f, &nlines, (void *)&map, 0, "    ret");
    out(f, &nlines, (void *)&map, 0, ".endf");
    out(f, &nlines, (void *)&map, 0, "%s", "");
    out(f, &nlines, (void *)&map, 0, ".func rt$init_lib");
    const char *cur = (const char *)-1;
    for (size_t i = 0; i < sizeof builtins / sizeof *builtins; i++) {
        const Builtin *b = &builtins[i];
        if (b->lib != cur && (!b->lib || !cur || cur == (const char *)-1 || strcmp(b->lib, cur) != 0)) {
            cur = b->lib;
            if (!cur)
                out(f, &nlines, (void *)&map, 0, "    mov R1, @rt$G");
            else if (strcmp(cur, "P") == 0)
                out(f, &nlines, (void *)&map, 0, "    mov R1, @rt$P");
            else {
                char *nm = cstr_lit(cur);
                out(f, &nlines, (void *)&map, 0, "    call rt$deflib (%s) -> (R1)", nm);
                free(nm);
            }
        }
        char *nm = cstr_lit(b->name);
        out(f, &nlines, (void *)&map, 0, "    closure %s -> (R2)", b->fn);
        out(f, &nlines, (void *)&map, 0, "    call rt$defn (R1, %s, R2, %d)", nm, b->sig);
        free(nm);
    }
    out(f, &nlines, (void *)&map, 0, "    ret");
    out(f, &nlines, (void *)&map, 0, ".endf");
    out(f, &nlines, (void *)&map, 0, "%s", "");
    /* the inline caches, each empty: a shape no table has (nil), and a word
       index past any table's slots */
    out(f, &nlines, (void *)&map, 0, ".func rt$init_ic");
    for (int i = 0; i < G.nic; i++)
        out(f, &nlines, (void *)&map, 0, "    mov.r @ic$%ds, " NIL, i);
    out(f, &nlines, (void *)&map, 0, "    ret");
    out(f, &nlines, (void *)&map, 0, ".endf");
    out(f, &nlines, (void *)&map, 0, "%s", "");
    out(f, &nlines, (void *)&map, 0, ".func main -> i64");
    out(f, &nlines, (void *)&map, 0, "    call rt$init (%s)", chunk);
    out(f, &nlines, (void *)&map, 0, "    call rt$init_ic");
    out(f, &nlines, (void *)&map, 0, "    call rt$init_shapes (%d)", G.nctor);
    out(f, &nlines, (void *)&map, 0, "    call rt$init_globals");
    out(f, &nlines, (void *)&map, 0, "    call rt$init_lib");
    out(f, &nlines, (void *)&map, 0, "    call rt$init_done");
    out(f, &nlines, (void *)&map, 0, ".try 8f");
    out(f, &nlines, (void *)&map, 0, "    lnew R1");
    out(f, &nlines, (void *)&map, 0, "    lpush R1, @rt$P, 0.0");
    out(f, &nlines, (void *)&map, 0, "    call L%d$%s (R1) -> (I0, I1, F1, R2)", in->prelude->id, in->prelude->name);
    out(f, &nlines, (void *)&map, 0, "    call rt$script_args -> (R1)");
    out(f, &nlines, (void *)&map, 0, "    call L%d$%s (R1) -> (I0, I1, F1, R2)", in->main->id, in->main->name);
    out(f, &nlines, (void *)&map, 0, ".endtry");
    out(f, &nlines, (void *)&map, 0, "    call rt$exit (0)");
    out(f, &nlines, (void *)&map, 0, "    ret 0");
    out(f, &nlines, (void *)&map, 0, "8:  catch R1");
    out(f, &nlines, (void *)&map, 0, "    call rt$uncaught (R1)");
    out(f, &nlines, (void *)&map, 0, "    ret 1");
    out(f, &nlines, (void *)&map, 0, ".endf");
    out(f, &nlines, (void *)&map, 0, "%s", "");
    Buf helpers = {0};
    gen_helpers(&helpers);
    for (int i = 0; i < helpers.n; i++)
        out(f, &nlines, (void *)&map, 0, "%s", helpers.v[i].text);
    for (int i = 0; i < G.out.n; i++)
        out(f, &nlines, (void *)&map, G.out.v[i].line, "%s", G.out.v[i].text);
    for (int i = 0; i < G.data.n; i++)
        out(f, &nlines, (void *)&map, 0, "%s", G.data.v[i].text);
    out(f, &nlines, (void *)&map, 0, "        .align 8");
    for (int i = 0; i < G.nic; i++) {
        out(f, &nlines, (void *)&map, 0, "ic$%ds: .zero 8", i);
        out(f, &nlines, (void *)&map, 0, "ic$%dw: .i64 1099511627776", i);
    }
    for (int i = 0; i < G.nmc; i++)
        out(f, &nlines, (void *)&map, 0, "mc$%d: .zero 8", i);
    for (int i = 0; i < G.nctor; i++)
        out(f, &nlines, (void *)&map, 0, "ct$%d: .zero 8", i);
    /* the line map: Lua lines of the lines above */
    int n = map.n;
    fprintf(f, "\n        .align 8\nrt$nlines: .i64 %d\nrt$linemap:\n", n);
    for (int i = 0; i < n; i += 16) {
        fprintf(f, "        .i32 ");
        for (int j = i; j < n && j < i + 16; j++)
            fprintf(f, "%s%d", j > i ? ", " : "", map.v[j]);
        fprintf(f, "\n");
    }
    free(chunk);
}
