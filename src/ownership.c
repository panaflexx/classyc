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
  /* `new T(...)` is a language-level acquire whose paired release is the
     `delete` keyword (not a function call).  The release_fn string "delete"
     is a sentinel: transfer_call never matches it (delete isn't a call),
     transfer_delete handles the actual state transition, and the auto-
     release synthesizer builds an N_DELETE node rather than an N_CALL. */
  if (strcmp (acquire, "new")     == 0) return "delete";
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
  if (init == NULL) return NULL;
  /* `new T(...)` is a first-class acquire form in ClassyC: the binding
     owns a fresh heap object that must be `delete`d.  We return the
     sentinel "new" so release_fn_for_acquire maps it to "delete". */
  if (init->code == N_NEW) return "new";
  if (init->code != N_CALL) return NULL;
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

/* ────────────────────────────────────────────────────────────────────────
 * Non-retaining consumer table (Step A).
 *
 * Standard library calls that *read* their pointer arguments but do not
 * retain or release ownership.  Treating these as escapes was the single
 * biggest false-negative source in v1; teaching the analyser they're
 * non-retaining means `strdup` -> `printf` correctly trips a leak warning.
 *
 * Conservative: this list is allow-listed.  An unknown call carrying a
 * tracked pointer is still treated as a possible escape — we'd rather miss
 * a leak than complain about a correct hand-off to a user destroyer.
 * ──────────────────────────────────────────────────────────────────────── */
static int is_non_retaining_consumer (const char *fn) {
  if (fn == NULL) return 0;
  /* Output / formatting — read pointer, don't retain. */
  if (strcmp (fn, "printf")     == 0) return 1;
  if (strcmp (fn, "fprintf")    == 0) return 1;
  if (strcmp (fn, "sprintf")    == 0) return 1;
  if (strcmp (fn, "snprintf")   == 0) return 1;
  if (strcmp (fn, "vprintf")    == 0) return 1;
  if (strcmp (fn, "vfprintf")   == 0) return 1;
  if (strcmp (fn, "puts")       == 0) return 1;
  if (strcmp (fn, "fputs")      == 0) return 1;
  if (strcmp (fn, "fwrite")     == 0) return 1;
  if (strcmp (fn, "putchar")    == 0) return 1;
  if (strcmp (fn, "fputc")      == 0) return 1;
  if (strcmp (fn, "perror")     == 0) return 1;
  /* String inspection / measurement. */
  if (strcmp (fn, "strlen")     == 0) return 1;
  if (strcmp (fn, "strnlen")    == 0) return 1;
  if (strcmp (fn, "strcmp")     == 0) return 1;
  if (strcmp (fn, "strncmp")    == 0) return 1;
  if (strcmp (fn, "strcasecmp") == 0) return 1;
  if (strcmp (fn, "strchr")     == 0) return 1;
  if (strcmp (fn, "strrchr")    == 0) return 1;
  if (strcmp (fn, "strstr")     == 0) return 1;
  if (strcmp (fn, "strspn")     == 0) return 1;
  if (strcmp (fn, "strcspn")    == 0) return 1;
  /* Memory inspection (read-only on these args). */
  if (strcmp (fn, "memcmp")     == 0) return 1;
  if (strcmp (fn, "memchr")     == 0) return 1;
  /* Buffered I/O reads — they write into the user's buffer but don't
     retain a pointer to it after returning. */
  if (strcmp (fn, "fread")      == 0) return 1;
  if (strcmp (fn, "fgets")      == 0) return 1;
  if (strcmp (fn, "fgetc")      == 0) return 1;
  if (strcmp (fn, "fscanf")     == 0) return 1;
  if (strcmp (fn, "sscanf")     == 0) return 1;
  if (strcmp (fn, "read")       == 0) return 1;
  /* ClassyC builtins that read a pointer arg but never retain it.  `json` is
     a special compiler-lowered call (parsed as an N_CALL to the builtin
     `json` before codegen): `json(string)` parses a fresh dict OUT of the
     input buffer (copying every leaf), and `json(dict)` serializes into a
     fresh String.  In both forms the pointer argument is only read, so a
     buffer passed to `json(...)` is still the caller's to free — without
     this, `dict d = json(text); free(text);` is a spurious double-free. */
  if (strcmp (fn, "json")       == 0) return 1;
  return 0;
}

/* ────────────────────────────────────────────────────────────────────────
 * Decl-identity helpers (Step D).
 *
 * Track bindings by their declaring `N_SPEC_DECL` node, not by name string.
 * This survives shadowing inside nested scopes and gives us a stable key
 * for the per-decl state map below.  The check pass already populates
 * `((struct expr *) id_node->attr)->def_node` to point at the declaration
 * (see classyc.c N_ID case in `check`), so we just consume that link.
 * ──────────────────────────────────────────────────────────────────────── */

/* The SPEC_DECL an identifier-reference resolves to, or NULL if the
 * expression isn't an N_ID reference — directly, or after peeling leading
 * C casts.  The cast peeling matters at every call site that classifies an
 * arbitrary expression: in `free((void *)keys)`, transfer_call needs to
 * recognise `(void *)keys` as a direct reference to the `keys` binding to
 * fire the release transition.  Returning NULL on field accesses,
 * constants, function-pointer derefs, etc. keeps complex expressions in
 * the "not a direct identifier" bucket. */
static node_t id_resolves_to_decl (node_t n) {
  struct expr *e;
  while (n != NULL && n->code == N_CAST) n = NL_EL (n->u.ops, 1);
  if (n == NULL || n->code != N_ID) return NULL;
  e = (struct expr *) n->attr;
  if (e == NULL) return NULL;
  return e->def_node;
}

/* Resolve a call's callee expression to the function / method definition it
 * invokes.  Handles three callee shapes uniformly:
 *
 *   foo(...)        — callee is N_ID         (ordinary free function)
 *   obj->m(...)      — callee is N_DEREF_FIELD (pointer-receiver method)
 *   obj.m(...) /     — callee is N_FIELD       (value-receiver or static
 *   Class.m(...)                               method)
 *
 * In every case the check pass stashes the resolved declaration on the
 * callee node's `struct expr`->def_node: the N_ID handler points it at the
 * declaration, and the N_CALL method path assigns the *resolved overload's*
 * N_FUNC_DEF onto the field-access node (see classyc.c, `fe->def_node =
 * func_def`).  So consuming def_node here gives interprocedural reach into
 * `db->query(...)` and `Sqlite.open(...)` exactly like a plain call.
 * Returns NULL for indirect / unresolved calls. */
static node_t callee_def_node (node_t callee) {
  struct expr *ce;
  if (callee == NULL) return NULL;
  if (callee->code != N_ID && callee->code != N_DEREF_FIELD
      && callee->code != N_FIELD)
    return NULL;
  ce = (struct expr *) callee->attr;
  if (ce == NULL) return NULL;
  return ce->def_node;
}

/* The source-level name of a call's callee for diagnostics / acquire-tag
 * rendering.  For `foo(...)` that's "foo"; for `obj->query(...)` it's the
 * method name "query".  NULL for shapes we can't name. */
static const char *callee_name_of (node_t callee) {
  if (callee == NULL) return NULL;
  if (callee->code == N_ID) return callee->u.s.s;
  if (callee->code == N_DEREF_FIELD || callee->code == N_FIELD) {
    node_t mid = NL_EL (callee->u.ops, 1); /* operand 1 is the N_ID method name */
    if (mid != NULL && mid->code == N_ID) return mid->u.s.s;
  }
  return NULL;
}

/* TRUE iff the subtree at `n` references the binding declared at `target`.
 * Stable under shadowing because we compare AST identity, not strings. */
static int subtree_mentions_decl (node_t n, node_t target) {
  if (n == NULL || target == NULL) return 0;
  if (n->code == N_ID && id_resolves_to_decl (n) == target) return 1;
  if (!ownership_node_has_ops (n->code)) return 0;
  for (node_t c = NL_HEAD (n->u.ops); c != NULL; c = NL_NEXT (c))
    if (subtree_mentions_decl (c, target)) return 1;
  return 0;
}

/* ────────────────────────────────────────────────────────────────────────
 * State lattice + per-function analysis context (Step F).
 *
 * Per-binding state: one of the five values from the design doc.  Stored
 * inline on each `candidate_t`; cloned for save/restore at branch points.
 * The lattice meet is the textbook "agreement, else MaybeOwned" rule.
 * ──────────────────────────────────────────────────────────────────────── */

typedef enum {
  OS_UNOWNED      = 0,   /* default — binding holds a value it does not own  */
  OS_OWNED,              /* binding owns the resource; must be released      */
  OS_DETACHED,           /* ownership transferred out (detach / return)      */
  OS_RELEASED,           /* explicitly released; any further use is UAF      */
  OS_MAYBE_OWNED,        /* join-point conflict between Owned and !Owned     */
  OS_MOVED,              /* consumed by `move`: the binding is dead — ANY use
                            (read or write) is an error.  At runtime the source
                            pointer was nulled; the new owner holds the value. */
} owstate_t;

/* Definite-assignment lattice (added for compile-time uninit-read detection).
   Orthogonal to the ownership lattice; tracked only for scalar/pointer locals. */
typedef enum {
  DA_UNINIT = 0,   /* no write on any path reaching this point */
  DA_INIT,         /* definitely written on every path */
  DA_MAYBE         /* written on some paths, not others (join) */
} da_state_t;

static const char *owstate_name (owstate_t s) {
  switch (s) {
  case OS_UNOWNED:     return "Unowned";
  case OS_OWNED:       return "Owned";
  case OS_DETACHED:    return "Detached";
  case OS_RELEASED:    return "Released";
  case OS_MAYBE_OWNED: return "MaybeOwned";
  case OS_MOVED:       return "Moved";
  default:             return "?";
  }
}

/* Lattice meet: combine two states at a control-flow join.
 *
 * Rules:
 *   - Same on both sides: trivial passthrough.
 *   - Unowned + X: propagate X (the Unowned side hadn't acquired yet).
 *   - Released + Detached: both paths disposed of the resource, just by
 *     different mechanisms; collapse to Detached (a "gone" state that
 *     doesn't trigger leak warnings and still flags any subsequent release
 *     as a double-free risk).  Without this, code like
 *         if (a) free(p); else escape(p);
 *     would falsely warn about a potential leak on the join.
 *   - Anything else (Owned vs Released, Owned vs Detached, …): conflict;
 *     widen to MaybeOwned so the user gets a path-sensitive diagnostic. */
static owstate_t state_meet (owstate_t a, owstate_t b) {
  if (a == b) return a;
  if (a == OS_UNOWNED) return b;
  if (b == OS_UNOWNED) return a;
  /* OS_MOVED is a "gone" state like OS_DETACHED for merge purposes: a binding
     that is Moved on one path and Detached/Released on another is simply gone.
     Moved meeting Owned is a genuine conflict (MaybeOwned) — e.g. a
     conditional move — so a later use is not flagged (it may still be live). */
  {
    owstate_t na = (a == OS_MOVED) ? OS_DETACHED : a;
    owstate_t nb = (b == OS_MOVED) ? OS_DETACHED : b;
    if (na == nb) return na;
    if ((na == OS_RELEASED && nb == OS_DETACHED)
        || (na == OS_DETACHED && nb == OS_RELEASED))
      return OS_DETACHED;
  }
  return OS_MAYBE_OWNED;
}

static da_state_t da_meet (da_state_t a, da_state_t b) {
  if (a == b) return a;
  return DA_MAYBE;   /* INIT ⊓ UNINIT = MAYBE */
}

/* A tracked binding being analysed.  One slot per candidate per function. */
typedef struct candidate {
  node_t      decl;          /* SPEC_DECL node (identity key) */
  const char *name;          /* for diagnostics */
  const char *acquire_fn;    /* e.g. "malloc" */
  const char *release_fn;    /* e.g. "free" */
  pos_t       acquire_pos;   /* position of the declaration */
  owstate_t   state;         /* current per-path state */
  int         warned_p;      /* TRUE once we've emitted a diagnostic for it */
  int         has_cleanup_p; /* TRUE if marked __attribute__((cleanup(fn))) */
  /* Sticky flag: set whenever the candidate's ownership was observed to
     escape on some path (return p; store-into-other-loc; call detaches it).
     Read by the -fauto-release synthesis pass to refuse generating a
     scope-exit free(p) on candidates that may have already escaped — doing
     so would double-free on the escaping branch. */
  int         escapes_p;
  /* Sticky flag: set when this owned local was handed to the caller via
     `return <candidate>;` on *some* path.  Distinct from the merged exit
     state: a function can both `delete p;` on an error/throw path and
     `return p;` on the success path (the canonical "cleanup on failure,
     hand back on success" shape, e.g. Sqlite::query).  The merged exit
     state is then MaybeOwned, which would hide the fact that the function
     ships ownership on its normal return.  returns_owned_p inference reads
     this flag so such functions are correctly summarised as owned-returning. */
  int         returned_p;
  /* Sticky flag: set when the *user* explicitly released this binding on
     some path (`delete p;` / `free(p)` / a ((releases)) call).  Gates the
     -fauto-release MaybeOwned relaxation: a binding that the user already
     frees somewhere must NOT also get a synthesized scope-exit release
     (that would double-free).  When this is clear and the binding never
     escaped, a synthesized `defer delete p;` is always safe even at a
     MaybeOwned merge, because delete/free are null-safe and the only way
     to reach MaybeOwned without a release is a null-guard narrowing. */
  int         released_p;
  /* First-observed disposition site (`-fownership-report`).  Populated
     during the diagnostic replay (quiet_p == 0) so it reflects source
     order, not worklist iteration order.  `release_kind` is a short human
     string ("freed", "returned", "stored", ...); when NULL at end of
     analysis the binding leaked. */
  pos_t       release_pos;
  const char *release_kind;
  /* Interprocedural summary tracking.  When set, the candidate represents
     an incoming function parameter rather than a local heap binding.
     - leak warnings are suppressed (the caller still owns the pointer)
     - the final state at function-exit drives summary inference
       (RELEASED on every path  -> ((releases)),
        OWNED on every path     -> ((borrows)),
        anything else           -> conservative PA_DEFAULT) */
  unsigned    param_p : 1;
  size_t      param_pos;
  /* Managed-ownership layer (owned / move / readonly).  `managed_p` is set for
     bindings declared `owned` (or `move`-initialized from an owned value):
     they are single-owner and the compiler GUARANTEES a single scope-exit
     release unless ownership is moved out — their leak diagnostics are
     suppressed and a `delete` is synthesized unconditionally (no
     -fauto-release needed).  `moved_p` is set once ownership has been moved
     OUT of this binding via `move`; the binding then behaves as a read-only
     view and any further ownership operation on it is an error. */
  int         managed_p;
  int         moved_p;
  /* Set when the binding escaped via a mechanism we CANNOT neutralize at the
     source: `return`, store into a non-tracked location (global/field), an
     unknown/owning call, a plain `=` alias, `detach`, or a user `defer`
     release.  A `move` escape is deliberately NOT counted here, because
     `move` nulls the source pointer — so a synthesized null-safe `delete` on
     the moved-from binding is a no-op on the moved path.  Managed-binding
     auto-release synthesis is gated on this flag (not the broader escapes_p),
     which lets conditionally-moved owned locals still get cleaned up on the
     paths where they were NOT moved out. */
  int         unsafe_escape_p;
  /* Definite-assignment state (new).  Only meaningful for scalar/pointer
     locals.  DA_UNINIT means the binding is read before any write on every
     path; DA_MAYBE means some paths write, others do not.  Emitted as a
     compile-time error (UNINIT) or warning (MAYBE) at the read site. */
  da_state_t  da_state;
  /* If set, the definite-assignment diagnostic is suppressed for this
     candidate.  Set by the `__attribute__((classyc_da_ignore))` (or the
     shorter `__attribute__((da_ignore))`) on the declaration.
     Used by the collection headers (list.h, set.h, map.h) to silence
     the check for their internal temporaries without disabling it
     globally. */
  int         da_ignore_p;
  /* Pure definite-assignment candidate: a local pointer (or other
     tracked-for-DA binding) that is NOT an ownership / auto-defer
     candidate.  Leak diagnostics, auto-release synthesis, and ownership
     state transitions must leave these alone — they only participate in
     the DA lattice and the uninit-use warning. */
  int         da_only_p;
} candidate_t;

DEF_VARR (candidate_t);

/* Interprocedural state (summaries + iteration flags) is declared further
   down, just after `param_attr_t`.  Forward-declared here for the few
   transfer functions and helpers that need to consult it. */
static int   ownership_silent_pass_p;
static node_t func_node_of (node_t func_def_or_decl);
static const char *summary_release_for_init (node_t init);

/* Per-function analysis context.
 *
 *   dead_p     — control flow has terminated on the current path (return /
 *                throw); transfer functions then become no-ops, and meet
 *                operations skip dead branches.
 *   quiet_p    — set during the CFG worklist's fixpoint iteration so the
 *                transfer functions transition state without firing
 *                diagnostics.  The final diagnostic pass clears this flag
 *                and replays each block once with its stable in-state.
 *                Also gates the `warned_p` write so suppressed diagnostics
 *                don't poison the final pass. */
typedef struct flowctx {
  c2m_ctx_t           c2m_ctx;
  node_t              func_def;
  VARR (candidate_t) *cands;
  int                 dead_p;
  int                 quiet_p;
  int                 verbose_p;
  /* Set by transfer_return when the returned expression itself is an
     acquire form (`return new T(...)`, `return malloc(...)`, `return
     detach <acquire>`), even if no intermediate binding was tracked.
     Drives returns_owned_p inference for thin-wrapper functions. */
  int                 returns_owned_inline_p;
  const char         *returns_release_fn_inline;
} flowctx_t;

/* Emit-or-suppress helpers used by the transfer functions below.
   diag_active_p returns FALSE during interprocedural inference iterations
   so the silent passes don't emit warnings the user would then see again
   on the final pass. */
static int diag_active_p (flowctx_t *ctx) {
  return !ctx->quiet_p && !ownership_silent_pass_p;
}
static void diag_mark_warned (flowctx_t *ctx, candidate_t *c) {
  if (!ctx->quiet_p && !ownership_silent_pass_p) c->warned_p = 1;
}

/* `-fownership-report`: record the first observed disposition of a tracked
   binding.  Called from every transfer function as it transitions the
   candidate to a terminal state.  First-writer-wins so the recorded site
   reflects source order during the diagnostic replay (quiet_p == 0). */
static void record_disposition (flowctx_t *ctx, candidate_t *c,
                                pos_t pos, const char *kind) {
  if (ctx->quiet_p) return;
  if (c->release_kind != NULL) return;
  c->release_kind = kind;
  c->release_pos  = pos;
}

/* Find a candidate by decl identity.  Returns -1 if not tracked. */
static int cand_lookup (flowctx_t *ctx, node_t decl) {
  size_t n;
  if (decl == NULL) return -1;
  n = VARR_LENGTH (candidate_t, ctx->cands);
  for (size_t i = 0; i < n; i++)
    if (VARR_ADDR (candidate_t, ctx->cands)[i].decl == decl) return (int) i;
  return -1;
}

/* Save / restore / merge state across a branch split. */
static void state_snapshot (flowctx_t *ctx, owstate_t *out) {
  candidate_t *a = VARR_ADDR (candidate_t, ctx->cands);
  size_t n = VARR_LENGTH (candidate_t, ctx->cands);
  for (size_t i = 0; i < n; i++) out[i] = a[i].state;
}
static void state_restore (flowctx_t *ctx, const owstate_t *in) {
  candidate_t *a = VARR_ADDR (candidate_t, ctx->cands);
  size_t n = VARR_LENGTH (candidate_t, ctx->cands);
  for (size_t i = 0; i < n; i++) a[i].state = in[i];
}
static void state_merge_from (flowctx_t *ctx, const owstate_t *other) {
  candidate_t *a = VARR_ADDR (candidate_t, ctx->cands);
  size_t n = VARR_LENGTH (candidate_t, ctx->cands);
  for (size_t i = 0; i < n; i++) a[i].state = state_meet (a[i].state, other[i]);
}

/* Mid-loop conservative widening: collapse Owned to MaybeOwned for every
 * candidate.  Used by the loop walker since we don't iterate to fixpoint
 * for v1 — a binding that's still Owned at the back-edge could be the same
 * acquire from the previous iteration about to be overwritten. */
static void state_widen_owned_to_maybe (flowctx_t *ctx) {
  candidate_t *a = VARR_ADDR (candidate_t, ctx->cands);
  size_t n = VARR_LENGTH (candidate_t, ctx->cands);
  for (size_t i = 0; i < n; i++)
    if (a[i].state == OS_OWNED) a[i].state = OS_MAYBE_OWNED;
}

