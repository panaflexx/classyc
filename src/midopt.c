/* src/midopt.c — Mid-level optimizer (check → gen) for ClassyC.
 *
 * STATUS: Phase B–E of GEN-OPT.md — method-level dead pruning (no whole-class
 * expand), gen protocol stamps, accessor inline marks, P1 guard elision.
 *
 * Single-TU build model: this file is `#include`d from `src/classyc.c`
 * next to `ownership.c`, so it sees internal types (`c2m_ctx_t`, `node_t`,
 * `decl_t`, `N_*`, traversal macros, `c2m_options`, …).  It MUST NOT be
 * added to CMakeLists.txt as an independent translation unit.
 *
 * Pipeline position:
 *     parse → check → ownership → [midopt] → gen → MIR
 *                                 ^^^^^^^^
 *                                 this file
 *
 * Goals:
 *   P0 — Mark unreachable *class methods* `midopt_dead_p` so gen skips
 *        bodies and MIR forwards. Free functions always stay (C linkage).
 *        Reachability is method-level (free-func seed → worklist) plus
 *        gen-time protocol stamps (for-in, class[i], helpers).  Whole-class
 *        expand is NOT used (it pinned entire monomorph public APIs).
 *   P1 — Local nullness + integer intervals: stamp SAFE / elide_oob when
 *        proven; warn (or -fsafety-errors) on definite null/OOB/div0/shift.
 *
 * Flags:
 *   default on; `-fno-midopt` skips; `-fsafety-errors` promotes diagnostics
 *   to errors; `-v` prints keep/dead + safety counts.
 */

/* ── helpers ─────────────────────────────────────────────────────────────── */

static int midopt_node_has_ops (node_code_t code) {
  switch (code) {
  case N_I: case N_L: case N_LL: case N_U: case N_UL: case N_ULL:
  case N_F: case N_D: case N_LD:
  case N_CH: case N_CH16: case N_CH32:
  case N_STR: case N_STR16: case N_STR32: case N_ID:
  case N_IGNORE:
    return 0;
  default:
    return 1;
  }
}

/* True if this FUNC_DEF is a class method (has class_scope on its type). */
static int midopt_class_method_p (node_t func_def) {
  decl_t d;
  struct type *t;
  struct func_type *ft;

  if (func_def == NULL || func_def->code != N_FUNC_DEF) return 0;
  if (func_def->attr == NULL || func_def->attr == (void *) ((intptr_t) -1)) return 0;
  d = (decl_t) func_def->attr;
  t = d->decl_spec.type;
  if (t == NULL || t->mode != TM_FUNC) return 0;
  ft = t->u.func_type;
  return ft != NULL && ft->class_scope != NULL && ft->class_scope->code == N_CLASS;
}

static const char *midopt_func_name (node_t func_def) {
  node_t declarator, id;

  if (func_def == NULL || func_def->code != N_FUNC_DEF) return NULL;
  declarator = FUNC_DEF_DECL (func_def);
  if (declarator == NULL || declarator->code != N_DECL) return NULL;
  id = NL_HEAD (declarator->u.ops);
  if (id == NULL || id->code != N_ID) return NULL;
  return id->u.s.s;
}

/* ── keep-set for methods (side table of node_t FUNC_DEF) ──────────────────
   Reuses existing VARR(node_t) from classyc.c (already DEF_VARR'd). */

static VARR (node_t) * midopt_keep;
static VARR (node_t) * midopt_work;
static int midopt_verbose_p;

static int midopt_keep_has (node_t f) {
  size_t i, n;

  if (f == NULL || midopt_keep == NULL) return 0;
  n = VARR_LENGTH (node_t, midopt_keep);
  for (i = 0; i < n; i++)
    if (VARR_GET (node_t, midopt_keep, i) == f) return 1;
  return 0;
}

static void midopt_mark_keep (node_t f) {
  decl_t d;

  if (f == NULL || f->code != N_FUNC_DEF) return;
  if (f->attr == NULL || f->attr == (void *) ((intptr_t) -1)) return;
  if (!midopt_class_method_p (f)) return; /* free funcs not tracked as dead */
  if (midopt_keep_has (f)) return;
  VARR_PUSH (node_t, midopt_keep, f);
  VARR_PUSH (node_t, midopt_work, f);
  d = (decl_t) f->attr;
  d->midopt_dead_p = FALSE;
  d->used_p = TRUE; /* consistent with check-time used_p for refs */
  if (midopt_verbose_p) {
    const char *nm = midopt_func_name (f);
    fprintf (stderr, "  [midopt] keep method %s\n", nm != NULL ? nm : "?");
  }
}

/* Only nodes whose ->attr is a struct expr * after check. */
static int midopt_expr_node_p (node_code_t code) {
  switch (code) {
  case N_I: case N_L: case N_LL: case N_U: case N_UL: case N_ULL:
  case N_F: case N_D: case N_LD: case N_CH: case N_CH16: case N_CH32:
  case N_STR: case N_STR16: case N_STR32: case N_ID:
  case N_COMMA: case N_ANDAND: case N_OROR: case N_EQ: case N_NE:
  case N_LT: case N_LE: case N_GT: case N_GE:
  case N_ASSIGN: case N_BITWISE_NOT: case N_NOT:
  case N_AND: case N_AND_ASSIGN: case N_OR: case N_OR_ASSIGN:
  case N_XOR: case N_XOR_ASSIGN: case N_LSH: case N_LSH_ASSIGN:
  case N_RSH: case N_RSH_ASSIGN: case N_ADD: case N_ADD_ASSIGN:
  case N_SUB: case N_SUB_ASSIGN: case N_MUL: case N_MUL_ASSIGN:
  case N_DIV: case N_DIV_ASSIGN: case N_MOD: case N_MOD_ASSIGN:
  case N_IND: case N_FIELD: case N_ADDR: case N_DEREF: case N_DEREF_FIELD:
  case N_COND: case N_INC: case N_DEC: case N_POST_INC: case N_POST_DEC:
  case N_ALIGNOF: case N_SIZEOF: case N_EXPR_SIZEOF: case N_CAST:
  case N_COMPOUND_LITERAL: case N_CALL: case N_GENERIC: case N_STMTEXPR:
  case N_NEW: case N_DELETE: case N_LAMBDA: case N_CONCAT: case N_DICT:
  case N_IN: case N_COALESCE: case N_MOVE: case N_READONLY: case N_DETACH:
    return 1;
  default:
    return 0;
  }
}

static void midopt_mark_from_expr_node (node_t n) {
  struct expr *e;
  node_t def;

  if (n == NULL || !midopt_expr_node_p (n->code)) return;
  if (n->attr == NULL || n->attr == (void *) ((intptr_t) -1)) return;
  e = (struct expr *) n->attr;
  /* After check, real exprs have a type.  Skip incomplete/error attrs. */
  if (e->type == NULL) return;
  def = e->def_node;
  if (def == NULL) return;
  /* N_DETACH stashes runtime selectors as tiny integer sentinels, not nodes:
       (node_t)1 — string detach, (node_t)2 — object detach. */
  if (def == (node_t) (intptr_t) 1 || def == (node_t) (intptr_t) 2) return;
  if (def->code == N_FUNC_DEF) midopt_mark_keep (def);
}

