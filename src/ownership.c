/* src/ownership.c — Ownership / lifetime analysis pass for ClassyC.
 *
 * STATUS: MINIMAL FIRST STAGE.  The pass now runs between check and gen, but
 * does no rewriting or diagnosis yet.  It walks the post-check AST, finds
 * every declaration the check pass marked `auto_defer_p = TRUE`, and \u2014 when
 * the user passes `-v`/`--verbose` \u2014 prints one line per candidate.  Returns
 * zero (no errors) unconditionally.
 *
 * Single-TU build model: this file is `#include`d from `src/classyc.c` near
 * the end, so it sees the full set of internal types (`c2m_ctx_t`, `node_t`,
 * `decl_t`, `struct decl`, the `N_*` enum, traversal macros, `c2m_options`,
 * etc.).  It MUST NOT be added to `CMakeLists.txt` as an independent source.
 *
 * ──────────────────────────────────────────────────────────────────────
 * Why a separate pass?
 *
 * The check pass needs to be reentrant and produce a fully-typed AST for
 * every well-formed program.  Ownership is a different concern: it answers
 * "for each acquired resource, is it released exactly once on every path?"
 * That's a flow-sensitive question \u2014 it inherently needs to walk the CFG of
 * each function and propagate per-binding state through joins.  Folding it
 * into check would either spread that logic across many `case N_*` arms or
 * force every check arm to carry a synthetic CFG.  Neither scales.
 *
 * Keeping ownership in its own pass also lets it be:
 *   - Optional: a `-fno-ownership` flag (or just not running the pass)
 *     keeps the language usable on weird code without forcing migration.
 *   - Replaceable: alternative analyses (a borrow checker, an escape
 *     analysis, a leak sanitizer) can live alongside or replace this one.
 *   - Standalone: callable from external tools (`classyc --analyze foo.cy`).
 *
 * Pipeline position:
 *     parse  →  check  →  [ownership]  →  gen  →  MIR
 *                          ^^^^^^^^^^^
 *                          this file
 *
 * After check: all names resolved, types known, generics specialised,
 * keywords (`defer`, `detach`, `attach`, `unowned`, `new`, `delete`) have
 * become concrete N_* nodes with valid attrs.  Before gen: nothing has been
 * lowered to MIR yet; we can still synthesize / reject without rollback.
 *
 * ──────────────────────────────────────────────────────────────────────
 * The state machine (planned).
 *
 * For each candidate local (decl->auto_defer_p TRUE, or any binding the
 * pass elects to track), we maintain one of these states per program point:
 *
 *      Unowned     \u2014 the binding holds a value but does not own it; no
 *                    release responsibility.  Initial state for `unowned`
 *                    locals and for borrowed parameters.
 *      Owned       \u2014 the binding holds a resource it is responsible for
 *                    releasing exactly once before going out of scope.
 *                    Initial state after `new T(...)`, `malloc(...)`,
 *                    `fopen(...)`, or any future acquire pattern.
 *      Detached    \u2014 the binding's value was extracted via `detach <expr>`
 *                    and ownership transferred elsewhere.  Reading the
 *                    binding is allowed (the pointer is still there) but
 *                    deleting / releasing it via this binding is an error.
 *      Released    \u2014 the resource has been explicitly released
 *                    (`delete x`, `free(x)`, `fclose(x)`, …).  Any further
 *                    use is UAF.
 *      MaybeOwned  \u2014 join-point lattice meet of Owned and {Detached or
 *                    Released}.  Reading the binding is allowed only if a
 *                    null check guards it; release attempts are an error;
 *                    end-of-scope is an error (potential leak on one path).
 *
 * Transitions:
 *
 *      acquire(x)      *           → Owned        e.g. x = new T(...)
 *      attach(x)       Unowned     → Owned        explicit adopt
 *      attach(x)       Owned       → ERROR(double)
 *      detach(x)       Owned       → Detached     escape ownership out
 *      detach(x)       Unowned     → Unowned      (warn: no-op)
 *      release(x)      Owned       → Released     explicit delete/free
 *      release(x)      Detached    → ERROR(uaf)
 *      release(x)      Released    → ERROR(double)
 *      use(x)          Released    → ERROR(uaf)
 *      use(x)          *           → unchanged
 *      return v        if v Owned  → transfer to caller (caller's responsibility)
 *      scope-end       Owned       → ERROR(leak)  unless an inserted defer catches it
 *      scope-end       Detached    → OK
 *      scope-end       Released    → OK
 *      scope-end       Unowned     → OK
 *
 * Join rule (meet) at CFG merges:
 *      meet(s, s)             = s
 *      meet(Owned, Released)  = MaybeOwned
 *      meet(Owned, Detached)  = MaybeOwned
 *      meet(MaybeOwned, *)    = MaybeOwned
 *      meet(Unowned, Owned)   = Owned
 *
 * Synthesis policy: insert a synthetic `defer delete x;` at the head of the
 * binding's block when the join-state at every reachable scope-exit is
 * Owned.  If it's MaybeOwned, emit `-Wleak-candidate` and stay out of the
 * user's way.
 *
 * ──────────────────────────────────────────────────────────────────────
 * Resource-pair table — making it work for plain C11.
 *
 * The acquire/release vocabulary will live in a flat table the pass
 * consults.  Adding a new pair is a one-line change.  This is what turns
 * the pass into a general "RAII for C" feature, not just a sweetener for
 * `new`/`delete`.
 *
 * Seed entries (to be implemented):
 *
 *     { "malloc",   "free"   }    { "calloc",  "free"   }
 *     { "realloc",  "free"   }    { "strdup",  "free"   }
 *     { "strndup",  "free"   }    { "fopen",   "fclose" }
 *     { "tmpfile",  "fclose" }    { "fdopen",  "fclose" }
 *     { "popen",    "pclose" }    { "opendir", "closedir" }
 *     { "mmap",     "munmap" }    { "dlopen",  "dlclose" }
 *     { "open",     "close"  }    { "socket",  "close"  }
 *
 * User extension: `__attribute__((acquires(release_fn)))` on a function
 * declaration registers a pair on the fly.  Also recognise GCC's existing
 * `__attribute__((cleanup(fn)))` as a per-variable RP_RELEASE_VIA_ATTR
 * marker that suppresses synthesis.
 *
 * ──────────────────────────────────────────────────────────────────────
 * Implementation order (suggested, smallest viable steps):
 *
 *   1. [DONE] Mark candidates in check (decl->auto_defer_p).
 *   2. [DONE] Wire pass as a stage between check and gen.
 *   3. Build per-function CFG over N_BLOCK / N_FOR / N_WHILE / N_IF /
 *      N_BREAK / N_CONTINUE / N_RETURN / N_TRY / N_CATCH.
 *   4. Define transfer functions for the statements/expressions that mention
 *      a tracked binding (N_DELETE, N_DETACH, N_ATTACH, N_RETURN, field
 *      stores, N_CALL passing the binding to a known release, etc.).
 *   5. Iterate the dataflow to fixpoint with the lattice above.
 *   6. Read off synthesis decisions and diagnostics.
 *   7. Add the resource-pair table and `__attribute__((acquires))` /
 *      `__attribute__((cleanup))` recognisers.
 *   8. Add `-fownership` / `-fno-ownership` driver flag.
 *
 * Validation suite to grow alongside the pass: `cy-validate/val-020-..` etc.
 * ────────────────────────────────────────────────────────────────────── */