/* ────────────────────────────────────────────────────────────────────────
 * Candidate collection: discover tracked bindings before analysis runs.
 * ──────────────────────────────────────────────────────────────────────── */

/* True when a SPEC_DECL initializer is a real rvalue (not missing / N_IGNORE).
   The parse always materialises a placeholder N_IGNORE for "no initializer",
   so a bare non-NULL test would treat every local as definitely assigned. */
static int decl_has_real_init_p (node_t init) {
  return init != NULL && init->code != N_IGNORE;
}

/* Scan declaration attributes for cleanup / da_ignore markers. */
static void candidate_scan_attrs (node_t n, int *has_cleanup_p, int *da_ignore_p) {
  *has_cleanup_p = 0;
  *da_ignore_p = 0;
  node_t attrs = SPEC_DECL_ATTRS (n);
  if (attrs == NULL || attrs->code != N_LIST) return;
  for (node_t aa = NL_HEAD (attrs->u.ops); aa != NULL; aa = NL_NEXT (aa)) {
    if (aa->code != N_ATTR) continue;
    node_t aname = NL_HEAD (aa->u.ops);
    if (aname != NULL && aname->code == N_ID && aname->u.s.s != NULL) {
      if (strcmp (aname->u.s.s, "cleanup") == 0) {
        *has_cleanup_p = 1;
      } else if (strcmp (aname->u.s.s, "da_ignore") == 0
                 || strcmp (aname->u.s.s, "classyc_da_ignore") == 0) {
        *da_ignore_p = 1;
      }
    }
  }
}

static void collect_candidates (c2m_ctx_t c2m_ctx, node_t n, VARR (candidate_t) *out) {
  /* c2m_ctx parameter present only so POS() macro expands correctly here. */
  (void) c2m_ctx;
  if (n == NULL) return;
  if (n->code == N_SPEC_DECL) {
    decl_t d = (decl_t) n->attr;
    /* Two acquire-discovery paths:
        (1) check-pass-marked `auto_defer_p` bindings whose init matches a
            known acquire form (malloc-family or `new T(...)`);
        (2) interprocedural: any SPEC_DECL whose call initializer resolves
            to a callee whose summary says it returns owned ownership.
       Path (2) lets us track local bindings hooked to user wrappers like
       `auto x = make_buf(...);` without anyone marking them auto_defer_p
       in the check pass.  We only consider locals (scope != top_scope). */
    int auto_defer_path = (d != NULL && d->auto_defer_p);
    const char *ip_release_fn = NULL;
    /* Managed-ownership opt-in: a binding declared `owned` (or `move`-
       initialized from an owned value) joins the GC-like layer.  When its
       initializer isn't already a recognized acquire (the `new`-init case
       sets auto_defer_p too), this flag is what gets it collected — e.g.
       `auto y = move x;`. */
    int owned_managed = (d != NULL && d->owned_p && !d->unowned_p);
    /* `unowned <decl>` opts the binding out of ALL ownership tracking: the
       user takes manual responsibility, so we neither leak-warn nor
       auto-release it, and the interprocedural call-returned-owner path
       below must also skip it (auto_defer_p alone wouldn't, since that
       flag is never set for call/method initializers). */
    if (d != NULL && d->unowned_p) { auto_defer_path = 0; }
    else if (!auto_defer_path && d != NULL && d->scope != NULL
        && d->decl_spec.type != NULL && d->decl_spec.type->mode == TM_PTR
        && !d->decl_spec.typedef_p && !d->decl_spec.static_p
        && !d->decl_spec.thread_local_p) {
      ip_release_fn = summary_release_for_init (SPEC_DECL_INIT (n));
    }
    int collected = 0;
    if (auto_defer_path || ip_release_fn != NULL || owned_managed) {
      node_t init = SPEC_DECL_INIT (n);
      const char *acquire, *release, *name;
      if (auto_defer_path) {
        acquire = acquire_fn_name_of_init (init);
        release = release_fn_for_acquire (acquire);
      } else if (ip_release_fn == NULL && owned_managed) {
        /* Managed binding whose initializer is not a direct acquire — the
           canonical case is `auto y = move x;`.  Ownership of an existing
           owned object flows in; the paired release is `delete`. */
        node_t om_init = init;
        while (om_init != NULL && om_init->code == N_CAST)
          om_init = NL_EL (om_init->u.ops, 1);
        acquire = (om_init != NULL && om_init->code == N_MOVE) ? "move" : "owned";
        release = "delete";
      } else {
        /* IP path: use the callee's actual name as the acquire tag so the
           ownership report reads `x = make_buf(...)` not `x = <ip>(...)`.
           The release fn was captured in the callee's summary. */
        node_t ip_init = init;
        while (ip_init != NULL && ip_init->code == N_CAST)
          ip_init = NL_EL (ip_init->u.ops, 1);
        node_t ip_callee = (ip_init != NULL && ip_init->code == N_CALL)
                            ? NL_HEAD (ip_init->u.ops) : NULL;
        const char *ip_name = callee_name_of (ip_callee);
        acquire = ip_name != NULL ? ip_name : "<ip>";
        release = ip_release_fn;
      }
      name = ownership_spec_decl_name (n);
      if (acquire != NULL && release != NULL && name != NULL) {
        candidate_t c;
        memset (&c, 0, sizeof (c));
        c.decl         = n;
        c.name         = name;
        c.acquire_fn   = acquire;
        c.release_fn   = release;
        c.acquire_pos  = POS (n);
        c.state        = OS_UNOWNED; /* becomes OS_OWNED when its decl is hit */
        c.managed_p    = (d != NULL && d->owned_p) ? 1 : 0;
        /* Real initializer => DA_INIT; bare `T *p;` stays DA_UNINIT. */
        c.da_state     = decl_has_real_init_p (init) ? DA_INIT : DA_UNINIT;
        c.da_only_p    = 0;
        candidate_scan_attrs (n, &c.has_cleanup_p, &c.da_ignore_p);
        VARR_PUSH (candidate_t, out, c);
        collected = 1;
      }
    }
    /* Definite-assignment path: track local pointer bindings that are not
       already ownership candidates.  This is how `int *p;` without an
       initializer is recognised so a later `*p` can warn (and gen can
       zero-init so the null guard traps the wild read at runtime). */
    if (!collected && d != NULL && !d->unowned_p
        && d->scope != NULL && d->scope != top_scope
        && d->scope->code == N_BLOCK
        && d->decl_spec.type != NULL && d->decl_spec.type->mode == TM_PTR
        && !d->decl_spec.typedef_p && !d->decl_spec.extern_p
        && !d->decl_spec.static_p && !d->decl_spec.thread_local_p) {
      const char *name = ownership_spec_decl_name (n);
      node_t init = SPEC_DECL_INIT (n);
      /* Skip the synthesized method receiver: create_decl stores `this` as a
         block-scoped pointer SPEC_DECL without an initializer, so without this
         gate it would look like an uninitialized local and later gen could
         interfere.  Real parameters live on the N_FUNC param list; we only
         walk the function body for DA candidates (see analyze_function). */
      if (name != NULL && strcmp (name, "this") != 0) {
        candidate_t c;
        memset (&c, 0, sizeof (c));
        c.decl         = n;
        c.name         = name;
        c.acquire_fn   = "<da>";
        c.release_fn   = NULL; /* no ownership / release semantics */
        c.acquire_pos  = POS (n);
        c.state        = OS_UNOWNED;
        c.da_state     = decl_has_real_init_p (init) ? DA_INIT : DA_UNINIT;
        c.da_only_p    = 1;
        candidate_scan_attrs (n, &c.has_cleanup_p, &c.da_ignore_p);
        VARR_PUSH (candidate_t, out, c);
      }
    }
  }
  if (!ownership_node_has_ops (n->code)) return;
  for (node_t c = NL_HEAD (n->u.ops); c != NULL; c = NL_NEXT (c))
    collect_candidates (c2m_ctx, c, out);
}

/* Seed param-typed candidates so the analyzer can infer summaries.
   Walks the function's declarator-N_FUNC param list, and for each
   pointer-typed N_SPEC_DECL adds a candidate with `param_p=1` and an
   initial conceptual state of OS_OWNED (assume caller passes ownership).
   `this` is skipped.  Non-pointer params are skipped (we can't track
   ownership of an int).

   These candidates serve two purposes:
   1. Their final dataflow state drives summary inference (RELEASES /
      BORROWS / DEFAULT) at the end of the pass.
   2. They participate in transfer_call / transfer_assign exactly like
      heap-acquire candidates, so `free(p)` on a parameter is recognized
      as a release and the function gets ((releases)) inferred. */
static void collect_param_candidates (c2m_ctx_t c2m_ctx, node_t func_def,
                                      VARR (candidate_t) *out) {
  (void) c2m_ctx;
  node_t fn = func_node_of (func_def);
  if (fn == NULL) return;
  node_t params = NL_HEAD (fn->u.ops);
  if (params == NULL || params->code != N_LIST) return;
  size_t pos = 0;
  for (node_t p = NL_HEAD (params->u.ops); p != NULL; p = NL_NEXT (p), pos++) {
    if (p->code == N_DOTS) break;
    if (p->code != N_SPEC_DECL) continue;
    decl_t pd = (decl_t) p->attr;
    if (pd == NULL) continue;
    if (pd->decl_spec.type == NULL || pd->decl_spec.type->mode != TM_PTR) continue;
    /* Skip the implicit `this` receiver of class methods. */
    node_t declr = SPEC_DECL_DECL (p);
    if (declr == NULL || declr->code != N_DECL) continue;
    node_t id = DECL_ID (declr);
    if (id == NULL || id->code != N_ID) continue;
    if (id->u.s.s != NULL && strcmp (id->u.s.s, "this") == 0) continue;

    candidate_t c;
    memset (&c, 0, sizeof (c));
    c.decl         = p;
    c.name         = id->u.s.s != NULL ? id->u.s.s : "<param>";
    c.acquire_fn   = "<param>";
    c.release_fn   = "free";   /* assume free for diag strings; actual release
                                  is inferred from how the body treats it */
    c.acquire_pos  = POS (p);
    c.state        = OS_OWNED; /* assume caller passed ownership */
    c.param_p      = 1;
    c.param_pos    = pos;
    c.da_state     = DA_INIT; /* parameters are inputs, considered defined */
    c.da_only_p    = 0;
    VARR_PUSH (candidate_t, out, c);
  }
}

/* ────────────────────────────────────────────────────────────────────────
 * Diagnostics.
 *
 * Emit at every exit point we reach (function-end, N_RETURN, throw), but
 * only once per candidate — the `warned_p` flag guards re-firing if a
 * function has multiple return paths.
 * ──────────────────────────────────────────────────────────────────────── */

static void check_exit_state (flowctx_t *ctx, pos_t exit_pos, const char *exit_kind) {
  c2m_ctx_t c2m_ctx = ctx->c2m_ctx; /* for POS() macro hygiene */
  candidate_t *a = VARR_ADDR (candidate_t, ctx->cands);
  size_t n = VARR_LENGTH (candidate_t, ctx->cands);
  (void) exit_pos;
  (void) c2m_ctx;
  if (!diag_active_p (ctx)) return; /* worklist mode: state-only, no emit */
  for (size_t i = 0; i < n; i++) {
    candidate_t *c = &a[i];
    if (c->warned_p) continue;
    /* __attribute__((cleanup(fn))) on the declaration means the user has
       arranged automatic release via GCC's RAII attribute.  Skip our leak
       diagnostics for these bindings. */
    if (c->has_cleanup_p) continue;
    /* Parameters are tracked only to infer summaries; the caller still
       owns them, so it's not a leak if they stay Owned at function exit. */
    if (c->param_p) continue;
    /* Pure DA candidates have no ownership / release responsibility. */
    if (c->da_only_p || c->release_fn == NULL) continue;
    /* Managed (`owned` / `move`) bindings never leak: the compiler
       guarantees a scope-exit release (synthesized in cfg_emit_diagnostics)
       unless ownership was moved out (escapes_p).  Suppress all leak
       diagnostics for them. */
    if (c->managed_p) continue;
    /* `delete` is a statement keyword (`delete p;`), not a function call,
       so don't render it with the function-call parens that suit `free(p)`. */
    int delete_p = (c->release_fn != NULL && strcmp (c->release_fn, "delete") == 0);
    /* `new T(...)` is the literal-acquire form; for IP-discovered wrappers
       (acquire_fn is the wrapper's name like "spawn"), use the call-style
       rendering instead so the message reads correctly. */
    int literal_new_p = (c->acquire_fn != NULL && strcmp (c->acquire_fn, "new") == 0);
    if (c->state == OS_OWNED) {
      if (delete_p)
        warning (ctx->c2m_ctx, c->acquire_pos,
                 literal_new_p
                   ? "leak: `%s` allocated by `%s T(...)` is still owned at %s "
                     "but never `delete`d, returned, or stored elsewhere\n"
                     "  hint: add `defer delete %s;` right after this declaration, "
                     "or mark the binding `unowned` to silence this check"
                   : "leak: `%s` allocated by `%s(...)` is still owned at %s "
                     "but never `delete`d, returned, or stored elsewhere\n"
                     "  hint: add `defer delete %s;` right after this declaration, "
                     "or mark the binding `unowned` to silence this check",
                 c->name, c->acquire_fn, exit_kind, c->name);
      else
        warning (ctx->c2m_ctx, c->acquire_pos,
                 "leak: `%s` allocated by `%s()` is still owned at %s but "
                 "never `%s()`d, returned, or stored elsewhere\n"
                 "  hint: add `defer %s(%s);` right after this declaration, "
                 "or mark the binding `unowned` to silence this check",
                 c->name, c->acquire_fn, exit_kind, c->release_fn,
                 c->release_fn, c->name);
      diag_mark_warned (ctx, c);
    } else if (c->state == OS_MAYBE_OWNED) {
      if (delete_p)
        warning (ctx->c2m_ctx, c->acquire_pos,
                 literal_new_p
                   ? "potential leak: `%s` allocated by `%s T(...)` may be owned on "
                     "some path reaching %s (some branches `delete` or escape it, "
                     "others do not)\n"
                     "  hint: add `delete %s;` on the branch that misses it, or mark "
                     "the binding `unowned`"
                   : "potential leak: `%s` allocated by `%s(...)` may be owned on "
                     "some path reaching %s (some branches `delete` or escape it, "
                     "others do not)\n"
                     "  hint: add `delete %s;` on the branch that misses it, or mark "
                     "the binding `unowned`",
                 c->name, c->acquire_fn, exit_kind, c->name);
      else
        warning (ctx->c2m_ctx, c->acquire_pos,
                 "potential leak: `%s` allocated by `%s()` may be owned on "
                 "some path reaching %s (some branches `%s()` or escape it, "
                 "others do not)\n"
                 "  hint: add `%s(%s);` on the branch that misses it, or mark "
                 "the binding `unowned`",
                 c->name, c->acquire_fn, exit_kind, c->release_fn,
                 c->release_fn, c->name);
      diag_mark_warned (ctx, c);
    }
  }
}

/* ────────────────────────────────────────────────────────────────────────
 * Function-parameter attribute handling (Step H).
 *
 * Walks the callee's declarator to find its N_FUNC node and per-position
 * parameter SPEC_DECLs, then classifies each parameter by the attributes
 * the user attached to it.  Recognises three names today, all standard C
 * extension syntax that GCC/Clang accept (they emit a -Wattributes warning
 * for the unknown ones but still compile):
 *
 *   __attribute__((borrows))   The function reads the pointer but does not
 *                              retain or release it.  Equivalent to adding
 *                              the callee to the non-retaining table on a
 *                              per-arg basis.
 *   __attribute__((releases))  The function takes ownership of the pointer
 *                              and frees it.  Caller's binding goes Released.
 *   __attribute__((acquires))  (Reserved.  Future: function returns a
 *                              freshly-acquired pointer.)
 *
 * Direct function calls only — the callee N_ID must resolve to an N_FUNC_DEF
 * or to an N_SPEC_DECL prototype.  Indirect calls via function-pointer
 * parameters / locals are not yet annotated; those still fall through to
 * the conservative-escape default.  Annotating function-pointer parameter
 * types is the natural extension and the obvious next step. */

typedef enum {
  PA_DEFAULT  = 0,   /* unannotated — conservative escape */
  PA_BORROWS,        /* does not retain */
  PA_RELEASES,       /* releases the arg's pointer */
} param_attr_t;

/* ────────────────────────────────────────────────────────────────────────
 * Interprocedural function summaries.
 *
 * One entry per analyzed function definition in this TU.  The summary is
 * computed at the end of each per-function dataflow pass by inspecting
 * the final state of param-typed candidates: a parameter that's
 * RELEASED on every reachable path infers ((releases)); one that stays
 * OWNED everywhere infers ((borrows)); anything else stays PA_DEFAULT.
 *
 * Summaries are consumed by transfer_call when a call's matching
 * parameter has no explicit attribute, giving us free-equivalent
 * recognition of user-written wrappers without annotation.
 *
 * Iteration: ownership_run() walks the module repeatedly, capping at
 * OWNERSHIP_MAX_PASSES.  We stop early when no summary changes between
 * passes; only the final pass emits diagnostics.
 */
#define OWNERSHIP_MAX_PASSES 4

typedef struct func_summary {
  node_t        func_def;        /* identity key */
  size_t        param_count;
  param_attr_t *param_attrs;     /* owned; reg_malloc'd per slot */
  int           returns_owned_p; /* function returns ownership of a fresh resource */
  /* When returns_owned_p, the matching release_fn name ("free" or
     "delete") so caller-side acquisition picks the right diagnostic and
     auto-release synthesis form. */
  const char   *returns_release_fn;
} func_summary_t;
DEF_VARR (func_summary_t);

/* Module-scope summary storage.  Owned by ownership_run() across iterations
   so transfer_call can consult summaries built on previous passes.  Reset
   at the start of each TU. */
static VARR (func_summary_t) *func_summaries = NULL;
static int   summaries_dirty_p   = 0;   /* set when any summary changed this pass */
static int   summaries_pass_idx  = 0;   /* current iteration (0 = first pass) */

/* ──────────────────────────────────────────────────────────────────────
 * Call-graph worklist registry.
 *
 * One record per function definition (plus records for any prototype whose
 * summary was queried).  While `analyze_function` runs for a function F,
 * `ow_curr_idx` points at F's record and every interprocedural summary
 * query records a dependency edge "F reads G's summary".  summary_lookup
 * is the single funnel for those queries (summary_param_attr,
 * return_expr_is_acquire_p and summary_release_for_init all go through
 * it), so the recorded edges are exactly the dynamic dependency frontier.
 *
 * When G's summary later changes, precisely the recorded dependents are
 * pushed on a worklist and re-analyzed — replacing the old fixed-count
 * whole-module re-walks with a classic fixpoint iteration.  The registry
 * also caches each function's "trivial" verdict (the no-candidate early
 * exit in analyze_function) so the final diagnostics pass can skip
 * functions that provably emit nothing.
 * ────────────────────────────────────────────────────────────────────── */
typedef struct ow_funcinfo {
  node_t func_def;        /* key: N_FUNC_DEF (or a queried N_SPEC_DECL proto) */
  VARR (int) *dependents; /* ow_funcs indices of funcs that queried our summary */
  size_t summary_idx_p1;  /* 1-based index into func_summaries; 0 = none yet */
  unsigned char on_worklist_p;
  unsigned char trivial_p; /* last analysis took the no-candidate early exit */
  unsigned char analyzed_p;
} ow_funcinfo_t;
DEF_VARR (ow_funcinfo_t);

typedef struct ow_fidx {
  node_t def;
  size_t idx;
} ow_fidx_t;
DEF_HTAB (ow_fidx_t);

static VARR (ow_funcinfo_t) *ow_funcs = NULL;
static VARR (int) *ow_worklist = NULL;
static HTAB (ow_fidx_t) *ow_fidx_tab = NULL;
static MIR_alloc_t ow_alloc;               /* captured by ownership_run */
static size_t ow_curr_idx = (size_t) -1;   /* record of the func being analyzed */
static int ow_inference_p = 0;             /* 1 = silent inference (track + enqueue) */
static int ow_last_install_changed_p = 0;  /* summary changed during curr analysis */
static int ow_last_trivial_p = 0;          /* curr analysis took the trivial exit */