/* Keep every overload of NAME on CLASS_TAG (same-class call-graph fill-in). */
static void midopt_mark_named_on_class (c2m_ctx_t c2m_ctx, node_t class_tag, const char *name,
                                       pos_t pos) {
  symbol_t sym;
  node_t id;
  size_t i;

  if (class_tag == NULL || name == NULL || name[0] == '\0') return;
  id = build_id (c2m_ctx, name, pos);
  if (!find_overload_sym (c2m_ctx, id, class_tag, &sym)) return;
  for (i = 0; i < VARR_LENGTH (node_t, sym.defs); i++) {
    node_t def = VARR_GET (node_t, sym.defs, i);
    decl_t d;
    struct func_type *ft;

    if (def == NULL || def->code != N_FUNC_DEF || def->attr == NULL) continue;
    if (def->attr == (void *) ((intptr_t) -1)) continue;
    d = (decl_t) def->attr;
    if (d->decl_spec.type == NULL || d->decl_spec.type->mode != TM_FUNC) continue;
    ft = d->decl_spec.type->u.func_type;
    if (ft == NULL || ft->class_scope != class_tag) continue;
    midopt_mark_keep (def);
  }
}

/* ── AST walk: collect uses / apply elisions ─────────────────────────────── */

static void midopt_collect_uses (c2m_ctx_t c2m_ctx, node_t n, node_t class_tag);
static void midopt_elide_walk (c2m_ctx_t c2m_ctx, node_t n);

static void midopt_collect_uses (c2m_ctx_t c2m_ctx, node_t n, node_t class_tag) {
  if (n == NULL) return;

  /* Method references stashed on expression nodes only (never scopes/decls). */
  midopt_mark_from_expr_node (n);

  /* Same-class call by name: this.Foo() / this->Foo() may lack def_node on
     some monomorph paths; mark all Foo overloads on the enclosing class. */
  if (class_tag != NULL && n->code == N_CALL) {
    node_t func = NL_HEAD (n->u.ops);
    if (func != NULL && (func->code == N_FIELD || func->code == N_DEREF_FIELD)) {
      node_t mid = NL_NEXT (NL_HEAD (func->u.ops));
      if (mid != NULL && mid->code == N_ID)
        midopt_mark_named_on_class (c2m_ctx, class_tag, mid->u.s.s, POS (n));
    }
  }

  /* Stack RAII ctor/dtor calls on locals. */
  if (n->code == N_SPEC_DECL && n->attr != NULL
      && n->attr != (void *) ((intptr_t) -1)) {
    decl_t d = (decl_t) n->attr;
    if (d->ctor_call != NULL) midopt_collect_uses (c2m_ctx, d->ctor_call, class_tag);
    if (d->dtor_call != NULL) midopt_collect_uses (c2m_ctx, d->dtor_call, class_tag);
    if (d->auto_release_call != NULL)
      midopt_collect_uses (c2m_ctx, d->auto_release_call, class_tag);
  }

  /* for-in over classes: gen may call Count/Get or KeyAt/ValAt, or open-code
     dense field loads (List/Set data+length/dense+count, Map keys+vals+count).
     Only stamp protocol methods when gen will actually call them. */
  if (n->code == N_FORIN) {
    node_t coll = NL_EL (n->u.ops, 3);
    struct expr *ce;
    struct type *ct, *cls;
    node_t tag, m;
    decl_t arr_f, len_f, keys_f, vals_f, count_f;
    int dense_list, dense_map;

    if (coll != NULL && coll->attr != NULL) {
      ce = (struct expr *) coll->attr;
      ct = ce->type;
      if (ct != NULL) {
        if (ct->mode == TM_PTR && ct->u.ptr_type != NULL
            && ct->u.ptr_type->mode == TM_CLASS)
          cls = ct->u.ptr_type;
        else if (ct->mode == TM_CLASS)
          cls = ct;
        else
          cls = NULL;
        if (cls != NULL && cls->u.tag_type != NULL) {
          tag = cls->u.tag_type;
          keys_f = find_class_field_by_name (tag, "keys");
          vals_f = find_class_field_by_name (tag, "vals");
          count_f = find_class_field_by_name (tag, "count");
          dense_map = (count_f != NULL && keys_f != NULL && vals_f != NULL
                       && keys_f->decl_spec.type != NULL
                       && keys_f->decl_spec.type->mode == TM_PTR
                       && vals_f->decl_spec.type != NULL
                       && vals_f->decl_spec.type->mode == TM_PTR);
          dense_list = !dense_map && find_dense_buffer_fields (tag, &arr_f, &len_f, NULL);
          if (dense_list || dense_map) {
            /* gen open-codes for-in — no Count/Get/KeyAt/ValAt MIR needed. */
          } else {
            m = find_class_protocol_method (c2m_ctx, tag, "Count", 0, POS (n));
            if (m) midopt_mark_keep (m);
            m = find_class_protocol_method (c2m_ctx, tag, "Get", 1, POS (n));
            if (m) midopt_mark_keep (m);
            m = find_class_protocol_method (c2m_ctx, tag, "KeyAt", 1, POS (n));
            if (m) midopt_mark_keep (m);
            m = find_class_protocol_method (c2m_ctx, tag, "ValAt", 1, POS (n));
            if (m) midopt_mark_keep (m);
          }
        }
      }
    }
  }

  /* class[i] / class[k] lowers at gen to Get or GetMut (def_node / protocol). */
  if (n->code == N_IND && n->attr != NULL && n->attr != (void *) ((intptr_t) -1)) {
    struct expr *e = (struct expr *) n->attr;
    node_t def = e->def_node;
    if (def != NULL && def != (node_t) (intptr_t) 1 && def != (node_t) (intptr_t) 2
        && def->code == N_FUNC_DEF)
      midopt_mark_keep (def);
    else if (e->type != NULL) {
      node_t arr = NL_HEAD (n->u.ops);
      struct expr *ae = arr != NULL ? arr->attr : NULL;
      struct type *at = ae != NULL ? ae->type : NULL;
      struct type *cls = NULL;
      if (at != NULL && at->mode == TM_CLASS)
        cls = at;
      else if (at != NULL && at->mode == TM_PTR && at->u.ptr_type != NULL
               && at->u.ptr_type->mode == TM_CLASS)
        cls = at->u.ptr_type;
      if (cls != NULL && cls->u.tag_type != NULL) {
        node_t m;
        const char *proto = e->mut_sub_p ? "GetMut" : "Get";
        m = find_class_protocol_method (c2m_ctx, cls->u.tag_type, proto, 1, POS (n));
        if (m) midopt_mark_keep (m);
        if (e->mut_sub_p) {
          m = find_class_protocol_method (c2m_ctx, cls->u.tag_type, "Get", 1, POS (n));
          if (m) midopt_mark_keep (m);
        }
      }
    }
  }

  /* class[i] = v / map[k] = v → Set protocol at gen. */
  if (n->code == N_ASSIGN) {
    node_t lhs = NL_HEAD (n->u.ops);
    if (lhs != NULL && lhs->code == N_IND) {
      node_t arr = NL_HEAD (lhs->u.ops);
      struct expr *ae = arr != NULL ? arr->attr : NULL;
      struct type *at = ae != NULL ? ae->type : NULL;
      struct type *cls = NULL;
      if (at != NULL && at->mode == TM_CLASS)
        cls = at;
      else if (at != NULL && at->mode == TM_PTR && at->u.ptr_type != NULL
               && at->u.ptr_type->mode == TM_CLASS)
        cls = at->u.ptr_type;
      if (cls != NULL && cls->u.tag_type != NULL) {
        node_t m = find_class_protocol_method (c2m_ctx, cls->u.tag_type, "Set", 2, POS (n));
        if (m) midopt_mark_keep (m);
      }
    }
  }

  /* new T{e1,e2,...} → gen emits ctor + Add(ei) per element (no N_CALL for Add). */
  if (n->code == N_NEW && n->attr != NULL && n->attr != (void *) ((intptr_t) -1)) {
    struct expr *ne = (struct expr *) n->attr;
    node_t type_id = NL_HEAD (n->u.ops);
    node_t arg_list = type_id != NULL ? NL_NEXT (type_id) : NULL;
    node_t init_list = arg_list != NULL ? NL_NEXT (arg_list) : NULL;
    struct type *cls = NULL;

    if (ne->type != NULL && ne->type->mode == TM_PTR && ne->type->u.ptr_type != NULL
        && ne->type->u.ptr_type->mode == TM_CLASS)
      cls = ne->type->u.ptr_type;
    if (cls != NULL && cls->u.tag_type != NULL && init_list != NULL
        && init_list->code == N_LIST) {
      node_t m = find_class_protocol_method (c2m_ctx, cls->u.tag_type, "Add", 1, POS (n));
      if (m) midopt_mark_keep (m);
    }
  }

  /* delete p: r->attr is the resolved destructor N_FUNC_DEF (or NULL / -1). */
  if (n->code == N_DELETE) {
    node_t dtor = (node_t) n->attr;
    if (dtor != NULL && dtor != (node_t) (intptr_t) -1 && dtor->code == N_FUNC_DEF)
      midopt_mark_keep (dtor);
  }

  if (!midopt_node_has_ops (n->code)) return;
  for (node_t c = NL_HEAD (n->u.ops); c != NULL; c = NL_NEXT (c))
    midopt_collect_uses (c2m_ctx, c, class_tag);
}