/* ────────────────────────────────────────────────────────────────────────
 * Planned data types — defined now to anchor the public shape, even though
 * only the entry point uses them in this first stage.
 * ──────────────────────────────────────────────────────────────────────── */

/* Per-binding state in the lattice. */
typedef enum {
  OWN_UNOWNED,
  OWN_OWNED,
  OWN_DETACHED,
  OWN_RELEASED,
  OWN_MAYBE_OWNED,
} ownership_state_t;

/* Flags on a resource-pair entry. */
enum {
  RP_NULLABLE         = 1u << 0, /* acquire may return NULL on failure */
  RP_ERROR_AS_VALUE   = 1u << 1, /* acquire returns a sentinel (-1, MAP_FAILED) on err */
  RP_RELEASE_VIA_ATTR = 1u << 2, /* release is via __attribute__((cleanup(...))) */
};

typedef struct ownership_resource_pair {
  const char *acquire_fn;
  const char *release_fn;
  unsigned    flags;
} ownership_resource_pair_t;

/* ────────────────────────────────────────────────────────────────────────
 * First-stage implementation: walk the AST, count auto-defer candidates,
 * and (in verbose mode) print one line per candidate so the rest of the
 * pipeline can be observed without committing to any rewrites yet.
 * ──────────────────────────────────────────────────────────────────────── */