static htab_hash_t ow_fidx_hash (ow_fidx_t e, void *arg MIR_UNUSED) {
  return (htab_hash_t) mir_hash_finish (mir_hash_step (mir_hash_init (0x51), (uint64_t) e.def));
}
static int ow_fidx_eq (ow_fidx_t e1, ow_fidx_t e2, void *arg MIR_UNUSED) {
  return e1.def == e2.def;
}

/* Get-or-create the registry record for DEF; returns its ow_funcs index. */
static size_t ow_func_idx (node_t def) {
  ow_fidx_t el, key;

  key.def = def;
  if (HTAB_DO (ow_fidx_t, ow_fidx_tab, key, HTAB_FIND, el)) return el.idx;
  {
    ow_funcinfo_t fi;

    memset (&fi, 0, sizeof (fi));
    fi.func_def = def;
    VARR_CREATE (int, fi.dependents, ow_alloc, 4);
    key.idx = VARR_LENGTH (ow_funcinfo_t, ow_funcs);
    VARR_PUSH (ow_funcinfo_t, ow_funcs, fi);
    HTAB_DO (ow_fidx_t, ow_fidx_tab, key, HTAB_INSERT, el);
    return key.idx;
  }
}

/* Record "the function currently being analyzed reads DEF_IDX's summary".
   Deduped linearly — dependent lists are short in practice. */
static void ow_record_dep (size_t def_idx) {
  ow_funcinfo_t *fi;
  int me;

  if (ow_curr_idx == (size_t) -1) return;
  fi = &VARR_ADDR (ow_funcinfo_t, ow_funcs)[def_idx];
  me = (int) ow_curr_idx;
  for (size_t i = VARR_LENGTH (int, fi->dependents); i-- > 0;)
    if (VARR_GET (int, fi->dependents, i) == me) return;
  VARR_PUSH (int, fi->dependents, me);
}

/* IDX's summary changed: queue every already-analyzed dependent for
   re-analysis (unanalyzed ones will be reached by the ongoing walk). */
static void ow_enqueue_dependents (size_t idx) {
  VARR (int) *deps = VARR_ADDR (ow_funcinfo_t, ow_funcs)[idx].dependents;

  for (size_t i = 0; i < VARR_LENGTH (int, deps); i++) {
    int d = VARR_GET (int, deps, i);
    ow_funcinfo_t *dep = &VARR_ADDR (ow_funcinfo_t, ow_funcs)[d];

    if (dep->on_worklist_p || !dep->analyzed_p) continue;
    if (dep->func_def == NULL || dep->func_def->code != N_FUNC_DEF) continue;
    dep->on_worklist_p = 1;
    VARR_PUSH (int, ow_worklist, d);
  }
}

/* Find a summary by function-def identity (O(1) via the registry).
   Returns NULL if not yet computed.  No dependency recording — used by
   summary_install itself; analysis-side queries go through summary_lookup
   below. */
static func_summary_t *summary_lookup_raw (node_t func_def) {
  ow_fidx_t el, key;
  size_t sidx;

  if (func_summaries == NULL || func_def == NULL || ow_fidx_tab == NULL) return NULL;
  key.def = func_def;
  if (!HTAB_DO (ow_fidx_t, ow_fidx_tab, key, HTAB_FIND, el)) return NULL;
  sidx = VARR_GET (ow_funcinfo_t, ow_funcs, el.idx).summary_idx_p1;
  if (sidx == 0) return NULL;
  return &VARR_ADDR (func_summary_t, func_summaries)[sidx - 1];
}

/* Analysis-side summary query: also records the call-graph dependency so a
   later change to FUNC_DEF's summary re-queues exactly the affected
   callers (including queries that return NULL now but gain a summary
   later, and self-queries of recursive functions). */
static func_summary_t *summary_lookup (node_t func_def) {
  if (func_summaries == NULL || func_def == NULL || ow_fidx_tab == NULL) return NULL;
  if (ow_curr_idx != (size_t) -1) ow_record_dep (ow_func_idx (func_def));
  return summary_lookup_raw (func_def);
}

/* Install (or update in place) a summary for func_def.  Marks
   summaries_dirty_p when the stored value differs from the previous one
   so ownership_run knows to keep iterating. */
static void summary_install (c2m_ctx_t c2m_ctx, node_t func_def,
                             size_t param_count,
                             const param_attr_t *param_attrs,
                             int returns_owned_p,
                             const char *returns_release_fn) {
  func_summary_t *s = summary_lookup_raw (func_def);
  int changed_p = 0;
  if (s == NULL) {
    func_summary_t fresh;
    fresh.func_def           = func_def;
    fresh.param_count        = param_count;
    fresh.param_attrs        = param_count == 0 ? NULL
      : reg_malloc (c2m_ctx, param_count * sizeof (param_attr_t));
    for (size_t i = 0; i < param_count; i++) fresh.param_attrs[i] = param_attrs[i];
    fresh.returns_owned_p    = returns_owned_p;
    fresh.returns_release_fn = returns_release_fn;
    VARR_PUSH (func_summary_t, func_summaries, fresh);
    VARR_ADDR (ow_funcinfo_t, ow_funcs)[ow_func_idx (func_def)].summary_idx_p1
      = VARR_LENGTH (func_summary_t, func_summaries);
    changed_p = 1;
  } else {
    if (s->returns_owned_p != returns_owned_p) changed_p = 1;
    s->returns_owned_p = returns_owned_p;
    if (s->returns_release_fn != returns_release_fn) {
      if (s->returns_release_fn == NULL || returns_release_fn == NULL
          || strcmp (s->returns_release_fn, returns_release_fn) != 0)
        changed_p = 1;
      s->returns_release_fn = returns_release_fn;
    }
    for (size_t i = 0; i < param_count && i < s->param_count; i++) {
      if (s->param_attrs[i] != param_attrs[i]) {
        changed_p = 1; s->param_attrs[i] = param_attrs[i];
      }
    }
  }
  if (changed_p) {
    summaries_dirty_p = 1;
    ow_last_install_changed_p = 1;
  }
}

/* Read inferred attribute for a callee parameter; PA_DEFAULT if no summary. */
static param_attr_t summary_param_attr (node_t callee_def, size_t pos) {
  func_summary_t *s = summary_lookup (callee_def);
  if (s == NULL || pos >= s->param_count) return PA_DEFAULT;
  return s->param_attrs[pos];
}

/* Return TRUE and (via out_release_fn) the matching release form if `expr`
   is an acquire shape:  N_NEW (anywhere), an N_CALL to a known acquire fn,
   or an N_CALL to a function with returns_owned_p summary.  Peels
   `(T)<expr>` casts and `detach <expr>` wrappers.  Used by transfer_return
   so thin wrappers like  `Box* spawn(int v) { return detach new Box(v); }`
   get returns_owned_p inferred even though there's no intermediate
   binding to drive the candidate-based path. */
static int return_expr_is_acquire_p (node_t expr, const char **out_release_fn) {
  if (out_release_fn != NULL) *out_release_fn = NULL;
  /* Peel any number of casts and a leading detach. */
  for (;;) {
    if (expr == NULL) return 0;
    if (expr->code == N_CAST) { expr = NL_EL (expr->u.ops, 1); continue; }
    if (expr->code == N_DETACH) { expr = NL_HEAD (expr->u.ops); continue; }
    break;
  }
  if (expr->code == N_NEW) {
    if (out_release_fn != NULL) *out_release_fn = "delete";
    return 1;
  }
  if (expr->code == N_CALL) {
    node_t callee = NL_HEAD (expr->u.ops);
    const char *cname = callee_name_of (callee);
    if (cname != NULL && callee != NULL && callee->code == N_ID) {
      /* Hard-coded acquire table mirror.  Keep in sync with
         release_fn_for_acquire.  Only meaningful for free-function names. */
      const char *r = NULL;
      if (strcmp (cname, "malloc")  == 0) r = "free";
      else if (strcmp (cname, "calloc")  == 0) r = "free";
      else if (strcmp (cname, "realloc") == 0) r = "free";
      else if (strcmp (cname, "strdup")  == 0) r = "free";
      else if (strcmp (cname, "strndup") == 0) r = "free";
      if (r != NULL) { if (out_release_fn != NULL) *out_release_fn = r; return 1; }
    }
    /* IP-summary lookup — works for both `wrap(...)` and `obj->method(...)`. */
    node_t def = callee_def_node (callee);
    if (def != NULL) {
      func_summary_t *s = summary_lookup (def);
      if (s != NULL && s->returns_owned_p) {
        if (out_release_fn != NULL)
          *out_release_fn = s->returns_release_fn != NULL ? s->returns_release_fn : "free";
        return 1;
      }
    }
  }
  return 0;
}

/* IP variant of acquire detection: resolve an N_CALL init through summary
   lookup.  Returns the release_fn name ("free" / "delete") when the
   callee's summary says it returns owned, or NULL otherwise.  Used by
   collect_candidates and transfer_assign to recognize wrapper-style
   acquires like
     auto p = make_buf(64);   // make_buf has summary.returns_owned_p
   without needing the callee in the hard-coded acquire table. */
static const char *summary_release_for_init (node_t init) {
  while (init != NULL && init->code == N_CAST)
    init = NL_EL (init->u.ops, 1);
  if (init == NULL || init->code != N_CALL) return NULL;
  node_t callee = NL_HEAD (init->u.ops);
  /* Resolve free-function *and* method callees (`obj->query(...)`,
     `Class.open(...)`) to their def so the IP summary applies to both. */
  node_t def = callee_def_node (callee);
  if (def == NULL) return NULL;
  func_summary_t *s = summary_lookup (def);
  if (s == NULL || !s->returns_owned_p) return NULL;
  /* Default to "free" if we somehow lost the release fn (shouldn't happen). */
  return s->returns_release_fn != NULL ? s->returns_release_fn : "free";
}

/* Locate the N_FUNC node inside a function definition's or prototype's
 * declarator.  Returns NULL if the input isn't a function declaration. */
static node_t func_node_of (node_t func_def_or_decl) {
  node_t decl_node = NULL;
  node_t decl_list;
  if (func_def_or_decl == NULL) return NULL;
  if (func_def_or_decl->code == N_FUNC_DEF)
    decl_node = FUNC_DEF_DECL (func_def_or_decl);
  else if (func_def_or_decl->code == N_SPEC_DECL)
    decl_node = SPEC_DECL_DECL (func_def_or_decl);
  else
    return NULL;
  if (decl_node == NULL || decl_node->code != N_DECL) return NULL;
  /* The declarator's operand 1 is an N_LIST of type modifiers (N_POINTER,
     N_FUNC, N_ARR).  Find the N_FUNC. */
  decl_list = NL_NEXT (NL_HEAD (decl_node->u.ops));
  if (decl_list == NULL || decl_list->code != N_LIST) return NULL;
  for (node_t n = NL_HEAD (decl_list->u.ops); n != NULL; n = NL_NEXT (n))
    if (n->code == N_FUNC) return n;
  return NULL;
}

/* Get the parameter at position i (0-based) in a function's parameter
 * list.  Returns the parameter's SPEC_DECL (or N_TYPE for unnamed-type-only
 * params), or NULL if the callee has no such position or hits varargs. */
static node_t param_at_pos (node_t func_def_or_decl, size_t i) {
  node_t fn = func_node_of (func_def_or_decl);
  node_t params;
  size_t idx = 0;
  if (fn == NULL) return NULL;
  params = NL_HEAD (fn->u.ops);
  if (params == NULL || params->code != N_LIST) return NULL;
  for (node_t p = NL_HEAD (params->u.ops); p != NULL; p = NL_NEXT (p)) {
    if (p->code == N_DOTS) return NULL; /* varargs region; no per-position attr */
    if (idx == i) return p;
    idx++;
  }
  return NULL;
}

/* Classify a parameter node by its recognised attribute, if any. */
static param_attr_t classify_param_attr (node_t param) {
  node_t attrs;
  if (param == NULL || param->code != N_SPEC_DECL) return PA_DEFAULT;
  attrs = SPEC_DECL_ATTRS (param);
  if (attrs == NULL || attrs->code != N_LIST) return PA_DEFAULT;
  for (node_t a = NL_HEAD (attrs->u.ops); a != NULL; a = NL_NEXT (a)) {
    node_t name;
    if (a->code != N_ATTR) continue;
    name = NL_HEAD (a->u.ops);
    if (name == NULL || name->code != N_ID || name->u.s.s == NULL) continue;
    if (strcmp (name->u.s.s, "borrows")  == 0) return PA_BORROWS;
    if (strcmp (name->u.s.s, "releases") == 0) return PA_RELEASES;
  }
  return PA_DEFAULT;
}

/* Resolve a call's callee N_ID to its declaring N_FUNC_DEF or N_SPEC_DECL
 * prototype.  Returns NULL for indirect calls (function-pointer expressions)
 * and for anything we can't statically resolve. */
static node_t callee_def_of (node_t callee) {
  node_t def = callee_def_node (callee);
  if (def == NULL) return NULL;
  if (def->code == N_FUNC_DEF) return def;
  if (def->code == N_SPEC_DECL) return def;
  return NULL;
}

/* ────────────────────────────────────────────────────────────────────────
 * Transfer functions and the structured-flow analyser.
 *
 * We do structured flow over the AST rather than a fully-flattened CFG.
 * c2mir's source language doesn't use `goto` heavily and the structured
 * approach keeps the implementation small.  Each control construct splits
 * the state, recurses with `analyze`, and meets results.  Loops are
 * handled with a conservative single-iteration widening (see
 * `state_widen_owned_to_maybe`) until the worklist-style fixpoint lands.
 * ──────────────────────────────────────────────────────────────────────── */

static void analyze (flowctx_t *ctx, node_t n);

/* Strip leading C casts from an expression.  Used to recognise idioms like
 * `(char *) tf` as still being a direct identifier reference to `tf`. */
static node_t peel_casts (node_t n) {
  while (n != NULL && n->code == N_CAST) n = NL_EL (n->u.ops, 1);
  return n;
}

/* Transfer: an assignment `lhs = rhs`.
 *
 * Two distinct ownership effects to consider:
 *
 * 1. RHS-side escape.  The RHS escapes a candidate's POINTER only when the
 *    RHS expression *evaluates to* that pointer — i.e. it's a direct N_ID
 *    reference (possibly wrapped in casts).  A subscript `tf[i]` reads an
 *    int through `tf`; arithmetic `p + 1` reads a derived pointer; neither
 *    of those is an ownership transfer of the original pointer, so we must
 *    NOT trip the escape transition just because the candidate's name
 *    *appears* somewhere in the RHS.  The previous version of this check
 *    used `subtree_mentions_decl` and falsely flagged read-modify-write
 *    patterns like `tf[docId] = tf[docId] + 1` as escapes.
 *
 * 2. LHS-side acquisition.  If the LHS is a tracked binding and the RHS is
 *    a recognised acquire call (malloc/calloc/strdup/...), this is a
 *    re-acquisition: state becomes Owned.  Otherwise, if the binding was
 *    already Owned, it's been overwritten without an intervening release
 *    — widen to MaybeOwned so a later release in either direction is
 *    flagged. */
static void transfer_assign (flowctx_t *ctx, node_t assign) {
  c2m_ctx_t c2m_ctx = ctx->c2m_ctx; /* for POS() */
  (void) c2m_ctx;
  node_t lhs = NL_HEAD (assign->u.ops);
  node_t rhs = lhs != NULL ? NL_NEXT (lhs) : NULL;
  int lhs_idx = cand_lookup (ctx, id_resolves_to_decl (lhs));
  candidate_t *a = VARR_ADDR (candidate_t, ctx->cands);

  /* Compute the RHS's direct candidate (if any) once for use below.  We
     peel leading C casts so `(char *) tmp` and `tmp` are treated alike. */
  node_t rhs_peeled = (rhs != NULL) ? peel_casts (rhs) : NULL;
  int rhs_idx = -1;
  if (rhs_peeled != NULL && rhs_peeled->code == N_ID)
    rhs_idx = cand_lookup (ctx, id_resolves_to_decl (rhs_peeled));

  /* --- Case A: LHS is tracked, RHS is a recognised acquire call ---
     Re-acquisition: lhs becomes Owned.  Whatever it was before is
     overwritten; if it was still Owned that's a real leak the user
     should see at scope-end (but we don't double-warn here). */
  if (lhs_idx >= 0) {
    const char *acq = acquire_fn_name_of_init (rhs);
    if (acq != NULL && release_fn_for_acquire (acq) != NULL) {
      a[lhs_idx].state = OS_OWNED;
      a[lhs_idx].da_state = DA_INIT; /* writing a freshly acquired value */
      return;
    }
    /* IP path: x = make_buf(...) where make_buf has summary.returns_owned_p. */
    if (summary_release_for_init (rhs) != NULL) {
      a[lhs_idx].state = OS_OWNED;
      a[lhs_idx].da_state = DA_INIT;
      return;
    }
    /* Any other write to a tracked binding establishes definite assignment. */
    a[lhs_idx].da_state = DA_INIT;
  }

  /* --- Case B: Both sides are tracked candidates and RHS is a direct
     N_ID copy (`lhs = rhs;` with rhs another tracked binding) ---
     This is the canonical ownership-transfer pattern — e.g.
         char *tmp = realloc(buf, cap);
         if (tmp) buf = tmp;
     The `buf = tmp` assignment moves Owned from tmp to buf so we know
     the new buffer is the live one and the old name (tmp) is gone. */
  if (lhs_idx >= 0 && rhs_idx >= 0 && rhs_idx != lhs_idx) {
    owstate_t rs = a[rhs_idx].state;
    if (rs == OS_OWNED) {
      a[lhs_idx].state = OS_OWNED;
      a[rhs_idx].state = OS_DETACHED;
      a[rhs_idx].escapes_p = 1;
      a[rhs_idx].unsafe_escape_p = 1; /* plain `=` alias is not a nulling move */
      record_disposition (ctx, &a[rhs_idx], POS (assign), "moved to another binding");
    } else if (rs == OS_MAYBE_OWNED) {
      a[lhs_idx].state = OS_MAYBE_OWNED;
      a[rhs_idx].state = OS_DETACHED;
      a[rhs_idx].escapes_p = 1;
      a[rhs_idx].unsafe_escape_p = 1;
      record_disposition (ctx, &a[rhs_idx], POS (assign), "moved to another binding");
    } else if (rs == OS_DETACHED) {
      /* Copying a dead pointer.  lhs inherits the disposed state. */
      a[lhs_idx].state = OS_DETACHED;
    } else if (rs == OS_RELEASED) {
      /* Reading a freed pointer — UAF already emitted by the N_ID case
         during recursion.  Inherit Released so a later release sees it. */
      a[lhs_idx].state = OS_RELEASED;
    } else {
      /* Unowned: lhs is just receiving a non-tracked pointer value.
         If lhs was Owned, it's been overwritten without a free. */
      if (a[lhs_idx].state == OS_OWNED) a[lhs_idx].state = OS_MAYBE_OWNED;
    }
    return;
  }

  /* --- Case C: RHS is a direct tracked N_ID being stored into something
     non-tracked (e.g. a global, a struct field, a non-candidate local) ---
     The pointer's escaped into a wider scope; treat as Detached. */
  if (rhs_idx >= 0
      && (a[rhs_idx].state == OS_OWNED || a[rhs_idx].state == OS_MAYBE_OWNED)) {
    a[rhs_idx].state = OS_DETACHED;
    a[rhs_idx].escapes_p = 1;
    a[rhs_idx].unsafe_escape_p = 1;
    record_disposition (ctx, &a[rhs_idx], POS (assign), "stored into non-tracked location");
    return;
  }

  /* --- Case D: LHS is tracked, RHS is some other expression
     (function call we don't recognise, arithmetic, etc.) ---
     The previous Owned value is overwritten by an unknown one.  Widen to
     MaybeOwned so a subsequent release picks it up either way. */
  if (lhs_idx >= 0 && a[lhs_idx].state == OS_OWNED) {
    a[lhs_idx].state = OS_MAYBE_OWNED;
  }
}

/* Transfer: a function call.
 *
 * Applies the call's *direct-arg* effects on each tracked candidate:
 *   - direct N_ID arg == candidate, callee == candidate's release_fn
 *       -> Released  (double-free / double-free-risk diagnostic if state was
 *                     already Released or Detached)
 *   - direct N_ID arg == candidate, callee is non-retaining
 *       -> state unchanged (just a use)  (UAF if state was Released)
 *   - direct N_ID arg == candidate, any other callee
 *       -> Owned becomes Detached        (UAF if state was Released)
 *   - candidate appears DEEP in a complex arg (cast, subscript, nested call)
 *       -> conservative escape: Owned becomes Detached.
 *          UAF and other diagnostics for the deep N_ID are emitted by the
 *          general N_ID transfer once `analyze` recurses into the arg.
 *
 * The N_CALL case in `analyze` invokes this BEFORE recursing into operands,
 * so the call's release / double-free diagnostic always fires before any
 * generic N_ID UAF emission for the same identifier. */