/* Walk only a function body (for worklist propagation). */
static void midopt_collect_uses_body (c2m_ctx_t c2m_ctx, node_t func_def) {
  node_t block;
  node_t class_tag = NULL;
  decl_t d;
  struct func_type *ft;

  if (func_def == NULL || func_def->code != N_FUNC_DEF) return;
  if (func_def->attr != NULL && func_def->attr != (void *) ((intptr_t) -1)) {
    d = (decl_t) func_def->attr;
    if (d->decl_spec.type != NULL && d->decl_spec.type->mode == TM_FUNC) {
      ft = d->decl_spec.type->u.func_type;
      if (ft != NULL && ft->class_scope != NULL && ft->class_scope->code == N_CLASS)
        class_tag = ft->class_scope;
    }
  }
  block = FUNC_DEF_BLOCK (func_def);
  midopt_collect_uses (c2m_ctx, block, class_tag);
  midopt_collect_uses (c2m_ctx, FUNC_DEF_DECLS (func_def), class_tag);
}

/* ── P1 safety lattice: local nullness + integer intervals ─────────────────
 *
 * Dual use:
 *   proven SAFE   → stamp elide_oob_p / DEREF_GUARD_SAFE (gen drops traps)
 *   proven UNSAFE → warning (or error with -fsafety-errors)
 *   unknown       → leave gen traps alone
 *
 * Scope: per-function forward walk with simple if-join (not full CFG).  Tracks
 * locals by their declaration node.  Sufficient for definite null/OOB paths
 * and for eliding traps on stack bases / const in-range indexes. */

enum midopt_null { MN_TOP = 0, MN_NULL = 1, MN_NONNULL = 2 };

#define MIDOPT_FACT_MAX 160

struct midopt_fact {
  node_t decl; /* N_ID def / SPEC_DECL attr identity */
  enum midopt_null nullness;
  int ival_p; /* integer interval known */
  mir_llong lo, hi;
};

struct midopt_env {
  struct midopt_fact f[MIDOPT_FACT_MAX];
  int n;
};

static int midopt_safety_n_warn;
static int midopt_safety_n_elide;

static void midopt_safety_report (c2m_ctx_t c2m_ctx, pos_t pos, const char *fmt, ...) {
  va_list args;
  char buf[512];
  va_start (args, fmt);
  vsnprintf (buf, sizeof buf, fmt, args);
  va_end (args);
  midopt_safety_n_warn++;
  if (c2m_options != NULL && c2m_options->safety_errors_p)
    error (c2m_ctx, pos, "%s", buf);
  else
    warning (c2m_ctx, pos, "%s", buf);
}

static node_t midopt_id_decl (node_t id) {
  struct expr *e;
  node_t def;
  if (id == NULL || id->code != N_ID || id->attr == NULL) return NULL;
  e = (struct expr *) id->attr;
  def = e->def_node;
  if (def == NULL) return NULL;
  if (def == (node_t) (intptr_t) 1 || def == (node_t) (intptr_t) 2) return NULL;
  /* Normalize: symbol may point at N_DECL; facts are stored on N_SPEC_DECL. */
  if (def->code == N_DECL) {
    /* parent not stored; leave as N_DECL — we also store on SPEC_DECL at init. */
    return def;
  }
  return def;
}

static struct midopt_fact *midopt_env_find (const struct midopt_env *env, node_t decl) {
  int i;
  if (decl == NULL || env == NULL) return NULL;
  for (i = 0; i < env->n; i++)
    if (env->f[i].decl == decl) return (struct midopt_fact *) &env->f[i];
  return NULL;
}

static struct midopt_fact *midopt_env_get (struct midopt_env *env, node_t decl) {
  struct midopt_fact *p = midopt_env_find (env, decl);
  if (p != NULL) return p;
  if (env->n >= MIDOPT_FACT_MAX) return NULL;
  p = &env->f[env->n++];
  p->decl = decl;
  p->nullness = MN_TOP;
  p->ival_p = 0;
  p->lo = p->hi = 0;
  return p;
}

static void midopt_env_copy (struct midopt_env *dst, const struct midopt_env *src) {
  *dst = *src;
}

/* Lattice join (least upper bound).  MN_TOP is *unknown* (top), not bottom:
   TOP ⊔ x = TOP.  (Earlier this treated TOP as identity and turned
   `if (!p) return; *p` into a false "definite null".) */
static enum midopt_null midopt_null_join (enum midopt_null a, enum midopt_null b) {
  if (a == b) return a;
  return MN_TOP;
}

static void midopt_env_join (struct midopt_env *dst, const struct midopt_env *a,
                             const struct midopt_env *b) {
  int i;
  struct midopt_env out;
  out.n = 0;
  /* Union of keys from a and b */
  for (i = 0; i < a->n; i++) {
    const struct midopt_fact *fa = &a->f[i];
    const struct midopt_fact *fb = midopt_env_find ((struct midopt_env *) b, fa->decl);
    struct midopt_fact *fo = midopt_env_get (&out, fa->decl);
    if (fo == NULL) continue;
    if (fb == NULL) {
      *fo = *fa; /* only on one path → TOP for safety? keep a (conservative TOP) */
      fo->nullness = MN_TOP;
      fo->ival_p = 0;
    } else {
      fo->nullness = midopt_null_join (fa->nullness, fb->nullness);
      if (fa->ival_p && fb->ival_p) {
        fo->ival_p = 1;
        fo->lo = fa->lo < fb->lo ? fa->lo : fb->lo;
        fo->hi = fa->hi > fb->hi ? fa->hi : fb->hi;
      } else {
        fo->ival_p = 0;
      }
    }
  }
  for (i = 0; i < b->n; i++) {
    if (midopt_env_find (a, b->f[i].decl) != NULL) continue;
    struct midopt_fact *fo = midopt_env_get (&out, b->f[i].decl);
    if (fo == NULL) continue;
    fo->nullness = MN_TOP;
    fo->ival_p = 0;
  }
  *dst = out;
}

static int midopt_expr_is_null_const (node_t n) {
  struct expr *e;
  if (n == NULL || n->attr == NULL) return 0;
  e = (struct expr *) n->attr;
  if (!e->const_p) return 0;
  if (e->type != NULL && floating_type_p (e->type)) return 0;
  return e->c.u_val == 0;
}