/* Extract the bound identifier's source string from a SPEC_DECL node, or
 * NULL if the declarator isn't a simple N_DECL with an N_ID.  Mirrors the
 * convention used by `dbinfo_walk_stmt`. */
static const char *ownership_spec_decl_name (node_t spec_decl) {
  node_t declr;
  node_t id;

  if (spec_decl == NULL || spec_decl->code != N_SPEC_DECL) return NULL;
  declr = NL_EL (spec_decl->u.ops, 1);
  if (declr == NULL || declr->code != N_DECL) return NULL;
  id = NL_HEAD (declr->u.ops);
  if (id == NULL || id->code != N_ID) return NULL;
  return id->u.s.s;
}

/* ────────────────────────────────────────────────────────────────────────
 * Resource pair table (malloc-family slice for v1).
 *
 * Keep in lockstep with `is_resource_acquire` in src/classyc.c — both will
 * be unified into one shared table once we add fopen/fclose, mmap/munmap,
 * open/close, and the rest of the planned seed list documented at the top
 * of this file.
 * ──────────────────────────────────────────────────────────────────────── */

/* Return the name of the release function that matches an acquire function
 * name, or NULL if `acquire` is not one we know about. */
static const char *release_fn_for_acquire (const char *acquire) {
  if (acquire == NULL) return NULL;
  if (strcmp (acquire, "malloc")  == 0) return "free";
  if (strcmp (acquire, "calloc")  == 0) return "free";
  if (strcmp (acquire, "realloc") == 0) return "free";
  if (strcmp (acquire, "strdup")  == 0) return "free";
  if (strcmp (acquire, "strndup") == 0) return "free";
  return NULL;
}

/* If `init` is a C-runtime acquire call (e.g. `malloc(n)` or the cast form
 * `(char *)malloc(n)`), return the callee identifier (`"malloc"`); else
 * NULL.  Single-call only — chained results like `wrap(malloc(n))` are
 * intentionally ignored for v1.  Mirrors the cast-peeling in classyc.c's
 * `is_resource_acquire` so the two stay in agreement on what counts as an
 * acquire. */
static const char *acquire_fn_name_of_init (node_t init) {
  node_t callee;
  /* Peel leading C casts — `(char *)malloc(n)` is the universal idiom. */
  while (init != NULL && init->code == N_CAST)
    init = NL_EL (init->u.ops, 1);
  if (init == NULL || init->code != N_CALL) return NULL;
  callee = NL_HEAD (init->u.ops);
  if (callee == NULL || callee->code != N_ID || callee->u.s.s == NULL) return NULL;
  return callee->u.s.s;
}

/* TRUE when this node's `u` union actively holds the operand DLIST (i.e.
 * we may walk its children); FALSE for scalar / string leaf nodes whose
 * constructors overwrite the union with `u.l`, `u.s`, `u.d`, etc.  Trying
 * to read `u.ops` on those is undefined and crashes immediately.
 *
 * Keeping the blacklist tight and explicit (rather than a permissive
 * walker that catches segfaults) makes it obvious which AST shapes the
 * ownership pass will eventually need transfer-function support for. */
static int ownership_node_has_ops (node_code_t code) {
  switch (code) {
  /* Integer / floating constants — store value in u.l / u.ll / u.f / u.d. */
  case N_I: case N_L: case N_LL: case N_U: case N_UL: case N_ULL:
  case N_F: case N_D: case N_LD:
  /* Character constants — u.ch / u.ull. */
  case N_CH: case N_CH16: case N_CH32:
  /* String constants and identifiers — u.s. */
  case N_STR: case N_STR16: case N_STR32: case N_ID:
  /* Placeholder / synthesised stand-ins with no children. */
  case N_IGNORE:
    return 0;
  default:
    return 1;
  }
}