static void transfer_call (flowctx_t *ctx, node_t call) {
  c2m_ctx_t c2m_ctx = ctx->c2m_ctx; /* for POS() macro hygiene */
  (void) c2m_ctx;
  node_t callee = NL_HEAD (call->u.ops);
  node_t arg_list = callee != NULL ? NL_NEXT (callee) : NULL;
  const char *callee_name = NULL;

  if (callee != NULL && callee->code == N_ID) callee_name = callee->u.s.s;

  if (arg_list == NULL || arg_list->code != N_LIST) return;

  candidate_t *a = VARR_ADDR (candidate_t, ctx->cands);
  size_t n = VARR_LENGTH (candidate_t, ctx->cands);
  int non_retaining = is_non_retaining_consumer (callee_name);
  /* Resolve the callee's declaration (if any) so we can look up per-arg
     attribute annotations.  Indirect calls and unresolved identifiers
     return NULL and fall back to the default conservative behaviour. */
  node_t callee_def = callee_def_of (callee);
  size_t arg_pos = 0;

  for (node_t arg = NL_HEAD (arg_list->u.ops); arg != NULL; arg = NL_NEXT (arg), arg_pos++) {
    node_t direct_decl = id_resolves_to_decl (arg);
    int direct_idx = cand_lookup (ctx, direct_decl);
    /* Look up the matching parameter's attribute (PA_BORROWS / PA_RELEASES
       / PA_DEFAULT) so direct-arg transitions can be refined per position.
       Explicit user attribute wins; otherwise fall back to the inferred
       summary from the interprocedural pass (PA_DEFAULT if neither). */
    param_attr_t pa = PA_DEFAULT;
    if (callee_def != NULL) {
      pa = classify_param_attr (param_at_pos (callee_def, arg_pos));
      if (pa == PA_DEFAULT)
        pa = summary_param_attr (callee_def, arg_pos);
    }
    /* Method-call special case: the implicit `this` receiver of a class
       method is borrowed by default — methods don't free their receiver
       (Foo::bar(this, ...) is a use, not a transfer).  Detect by looking
       at the callee shape: an N_DEREF_FIELD callee with `this` as the
       first arg is the method-call form.  Without this carve-out every
       `obj->method()` would mark `obj` as escaped, giving false-positive
       leaks and double-free errors at the eventual `delete obj`. */
    if (arg_pos == 0 && pa == PA_DEFAULT && callee != NULL
        && (callee->code == N_DEREF_FIELD || callee->code == N_FIELD)) {
      node_t recv_id = NL_HEAD (callee->u.ops);
      /* The check pass generates two distinct N_ID nodes for the receiver
         and the implicit-this arg, so direct node identity won't match.
         Compare via id_resolves_to_decl, which looks up each id's symbol
         and returns the SPEC_DECL it references. */
      node_t recv_decl = id_resolves_to_decl (recv_id);
      node_t arg_decl  = id_resolves_to_decl (arg);
      if (recv_decl != NULL && recv_decl == arg_decl)
        pa = PA_BORROWS;
    }

    if (direct_idx >= 0) {
      /* Direct N_ID argument referring to a tracked candidate.
         Authoritative path for release / double-free / direct UAF. */
      candidate_t *c = &a[direct_idx];
      int is_release_call = (callee_name != NULL
                             && c->release_fn != NULL
                             && strcmp (callee_name, c->release_fn) == 0)
                            || pa == PA_RELEASES;

      /* realloc(p, n) is the universal "ambiguous" call: it either frees
         p and returns a new pointer, or leaves p valid and returns NULL.
         Without per-branch flow refinement the safest behaviour is to
         leave p's state alone here — the user's null-check on the return
         value drives correctness, and our Case B in transfer_assign moves
         ownership when they do `buf = tmp;` after realloc.
         This special case has to come *before* the `is_release_call`
         branch (release_fn for `buf` is `free`, and we don't want to
         mistakenly transition Owned -> Detached on the realloc call). */
      if (callee_name != NULL && strcmp (callee_name, "realloc") == 0
          && c->state == OS_OWNED) {
        continue;
      }
      if (is_release_call) {
        if (c->state == OS_RELEASED && !c->warned_p && diag_active_p (ctx)) {
          error (ctx->c2m_ctx, POS (call),
                 "double-free: `%s` was already `%s()`d on this path",
                 c->name, c->release_fn);
          diag_mark_warned (ctx, c);
        } else if (c->state == OS_DETACHED && !c->warned_p && diag_active_p (ctx)) {
          error (ctx->c2m_ctx, POS (call),
                 "double-free risk: `%s` was already escaped (detach/return/"
                 "store) on this path; freeing it again is undefined",
                 c->name);
          diag_mark_warned (ctx, c);
        } else if (c->state == OS_MAYBE_OWNED && !c->warned_p && diag_active_p (ctx)) {
          /* Path-sensitive double-free: some incoming control-flow path
             already released this binding (loop second iteration, an
             earlier `if (cond) free(p);` etc.).  Releasing here may
             double-free.  Warning rather than error since the user might
             know the paths are mutually exclusive in a way the analyser
             can't model yet (Step G: path-condition narrowing). */
          warning (ctx->c2m_ctx, POS (call),
                   "double-free risk: `%s` may already be `%s()`d on some "
                   "path reaching this call (loop back-edge or earlier "
                   "branch); mark `unowned` if the paths are mutually exclusive",
                   c->name, c->release_fn);
          diag_mark_warned (ctx, c);
        }
        c->state = OS_RELEASED;
        c->released_p = 1; /* user released it here (sticky) */
        record_disposition (ctx, c, POS (call), "freed by release fn");
      } else {
        /* Non-release call carrying the candidate by N_ID. */
        if (c->state == OS_RELEASED && !c->warned_p && diag_active_p (ctx)) {
          error (ctx->c2m_ctx, POS (arg),
                 "use-after-free: `%s` was released earlier on this path",
                 c->name);
          diag_mark_warned (ctx, c);
        } else if (c->state == OS_MOVED && !c->warned_p && diag_active_p (ctx)) {
          error (ctx->c2m_ctx, POS (arg),
                 "use of moved value: `%s` was consumed by `move` on this path "
                 "and is dead; use the new owner instead", c->name);
          diag_mark_warned (ctx, c);
        }
        /* `__attribute__((borrows))` on the matching parameter is the
           per-arg form of the non-retaining-consumer table.  When it's
           set we skip the conservative-escape transition. */
        int param_is_borrowing = (pa == PA_BORROWS);
        if (!non_retaining && !param_is_borrowing && c->state == OS_OWNED) {
          c->state = OS_DETACHED;
          c->escapes_p = 1;
          c->unsafe_escape_p = 1;
          record_disposition (ctx, c, POS (call), "escaped via call");
        }
      }
    } else {
      /* Complex expression argument (cast peeled to non-id, subscript,
         deref, nested call, etc.).

         We deliberately do NOT escape candidates just because they appear
         somewhere in the arg subtree.  That over-approximation caused
         false-positive double-free / leak warnings on idiomatic code like
             hits->Add(new Hit(d, tf[d]));            // reads tf[d] (int)
             cb(p->next);                              // passes p->next
             memcpy(dst, src, n);                      // reads through src
         where the pointer being passed is something *derived* from the
         candidate (an int element, a field, an address-of-sub-expression)
         rather than the candidate's pointer itself.

         Nested calls inside the arg (`free(get(p))`, `f(g(p))`) are still
         analysed correctly: `analyze` recurses into the arg below, and
         each inner N_CALL applies its own direct-arg effects.  Direct
         pointer args (with optional casts) are caught by the `direct_idx`
         branch above. */
      (void) a;
      (void) n;
    }
  }
}

/* Mark the candidate(s) a `return <expr>;` actually ships to the caller.
 * Only the expression's *value* escapes: a direct binding reference (seen
 * through casts, a leading `detach`, or either arm of a ?:), NOT a binding
 * that merely appears as a method receiver or call argument somewhere in
 * the expression — its pointer isn't what's returned.  This is the same
 * value-vs-mention distinction transfer_assign / transfer_call already
 * make; using it here avoids falsely escaping `return resp_ok(rows->Get(0))`
 * (where `rows` is just the receiver and must stay owned so the function's
 * other return paths get their leak fixed / auto-released). */
static void mark_return_value_escape (flowctx_t *ctx, node_t expr) {
  c2m_ctx_t c2m_ctx = ctx->c2m_ctx; /* for POS() */
  candidate_t *a = VARR_ADDR (candidate_t, ctx->cands);
  size_t n = VARR_LENGTH (candidate_t, ctx->cands);
  (void) c2m_ctx;
  /* Peel value-preserving wrappers: casts and a leading `detach`. */
  while (expr != NULL && (expr->code == N_CAST || expr->code == N_DETACH))
    expr = (expr->code == N_CAST) ? NL_EL (expr->u.ops, 1) : NL_HEAD (expr->u.ops);
  if (expr == NULL) return;
  if (expr->code == N_COND) {
    /* `c ? t : f` ships whichever arm is taken; both are value positions. */
    mark_return_value_escape (ctx, NL_EL (expr->u.ops, 1));
    mark_return_value_escape (ctx, NL_EL (expr->u.ops, 2));
    return;
  }
  if (expr->code != N_ID) return;
  node_t decl = id_resolves_to_decl (expr);
  for (size_t i = 0; i < n; i++)
    if (a[i].decl == decl
        && (a[i].state == OS_OWNED || a[i].state == OS_MAYBE_OWNED)) {
      a[i].state = OS_DETACHED;
      a[i].escapes_p = 1;
      a[i].unsafe_escape_p = 1; /* returned value escapes; delete would UAF */
      a[i].returned_p = 1; /* shipped to caller on this path (sticky) */
      record_disposition (ctx, &a[i], POS (expr), "returned to caller");
    }
}

/* Transfer: a `return <expr>;`. */
static void transfer_return (flowctx_t *ctx, node_t ret) {
  c2m_ctx_t c2m_ctx = ctx->c2m_ctx; /* for POS() macro hygiene */
  (void) c2m_ctx;
  node_t expr = NL_EL (ret->u.ops, 1);

  if (expr != NULL && expr->code != N_IGNORE) {
    /* First, recurse into the return expression so any call effects /
       use-after-frees inside it fire properly. */
    analyze (ctx, expr);
    /* Then mark only the binding whose *value* is returned as escaped. */
    mark_return_value_escape (ctx, expr);
    /* Inline acquire-shape detection: even when there's no candidate to
       track, `return new T(...)` / `return detach new T(...)` / `return
       malloc(n)` / `return some_acquire_wrapper(...)` ships ownership
       to the caller, so the function's summary should reflect that.
       Record on the flowctx for summary derivation at function-end. */
    const char *inline_release = NULL;
    if (return_expr_is_acquire_p (expr, &inline_release) && inline_release != NULL) {
      ctx->returns_owned_inline_p = 1;
      if (ctx->returns_release_fn_inline == NULL)
        ctx->returns_release_fn_inline = inline_release;
    }
  }

  /* Check what's still Owned/MaybeOwned at this exit point. */
  check_exit_state (ctx, POS (ret), "this return");
  ctx->dead_p = 1;
}

/* Transfer: `delete <expr>;` — a release for `new`-allocated bindings.
 * Currently we don't track `new` in v1's candidate set; the call exists so
 * the analysis is symmetric and ready when we add it.  Recurses for UAF
 * checks. */
static void transfer_delete (flowctx_t *ctx, node_t del) {
  c2m_ctx_t c2m_ctx = ctx->c2m_ctx; /* for POS() macro hygiene */
  (void) c2m_ctx;
  node_t expr = NL_EL (del->u.ops, 1);
  candidate_t *a = VARR_ADDR (candidate_t, ctx->cands);
  size_t n = VARR_LENGTH (candidate_t, ctx->cands);
  if (expr == NULL) return;
  /* Only release a candidate whose decl is the *direct* operand of delete
     (after peeling casts).  Sub-expression mentions don't count: writing
     `delete cont->head;` releases the `head` field, NOT `cont` itself,
     even though `cont` appears in the expression's subtree.  Use
     id_resolves_to_decl so cast/lvalue forms still resolve correctly. */
  node_t target_decl = id_resolves_to_decl (expr);
  if (target_decl == NULL) {
    /* Couldn't reduce to a direct binding (e.g. `delete arr[i]`); recurse
       into the expression for nested UAF checks but don't release anything. */
    return;
  }
  for (size_t i = 0; i < n; i++)
    if (a[i].decl == target_decl) {
      if (a[i].state == OS_RELEASED && !a[i].warned_p && diag_active_p (ctx)) {
        error (ctx->c2m_ctx, POS (del),
               "double-free: `%s` was already released on this path",
               a[i].name);
        diag_mark_warned (ctx, &a[i]);
      } else if (a[i].state == OS_MOVED
                 && !a[i].warned_p && diag_active_p (ctx)) {
        error (ctx->c2m_ctx, POS (del),
               "use of moved value: `%s` was consumed by `move` on this path "
               "and is dead; the new owner releases the object", a[i].name);
        diag_mark_warned (ctx, &a[i]);
      } else if (a[i].managed_p && a[i].state == OS_OWNED
                 && !a[i].warned_p && diag_active_p (ctx)) {
        /* Managed bindings are released automatically at scope exit; an
           explicit `delete` would double-free. */
        warning (ctx->c2m_ctx, POS (del),
                 "redundant `delete` of managed (`owned`) binding `%s`: the "
                 "compiler already releases it at the end of its scope",
                 a[i].name);
        diag_mark_warned (ctx, &a[i]);
      }
      a[i].state = OS_RELEASED;
      a[i].released_p = 1; /* user released it here (sticky) */
      record_disposition (ctx, &a[i], POS (del), "deleted");
    }
}

/* Transfer: `detach <expr>` — explicit ownership escape. */
static void transfer_detach (flowctx_t *ctx, node_t det) {
  c2m_ctx_t c2m_ctx = ctx->c2m_ctx; /* for POS() */
  (void) c2m_ctx;
  node_t inner = NL_HEAD (det->u.ops);
  candidate_t *a = VARR_ADDR (candidate_t, ctx->cands);
  size_t n = VARR_LENGTH (candidate_t, ctx->cands);
  if (inner == NULL) return;
  /* Recurse first so nested effects (like a use inside the expression) fire. */
  analyze (ctx, inner);
  /* `detach <expr>` escapes the *value* of <expr>, not every binding that
     appears anywhere inside it.  Only act if the inner expression directly
     names a tracked binding (after cast peeling); patterns like
     `detach m.trim().upper()` produce a fresh value derived from `m` and
     do NOT transfer m's ownership.  Patterns like `detach new Box(v)` or
     `detach (String)"x#" + i` have no binding to transfer at all (the
     value flows through transfer_return's return-owned detection). */
  node_t direct = id_resolves_to_decl (inner);
  if (direct == NULL) return;
  for (size_t i = 0; i < n; i++)
    if (a[i].decl == direct) {
      a[i].state = OS_DETACHED;
      a[i].escapes_p = 1;
      a[i].unsafe_escape_p = 1;
      record_disposition (ctx, &a[i], POS (det), "detached");
    }
}

/* Transfer: `move <expr>` — transfer single ownership OUT of a managed
 * binding into the receiver.  The source binding becomes a read-only view:
 * ownership has left it, so it must not be released or moved again, but reads
 * through it remain legal (the receiver keeps the object alive).
 *
 * The receiver's acquisition (it becomes Owned) is handled at the enclosing
 * N_SPEC_DECL / N_ASSIGN; this function only neutralizes the *source*. */
static void transfer_move (flowctx_t *ctx, node_t mov) {
  c2m_ctx_t c2m_ctx = ctx->c2m_ctx; /* for POS() */
  (void) c2m_ctx;
  node_t inner = NL_HEAD (mov->u.ops);
  candidate_t *a = VARR_ADDR (candidate_t, ctx->cands);
  size_t n = VARR_LENGTH (candidate_t, ctx->cands);
  if (inner == NULL) return;
  /* `move <expr>` transfers the value of <expr>.  Only a direct binding
     reference (after cast peeling) names a source whose ownership moves;
     `move new Box(v)` or `move f()` produce a fresh value with no source to
     neutralize (the receiver simply owns it). */
  node_t direct = id_resolves_to_decl (inner);
  if (direct == NULL) return;
  int idx = cand_lookup (ctx, direct);
  if (idx < 0) return;
  candidate_t *c = &a[idx];
  /* Use-after-move / move-after-release diagnostics, keyed off the per-path
     STATE (not the sticky moved_p flag, which is set during the silent
     fixpoint and would otherwise mis-fire on the first move during replay). */
  if (diag_active_p (ctx) && !c->warned_p) {
    if (c->state == OS_MOVED) {
      error (ctx->c2m_ctx, POS (mov),
             "use of moved value: `%s` was already consumed by `move` on this "
             "path; it is dead (use the new owner, or take a fresh `readonly`)",
             c->name);
      diag_mark_warned (ctx, c);
    } else if (c->state == OS_DETACHED) {
      error (ctx->c2m_ctx, POS (mov),
             "use of escaped value: `%s` already had its ownership transferred "
             "out on this path and cannot be moved again", c->name);
      diag_mark_warned (ctx, c);
    } else if (c->state == OS_RELEASED) {
      error (ctx->c2m_ctx, POS (mov),
             "use-after-free: `%s` was released earlier on this path", c->name);
      diag_mark_warned (ctx, c);
    }
  }
  /* Consumed move: the source binding is now dead (OS_MOVED).  gen nulls the
     source pointer, so the synthesized scope-exit delete is a safe no-op. */
  c->state = OS_MOVED;
  c->escapes_p = 1;
  c->moved_p = 1;
  record_disposition (ctx, c, POS (mov), "moved out");
}

/* Analyse an if-statement with structured split-and-meet. */
static void analyze_if (flowctx_t *ctx, node_t ifn) {
  node_t cond     = NL_EL (ifn->u.ops, 1);
  node_t then_arm = NL_EL (ifn->u.ops, 2);
  node_t else_arm = NL_EL (ifn->u.ops, 3);
  size_t ncands   = VARR_LENGTH (candidate_t, ctx->cands);
  owstate_t *entry = NULL, *then_state = NULL;
  int then_dead, else_dead;

  /* Recurse into the condition so any side-effects fire (e.g. `if (free(p), 0)`). */
  if (cond != NULL) analyze (ctx, cond);

  entry      = (owstate_t *) malloc (ncands * sizeof (owstate_t));
  then_state = (owstate_t *) malloc (ncands * sizeof (owstate_t));
  if (entry == NULL || then_state == NULL) {
    /* Allocation failed: degrade to single-path analysis. */
    if (then_arm != NULL) analyze (ctx, then_arm);
    if (else_arm != NULL) analyze (ctx, else_arm);
    free (entry); free (then_state);
    return;
  }

  /* Save entry, analyse THEN, snapshot, restore for ELSE. */
  state_snapshot (ctx, entry);
  int saved_dead = ctx->dead_p;
  if (then_arm != NULL) analyze (ctx, then_arm);
  state_snapshot (ctx, then_state);
  then_dead = ctx->dead_p;

  state_restore (ctx, entry);
  ctx->dead_p = saved_dead;
  if (else_arm != NULL && else_arm->code != N_IGNORE) analyze (ctx, else_arm);
  else_dead = ctx->dead_p;

  /* Merge.  A dead branch contributes nothing to the join. */
  if (then_dead && else_dead) {
    /* Both branches exit; rest of the enclosing scope is unreachable. */
    ctx->dead_p = 1;
  } else if (then_dead) {
    /* Only else continues — keep current state (else_state). */
    ctx->dead_p = 0;
  } else if (else_dead) {
    /* Only then continues — adopt then_state. */
    state_restore (ctx, then_state);
    ctx->dead_p = 0;
  } else {
    /* Both branches continue — meet then_state with current (else_state). */
    state_merge_from (ctx, then_state);
    ctx->dead_p = 0;
  }

  free (entry);
  free (then_state);
}

/* Conservative loop transfer: analyse the body once, then widen any Owned
 * to MaybeOwned (since a second iteration would overwrite a still-Owned
 * binding from the previous iteration).  break/continue inside the body
 * mark dead_p which we clear after the loop. */
