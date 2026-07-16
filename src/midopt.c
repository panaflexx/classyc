/* src/midopt.c — Mid-level optimizer (check → gen) for ClassyC.
 *
 * STATUS: Phase B of GEN-OPT.md — P0 dead class-method pruning + P1
 * static OOB / null-guard elision stamps.
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
 * Goals (Phase B):
 *   P0 — Mark unreachable *class methods* `midopt_dead_p` so gen skips
 *        bodies and MIR forwards. Free functions always stay (C linkage).
 *   P1 — Stamp `elide_oob_p` / expand `own_deref_class = SAFE` where
 *        trivial static facts make safety traps redundant.
 *
 * Flags:
 *   default on; `-fno-midopt` skips; `-v` prints keep/dead counts.
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

  if (n == NULL || !midopt_expr_node_p (n->code)) return;
  if (n->attr == NULL || n->attr == (void *) ((intptr_t) -1)) return;
  e = (struct expr *) n->attr;
  if (e->def_node != NULL && e->def_node->code == N_FUNC_DEF)
    midopt_mark_keep (e->def_node);
}

/* ── AST walk: collect uses / apply elisions ─────────────────────────────── */

static void midopt_collect_uses (c2m_ctx_t c2m_ctx, node_t n);
static void midopt_elide_walk (c2m_ctx_t c2m_ctx, node_t n);

static void midopt_collect_uses (c2m_ctx_t c2m_ctx, node_t n) {
  if (n == NULL) return;

  /* Method references stashed on expression nodes only (never scopes/decls). */
  midopt_mark_from_expr_node (n);

  /* Stack RAII ctor/dtor calls on locals. */
  if (n->code == N_SPEC_DECL && n->attr != NULL
      && n->attr != (void *) ((intptr_t) -1)) {
    decl_t d = (decl_t) n->attr;
    if (d->ctor_call != NULL) midopt_collect_uses (c2m_ctx, d->ctor_call);
    if (d->dtor_call != NULL) midopt_collect_uses (c2m_ctx, d->dtor_call);
    if (d->auto_release_call != NULL)
      midopt_collect_uses (c2m_ctx, d->auto_release_call);
  }

  /* for-in over classes uses Count/Get (or KeyAt/ValAt) resolved only at gen
     time — stamp them here so midopt does not prune the protocol. */
  if (n->code == N_FORIN) {
    node_t coll = NL_EL (n->u.ops, 3);
    struct expr *ce;
    struct type *ct, *cls;
    node_t tag, m;

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

  if (!midopt_node_has_ops (n->code)) return;
  for (node_t c = NL_HEAD (n->u.ops); c != NULL; c = NL_NEXT (c))
    midopt_collect_uses (c2m_ctx, c);
}

/* Walk only a function body (for worklist propagation). */
static void midopt_collect_uses_body (c2m_ctx_t c2m_ctx, node_t func_def) {
  node_t block;

  if (func_def == NULL || func_def->code != N_FUNC_DEF) return;
  block = FUNC_DEF_BLOCK (func_def);
  midopt_collect_uses (c2m_ctx, block);
  /* Also scan param list / decls list for odd attrs. */
  midopt_collect_uses (c2m_ctx, FUNC_DEF_DECLS (func_def));
}

/* P1: static OOB elision for arr[i] when i and length are compile-time constants. */
static void midopt_try_elide_oob (node_t n) {
  node_t arr, idx;
  struct expr *e, *ie, *ae;
  struct type *arr_type, *el_type MIR_UNUSED;

  if (n == NULL || n->code != N_IND || n->attr == NULL) return;
  e = (struct expr *) n->attr;
  if (e->type == NULL) return;
  arr = NL_HEAD (n->u.ops);
  idx = NL_NEXT (arr);
  if (arr == NULL || idx == NULL || idx->attr == NULL) return;
  ie = (struct expr *) idx->attr;
  if (!ie->const_p) return;
  ae = arr->attr;
  if (ae == NULL || ae->type == NULL) return;
  arr_type = ae->type;
  /* Decayed array: arr_type is PTR with arr_type pointing at original TM_ARR. */
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
      /* Indexing a true array base is never a null load of a pointer. */
      if (e->own_deref_class == DEREF_GUARD_DEFAULT)
        e->own_deref_class = DEREF_GUARD_SAFE;
    }
  }
}

/* P1: `&obj` / stack address bases, and `this`, already handled in gen for
   null; strengthen own_deref_class on N_DEREF / N_DEREF_FIELD when receiver is
   `this` or ADDR of a local (conservative: only `this`). */