/* TRUE iff the subtree rooted at `n` contains an N_ID node whose name
 * matches `name`.  Pure read; never mutates.  Used by the leak-check
 * scanner to decide whether an expression "mentions" the candidate local. */
static int subtree_mentions_id (node_t n, const char *name) {
  if (n == NULL) return 0;
  if (n->code == N_ID && n->u.s.s != NULL && strcmp (n->u.s.s, name) == 0)
    return 1;
  if (!ownership_node_has_ops (n->code)) return 0;
  for (node_t c = NL_HEAD (n->u.ops); c != NULL; c = NL_NEXT (c))
    if (subtree_mentions_id (c, name)) return 1;
  return 0;
}

/* Per-candidate usage classification accumulated by `scan_uses`.
 *   released_p — the recognised release call appears with `name` as an arg
 *   escaped_p  — `name` is returned, stored elsewhere, address-taken, or
 *                 passed to a function other than the release (treated
 *                 conservatively as "caller may now own it") */
typedef struct {
  int released_p;
  int escaped_p;
} usage_summary_t;

/* Walk the function body and classify every use of `name`.  Conservative:
 * an unknown function call carrying `name` is treated as an escape — we'd
 * rather miss a real leak than complain about working code that hands the
 * pointer to a custom destroyer like `mylib_release(p)`.  When the resource
 * pair table grows to cover such helpers, those calls will be reclassified
 * as proper `released_p` evidence. */
static void scan_uses (node_t n, const char *name, const char *release_fn,
                       usage_summary_t *out) {
  if (n == NULL) return;

  switch (n->code) {
  case N_RETURN: {
    /* N_RETURN has labels (op 0) and the return-expression (op 1). */
    node_t expr = NL_EL (n->u.ops, 1);
    if (expr != NULL && expr->code != N_IGNORE
        && subtree_mentions_id (expr, name))
      out->escaped_p = 1;
    break;
  }
  case N_CALL: {
    /* N_CALL: (callee, N_LIST of arguments).  See parser construction sites
       e.g. `new_pos_node2 (..., N_CALL, ..., callee, arg_list)`. */
    node_t callee = NL_HEAD (n->u.ops);
    node_t arg_list = callee != NULL ? NL_NEXT (callee) : NULL;
    int is_release = (callee != NULL && callee->code == N_ID
                      && callee->u.s.s != NULL
                      && strcmp (callee->u.s.s, release_fn) == 0);
    if (arg_list != NULL && arg_list->code == N_LIST) {
      for (node_t a = NL_HEAD (arg_list->u.ops); a != NULL; a = NL_NEXT (a))
        if (subtree_mentions_id (a, name)) {
          if (is_release) out->released_p = 1;
          else            out->escaped_p = 1;
        }
    }
    break;
  }
  case N_ASSIGN: {
    /* `lhs = rhs`.  If `name` appears in the RHS, the value may now live
       in some other binding; conservatively treat as escaped. */
    node_t lhs = NL_HEAD (n->u.ops);
    node_t rhs = lhs != NULL ? NL_NEXT (lhs) : NULL;
    if (rhs != NULL && subtree_mentions_id (rhs, name))
      out->escaped_p = 1;
    break;
  }
  case N_ADDR: {
    /* `&name` — caller's now holding a pointer-to-pointer; could free via
       indirection.  Treat as escaped. */
    node_t arg = NL_HEAD (n->u.ops);
    if (arg != NULL && subtree_mentions_id (arg, name))
      out->escaped_p = 1;
    break;
  }
  default:
    break;
  }

  /* Generic recursion to find nested release calls / returns / escapes. */
  if (!ownership_node_has_ops (n->code)) return;
  for (node_t c = NL_HEAD (n->u.ops); c != NULL; c = NL_NEXT (c))
    scan_uses (c, name, release_fn, out);
}

/* For each malloc-family SPEC_DECL inside `func_def`, scan the entire
 * function for uses of the bound identifier.  Emit a leak warning when the
 * scan finds no release call and no escape — a definite leak under the
 * conservative rules.
 *
 * Walks the whole FUNC_DEF subtree once per candidate.  Cost is O(C × N)
 * where C is the candidate count and N is the function's AST size; fine
 * for the typical 1–3 candidates per function.  Will be replaced by a
 * single CFG-aware dataflow walk once the full state machine lands. */