static void analyze_loop_body (flowctx_t *ctx, node_t body) {
  int saved_dead = ctx->dead_p;
  if (body != NULL) analyze (ctx, body);
  state_widen_owned_to_maybe (ctx);
  ctx->dead_p = saved_dead; /* loop joins back even if body exited early */
}

/* The core analyser.  Walks the AST in source order, applying transfer
 * functions where node kinds have semantic effect on ownership, and
 * recursing into children otherwise.  Honours `ctx->dead_p` to short-
 * circuit unreachable code after returns / throws. */
static void analyze (flowctx_t *ctx, node_t n) {
  c2m_ctx_t c2m_ctx = ctx->c2m_ctx; /* for POS() macro hygiene */
  (void) c2m_ctx;
  if (n == NULL || ctx->dead_p) return;

  switch (n->code) {
  case N_SPEC_DECL: {
    /* Ownership candidates become Owned at the declaration if they have a
       real initializer (or are managed/auto-defer acquires).  Definite
       assignment only flips to INIT when there is a real initializer;
       bare `T *p;` stays DA_UNINIT until a later write. */
    int idx = cand_lookup (ctx, n);
    /* Recurse into all operands so nested expressions (initializer) fire. */
    for (node_t c = NL_HEAD (n->u.ops); c != NULL; c = NL_NEXT (c))
      analyze (ctx, c);
    if (idx >= 0) {
      candidate_t *c = &VARR_ADDR (candidate_t, ctx->cands)[idx];
      int has_init = decl_has_real_init_p (SPEC_DECL_INIT (n));
      if (!c->da_only_p) {
        /* Ownership tracking: declaration-with-init (or managed empty slot
           later assigned via move/acquire) is treated as Owned once the
           declarator is reached.  Bare `owned T *p;` stays UNOWNED until a
           real write — the `has_init` gate keeps uninit from looking live. */
        if (has_init || c->managed_p)
          c->state = OS_OWNED;
      }
      if (has_init) c->da_state = DA_INIT;
      /* else leave c->da_state as seeded at candidate creation (typically
         DA_UNINIT for bare declarators). */
    }
    return;
  }
  case N_BLOCK: {
    /* The block body is operand 1 (an N_LIST of statements). */
    node_t list = NL_EL (n->u.ops, 1);
    if (list != NULL) analyze (ctx, list);
    return;
  }
  case N_IF:
    analyze_if (ctx, n);
    return;
  case N_WHILE:
  case N_DO: {
    /* labels(0), expr(1), stmt(2)  (DO: labels(0), stmt(1), expr(2)).
       The body op index differs but for v1 we just locate by code. */
    node_t body = (n->code == N_DO) ? NL_EL (n->u.ops, 1) : NL_EL (n->u.ops, 2);
    analyze_loop_body (ctx, body);
    return;
  }
  case N_FOR: {
    /* labels(0), init(1), cond(2), iter(3), body(4) */
    node_t init = NL_EL (n->u.ops, 1);
    node_t body = NL_EL (n->u.ops, 4);
    if (init != NULL) analyze (ctx, init);
    analyze_loop_body (ctx, body);
    return;
  }
  case N_FORIN: {
    /* labels(0), var_id(1), val_id(2), collection(3), body(4) */
    node_t body = NL_EL (n->u.ops, 4);
    analyze_loop_body (ctx, body);
    return;
  }
  case N_SWITCH: {
    /* v1: analyse body once, then widen as if a loop.  Proper case-by-case
       analysis lands with the CFG. */
    node_t body = NL_EL (n->u.ops, 2);
    if (body != NULL) analyze (ctx, body);
    state_widen_owned_to_maybe (ctx);
    return;
  }
  case N_TRY: {
    /* labels(0), body(1), catch_list(2).  Analyse the body; conservatively
       widen at the end since a throw could have left an acquire mid-flight. */
    node_t body = NL_EL (n->u.ops, 1);
    if (body != NULL) analyze (ctx, body);
    state_widen_owned_to_maybe (ctx);
    return;
  }
  case N_RETURN:
    transfer_return (ctx, n);
    return;
  case N_THROW:
  case N_BREAK:
  case N_CONTINUE:
  case N_GOTO:
    /* Control-flow exits / jumps.  v1: emit exit diagnostics at throw the
       same as a return; for break/continue just terminate this path so the
       loop walker takes over.  Goto we conservatively treat as throw-like. */
    if (n->code == N_THROW) check_exit_state (ctx, POS (n), "this throw");
    ctx->dead_p = 1;
    return;
  case N_DELETE:
    transfer_delete (ctx, n);
    return;
  case N_DEFER: {
    /* `defer <stmt>` postpones execution to scope exit (see gen's
       defer_stmts machinery).  From the ownership analyzer's point of
       view this means:
         - the deferred release IS reliable cleanup, so the candidate is
           NOT a leak at function exit;
         - the candidate is still valid for use up to scope exit, so
           subsequent reads must NOT trip use-after-free.
       The cleanest single-pass approximation is to transition the
       targeted candidate to OS_DETACHED here — it won't fire the leak
       warning, won't fire UAF on later reads, but WILL flag a
       subsequent explicit `free`/`delete` as double-free (correct: the
       defer is going to run too).  Recurse into the body for nested
       effects (UAF on the operand expression) but skip the actual
       release transfer. */
    node_t body = NL_EL (n->u.ops, 1);
    if (body == NULL) return;
    /* Identify the direct release target without firing the release. */
    node_t target_decl = NULL;
    const char *release_kind_str = "deferred release";
    if (body->code == N_DELETE) {
      node_t expr = NL_EL (body->u.ops, 1);
      target_decl = id_resolves_to_decl (expr);
      release_kind_str = "deferred delete";
    } else if (body->code == N_CALL) {
      node_t callee_n = NL_HEAD (body->u.ops);
      node_t args = callee_n != NULL ? NL_NEXT (callee_n) : NULL;
      if (callee_n != NULL && callee_n->code == N_ID && args != NULL
          && args->code == N_LIST) {
        node_t a0 = NL_HEAD (args->u.ops);
        if (a0 != NULL) {
          node_t cand_decl = id_resolves_to_decl (a0);
          if (cand_decl != NULL) {
            /* Check whether callee_n's name matches any tracked candidate's
               release_fn (e.g. `defer free(p)`). */
            int idx = cand_lookup (ctx, cand_decl);
            if (idx >= 0) {
              candidate_t *c = &VARR_ADDR (candidate_t, ctx->cands)[idx];
              if (c->release_fn != NULL && callee_n->u.s.s != NULL
                  && strcmp (callee_n->u.s.s, c->release_fn) == 0) {
                target_decl = cand_decl;
                release_kind_str = "deferred release";
              }
            }
          }
        }
      }
    }
    if (target_decl != NULL) {
      candidate_t *a = VARR_ADDR (candidate_t, ctx->cands);
      size_t cn = VARR_LENGTH (candidate_t, ctx->cands);
      for (size_t i = 0; i < cn; i++)
        if (a[i].decl == target_decl
            && (a[i].state == OS_OWNED || a[i].state == OS_MAYBE_OWNED)) {
          a[i].state = OS_DETACHED;
          a[i].escapes_p = 1;
          a[i].unsafe_escape_p = 1; /* user wired a defer release; don't double up */
          record_disposition (ctx, &a[i], POS (n), release_kind_str);
        }
    }
    return;
  }
  case N_DETACH:
    transfer_detach (ctx, n);
    return;
  case N_MOVE:
    transfer_move (ctx, n);
    return;
  case N_READONLY: {
    /* `readonly <expr>` borrows a non-owning view.  It confers no ownership
       and leaves the viewed binding's state untouched; we only recurse into
       the inner expression so a use-after-free on a released view still
       fires. */
    node_t inner = NL_HEAD (n->u.ops);
    if (inner != NULL) analyze (ctx, inner);
    return;
  }
  case N_ASSIGN: {
    /* Recurse into the RHS for nested effects (calls, reads).  For the
       LHS, recurse only if it's a complex expression (subscript, deref,
       field access) where reading the addressed value IS a real use; a
       bare N_ID lhs is a write target and must NOT trip the generic N_ID
       UAF check.  Without this guard, `p = malloc(...)` after `free(p)`
       would falsely report use-after-free on the LHS rather than treat
       it as a re-acquisition. */
    node_t lhs = NL_HEAD (n->u.ops);
    node_t rhs = lhs != NULL ? NL_NEXT (lhs) : NULL;
    if (rhs != NULL) analyze (ctx, rhs);
    if (lhs != NULL && lhs->code != N_ID) analyze (ctx, lhs);
    transfer_assign (ctx, n);
    return;
  }
  case N_CALL: {
    /* Apply the call's direct-arg effects first.  This emits the
       authoritative double-free / release / direct-UAF diagnostics
       *before* the generic N_ID handler would fire a less specific UAF
       during recursive descent into the arg list. */
    transfer_call (ctx, n);
    /* Then recurse into nested expressions for their own effects.  Skip
       any arg that resolves to a direct binding reference — transfer_call
       already transitioned it and a recursive visit would re-fire the
       generic N_ID UAF check against the freshly-set state.  This guard
       uses `id_resolves_to_decl` (which peels casts), so cast-wrapped
       direct args like `free((void *)p)` are correctly skipped too;
       genuinely complex args (subscripts, derefs, nested calls) still
       get walked so their deep effects fire. */
    node_t callee = NL_HEAD (n->u.ops);
    node_t args   = callee != NULL ? NL_NEXT (callee) : NULL;
    if (callee != NULL) analyze (ctx, callee);
    if (args != NULL && args->code == N_LIST) {
      /* Stamp the prepended instance-method receiver (`this` arg) so gen can
         elide the single call-site null check when the receiver is a proven
         live `new` binding.  The arg node shares its expr attr with the
         original ID; reusing own_deref_class as a non-null proof is fine
         because bare-ID reads never consult that field. */
      if (diag_active_p (ctx)) {
        node_t first = NL_HEAD (args->u.ops);
        node_t peel = first;
        if (peel != NULL && peel->code == N_ADDR) peel = NL_HEAD (peel->u.ops);
        node_t target = id_resolves_to_decl (peel);
        int idx = cand_lookup (ctx, target);
        if (first != NULL && first->attr != NULL) {
          if (first->code == N_ADDR) {
            ((struct expr *) first->attr)->own_deref_class = DEREF_GUARD_SAFE;
          } else if (idx >= 0) {
            candidate_t *c = &VARR_ADDR (candidate_t, ctx->cands)[idx];
            int new_binding
              = c->managed_p
                || (c->release_fn != NULL && strcmp (c->release_fn, "delete") == 0);
            if (new_binding && c->state == OS_OWNED)
              ((struct expr *) first->attr)->own_deref_class = DEREF_GUARD_SAFE;
          }
        }
      }
      for (node_t a = NL_HEAD (args->u.ops); a != NULL; a = NL_NEXT (a))
        if (id_resolves_to_decl (a) == NULL) analyze (ctx, a);
    }
    return;
  }
  case N_DEREF:
  case N_DEREF_FIELD:
  case N_IND: {
    /* Ownership-directed dereference classification.  The gen phase emits a
       null guard before *p / p->f / p[i] when -fexceptions is on.  We refine
       that per site (see enum deref_guard_class):

         SAFE  — receiver is a tracked binding in state OS_OWNED (never
                 released/detached/moved and not widened to MaybeOwned at a
                 join) AND acquired via `new` (whose OOM check guarantees
                 non-null).  gen elides the redundant guard.
         CHECK — receiver is a `new` heap binding that is live-but-uncertain
                 (OS_MAYBE_OWNED: owned on some paths, gone on others).  It is
                 the natural home for the future object generation-tag
                 use-after-free check; for now gen still emits the null guard.

       Only classify on the final diagnostic pass (authoritative, converged
       state); silent inference passes never stamp. */
    if (diag_active_p (ctx) && n->attr != NULL) {
      node_t recv   = NL_HEAD (n->u.ops);
      node_t target = id_resolves_to_decl (recv);
      int idx = cand_lookup (ctx, target);
      /* Method / constructor / destructor bodies: the language always passes a
         non-null receiver into `this` (call sites emit one null trap before the
         call under -fexceptions).  Classify every `this->f` / `*this` / `this[i]`
         as SAFE so gen does not re-emit per-field traps inside the body. */
      if (recv != NULL && recv->code == N_ID && recv->u.s.s != NULL
          && strcmp (recv->u.s.s, "this") == 0) {
        ((struct expr *) n->attr)->own_deref_class = DEREF_GUARD_SAFE;
      } else if (idx >= 0) {
        candidate_t *c = &VARR_ADDR (candidate_t, ctx->cands)[idx];
        int new_binding
          = c->managed_p
            || (c->release_fn != NULL && strcmp (c->release_fn, "delete") == 0);
        if (new_binding && c->state == OS_OWNED) {
          ((struct expr *) n->attr)->own_deref_class = DEREF_GUARD_SAFE;
        } else if (new_binding && c->state == OS_MAYBE_OWNED) {
          ((struct expr *) n->attr)->own_deref_class = DEREF_GUARD_CHECK;
          if (ctx->verbose_p) {
            pos_t p = POS (n);
            fprintf (stderr,
                     "  [ownership] deref of `%s` needs liveness check "
                     "(MaybeOwned) at %s:%d — future generation-tag site\n",
                     c->name, p.fname != NULL ? p.fname : "?", p.lno);
          }
        }
      }
    }
    /* Recurse for nested effects (the receiver's own N_ID UAF check, etc.). */
    for (node_t ch = NL_HEAD (n->u.ops); ch != NULL; ch = NL_NEXT (ch))
      analyze (ctx, ch);
    return;
  }
  case N_ID: {
    /* Bare use: check for use-after-release.  We don't fire on an N_ID
       directly inside an N_CALL arg list (transfer_call already fires);
       this path catches reads via subscript, deref, field-access, etc. */
    node_t target = id_resolves_to_decl (n);
    int idx = cand_lookup (ctx, target);
    if (idx >= 0) {
      candidate_t *c = &VARR_ADDR (candidate_t, ctx->cands)[idx];
      if (c->state == OS_RELEASED && !c->warned_p && diag_active_p (ctx)) {
        error (ctx->c2m_ctx, POS (n),
               "use-after-free: `%s` was released earlier on this path",
               c->name);
        diag_mark_warned (ctx, c);
      } else if (c->state == OS_MOVED && !c->warned_p && diag_active_p (ctx)) {
        /* Consumed-move: the moved-from binding is dead, so reading it is an
           error.  Use the new owner, or take a fresh `readonly` before moving. */
        error (ctx->c2m_ctx, POS (n),
               "use of moved value: `%s` was consumed by `move` on this path "
               "and is dead; use the new owner instead", c->name);
        diag_mark_warned (ctx, c);
      }
      /* Definite-assignment (da) checks: UNINIT / MAYBE -> warning.
         Only emit on the final diagnostic pass (diag_active_p).
         Covers both ownership-tracked heap bindings (`new`/`malloc`) and
         pure-DA local pointers (`int *p;`).  Header files and
         `__attribute__((da_ignore))` bindings are suppressed so list/set/map
         internals stay quiet while user code still gets the diagnostic.
         Softened to warning (not error) so runtime safety guards can still
         catch the wild use under -fexceptions (bugs/010-uninit-read.cy). */
      if (!c->da_ignore_p && !c->param_p && diag_active_p (ctx)) {
        const char *fname = c->acquire_pos.fname;
        int is_header = (fname && strstr (fname, ".h") != NULL);
        if (!is_header && !c->warned_p) {
          if (c->da_state == DA_UNINIT) {
            warning (ctx->c2m_ctx, POS (n),
                     "use of uninitialized value `%s`",
                     c->name);
            /* Do not mark warned_p for DA: a later ownership diagnostic
               (UAF/leak) can still fire.  Suppress only re-emitting the
               same uninit note by reusing warned for da_only. */
            if (c->da_only_p) diag_mark_warned (ctx, c);
          } else if (c->da_state == DA_MAYBE) {
            warning (ctx->c2m_ctx, POS (n),
                     "possible use of uninitialized value `%s`",
                     c->name);
          }
        }
      }
    }
    return;
  }
  default:
    /* Generic recursion in source order. */
    if (!ownership_node_has_ops (n->code)) return;
    for (node_t c = NL_HEAD (n->u.ops); c != NULL; c = NL_NEXT (c))
      analyze (ctx, c);
    return;
  }
}

/* ────────────────────────────────────────────────────────────────────────
 * Control-Flow Graph (Step E + F).
 *
 * Per-function CFG built once from the AST, then iterated to fixpoint by a
 * forward dataflow worklist.  Replaces the structured AST analyser; the
 * worklist correctly converges on loops instead of conservatively widening
 * a single iteration, and `break` / `continue` become real edges instead
 * of dead paths.
 *
 * Basic-block model:
 *   - Each BB holds a flat list of "effect" AST nodes — statements that
 *     carry ownership semantics (acquires, calls, assigns, returns, ...).
 *   - The recursive AST walker `cfg_build_node` partitions the function
 *     body: control-flow constructs (N_IF / N_WHILE / N_FOR / N_BREAK /
 *     N_RETURN / ...) create new blocks and edges; everything else is
 *     pushed as an effect on the current block.
 *   - A synthetic `exit_bb` is the unique join point for return/throw and
 *     for fall-through off the end of the body.
 *
 * Dataflow:
 *   - Per-BB `state_in` and `state_out` arrays sized by candidate count.
 *   - The worklist iterates: pop a dirty BB, recompute its in_state as the
 *     meet over all predecessors' out_states, apply transfers to compute
 *     a new out_state, and if it changed, push successors back on the
 *     worklist.
 *   - Transfers run with `quiet_p = 1` during the worklist (no diagnostics).
 *   - A final pass walks each reachable BB once with `quiet_p = 0`, with
 *     `state` reset to that BB's fixpoint in_state, replaying the effects
 *     to emit the real diagnostics in source order.
 * ──────────────────────────────────────────────────────────────────────── */

typedef enum {
  EDGE_FALL,   /* sequential fall-through */
  EDGE_TRUE,   /* condition true branch */
  EDGE_FALSE,  /* condition false branch */
  EDGE_BACK,   /* loop back-edge (header) */
} edge_kind_t;

typedef struct cfg_edge {
  int         dst;
  edge_kind_t kind;
  /* Path-condition narrowing (Step G).
   *
   * When the source block's branch is a null-check on a tracked candidate
   * (`if (p == NULL)`, `if (!p)`, `if (p)`), one of the two outgoing edges
   * carries an assertion that p is null on that path.  The dataflow meet
   * applies this assertion to the predecessor's out_state before combining
   * — effectively saying "on this edge, treat `p` as if it had been
   * disposed of, since NULL doesn't need release".
   *
   * `narrow_decl == NULL` means no narrowing on this edge.
   * `narrow_state` is the value to overwrite `narrow_decl`'s state with;
   * for null narrowing we use OS_DETACHED (ownership effectively gone,
   * and — unlike OS_RELEASED — a subsequent N_ID use doesn't trip the
   * use-after-free check, which would be wrong since p is null, not
   * freed). */
  node_t      narrow_decl;
  owstate_t   narrow_state;
} cfg_edge_t;
DEF_VARR (cfg_edge_t);

typedef struct basic_block {
  VARR (node_t)      *effects;
  VARR (cfg_edge_t)  *succs;
  VARR (int)         *preds;
  /* Per-candidate state arrays, sized = candidate count.  Allocated by
     the dataflow runner once it knows ncands; freed in cfg_destroy. */
  owstate_t          *state_in;
  owstate_t          *state_out;
  /* Definite-assignment (da) state — parallel arrays to the owstate ones. */
  da_state_t         *da_state_in;
  da_state_t         *da_state_out;
} basic_block_t;
DEF_VARR (basic_block_t);

typedef struct cfg {
  VARR (basic_block_t) *blocks;
  int                   entry_bb;
  int                   exit_bb;
} cfg_t;

typedef struct cfg_builder {
  c2m_ctx_t       c2m_ctx;
  cfg_t          *cfg;
  /* curr_bb == -1 means "unreachable" (we just emitted a return / break /
     continue and haven't started a new live block yet).  All emit / link
     operations are no-ops in this state until a control-flow construct
     opens a fresh block. */
  int             curr_bb;
  VARR (int)     *loop_continues; /* stack of continue-target BB ids */
  VARR (int)     *loop_breaks;    /* stack of break-target BB ids */
} cfg_builder_t;

/* Allocate a fresh BB.  All internal VARRs are created here; state arrays
 * are allocated lazily by the dataflow runner. */