static int midopt_expr_is_nonnull_const (node_t n) {
  struct expr *e;
  if (n == NULL || n->attr == NULL) return 0;
  e = (struct expr *) n->attr;
  if (!e->const_p) return 0;
  if (e->type != NULL && floating_type_p (e->type)) return 0;
  return e->c.u_val != 0;
}

/* Classify an rvalue expression's nullness without env (structural). */
static enum midopt_null midopt_expr_nullness_struct (node_t n) {
  if (n == NULL) return MN_TOP;
  if (n->code == N_ADDR) return MN_NONNULL; /* &x never null */
  /* N_NEW is *not* treated as NONNULL here: ownership/object-guards track
     MaybeOwned after conditional free; over-stamping SAFE would hide UAF. */
  if (n->code == N_STR || n->code == N_STR16 || n->code == N_STR32) return MN_NONNULL;
  if (midopt_expr_is_null_const (n)) return MN_NULL;
  if (n->code == N_CAST) {
    node_t inner = NL_EL (n->u.ops, 1);
    return midopt_expr_nullness_struct (inner);
  }
  return MN_TOP;
}

static enum midopt_null midopt_expr_nullness (struct midopt_env *env, node_t n) {
  enum midopt_null sn = midopt_expr_nullness_struct (n);
  node_t decl;
  struct midopt_fact *f;
  if (sn != MN_TOP) return sn;
  decl = midopt_id_decl (n);
  if (decl == NULL) return MN_TOP;
  f = midopt_env_find (env, decl);
  if (f == NULL) return MN_TOP;
  return f->nullness;
}

static int midopt_expr_ival (struct midopt_env *env, node_t n, mir_llong *lo, mir_llong *hi) {
  struct expr *e;
  node_t decl;
  struct midopt_fact *f;
  if (n == NULL || n->attr == NULL) return 0;
  e = (struct expr *) n->attr;
  if (e->const_p && e->type != NULL && integer_type_p (e->type)) {
    *lo = *hi = (mir_llong) e->c.i_val;
    return 1;
  }
  decl = midopt_id_decl (n);
  if (decl == NULL) return 0;
  f = midopt_env_find (env, decl);
  if (f == NULL || !f->ival_p) return 0;
  *lo = f->lo;
  *hi = f->hi;
  return 1;
}

static void midopt_set_null (struct midopt_env *env, node_t decl, enum midopt_null nn) {
  struct midopt_fact *f = midopt_env_get (env, decl);
  if (f == NULL) return;
  f->nullness = nn;
}

static void midopt_set_ival (struct midopt_env *env, node_t decl, mir_llong lo, mir_llong hi) {
  struct midopt_fact *f = midopt_env_get (env, decl);
  if (f == NULL) return;
  f->ival_p = 1;
  f->lo = lo;
  f->hi = hi;
}

static void midopt_kill_ival (struct midopt_env *env, node_t decl) {
  struct midopt_fact *f = midopt_env_find (env, decl);
  if (f != NULL) f->ival_p = 0;
}

/* Refine env for then-branch of `if (cond)`.  then_p=1 → then arm; 0 → else. */
static void midopt_refine_cond (struct midopt_env *env, node_t cond, int then_p) {
  node_t decl, a, b;
  if (cond == NULL) return;
  /* if (p) / if (!p) */
  if (cond->code == N_ID) {
    decl = midopt_id_decl (cond);
    if (decl != NULL) midopt_set_null (env, decl, then_p ? MN_NONNULL : MN_NULL);
    return;
  }
  if (cond->code == N_NOT) {
    midopt_refine_cond (env, NL_HEAD (cond->u.ops), !then_p);
    return;
  }
  /* p == 0 / 0 == p / p != 0 */
  if (cond->code == N_EQ || cond->code == N_NE) {
    a = NL_HEAD (cond->u.ops);
    b = NL_NEXT (a);
    if (a == NULL || b == NULL) return;
    if (midopt_expr_is_null_const (b) && a->code == N_ID) {
      decl = midopt_id_decl (a);
      if (decl != NULL) {
        if (cond->code == N_EQ)
          midopt_set_null (env, decl, then_p ? MN_NULL : MN_NONNULL);
        else
          midopt_set_null (env, decl, then_p ? MN_NONNULL : MN_NULL);
      }
    } else if (midopt_expr_is_null_const (a) && b->code == N_ID) {
      decl = midopt_id_decl (b);
      if (decl != NULL) {
        if (cond->code == N_EQ)
          midopt_set_null (env, decl, then_p ? MN_NULL : MN_NONNULL);
        else
          midopt_set_null (env, decl, then_p ? MN_NONNULL : MN_NULL);
      }
    }
  }
}

static void midopt_safety_stmt (c2m_ctx_t c2m_ctx, node_t n, struct midopt_env *env);

/* Apply assignment of `rhs` into `lhs` (N_ID or *p etc.). */
static void midopt_on_assign (struct midopt_env *env, node_t lhs, node_t rhs) {
  node_t decl;
  enum midopt_null nn;
  mir_llong lo, hi;
  if (lhs == NULL) return;
  decl = midopt_id_decl (lhs);
  if (decl == NULL) return;
  nn = midopt_expr_nullness (env, rhs);
  if (nn != MN_TOP)
    midopt_set_null (env, decl, nn);
  else {
    /* unknown pointer store kills nullness */
    struct midopt_fact *f = midopt_env_find (env, decl);
    if (f != NULL) f->nullness = MN_TOP;
  }
  if (midopt_expr_ival (env, rhs, &lo, &hi))
    midopt_set_ival (env, decl, lo, hi);
  else
    midopt_kill_ival (env, decl);
}

static void midopt_check_deref (c2m_ctx_t c2m_ctx, node_t n, struct midopt_env *env) {
  struct expr *e;
  node_t recv;
  enum midopt_null nn;

  if (n == NULL || n->attr == NULL) return;
  if (n->code != N_DEREF && n->code != N_DEREF_FIELD && n->code != N_IND) return;
  e = (struct expr *) n->attr;
  if (e->type == NULL) return;
  recv = NL_HEAD (n->u.ops);
  if (recv == NULL) return;

  /* Structural non-null bases */
  if (recv->code == N_ID && recv->u.s.s != NULL && strcmp (recv->u.s.s, "this") == 0) {
    if (e->own_deref_class != DEREF_GUARD_SAFE) {
      e->own_deref_class = DEREF_GUARD_SAFE;
      midopt_safety_n_elide++;
    }
    return;
  }
  if (recv->code == N_ADDR) {
    if (e->own_deref_class != DEREF_GUARD_SAFE) {
      e->own_deref_class = DEREF_GUARD_SAFE;
      midopt_safety_n_elide++;
    }
    return;
  }

  nn = midopt_expr_nullness (env, recv);
  if (nn == MN_NULL) {
    midopt_safety_report (c2m_ctx, POS (n),
                          "definite null dereference (pointer proven null on this path)");
  } else if (nn == MN_NONNULL) {
    /* Do not override ownership CHECK (MaybeOwned → object-guard UAF path). */
    if (e->own_deref_class == DEREF_GUARD_DEFAULT) {
      e->own_deref_class = DEREF_GUARD_SAFE;
      midopt_safety_n_elide++;
    }
  }
}