static void leakcheck_function (c2m_ctx_t c2m_ctx, node_t func_def, node_t n) {
  if (n == NULL) return;

  if (n->code == N_SPEC_DECL) {
    decl_t d = (decl_t) n->attr;
    /* Only inspect bindings the check pass has already vetted as candidates
       (`auto_defer_p` is FALSE under `unowned`, at top-level, for non-pointer
       declarators, etc.). */
    if (d != NULL && d->auto_defer_p) {
      node_t init = SPEC_DECL_INIT (n);
      const char *acquire = acquire_fn_name_of_init (init);
      const char *release = release_fn_for_acquire (acquire);
      if (acquire != NULL && release != NULL) {
        const char *name = ownership_spec_decl_name (n);
        if (name != NULL) {
          usage_summary_t u;
          u.released_p = 0;
          u.escaped_p  = 0;
          scan_uses (func_def, name, release, &u);
          if (!u.released_p && !u.escaped_p) {
            warning (c2m_ctx, POS (n),
                     "leak: `%s` allocated by `%s()` is never `%s()`d, "
                     "returned, or stored elsewhere in this function "
                     "(mark it `unowned` to silence this check)",
                     name, acquire, release);
          }
        }
      }
    }
  }

  if (!ownership_node_has_ops (n->code)) return;
  for (node_t c = NL_HEAD (n->u.ops); c != NULL; c = NL_NEXT (c))
    leakcheck_function (c2m_ctx, func_def, c);
}

/* Recursively walk an AST subtree, accounting for any SPEC_DECLs the check
 * pass marked as auto-defer candidates, and kicking off the per-function
 * leak check at each N_FUNC_DEF.  When `verbose_p` is set, prints one line
 * per candidate.  Never mutates the AST. */
static void ownership_walk (c2m_ctx_t c2m_ctx, node_t n, int *count, int verbose_p) {
  if (n == NULL) return;

  /* Function-level leak check: scoped to this FUNC_DEF's subtree so a
     candidate in `foo()` doesn't see a `free()` call in `bar()`.  We still
     continue recursion below so nested defs (class methods are hoisted to
     module top, but other future nesting patterns may appear) are visited
     and the auto-defer candidate count stays correct. */
  if (n->code == N_FUNC_DEF)
    leakcheck_function (c2m_ctx, n, n);

  if (n->code == N_SPEC_DECL) {
    decl_t d = (decl_t) n->attr;
    if (d != NULL && d->auto_defer_p) {
      (*count)++;
      if (verbose_p) {
        const char *name = ownership_spec_decl_name (n);
        pos_t p = POS (n);
        fprintf (stderr,
                 "  [ownership] auto-defer candidate: %s  (%s:%d)\n",
                 name != NULL ? name : "<anonymous>",
                 p.fname != NULL ? p.fname : "?",
                 p.lno);
      }
    }
  }

  /* Recurse only when the node's union actively holds an op list. */
  if (!ownership_node_has_ops (n->code)) return;
  for (node_t c = NL_HEAD (n->u.ops); c != NULL; c = NL_NEXT (c))
    ownership_walk (c2m_ctx, c, count, verbose_p);
}

/* Entry point: run the ownership analysis over an entire module's AST.
 * Returns the number of *errors* emitted; warnings don't count.  The first-
 * stage implementation always returns 0 — observation only, no diagnostics. */
static int ownership_run (c2m_ctx_t c2m_ctx, node_t module) {
  int count = 0;
  int verbose_p = c2m_options != NULL && c2m_options->verbose_p;

  if (verbose_p)
    fprintf (stderr, "  [ownership] pass: scanning AST for auto-defer candidates...\n");

  ownership_walk (c2m_ctx, module, &count, verbose_p);

  if (verbose_p)
    fprintf (stderr,
             "  [ownership] pass: %d auto-defer candidate%s found "
             "(no synthesis yet; see src/ownership.c TODOs)\n",
             count, count == 1 ? "" : "s");

  return 0;
}