static int bb_new (cfg_builder_t *b) {
  c2m_ctx_t c2m_ctx = b->c2m_ctx;
  MIR_alloc_t alloc = c2m_alloc (c2m_ctx);
  basic_block_t bb;
  VARR_CREATE (node_t, bb.effects, alloc, 4);
  VARR_CREATE (cfg_edge_t, bb.succs, alloc, 2);
  VARR_CREATE (int, bb.preds, alloc, 2);
  bb.state_in = NULL;
  bb.state_out = NULL;
  int id = (int) VARR_LENGTH (basic_block_t, b->cfg->blocks);
  VARR_PUSH (basic_block_t, b->cfg->blocks, bb);
  return id;
}

/* Link src -> dst with the given edge kind, also recording the reverse
 * link on dst's predecessor list.  No-op if either endpoint is -1.
 * Narrowing fields default to none; call `bb_narrow_last_edge` to attach
 * a path-condition refinement after the link. */
static void bb_link (cfg_builder_t *b, int src, int dst, edge_kind_t kind) {
  cfg_edge_t e;
  basic_block_t *bbs;
  if (src < 0 || dst < 0) return;
  bbs = VARR_ADDR (basic_block_t, b->cfg->blocks);
  e.dst = dst;
  e.kind = kind;
  e.narrow_decl = NULL;
  e.narrow_state = OS_UNOWNED;
  VARR_PUSH (cfg_edge_t, bbs[src].succs, e);
  VARR_PUSH (int, bbs[dst].preds, src);
}

/* Locate the (just-added) edge from `src` whose kind is `kind` and attach
 * a path-condition refinement to it.  Used by the N_IF builder once it has
 * recognised a null-check shape on a tracked candidate. */
static void bb_narrow_edge (cfg_builder_t *b, int src, edge_kind_t kind,
                            node_t narrow_decl, owstate_t narrow_state) {
  basic_block_t *bbs;
  cfg_edge_t *es;
  size_t ns;
  if (src < 0 || narrow_decl == NULL) return;
  bbs = VARR_ADDR (basic_block_t, b->cfg->blocks);
  es = VARR_ADDR (cfg_edge_t, bbs[src].succs);
  ns = VARR_LENGTH (cfg_edge_t, bbs[src].succs);
  for (size_t i = 0; i < ns; i++)
    if (es[i].kind == kind) {
      es[i].narrow_decl  = narrow_decl;
      es[i].narrow_state = narrow_state;
      return;
    }
}

/* Recognise the small set of null-check expression shapes the analyser
 * narrows.  Returns the SPEC_DECL of the tracked binding the check is
 * about (or NULL if the cond isn't a recognised null-check), and writes
 * the edge-kind to refine via `*out_kind` — EDGE_TRUE when the "narrowed"
 * branch is the then-arm (p is null when cond is true), EDGE_FALSE when
 * it's the else-arm.
 *
 * Recognised shapes:
 *      p == NULL    p == 0    (void*)p == 0    —  narrow on TRUE
 *      !p                                       —  narrow on TRUE
 *      p != NULL    p != 0                      —  narrow on FALSE
 *      p           (bare truthy check)          —  narrow on FALSE
 *
 * Any cast wrapping either side of an equality is peeled, so the analyser
 * sees `((void *)p == NULL)` as `p == 0`. */
static int is_zero_constant (node_t n) {
  if (n == NULL) return 0;
  while (n->code == N_CAST) n = NL_EL (n->u.ops, 1);
  if (n == NULL) return 0;
  switch (n->code) {
  case N_I:   return n->u.l   == 0;
  case N_L:   return n->u.l   == 0;
  case N_LL:  return n->u.ll  == 0;
  case N_U:   return n->u.ul  == 0;
  case N_UL:  return n->u.ul  == 0;
  case N_ULL: return n->u.ull == 0;
  default:    return 0;
  }
}

static node_t cond_null_check (node_t cond, edge_kind_t *out_kind) {
  if (cond == NULL) return NULL;
  /* `!p` — narrow on TRUE arm */
  if (cond->code == N_NOT) {
    node_t inner = NL_HEAD (cond->u.ops);
    while (inner != NULL && inner->code == N_CAST) inner = NL_EL (inner->u.ops, 1);
    if (inner != NULL && inner->code == N_ID) {
      *out_kind = EDGE_TRUE;
      return id_resolves_to_decl (inner);
    }
    return NULL;
  }
  /* `p == 0` / `p == NULL` — narrow on TRUE; `p != 0` — narrow on FALSE. */
  if (cond->code == N_EQ || cond->code == N_NE) {
    node_t lhs = NL_HEAD (cond->u.ops);
    node_t rhs = lhs != NULL ? NL_NEXT (lhs) : NULL;
    node_t maybe_p = NULL;
    if (lhs == NULL || rhs == NULL) return NULL;
    while (lhs != NULL && lhs->code == N_CAST) lhs = NL_EL (lhs->u.ops, 1);
    while (rhs != NULL && rhs->code == N_CAST) rhs = NL_EL (rhs->u.ops, 1);
    if (lhs != NULL && lhs->code == N_ID && is_zero_constant (rhs)) maybe_p = lhs;
    else if (rhs != NULL && rhs->code == N_ID && is_zero_constant (lhs)) maybe_p = rhs;
    if (maybe_p == NULL) return NULL;
    *out_kind = (cond->code == N_EQ) ? EDGE_TRUE : EDGE_FALSE;
    return id_resolves_to_decl (maybe_p);
  }
  /* Bare `p` as a truthy check (after casts) — narrow on FALSE arm. */
  {
    node_t inner = cond;
    while (inner != NULL && inner->code == N_CAST) inner = NL_EL (inner->u.ops, 1);
    if (inner != NULL && inner->code == N_ID) {
      *out_kind = EDGE_FALSE;
      return id_resolves_to_decl (inner);
    }
  }
  return NULL;
}

/* Push an effect onto a specific BB's effect list. */
static void bb_emit_to (cfg_builder_t *b, int bb_id, node_t n) {
  if (bb_id < 0 || n == NULL) return;
  VARR_PUSH (node_t, VARR_ADDR (basic_block_t, b->cfg->blocks)[bb_id].effects, n);
}

/* Push an effect onto the builder's current BB (no-op when unreachable). */
static void bb_emit (cfg_builder_t *b, node_t n) {
  if (b->curr_bb < 0) return;
  bb_emit_to (b, b->curr_bb, n);
}

/* Recursive CFG builder.  Partitions the AST into basic blocks and edges. */
static void cfg_build_node (cfg_builder_t *b, node_t n) {
  if (n == NULL) return;

  switch (n->code) {
  case N_BLOCK:
    cfg_build_node (b, NL_EL (n->u.ops, 1));
    return;
  case N_LIST:
    for (node_t c = NL_HEAD (n->u.ops); c != NULL; c = NL_NEXT (c))
      cfg_build_node (b, c);
    return;
  case N_IF: {
    /* labels(0), cond(1), then(2), else(3) */
    if (b->curr_bb < 0) return;
    int branch_bb = b->curr_bb;
    int then_bb   = bb_new (b);
    int else_bb   = bb_new (b);
    int merge_bb  = bb_new (b);
    /* The condition expression is evaluated in the branch BB (its side
       effects are part of the predecessor's transfer trace). */
    node_t cond = NL_EL (n->u.ops, 1);
    bb_emit_to (b, branch_bb, cond);
    bb_link (b, branch_bb, then_bb, EDGE_TRUE);
    bb_link (b, branch_bb, else_bb, EDGE_FALSE);
    /* Path-condition narrowing: if the branch condition is a null check on
       a tracked binding, mark the corresponding outgoing edge so the
       dataflow meet narrows the binding's state on that path.  We use
       OS_DETACHED — "ownership effectively gone, no further action needed"
       — because NULL doesn't need to be freed, and a subsequent N_ID use
       on the narrowed path correctly doesn't trip the use-after-free
       check (it would in OS_RELEASED). */
    {
      edge_kind_t narrow_kind;
      node_t narrow_decl = cond_null_check (cond, &narrow_kind);
      if (narrow_decl != NULL)
        bb_narrow_edge (b, branch_bb, narrow_kind, narrow_decl, OS_DETACHED);
    }
    /* then arm */
    b->curr_bb = then_bb;
    cfg_build_node (b, NL_EL (n->u.ops, 2));
    if (b->curr_bb >= 0) bb_link (b, b->curr_bb, merge_bb, EDGE_FALL);
    /* else arm */
    b->curr_bb = else_bb;
    node_t else_arm = NL_EL (n->u.ops, 3);
    if (else_arm != NULL && else_arm->code != N_IGNORE)
      cfg_build_node (b, else_arm);
    if (b->curr_bb >= 0) bb_link (b, b->curr_bb, merge_bb, EDGE_FALL);
    b->curr_bb = merge_bb;
    return;
  }
  case N_WHILE: {
    /* labels(0), cond(1), body(2) */
    int header_bb = bb_new (b);
    int body_bb   = bb_new (b);
    int exit_bb   = bb_new (b);
    if (b->curr_bb >= 0) bb_link (b, b->curr_bb, header_bb, EDGE_FALL);
    bb_emit_to (b, header_bb, NL_EL (n->u.ops, 1)); /* cond */
    bb_link (b, header_bb, body_bb, EDGE_TRUE);
    bb_link (b, header_bb, exit_bb, EDGE_FALSE);
    VARR_PUSH (int, b->loop_continues, header_bb);
    VARR_PUSH (int, b->loop_breaks, exit_bb);
    b->curr_bb = body_bb;
    cfg_build_node (b, NL_EL (n->u.ops, 2));
    if (b->curr_bb >= 0) bb_link (b, b->curr_bb, header_bb, EDGE_BACK);
    VARR_POP (int, b->loop_continues);
    VARR_POP (int, b->loop_breaks);
    b->curr_bb = exit_bb;
    return;
  }
  case N_DO: {
    /* labels(0), body(1), cond(2) */
    int body_bb  = bb_new (b);
    int cond_bb  = bb_new (b);
    int exit_bb  = bb_new (b);
    if (b->curr_bb >= 0) bb_link (b, b->curr_bb, body_bb, EDGE_FALL);
    VARR_PUSH (int, b->loop_continues, cond_bb);
    VARR_PUSH (int, b->loop_breaks, exit_bb);
    b->curr_bb = body_bb;
    cfg_build_node (b, NL_EL (n->u.ops, 1));
    if (b->curr_bb >= 0) bb_link (b, b->curr_bb, cond_bb, EDGE_FALL);
    bb_emit_to (b, cond_bb, NL_EL (n->u.ops, 2));
    bb_link (b, cond_bb, body_bb, EDGE_BACK);
    bb_link (b, cond_bb, exit_bb, EDGE_FALSE);
    VARR_POP (int, b->loop_continues);
    VARR_POP (int, b->loop_breaks);
    b->curr_bb = exit_bb;
    return;
  }
  case N_FOR: {
    /* labels(0), init(1), cond(2), iter(3), body(4) */
    if (b->curr_bb >= 0) bb_emit (b, NL_EL (n->u.ops, 1)); /* init */
    int header_bb = bb_new (b);
    int body_bb   = bb_new (b);
    int iter_bb   = bb_new (b);
    int exit_bb   = bb_new (b);
    if (b->curr_bb >= 0) bb_link (b, b->curr_bb, header_bb, EDGE_FALL);
    node_t cond = NL_EL (n->u.ops, 2);
    if (cond != NULL && cond->code != N_IGNORE) bb_emit_to (b, header_bb, cond);
    bb_link (b, header_bb, body_bb, EDGE_TRUE);
    bb_link (b, header_bb, exit_bb, EDGE_FALSE);
    VARR_PUSH (int, b->loop_continues, iter_bb);
    VARR_PUSH (int, b->loop_breaks, exit_bb);
    b->curr_bb = body_bb;
    cfg_build_node (b, NL_EL (n->u.ops, 4));
    if (b->curr_bb >= 0) bb_link (b, b->curr_bb, iter_bb, EDGE_FALL);
    node_t iter = NL_EL (n->u.ops, 3);
    if (iter != NULL && iter->code != N_IGNORE) bb_emit_to (b, iter_bb, iter);
    bb_link (b, iter_bb, header_bb, EDGE_BACK);
    VARR_POP (int, b->loop_continues);
    VARR_POP (int, b->loop_breaks);
    b->curr_bb = exit_bb;
    return;
  }
  case N_FORIN: {
    /* labels(0), var_id(1), val_id(2), collection(3), body(4) */
    int header_bb = bb_new (b);
    int body_bb   = bb_new (b);
    int exit_bb   = bb_new (b);
    if (b->curr_bb >= 0) bb_link (b, b->curr_bb, header_bb, EDGE_FALL);
    bb_emit_to (b, header_bb, NL_EL (n->u.ops, 3));
    bb_link (b, header_bb, body_bb, EDGE_TRUE);
    bb_link (b, header_bb, exit_bb, EDGE_FALSE);
    VARR_PUSH (int, b->loop_continues, header_bb);
    VARR_PUSH (int, b->loop_breaks, exit_bb);
    b->curr_bb = body_bb;
    cfg_build_node (b, NL_EL (n->u.ops, 4));
    if (b->curr_bb >= 0) bb_link (b, b->curr_bb, header_bb, EDGE_BACK);
    VARR_POP (int, b->loop_continues);
    VARR_POP (int, b->loop_breaks);
    b->curr_bb = exit_bb;
    return;
  }
  case N_BREAK:
    if (b->curr_bb >= 0 && VARR_LENGTH (int, b->loop_breaks) > 0) {
      int target = VARR_GET (int, b->loop_breaks,
                             VARR_LENGTH (int, b->loop_breaks) - 1);
      bb_link (b, b->curr_bb, target, EDGE_FALL);
    }
    b->curr_bb = -1;
    return;
  case N_CONTINUE:
    if (b->curr_bb >= 0 && VARR_LENGTH (int, b->loop_continues) > 0) {
      int target = VARR_GET (int, b->loop_continues,
                             VARR_LENGTH (int, b->loop_continues) - 1);
      bb_link (b, b->curr_bb, target, EDGE_BACK);
    }
    b->curr_bb = -1;
    return;
  case N_RETURN:
    if (b->curr_bb >= 0) {
      bb_emit (b, n);
      bb_link (b, b->curr_bb, b->cfg->exit_bb, EDGE_FALL);
    }
    b->curr_bb = -1;
    return;
  case N_THROW:
  case N_GOTO:
    /* Coarse: just terminate the current block and connect to function exit. */
    if (b->curr_bb >= 0) {
      bb_emit (b, n);
      bb_link (b, b->curr_bb, b->cfg->exit_bb, EDGE_FALL);
    }
    b->curr_bb = -1;
    return;
  case N_TRY:
    /* labels(0), body(1), catch_list(2).  Coarse: analyse the body only. */
    cfg_build_node (b, NL_EL (n->u.ops, 1));
    return;
  case N_SWITCH:
    /* labels(0), expr(1), body(2).  Coarse: analyse body, no case-by-case
       branching yet.  Proper switch handling is on the roadmap. */
    cfg_build_node (b, NL_EL (n->u.ops, 2));
    return;
  case N_FUNC_DEF:
    /* Entry into the function: walk its body block (specs / decl / decls
       are operands 0-2; the block is operand 3.  Use the FUNC_DEF_BLOCK
       macro to stay in lockstep with the rest of c2mir if the structure
       ever changes). */
    cfg_build_node (b, FUNC_DEF_BLOCK (n));
    return;
  default:
    /* Linear effect (declaration / expression / assignment / call / ...)
       — add to current block in source order. */
    bb_emit (b, n);
    return;
  }
}

/* ────────────────────────────────────────────────────────────────────────
 * Worklist dataflow over the CFG.
 *
 * Iterates until every BB's out_state is stable under the meet-then-
 * transfer-functions rule.  Transfers run with `quiet_p = 1` so they
 * only mutate the candidate states; no diagnostics are emitted yet.
 * Terminates because the lattice has finite height and `state_meet` is
 * monotone.  A safety cap of 100 outer passes keeps a pathological
 * configuration from hanging.
 * ──────────────────────────────────────────────────────────────────────── */

static void cfg_apply_block (flowctx_t *ctx, basic_block_t *bb) {
  candidate_t *cands = VARR_ADDR (candidate_t, ctx->cands);
  size_t ncands = VARR_LENGTH (candidate_t, ctx->cands);
  /* Load in_state into the candidate scratch slots. */
  for (size_t k = 0; k < ncands; k++) cands[k].state = bb->state_in[k];
  for (size_t k = 0; k < ncands; k++) cands[k].da_state = bb->da_state_in[k];
  ctx->dead_p = 0;
  /* Run each effect's transfer.  The existing `analyze` dispatcher does the
     per-AST-node transitions; control-flow nodes never appear inside a
     block's effect list (the builder stripped them out). */
  node_t *effects = VARR_ADDR (node_t, bb->effects);
  size_t ne = VARR_LENGTH (node_t, bb->effects);
  for (size_t e = 0; e < ne; e++) {
    if (ctx->dead_p) break;
    analyze (ctx, effects[e]);
  }
  /* Snapshot out_state for propagation to successors. */
  for (size_t k = 0; k < ncands; k++) bb->state_out[k] = cands[k].state;
  for (size_t k = 0; k < ncands; k++) bb->da_state_out[k] = cands[k].da_state;
}