static void midopt_check_ind_oob (c2m_ctx_t c2m_ctx, node_t n, struct midopt_env *env) {
  node_t arr, idx;
  struct expr *e, *ae;
  struct type *arr_type;
  mir_llong lo, hi, len;
  node_t sz_node;
  struct expr *sze;

  if (n == NULL || n->code != N_IND || n->attr == NULL) return;
  e = (struct expr *) n->attr;
  if (e->type == NULL) return;
  arr = NL_HEAD (n->u.ops);
  idx = NL_NEXT (arr);
  if (arr == NULL || idx == NULL) return;
  ae = arr->attr;
  if (ae == NULL || ae->type == NULL) return;
  arr_type = ae->type;

  if (!(arr_type->mode == TM_PTR && arr_type->arr_type != NULL
        && arr_type->arr_type->mode == TM_ARR))
    return;
  sz_node = arr_type->arr_type->u.arr_type->size;
  if (sz_node == NULL || sz_node->code == N_IGNORE || sz_node->attr == NULL) return;
  sze = (struct expr *) sz_node->attr;
  if (!sze->const_p || sze->c.i_val <= 0) return;
  len = sze->c.i_val;

  if (!midopt_expr_ival (env, idx, &lo, &hi)) return;

  /* Entire interval in [0, len) → elide */
  if (lo >= 0 && hi < len) {
    if (!e->elide_oob_p) {
      e->elide_oob_p = 1;
      midopt_safety_n_elide++;
    }
    if (e->own_deref_class == DEREF_GUARD_DEFAULT)
      e->own_deref_class = DEREF_GUARD_SAFE;
    return;
  }
  /* Entire interval outside → definite OOB */
  if (hi < 0 || lo >= len) {
    midopt_safety_report (c2m_ctx, POS (n),
                          "definite out-of-bounds index (index in [%lld,%lld], length %lld)",
                          (long long) lo, (long long) hi, (long long) len);
  }
}

static void midopt_check_div (c2m_ctx_t c2m_ctx, node_t n, struct midopt_env *env) {
  node_t rhs;
  mir_llong lo, hi;
  if (n == NULL) return;
  if (n->code != N_DIV && n->code != N_MOD && n->code != N_DIV_ASSIGN && n->code != N_MOD_ASSIGN)
    return;
  rhs = (n->code == N_DIV || n->code == N_MOD) ? NL_EL (n->u.ops, 1) : NL_EL (n->u.ops, 1);
  if (!midopt_expr_ival (env, rhs, &lo, &hi)) return;
  /* Only report when the divisor is *exactly* zero on all paths. */
  if (lo == 0 && hi == 0)
    midopt_safety_report (c2m_ctx, POS (n), "definite division by zero");
}

static void midopt_check_shift (c2m_ctx_t c2m_ctx, node_t n, struct midopt_env *env) {
  node_t rhs;
  mir_llong lo, hi;
  int width = 64;
  struct expr *e;
  if (n == NULL) return;
  if (n->code != N_LSH && n->code != N_RSH && n->code != N_LSH_ASSIGN && n->code != N_RSH_ASSIGN)
    return;
  e = n->attr;
  if (e != NULL && e->type != NULL && integer_type_p (e->type)) {
    mir_size_t sz = type_size (c2m_ctx, e->type);
    if (sz > 0 && sz <= 8) width = (int) (sz * 8);
  }
  rhs = NL_EL (n->u.ops, 1);
  if (!midopt_expr_ival (env, rhs, &lo, &hi)) return;
  if (hi < 0 || lo >= width)
    midopt_safety_report (c2m_ctx, POS (n),
                          "definite shift out of range (count in [%lld,%lld], width %d)",
                          (long long) lo, (long long) hi, width);
}

/* Side-effect walk: update env for subexpressions that assign (++, --, =). */
static void midopt_safety_expr (c2m_ctx_t c2m_ctx, node_t n, struct midopt_env *env) {
  node_t decl;
  mir_llong lo, hi;

  if (n == NULL) return;

  /* Pre-order checks on this node after children for most ops; for assign
     process rhs first then update. */
  switch (n->code) {
  case N_ASSIGN:
  case N_ADD_ASSIGN: case N_SUB_ASSIGN: case N_MUL_ASSIGN: case N_DIV_ASSIGN:
  case N_MOD_ASSIGN: case N_LSH_ASSIGN: case N_RSH_ASSIGN:
  case N_AND_ASSIGN: case N_OR_ASSIGN: case N_XOR_ASSIGN: {
    node_t lhs = NL_HEAD (n->u.ops);
    node_t rhs = NL_NEXT (lhs);
    midopt_safety_expr (c2m_ctx, rhs, env);
    midopt_safety_expr (c2m_ctx, lhs, env);
    if (n->code == N_ASSIGN)
      midopt_on_assign (env, lhs, rhs);
    else {
      decl = midopt_id_decl (lhs);
      if (decl != NULL) {
        midopt_kill_ival (env, decl);
        /* compound assign on pointer rare; kill nullness if any */
        {
          struct midopt_fact *f = midopt_env_find (env, decl);
          if (f != NULL && (n->code == N_ADD_ASSIGN || n->code == N_SUB_ASSIGN))
            f->nullness = MN_TOP;
        }
      }
    }
    if (n->code == N_DIV_ASSIGN || n->code == N_MOD_ASSIGN)
      midopt_check_div (c2m_ctx, n, env);
    if (n->code == N_LSH_ASSIGN || n->code == N_RSH_ASSIGN)
      midopt_check_shift (c2m_ctx, n, env);
    return;
  }
  case N_INC: case N_DEC: case N_POST_INC: case N_POST_DEC: {
    node_t op = NL_HEAD (n->u.ops);
    midopt_safety_expr (c2m_ctx, op, env);
    decl = midopt_id_decl (op);
    if (decl != NULL && midopt_expr_ival (env, op, &lo, &hi) && lo == hi) {
      mir_llong d = (n->code == N_INC || n->code == N_POST_INC) ? 1 : -1;
      midopt_set_ival (env, decl, lo + d, hi + d);
    } else if (decl != NULL) {
      midopt_kill_ival (env, decl);
    }
    return;
  }
  case N_DEREF: case N_DEREF_FIELD: case N_IND:
    if (midopt_node_has_ops (n->code)) {
      for (node_t c = NL_HEAD (n->u.ops); c != NULL; c = NL_NEXT (c))
        midopt_safety_expr (c2m_ctx, c, env);
    }
    midopt_check_deref (c2m_ctx, n, env);
    if (n->code == N_IND) midopt_check_ind_oob (c2m_ctx, n, env);
    return;
  case N_CALL: {
    node_t args, a, decl;
    /* Evaluate callee + args, then kill facts for any pointer arg (may free /
       mutate / escape).  Conservatively also kill integer intervals on args. */
    if (midopt_node_has_ops (n->code)) {
      for (node_t c = NL_HEAD (n->u.ops); c != NULL; c = NL_NEXT (c))
        midopt_safety_expr (c2m_ctx, c, env);
    }
    args = NL_EL (n->u.ops, 1);
    if (args != NULL && args->code == N_LIST) {
      for (a = NL_HEAD (args->u.ops); a != NULL; a = NL_NEXT (a)) {
        decl = midopt_id_decl (a);
        if (decl == NULL && a->code == N_ADDR) decl = midopt_id_decl (NL_HEAD (a->u.ops));
        if (decl != NULL) {
          struct midopt_fact *f = midopt_env_find (env, decl);
          if (f != NULL) {
            f->nullness = MN_TOP;
            f->ival_p = 0;
          }
        }
      }
    }
    return;
  }
  case N_DIV: case N_MOD:
    if (midopt_node_has_ops (n->code)) {
      for (node_t c = NL_HEAD (n->u.ops); c != NULL; c = NL_NEXT (c))
        midopt_safety_expr (c2m_ctx, c, env);
    }
    midopt_check_div (c2m_ctx, n, env);
    return;
  case N_LSH: case N_RSH:
    if (midopt_node_has_ops (n->code)) {
      for (node_t c = NL_HEAD (n->u.ops); c != NULL; c = NL_NEXT (c))
        midopt_safety_expr (c2m_ctx, c, env);
    }
    midopt_check_shift (c2m_ctx, n, env);
    return;
  default:
    if (!midopt_node_has_ops (n->code)) return;
    for (node_t c = NL_HEAD (n->u.ops); c != NULL; c = NL_NEXT (c))
      midopt_safety_expr (c2m_ctx, c, env);
    return;
  }
}