static void midopt_try_elide_null (node_t n) {
  struct expr *e;
  node_t recv;

  if (n == NULL || n->attr == NULL) return;
  if (n->code != N_DEREF && n->code != N_DEREF_FIELD && n->code != N_IND) return;
  e = (struct expr *) n->attr;
  if (e->type == NULL) return;
  if (e->own_deref_class == DEREF_GUARD_SAFE) return;
  recv = NL_HEAD (n->u.ops);
  if (recv != NULL && recv->code == N_ID && recv->u.s.s != NULL
      && strcmp (recv->u.s.s, "this") == 0)
    e->own_deref_class = DEREF_GUARD_SAFE;
}

static void midopt_elide_walk (c2m_ctx_t c2m_ctx, node_t n) {
  if (n == NULL) return;
  midopt_try_elide_oob (n);
  midopt_try_elide_null (n);
  if (!midopt_node_has_ops (n->code)) return;
  for (node_t c = NL_HEAD (n->u.ops); c != NULL; c = NL_NEXT (c))
    midopt_elide_walk (c2m_ctx, c);
}

/* Class-level liveness: if any method of a class is kept, keep *all* methods
   of that class.  Intra-class calls (this->EnsureCapacity from Add, etc.) are
   not always stamped with def_node/used_p on every path; pruning individual
   methods leaves MIR_CALL refs with null items.  Whole-class keep is still a
   large win vs emitting every monomorph of every unused List/Map/Set. */
static void midopt_expand_class_keeps (c2m_ctx_t c2m_ctx, node_t n) {
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
        for (node_t m = NL_HEAD (decl_list->u.ops); m != NULL; m = NL_NEXT (m)) {
          if (m->code == N_FUNC_DEF && midopt_class_method_p (m))
            midopt_mark_keep (m);
        }
      }
    }
  }

  if (!midopt_node_has_ops (n->code)) return;
  for (node_t c = NL_HEAD (n->u.ops); c != NULL; c = NL_NEXT (c))
    midopt_expand_class_keeps (c2m_ctx, c);
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
    /* Always "live": scan body for method refs. */
    midopt_collect_uses_body (c2m_ctx, n);
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

  /* 1) Seed ONLY from free functions (and nested decls/ctors in them).
     Do not walk monomorph method bodies yet — that would mark every
     intra-class call as a root and pin the whole specialization. */
  midopt_seed_from_free_funcs (c2m_ctx, module);

  /* 2) Fixpoint: scan newly kept method bodies for more method refs. */
  wi = 0;
  while (wi < VARR_LENGTH (node_t, midopt_work)) {
    node_t f = VARR_GET (node_t, midopt_work, wi++);
    midopt_collect_uses_body (c2m_ctx, f);
  }

  /* 3) Class-level expand once for monomorphs with ≥1 kept method. */
  if (midopt_verbose_p)
    fprintf (stderr, "  [midopt] before class-expand kept=%lu\n",
             (unsigned long) VARR_LENGTH (node_t, midopt_keep));
  midopt_expand_class_keeps (c2m_ctx, module);

  /* 3b) One worklist drain over expanded methods: mark any *direct* callees
     (including other monomorphs) keep, but do not re-expand those monomorphs
     wholesale — residual AST edges get a MIR warning in gen if still missing. */
  wi = 0;
  while (wi < VARR_LENGTH (node_t, midopt_work)) {
    node_t f = VARR_GET (node_t, midopt_work, wi++);
    midopt_collect_uses_body (c2m_ctx, f);
  }

  /* 4) Safety net: zero keeps → do not prune (P1 elision still runs). */
  if (VARR_LENGTH (node_t, midopt_keep) == 0) {
    if (midopt_verbose_p)
      fprintf (stderr, "  [midopt] no method keeps found — skip dead pruning\n");
    midopt_elide_walk (c2m_ctx, module);
    if (c2m_options != NULL && c2m_options->verbose_p) {
      FILE *f = c2m_options->message_file != NULL ? c2m_options->message_file : stderr;
      fprintf (f, "  [midopt] class methods=unknown kept=0 dead=0 (prune skipped)\n");
    }
    VARR_DESTROY (node_t, midopt_keep);
    VARR_DESTROY (node_t, midopt_work);
    midopt_keep = NULL;
    midopt_work = NULL;
    return;
  }

  midopt_mark_dead_methods (c2m_ctx, module, &n_methods, &n_dead);

  /* 5) P1 safety elision stamps. */
  midopt_elide_walk (c2m_ctx, module);

  if (midopt_verbose_p || (c2m_options != NULL && c2m_options->verbose_p)) {
    FILE *f = c2m_options != NULL && c2m_options->message_file != NULL
                ? c2m_options->message_file
                : stderr;
    fprintf (f, "  [midopt] class methods=%d kept=%lu dead=%d\n", n_methods,
             (unsigned long) VARR_LENGTH (node_t, midopt_keep), n_dead);
  }

  VARR_DESTROY (node_t, midopt_keep);
  VARR_DESTROY (node_t, midopt_work);
  midopt_keep = NULL;
  midopt_work = NULL;
}