static void cfg_dataflow (flowctx_t *ctx, cfg_t *cfg) {
  size_t ncands = VARR_LENGTH (candidate_t, ctx->cands);
  size_t nblocks = VARR_LENGTH (basic_block_t, cfg->blocks);
  basic_block_t *bbs = VARR_ADDR (basic_block_t, cfg->blocks);

  /* Allocate state arrays.  Initial state is all-Unowned for every BB. */
  for (size_t i = 0; i < nblocks; i++) {
    bbs[i].state_in  = (owstate_t *) calloc (ncands, sizeof (owstate_t));
    bbs[i].state_out = (owstate_t *) calloc (ncands, sizeof (owstate_t));
    bbs[i].da_state_in  = (da_state_t *) calloc (ncands, sizeof (da_state_t));
    bbs[i].da_state_out = (da_state_t *) calloc (ncands, sizeof (da_state_t));
  }

  ctx->quiet_p = 1;

  int max_passes = 100;
  int changed = 1;
  while (changed && max_passes-- > 0) {
    changed = 0;
    for (size_t i = 0; i < nblocks; i++) {
      basic_block_t *bb = &bbs[i];
      /* Recompute in_state from predecessors (meet over their out_states). */
      owstate_t new_in[64]; /* candidates per function is small; stack-cap */
      owstate_t *in_buf = new_in;
      owstate_t *heap_in = NULL;
      if (ncands > 64) {
        heap_in = (owstate_t *) calloc (ncands, sizeof (owstate_t));
        in_buf = heap_in;
      }
      /* Initial in-state: Unowned everywhere, EXCEPT that the function
         entry block conceptually receives each pointer parameter already
         Owned (the caller hands ownership in).  Without this seed every
         param starts Unowned and stays Unowned, so the borrow inference
         (Owned && !escapes -> ((borrows))) could never fire — only the
         release inference worked, because free(p) forces RELEASED from any
         prior state.  The entry block has no predecessors, so the
         pred-merge below leaves this seed intact. */
      {
        candidate_t *ca = VARR_ADDR (candidate_t, ctx->cands);
        int entry_p = (i == (size_t) cfg->entry_bb);
        for (size_t k = 0; k < ncands; k++)
          in_buf[k] = (entry_p && ca[k].param_p) ? OS_OWNED : OS_UNOWNED;
      }
      /* Definite-assignment in-state: locals start UNINIT; entry params
         start INIT (caller is responsible for providing a defined value). */
      da_state_t da_new_in[64];
      da_state_t *da_in_buf = da_new_in;
      da_state_t *heap_da_in = NULL;
      if (ncands > 64) {
        heap_da_in = (da_state_t *) calloc (ncands, sizeof (da_state_t));
        da_in_buf = heap_da_in;
      }
      {
        candidate_t *ca = VARR_ADDR (candidate_t, ctx->cands);
        int entry_p = (i == (size_t) cfg->entry_bb);
        for (size_t k = 0; k < ncands; k++) {
          if (entry_p)
            da_in_buf[k] = ca[k].da_state;   /* already set correctly at candidate creation */
          else
            da_in_buf[k] = DA_UNINIT;
        }
      }
      size_t npreds = VARR_LENGTH (int, bb->preds);
      if (npreds > 0) {
        int *preds = VARR_ADDR (int, bb->preds);
        owstate_t refined[64];
        owstate_t *r_buf = refined;
        owstate_t *heap_r = NULL;
        if (ncands > 64) {
          heap_r = (owstate_t *) calloc (ncands, sizeof (owstate_t));
          r_buf = heap_r;
        }
        for (size_t pi = 0; pi < npreds; pi++) {
          basic_block_t *pred = &bbs[preds[pi]];
          owstate_t *po = pred->state_out;
          memcpy (r_buf, po, ncands * sizeof (owstate_t));
          /* Find the edge from pred -> bb and apply its narrowing. */
          cfg_edge_t *pred_succs = VARR_ADDR (cfg_edge_t, pred->succs);
          size_t npred_succs = VARR_LENGTH (cfg_edge_t, pred->succs);
          for (size_t s = 0; s < npred_succs; s++)
            if (pred_succs[s].dst == (int) i
                && pred_succs[s].narrow_decl != NULL) {
              int nidx = cand_lookup (ctx, pred_succs[s].narrow_decl);
              /* Apply narrowing whenever the pre-narrow state asserts the
                 binding *might* own a resource (Owned or MaybeOwned).  The
                 narrowed value (Detached) says "on this path the binding
                 is null and there's nothing to clean up".  Released and
                 Detached are left alone — they're already "disposed" so
                 narrowing is redundant. */
              if (nidx >= 0
                  && (r_buf[nidx] == OS_OWNED || r_buf[nidx] == OS_MAYBE_OWNED))
                r_buf[nidx] = pred_succs[s].narrow_state;
              break;
            }
          if (pi == 0) memcpy (in_buf, r_buf, ncands * sizeof (owstate_t));
          else
            for (size_t k = 0; k < ncands; k++)
              in_buf[k] = state_meet (in_buf[k], r_buf[k]);
        }
        if (heap_r != NULL) free (heap_r);
      }
      int in_diff = 0;
      for (size_t k = 0; k < ncands; k++)
        if (in_buf[k] != bb->state_in[k]) { in_diff = 1; break; }
      if (in_diff) memcpy (bb->state_in, in_buf, ncands * sizeof (owstate_t));
      if (heap_in != NULL) free (heap_in);

      /* --- Definite-assignment (DA) propagation & store --- */
      if (npreds > 0) {
        int *preds = VARR_ADDR (int, bb->preds);
        da_state_t da_refined[64];
        da_state_t *da_r_buf = da_refined;
        da_state_t *heap_da_r = NULL;
        if (ncands > 64) {
          heap_da_r = (da_state_t *) calloc (ncands, sizeof (da_state_t));
          da_r_buf = heap_da_r;
        }
        for (size_t pi = 0; pi < npreds; pi++) {
          basic_block_t *pred = &bbs[preds[pi]];
          da_state_t *po = pred->da_state_out;
          if (pi == 0) memcpy (da_in_buf, po, ncands * sizeof (da_state_t));
          else
            for (size_t k = 0; k < ncands; k++)
              da_in_buf[k] = da_meet (da_in_buf[k], po[k]);
        }
        if (heap_da_r) free (heap_da_r);
      }
      int da_in_diff = 0;
      for (size_t k = 0; k < ncands; k++)
        if (da_in_buf[k] != bb->da_state_in[k]) { da_in_diff = 1; break; }
      if (da_in_diff) memcpy (bb->da_state_in, da_in_buf, ncands * sizeof (da_state_t));
      if (heap_da_in) free (heap_da_in);

      /* Post-fixed-point guarantee: any candidate declared with a *real*
         initializer (new / malloc / ... — not the placeholder N_IGNORE)
         must be considered DA_INIT from the first use onward.  This
         overrides any conservative MAYBE that the data-flow lattice
         produced at a merge (e.g. loop header).  Using `!= NULL` would
         treat every bare `T *p;` as INIT, because the parser always
         materialises N_IGNORE for a missing initializer. */
      {
        candidate_t *ca = VARR_ADDR (candidate_t, ctx->cands);
        for (size_t k = 0; k < ncands; k++) {
          if (ca[k].decl != NULL && ca[k].decl->code == N_SPEC_DECL
              && decl_has_real_init_p (SPEC_DECL_INIT (ca[k].decl)))
            bb->da_state_in[k] = DA_INIT;
        }
      }

      /* Apply transfers to compute new out_state. */
      owstate_t prev_out[64];
      owstate_t *prev_buf = prev_out;
      owstate_t *heap_prev = NULL;
      if (ncands > 64) {
        heap_prev = (owstate_t *) calloc (ncands, sizeof (owstate_t));
        prev_buf = heap_prev;
      }
      memcpy (prev_buf, bb->state_out, ncands * sizeof (owstate_t));
      cfg_apply_block (ctx, bb);
      int out_diff = 0;
      for (size_t k = 0; k < ncands; k++)
        if (prev_buf[k] != bb->state_out[k]) { out_diff = 1; break; }
      if (out_diff) changed = 1;
      if (heap_prev != NULL) free (heap_prev);
    }
  }

  ctx->quiet_p = 0;
}

/* ────────────────────────────────────────────────────────────────────────
 * Step I: auto-defer-release synthesis.
 *
 * Under `-fauto-release` the analyzer is allowed to *fix* a definite-leak
 * binding by synthesizing a `defer release_fn(p);` immediately after its
 * declaration.  Gated tightly because over-synthesis would double-free:
 *
 *   - candidate's state at the synthetic exit BB must be OS_OWNED
 *     (a leak that the analyzer is certain about; not MaybeOwned).
 *   - `escapes_p` must be 0 — the candidate was never seen to escape via
 *     return, store, or detaching call on any path.  An escape on even
 *     one path means the synthesized scope-exit free would double-free
 *     (the defer machinery unwinds through `return` too).
 *   - !has_cleanup_p — `__attribute__((cleanup(fn)))` already handles it.
 *   - the release_fn (`free`, `fclose`, ...) must resolve as an in-scope
 *     symbol at the declaration's scope; otherwise check() on our
 *     synthesized node would emit errors.
 *
 * The synthesized node is type-checked here; on success it's stored on the
 * decl as `auto_release_call`, and the leak warning is suppressed (the
 * fix is silent at the source level; `-v` reports it).  The gen pass picks
 * up `auto_release_call` exactly like `dtor_call` and pushes it onto
 * defer_stmts, so it unwinds at scope exit and through return/break/continue.
 */
static int try_synthesize_auto_release (flowctx_t *ctx, candidate_t *c) {
  c2m_ctx_t  c2m_ctx = ctx->c2m_ctx;
  check_ctx_t check_ctx = c2m_ctx->check_ctx;
  node_t     spec_decl = c->decl;
  decl_t     d;
  node_t     declr, id, callee_id, callee_def, args, arg, call;
  node_t     saved_scope;
  size_t     saved_errs;
  int        verbose_p = c2m_options != NULL && c2m_options->verbose_p;

  if (spec_decl == NULL || spec_decl->code != N_SPEC_DECL) return 0;
  d = (decl_t) spec_decl->attr;
  if (d == NULL || d->auto_release_call != NULL) return 0;

  declr = NL_EL (spec_decl->u.ops, 1);
  if (declr == NULL || declr->code != N_DECL) return 0;
  id = NL_HEAD (declr->u.ops);
  if (id == NULL || id->code != N_ID) return 0;

  /* `delete` is the release sentinel for `new T(...)` bindings.  It's a
     language statement, not a function call, so synthesize N_DELETE
     rather than N_CALL.  Skip the find_def visibility check (delete is
     always available) and the callee_id construction. */
  if (strcmp (c->release_fn, "delete") == 0) {
    node_t labels  = new_node (c2m_ctx, N_LIST);
    node_t target  = copy_node (c2m_ctx, id);
    node_t del     = new_pos_node2 (c2m_ctx, N_DELETE, POS (id), labels, target);
    saved_scope = curr_scope;
    curr_scope  = d->scope;
    saved_errs  = n_errors;
    check (c2m_ctx, del, spec_decl);
    curr_scope  = saved_scope;
    if (n_errors != saved_errs) {
      if (verbose_p)
        fprintf (stderr,
                 "  [ownership] auto-release: check() rejected synthesized "
                 "`delete %s;` — falling back to leak warning\n", c->name);
      return 0;
    }
    d->auto_release_call = del;
    c->release_kind = c->managed_p ? "auto-deleted at scope exit (owned)"
                                   : "auto-released (-fauto-release)";
    c->release_pos  = POS (id);
    if (verbose_p) {
      pos_t p = c->acquire_pos;
      fprintf (stderr,
               "  [ownership] auto-release: synthesized `defer delete %s;` at %s:%d\n",
               c->name, p.fname != NULL ? p.fname : "?", p.lno);
    }
    return 1;
  }

  /* Verify release_fn is visible at the declaration's scope.  Without this
     guard the synthesized N_CALL would emit "unknown identifier" errors
     during check() and pollute the build. */
  callee_id  = build_id (c2m_ctx, c->release_fn, POS (id));
  callee_def = find_def (c2m_ctx, S_REGULARS, callee_id, d->scope, NULL);
  if (callee_def == NULL) {
    if (verbose_p)
      fprintf (stderr,
               "  [ownership] auto-release: skipping `%s` — release fn `%s` "
               "not in scope (no <stdlib.h> in this TU?)\n",
               c->name, c->release_fn);
    return 0;
  }

  /* Build `release_fn(id)` as a freshly checkable expression statement. */
  args = new_node (c2m_ctx, N_LIST);
  arg  = copy_node (c2m_ctx, id);
  op_append (c2m_ctx, args, arg);
  call = new_pos_node2 (c2m_ctx, N_CALL, POS (id), callee_id, args);

  /* Temporarily install the declaration's scope so check() resolves the
     callee and the argument identifier through the right symbol chain. */
  saved_scope = curr_scope;
  curr_scope  = d->scope;
  saved_errs  = n_errors;
  check (c2m_ctx, call, spec_decl);
  curr_scope  = saved_scope;

  if (n_errors != saved_errs) {
    if (verbose_p)
      fprintf (stderr,
               "  [ownership] auto-release: check() rejected synthesized `%s(%s)` "
               "— falling back to leak warning\n",
               c->release_fn, c->name);
    return 0;
  }
  d->auto_release_call = call;
  /* Mark disposition for -fownership-report.  Reuse the decl pos so the
     report says "auto-released at scope exit" at the same line as the alloc. */
  c->release_kind = c->managed_p ? "auto-released at scope exit (owned)"
                                 : "auto-released (-fauto-release)";
  c->release_pos  = POS (id);
  if (verbose_p) {
    pos_t p = c->acquire_pos;
    fprintf (stderr,
             "  [ownership] auto-release: synthesized `defer %s(%s);` at %s:%d\n",
             c->release_fn, c->name, p.fname != NULL ? p.fname : "?", p.lno);
  }
  return 1;
}

/* Diagnostic pass: replay each reachable block once with its stable
 * in_state and `quiet_p == 0`, so transfer functions emit real warnings
 * and errors at the correct source positions. */
static void cfg_emit_diagnostics (flowctx_t *ctx, cfg_t *cfg) {
  c2m_ctx_t c2m_ctx = ctx->c2m_ctx; /* for POS() */
  size_t nblocks = VARR_LENGTH (basic_block_t, cfg->blocks);
  basic_block_t *bbs = VARR_ADDR (basic_block_t, cfg->blocks);
  (void) c2m_ctx;
  basic_block_t *exit_bb = &bbs[cfg->exit_bb];
  candidate_t *cands = VARR_ADDR (candidate_t, ctx->cands);
  size_t ncands = VARR_LENGTH (candidate_t, ctx->cands);

  /* Step I (FIRST): -fauto-release silently fixes definite leaks by
     synthesizing `defer release_fn(p);` on the binding.  This MUST run
     before the diagnostic replay below.  Rationale: a synthesized
     `defer delete p;` is placed at the *declaration*, so it covers every
     scope exit — including early returns.  If we instead let the replay
     run first, check_exit_state would fire the leak warning (and set
     warned_p) at the first `return` it reaches, defeating synthesis on any
     function that returns.  By deciding here, off the *aggregated* exit
     state (`exit_bb->state_in`, the meet over all paths) plus the sticky
     `escapes_p` flag, we get the multi-exit-safe gate: a binding that is
     OS_OWNED on every path and never escaped is genuinely leaked and a
     scope-bound delete is always correct.  Once synthesized, there's no
     leak left to report — so we set warned_p to keep the replay silent. */
  for (size_t k = 0; k < ncands; k++) cands[k].state = exit_bb->state_in[k];
  if (!ownership_silent_pass_p) {
    int global_auto = (c2m_options != NULL && c2m_options->auto_release_p);
    for (size_t k = 0; k < ncands; k++) {
      candidate_t *c = &cands[k];
      /* Managed (`owned`/`move`) bindings ALWAYS get their release
         synthesized — that is the whole point of the GC-like layer.  Other
         bindings only get the silent fix under -fauto-release. */
      if (!c->managed_p && !global_auto) continue;
      if (c->warned_p) continue;
      if (c->has_cleanup_p) continue;
      if (c->param_p) continue;  /* never synthesize free on incoming params */
      if (c->da_only_p || c->release_fn == NULL) continue;
      /* Escape gate.  A binding that escaped UNSAFELY (returned, stored into a
         non-tracked location, handed to an unknown/owning call, aliased by a
         plain `=`, detached, or already covered by a user `defer`) must not
         get a synthesized scope-exit delete — that would double-free / UAF.
         For managed (`owned`/`move`) bindings a `move` escape is SAFE: `move`
         nulls the source pointer, so the synthesized null-safe `delete` is a
         no-op on the moved path while the NON-moved paths still get correct
         cleanup (the conditional-move case).  Non-managed (-fauto-release)
         bindings keep the stricter any-escape gate. */
      if (c->managed_p) { if (c->unsafe_escape_p) continue; }
      else              { if (c->escapes_p)        continue; }
      /* Eligible states:
           OS_OWNED       — owned on every path; classic definite leak.
           OS_MAYBE_OWNED — owned on some paths, "gone" (null) on others,
                            but never user-released (released_p) and never
                            escaped.  The only non-release way to reach
                            MaybeOwned is a null-guard narrowing
                            (`if (!p) return; ... use p ...`), and since
                            delete/free are null-safe a scope-exit release
                            is correct on every path.  Requiring
                            !released_p keeps us from double-freeing a
                            binding the user already frees on one branch. */
      int eligible = (c->state == OS_OWNED)
                     || (c->state == OS_MAYBE_OWNED && !c->released_p);
      if (!eligible) continue;
      if (try_synthesize_auto_release (ctx, c)) c->warned_p = 1;
    }
  }

  /* Diagnostic replay: walk each reachable block once with quiet_p == 0 so
     transfer functions emit real warnings / errors at the right positions.
     Auto-released bindings already carry warned_p, so their (now-fixed)
     leaks stay silent. */
  ctx->quiet_p = 0;
  for (size_t i = 0; i < nblocks; i++) {
    /* Unreachable blocks: no predecessors and not the entry.  Skip. */
    if (i != (size_t) cfg->entry_bb && VARR_LENGTH (int, bbs[i].preds) == 0)
      continue;
    cfg_apply_block (ctx, &bbs[i]);
  }
  /* Fall-through exit at the synthetic exit block: any candidate still in
     Owned/MaybeOwned at this point has escaped neither via return (which
     would have transitioned to Detached) nor via release (Released).
     check_exit_state guards itself with `warned_p` so we never duplicate
     a diagnostic that already fired at an explicit return. */
  for (size_t k = 0; k < ncands; k++) cands[k].state = exit_bb->state_in[k];

  check_exit_state (ctx, POS (ctx->func_def), "the end of this function");

  /* Interprocedural summary derivation.  Walk the param-candidate roster
     and infer each parameter's effective attribute from its final state.
     Conservative: any uncertainty (escape, MaybeOwned) keeps PA_DEFAULT.
     We also collect a `returns_owned_p` signal: TRUE if any local heap
     candidate (non-param) ended Detached via return AND we observed an
     escapes_p flag from a return on its path — i.e. this function is in
     the business of allocating-then-returning. */
  {
    c2m_ctx_t c2m_ctx = ctx->c2m_ctx;
    node_t fn = func_node_of (ctx->func_def);
    size_t param_count = 0;
    if (fn != NULL) {
      node_t params = NL_HEAD (fn->u.ops);
      if (params != NULL && params->code == N_LIST)
        for (node_t p = NL_HEAD (params->u.ops); p != NULL; p = NL_NEXT (p)) {
          if (p->code == N_DOTS) break;
          param_count++;
        }
    }
    /* Pre-fill with PA_DEFAULT; positions we never saw stay default. */
    param_attr_t stack_attrs[64];
    param_attr_t *attrs = stack_attrs;
    if (param_count > sizeof (stack_attrs) / sizeof (stack_attrs[0]))
      attrs = reg_malloc (c2m_ctx, param_count * sizeof (param_attr_t));
    for (size_t i = 0; i < param_count; i++) attrs[i] = PA_DEFAULT;

    int returns_owned_p = 0;
    const char *returns_release_fn = NULL;
    for (size_t k = 0; k < ncands; k++) {
      candidate_t *c = &cands[k];
      if (c->param_p) {
        param_attr_t inferred = PA_DEFAULT;
        if (c->state == OS_RELEASED && !c->escapes_p) {
          inferred = PA_RELEASES;
        } else if (c->state == OS_OWNED && !c->escapes_p) {
          /* Param wasn't released, never escaped, never had its state
             clobbered — the function read it (possibly) and that's it. */
          inferred = PA_BORROWS;
        }
        if (c->param_pos < param_count) attrs[c->param_pos] = inferred;
      } else {
        /* Local heap candidate that was handed to the caller via `return`
           on some path ships ownership.  We key off the sticky returned_p
           flag rather than the merged exit state: a function that both
           `delete`s the binding on an error/throw path and `return`s it on
           success (Sqlite::query, List::Slice with a guard, ...) has a
           merged exit state of MaybeOwned, but it still returns owned on
           its normal path.  Record the candidate's release_fn so callers
           know which release form to expect ("free" vs. "delete"). */
        if (c->returned_p) {
          returns_owned_p = 1;
          if (returns_release_fn == NULL) returns_release_fn = c->release_fn;
        }
      }
    }
    /* Fold in the inline acquire-shape signal recorded by transfer_return:
       a wrapper like  `Box* spawn(int) { return detach new Box(v); }`
       has no intermediate candidate, so we'd otherwise miss returns_owned. */
    if (!returns_owned_p && ctx->returns_owned_inline_p
        && ctx->returns_release_fn_inline != NULL) {
      returns_owned_p     = 1;
      returns_release_fn = ctx->returns_release_fn_inline;
    }
    summary_install (c2m_ctx, ctx->func_def, param_count, attrs,
                     returns_owned_p, returns_release_fn);
  }
}

/* Free CFG state arrays and the internal per-block VARRs. */
static void cfg_destroy (cfg_t *cfg) {
  size_t nblocks = VARR_LENGTH (basic_block_t, cfg->blocks);
  basic_block_t *bbs = VARR_ADDR (basic_block_t, cfg->blocks);
  for (size_t i = 0; i < nblocks; i++) {
    if (bbs[i].state_in  != NULL) free (bbs[i].state_in);
    if (bbs[i].state_out != NULL) free (bbs[i].state_out);
    if (bbs[i].da_state_in  != NULL) free (bbs[i].da_state_in);
    if (bbs[i].da_state_out != NULL) free (bbs[i].da_state_out);
    VARR_DESTROY (node_t, bbs[i].effects);
    VARR_DESTROY (cfg_edge_t, bbs[i].succs);
    VARR_DESTROY (int, bbs[i].preds);
  }
  VARR_DESTROY (basic_block_t, cfg->blocks);
}

/* Verbose-mode helper: print the CFG shape and per-block effect counts. */
static void cfg_dump (cfg_t *cfg) {
  size_t nblocks = VARR_LENGTH (basic_block_t, cfg->blocks);
  basic_block_t *bbs = VARR_ADDR (basic_block_t, cfg->blocks);
  fprintf (stderr, "  [ownership]   CFG: %lu blocks (entry=%d exit=%d)\n",
           (unsigned long) nblocks, cfg->entry_bb, cfg->exit_bb);
  for (size_t i = 0; i < nblocks; i++) {
    fprintf (stderr, "  [ownership]     bb%lu: %lu effect%s -> [",
             (unsigned long) i,
             (unsigned long) VARR_LENGTH (node_t, bbs[i].effects),
             VARR_LENGTH (node_t, bbs[i].effects) == 1 ? "" : "s");
    size_t ns = VARR_LENGTH (cfg_edge_t, bbs[i].succs);
    cfg_edge_t *es = VARR_ADDR (cfg_edge_t, bbs[i].succs);
    for (size_t s = 0; s < ns; s++) {
      const char *kn = (es[s].kind == EDGE_FALL  ? "fall"
                       : es[s].kind == EDGE_TRUE  ? "true"
                       : es[s].kind == EDGE_FALSE ? "false"
                                                  : "back");
      fprintf (stderr, "%s%d/%s", s == 0 ? "" : ", ", es[s].dst, kn);
    }
    fprintf (stderr, "]\n");
  }
}