/* Peel N_DECL id out of a (possibly pointer-wrapped) declarator. */
static node_t midopt_declarator_id (node_t declarator) {
  node_t d = declarator;
  int guard = 0;
  while (d != NULL && guard++ < 16) {
    if (d->code == N_DECL) return DECL_ID (d);
    if (d->code == N_POINTER || d->code == N_ARR || d->code == N_FUNC)
      d = NL_HEAD (d->u.ops);
    else
      break;
  }
  return NULL;
}

static void midopt_safety_decl (c2m_ctx_t c2m_ctx, node_t n, struct midopt_env *env) {
  /* N_SPEC_DECL: specs, declarator, attrs, asm, initializer */
  node_t declarator, initializer, id;
  decl_t dd;
  enum midopt_null nn;
  mir_llong lo, hi;
  struct midopt_fact *f;

  if (n == NULL || n->code != N_SPEC_DECL || n->attr == NULL) return;
  if (n->attr == (void *) ((intptr_t) -1)) return;
  dd = (decl_t) n->attr;
  declarator = SPEC_DECL_DECL (n);
  initializer = SPEC_DECL_INIT (n);
  id = midopt_declarator_id (declarator);

  if (initializer != NULL && initializer->code != N_IGNORE) {
    midopt_safety_expr (c2m_ctx, initializer, env);
    /* Always key by SPEC_DECL (stable). Expression N_IDs resolve def_node to
       this same node after check. */
    f = midopt_env_get (env, n);
    if (f != NULL) {
      nn = midopt_expr_nullness (env, initializer);
      if (nn != MN_TOP) f->nullness = nn;
      if (midopt_expr_ival (env, initializer, &lo, &hi)) {
        f->ival_p = 1;
        f->lo = lo;
        f->hi = hi;
      }
    }
    if (id != NULL && id->code == N_ID) midopt_on_assign (env, id, initializer);
  }
  if (dd != NULL && dd->ctor_call != NULL) midopt_safety_expr (c2m_ctx, dd->ctor_call, env);
}

static void midopt_safety_stmt (c2m_ctx_t c2m_ctx, node_t n, struct midopt_env *env) {
  if (n == NULL) return;

  switch (n->code) {
  case N_BLOCK: {
    /* labels, decl/stmt list */
    node_t list = NL_EL (n->u.ops, 1);
    if (list != NULL && list->code == N_LIST) {
      for (node_t s = NL_HEAD (list->u.ops); s != NULL; s = NL_NEXT (s))
        midopt_safety_stmt (c2m_ctx, s, env);
    }
    return;
  }
  case N_IF: {
    /* labels, cond, then, else? */
    node_t cond = NL_EL (n->u.ops, 1);
    node_t then_s = NL_EL (n->u.ops, 2);
    node_t else_s = NL_EL (n->u.ops, 3);
    struct midopt_env env_then, env_else;

    midopt_safety_expr (c2m_ctx, cond, env);
    midopt_env_copy (&env_then, env);
    midopt_env_copy (&env_else, env);
    midopt_refine_cond (&env_then, cond, 1);
    midopt_refine_cond (&env_else, cond, 0);
    midopt_safety_stmt (c2m_ctx, then_s, &env_then);
    if (else_s != NULL && else_s->code != N_IGNORE) {
      midopt_safety_stmt (c2m_ctx, else_s, &env_else);
      midopt_env_join (env, &env_then, &env_else);
    } else {
      /* No else arm: fallthrough is the implicit else (cond false).
         Still analyze `then` for diagnostics (e.g. if (p==NULL) *p).
         If then is a plain return / return-expr, fallthrough is only else. */
      int then_returns = 0;
      node_t ts = then_s;
      if (ts != NULL && ts->code == N_BLOCK) {
        node_t list = NL_EL (ts->u.ops, 1);
        if (list != NULL && list->code == N_LIST) {
          node_t last = NULL;
          for (node_t s = NL_HEAD (list->u.ops); s != NULL; s = NL_NEXT (s)) last = s;
          ts = last;
        }
      }
      if (ts != NULL && ts->code == N_RETURN) then_returns = 1;
      if (then_returns)
        midopt_env_copy (env, &env_else);
      else
        midopt_env_join (env, &env_then, &env_else);
    }
    return;
  }
  case N_WHILE: case N_DO: case N_FOR: case N_FORIN: case N_SWITCH: {
    /* Conservative: analyze body with TOP-killed increments unknown.
       Still check expressions inside for definite const issues. */
    if (midopt_node_has_ops (n->code)) {
      for (node_t c = NL_HEAD (n->u.ops); c != NULL; c = NL_NEXT (c)) {
        if (c->code == N_BLOCK || c->code == N_IF || c->code == N_WHILE || c->code == N_DO
            || c->code == N_FOR || c->code == N_FORIN || c->code == N_SWITCH
            || c->code == N_EXPR || c->code == N_RETURN)
          midopt_safety_stmt (c2m_ctx, c, env);
        else
          midopt_safety_expr (c2m_ctx, c, env);
      }
    }
    /* Kill all ival after loop (iteration unknown) */
    {
      int i;
      for (i = 0; i < env->n; i++) env->f[i].ival_p = 0;
    }
    return;
  }
  case N_EXPR: case N_RETURN: {
    /* labels + expr */
    node_t ex = NL_EL (n->u.ops, 1);
    if (ex != NULL) midopt_safety_expr (c2m_ctx, ex, env);
    return;
  }
  case N_SPEC_DECL:
    midopt_safety_decl (c2m_ctx, n, env);
    return;
  default:
    if (midopt_expr_node_p (n->code)) {
      midopt_safety_expr (c2m_ctx, n, env);
      return;
    }
    if (midopt_node_has_ops (n->code)) {
      for (node_t c = NL_HEAD (n->u.ops); c != NULL; c = NL_NEXT (c))
        midopt_safety_stmt (c2m_ctx, c, env);
    }
    return;
  }
}

static void midopt_safety_func (c2m_ctx_t c2m_ctx, node_t func_def) {
  struct midopt_env env;
  node_t block, decls;
  if (func_def == NULL || func_def->code != N_FUNC_DEF) return;
  env.n = 0;
  /* Method `this` is non-null at entry */
  decls = FUNC_DEF_DECLS (func_def);
  if (decls != NULL) midopt_safety_stmt (c2m_ctx, decls, &env);
  block = FUNC_DEF_BLOCK (func_def);
  if (block != NULL) midopt_safety_stmt (c2m_ctx, block, &env);
}

static void midopt_safety_module (c2m_ctx_t c2m_ctx, node_t n) {
  if (n == NULL) return;
  if (n->code == N_FUNC_DEF) {
    /* Analyze free functions and live methods (dead methods still checked if
       present — cheap; helps catch bugs in monomorphs kept for other reasons). */
    midopt_safety_func (c2m_ctx, n);
  }
  if (!midopt_node_has_ops (n->code)) return;
  for (node_t c = NL_HEAD (n->u.ops); c != NULL; c = NL_NEXT (c))
    midopt_safety_module (c2m_ctx, c);
}

