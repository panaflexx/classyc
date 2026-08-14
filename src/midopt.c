/* src/midopt.c — Mid-level optimizer (check → gen) for ClassyC.
 *
 * STATUS: Phase B–J + demand-driven keep + open-code skip-keep + Phase 3 IVs
 * + Phase 4 MIR_INLINE of tiny methods (budget/hardness from -On)
 * + C11 dead-arm / unevaluated walks (_Generic, const if/?:/&&/||, sizeof).
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

/* True when func_def->attr is a real decl_t (not NULL, not the generic-template
   sentinel -1, and not PRECHECK_DA_IGNORE which stashes da_ignore before
   create_decl).  Casting either sentinel to decl_t and reading it crashes. */
static int midopt_decl_ready_p (node_t n) {
  if (n == NULL || n->attr == NULL) return 0;
  if (n->attr == (void *) ((intptr_t) -1)) return 0;
#ifdef PRECHECK_DA_IGNORE
  if (n->attr == PRECHECK_DA_IGNORE) return 0;
#endif
  return 1;
}

/* True if this FUNC_DEF is a class method (has class_scope on its type). */
static int midopt_class_method_p (node_t func_def) {
  decl_t d;
  struct type *t;
  struct func_type *ft;

  if (func_def == NULL || func_def->code != N_FUNC_DEF) return 0;
  if (!midopt_decl_ready_p (func_def)) return 0;
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
static int midopt_level; /* latched from -On at midopt_run; 2 if unspecified */

/* Unspecified (-1) matches MIR's default (= 2). */
static int midopt_opt_level (void) {
  return midopt_level;
}

static int midopt_latch_opt_level (c2m_ctx_t c2m_ctx) {
  int l = (c2m_options != NULL) ? c2m_options->optimize_level : -1;
  if (l < 0) l = 2;
  if (l > 3) l = 3;
  return l;
}

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
  if (!midopt_decl_ready_p (f)) return;
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

/* True when gen will open-code every direct call of DEF (dense List/Set/Map
   Count/Get/…).  Address-of still needs a MIR body. */
static int midopt_open_code_accessor_p (node_t def) {
  const char *nm;
  int n_args;

  if (def == NULL || def->code != N_FUNC_DEF) return 0;
  nm = midopt_func_name (def);
  if (nm == NULL) return 0;
  if (strcmp (nm, "Count") == 0 || strcmp (nm, "IsEmpty") == 0 || strcmp (nm, "Capacity") == 0
      || strcmp (nm, "First") == 0 || strcmp (nm, "Last") == 0
      || strcmp (nm, "FirstMut") == 0 || strcmp (nm, "LastMut") == 0)
    n_args = 0;
  else if (strcmp (nm, "Get") == 0 || strcmp (nm, "GetMut") == 0)
    n_args = 1;
  else
    return 0;
  return dense_accessor_open_codeable_p (def, n_args);
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
  if (def->code != N_FUNC_DEF) return;
  /* Open-coded dense accessors: a direct call does not need the MIR function.
     N_ADDR of the method still keeps it (func-ptr / leftover ref). */
  if (n->code != N_ADDR && midopt_open_code_accessor_p (def)) return;
  if (n->code == N_ADDR) {
    node_t inner = NL_HEAD (n->u.ops);
    struct expr *ie;
    if (inner != NULL && inner->attr != NULL && inner->attr != (void *) ((intptr_t) -1)) {
      ie = (struct expr *) inner->attr;
      if (ie->def_node != NULL && ie->def_node != (node_t) (intptr_t) 1
          && ie->def_node != (node_t) (intptr_t) 2 && ie->def_node->code == N_FUNC_DEF)
        midopt_mark_keep (ie->def_node);
    }
  }
  midopt_mark_keep (def);
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

/* c11_cond_known lives in classyc.c (also used by gen to skip dead arms). */
#define midopt_cond_known c11_cond_known

/* Unevaluated / short-circuit / const-dead children.  Recurses via
   midopt_collect_uses on live subtrees only.  Returns 1 if the default
   child walk should be skipped. */
static int midopt_collect_c11_p (c2m_ctx_t c2m_ctx, node_t n, node_t class_tag) {
  int k, lt;
  node_t a, b, c, list, ga, body;

  switch (n->code) {
  case N_SIZEOF:
  case N_EXPR_SIZEOF:
  case N_ALIGNOF:
    /* Operand is unevaluated (VLA size is a type operand, not a call). */
    return 1;
  case N_GENERIC:
    /* Controlling expr is unevaluated (C11 6.5.1.1).  Check moved the
       selected association to the list head; gen emits only that expr. */
    list = NL_EL (n->u.ops, 1);
    ga = (list != NULL && list->code == N_LIST) ? NL_HEAD (list->u.ops) : NULL;
    if (ga != NULL && ga->code == N_GENERIC_ASSOC)
      midopt_collect_uses (c2m_ctx, NL_EL (ga->u.ops, 1), class_tag);
    return 1;
  case N_ANDAND:
  case N_OROR:
    a = NL_HEAD (n->u.ops);
    b = a != NULL ? NL_NEXT (a) : NULL;
    midopt_collect_uses (c2m_ctx, a, class_tag);
    lt = midopt_cond_known (a);
    if (n->code == N_ANDAND && lt == 0 && c11_dead_skippable_p (b)) return 1;
    if (n->code == N_OROR && lt == 1 && c11_dead_skippable_p (b)) return 1;
    midopt_collect_uses (c2m_ctx, b, class_tag);
    return 1;
  case N_COND:
    a = NL_HEAD (n->u.ops);
    b = a != NULL ? NL_NEXT (a) : NULL;
    c = b != NULL ? NL_NEXT (b) : NULL;
    midopt_collect_uses (c2m_ctx, a, class_tag);
    k = midopt_cond_known (a);
    if (k == 1) {
      midopt_collect_uses (c2m_ctx, b, class_tag);
      if (!c11_dead_skippable_p (c)) midopt_collect_uses (c2m_ctx, c, class_tag);
    } else if (k == 0) {
      if (!c11_dead_skippable_p (b)) midopt_collect_uses (c2m_ctx, b, class_tag);
      midopt_collect_uses (c2m_ctx, c, class_tag);
    } else {
      midopt_collect_uses (c2m_ctx, b, class_tag);
      midopt_collect_uses (c2m_ctx, c, class_tag);
    }
    return 1;
  case N_COALESCE:
    a = NL_HEAD (n->u.ops);
    b = a != NULL ? NL_NEXT (a) : NULL;
    midopt_collect_uses (c2m_ctx, a, class_tag);
    if (midopt_cond_known (a) != 1 || !c11_dead_skippable_p (b))
      midopt_collect_uses (c2m_ctx, b, class_tag);
    return 1;
  case N_IF:
    a = NL_EL (n->u.ops, 1);
    b = NL_EL (n->u.ops, 2);
    c = NL_EL (n->u.ops, 3);
    midopt_collect_uses (c2m_ctx, a, class_tag);
    k = midopt_cond_known (a);
    if (k == 1) {
      midopt_collect_uses (c2m_ctx, b, class_tag);
      if (!c11_dead_skippable_p (c)) midopt_collect_uses (c2m_ctx, c, class_tag);
    } else if (k == 0) {
      if (!c11_dead_skippable_p (b)) midopt_collect_uses (c2m_ctx, b, class_tag);
      midopt_collect_uses (c2m_ctx, c, class_tag);
    } else {
      midopt_collect_uses (c2m_ctx, b, class_tag);
      midopt_collect_uses (c2m_ctx, c, class_tag);
    }
    return 1;
  case N_WHILE:
    a = NL_EL (n->u.ops, 1);
    b = NL_EL (n->u.ops, 2);
    midopt_collect_uses (c2m_ctx, a, class_tag);
    if (midopt_cond_known (a) != 0 || !c11_dead_skippable_p (b))
      midopt_collect_uses (c2m_ctx, b, class_tag);
    return 1;
  case N_FOR:
    a = NL_EL (n->u.ops, 1); /* init */
    b = a != NULL ? NL_NEXT (a) : NULL; /* cond */
    c = b != NULL ? NL_NEXT (b) : NULL; /* iter */
    body = c != NULL ? NL_NEXT (c) : NULL;
    midopt_collect_uses (c2m_ctx, a, class_tag);
    midopt_collect_uses (c2m_ctx, b, class_tag);
    if (b == NULL || b->code == N_IGNORE || midopt_cond_known (b) != 0
        || !c11_dead_skippable_p (body) || !c11_dead_skippable_p (c)) {
      midopt_collect_uses (c2m_ctx, c, class_tag);
      midopt_collect_uses (c2m_ctx, body, class_tag);
    }
    return 1;
  default:
    return 0;
  }
}
static void midopt_elide_walk (c2m_ctx_t c2m_ctx, node_t n);

/* Gen-time dict-to-class bind fills collection pointer fields via default
   ctor + Add (no N_CALL in the AST).  Keep that protocol on the bound type
   and nested collection fields. */
static void midopt_keep_default_ctor (node_t class_tag) {
  node_t id, decl_list;
  if (class_tag == NULL || class_tag->code != N_CLASS) return;
  id = NL_HEAD (class_tag->u.ops);
  decl_list = id != NULL ? NL_NEXT (id) : NULL;
  if (decl_list == NULL || decl_list->code != N_LIST) return;
  for (node_t m = NL_HEAD (decl_list->u.ops); m != NULL; m = NL_NEXT (m)) {
    const char *nm;
    decl_t d;
    struct func_type *ft;
    node_t cp;
    if (m->code != N_FUNC_DEF || !midopt_class_method_p (m)) continue;
    nm = midopt_func_name (m);
    if (nm == NULL || strncmp (nm, "__ctor_", 7) != 0) continue;
    d = (decl_t) m->attr;
    if (d == NULL || d->decl_spec.type == NULL || d->decl_spec.type->mode != TM_FUNC) continue;
    ft = d->decl_spec.type->u.func_type;
    if (ft == NULL || ft->param_list == NULL) continue;
    cp = NL_HEAD (ft->param_list->u.ops);
    if (cp != NULL) cp = NL_NEXT (cp);
    if (cp == NULL) midopt_mark_keep (m);
  }
}

static void midopt_keep_bind_type (c2m_ctx_t c2m_ctx, struct type *t, pos_t pos, int depth) {
  node_t tag, id, decl_list, add;
  if (t == NULL || depth > 8) return;
  if (t->mode == TM_PTR && t->u.ptr_type != NULL) {
    midopt_keep_bind_type (c2m_ctx, t->u.ptr_type, pos, depth + 1);
    return;
  }
  if ((t->mode != TM_CLASS && t->mode != TM_STRUCT) || t->u.tag_type == NULL) return;
  tag = t->u.tag_type;
  add = find_class_protocol_method (c2m_ctx, tag, "Add", 1, pos);
  if (add != NULL) {
    midopt_mark_keep (add);
    midopt_keep_default_ctor (tag);
  }
  id = NL_HEAD (tag->u.ops);
  decl_list = id != NULL ? NL_NEXT (id) : NULL;
  if (decl_list == NULL || decl_list->code != N_LIST) return;
  for (node_t mem = NL_HEAD (decl_list->u.ops); mem != NULL; mem = NL_NEXT (mem)) {
    decl_t md;
    if (mem->code != N_MEMBER || mem->attr == NULL) continue;
    md = (decl_t) mem->attr;
    if (md != NULL && md->decl_spec.type != NULL)
      midopt_keep_bind_type (c2m_ctx, md->decl_spec.type, pos, depth + 1);
  }
}

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
        && def->code == N_FUNC_DEF) {
      if (!midopt_open_code_accessor_p (def)) midopt_mark_keep (def);
    } else if (e->type != NULL) {
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
        if (m && !midopt_open_code_accessor_p (m)) midopt_mark_keep (m);
        if (e->mut_sub_p) {
          m = find_class_protocol_method (c2m_ctx, cls->u.tag_type, "Get", 1, POS (n));
          if (m && !midopt_open_code_accessor_p (m)) midopt_mark_keep (m);
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

  /* (T) dict bind: gen_dict_bind_collection_field calls default ctor + Add
     on collection pointer fields with no N_CALL in the AST. */
  if (n->code == N_CAST && n->attr != NULL && n->attr != (void *) ((intptr_t) -1)) {
    struct expr *ce = (struct expr *) n->attr;
    if (ce->type != NULL && ce->bind_p) midopt_keep_bind_type (c2m_ctx, ce->type, POS (n), 0);
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

  if (midopt_collect_c11_p (c2m_ctx, n, class_tag)) return;

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
  if (midopt_decl_ready_p (func_def)) {
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
  /* Symbolic upper bound (R1): when this local is an induction variable
     `i < recv.Count()` (directly or via a bound local), sym_recv is the
     receiver's decl identity and sym_lo the IV's constant initial value.
     Enables OOB elision on recv.Get(i) / recv[i] inside the loop body. */
  node_t sym_recv;
  mir_llong sym_lo;
  int sym_minus; /* bound is Count()-sym_minus (0 for a raw Count()) */
  int addr_taken_p; /* &decl observed anywhere (monotone) — disables sym proofs */
  int uniq_ptr_p; /* local pointer from `new`, never copied — alias-free */
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
  if (def == (node_t) (intptr_t) 1 || def == (node_t) (intptr_t) 2) return NULL;
  /* Locals are linked through u.lvalue_node (the N_SPEC_DECL); def_node is
     NULL for plain local uses (it serves funcs/refs/labels). */
  if (def == NULL) def = e->u.lvalue_node;
  if (def == NULL) return NULL;
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
  p->sym_recv = NULL;
  p->sym_lo = 0;
  p->sym_minus = 0;
  p->addr_taken_p = 0;
  p->uniq_ptr_p = 0;
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
      fo->sym_recv = NULL;
      fo->uniq_ptr_p = 0;
      fo->addr_taken_p = fa->addr_taken_p;
    } else {
      fo->nullness = midopt_null_join (fa->nullness, fb->nullness);
      if (fa->ival_p && fb->ival_p) {
        fo->ival_p = 1;
        fo->lo = fa->lo < fb->lo ? fa->lo : fb->lo;
        fo->hi = fa->hi > fb->hi ? fa->hi : fb->hi;
      } else {
        fo->ival_p = 0;
      }
      /* Symbolic bound survives a join only when identical on both paths. */
      if (fa->sym_recv != NULL && fa->sym_recv == fb->sym_recv && fa->sym_lo == fb->sym_lo
          && fa->sym_minus == fb->sym_minus)
        fo->sym_recv = fa->sym_recv;
      else
        fo->sym_recv = NULL;
      fo->addr_taken_p = fa->addr_taken_p || fb->addr_taken_p;
      fo->uniq_ptr_p = fa->uniq_ptr_p && fb->uniq_ptr_p;
    }
  }
  for (i = 0; i < b->n; i++) {
    if (midopt_env_find (a, b->f[i].decl) != NULL) continue;
    struct midopt_fact *fo = midopt_env_get (&out, b->f[i].decl);
    if (fo == NULL) continue;
    fo->nullness = MN_TOP;
    fo->ival_p = 0;
    fo->sym_recv = NULL;
    fo->uniq_ptr_p = 0;
    fo->addr_taken_p = b->f[i].addr_taken_p;
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

static node_t midopt_peel_count (struct midopt_env *env, node_t n, mir_llong *minus);

/* Refine env for then-branch of `if (cond)`.  then_p=1 → then arm; 0 → else. */
static void midopt_refine_cond (struct midopt_env *env, node_t cond, int then_p) {
  node_t decl, a, b, iv, bound_expr, recv;
  int strict;
  mir_llong minus, blo, bhi, klo;
  struct midopt_fact *f, *bf;
  struct expr *ie;

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
  if (cond->code == N_ANDAND && then_p) {
    midopt_refine_cond (env, NL_HEAD (cond->u.ops), 1);
    midopt_refine_cond (env, NL_NEXT (NL_HEAD (cond->u.ops)), 1);
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
    return;
  }

  /* Relational: then-arm only.  `i >= K` raises lo; `i < Count()` sets sym.
     Gated at -O2+ (hardness). */
  if (!then_p || midopt_opt_level () < 2) return;
  if (cond->code != N_LT && cond->code != N_LE && cond->code != N_GT && cond->code != N_GE)
    return;
  a = NL_HEAD (cond->u.ops);
  b = a != NULL ? NL_NEXT (a) : NULL;
  if (a == NULL || b == NULL) return;

  /* i >= K / K <= i  (const K) */
  if ((cond->code == N_GE || cond->code == N_GT) && a->code == N_ID
      && midopt_expr_ival (env, b, &blo, &bhi) && blo == bhi) {
    decl = midopt_id_decl (a);
    f = decl != NULL ? midopt_env_get (env, decl) : NULL;
    if (f != NULL) {
      klo = blo + (cond->code == N_GT ? 1 : 0);
      if (!f->ival_p) {
        f->ival_p = 1;
        f->lo = klo;
        f->hi = 0x7fffffffffffffffLL;
      } else if (klo > f->lo)
        f->lo = klo;
    }
    return;
  }
  if ((cond->code == N_LE || cond->code == N_LT) && b->code == N_ID
      && midopt_expr_ival (env, a, &blo, &bhi) && blo == bhi) {
    decl = midopt_id_decl (b);
    f = decl != NULL ? midopt_env_get (env, decl) : NULL;
    if (f != NULL) {
      klo = blo + (cond->code == N_LT ? 1 : 0);
      if (!f->ival_p) {
        f->ival_p = 1;
        f->lo = klo;
        f->hi = 0x7fffffffffffffffLL;
      } else if (klo > f->lo)
        f->lo = klo;
    }
    return;
  }

  iv = NULL;
  bound_expr = NULL;
  strict = 1;
  if ((cond->code == N_LT || cond->code == N_LE) && a->code == N_ID) {
    iv = a;
    bound_expr = b;
    strict = cond->code == N_LT;
  } else if ((cond->code == N_GT || cond->code == N_GE) && b->code == N_ID) {
    iv = b;
    bound_expr = a;
    strict = cond->code == N_GT;
  } else {
    return;
  }
  decl = midopt_id_decl (iv);
  if (decl == NULL) return;
  f = midopt_env_get (env, decl);
  if (f == NULL) return;
  {
    int nonneg = (f->ival_p && f->lo >= 0);
    ie = iv->attr;
    if (!nonneg && ie != NULL && ie->type != NULL && integer_type_p (ie->type)
        && !signed_integer_type_p (ie->type))
      nonneg = 1;
    if (!nonneg) return;
  }
  minus = 0;
  recv = midopt_peel_count (env, bound_expr, &minus);
  if (recv == NULL) {
    node_t bd = midopt_id_decl (bound_expr);
    bf = bd != NULL ? midopt_env_find (env, bd) : NULL;
    if (bf != NULL && bf->sym_recv != NULL) {
      recv = bf->sym_recv;
      minus = bf->sym_minus;
    }
  }
  if (recv != NULL) {
    if ((strict && minus >= 0) || (!strict && minus >= 1)) {
      f->sym_recv = recv;
      f->sym_lo = f->ival_p && f->lo >= 0 ? f->lo : 0;
      f->sym_minus = (int) minus;
    }
  } else if (midopt_expr_ival (env, bound_expr, &blo, &bhi)) {
    mir_llong hi = bhi - (strict ? 1 : 0);
    if (!f->ival_p) {
      f->ival_p = 1;
      f->lo = 0;
      f->hi = hi;
    } else if (hi < f->hi)
      f->hi = hi;
  }
}

static void midopt_safety_stmt (c2m_ctx_t c2m_ctx, node_t n, struct midopt_env *env);

/* R1 helpers (defined below midopt_safety_expr; used by its N_CALL/N_IND cases). */
static int midopt_method_safe_p (const char *nm);
static void midopt_kill_sym_for (struct midopt_env *env, node_t decl);
static node_t midopt_method_call_recv (node_t func, const char **name);
static void midopt_check_class_iv (c2m_ctx_t c2m_ctx, node_t n, struct midopt_env *env);
static void midopt_safety_for (c2m_ctx_t c2m_ctx, node_t n, struct midopt_env *env);
static void midopt_safety_while (c2m_ctx_t c2m_ctx, node_t n, struct midopt_env *env);
static int midopt_pure_coll_method_p (const char *nm);
static int midopt_recv_mutated_p (node_t n, node_t recv);

/* True when N is `new T(...)` (casts peeled). */
static int midopt_expr_is_new (node_t n) {
  int g = 0;
  while (n != NULL && g++ < 8) {
    if (n->code == N_NEW) return 1;
    if (n->code == N_CAST) {
      n = NL_EL (n->u.ops, 1);
      continue;
    }
    break;
  }
  return 0;
}

/* Receiver decl of `xs.Count()` / `p->Count()` when the proof may trust it.
   Value-class locals are always ok.  Pointer locals only if uniq_ptr_p
   (assigned from `new`, never copied) and not address-taken. */
static node_t midopt_count_recv_from_call (struct midopt_env *env, node_t n) {
  node_t func, recv, mid, args, d;
  struct expr *re;
  struct midopt_fact *f;

  if (n == NULL || n->code != N_CALL) return NULL;
  func = NL_HEAD (n->u.ops);
  if (func == NULL || (func->code != N_FIELD && func->code != N_DEREF_FIELD)) return NULL;
  recv = NL_HEAD (func->u.ops);
  mid = NL_NEXT (recv);
  if (recv == NULL || recv->code != N_ID || mid == NULL || mid->code != N_ID) return NULL;
  if (mid->u.s.s == NULL || strcmp (mid->u.s.s, "Count") != 0) return NULL;
  args = NL_EL (n->u.ops, 1);
  if (args != NULL && args->code == N_LIST) {
    node_t a0 = NL_HEAD (args->u.ops);
    /* Injected receiver only (N_ADDR for value, the pointer for ->). */
    if (a0 != NULL && NL_NEXT (a0) != NULL) return NULL;
  }
  re = recv->attr;
  if (re == NULL || re->type == NULL) return NULL;
  d = midopt_id_decl (recv);
  if (d == NULL) return NULL;
  if (re->type->mode == TM_CLASS) return d;
  if (midopt_opt_level () >= 2 && re->type->mode == TM_PTR && re->type->u.ptr_type != NULL
      && re->type->u.ptr_type->mode == TM_CLASS) {
    if (env == NULL) return NULL;
    f = midopt_env_find (env, d);
    if (f == NULL || !f->uniq_ptr_p || f->addr_taken_p) return NULL;
    return d;
  }
  return NULL;
}

/* Peel `recv.Count()` or `recv.Count() - K` (K const >= 0).  *MINUS is K. */
static node_t midopt_peel_count (struct midopt_env *env, node_t n, mir_llong *minus) {
  node_t recv;
  if (minus != NULL) *minus = 0;
  if (n == NULL) return NULL;
  recv = midopt_count_recv_from_call (env, n);
  if (recv != NULL) return recv;
  if (n->code == N_SUB) {
    node_t a = NL_HEAD (n->u.ops);
    node_t b = NL_NEXT (a);
    mir_llong lo, hi;
    recv = midopt_count_recv_from_call (env, a);
    if (recv != NULL && midopt_expr_ival (env, b, &lo, &hi) && lo == hi && lo >= 0) {
      if (minus != NULL) *minus = lo;
      return recv;
    }
  }
  return NULL;
}

/* Exact `recv.Count()` (minus == 0).  Used for R-LICM hoist of the call node. */
static node_t midopt_count_call_recv (struct midopt_env *env, node_t n) {
  mir_llong minus = 0;
  node_t recv = midopt_peel_count (env, n, &minus);
  return (recv != NULL && minus == 0) ? recv : NULL;
}

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
  /* Symbolic Count bound: `n = xs.Count()` ties n to xs's length.  Any other
     assignment severs the tie. */
  {
    struct midopt_fact *f = midopt_env_get (env, decl);
    mir_llong minus = 0;
    node_t recv = midopt_peel_count (env, rhs, &minus);
    if (f != NULL) {
      f->sym_recv = recv;
      f->sym_lo = 0;
      f->sym_minus = recv != NULL ? (int) minus : 0;
    }
  }
  /* Unique heap pointer: `p = new T` is alias-free until copied or &p. */
  {
    struct expr *le = lhs->attr;
    if (le != NULL && le->type != NULL && le->type->mode == TM_PTR) {
      struct midopt_fact *pf = midopt_env_get (env, decl);
      if (pf != NULL) {
        if (midopt_expr_is_new (rhs)) {
          pf->uniq_ptr_p = 1;
        } else {
          node_t rd = midopt_id_decl (rhs);
          struct midopt_fact *rf = rd != NULL ? midopt_env_find (env, rd) : NULL;
          pf->uniq_ptr_p = 0;
          if (rf != NULL) rf->uniq_ptr_p = 0; /* copy aliases — neither unique */
        }
      }
    }
  }
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
  /* Pointer-to-struct FAM (`p->a[i]` on trailing `T a[1]`): the declared
     length is not the live bound.  Value-object `s.a[i]` still uses it. */
  if (type_flex_arr_p (arr_type)) {
    node_t abase = arr;
    while (abase != NULL && abase->code == N_CAST) abase = NL_EL (abase->u.ops, 1);
    if (abase == NULL || abase->code != N_FIELD) return;
  }
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
    if (n->code == N_IND) {
      midopt_check_ind_oob (c2m_ctx, n, env);
      midopt_check_class_iv (c2m_ctx, n, env);
    }
    return;
  case N_ADDR: {
    /* &decl observed — monotone: an alias could shrink the collection later,
       unseen by the loop-body hazard scan.  Record on the decl's fact. */
    node_t d;
    if (midopt_node_has_ops (n->code)) {
      for (node_t c = NL_HEAD (n->u.ops); c != NULL; c = NL_NEXT (c))
        midopt_safety_expr (c2m_ctx, c, env);
    }
    d = midopt_id_decl (NL_HEAD (n->u.ops));
    if (d != NULL) {
      struct midopt_fact *f = midopt_env_get (env, d);
      if (f != NULL) f->addr_taken_p = 1;
    }
    return;
  }
  case N_MOVE: {
    /* move xs empties the source (count → 0): sever symbolic bounds on it. */
    node_t d;
    if (midopt_node_has_ops (n->code)) {
      for (node_t c = NL_HEAD (n->u.ops); c != NULL; c = NL_NEXT (c))
        midopt_safety_expr (c2m_ctx, c, env);
    }
    d = midopt_id_decl (NL_HEAD (n->u.ops));
    if (d != NULL) midopt_kill_sym_for (env, d);
    return;
  }
  case N_CALL: {
    node_t args, a, decl;
    const char *nm = NULL;
    node_t rd, funcn, first;
    int skip_first = 0;

    funcn = NL_HEAD (n->u.ops);
    args = NL_EL (n->u.ops, 1);
    first = (args != NULL && args->code == N_LIST) ? NL_HEAD (args->u.ops) : NULL;
    /* Method calls carry the receiver injected as args[0] (an N_ADDR copy for
       value receivers).  Walking it as an ordinary expression would mark the
       receiver addr_taken and sever its symbolic bounds; the injected copy
       has no user-visible effects — skip it. */
    if (first != NULL && funcn != NULL
        && (funcn->code == N_FIELD || funcn->code == N_DEREF_FIELD)) {
      node_t mobj = NL_HEAD (funcn->u.ops);
      struct expr *me = mobj != NULL ? mobj->attr : NULL;
      if (me != NULL && me->type != NULL
          && (me->type->mode == TM_CLASS
              || (me->type->mode == TM_PTR && me->type->u.ptr_type != NULL
                  && me->type->u.ptr_type->mode == TM_CLASS)))
        skip_first = 1;
    }

    /* Evaluate callee + args (minus the injected receiver). */
    if (funcn != NULL) midopt_safety_expr (c2m_ctx, funcn, env);
    if (args != NULL && args->code == N_LIST) {
      for (a = NL_HEAD (args->u.ops); a != NULL; a = NL_NEXT (a)) {
        if (skip_first && a == first) continue;
        midopt_safety_expr (c2m_ctx, a, env);
      }
    }
    /* IV-guarded Get/subscript access may be elide_oob-stamped (before the
       fact kills below, and while the env still holds the IV sym fact). */
    midopt_check_class_iv (c2m_ctx, n, env);
    /* A shrinking/unknown method on a collection severs its symbolic bounds.
       Safe (count-preserving) methods keep them. */
    rd = midopt_method_call_recv (funcn, &nm);
    if (rd != NULL && !midopt_method_safe_p (nm)) midopt_kill_sym_for (env, rd);
    /* Kill facts for pointer/int args (may free / mutate / escape). */
    if (args != NULL && args->code == N_LIST) {
      for (a = NL_HEAD (args->u.ops); a != NULL; a = NL_NEXT (a)) {
        if (skip_first && a == first) continue;
        decl = midopt_id_decl (a);
        if (decl == NULL && a->code == N_ADDR) {
          decl = midopt_id_decl (NL_HEAD (a->u.ops));
          if (decl != NULL) midopt_kill_sym_for (env, decl); /* &recv may shrink */
        }
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
  case N_SIZEOF: case N_EXPR_SIZEOF: case N_ALIGNOF:
    /* Operands of sizeof/_Alignof are UNEVALUATED in C: `sizeof(*p)` does not
       dereference p.  Descending would let the inner N_DEREF reach
       midopt_check_deref and raise a spurious "definite null dereference" on
       idioms like `x = calloc(1, sizeof(*x))` where x is provably null on this
       path.  (VLA size exprs are evaluated, but `sizeof(*p)` never derefs p.) */
    return;
  case N_GENERIC: {
    /* Controlling expr unevaluated; only the selected association (list head). */
    node_t list = NL_EL (n->u.ops, 1);
    node_t ga = (list != NULL && list->code == N_LIST) ? NL_HEAD (list->u.ops) : NULL;
    if (ga != NULL && ga->code == N_GENERIC_ASSOC)
      midopt_safety_expr (c2m_ctx, NL_EL (ga->u.ops, 1), env);
    return;
  }
  case N_ANDAND:
  case N_OROR: {
    node_t lhs = NL_HEAD (n->u.ops);
    node_t rhs = lhs != NULL ? NL_NEXT (lhs) : NULL;
    node_t decl;
    struct midopt_fact *f;
    int known;

    midopt_safety_expr (c2m_ctx, lhs, env);
    known = midopt_cond_known (lhs);
    if (n->code == N_ANDAND && known == 0 && c11_dead_skippable_p (rhs)) return;
    if (n->code == N_OROR && known == 1 && c11_dead_skippable_p (rhs)) return;
    decl = midopt_id_decl (lhs);
    f = decl != NULL ? midopt_env_find (env, decl) : NULL;
    if (f != NULL) {
      if (n->code == N_ANDAND && f->nullness == MN_NULL) return;
      if (n->code == N_OROR && f->nullness == MN_NONNULL) return;
    }
    midopt_safety_expr (c2m_ctx, rhs, env);
    return;
  }
  case N_COND: {
    node_t cond = NL_HEAD (n->u.ops);
    node_t texpr = cond != NULL ? NL_NEXT (cond) : NULL;
    node_t fexpr = texpr != NULL ? NL_NEXT (texpr) : NULL;
    int k;

    midopt_safety_expr (c2m_ctx, cond, env);
    k = midopt_cond_known (cond);
    if (k == 1 && c11_dead_skippable_p (fexpr)) {
      midopt_safety_expr (c2m_ctx, texpr, env);
      return;
    }
    if (k == 0 && c11_dead_skippable_p (texpr)) {
      midopt_safety_expr (c2m_ctx, fexpr, env);
      return;
    }
    midopt_safety_expr (c2m_ctx, texpr, env);
    midopt_safety_expr (c2m_ctx, fexpr, env);
    return;
  }
  case N_COALESCE: {
    node_t a = NL_HEAD (n->u.ops);
    node_t b = a != NULL ? NL_NEXT (a) : NULL;

    midopt_safety_expr (c2m_ctx, a, env);
    if (midopt_cond_known (a) != 1 || !c11_dead_skippable_p (b))
      midopt_safety_expr (c2m_ctx, b, env);
    return;
  }
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

/* ── R1: symbolic loop bounds (IV `i < recv.Count()`) ────────────────────────
 *
 * The Phase G lattice tracks constant intervals; it cannot prove `i < n` when
 * `n` is a receiver length.  This extends it with a symbolic fact: an
 * induction variable whose loop guard is `i < recv.Count()` (directly or via
 * a bound local `int n = recv.Count()`) carries sym_recv = recv's decl.
 * When the loop body provably cannot shrink or alias recv, accesses
 * `recv.Get(i)` / `recv.GetMut(i)` / `recv[i]` are stamped elide_oob_p and
 * gen drops the bounds trap (gen reads the stamp on the N_CALL / N_IND).
 *
 * Soundness rules (all conservative):
 *   · sym tie is severed (midopt_kill_sym_for) by: shrink/unknown method on
 *     recv, taking recv's address anywhere, move/reassign/field-write of recv.
 *   · the IV handler also requires recv !addr_taken and a clean body scan
 *     (no IV/bound writes, no recv hazards) before stamping.
 *   · only step forms that never decrease the IV qualify (i ≥ init).
 *   · sym_lo (constant initial value) must be >= 0 for the lower bound.
 * Growth of recv during the loop is SAFE for these proofs (count can only
 * exceed the guard value); shrink is what we exclude. */

/* Methods that never *decrease* a dense collection's element count.
   Unknown (user-defined) methods on the receiver are treated as hazards. */
static const char *const midopt_safe_methods[] = {
  /* List */
  "Get", "GetMut", "Count", "IsEmpty", "Capacity", "First", "Last", "FirstMut",
  "LastMut", "GetOr", "TryGet", "FirstOr", "LastOr", "IndexOf", "LastIndexOf",
  "Contains", "FindIndex", "Find", "FindOr", "Any", "All", "CountWhere",
  "ForEach", "View", "ToArray", "CopyTo", "Equals", "ToJson", "ToString",
  "to_string", "ToJsonArray", "StringsToJsonArray", "IntsToJsonArray",
  "ToArrayDict", "ToDict", "ToJsonArrayBy", "ToDictBy", "Add", "Insert",
  "Concat", "AddRange", "InsertRange", "EnsureCapacity", "Set", "Sort",
  "Reverse", "TrimExcess", "Copy", "Slice", "Plus", "Distinct", "Filter",
  "Where", "Map", "Select", "SelectString", "Take", "Skip", "Repeat", "Range",
  "FromJson", "FromView", "owns",
  /* Set */
  "Union", "Intersect", "Difference", "IsSubsetOf",
  /* Map */
  "ContainsKey", "TryAdd", "GetOrAdd", "AddOrUpdate", "Merge", "WhereKeys",
  "WhereValues", "SelectValues", "SelectKeys", "GroupBy", "Keys", "Values",
  "ContainsValue", "insert_new_at", "find_slot", "find_index", "ensure_table",
  "grow_table", "init_storage", "destroy_key_at", "destroy_val_at",
  "ownsValues", "ownsKeys", "KeyAt", "ValAt", "ValMut", NULL
};

static int midopt_method_safe_p (const char *nm) {
  size_t i;
  if (nm == NULL) return 0;
  for (i = 0; midopt_safe_methods[i] != NULL; i++)
    if (strcmp (nm, midopt_safe_methods[i]) == 0) return 1;
  return 0;
}

/* Clear every symbolic bound tied to receiver DECL (it may have shrunk). */
static void midopt_kill_sym_for (struct midopt_env *env, node_t decl) {
  int i;
  if (decl == NULL || env == NULL) return;
  for (i = 0; i < env->n; i++)
    if (env->f[i].sym_recv == decl) env->f[i].sym_recv = NULL;
}

/* Decl of the receiver object for a method call's func node, if it is an
   N_FIELD (value receiver) on an N_ID.  Fills *name with the method name. */
static node_t midopt_method_call_recv (node_t func, const char **name) {
  node_t recv, mid;
  if (func == NULL || (func->code != N_FIELD && func->code != N_DEREF_FIELD)) return NULL;
  recv = NL_HEAD (func->u.ops);
  mid = NL_NEXT (recv);
  if (recv == NULL || recv->code != N_ID || mid == NULL || mid->code != N_ID) return NULL;
  if (name != NULL) *name = mid->u.s.s;
  return midopt_id_decl (recv);
}

/* Peel the N_DECL (declarator node) out of a (possibly wrapped) declarator. */
static node_t midopt_declarator_node (node_t declarator) {
  node_t d = declarator;
  int guard = 0;
  while (d != NULL && guard++ < 16) {
    if (d->code == N_DECL) return d;
    if (d->code == N_POINTER || d->code == N_ARR || d->code == N_FUNC)
      d = NL_HEAD (d->u.ops);
    else
      break;
  }
  return NULL;
}

/* Decl-identity compare tolerant of the lattice's dual keying: facts are
   stored under the N_DECL declarator *and* the N_SPEC_DECL wrapper. */
static int midopt_same_decl (node_t a, node_t b) {
  if (a == NULL || b == NULL) return 0;
  if (a == b) return 1;
  if (a->code == N_SPEC_DECL && midopt_declarator_node (SPEC_DECL_DECL (a)) == b) return 1;
  if (b->code == N_SPEC_DECL && midopt_declarator_node (SPEC_DECL_DECL (b)) == a) return 1;
  return 0;
}

/* Recursive hazard scan for the IV loop body.  Returns 1 when the subtree
   may shrink/alias the collection (BOUND_RECV), write the IV or the bound
   local (IV_DECL / BOUND_DECL), or take the collection's address. */
static int midopt_iv_hazard_p (node_t n, node_t iv_decl, node_t bound_decl, node_t bound_recv) {
  node_t c;
  if (n == NULL) return 0;

  switch (n->code) {
  case N_ASSIGN: case N_ADD_ASSIGN: case N_SUB_ASSIGN: case N_MUL_ASSIGN:
  case N_DIV_ASSIGN: case N_MOD_ASSIGN: case N_LSH_ASSIGN: case N_RSH_ASSIGN:
  case N_AND_ASSIGN: case N_OR_ASSIGN: case N_XOR_ASSIGN: {
    node_t lhs = NL_HEAD (n->u.ops);
    node_t d = midopt_id_decl (lhs);
    if (d != NULL && (midopt_same_decl (d, iv_decl) || midopt_same_decl (d, bound_decl)
                      || midopt_same_decl (d, bound_recv)))
      return 1;
    /* recv.field = … / recv[i] = … writes: lhs shape FIELD/IND on the recv. */
    if (lhs != NULL && (lhs->code == N_FIELD || lhs->code == N_DEREF_FIELD
                        || lhs->code == N_IND)) {
      node_t base = NL_HEAD (lhs->u.ops);
      if (midopt_same_decl (midopt_id_decl (base), bound_recv)) return 1;
    }
    break;
  }
  case N_INC: case N_DEC: case N_POST_INC: case N_POST_DEC: {
    node_t d = midopt_id_decl (NL_HEAD (n->u.ops));
    if (d != NULL && (midopt_same_decl (d, iv_decl) || midopt_same_decl (d, bound_decl)))
      return 1;
    break;
  }
  case N_ADDR: {
    if (midopt_same_decl (midopt_id_decl (NL_HEAD (n->u.ops)), bound_recv)) return 1;
    break;
  }
  case N_MOVE: {
    if (midopt_same_decl (midopt_id_decl (NL_HEAD (n->u.ops)), bound_recv)) return 1;
    break;
  }
  case N_CALL: {
    const char *nm = NULL;
    node_t fn = NL_HEAD (n->u.ops);
    node_t rd = midopt_method_call_recv (fn, &nm);
    if (rd != NULL && midopt_same_decl (rd, bound_recv) && !midopt_method_safe_p (nm))
      return 1;
    /* Recurse into the args but skip the injected receiver copy (args[0]
       N_ADDR) — otherwise every value-receiver method call looks like taking
       the collection's address. */
    if (fn != NULL && (fn->code == N_FIELD || fn->code == N_DEREF_FIELD)) {
      node_t args = NL_EL (n->u.ops, 1);
      node_t a0 = args != NULL && args->code == N_LIST ? NL_HEAD (args->u.ops) : NULL;
      if (a0 != NULL) {
        node_t a;
        /* Skip injected receiver (N_ADDR for value, the pointer for ->). */
        if (midopt_iv_hazard_p (fn, iv_decl, bound_decl, bound_recv)) return 1;
        for (a = NL_NEXT (a0); a != NULL; a = NL_NEXT (a))
          if (midopt_iv_hazard_p (a, iv_decl, bound_decl, bound_recv)) return 1;
        return 0;
      }
    }
    break;
  }
  case N_DEFER: {
    /* A defer mentioning the receiver runs at scope exit — inside a loop body
       that is once per iteration, before later iterations' accesses.  Scan it. */
    break;
  }
  default:
    break;
  }

  if (!midopt_node_has_ops (n->code)) return 0;
  for (c = NL_HEAD (n->u.ops); c != NULL; c = NL_NEXT (c))
    if (midopt_iv_hazard_p (c, iv_decl, bound_decl, bound_recv)) return 1;
  return 0;
}

/* Recognize the counted-loop shape and run the body with the IV fact.
 * Returns 1 when handled (always — caller continues with the post-loop kill).
 *
 *   for (int i = LO; i < BOUND; i++)      — LO const >= 0
 *   for (i = LO; i <= BOUND; i += k)
 *
 * BOUND: const / bound local with interval / recv.Count() / bound local tied
 * to recv.Count().  Steps: ++, += k>0, i = i + k>0, or empty. */
static void midopt_safety_for (c2m_ctx_t c2m_ctx, node_t n, struct midopt_env *env) {
  node_t init, cond, iter, stmt;
  node_t iv_decl = NULL, iv_spec = NULL, bound_decl = NULL, bound_recv = NULL;
  node_t bound_call = NULL; /* the direct `recv.Count()` call node, if any (R-LICM) */
  mir_llong init_lo = 0, init_hi = 0, bound_lo = 0, bound_hi = 0, bound_minus = 0;
  int init_p = 0, bound_ival_p = 0, strict = 1, step_ok = 0, allow_sym = 0;
  struct midopt_env body_env;
  struct midopt_fact *f;

  init = NL_EL (n->u.ops, 1);
  cond = init != NULL ? NL_NEXT (init) : NULL;
  iter = cond != NULL ? NL_NEXT (cond) : NULL;
  stmt = iter != NULL ? NL_NEXT (iter) : NULL;

  /* Walk init first: registers the IV's initializer facts (and diagnostics)
     in env, keyed under both SPEC_DECL and N_DECL like other locals. */
  if (init != NULL && init->code != N_IGNORE)
    midopt_safety_stmt (c2m_ctx, init, env);

  /* for (...; 0; ...) — init runs, body/iter do not (unless a label is
     reachable via goto). */
  if (cond != NULL && cond->code != N_IGNORE && midopt_cond_known (cond) == 0
      && c11_dead_skippable_p (stmt) && c11_dead_skippable_p (iter)) {
    midopt_safety_expr (c2m_ctx, cond, env);
    return;
  }

  /* ── init: `int i = LO;` (N_LIST-wrapped N_SPEC_DECL) or `i = LO;`
     (bare N_ASSIGN — for-headers do not wrap in N_EXPR here) ── */
  {
    node_t init0 = init;
    if (init0 != NULL && init0->code == N_LIST && NL_HEAD (init0->u.ops) != NULL
        && NL_NEXT (NL_HEAD (init0->u.ops)) == NULL)
      init0 = NL_HEAD (init0->u.ops); /* single-decl list wrapper */
    if (init0 != NULL && init0->code == N_SPEC_DECL) {
      node_t initializer = SPEC_DECL_INIT (init0);
      /* Key by the N_SPEC_DECL itself — the node u.lvalue_node points at. */
      iv_decl = init0;
      iv_spec = init0;
      if (initializer != NULL && initializer->code != N_IGNORE)
        init_p = midopt_expr_ival (env, initializer, &init_lo, &init_hi);
    } else if (init0 != NULL && (init0->code == N_ASSIGN
               || (init0->code == N_EXPR && NL_EL (init0->u.ops, 1) != NULL
                   && NL_EL (init0->u.ops, 1)->code == N_ASSIGN))) {
      node_t ex = init0->code == N_EXPR ? NL_EL (init0->u.ops, 1) : init0;
      node_t lhs = NL_HEAD (ex->u.ops);
      node_t rhs = NL_NEXT (lhs);
      iv_decl = midopt_id_decl (lhs);
      if (iv_decl != NULL && rhs != NULL)
        init_p = midopt_expr_ival (env, rhs, &init_lo, &init_hi);
    }
  }

  /* ── cond: `i < B` / `i <= B` / `B > i` / `B >= i` with B matching ── */
  if (iv_decl != NULL && cond != NULL
      && (cond->code == N_LT || cond->code == N_LE || cond->code == N_GT
          || cond->code == N_GE)) {
    node_t a = NL_HEAD (cond->u.ops);
    node_t b = NL_NEXT (a);
    node_t bound_expr = NULL;
    strict = (cond->code == N_LT || cond->code == N_GT);
    if (a != NULL && b != NULL) {
      if ((cond->code == N_LT || cond->code == N_LE)
          && midopt_same_decl (midopt_id_decl (a), iv_decl))
        bound_expr = b;
      else if ((cond->code == N_GT || cond->code == N_GE)
               && midopt_same_decl (midopt_id_decl (b), iv_decl))
        bound_expr = a;
    }
    if (bound_expr != NULL) {
      bound_ival_p = midopt_expr_ival (env, bound_expr, &bound_lo, &bound_hi);
      bound_recv = midopt_peel_count (env, bound_expr, &bound_minus);
      if (bound_recv != NULL && bound_minus == 0
          && midopt_count_call_recv (env, bound_expr) != NULL)
        bound_call = bound_expr; /* exact Count() — eligible for R-LICM */
      bound_decl = midopt_id_decl (bound_expr);
      if (bound_recv == NULL && bound_decl != NULL) {
        struct midopt_fact *bf = midopt_env_find (env, bound_decl);
        if (bf != NULL && bf->sym_recv != NULL) {
          bound_recv = bf->sym_recv;
          bound_minus = bf->sym_minus;
        }
      }
      /* i < Count()-k is always in range for k>=0; i <= Count()-k needs k>=1. */
      allow_sym = bound_recv != NULL && ((strict && bound_minus >= 0) || (!strict && bound_minus >= 1));
    }
  }

  /* ── iter: only non-decreasing steps on the IV qualify ── */
  if (iv_decl != NULL) {
    if (iter == NULL || iter->code == N_IGNORE) {
      step_ok = 1; /* no step: body cannot change i (hazard scan enforces) */
    } else if (iter->code == N_EXPR || 1) {
      node_t ex = iter->code == N_EXPR ? NL_EL (iter->u.ops, 1) : iter;
      if (ex != NULL) {
        if ((ex->code == N_POST_INC || ex->code == N_INC)
            && midopt_same_decl (midopt_id_decl (NL_HEAD (ex->u.ops)), iv_decl)) {
          step_ok = 1;
        } else if (ex->code == N_ADD_ASSIGN
                   && midopt_same_decl (midopt_id_decl (NL_HEAD (ex->u.ops)), iv_decl)) {
          mir_llong lo, hi;
          if (midopt_expr_ival (env, NL_NEXT (NL_HEAD (ex->u.ops)), &lo, &hi) && lo >= 0)
            step_ok = 1;
        } else if (ex->code == N_ASSIGN
                   && midopt_same_decl (midopt_id_decl (NL_HEAD (ex->u.ops)), iv_decl)) {
          node_t rhs = NL_NEXT (NL_HEAD (ex->u.ops));
          if (rhs != NULL && rhs->code == N_ADD) {
            node_t x = NL_HEAD (rhs->u.ops);
            node_t y = NL_NEXT (x);
            mir_llong lo, hi;
            if (midopt_same_decl (midopt_id_decl (x), iv_decl)
                && midopt_expr_ival (env, y, &lo, &hi) && lo >= 0)
              step_ok = 1;
            else if (midopt_same_decl (midopt_id_decl (y), iv_decl)
                     && midopt_expr_ival (env, x, &lo, &hi) && lo >= 0)
              step_ok = 1;
          }
        }
      }
    }
  }

  /* ── analyze cond for diagnostics, then the body with the IV fact ── */
  if (cond != NULL && cond->code != N_IGNORE)
    midopt_safety_expr (c2m_ctx, cond, env);

  midopt_env_copy (&body_env, env);

  if (iv_decl != NULL && step_ok && (bound_ival_p || bound_recv != NULL)
      && init_p && init_lo >= 0
      && !midopt_iv_hazard_p (stmt, iv_decl, bound_decl, bound_recv)) {
    /* recv must never be address-taken (an alias could shrink it unseen). */
    int recv_escaped = 0;
    if (bound_recv != NULL) {
      struct midopt_fact *rf = midopt_env_find (env, bound_recv);
      recv_escaped = rf != NULL && rf->addr_taken_p;
    }
    /* Store the IV fact under every key form N_ID lookups may resolve to. */
    node_t keys[2];
    int nk = 0, ki;
    keys[nk++] = iv_decl;
    if (iv_spec != NULL && iv_spec != iv_decl) keys[nk++] = iv_spec;
    for (ki = 0; ki < nk; ki++) {
      f = midopt_env_get (&body_env, keys[ki]);
      if (f == NULL) continue;
      if (bound_ival_p) {
        f->ival_p = 1;
        f->lo = init_lo;
        f->hi = bound_hi - (strict ? 1 : 0);
      }
      if (allow_sym && !recv_escaped && init_lo == init_hi) {
        f->sym_recv = bound_recv;
        f->sym_lo = init_lo;
        f->sym_minus = (int) bound_minus;
      }
    }
    /* R-LICM: the loop bound is a direct `recv.Count()` whose receiver is
       neither escaped nor mutated anywhere in the body/iter → the count is
       loop-invariant.  Stamp the call so gen memoizes its pre-header value and
       stops re-calling it every iteration.  This is STRICTER than the OOB proof
       above (which tolerates growth): here any mutation, incl. Add, disqualifies. */
    if (bound_call != NULL && bound_recv != NULL && !recv_escaped
        && bound_call->attr != NULL
        && !midopt_recv_mutated_p (stmt, bound_recv)
        && !midopt_recv_mutated_p (iter, bound_recv)) {
      ((struct expr *) bound_call->attr)->hoist_call_p = 1;
      if (midopt_verbose_p)
        fprintf (stderr, "  [midopt] R-LICM hoist loop-invariant Count() bound\n");
    }
  }

  if (stmt != NULL) midopt_safety_stmt (c2m_ctx, stmt, &body_env);
  if (iter != NULL && iter->code != N_IGNORE)
    midopt_safety_expr (c2m_ctx, iter, &body_env);

  /* Loops invalidate intervals in the continuing env (existing rule). */
  {
    int i;
    for (i = 0; i < env->n; i++) env->f[i].ival_p = 0;
  }
}

/* True when N is a non-decreasing step of IV (`i++`, `++i`, `i += k>=0`,
   `i = i + k>=0`).  N_EXPR wrappers are peeled. */
static int midopt_nondec_step_p (struct midopt_env *env, node_t n, node_t iv) {
  node_t ex;
  if (n == NULL || iv == NULL) return 0;
  ex = n->code == N_EXPR ? NL_EL (n->u.ops, 1) : n;
  if (ex == NULL) return 0;
  if ((ex->code == N_POST_INC || ex->code == N_INC)
      && midopt_same_decl (midopt_id_decl (NL_HEAD (ex->u.ops)), iv))
    return 1;
  if (ex->code == N_ADD_ASSIGN
      && midopt_same_decl (midopt_id_decl (NL_HEAD (ex->u.ops)), iv)) {
    mir_llong lo, hi;
    if (midopt_expr_ival (env, NL_NEXT (NL_HEAD (ex->u.ops)), &lo, &hi) && lo >= 0)
      return 1;
  }
  if (ex->code == N_ASSIGN
      && midopt_same_decl (midopt_id_decl (NL_HEAD (ex->u.ops)), iv)) {
    node_t rhs = NL_NEXT (NL_HEAD (ex->u.ops));
    if (rhs != NULL && rhs->code == N_ADD) {
      node_t x = NL_HEAD (rhs->u.ops);
      node_t y = NL_NEXT (x);
      mir_llong lo, hi;
      if (midopt_same_decl (midopt_id_decl (x), iv) && midopt_expr_ival (env, y, &lo, &hi)
          && lo >= 0)
        return 1;
      if (midopt_same_decl (midopt_id_decl (y), iv) && midopt_expr_ival (env, x, &lo, &hi)
          && lo >= 0)
        return 1;
    }
  }
  return 0;
}

/* `while (i < B) { …; i++; }` — same IV fact as N_FOR when the increment is
   the last statement (so the body sees i still below B).  N_DO is not handled:
   the first iteration is unguarded. */
static void midopt_safety_while (c2m_ctx_t c2m_ctx, node_t n, struct midopt_env *env) {
  node_t cond, stmt, a, b, bound_expr, iv_decl, bound_decl, bound_recv, bound_call;
  node_t list = NULL, last = NULL, s;
  int strict = 1, step_ok = 0, allow_sym = 0, bound_ival_p = 0, init_p = 0, hazard = 0;
  mir_llong init_lo = 0, init_hi = 0, bound_lo = 0, bound_hi = 0, bound_minus = 0;
  struct midopt_env body_env;
  struct midopt_fact *f, *bf;

  cond = NL_EL (n->u.ops, 1);
  stmt = NL_EL (n->u.ops, 2);

  /* while (0) { ... } — body never runs unless a label is a goto target. */
  if (midopt_cond_known (cond) == 0 && c11_dead_skippable_p (stmt)) {
    if (cond != NULL && cond->code != N_IGNORE) midopt_safety_expr (c2m_ctx, cond, env);
    return;
  }

  iv_decl = NULL;
  bound_expr = NULL;
  bound_decl = NULL;
  bound_recv = NULL;
  bound_call = NULL;
  if (cond != NULL
      && (cond->code == N_LT || cond->code == N_LE || cond->code == N_GT
          || cond->code == N_GE)) {
    a = NL_HEAD (cond->u.ops);
    b = a != NULL ? NL_NEXT (a) : NULL;
    strict = (cond->code == N_LT || cond->code == N_GT);
    if (a != NULL && b != NULL) {
      if ((cond->code == N_LT || cond->code == N_LE) && a->code == N_ID) {
        iv_decl = midopt_id_decl (a);
        bound_expr = b;
      } else if ((cond->code == N_GT || cond->code == N_GE) && b->code == N_ID) {
        iv_decl = midopt_id_decl (b);
        bound_expr = a;
      }
    }
  }

  if (iv_decl != NULL) {
    f = midopt_env_find (env, iv_decl);
    if (f != NULL && f->ival_p && f->lo >= 0) {
      init_p = 1;
      init_lo = f->lo;
      init_hi = f->hi;
    }
  }
  if (bound_expr != NULL) {
    bound_ival_p = midopt_expr_ival (env, bound_expr, &bound_lo, &bound_hi);
    bound_recv = midopt_peel_count (env, bound_expr, &bound_minus);
    if (bound_recv != NULL && bound_minus == 0
        && midopt_count_call_recv (env, bound_expr) != NULL)
      bound_call = bound_expr;
    bound_decl = midopt_id_decl (bound_expr);
    if (bound_recv == NULL && bound_decl != NULL) {
      bf = midopt_env_find (env, bound_decl);
      if (bf != NULL && bf->sym_recv != NULL) {
        bound_recv = bf->sym_recv;
        bound_minus = bf->sym_minus;
      }
    }
    allow_sym
      = bound_recv != NULL && ((strict && bound_minus >= 0) || (!strict && bound_minus >= 1));
  }

  if (stmt != NULL && stmt->code == N_BLOCK) {
    list = NL_EL (stmt->u.ops, 1);
    if (list != NULL && list->code == N_LIST) {
      for (s = NL_HEAD (list->u.ops); s != NULL; s = NL_NEXT (s)) last = s;
      step_ok = midopt_nondec_step_p (env, last, iv_decl);
    }
  } else {
    step_ok = midopt_nondec_step_p (env, stmt, iv_decl);
  }

  if (cond != NULL && cond->code != N_IGNORE) midopt_safety_expr (c2m_ctx, cond, env);
  midopt_env_copy (&body_env, env);

  if (iv_decl != NULL && step_ok && init_p && init_lo == init_hi
      && (bound_ival_p || allow_sym)) {
    if (list != NULL) {
      for (s = NL_HEAD (list->u.ops); s != NULL; s = NL_NEXT (s)) {
        if (s == last) continue;
        if (midopt_iv_hazard_p (s, iv_decl, bound_decl, bound_recv)) {
          hazard = 1;
          break;
        }
      }
    } else {
      hazard = 0; /* body is only the step */
    }
    if (!hazard) {
      int recv_escaped = 0;
      if (bound_recv != NULL) {
        struct midopt_fact *rf = midopt_env_find (env, bound_recv);
        recv_escaped = rf != NULL && rf->addr_taken_p;
      }
      f = midopt_env_get (&body_env, iv_decl);
      if (f != NULL) {
        if (bound_ival_p) {
          f->ival_p = 1;
          f->lo = init_lo;
          f->hi = bound_hi - (strict ? 1 : 0);
        }
        if (allow_sym && !recv_escaped) {
          f->sym_recv = bound_recv;
          f->sym_lo = init_lo;
          f->sym_minus = (int) bound_minus;
        }
      }
      if (bound_call != NULL && bound_recv != NULL && !recv_escaped
          && bound_call->attr != NULL) {
        int mut = 0;
        if (list != NULL) {
          for (s = NL_HEAD (list->u.ops); s != NULL; s = NL_NEXT (s)) {
            if (s == last) continue;
            if (midopt_recv_mutated_p (s, bound_recv)) {
              mut = 1;
              break;
            }
          }
        }
        if (!mut) {
          ((struct expr *) bound_call->attr)->hoist_call_p = 1;
          if (midopt_verbose_p)
            fprintf (stderr, "  [midopt] R-LICM hoist while-invariant Count() bound\n");
        }
      }
    }
  }

  if (stmt != NULL) midopt_safety_stmt (c2m_ctx, stmt, &body_env);
  {
    int i;
    for (i = 0; i < env->n; i++) env->f[i].ival_p = 0;
  }
}

/* Stamp elide_oob_p on collection accesses guarded by a proven IV bound:
 *   xs[i]           — class subscript (N_IND, TM_CLASS value receiver)
 *   xs.Get(i) / xs.GetMut(i) — N_CALL on a TM_CLASS value receiver
 * gen reads elide_oob_p on these nodes (N_CALL intercept / subscript flags). */
static void midopt_check_class_iv (c2m_ctx_t c2m_ctx, node_t n, struct midopt_env *env) {
  struct expr *e;
  node_t recv, idx, rdecl, idecl;
  struct midopt_fact *f;

  if (n == NULL || n->attr == NULL || n->attr == (void *) ((intptr_t) -1)) return;
  e = (struct expr *) n->attr;
  if (e->type == NULL) return;

  if (n->code == N_IND) {
    recv = NL_HEAD (n->u.ops);
    idx = NL_NEXT (recv);
    if (recv == NULL || recv->code != N_ID || idx == NULL || idx->code != N_ID) return;
    {
      struct expr *ae = recv->attr;
      struct type *at;
      if (ae == NULL || ae->type == NULL) return;
      at = ae->type;
      if (at->mode == TM_PTR && at->u.ptr_type != NULL) at = at->u.ptr_type;
      if (at->mode != TM_CLASS) return;
    }
    rdecl = midopt_id_decl (recv);
    idecl = midopt_id_decl (idx);
    f = midopt_env_find (env, idecl);
    if (rdecl != NULL && f != NULL && f->sym_recv == rdecl && f->sym_lo >= 0) {
      if (!e->elide_oob_p) {
        e->elide_oob_p = 1;
        midopt_safety_n_elide++;
      }
      if (e->own_deref_class == DEREF_GUARD_DEFAULT) e->own_deref_class = DEREF_GUARD_SAFE;
    }
    return;
  }

  if (n->code == N_CALL) {
    const char *nm = NULL;
    node_t args, a0, fn;
    fn = NL_HEAD (n->u.ops);
    rdecl = midopt_method_call_recv (fn, &nm);
    if (rdecl == NULL || nm == NULL) return;
    if (strcmp (nm, "Get") != 0 && strcmp (nm, "GetMut") != 0
        && strcmp (nm, "KeyAt") != 0 && strcmp (nm, "ValAt") != 0
        && strcmp (nm, "ValMut") != 0)
      return;
    recv = NL_HEAD (fn->u.ops);
    {
      struct expr *ae = recv->attr;
      struct type *at;
      if (ae == NULL || ae->type == NULL) return;
      at = ae->type;
      if (at->mode == TM_PTR && at->u.ptr_type != NULL) at = at->u.ptr_type;
      if (at->mode != TM_CLASS) return;
    }
    args = NL_EL (n->u.ops, 1);
    if (args == NULL || args->code != N_LIST) return;
    a0 = NL_HEAD (args->u.ops);
    /* args[0] is the injected receiver; the user index is args[1]. */
    if (a0 == NULL) return;
    a0 = NL_NEXT (a0);
    if (a0 == NULL || a0->code != N_ID || NL_NEXT (a0) != NULL) return;
    idecl = midopt_id_decl (a0);
    f = midopt_env_find (env, idecl);
    if (f != NULL && f->sym_recv == rdecl && f->sym_lo >= 0) {
      if (!e->elide_oob_p) {
        e->elide_oob_p = 1;
        midopt_safety_n_elide++;
      }
    }
    return;
  }
}

/* ── R2: for-in loop var borrow-don't-copy proof ─────────────────────────────
 *
 * For `for (auto s in xs)` (and the two-var form's element var) over a dense
 * List/Set of by-value class elements, gen normally block-copies `*(data+i)`
 * into the loop var each iteration.  When the body (a) never mutates the var,
 * its fields, or the collection, (b) never takes the var's address or moves
 * it, and (c) calls only proven read-only methods on the var, the var can be
 * bound *by reference* (pointer into the buffer) with identical semantics —
 * no per-iteration copy.  This file proves those conditions and stamps the
 * var decl's byref_p; gen (classyc.c) consumes it. */

/* Collection methods that only read (never mutate buffer contents or address).
   Anything not listed disqualifies the borrow (conservative). */
static const char *const midopt_pure_coll_methods[] = {
  "Get", "Count", "IsEmpty", "Capacity", "First", "Last", "FirstOr", "LastOr",
  "GetOr", "TryGet", "IndexOf", "LastIndexOf", "Contains", "FindIndex", "Find",
  "FindOr", "Any", "All", "CountWhere", "ForEach", "Equals", "View", "ToArray",
  "CopyTo", "ToJson", "ToString", "to_string", "ToJsonArray",
  "StringsToJsonArray", "IntsToJsonArray", "ToArrayDict", "ToDict",
  "ToJsonArrayBy", "ToDictBy", "Copy", "Slice", "Plus", "Distinct", "Filter",
  "Where", "Map", "Select", "SelectString", "Take", "Skip", "FromJson",
  "FromView", "KeyAt", "ValAt", "ContainsKey", "ContainsValue", "Keys",
  "Values", "GroupBy", "WhereKeys", "WhereValues", "SelectValues", "SelectKeys",
  NULL
};

static int midopt_pure_coll_method_p (const char *nm) {
  size_t i;
  if (nm == NULL) return 0;
  for (i = 0; midopt_pure_coll_methods[i] != NULL; i++)
    if (strcmp (nm, midopt_pure_coll_methods[i]) == 0) return 1;
  return 0;
}

/* Expression's root object is DECL?  (N_ID decl, or N_FIELD/N_DEREF chain.) */
static int midopt_rooted_at (node_t n, node_t decl) {
  int guard = 0;
  while (n != NULL && guard++ < 32) {
    if (n->code == N_ID) return midopt_same_decl (midopt_id_decl (n), decl);
    if (n->code == N_FIELD || n->code == N_DEREF_FIELD || n->code == N_DEREF
        || n->code == N_IND) {
      n = NL_HEAD (n->u.ops);
      continue;
    }
    return 0;
  }
  return 0;
}

/* Strict receiver-mutation scan for R-LICM (hoisting `recv.Count()`).  Unlike
   midopt_iv_hazard_p — which whitelists growth (Add/Insert/…) as OOB-safe
   because it only *increases* length — hoisting a count bound requires the
   count be INVARIANT, so any mutating method (not in the pure-read whitelist),
   any write through RECV, address-of, move, or delete disqualifies.  Returns 1
   if N (a loop body / iter) may change RECV's observable count. */
static int midopt_recv_mutated_p (node_t n, node_t recv) {
  node_t c;
  if (n == NULL || recv == NULL) return 0;
  switch (n->code) {
  case N_ASSIGN: case N_ADD_ASSIGN: case N_SUB_ASSIGN: case N_MUL_ASSIGN:
  case N_DIV_ASSIGN: case N_MOD_ASSIGN: case N_LSH_ASSIGN: case N_RSH_ASSIGN:
  case N_AND_ASSIGN: case N_OR_ASSIGN: case N_XOR_ASSIGN: {
    node_t lhs = NL_HEAD (n->u.ops);
    if (midopt_same_decl (midopt_id_decl (lhs), recv)) return 1;
    if (lhs != NULL && (lhs->code == N_FIELD || lhs->code == N_DEREF_FIELD
                        || lhs->code == N_IND || lhs->code == N_DEREF)
        && midopt_rooted_at (lhs, recv))
      return 1;
    break;
  }
  case N_INC: case N_DEC: case N_POST_INC: case N_POST_DEC:
    if (midopt_rooted_at (NL_HEAD (n->u.ops), recv)) return 1;
    break;
  case N_ADDR: case N_MOVE: case N_DELETE:
    if (midopt_same_decl (midopt_id_decl (NL_HEAD (n->u.ops)), recv)) return 1;
    break;
  case N_CALL: {
    const char *nm = NULL;
    node_t fn = NL_HEAD (n->u.ops);
    node_t rd = midopt_method_call_recv (fn, &nm);
    if (rd != NULL && midopt_same_decl (rd, recv) && !midopt_pure_coll_method_p (nm))
      return 1;
    /* Skip the injected receiver-copy arg (args[0] N_ADDR) like iv_hazard_p. */
    if (fn != NULL && (fn->code == N_FIELD || fn->code == N_DEREF_FIELD)) {
      node_t args = NL_EL (n->u.ops, 1);
      node_t a0 = args != NULL && args->code == N_LIST ? NL_HEAD (args->u.ops) : NULL;
      if (a0 != NULL) {
        node_t a;
        if (midopt_recv_mutated_p (fn, recv)) return 1;
        for (a = NL_NEXT (a0); a != NULL; a = NL_NEXT (a))
          if (midopt_recv_mutated_p (a, recv)) return 1;
        return 0;
      }
    }
    break;
  }
  default:
    break;
  }
  if (!midopt_node_has_ops (n->code)) return 0;
  for (c = NL_HEAD (n->u.ops); c != NULL; c = NL_NEXT (c))
    if (midopt_recv_mutated_p (c, recv)) return 1;
  return 0;
}

/* Resolve a method call's FUNC_DEF on the receiver's class (name+arity), or
   NULL when not uniquely resolvable.  RECV_EXPR is the receiver (N_ID or
   N_FIELD chain); NARGS counts user args (this excluded). */
static node_t midopt_find_method_def (c2m_ctx_t c2m_ctx, node_t func, int nargs) {
  node_t recv, mid;
  struct expr *re;
  struct type *rt;
  node_t tag, id;
  symbol_t sym;
  size_t i;

  if (func == NULL || func->code != N_FIELD) return NULL;
  recv = NL_HEAD (func->u.ops);
  mid = NL_NEXT (recv);
  if (recv == NULL || mid == NULL || mid->code != N_ID || mid->u.s.s == NULL) return NULL;
  re = recv->attr;
  if (re == NULL || re->type == NULL) return NULL;
  rt = re->type;
  if (rt->mode != TM_CLASS || rt->u.tag_type == NULL) return NULL;
  tag = rt->u.tag_type;
  id = build_id (c2m_ctx, mid->u.s.s, POS (func));
  if (!find_overload_sym (c2m_ctx, id, tag, &sym)) return NULL;
  node_t found = NULL;
  for (i = 0; i < VARR_LENGTH (node_t, sym.defs); i++) {
    node_t def = VARR_GET (node_t, sym.defs, i);
    decl_t d;
    struct func_type *ft;
    int nparams = 0;

    if (def == NULL || def->code != N_FUNC_DEF || def->attr == NULL) continue;
    if (def->attr == (void *) ((intptr_t) -1)) continue;
    d = (decl_t) def->attr;
    if (d->decl_spec.type == NULL || d->decl_spec.type->mode != TM_FUNC) continue;
    ft = d->decl_spec.type->u.func_type;
    if (ft == NULL || ft->class_scope != tag) continue;
    if (ft->param_list != NULL && ft->param_list->code == N_LIST)
      nparams = (int) NL_LENGTH (ft->param_list->u.ops);
    /* Methods carry `this` as the first param. */
    if (nparams != nargs + 1) continue;
    if (found != NULL) return NULL; /* ambiguous */
    found = def;
  }
  return found;
}

/* Analyze a candidate method body for writes to `this` (depth-capped).
   Returns 1 when the method provably does not mutate the receiver. */
static int midopt_stmt_no_this_write_p (c2m_ctx_t c2m_ctx, node_t n, int depth);

static int midopt_method_readonly_p (c2m_ctx_t c2m_ctx, node_t func_def, int depth) {
  node_t block;

  if (func_def == NULL || func_def->code != N_FUNC_DEF || depth > 2) return 0;
  block = FUNC_DEF_BLOCK (func_def);
  if (block == NULL) return 0; /* extern / unknown body: not provable */

  for (node_t st = block; st != NULL;) {
    node_t list, s;
    if (st->code != N_BLOCK) break;
    list = NL_EL (st->u.ops, 1);
    if (list == NULL || list->code != N_LIST) break;
    for (s = NL_HEAD (list->u.ops); s != NULL; s = NL_NEXT (s)) {
      if (!midopt_stmt_no_this_write_p (c2m_ctx, s, depth)) return 0;
    }
    break;
  }
  return 1;
}

/* Expression-level this-write scan used by midopt_method_readonly_p.  Returns
   0 on any write/escape of `this`, its fields, or unknown method effects. */
static int midopt_expr_no_this_write_p (c2m_ctx_t c2m_ctx, node_t n, int depth) {
  node_t c;

  if (n == NULL) return 1;
  switch (n->code) {
  case N_ASSIGN: case N_ADD_ASSIGN: case N_SUB_ASSIGN: case N_MUL_ASSIGN:
  case N_DIV_ASSIGN: case N_MOD_ASSIGN: case N_LSH_ASSIGN: case N_RSH_ASSIGN:
  case N_AND_ASSIGN: case N_OR_ASSIGN: case N_XOR_ASSIGN: {
    node_t lhs = NL_HEAD (n->u.ops);
    /* Any write through `this` (field or deref) is a mutation. */
    if (lhs != NULL && (lhs->code == N_FIELD || lhs->code == N_DEREF_FIELD
                        || lhs->code == N_DEREF)) {
      node_t base = NL_HEAD (lhs->u.ops);
      if (base != NULL && base->code == N_ID && base->u.s.s != NULL
          && strcmp (base->u.s.s, "this") == 0)
        return 0;
    }
    break;
  }
  case N_INC: case N_DEC: case N_POST_INC: case N_POST_DEC: {
    node_t op = NL_HEAD (n->u.ops);
    if (op != NULL && (op->code == N_FIELD || op->code == N_DEREF_FIELD)) {
      node_t base = NL_HEAD (op->u.ops);
      if (base != NULL && base->code == N_ID && base->u.s.s != NULL
          && strcmp (base->u.s.s, "this") == 0)
        return 0;
    }
    break;
  }
  case N_ADDR: {
    node_t op = NL_HEAD (n->u.ops);
    /* &this / &this.field — the address escapes; writes become invisible. */
    if (op != NULL && op->code == N_ID && op->u.s.s != NULL
        && strcmp (op->u.s.s, "this") == 0)
      return 0;
    if (op != NULL && (op->code == N_FIELD || op->code == N_DEREF_FIELD)) {
      node_t base = NL_HEAD (op->u.ops);
      if (base != NULL && base->code == N_ID && base->u.s.s != NULL
          && strcmp (base->u.s.s, "this") == 0)
        return 0;
    }
    break;
  }
  case N_MOVE: case N_DELETE:
    return 0; /* conservative inside a read-only candidate */
  case N_CALL: {
    node_t fn = NL_HEAD (n->u.ops);
    if (fn != NULL && (fn->code == N_FIELD || fn->code == N_DEREF_FIELD)) {
      node_t base = NL_HEAD (fn->u.ops);
      struct expr *be = base != NULL ? base->attr : NULL;
      /* String builtin receivers (String fields of this) are pure. */
      if (be != NULL && be->type != NULL && builtin_string_type_p (be->type)) break;
      if (base != NULL && base->code == N_ID && base->u.s.s != NULL
          && strcmp (base->u.s.s, "this") == 0) {
        node_t args = NL_EL (n->u.ops, 1);
        int nargs = (args != NULL && args->code == N_LIST)
                      ? (int) NL_LENGTH (args->u.ops) - 1
                      : 0;
        node_t def = midopt_find_method_def (c2m_ctx, fn, nargs);
        if (def == NULL || !midopt_method_readonly_p (c2m_ctx, def, depth + 1))
          return 0;
      }
    } else if (fn != NULL && fn->code == N_ID) {
      /* Free function: `this` (or a field address) as an argument escapes. */
      node_t args = NL_EL (n->u.ops, 1);
      if (args != NULL && args->code == N_LIST) {
        node_t a;
        for (a = NL_HEAD (args->u.ops); a != NULL; a = NL_NEXT (a)) {
          if (a->code == N_ID && a->u.s.s != NULL && strcmp (a->u.s.s, "this") == 0)
            return 0;
          if (a->code == N_ADDR) {
            node_t op = NL_HEAD (a->u.ops);
            if (op != NULL && op->code == N_ID && op->u.s.s != NULL
                && strcmp (op->u.s.s, "this") == 0)
              return 0;
          }
        }
      }
    }
    break;
  }
  default:
    break;
  }

  if (!midopt_node_has_ops (n->code)) return 1;
  for (c = NL_HEAD (n->u.ops); c != NULL; c = NL_NEXT (c))
    if (!midopt_expr_no_this_write_p (c2m_ctx, c, depth)) return 0;
  return 1;
}

static int midopt_stmt_no_this_write_p (c2m_ctx_t c2m_ctx, node_t n, int depth) {
  if (n == NULL) return 1;
  if (n->code == N_BLOCK) {
    node_t list = NL_EL (n->u.ops, 1);
    if (list != NULL && list->code == N_LIST) {
      node_t s;
      for (s = NL_HEAD (list->u.ops); s != NULL; s = NL_NEXT (s))
        if (!midopt_stmt_no_this_write_p (c2m_ctx, s, depth)) return 0;
    }
    return 1;
  }
  if (n->code == N_SPEC_DECL) {
    node_t initializer = SPEC_DECL_INIT (n);
    decl_t dd = (decl_t) n->attr;
    if (initializer != NULL && initializer->code != N_IGNORE
        && !midopt_expr_no_this_write_p (c2m_ctx, initializer, depth))
      return 0;
    if (dd != NULL && dd->ctor_call != NULL
        && !midopt_expr_no_this_write_p (c2m_ctx, dd->ctor_call, depth))
      return 0;
    return 1;
  }
  /* Everything else (IF/loops/EXPR/RETURN/...) is an expression-shaped walk. */
  return midopt_expr_no_this_write_p (c2m_ctx, n, depth);
}

/* ── R2 for-in use-walk: does BODY use VAR/COLL safely for a by-ref bind? ──
   VAR_DECL is the element loop var's decl; COLL_DECL the collection's.
   Returns 1 when every use is a read. */

static int midopt_byref_use_ok (c2m_ctx_t c2m_ctx, node_t n, node_t var_decl,
                                node_t coll_decl, int depth);

/* Expression-level use-walk. */
static int midopt_byref_expr_ok (c2m_ctx_t c2m_ctx, node_t n, node_t var_decl,
                                 node_t coll_decl, int depth) {
  node_t c;

  if (n == NULL) return 1;
  switch (n->code) {
  case N_ASSIGN: case N_ADD_ASSIGN: case N_SUB_ASSIGN: case N_MUL_ASSIGN:
  case N_DIV_ASSIGN: case N_MOD_ASSIGN: case N_LSH_ASSIGN: case N_RSH_ASSIGN:
  case N_AND_ASSIGN: case N_OR_ASSIGN: case N_XOR_ASSIGN: {
    node_t lhs = NL_HEAD (n->u.ops);
    if (midopt_rooted_at (lhs, var_decl) || midopt_rooted_at (lhs, coll_decl))
      return 0; /* a write to the var (snapshot) or the collection (buffer) */
    break;
  }
  case N_INC: case N_DEC: case N_POST_INC: case N_POST_DEC: {
    if (midopt_rooted_at (NL_HEAD (n->u.ops), var_decl)) return 0;
    break;
  }
  case N_MOVE: case N_DELETE: {
    node_t op = NL_HEAD (n->u.ops);
    if (midopt_rooted_at (op, var_decl) || midopt_rooted_at (op, coll_decl))
      return 0;
    break;
  }
  case N_ADDR: {
    node_t op = NL_HEAD (n->u.ops);
    /* &var / &var.field / &coll — aliasing defeats the borrow. */
    if (midopt_rooted_at (op, var_decl) || midopt_rooted_at (op, coll_decl))
      return 0;
    break;
  }
  case N_CALL: {
    node_t fn = NL_HEAD (n->u.ops);
    node_t args = NL_EL (n->u.ops, 1);
    node_t a0 = (args != NULL && args->code == N_LIST) ? NL_HEAD (args->u.ops) : NULL;
    if (fn != NULL && (fn->code == N_FIELD || fn->code == N_DEREF_FIELD)) {
      node_t base = NL_HEAD (fn->u.ops);
      struct expr *be = base != NULL ? base->attr : NULL;
      int on_var = midopt_rooted_at (base, var_decl);
      int on_coll = midopt_rooted_at (base, coll_decl);
      if (on_var) {
        /* Method on the loop var: must be provably read-only.  String
           builtin receivers (String fields) are pure. */
        int is_str = (be != NULL && be->type != NULL && builtin_string_type_p (be->type));
        if (!is_str) {
          int nargs = (args != NULL && args->code == N_LIST)
                        ? (int) NL_LENGTH (args->u.ops) - 1
                        : 0;
          node_t def = midopt_find_method_def (c2m_ctx, fn, nargs);
          if (def == NULL || !midopt_method_readonly_p (c2m_ctx, def, 0)) return 0;
        }
      }
      if (on_coll) {
        /* Method on the collection: pure reads only (no buffer mutation
           or reallocation for the whole loop). */
        node_t mid = NL_NEXT (base);
        const char *nm = (mid != NULL && mid->code == N_ID) ? mid->u.s.s : NULL;
        if (!midopt_pure_coll_method_p (nm)) return 0;
      }
      /* Recurse into args, skipping the injected receiver (args[0]). */
      {
        node_t a;
        if (!midopt_byref_expr_ok (c2m_ctx, fn, var_decl, coll_decl, depth)) return 0;
        if (args != NULL && args->code == N_LIST) {
          for (a = a0 != NULL ? NL_NEXT (a0) : NULL; a != NULL; a = NL_NEXT (a))
            if (!midopt_byref_expr_ok (c2m_ctx, a, var_decl, coll_decl, depth)) return 0;
        }
        return 1;
      }
    }
    /* Free function (func N_ID): by-value copies of var/coll are fine;
       address/move escapes are caught by the N_ADDR / N_MOVE cases. */
    break;
  }
  default:
    break;
  }

  if (!midopt_node_has_ops (n->code)) return 1;
  for (c = NL_HEAD (n->u.ops); c != NULL; c = NL_NEXT (c))
    if (!midopt_byref_expr_ok (c2m_ctx, c, var_decl, coll_decl, depth)) return 0;
  return 1;
}

static int midopt_byref_use_ok (c2m_ctx_t c2m_ctx, node_t n, node_t var_decl,
                                node_t coll_decl, int depth) {
  if (n == NULL) return 1;
  if (n->code == N_BLOCK) {
    node_t list = NL_EL (n->u.ops, 1);
    if (list != NULL && list->code == N_LIST) {
      node_t s;
      for (s = NL_HEAD (list->u.ops); s != NULL; s = NL_NEXT (s))
        if (!midopt_byref_use_ok (c2m_ctx, s, var_decl, coll_decl, depth)) return 0;
    }
    return 1;
  }
  if (n->code == N_SPEC_DECL) {
    node_t initializer = SPEC_DECL_INIT (n);
    decl_t dd = (decl_t) n->attr;
    if (initializer != NULL && initializer->code != N_IGNORE
        && !midopt_byref_expr_ok (c2m_ctx, initializer, var_decl, coll_decl, depth))
      return 0;
    if (dd != NULL && dd->ctor_call != NULL
        && !midopt_byref_expr_ok (c2m_ctx, dd->ctor_call, var_decl, coll_decl, depth))
      return 0;
    return 1;
  }
  return midopt_byref_expr_ok (c2m_ctx, n, var_decl, coll_decl, depth);
}

/* Prove one N_FORIN for a by-ref element binding; stamp decl byref_p when
   proven.  Layout: labels(0), key_id(1), val_id(2), coll(3), body(4). */
static void midopt_byref_forin (c2m_ctx_t c2m_ctx, node_t n) {
  node_t labels, key_id, val_id, coll, body;
  struct expr *coll_e;
  struct type *cls, *el_t;
  node_t el_var, tag, coll_decl, var_spec;
  decl_t data_f, len_f, var_d;
  labels = NL_HEAD (n->u.ops);
  key_id = NL_NEXT (labels);
  val_id = NL_NEXT (key_id);
  coll = val_id != NULL ? NL_NEXT (val_id) : NULL;
  body = coll != NULL ? NL_NEXT (coll) : NULL;
  if (coll == NULL || body == NULL) {
    return;
  }

  /* Dense List/Set/Map: value receiver, `*p`, or a pointer local (`p`).
     Mutation is scanned on the named collection decl; aliased pointers that
     mutate through another name stay conservative (no stamp). */
  coll_e = coll->attr;
  if (coll_e == NULL || coll_e->type == NULL) return;
  cls = NULL;
  if (coll_e->type->mode == TM_CLASS)
    cls = coll_e->type;
  else if (coll_e->type->mode == TM_PTR && coll_e->type->u.ptr_type != NULL
           && coll_e->type->u.ptr_type->mode == TM_CLASS)
    cls = coll_e->type->u.ptr_type;
  if (cls == NULL || cls->u.tag_type == NULL) return;
  tag = cls->u.tag_type;

  /* Two dense layouts:
       List/Set (data+length): element var = single var, or two-var val
       Map (keys+vals+count):  only the two-var VALUE var is worth borrowing
                               (keys copy cheap — scalars and Strings). */
  el_var = NULL;
  el_t = NULL;
  if (find_dense_buffer_fields (tag, &data_f, &len_f, NULL)) {
    el_t = data_f->decl_spec.type != NULL ? data_f->decl_spec.type->u.ptr_type : NULL;
    el_var = (val_id != NULL && val_id->code == N_ID) ? val_id
             : (key_id != NULL && key_id->code == N_ID) ? key_id : NULL;
  } else {
    decl_t keys_f = find_class_field_by_name (tag, "keys");
    decl_t vals_f = find_class_field_by_name (tag, "vals");
    decl_t count_f = find_class_field_by_name (tag, "count");
    int dense_map = (count_f != NULL && keys_f != NULL && vals_f != NULL
                     && keys_f->decl_spec.type != NULL
                     && keys_f->decl_spec.type->mode == TM_PTR
                     && vals_f->decl_spec.type != NULL
                     && vals_f->decl_spec.type->mode == TM_PTR);
    if (dense_map) {
      el_t = vals_f->decl_spec.type->u.ptr_type;
      el_var = (val_id != NULL && val_id->code == N_ID) ? val_id : NULL;
    }
  }
  if (el_var == NULL || el_t == NULL) return;
  if (el_t->mode != TM_CLASS && el_t->mode != TM_STRUCT && el_t->mode != TM_UNION) {
    return; /* scalars/pointers are already cheap single loads */
  }

  /* Collection identity: `xs`, `p`, or `*p`. */
  {
    node_t coll_id = coll;
    if (coll->code == N_DEREF) {
      node_t inner = NL_HEAD (coll->u.ops);
      if (inner != NULL && inner->code == N_ID) coll_id = inner;
    }
    if (coll_id->code != N_ID) return;
    coll_decl = midopt_id_decl (coll_id);
    if (coll_decl == NULL) return;
  }

  /* The loop var N_IDs are declaration sites (never checked as expression
     uses), so they carry no u.lvalue_node — resolve via the symbol table
     like gen does. */
  {
    symbol_t vsym;
    if (!symbol_find (c2m_ctx, S_REGULARS, el_var, n, &vsym)
        || vsym.def_node == NULL || vsym.def_node->attr == NULL) {
      return;
    }
    var_spec = vsym.def_node;
    var_d = (decl_t) var_spec->attr;
  }

  if (!midopt_byref_use_ok (c2m_ctx, body, var_spec, coll_decl, 0)) {
    return;
  }

  var_d->byref_p = TRUE;
  if (midopt_verbose_p)
    fprintf (stderr, "  [midopt] byref for-in var %s\n",
             el_var->u.s.s != NULL ? el_var->u.s.s : "?");
}

static void midopt_byref_module (c2m_ctx_t c2m_ctx, node_t n) {
  if (n == NULL) return;
  if (n->code == N_FORIN) midopt_byref_forin (c2m_ctx, n);
  if (!midopt_node_has_ops (n->code)) return;
  for (node_t c = NL_HEAD (n->u.ops); c != NULL; c = NL_NEXT (c))
    midopt_byref_module (c2m_ctx, c);
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
    /* Always key by SPEC_DECL (stable). Expression N_ID uses resolve here
       via u.lvalue_node. */
    f = midopt_env_get (env, n);
    if (f != NULL) {
      nn = midopt_expr_nullness (env, initializer);
      if (nn != MN_TOP) f->nullness = nn;
      if (midopt_expr_ival (env, initializer, &lo, &hi)) {
        f->ival_p = 1;
        f->lo = lo;
        f->hi = hi;
      }
      /* `int n = recv.Count();` ties n to recv's length (R1 symbolic bound).
         The declarator's N_ID has no expr attr, so on_assign never sees it. */
      {
        mir_llong minus = 0;
        f->sym_recv = midopt_peel_count (env, initializer, &minus);
        f->sym_minus = f->sym_recv != NULL ? (int) minus : 0;
        if (midopt_expr_is_new (initializer)) f->uniq_ptr_p = 1;
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
    int k;

    midopt_safety_expr (c2m_ctx, cond, env);
    k = midopt_cond_known (cond);
    if (k == 1 && c11_dead_skippable_p (else_s)) {
      midopt_env_copy (&env_then, env);
      midopt_refine_cond (&env_then, cond, 1);
      midopt_safety_stmt (c2m_ctx, then_s, &env_then);
      midopt_env_copy (env, &env_then);
      return;
    }
    if (k == 0 && c11_dead_skippable_p (then_s)) {
      if (else_s != NULL && else_s->code != N_IGNORE) {
        midopt_env_copy (&env_else, env);
        midopt_refine_cond (&env_else, cond, 0);
        midopt_safety_stmt (c2m_ctx, else_s, &env_else);
        midopt_env_copy (env, &env_else);
      }
      return;
    }
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
  case N_FOR:
    /* Counted-loop IV analysis (R1): symbolic `i < recv.Count()` bounds and
       interval facts for the body; falls back to conservative scans inside. */
    midopt_safety_for (c2m_ctx, n, env);
    return;
  case N_WHILE:
    if (midopt_opt_level () >= 1) {
      midopt_safety_while (c2m_ctx, n, env);
      return;
    }
    /* -O0: fall through to conservative loop handling */
    /* FALLTHROUGH */
  case N_DO: case N_FORIN: case N_SWITCH: {
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
    /* Skip methods midopt will not emit — their traps never reach gen. */
    if (midopt_class_method_p (n) && midopt_decl_ready_p (n)
        && ((decl_t) n->attr)->midopt_dead_p) {
      /* fall through to children (none on FUNC_DEF that matter) */
    } else {
      midopt_safety_func (c2m_ctx, n);
    }
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

    if (type_flex_arr_p (arr_type)) {
      node_t abase = arr;
      while (abase != NULL && abase->code == N_CAST) abase = NL_EL (abase->u.ops, 1);
      if (abase == NULL || abase->code != N_FIELD) return;
    }
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

/* Private helpers implied by a *kept* public method.  Public APIs (Copy,
   Clear, owns*) are never implied — they must be reached from a real call.
   Needed because monomorph bodies sometimes lack def_node on this.Foo(). */
static const char *const midopt_hlp_grow[]
  = {"EnsureCapacity", "init_storage", NULL};
static const char *const midopt_hlp_map_write[]
  = {"ensure_table", "find_slot", "insert_new_at", "grow_table", "init_storage",
     "destroy_val_at", NULL};
static const char *const midopt_hlp_map_read[] = {"find_slot", NULL};
static const char *const midopt_hlp_map_remove[]
  = {"find_slot", "find_index", "destroy_key_at", "destroy_val_at", NULL};
static const char *const midopt_hlp_dtor[]
  = {"destroy_key_at", "destroy_val_at", NULL};

static int midopt_streq_any (const char *nm, const char *const *names) {
  size_t i;
  if (nm == NULL) return 0;
  for (i = 0; names[i] != NULL; i++)
    if (strcmp (nm, names[i]) == 0) return 1;
  return 0;
}

static const char *const *midopt_implied_helpers (const char *nm) {
  static const char *const grow_callers[]
    = {"Add", "Insert", "EnsureCapacity", "Concat", "AddRange", "InsertRange",
       "Plus", "Slice", "Take", "Skip", "Repeat", "Range", "FromView", "FromJson",
       "Copy", "Distinct", "Filter", "Where", "Map", "Select", "SelectString", NULL};
  static const char *const map_write_callers[]
    = {"Set", "TryAdd", "GetOrAdd", "AddOrUpdate", "Merge", "insert_new_at",
       "GroupBy", "WhereKeys", "WhereValues", "SelectValues", "SelectKeys", NULL};
  static const char *const map_read_callers[]
    = {"Get", "GetOr", "TryGet", "Contains", "ContainsKey", "ContainsValue",
       "GetMut", "ValMut", "KeyAt", "ValAt", NULL};
  static const char *const map_remove_callers[] = {"Remove", "Clear", NULL};

  if (nm == NULL) return NULL;
  if (strncmp (nm, "__dtor_", 7) == 0) return midopt_hlp_dtor;
  if (strncmp (nm, "__ctor_", 7) == 0) return midopt_hlp_grow; /* Map ctors → init_storage */
  if (midopt_streq_any (nm, grow_callers)) return midopt_hlp_grow;
  if (midopt_streq_any (nm, map_write_callers)) return midopt_hlp_map_write;
  if (midopt_streq_any (nm, map_read_callers)) return midopt_hlp_map_read;
  if (midopt_streq_any (nm, map_remove_callers)) return midopt_hlp_map_remove;
  return NULL;
}

static void midopt_keep_implied_helpers (c2m_ctx_t c2m_ctx, node_t class_tag, const char *nm,
                                        pos_t pos) {
  const char *const *hs = midopt_implied_helpers (nm);
  size_t i;
  if (hs == NULL) return;
  for (i = 0; hs[i] != NULL; i++)
    midopt_mark_named_on_class (c2m_ctx, class_tag, hs[i], pos);
}

/* RAII / delete often miss def_node on the destructor.  Extra ctor overloads
   are reached via ctor_call / N_NEW / the worklist — do not pin all of them. */
static void midopt_keep_dtors_on_class (node_t class_tag) {
  node_t id, decl_list;
  if (class_tag == NULL || class_tag->code != N_CLASS) return;
  id = NL_HEAD (class_tag->u.ops);
  decl_list = id != NULL ? NL_NEXT (id) : NULL;
  if (decl_list == NULL || decl_list->code != N_LIST) return;
  for (node_t m = NL_HEAD (decl_list->u.ops); m != NULL; m = NL_NEXT (m)) {
    const char *nm;
    if (m->code != N_FUNC_DEF || !midopt_class_method_p (m)) continue;
    nm = midopt_func_name (m);
    if (nm != NULL && strncmp (nm, "__dtor_", 7) == 0) midopt_mark_keep (m);
  }
}

static void midopt_keep_helpers_for_live (c2m_ctx_t c2m_ctx, node_t n) {
  if (n == NULL) return;

  if (n->code == N_CLASS && n->attr != (void *) ((intptr_t) -1)) {
    node_t id = NL_HEAD (n->u.ops);
    node_t decl_list = id != NULL ? NL_NEXT (id) : NULL;
    pos_t pos = id != NULL ? POS (id) : no_pos;
    int any_keep = 0;

    if (decl_list != NULL && decl_list->code == N_LIST) {
      for (node_t m = NL_HEAD (decl_list->u.ops); m != NULL; m = NL_NEXT (m)) {
        const char *nm;
        if (m->code != N_FUNC_DEF || !midopt_keep_has (m)) continue;
        any_keep = 1;
        nm = midopt_func_name (m);
        midopt_keep_implied_helpers (c2m_ctx, n, nm, pos);
      }
      if (any_keep) midopt_keep_dtors_on_class (n);
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

  if (n->code == N_FUNC_DEF && midopt_decl_ready_p (n)
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
/* Does subtree N contain a call?  Keeps auto-inline candidates tiny and dodges
   the pointer-into-`this` chaining hazard (a return that flows through a call). */
static int midopt_expr_has_call_p (node_t n) {
  node_t c;
  if (n == NULL) return 0;
  if (n->code == N_CALL) return 1;
  if (!midopt_node_has_ops (n->code)) return 0;
  for (c = NL_HEAD (n->u.ops); c != NULL; c = NL_NEXT (c))
    if (midopt_expr_has_call_p (c)) return 1;
  return 0;
}

/* True when FUNC_DEF is a trivial getter safe to mark MIR_INLINE: its body is
   exactly one `return <arithmetic/enum expr>;` with no nested calls and no
   writes to `this`.  Requiring an arithmetic (incl. enum) return type excludes
   pointer / String / aggregate returns — i.e. the Get/GetMut pointer-into-`this`
   case that miscompiles chaining (see midopt_run step 4). */
static int midopt_trivial_scalar_getter_p (c2m_ctx_t c2m_ctx, node_t func_def);

static int midopt_ptr_into_this_ret_p (node_t func_def) {
  const char *nm;
  decl_t d;
  struct func_type *ft;
  struct type *ret;

  nm = midopt_func_name (func_def);
  if (nm != NULL
      && (strcmp (nm, "Get") == 0 || strcmp (nm, "GetMut") == 0 || strcmp (nm, "FirstMut") == 0
          || strcmp (nm, "LastMut") == 0 || strcmp (nm, "ValMut") == 0 || strcmp (nm, "KeyAt") == 0
          || strcmp (nm, "ValAt") == 0))
    return 1;
  if (!midopt_decl_ready_p (func_def)) return 0;
  d = (decl_t) func_def->attr;
  if (d == NULL || d->decl_spec.type == NULL || d->decl_spec.type->mode != TM_FUNC) return 0;
  ft = d->decl_spec.type->u.func_type;
  if (ft == NULL || ft->ret_type == NULL) return 0;
  ret = ft->ret_type;
  return ret->mode == TM_PTR && ret->u.ptr_type != NULL
         && (ret->u.ptr_type->mode == TM_CLASS || ret->u.ptr_type->mode == TM_STRUCT);
}

static void midopt_metrics_walk (node_t n, int *nst, int *ncall, int *hard) {
  if (n == NULL) return;
  switch (n->code) {
  case N_WHILE: case N_DO: case N_FOR: case N_FORIN: case N_SWITCH: case N_TRY:
    *hard = 1;
    break;
  case N_CALL: (*ncall)++; break;
  case N_RETURN: case N_EXPR: case N_IF: (*nst)++; break;
  default: break;
  }
  if (!midopt_node_has_ops (n->code)) return;
  for (node_t c = NL_HEAD (n->u.ops); c != NULL; c = NL_NEXT (c))
    midopt_metrics_walk (c, nst, ncall, hard);
}

static int midopt_should_inline_p (c2m_ctx_t c2m_ctx, node_t func_def) {
  const char *nm;
  decl_t d;
  struct func_type *ft;
  struct type *ret;
  int lvl, nst = 0, ncall = 0, hard = 0, budget, call_ok;

  nm = midopt_func_name (func_def);
  if (nm == NULL) return 0;
  if (strcmp (nm, "Count") == 0 || strcmp (nm, "IsEmpty") == 0 || strcmp (nm, "Capacity") == 0)
    return 1;
  if (midopt_ptr_into_this_ret_p (func_def)) return 0;
  lvl = midopt_opt_level ();
  if (lvl <= 0) return 0;
  if (lvl == 1) return midopt_trivial_scalar_getter_p (c2m_ctx, func_def);

  if (!midopt_decl_ready_p (func_def)) return 0;
  d = (decl_t) func_def->attr;
  if (d == NULL || d->decl_spec.type == NULL || d->decl_spec.type->mode != TM_FUNC) return 0;
  ft = d->decl_spec.type->u.func_type;
  if (ft == NULL || ft->ret_type == NULL) return 0;
  ret = ft->ret_type;
  if (!void_type_p (ret) && !arithmetic_type_p (ret)) return 0;

  midopt_metrics_walk (FUNC_DEF_BLOCK (func_def), &nst, &ncall, &hard);
  if (hard) return 0;
  budget = (lvl >= 3) ? 8 : 3;
  call_ok = (lvl >= 3) ? 2 : 0;
  if (nst > budget || ncall > call_ok) return 0;
  if (lvl < 3 && !midopt_method_readonly_p (c2m_ctx, func_def, 0)) return 0;
  return 1;
}

static int midopt_trivial_scalar_getter_p (c2m_ctx_t c2m_ctx, node_t func_def) {
  decl_t d;
  struct func_type *ft;
  struct type *ret;
  node_t block, list, s = NULL, expr;
  int n_stmt = 0;

  if (func_def == NULL || func_def->code != N_FUNC_DEF) return 0;
  if (!midopt_decl_ready_p (func_def)) return 0;
  d = (decl_t) func_def->attr;
  if (d == NULL || d->decl_spec.type == NULL || d->decl_spec.type->mode != TM_FUNC) return 0;
  ft = d->decl_spec.type->u.func_type;
  if (ft == NULL || ft->ret_type == NULL) return 0;
  ret = ft->ret_type;
  if (!arithmetic_type_p (ret)) return 0; /* excludes ptr/String/struct/class */

  block = FUNC_DEF_BLOCK (func_def);
  if (block == NULL || block->code != N_BLOCK) return 0;
  list = NL_EL (block->u.ops, 1);
  if (list == NULL || list->code != N_LIST) return 0;
  for (node_t st = NL_HEAD (list->u.ops); st != NULL; st = NL_NEXT (st)) {
    if (++n_stmt > 1) return 0; /* more than a single statement */
    s = st;
  }
  if (n_stmt != 1 || s == NULL || s->code != N_RETURN) return 0;
  expr = NL_EL (s->u.ops, 1); /* N_RETURN(N_LIST labels, expr?) */
  if (expr == NULL || expr->code == N_IGNORE) return 0; /* bare `return;` */
  if (midopt_expr_has_call_p (expr)) return 0;
  if (!midopt_method_readonly_p (c2m_ctx, func_def, 0)) return 0;
  return 1;
}

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
  midopt_level = midopt_latch_opt_level (c2m_ctx);
  VARR_CREATE (node_t, midopt_keep, alloc, 64);
  VARR_CREATE (node_t, midopt_work, alloc, 64);

  if (midopt_verbose_p)
    fprintf (stderr, "  [midopt] start (P0/P1 + IV/inline at -O%d)\n", midopt_level);

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
    midopt_byref_module (c2m_ctx, module);
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
      if (midopt_should_inline_p (c2m_ctx, f)) {
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

  /* 6) R2: prove for-in loop vars borrowable (read-only + unmutated dense
     collection) and stamp them for by-reference binding in gen. */
  midopt_byref_module (c2m_ctx, module);

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