/* ────────────────────────────────────────────────────────────────────────
 * `-fownership-report`: human-readable per-function dump of every tracked
 * binding and the first observed disposition of its ownership.
 *
 * One block per function (grouped under its class when the function is a
 * class method).  For each candidate we print:
 *
 *   class Track  (src/foo.cy:10)
 *     fn Track::ctor  (src/foo.cy:15)
 *       char* name = strdup(...)  at src/foo.cy:17
 *         → stored into non-tracked location  at src/foo.cy:17
 *     fn ~Track  (src/foo.cy:22)
 *       (no tracked allocations)
 *   fn main  (src/foo.cy:50)
 *     char* a = malloc(...)       at src/foo.cy:52
 *       → auto-released (-fauto-release)  at src/foo.cy:52
 *     char* q = malloc(...)       at src/foo.cy:60
 *       → returned to caller  at src/foo.cy:62
 *
 * Bindings with no recorded disposition print as `→ LEAKED`.
 */

/* Module-scope cursor so we emit the `class <Name>` header exactly once
   per contiguous run of methods.  Reset by ownership_run() per TU. */
static const char *report_curr_class_name = NULL;
static int         report_header_emitted_p = 0;

static const char *func_name_of (node_t func_def) {
  node_t decl, id;
  if (func_def == NULL || func_def->code != N_FUNC_DEF) return "<anon>";
  decl = FUNC_DEF_DECL (func_def);
  if (decl == NULL || decl->code != N_DECL) return "<anon>";
  id = DECL_ID (decl);
  if (id == NULL || id->code != N_ID) return "<anon>";
  return id->u.s.s != NULL ? id->u.s.s : "<anon>";
}

/* If this function is a class method, return the owning class's name;
   otherwise NULL.  Class methods are registered with their decl's `scope`
   pointing at the N_CLASS / N_STRUCT node. */
static const char *class_name_of_func (node_t func_def) {
  decl_t d;
  node_t scope, cid;
  if (func_def == NULL || func_def->code != N_FUNC_DEF) return NULL;
  d = (decl_t) func_def->attr;
  if (d == NULL) return NULL;
  scope = d->scope;
  if (scope == NULL) return NULL;
  if (scope->code != N_CLASS && scope->code != N_STRUCT) return NULL;
  cid = NL_HEAD (scope->u.ops);
  if (cid == NULL || cid->code != N_ID) return NULL;
  return cid->u.s.s;
}

static void emit_ownership_report (flowctx_t *ctx, node_t func_def,
                                   VARR (candidate_t) *cands) {
  c2m_ctx_t   c2m_ctx = ctx->c2m_ctx;
  candidate_t *a       = VARR_ADDR (candidate_t, cands);
  size_t       n       = VARR_LENGTH (candidate_t, cands);
  FILE        *out     = c2m_options != NULL && c2m_options->message_file != NULL
                          ? c2m_options->message_file : stderr;
  const char  *fname   = func_name_of (func_def);
  const char  *cname   = class_name_of_func (func_def);
  pos_t        fp      = POS (func_def);
  (void) c2m_ctx;

  /* Quiet for functions with nothing tracked AND no interesting summary.
     A summary with returns_owned_p or any non-default param attr is worth
     reporting ("this wrapper returns ownership" is information the user
     wants).  param-only candidates without an interesting summary are
     skipped: incoming params alone aren't an interesting report row. */
  {
    func_summary_t *s = summary_lookup (func_def);
    int summary_has_info = 0;
    if (s != NULL) {
      if (s->returns_owned_p) summary_has_info = 1;
      for (size_t p = 0; !summary_has_info && p < s->param_count; p++)
        if (s->param_attrs[p] != PA_DEFAULT) summary_has_info = 1;
    }
    int has_non_param_candidate = 0;
    for (size_t k = 0; !has_non_param_candidate && k < n; k++)
      if (!a[k].param_p) has_non_param_candidate = 1;
    if (!has_non_param_candidate && !summary_has_info) return;
  }

  if (!report_header_emitted_p) {
    fprintf (out, "\n[ownership report]\n");
    report_header_emitted_p = 1;
  }
  if (cname != NULL
      && (report_curr_class_name == NULL
          || strcmp (report_curr_class_name, cname) != 0)) {
    fprintf (out, "class %s\n", cname);
    report_curr_class_name = cname;
  } else if (cname == NULL && report_curr_class_name != NULL) {
    report_curr_class_name = NULL;
  }

  if (cname != NULL)
    fprintf (out, "  fn %s::%s  (%s:%d)\n",
             cname, fname,
             fp.fname != NULL ? fp.fname : "?", fp.lno);
  else
    fprintf (out, "fn %s  (%s:%d)\n",
             fname, fp.fname != NULL ? fp.fname : "?", fp.lno);

  /* Render the function's inferred summary (if any) first, then the
     allocation roster.  We only show summary lines when there's something
     interesting to say. */
  {
    func_summary_t *s = summary_lookup (func_def);
    if (s != NULL) {
      const char *base_indent = cname != NULL ? "    " : "  ";
      for (size_t p = 0; p < s->param_count; p++) {
        const char *attr = NULL;
        if (s->param_attrs[p] == PA_RELEASES) attr = "((releases))  inferred";
        else if (s->param_attrs[p] == PA_BORROWS) attr = "((borrows))  inferred";
        if (attr != NULL)
          fprintf (out, "%s[summary] param %zu: %s\n", base_indent, p, attr);
      }
      if (s->returns_owned_p)
        fprintf (out, "%s[summary] returns owned pointer (inferred)\n", base_indent);
    }
  }

  for (size_t i = 0; i < n; i++) {
    candidate_t *c = &a[i];
    if (c->param_p) continue;  /* params are not user-facing allocations */
    const char *indent = cname != NULL ? "    " : "  ";
    fprintf (out, "%s%s = %s(...)  at %s:%d\n",
             indent, c->name, c->acquire_fn,
             c->acquire_pos.fname != NULL ? c->acquire_pos.fname : "?",
             c->acquire_pos.lno);
    if (c->has_cleanup_p) {
      fprintf (out, "%s  → cleanup attribute handles release\n", indent);
    } else if (c->release_kind != NULL) {
      fprintf (out, "%s  → %s  at %s:%d\n",
               indent, c->release_kind,
               c->release_pos.fname != NULL ? c->release_pos.fname : "?",
               c->release_pos.lno);
    } else if (c->state == OS_MAYBE_OWNED) {
      fprintf (out, "%s  → POTENTIAL LEAK (owned on some paths)\n", indent);
    } else if (c->state == OS_OWNED) {
      fprintf (out, "%s  → LEAKED (still owned at function exit)\n", indent);
    } else {
      fprintf (out, "%s  → disposed (state=%s)\n",
               indent, owstate_name (c->state));
    }
  }
}

/* Cheap predicate used by the pre-filter below.
 * Returns TRUE if the function body (or its single return) contains an
 * obvious acquire form that would set returns_owned_inline_p even when
 * there are no tracked candidates.  We only need a shallow scan; we stop
 * as soon as we see a return whose peeled expression is N_NEW, N_DETACH
 * or an N_CALL.  This is O(1) in practice for the common case.
 */
static int func_might_return_owned_inline (node_t func_def) {
  if (func_def == NULL || func_def->code != N_FUNC_DEF) return 0;
  node_t block = FUNC_DEF_BLOCK (func_def);
  if (block == NULL || block->code != N_BLOCK) return 0;
  /* Walk the top-level statements only (no deep recursion). */
  for (node_t s = NL_HEAD (block->u.ops); s != NULL; s = NL_NEXT (s)) {
    if (s->code != N_RETURN) continue;
    node_t expr = NL_EL (s->u.ops, 0);
    if (expr == NULL) return 0; /* void return */
    expr = peel_casts (expr);
    if (expr->code == N_NEW || expr->code == N_DETACH) return 1;
    if (expr->code == N_CALL) return 1;
    /* Common wrapper shape: return detach <call or new> */
    if (expr->code == N_CAST) {
      node_t inner = peel_casts (NL_EL (expr->u.ops, 1));
      if (inner && (inner->code == N_NEW || inner->code == N_DETACH || inner->code == N_CALL))
        return 1;
    }
  }
  return 0;
}

/* Per-function entry point.  Collects candidates, builds the CFG, runs the
 * worklist to fixpoint, then a single diagnostic pass that emits the real
 * warnings and errors. */
static void analyze_function (c2m_ctx_t c2m_ctx, node_t func_def) {
  /* c2m_ctx parameter named so the POS()/check_decl_pos() macros expand. */
  VARR (candidate_t) *cands;
  flowctx_t ctx;
  cfg_t cfg;
  cfg_builder_t builder;
  MIR_alloc_t alloc = c2m_alloc (c2m_ctx);
  int verbose_p = c2m_options != NULL && c2m_options->verbose_p;

  VARR_CREATE (candidate_t, cands, alloc, 8);
  /* Ownership acquire/IP candidates: walk the whole function def so a
     top-level `auto x = new T();` style binding is found.  Pure DA
     candidates (uninitialized local pointers) only come from the body
     block — that avoids treating parameter SPEC_DECLs / the synthetic
     `this` receiver as uninitialized locals. */
  collect_candidates (c2m_ctx, FUNC_DEF_BLOCK (func_def), cands);
  /* Seed param candidates so interprocedural summary inference can fire. */
  collect_param_candidates (c2m_ctx, func_def, cands);

  /* Cheap pre-filter: if no candidates at all and the function cannot
     possibly set returns_owned_inline_p via a direct return-acquire shape,
     install a trivial summary (returns_owned_p=0) and skip the expensive
     CFG build + dataflow.  This preserves correctness for wrappers because
     we still let the inline detection run when it might fire. */
  if (VARR_LENGTH (candidate_t, cands) == 0 &&
      !func_might_return_owned_inline (func_def)) {
    /* No ownership state to track and no inline acquire return → trivial summary. */
    ow_last_trivial_p = 1;
    summary_install (c2m_ctx, func_def, 0, NULL, 0, NULL);
    VARR_DESTROY (candidate_t, cands);
    return;
  }

  /* Note: we used to early-exit when cands was empty, but the
     interprocedural summary pass needs to run on every function so that
     wrappers with no candidates (e.g. `Box* spawn(int v) { return new
     Box(v); }`) still get returns_owned_p recorded.  The pre-filter above
     restores most of that saving while still handling the wrapper case. */

  if (verbose_p) {
    pos_t fp = POS (func_def);
    fprintf (stderr,
             "  [ownership] analysing %s (%lu candidate%s)\n",
             fp.fname != NULL ? fp.fname : "?",
             (unsigned long) VARR_LENGTH (candidate_t, cands),
             VARR_LENGTH (candidate_t, cands) == 1 ? "" : "s");
  }

  /* ---- Build CFG ---- */
  VARR_CREATE (basic_block_t, cfg.blocks, alloc, 8);
  cfg.entry_bb = -1;
  cfg.exit_bb  = -1;
  builder.c2m_ctx = c2m_ctx;
  builder.cfg     = &cfg;
  builder.curr_bb = -1;
  VARR_CREATE (int, builder.loop_continues, alloc, 4);
  VARR_CREATE (int, builder.loop_breaks,    alloc, 4);
  /* Entry first, then exit (so exit_bb id is stable for early returns). */
  cfg.entry_bb = bb_new (&builder);
  cfg.exit_bb  = bb_new (&builder);
  builder.curr_bb = cfg.entry_bb;
  cfg_build_node (&builder, func_def);
  /* If the body fell through without an explicit return, link the last
     live BB to the exit. */
  if (builder.curr_bb >= 0 && builder.curr_bb != cfg.exit_bb)
    bb_link (&builder, builder.curr_bb, cfg.exit_bb, EDGE_FALL);
  VARR_DESTROY (int, builder.loop_continues);
  VARR_DESTROY (int, builder.loop_breaks);

  /* ---- Run analysis ---- */
  ctx.c2m_ctx   = c2m_ctx;
  ctx.func_def  = func_def;
  ctx.cands     = cands;
  ctx.dead_p    = 0;
  ctx.quiet_p   = 0;
  ctx.verbose_p = verbose_p;
  ctx.returns_owned_inline_p   = 0;
  ctx.returns_release_fn_inline = NULL;

  if (verbose_p) cfg_dump (&cfg);
  cfg_dataflow (&ctx, &cfg);
  cfg_emit_diagnostics (&ctx, &cfg);

  if (verbose_p) {
    candidate_t *a = VARR_ADDR (candidate_t, cands);
    size_t n = VARR_LENGTH (candidate_t, cands);
    for (size_t i = 0; i < n; i++)
      fprintf (stderr, "  [ownership]   %-12s final state = %s%s\n",
               a[i].name, owstate_name (a[i].state),
               a[i].warned_p ? " (warned)" : "");
  }

  if (c2m_options != NULL && c2m_options->ownership_report_p && !ownership_silent_pass_p)
    emit_ownership_report (&ctx, func_def, cands);

  cfg_destroy (&cfg);
  VARR_DESTROY (candidate_t, cands);
}

/* Tracked analysis wrapper.  Sets up dependency recording for the duration
   of one analyze_function run, caches the trivial verdict, and — during
   silent inference — queues dependents when this function's summary
   changed.  In the final (diagnostic) pass, a function whose last analysis
   was trivial is skipped outright: it built no CFG, can emit no
   diagnostics, and its summary is already stable (any callee-summary
   change would have re-queued it during the worklist drain, refreshing the
   verdict). */
static void ow_analyze_function (c2m_ctx_t c2m_ctx, node_t func_def) {
  size_t idx = ow_func_idx (func_def);
  size_t saved_idx;
  ow_funcinfo_t *fi;

  if (!ow_inference_p) {
    fi = &VARR_ADDR (ow_funcinfo_t, ow_funcs)[idx];
    if (fi->analyzed_p && fi->trivial_p) return;
  }
  saved_idx = ow_curr_idx;
  ow_curr_idx = idx;
  ow_last_install_changed_p = 0;
  ow_last_trivial_p = 0;
  analyze_function (c2m_ctx, func_def);
  ow_curr_idx = saved_idx;
  /* Re-fetch: the registry may have grown (and moved) during analysis. */
  fi = &VARR_ADDR (ow_funcinfo_t, ow_funcs)[idx];
  fi->analyzed_p = 1;
  fi->trivial_p = (unsigned char) ow_last_trivial_p;
  if (ow_inference_p && ow_last_install_changed_p) ow_enqueue_dependents (idx);
}

/* Recursively walk an AST subtree, accounting for any SPEC_DECLs the check
 * pass marked as auto-defer candidates, and kicking off the per-function
 * leak check at each N_FUNC_DEF.  When `verbose_p` is set, prints one line
 * per candidate.  Never mutates the AST. */
static void ownership_walk (c2m_ctx_t c2m_ctx, node_t n, int *count, int verbose_p) {
  if (n == NULL) return;

  /* Function-level dataflow analysis: scoped to this FUNC_DEF's subtree so
     a candidate in `foo()` doesn't see a `free()` call in `bar()`.  We
     still continue recursion below so nested defs (class methods are
     hoisted to module top, but other future nesting patterns may appear)
     are visited and the auto-defer candidate count stays correct. */
  if (n->code == N_FUNC_DEF)
    ow_analyze_function (c2m_ctx, n);

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
 *
 * Interprocedural iteration: one silent walk analyzes every function with
 * empty summaries (unannotated calls default to conservative escapes),
 * recording which callee summaries each analysis consulted.  Whenever a
 * summary changes, exactly the recorded dependents are pushed on a
 * worklist and re-analyzed — a call-graph fixpoint instead of the old
 * fixed-count whole-module re-walks.  Diagnostics are suppressed during
 * inference via `ownership_silent_pass_p`; a final walk with diagnostics
 * enabled replays the analysis under the stable summaries, skipping
 * functions whose cached verdict is trivial (no candidates, no inline
 * acquire-return — they can't emit anything).
 *
 * The drain is bounded by OWNERSHIP_MAX_PASSES * nfuncs as a termination
 * safety net; pathological mutual-recursion cases hit the cap and we
 * accept whatever summaries we have (as before).
 *
 * Returns the number of errors emitted (always 0 today — ownership pass
 * doesn't fail the compile yet). */
static int ownership_run (c2m_ctx_t c2m_ctx, node_t module) {
  int count = 0;
  int verbose_p = c2m_options != NULL && c2m_options->verbose_p;
  MIR_alloc_t alloc = c2m_alloc (c2m_ctx);

  if (verbose_p)
    fprintf (stderr, "  [ownership] pass: scanning AST for auto-defer candidates...\n");

  /* Reset per-TU ownership-report state so each compile starts fresh. */
  report_curr_class_name  = NULL;
  report_header_emitted_p = 0;

  /* Reset interprocedural state for this TU. */
  ow_alloc = alloc;
  if (func_summaries == NULL)
    VARR_CREATE (func_summary_t, func_summaries, alloc, 64);
  else
    VARR_TRUNC (func_summary_t, func_summaries, 0);
  if (ow_funcs == NULL) {
    VARR_CREATE (ow_funcinfo_t, ow_funcs, alloc, 128);
    VARR_CREATE (int, ow_worklist, alloc, 64);
    HTAB_CREATE (ow_fidx_t, ow_fidx_tab, alloc, 512, ow_fidx_hash, ow_fidx_eq, NULL);
  } else {
    for (size_t i = 0; i < VARR_LENGTH (ow_funcinfo_t, ow_funcs); i++)
      VARR_DESTROY (int, VARR_ADDR (ow_funcinfo_t, ow_funcs)[i].dependents);
    VARR_TRUNC (ow_funcinfo_t, ow_funcs, 0);
    VARR_TRUNC (int, ow_worklist, 0);
    HTAB_CLEAR (ow_fidx_t, ow_fidx_tab);
  }

  /* ---- Inference (silent): one full walk + call-graph worklist drain ---- */
  ownership_silent_pass_p = 1;
  ow_inference_p = 1;
  summaries_dirty_p = 0;
  summaries_pass_idx = 0;
  if (verbose_p) fprintf (stderr, "  [ownership] inference pass (silent)\n");
  {
    int local_count = 0;
    ownership_walk (c2m_ctx, module, &local_count, verbose_p);
    count = local_count;
  }
  {
    size_t nfuncs = VARR_LENGTH (ow_funcinfo_t, ow_funcs);
    size_t cap = (size_t) OWNERSHIP_MAX_PASSES * nfuncs + 16; /* termination safety net */
    size_t reanalyzed = 0;

    summaries_pass_idx = 1;
    while (VARR_LENGTH (int, ow_worklist) > 0 && reanalyzed < cap) {
      int idx = VARR_POP (int, ow_worklist);

      VARR_ADDR (ow_funcinfo_t, ow_funcs)[idx].on_worklist_p = 0;
      ow_analyze_function (c2m_ctx, VARR_GET (ow_funcinfo_t, ow_funcs, idx).func_def);
      reanalyzed++;
    }
    if (verbose_p)
      fprintf (stderr,
               "  [ownership] worklist drained: %lu targeted re-analyses (%lu functions)%s\n",
               (unsigned long) reanalyzed, (unsigned long) nfuncs,
               VARR_LENGTH (int, ow_worklist) > 0 ? " — hit safety cap" : "");
  }

  /* ---- Final pass: emit diagnostics with stable summaries.  Cached-trivial
     functions are skipped (see ow_analyze_function). ---- */
  ownership_silent_pass_p = 0;
  ow_inference_p = 0;
  summaries_pass_idx = OWNERSHIP_MAX_PASSES;
  if (verbose_p)
    fprintf (stderr, "  [ownership] final pass (diagnostics enabled)\n");
  int final_count = 0;
  ownership_walk (c2m_ctx, module, &final_count, verbose_p);
  count = final_count;

  if (verbose_p)
    fprintf (stderr,
             "  [ownership] pass: %d auto-defer candidate%s found, "
             "%zu function summar%s computed\n",
             count, count == 1 ? "" : "s",
             func_summaries == NULL ? 0 : VARR_LENGTH (func_summary_t, func_summaries),
             (func_summaries == NULL || VARR_LENGTH (func_summary_t, func_summaries) == 1)
               ? "y" : "ies");

  return 0;
}