/* Fallback structural elision (no env) still applied after lattice pass. */
static void midopt_try_elide_oob (node_t n) {
  node_t arr, idx;
  struct expr *e, *ie, *ae;
  struct type *arr_type;

  if (n == NULL || n->code != N_IND || n->attr == NULL) return;
  e = (struct expr *) n->attr;
  if (e->type == NULL || e->elide_oob_p) return;
  arr = NL_HEAD (n->u.ops);
  idx = NL_NEXT (arr);
  if (arr == NULL || idx == NULL || idx->attr == NULL) return;
  ie = (struct expr *) idx->attr;
  if (!ie->const_p) return;
  ae = arr->attr;
  if (ae == NULL || ae->type == NULL) return;
  arr_type = ae->type;
  if (arr_type->mode == TM_PTR && arr_type->arr_type != NULL
      && arr_type->arr_type->mode == TM_ARR) {
    node_t sz_node = arr_type->arr_type->u.arr_type->size;
    struct expr *sze;
    mir_llong len, i;

    if (sz_node == NULL || sz_node->code == N_IGNORE || sz_node->attr == NULL) return;
    sze = (struct expr *) sz_node->attr;
    if (!sze->const_p || sze->c.i_val <= 0) return;
    len = sze->c.i_val;
    i = ie->c.i_val;
    if (i >= 0 && i < len) {
      e->elide_oob_p = 1;
      if (e->own_deref_class == DEREF_GUARD_DEFAULT)
        e->own_deref_class = DEREF_GUARD_SAFE;
      midopt_safety_n_elide++;
    } else if (i < 0 || i >= len) {
      /* Handled in lattice pass with full diagnostics when possible. */
    }
  }
}

static void midopt_try_elide_null (node_t n) {
  struct expr *e;
  node_t recv;

  if (n == NULL || n->attr == NULL) return;
  if (n->code != N_DEREF && n->code != N_DEREF_FIELD && n->code != N_IND) return;
  e = (struct expr *) n->attr;
  if (e->type == NULL) return;
  if (e->own_deref_class == DEREF_GUARD_SAFE) return;
  recv = NL_HEAD (n->u.ops);
  if (recv == NULL) return;
  if (recv->code == N_ID && recv->u.s.s != NULL && strcmp (recv->u.s.s, "this") == 0) {
    e->own_deref_class = DEREF_GUARD_SAFE;
    midopt_safety_n_elide++;
    return;
  }
  if (recv->code == N_ADDR) {
    e->own_deref_class = DEREF_GUARD_SAFE;
    midopt_safety_n_elide++;
    return;
  }
}

static void midopt_elide_walk (c2m_ctx_t c2m_ctx, node_t n) {
  if (n == NULL) return;
  midopt_try_elide_oob (n);
  midopt_try_elide_null (n);
  if (!midopt_node_has_ops (n->code)) return;
  for (node_t c = NL_HEAD (n->u.ops); c != NULL; c = NL_NEXT (c))
    midopt_elide_walk (c2m_ctx, c);
}

/* Private/collection helpers often lack def_node on monomorph paths.  When a
   monomorph already has ≥1 kept method, keep known helper names so Add/Set/…
   do not call midopt-dead MIR items.  Does NOT keep the whole public API. */
static const char *const midopt_helper_names[] = {
  "EnsureCapacity", "init_storage", "grow_table", "ensure_table", "find_slot",
  "find_index", "destroy_key_at", "destroy_val_at", "Copy", "Clear", "owns",
  "ownsValues", "ownsKeys", NULL
};

static void midopt_keep_helpers_on_class (c2m_ctx_t c2m_ctx, node_t class_tag, pos_t pos) {
  size_t i;
  if (class_tag == NULL || class_tag->code != N_CLASS) return;
  for (i = 0; midopt_helper_names[i] != NULL; i++)
    midopt_mark_named_on_class (c2m_ctx, class_tag, midopt_helper_names[i], pos);
}

/* Keep every constructor and destructor of a live class (delete / RAII / new
   overload paths often miss def_node on sibling ctors/dtors). */
static void midopt_keep_ctors_dtors_on_class (node_t class_tag) {
  node_t id, decl_list;
  if (class_tag == NULL || class_tag->code != N_CLASS) return;
  id = NL_HEAD (class_tag->u.ops);
  decl_list = id != NULL ? NL_NEXT (id) : NULL;
  if (decl_list == NULL || decl_list->code != N_LIST) return;
  for (node_t m = NL_HEAD (decl_list->u.ops); m != NULL; m = NL_NEXT (m)) {
    const char *nm;
    if (m->code != N_FUNC_DEF || !midopt_class_method_p (m)) continue;
    nm = midopt_func_name (m);
    if (nm == NULL) continue;
    if (strncmp (nm, "__ctor_", 7) == 0 || strncmp (nm, "__dtor_", 7) == 0)
      midopt_mark_keep (m);
  }
}

static void midopt_keep_helpers_for_live (c2m_ctx_t c2m_ctx, node_t n) {
  if (n == NULL) return;

  if (n->code == N_CLASS && n->attr != (void *) ((intptr_t) -1)) {
    node_t id = NL_HEAD (n->u.ops);
    node_t decl_list = id != NULL ? NL_NEXT (id) : NULL;
    int any_keep = 0;

    if (decl_list != NULL && decl_list->code == N_LIST) {
      for (node_t m = NL_HEAD (decl_list->u.ops); m != NULL; m = NL_NEXT (m)) {
        if (m->code == N_FUNC_DEF && midopt_keep_has (m)) {
          any_keep = 1;
          break;
        }
      }
      if (any_keep) {
        midopt_keep_helpers_on_class (c2m_ctx, n, id != NULL ? POS (id) : no_pos);
        midopt_keep_ctors_dtors_on_class (n);
      }
    }
  }

  if (!midopt_node_has_ops (n->code)) return;
  for (node_t c = NL_HEAD (n->u.ops); c != NULL; c = NL_NEXT (c))
    midopt_keep_helpers_for_live (c2m_ctx, c);
}

/* Enumerate all class methods; mark those not in keep set as dead. */
static void midopt_mark_dead_methods (c2m_ctx_t c2m_ctx, node_t n, int *n_methods,
                                     int *n_dead) {
  if (n == NULL) return;

  if (n->code == N_FUNC_DEF && midopt_class_method_p (n)) {
    decl_t d = (decl_t) n->attr;
    (*n_methods)++;
    if (!midopt_keep_has (n)) {
      d->midopt_dead_p = TRUE;
      (*n_dead)++;
      if (midopt_verbose_p) {
        const char *nm = midopt_func_name (n);
        fprintf (stderr, "  [midopt] dead method %s\n", nm != NULL ? nm : "?");
      }
    } else {
      d->midopt_dead_p = FALSE;
    }
  }

  if (!midopt_node_has_ops (n->code)) return;
  for (node_t c = NL_HEAD (n->u.ops); c != NULL; c = NL_NEXT (c))
    midopt_mark_dead_methods (c2m_ctx, c, n_methods, n_dead);
}

/* Free-function bodies are always live roots for method reachability. */
static void midopt_seed_from_free_funcs (c2m_ctx_t c2m_ctx, node_t n) {
  if (n == NULL) return;

  if (n->code == N_FUNC_DEF && n->attr != NULL
      && n->attr != (void *) ((intptr_t) -1)
      && !midopt_class_method_p (n)) {
    /* Always "live": scan body for method refs (no enclosing class_tag). */
    midopt_collect_uses (c2m_ctx, FUNC_DEF_BLOCK (n), NULL);
    midopt_collect_uses (c2m_ctx, FUNC_DEF_DECLS (n), NULL);
  }

  if (!midopt_node_has_ops (n->code)) return;
  for (node_t c = NL_HEAD (n->u.ops); c != NULL; c = NL_NEXT (c))
    midopt_seed_from_free_funcs (c2m_ctx, c);
}

/* ── MIR stats (Phase A instrumentation) ─────────────────────────────────── */

static void midopt_dump_mir_stats (c2m_ctx_t c2m_ctx, MIR_module_t m) {
  MIR_context_t ctx = c2m_ctx->ctx;
  MIR_item_t item;
  long n_func = 0, n_fwd = 0, n_other = 0;
  long n_insn = 0, n_call = 0;
  FILE *f = c2m_options != NULL && c2m_options->message_file != NULL
              ? c2m_options->message_file
              : stderr;

  if (m == NULL) return;
  for (item = DLIST_HEAD (MIR_item_t, m->items); item != NULL;
       item = DLIST_NEXT (MIR_item_t, item)) {
    if (item->item_type == MIR_func_item) {
      MIR_insn_t insn;
      n_func++;
      for (insn = DLIST_HEAD (MIR_insn_t, item->u.func->insns); insn != NULL;
           insn = DLIST_NEXT (MIR_insn_t, insn)) {
        n_insn++;
        if (MIR_call_code_p (insn->code)) n_call++;
      }
    } else if (item->item_type == MIR_forward_item) {
      n_fwd++;
    } else {
      n_other++;
    }
  }
  (void) ctx;
  fprintf (f,
           "  [mir-stats] module=%s funcs=%ld forwards=%ld other_items=%ld "
           "insns=%ld calls=%ld\n",
           m->name != NULL ? m->name : "?", n_func, n_fwd, n_other, n_insn, n_call);
}

/* ── entry point ─────────────────────────────────────────────────────────── */

/* Run midopt over the module AST.  Safe to call when no_midopt_p (no-op). */
static void midopt_run (c2m_ctx_t c2m_ctx, node_t module) {
  MIR_alloc_t alloc;
  int n_methods = 0, n_dead = 0;
  size_t wi;

  if (c2m_ctx == NULL || module == NULL) return;
  if (c2m_options != NULL && c2m_options->no_midopt_p) {
    if (c2m_options->verbose_p && c2m_options->message_file != NULL)
      fprintf (c2m_options->message_file, "midopt - SKIPPED (-fno-midopt)\n");
    return;
  }

  alloc = c2m_alloc (c2m_ctx);
  midopt_verbose_p = c2m_options != NULL && c2m_options->verbose_p;
  VARR_CREATE (node_t, midopt_keep, alloc, 64);
  VARR_CREATE (node_t, midopt_work, alloc, 64);

  if (midopt_verbose_p)
    fprintf (stderr, "  [midopt] start (P0 dead methods + P1 guard elision)\n");

  /* 1) Seed ONLY from free functions (and nested decls/ctors in them). */
  midopt_seed_from_free_funcs (c2m_ctx, module);

  /* 2) Method-level fixpoint (same-class name resolution for this->Foo). */
  wi = 0;
  while (wi < VARR_LENGTH (node_t, midopt_work)) {
    node_t f = VARR_GET (node_t, midopt_work, wi++);
    midopt_collect_uses_body (c2m_ctx, f);
  }

  if (midopt_verbose_p)
    fprintf (stderr, "  [midopt] method-level keep=%lu before helper-fill\n",
             (unsigned long) VARR_LENGTH (node_t, midopt_keep));

  /* 3) Keep collection helpers on live monomorphs (not the whole public API). */
  midopt_keep_helpers_for_live (c2m_ctx, module);

  /* 3b) Drain callees of newly kept helpers / methods. */
  while (wi < VARR_LENGTH (node_t, midopt_work)) {
    node_t f = VARR_GET (node_t, midopt_work, wi++);
    midopt_collect_uses_body (c2m_ctx, f);
  }

  /* 3c) One more helper fill + drain (helpers may pull siblings). */
  {
    size_t before = VARR_LENGTH (node_t, midopt_keep);
    midopt_keep_helpers_for_live (c2m_ctx, module);
    while (wi < VARR_LENGTH (node_t, midopt_work)) {
      node_t f = VARR_GET (node_t, midopt_work, wi++);
      midopt_collect_uses_body (c2m_ctx, f);
    }
    if (midopt_verbose_p)
      fprintf (stderr, "  [midopt] after helper-fill keep=%lu (was %lu)\n",
               (unsigned long) VARR_LENGTH (node_t, midopt_keep),
               (unsigned long) before);
  }

  /* 3d) Safety net: zero keeps → do not prune (P1 elision still runs). */
  if (VARR_LENGTH (node_t, midopt_keep) == 0) {
    if (midopt_verbose_p)
      fprintf (stderr, "  [midopt] no method keeps found — skip dead pruning\n");
    midopt_safety_n_warn = 0;
    midopt_safety_n_elide = 0;
    midopt_safety_module (c2m_ctx, module);
    midopt_elide_walk (c2m_ctx, module);
    if (c2m_options != NULL && c2m_options->verbose_p) {
      FILE *f = c2m_options->message_file != NULL ? c2m_options->message_file : stderr;
      fprintf (f, "  [midopt] class methods=unknown kept=0 dead=0 (prune skipped)\n");
      fprintf (f, "  [midopt] safety: diagnostics=%d elisions=%d\n", midopt_safety_n_warn,
               midopt_safety_n_elide);
    }
    VARR_DESTROY (node_t, midopt_keep);
    VARR_DESTROY (node_t, midopt_work);
    midopt_keep = NULL;
    midopt_work = NULL;
    return;
  }

  midopt_mark_dead_methods (c2m_ctx, module, &n_methods, &n_dead);

  /* 4) Mark only trivial scalar accessors for MIR_INLINE.
     Do NOT mark Get/GetMut — MIR_INLINE of methods that return pointers into
     `this` buffers has been observed to miscompile chaining (GetMut().Boost).
     Dense List Count/IsEmpty/Capacity are open-coded in gen instead. */
  {
    size_t ki;
    for (ki = 0; ki < VARR_LENGTH (node_t, midopt_keep); ki++) {
      node_t f = VARR_GET (node_t, midopt_keep, ki);
      const char *nm = midopt_func_name (f);
      decl_t d = f->attr;
      if (nm == NULL || d == NULL) continue;
      if (strcmp (nm, "Count") == 0 || strcmp (nm, "IsEmpty") == 0
          || strcmp (nm, "Capacity") == 0) {
        d->decl_spec.inline_p = TRUE;
        if (midopt_verbose_p) fprintf (stderr, "  [midopt] inline mark %s\n", nm);
      }
    }
  }

  /* 5) P1 safety lattice: nullness + intervals → diagnose / elide. */
  midopt_safety_n_warn = 0;
  midopt_safety_n_elide = 0;
  midopt_safety_module (c2m_ctx, module);
  /* Structural fallback elision (this / &local / const index). */
  midopt_elide_walk (c2m_ctx, module);

  if (midopt_verbose_p || (c2m_options != NULL && c2m_options->verbose_p)) {
    FILE *f = c2m_options != NULL && c2m_options->message_file != NULL
                ? c2m_options->message_file
                : stderr;
    fprintf (f, "  [midopt] class methods=%d kept=%lu dead=%d\n", n_methods,
             (unsigned long) VARR_LENGTH (node_t, midopt_keep), n_dead);
    fprintf (f, "  [midopt] safety: diagnostics=%d elisions=%d%s\n", midopt_safety_n_warn,
             midopt_safety_n_elide,
             (c2m_options != NULL && c2m_options->safety_errors_p) ? " (-fsafety-errors)" : "");
  }

  VARR_DESTROY (node_t, midopt_keep);
  VARR_DESTROY (node_t, midopt_work);
  midopt_keep = NULL;
  midopt_work = NULL;
}
