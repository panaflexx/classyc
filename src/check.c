/* ---------------------- Context Checker Start ---------------------- */

/* The context checker is AST traversing pass which checks C11
   constraints.  It also augmenting AST nodes by type and layout
   information.  Here are the created node attributes:

 1. expr nodes have attribute "struct expr", N_ID not expr context has NULL attribute.
 2. N_SWITCH has attribute "struct switch_attr"
 3. N_SPEC_DECL (only with ID), N_MEMBER, N_FUNC_DEF have attribute "struct decl"
 4. N_GOTO has attribute node_t (target stmt)
 5. N_STRUCT, N_UNION have attribute "struct node_scope" if they have a decl list
 6. N_MODULE, N_BLOCK, N_FOR, N_FORIN, N_FUNC have attribute "struct node_scope"
 7. declaration_specs or spec_qual_list N_LISTs have attribute "struct decl_spec",
    but as a part of N_COMPOUND_LITERAL have attribute "struct decl"
 8. N_ENUM has attribute "struct enum_type"
 9. N_ENUM_CONST has attribute "struct enum_value"
10. N_CASE and N_DEFAULT have attribute "struct case_attr"

*/

typedef struct decl *decl_t;
DEF_VARR (decl_t);

typedef struct case_attr *case_t;
DEF_HTAB (case_t);

/* Deferred class-method body record.  When `defer_method_bodies_p` is set
   (true throughout the module's first check pass), every N_FUNC_DEF whose
   enclosing context is a class skips its body check and instead pushes one of
   these onto `pending_method_bodies`.  After every top-level item has had its
   signature processed (so every class's members, constructors, and method
   prototypes are visible in the symbol table), the module driver drains the
   queue and runs each body in the right scope/class context.  This is what
   makes `new B(args)` and `b->method()` work inside class A regardless of
   whether B is declared above or below A in the same translation unit. */
struct pending_body {
  node_t func_def;     /* the N_FUNC_DEF node still awaiting body check */
  node_t block;        /* the function's N_BLOCK whose node_scope is live */
  node_t class_node;   /* enclosing class tag (curr_class at deferral time) */
  /* Snapshot of `func_decls_for_allocation` taken at deferral time.  Holds
     the decls that the signature pass already registered for this method
     (the FUNC_DEF's own decl + the synthetic `this` parameter for instance
     methods).  Subsequent class members would otherwise overwrite the global
     accumulator before the drain runs.  Restored verbatim into the global
     VARR at drain time so process_func_decls_for_allocation can lay them
     out together with any locals declared inside the body. */
  VARR (decl_t) * saved_fda;
};
typedef struct pending_body pending_body_t;
DEF_VARR (pending_body_t);

#undef curr_scope

struct check_ctx {
  node_t curr_scope;
  node_t curr_class;
  VARR (node_t) * label_uses;
  node_t func_block_scope;
  unsigned curr_func_scope_num;
  unsigned char in_params_p, jump_ret_p;
  /* Set TRUE while checking a function body that contains a `try` statement.
     Consulted by process_func_decls_for_allocation to force scalar locals and
     parameters of such functions into memory instead of MIR registers: values
     held in callee-saved registers across the try's setjmp are reverted to
     their setjmp-time values by longjmp, so a variable modified in the try
     body and read in a catch handler would otherwise see a stale (or garbage)
     value.  Memory-backing gives the intuitive, reliable behavior. */
  int curr_func_has_try;
  node_t curr_unnamed_anon_struct_union_member;
  node_t curr_switch;
  VARR (decl_t) * func_decls_for_allocation;
  VARR (node_t) * possible_incomplete_decls;
  node_t n_i1_node;
  HTAB (case_t) * case_tab;
  node_t curr_func_def, curr_loop, curr_loop_switch;
  mir_size_t curr_call_arg_area_offset;
  VARR (node_t) * context_stack;
  /* Set while inside an `unowned <decl>` wrapper so the auto-defer-candidate
     detection on `N_SPEC_DECL` knows to skip this binding.  Save/restore at
     N_UNOWNED entry/exit so nested declarations behave correctly. */
  int in_unowned_p;
  /* Set while inside an `owned <decl>` wrapper so N_SPEC_DECL marks the binding
     as part of the managed-ownership layer.  Save/restore at N_OWNED entry/exit. */
  int in_owned_p;
  /* Sequence lambda methods (filter/map/reduce): module-level injection points
     for lambda FUNC_DEFs synthesized while checking a call site. */
  node_t module_item_list; /* the module's top-level N_LIST */
  node_t curr_module_item; /* module item currently being checked */
  node_t curr_lambda_def;  /* innermost synthetic lambda FUNC_DEF being checked */
  /* Same as module_item_list but NEVER cleared after the module walk: the
     ownership pass runs after check (when module_item_list is already
     restored to NULL) and still needs to append synthesized top-level
     functions (defer-cleanup thunks, ensure_defer_thunk). */
  node_t module_items_root;
  /* When non-zero (set across the module's first check pass), method bodies
     of class members are queued in `pending_method_bodies` instead of being
     checked immediately.  Restoring 0 around the drain prevents recursive
     deferral of bodies that are checked from within the drain itself. */
  int defer_method_bodies_p;
  VARR (pending_body_t) * pending_method_bodies;
};

#define curr_scope check_ctx->curr_scope
#define curr_class check_ctx->curr_class
#define label_uses check_ctx->label_uses
#define func_block_scope check_ctx->func_block_scope
#define curr_func_scope_num check_ctx->curr_func_scope_num
#define in_params_p check_ctx->in_params_p
#define jump_ret_p check_ctx->jump_ret_p
#define curr_func_has_try check_ctx->curr_func_has_try
#define curr_unnamed_anon_struct_union_member check_ctx->curr_unnamed_anon_struct_union_member
#define curr_switch check_ctx->curr_switch
#define func_decls_for_allocation check_ctx->func_decls_for_allocation
#define possible_incomplete_decls check_ctx->possible_incomplete_decls
#define n_i1_node check_ctx->n_i1_node
#define case_tab check_ctx->case_tab
#define curr_func_def check_ctx->curr_func_def
#define curr_loop check_ctx->curr_loop
#define curr_loop_switch check_ctx->curr_loop_switch
#define curr_call_arg_area_offset check_ctx->curr_call_arg_area_offset
#define context_stack check_ctx->context_stack
#define module_item_list check_ctx->module_item_list
#define curr_module_item check_ctx->curr_module_item
#define module_items_root check_ctx->module_items_root
#define curr_lambda_def check_ctx->curr_lambda_def
#define in_unowned_p check_ctx->in_unowned_p
#define in_owned_p check_ctx->in_owned_p
#define defer_method_bodies_p check_ctx->defer_method_bodies_p
#define pending_method_bodies check_ctx->pending_method_bodies


static int supported_alignment_p (mir_llong align MIR_UNUSED) { return TRUE; }  // ???

static int symbol_eq (symbol_t s1, symbol_t s2, void *arg MIR_UNUSED) {
  return s1.mode == s2.mode && s1.id->u.s.s == s2.id->u.s.s && s1.scope == s2.scope;
}

static htab_hash_t symbol_hash (symbol_t s, void *arg MIR_UNUSED) {
  return (htab_hash_t) (mir_hash_finish (
    mir_hash_step (mir_hash_step (mir_hash_step (mir_hash_init (0x42), (uint64_t) s.mode),
                                  (uint64_t) s.id->u.s.s),
                   (uint64_t) s.scope)));
}

static void symbol_clear (symbol_t sym, void *arg MIR_UNUSED) { VARR_DESTROY (node_t, sym.defs); }

static void symbol_init (c2m_ctx_t c2m_ctx) {
  MIR_alloc_t alloc = c2m_alloc (c2m_ctx);
  HTAB_CREATE_WITH_FREE_FUNC (symbol_t, symbol_tab, alloc, 5000, symbol_hash, symbol_eq, symbol_clear,
                              NULL);
}

static int symbol_find (c2m_ctx_t c2m_ctx, enum symbol_mode mode, node_t id, node_t scope,
                        symbol_t *res) {
  int found_p;
  symbol_t el, symbol;

  symbol.mode = mode;
  symbol.id = id;
  symbol.scope = scope;
  found_p = HTAB_DO (symbol_t, symbol_tab, symbol, HTAB_FIND, el);
  if (res != NULL && found_p) *res = el;
  return found_p;
}

static void symbol_insert (c2m_ctx_t c2m_ctx, enum symbol_mode mode, node_t id, node_t scope,
                           node_t def_node, node_t aux_node) {
  MIR_alloc_t alloc = c2m_alloc (c2m_ctx);
  symbol_t el, symbol;
  const char *smode = (mode == S_TAG ? "s_tag" :
                       mode == S_REGULARS ? "s_regular" : "s_label");

  if (c2m_options->verbose_p) {
      printf("symbol_insert: %s type %s class ? %s\n", id->u.s.s, smode, c2m_ctx->curr_class?"YES":"NO");
      if( strcmp(id->u.s.s, "myClass")==0) {
        printf("MYCLASS FOUND\n");
      }
  }

  symbol.mode = mode;
  symbol.id = id;
  symbol.scope = scope;
  symbol.def_node = def_node;
  symbol.aux_node = aux_node;
  //if(scope == NULL)
  //      printf("COMPILER: symbol_insert %s %s scope = NULL\n", id->u.s.s, smode);

  VARR_CREATE (node_t, symbol.defs, alloc, 4);
  VARR_PUSH (node_t, symbol.defs, def_node);
  HTAB_DO (symbol_t, symbol_tab, symbol, HTAB_INSERT, el);
}

static void symbol_def_replace (c2m_ctx_t c2m_ctx, symbol_t symbol, node_t def_node) {
  MIR_alloc_t alloc = c2m_alloc (c2m_ctx);
  symbol_t el;
  VARR (node_t) * defs;

  VARR_CREATE (node_t, defs, alloc, 4);
  for (size_t i = 0; i < VARR_LENGTH (node_t, symbol.defs); i++)
    VARR_PUSH (node_t, defs, VARR_GET (node_t, symbol.defs, i));
  symbol.defs = defs;
  symbol.def_node = def_node;
  HTAB_DO (symbol_t, symbol_tab, symbol, HTAB_REPLACE, el);
}

static void symbol_finish (c2m_ctx_t c2m_ctx) {
  if (symbol_tab != NULL) HTAB_DESTROY (symbol_t, symbol_tab);
}

static void symbol_dump_one (symbol_t sym, void *arg) {
  c2m_ctx_t c2m_ctx = (c2m_ctx_t) arg;
  pos_t pos = no_pos;

  if (sym.def_node != NULL)
    pos = POS(sym.def_node);

  const char *smode = (sym.mode == S_TAG ? "s_tag" :
                       sym.mode == S_REGULARS ? "s_regular" : "s_label");

  fprintf (stderr, "Symbol: %s (%s), at %s:%d:%d\n",
           sym.id->u.s.s, smode,
           pos.fname != NULL ? pos.fname : "<unknown>",
           pos.lno, pos.ln_pos);
}
static void symbol_dump (c2m_ctx_t c2m_ctx, FILE *f) {
  fprintf (stderr, "==== SYMBOL DUMP ====\n");
  HTAB_FOREACH_ELEM (symbol_t, symbol_tab, symbol_dump_one, c2m_ctx);
  fprintf (stderr, "=====================\n");
}

enum basic_type get_int_basic_type (size_t s) {
  return (s == sizeof (mir_int)     ? TP_INT
          : s == sizeof (mir_short) ? TP_SHORT
          : s == sizeof (mir_long)  ? TP_LONG
          : s == sizeof (mir_schar) ? TP_SCHAR
                                    : TP_LLONG);
}

static int type_qual_eq_p (const struct type_qual *tq1, const struct type_qual *tq2) {
  return (tq1->const_p == tq2->const_p && tq1->restrict_p == tq2->restrict_p
          && tq1->volatile_p == tq2->volatile_p && tq1->atomic_p == tq2->atomic_p);
}

static void clear_type_qual (struct type_qual *tq) {
  tq->const_p = tq->restrict_p = tq->volatile_p = tq->atomic_p = FALSE;
}

static int type_qual_subset_p (const struct type_qual *tq1, const struct type_qual *tq2) {
  return (tq1->const_p <= tq2->const_p && tq1->restrict_p <= tq2->restrict_p
          && tq1->volatile_p <= tq2->volatile_p && tq1->atomic_p <= tq2->atomic_p);
}

static struct type_qual type_qual_union (const struct type_qual *tq1, const struct type_qual *tq2) {
  struct type_qual res;

  res.const_p = tq1->const_p || tq2->const_p;
  res.restrict_p = tq1->restrict_p || tq2->restrict_p;
  res.volatile_p = tq1->volatile_p || tq2->volatile_p;
  res.atomic_p = tq1->atomic_p || tq2->atomic_p;
  return res;
}

static void init_type (struct type *type) {
  clear_type_qual (&type->type_qual);
  type->mode = TM_UNDEF;
  type->pos_node = NULL;
  type->arr_type = NULL;
  type->antialias = 0;
  type->align = -1;
  type->raw_size = MIR_SIZE_MAX;
  type->func_type_before_adjustment_p = FALSE;
  type->unnamed_anon_struct_union_member_type_p = FALSE;
}

static void set_type_pos_node (struct type *type, node_t n) {
  if (type->pos_node == NULL) type->pos_node = n;
}

static int char_type_p (const struct type *type) {
  return (type->mode == TM_BASIC
          && (type->u.basic_type == TP_CHAR || type->u.basic_type == TP_SCHAR
              || type->u.basic_type == TP_UCHAR));
}

static int standard_integer_type_p (const struct type *type) {
  return (type->mode == TM_BASIC && type->u.basic_type >= TP_BOOL
          && type->u.basic_type <= TP_ULLONG);
}

static int integer_type_p (const struct type *type) {
  return standard_integer_type_p (type) || type->mode == TM_ENUM;
}

static enum basic_type get_enum_basic_type (const struct type *type);

static int signed_integer_type_p (const struct type *type) {
  if (standard_integer_type_p (type)) {
    enum basic_type tp = type->u.basic_type;

    return ((tp == TP_CHAR && char_is_signed_p ()) || tp == TP_SCHAR || tp == TP_SHORT
            || tp == TP_INT || tp == TP_LONG || tp == TP_LLONG);
  }
  if (type->mode == TM_ENUM) {
    enum basic_type basic_type = get_enum_basic_type (type);
    return (basic_type == TP_INT || basic_type == TP_LONG || basic_type == TP_LLONG);
  }
  return FALSE;
}

static int floating_type_p (const struct type *type) {
  return type->mode == TM_BASIC
         && (type->u.basic_type == TP_FLOAT || type->u.basic_type == TP_DOUBLE
             || type->u.basic_type == TP_LDOUBLE);
}

static int arithmetic_type_p (const struct type *type) {
  return integer_type_p (type) || floating_type_p (type);
}

static int string_type_p (const struct type *type) {
    if (type->mode == TM_PTR && type->pos_node && type->pos_node->code == N_STR)
        return 1;
    return (type->mode == TM_BASIC || type->mode == TM_PTR)
        && (type->u.basic_type == TP_STRING || type->mode == TM_PTR);
}

/* True only for the built-in String basic type (TP_STRING), not arbitrary
   pointers.  Used to recognize `String`-typed receivers of method calls. */
static int builtin_string_type_p (const struct type *type) {
  return type != NULL && type->mode == TM_BASIC && type->u.basic_type == TP_STRING;
}

/* For the String `+` concatenation extension: true when an operand contributes a
   string value to a concatenation.  That is a built-in `String` value, a bare
   narrow string literal, or a char-array type that came from a string literal.
   Ordinary `char *` pointers are deliberately excluded so that normal C11
   pointer arithmetic on pointers is preserved. */
static int str_concat_string_operand_p (const struct type *type, node_t op) {
  if (builtin_string_type_p (type)) return 1;
  if (op != NULL && op->code == N_STR) return 1;
  if (type != NULL && type->mode == TM_ARR && type->pos_node != NULL
      && type->pos_node->code == N_STR)
    return 1;
  return 0;
}

/* Built-in method registry — String / ListString (and extensible types).

   Methods are *data* in a process-global table, not a giant if/else in the
   checker.  The table is seeded with the stock String methods at parse_init,
   and headers can add (or re-register) entries with C23 attributes:

     [[builtin_method("String", "titlecase", "c2m_str_titlecase", 0, "String")]]
     static int __bm_titlecase;   // dummy declarator so attrs attach

   Args: (type, method, runtime_symbol, nargs, retkind [, "static"]).
   retkind is one of: String, size_t, int, char*, ListString.
   type is "String" (instance), "StringStatic" (String.copy), or
   "ListString" (List<String> join).

   Known SM_* ids keep existing gen special cases (substr bounds, in-place
   replace, split→List).  Newly registered methods use SM_EXT and lower to a
   plain runtime call. */
enum str_method {
  SM_NONE = 0,
  SM_LENGTH,
  SM_EMPTY,
  SM_SUBSTR,
  SM_FIND,
  SM_REPLACE,
  SM_REPLACE_ALL,
  SM_UPPER,
  SM_LOWER,
  SM_COPY,
  SM_DETACH,
  SM_ATTACH,
  SM_STARTS_WITH,
  SM_ENDS_WITH,
  SM_CONTAINS,
  SM_TRIM,
  SM_SPLIT,
  SM_JOIN,
  SM_EQUALS,
  SM_EXT, /* header-registered, no special codegen */
};

enum builtin_ret_kind {
  BMR_NONE = 0,
  BMR_STRING,
  BMR_SIZE,
  BMR_INT,
  BMR_CHAR_PTR,
  BMR_LIST_STRING,
};

typedef struct {
  const char *type_name;  /* "String" | "StringStatic" | "ListString" */
  const char *method;     /* "trim" */
  const char *rt_name;    /* "c2m_str_trim" */
  int nargs;              /* user arg count (not including receiver) */
  int static_p;           /* 1 = type-name receiver (String.copy) */
  enum str_method sm;     /* SM_* for specials; SM_EXT for generic */
  enum builtin_ret_kind ret;
} builtin_method_t;

DEF_VARR (builtin_method_t);

/* Process-global so get_string_method() stays signature-stable (no c2m_ctx).
   Reset/seeded at the start of every parse. */
static VARR (builtin_method_t) *g_builtin_methods = NULL;

static enum builtin_ret_kind builtin_ret_from_name (const char *s) {
  if (s == NULL) return BMR_NONE;
  if (strcmp (s, "String") == 0) return BMR_STRING;
  if (strcmp (s, "size_t") == 0) return BMR_SIZE;
  if (strcmp (s, "int") == 0 || strcmp (s, "bool") == 0) return BMR_INT;
  if (strcmp (s, "char*") == 0 || strcmp (s, "charP") == 0) return BMR_CHAR_PTR;
  if (strcmp (s, "ListString") == 0 || strcmp (s, "List<String>") == 0
      || strcmp (s, "List_String") == 0)
    return BMR_LIST_STRING;
  return BMR_NONE;
}

/* Register one builtin method.  Idempotent on (type, method, nargs). */
static void register_builtin_method (const builtin_method_t *bm) {
  if (g_builtin_methods == NULL || bm == NULL || bm->method == NULL) return;
  for (size_t i = 0; i < VARR_LENGTH (builtin_method_t, g_builtin_methods); i++) {
    builtin_method_t *e = &VARR_ADDR (builtin_method_t, g_builtin_methods)[i];
    if (e->nargs == bm->nargs && e->method != NULL && strcmp (e->method, bm->method) == 0
        && e->type_name != NULL && bm->type_name != NULL
        && strcmp (e->type_name, bm->type_name) == 0)
      return; /* already present */
  }
  VARR_PUSH (builtin_method_t, g_builtin_methods, *bm);
}

static void seed_default_string_methods (void) {
  /* Stock String methods — same set previously hard-coded in get_string_method.
     Headers can re-declare these with [[builtin_method]] (no-ops due to
     idempotent register) or add new SM_EXT entries. */
  static const builtin_method_t defaults[] = {
    { "String", "length",       "c2m_str_length",       0, 0, SM_LENGTH,      BMR_SIZE },
    { "String", "empty",        "c2m_str_empty",        0, 0, SM_EMPTY,       BMR_INT },
    { "String", "substr",       "c2m_str_substr",       2, 0, SM_SUBSTR,      BMR_STRING },
    { "String", "find",         "c2m_str_find",         1, 0, SM_FIND,        BMR_SIZE },
    { "String", "replace",      "c2m_str_replace",      3, 0, SM_REPLACE,     BMR_STRING },
    { "String", "replace",      "c2m_str_replace_all",  2, 0, SM_REPLACE_ALL, BMR_STRING },
    { "String", "upper",        "c2m_str_upper",        0, 0, SM_UPPER,       BMR_STRING },
    { "String", "lower",        "c2m_str_lower",        0, 0, SM_LOWER,       BMR_STRING },
    { "String", "detach",       "c2m_str_detach",       0, 0, SM_DETACH,      BMR_CHAR_PTR },
    { "String", "starts_with",  "c2m_str_starts_with",  1, 0, SM_STARTS_WITH, BMR_INT },
    { "String", "ends_with",    "c2m_str_ends_with",    1, 0, SM_ENDS_WITH,   BMR_INT },
    { "String", "contains",     "c2m_str_contains",     1, 0, SM_CONTAINS,    BMR_INT },
    { "String", "trim",         "c2m_str_trim",         0, 0, SM_TRIM,        BMR_STRING },
    { "String", "split",        "c2m_str_split",        1, 0, SM_SPLIT,       BMR_LIST_STRING },
    { "String", "equals",       "c2m_str_equals",       1, 0, SM_EQUALS,      BMR_INT },
    { "StringStatic", "copy",   "c2m_str_copy",         2, 1, SM_COPY,        BMR_STRING },
    { "StringStatic", "attach", "c2m_str_attach",       1, 1, SM_ATTACH,      BMR_STRING },
    { "ListString", "join",     "c2m_str_join",         1, 0, SM_JOIN,        BMR_STRING },
  };
  for (size_t i = 0; i < sizeof (defaults) / sizeof (defaults[0]); i++)
    register_builtin_method (&defaults[i]);
}

static void builtin_methods_init (MIR_alloc_t alloc) {
  if (g_builtin_methods == NULL)
    VARR_CREATE (builtin_method_t, g_builtin_methods, alloc, 32);
  else
    VARR_TRUNC (builtin_method_t, g_builtin_methods, 0);
  seed_default_string_methods ();
}

static void builtin_methods_finish (void) {
  if (g_builtin_methods != NULL) {
    VARR_DESTROY (builtin_method_t, g_builtin_methods);
    g_builtin_methods = NULL;
  }
}

/* Find a registered method.  If want_nargs >= 0, require that arity; else the
   first entry with that method name.  type_name NULL matches any type that
   uses the legacy String-instance table (type "String"). */
static const builtin_method_t *find_builtin_method (const char *type_name,
                                                     const char *method,
                                                     int want_nargs) {
  if (g_builtin_methods == NULL || method == NULL) return NULL;
  for (size_t i = 0; i < VARR_LENGTH (builtin_method_t, g_builtin_methods); i++) {
    const builtin_method_t *e = &VARR_ADDR (builtin_method_t, g_builtin_methods)[i];
    if (e->method == NULL || strcmp (e->method, method) != 0) continue;
    if (type_name != NULL) {
      if (e->type_name == NULL || strcmp (e->type_name, type_name) != 0) continue;
    } else {
      if (e->type_name == NULL || strcmp (e->type_name, "String") != 0) continue;
    }
    if (want_nargs >= 0 && e->nargs != want_nargs) continue;
    return e;
  }
  return NULL;
}

/* Legacy API: map a method name (String instance/static shared names) to SM_*.
   Prefers the primary arity entry.  replace defaults to 3-arg form; callers
   re-resolve SM_REPLACE_ALL when actual arity is 2. */
static enum str_method get_string_method (const char *name, int *nargs,
                                          const char **rt_name) {
  const builtin_method_t *e;

  if (name == NULL) {
    if (nargs) *nargs = 0;
    if (rt_name) *rt_name = NULL;
    return SM_NONE;
  }
  /* Prefer instance methods; static names also live under StringStatic. */
  e = find_builtin_method ("String", name, -1);
  if (e == NULL) e = find_builtin_method ("StringStatic", name, -1);
  if (e == NULL) e = find_builtin_method ("ListString", name, -1);
  if (e == NULL) {
    if (nargs) *nargs = 0;
    if (rt_name) *rt_name = NULL;
    return SM_NONE;
  }
  if (nargs) *nargs = e->nargs;
  if (rt_name) *rt_name = e->rt_name;
  return e->sm;
}

/* Scan a declaration's attribute list for [[builtin_method(...)]]. */
static void register_builtin_methods_from_attrs (c2m_ctx_t c2m_ctx, node_t attrs) {
  if (attrs == NULL || attrs->code != N_LIST) return;
  for (node_t a = NL_HEAD (attrs->u.ops); a != NULL; a = NL_NEXT (a)) {
    node_t id, arglist, args[8];
    int n = 0;
    builtin_method_t bm;

    if (a->code != N_ATTR) continue;
    id = NL_HEAD (a->u.ops);
    if (id == NULL || id->code != N_ID || strcmp (id->u.s.s, "builtin_method") != 0)
      continue;
    arglist = NL_NEXT (id);
    if (arglist == NULL || arglist->code != N_LIST) {
      error (c2m_ctx, POS (a), "builtin_method expects (type, method, rt, nargs, ret[, \"static\"])");
      continue;
    }
    for (node_t arg = NL_HEAD (arglist->u.ops); arg != NULL && n < 8; arg = NL_NEXT (arg))
      args[n++] = arg;
    if (n < 5) {
      error (c2m_ctx, POS (a),
             "builtin_method needs at least 5 args: type, method, runtime, nargs, retkind");
      continue;
    }
    memset (&bm, 0, sizeof (bm));
    /* Arg 0: type name */
    if (args[0]->code == N_STR || args[0]->code == N_ID)
      bm.type_name = args[0]->u.s.s;
    else {
      error (c2m_ctx, POS (a), "builtin_method type must be a string or identifier");
      continue;
    }
    /* Arg 1: method name */
    if (args[1]->code == N_STR || args[1]->code == N_ID)
      bm.method = args[1]->u.s.s;
    else {
      error (c2m_ctx, POS (a), "builtin_method name must be a string or identifier");
      continue;
    }
    /* Arg 2: runtime symbol */
    if (args[2]->code == N_STR || args[2]->code == N_ID)
      bm.rt_name = args[2]->u.s.s;
    else {
      error (c2m_ctx, POS (a), "builtin_method runtime symbol must be a string or identifier");
      continue;
    }
    /* Arg 3: nargs (integer literal) */
    if (args[3]->code == N_I || args[3]->code == N_L || args[3]->code == N_LL)
      bm.nargs = (int) args[3]->u.l;
    else if (args[3]->code == N_U || args[3]->code == N_UL || args[3]->code == N_ULL)
      bm.nargs = (int) args[3]->u.ul;
    else {
      error (c2m_ctx, POS (a), "builtin_method nargs must be an integer constant");
      continue;
    }
    /* Arg 4: return kind */
    if (args[4]->code == N_STR || args[4]->code == N_ID)
      bm.ret = builtin_ret_from_name (args[4]->u.s.s);
    else {
      error (c2m_ctx, POS (a), "builtin_method retkind must be a string or identifier");
      continue;
    }
    if (bm.ret == BMR_NONE) {
      error (c2m_ctx, POS (a),
             "unknown builtin_method retkind (use String, size_t, int, char*, ListString)");
      continue;
    }
    /* Optional arg 5: "static" */
    if (n >= 6 && (args[5]->code == N_STR || args[5]->code == N_ID)
        && strcmp (args[5]->u.s.s, "static") == 0)
      bm.static_p = 1;
    if (bm.static_p && strcmp (bm.type_name, "String") == 0)
      bm.type_name = "StringStatic";
    /* Re-map well-known methods to SM_* so existing gen special cases fire;
       everything else is SM_EXT (plain rt call). */
    {
      const builtin_method_t *known = find_builtin_method (bm.type_name, bm.method, bm.nargs);
      if (known != NULL && known->sm != SM_EXT && known->sm != SM_NONE)
        bm.sm = known->sm;
      else
        bm.sm = SM_EXT;
    }
    /* Intern strings so they outlive the AST (stream buffers). */
    bm.type_name = uniq_cstr (c2m_ctx, bm.type_name).s;
    bm.method = uniq_cstr (c2m_ctx, bm.method).s;
    bm.rt_name = uniq_cstr (c2m_ctx, bm.rt_name).s;
    register_builtin_method (&bm);
  }
}

static int scalar_type_p (const struct type *type) {
  // RSD: Added string_type_p
  return arithmetic_type_p (type) || string_type_p (type) || type->mode == TM_PTR
         || type->mode == TM_DICT || type->mode == TM_SLICE;
}

static struct type get_ptr_int_type (int signed_p) {
  struct type res;

  init_type (&res);
  res.mode = TM_BASIC;
  if (sizeof (mir_int) == sizeof (mir_size_t)) {
    res.u.basic_type = signed_p ? TP_INT : TP_UINT;
    return res;
  } else if (sizeof (mir_long) == sizeof (mir_size_t)) {
    res.u.basic_type = signed_p ? TP_LONG : TP_ULONG;
    return res;
  }
  assert (sizeof (mir_llong) == sizeof (mir_size_t));
  res.u.basic_type = signed_p ? TP_LLONG : TP_ULLONG;
  return res;
}

static struct type integer_promotion (const struct type *type) {
  struct type res;

  assert (integer_type_p (type));
  init_type (&res);
  res.mode = TM_BASIC;
  if (type->mode == TM_BASIC && TP_LONG <= type->u.basic_type && type->u.basic_type <= TP_ULLONG) {
    res.u.basic_type = type->u.basic_type;
    return res;
  }
  if (type->mode == TM_BASIC
      && ((type->u.basic_type == TP_CHAR && MIR_CHAR_MAX > MIR_INT_MAX)
          || (type->u.basic_type == TP_UCHAR && MIR_UCHAR_MAX > MIR_INT_MAX)
          || (type->u.basic_type == TP_USHORT && MIR_USHORT_MAX > MIR_INT_MAX)))
    res.u.basic_type = TP_UINT;
  else if (type->mode == TM_ENUM)
    res.u.basic_type = get_enum_basic_type (type);
  else if (type->mode == TM_BASIC && type->u.basic_type == TP_UINT)
    res.u.basic_type = TP_UINT;
  else
    res.u.basic_type = TP_INT;
  return res;
}

static struct type arithmetic_conversion (const struct type *type1, const struct type *type2) {
  struct type res, t1, t2;

  assert (arithmetic_type_p (type1) && arithmetic_type_p (type2));
  init_type (&res);
  res.mode = TM_BASIC;
  if (floating_type_p (type1) || floating_type_p (type2)) {
    if ((type1->mode == TM_BASIC && type1->u.basic_type == TP_LDOUBLE)
        || (type2->mode == TM_BASIC && type2->u.basic_type == TP_LDOUBLE)) {
      res.u.basic_type = TP_LDOUBLE;
    } else if ((type1->mode == TM_BASIC && type1->u.basic_type == TP_DOUBLE)
               || (type2->mode == TM_BASIC && type2->u.basic_type == TP_DOUBLE)) {
      res.u.basic_type = TP_DOUBLE;
    } else if ((type1->mode == TM_BASIC && type1->u.basic_type == TP_FLOAT)
               || (type2->mode == TM_BASIC && type2->u.basic_type == TP_FLOAT)) {
      res.u.basic_type = TP_FLOAT;
    }
    return res;
  }
  t1 = integer_promotion (type1);
  t2 = integer_promotion (type2);
  if (signed_integer_type_p (&t1) == signed_integer_type_p (&t2)) {
    res.u.basic_type = t1.u.basic_type < t2.u.basic_type ? t2.u.basic_type : t1.u.basic_type;
  } else {
    struct type t;

    if (signed_integer_type_p (&t1)) SWAP (t1, t2, t);
    assert (!signed_integer_type_p (&t1) && signed_integer_type_p (&t2));
    if ((t1.u.basic_type == TP_ULONG && t2.u.basic_type < TP_LONG)
        || (t1.u.basic_type == TP_ULLONG && t2.u.basic_type < TP_LLONG)) {
      res.u.basic_type = t1.u.basic_type;
    } else if ((t1.u.basic_type == TP_UINT && t2.u.basic_type >= TP_LONG
                && MIR_LONG_MAX >= MIR_UINT_MAX)
               || (t1.u.basic_type == TP_ULONG && t2.u.basic_type >= TP_LLONG
                   && MIR_LLONG_MAX >= MIR_ULONG_MAX)) {
      res.u.basic_type = t2.u.basic_type;
    } else {
      res.u.basic_type = t1.u.basic_type;
    }
  }
  return res;
}

/* Per-dereference-site guard classification produced by the ownership pass and
   consumed by gen (see struct expr::own_deref_class). */
enum deref_guard_class { DEREF_GUARD_DEFAULT = 0, DEREF_GUARD_SAFE = 1, DEREF_GUARD_CHECK = 2 };

struct expr {
  unsigned int const_p : 1, const_addr_p : 1, builtin_call_p : 1;
  /* Dict-to-class bind cast flags (set on N_CAST nodes):
     bind_p     : 1 iff this is the (T)dict / (T?)dict synthesized binder.
     lenient_p  : 1 iff the `?` form (no throw on missing/mismatched fields). */
  unsigned int bind_p : 1, lenient_p : 1;
  /* Ownership-directed dereference classification (set by the ownership pass on
     N_DEREF / N_DEREF_FIELD / N_IND nodes).  Drives gen's per-site guard
     decision.  Values (see enum deref_guard_class):
       DEREF_GUARD_DEFAULT (0) — not classified: emit the usual null guard.
       DEREF_GUARD_SAFE    (1) — receiver proven live and non-null (a currently
                                 `OS_OWNED` binding acquired via `new`, whose
                                 OOM check guarantees non-null): elide the guard.
       DEREF_GUARD_CHECK   (2) — receiver is a heap object the pass could tag
                                 but whose liveness is NOT statically proven
                                 (join-widened to MaybeOwned): today it still
                                 gets the null guard; this is the future home of
                                 the object generation-tag use-after-free check.
     Defaults 0, so any site the pass didn't classify (or when -fno-ownership is
     set) keeps its guard. */
  unsigned int own_deref_class : 2;
  /* class[i] / map[k] uses GetMut → true element lvalue (not Get by-value copy). */
  unsigned int mut_sub_p : 1;
  /* Midopt: static array index proved in-range (const index + known length).
     gen elides the OOB safety trap when set. */
  unsigned int elide_oob_p : 1;
  /* Midopt R-LICM: this N_CALL is a proven loop-invariant, side-effect-free
     accessor (e.g. `recv.Count()`) used as a counted-loop bound whose receiver
     is never mutated in the loop.  gen memoizes its pre-header value and reuses
     it at the loop-bottom condition instead of re-calling each iteration. */
  unsigned int hoist_call_p : 1;
  /* Midopt C2: divisor interval does not contain 0 — gen skips div0 trap.
     Set on N_DIV / N_MOD / N_DIV_ASSIGN / N_MOD_ASSIGN. */
  unsigned int elide_div0_p : 1;
  /* Midopt C2: shift count interval is inside [0, width) — gen skips range trap.
     Set on N_LSH / N_RSH / N_LSH_ASSIGN / N_RSH_ASSIGN. */
  unsigned int elide_shift_p : 1;
  /* Midopt C2: signed MIN / -1 overflow is impossible on this divide. */
  unsigned int elide_div_ovf_p : 1;
  union {
    node_t lvalue_node;       /* for id, str, field, deref field, ind, deref, compound literal */
    node_t label_addr_target; /* for label address */
  } u;
  node_t def_node;    /* defined for id or const address (ref) or label address node */
  struct type *type;  /* type of the result */
  struct type *type2; /* used for assign expr type */
  union {             /* defined for const or const addr (i_val is offset) */
    mir_llong i_val;
    mir_ullong u_val;
    mir_ldouble d_val;
  } c;
  /* Unprototyped call (`T f();`) site-specific proto; NULL otherwise. */
  MIR_item_t call_proto_item;
};

struct decl_spec {
  unsigned int typedef_p : 1, extern_p : 1, static_p : 1;
  unsigned int auto_p : 1, register_p : 1, thread_local_p : 1;
  unsigned int inline_p : 1, no_return_p : 1; /* func specifiers  */
  int align;                                  // negative value means undefined
  node_t align_node;                          //  strictest valid N_ALIGNAS node
  node_code_t linkage;  // N_IGNORE - none, N_STATIC - internal, N_EXTERN - external
  struct type *type;
};

struct enum_type {
  enum basic_type enum_basic_type;
};

struct enum_value {
  union {
    mir_llong i_val;
    mir_ullong u_val;
  } u;
};

struct node_scope {
  unsigned char stack_var_p; /* necessity for frame */
  unsigned func_scope_num;
  mir_size_t size, offset, call_arg_area_size;
  node_t scope;
};

struct decl {
  /* true if address is taken, reg can be used or is used: */
  unsigned addr_p : 1, reg_p : 1, asm_p : 1, used_p : 1;
  /* Marked by the check pass when this is a local declaration whose
     initializer matches a recognized resource-acquire pattern
     (`new T(...)` today; `malloc`/`fopen`/`strdup`/... when the upcoming
     ownership pass lands).  The auto-defer-delete *synthesis* is intentionally
     deferred to that pass — today we only set the bit so the future analyzer
     has a starting set of candidates without retracing the AST.
     Cleared whenever the declaration is wrapped in `unowned`.
     See `src/ownership.c` for the planned state machine. */
  unsigned auto_defer_p : 1;
  /* Set when this local declaration was wrapped in `unowned` (the user is
     taking manual responsibility for its lifetime).  Persisted on the decl
     — distinct from the transient check-time `in_unowned_p` — so the
     ownership pass (src/ownership.c) can skip the binding for BOTH leak
     diagnostics and -fauto-release synthesis, including the
     interprocedural call-returned-owner path that `auto_defer_p` does not
     cover. */
  unsigned unowned_p : 1;
  /* Set when this local declaration is part of the managed ownership layer
     (declared `owned`, or initialized by `new`/`move` of an owned value).
     The ownership pass (src/ownership.c) tracks these bindings as single-owner,
     move-only, and guarantees a single scope-exit release unless moved out. */
  unsigned owned_p : 1;
  /* Midopt (check→gen): proved unreachable from live roots.  gen skips body
     emission and forward MIR items.  Class methods (P0) and internal-linkage
     (`static`) free functions (C16) may be marked; exported C functions are
     never pruned. See src/midopt.c and DOC/MIDOPT-C.md. */
  unsigned midopt_dead_p : 1;
  /* Midopt (R2): for-in element loop var proven read-only over an unmutated
     dense collection — gen binds it by reference (pointer into the buffer)
     instead of a per-iteration block copy. */
  unsigned byref_p : 1;
  /* `__mirc_attribute__((da_ignore))` / `__attribute__((da_ignore))` /
     `classyc_da_ignore` on a binding OR on the enclosing function/method.
     Function-level: set on the N_FUNC_DEF's decl during create_decl from
     trailing attrs (PRECHECK_DA_IGNORE).  Prefer `__mirc_attribute__` or
     CLASSYC_DA_IGNORE after system headers — glibc empties `__attribute__`
     when not building as GCC.  Suppresses definite-assignment noise and
     String-out-param scope warnings in stdlib methods without silencing
     all .h files. */
  unsigned da_ignore_p : 1;
  int bit_offset, width; /* for bitfields, -1 bit_offset for non bitfields. */
  mir_size_t offset;     /* var offset in frame or bss */
  /* Extra stack bytes for a class/struct local whose trailing flexible array
     member is brace-initialized with more elements than the (cached) type
     layout accounts for.  Without it the initializer overruns the frame. */
  mir_size_t flex_extra_size;
  node_t scope;          /* declaration scope */
  /* The next 2 members are used only for param decls. The 1st member is number of the start MIR
     func arg. The 2nd one is number or MIR func args used to pass param value, it is positive
     only for aggregates passed by value.  */
  uint32_t param_args_start, param_args_num;
  struct decl_spec decl_spec;
  /* Unnamed member if this scope is anon struct/union for the member,
     NULL otherwise: */
  node_t containing_unnamed_anon_struct_union_member;
  /* RAII support for automatic (stack) class objects: when a local variable of
     a class type with a constructor/destructor is declared, these hold checked
     `var.__ctor_C(...)` / `var.__dtor_C()` method-call nodes (NULL otherwise).
     gen emits the ctor at the declaration and registers the dtor at scope exit
     (via the defer machinery) — no free, since the storage is automatic. */
  node_t ctor_call, dtor_call;
  /* Auto-synthesized `free(p)` (or release-fn) N_CALL deferred at scope exit.
     Populated by src/ownership.c when -fauto-release proves the binding leaks
     on every reachable function exit. NULL otherwise.  Registered alongside
     dtor_call in the gen pass so it runs through the existing defer machinery. */
  node_t auto_release_call;
  union {
    const char *asm_str; /* register name for global reg used and defined only if asm_p */
    MIR_item_t item;     /* MIR_item for some declarations */
  } u;
  c2m_ctx_t c2m_ctx;
};

static enum basic_type get_enum_basic_type (const struct type *type) {
  assert (type->mode == TM_ENUM);
  if (type->u.tag_type->attr == NULL) return TP_INT; /* in case of undefined enum */
  return ((struct enum_type *) type->u.tag_type->attr)->enum_basic_type;
}

static struct decl_spec *get_param_decl_spec (node_t param) {
  node_t MIR_UNUSED declarator;

  if (param->code == N_TYPE) return param->attr;
  declarator = NL_EL (param->u.ops, 1);
  assert (param->code == N_SPEC_DECL && declarator != NULL && declarator->code == N_DECL);
  return &((decl_t) param->attr)->decl_spec;
}

static int type_eq_p (struct type *type1, struct type *type2) {
  if (type1->mode != type2->mode) return FALSE;
  if (!type_qual_eq_p (&type1->type_qual, &type2->type_qual)) return FALSE;
  switch (type1->mode) {
  case TM_BASIC: return type1->u.basic_type == type2->u.basic_type;
  case TM_ENUM:
  case TM_CLASS:
  case TM_STRUCT:
  case TM_UNION: return type1->u.tag_type == type2->u.tag_type;
  case TM_PTR: return type_eq_p (type1->u.ptr_type, type2->u.ptr_type);
  case TM_SLICE: return type_eq_p (type1->u.ptr_type, type2->u.ptr_type);
  case TM_ARR: {
    struct expr *cexpr1, *cexpr2;
    struct arr_type *at1 = type1->u.arr_type, *at2 = type2->u.arr_type;

    return (at1->static_p == at2->static_p && type_eq_p (at1->el_type, at2->el_type)
            && type_qual_eq_p (&at1->ind_type_qual, &at2->ind_type_qual)
            && at1->size->code != N_IGNORE && at2->size->code != N_IGNORE
            && (cexpr1 = at1->size->attr)->const_p && (cexpr2 = at2->size->attr)->const_p
            && integer_type_p (cexpr2->type) && integer_type_p (cexpr2->type)
            && cexpr1->c.i_val == cexpr2->c.i_val);
  }
  case TM_FUNC: {
    struct func_type *ft1 = type1->u.func_type, *ft2 = type2->u.func_type;
    struct decl_spec *ds1, *ds2;

    if (ft1->dots_p != ft2->dots_p || !type_eq_p (ft1->ret_type, ft2->ret_type)
        || NL_LENGTH (ft1->param_list->u.ops) != NL_LENGTH (ft2->param_list->u.ops))
      return FALSE;
    for (node_t p1 = NL_HEAD (ft1->param_list->u.ops), p2 = NL_HEAD (ft2->param_list->u.ops);
         p1 != NULL; p1 = NL_NEXT (p1), p2 = NL_NEXT (p2)) {
      ds1 = get_param_decl_spec (p1);
      ds2 = get_param_decl_spec (p2);
      if (!type_eq_p (ds1->type, ds2->type)) return FALSE;
      // ??? other qualifiers
    }
    return TRUE;
  }
  default: return FALSE;
  }
}

static int compatible_types_p (struct type *type1, struct type *type2, int ignore_quals_p) {
  if (type1->mode != type2->mode) {
    if (type1->mode == TM_CLASS && type2->mode == TM_PTR)
      return TRUE;
    if (!ignore_quals_p && !type_qual_eq_p (&type1->type_qual, &type2->type_qual)) return FALSE;
    if (type1->mode == TM_ENUM && type2->mode == TM_BASIC)
      return type2->u.basic_type == get_enum_basic_type (type1);
    if (type2->mode == TM_ENUM && type1->mode == TM_BASIC)
      return type1->u.basic_type == get_enum_basic_type (type2);
    return FALSE;
  }
  if (type1->mode == TM_BASIC) {
    return (type1->u.basic_type == type2->u.basic_type
            && (ignore_quals_p || type_qual_eq_p (&type1->type_qual, &type2->type_qual)));
  } else if (type1->mode == TM_PTR) {
    return ((ignore_quals_p || type_qual_eq_p (&type1->type_qual, &type2->type_qual))
            && compatible_types_p (type1->u.ptr_type, type2->u.ptr_type, ignore_quals_p));
  } else if (type1->mode == TM_ARR) {
    struct expr *cexpr1, *cexpr2;
    struct arr_type *at1 = type1->u.arr_type, *at2 = type2->u.arr_type;

    if (!compatible_types_p (at1->el_type, at2->el_type, ignore_quals_p)) return FALSE;
    if (at1->size->code == N_IGNORE || at2->size->code == N_IGNORE) return TRUE;
    if ((cexpr1 = at1->size->attr)->const_p && (cexpr2 = at2->size->attr)->const_p
        && integer_type_p (cexpr2->type) && integer_type_p (cexpr2->type))
      return cexpr1->c.i_val == cexpr2->c.i_val;
    return TRUE;
  } else if (type1->mode == TM_FUNC) {
    struct func_type *ft1 = type1->u.func_type, *ft2 = type2->u.func_type;

    if (NL_HEAD (ft1->param_list->u.ops) != NULL && NL_HEAD (ft2->param_list->u.ops) != NULL
        && NL_LENGTH (ft1->param_list->u.ops) != NL_LENGTH (ft2->param_list->u.ops))
      return FALSE;
    // ??? check parameter types
  } else {
    assert (type1->mode == TM_STRUCT || type1->mode == TM_UNION || type1->mode == TM_ENUM
            || type1->mode == TM_CLASS || type1->mode == TM_DICT);
    return (type1->u.tag_type == type2->u.tag_type
            && (ignore_quals_p || type_qual_eq_p (&type1->type_qual, &type2->type_qual)));
  }
  return TRUE;
}

static struct type composite_type (c2m_ctx_t c2m_ctx, struct type *tp1, struct type *tp2) {
  struct type t = *tp1;

  assert (compatible_types_p (tp1, tp2, TRUE));
  if (tp1->mode == TM_ARR) {
    struct arr_type *arr_type;

    t.u.arr_type = arr_type = reg_malloc (c2m_ctx, sizeof (struct arr_type));
    *arr_type = *tp1->u.arr_type;
    if (arr_type->size->code == N_IGNORE) arr_type->size = tp2->u.arr_type->size;
    *arr_type->el_type
      = composite_type (c2m_ctx, tp1->u.arr_type->el_type, tp2->u.arr_type->el_type);
  } else if (tp1->mode == TM_FUNC) { /* ??? */
  }
  return t;
}

static struct type *create_type (c2m_ctx_t c2m_ctx, struct type *copy) {
  struct type *res = reg_malloc (c2m_ctx, sizeof (struct type));

  if (copy == NULL)
    init_type (res);
  else
    *res = *copy;
  return res;
}

DEF_DLIST_LINK (case_t);

struct case_attr {
  node_t case_node, case_target_node;
  DLIST_LINK (case_t) case_link;
};

DEF_DLIST (case_t, case_link);

struct switch_attr {
  struct type type; /* integer promoted type */
  int ranges_p;
  case_t min_val_case, max_val_case;
  DLIST (case_t) case_labels; /* default case is always a tail */
};

static int basic_type_size (enum basic_type bt) {
  switch (bt) {
  case TP_BOOL: return sizeof (mir_bool);
  case TP_CHAR: return sizeof (mir_char);
  case TP_SCHAR: return sizeof (mir_schar);
  case TP_UCHAR: return sizeof (mir_uchar);
  case TP_SHORT: return sizeof (mir_short);
  case TP_USHORT: return sizeof (mir_ushort);
  case TP_INT: return sizeof (mir_int);
  case TP_UINT: return sizeof (mir_uint);
  case TP_LONG: return sizeof (mir_long);
  case TP_ULONG: return sizeof (mir_ulong);
  case TP_LLONG: return sizeof (mir_llong);
  case TP_ULLONG: return sizeof (mir_ullong);
  case TP_FLOAT: return sizeof (mir_float);
  case TP_DOUBLE: return sizeof (mir_double);
  case TP_LDOUBLE: return sizeof (mir_ldouble);
  case TP_VOID: return 1;  // ???
  case TP_STRING:
    /* Built-in String is a pointer-width handle, not a C basic type. */
    return (int) sizeof (mir_size_t);
  default: abort ();
  }
}

static int basic_type_align (enum basic_type bt) {
#ifdef MIR_LDOUBLE_ALIGN
  if (bt == TP_LDOUBLE) return MIR_LDOUBLE_ALIGN;
#endif
  return basic_type_size (bt);
}

static int type_align (struct type *type) {
  assert (type->align >= 0);
  return type->align;
}

static int incomplete_type_p (c2m_ctx_t c2m_ctx, struct type *type);

static void aux_set_type_align (c2m_ctx_t c2m_ctx, struct type *type) {
  /* Should be called only from set_type_layout. */
  int align, member_align;

  if (type->align >= 0) return;
  if (type->mode == TM_BASIC) {
    align = basic_type_align (type->u.basic_type);
  } else if (type->mode == TM_PTR) {
    align = sizeof (mir_size_t);
  } else if (type->mode == TM_ENUM) {
    align = basic_type_align (get_enum_basic_type (type));
  } else if (type->mode == TM_FUNC) {
    align = sizeof (mir_size_t);
  } else if (type->mode == TM_ARR) {
    align = type_align (type->u.arr_type->el_type);
  } else if (type->mode == TM_UNDEF) {
    align = 0; /* error type */
  } else if (type->mode == TM_DICT || type->mode == TM_SLICE) {
    align = sizeof(void*);
  } else {
    assert (type->mode == TM_STRUCT || type->mode == TM_UNION || type->mode == TM_CLASS || type->mode == TM_DICT);
    if (incomplete_type_p (c2m_ctx, type)) {
      align = -1;
    } else {
      align = 1;
      for (node_t member = NL_HEAD (TAG_MEMBER_LIST (type->u.tag_type)->u.ops); member != NULL;
           member = NL_NEXT (member))
        if (member->code == N_MEMBER) {
          decl_t decl = member->attr;
          node_t width = MEMBER_WIDTH (member);
          struct expr *expr;

          if (type->mode == TM_UNION && width->code != N_IGNORE && (expr = width->attr)->const_p
              && expr->c.u_val == 0)
            continue;
          member_align = type_align (decl->decl_spec.type);
          /* member-level _Alignas/aligned over-aligns the field (1fdf44d8). */
          if (decl->decl_spec.align > member_align) member_align = decl->decl_spec.align;
          if (align < member_align) align = member_align;
        }
    }
  }
  type->align = align;
}

static mir_size_t type_size (c2m_ctx_t c2m_ctx, struct type *type) {
  mir_size_t size = raw_type_size (c2m_ctx, type);

  return type->align == 0 ? size : round_size (size, type->align);
}

static mir_size_t var_align (c2m_ctx_t c2m_ctx, struct type *type) {
  int align;

  raw_type_size (c2m_ctx, type); /* check */
  align = type->align;
  assert (align >= 0);
#ifdef ADJUST_VAR_ALIGNMENT
  align = ADJUST_VAR_ALIGNMENT (c2m_ctx, align, type);
#endif
  return align;
}

static mir_size_t var_size (c2m_ctx_t c2m_ctx, struct type *type) {
  mir_size_t size = raw_type_size (c2m_ctx, type);

  return round_size (size, var_align (c2m_ctx, type));
}

/* BOUND_BIT is used only if BF_P and updated only if BITS >= 0  */
static void update_field_layout (int *bf_p, mir_size_t *overall_size, mir_size_t *offset,
                                 int *bound_bit, mir_size_t prev_field_type_size,
                                 mir_size_t field_type_size, int field_type_align, int bits) {
  mir_size_t start_offset, curr_offset, prev_field_offset = *offset;

  assert (field_type_size > 0 && field_type_align > 0);
  start_offset = curr_offset
    = (*overall_size + field_type_align - 1) / field_type_align * field_type_align;
  if ((long) start_offset < field_type_align && bits >= 0) *bound_bit = 0;
  for (;; start_offset = curr_offset) {
    if ((long) curr_offset < field_type_align) {
      if (bits >= 0) *bound_bit += bits;
      break;
    }
    curr_offset -= field_type_align;
    if (!*bf_p) { /* previous is a regular field: */
      if (curr_offset < prev_field_offset + prev_field_type_size) {
        if (bits >= 0) {
          *bound_bit
            = (int) (prev_field_offset + prev_field_type_size - curr_offset) * MIR_CHAR_BIT;
          if (*bound_bit + bits <= (long) field_type_size * MIR_CHAR_BIT) continue;
          *bound_bit = bits;
          if (prev_field_offset + prev_field_type_size > start_offset)
            *bound_bit
              += (int) (prev_field_offset + prev_field_type_size - start_offset) * MIR_CHAR_BIT;
        }
        break;
      }
    } else if (bits < 0) { /* bitfield then regular field: */
      if (curr_offset < prev_field_offset + (*bound_bit + MIR_CHAR_BIT - 1) / MIR_CHAR_BIT) break;
    } else { /* bitfield then another bitfield: */
      if ((curr_offset + field_type_size) * MIR_CHAR_BIT
          < prev_field_offset * MIR_CHAR_BIT + *bound_bit + bits) {
        if (start_offset * MIR_CHAR_BIT >= prev_field_offset * MIR_CHAR_BIT + *bound_bit) {
          *bound_bit = bits;
        } else {
          *bound_bit = (int) (prev_field_offset * MIR_CHAR_BIT + *bound_bit + bits
                              - start_offset * MIR_CHAR_BIT);
        }
        break;
      }
    }
  }
  *bf_p = bits >= 0;
  *offset = start_offset;
  if (*overall_size < start_offset + field_type_size)
    *overall_size = start_offset + field_type_size;
}

/* Update offsets inside unnamed anonymous struct/union member. */
static void update_members_offset (struct type *type, mir_size_t offset) {
  assert ((type->mode == TM_STRUCT || type->mode == TM_UNION || type->mode == TM_CLASS)
          && type->unnamed_anon_struct_union_member_type_p);
  assert (offset != MIR_SIZE_MAX || type->raw_size == MIR_SIZE_MAX);
  for (node_t el = NL_HEAD (TAG_MEMBER_LIST (type->u.tag_type)->u.ops); el != NULL;
       el = NL_NEXT (el))
    if (el->code == N_MEMBER) {
      decl_t decl = el->attr;

      decl->offset = offset == MIR_SIZE_MAX ? 0 : decl->offset + offset;
      if (decl->decl_spec.type->unnamed_anon_struct_union_member_type_p)
        update_members_offset (decl->decl_spec.type,
                               offset == MIR_SIZE_MAX ? offset : decl->offset);
    }
}

/* ── Trailing flexible-array members (`T a[]` / `T a[0]` / C89 `T a[1]`) ── */

static const char *member_decl_name (node_t mem) {
  node_t md, id;

  if (mem == NULL || mem->code != N_MEMBER) return NULL;
  md = MEMBER_DECL (mem);
  if (md == NULL || md->code != N_DECL) return NULL;
  id = NL_HEAD (md->u.ops);
  if (id == NULL || id->code != N_ID) return NULL;
  return id->u.s.s;
}

static int fam_name_ieq (const char *a, const char *b) {
  if (a == NULL || b == NULL) return 0;
  while (*a != '\0' && *b != '\0') {
    unsigned ca = (unsigned char) *a++, cb = (unsigned char) *b++;
    if (ca >= 'A' && ca <= 'Z') ca += 32;
    if (cb >= 'A' && cb <= 'Z') cb += 32;
    if (ca != cb) return 0;
  }
  return *a == *b;
}

/* How likely `name` is a live length/capacity next to a trailing FAM.
   >= 80: attach at layout time.  40–79: attach if it is the only integer
   sibling, or if ownership later sees it used as a bound. */
static int fam_bound_name_score (const char *name) {
  if (name == NULL || name[0] == '\0') return 0;
  if (fam_name_ieq (name, "nAlloc") || fam_name_ieq (name, "n_alloc")
      || fam_name_ieq (name, "nalloc") || fam_name_ieq (name, "nCapacity")
      || fam_name_ieq (name, "ncapacity"))
    return 100;
  if (fam_name_ieq (name, "capacity") || fam_name_ieq (name, "alloc")
      || fam_name_ieq (name, "cap"))
    return 90;
  if (fam_name_ieq (name, "nExpr") || fam_name_ieq (name, "nItems")
      || fam_name_ieq (name, "nUsed") || fam_name_ieq (name, "nCount")
      || fam_name_ieq (name, "nLen") || fam_name_ieq (name, "nLength")
      || fam_name_ieq (name, "nSize") || fam_name_ieq (name, "nslots")
      || fam_name_ieq (name, "nelem") || fam_name_ieq (name, "nmemb"))
    return 55;
  if (fam_name_ieq (name, "count") || fam_name_ieq (name, "length")
      || fam_name_ieq (name, "len") || fam_name_ieq (name, "size")
      || fam_name_ieq (name, "used") || fam_name_ieq (name, "num"))
    return 50;
  if (fam_name_ieq (name, "n") || fam_name_ieq (name, "cnt")) return 40;
  return 0;
}

static struct arr_type *type_arr_info (struct type *t) {
  if (t == NULL) return NULL;
  if (t->mode == TM_ARR && t->u.arr_type != NULL) return t->u.arr_type;
  if (t->mode == TM_PTR && t->arr_type != NULL && t->arr_type->mode == TM_ARR)
    return t->arr_type->u.arr_type;
  return NULL;
}

static int type_flex_arr_p (struct type *t) {
  struct arr_type *a = type_arr_info (t);
  return a != NULL && a->flex_p;
}

/* Last named data N_MEMBER of a struct/class (methods are N_FUNC_DEF). */
static node_t last_data_member (struct type *type) {
  node_t dl, last = NULL, m;

  if (type == NULL
      || (type->mode != TM_STRUCT && type->mode != TM_CLASS && type->mode != TM_UNION)
      || type->u.tag_type == NULL)
    return NULL;
  dl = TAG_MEMBER_LIST (type->u.tag_type);
  if (dl == NULL || dl->code != N_LIST) return NULL;
  for (m = NL_HEAD (dl->u.ops); m != NULL; m = NL_NEXT (m)) {
    decl_t d;
    if (m->code != N_MEMBER || m->attr == NULL) continue;
    d = (decl_t) m->attr;
    if (d->decl_spec.type == NULL) continue;
    if (d->decl_spec.type->mode == TM_FUNC) continue;
    if (d->decl_spec.type->func_type_before_adjustment_p) continue;
    last = m;
  }
  return last;
}

static void fam_collect_int_members (struct type *type, node_t skip, node_t *best,
                                     int *best_score, int *n_int) {
  node_t dl, m;

  if (type == NULL
      || (type->mode != TM_STRUCT && type->mode != TM_CLASS && type->mode != TM_UNION)
      || type->u.tag_type == NULL)
    return;
  dl = TAG_MEMBER_LIST (type->u.tag_type);
  if (dl == NULL || dl->code != N_LIST) return;
  for (m = NL_HEAD (dl->u.ops); m != NULL; m = NL_NEXT (m)) {
    decl_t d;
    struct type *mt;
    if (m->code != N_MEMBER || m->attr == NULL || m == skip) continue;
    d = (decl_t) m->attr;
    mt = d->decl_spec.type;
    if (mt == NULL) continue;
    if (mt->unnamed_anon_struct_union_member_type_p
        && (mt->mode == TM_STRUCT || mt->mode == TM_CLASS || mt->mode == TM_UNION)) {
      fam_collect_int_members (mt, skip, best, best_score, n_int);
      continue;
    }
    if (!integer_type_p (mt)) continue;
    if (d->width >= 0) continue; /* skip bitfields */
    (*n_int)++;
    {
      int sc = fam_bound_name_score (member_decl_name (m));
      if (sc > *best_score) {
        *best_score = sc;
        *best = m;
      }
    }
  }
}

static void attach_fam_bound_by_name (struct type *outer, struct arr_type *at) {
  node_t best = NULL;
  int best_score = 0, n_int = 0;

  if (at == NULL || at->flex_bound_member != NULL) return;
  fam_collect_int_members (outer, NULL, &best, &best_score, &n_int);
  if (best == NULL) return;
  if (best_score >= 80 || (best_score >= 40 && n_int == 1))
    at->flex_bound_member = best;
}

static void mark_trailing_flex_array_1 (struct type *outer, struct type *type) {
  node_t last;
  decl_t ld;
  struct type *ft;
  struct arr_type *at;
  node_t sz;
  int is_flex = 0;

  last = last_data_member (type);
  if (last == NULL) return;
  ld = (decl_t) last->attr;
  if (ld == NULL) return;
  ft = ld->decl_spec.type;
  if (ft != NULL && (ft->mode == TM_STRUCT || ft->mode == TM_CLASS)
      && ft->unnamed_anon_struct_union_member_type_p) {
    mark_trailing_flex_array_1 (outer, ft);
    return;
  }
  if (ft == NULL || ft->mode != TM_ARR || ft->u.arr_type == NULL) return;
  at = ft->u.arr_type;
  sz = at->size;
  if (sz == NULL || sz->code == N_IGNORE)
    is_flex = 1;
  else if (sz->attr != NULL) {
    struct expr *se = (struct expr *) sz->attr;
    if (se->const_p && (se->c.i_val == 0 || se->c.i_val == 1)) is_flex = 1;
  }
  if (!is_flex) return;
  at->flex_p = 1;
  attach_fam_bound_by_name (outer, at);
}

static void mark_trailing_flex_array (struct type *type) {
  if (type != NULL && (type->mode == TM_STRUCT || type->mode == TM_CLASS))
    mark_trailing_flex_array_1 (type, type);
}

/* Trailing FAM member of a struct/class, or NULL. */
static node_t type_trailing_flex_member (struct type *type) {
  node_t last;
  decl_t ld;
  struct type *ft;

  last = last_data_member (type);
  if (last == NULL) return NULL;
  ld = (decl_t) last->attr;
  if (ld == NULL) return NULL;
  ft = ld->decl_spec.type;
  if (ft != NULL && (ft->mode == TM_STRUCT || ft->mode == TM_CLASS)
      && ft->unnamed_anon_struct_union_member_type_p)
    return type_trailing_flex_member (ft);
  if (ft != NULL && type_flex_arr_p (ft)) return last;
  return NULL;
}

static void set_type_layout (c2m_ctx_t c2m_ctx, struct type *type) {
  mir_size_t overall_size = 0;

  if (type->raw_size != MIR_SIZE_MAX) return; /* defined */
  if (type->mode == TM_BASIC) {
    /* Guard: after a type error, u.basic_type may be uninitialized (garbage).
       Any out-of-range value is a sign we are in error-recovery mode. */
    if (n_errors > 0 && (unsigned) type->u.basic_type > (unsigned) TP_GENERIC) {
      overall_size = sizeof (int); /* safe fallback during error recovery */
    } else {
      overall_size = basic_type_size (type->u.basic_type);
    }
  } else if (type->mode == TM_PTR) {
    overall_size = sizeof (mir_size_t);
  } else if (type->mode == TM_ENUM) {
    overall_size = basic_type_size (get_enum_basic_type (type));
  } else if (type->mode == TM_FUNC) {
    overall_size = sizeof (mir_size_t);
  } else if (type->mode == TM_ARR) {
    struct arr_type *arr_type = type->u.arr_type;
    struct expr *cexpr = arr_type->size->attr;
    mir_size_t nel
      = (arr_type->size->code == N_IGNORE || cexpr == NULL || !cexpr->const_p ? 1 : cexpr->c.i_val);

    set_type_layout (c2m_ctx, arr_type->el_type);
    overall_size = type_size (c2m_ctx, arr_type->el_type) * nel;
  } else if (type->mode == TM_UNDEF) {
    overall_size = sizeof (int); /* error type */
  } else if (type->mode == TM_DICT || type->mode == TM_SLICE) {
    overall_size = sizeof (void*); /* DictValue* / slice header pointer */
  } else {
    int bf_p = FALSE, bits = -1, bound_bit = 0;
    mir_size_t offset = 0, prev_size = 0;

    assert (type->mode == TM_STRUCT || type->mode == TM_UNION || type->mode == TM_CLASS || type->mode == TM_DICT);
    if (incomplete_type_p (c2m_ctx, type)) {
      overall_size = MIR_SIZE_MAX;
      if (c2m_options->debug_p) printf("set_type_layout: using MIR_SIZE_MAX storage type!?!\n");
    } else {
      for (node_t el = NL_HEAD (TAG_MEMBER_LIST (type->u.tag_type)->u.ops); el != NULL;
           el = NL_NEXT (el))
        if (el->code == N_MEMBER) {
          decl_t decl = el->attr;
          int member_align;
          mir_size_t member_size;
          node_t width = MEMBER_WIDTH (el);
          struct expr *expr;
          int anon_process_p = (!type->unnamed_anon_struct_union_member_type_p
                                && decl->decl_spec.type->unnamed_anon_struct_union_member_type_p
                                && decl->decl_spec.type->raw_size == MIR_SIZE_MAX);

          if (anon_process_p) update_members_offset (decl->decl_spec.type, MIR_SIZE_MAX);
          set_type_layout (c2m_ctx, decl->decl_spec.type);
          if ((member_size = type_size (c2m_ctx, decl->decl_spec.type)) == 0) {
            /* MIR #451: GNU zero-length array member keeps current offset, not 0. */
            member_align = type_align (decl->decl_spec.type);
            if (decl->decl_spec.align > member_align) member_align = decl->decl_spec.align;
            decl->offset = type->mode == TM_UNION
                             ? 0
                             : (overall_size + member_align - 1) / member_align * member_align;
            decl->bit_offset = -1;
            decl->width = -1;
            continue;
          }
          member_align = type_align (decl->decl_spec.type);
          /* _Alignas(N) / aligned(N) on the member over-aligns it (1fdf44d8). */
          if (decl->decl_spec.align > member_align) member_align = decl->decl_spec.align;
          bits
            = width->code == N_IGNORE || !(expr = width->attr)->const_p ? -1 : (int) expr->c.u_val;
          update_field_layout (&bf_p, &overall_size, &offset, &bound_bit, prev_size, member_size,
                               member_align, bits);
          prev_size = member_size;
          decl->offset = offset;
          decl->bit_offset = bits < 0 ? -1 : bound_bit - bits;
          if (bits == 0) bf_p = FALSE;
          decl->width = bits;
          if (type->mode == TM_UNION) { // This will make raw_size the max size of all the members
            offset = prev_size = 0;
            bf_p = FALSE;
            bits = -1;
            bound_bit = 0;
          }
          if (anon_process_p) update_members_offset (decl->decl_spec.type, decl->offset);
        }
    }
  }
  /* we might need raw_size for alignment calculations */
  type->raw_size = overall_size;
  aux_set_type_align (c2m_ctx, type);
  if ((type->mode == TM_STRUCT || type->mode == TM_CLASS) && overall_size != MIR_SIZE_MAX)
    mark_trailing_flex_array (type);
  if (type->mode == TM_PTR) /* Visit the pointed but after setting size to avoid looping */
    set_type_layout (c2m_ctx, type->u.ptr_type);
  if (c2m_options->debug_p) {
    fprintf (stderr, "set_type_layout: ");
    print_type (c2m_ctx, stderr, type);
    fprintf (stderr, " raw_size=%zu, align=%d; type: \n", (size_t) type->raw_size, type->align);
  }
}

/* ===== Type Construction Helpers =====
 * Reduce boilerplate in check/gen when building common type patterns.
 */

/* Create a pointer type pointing to the given type, with layout computed. */
static struct type *create_ptr_type (c2m_ctx_t c2m_ctx, struct type *pointee) {
  struct type *t = create_type (c2m_ctx, NULL);
  t->mode = TM_PTR;
  t->u.ptr_type = pointee;
  set_type_layout (c2m_ctx, t);
  return t;
}

/* Create a basic type (no layout computation -- caller must do that if needed). */
static struct type *create_basic_type (c2m_ctx_t c2m_ctx, enum basic_type bt) {
  struct type *t = create_type (c2m_ctx, NULL);
  t->mode = TM_BASIC;
  t->u.basic_type = bt;
  return t;
}

/* Create a class type referencing the given class tag node. */
static struct type *create_class_type (c2m_ctx_t c2m_ctx, node_t class_node) {
  struct type *t = create_type (c2m_ctx, NULL);
  t->mode = TM_CLASS;
  t->u.tag_type = class_node;
  set_type_pos_node (t, class_node);
  return t;
}

/* Forward: defined with the other symbol helpers. */
static node_t find_def (c2m_ctx_t c2m_ctx, enum symbol_mode mode, node_t id, node_t scope,
                        node_t *aux_node);
static node_t find_class_dtor_def (c2m_ctx_t c2m_ctx, node_t class_def);
static int class_has_dtor_p (c2m_ctx_t c2m_ctx, struct type *type);

/* Zero-initialize a decl_spec struct with sensible defaults. */
static void init_decl_spec (struct decl_spec *ds) {
  ds->typedef_p = ds->extern_p = ds->static_p = FALSE;
  ds->auto_p = ds->register_p = ds->thread_local_p = FALSE;
  ds->inline_p = ds->no_return_p = FALSE;
  ds->align = -1;
  ds->align_node = NULL;
  ds->linkage = N_IGNORE;
  ds->type = NULL;
}

/* Iterate over the member list of a struct/union/class tag type.
   Returns the first member node, or NULL if the member list is N_IGNORE. */
static node_t tag_type_first_member (struct type *type) {
  node_t decl_list = TAG_MEMBER_LIST (type->u.tag_type);
  return (decl_list->code != N_IGNORE) ? NL_HEAD (decl_list->u.ops) : NULL;
}

static int int_bit_size (struct type *type) {
  assert (type->mode == TM_BASIC || type->mode == TM_ENUM);
  return (basic_type_size (type->mode == TM_ENUM ? get_enum_basic_type (type) : type->u.basic_type)
          * MIR_CHAR_BIT);
}

static int void_type_p (struct type *type) {
  return type->mode == TM_BASIC && type->u.basic_type == TP_VOID;
}

static int void_ptr_p (struct type *type) {
  return type->mode == TM_PTR && void_type_p (type->u.ptr_type);
}

static int incomplete_type_p (c2m_ctx_t c2m_ctx, struct type *type) {
  check_ctx_t check_ctx = c2m_ctx->check_ctx;

  if(!type) {
      printf("ERROR: incomplete type, type is NULL\n");
      return FALSE;
  }

  switch (type->mode) {
  case TM_BASIC: return type->u.basic_type == TP_VOID;
  case TM_ENUM:
  case TM_STRUCT:
  case TM_UNION:
  case TM_CLASS:{
    node_t scope, n = type->u.tag_type;

    if (NL_EL (n->u.ops, 1)->code == N_IGNORE) return TRUE;
    /* C++ rule: class C is complete in the bodies of its own methods.  Needed
       for by-value collection APIs (`List<T> Take(...) { auto r = List<T>();
       return move r; }`).  Free functions and foreign-class methods still see
       the usual incomplete-while-nested rule (field layout uses curr_func_def
       == NULL, so data members of type C remain rejected). */
    if (curr_func_def != NULL) {
      decl_t fd = curr_func_def->attr;
      if (fd != NULL && fd->decl_spec.type != NULL
          && fd->decl_spec.type->mode == TM_FUNC) {
        struct func_type *ft = fd->decl_spec.type->u.func_type;
        if (ft != NULL && ft->class_scope == n) return FALSE;
      }
    }
    for (scope = curr_scope; scope != NULL && scope != top_scope && scope != n;
         scope = ((struct node_scope *) scope->attr)->scope)
      ;
    return scope == n;
  }
  case TM_PTR: return FALSE;
  case TM_ARR: {
    struct arr_type *arr_type = type->u.arr_type;

    return (arr_type->size->code == N_IGNORE || incomplete_type_p (c2m_ctx, arr_type->el_type));
  }
  case TM_FUNC:
    return ((type = type->u.func_type->ret_type) == NULL
            || (!void_type_p (type) && incomplete_type_p (c2m_ctx, type)));
  default: return FALSE;
  }
}

static int null_const_p (struct expr *expr, struct type *type) {
  return ((integer_type_p (type) && expr->const_p && expr->c.u_val == 0)
          || (void_ptr_p (type) && expr->const_p && expr->c.u_val == 0
              && type_qual_eq_p (&type->type_qual, &zero_type_qual)));
}

static void cast_value (struct expr *to_e, struct expr *from_e, struct type *to) {
  assert (to_e->const_p && from_e->const_p);
  struct type *from = from_e->type;

#define CONV(TP, cast, mto, mfrom) \
  case TP: to_e->c.mto = (cast) from_e->c.mfrom; break;
#define BASIC_FROM_CONV(mfrom)                                                           \
  switch (to->u.basic_type) {                                                            \
    CONV (TP_BOOL, mir_bool, u_val, mfrom) CONV (TP_UCHAR, mir_uchar, u_val, mfrom);     \
    CONV (TP_USHORT, mir_ushort, u_val, mfrom) CONV (TP_UINT, mir_uint, u_val, mfrom);   \
    CONV (TP_ULONG, mir_ulong, u_val, mfrom) CONV (TP_ULLONG, mir_ullong, u_val, mfrom); \
    CONV (TP_SCHAR, mir_char, i_val, mfrom);                                             \
    CONV (TP_SHORT, mir_short, i_val, mfrom) CONV (TP_INT, mir_int, i_val, mfrom);       \
    CONV (TP_LONG, mir_long, i_val, mfrom) CONV (TP_LLONG, mir_llong, i_val, mfrom);     \
    CONV (TP_FLOAT, mir_float, d_val, mfrom) CONV (TP_DOUBLE, mir_double, d_val, mfrom); \
    CONV (TP_LDOUBLE, mir_ldouble, d_val, mfrom);                                        \
  case TP_CHAR:                                                                          \
    if (char_is_signed_p ())                                                             \
      to_e->c.i_val = (mir_char) from_e->c.mfrom;                                        \
    else                                                                                 \
      to_e->c.u_val = (mir_char) from_e->c.mfrom;                                        \
    break;                                                                               \
  default: assert (FALSE);                                                               \
  }

#define BASIC_TO_CONV(cast, mto)                                \
  switch (from->u.basic_type) {                                 \
  case TP_BOOL:                                                 \
  case TP_UCHAR:                                                \
  case TP_USHORT:                                               \
  case TP_UINT:                                                 \
  case TP_ULONG:                                                \
  case TP_ULLONG: to_e->c.mto = (cast) from_e->c.u_val; break;  \
  case TP_CHAR:                                                 \
    if (!char_is_signed_p ()) {                                 \
      to_e->c.mto = (cast) from_e->c.u_val;                     \
      break;                                                    \
    }                                                           \
    /* falls through */                                         \
  case TP_SCHAR:                                                \
  case TP_SHORT:                                                \
  case TP_INT:                                                  \
  case TP_LONG:                                                 \
  case TP_LLONG: to_e->c.mto = (cast) from_e->c.i_val; break;   \
  case TP_FLOAT:                                                \
  case TP_DOUBLE:                                               \
  case TP_LDOUBLE: to_e->c.mto = (cast) from_e->c.d_val; break; \
  default: assert (FALSE);                                      \
  }

  struct type temp, temp2;
  if (to->mode == TM_ENUM) {
    temp.mode = TM_BASIC;
    temp.u.basic_type = get_enum_basic_type (to);
    to = &temp;
  } else if (to->mode == TM_BASIC && to->u.basic_type == TP_STRING) {
    /* `String` is a TM_BASIC tag but a pointer-width handle at runtime.
       Fold constant casts to it like a pointer so `(String)0` (a null
       String) and friends don't fall through to the assert below. */
    temp.mode = TM_PTR;
    to = &temp;
  }
  if (from->mode == TM_ENUM) {
    temp2.mode = TM_BASIC;
    temp2.u.basic_type = get_enum_basic_type (from);
    from = &temp2;
  } else if (from->mode == TM_BASIC && from->u.basic_type == TP_STRING) {
    /* Symmetric: a constant `String` source folds like a pointer source. */
    temp2.mode = TM_PTR;
    from = &temp2;
  }
  if (to->mode == from->mode && (from->mode == TM_PTR || from->mode == TM_ENUM)) {
    to_e->c = from_e->c;
  } else if (from->mode == TM_PTR) {
    BASIC_FROM_CONV (u_val);
  } else if (to->mode == TM_PTR) {
    BASIC_TO_CONV (mir_size_t, u_val);
  } else {
    switch (from->u.basic_type) {
    case TP_BOOL:
    case TP_UCHAR:
    case TP_USHORT:
    case TP_UINT:
    case TP_ULONG:
    case TP_ULLONG: BASIC_FROM_CONV (u_val); break;
    case TP_CHAR:
      if (!char_is_signed_p ()) {
        BASIC_FROM_CONV (u_val);
        break;
      }
      /* falls through */
    case TP_SCHAR:
    case TP_SHORT:
    case TP_INT:
    case TP_LONG:
    case TP_LLONG: BASIC_FROM_CONV (i_val); break;
    case TP_FLOAT:
    case TP_DOUBLE:
    case TP_LDOUBLE: BASIC_FROM_CONV (d_val); break;
    default: assert (FALSE);
    }
  }
#undef CONV
#undef BASIC_FROM_CONV
#undef BASIC_TO_CONV
}

static void convert_value (struct expr *e, struct type *to) { cast_value (e, e, to); }

static int non_reg_decl_spec_p (struct decl_spec *ds) {
  return (ds->typedef_p || ds->extern_p || ds->static_p || ds->auto_p || ds->thread_local_p
          || ds->inline_p || ds->no_return_p || ds->align_node);
}

static void create_node_scope (c2m_ctx_t c2m_ctx, node_t node) {
  check_ctx_t check_ctx = c2m_ctx->check_ctx;
  struct node_scope *ns = reg_malloc (c2m_ctx, sizeof (struct node_scope));

  assert (node != curr_scope);
  ns->func_scope_num = curr_func_scope_num++;
  ns->stack_var_p = FALSE;
  ns->offset = ns->size = ns->call_arg_area_size = 0;
  node->attr = ns;
  ns->scope = curr_scope;
  curr_scope = node;
}

static void finish_scope (c2m_ctx_t c2m_ctx) {
  check_ctx_t check_ctx = c2m_ctx->check_ctx;

  curr_scope = ((struct node_scope *) curr_scope->attr)->scope;
}

static void set_type_qual (c2m_ctx_t c2m_ctx, node_t r, struct type_qual *tq,
                           enum type_mode tmode) {
  for (node_t n = NL_HEAD (r->u.ops); n != NULL; n = NL_NEXT (n)) switch (n->code) {
      /* Type qualifiers: */
    case N_CONST: tq->const_p = TRUE; break;
    case N_RESTRICT:
      tq->restrict_p = TRUE;
      if (tmode != TM_PTR && tmode != TM_UNDEF)
        error (c2m_ctx, POS (n), "restrict requires a pointer");
      break;
    case N_VOLATILE: tq->volatile_p = TRUE; break;
    case N_ATOMIC:
      tq->atomic_p = TRUE;
      if (tmode == TM_ARR)
        error (c2m_ctx, POS (n), "_Atomic qualifying array");
      else if (tmode == TM_FUNC)
        error (c2m_ctx, POS (n), "_Atomic qualifying function");
      break;
    default: break; /* Ignore */
    }
}

static void check_type_duplication (c2m_ctx_t c2m_ctx, struct type *type, node_t n,
                                    const char *name, int size, int sign) {
  if (type->mode != TM_BASIC || type->u.basic_type != TP_UNDEF)
    error (c2m_ctx, POS (n), "%s with another type", name);
  else if (type->mode != TM_BASIC && size != 0)
    error (c2m_ctx, POS (n), "size with non-numeric type");
  else if (type->mode != TM_BASIC && sign != 0)
    error (c2m_ctx, POS (n), "sign attribute with non-integer type");
}

static node_t find_def (c2m_ctx_t c2m_ctx, enum symbol_mode mode, node_t id, node_t scope,
                        node_t *aux_node) {
  symbol_t sym;

  for (;;) {
    if (!symbol_find (c2m_ctx, mode, id, scope, &sym)) {
      if (scope == NULL)
          return NULL;
      if(scope->attr) {
          scope = ((struct node_scope *) scope->attr)->scope;
      } else {
          if (c2m_options->debug_p) printf("COMPILER: find_def: Scope has NULL attr\n");
          return NULL;
      }

    } else {
      if (aux_node) *aux_node = sym.aux_node;
      return sym.def_node;
    }
  }
}

/* Look up the user destructor `__dtor_<ClassName>` for a class tag, or NULL.
   Safe in both check and gen: prefers top_scope (where ctors/dtors are
   mirrored), then curr_scope when a check context is live. */
static node_t find_class_dtor_def (c2m_ctx_t c2m_ctx, node_t class_def) {
  check_ctx_t check_ctx = c2m_ctx->check_ctx;
  node_t cid;
  char dtor_name[320];
  node_t dtor_id, def;

  if (class_def == NULL || class_def->code != N_CLASS) return NULL;
  cid = TAG_ID (class_def);
  if (cid == NULL || cid->code != N_ID || cid->u.s.s == NULL) return NULL;
  snprintf (dtor_name, sizeof (dtor_name), "__dtor_%s", cid->u.s.s);
  dtor_id = build_id (c2m_ctx, dtor_name, POS (class_def));
  if (check_ctx != NULL && top_scope != NULL) {
    def = find_def (c2m_ctx, S_REGULARS, dtor_id, top_scope, NULL);
    if (def != NULL && def->code == N_FUNC_DEF) return def;
  }
  if (check_ctx != NULL && curr_scope != NULL) {
    def = find_def (c2m_ctx, S_REGULARS, dtor_id, curr_scope, NULL);
    if (def != NULL && def->code == N_FUNC_DEF) return def;
  }
  return NULL;
}

/* TRUE iff TYPE is a by-value class that owns resources via a user destructor. */
static int class_has_dtor_p (c2m_ctx_t c2m_ctx, struct type *type) {
  if (type == NULL || type->mode != TM_CLASS || type->u.tag_type == NULL) return FALSE;
  return find_class_dtor_def (c2m_ctx, type->u.tag_type) != NULL;
}

/* TRUE for monomorphized List/Map/Set specializations (`__generic_List_int`,
   `__generic_Map_String_int`, …).  These own a heap buffer; shallow assign/
   copy-init aliases the buffer and double-frees.  Ordinary by-value classes
   with dtors (element types like LapSample) are NOT covered — List internals
   freely assign those. */
static int class_is_move_only_collection_p (c2m_ctx_t c2m_ctx, struct type *type) {
  node_t cid;
  const char *nm;
  if (!class_has_dtor_p (c2m_ctx, type)) return FALSE;
  cid = TAG_ID (type->u.tag_type);
  if (cid == NULL || cid->code != N_ID || cid->u.s.s == NULL) return FALSE;
  nm = cid->u.s.s;
  return (strncmp (nm, "__generic_List_", 15) == 0
          || strncmp (nm, "__generic_Map_", 14) == 0
          || strncmp (nm, "__generic_Set_", 14) == 0);
}

/* ── type_kind (T) — check-time API for semantic analysis & ownership ────
 *
 * Prefer the parse-time class_type_meta cache; refine MOVE_ONLY via the
 * monomorphized-name check.  Scalars/String/pointers do not need a class meta.
 *
 * Consumers:
 *   - specialization gate (parse-time kind on class metas)
 *   - ownership.c: skip QUIET/POD by-value; track UNIQUE/POINTER
 *   - midopt/gen (future): memcpy vs deep paths, destroy elision
 *   - list.h/map.h (future): is_byvalue_element_ok<T>() style intrinsics
 */
static type_kind_t type_kind (c2m_ctx_t c2m_ctx, struct type *type) {
  class_type_meta_t *meta;
  node_t tag;
  type_kind_t k;

  if (type == NULL) return TK_OPAQUE;
  switch (type->mode) {
  case TM_BASIC:
    if (type->u.basic_type == TP_STRING) return TK_ARENA_VALUE;
    if (type->u.basic_type == TP_VOID) return TK_OPAQUE;
    return TK_POD;
  case TM_ENUM:
    return TK_POD;
  case TM_PTR:
    return TK_POINTER;
  case TM_ARR:
    if (type->u.arr_type != NULL && type->u.arr_type->el_type != NULL)
      return type_kind (c2m_ctx, type->u.arr_type->el_type);
    return TK_OPAQUE;
  case TM_STRUCT:
  case TM_UNION:
    return TK_POD;
  case TM_CLASS:
    tag = type->u.tag_type;
    if (tag == NULL) return TK_OPAQUE;
    if (class_is_move_only_collection_p (c2m_ctx, type)) return TK_MOVE_ONLY;
    meta = class_type_meta_find_ctx (c2m_ctx, tag);
    if (meta == NULL && TAG_ID (tag) != NULL && TAG_ID (tag)->code == N_ID)
      meta = class_type_meta_find_by_name (c2m_ctx, TAG_ID (tag)->u.s.s);
    if (meta != NULL && meta->kind_valid_p) {
      k = meta->kind;
      if (k != TK_MOVE_ONLY && class_is_move_only_collection_p (c2m_ctx, type))
        return TK_MOVE_ONLY;
      return k;
    }
    if (tag->code == N_CLASS) {
      class_type_meta_register (c2m_ctx, tag, 0);
      meta = class_type_meta_find_ctx (c2m_ctx, tag);
      if (meta != NULL && meta->kind_valid_p) return meta->kind;
    }
    return class_has_dtor_p (c2m_ctx, type) ? TK_OPAQUE : TK_POD;
  case TM_DICT:
    return TK_UNIQUE_RESOURCE;
  case TM_FUNC:
  case TM_SLICE:
  case TM_UNDEF:
  default:
    return TK_OPAQUE;
  }
}

static int element_ok_byvalue_p (c2m_ctx_t c2m_ctx, struct type *type) {
  return type_kind_ok_byvalue_element_p (type_kind (c2m_ctx, type));
}

static int type_is_unique_resource_p (c2m_ctx_t c2m_ctx, struct type *type) {
  return type_kind (c2m_ctx, type) == TK_UNIQUE_RESOURCE;
}

static int type_is_quiet_or_pod_p (c2m_ctx_t c2m_ctx, struct type *type) {
  type_kind_t k = type_kind (c2m_ctx, type);
  return k == TK_POD || k == TK_QUIET_VALUE || k == TK_ARENA_VALUE;
}

static node_t process_tag (c2m_ctx_t c2m_ctx, node_t r, node_t id, node_t decl_list) {
  check_ctx_t check_ctx = c2m_ctx->check_ctx;
  symbol_t sym;
  int found_p;
  node_t scope, tab_decl_list;

  if (id->code != N_ID) return r;
  scope = curr_scope;
  while (scope != top_scope && (scope->code == N_STRUCT || scope->code == N_UNION || scope->code == N_CLASS))
    scope = ((struct node_scope *) scope->attr)->scope;
  sym.def_node = NULL; /* to remove uninitialized warning */
  if (decl_list->code != N_IGNORE) {
    found_p = symbol_find (c2m_ctx, S_TAG, id, scope, &sym);
  } else {
    sym.def_node = find_def (c2m_ctx, S_TAG, id, scope, NULL);
    found_p = sym.def_node != NULL;
  }
  if (!found_p) {
    // Classes need both, since they implement their own definition(tag)
    //if (r->code != N_CLASS)
    //    symbol_insert (c2m_ctx, S_REGULARS, id, scope, r, NULL);
    //else
        symbol_insert (c2m_ctx, S_TAG, id, scope, r, NULL);
  } else if (sym.def_node == r) {
    /* Same AST node already pre-registered at parse time (classes do this so
       members can refer to their own type while the body is still being
       parsed).  Not a redeclaration -- common for block-scoped/nested classes
       where parse-time curr_scope is a real block (unlike file-scope parse
       where curr_scope may still be NULL and the insert is effectively a
       no-op). */
  } else if (sym.def_node->code != r->code) {
    error (c2m_ctx, POS (id), "kind of tag %s is unmatched with previous declaration", id->u.s.s);
  } else if ((tab_decl_list = NL_EL (sym.def_node->u.ops, 1))->code != N_IGNORE
             && decl_list->code != N_IGNORE) {
    error (c2m_ctx, POS (id), "tag %s redeclaration", id->u.s.s);
  } else {
    if (decl_list->code != N_IGNORE) { /* swap decl lists */
      DLIST (node_t) temp;
      SWAP (r->u.ops, sym.def_node->u.ops, temp);
    }
    r = sym.def_node;
  }
  return r;
}

static void def_symbol (c2m_ctx_t c2m_ctx, enum symbol_mode mode, node_t id, node_t scope,
                        node_t def_node, node_code_t linkage) {
  check_ctx_t check_ctx = c2m_ctx->check_ctx;
  symbol_t sym;
  struct decl_spec tab_decl_spec, decl_spec;

  if (id->code == N_IGNORE) return;
  assert (id->code == N_ID && scope != NULL);
  assert (scope->code == N_MODULE || scope->code == N_BLOCK || scope->code == N_STRUCT
          || scope->code == N_UNION || scope->code == N_FUNC || scope->code == N_FOR
          || scope->code == N_FORIN || scope->code == N_CLASS);
  decl_spec = ((decl_t) def_node->attr)->decl_spec;
  if (decl_spec.thread_local_p && !decl_spec.static_p && !decl_spec.extern_p)
    error (c2m_ctx, POS (id), "auto %s is declared as thread local", id->u.s.s);
  if (!symbol_find (c2m_ctx, mode, id, scope, &sym)) {
    symbol_insert (c2m_ctx, mode, id, scope, def_node, NULL);
    return;
  }
  /* Class method overloading: several methods of one class may share a name
     and be distinguished by their parameter types.  Methods are registered by
     plain name (in the enclosing scope), so `curr_class` is what tells us this
     is a method definition.  Record the additional overload (resolved at the
     call site) instead of rejecting it as an incompatible redeclaration. */
  if (curr_class != NULL && def_node->code == N_FUNC_DEF
      && sym.def_node != NULL && sym.def_node->code == N_FUNC_DEF) {
    VARR_PUSH (node_t, sym.defs, def_node);
    return;
  }
  /* Prior entry without a declaration attr (error-recovery node, tag-only
     registration, garbled input, …).  Do not crash; replace with the new
     definition and report a repeated declaration. */
  if (sym.def_node == NULL || sym.def_node->attr == NULL
      || (sym.def_node->code != N_SPEC_DECL && sym.def_node->code != N_FUNC_DEF
          && sym.def_node->code != N_MEMBER && sym.def_node->code != N_ENUM_CONST
          && sym.def_node->code != N_CLASS)) {
    error (c2m_ctx, POS (id), "repeated declaration %s", id->u.s.s);
    symbol_def_replace (c2m_ctx, sym, def_node);
    return;
  }
  tab_decl_spec = ((decl_t) sym.def_node->attr)->decl_spec;
  if ((def_node->code == N_ENUM_CONST || sym.def_node->code == N_ENUM_CONST)
      && def_node->code != sym.def_node->code) {
    error (c2m_ctx, POS (id), "%s redeclared as a different kind of symbol", id->u.s.s);
    return;
  } else if (linkage == N_IGNORE) {
    if (!decl_spec.typedef_p || !tab_decl_spec.typedef_p
        || !type_eq_p (decl_spec.type, tab_decl_spec.type))
#if defined(__APPLE__)
      /* a hack to use our definition instead of macosx for non-GNU compiler */
      if (strcmp (id->u.s.s, "__darwin_va_list") != 0)
#endif
        error (c2m_ctx, POS (id), "repeated declaration %s", id->u.s.s);
  } else if (!compatible_types_p (decl_spec.type, tab_decl_spec.type, FALSE)) {
    error (c2m_ctx, POS (id), "incompatible types of %s declarations", id->u.s.s);
  }
  if (tab_decl_spec.thread_local_p != decl_spec.thread_local_p) {
    error (c2m_ctx, POS (id), "thread local and non-thread local declarations of %s", id->u.s.s);
  }
  if ((decl_spec.linkage == N_EXTERN && linkage == N_STATIC)
      || (decl_spec.linkage == N_STATIC && linkage == N_EXTERN))
    warning (c2m_ctx, POS (id), "%s defined with external and internal linkage", id->u.s.s);
  VARR_PUSH (node_t, sym.defs, def_node);
  if (incomplete_type_p (c2m_ctx, tab_decl_spec.type)) symbol_def_replace (c2m_ctx, sym, def_node);
}

static void make_type_complete (c2m_ctx_t c2m_ctx, struct type *type) {
  if (incomplete_type_p (c2m_ctx, type)) return;
  /* The type may become complete: recalculate size: */
  type->raw_size = MIR_SIZE_MAX;
  set_type_layout (c2m_ctx, type);
}

static node_t skip_struct_scopes (node_t scope) {
  for (; scope != NULL && (scope->code == N_STRUCT || scope->code == N_UNION || scope->code == N_CLASS);
       scope = ((struct node_scope *) scope->attr)->scope)
    ;
  return scope;
}

static void check (c2m_ctx_t c2m_ctx, node_t node, node_t context);

/* ── nameof / typeof reflection (check-time) ───────────────────────────── */

static const char *basic_type_reflection_name (enum basic_type bt) {
  switch (bt) {
  case TP_VOID: return "void";
  case TP_BOOL: return "bool";
  case TP_CHAR:
  case TP_SCHAR:
  case TP_UCHAR: return "char";
  case TP_SHORT:
  case TP_USHORT: return "short";
  case TP_INT:
  case TP_UINT: return "int";
  case TP_LONG:
  case TP_ULONG: return "long";
  case TP_LLONG:
  case TP_ULLONG: return "long";
  case TP_FLOAT: return "float";
  case TP_DOUBLE: return "double";
  case TP_LDOUBLE: return "double";
  case TP_STRING: return "String";
  default: return "?";
  }
}

static const char *type_reflection_name (c2m_ctx_t c2m_ctx, const struct type *t,
                                         int keep_ptr_p) {
  int ptr_depth = 0;
  const char *base = "?";
  char buf[256];

  if (t == NULL) return "?";
  while (t != NULL && t->mode == TM_PTR) {
    ptr_depth++;
    t = t->u.ptr_type;
  }
  if (t == NULL) return "?";
  switch (t->mode) {
  case TM_BASIC: base = basic_type_reflection_name (t->u.basic_type); break;
  case TM_ENUM: {
    node_t id = (t->u.tag_type != NULL) ? TAG_ID (t->u.tag_type) : NULL;
    base = (id != NULL && id->code == N_ID) ? id->u.s.s : "enum";
    break;
  }
  case TM_CLASS: {
    node_t id = (t->u.tag_type != NULL) ? TAG_ID (t->u.tag_type) : NULL;
    base = (id != NULL && id->code == N_ID)
             ? pretty_generic_type_name (c2m_ctx, id->u.s.s) : "class";
    break;
  }
  case TM_STRUCT: {
    node_t id = (t->u.tag_type != NULL) ? TAG_ID (t->u.tag_type) : NULL;
    base = (id != NULL && id->code == N_ID) ? id->u.s.s : "struct";
    break;
  }
  case TM_UNION: {
    node_t id = (t->u.tag_type != NULL) ? TAG_ID (t->u.tag_type) : NULL;
    base = (id != NULL && id->code == N_ID) ? id->u.s.s : "union";
    break;
  }
  case TM_DICT: base = "dict"; break;
  case TM_ARR: base = "array"; break;
  case TM_FUNC: base = "func"; break;
  case TM_SLICE: base = "slice"; break;
  default: base = "?"; break;
  }
  if (!keep_ptr_p || ptr_depth == 0) return base;
  {
    size_t nlen = strlen (base);
    if (nlen + (size_t) ptr_depth + 1 >= sizeof (buf)) return base;
    memcpy (buf, base, nlen);
    for (int i = 0; i < ptr_depth; i++) buf[nlen + (size_t) i] = '*';
    buf[nlen + (size_t) ptr_depth] = '\0';
    return uniq_cstr (c2m_ctx, buf).s;
  }
}

static const char *enum_const_name_for_value (node_t enum_node, mir_llong val) {
  node_t elist;
  if (enum_node == NULL || enum_node->code != N_ENUM) return NULL;
  elist = TAG_MEMBER_LIST (enum_node);
  if (elist == NULL || elist->code != N_LIST) return NULL;
  for (node_t en = NL_HEAD (elist->u.ops); en != NULL; en = NL_NEXT (en)) {
    struct enum_value *ev;
    node_t id;
    if (en->code != N_ENUM_CONST || en->attr == NULL) continue;
    ev = (struct enum_value *) en->attr;
    if (ev->u.i_val != val) continue;
    id = NL_HEAD (en->u.ops);
    if (id != NULL && id->code == N_ID) return id->u.s.s;
  }
  return NULL;
}

static node_t build_enum_nameof_expr (c2m_ctx_t c2m_ctx, node_t val_expr,
                                      node_t enum_node, pos_t pos) {
  node_t elist, result, en;
  node_t names[64];
  mir_llong vals[64];
  int n = 0, i;

  result = new_str_node (c2m_ctx, N_STR, uniq_cstr (c2m_ctx, "?"), pos);
  if (enum_node == NULL || enum_node->code != N_ENUM) return result;
  elist = TAG_MEMBER_LIST (enum_node);
  if (elist == NULL || elist->code != N_LIST) return result;
  for (en = NL_HEAD (elist->u.ops); en != NULL && n < 64; en = NL_NEXT (en)) {
    struct enum_value *ev;
    node_t id;
    if (en->code != N_ENUM_CONST || en->attr == NULL) continue;
    ev = (struct enum_value *) en->attr;
    id = NL_HEAD (en->u.ops);
    if (id == NULL || id->code != N_ID) continue;
    names[n] = id;
    vals[n] = ev->u.i_val;
    n++;
  }
  for (i = n - 1; i >= 0; i--) {
    node_t val_copy = copy_node (c2m_ctx, val_expr);
    node_t lit = new_i_node (c2m_ctx, (long) vals[i], pos);
    node_t eq = new_pos_node2 (c2m_ctx, N_EQ, pos, val_copy, lit);
    node_t then_s = new_str_node (c2m_ctx, N_STR,
                                  uniq_cstr (c2m_ctx, names[i]->u.s.s), pos);
    result = new_pos_node3 (c2m_ctx, N_COND, pos, eq, then_s, result);
  }
  return result;
}

static void rewrite_node_to_str (c2m_ctx_t c2m_ctx, node_t r, const char *nm) {
  node_t str_node;
  DLIST_LINK (node_t) saved_link;
  if (nm == NULL) nm = "?";
  str_node = new_str_node (c2m_ctx, N_STR, uniq_cstr (c2m_ctx, nm), POS (r));
  check (c2m_ctx, str_node, NULL);
  saved_link = r->op_link;
  *r = *str_node;
  r->op_link = saved_link;
}

static void set_class_layout (c2m_ctx_t c2m_ctx, node_t decl_node, struct type *type) {
    if (c2m_options->debug_p) printf("set_class_layout decl_node=%s\n", decl_node->u.s.s );
    if (type->u.tag_type == NULL) {
        fprintf(stderr, "Error: tag_type is NULL in set_class_layout\n");
        type->raw_size = 0;
        type->align = 1;
        return;
    }
    mir_size_t size = 0;
    mir_size_t align = 8;
    node_t decl_list = TAG_MEMBER_LIST (type->u.tag_type);
    if (decl_list->code != N_IGNORE) {
        for (node_t member = NL_HEAD (decl_list->u.ops); member != NULL; member = NL_NEXT (member)) {
            if(member->code == N_FUNC_DEF) continue; // Skip methods
            if(member->attr) {
                decl_t member_decl = (decl_t) member->attr;
                struct type *member_type = member_decl->decl_spec.type;
                if(member_type != NULL) {
                    mir_size_t member_size;
                    mir_size_t member_align;

                    /* Make sure the member's own layout is computed first so
                       raw_size/align are valid (e.g. nested class members). */
                    set_type_layout (c2m_ctx, member_type);
                    member_size = member_type->raw_size;
                    member_align = member_type->align;
                    if (member_align < 1) member_align = 1;

                    /* Align the running size up to the member's alignment and
                       record the member's offset.  Failing to set decl->offset
                       leaves member accesses pointing at the wrong location. */
                    size = (size + member_align - 1) / member_align * member_align;
                    member_decl->offset = size;
                    size += member_size;
                    if (align < member_align) align = member_align;
                } else {
                    printf("set_class_layout: member type is null\n");
                }
            }
        }
    }
    type->raw_size = size;
    type->align = align;
}

static void init_decl (c2m_ctx_t c2m_ctx, decl_t decl);

/* Phase 1: verify a class's optional `impl` clauses (defined after the protocol
   helpers below).  Forward-declared so the N_CLASS finalization paths can call it. */
static void verify_class_impls (c2m_ctx_t c2m_ctx, node_t class_node, node_t class_tag);

static struct decl_spec check_decl_spec (c2m_ctx_t c2m_ctx, node_t r, node_t decl_node) {
  check_ctx_t check_ctx = c2m_ctx->check_ctx;
  int n_sc = 0, sign = 0, size = 0, func_p = FALSE;
  struct decl_spec *res;
  struct type *type;

  if (r->attr != NULL) return *(struct decl_spec *) r->attr;
  if (decl_node->code == N_FUNC_DEF) {
    func_p = TRUE;
  } else if (decl_node->code == N_SPEC_DECL) {
    node_t declarator = NL_EL (decl_node->u.ops, 1);
    node_t list = NL_EL (declarator->u.ops, 1);

    func_p = list != NULL && NL_HEAD (list->u.ops) != NULL && NL_HEAD (list->u.ops)->code == N_FUNC;
  }
  r->attr = res = reg_malloc (c2m_ctx, sizeof (struct decl_spec));
  res->typedef_p = res->extern_p = res->static_p = FALSE;
  res->auto_p = res->register_p = res->thread_local_p = FALSE;
  res->inline_p = res->no_return_p = FALSE;
  res->align = -1;
  res->align_node = NULL;
  res->linkage = N_IGNORE;
  res->type = type = create_type (c2m_ctx, NULL);
  type->pos_node = r;
  type->mode = TM_BASIC;
  type->u.basic_type = TP_UNDEF;
  for (node_t n = NL_HEAD (r->u.ops); n != NULL; n = NL_NEXT (n))
    if (n->code == N_SIGNED || n->code == N_UNSIGNED) {
      if (sign != 0)
        error (c2m_ctx, POS (n), "more than one sign qualifier");
      else
        sign = n->code == N_SIGNED ? 1 : -1;
    } else if (n->code == N_SHORT) {
      if (size != 0)
        error (c2m_ctx, POS (n), "more than one type");
      else
        size = 1;
    } else if (n->code == N_LONG) {
      if (size == 2)
        size = 3;
      else if (size == 3)
        error (c2m_ctx, POS (n), "more than two long");
      else if (size == 1)
        error (c2m_ctx, POS (n), "short with long");
      else
        size = 2;
    }
  for (node_t n = NL_HEAD (r->u.ops); n != NULL; n = NL_NEXT (n)) switch (n->code) {
      /* Type qualifiers are already processed. */
    case N_CONST:
    case N_RESTRICT:
    case N_VOLATILE:
    case N_ATOMIC:
      break;
      /* Func specifiers: */
    case N_INLINE:
      if (!func_p)
        error (c2m_ctx, POS (n), "non-function declaration with inline");
      else
        res->inline_p = TRUE;
      break;
    case N_NO_RETURN:
      if (!func_p)
        error (c2m_ctx, POS (n), "non-function declaration with _Noreturn");
      else
        res->no_return_p = TRUE;
      break;
      /* Storage specifiers: */
    case N_TYPEDEF:
    case N_AUTO:
    case N_REGISTER:
      /* Allow 'static auto' as a classyc extension for lambda return-type inference */
      if (n_sc != 0 && !(n->code == N_AUTO && n_sc == 1 && res->static_p))
        error (c2m_ctx, POS (n), "more than one storage specifier");
      else if (n->code == N_TYPEDEF)
        res->typedef_p = TRUE;
      else if (n->code == N_AUTO)
        res->auto_p = TRUE;
      else
        res->register_p = TRUE;
      n_sc++;
      break;
    case N_EXTERN:
    case N_STATIC:
      if (n_sc != 0 && (n_sc != 1 || !res->thread_local_p))
        error (c2m_ctx, POS (n), "more than one storage specifier");
      else if (n->code == N_EXTERN)
        res->extern_p = TRUE;
      else
        res->static_p = TRUE;
      n_sc++;
      break;
    case N_THREAD_LOCAL:
      if (n_sc != 0 && (n_sc != 1 || (!res->extern_p && !res->static_p)))
        error (c2m_ctx, POS (n), "more than one storage specifier");
      else
        res->thread_local_p = TRUE;
      n_sc++;
      break;
    case N_VOID:
      set_type_pos_node (type, n);
      if (type->mode != TM_BASIC || type->u.basic_type != TP_UNDEF)
        error (c2m_ctx, POS (n), "void with another type");
      else if (sign != 0)
        error (c2m_ctx, POS (n), "void with sign qualifier");
      else if (size != 0)
        error (c2m_ctx, POS (n), "void with short or long");
      else
        type->u.basic_type = TP_VOID;
      break;
    case N_UNSIGNED:
    case N_SIGNED:
    case N_SHORT:
    case N_LONG: set_type_pos_node (type, n); break;
      // FIXME: Just set it to char ???
      printf("THIS MIGht bReAK\n");
      set_type_pos_node (type, n);
      type->u.basic_type = TP_CHAR;
      type->type_qual.const_p = 1; // Set constant
      type->raw_size = 8; // ptr size
      type->align = 8; // 64b alignment
      break;
    case N_STRING:
      // FIXME: Just set it to char ???
      set_type_pos_node (type, n);
      type->u.basic_type = TP_STRING;
      type->type_qual.const_p = 1; // Set constant
      type->raw_size = 8; // ptr size
      type->align = 8; // 64b alignment
      break;
    case N_CHAR:
    case N_INT:
      set_type_pos_node (type, n);
      if (type->mode != TM_BASIC || type->u.basic_type != TP_UNDEF) {
        error (c2m_ctx, POS (n), "char or int with another type");
      } else if (n->code == N_CHAR) {
        if (size != 0)
          error (c2m_ctx, POS (n), "char with short or long");
        else
          type->u.basic_type = sign == 0 ? TP_CHAR : sign < 0 ? TP_UCHAR : TP_SCHAR;
      } else if (size == 0)
        type->u.basic_type = sign >= 0 ? TP_INT : TP_UINT;
      else if (size == 1)
        type->u.basic_type = sign >= 0 ? TP_SHORT : TP_USHORT;
      else if (size == 2)
        type->u.basic_type = sign >= 0 ? TP_LONG : TP_ULONG;
      else
        type->u.basic_type = sign >= 0 ? TP_LLONG : TP_ULLONG;
      break;
    case N_BOOL:
      set_type_pos_node (type, n);
      if (type->mode != TM_BASIC || type->u.basic_type != TP_UNDEF)
        error (c2m_ctx, POS (n), "_Bool with another type");
      else if (sign != 0)
        error (c2m_ctx, POS (n), "_Bool with sign qualifier");
      else if (size != 0)
        error (c2m_ctx, POS (n), "_Bool with short or long");
      type->u.basic_type = TP_BOOL;
      break;
    case N_FLOAT:
      set_type_pos_node (type, n);
      if (type->mode != TM_BASIC || type->u.basic_type != TP_UNDEF)
        error (c2m_ctx, POS (n), "float with another type");
      else if (sign != 0)
        error (c2m_ctx, POS (n), "float with sign qualifier");
      else if (size != 0)
        error (c2m_ctx, POS (n), "float with short or long");
      else
        type->u.basic_type = TP_FLOAT;
      break;
    case N_DOUBLE:
      set_type_pos_node (type, n);
      if (type->mode != TM_BASIC || type->u.basic_type != TP_UNDEF)
        error (c2m_ctx, POS (n), "double with another type");
      else if (sign != 0)
        error (c2m_ctx, POS (n), "double with sign qualifier");
      else if (size == 0)
        type->u.basic_type = TP_DOUBLE;
      else if (size == 2)
        type->u.basic_type = TP_LDOUBLE;
      else
        error (c2m_ctx, POS (n), "double with short");
      break;
    case N_ID: {
      node_t def = find_def (c2m_ctx, S_REGULARS, n, skip_struct_scopes (curr_scope), NULL);
      decl_t decl;

      set_type_pos_node (type, n);
      if (def == NULL) {
        /* Named enums also live under S_TAG (like struct tags).  After bare-name
           registration, prefer S_REGULAR; fall back to the tag table so older
           units and forward uses still resolve. */
        def = find_def (c2m_ctx, S_TAG, n, skip_struct_scopes (curr_scope), NULL);
      }
      if (def == NULL) {
        error (c2m_ctx, POS (n), "unknown type %s", n->u.s.s);
        init_type (type);
        type->mode = TM_BASIC;
        type->u.basic_type = TP_INT;
      } else if (def->code == N_CLASS) {
        // Class used as a type name: build TM_CLASS type directly
        type->mode = TM_CLASS;
        type->u.tag_type = def;
        set_type_pos_node(type, def);
        set_class_layout(c2m_ctx, def, type);
      } else if (def->code == N_ENUM) {
        /* Named enum used as a type name: `Faction x` / `(Faction)v`. */
        type->mode = TM_ENUM;
        type->u.tag_type = def;
        set_type_pos_node (type, def);
        if (incomplete_type_p (c2m_ctx, type))
          error (c2m_ctx, POS (n), "enum storage size is unknown");
      } else {
        assert (def->code == N_SPEC_DECL);
        if (!def->attr) {
            error(c2m_ctx, POS(n), "declaration missing attribute");
            init_type(type);
            type->u.basic_type = TP_INT; // fail-safe fallback
        } else {
            decl = def->attr;
            decl->used_p = TRUE;
            assert (decl->decl_spec.typedef_p);
            if (decl->decl_spec.type) {
                *type = *decl->decl_spec.type;
            } else {
                printf("ERROR: N_ID %s has no type\n", n->u.s.s);
            }
        }
      }
      break;
    }
    case N_CLASS: {
                /* Skip generic class templates: mark with sentinel attr; only
                   their monomorphized specializations are checked/generated. */
                if (n->attr == (void *)((intptr_t)-1)) {
                  type->mode = TM_CLASS;
                  type->u.tag_type = n;
                  break;
                }
                int new_scope_p;
                node_t res_tag_type, id = NL_HEAD(n->u.ops);
                node_t decl_list = NL_NEXT(id);
                node_t saved_unnamed_anon_struct_union_member = curr_unnamed_anon_struct_union_member;
                const char *type_name = "class";

                set_type_pos_node(type, n);
                res_tag_type = process_tag(c2m_ctx, n, id, decl_list);
                check_type_duplication(c2m_ctx, type, n, type_name, size, sign);
                type->mode = TM_CLASS;
                type->u.tag_type = res_tag_type;
                new_scope_p = (id->code != N_IGNORE || decl_node->code != N_MEMBER
                               || NL_EL(decl_node->u.ops, 1)->code != N_IGNORE);
                type->unnamed_anon_struct_union_member_type_p = !new_scope_p;
                curr_unnamed_anon_struct_union_member = new_scope_p ? NULL : decl_node;

            // Register class as a type name if named
            // Note: use the *declaration scope* (curr_scope) as the symbol's scope key for proper find_def walking.
            // The def_node is the CLASS node (class_node_for_symbol) to which we attach .attr.
            node_t class_node_for_symbol = res_tag_type ? res_tag_type : n;
            if (id->code == N_ID) {
                symbol_insert(c2m_ctx, S_REGULARS, id, curr_scope, class_node_for_symbol, NULL);
                tpname_add(c2m_ctx, id, curr_scope, TRUE); // Register in tpname_tab
            }

            if (c2m_options->verbose_p || c2m_options->debug_p) {
                            printf("N_CLASS case: n_uid=%u res_tag_type_uid=%u class_node_for_symbol_uid=%u mode set to TM_CLASS\n",
                                   n->uid, res_tag_type ? res_tag_type->uid : 0,
                                   class_node_for_symbol ? class_node_for_symbol->uid : 0);
                        }

                if (decl_list->code != N_IGNORE) {
                    if (new_scope_p) create_node_scope(c2m_ctx, res_tag_type);
                    node_t prev_class = curr_class;
                    curr_class = res_tag_type;
                    if (c2m_options->verbose_p || c2m_options->debug_p)
                        printf("SET curr_class to uid=%u before check(decl_list)\n", res_tag_type ? res_tag_type->uid : 0);
                    check(c2m_ctx, decl_list, n);
                    curr_class = prev_class;
                    if (new_scope_p) finish_scope(c2m_ctx);
                    if (res_tag_type != n) make_type_complete(c2m_ctx, type);
                }
                curr_unnamed_anon_struct_union_member = saved_unnamed_anon_struct_union_member;
                set_class_layout(c2m_ctx, n, type);
                /* Phase 1: structurally verify any `impl I, J` clauses now that
                   the class's methods are registered in its scope. */
                verify_class_impls (c2m_ctx, n, res_tag_type);
                break;
            }
            case N_DICT: {
                set_type_pos_node(type, n);
                type->mode = TM_DICT;
                break;
            }
    case N_STRUCT:
    case N_UNION: {
      int new_scope_p;
      node_t res_tag_type, id = NL_HEAD (n->u.ops);
      node_t decl_list = NL_NEXT (id);
      node_t saved_unnamed_anon_struct_union_member = curr_unnamed_anon_struct_union_member;
      const char *type_name = n->code == N_STRUCT ? "struct" : n->code == N_CLASS ? "class" : "union";

      set_type_pos_node (type, n);

      res_tag_type = process_tag (c2m_ctx, n, id, decl_list);
      check_type_duplication (c2m_ctx, type, n, type_name, size, sign);
      type->mode = n->code == N_STRUCT ? TM_STRUCT : n->code == N_CLASS ? TM_CLASS : TM_UNION;
      type->u.tag_type = res_tag_type;
      new_scope_p = (id->code != N_IGNORE || decl_node->code != N_MEMBER
                     || NL_EL (decl_node->u.ops, 1)->code != N_IGNORE);
      type->unnamed_anon_struct_union_member_type_p = !new_scope_p;
      curr_unnamed_anon_struct_union_member = new_scope_p ? NULL : decl_node;
      if (decl_list->code != N_IGNORE) {
        if (new_scope_p) create_node_scope (c2m_ctx, res_tag_type);
        check (c2m_ctx, decl_list, n);
        if (new_scope_p) finish_scope (c2m_ctx);
        if (res_tag_type != n) make_type_complete (c2m_ctx, type); /* recalculate size */
      }
      curr_unnamed_anon_struct_union_member = saved_unnamed_anon_struct_union_member;

      break;
    }
    case N_ENUM: {
      node_t res_tag_type, id = NL_HEAD (n->u.ops);
      node_t enum_list = NL_NEXT (id);
      node_t enum_const_scope = skip_struct_scopes (curr_scope);

      set_type_pos_node (type, n);
      res_tag_type = process_tag (c2m_ctx, n, id, enum_list);
      check_type_duplication (c2m_ctx, type, n, "enum", size, sign);
      type->mode = TM_ENUM;
      type->u.tag_type = res_tag_type;
      /* Bare named-enum type names (`Faction x`) resolve via:
           · tpname (parse-time; already added when the definition is parsed)
           · S_TAG  (process_tag) then the N_ID / N_ENUM case in check_decl_spec
         Do NOT also insert S_REGULAR with the N_ENUM tag as def_node: the enum
         node never carries a decl_t attr, and the common C pattern
           typedef enum EPosition EPosition;
         would then crash in def_symbol reading ->decl_spec from a null/wrong
         attr (and report bogus "repeated declaration" errors).  Keep the
         parse-time tpname only; S_TAG fall-back covers type resolution. */
      if (id->code == N_ID)
        tpname_add (c2m_ctx, id, skip_struct_scopes (curr_scope), TRUE);
      if (enum_list->code == N_IGNORE) {
        if (incomplete_type_p (c2m_ctx, type))
          error (c2m_ctx, POS (n), "enum storage size is unknown");
      } else {
        mir_llong curr_val = -1, min_val = 0;
        mir_ullong max_val = 0;
        struct enum_type *enum_type;
        int neg_p = FALSE;

        n->attr = enum_type = reg_malloc (c2m_ctx, sizeof (struct enum_type));
        enum_type->enum_basic_type = TP_INT;                                           // ???
        for (node_t en = NL_HEAD (enum_list->u.ops); en != NULL; en = NL_NEXT (en)) {  // ??? id
          node_t const_expr;
          symbol_t sym;
          struct enum_value *enum_value;

          assert (en->code == N_ENUM_CONST);
          id = NL_HEAD (en->u.ops);
          const_expr = NL_NEXT (id);
          check (c2m_ctx, const_expr, n);
          if (symbol_find (c2m_ctx, S_REGULARS, id, enum_const_scope, &sym)) {
            error (c2m_ctx, POS (id), "enum constant %s redeclaration", id->u.s.s);
          } else {
            symbol_insert (c2m_ctx, S_REGULARS, id, enum_const_scope, en, n);
          }
          curr_val++;
          if (curr_val == 0) neg_p = FALSE;
          if (const_expr->code != N_IGNORE) {
            struct expr *cexpr = const_expr->attr;

            if (!cexpr->const_p) {
              error (c2m_ctx, POS (const_expr), "non-constant value in enum const expression");
              continue;
            } else if (!integer_type_p (cexpr->type)) {
              error (c2m_ctx, POS (const_expr), "enum const expression is not of an integer type");
              continue;
            }
            curr_val = cexpr->c.i_val;
            neg_p = signed_integer_type_p (cexpr->type) && cexpr->c.i_val < 0;
          }
          en->attr = enum_value = reg_malloc (c2m_ctx, sizeof (struct enum_value));
          if (!neg_p) {
            if (max_val < (mir_ullong) curr_val) max_val = (mir_ullong) curr_val;
            if (min_val < 0 && (mir_ullong) curr_val >= MIR_LLONG_MAX)
              error (c2m_ctx, POS (const_expr),
                     "enum const expression is not represented by an int");
            enum_value->u.u_val = (mir_ullong) curr_val;
          } else {
            if (min_val > curr_val) {
              min_val = curr_val;
              if (min_val < 0 && max_val >= MIR_LLONG_MAX)
                error (c2m_ctx, POS (const_expr),
                       "enum const expression is not represented by an int");
            } else if (curr_val >= 0 && max_val < (mir_ullong) curr_val) {
              max_val = curr_val;
            }
            enum_value->u.i_val = curr_val;
          }
          enum_type->enum_basic_type
            = (max_val <= MIR_INT_MAX && MIR_INT_MIN <= min_val     ? TP_INT
               : max_val <= MIR_UINT_MAX && 0 <= min_val            ? TP_UINT
               : max_val <= MIR_LONG_MAX && MIR_LONG_MIN <= min_val ? TP_LONG
               : max_val <= MIR_ULONG_MAX && 0 <= min_val           ? TP_ULONG
               : min_val < 0 || max_val <= MIR_LLONG_MAX            ? TP_LLONG
                                                                    : TP_ULLONG);
        }
      }
      break;
    }
    case N_ALIGNAS: {
      node_t el;
      int align = -1;

      if (decl_node->code == N_FUNC_DEF) {
        error (c2m_ctx, POS (n), "_Alignas for function");
      } else if (decl_node->code == N_MEMBER && (el = NL_EL (decl_node->u.ops, 3)) != NULL
                 && el->code != N_IGNORE) {
        error (c2m_ctx, POS (n), "_Alignas for a bit-field");
      } else if (decl_node->code == N_SPEC_DECL && in_params_p) {
        error (c2m_ctx, POS (n), "_Alignas for a function parameter");
      } else {
        node_t op = NL_HEAD (n->u.ops);

        check (c2m_ctx, op, n);
        if (op->code == N_TYPE) {
          struct decl_spec *decl_spec = op->attr;

          align = type_align (decl_spec->type);
        } else {
          struct expr *cexpr = op->attr;

          if (!cexpr->const_p) {
            error (c2m_ctx, POS (op), "non-constant value in _Alignas");
          } else if (!integer_type_p (cexpr->type)) {
            error (c2m_ctx, POS (op), "constant value in _Alignas is not of an integer type");
          } else if (!signed_integer_type_p (cexpr->type)
                     || !supported_alignment_p (cexpr->c.i_val)) {
            error (c2m_ctx, POS (op), "constant value in _Alignas specifies unsupported alignment");
          } else if (invalid_alignment (cexpr->c.i_val)) {
            error (c2m_ctx, POS (op), "unsupported alignmnent");
          } else {
            align = (int) cexpr->c.i_val;
          }
        }
        if (align != 0 && res->align < align) {
          res->align = align;
          res->align_node = n;
        }
      }
      break;
    }
    default:
        printf("ERROR: Invalide n->code = %d\n", n->code);
        exit(-1);
    }
  if (type->mode == TM_BASIC && type->u.basic_type == TP_UNDEF) {
    if (size == 0 && sign == 0) {
      /* `auto x = init;` has no type specifier on purpose: the type is inferred
         from the initializer later, so don't warn about defaulting to int. */
      if (!res->auto_p)
        (c2m_options->pedantic_p ? error (c2m_ctx, POS (r), "no any type specifier")
                                 : warning (c2m_ctx, POS (r), "type defaults to int"));
      type->u.basic_type = TP_INT;
    } else if (size == 0) {
      type->u.basic_type = sign >= 0 ? TP_INT : TP_UINT;
    } else if (size == 1) {
      type->u.basic_type = sign >= 0 ? TP_SHORT : TP_USHORT;
    } else if (size == 2) {
      type->u.basic_type = sign >= 0 ? TP_LONG : TP_ULONG;
    } else {
      type->u.basic_type = sign >= 0 ? TP_LLONG : TP_ULLONG;
    }
  }
  set_type_qual (c2m_ctx, r, &type->type_qual, type->mode);
  if (res->align_node) {
    if (res->typedef_p)
      error (c2m_ctx, POS (res->align_node), "_Alignas in typedef");
    else if (res->register_p)
      error (c2m_ctx, POS (res->align_node), "_Alignas with register");
  }
  return *res;
}

static struct type *append_type (struct type *head, struct type *el) {
  struct type **holder;

  if (head == NULL) return el;
  if (head->mode == TM_PTR) {
    holder = &head->u.ptr_type;
  } else if (head->mode == TM_ARR) {
    holder = &head->u.arr_type->el_type;
  } else {
    assert (head->mode == TM_FUNC);
    holder = &head->u.func_type->ret_type;
  }
  *holder = append_type (*holder, el);
  return head;
}

static int void_param_p (node_t param) {
  struct decl_spec *decl_spec;
  struct type *type;

  if (param != NULL && param->code == N_TYPE) {
    decl_spec = param->attr;
    type = decl_spec->type;
    if (void_type_p (type)) return TRUE;
  }
  return FALSE;
}

static void adjust_param_type (c2m_ctx_t c2m_ctx, struct type **type_ptr) {
  struct type *par_type, *type = *type_ptr;
  struct arr_type *arr_type;

  if (type->mode == TM_ARR) {  // ??? static, old type qual
    arr_type = type->u.arr_type;
    par_type = create_type (c2m_ctx, NULL);
    par_type->mode = TM_PTR;
    par_type->pos_node = type->pos_node;
    par_type->u.ptr_type = arr_type->el_type;
    par_type->type_qual = arr_type->ind_type_qual;
    par_type->arr_type = type;
    *type_ptr = type = par_type;
    make_type_complete (c2m_ctx, type);
  } else if (type->mode == TM_FUNC) {
    par_type = create_type (c2m_ctx, NULL);
    par_type->mode = TM_PTR;
    par_type->pos_node = type->pos_node;
    par_type->func_type_before_adjustment_p = TRUE;
    par_type->u.ptr_type = type;
    *type_ptr = type = par_type;
    make_type_complete (c2m_ctx, type);
  }
}

static struct type *check_declarator (c2m_ctx_t c2m_ctx, node_t r, int func_def_p) {
  check_ctx_t check_ctx = c2m_ctx->check_ctx;
  struct type *type, *res = NULL;
  node_t list = NL_EL (r->u.ops, 1);

  assert (r->code == N_DECL);
  if (NL_HEAD (list->u.ops) == NULL) return NULL;
  for (node_t n = NL_HEAD (list->u.ops); n != NULL; n = NL_NEXT (n)) {
    type = create_type (c2m_ctx, NULL);
    type->pos_node = n;
    switch (n->code) {
    case N_POINTER: {
      node_t type_qual = NL_HEAD (n->u.ops);

      type->mode = TM_PTR;
      type->pos_node = n;
      type->u.ptr_type = NULL;
      set_type_qual (c2m_ctx, type_qual, &type->type_qual, TM_PTR);
      break;
    }
    case N_ARR: {
      struct arr_type *arr_type;
      node_t static_node = NL_HEAD (n->u.ops);
      node_t type_qual = NL_NEXT (static_node);
      node_t size = NL_NEXT (type_qual);

      type->mode = TM_ARR;
      type->pos_node = n;
      type->u.arr_type = arr_type = reg_malloc (c2m_ctx, sizeof (struct arr_type));
      clear_type_qual (&arr_type->ind_type_qual);
      set_type_qual (c2m_ctx, type_qual, &arr_type->ind_type_qual, TM_UNDEF);
      check (c2m_ctx, size, n);
      arr_type->size = size;
      arr_type->static_p = static_node->code == N_STATIC;
      arr_type->flex_p = 0;
      arr_type->flex_bound_member = NULL;
      arr_type->el_type = NULL;
      break;
    }
    case N_FUNC: {
      struct func_type *func_type;
      node_t first_param, param_list = NL_HEAD (n->u.ops);
      node_t last = NL_TAIL (param_list->u.ops);
      int saved_in_params_p = in_params_p;

      type->mode = TM_FUNC;
      type->pos_node = n;
      type->u.func_type = func_type = reg_malloc (c2m_ctx, sizeof (struct func_type));
      func_type->ret_type = NULL;
      func_type->proto_item = NULL;
      func_type->class_scope = (curr_scope && curr_scope->code == N_CLASS) ? curr_scope : NULL;

      //printf("func_type curr_scope class=%s code=%s\n", curr_scope->code==N_CLASS?"CLASS":"NO CLASS",
      //        get_token_name( c2m_ctx, curr_scope->code ));

      if ((func_type->dots_p = last != NULL && last->code == N_DOTS))
        NL_REMOVE (param_list->u.ops, last);
      if (!func_def_p) create_node_scope (c2m_ctx, n);
      func_type->param_list = param_list;
      in_params_p = TRUE;
      first_param = NL_HEAD (param_list->u.ops);
      if (first_param != NULL && first_param->code != N_ID) check (c2m_ctx, first_param, n);
      if (void_param_p (first_param)) {
        struct decl_spec *ds = first_param->attr;
        if (non_reg_decl_spec_p (ds) || ds->register_p
            || !type_qual_eq_p (&ds->type->type_qual, &zero_type_qual)) {
          error (c2m_ctx, POS (first_param), "qualified void parameter");
        }
        if (NL_NEXT (first_param) != NULL) {
          error (c2m_ctx, POS (first_param), "void must be the only parameter");
        }
      } else {
        for (node_t p = first_param; p != NULL; p = NL_NEXT (p)) {
          struct decl_spec *decl_spec_ptr;
          if (p->code == N_ID) {
            if (!func_def_p)
              error (c2m_ctx, POS (p), "parameters identifier list can be only in function definition");
            break;
          } else {
            if (p != first_param) check (c2m_ctx, p, n);
            decl_spec_ptr = get_param_decl_spec (p);
            adjust_param_type (c2m_ctx, &decl_spec_ptr->type);
          }
        }
      }
      in_params_p = saved_in_params_p;
      if (!func_def_p) finish_scope (c2m_ctx);
      break;
    }

    default: abort ();
    }
    res = append_type (res, type);
  }
  return res;
}

static int check_case_expr (c2m_ctx_t c2m_ctx, node_t case_expr, struct type *type, node_t target) {
  struct expr *expr;

  check (c2m_ctx, case_expr, target);
  expr = case_expr->attr;
  if (!expr->const_p) {
    error (c2m_ctx, POS (case_expr), "case-expr is not a constant expression");
    return FALSE;
  } else if (!integer_type_p (expr->type)) {
    error (c2m_ctx, POS (case_expr), "case-expr is not an integer type expression");
    return FALSE;
  } else {
    convert_value (expr, type);
    return TRUE;
  }
}

static void check_labels (c2m_ctx_t c2m_ctx, node_t labels, node_t target) {
  check_ctx_t check_ctx = c2m_ctx->check_ctx;

  for (node_t l = NL_HEAD (labels->u.ops); l != NULL; l = NL_NEXT (l)) {
    if (l->code == N_LABEL) {
      symbol_t sym;
      node_t id = NL_HEAD (l->u.ops);

      if (symbol_find (c2m_ctx, S_LABEL, id, func_block_scope, &sym)) {
        error (c2m_ctx, POS (id), "label %s redeclaration", id->u.s.s);
      } else {
            symbol_insert (c2m_ctx, S_LABEL, id, func_block_scope, target, NULL);
          }
        } else if (curr_switch == NULL) {
          error (c2m_ctx, POS (l), "%s not within a switch-stmt",
                 l->code == N_CASE ? "case label" : "default label");
        } else {
          struct switch_attr *switch_attr = curr_switch->attr;
          struct type *type = &switch_attr->type;
          node_t case_expr = l->code == N_CASE ? NL_HEAD (l->u.ops) : NULL;
          node_t case_expr2 = l->code == N_CASE ? NL_EL (l->u.ops, 1) : NULL;
          case_t case_attr, tail = DLIST_TAIL (case_t, switch_attr->case_labels);
          int ok_p = FALSE, default_p = tail != NULL && tail->case_node->code == N_DEFAULT;

          if (case_expr == NULL) {
            if (default_p) {
              error (c2m_ctx, POS (l), "multiple default labels in one switch");
            } else {
              ok_p = TRUE;
            }
          } else {
            ok_p = check_case_expr (c2m_ctx, case_expr, type, target);
            if (case_expr2 != NULL) {
              ok_p = check_case_expr (c2m_ctx, case_expr2, type, target) && ok_p;
              (c2m_options->pedantic_p ? error : warning) (c2m_ctx, POS (l),
                                                           "range cases are not a part of C standard");
            }
          }
          if (ok_p) {
            case_attr = reg_malloc (c2m_ctx, sizeof (struct case_attr));
            case_attr->case_node = l;
            case_attr->case_target_node = target;
            if (default_p) {
              DLIST_INSERT_BEFORE (case_t, switch_attr->case_labels, tail, case_attr);
            } else {
              DLIST_APPEND (case_t, switch_attr->case_labels, case_attr);
            }
          }
        }
      }
    }

    static node_code_t get_id_linkage (c2m_ctx_t c2m_ctx, int func_p, node_t id, node_t scope,
                                       struct decl_spec decl_spec) {
      node_code_t linkage;
      node_t def = find_def (c2m_ctx, S_REGULARS, id, scope, NULL);

      if (decl_spec.typedef_p) return N_IGNORE;                       // p6: no linkage
      if (decl_spec.static_p && scope == top_scope) return N_STATIC;  // p3: internal linkage
      if (decl_spec.extern_p && def != NULL
          && (linkage = ((decl_t) def->attr)->decl_spec.linkage) != N_IGNORE)
        return linkage;  // p4: previous linkage
      if (decl_spec.extern_p && (def == NULL || ((decl_t) def->attr)->decl_spec.linkage == N_IGNORE))
        return N_EXTERN;  // p4: external linkage
      if (!decl_spec.static_p && !decl_spec.extern_p && (scope == top_scope || func_p))
        return N_EXTERN;                                                          // p5
      if (!decl_spec.extern_p && scope != top_scope && !func_p) return N_IGNORE;  // p6: no linkage
      return N_IGNORE;
    }

    static void check_type (c2m_ctx_t c2m_ctx, struct type *type, int level, int func_def_p) {
      check_ctx_t check_ctx = c2m_ctx->check_ctx;

      switch (type->mode) {
      case TM_PTR: check_type (c2m_ctx, type->u.ptr_type, level + 1, FALSE); break;
      case TM_STRUCT:
      case TM_UNION: break;
      case TM_CLASS:
        if (c2m_options->debug_p) printf("check_type TM_CLASS\n");
        break;
      case TM_ARR: {
        struct arr_type *arr_type = type->u.arr_type;
        node_t size_node = arr_type->size;
        struct type *el_type = arr_type->el_type;

        if (size_node->code == N_STAR) {
          error (c2m_ctx, POS (size_node), "variable size arrays are not supported");
        } else if (size_node->code != N_IGNORE) {
          struct expr *cexpr = size_node->attr;

          if (!integer_type_p (cexpr->type)) {
            error (c2m_ctx, POS (size_node), "non-integer array size type");
          } else if (!cexpr->const_p) {
            error (c2m_ctx, POS (size_node), "variable size arrays are not supported");
          } else if (signed_integer_type_p (cexpr->type) && cexpr->c.i_val < 0) {
            error (c2m_ctx, POS (size_node), "array size should be not negative");
          } else if (cexpr->c.i_val == 0) {
            /* GNU zero-length arrays: silent by default, warn under -pedantic
               (8f3934ac; matches gcc/clang). */
            if (c2m_options->pedantic_p)
              warning (c2m_ctx, POS (size_node), "zero array size");
          }
        }
        check_type (c2m_ctx, el_type, level + 1, FALSE);
        if (el_type->mode == TM_FUNC) {
          error (c2m_ctx, POS (type->pos_node), "array of functions");
        } else if (incomplete_type_p (c2m_ctx, el_type)) {
          error (c2m_ctx, POS (type->pos_node), "incomplete array element type");
        } else if (!in_params_p || level != 0) {
          if (arr_type->static_p)
            error (c2m_ctx, POS (type->pos_node), "static should be only in parameter outermost");
          else if (!type_qual_eq_p (&arr_type->ind_type_qual, &zero_type_qual))
            error (c2m_ctx, POS (type->pos_node),
                   "type qualifiers should be only in parameter outermost array");
        }
        break;
      }
      case TM_FUNC: {
        struct decl_spec decl_spec;
        struct func_type *func_type = type->u.func_type;
        struct type *ret_type = func_type->ret_type;
        node_t first_param, param_list = func_type->param_list;

        check_type (c2m_ctx, ret_type, level + 1, FALSE);
        if (ret_type->mode == TM_FUNC) {
          error (c2m_ctx, POS (ret_type->pos_node), "function returning a function");
        } else if (ret_type->mode == TM_ARR) {
          error (c2m_ctx, POS (ret_type->pos_node), "function returning an array");
        }
        first_param = NL_HEAD (param_list->u.ops);
        if (!void_param_p (first_param)) {
          for (node_t p = first_param; p != NULL; p = NL_NEXT (p)) {
            if (p->code == N_TYPE) {
              decl_spec = *((struct decl_spec *) p->attr);
              check_type (c2m_ctx, decl_spec.type, level + 1, FALSE);
            } else if (p->code == N_SPEC_DECL) {
              decl_spec = ((decl_t) p->attr)->decl_spec;
              check_type (c2m_ctx, decl_spec.type, level + 1, FALSE);
            } else {
              assert (p->code == N_ID);
              break;
            }
            if (non_reg_decl_spec_p (&decl_spec)) {
              error (c2m_ctx, POS (p), "prohibited specifier in a function parameter");
            } else if (func_def_p) {
              if (p->code == N_TYPE)
                error (c2m_ctx, POS (p), "parameter type without a name in function definition");
              else if (incomplete_type_p (c2m_ctx, decl_spec.type))
                error (c2m_ctx, POS (p), "incomplete parameter type in function definition");
            }
          }
        }
        break;
      }
      case TM_BASIC: break;
      default:
        //printf("UNKNOWN TM TYPE MODE %d\n", type->mode);
        break;  // ???
      }
    }

    /* Like find_def, but returns the whole symbol (with its `defs` overload set)
       found by walking up from `scope`. */
    static int find_overload_sym (c2m_ctx_t c2m_ctx, node_t id, node_t scope, symbol_t *out) {
      for (;;) {
        if (symbol_find (c2m_ctx, S_REGULARS, id, scope, out)) return TRUE;
        if (scope == NULL || scope->attr == NULL) return FALSE;
        scope = ((struct node_scope *) scope->attr)->scope;
      }
    }

    /* Choose the best-matching overload of a class method.  `msym` holds every
       method def registered under one name (in declaration order); `class_tag`
       is the receiver's class node, used to ignore unrelated same-named methods
       of other classes (methods are registered by plain name).  Returns the
       chosen N_FUNC_DEF, or `msym->def_node` (first declared) as a fallback so
       the single-method case is unchanged.  Scoring favours exact parameter-type
       matches over compatible/convertible ones; argument counts must match
       (unless the candidate is variadic).  `arg_list` holds the already-checked
       user arguments (no implicit `this`). */
    static node_t select_method_overload (c2m_ctx_t c2m_ctx, symbol_t *msym, node_t class_tag,
                                          node_t arg_list) {
      size_t i, n = VARR_LENGTH (node_t, msym->defs);
      node_t best = NULL;
      int best_score = -1;

      if (n <= 1) return msym->def_node;
      for (i = 0; i < n; i++) {
        node_t def = VARR_GET (node_t, msym->defs, i);
        decl_t d;
        struct func_type *ft;
        node_t param, a;
        int score = 0, ok = TRUE;
        if (def->code != N_FUNC_DEF) continue;
        d = def->attr;
        if (d == NULL || d->decl_spec.type == NULL || d->decl_spec.type->mode != TM_FUNC) continue;
        ft = d->decl_spec.type->u.func_type;
        /* Only consider overloads belonging to the receiver's class. */
        if (class_tag != NULL && ft->class_scope != NULL && ft->class_scope != class_tag) continue;
        param = NL_HEAD (ft->param_list->u.ops);
        if (!d->decl_spec.static_p && param != NULL) param = NL_NEXT (param); /* skip 'this' */
        a = NL_HEAD (arg_list->u.ops);
        for (; param != NULL && a != NULL; param = NL_NEXT (param), a = NL_NEXT (a)) {
          struct decl_spec *pds = get_param_decl_spec (param);
          struct expr *ae = a->attr;
          struct type *pt = pds != NULL ? pds->type : NULL;
          struct type *at = ae != NULL ? ae->type : NULL;
          if (pt == NULL || at == NULL) { ok = FALSE; break; }
          if (type_eq_p (pt, at))
            score += 3;
          else if (compatible_types_p (pt, at, TRUE))
            score += 2;
          /* String parameter accepts string literals (TM_ARR of char) and other
             String values; use the same leniency as check_assignment_types. */
          else if (builtin_string_type_p (pt) && str_concat_string_operand_p (at, a))
            score += 2;
          else if (arithmetic_type_p (pt) && arithmetic_type_p (at))
            score += 1; /* implicit arithmetic conversion */
          else if (pt->mode == TM_PTR && (at->mode == TM_PTR || at->mode == TM_ARR))
            score += 1; /* pointer/array compatibility, refined later */
          else { ok = FALSE; break; }
        }
        if (!ok) continue;
        if ((param != NULL || a != NULL) && !ft->dots_p) continue; /* arg-count mismatch */
        if (score > best_score) { best_score = score; best = def; }
      }
      return best != NULL ? best : msym->def_node;
    }

    /* Name of a class type (its tag), or NULL for a non-class/anonymous type. */
    static const char *class_type_name (const struct type *type) {
      node_t id;
      if (type == NULL || type->mode != TM_CLASS || type->u.tag_type == NULL) return NULL;
      id = TAG_ID (type->u.tag_type);
      return (id != NULL && id->code == N_ID) ? id->u.s.s : NULL;
    }

    /* True when TYPE is the List<String> generic specialization (the receiver
       type accepted by the built-in `.join(delim)` method).  A pointer to such
       a class also qualifies (one level is peeled), so both `List<String>` and
       `List<String>*` receivers match. */
    static int list_string_type_p (const struct type *type) {
      const char *name;
      if (type != NULL && type->mode == TM_PTR) type = type->u.ptr_type;
      name = class_type_name (type);
      return name != NULL && strcmp (name, "__generic_List_String") == 0;
    }

    /* Find a non-static DATA member NAME in class CLASS_TAG and return its
       N_MEMBER node (whose attr is the decl_t carrying the member's offset and
       type).  Method members and static members are skipped.  Used by the
       object-initializer form `new T(args) { .field = value, ... }` so check
       and gen agree on which field each designator targets. */
    static node_t find_class_field_member (c2m_ctx_t c2m_ctx MIR_UNUSED, node_t class_tag,
                                           const char *name) {
      node_t member_list;
      if (class_tag == NULL) return NULL;
      member_list = TAG_MEMBER_LIST (class_tag);
      if (member_list == NULL || member_list->code != N_LIST) return NULL;
      for (node_t m = NL_HEAD (member_list->u.ops); m != NULL; m = NL_NEXT (m)) {
        node_t declarator, mid;
        decl_t md;
        struct type *mt;
        if (m->code != N_MEMBER) continue;
        md = (decl_t) m->attr;
        if (md == NULL || (mt = md->decl_spec.type) == NULL) continue;
        if (mt->mode == TM_FUNC || md->decl_spec.static_p) continue;
        declarator = NL_EL (m->u.ops, 1);
        mid = (declarator != NULL && declarator->code == N_DECL)
                ? NL_HEAD (declarator->u.ops) : NULL;
        if (mid != NULL && mid->code == N_ID && strcmp (mid->u.s.s, name) == 0)
          return m;
      }
      return NULL;
    }

    /* Duck-typed protocol lookup: find an instance method NAME belonging to
       class CLASS_TAG (methods are registered by plain name in an enclosing
       scope, so candidates are filtered by their func_type->class_scope; a
       global function of the same name is never picked up) taking exactly
       N_USER_PARAMS arguments (not counting the implicit 'this').  Used by
       the brace-init protocol (new T{...} needs Add(item)) and the for-in
       iteration protocol (Count() / Get(int)).  Returns the N_FUNC_DEF node
       or NULL.  Both check and gen resolve through this helper so they always
       agree on the chosen overload. */
    static node_t find_class_protocol_method (c2m_ctx_t c2m_ctx, node_t class_tag,
                                              const char *name, int n_user_params, pos_t pos) {
      symbol_t sym;
      node_t id;

      if (class_tag == NULL) return NULL;
      id = build_id (c2m_ctx, name, pos);
      if (!find_overload_sym (c2m_ctx, id, class_tag, &sym)) return NULL;
      for (size_t i = 0; i < VARR_LENGTH (node_t, sym.defs); i++) {
        node_t def = VARR_GET (node_t, sym.defs, i);
        decl_t d;
        struct func_type *ft;
        node_t param;
        int n = 0;

        if (def == NULL || def->code != N_FUNC_DEF) continue;
        d = def->attr;
        if (d == NULL || d->decl_spec.type == NULL || d->decl_spec.type->mode != TM_FUNC) continue;
        if (d->decl_spec.static_p) continue; /* protocol methods are instance methods */
        ft = d->decl_spec.type->u.func_type;
        if (ft->class_scope != class_tag) continue; /* method of another class / global func */
        param = NL_HEAD (ft->param_list->u.ops);
        if (param != NULL) param = NL_NEXT (param); /* skip implicit 'this' */
        for (; param != NULL; param = NL_NEXT (param)) {
          if (param->code != N_SPEC_DECL && param->code != N_TYPE) { n = -1; break; } /* dots */
          n++;
        }
        if (n == n_user_params) return def;
      }
      return NULL;
    }

    /* Phase 1 conformance: does class CLASS_TAG structurally satisfy interface
       IFACE_NODE?  For each interface method (matched by name + user-arg count,
       the same structural rule find_class_protocol_method uses for Count/Get/
       Add), the class must have a matching instance method.  Returns 1 on
       success; on failure returns 0 and sets *MISSING_OUT to the unsatisfied
       method's name.  This is the single source of truth for "is C an I?". */
    static int class_satisfies_interface_p (c2m_ctx_t c2m_ctx, node_t class_tag,
                                            node_t iface_node, const char **missing_out) {
      node_t members;

      if (missing_out != NULL) *missing_out = NULL;
      if (class_tag == NULL || iface_node == NULL || iface_node->code != N_INTERFACE) return 1;
      members = TAG_MEMBER_LIST (iface_node); /* N_LIST of N_MEMBER prototypes */
      for (node_t m = NL_HEAD (members->u.ops); m != NULL; m = NL_NEXT (m)) {
        node_t declr, mid, decl_list, func;
        int nparams = 0;

        if (m->code != N_MEMBER) continue;
        declr = NL_EL (m->u.ops, 1); /* declarator */
        if (declr == NULL || declr->code != N_DECL) continue;
        mid = NL_HEAD (declr->u.ops); /* method name */
        if (mid == NULL || mid->code != N_ID) continue;
        /* Count user parameters from the function declarator. */
        decl_list = NL_NEXT (mid);
        func = decl_list != NULL ? NL_HEAD (decl_list->u.ops) : NULL;
        if (func != NULL && func->code == N_FUNC) {
          node_t plist = NL_HEAD (func->u.ops);
          for (node_t p = NL_HEAD (plist->u.ops); p != NULL; p = NL_NEXT (p)) {
            if (p->code != N_SPEC_DECL && p->code != N_TYPE) { nparams = -1; break; }
            nparams++;
          }
        }
        if (find_class_protocol_method (c2m_ctx, class_tag, mid->u.s.s, nparams, POS (mid))
            == NULL) {
          if (missing_out != NULL) *missing_out = mid->u.s.s;
          return 0;
        }
      }
      return 1;
    }

    /* Verify every `impl` clause on a class definition (the optional 3rd child
       of the N_CLASS node).  Emits a precise diagnostic for each unsatisfied or
       unknown interface.  No-op when the class has no impl clause.  Conformance
       does not depend on `impl`; this only provides EARLY, opt-in checking. */
    static void verify_class_impls (c2m_ctx_t c2m_ctx, node_t class_node, node_t class_tag) {
      node_t impl_list, cid;
      const char *cname;

      if (class_node == NULL || class_node->code != N_CLASS) return;
      impl_list = NL_EL (class_node->u.ops, 2); /* 3rd child or NULL */
      if (impl_list == NULL || impl_list->code != N_LIST) return;
      cid = NL_HEAD (class_node->u.ops);
      cname = (cid != NULL && cid->code == N_ID) ? cid->u.s.s : "<class>";
      for (node_t in = NL_HEAD (impl_list->u.ops); in != NULL; in = NL_NEXT (in)) {
        node_t iface;
        const char *missing = NULL;

        if (in->code != N_ID) continue;
        iface = find_interface (c2m_ctx, in->u.s.s);
        if (iface == NULL) {
          error (c2m_ctx, POS (in), "class %s impl unknown interface %s", cname, in->u.s.s);
          continue;
        }
        if (!class_satisfies_interface_p (c2m_ctx, class_tag, iface, &missing))
          error (c2m_ctx, POS (in),
                 "class %s does not satisfy interface %s: missing %s()", cname, in->u.s.s,
                 missing != NULL ? missing : "method");
      }
    }

    /* Implicit erasure: when ARG (currently a concrete C*) is being placed where
       an erased handle __Any_I* is expected, and C structurally satisfies I,
       transparently rewrite ARG in PARENT_LIST as `any<I>(arg)` and check it.
       This lets a developer write `list->Add(new Button(...))` or a brace
       initializer `new List<Any<View>*>{ new Button(...), ... }` without an
       explicit any<View>(...) at every element.  Returns the new N_ANY node on
       success (now occupying ARG's slot in PARENT_LIST), or NULL if no coercion
       applies. */
    static node_t try_coerce_to_any (c2m_ctx_t c2m_ctx, node_t parent_list, node_t arg,
                                     struct type *target) {
      struct expr *ae;
      struct type *ccls, *tcls;
      const char *sname, *cname, *iface_name;
      node_t iface, anyn, iface_id, struct_id;
      pos_t pos;

      if (parent_list == NULL || arg == NULL || target == NULL || target->mode != TM_PTR) return NULL;
      tcls = target->u.ptr_type;
      if (tcls == NULL || tcls->mode != TM_CLASS) return NULL;
      sname = class_type_name (tcls); /* e.g. "__Any_View" */
      if (sname == NULL || strncmp (sname, "__Any_", 6) != 0) return NULL;

      ae = arg->attr;
      if (ae == NULL || ae->type == NULL || ae->type->mode != TM_PTR) return NULL;
      ccls = ae->type->u.ptr_type;
      if (ccls == NULL || ccls->mode != TM_CLASS || ccls->u.tag_type == NULL) return NULL;
      cname = class_type_name (ccls);
      if (cname != NULL && strncmp (cname, "__Any_", 6) == 0) return NULL; /* already erased */

      iface_name = sname + 6; /* strip the "__Any_" prefix */
      iface = find_interface (c2m_ctx, iface_name);
      if (iface == NULL) return NULL;
      if (!class_satisfies_interface_p (c2m_ctx, ccls->u.tag_type, iface, NULL)) return NULL;

      pos = POS (arg);
      iface_id = build_id (c2m_ctx, iface_name, pos);
      struct_id = build_id (c2m_ctx, sname, pos);
      anyn = new_pos_node2 (c2m_ctx, N_ANY, pos, iface_id, struct_id);
      DLIST_INSERT_BEFORE (node_t, parent_list->u.ops, arg, anyn);
      NL_REMOVE (parent_list->u.ops, arg);
      op_append (c2m_ctx, anyn, arg); /* arg becomes the N_ANY's 3rd child (already checked) */
      check (c2m_ctx, anyn, parent_list);
      return anyn;
    }

    static void check_assignment_types (c2m_ctx_t c2m_ctx, struct type *left, struct type *right,
                                        struct expr *expr, node_t assign_node) {
      node_code_t code = assign_node->code;
      const char *msg;

      if (right == NULL) right = expr->type;
      /* A dict value (DictValue*) coerces to any scalar lvalue/parameter: gen
         unwraps the union payload (int64_value / string_value / ...) to the
         target type at run time.  Assigning into a dict field is handled by the
         left->mode == TM_DICT case below. */
      if (right != NULL && right->mode == TM_DICT && left != NULL
          && (arithmetic_type_p (left) || left->mode == TM_PTR || string_type_p (left)))
        return;
      if (arithmetic_type_p (left)) {
        if (!arithmetic_type_p (right)
            && !(left->mode == TM_BASIC && left->u.basic_type == TP_BOOL && right->mode == TM_PTR)) {
          if (integer_type_p (left) && right->mode == TM_PTR) {
            msg = (code == N_CALL     ? "using pointer without cast for integer type parameter"
                   : code == N_RETURN ? "returning pointer without cast for integer result"
                                      : "assigning pointer without cast to integer");
            (c2m_options->pedantic_p ? error : warning) (c2m_ctx, POS (assign_node), "%s", msg);
          } else {
            msg = (code == N_CALL ? "incompatible argument type for arithmetic type parameter"
                   : code != N_RETURN
                     ? "incompatible types in assignment to an arithmetic type lvalue"
                     : "incompatible return-expr type in function returning an arithmetic value");
            error (c2m_ctx, POS (assign_node), "%s", msg);
          }
        }
	      } else if (builtin_string_type_p (left)) {
          /* String lvalue: accept String, char*, const char*, and string literals
             (TM_ARR of char), so that `String key = "foo"` and struct field
             initializers like `{.key = "Content-Type"}` work naturally. */
          int rhs_ok = string_type_p (right)
                       || null_const_p (expr, right)
                       || (right->mode == TM_PTR && right->u.ptr_type != NULL
                           && right->u.ptr_type->mode == TM_BASIC
                           && char_type_p (right->u.ptr_type))
                       || (right->mode == TM_ARR && right->u.arr_type != NULL
                           && right->u.arr_type->el_type != NULL
                           && right->u.arr_type->el_type->mode == TM_BASIC
                           && char_type_p (right->u.arr_type->el_type));
          if (!rhs_ok) {
            msg = (code == N_CALL ? "incompatible argument type for string type parameter"
                   : code != N_RETURN
                     ? "incompatible types in assignment to an string type lvalue"
                     : "incompatible return-expr type in function returning an string value");
            error (c2m_ctx, POS (assign_node), "%s", msg);
          }
      } else if (left->mode == TM_STRUCT || left->mode == TM_UNION) {
        if ((right->mode != TM_STRUCT && right->mode != TM_UNION)
            || !compatible_types_p (left, right, TRUE)) {
          msg = (code == N_CALL ? "incompatible argument type for struct/union type parameter"
                 : code != N_RETURN
                   ? "incompatible types in assignment to struct/union"
                   : "incompatible return-expr type in function returning a struct/union");
          error (c2m_ctx, POS (assign_node), "%s", msg);
        }
      } else if (left->mode == TM_CLASS ) {
        if ((right->mode != TM_CLASS )
            || !compatible_types_p (left, right, TRUE)) {
          const char *cname;
          /* Very common mistake: assigning a `ClassName *` -- typically the
             result of `new ClassName(...)`, which heap-allocates and yields a
             pointer -- to a by-value `ClassName` target.  Detect that exact case
             and emit an actionable hint instead of a cryptic type error. */
          if (right->mode == TM_PTR && right->u.ptr_type != NULL
              && right->u.ptr_type->mode == TM_CLASS
              && left->u.tag_type == right->u.ptr_type->u.tag_type
              && (cname = class_type_name (left)) != NULL) {
            char hint[512];
            if (code == N_CALL)
              snprintf (hint, sizeof hint,
                        "cannot pass a '%s *' (e.g. the result of `new %s(...)`) where a "
                        "by-value '%s' parameter is expected; make the parameter a pointer '%s *'",
                        cname, cname, cname, cname);
            else if (code == N_RETURN)
              snprintf (hint, sizeof hint,
                        "cannot return a '%s *' (e.g. the result of `new %s(...)`) from a function "
                        "returning a by-value '%s'; make the return type a pointer '%s *'",
                        cname, cname, cname, cname);
            else
              snprintf (hint, sizeof hint,
                        "cannot assign a '%s *' (e.g. the result of `new %s(...)`) to a by-value "
                        "'%s'; declare the variable as a pointer instead: `%s *p = new %s(...);`",
                        cname, cname, cname, cname, cname);
            error (c2m_ctx, POS (assign_node), "%s", hint);
          } else {
            msg = (code == N_CALL ? "incompatible argument type for class type parameter"
                   : code != N_RETURN
                     ? "incompatible types in assignment to class"
                     : "incompatible return-expr type in function returning a class");
            error (c2m_ctx, POS (assign_node), "%s", msg);
          }
        }
      } else if (left->mode == TM_PTR) {
          if (null_const_p (expr, right)) {
          } else if (builtin_string_type_p (right)
                   && (void_ptr_p (left)
                       || (left->u.ptr_type->mode == TM_BASIC
                           && char_type_p (left->u.ptr_type)))) {
          /* A built-in String is a char*; accept it wherever char*, const char*,
             or void* is expected (strcmp, strlen, free, memcpy, ...). */
        } else if (right->mode == TM_CLASS || right->mode == TM_STRUCT
                   || right->mode == TM_UNION) {
            /* Map/List Copy() returns a by-value collection — do not assign to T*. */
            msg = (code == N_CALL
                     ? "cannot pass a by-value class where a pointer is expected"
                   : code == N_RETURN
                     ? "cannot return a by-value class for a pointer result"
                     : "cannot assign a by-value class to a pointer (use `auto x = f.Copy()`)");
            error (c2m_ctx, POS (assign_node), "%s", msg);
        } else if (right->mode != TM_PTR
                   || !(compatible_types_p (left->u.ptr_type, right->u.ptr_type, TRUE)
                        || (void_ptr_p (left) || void_ptr_p (right))
                        || (left->u.ptr_type->mode == TM_ARR
                            && compatible_types_p (left->u.ptr_type->u.arr_type->el_type,
                                                   right->u.ptr_type, TRUE)))) {
          if (right->mode == TM_PTR && left->u.ptr_type->mode == TM_BASIC
              && right->u.ptr_type->mode == TM_BASIC) {
            msg = (code == N_CALL     ? "incompatible pointer types of argument and parameter"
                   : code == N_RETURN ? "incompatible pointer types of return-expr and function result"
                                      : "incompatible pointer types in assignment");
            int sign_diff_p = char_type_p (left->u.ptr_type) && char_type_p (right->u.ptr_type);
            if (!sign_diff_p || c2m_options->pedantic_p)
              (c2m_options->pedantic_p && !sign_diff_p ? error : warning) (c2m_ctx, POS (assign_node),
                                                                           "%s", msg);
          } else if (integer_type_p (right)) {
            msg = (code == N_CALL     ? "using integer without cast for pointer type parameter"
                   : code == N_RETURN ? "returning integer without cast for pointer result"
                                      : "assigning integer without cast to pointer");
            (c2m_options->pedantic_p ? error : warning) (c2m_ctx, POS (assign_node), "%s", msg);
          } else {
            msg = (code == N_CALL     ? "incompatible argument type for pointer type parameter"
                   : code == N_RETURN ? "incompatible return-expr type in function returning a pointer"
                                      : "incompatible types in assignment to a pointer");
            (c2m_options->pedantic_p || right->mode != TM_PTR ? error : warning) (c2m_ctx,
                                                                                  POS (assign_node),
                                                                                  "%s", msg);
          }
        } else if (right->u.ptr_type->type_qual.atomic_p) {
          msg = (code == N_CALL     ? "passing a pointer of an atomic type"
                 : code == N_RETURN ? "returning a pointer of an atomic type"
                                    : "assignment of pointer of an atomic type");
          error (c2m_ctx, POS (assign_node), "%s", msg);
        } else if (!type_qual_subset_p (&right->u.ptr_type->type_qual, &left->u.ptr_type->type_qual)) {
          msg = (code == N_CALL     ? "discarding type qualifiers in passing argument"
                 : code == N_RETURN ? "return discards a type qualifier from a pointer"
                                    : "assignment discards a type qualifier from a pointer");
          (c2m_options->pedantic_p ? error : warning) (c2m_ctx, POS (assign_node), "%s", msg);
        }
      } else if (left->mode == TM_DICT) {
        /* allow assignment to dict field; runtime will handle coercion */
      } else if (left->mode == TM_SLICE && right != NULL && right->mode == TM_SLICE
                 && type_eq_p (left->u.ptr_type, right->u.ptr_type)) {
        /* slice-to-slice of the same element type: copies the header pointer */
      } else {
        msg = (code == N_CALL     ? "passing assign incompatible value"
               : code == N_RETURN ? "returning assign incompatible value"
                                  : "assignment of incompatible value");
        error (c2m_ctx, POS (assign_node), "%s", msg);
      }
    }

    static int anon_struct_union_type_member_p (node_t member) {
      decl_t decl = member->attr;

      return decl != NULL && decl->decl_spec.type->unnamed_anon_struct_union_member_type_p;
    }

    static node_t get_adjacent_member (node_t member, int next_p) {
      assert (member->code == N_MEMBER);
      while ((member = next_p ? NL_NEXT (member) : NL_PREV (member)) != NULL)
        if (member->code == N_MEMBER
            && (NL_EL (member->u.ops, 1)->code != N_IGNORE || anon_struct_union_type_member_p (member)))
          break;
      return member;
    }

    static int init_compatible_string_p (node_t n, struct type *el_type);

    static int update_init_object_path (c2m_ctx_t c2m_ctx, size_t mark, struct type *value_type,
                                        int list_p) {
      init_object_t init_object;
      struct type *el_type;
      node_t size_node;
      mir_llong size_val;
      struct expr *sexpr;

      for (;;) {
        for (;;) {
          if (mark == VARR_LENGTH (init_object_t, init_object_path)) return FALSE;
          init_object = VARR_LAST (init_object_t, init_object_path);
          if (init_object.container_type->mode == TM_ARR) {
            el_type = init_object.container_type->u.arr_type->el_type;
            size_node = init_object.container_type->u.arr_type->size;
            sexpr = size_node->attr;
            size_val = (size_node->code != N_IGNORE && sexpr->const_p && integer_type_p (sexpr->type)
                          ? sexpr->c.i_val
                          : -1);
            init_object.u.curr_index++;
            if (size_val < 0 || init_object.u.curr_index < size_val) break;
            VARR_POP (init_object_t, init_object_path);
          } else {
            assert (init_object.container_type->mode == TM_STRUCT
                    || init_object.container_type->mode == TM_UNION
                    || init_object.container_type->mode == TM_CLASS);
            if (init_object.u.curr_member == NULL) { /* finding the first named member */
              node_t declaration_list = NL_EL (init_object.container_type->u.tag_type->u.ops, 1);

              assert (declaration_list != NULL && declaration_list->code == N_LIST);
              for (init_object.u.curr_member = NL_HEAD (declaration_list->u.ops);
                   init_object.u.curr_member != NULL
                   && (init_object.u.curr_member->code != N_MEMBER
                       || (NL_EL (init_object.u.curr_member->u.ops, 1)->code == N_IGNORE
                           && !anon_struct_union_type_member_p (init_object.u.curr_member)));
                   init_object.u.curr_member = NL_NEXT (init_object.u.curr_member))
                ;
            } else if (init_object.container_type->mode == TM_UNION
                       && !init_object.field_designator_p) { /* no next union member: */
              init_object.u.curr_member = NULL;
            } else { /* finding the next named struct member: */
              init_object.u.curr_member = get_adjacent_member (init_object.u.curr_member, TRUE);
            }
            if (init_object.u.curr_member != NULL) {
              init_object.field_designator_p = FALSE;
              el_type = ((decl_t) init_object.u.curr_member->attr)->decl_spec.type;
              break;
            }
            VARR_POP (init_object_t, init_object_path);
          }
        }
        VARR_SET (init_object_t, init_object_path, VARR_LENGTH (init_object_t, init_object_path) - 1,
                  init_object);
        if (list_p || scalar_type_p (el_type) || void_type_p (el_type)) return TRUE;
        /* A string literal initializes a whole char/char16/char32 array element
           (like a brace-enclosed row), so stop descending at that array level
           instead of stepping into its first character.  Without this, a string
           row in a multi-dimensional char array would not advance the outer
           index, undercounting the array length (e.g. `char a[][6] = {"x","y"}`
           must have 2 rows). */
        if (el_type->mode == TM_ARR && value_type != NULL && value_type->pos_node != NULL
            && init_compatible_string_p (value_type->pos_node, el_type->u.arr_type->el_type))
          return TRUE;
        assert (el_type->mode == TM_ARR || el_type->mode == TM_STRUCT || el_type->mode == TM_UNION || el_type->mode == TM_CLASS);
        if (el_type->mode != TM_ARR && value_type != NULL
            && el_type->u.tag_type == value_type->u.tag_type)
          return TRUE;
        init_object.container_type = el_type;
        init_object.field_designator_p = FALSE;
        if (el_type->mode == TM_ARR) {
          init_object.u.curr_index = -1;
        } else {
          init_object.u.curr_member = NULL;
        }
        VARR_PUSH (init_object_t, init_object_path, init_object);
      }
    }

    static enum basic_type get_uint_basic_type (size_t size) {
      if (sizeof (mir_uint) == size) return TP_UINT;
      if (sizeof (mir_ulong) == size) return TP_ULONG;
      if (sizeof (mir_ullong) == size) return TP_ULLONG;
      if (sizeof (mir_ushort) == size) return TP_USHORT;
      return TP_UCHAR;
    }

    static int init_compatible_string_p (node_t n, struct type *el_type) {
      return ((n->code == N_STR && char_type_p (el_type))
              || (n->code == N_STR16 && el_type->mode == TM_BASIC
                  && el_type->u.basic_type == get_uint_basic_type (2))
              || (n->code == N_STR32 && el_type->mode == TM_BASIC
                  && el_type->u.basic_type == get_uint_basic_type (4)));
    }

    static int update_path_and_do (c2m_ctx_t c2m_ctx, int go_inside_p,
                                   void (*action) (c2m_ctx_t c2m_ctx, decl_t member_decl,
                                                   struct type **type_ptr, node_t initializer,
                                                   int const_only_p, int top_p),
                                   size_t mark, node_t value, int const_only_p, mir_llong *max_index,
                                   pos_t pos, const char *detail) {
      init_object_t init_object;
      mir_llong index;
      struct type *el_type;
      struct expr *value_expr = value->attr;

      if (!update_init_object_path (c2m_ctx, mark, value_expr == NULL ? NULL : value_expr->type,
                                    !go_inside_p || value->code == N_LIST
                                      || value->code == N_COMPOUND_LITERAL)) {
        error (c2m_ctx, pos, "excess elements in %s initializer", detail);
        return FALSE;
      }
      if (!go_inside_p) return TRUE;
      init_object = VARR_LAST (init_object_t, init_object_path);
      if (init_object.container_type->mode == TM_ARR) {
        el_type = init_object.container_type->u.arr_type->el_type;
        action (c2m_ctx, NULL,
                (init_compatible_string_p (value, el_type)
                   ? &init_object.container_type
                   : &init_object.container_type->u.arr_type->el_type),
                value, const_only_p, FALSE);
      } else if (init_object.container_type->mode == TM_STRUCT
                 || init_object.container_type->mode == TM_UNION
                 || init_object.container_type->mode == TM_CLASS) {
        action (c2m_ctx, (decl_t) init_object.u.curr_member->attr,
                &((decl_t) init_object.u.curr_member->attr)->decl_spec.type, value, const_only_p,
                FALSE);
      }
      if (max_index != NULL) {
        init_object = VARR_GET (init_object_t, init_object_path, mark);
        if (init_object.container_type->mode == TM_ARR
            && *max_index < (index = init_object.u.curr_index))
          *max_index = index;
      }
      return TRUE;
    }

	    static int check_const_addr_p (c2m_ctx_t c2m_ctx, node_t r, node_t *base, mir_llong *offset,
	                                   int *deref) {
	      check_ctx_t check_ctx = c2m_ctx->check_ctx;
	      struct expr *e = r->attr;
	      struct type *type;
	      node_t op1, op2, temp;
	      decl_t decl;
	      struct decl_spec *decl_spec;
	      mir_size_t size;

	      if (e == NULL || r->code == N_STRING) return FALSE;
	      if (e->const_p && integer_type_p (e->type)) {
        *base = NULL;
        *offset = (mir_size_t) e->c.u_val;
        *deref = 0;
        return TRUE;
      }
      switch (r->code) {
      case N_STR:
      case N_STR16:
      case N_STR32:
        /* A string literal denotes a static array; its address is always a
           compile-time constant, regardless of the scope it appears in.  (An
           earlier `curr_scope == top_scope` restriction here broke constant
           address initializers such as `char *p = "x" + 1;` and
           `static char *p = "x";`.) */
        *base = r;
        *offset = 0;
        *deref = 0;
        return TRUE;
      case N_LABEL_ADDR:
        *base = r;
        *offset = 0;
        *deref = 0;
        return TRUE;
      case N_ID:
        if (e->def_node == NULL)
          return FALSE;
        else if (e->def_node->code == N_FUNC_DEF
                 || (e->def_node->code == N_SPEC_DECL
                     && ((decl_t) e->def_node->attr)->decl_spec.type->mode == TM_FUNC)) {
          *base = e->def_node;
          *deref = 0;
        } else if (e->u.lvalue_node == NULL
                   || ((decl = e->u.lvalue_node->attr)->scope != top_scope
                       && decl->decl_spec.linkage != N_IGNORE)) {
          return FALSE;
        } else {
          *base = e->def_node;
          *deref = e->type->arr_type == NULL;
        }
        *offset = 0;
        return TRUE;
      case N_DEREF:
      case N_ADDR: {
        node_t op = NL_HEAD (r->u.ops);
        struct expr *op_e = op->attr;

        if (!check_const_addr_p (c2m_ctx, op, base, offset, deref)) return FALSE;
        if (r->code == N_ADDR
            && (op_e->type->mode == TM_ARR
                || (op_e->type->mode == TM_PTR && op_e->type->arr_type != NULL))) {
          if (*deref > 0) (*deref)--;
        } else if (op->code != N_ID
                   || (op_e->def_node->code != N_FUNC_DEF
                       && (op_e->def_node->code != N_SPEC_DECL
                           || ((decl_t) op_e->def_node->attr)->decl_spec.type->mode != TM_FUNC))) {
          r->code == N_DEREF ? (*deref)++ : (*deref)--;
        }
        return TRUE;
      }
      case N_FIELD:
      case N_DEREF_FIELD:
        if (!check_const_addr_p (c2m_ctx, NL_HEAD (r->u.ops), base, offset, deref)) return FALSE;
        if (*deref != (r->code == N_FIELD ? 1 : 0)) return FALSE;
        *deref = 1;
        e = r->attr;
        if (e->u.lvalue_node != NULL) {
          decl = e->u.lvalue_node->attr;
          *offset += decl->offset;
        }
        return TRUE;
      case N_IND:
        //printf("1.N_IND: r->type->mode=");
        //print_type(c2m_ctx, stdout, ((struct expr *) NL_HEAD (r->u.ops)->attr)->type);
        //printf("\n");
        if (((struct expr *) NL_HEAD (r->u.ops)->attr)->type->mode != TM_PTR) return FALSE;
        if (!check_const_addr_p (c2m_ctx, NL_HEAD (r->u.ops), base, offset, deref)) return FALSE;
        if (!(e = NL_EL (r->u.ops, 1)->attr)->const_p) return FALSE;
        type = ((struct expr *) r->attr)->type;
        size = type_size (c2m_ctx, type->arr_type != NULL ? type->arr_type : type);
        *deref = 1;
        *offset += e->c.i_val * size;
        return TRUE;
      case N_CONCAT:
        /* The String `+` extension overloads `string_literal + integer` to
           N_CONCAT.  In a constant-address context (a static/global
           initializer) that is ordinary C11 pointer arithmetic on the string
           literal, so evaluate it as an additive expression below. */
        /* falls through */
      case N_ADD:
      case N_SUB: {
        int add_p = r->code != N_SUB; /* N_ADD and N_CONCAT are additive */
        if ((op2 = NL_EL (r->u.ops, 1)) == NULL) return FALSE;
        op1 = NL_HEAD (r->u.ops);
        if (add_p && (e = op1->attr)->const_p) SWAP (op1, op2, temp);
        if (!check_const_addr_p (c2m_ctx, op1, base, offset, deref)) return FALSE;
        if (*deref != 0 && ((struct expr *) op1->attr)->type->arr_type == NULL) return FALSE;
        if (!(e = op2->attr)->const_p
            && (!e->const_addr_p || e->def_node == NULL || e->def_node->code != N_LABEL_ADDR))
          return FALSE;
        type = ((struct expr *) r->attr)->type;
        assert (type->mode == TM_BASIC || type->mode == TM_PTR);
        size = (type->mode == TM_BASIC || type->u.ptr_type->mode == TM_FUNC
                  ? 1
                  : type_size (c2m_ctx, type->u.ptr_type->arr_type != NULL ? type->u.ptr_type->arr_type
                                                                           : type->u.ptr_type));
        if (add_p)
          *offset += e->c.i_val * size;
        else
          *offset -= e->c.i_val * size;
        return TRUE;
      }
      case N_CAST:
        decl_spec = NL_HEAD (r->u.ops)->attr;
        if (type_size (c2m_ctx, decl_spec->type) != sizeof (mir_size_t)) return FALSE;
        return check_const_addr_p (c2m_ctx, NL_EL (r->u.ops, 1), base, offset, deref);
      default: return FALSE;
      }
    }

    static void setup_const_addr_p (c2m_ctx_t c2m_ctx, node_t r) {
      node_t base;
      mir_llong offset;
      int deref;
      struct expr *e;

      if (!check_const_addr_p (c2m_ctx, r, &base, &offset, &deref) || deref != 0) return;
      e = r->attr;
      e->const_addr_p = TRUE;
      e->def_node = base;
      e->c.i_val = offset;
    }

    static void process_init_field_designator (c2m_ctx_t c2m_ctx, node_t designator_member,
                                               struct type *container_type) {
      decl_t decl;
      init_object_t init_object;
      node_t curr_member;

      assert (designator_member->code == N_MEMBER);
      /* We can have *partial* path of containing anon members: pop them */
      while (VARR_LENGTH (init_object_t, init_object_path) != 0) {
        init_object = VARR_LAST (init_object_t, init_object_path);
        if (!init_object.field_designator_p || (decl = init_object.u.curr_member->attr) == NULL
            || !decl->decl_spec.type->unnamed_anon_struct_union_member_type_p) {
          break;
        }
        container_type = init_object.container_type;
        VARR_POP (init_object_t, init_object_path);
      }
      /* Now add *full* path to designator_member of containing anon members */
      assert (VARR_LENGTH (node_t, containing_anon_members) == 0);
      decl = designator_member->attr;
      for (curr_member = decl->containing_unnamed_anon_struct_union_member; curr_member != NULL;
           curr_member = decl->containing_unnamed_anon_struct_union_member) {
        decl = curr_member->attr;
        VARR_PUSH (node_t, containing_anon_members, curr_member);
      }
      while (VARR_LENGTH (node_t, containing_anon_members) != 0) {
        init_object.u.curr_member = VARR_POP (node_t, containing_anon_members);
        init_object.container_type = container_type;
        init_object.field_designator_p = FALSE;
        VARR_PUSH (init_object_t, init_object_path, init_object);
        container_type = (decl = init_object.u.curr_member->attr)->decl_spec.type;
      }
      init_object.u.curr_member = get_adjacent_member (designator_member, FALSE);
      init_object.container_type = container_type;
      init_object.field_designator_p = TRUE;
      VARR_PUSH (init_object_t, init_object_path, init_object);
    }

    static node_t get_compound_literal (node_t n, int *addr_p) {
      for (int addr = 0; n != NULL; n = NL_HEAD (n->u.ops)) {
        switch (n->code) {
        case N_ADDR: addr++; break;
        case N_DEREF: addr--; break;
        case N_CAST: break;  // ???
        case N_STR:
        case N_STR16:
        case N_STR32:
        case N_COMPOUND_LITERAL:
          if (addr < 0) return NULL;
          *addr_p = addr > 0;
          return n;
          break;
        default: return NULL;
        }
        if (addr != -1 && addr != 0 && addr != 1) return NULL;
      }
      return NULL;
    }

    static mir_llong get_arr_type_size (struct type *arr_type) {
      node_t size_node;
      struct expr *sexpr;

      assert (arr_type->mode == TM_ARR);
      size_node = arr_type->u.arr_type->size;
      sexpr = size_node->attr;
      return (size_node->code != N_IGNORE && sexpr->const_p && integer_type_p (sexpr->type)
                ? sexpr->c.i_val
                : -1);
    }

/* ==========================================================================
   Sequence lambda methods:
     seq.filter(pred)        -> slice of seq's element type
     seq.map(fn)             -> slice of fn's return type
     seq.reduce(init, fn)    -> type of init
     seq.count()             -> size_t
   where seq is a C array, a slice (the result of a previous filter/map), or
   a class instance implementing the same Count()/Get(int) iteration protocol
   that for-in uses (so List<T> works out of the box).

   A slice is a pointer to a stack (alloca) block: a 16-byte header holding
   the i64 element count, followed by the packed elements.  Slices are scalar
   values at MIR level, bound with `auto`, indexable, for-in iterable, and
   chainable.  They live until the enclosing function returns and therefore
   cannot be returned from a function.
   ========================================================================== */

#define SLICE_HDR_SIZE 16

static struct type *adjust_type (c2m_ctx_t c2m_ctx, struct type *type);
static struct expr *create_expr (c2m_ctx_t c2m_ctx, node_t r);

enum seq_method { SEQM_NONE = 0, SEQM_FILTER, SEQM_MAP, SEQM_REDUCE, SEQM_COUNT, SEQM_TOLIST };

static enum seq_method get_seq_method (const char *name, int *nargs) {
  static const struct {
    const char *name;
    enum seq_method sm;
    int nargs;
  } tab[] = {
    {"filter", SEQM_FILTER, 1},
    {"map", SEQM_MAP, 1},
    {"reduce", SEQM_REDUCE, 2},
    {"count", SEQM_COUNT, 0},
    {"ToList", SEQM_TOLIST, 0},
  };
  for (size_t i = 0; i < sizeof (tab) / sizeof (tab[0]); i++)
    if (strcmp (name, tab[i].name) == 0) {
      if (nargs != NULL) *nargs = tab[i].nargs;
      return tab[i].sm;
    }
  return SEQM_NONE;
}

enum seq_recv_kind { SEQ_RECV_NONE = 0, SEQ_RECV_ARR, SEQ_RECV_SLICE, SEQ_RECV_CLASS };

struct seq_recv {
  enum seq_recv_kind kind;
  struct type *el_type;      /* element type of the sequence */
  mir_llong static_len;      /* SEQ_RECV_ARR: element count (-1 if not constant) */
  struct type *cls_type;     /* SEQ_RECV_CLASS: the class type */
  node_t count_def, get_def; /* SEQ_RECV_CLASS: Count()/Get(int) FUNC_DEFs */
};

/* Classify a checked receiver type T as a lambda-method sequence; fill *SR.
   Returns SEQ_RECV_NONE if T is not a sequence (no errors are reported). */
static enum seq_recv_kind classify_seq_receiver (c2m_ctx_t c2m_ctx, struct type *t, pos_t pos,
                                                 struct seq_recv *sr) {
  memset (sr, 0, sizeof (*sr));
  sr->static_len = -1;
  if (t == NULL) return SEQ_RECV_NONE;
  if (t->mode == TM_ARR || (t->mode == TM_PTR && t->arr_type != NULL)) {
    struct type *arr = t->mode == TM_ARR ? t : t->arr_type;
    sr->kind = SEQ_RECV_ARR;
    sr->el_type = arr->u.arr_type->el_type;
    sr->static_len = get_arr_type_size (arr);
    return sr->kind;
  }
  if (t->mode == TM_SLICE) {
    sr->kind = SEQ_RECV_SLICE;
    sr->el_type = t->u.ptr_type;
    return sr->kind;
  }
  if (t->mode == TM_CLASS
      || (t->mode == TM_PTR && t->u.ptr_type != NULL && t->u.ptr_type->mode == TM_CLASS)) {
    struct type *cls = t->mode == TM_PTR ? t->u.ptr_type : t;
    node_t tag = cls->u.tag_type;
    node_t count_def = find_class_protocol_method (c2m_ctx, tag, "Count", 0, pos);
    node_t get_def = find_class_protocol_method (c2m_ctx, tag, "Get", 1, pos);
    decl_t gd;

    if (count_def == NULL || get_def == NULL) return SEQ_RECV_NONE;
    gd = get_def->attr;
    if (gd == NULL || gd->decl_spec.type == NULL || gd->decl_spec.type->mode != TM_FUNC)
      return SEQ_RECV_NONE;
    sr->kind = SEQ_RECV_CLASS;
    sr->cls_type = cls;
    sr->count_def = count_def;
    sr->get_def = get_def;
    sr->el_type = gd->decl_spec.type->u.func_type->ret_type;
    return sr->kind;
  }
  return SEQ_RECV_NONE;
}

static void add_type_spec (c2m_ctx_t c2m_ctx, node_t specs, node_code_t code, pos_t pos) {
  op_append (c2m_ctx, specs, new_pos_node (c2m_ctx, code, pos));
}

/* Synthesize declaration-specifier AST nodes (appended to SPECS) and pointer
   declarator nodes (appended to DECL_OPS) expressing the semantic type T, so
   an inferred lambda parameter can flow through the normal AST type checks.
   Returns FALSE for types that cannot be expressed (slices, functions, ...). */
static int build_type_spec_nodes (c2m_ctx_t c2m_ctx, struct type *t, pos_t pos, node_t specs,
                                  node_t decl_ops) {
  switch (t->mode) {
  case TM_PTR:
    if (!build_type_spec_nodes (c2m_ctx, t->u.ptr_type, pos, specs, decl_ops)) return FALSE;
    op_append (c2m_ctx, decl_ops,
               new_pos_node1 (c2m_ctx, N_POINTER, pos, new_node (c2m_ctx, N_LIST)));
    return TRUE;
  case TM_BASIC:
    switch (t->u.basic_type) {
    case TP_VOID: add_type_spec (c2m_ctx, specs, N_VOID, pos); return TRUE;
    case TP_BOOL: add_type_spec (c2m_ctx, specs, N_BOOL, pos); return TRUE;
    case TP_CHAR: add_type_spec (c2m_ctx, specs, N_CHAR, pos); return TRUE;
    case TP_SCHAR:
      add_type_spec (c2m_ctx, specs, N_SIGNED, pos);
      add_type_spec (c2m_ctx, specs, N_CHAR, pos);
      return TRUE;
    case TP_UCHAR:
      add_type_spec (c2m_ctx, specs, N_UNSIGNED, pos);
      add_type_spec (c2m_ctx, specs, N_CHAR, pos);
      return TRUE;
    case TP_SHORT: add_type_spec (c2m_ctx, specs, N_SHORT, pos); return TRUE;
    case TP_USHORT:
      add_type_spec (c2m_ctx, specs, N_UNSIGNED, pos);
      add_type_spec (c2m_ctx, specs, N_SHORT, pos);
      return TRUE;
    case TP_INT: add_type_spec (c2m_ctx, specs, N_INT, pos); return TRUE;
    case TP_UINT:
      add_type_spec (c2m_ctx, specs, N_UNSIGNED, pos);
      add_type_spec (c2m_ctx, specs, N_INT, pos);
      return TRUE;
    case TP_LONG: add_type_spec (c2m_ctx, specs, N_LONG, pos); return TRUE;
    case TP_ULONG:
      add_type_spec (c2m_ctx, specs, N_UNSIGNED, pos);
      add_type_spec (c2m_ctx, specs, N_LONG, pos);
      return TRUE;
    case TP_LLONG:
      add_type_spec (c2m_ctx, specs, N_LONG, pos);
      add_type_spec (c2m_ctx, specs, N_LONG, pos);
      return TRUE;
    case TP_ULLONG:
      add_type_spec (c2m_ctx, specs, N_UNSIGNED, pos);
      add_type_spec (c2m_ctx, specs, N_LONG, pos);
      add_type_spec (c2m_ctx, specs, N_LONG, pos);
      return TRUE;
    case TP_FLOAT: add_type_spec (c2m_ctx, specs, N_FLOAT, pos); return TRUE;
    case TP_DOUBLE: add_type_spec (c2m_ctx, specs, N_DOUBLE, pos); return TRUE;
    case TP_LDOUBLE:
      add_type_spec (c2m_ctx, specs, N_LONG, pos);
      add_type_spec (c2m_ctx, specs, N_DOUBLE, pos);
      return TRUE;
    case TP_STRING: add_type_spec (c2m_ctx, specs, N_STRING, pos); return TRUE;
    default: return FALSE;
    }
  case TM_ENUM:
  case TM_STRUCT:
  case TM_UNION:
  case TM_CLASS: {
    node_t tag_id = NL_HEAD (t->u.tag_type->u.ops);
    node_code_t code = t->mode == TM_ENUM     ? N_ENUM
                       : t->mode == TM_STRUCT ? N_STRUCT
                       : t->mode == TM_UNION  ? N_UNION
                                              : N_CLASS;
    if (tag_id == NULL || tag_id->code != N_ID) return FALSE; /* unnamed tag */
    op_append (c2m_ctx, specs,
               new_pos_node2 (c2m_ctx, code, pos, copy_node (c2m_ctx, tag_id),
                              new_node (c2m_ctx, N_IGNORE)));
    return TRUE;
  }
  case TM_DICT: add_type_spec (c2m_ctx, specs, N_DICT, pos); return TRUE;
  default: return FALSE;
  }
}

/* Check a synthetic lambda FUNC_DEF in the middle of checking another
   function.  All per-function check state is saved and restored so the
   enclosing function's check continues undisturbed.  The lambda is checked
   at top scope: there are no closures, so enclosing locals are not visible. */
static void check_lambda_func_def (c2m_ctx_t c2m_ctx, node_t func_def) {
  check_ctx_t check_ctx = c2m_ctx->check_ctx;
  node_t saved_scope = curr_scope, saved_func_block_scope = func_block_scope;
  node_t saved_func_def = curr_func_def, saved_class = curr_class;
  node_t saved_switch = curr_switch, saved_loop = curr_loop;
  node_t saved_loop_switch = curr_loop_switch;
  node_t saved_unnamed = curr_unnamed_anon_struct_union_member;
  node_t saved_lambda_def = curr_lambda_def;
  unsigned saved_scope_num = curr_func_scope_num;
  unsigned char saved_in_params_p = in_params_p, saved_jump_ret_p = jump_ret_p;
  mir_size_t saved_arg_area = curr_call_arg_area_offset;
  size_t i, fda_len = VARR_LENGTH (decl_t, func_decls_for_allocation);
  size_t lu_len = VARR_LENGTH (node_t, label_uses);
  decl_t *fda_save = NULL;
  node_t *lu_save = NULL;

  /* The FUNC_DEF check truncates and consumes these per-function VARRs;
     preserve the enclosing function's entries. */
  if (fda_len != 0) {
    fda_save = reg_malloc (c2m_ctx, fda_len * sizeof (decl_t));
    memcpy (fda_save, VARR_ADDR (decl_t, func_decls_for_allocation), fda_len * sizeof (decl_t));
  }
  if (lu_len != 0) {
    lu_save = reg_malloc (c2m_ctx, lu_len * sizeof (node_t));
    memcpy (lu_save, VARR_ADDR (node_t, label_uses), lu_len * sizeof (node_t));
  }
  VARR_TRUNC (node_t, label_uses, 0);
  curr_scope = top_scope;
  curr_class = NULL;
  curr_lambda_def = func_def;
  check (c2m_ctx, func_def, NULL);
  curr_lambda_def = saved_lambda_def;
  VARR_TRUNC (decl_t, func_decls_for_allocation, 0);
  for (i = 0; i < fda_len; i++) VARR_PUSH (decl_t, func_decls_for_allocation, fda_save[i]);
  VARR_TRUNC (node_t, label_uses, 0);
  for (i = 0; i < lu_len; i++) VARR_PUSH (node_t, label_uses, lu_save[i]);
  /* fda_save/lu_save are arena (reg_malloc) memory: reclaimed at compile end */
  curr_scope = saved_scope;
  func_block_scope = saved_func_block_scope;
  curr_func_def = saved_func_def;
  curr_class = saved_class;
  curr_switch = saved_switch;
  curr_loop = saved_loop;
  curr_loop_switch = saved_loop_switch;
  curr_unnamed_anon_struct_union_member = saved_unnamed;
  curr_func_scope_num = saved_scope_num;
  in_params_p = saved_in_params_p;
  jump_ret_p = saved_jump_ret_p;
  curr_call_arg_area_offset = saved_arg_area;
}

/* Ensure the per-class defer-cleanup thunk exists as a checked,
   module-injected top-level function (see synthesize_defer_thunk_items,
   defined near synthesize_any_thunks, for the actual source-text
   synthesis). Split across the file because synthesis needs the parser's
   C()/TRY() macros, which are #undef'd before this point, while injection
   needs module_item_list/curr_module_item/check_lambda_func_def, none of
   which are declared yet back where synthesis has to live. Returns the
   interned thunk name. */
static const char *ensure_defer_thunk (c2m_ctx_t c2m_ctx,
                                       const char *concrete_name, pos_t pos) {
  check_ctx_t check_ctx = c2m_ctx->check_ctx;
  const char *name;
  node_t items = synthesize_defer_thunk_items (c2m_ctx, concrete_name, pos, &name);

  /* Inject + check right now, just like synthesize_any_thunks's caller does
     for the Any<I> factory, so `name` resolves for any find_def lookup
     that follows. NULL items means the thunk was already registered (by an
     earlier call or an any<I>(C*) erasure of the same class) or the class
     name wasn't synthesizable -- nothing to inject, `name` is still valid
     (gen tolerates the decl being absent: no shadow push is emitted).

     Insertion point: before curr_module_item during the check pass (so gen
     emits the thunk before the function that references it). The ownership
     pass's `owned` hook calls us AFTER check (module_item_list has been
     restored to NULL) but BEFORE gen_mir/top_gen ever walks module_items_root
     (the same list, stashed at N_MODULE check entry) -- ownership_run always
     completes before gen_mir starts, per c2mir_compile's pipeline. So prepend
     rather than append: gen still walks the list front-to-back, and putting
     every ownership-synthesized thunk at the front makes it generate (a real
     MIR_func_item, not just a MIR_new_forward stand-in) before ANY function
     that might reference it, however early. This matters beyond tidiness --
     MIR's binary (.bmir) writer serializes items in this same order, and its
     reader resolves a name reference against whatever's already in the
     module's item table *at that point in the stream*; a forward item
     created only when the first reference is gen'd (mid-body, well after
     that referencing function's own item already exists earlier in the
     list) serializes AFTER its own first use, which round-trips fine for
     JIT (MIR_link works off the live in-memory graph, order-independent)
     but made b2obj's AOT read fail with "not found item __thunk_dtor_X".
     Prepending removes the ordering hazard instead of relying on
     MIR_finish_module's in-memory-only forward/real unification to paper
     over stream order that a subsequent write+read doesn't preserve. */
  {
    node_t root = module_item_list != NULL ? module_item_list : module_items_root;
    if (items != NULL && root != NULL) {
      node_t arr[8];
      int n = 0;
      for (node_t it = NL_HEAD (items->u.ops); it != NULL && n < 8; it = NL_NEXT (it)) arr[n++] = it;
      if (curr_module_item != NULL) {
        for (int k = 0; k < n; k++) {
          NL_REMOVE (items->u.ops, arr[k]);
          DLIST_INSERT_BEFORE (node_t, root->u.ops, curr_module_item, arr[k]);
          check_lambda_func_def (c2m_ctx, arr[k]);
        }
      } else {
        /* Prepend in order: inserting each at the head one at a time would
           reverse them, so insert-before-current-head as we go instead. */
        for (int k = n; k-- > 0;) {
          NL_REMOVE (items->u.ops, arr[k]);
          node_t head = DLIST_HEAD (node_t, root->u.ops);
          if (head != NULL)
            DLIST_INSERT_BEFORE (node_t, root->u.ops, head, arr[k]);
          else
            DLIST_APPEND (node_t, root->u.ops, arr[k]);
          check_lambda_func_def (c2m_ctx, arr[k]);
        }
      }
    }
  }
  return name;
}

/* Instantiate an untyped lambda  (params) => body  with concrete parameter
   types inferred at the call site.  Builds a synthetic static FUNC_DEF,
   inserts it into the module item list before the item being checked (so its
   MIR function is generated before its caller needs the func item ref), and
   checks it at top scope.  Returns the FUNC_DEF node or NULL on error. */
static node_t instantiate_lambda (c2m_ctx_t c2m_ctx, node_t lam, struct type **ptypes,
                                  int n_ptypes) {
  check_ctx_t check_ctx = c2m_ctx->check_ctx;
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  node_t params = NL_HEAD (lam->u.ops), body = NL_NEXT (params);
  pos_t pos = POS (lam);
  node_t plist, func_def, insert_before;
  char lname[64];
  int i;

  if ((insert_before = curr_lambda_def != NULL ? curr_lambda_def : curr_module_item) == NULL
      || module_item_list == NULL) {
    error (c2m_ctx, pos, "lambda methods are only supported inside a function body");
    return NULL;
  }
  if ((int) NL_LENGTH (params->u.ops) != n_ptypes) {
    error (c2m_ctx, pos, "lambda takes %u parameter%s but %d %s expected here",
           (unsigned) NL_LENGTH (params->u.ops), NL_LENGTH (params->u.ops) == 1 ? "" : "s",
           n_ptypes, n_ptypes == 1 ? "is" : "are");
    return NULL;
  }
  plist = new_node (c2m_ctx, N_LIST);
  i = 0;
  for (node_t pid = NL_HEAD (params->u.ops); pid != NULL; pid = NL_NEXT (pid), i++) {
    node_t pspecs = new_node (c2m_ctx, N_LIST);
    node_t pdecl_ops = new_node (c2m_ctx, N_LIST);

    if (!build_type_spec_nodes (c2m_ctx, ptypes[i], pos, pspecs, pdecl_ops)) {
      error (c2m_ctx, pos, "cannot express the inferred type of lambda parameter '%s'",
             pid->u.s.s);
      return NULL;
    }
    op_append (c2m_ctx, plist,
               build_spec_decl (c2m_ctx, pos, pspecs,
                                new_pos_node2 (c2m_ctx, N_DECL, pos, copy_node (c2m_ctx, pid),
                                               pdecl_ops),
                                NULL, NULL, NULL));
  }
  snprintf (lname, sizeof (lname), "__lambda_%u", lambda_uid++);
  func_def = build_lambda_func_def (c2m_ctx, lname, plist, body, pos);
  /* Generation order: the lambda must precede its caller in the module list.
     For a lambda inside another lambda's body, insert before the enclosing
     lambda's FUNC_DEF (which is already in the list before the caller). */
  DLIST_INSERT_BEFORE (node_t, module_item_list->u.ops, insert_before, func_def);
  check_lambda_func_def (c2m_ctx, func_def);
  if (func_def->attr == NULL || ((decl_t) func_def->attr)->decl_spec.type == NULL
      || ((decl_t) func_def->attr)->decl_spec.type->mode != TM_FUNC)
    return NULL; /* body check failed; errors already reported */
  return func_def;
}

/* ==========================================================================
   Capturing lambdas — Strategy A (open-code / desugar at HOF call sites).
   See LAMBDA-CAPTURE.md.  Capturing lambda literal args to List/Map/Set HOFs
   are rewritten into for-in loops in the caller's frame so free locals stay
   normal names.  Non-capturing lambdas still lower to thin static functions.
   ========================================================================== */

enum hof_kind {
  HOF_NONE = 0,
  HOF_WHERE,
  HOF_FILTER,
  HOF_MAP,
  HOF_FOREACH,
  HOF_ANY,
  HOF_ALL,
  HOF_FIND,
  HOF_SORT,
  HOF_SELECT,
  HOF_COUNTWHERE
};

static enum hof_kind get_hof_kind (const char *name) {
  if (name == NULL) return HOF_NONE;
  if (strcmp (name, "Where") == 0) return HOF_WHERE;
  if (strcmp (name, "Filter") == 0) return HOF_FILTER;
  if (strcmp (name, "Map") == 0) return HOF_MAP;
  if (strcmp (name, "ForEach") == 0) return HOF_FOREACH;
  if (strcmp (name, "Any") == 0) return HOF_ANY;
  if (strcmp (name, "All") == 0) return HOF_ALL;
  if (strcmp (name, "Find") == 0) return HOF_FIND;
  if (strcmp (name, "Sort") == 0) return HOF_SORT;
  if (strcmp (name, "Select") == 0) return HOF_SELECT;
  if (strcmp (name, "CountWhere") == 0) return HOF_COUNTWHERE;
  return HOF_NONE;
}

/* True if params is a typed parameter list (N_SPEC_DECL/N_TYPE) or empty;
   false for untyped identifier lists used by shorthand lambdas. */
static int lambda_typed_p (node_t params) {
  node_t p;
  if (params == NULL || params->code != N_LIST) return FALSE;
  for (p = NL_HEAD (params->u.ops); p != NULL; p = NL_NEXT (p))
    if (p->code == N_ID) return FALSE;
  return TRUE;
}

/* Parameter name of an N_LAMBDA param: N_ID (untyped) or N_SPEC_DECL (typed). */
static const char *lambda_param_name (node_t p) {
  node_t decl, id;
  if (p == NULL) return NULL;
  if (p->code == N_ID) return p->u.s.s;
  if (p->code == N_SPEC_DECL) {
    decl = NL_EL (p->u.ops, 1);
    if (decl != NULL && decl->code == N_DECL) {
      id = NL_HEAD (decl->u.ops);
      if (id != NULL && id->code == N_ID) return id->u.s.s;
    }
  }
  return NULL;
}

static int name_in_varr (VARR (cstr_t) * names, const char *s) {
  size_t i;
  if (s == NULL) return FALSE;
  for (i = 0; i < VARR_LENGTH (cstr_t, names); i++)
    if (strcmp (VARR_GET (cstr_t, names, i), s) == 0) return TRUE;
  return FALSE;
}

/* True if def is an automatic binding in an enclosing function/method frame
   (local, parameter, or for/for-in binding) — not a global/static/function. */
static int def_is_auto_capture_p (c2m_ctx_t c2m_ctx, node_t def) {
  check_ctx_t check_ctx = c2m_ctx->check_ctx;
  decl_t d;
  node_t scope;

  if (def == NULL) return FALSE;
  if (def->code == N_FUNC_DEF || def->code == N_FUNC) return FALSE;
  if (def->code == N_ENUM_CONST || def->code == N_CLASS || def->code == N_INTERFACE)
    return FALSE;
  if (def->code == N_MEMBER) return FALSE; /* bare member access is not a free local */
  if (def->code != N_SPEC_DECL) return FALSE;
  d = def->attr;
  if (d == NULL) return FALSE;
  if (d->decl_spec.static_p || d->decl_spec.extern_p || d->decl_spec.typedef_p
      || d->decl_spec.thread_local_p)
    return FALSE;
  scope = d->scope;
  if (scope == NULL) return FALSE;
  /* File-scope objects are not captures. */
  if (scope == top_scope || scope->code == N_MODULE) return FALSE;
  return TRUE;
}

/* Recursively collect free automatic names used by NODE into FREE, ignoring
   names in BOUND.  Declarations push into BOUND for nested scopes (trunc on exit). */
static void lambda_free_vars_walk (c2m_ctx_t c2m_ctx, node_t n, VARR (cstr_t) * bound,
                                   VARR (cstr_t) * free_names) {
  check_ctx_t check_ctx = c2m_ctx->check_ctx;
  size_t bound_mark;
  node_t c;

  if (n == NULL) return;
  switch (n->code) {
  case N_IGNORE: case N_I: case N_L: case N_LL: case N_U: case N_UL: case N_ULL:
  case N_F: case N_D: case N_LD: case N_CH: case N_CH16: case N_CH32:
  case N_STR: case N_STR16: case N_STR32: case N_STRING:
    return;
  case N_ID: {
    const char *s = n->u.s.s;
    node_t def;
    if (s == NULL) return;
    if (name_in_varr (bound, s)) return;
    if (name_in_varr (free_names, s)) return;
    /* this is the method receiver — treat as a free auto-like binding. */
    if (strcmp (s, "this") == 0) {
      VARR_PUSH (cstr_t, free_names, s);
      return;
    }
    def = find_def (c2m_ctx, S_REGULARS, n, curr_scope, NULL);
    if (def_is_auto_capture_p (c2m_ctx, def))
      VARR_PUSH (cstr_t, free_names, s);
    return;
  }
  case N_SPEC_DECL: {
    node_t decl = NL_EL (n->u.ops, 1);
    node_t init = NL_EL (n->u.ops, 4);
    /* Walk initializer first (names not yet in scope), then bind the id. */
    if (init != NULL && init->code != N_IGNORE)
      lambda_free_vars_walk (c2m_ctx, init, bound, free_names);
    if (decl != NULL && decl->code == N_DECL) {
      node_t id = NL_HEAD (decl->u.ops);
      if (id != NULL && id->code == N_ID && id->u.s.s != NULL
          && !name_in_varr (bound, id->u.s.s))
        VARR_PUSH (cstr_t, bound, id->u.s.s);
    }
    return;
  }
  case N_BLOCK:
  case N_FOR:
  case N_FORIN:
  case N_SWITCH:
  case N_WHILE:
  case N_DO:
  case N_IF: {
    bound_mark = VARR_LENGTH (cstr_t, bound);
    /* FORIN binds its loop vars before body/collection uses. */
    if (n->code == N_FORIN) {
      node_t key = NL_EL (n->u.ops, 1);
      node_t val = NL_EL (n->u.ops, 2);
      if (key != NULL && key->code == N_ID && key->u.s.s != NULL
          && !name_in_varr (bound, key->u.s.s))
        VARR_PUSH (cstr_t, bound, key->u.s.s);
      if (val != NULL && val->code == N_ID && val->u.s.s != NULL
          && !name_in_varr (bound, val->u.s.s))
        VARR_PUSH (cstr_t, bound, val->u.s.s);
    }
    for (c = NL_HEAD (n->u.ops); c != NULL; c = NL_NEXT (c))
      lambda_free_vars_walk (c2m_ctx, c, bound, free_names);
    VARR_TRUNC (cstr_t, bound, bound_mark);
    return;
  }
  default:
    if (generic_node_has_scalar_data (n->code)) return;
    for (c = NL_HEAD (n->u.ops); c != NULL; c = NL_NEXT (c))
      lambda_free_vars_walk (c2m_ctx, c, bound, free_names);
    return;
  }
}

/* Collect free automatic names of LAM into FREE_NAMES (caller-owned VARR).
   Parameters and locals declared in the body are bound. */
static void lambda_collect_free_vars (c2m_ctx_t c2m_ctx, node_t lam,
                                      VARR (cstr_t) * free_names) {
  node_t params, body, p;
  VARR (cstr_t) * bound;

  if (lam == NULL || lam->code != N_LAMBDA) return;
  params = NL_HEAD (lam->u.ops);
  body = NL_NEXT (params);
  {
    MIR_alloc_t alloc = c2m_alloc (c2m_ctx);
    VARR_CREATE (cstr_t, bound, alloc, 8);
  }
  if (params != NULL && params->code == N_LIST)
    for (p = NL_HEAD (params->u.ops); p != NULL; p = NL_NEXT (p)) {
      const char *pn = lambda_param_name (p);
      if (pn != NULL && !name_in_varr (bound, pn)) VARR_PUSH (cstr_t, bound, pn);
    }
  lambda_free_vars_walk (c2m_ctx, body, bound, free_names);
  VARR_DESTROY (cstr_t, bound);
}

/* Instantiate a typed N_LAMBDA (params already N_SPEC_DECL list) as a static
   function, or reuse a prior instantiation stashed on lam->attr. */
static node_t instantiate_typed_lambda (c2m_ctx_t c2m_ctx, node_t lam) {
  check_ctx_t check_ctx = c2m_ctx->check_ctx;
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  node_t params, body, func_def, insert_before;
  char lname[64];
  struct expr *le;

  if (lam->attr != NULL && (le = lam->attr)->def_node != NULL
      && le->def_node->code == N_FUNC_DEF)
    return le->def_node;

  params = NL_HEAD (lam->u.ops);
  body = NL_NEXT (params);
  if ((insert_before = curr_lambda_def != NULL ? curr_lambda_def : curr_module_item) == NULL
      || module_item_list == NULL) {
    error (c2m_ctx, POS (lam), "lambda is only supported inside a function body");
    return NULL;
  }
  snprintf (lname, sizeof (lname), "__lambda_%u", lambda_uid++);
  /* Deep-copy body/params so a second check pass does not reparent the only copy. */
  func_def = build_lambda_func_def (c2m_ctx, lname, parse_copy_expr (c2m_ctx, params),
                                    parse_copy_expr (c2m_ctx, body), POS (lam));
  DLIST_INSERT_BEFORE (node_t, module_item_list->u.ops, insert_before, func_def);
  check_lambda_func_def (c2m_ctx, func_def);
  if (func_def->attr == NULL || ((decl_t) func_def->attr)->decl_spec.type == NULL
      || ((decl_t) func_def->attr)->decl_spec.type->mode != TM_FUNC)
    return NULL;
  {
    decl_t ld = func_def->attr;
    le = create_expr (c2m_ctx, lam);
    le->type->mode = TM_PTR;
    le->type->u.ptr_type = ld->decl_spec.type;
    set_type_layout (c2m_ctx, le->type);
    le->def_node = func_def;
    le->u.lvalue_node = NULL;
  }
  return func_def;
}

/* If body is a single-return block `{ return e; }`, return e; else NULL. */
static node_t lambda_single_return_expr (node_t body) {
  node_t stmts, only, expr;
  if (body == NULL || body->code != N_BLOCK) return NULL;
  stmts = NL_EL (body->u.ops, 1);
  if (stmts == NULL || stmts->code != N_LIST) return NULL;
  only = NL_HEAD (stmts->u.ops);
  if (only == NULL || NL_NEXT (only) != NULL || only->code != N_RETURN) return NULL;
  expr = NL_EL (only->u.ops, 1);
  if (expr == NULL || expr->code == N_IGNORE) return NULL;
  return expr;
}

/* Build `auto name = init;` SPEC_DECL. */
static node_t build_auto_init_decl (c2m_ctx_t c2m_ctx, pos_t pos, const char *name,
                                    node_t init) {
  node_t specs = new_node (c2m_ctx, N_LIST);
  op_append (c2m_ctx, specs, new_pos_node (c2m_ctx, N_AUTO, pos));
  return build_spec_decl (c2m_ctx, pos, specs,
                          build_decl (c2m_ctx, pos, build_id (c2m_ctx, name, pos), NULL),
                          NULL, NULL, init);
}

/* Build recv.method(args...) as N_CALL(N_FIELD(recv, method), arglist). */
static node_t build_dot_call (c2m_ctx_t c2m_ctx, pos_t pos, node_t recv, const char *method,
                              node_t arglist) {
  node_t field = new_pos_node2 (c2m_ctx, N_FIELD, pos, recv, build_id (c2m_ctx, method, pos));
  if (arglist == NULL) arglist = new_node (c2m_ctx, N_LIST);
  return new_pos_node2 (c2m_ctx, N_CALL, pos, field, arglist);
}

/* Classify a checked receiver type as List/Map/Set specialization. */
static int coll_class_kind (struct type *t, const char **kind_out, node_t *cid_out) {
  node_t cid;
  const char *nm;
  if (t == NULL) return FALSE;
  if (t->mode == TM_PTR && t->u.ptr_type != NULL) t = t->u.ptr_type;
  if (t->mode != TM_CLASS || t->u.tag_type == NULL) return FALSE;
  cid = TAG_ID (t->u.tag_type);
  if (cid == NULL || cid->code != N_ID || cid->u.s.s == NULL) return FALSE;
  nm = cid->u.s.s;
  if (strncmp (nm, "__generic_List_", 15) == 0) {
    if (kind_out) *kind_out = "List";
    if (cid_out) *cid_out = cid;
    return TRUE;
  }
  /* ListView uses the same Count/Get open-code protocol as List (non-alloc HOFs). */
  if (strncmp (nm, "__generic_ListView_", 19) == 0) {
    if (kind_out) *kind_out = "ListView";
    if (cid_out) *cid_out = cid;
    return TRUE;
  }
  if (strncmp (nm, "__generic_Map_", 14) == 0) {
    if (kind_out) *kind_out = "Map";
    if (cid_out) *cid_out = cid;
    return TRUE;
  }
  if (strncmp (nm, "__generic_Set_", 14) == 0) {
    if (kind_out) *kind_out = "Set";
    if (cid_out) *cid_out = cid;
    return TRUE;
  }
  return FALSE;
}

/* Deep-copy N, replacing free occurrences of identifier FROM with a fresh
   copy of TO (used to rewrite lambda params as recv.Get(i) in open-coded HOFs). */
static node_t lambda_subst_id (c2m_ctx_t c2m_ctx, node_t n, const char *from, node_t to) {
  node_t r, c;
  if (n == NULL) return NULL;
  if (n->code == N_ID && n->u.s.s != NULL && from != NULL && strcmp (n->u.s.s, from) == 0)
    return parse_copy_expr (c2m_ctx, to);
  if (generic_node_has_scalar_data (n->code) || n->code == N_ID || n->code == N_STRING
      || n->code == N_IGNORE)
    return copy_node (c2m_ctx, n);
  r = new_node (c2m_ctx, n->code);
  set_node_pos (c2m_ctx, r, POS (n));
  for (c = NL_HEAD (n->u.ops); c != NULL; c = NL_NEXT (c))
    op_append (c2m_ctx, r, lambda_subst_id (c2m_ctx, c, from, to));
  return r;
}

/* Build `recv.Count()` call AST. */
static node_t build_count_call (c2m_ctx_t c2m_ctx, pos_t pos, node_t recv) {
  return build_dot_call (c2m_ctx, pos, parse_copy_expr (c2m_ctx, recv), "Count",
                         new_node (c2m_ctx, N_LIST));
}

/* Build `recv.Get(index_expr)` call AST. */
static node_t build_get_call (c2m_ctx_t c2m_ctx, pos_t pos, node_t recv, node_t index_expr) {
  return build_dot_call (c2m_ctx, pos, parse_copy_expr (c2m_ctx, recv), "Get",
                         new_node1 (c2m_ctx, N_LIST, parse_copy_expr (c2m_ctx, index_expr)));
}

/* Defined later in this TU (gen side); used by the R2.2a substitution below. */
static int find_dense_buffer_fields (node_t class_tag, decl_t *arr_out, decl_t *len_out,
                                     decl_t *cap_out);

/* ── R2.2a: read-only lambda param proof for GetMut-deref substitution ──────
 * A capturing HOF lambda `(Ship s) => s.IsHot()` is open-coded with the param
 * substituted by `recv.Get(i)` — a call + block copy per element.  When the
 * param is only *read* we can instead substitute `*(recv.GetMut(i))`, a
 * pointer into the buffer: identical reads, zero copies.  These walks prove
 * the param is never written, addressed, moved, or passed to a mutating
 * method. */

/* Depth-1 scan of a method body for writes to `this` (name-based; the bodies
   are already checked, so helper calls on `this` are treated as unknown). */
static int hof_method_body_pure_p (c2m_ctx_t c2m_ctx, node_t n) {
  node_t c;
  if (n == NULL) return 1;
  switch (n->code) {
  case N_ASSIGN: case N_ADD_ASSIGN: case N_SUB_ASSIGN: case N_MUL_ASSIGN:
  case N_DIV_ASSIGN: case N_MOD_ASSIGN: case N_LSH_ASSIGN: case N_RSH_ASSIGN:
  case N_AND_ASSIGN: case N_OR_ASSIGN: case N_XOR_ASSIGN: {
    node_t lhs = NL_HEAD (n->u.ops);
    if (lhs != NULL && (lhs->code == N_FIELD || lhs->code == N_DEREF_FIELD
                        || lhs->code == N_DEREF)) {
      node_t base = NL_HEAD (lhs->u.ops);
      if (base != NULL && base->code == N_ID && base->u.s.s != NULL
          && strcmp (base->u.s.s, "this") == 0)
        return 0;
    }
    break;
  }
  case N_MOVE: case N_DELETE:
    return 0;
  case N_ADDR: {
    node_t op = NL_HEAD (n->u.ops);
    if (op != NULL && op->code == N_ID && op->u.s.s != NULL
        && strcmp (op->u.s.s, "this") == 0)
      return 0;
    break;
  }
  case N_CALL: {
    node_t fn = NL_HEAD (n->u.ops);
    if (fn != NULL && (fn->code == N_FIELD || fn->code == N_DEREF_FIELD)) {
      node_t base = NL_HEAD (fn->u.ops);
      struct expr *be = base != NULL ? base->attr : NULL;
      if (be != NULL && be->type != NULL && builtin_string_type_p (be->type)) break;
      if (base != NULL && base->code == N_ID && base->u.s.s != NULL
          && strcmp (base->u.s.s, "this") == 0)
        return 0; /* another method on this: unknown effect */
    }
    break;
  }
  default:
    break;
  }
  if (generic_node_has_scalar_data (n->code) || n->code == N_ID || n->code == N_STRING
      || n->code == N_IGNORE)
    return 1;
  for (c = NL_HEAD (n->u.ops); c != NULL; c = NL_NEXT (c))
    if (!hof_method_body_pure_p (c2m_ctx, c)) return 0;
  return 1;
}

/* Is the method named NM on class TAG (arity NARGS, this excluded) present
   and provably read-only (depth-1)? */
static int hof_method_readonly_p (c2m_ctx_t c2m_ctx, node_t tag, const char *nm, int nargs,
                                  pos_t pos) {
  node_t id;
  symbol_t sym;
  size_t i;

  if (tag == NULL || nm == NULL) return 0;
  id = build_id (c2m_ctx, nm, pos);
  if (!find_overload_sym (c2m_ctx, id, tag, &sym)) return 0;
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
    if (nparams != nargs + 1) continue;
    return hof_method_body_pure_p (c2m_ctx, FUNC_DEF_BLOCK (def));
  }
  return 0;
}

/* Does the lambda body expression N use its param PN read-only?  (Parse-level
   AST: no receiver injection yet, so `pn.Method()` args are all user args.) */
static int hof_lambda_param_readonly_p (c2m_ctx_t c2m_ctx, node_t n, const char *pn,
                                        node_t el_tag, pos_t pos) {
  node_t c;
  if (n == NULL || pn == NULL) return 1;
  switch (n->code) {
  case N_ASSIGN: case N_ADD_ASSIGN: case N_SUB_ASSIGN: case N_MUL_ASSIGN:
  case N_DIV_ASSIGN: case N_MOD_ASSIGN: case N_LSH_ASSIGN: case N_RSH_ASSIGN:
  case N_AND_ASSIGN: case N_OR_ASSIGN: case N_XOR_ASSIGN: {
    node_t lhs = NL_HEAD (n->u.ops);
    while (lhs != NULL && (lhs->code == N_FIELD || lhs->code == N_DEREF_FIELD
                           || lhs->code == N_DEREF || lhs->code == N_IND))
      lhs = NL_HEAD (lhs->u.ops);
    if (lhs != NULL && lhs->code == N_ID && lhs->u.s.s != NULL
        && strcmp (lhs->u.s.s, pn) == 0)
      return 0;
    break;
  }
  case N_INC: case N_DEC: case N_POST_INC: case N_POST_DEC: {
    node_t op = NL_HEAD (n->u.ops);
    while (op != NULL && (op->code == N_FIELD || op->code == N_DEREF_FIELD))
      op = NL_HEAD (op->u.ops);
    if (op != NULL && op->code == N_ID && op->u.s.s != NULL
        && strcmp (op->u.s.s, pn) == 0)
      return 0;
    break;
  }
  case N_MOVE: case N_DELETE: case N_ADDR: {
    node_t op = NL_HEAD (n->u.ops);
    while (op != NULL && (op->code == N_FIELD || op->code == N_DEREF_FIELD))
      op = NL_HEAD (op->u.ops);
    if (op != NULL && op->code == N_ID && op->u.s.s != NULL
        && strcmp (op->u.s.s, pn) == 0)
      return 0;
    break;
  }
  case N_CALL: {
    node_t fn = NL_HEAD (n->u.ops);
    if (fn != NULL && fn->code == N_FIELD) {
      node_t base = NL_HEAD (fn->u.ops);
      node_t mid = NL_NEXT (base);
      if (base != NULL && base->code == N_ID && base->u.s.s != NULL
          && strcmp (base->u.s.s, pn) == 0) {
        /* Method on the param: must be provably read-only on the element. */
        node_t args = NL_EL (n->u.ops, 1);
        int nargs = (args != NULL && args->code == N_LIST)
                      ? (int) NL_LENGTH (args->u.ops) : 0;
        const char *nm = (mid != NULL && mid->code == N_ID) ? mid->u.s.s : NULL;
        if (!hof_method_readonly_p (c2m_ctx, el_tag, nm, nargs, pos)) return 0;
      }
    }
    break;
  }
  default:
    break;
  }
  if (generic_node_has_scalar_data (n->code) || n->code == N_ID || n->code == N_STRING
      || n->code == N_IGNORE)
    return 1;
  for (c = NL_HEAD (n->u.ops); c != NULL; c = NL_NEXT (c))
    if (!hof_lambda_param_readonly_p (c2m_ctx, c, pn, el_tag, pos)) return 0;
  return 1;
}

/* Build `*(recv.GetMut(index_expr))` — an element lvalue in the buffer. */
static node_t build_getmut_deref_call (c2m_ctx_t c2m_ctx, pos_t pos, node_t recv,
                                       node_t index_expr) {
  node_t call
    = build_dot_call (c2m_ctx, pos, parse_copy_expr (c2m_ctx, recv), "GetMut",
                      new_node1 (c2m_ctx, N_LIST, parse_copy_expr (c2m_ctx, index_expr)));
  return new_pos_node1 (c2m_ctx, N_DEREF, pos, call);
}

/* Open-code a capturing HOF call.  Replaces *CALL with an N_STMTEXPR and
   fully type-checks it.  Returns TRUE on success (call is rewritten). */
static int desugar_capturing_hof (c2m_ctx_t c2m_ctx, node_t call, node_t recv,
                                  struct type *recv_cls_type, node_t cid,
                                  const char *coll_kind, enum hof_kind hk,
                                  node_t lam) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  pos_t pos = POS (call);
  node_t params = NL_HEAD (lam->u.ops);
  node_t body = NL_NEXT (params);
  node_t p0, p1;
  const char *pn0, *pn1;
  int nparams;
  unsigned uid;
  char rname[64], aname[64], bname[64];
  node_t pred_expr = NULL;
  node_t stmt_list, block, stmtexpr, last;
  node_t for_body, for_body_stmts;
  node_t result_init, result_decl;
  DLIST_LINK (node_t) saved_link;
  int is_map = (coll_kind != NULL && strcmp (coll_kind, "Map") == 0);
  int want_2 = (is_map && (hk == HOF_WHERE || hk == HOF_FOREACH || hk == HOF_ANY
                           || hk == HOF_ALL))
               || (!is_map && hk == HOF_SORT); /* Sort(a,b) comparator */

  if (params == NULL || params->code != N_LIST) return FALSE;
  nparams = (int) NL_LENGTH (params->u.ops);
  p0 = NL_HEAD (params->u.ops);
  p1 = p0 != NULL ? NL_NEXT (p0) : NULL;
  pn0 = lambda_param_name (p0);
  pn1 = lambda_param_name (p1);
  if (want_2) {
    if (nparams != 2 || pn0 == NULL || pn1 == NULL) {
      error (c2m_ctx, POS (lam),
             hk == HOF_SORT
               ? "Sort comparator lambda must take two parameters (a, b)"
               : "Map HOF lambda must take two parameters (key, value)");
      return FALSE;
    }
  } else {
    if (nparams != 1 || pn0 == NULL) {
      error (c2m_ctx, POS (lam),
             "capturing HOF lambda must take one parameter matching the element type");
      return FALSE;
    }
  }

  /* Predicate/map HOFs need an expression-bodied (or single-return) lambda.
     ForEach also prefers a single expression; otherwise the block body is
     open-coded statement-by-statement. */
  pred_expr = lambda_single_return_expr (body);
  if (pred_expr != NULL) pred_expr = parse_copy_expr (c2m_ctx, pred_expr);
  /* Expression body: if body itself is an expression (not a block), use it. */
  if (pred_expr == NULL && body != NULL && body->code != N_BLOCK)
    pred_expr = parse_copy_expr (c2m_ctx, body);
  if ((hk == HOF_WHERE || hk == HOF_FILTER || hk == HOF_MAP || hk == HOF_ANY
       || hk == HOF_ALL || hk == HOF_FIND || hk == HOF_SORT || hk == HOF_SELECT
       || hk == HOF_COUNTWHERE)
      && pred_expr == NULL) {
    error (c2m_ctx, POS (lam),
           "capturing HOF lambda must use an expression body or a single `return expr;` "
           "(block multi-statement predicates not yet supported for capture desugar)");
    return FALSE;
  }

  uid = lambda_uid++;
  snprintf (rname, sizeof (rname), "__cap_r_%u", uid);
  snprintf (aname, sizeof (aname), "__cap_a_%u", uid);
  snprintf (bname, sizeof (bname), "__cap_b_%u", uid);

  stmt_list = new_node (c2m_ctx, N_LIST);

  /* Result container for filter/map/select HOFs.  Where/Filter/Map: same
     specialized type as the receiver.  Select: List<U> from explicit type
     args on the call (Select<U>) or same List specialization when U==T. */
  if (hk == HOF_WHERE || hk == HOF_FILTER || hk == HOF_MAP || hk == HOF_SELECT) {
    const char *result_cid = cid->u.s.s;
    if (hk == HOF_SELECT) {
      /* N_FIELD(recv, Select, optional N_LIST type args). */
      node_t field = NL_HEAD (call->u.ops);
      node_t mid = field != NULL ? NL_NEXT (NL_HEAD (field->u.ops)) : NULL;
      node_t targs = mid != NULL ? NL_NEXT (mid) : NULL;
      if (targs != NULL && targs->code == N_LIST && NL_HEAD (targs->u.ops) != NULL) {
        node_t ta = NL_HEAD (targs->u.ops);
        node_t spec_id = get_or_create_specialization (c2m_ctx, "List", 1, &ta, pos);
        if (spec_id != NULL && spec_id->code == N_ID) result_cid = spec_id->u.s.s;
      }
      /* else: same List_T as receiver — works when U coincides with T;
         U≠T without explicit <U> is diagnosed when Add typechecks. */
    }
    result_init = new_pos_node2 (c2m_ctx, N_CALL, pos,
                                 build_id (c2m_ctx, result_cid, pos),
                                 new_node (c2m_ctx, N_LIST));
    result_decl = build_auto_init_decl (c2m_ctx, pos, rname, result_init);
    op_append (c2m_ctx, stmt_list, result_decl);
  } else if (hk == HOF_ANY || hk == HOF_COUNTWHERE) {
    /* int r = 0;  ANY flips to 1; COUNTWHERE increments. */
    op_append (c2m_ctx, stmt_list,
               build_spec_decl (c2m_ctx, pos,
                                new_node1 (c2m_ctx, N_LIST, new_pos_node (c2m_ctx, N_INT, pos)),
                                build_decl (c2m_ctx, pos, build_id (c2m_ctx, rname, pos), NULL),
                                NULL, NULL, new_i_node (c2m_ctx, 0, pos)));
  } else if (hk == HOF_ALL) {
    op_append (c2m_ctx, stmt_list,
               build_spec_decl (c2m_ctx, pos,
                                new_node1 (c2m_ctx, N_LIST, new_pos_node (c2m_ctx, N_INT, pos)),
                                build_decl (c2m_ctx, pos, build_id (c2m_ctx, rname, pos), NULL),
                                NULL, NULL, new_i_node (c2m_ctx, 1, pos)));
  }
  /* FOREACH / FIND: no pre-loop result (FIND handled via break binding below). */

  for_body_stmts = new_node (c2m_ctx, N_LIST);

  if (is_map) {
    /* Map: for (auto k, v in recv) … keeps KeyAt/ValAt protocol. */
    if (hk == HOF_WHERE || hk == HOF_FILTER) {
      node_t args = new_node (c2m_ctx, N_LIST);
      op_append (c2m_ctx, args, build_id (c2m_ctx, pn0, pos));
      op_append (c2m_ctx, args, build_id (c2m_ctx, pn1, pos));
      node_t then_call
        = build_dot_call (c2m_ctx, pos, build_id (c2m_ctx, rname, pos), "Set", args);
      node_t then_stmt
        = new_pos_node2 (c2m_ctx, N_EXPR, pos, new_node (c2m_ctx, N_LIST), then_call);
      op_append (c2m_ctx, for_body_stmts,
                 new_pos_node4 (c2m_ctx, N_IF, pos, new_node (c2m_ctx, N_LIST), pred_expr,
                                then_stmt, new_node (c2m_ctx, N_IGNORE)));
    } else if (hk == HOF_FOREACH) {
      if (pred_expr != NULL)
        op_append (c2m_ctx, for_body_stmts,
                   new_pos_node2 (c2m_ctx, N_EXPR, pos, new_node (c2m_ctx, N_LIST), pred_expr));
      else if (body->code == N_BLOCK) {
        node_t bs = NL_EL (body->u.ops, 1);
        for (node_t s = (bs != NULL) ? NL_HEAD (bs->u.ops) : NULL; s != NULL; s = NL_NEXT (s))
          if (s->code == N_RETURN) {
            node_t re = NL_EL (s->u.ops, 1);
            if (re != NULL && re->code != N_IGNORE)
              op_append (c2m_ctx, for_body_stmts,
                         new_pos_node2 (c2m_ctx, N_EXPR, pos, new_node (c2m_ctx, N_LIST),
                                        parse_copy_expr (c2m_ctx, re)));
          } else
            op_append (c2m_ctx, for_body_stmts, parse_copy_expr (c2m_ctx, s));
      }
    } else if (hk == HOF_ANY || hk == HOF_ALL) {
      node_t cond = (hk == HOF_ANY) ? pred_expr : new_pos_node1 (c2m_ctx, N_NOT, pos, pred_expr);
      node_t setv = new_pos_node2 (c2m_ctx, N_ASSIGN, pos, build_id (c2m_ctx, rname, pos),
                                   new_i_node (c2m_ctx, hk == HOF_ANY ? 1 : 0, pos));
      node_t brk = new_pos_node1 (c2m_ctx, N_BREAK, pos, new_node (c2m_ctx, N_LIST));
      node_t then_list = new_node (c2m_ctx, N_LIST);
      op_append (c2m_ctx, then_list,
                 new_pos_node2 (c2m_ctx, N_EXPR, pos, new_node (c2m_ctx, N_LIST), setv));
      op_append (c2m_ctx, then_list, brk);
      node_t then_blk
        = new_pos_node2 (c2m_ctx, N_BLOCK, pos, new_node (c2m_ctx, N_LIST), then_list);
      then_blk->attr = NULL;
      op_append (c2m_ctx, for_body_stmts,
                 new_pos_node4 (c2m_ctx, N_IF, pos, new_node (c2m_ctx, N_LIST), cond, then_blk,
                                new_node (c2m_ctx, N_IGNORE)));
    } else {
      return FALSE;
    }
    for_body = new_pos_node2 (c2m_ctx, N_BLOCK, pos, new_node (c2m_ctx, N_LIST), for_body_stmts);
    for_body->attr = NULL;
    {
      node_t fin = new_pos_node5 (c2m_ctx, N_FORIN, pos, new_node (c2m_ctx, N_LIST),
                                  build_id (c2m_ctx, pn0, pos), build_id (c2m_ctx, pn1, pos),
                                  parse_copy_expr (c2m_ctx, recv), for_body);
      op_append (c2m_ctx, stmt_list, fin);
    }
  } else {
    /* List / Set: open-code with Count/Get (matches list.h Filter's double-Get
       policy so by-value class elements like Hit don't miscompile via for-in
       RAII loop vars inside statement expressions).

       HOF_FIND reuses the Where-style filter loop into a temp List, then
       yields hits.Find((T x) => 1) so the first match (or zero-init miss)
       matches list.h Find semantics without a typed zero-init AST. */
    char iname[64];
    node_t i_id, i_decl, cond, incr, get1, get2, loop_body, for_loop;
    node_t pred_sub = NULL;
    enum hof_kind loop_hk = hk;

    /* FIND: seed result with zero via non-capturing always-false Find on
       the receiver, then overwrite on the first capturing match.
       Lambda body must be a BLOCK (`{ return 0; }`), never a bare expr —
       N_FUNC_DEF uses the body node as the function scope. */
    if (hk == HOF_FIND) {
      node_t zero_lam, zero_args, zero_call, old_body, ret_stmt, blist, zbody;
      zero_lam = parse_copy_expr (c2m_ctx, lam);
      if (zero_lam != NULL && zero_lam->code == N_LAMBDA) {
        zero_lam->attr = NULL;
        old_body = NL_NEXT (NL_HEAD (zero_lam->u.ops));
        if (old_body != NULL) NL_REMOVE (zero_lam->u.ops, old_body);
        ret_stmt = new_pos_node2 (c2m_ctx, N_RETURN, pos, new_node (c2m_ctx, N_LIST),
                                  new_i_node (c2m_ctx, 0, pos));
        blist = new_node (c2m_ctx, N_LIST);
        op_append (c2m_ctx, blist, ret_stmt);
        zbody = new_pos_node2 (c2m_ctx, N_BLOCK, pos, new_node (c2m_ctx, N_LIST), blist);
        zbody->attr = NULL;
        op_append (c2m_ctx, zero_lam, zbody);
      }
      zero_args = new_node1 (c2m_ctx, N_LIST,
                             zero_lam != NULL ? zero_lam : new_i_node (c2m_ctx, 0, pos));
      zero_call
        = build_dot_call (c2m_ctx, pos, parse_copy_expr (c2m_ctx, recv), "Find", zero_args);
      result_decl = build_auto_init_decl (c2m_ctx, pos, rname, zero_call);
      op_append (c2m_ctx, stmt_list, result_decl);
    }

    snprintf (iname, sizeof (iname), "__cap_i_%u", uid);
    i_id = build_id (c2m_ctx, iname, pos);
    i_decl = build_spec_decl (c2m_ctx, pos,
                              new_node1 (c2m_ctx, N_LIST, new_pos_node (c2m_ctx, N_INT, pos)),
                              build_decl (c2m_ctx, pos, build_id (c2m_ctx, iname, pos), NULL),
                              NULL, NULL, new_i_node (c2m_ctx, 0, pos));
    cond = new_pos_node2 (c2m_ctx, N_LT, pos, build_id (c2m_ctx, iname, pos),
                          build_count_call (c2m_ctx, pos, recv));
    incr = new_pos_node2 (c2m_ctx, N_ASSIGN, pos, build_id (c2m_ctx, iname, pos),
                          new_pos_node2 (c2m_ctx, N_ADD, pos, build_id (c2m_ctx, iname, pos),
                                         new_i_node (c2m_ctx, 1, pos)));
    get1 = build_get_call (c2m_ctx, pos, recv, i_id);
    get2 = build_get_call (c2m_ctx, pos, recv, i_id);

    /* R2.2a: when the element is a non-move-only aggregate and the lambda
       only reads its param, substitute `*(recv.GetMut(i))` instead of
       `recv.Get(i)` — identical reads, no per-element block copy.  Move-only
       elements keep Get (its return path carries the deep-Copy rewrite). */
    if (pred_expr != NULL && pn0 != NULL && recv_cls_type != NULL
        && recv_cls_type->u.tag_type != NULL) {
      node_t tag = recv_cls_type->u.tag_type;
      decl_t data_f = NULL, len_f = NULL;
      struct type *el_t = NULL;
      if (find_dense_buffer_fields (tag, &data_f, &len_f, NULL)
          && data_f != NULL && data_f->decl_spec.type != NULL)
        el_t = data_f->decl_spec.type->u.ptr_type;
      if (el_t != NULL
          && (el_t->mode == TM_CLASS || el_t->mode == TM_STRUCT || el_t->mode == TM_UNION)
          && !class_is_move_only_collection_p (c2m_ctx, el_t)
          && find_class_protocol_method (c2m_ctx, tag, "GetMut", 1, pos) != NULL
          && hof_lambda_param_readonly_p (c2m_ctx, pred_expr, pn0,
                                          el_t->mode == TM_CLASS ? el_t->u.tag_type : NULL,
                                          pos)) {
        get1 = build_getmut_deref_call (c2m_ctx, pos, recv, i_id);
        get2 = build_getmut_deref_call (c2m_ctx, pos, recv, i_id);
      }
    }

    if (pred_expr != NULL)
      pred_sub = lambda_subst_id (c2m_ctx, pred_expr, pn0, get1);

    if (hk == HOF_WHERE || hk == HOF_FILTER) {
      /* if (pred(Get(i))) result.Add(Get(i)); */
      node_t args = new_node1 (c2m_ctx, N_LIST, get2);
      node_t then_call
        = build_dot_call (c2m_ctx, pos, build_id (c2m_ctx, rname, pos), "Add", args);
      node_t then_stmt
        = new_pos_node2 (c2m_ctx, N_EXPR, pos, new_node (c2m_ctx, N_LIST), then_call);
      op_append (c2m_ctx, for_body_stmts,
                 new_pos_node4 (c2m_ctx, N_IF, pos, new_node (c2m_ctx, N_LIST), pred_sub,
                                then_stmt, new_node (c2m_ctx, N_IGNORE)));
    } else if (hk == HOF_FIND) {
      /* if (pred(Get(i))) { result = Get(i); break; } */
      node_t setv = new_pos_node2 (c2m_ctx, N_ASSIGN, pos, build_id (c2m_ctx, rname, pos), get2);
      node_t brk = new_pos_node1 (c2m_ctx, N_BREAK, pos, new_node (c2m_ctx, N_LIST));
      node_t then_list = new_node (c2m_ctx, N_LIST);
      op_append (c2m_ctx, then_list,
                 new_pos_node2 (c2m_ctx, N_EXPR, pos, new_node (c2m_ctx, N_LIST), setv));
      op_append (c2m_ctx, then_list, brk);
      node_t then_blk
        = new_pos_node2 (c2m_ctx, N_BLOCK, pos, new_node (c2m_ctx, N_LIST), then_list);
      then_blk->attr = NULL;
      op_append (c2m_ctx, for_body_stmts,
                 new_pos_node4 (c2m_ctx, N_IF, pos, new_node (c2m_ctx, N_LIST), pred_sub,
                                then_blk, new_node (c2m_ctx, N_IGNORE)));
    } else if (hk == HOF_MAP || hk == HOF_SELECT) {
      /* result.Add(proj(Get(i))) — Select projects T→U; Map is T→T. */
      node_t mapped = pred_sub;
      node_t args = new_node1 (c2m_ctx, N_LIST, mapped);
      node_t add = build_dot_call (c2m_ctx, pos, build_id (c2m_ctx, rname, pos), "Add", args);
      op_append (c2m_ctx, for_body_stmts,
                 new_pos_node2 (c2m_ctx, N_EXPR, pos, new_node (c2m_ctx, N_LIST), add));
    } else if (hk == HOF_SORT) {
      /* Shell sort in place (matches list.h Sort).  cmp(a,b) > 0 means a after b. */
      char gname[64], jname[64], tname[64];
      node_t gap_decl, i_sort_decl, j_decl, tmp_decl;
      node_t outer_init, outer_cond, outer_incr, outer_body;
      node_t inner_stmts, while_cond, while_body, while_loop;
      node_t get_jg, get_tmp_src, cmp_call, cmp_gt, shift, place_tmp;
      node_t gap_id, i_id2, j_id, tmp_id;
      node_t gap_halve, gap_gt0, for_i, i_lt_n, i_incr;
      node_t j_ge_gap, get_j, get_i, swap_set_j, swap_set_i, tmp_from_i;
      node_t pred_ab, pn_a, pn_b;

      snprintf (gname, sizeof (gname), "__cap_gap_%u", uid);
      snprintf (jname, sizeof (jname), "__cap_j_%u", uid);
      snprintf (tname, sizeof (tname), "__cap_tmp_%u", uid);
      /* int gap = Count()/2; */
      gap_decl = build_spec_decl (c2m_ctx, pos,
                                  new_node1 (c2m_ctx, N_LIST, new_pos_node (c2m_ctx, N_INT, pos)),
                                  build_decl (c2m_ctx, pos, build_id (c2m_ctx, gname, pos), NULL),
                                  NULL, NULL,
                                  new_pos_node2 (c2m_ctx, N_DIV, pos,
                                                 build_count_call (c2m_ctx, pos, recv),
                                                 new_i_node (c2m_ctx, 2, pos)));
      op_append (c2m_ctx, stmt_list, gap_decl);
      /* while (gap > 0) { for (i = gap; i < n; i++) { ... } gap /= 2; } */
      {
        node_t while_body_stmts = new_node (c2m_ctx, N_LIST);
        char iname2[64];
        snprintf (iname2, sizeof (iname2), "__cap_si_%u", uid);
        /* for (int i = gap; i < Count(); i++) */
        i_sort_decl
          = build_spec_decl (c2m_ctx, pos,
                             new_node1 (c2m_ctx, N_LIST, new_pos_node (c2m_ctx, N_INT, pos)),
                             build_decl (c2m_ctx, pos, build_id (c2m_ctx, iname2, pos), NULL),
                             NULL, NULL, build_id (c2m_ctx, gname, pos));
        i_lt_n = new_pos_node2 (c2m_ctx, N_LT, pos, build_id (c2m_ctx, iname2, pos),
                                build_count_call (c2m_ctx, pos, recv));
        i_incr = new_pos_node2 (c2m_ctx, N_ASSIGN, pos, build_id (c2m_ctx, iname2, pos),
                                new_pos_node2 (c2m_ctx, N_ADD, pos,
                                               build_id (c2m_ctx, iname2, pos),
                                               new_i_node (c2m_ctx, 1, pos)));
        /* T tmp = Get(i); int j = i; */
        inner_stmts = new_node (c2m_ctx, N_LIST);
        tmp_from_i = build_get_call (c2m_ctx, pos, recv, build_id (c2m_ctx, iname2, pos));
        tmp_decl = build_auto_init_decl (c2m_ctx, pos, tname, tmp_from_i);
        op_append (c2m_ctx, inner_stmts, tmp_decl);
        j_decl = build_spec_decl (c2m_ctx, pos,
                                  new_node1 (c2m_ctx, N_LIST, new_pos_node (c2m_ctx, N_INT, pos)),
                                  build_decl (c2m_ctx, pos, build_id (c2m_ctx, jname, pos), NULL),
                                  NULL, NULL, build_id (c2m_ctx, iname2, pos));
        op_append (c2m_ctx, inner_stmts, j_decl);
        /* while (j >= gap && cmp(Get(j-gap), tmp) > 0) { Set(j, Get(j-gap)); j -= gap; } */
        {
          node_t j_minus_gap
            = new_pos_node2 (c2m_ctx, N_SUB, pos, build_id (c2m_ctx, jname, pos),
                             build_id (c2m_ctx, gname, pos));
          node_t get_jgap = build_get_call (c2m_ctx, pos, recv, j_minus_gap);
          node_t cmp_body = pred_expr;
          /* Substitute pn0 ← Get(j-gap), pn1 ← tmp */
          cmp_body = lambda_subst_id (c2m_ctx, cmp_body, pn0, get_jgap);
          cmp_body = lambda_subst_id (c2m_ctx, cmp_body, pn1, build_id (c2m_ctx, tname, pos));
          node_t cmp_gt0
            = new_pos_node2 (c2m_ctx, N_GT, pos, cmp_body, new_i_node (c2m_ctx, 0, pos));
          node_t j_ge
            = new_pos_node2 (c2m_ctx, N_GE, pos, build_id (c2m_ctx, jname, pos),
                             build_id (c2m_ctx, gname, pos));
          while_cond = new_pos_node2 (c2m_ctx, N_ANDAND, pos, j_ge, cmp_gt0);
          node_t wstmts = new_node (c2m_ctx, N_LIST);
          /* recv.Set(j, Get(j-gap)) */
          {
            node_t set_args = new_node (c2m_ctx, N_LIST);
            op_append (c2m_ctx, set_args, build_id (c2m_ctx, jname, pos));
            op_append (c2m_ctx, set_args,
                       build_get_call (c2m_ctx, pos, recv,
                                       new_pos_node2 (c2m_ctx, N_SUB, pos,
                                                      build_id (c2m_ctx, jname, pos),
                                                      build_id (c2m_ctx, gname, pos))));
            op_append (c2m_ctx, wstmts,
                       new_pos_node2 (c2m_ctx, N_EXPR, pos, new_node (c2m_ctx, N_LIST),
                                      build_dot_call (c2m_ctx, pos,
                                                      parse_copy_expr (c2m_ctx, recv), "Set",
                                                      set_args)));
          }
          /* j = j - gap */
          op_append (c2m_ctx, wstmts,
                     new_pos_node2 (c2m_ctx, N_EXPR, pos, new_node (c2m_ctx, N_LIST),
                                    new_pos_node2 (c2m_ctx, N_ASSIGN, pos,
                                                   build_id (c2m_ctx, jname, pos),
                                                   new_pos_node2 (c2m_ctx, N_SUB, pos,
                                                                  build_id (c2m_ctx, jname, pos),
                                                                  build_id (c2m_ctx, gname, pos)))));
          while_body
            = new_pos_node2 (c2m_ctx, N_BLOCK, pos, new_node (c2m_ctx, N_LIST), wstmts);
          while_body->attr = NULL;
          while_loop = new_pos_node3 (c2m_ctx, N_WHILE, pos, new_node (c2m_ctx, N_LIST),
                                      while_cond, while_body);
          op_append (c2m_ctx, inner_stmts, while_loop);
        }
        /* Set(j, tmp) */
        {
          node_t set_args = new_node (c2m_ctx, N_LIST);
          op_append (c2m_ctx, set_args, build_id (c2m_ctx, jname, pos));
          op_append (c2m_ctx, set_args, build_id (c2m_ctx, tname, pos));
          op_append (c2m_ctx, inner_stmts,
                     new_pos_node2 (c2m_ctx, N_EXPR, pos, new_node (c2m_ctx, N_LIST),
                                    build_dot_call (c2m_ctx, pos, parse_copy_expr (c2m_ctx, recv),
                                                    "Set", set_args)));
        }
        {
          node_t for_body2
            = new_pos_node2 (c2m_ctx, N_BLOCK, pos, new_node (c2m_ctx, N_LIST), inner_stmts);
          node_t for_i_loop = new_pos_node (c2m_ctx, N_FOR, pos);
          for_body2->attr = NULL;
          for_i_loop->attr = NULL;
          op_append (c2m_ctx, for_i_loop, new_node (c2m_ctx, N_LIST));
          op_append (c2m_ctx, for_i_loop, i_sort_decl);
          op_append (c2m_ctx, for_i_loop, i_lt_n);
          op_append (c2m_ctx, for_i_loop, i_incr);
          op_append (c2m_ctx, for_i_loop, for_body2);
          op_append (c2m_ctx, while_body_stmts, for_i_loop);
        }
        /* gap = gap / 2 */
        op_append (c2m_ctx, while_body_stmts,
                   new_pos_node2 (c2m_ctx, N_EXPR, pos, new_node (c2m_ctx, N_LIST),
                                  new_pos_node2 (c2m_ctx, N_ASSIGN, pos,
                                                 build_id (c2m_ctx, gname, pos),
                                                 new_pos_node2 (c2m_ctx, N_DIV, pos,
                                                                build_id (c2m_ctx, gname, pos),
                                                                new_i_node (c2m_ctx, 2, pos)))));
        gap_gt0 = new_pos_node2 (c2m_ctx, N_GT, pos, build_id (c2m_ctx, gname, pos),
                                 new_i_node (c2m_ctx, 0, pos));
        {
          node_t wbody
            = new_pos_node2 (c2m_ctx, N_BLOCK, pos, new_node (c2m_ctx, N_LIST), while_body_stmts);
          node_t wloop
            = new_pos_node3 (c2m_ctx, N_WHILE, pos, new_node (c2m_ctx, N_LIST), gap_gt0, wbody);
          wbody->attr = NULL;
          op_append (c2m_ctx, stmt_list, wloop);
        }
      }
      /* Sort is void-ish: no per-element for_body_stmts loop below. */
      goto list_hof_skip_index_loop;
    } else if (hk == HOF_FOREACH) {
      if (pred_sub != NULL)
        op_append (c2m_ctx, for_body_stmts,
                   new_pos_node2 (c2m_ctx, N_EXPR, pos, new_node (c2m_ctx, N_LIST), pred_sub));
      else if (body->code == N_BLOCK) {
        /* Block ForEach: bind auto x = Get(i) then run statements. */
        node_t bind
          = build_auto_init_decl (c2m_ctx, pos, pn0, get1);
        op_append (c2m_ctx, for_body_stmts, bind);
        node_t bs = NL_EL (body->u.ops, 1);
        for (node_t s = (bs != NULL) ? NL_HEAD (bs->u.ops) : NULL; s != NULL; s = NL_NEXT (s))
          if (s->code == N_RETURN) {
            node_t re = NL_EL (s->u.ops, 1);
            if (re != NULL && re->code != N_IGNORE)
              op_append (c2m_ctx, for_body_stmts,
                         new_pos_node2 (c2m_ctx, N_EXPR, pos, new_node (c2m_ctx, N_LIST),
                                        parse_copy_expr (c2m_ctx, re)));
          } else
            op_append (c2m_ctx, for_body_stmts, parse_copy_expr (c2m_ctx, s));
      }
    } else if (hk == HOF_ANY || hk == HOF_ALL) {
      node_t condp
        = (hk == HOF_ANY) ? pred_sub : new_pos_node1 (c2m_ctx, N_NOT, pos, pred_sub);
      node_t setv = new_pos_node2 (c2m_ctx, N_ASSIGN, pos, build_id (c2m_ctx, rname, pos),
                                   new_i_node (c2m_ctx, hk == HOF_ANY ? 1 : 0, pos));
      node_t brk = new_pos_node1 (c2m_ctx, N_BREAK, pos, new_node (c2m_ctx, N_LIST));
      node_t then_list = new_node (c2m_ctx, N_LIST);
      op_append (c2m_ctx, then_list,
                 new_pos_node2 (c2m_ctx, N_EXPR, pos, new_node (c2m_ctx, N_LIST), setv));
      op_append (c2m_ctx, then_list, brk);
      node_t then_blk
        = new_pos_node2 (c2m_ctx, N_BLOCK, pos, new_node (c2m_ctx, N_LIST), then_list);
      then_blk->attr = NULL;
      op_append (c2m_ctx, for_body_stmts,
                 new_pos_node4 (c2m_ctx, N_IF, pos, new_node (c2m_ctx, N_LIST), condp, then_blk,
                                new_node (c2m_ctx, N_IGNORE)));
    } else if (hk == HOF_COUNTWHERE) {
      /* if (pred(Get(i))) r = r + 1; */
      node_t incr_r
        = new_pos_node2 (c2m_ctx, N_ASSIGN, pos, build_id (c2m_ctx, rname, pos),
                         new_pos_node2 (c2m_ctx, N_ADD, pos, build_id (c2m_ctx, rname, pos),
                                        new_i_node (c2m_ctx, 1, pos)));
      node_t then_stmt
        = new_pos_node2 (c2m_ctx, N_EXPR, pos, new_node (c2m_ctx, N_LIST), incr_r);
      op_append (c2m_ctx, for_body_stmts,
                 new_pos_node4 (c2m_ctx, N_IF, pos, new_node (c2m_ctx, N_LIST), pred_sub,
                                then_stmt, new_node (c2m_ctx, N_IGNORE)));
    } else {
      return FALSE;
    }

    loop_body = new_pos_node2 (c2m_ctx, N_BLOCK, pos, new_node (c2m_ctx, N_LIST), for_body_stmts);
    loop_body->attr = NULL;
    for_loop = new_pos_node (c2m_ctx, N_FOR, pos);
    for_loop->attr = NULL;
    op_append (c2m_ctx, for_loop, new_node (c2m_ctx, N_LIST)); /* labels */
    op_append (c2m_ctx, for_loop, i_decl);
    op_append (c2m_ctx, for_loop, cond);
    op_append (c2m_ctx, for_loop, incr);
    op_append (c2m_ctx, for_loop, loop_body);
    op_append (c2m_ctx, stmt_list, for_loop);
  list_hof_skip_index_loop:;
  }

  /* Yield result (move for collections). ForEach/Sort yield 0 (void-like). */
  if (hk == HOF_FOREACH || hk == HOF_SORT) {
    last = new_i_node (c2m_ctx, 0, pos);
  } else if (hk == HOF_FIND) {
    last = build_id (c2m_ctx, rname, pos);
  } else if (hk == HOF_WHERE || hk == HOF_FILTER || hk == HOF_MAP || hk == HOF_SELECT) {
    last = new_pos_node1 (c2m_ctx, N_MOVE, pos, build_id (c2m_ctx, rname, pos));
  } else {
    last = build_id (c2m_ctx, rname, pos);
  }
  op_append (c2m_ctx, stmt_list,
             new_pos_node2 (c2m_ctx, N_EXPR, pos, new_node (c2m_ctx, N_LIST), last));

  block = new_pos_node2 (c2m_ctx, N_BLOCK, pos, new_node (c2m_ctx, N_LIST), stmt_list);
  block->attr = NULL;
  stmtexpr = new_pos_node1 (c2m_ctx, N_STMTEXPR, pos, block);

  /* Replace call in place, preserving sibling links.
     Select may have created a new List<U> specialization — materialize it
     before type-checking the open-coded stmtexpr. */
  {
    parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
    size_t pend_mark = 0;
    if (generic_method_specs != NULL || pending_lambdas != NULL)
      pend_mark = VARR_LENGTH (node_t, pending_lambdas);
    /* Drain all currently pending specializations (new List_U for Select). */
    materialize_pending_specs (c2m_ctx, 0);
    saved_link = call->op_link;
    check (c2m_ctx, stmtexpr, NULL);
    *call = *stmtexpr;
    call->op_link = saved_link;
    (void) pend_mark;
  }
  (void) recv_cls_type; /* reserved for future type checks */
  (void) aname;
  (void) bname;
  return TRUE;
}

/* If CALL is recv.Where/Filter/... (N_FIELD or N_DEREF_FIELD) with an N_LAMBDA
   arg that captures outer automatics, open-code it.  Returns TRUE if rewritten. */
static int try_desugar_capturing_hof_call (c2m_ctx_t c2m_ctx, node_t call) {
  node_t op1, recv, mid, arg_list, lam;
  struct expr *re;
  struct type *obj_type;
  enum hof_kind hk;
  const char *coll_kind = NULL;
  node_t cid = NULL;
  VARR (cstr_t) * free_names;
  int nfree;

  if (call == NULL || call->code != N_CALL) return FALSE;
  op1 = NL_HEAD (call->u.ops);
  if (op1 == NULL || (op1->code != N_FIELD && op1->code != N_DEREF_FIELD)) return FALSE;
  recv = NL_HEAD (op1->u.ops);
  mid = NL_NEXT (recv);
  arg_list = NL_NEXT (op1);
  if (mid == NULL || mid->code != N_ID || arg_list == NULL || arg_list->code != N_LIST)
    return FALSE;
  hk = get_hof_kind (mid->u.s.s);
  if (hk == HOF_NONE) return FALSE;
  lam = NL_HEAD (arg_list->u.ops);
  if (lam == NULL || lam->code != N_LAMBDA || NL_NEXT (lam) != NULL) return FALSE;

  if (recv->attr == NULL) check (c2m_ctx, recv, call);
  re = recv->attr;
  if (re == NULL || re->type == NULL) return FALSE;
  obj_type = re->type;
  if (op1->code == N_DEREF_FIELD) {
    if (obj_type->mode != TM_PTR || obj_type->u.ptr_type == NULL) return FALSE;
    obj_type = obj_type->u.ptr_type;
  }
  if (!coll_class_kind (obj_type, &coll_kind, &cid)) return FALSE;

  /* Collection-specific HOF membership. */
  if (strcmp (coll_kind, "List") == 0) {
    if (hk == HOF_NONE) return FALSE;
    /* Sort / Select / Find / Where / CountWhere / … all allowed on List. */
  } else if (strcmp (coll_kind, "ListView") == 0) {
    /* Views: non-allocating HOFs only (Where materializes via method, not open-code). */
    if (hk != HOF_COUNTWHERE && hk != HOF_FOREACH && hk != HOF_ANY && hk != HOF_ALL
        && hk != HOF_FIND)
      return FALSE;
  } else if (strcmp (coll_kind, "Map") == 0) {
    if (hk != HOF_WHERE && hk != HOF_FOREACH && hk != HOF_ANY && hk != HOF_ALL)
      return FALSE;
  } else if (strcmp (coll_kind, "Set") == 0) {
    if (hk != HOF_FILTER && hk != HOF_FOREACH && hk != HOF_ANY && hk != HOF_ALL)
      return FALSE;
  }

  {
    MIR_alloc_t alloc = c2m_alloc (c2m_ctx);
    VARR_CREATE (cstr_t, free_names, alloc, 4);
  }
  lambda_collect_free_vars (c2m_ctx, lam, free_names);
  nfree = (int) VARR_LENGTH (cstr_t, free_names);
  VARR_DESTROY (cstr_t, free_names);
  if (nfree == 0) return FALSE; /* non-capturing: keep thin function-pointer path */

  return desugar_capturing_hof (c2m_ctx, call, recv, obj_type, cid, coll_kind, hk, lam);
}

/* Build a generic type-argument AST node (exactly as parse_generic_type_arg
   would have produced for an explicit `List<EL>`) denoting element type EL, so
   arr.ToList() can instantiate List<EL>.  Returns NULL for element types we
   cannot express as a type argument. */
static node_t build_seq_type_arg (c2m_ctx_t c2m_ctx, struct type *el, pos_t pos) {
  int ptr_depth = 0;
  node_t base = NULL;

  /* Peel pointer levels, but keep String intact (it is a basic type). */
  while (el != NULL && el->mode == TM_PTR && el->u.ptr_type != NULL && !string_type_p (el)) {
    ptr_depth++;
    el = el->u.ptr_type;
  }
  if (el == NULL) return NULL;
  if (string_type_p (el)) {
    base = new_pos_node (c2m_ctx, N_STRING, pos);
  } else if (el->mode == TM_BASIC) {
    switch (el->u.basic_type) {
    case TP_BOOL: base = new_pos_node (c2m_ctx, N_BOOL, pos); break;
    case TP_CHAR:
    case TP_SCHAR:
    case TP_UCHAR: base = new_pos_node (c2m_ctx, N_CHAR, pos); break;
    case TP_SHORT:
    case TP_USHORT: base = new_pos_node (c2m_ctx, N_SHORT, pos); break;
    case TP_INT: base = new_pos_node (c2m_ctx, N_INT, pos); break;
    case TP_UINT: base = new_pos_node (c2m_ctx, N_UNSIGNED, pos); break;
    case TP_LONG:
    case TP_ULONG:
    case TP_LLONG:
    case TP_ULLONG: base = new_pos_node (c2m_ctx, N_LONG, pos); break;
    case TP_FLOAT: base = new_pos_node (c2m_ctx, N_FLOAT, pos); break;
    case TP_DOUBLE:
    case TP_LDOUBLE: base = new_pos_node (c2m_ctx, N_DOUBLE, pos); break;
    default: break;
    }
  } else if (el->mode == TM_CLASS && el->u.tag_type != NULL) {
    node_t id = TAG_ID (el->u.tag_type);
    if (id != NULL && id->code == N_ID) base = build_id (c2m_ctx, id->u.s.s, pos);
  }
  if (base == NULL) return NULL;
  for (int i = 0; i < ptr_depth; i++) base = new_pos_node1 (c2m_ctx, N_POINTER, pos, base);
  return base;
}

/* Locate the array-view constructor of a (specialized) List class: the unique
   constructor overload taking exactly two user parameters, List(T* items,
   int count).  SPEC_NAME is the mangled class name (e.g. __generic_List_String).
   Returns the constructor N_FUNC_DEF or NULL. */
static node_t find_array_view_ctor (c2m_ctx_t c2m_ctx, const char *spec_name, node_t scope,
                                    pos_t pos) {
  char ctor_name[320];
  symbol_t ctor_sym;
  node_t ctor_id;

  snprintf (ctor_name, sizeof (ctor_name), "__ctor_%s", spec_name);
  ctor_id = build_id (c2m_ctx, ctor_name, pos);
  if (!find_overload_sym (c2m_ctx, ctor_id, scope, &ctor_sym)) return NULL;
  for (size_t ci = 0; ci < VARR_LENGTH (node_t, ctor_sym.defs); ci++) {
    node_t cand = VARR_GET (node_t, ctor_sym.defs, ci);
    decl_t cd;
    struct func_type *cft;
    node_t cp;
    int np = 0;
    if (cand == NULL || cand->code != N_FUNC_DEF) continue;
    cd = cand->attr;
    if (cd == NULL || cd->decl_spec.type == NULL || cd->decl_spec.type->mode != TM_FUNC) continue;
    cft = cd->decl_spec.type->u.func_type;
    cp = NL_HEAD (cft->param_list->u.ops);
    if (cp != NULL) cp = NL_NEXT (cp); /* skip implicit 'this' */
    for (; cp != NULL; cp = NL_NEXT (cp))
      if (cp->code == N_SPEC_DECL && !void_param_p (cp)) np++;
    if (np == 2) return cand;
  }
  return NULL;
}

/* ── Generalized collection-argument adaptation ─────────────────────────────
   A sized collection argument (a C array, a slice, or an array-decayed pointer
   that still remembers its origin) passed where the callee expects a pointer
   parameter immediately followed by an integer length parameter may omit the
   length: the compiler synthesizes it from the collection itself.  This lets
   any collection-style API written as `f(T* items, int count)` be called as
   `f(items)`, e.g. `new List<T>(arr)` — not just List, any such signature.

   The length is a compile-time constant for static arrays, otherwise it is the
   collection's `.count()` (valid for slices, decayed arrays, and any class with
   a Count() method).  These are check-phase AST rewrites only; gen simply walks
   the rewritten argument list. */

/* Argument types that carry, or can yield, a length. */
static int seq_arg_collection_p (struct type *t) {
  return t != NULL
         && (t->mode == TM_ARR || t->mode == TM_SLICE
             || (t->mode == TM_PTR && t->arr_type != NULL));
}

/* Compile-time element count of a collection arg type, or -1 if it is only
   known at run time. */
static mir_llong seq_arg_static_len (struct type *t) {
  if (t == NULL) return -1;
  if (t->mode == TM_ARR) return get_arr_type_size (t);
  if (t->mode == TM_PTR && t->arr_type != NULL) return get_arr_type_size (t->arr_type);
  return -1;
}

/* Build the synthesized length argument for collection ARG of type AT: a
   constant for static arrays, otherwise `argcopy.count()` (only synthesized for
   side-effect-free simple lvalues — a plain identifier). */
static node_t build_seq_count_arg (c2m_ctx_t c2m_ctx, node_t arg, struct type *at, pos_t pos) {
  mir_llong slen = seq_arg_static_len (at);
  node_t base_copy, field;
  if (slen >= 0) return new_i_node (c2m_ctx, (long) slen, pos);
  base_copy = copy_node (c2m_ctx, arg);
  field = new_pos_node2 (c2m_ctx, N_FIELD, pos, base_copy, build_id (c2m_ctx, "count", pos));
  return new_pos_node2 (c2m_ctx, N_CALL, pos, field, new_pos_node (c2m_ctx, N_LIST, pos));
}

/* If ARG_LIST has fewer arguments than FT's user parameters, attempt to make
   them match by synthesizing length arguments: wherever a pointer parameter
   that received a collection argument is immediately followed by an integer
   parameter with no aligned argument, insert the collection's length after the
   collection argument.  SKIP_THIS drops the implicit leading 'this' parameter
   (methods/constructors).  Returns TRUE and mutates ARG_LIST only on a complete
   match; otherwise leaves ARG_LIST untouched. */
static int try_expand_collection_count_args (c2m_ctx_t c2m_ctx, node_t r, struct func_type *ft,
                                             node_t arg_list, int skip_this) {
#define SEQ_EXPAND_MAX 32
  node_t params[SEQ_EXPAND_MAX], synth_src[SEQ_EXPAND_MAX];
  int is_synth[SEQ_EXPAND_MAX];
  node_t p, a, prev_arg = NULL;
  int np = 0, na, i, ai, n_synth = 0, prev_ptr_collection = FALSE;

  if (ft == NULL || ft->dots_p) return FALSE;
  na = (int) NL_LENGTH (arg_list->u.ops);
  p = NL_HEAD (ft->param_list->u.ops);
  if (skip_this && p != NULL) p = NL_NEXT (p);
  for (; p != NULL; p = NL_NEXT (p)) {
    if (p->code != N_SPEC_DECL && p->code != N_TYPE) continue;
    if (void_param_p (p)) continue;
    if (np >= SEQ_EXPAND_MAX) return FALSE;
    params[np++] = p;
  }
  if (na >= np) return FALSE; /* only adapt a short call */

  ai = 0;
  a = NL_HEAD (arg_list->u.ops);
  for (i = 0; i < np; i++) {
    struct decl_spec *pds = get_param_decl_spec (params[i]);
    struct type *pt = pds != NULL ? pds->type : NULL;
    is_synth[i] = FALSE;
    synth_src[i] = NULL;
    if ((na - ai) < (np - i) && prev_ptr_collection && pt != NULL && integer_type_p (pt)) {
      is_synth[i] = TRUE;
      synth_src[i] = prev_arg;
      prev_ptr_collection = FALSE;
      n_synth++;
      continue;
    }
    if (a == NULL) return FALSE;
    {
      struct expr *ae = a->attr;
      struct type *at = ae != NULL ? ae->type : NULL;
      int coll = seq_arg_collection_p (at);
      int synthesizable = coll && (seq_arg_static_len (at) >= 0 || a->code == N_ID);
      prev_ptr_collection = (pt != NULL && pt->mode == TM_PTR && synthesizable);
      prev_arg = a;
    }
    a = NL_NEXT (a);
    ai++;
  }
  if (a != NULL || ai != na || n_synth == 0) return FALSE;

  for (i = 0; i < np; i++) {
    node_t coll, cnt;
    struct expr *ce;
    if (!is_synth[i]) continue;
    coll = synth_src[i];
    ce = coll->attr;
    cnt = build_seq_count_arg (c2m_ctx, coll, ce != NULL ? ce->type : NULL, POS (coll));
    DLIST_INSERT_AFTER (node_t, arg_list->u.ops, coll, cnt);
    check (c2m_ctx, cnt, r);
  }
  return TRUE;
#undef SEQ_EXPAND_MAX
}

/* Scan the overloads in SYM for a function/method/constructor that ARG_LIST can
   satisfy via collection count-argument expansion (see above).  On success
   inserts the synthesized length argument(s) into ARG_LIST and returns the
   chosen N_FUNC_DEF; otherwise returns NULL and leaves ARG_LIST untouched. */
static node_t select_overload_with_count_expansion (c2m_ctx_t c2m_ctx, node_t r, symbol_t *sym,
                                                    int skip_this) {
  for (size_t ci = 0; ci < VARR_LENGTH (node_t, sym->defs); ci++) {
    node_t cand = VARR_GET (node_t, sym->defs, ci);
    decl_t cd;
    if (cand == NULL || cand->code != N_FUNC_DEF) continue;
    cd = cand->attr;
    if (cd == NULL || cd->decl_spec.type == NULL || cd->decl_spec.type->mode != TM_FUNC) continue;
    if (try_expand_collection_count_args (c2m_ctx, r, cd->decl_spec.type->u.func_type,
                                          NL_NEXT (NL_HEAD (r->u.ops)) /* arg_list */, skip_this))
      return cand;
  }
  return NULL;
}

/* Check a generic-class specialization (N_CLASS) that was synthesized in the
   middle of checking a function body (e.g. by `arr.ToList()`, which needs
   List<T> instantiated lazily — there is no explicit `List<T>` declaration to
   trigger it at parse time).  All per-function check state is saved and
   restored so the enclosing function's check continues undisturbed; the class
   is checked at top scope, exactly like a parse-time-injected specialization.
   Mirrors check_lambda_func_def. */
static void check_injected_class (c2m_ctx_t c2m_ctx, node_t class_node) {
  check_ctx_t check_ctx = c2m_ctx->check_ctx;
  node_t saved_scope = curr_scope, saved_func_block_scope = func_block_scope;
  node_t saved_func_def = curr_func_def, saved_class = curr_class;
  node_t saved_switch = curr_switch, saved_loop = curr_loop;
  node_t saved_loop_switch = curr_loop_switch;
  node_t saved_unnamed = curr_unnamed_anon_struct_union_member;
  node_t saved_lambda_def = curr_lambda_def;
  unsigned saved_scope_num = curr_func_scope_num;
  unsigned char saved_in_params_p = in_params_p, saved_jump_ret_p = jump_ret_p;
  mir_size_t saved_arg_area = curr_call_arg_area_offset;
  size_t i, fda_len = VARR_LENGTH (decl_t, func_decls_for_allocation);
  size_t lu_len = VARR_LENGTH (node_t, label_uses);
  decl_t *fda_save = NULL;
  node_t *lu_save = NULL;

  /* The class's method FUNC_DEFs truncate/consume these per-function VARRs;
     preserve the enclosing function's entries (as check_lambda_func_def does). */
  if (fda_len != 0) {
    fda_save = reg_malloc (c2m_ctx, fda_len * sizeof (decl_t));
    memcpy (fda_save, VARR_ADDR (decl_t, func_decls_for_allocation), fda_len * sizeof (decl_t));
  }
  if (lu_len != 0) {
    lu_save = reg_malloc (c2m_ctx, lu_len * sizeof (node_t));
    memcpy (lu_save, VARR_ADDR (node_t, label_uses), lu_len * sizeof (node_t));
  }
  VARR_TRUNC (node_t, label_uses, 0);
  curr_scope = top_scope;
  curr_class = NULL;
  check (c2m_ctx, class_node, NULL);
  VARR_TRUNC (decl_t, func_decls_for_allocation, 0);
  for (i = 0; i < fda_len; i++) VARR_PUSH (decl_t, func_decls_for_allocation, fda_save[i]);
  VARR_TRUNC (node_t, label_uses, 0);
  for (i = 0; i < lu_len; i++) VARR_PUSH (node_t, label_uses, lu_save[i]);
  curr_scope = saved_scope;
  func_block_scope = saved_func_block_scope;
  curr_func_def = saved_func_def;
  curr_class = saved_class;
  curr_switch = saved_switch;
  curr_loop = saved_loop;
  curr_loop_switch = saved_loop_switch;
  curr_unnamed_anon_struct_union_member = saved_unnamed;
  curr_lambda_def = saved_lambda_def;
  curr_func_scope_num = saved_scope_num;
  in_params_p = saved_in_params_p;
  jump_ret_p = saved_jump_ret_p;
  curr_call_arg_area_offset = saved_arg_area;
}

/* Drain generic-class specializations that get_or_create_specialization pushed
   onto pending_lambdas after MARK while we were checking a function body, and
   that aren't otherwise materialized (parse-time specializations are drained
   into the module before their consuming item; ones created lazily during the
   check phase — notably by `arr.ToList()` — are not).  Each new specialization
   is injected into the module item list before the item currently being checked
   (so gen emits its methods first) and then checked, defining its class symbol
   for the find_def lookup that follows.  No-op when nothing new was pushed. */
static void materialize_pending_specs (c2m_ctx_t c2m_ctx, size_t mark) {
  check_ctx_t check_ctx = c2m_ctx->check_ctx;
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;

  if (module_item_list == NULL || curr_module_item == NULL) return;
  while (VARR_LENGTH (node_t, pending_lambdas) > mark) {
    /* Snapshot and clear the new tail first: checking an item may itself push
       further specializations, which the outer loop then picks up in order. */
    size_t n = VARR_LENGTH (node_t, pending_lambdas) - mark;
    node_t *items = reg_malloc (c2m_ctx, n * sizeof (node_t));
    for (size_t i = 0; i < n; i++) items[i] = VARR_GET (node_t, pending_lambdas, mark + i);
    VARR_TRUNC (node_t, pending_lambdas, mark);

    /* Pass 1: inject every pending item into the module list so gen order is
       stable and class nodes are reachable from the AST. */
    for (size_t i = 0; i < n; i++)
      DLIST_INSERT_BEFORE (node_t, module_item_list->u.ops, curr_module_item, items[i]);

    /* Pass 2: pre-register every specialized CLASS name as a type before any
       method signature is checked.  Mutual generics (Host ↔ Peer, List ↔
       ListView) otherwise fail with "unknown type __generic_X_int" when peer
       A is checked while peer B is only sitting later in the same batch —
       return types on signatures are not deferred by defer_method_bodies_p. */
    for (size_t i = 0; i < n; i++) {
      node_t cls = items[i];
      node_t cid;
      if (cls == NULL || cls->code != N_CLASS) continue;
      if (cls->attr == (void *)((intptr_t)-1)) continue; /* template shell */
      cid = NL_HEAD (cls->u.ops);
      if (cid == NULL || cid->code != N_ID || cid->u.s.s == NULL) continue;
      {
        node_t scope = top_scope != NULL ? top_scope : curr_scope;
        if (scope == NULL) continue;
        if (find_def (c2m_ctx, S_REGULARS, cid, scope, NULL) == NULL)
          symbol_insert (c2m_ctx, S_REGULARS, cid, scope, cls, NULL);
        if (find_def (c2m_ctx, S_TAG, cid, scope, NULL) == NULL)
          symbol_insert (c2m_ctx, S_TAG, cid, scope, cls, NULL);
        tpname_add (c2m_ctx, cid, scope, TRUE);
      }
    }

    /* Pass 3: full check (class signatures / method bodies per defer flags). */
    for (size_t i = 0; i < n; i++) {
      if (items[i]->code == N_CLASS)
        check_injected_class (c2m_ctx, items[i]);
      else
        check_lambda_func_def (c2m_ctx, items[i]);
    }
  }
}

/* Check a sequence lambda-method call  recv.filter/map/reduce/count(...).
   R is the N_CALL node, SR the (already classified) receiver, ARG_LIST the
   call arguments.  Returns the call's result type, or NULL after reporting
   an error. */
static struct type *check_seq_method_call (c2m_ctx_t c2m_ctx, node_t r, enum seq_method sm,
                                           struct seq_recv *sr, node_t arg_list) {
  static const char *const seq_names[] = {"?", "filter", "map", "reduce", "count", "ToList"};
  check_ctx_t check_ctx = c2m_ctx->check_ctx; /* for the curr_scope macro */
  int nargs;
  node_t arg, cb_node, init_node;
  struct type *res, *acc_type = NULL, *el_type = sr->el_type, *cb_ret;
  struct func_type *cb_ft = NULL;

  get_seq_method (seq_names[sm], &nargs);
  if ((int) NL_LENGTH (arg_list->u.ops) != nargs) {
    error (c2m_ctx, POS (r), "'%s' expects %d argument%s", seq_names[sm], nargs,
           nargs == 1 ? "" : "s");
    return NULL;
  }
  set_type_layout (c2m_ctx, el_type);
  if (sm != SEQM_COUNT && sm != SEQM_TOLIST && (!scalar_type_p (el_type) || el_type->mode == TM_SLICE)) {
    error (c2m_ctx, POS (r),
           "'%s' requires a scalar element type (integer, floating, pointer, String)",
           seq_names[sm]);
    return NULL;
  }
  if (sr->kind == SEQ_RECV_ARR && sr->static_len < 0) {
    error (c2m_ctx, POS (r), "'%s' requires an array with a known constant length",
           seq_names[sm]);
    return NULL;
  }
  /* Check the non-lambda arguments (untyped lambdas are typed below). */
  for (arg = NL_HEAD (arg_list->u.ops); arg != NULL; arg = NL_NEXT (arg))
    if (arg->code != N_LAMBDA && arg->attr == NULL) check (c2m_ctx, arg, r);

  if (sm == SEQM_COUNT) {
    res = create_basic_type (c2m_ctx, get_uint_basic_type (sizeof (mir_size_t)));
    res->pos_node = r;
    return res;
  }
  if (sm == SEQM_TOLIST) {
    /* arr.ToList() / slice.ToList(): instantiate List<el_type> and lower (in gen)
       to a heap allocation followed by the array-view constructor
       List(T* items, int count), supplying the receiver's element base and its
       (statically known, or slice-header) length as the count.  Class receivers
       have no contiguous element storage, so are not supported. */
    node_t type_arg, spec_id, class_def, field, ctor_def;
    struct type *class_type;

    if (sr->kind != SEQ_RECV_ARR && sr->kind != SEQ_RECV_SLICE) {
      error (c2m_ctx, POS (r), "'ToList' is only supported for arrays and slices");
      return NULL;
    }
    if ((type_arg = build_seq_type_arg (c2m_ctx, el_type, POS (r))) == NULL) {
      error (c2m_ctx, POS (r), "'ToList' is not supported for this element type");
      return NULL;
    }
    {
      /* get_or_create_specialization queues a newly-created List<T> onto
         pending_lambdas.  At parse time that queue is drained into the module;
         during the check phase (the `auto lst = arr.ToList();` case, where no
         explicit `List<T>` declaration created the specialization earlier) it
         is not, so materialize it here before the lookup below. */
      parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
      size_t pend_mark = VARR_LENGTH (node_t, pending_lambdas);
      spec_id = get_or_create_specialization (c2m_ctx, "List", 1, &type_arg, POS (r));
      materialize_pending_specs (c2m_ctx, pend_mark);
    }
    if (spec_id == NULL || spec_id->code != N_ID) {
      error (c2m_ctx, POS (r), "'ToList' could not instantiate List<T>");
      return NULL;
    }
    class_def = find_def (c2m_ctx, S_REGULARS, spec_id, curr_scope, NULL);
    if (class_def == NULL || class_def->code != N_CLASS) {
      error (c2m_ctx, POS (r),
             "'ToList' requires the generic List<T> class in scope (#include \"list.h\")");
      return NULL;
    }
    class_type = create_class_type (c2m_ctx, class_def);
    set_class_layout (c2m_ctx, class_def, class_type);

    ctor_def = find_array_view_ctor (c2m_ctx, spec_id->u.s.s, curr_scope, POS (r));
    if (ctor_def == NULL) {
      error (c2m_ctx, POS (r),
             "'ToList' requires List<T>'s array-view constructor List(T*, int)");
      return NULL;
    }
    /* Stash the resolved constructor on the receiver field node's expr: r->attr
       is overwritten by create_expr at seq_method_done, but the field node's
       expr survives unchanged through to gen. */
    field = NL_HEAD (r->u.ops);
    if (field != NULL && field->attr != NULL)
      ((struct expr *) field->attr)->def_node = ctor_def;
    if (ctor_def != NULL && ctor_def->attr != NULL)
      ((decl_t) ctor_def->attr)->used_p = TRUE;

    res = create_type (c2m_ctx, NULL);
    init_type (res);
    res->mode = TM_PTR;
    res->u.ptr_type = class_type;
    set_type_layout (c2m_ctx, res);
    res->pos_node = r;
    return res;
  }
  if (sm == SEQM_REDUCE) {
    struct expr *ie;

    init_node = NL_HEAD (arg_list->u.ops);
    ie = init_node->attr;
    acc_type = create_type (c2m_ctx, ie->type);
    if (acc_type->mode == TM_ARR) acc_type = adjust_type (c2m_ctx, acc_type);
    if (!scalar_type_p (acc_type) || acc_type->mode == TM_SLICE) {
      error (c2m_ctx, POS (init_node), "'reduce' initial value must have a scalar type");
      return NULL;
    }
    set_type_layout (c2m_ctx, acc_type);
    cb_node = NL_EL (arg_list->u.ops, 1);
  } else {
    cb_node = NL_HEAD (arg_list->u.ops);
  }

  if (cb_node->code == N_LAMBDA) {
    struct type *ptypes[2];
    int np = sm == SEQM_REDUCE ? 2 : 1;
    node_t func_def;
    decl_t ld;
    struct expr *le;
    node_t lparams = NL_HEAD (cb_node->u.ops);

    /* Initializer expressions may be checked twice (auto deduction + decl
       creation): reuse the FUNC_DEF instantiated on the first pass. */
    if (cb_node->attr != NULL && (le = cb_node->attr)->def_node != NULL
        && le->def_node->code == N_FUNC_DEF) {
      func_def = le->def_node;
    } else if (lambda_typed_p (lparams)) {
      /* Typed lambda: hoist as static (same as free-standing typed lambdas). */
      if ((func_def = instantiate_typed_lambda (c2m_ctx, cb_node)) == NULL) return NULL;
    } else {
      if (sm == SEQM_REDUCE) {
        ptypes[0] = acc_type;
        ptypes[1] = el_type;
      } else {
        ptypes[0] = el_type;
      }
      if ((func_def = instantiate_lambda (c2m_ctx, cb_node, ptypes, np)) == NULL) return NULL;
      ld = func_def->attr;
      le = create_expr (c2m_ctx, cb_node);
      le->type->mode = TM_PTR;
      le->type->u.ptr_type = ld->decl_spec.type;
      set_type_layout (c2m_ctx, le->type);
      le->def_node = func_def;
      le->u.lvalue_node = NULL;
    }
    ld = func_def->attr;
    cb_ft = ld->decl_spec.type->u.func_type;
  } else {
    struct expr *ce = cb_node->attr;
    struct type *ct = ce != NULL ? ce->type : NULL;
    int want = sm == SEQM_REDUCE ? 2 : 1, have = 0;

    if (ct != NULL && ct->mode == TM_PTR && ct->u.ptr_type->mode == TM_FUNC)
      cb_ft = ct->u.ptr_type->u.func_type;
    else if (ct != NULL && ct->mode == TM_FUNC)
      cb_ft = ct->u.func_type;
    if (cb_ft == NULL) {
      error (c2m_ctx, POS (cb_node), "'%s' argument must be a lambda or a function",
             seq_names[sm]);
      return NULL;
    }
    for (node_t p = NL_HEAD (cb_ft->param_list->u.ops); p != NULL; p = NL_NEXT (p))
      if (p->code == N_SPEC_DECL || p->code == N_TYPE) have++;
    if (have != want) {
      error (c2m_ctx, POS (cb_node), "'%s' callback must take %d typed parameter%s, not %d",
             seq_names[sm], want, want == 1 ? "" : "s", have);
      return NULL;
    }
  }

  cb_ret = cb_ft->ret_type;
  switch (sm) {
  case SEQM_FILTER:
    if (!integer_type_p (cb_ret)) {
      error (c2m_ctx, POS (cb_node), "'filter' predicate must return an integer (0/1)");
      return NULL;
    }
    res = create_type (c2m_ctx, NULL);
    init_type (res);
    res->mode = TM_SLICE;
    res->pos_node = r;
    res->u.ptr_type = create_type (c2m_ctx, el_type);
    set_type_layout (c2m_ctx, res);
    return res;
  case SEQM_MAP:
    if (!scalar_type_p (cb_ret) || cb_ret->mode == TM_SLICE || void_type_p (cb_ret)) {
      error (c2m_ctx, POS (cb_node), "'map' transform must return a scalar value");
      return NULL;
    }
    res = create_type (c2m_ctx, NULL);
    init_type (res);
    res->mode = TM_SLICE;
    res->pos_node = r;
    res->u.ptr_type = create_type (c2m_ctx, cb_ret);
    set_type_layout (c2m_ctx, res);
    return res;
  case SEQM_REDUCE:
    if (void_type_p (cb_ret)) {
      error (c2m_ctx, POS (cb_node), "'reduce' accumulator function must return a value");
      return NULL;
    }
    return acc_type;
  default: return NULL;
  }
}

/* Instantiate (or reuse) the generic List<EL> specialization and return a fresh
   `List<EL>*` type, or NULL (after emitting a diagnostic) when the generic
   List<T> class is not in scope.  This mirrors the List resolution done for
   `arr.ToList()` and is what backs `String.split()`'s List<String>* result. */
static struct type *make_list_ptr_type (c2m_ctx_t c2m_ctx, struct type *el, pos_t pos) {
  check_ctx_t check_ctx = c2m_ctx->check_ctx; /* for the curr_scope macro */
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx; /* for pending_lambdas macro */
  node_t type_arg, spec_id, class_def;
  struct type *class_type, *res;
  size_t pend_mark;

  if ((type_arg = build_seq_type_arg (c2m_ctx, el, pos)) == NULL) {
    error (c2m_ctx, pos, "unsupported List element type");
    return NULL;
  }
  pend_mark = VARR_LENGTH (node_t, pending_lambdas);
  spec_id = get_or_create_specialization (c2m_ctx, "List", 1, &type_arg, pos);
  materialize_pending_specs (c2m_ctx, pend_mark);
  if (spec_id == NULL || spec_id->code != N_ID) {
    error (c2m_ctx, pos, "could not instantiate List<T>");
    return NULL;
  }
  class_def = find_def (c2m_ctx, S_REGULARS, spec_id, curr_scope, NULL);
  if (class_def == NULL || class_def->code != N_CLASS) {
    error (c2m_ctx, pos,
           "this operation requires the generic List<T> class in scope (#include \"list.h\")");
    return NULL;
  }
  class_type = create_class_type (c2m_ctx, class_def);
  set_class_layout (c2m_ctx, class_def, class_type);
  res = create_type (c2m_ctx, NULL);
  init_type (res);
  res->mode = TM_PTR;
  res->u.ptr_type = class_type;
  set_type_layout (c2m_ctx, res);
  return res;
}

    static void check_dict_init_list (c2m_ctx_t c2m_ctx, node_t initializer, node_t context);

    static void check_initializer (c2m_ctx_t c2m_ctx, decl_t member_decl MIR_UNUSED,
                                   struct type **type_ptr, node_t initializer, int const_only_p,
                                   int top_p) {
      struct type *type = *type_ptr;
      struct expr *cexpr;
      node_t literal, des_list, curr_des, init, str, value, size_node, temp;
      mir_llong max_index;
      mir_llong size_val = 0; /* to remove an uninitialized warning */
      size_t mark, len;
      symbol_t sym;
      init_object_t init_object;
      int addr_p = FALSE; /* to remove an uninitialized warning */

      literal = get_compound_literal (initializer, &addr_p);
      if (literal != NULL && !addr_p && initializer->code != N_STR && initializer->code != N_STR16
          && initializer->code != N_STR32) {
        cexpr = initializer->attr;
        check_assignment_types (c2m_ctx, type, NULL, cexpr, initializer);
        initializer = NL_EL (literal->u.ops, 1);
      }
    check_one_value:
      if (initializer->code != N_LIST
          && !(type->mode == TM_ARR
               && init_compatible_string_p (initializer, type->u.arr_type->el_type))) {
        if ((cexpr  = initializer->attr)->const_p || initializer->code == N_STR
            || initializer->code == N_STR16 || initializer->code == N_STR32 || !const_only_p) {
          //TODO handle N_STRING for fstring
          check_assignment_types (c2m_ctx, type, NULL, cexpr, initializer);
        } else {
          setup_const_addr_p (c2m_ctx, initializer);
          if ((cexpr = initializer->attr)->const_addr_p || (literal != NULL && addr_p))
            check_assignment_types (c2m_ctx, type, NULL, cexpr, initializer);
          else
            error (c2m_ctx, POS (initializer),
                   "initializer of non-auto or thread local object"
                   " should be a constant expression or address");
        }
        return;
      }
      init = NL_HEAD (initializer->u.ops);
      if (((str = initializer)->code == N_STR || str->code == N_STR16
           || str->code == N_STR32 /* string or string in parentheses */
           || (init != NULL && init->code == N_INIT && NL_EL (initializer->u.ops, 1) == NULL
               && (des_list = NL_HEAD (init->u.ops))->code == N_LIST
               && NL_HEAD (des_list->u.ops) == NULL && NL_EL (init->u.ops, 1) != NULL
               && ((str = NL_EL (init->u.ops, 1))->code == N_STR || str->code == N_STR16
                   || str->code == N_STR32)))
          && type->mode == TM_ARR && init_compatible_string_p (str, type->u.arr_type->el_type)) {
        len = str->u.s.len;
        if (incomplete_type_p (c2m_ctx, type)) {
          assert (len < MIR_INT_MAX);
          type->u.arr_type->size = new_i_node (c2m_ctx, (long) len, POS (type->u.arr_type->size));
          check (c2m_ctx, type->u.arr_type->size, NULL);
          make_type_complete (c2m_ctx, type);
        } else if (len > (size_t) ((struct expr *) type->u.arr_type->size->attr)->c.i_val + 1) {
          error (c2m_ctx, POS (initializer), "string is too long for array initializer");
        }
        return;
      }
      if (init == NULL) {
        /* TM_DICT: empty {} means "create an empty dict object"; gen handles it
           via gen_dict_create_object() + an empty gen_dict_init_list() pass. */
        if (scalar_type_p (type) && type->mode != TM_DICT)
          error (c2m_ctx, POS (initializer), "empty scalar initializer");
        return;
      }
      if (type->mode == TM_DICT) {
        /* A dict literal { "k": v, ... } is materialised at run time by
           gen_dict_init_list(); it does not take part in the aggregate
           init_object_path walk used for arrays/structs/classes (whose member
           offsets are compile-time constants).  We only need to type-check the
           value expressions here.  Running the generic path for a dict pops the
           dict off init_object_path for the first key and then underflows the
           VARR_TRUNC on the next key ("wrong trunc for init_object_t"). */
        check_dict_init_list (c2m_ctx, initializer, initializer);
        return;
      }
      assert (init->code == N_INIT);
      des_list = NL_HEAD (init->u.ops);
      assert (des_list->code == N_LIST);
      if (type->mode != TM_ARR && type->mode != TM_STRUCT && type->mode != TM_UNION && type->mode != TM_CLASS && type->mode != TM_DICT) {
        if ((temp = NL_NEXT (init)) != NULL) {
          error (c2m_ctx, POS (temp), "excess elements in scalar initializer");
          return;
        }
        if ((temp = NL_HEAD (des_list->u.ops)) != NULL) {
          error (c2m_ctx, POS (temp), "designator in scalar initializer");
          return;
        }
        initializer = NL_NEXT (des_list);
        if (!top_p) {
          error (c2m_ctx, POS (init), "braces around scalar initializer");
          return;
        }
        top_p = FALSE;
        goto check_one_value;
      }
      mark = VARR_LENGTH (init_object_t, init_object_path);
      init_object.container_type = type;
      init_object.field_designator_p = FALSE;
      if (type->mode == TM_ARR) {
        size_val = get_arr_type_size (type);
        init_object.u.curr_index = -1;
      } else {
        init_object.u.curr_member = NULL;
      }
      VARR_PUSH (init_object_t, init_object_path, init_object);
      max_index = -1;
      for (; init != NULL; init = NL_NEXT (init)) {
        assert (init->code == N_INIT);
        des_list = NL_HEAD (init->u.ops);
        value = NL_NEXT (des_list);
        if ((value->code == N_LIST || value->code == N_COMPOUND_LITERAL) && type->mode != TM_ARR
            && type->mode != TM_STRUCT && type->mode != TM_UNION && type->mode != TM_CLASS && type->mode != TM_DICT) {
          error (c2m_ctx, POS (init),
                 value->code == N_LIST ? "braces around scalar initializer"
                                       : "compound literal for scalar initializer");
          break;
        }
        if ((curr_des = NL_HEAD (des_list->u.ops)) == NULL) {
          if (!update_path_and_do (c2m_ctx, TRUE, check_initializer, mark, value, const_only_p,
                                   &max_index, POS (init), "array/struct/union"))
            break;
        } else {
          struct type *curr_type = type;
          mir_llong arr_size_val MIR_UNUSED;
          int first_p = TRUE;

          VARR_TRUNC (init_object_t, init_object_path, mark + 1);
          for (; curr_des != NULL; curr_des = NL_NEXT (curr_des), first_p = FALSE) {
            init_object = VARR_LAST (init_object_t, init_object_path);
            if (first_p) {
              VARR_POP (init_object_t, init_object_path);
            } else {
              if (init_object.container_type->mode == TM_ARR) {
                curr_type = init_object.container_type->u.arr_type->el_type;
              } else {
                assert (init_object.container_type->mode == TM_STRUCT
                        || init_object.container_type->mode == TM_UNION);
                decl_t el_decl = init_object.u.curr_member->attr;
                curr_type = el_decl->decl_spec.type;
              }
            }
            if (curr_des->code == N_FIELD_ID) {
              node_t id = NL_HEAD (curr_des->u.ops);

              if (curr_type->mode == TM_DICT) {
                /* dict key handled later in check_dict_initializer */
              } else if (curr_type->mode != TM_STRUCT && curr_type->mode != TM_UNION && curr_type->mode != TM_CLASS) {
                error (c2m_ctx, POS (curr_des), "field name not in struct, union or class initializer");
              } else if (!symbol_find (c2m_ctx, S_REGULARS, id, curr_type->u.tag_type, &sym)) {
                error (c2m_ctx, POS (curr_des), "unknown field %s in initializer", id->u.s.s);
              } else {
                process_init_field_designator (c2m_ctx, sym.def_node, curr_type);
                if (!update_path_and_do (c2m_ctx, NL_NEXT (curr_des) == NULL, check_initializer, mark,
                                         value, const_only_p, NULL, POS (init), "struct/union"))
                  break;
              }
            } else if (curr_type->mode != TM_ARR) {
              error (c2m_ctx, POS (curr_des), "array index in initializer for non-array");
            } else if (!(cexpr = curr_des->attr)->const_p) {
              error (c2m_ctx, POS (curr_des), "nonconstant array index in initializer");
            } else if (!integer_type_p (cexpr->type)) {
              error (c2m_ctx, POS (curr_des), "array index in initializer not of integer type");
            } else if (incomplete_type_p (c2m_ctx, curr_type) && signed_integer_type_p (cexpr->type)
                       && cexpr->c.i_val < 0) {
              error (c2m_ctx, POS (curr_des),
                     "negative array index in initializer for array without size");
            } else if ((arr_size_val = get_arr_type_size (curr_type)) >= 0
                       && (mir_ullong) arr_size_val <= cexpr->c.u_val) {
              error (c2m_ctx, POS (curr_des), "array index in initializer exceeds array bounds");
            } else {
              init_object.u.curr_index = cexpr->c.i_val - 1; /* previous el */
              init_object.field_designator_p = FALSE;
              init_object.container_type = curr_type;
              VARR_PUSH (init_object_t, init_object_path, init_object);
              if (!update_path_and_do (c2m_ctx, NL_NEXT (curr_des) == NULL, check_initializer, mark,
                                       value, const_only_p, first_p ? &max_index : NULL, POS (init),
                                       "array"))
                break;
            }
          }
        }
      }
      if (type->mode == TM_ARR && incomplete_type_p (c2m_ctx, type) && max_index >= 0) {
        /* Array w/o size: define it.  Copy the type as the incomplete
           type can be shared by declarations with different length
           initializers.  We need only one level of copying as sub-array
           can not have incomplete type with an initializer. */
        struct arr_type *arr_type = reg_malloc (c2m_ctx, sizeof (struct arr_type));

        type = create_type (c2m_ctx, type);
        assert (incomplete_type_p (c2m_ctx, type));
        *arr_type = *type->u.arr_type;
        type->u.arr_type = arr_type;
        size_node = type->u.arr_type->size;
        type->u.arr_type->size
          = (max_index < MIR_INT_MAX    ? new_i_node (c2m_ctx, (long) max_index + 1, POS (size_node))
             : max_index < MIR_LONG_MAX ? new_l_node (c2m_ctx, (long) max_index + 1, POS (size_node))
                                        : new_ll_node (c2m_ctx, max_index + 1, POS (size_node)));
        check (c2m_ctx, type->u.arr_type->size, NULL);
        make_type_complete (c2m_ctx, type);
      }
      VARR_TRUNC (init_object_t, init_object_path, mark);
      *type_ptr = type;
      return;
    }

    static void check_decl_align (c2m_ctx_t c2m_ctx, struct decl_spec *decl_spec) {
      if (decl_spec->align < 0) return;
      if (decl_spec->align < type_align (decl_spec->type))
        error (c2m_ctx, POS (decl_spec->align_node),
               "requested alignment is less than minimum alignment for the type");
    }

    static void init_decl (c2m_ctx_t c2m_ctx, decl_t decl) {
      check_ctx_t check_ctx = c2m_ctx->check_ctx;

      decl->addr_p = FALSE;
      decl->reg_p = decl->asm_p = decl->used_p = FALSE;
      decl->offset = 0;
      decl->flex_extra_size = 0;
      decl->bit_offset = -1;
      decl->param_args_start = decl->param_args_num = 0;
      decl->ctor_call = decl->dtor_call = NULL;
      decl->auto_release_call = NULL;
      decl->auto_defer_p = FALSE;
      decl->unowned_p = FALSE;
      decl->owned_p = FALSE;
      decl->da_ignore_p = FALSE;
      decl->midopt_dead_p = FALSE;
      decl->byref_p = FALSE;
      decl->scope = curr_scope;
      decl->containing_unnamed_anon_struct_union_member = curr_unnamed_anon_struct_union_member;
      decl->u.item = NULL;
      decl->c2m_ctx = c2m_ctx;
    }

    /* ============================================================
       Resource-acquire / auto-defer foundation.

       Recognises initializer patterns that hand the binding ownership of a
       heap resource.  Today only `new T(...)` / `new T{...}` / `new dict(...)`
       (the language-level acquire forms) are recognised.  The framework is
       designed to be extended without touching this file: the upcoming
       ownership pass (see `src/ownership.c`) will register a table of known
       C-runtime acquire/release pairs — `malloc`/`free`, `strdup`/`free`,
       `fopen`/`fclose`, `mmap`/`munmap`, etc. — and this predicate will then
       also return TRUE for `N_CALL` nodes whose callee is in that table.
       That generalisation is what makes RAII work for plain C11 code, not
       just code using `new`/`delete`.

       Used at one site: detection of auto-defer-delete candidates in
       N_SPEC_DECL.  The check pass only *marks* candidates (decl->auto_defer_p);
       it intentionally does NOT synthesize the `defer delete` itself.  The
       ownership pass owns the synthesis decision, because the right rule
       ("defer only if the binding is not later detached, returned, or stored
       into a longer-lived owner") needs flow analysis the check pass doesn't
       have. */
    static int is_resource_acquire (node_t init) {
      if (init == NULL) return FALSE;
      /* Peel off a leading C cast — `(char *)malloc(n)` is the universal
         idiom for typed pointer allocations.  N_CAST is (type, expr), so the
         expression is operand 1.  Recurse so chained casts also unwrap. */
      while (init != NULL && init->code == N_CAST)
        init = NL_EL (init->u.ops, 1);
      if (init == NULL) return FALSE;
      /* Language-level acquire: every `new` form (class, brace-init, dict).
         We deliberately key on the syntactic form, not the type, so that any
         future generic specialisation of `new T(...)` is recognised the same
         way as a primitive one. */
      if (init->code == N_NEW) return TRUE;
      /* C-runtime acquire: a direct call to one of the known malloc-family
         functions (or any other acquire registered in the table below).
         Method-chained results (`new T(...).withX(10)`, or in C terms
         `wrap(malloc(n))`) are intentionally NOT matched here — a chain may
         return any pointer, so until the ownership pass propagates return
         attributes, we stick to a single-call shape. */
      if (init->code == N_CALL) {
        node_t callee = NL_HEAD (init->u.ops);
        if (callee != NULL && callee->code == N_ID && callee->u.s.s != NULL) {
          const char *fn = callee->u.s.s;
          /* Keep this table in lockstep with `release_fn_for_acquire` in
             src/ownership.c — they will be unified into one shared resource
             table when the seed list grows beyond malloc-family.  See the
             header comments in src/ownership.c for the planned design. */
          if (strcmp (fn, "malloc")  == 0) return TRUE;
          if (strcmp (fn, "calloc")  == 0) return TRUE;
          if (strcmp (fn, "realloc") == 0) return TRUE;
          if (strcmp (fn, "strdup")  == 0) return TRUE;
          if (strcmp (fn, "strndup") == 0) return TRUE;
        }
      }
      return FALSE;
    }

    static void create_decl (c2m_ctx_t c2m_ctx, node_t scope, node_t decl_node,
                             struct decl_spec decl_spec, node_t initializer, int param_p) {
      check_ctx_t check_ctx = c2m_ctx->check_ctx;
      int func_def_p = decl_node->code == N_FUNC_DEF, func_p = FALSE;
      node_t id = NULL; /* to remove an uninitialized warning */
      node_t list_head, declarator;
      struct type *type;
      decl_t decl = reg_malloc (c2m_ctx, sizeof (struct decl));
      int pre_da_ignore = 0;

      assert (decl_node->code == N_MEMBER || decl_node->code == N_SPEC_DECL
              || decl_node->code == N_FUNC_DEF);
      /* Recover function-level da_ignore stashed by the parser (trailing
         attrs on the definition) before we overwrite ->attr with decl. */
      if (func_def_p && decl_node->attr != NULL
          && decl_node->attr != (void *) ((intptr_t) -1)) {
        if (decl_node->attr == PRECHECK_DA_IGNORE)
          pre_da_ignore = 1;
        else {
          node_t pre = (node_t) decl_node->attr;
          if (pre->code == N_LIST || pre->code == N_ATTR)
            pre_da_ignore = attr_list_has_da_ignore (pre);
        }
      }
      init_decl (c2m_ctx, decl);
      decl->scope = scope;
      decl->decl_spec = decl_spec;
      decl->da_ignore_p = pre_da_ignore;
      /* Binding-level attrs on N_SPEC_DECL (and members): same name. */
      if (!decl->da_ignore_p && decl_node->code == N_SPEC_DECL) {
        node_t battrs = SPEC_DECL_ATTRS (decl_node);
        if (battrs != NULL) decl->da_ignore_p = attr_list_has_da_ignore (battrs);
      }
      decl_node->attr = decl;
      declarator = NL_EL (decl_node->u.ops, 1);
      if (declarator->code == N_IGNORE) {
        assert (decl_node->code == N_MEMBER);
        decl->decl_spec.linkage = N_IGNORE;
      } else {
        assert (declarator->code == N_DECL);
        type = check_declarator (c2m_ctx, declarator, func_def_p);
        decl->decl_spec.type = append_type (type, decl->decl_spec.type);
      }
      check_type (c2m_ctx, decl->decl_spec.type, 0, func_def_p);
      if (declarator->code == N_DECL) {
        id = NL_HEAD (declarator->u.ops);
        list_head = NL_HEAD (NL_NEXT (id)->u.ops);
        func_p = !param_p && list_head && list_head->code == N_FUNC;
        decl->decl_spec.linkage = get_id_linkage (c2m_ctx, func_p, id, scope, decl->decl_spec);
      }
      if (decl_node->code != N_MEMBER) {
        set_type_layout (c2m_ctx, decl->decl_spec.type);
        check_decl_align (c2m_ctx, &decl->decl_spec);
        /* Stack-frame allocation is only for locals/params.  Do not push
           N_FUNC_DEF: free functions live in top_scope (already excluded),
           but class methods are create_decl'd into the class tag scope.  Putting
           their TM_FUNC (size 8) onto the FDA shifted every monomorphized method
           block's offsets by 8 while frame size stayed the local span only — so
           a BLK-by-value class param at frame+8 with size>=16 overflowed the
           alloca and clobbered the caller's stack (List.Copy/Filter/Where of
           List<LapSample> nulling the receiver). */
        if (!decl->decl_spec.typedef_p && decl_node->code != N_FUNC_DEF
            && decl->scope != top_scope && decl->scope->code != N_FUNC
            && decl->scope->code != N_CLASS && decl->scope->code != N_STRUCT
            && decl->scope->code != N_UNION)
          VARR_PUSH (decl_t, func_decls_for_allocation, decl);
      }
      if (declarator->code == N_DECL) {
        def_symbol (c2m_ctx, S_REGULARS, id, scope, decl_node, decl->decl_spec.linkage);
        /* A block-scope `extern` declaration also refers to the file-scope
           identifier, so it is mirrored into top_scope.

           This must NOT apply to ordinary class/struct/union member methods: a
           class method has external linkage but is intentionally scoped to its
           class (decl_scope == class node) so that a bare identifier never
           resolves to it.  Mirroring it to top_scope would let a plain call like
           `close(fd)` (meant for POSIX close) resolve to the method `File::close`
           and crash.  Instance/static methods are reached only via `obj.method`,
           `Class.method`, or the duck-typed protocol lookup, all of which search
           the class scope directly.

           Constructors and destructors are the exception: `new T(...)` and
           `delete obj` resolve `__ctor_T` / `__dtor_T` through the lexical scope
           chain (curr_scope -> ... -> top_scope), so those synthesized,
           prefix-namespaced symbols must still be mirrored to top_scope. */
        if (scope != top_scope && decl->decl_spec.linkage == N_EXTERN) {
          int member_scope_p = (scope->code == N_CLASS || scope->code == N_STRUCT
                                || scope->code == N_UNION);
          int ctor_dtor_p = (id->code == N_ID && id->u.s.s != NULL
                             && (strncmp (id->u.s.s, "__ctor_", 7) == 0
                                 || strncmp (id->u.s.s, "__dtor_", 7) == 0));
          if (!member_scope_p || ctor_dtor_p)
            def_symbol (c2m_ctx, S_REGULARS, id, top_scope, decl_node, N_EXTERN);
        }
        if (func_p && decl->decl_spec.thread_local_p) {
          error (c2m_ctx, POS (id), "thread local function declaration");
          if (c2m_options->message_file != NULL) {
            if (id->code != N_IGNORE) fprintf (c2m_options->message_file, " of %s", id->u.s.s);
            fprintf (c2m_options->message_file, "\n");
          }
        }
      }
      /* RAII for automatic (stack) class objects.  A local variable of a class
         type that has a constructor and/or destructor runs its ctor when it
         comes into scope and its dtor at scope exit (no free).  Synthesize the
         `var.__ctor_C()` / `var.__dtor_C()` method calls here (resolved through
         the normal member-call machinery) and stash them on the decl; gen emits
         the ctor at the declaration point and registers the dtor with the defer
         machinery so it runs at every scope exit, in reverse declaration order.
         Only plain class objects qualify (not pointers, params, members,
         statics, or `new`-initialized declarations).

         Stack-constructor syntax `C c = C(args);` is handled here too: the
         initializer is a call whose callee is the class name.  We lower it to
         the same in-place `c.__ctor_C(args)` method call (constructing directly
         into the variable's storage -- no temporary, no by-value return) and
         then neutralize the initializer so gen takes the RAII ctor path.

         Classes with a user destructor are move-only for init/assign: a plain
         copy would shallow-copy the object and double-free on both dtors.  Allow
         only default construction, ClassName(args), or `move other`. */
      if (decl_node->code == N_SPEC_DECL && declarator->code == N_DECL && id != NULL
          && id->code == N_ID && scope != NULL && scope->code == N_BLOCK && !param_p
          && !decl->decl_spec.typedef_p && !decl->decl_spec.static_p
          && !decl->decl_spec.extern_p && !decl->decl_spec.thread_local_p
          && decl->decl_spec.type->mode == TM_CLASS && decl->decl_spec.type->u.tag_type != NULL) {
        node_t tag = decl->decl_spec.type->u.tag_type;
        node_t cid = NL_HEAD (tag->u.ops);
        /* A constructor-call initializer:  `C c = C(args);`  (callee is the
           class name -- matching this variable's class tag). */
        node_t ctor_init_callee
          = (initializer != NULL && initializer->code == N_CALL)
              ? NL_HEAD (initializer->u.ops) : NULL;
        int ctor_init_p = (ctor_init_callee != NULL && ctor_init_callee->code == N_ID
                           && cid != NULL && cid->code == N_ID
                           && strcmp (ctor_init_callee->u.s.s, cid->u.s.s) == 0);
        int move_init_p = (initializer != NULL && initializer->code == N_MOVE);
        /* Function/method call or statement-expression (capturing HOF desugar)
           returning a collection by value is a prvalue ownership transfer
           (RAII bind), not a shallow alias of an lvalue. */
        int prvalue_init_p = (initializer != NULL
                              && (initializer->code == N_CALL
                                  || initializer->code == N_STMTEXPR)
                              && !ctor_init_p);
        int move_only = class_is_move_only_collection_p (c2m_ctx, decl->decl_spec.type);
        char nm[320];
        unsigned saved_errs;
        node_t cdtor_id, call;
        symbol_t ctor_sym;

        if (cid != NULL && cid->code == N_ID) {
          /* Ban shallow copy-init of List/Map/Set (owning buffer).  Allow
             default/ctor init, `move` from an lvalue, and prvalue bind from a
             call that returns List/Map/Set by value (the first-class idiom). */
          if (move_only && initializer != NULL && initializer->code != N_IGNORE
              && !ctor_init_p && !move_init_p && !prvalue_init_p) {
            error (c2m_ctx, POS (id),
                   "cannot copy-initialize collection '%s' "
                   "(shallow copy would double-free); use default construction, "
                   "Name(...), `move`, or a by-value return",
                   cid->u.s.s);
          }

          snprintf (nm, sizeof (nm), "__ctor_%s", cid->u.s.s);
          if (ctor_init_p) {
            /* Reuse the supplied argument list for the in-place ctor call, and
               neutralize the initializer so the rest of create_decl/gen treat
               this declaration as uninitialized (the ctor does the init). */
            node_t args = NL_NEXT (ctor_init_callee);
            if (args != NULL) NL_REMOVE (initializer->u.ops, args);
            else args = new_node (c2m_ctx, N_LIST);
            call = new_pos_node2 (c2m_ctx, N_CALL, POS (id),
                                  new_pos_node2 (c2m_ctx, N_FIELD, POS (id),
                                                 copy_node (c2m_ctx, id),
                                                 build_id (c2m_ctx, nm, POS (id))),
                                  args);
            saved_errs = n_errors;
            check (c2m_ctx, call, decl_node);
            if (n_errors == saved_errs) decl->ctor_call = call;
            /* Turn the original initializer into a no-op. */
            initializer->code = N_IGNORE;
          } else if (initializer == NULL || initializer->code == N_IGNORE) {
            int has_default_ctor = FALSE;
            /* Constructor: auto-invoke only when a zero-argument (default) ctor
               exists, so a class with only parametrized ctors is not flagged
               with a spurious "too few arguments" error for a plain `C c;`. */
            cdtor_id = build_id (c2m_ctx, nm, POS (id));
            if (find_overload_sym (c2m_ctx, cdtor_id, scope, &ctor_sym)) {
              for (size_t ci = 0; ci < VARR_LENGTH (node_t, ctor_sym.defs); ci++) {
                node_t cand = VARR_GET (node_t, ctor_sym.defs, ci);
                decl_t cd;
                struct func_type *cft;
                node_t cp;
                if (cand == NULL || cand->code != N_FUNC_DEF) continue;
                cd = cand->attr;
                if (cd == NULL || cd->decl_spec.type == NULL
                    || cd->decl_spec.type->mode != TM_FUNC) continue;
                cft = cd->decl_spec.type->u.func_type;
                cp = NL_HEAD (cft->param_list->u.ops);
                if (cp != NULL) cp = NL_NEXT (cp); /* skip implicit 'this' */
                if (cp == NULL) { has_default_ctor = TRUE; break; } /* no user params */
              }
            }
            if (has_default_ctor) {
              call = new_pos_node2 (c2m_ctx, N_CALL, POS (id),
                                    new_pos_node2 (c2m_ctx, N_FIELD, POS (id),
                                                   copy_node (c2m_ctx, id),
                                                   build_id (c2m_ctx, nm, POS (id))),
                                    new_node (c2m_ctx, N_LIST));
              saved_errs = n_errors;
              check (c2m_ctx, call, decl_node);
              if (n_errors == saved_errs) decl->ctor_call = call;
            }
          }
          /* Always register dtor for stack class values (ctor-init, default,
             or move-init).  Copy-init of dtor classes is rejected above. */
          snprintf (nm, sizeof (nm), "__dtor_%s", cid->u.s.s);
          cdtor_id = build_id (c2m_ctx, nm, POS (id));
          if (find_def (c2m_ctx, S_REGULARS, cdtor_id, scope, NULL) != NULL
              || find_def (c2m_ctx, S_REGULARS, cdtor_id, top_scope, NULL) != NULL) {
            call = new_pos_node2 (c2m_ctx, N_CALL, POS (id),
                                  new_pos_node2 (c2m_ctx, N_FIELD, POS (id),
                                                 copy_node (c2m_ctx, id),
                                                 build_id (c2m_ctx, nm, POS (id))),
                                  new_node (c2m_ctx, N_LIST));
            saved_errs = n_errors;
            check (c2m_ctx, call, decl_node);
            if (n_errors == saved_errs) {
              decl->dtor_call = call;
              /* NOTE: stack-object (RAII) destructors are deliberately NOT
                 registered on the exception-path defer shadow stack
                 (cy__defer_stack, cyexc.h): the thunk arg is captured by
                 value at registration, and a stack address is a dead-frame
                 pointer once a cross-function longjmp has unwound past it.
                 See cy-validate/SHORTCOMINGS.md (defer/throw gotcha). */
            }
          }
        }
      }
      /* P5: stack arrays of dtor-bearing class elements (`Pt arr[2];`) leak
         their elements at scope exit (arrays never got the RAII treatment
         above, which requires a plain TM_CLASS).  Synthesize per-element
         `arr[i].__dtor_T()` calls (reverse order, like C++) as the decl's
         dtor_call so the defer machinery runs them at every scope exit.
         Constant bound only — VLAs of class elements are not destroyed. */
      if (decl_node->code == N_SPEC_DECL && declarator->code == N_DECL && id != NULL
          && id->code == N_ID && scope != NULL && scope->code == N_BLOCK && !param_p
          && !decl->decl_spec.typedef_p && !decl->decl_spec.static_p
          && !decl->decl_spec.extern_p && !decl->decl_spec.thread_local_p
          && decl->decl_spec.type->mode == TM_ARR
          && decl->decl_spec.type->u.arr_type != NULL
          && decl->decl_spec.type->u.arr_type->el_type != NULL
          && decl->decl_spec.type->u.arr_type->el_type->mode == TM_CLASS
          && decl->decl_spec.type->u.arr_type->el_type->u.tag_type != NULL) {
        struct arr_type *at = decl->decl_spec.type->u.arr_type;
        struct expr *asz = at->size != NULL ? at->size->attr : NULL;
        node_t etag = at->el_type->u.tag_type;
        node_t ecid = NL_HEAD (etag->u.ops);
        long long anel = (asz != NULL && asz->const_p) ? asz->c.i_val : 0;
        char dnm[320];
        node_t arr_dtor_id;
        if (ecid != NULL && ecid->code == N_ID && anel > 0 && anel <= 4096) {
          snprintf (dnm, sizeof (dnm), "__dtor_%s", ecid->u.s.s);
          arr_dtor_id = build_id (c2m_ctx, dnm, POS (id));
          if (find_def (c2m_ctx, S_REGULARS, arr_dtor_id, scope, NULL) != NULL
              || find_def (c2m_ctx, S_REGULARS, arr_dtor_id, top_scope, NULL) != NULL) {
            node_t blk = new_node (c2m_ctx, N_LIST);
            int arr_ok = TRUE;
            unsigned arr_saved;
            for (long long ai = anel - 1; ai >= 0; ai--) {
              node_t dc
                = new_pos_node2 (c2m_ctx, N_CALL, POS (id),
                                 new_pos_node2 (c2m_ctx, N_FIELD, POS (id),
                                                new_pos_node2 (c2m_ctx, N_IND, POS (id),
                                                               copy_node (c2m_ctx, id),
                                                               new_i_node (c2m_ctx, (long) ai,
                                                                           POS (id))),
                                                build_id (c2m_ctx, dnm, POS (id))),
                                 new_node (c2m_ctx, N_LIST));
              arr_saved = n_errors;
              check (c2m_ctx, dc, decl_node);
              if (n_errors != arr_saved) { arr_ok = FALSE; break; }
              op_append (c2m_ctx, blk,
                         new_pos_node2 (c2m_ctx, N_EXPR, POS (id),
                                        new_node (c2m_ctx, N_LIST), dc));
            }
            if (arr_ok) decl->dtor_call = blk;
          }
        }
      }
      if (initializer == NULL || initializer->code == N_IGNORE) return;
      if (incomplete_type_p (c2m_ctx, decl->decl_spec.type)
          && (decl->decl_spec.type->mode != TM_ARR
              || incomplete_type_p (c2m_ctx, decl->decl_spec.type->u.arr_type->el_type))) {
        if (decl->decl_spec.type->mode == TM_ARR
            && decl->decl_spec.type->u.arr_type->el_type->mode == TM_ARR)
          error (c2m_ctx, POS (initializer), "initialization of incomplete sub-array");
        else
          error (c2m_ctx, POS (initializer), "initialization of incomplete type variable");
        return;
      }
      if (decl->decl_spec.linkage == N_EXTERN && scope != top_scope) {
        error (c2m_ctx, POS (initializer), "initialization of %s in block scope with external linkage",
               id->u.s.s);
        return;
      }
      /* classyc extension: a class with a trailing FLEXIBLE array member
         (e.g. `String arr[];') may be brace-initialized with N elements.
         check_initializer completes the (shared) member's array type to N
         elements and writes it back into the member decl - but the containing
         class' raw_size was cached at class-definition time counting the
         member as a single element.  Find such a flexible member up front so
         we can size this declaration's storage and then restore the member to
         its incomplete form (otherwise a later instance with a different
         length would wrongly report "excess elements"). */
      node_t flex_member = NULL;
      struct type *saved_flex_type = NULL;
      if (decl->decl_spec.type->mode == TM_CLASS && decl->decl_spec.type->u.tag_type != NULL) {
        node_t dl = TAG_MEMBER_LIST (decl->decl_spec.type->u.tag_type);
        node_t last_data = NULL;
        if (dl != NULL && dl->code != N_IGNORE)
          for (node_t m = NL_HEAD (dl->u.ops); m != NULL; m = NL_NEXT (m))
            if (m->code == N_MEMBER && m->attr != NULL) last_data = m;
        if (last_data != NULL) {
          struct type *ft = ((decl_t) last_data->attr)->decl_spec.type;
          if (ft != NULL && ft->mode == TM_ARR && ft->u.arr_type->size->code == N_IGNORE) {
            flex_member = last_data;
            saved_flex_type = ft;
          }
        }
      }
      check (c2m_ctx, initializer, decl_node);
      check_initializer (c2m_ctx, NULL, &decl->decl_spec.type, initializer,
                         decl->decl_spec.linkage == N_STATIC || decl->decl_spec.linkage == N_EXTERN
                           || decl->decl_spec.thread_local_p || decl->decl_spec.static_p,
                         TRUE);
      if (flex_member != NULL) {
        /* Reserve extra stack bytes for the elements beyond what the cached
           class layout accounts for, so the initializer does not overrun the
           frame and clobber callee-saved registers / adjacent variables. */
        struct type *ft = ((decl_t) flex_member->attr)->decl_spec.type;
        if (ft != NULL && ft->mode == TM_ARR) {
          mir_size_t required = ((decl_t) flex_member->attr)->offset + raw_type_size (c2m_ctx, ft);
          mir_size_t cls_raw = raw_type_size (c2m_ctx, decl->decl_spec.type);
          if (required > cls_raw) decl->flex_extra_size = required - cls_raw;
        }
        /* Restore the shared member to its incomplete (flexible) form. */
        ((decl_t) flex_member->attr)->decl_spec.type = saved_flex_type;
      }
    }

    static struct type *adjust_type (c2m_ctx_t c2m_ctx, struct type *type) {
      struct type *res;

      if (type->mode != TM_ARR && type->mode != TM_FUNC) return type;
      res = create_type (c2m_ctx, NULL);
      res->mode = TM_PTR;
      res->pos_node = type->pos_node;
      if (type->mode == TM_FUNC) {
        res->func_type_before_adjustment_p = TRUE;
        res->u.ptr_type = type;
      } else {
        res->arr_type = type;
        res->u.ptr_type = type->u.arr_type->el_type;
        res->type_qual = type->u.arr_type->ind_type_qual;
      }
      set_type_layout (c2m_ctx, res);
      return res;
    }

    static void process_unop (c2m_ctx_t c2m_ctx, node_t r, node_t *op, struct expr **e, struct type **t,
                              node_t context) {
      *op = NL_HEAD (r->u.ops);
      check (c2m_ctx, *op, context);
      *e = (*op)->attr;
      *t = (*e)->type;
    }

    static void process_bin_ops (c2m_ctx_t c2m_ctx, node_t r, node_t *op1, node_t *op2,
                                 struct expr **e1, struct expr **e2, struct type **t1, struct type **t2,
                                 node_t context) {
      *op1 = NL_HEAD (r->u.ops);
      *op2 = NL_NEXT (*op1);
      check (c2m_ctx, *op1, context);
      check (c2m_ctx, *op2, context);
      *e1 = (*op1)->attr;
      *e2 = (*op2)->attr;
      *t1 = (*e1)->type;
      *t2 = (*e2)->type;
    }

    static void process_type_bin_ops (c2m_ctx_t c2m_ctx, node_t r, node_t *op1, node_t *op2,
                                      struct expr **e2, struct type **t2, node_t context) {
      *op1 = NL_HEAD (r->u.ops);
      *op2 = NL_NEXT (*op1);
      check (c2m_ctx, *op1, context);
      check (c2m_ctx, *op2, context);
      *e2 = (*op2)->attr;
      *t2 = (*e2)->type;
    }

    static struct expr *create_expr (c2m_ctx_t c2m_ctx, node_t r) {
      struct expr *e = reg_malloc (c2m_ctx, sizeof (struct expr));

      /* reg_malloc is not zero-fill: any field we do not set here is garbage.
         def_node especially must be NULL — gen N_IND treats a non-NULL def_node
         as a stashed Get() method for class subscript sugar, so an uninit pointer
         can crash later when indexing a T* (e.g. List<Point>::data[i]). */
      r->attr = e;
      e->type = create_type (c2m_ctx, NULL);
      e->type2 = NULL;
      e->type->pos_node = r;
      e->u.lvalue_node = NULL;
      e->def_node = NULL;
      e->const_p = e->const_addr_p = e->builtin_call_p = FALSE;
      e->elide_oob_p = FALSE;
      /* hoist_call_p MUST be cleared: reg_malloc garbage here randomly marks
         calls as "loop-invariant pure", making gen reuse a stale pre-header
         value at a loop back-edge (infinite loop — gcc/20010129-1.c). */
      e->hoist_call_p = FALSE;
      e->elide_div0_p = FALSE;
      e->elide_shift_p = FALSE;
      e->elide_div_ovf_p = FALSE;
      e->bind_p = e->lenient_p = FALSE;
      e->own_deref_class = DEREF_GUARD_DEFAULT;
      e->mut_sub_p = 0;
      e->c.i_val = 0;
      e->call_proto_item = NULL;
      return e;
    }

    static struct expr *create_basic_type_expr (c2m_ctx_t c2m_ctx, node_t r, enum basic_type bt) {
      struct expr *e = create_expr (c2m_ctx, r);

      e->type->mode = TM_BASIC;
      e->type->u.basic_type = bt;
      return e;
    }

    static void get_int_node (c2m_ctx_t c2m_ctx, node_t *op, struct expr **e, struct type **t,
                              mir_size_t i) {
      check_ctx_t check_ctx = c2m_ctx->check_ctx;

      if (i == 1) {
        *op = n_i1_node;
      } else {
        *op = new_i_node (c2m_ctx, (long) i, no_pos);
        check (c2m_ctx, *op, NULL);
      }
      *e = (*op)->attr;
      *t = (*e)->type;
      init_type (*t);
      (*e)->type->mode = TM_BASIC;
      (*e)->type->u.basic_type = TP_INT;
      (*e)->c.i_val = i;  // ???
    }

    // Get an item from an array
    // TODO: Use the NL_ macros
    static node_t get_array_member_id(c2m_ctx_t c2m_ctx, node_t ind) {
        node_t id_node, i_node, spec_node;
        node_t list_node, init_node;
        struct expr *e;
        long count=0;

        if(ind->code != N_IND) {
            error(c2m_ctx, no_pos, "get_array_member not called with N_IND\n");
            return NULL;
        }

        if(ind->u.ops.tail && ind->u.ops.head &&
                ind->u.ops.head->code == N_ID &&
                ind->u.ops.tail->code == N_I) {
            id_node = ind->u.ops.head; // N_ID (ID node of array)
            i_node = ind->u.ops.tail; // N_I integer (array count offset)
        } else {
            error(c2m_ctx, no_pos, "get_array_member Invalid array (head->N_ID or tail->N_I not found)");
            return NULL;
        }
        // Get the array index from the N_I int node;
        count = i_node->u.l;
        //debug(c2m_ctx, no_pos, "get_array_member: array index = %lu", count);
        // Expression of ind node
        spec_node = ((struct expr *)(id_node->attr))->u.lvalue_node;
        // List node from expression -> lvalue_node->tail
        list_node = spec_node->u.ops.tail;
        if(list_node->code != N_LIST) {
            error(c2m_ctx, no_pos, "get_array_member Invalid expression, N_LIST not found)");
            return NULL;
        }
        // Get first N_INIT in the array
        init_node = list_node->u.ops.head;
        // Iterate list to item
        while(count-- && init_node->op_link.next)
            init_node = init_node->op_link.next;
        if(count >= 0) {
            error(c2m_ctx, no_pos, "get_array_member ERROR --- array would overflow by %d", count+1);
        }

        // Should be the dara item in the list
        return init_node->u.ops.tail;

        return NULL;
    }

    static struct expr *check_assign_op (c2m_ctx_t c2m_ctx, node_t r, struct expr *e1, struct expr *e2,
                                         struct type *t1, struct type *t2) {
      struct expr *e = NULL;
      struct expr *te;
      struct type t, *tt;

      switch (r->code) {
      case N_AND:
      case N_OR:
      case N_XOR:
      case N_AND_ASSIGN:
      case N_OR_ASSIGN:
      case N_XOR_ASSIGN:
        e = create_expr (c2m_ctx, r);
        e->type->mode = TM_BASIC;
        e->type->u.basic_type = TP_INT;
        if (!integer_type_p (t1) || !integer_type_p (t2)) {
          error (c2m_ctx, POS (r), "bitwise operation operands should be of an integer type");
        } else {
          t = arithmetic_conversion (t1, t2);
          e->type->u.basic_type = t.u.basic_type;
          if (e1->const_p && e2->const_p) {
            convert_value (e1, &t);
            convert_value (e2, &t);
            e->const_p = TRUE;
            if (signed_integer_type_p (&t))
              e->c.i_val = (r->code == N_AND  ? e1->c.i_val & e2->c.i_val
                            : r->code == N_OR ? e1->c.i_val | e2->c.i_val
                                              : e1->c.i_val ^ e2->c.i_val);
            else
              e->c.u_val = (r->code == N_AND  ? e1->c.u_val & e2->c.u_val
                            : r->code == N_OR ? e1->c.u_val | e2->c.u_val
                                              : e1->c.u_val ^ e2->c.u_val);
          }
        }
        break;
      case N_LSH:
      case N_RSH:
      case N_LSH_ASSIGN:
      case N_RSH_ASSIGN:
        e = create_expr (c2m_ctx, r);
        e->type->mode = TM_BASIC;
        e->type->u.basic_type = TP_INT;
        if (!integer_type_p (t1) || !integer_type_p (t2)) {
          error (c2m_ctx, POS (r), "shift operands should be of an integer type");
        } else {
          t = integer_promotion (t1);
          e->type->u.basic_type = t.u.basic_type;
          if (e1->const_p && e2->const_p) {
            struct type rt = integer_promotion (t2);

            convert_value (e1, &t);
            convert_value (e2, &rt);
            e->const_p = TRUE;
            if (signed_integer_type_p (&t)) {
              if (signed_integer_type_p (&rt))
                e->c.i_val = r->code == N_LSH ? e1->c.i_val << e2->c.i_val : e1->c.i_val >> e2->c.i_val;
              else
                e->c.i_val = r->code == N_LSH ? e1->c.i_val << e2->c.u_val : e1->c.i_val >> e2->c.u_val;
            } else if (signed_integer_type_p (&rt)) {
              e->c.u_val = r->code == N_LSH ? e1->c.u_val << e2->c.i_val : e1->c.u_val >> e2->c.i_val;
            } else {
              e->c.u_val = r->code == N_LSH ? e1->c.u_val << e2->c.u_val : e1->c.u_val >> e2->c.u_val;
            }
          }
        }
        break;
      case N_INC:
      case N_DEC:
      case N_POST_INC:
      case N_POST_DEC:
      case N_ADD:
      case N_SUB:
      case N_ADD_ASSIGN:
      case N_SUB_ASSIGN: {
        mir_size_t size;
        int add_p
          = (r->code == N_ADD || r->code == N_ADD_ASSIGN || r->code == N_INC || r->code == N_POST_INC);

        e = create_expr (c2m_ctx, r);
        e->type->mode = TM_BASIC;
        e->type->u.basic_type = TP_INT;

        if (arithmetic_type_p (t1) && arithmetic_type_p (t2)) {
          t = arithmetic_conversion (t1, t2);
          e->type->u.basic_type = t.u.basic_type;
          if (e1->const_p && e2->const_p) {
            e->const_p = TRUE;
            convert_value (e1, &t);
            convert_value (e2, &t);
            if (floating_type_p (&t))
              e->c.d_val = (add_p ? e1->c.d_val + e2->c.d_val : e1->c.d_val - e2->c.d_val);
            else if (signed_integer_type_p (&t))
              e->c.i_val = (add_p ? e1->c.i_val + e2->c.i_val : e1->c.i_val - e2->c.i_val);
            else
              e->c.u_val = (add_p ? e1->c.u_val + e2->c.u_val : e1->c.u_val - e2->c.u_val);
          }
        } else if (add_p) {
          if (t2->mode == TM_PTR) {
            SWAP (t1, t2, tt);
            SWAP (e1, e2, te);
          }
          if (t1->mode != TM_PTR || !integer_type_p (t2)) {
            error (c2m_ctx, POS (r), "invalid operand types of +");
          } else if (incomplete_type_p (c2m_ctx, t1->u.ptr_type)) {
            error (c2m_ctx, POS (r), "pointer to incomplete type as an operand of +");
          } else {
            *e->type = *t1;
            if (e1->const_p && e2->const_p) {
              size = type_size (c2m_ctx, t1->u.ptr_type);
              e->const_p = TRUE;
              e->c.u_val = (signed_integer_type_p (t2) ? e1->c.u_val + e2->c.i_val * size
                                                       : e1->c.u_val + e2->c.u_val * size);
            }
          }
        } else if (t1->mode == TM_PTR && integer_type_p (t2)) {
          if (incomplete_type_p (c2m_ctx, t1->u.ptr_type)) {
            error (c2m_ctx, POS (r), "pointer to incomplete type as an operand of -");
          } else {
            *e->type = *t1;
            if (e1->const_p && e2->const_p) {
              size = type_size (c2m_ctx, t1->u.ptr_type);
              e->const_p = TRUE;
              e->c.u_val = (signed_integer_type_p (t2) ? e1->c.u_val - e2->c.i_val * size
                                                       : e1->c.u_val - e2->c.u_val * size);
            }
          }
        } else if (t1->mode == TM_PTR && t2->mode == TM_PTR && compatible_types_p (t1, t2, TRUE)) {
          if (incomplete_type_p (c2m_ctx, t1->u.ptr_type)
              && incomplete_type_p (c2m_ctx, t2->u.ptr_type)) {
            error (c2m_ctx, POS (r), "pointer to incomplete type as an operand of -");
          } else if (t1->u.ptr_type->type_qual.atomic_p || t2->u.ptr_type->type_qual.atomic_p) {
            error (c2m_ctx, POS (r), "pointer to atomic type as an operand of -");
          } else {
            e->type->mode = TM_BASIC;
            e->type->u.basic_type = get_int_basic_type (sizeof (mir_ptrdiff_t));
            set_type_layout (c2m_ctx, e->type);
            if (e1->const_p && e2->const_p) {
              size = type_size (c2m_ctx, t1->u.ptr_type);
              e->const_p = TRUE;
              e->c.i_val
                = (e1->c.u_val > e2->c.u_val ? (mir_ptrdiff_t) ((e1->c.u_val - e2->c.u_val) / size)
                                             : -(mir_ptrdiff_t) ((e2->c.u_val - e1->c.u_val) / size));
            }
          }
        } else {
          error (c2m_ctx, POS (r), "invalid operand types of -");
        }
        break;
      }
      case N_MUL:
      case N_DIV:
      case N_MOD:
      case N_MUL_ASSIGN:
      case N_DIV_ASSIGN:
      case N_MOD_ASSIGN:
        e = create_expr (c2m_ctx, r);
        e->type->mode = TM_BASIC;
        e->type->u.basic_type = TP_INT;
        if (r->code == N_MOD && (!integer_type_p (t1) || !integer_type_p (t2))) {
          error (c2m_ctx, POS (r), "invalid operand types of %%");
        } else if (r->code != N_MOD && (!arithmetic_type_p (t1) || !arithmetic_type_p (t2))) {
          error (c2m_ctx, POS (r), "invalid operand types of %s", r->code == N_MUL ? "*" : "/");
        } else {
          t = arithmetic_conversion (t1, t2);
          e->type->u.basic_type = t.u.basic_type;
          if (e1->const_p && e2->const_p) {
            e->const_p = TRUE;
            convert_value (e1, &t);
            convert_value (e2, &t);
            if (r->code == N_MUL) {
              if (floating_type_p (&t))
                e->c.d_val = e1->c.d_val * e2->c.d_val;
              else if (signed_integer_type_p (&t))
                e->c.i_val = e1->c.i_val * e2->c.i_val;
              else
                e->c.u_val = e1->c.u_val * e2->c.u_val;
            } else if ((floating_type_p (&t) && e1->c.d_val == 0.0 && e2->c.d_val == 0.0)
                       || (signed_integer_type_p (&t) && e2->c.i_val == 0)
                       || (integer_type_p (&t) && !signed_integer_type_p (&t) && e2->c.u_val == 0)) {
              if (floating_type_p (&t)) {
                e->c.d_val = nanl (""); /* Use NaN */
              } else {
                if (signed_integer_type_p (&t))
                  e->c.i_val = 0;
                else
                  e->c.u_val = 0;
                error (c2m_ctx, POS (r), "Division by zero");
              }
            } else if (r->code != N_MOD && floating_type_p (&t)) {
              e->c.d_val = e1->c.d_val / e2->c.d_val;
            } else if (signed_integer_type_p (&t)) {  // ??? zero
              e->c.i_val = r->code == N_DIV ? e1->c.i_val / e2->c.i_val : e1->c.i_val % e2->c.i_val;
            } else {
              e->c.u_val = r->code == N_DIV ? e1->c.u_val / e2->c.u_val : e1->c.u_val % e2->c.u_val;
            }
          }
        }
        break;
      default: e = NULL; assert (FALSE);
      }
      return e;
    }

    static unsigned case_hash (case_t el, void *arg MIR_UNUSED) {
      node_t case_expr = NL_HEAD (el->case_node->u.ops);
      struct expr *expr;

      assert (el->case_node->code == N_CASE);
      expr = case_expr->attr;
      assert (expr->const_p);
      if (signed_integer_type_p (expr->type))
        return (unsigned) mir_hash (&expr->c.i_val, sizeof (expr->c.i_val), 0x42);
      return (unsigned) mir_hash (&expr->c.u_val, sizeof (expr->c.u_val), 0x42);
    }

    static int case_eq (case_t el1, case_t el2, void *arg MIR_UNUSED) {
      node_t case_expr1 = NL_HEAD (el1->case_node->u.ops);
      node_t case_expr2 = NL_HEAD (el2->case_node->u.ops);
      struct expr *expr1, *expr2;

      assert (el1->case_node->code == N_CASE && el2->case_node->code == N_CASE);
      expr1 = case_expr1->attr;
      expr2 = case_expr2->attr;
      assert (expr1->const_p && expr2->const_p);
      assert (signed_integer_type_p (expr1->type) == signed_integer_type_p (expr2->type));
      if (signed_integer_type_p (expr1->type)) return expr1->c.i_val == expr2->c.i_val;
      return expr1->c.u_val == expr2->c.u_val;
    }

    static void update_call_arg_area_offset (c2m_ctx_t c2m_ctx, struct type *type, int update_scope_p) {
      check_ctx_t check_ctx = c2m_ctx->check_ctx;
      node_t block = FUNC_DEF_BLOCK (curr_func_def);
      struct node_scope *ns = block->attr;
      mir_size_t slot = round_size (type_size (c2m_ctx, type), MAX_ALIGNMENT);

      /* Empty (zero-size) struct memory-class results still need a real
         fp-relative slot so the function gets a frame (3582b48e). */
      if (slot == 0) slot = MAX_ALIGNMENT;
      curr_call_arg_area_offset += slot;
      if (update_scope_p && ns->call_arg_area_size < curr_call_arg_area_offset)
        ns->call_arg_area_size = curr_call_arg_area_offset;
    }

#define NODE_CASE(n) case N_##n:
#define REP_SEP
    static void classify_node (node_t n, int *expr_attr_p, int *stmt_p) {
      *expr_attr_p = *stmt_p = FALSE;
      switch (n->code) {
        REP8 (NODE_CASE, I, L, LL, U, UL, ULL, F, D)
        REP7 (NODE_CASE, LD, CH, CH16, CH32, STR, STR16, STR32)
        REP7 (NODE_CASE, ID, LABEL_ADDR, COMMA, ANDAND, OROR, EQ, STMTEXPR)
        REP8 (NODE_CASE, NE, LT, LE, GT, GE, ASSIGN, BITWISE_NOT, NOT)
        REP8 (NODE_CASE, AND, AND_ASSIGN, OR, OR_ASSIGN, XOR, XOR_ASSIGN, LSH, LSH_ASSIGN)
        REP8 (NODE_CASE, RSH, RSH_ASSIGN, ADD, ADD_ASSIGN, SUB, SUB_ASSIGN, MUL, MUL_ASSIGN)
        REP8 (NODE_CASE, DIV, DIV_ASSIGN, MOD, MOD_ASSIGN, IND, FIELD, ADDR, DEREF)
        REP8 (NODE_CASE, DEREF_FIELD, COND, INC, DEC, POST_INC, POST_DEC, ALIGNOF, SIZEOF)
        REP6 (NODE_CASE, EXPR_SIZEOF, CAST, COMPOUND_LITERAL, CALL, GENERIC, GENERIC_ASSOC)
        NODE_CASE (IN)
        NODE_CASE (COALESCE)
        NODE_CASE (NEW)
        NODE_CASE (ANY)
        NODE_CASE (LAMBDA)
        NODE_CASE (CONCAT)
        NODE_CASE (DETACH) /* expression: same type as inner operand */
        NODE_CASE (MOVE)     /* expression: same pointer value as inner operand */
        NODE_CASE (READONLY) /* expression: same pointer value as inner operand */
        *expr_attr_p = TRUE;
        break;
        REP8 (NODE_CASE, IF, SWITCH, WHILE, DO, FOR, GOTO, INDIRECT_GOTO, CONTINUE)
        REP5 (NODE_CASE, BREAK, RETURN, EXPR, BLOCK, SPEC_DECL) /* SPEC DECL may have an initializer */
        NODE_CASE (FORIN)
        NODE_CASE (DEFER)
        NODE_CASE (DELETE)
        NODE_CASE (ATTACH)  /* statement (stub today): like DEFER/DELETE */
        NODE_CASE (UNOWNED) /* declaration wrapper; treated as a statement-like
                               container so the inner SPEC_DECL list runs */
        NODE_CASE (OWNED)   /* declaration wrapper (managed-ownership opt-in) */
        NODE_CASE (TRY)
        NODE_CASE (CATCH)
        NODE_CASE (THROW)
        NODE_CASE (GO)
        NODE_CASE (AWAIT)
        *stmt_p = TRUE;
        break;
        REP8 (NODE_CASE, IGNORE, CASE, DEFAULT, LABEL, LIST, SHARE, TYPEDEF, EXTERN)
        REP8 (NODE_CASE, STATIC, AUTO, REGISTER, THREAD_LOCAL, DECL, VOID, CHAR, SHORT)
        REP8 (NODE_CASE, INT, LONG, FLOAT, DOUBLE, SIGNED, UNSIGNED, BOOL, STRUCT)
        REP8 (NODE_CASE, UNION, ENUM, ENUM_CONST, MEMBER, CONST, RESTRICT, VOLATILE, ATOMIC)
        REP8 (NODE_CASE, INLINE, NO_RETURN, ALIGNAS, FUNC, STAR, POINTER, DOTS, ARR)
        REP7 (NODE_CASE, INIT, FIELD_ID, TYPE, ST_ASSERT, FUNC_DEF, MODULE, DICT)
        REP4 (NODE_CASE, CLASS, STRING, ASM, ATTR)
        NODE_CASE (INTERFACE)
        break;
      default: assert (FALSE);
      }
    }
#undef REP_SEP

    /* Create `static const char ID[] = "<func name>"` at the beginning of
       func_block when ID was referenced.  */
    static void add_func_name_def (c2m_ctx_t c2m_ctx, node_t func_block, str_t func_name,
                                   const char *id_name) {
      pos_t pos = POS (func_block);
      node_t list, declarator, decl, decl_specs;
      tab_str_t str;

      if (!str_exists_p (c2m_ctx, id_name, strlen (id_name) + 1, &str)) return;
      decl_specs = new_pos_node (c2m_ctx, N_LIST, pos);
      NL_APPEND (decl_specs->u.ops, new_pos_node (c2m_ctx, N_STATIC, pos));
      NL_APPEND (decl_specs->u.ops, new_pos_node (c2m_ctx, N_CONST, pos));
      NL_APPEND (decl_specs->u.ops, new_pos_node (c2m_ctx, N_CHAR, pos));
      list = new_pos_node (c2m_ctx, N_LIST, pos);
      NL_APPEND (list->u.ops, new_pos_node3 (c2m_ctx, N_ARR, pos, new_pos_node (c2m_ctx, N_IGNORE, pos),
                                             new_pos_node (c2m_ctx, N_LIST, pos),
                                             new_pos_node (c2m_ctx, N_IGNORE, pos)));
      declarator
        = new_pos_node2 (c2m_ctx, N_DECL, pos, new_str_node (c2m_ctx, N_ID, str.str, pos), list);
      decl = new_pos_node5 (c2m_ctx, N_SPEC_DECL, pos, decl_specs, declarator,
                            new_node (c2m_ctx, N_IGNORE), new_node (c2m_ctx, N_IGNORE),
                            new_str_node (c2m_ctx, N_STR, func_name, pos));
      NL_PREPEND (NL_EL (func_block->u.ops, 1)->u.ops, decl);
    }

    /* C99 `__func__` plus the GNU aliases glibc <assert.h> uses once
       `__GNUC__` is defined (`__PRETTY_FUNCTION__`, `__FUNCTION__`). */
    static void add__func__def (c2m_ctx_t c2m_ctx, node_t func_block, str_t func_name) {
      add_func_name_def (c2m_ctx, func_block, func_name, "__func__");
      add_func_name_def (c2m_ctx, func_block, func_name, "__FUNCTION__");
      add_func_name_def (c2m_ctx, func_block, func_name, "__PRETTY_FUNCTION__");
    }

    /* Sort by decl scope nesting then decl size.
       Use func_scope_num (assigned in create_node_scope during check, outer
       scopes first) — NOT node parse uid.  N_FORIN is allocated *after* its
       body is parsed (`P(stmt)` then `new N_FORIN`), so the body block has a
       lower parse uid than the for-in node.  Sorting by parse uid laid out
       body locals first, then for-in vars at the same outer offset → stack
       collision (Ship leader overlaying List v; List.data becomes Ship.id=1
       → memcpy from 0x1 SEGV in aurora-ops GroupBy for-in). */
    static int decl_cmp (const void *v1, const void *v2) {
      const decl_t d1 = *(const decl_t *) v1, d2 = *(const decl_t *) v2;
      struct type *t1 = d1->decl_spec.type, *t2 = d2->decl_spec.type;
      mir_size_t s1 = raw_type_size (d1->c2m_ctx, t1), s2 = raw_type_size (d2->c2m_ctx, t2);
      unsigned n1 = 0, n2 = 0;

      if (d1->scope != NULL && d1->scope->attr != NULL)
        n1 = ((struct node_scope *) d1->scope->attr)->func_scope_num;
      if (d2->scope != NULL && d2->scope->attr != NULL)
        n2 = ((struct node_scope *) d2->scope->attr)->func_scope_num;
      if (n1 < n2) return -1;
      if (n1 > n2) return 1;
      if (s1 < s2) return -1;
      if (s1 > s2) return 1;
      return 0;
    }

    static void process_func_decls_for_allocation (c2m_ctx_t c2m_ctx) {
      check_ctx_t check_ctx = c2m_ctx->check_ctx;
      size_t i, j;
      decl_t decl;
      struct type *type;
      struct node_scope *ns, *curr_ns;
      node_t scope;
      mir_size_t start_offset = 0; /* to remove an uninitialized warning */

      /* Exclude decls which will be in regs, and anything that is not a real
         stack local/parameter (function symbols, type-only entries). */
      for (i = j = 0; i < VARR_LENGTH (decl_t, func_decls_for_allocation); i++) {
        decl = VARR_GET (decl_t, func_decls_for_allocation, i);
        type = decl->decl_spec.type;
        ns = decl->scope->attr;
        /* Defense: never lay out a function-type symbol as a frame slot. */
        if (type != NULL && (type->mode == TM_FUNC
                            || (type->mode == TM_PTR && type->u.ptr_type != NULL
                                && type->u.ptr_type->mode == TM_FUNC))) {
          decl->reg_p = TRUE;
          continue;
        }
        if (scalar_type_p (type)) {
          /* In a function containing a `try` (with exceptions enabled), keep
             scalars in memory rather than MIR registers: longjmp reverts
             callee-saved registers to their setjmp-time contents, so a
             register-homed variable modified in the try body and read in a
             catch handler would see a stale/garbage value.  Memory-backed
             locals hold their last store and behave intuitively.  Only decls
             already registered here get a frame offset, so this is safe. */
          if (!(c2m_options->exceptions_p && curr_func_has_try)) {
            decl->reg_p = TRUE;
            continue;
          }
          decl->reg_p = FALSE;
        }
        VARR_SET (decl_t, func_decls_for_allocation, j, decl);
        j++;
      }
      VARR_TRUNC (decl_t, func_decls_for_allocation, j);
      qsort (VARR_ADDR (decl_t, func_decls_for_allocation), j, sizeof (decl_t), decl_cmp);
      /* Stop walking outer scopes at class/struct/union tags as well as top_scope.
         Method function-blocks are nested under the class node during check, but
         that class is NOT a stack frame: treating it like one shifted every
         method's locals by the class's accumulated `ns->size` and then wrote that
         method span back into the class, so each monomorphized method inched
         offsets upward (and the early +8 from the method's own FUNC_DEF made
         BLK params of size >= 16 overrun a too-small alloca). */
#define FRAME_SCOPE_OUTER_STOP_P(s) \
      ((s) == top_scope || (s) == NULL \
       || (s)->code == N_CLASS || (s)->code == N_STRUCT || (s)->code == N_UNION)
      scope = NULL;
      for (i = 0; i < VARR_LENGTH (decl_t, func_decls_for_allocation); i++) {
        decl = VARR_GET (decl_t, func_decls_for_allocation, i);
        type = decl->decl_spec.type;
        ns = decl->scope->attr;
        if (decl->scope != scope) { /* new scope: process upper scopes */
          for (scope = ns->scope; !FRAME_SCOPE_OUTER_STOP_P (scope); scope = curr_ns->scope) {
            curr_ns = scope->attr;
            ns->offset += curr_ns->size;
            curr_ns->stack_var_p = TRUE;
          }
          scope = decl->scope;
          ns->stack_var_p = TRUE;
          start_offset = ns->offset;
        }
        ns->offset = round_size (ns->offset, var_align (c2m_ctx, type));
        decl->offset = ns->offset;
        ns->offset += var_size (c2m_ctx, type) + decl->flex_extra_size;
        ns->size = ns->offset - start_offset;
      }
      scope = NULL;
      for (i = 0; i < VARR_LENGTH (decl_t, func_decls_for_allocation); i++) { /* update scope sizes: */
        decl = VARR_GET (decl_t, func_decls_for_allocation, i);
        ns = decl->scope->attr;
        if (decl->scope == scope) continue;
        /* new scope: update upper *frame* scope sizes only */
        for (scope = ns->scope; !FRAME_SCOPE_OUTER_STOP_P (scope); scope = curr_ns->scope) {
          curr_ns = scope->attr;
          if (curr_ns->size < ns->offset) curr_ns->size = ns->offset;
          if (ns->stack_var_p) curr_ns->stack_var_p = TRUE;
        }
      }
#undef FRAME_SCOPE_OUTER_STOP_P
    }

    static const char *check_attrs (c2m_ctx_t c2m_ctx, node_t r, decl_t decl, node_t attrs,
                                    int check_p) {
      node_t n, list, id, alias_id;
      if (attrs->code == N_IGNORE) return NULL;
      assert (attrs->code == N_LIST);
      alias_id = NULL;
      for (n = NL_HEAD (attrs->u.ops); n != NULL; n = NL_NEXT (n)) {
        assert (n->code == N_ATTR);
        id = NL_HEAD (n->u.ops);
        assert (id->code == N_ID);
        if (strcmp (id->u.s.s, "antialias") != 0) continue;
        list = NL_NEXT (id);
        assert (list->code == N_LIST);
        id = NL_HEAD (list->u.ops);
        if (id == NULL) continue;
        if (!check_p) {
          if (id->code == N_ID) return id->u.s.s;
        } else if (NL_NEXT (id) != NULL) {
          error (c2m_ctx, POS (r), "antialias attribute has more one arg");
        } else if (id->code != N_ID) {
          error (c2m_ctx, POS (r), "antialias attribute arg should be an identifier");
        } else if (alias_id != NULL && strcmp (id->u.s.s, alias_id->u.s.s) != 0) {
          error (c2m_ctx, POS (r), "antialias attributes have different ids %s and %s", id->u.s.s,
                 alias_id->u.s.s);
        }
        alias_id = id;
      }
      if (alias_id == NULL) return NULL;
      if (decl->decl_spec.type->mode != TM_PTR) {
        error (c2m_ctx, POS (r), "antialias attribute should be given for a pointer type");
      }
      return alias_id->u.s.s;
    }

#define BUILTIN_VA_START \
      (const char *[]) { "__builtin_va_start", NULL }
#define BUILTIN_VA_ARG \
      (const char *[]) { "__builtin_va_arg", NULL }
#define ALLOCA \
      (const char *[]) { "alloca", "__builtin_alloca", NULL }
#define BUILTIN_JSON "json"

    static int str_eq_p (const char *str, const char *v[]) {
      for (int i = 0; v[i] != NULL; i++)
        if (strcmp (v[i], str) == 0) return TRUE;
      return FALSE;
    }

    /* Recursively check the value expressions inside a dict-literal initializer
       list (the N_LIST of N_INIT produced for  d = { "k": v, ... } ) so that the
       value nodes get their expr attrs set, which gen_dict_init_list relies on. */
    static void check_dict_init_list (c2m_ctx_t c2m_ctx, node_t initializer, node_t context) {
      if (initializer == NULL || initializer->code != N_LIST) return;
      for (node_t init = NL_HEAD (initializer->u.ops); init != NULL; init = NL_NEXT (init)) {
        node_t des_list, value;
        if (init->code != N_INIT) continue;
        des_list = NL_HEAD (init->u.ops);
        value = NL_NEXT (des_list);
        if (value == NULL) continue;
        if (value->code == N_LIST)
          check_dict_init_list (c2m_ctx, value, context);
        else
          check (c2m_ctx, value, context);
      }
    }

    /* TRUE iff a declaration-specifier list explicitly names a type (rather than
       relying on the implicit int default).  Used to recognise inferred
       declarations of the form  auto x = init; */
    static int specs_have_type_spec_p (node_t specs) {
      if (specs == NULL) return FALSE;
      node_t list = UNSHARE (specs);
      if (list == NULL || list->code != N_LIST) return FALSE;
      for (node_t n = NL_HEAD (list->u.ops); n != NULL; n = NL_NEXT (n)) {
        switch (n->code) {
        case N_VOID: case N_CHAR: case N_SHORT: case N_INT: case N_LONG:
        case N_FLOAT: case N_DOUBLE: case N_SIGNED: case N_UNSIGNED: case N_BOOL:
        case N_STRUCT: case N_UNION: case N_CLASS: case N_ENUM:
	    case N_DICT: case N_STRING: case N_ID:
	          return TRUE;
	    default: break;
	    }
	  }
	  return FALSE;
	}

    /* Implicit-`this` resolution for class scope.  Inside a (non-static) method,
       a bare identifier `m` may name a data member or another method of the
       enclosing class instead of a global/local.  Such identifiers are NOT in
       the method's lexical scope chain (methods/members are registered under the
       class node itself, see the N_FUNC_DEF handler), so an ordinary find_def
       fails.  When that happens this helper rewrites the N_ID `r` in place into
       `this.m` (an N_DEREF_FIELD whose base is the implicit `this` pointer) so
       the normal member/method-access machinery resolves it.  Returns TRUE iff
       a rewrite was performed.  Only fires when (a) we are inside a class, (b)
       an implicit `this` is in scope (i.e. a non-static method), and (c) `m` is
       actually a member or method of that class. */
    static int try_rewrite_implicit_this (c2m_ctx_t c2m_ctx, node_t r) {
      check_ctx_t check_ctx = c2m_ctx->check_ctx;
      node_t class_tag, this_id, member_id;
      symbol_t sym;

      if (r->code != N_ID || curr_class == NULL) return FALSE;
      /* Resolve curr_class to the actual N_CLASS tag node. */
      class_tag = (curr_class->code == N_CLASS) ? curr_class : NULL;
      if (class_tag == NULL) {
        node_t cid = NL_HEAD (curr_class->u.ops);
        if (cid != NULL && cid->code == N_ID) {
          class_tag = find_def (c2m_ctx, S_REGULARS, cid, curr_scope, NULL);
          if (class_tag == NULL) class_tag = find_def (c2m_ctx, S_TAG, cid, curr_scope, NULL);
        }
      }
      if (class_tag == NULL || class_tag->code != N_CLASS) return FALSE;
      /* `m` must be a data member or method registered in the class scope. */
      if (!symbol_find (c2m_ctx, S_REGULARS, r, class_tag, &sym) || sym.def_node == NULL
          || (sym.def_node->code != N_MEMBER && sym.def_node->code != N_FUNC_DEF))
        return FALSE;
      /* An implicit `this` receiver must be in scope (non-static method). */
      this_id = build_id (c2m_ctx, "this", POS (r));
      if (find_def (c2m_ctx, S_REGULARS, this_id, curr_scope, NULL) == NULL) return FALSE;
      /* Rewrite `m` (N_ID) in place into `this.m` (N_DEREF_FIELD(this, m)).
         Capture the member name before reusing r's union for the op list. */
      member_id = new_str_node (c2m_ctx, N_ID, r->u.s, POS (r));
      r->code = N_DEREF_FIELD;
      DLIST_INIT (node_t, r->u.ops);
      op_append (c2m_ctx, r, this_id);
      op_append (c2m_ctx, r, member_id);
      r->attr = NULL;
      return TRUE;
    }

    /* ── Collection count companion parameters ──────────────────────────────
       A function/constructor body may recover the element count of a bare `T*`
       parameter by calling `param.count()` (e.g. List(T* items) {... items.count()
       ...}).  A plain pointer carries no length of its own, so for each such
       parameter the compiler appends a hidden companion `int` length parameter
       right after it and rewrites every `param.count()` to read the companion.
       Callers fill the companion automatically: array/slice arguments are
       expanded by try_expand_collection_count_args (so `new List<T>(arr)` just
       works), and arr.ToList() lowering already passes base+length to the
       resulting (T*, int) signature.  This lets generic collection APIs written
       against a bare `T*` recover the source array's / slice's length. */

    /* Node kinds whose union holds a scalar/string payload rather than an op
       list: do not descend into u.ops for these (it would alias the payload). */
    static int count_walk_leaf_p (node_code_t c) {
      switch (c) {
      case N_I: case N_L: case N_LL: case N_U: case N_UL: case N_ULL:
      case N_F: case N_D: case N_LD:
      case N_CH: case N_CH16: case N_CH32:
      case N_STR: case N_STR16: case N_STR32:
      case N_ID: case N_STRING: return TRUE;
      default: return FALSE;
      }
    }

    /* TRUE if N is exactly `PNAME.count()` (dot access, zero args).  Restricted
       to the dot form (N_FIELD): a genuine class-pointer method call uses
       `p->count()` (N_DEREF_FIELD) and must not be hijacked. */
    static int param_count_call_p (node_t n, const char *pname) {
      node_t f, base, m, args;
      if (n->code != N_CALL) return FALSE;
      f = CALL_FUNC (n);
      if (f == NULL || f->code != N_FIELD) return FALSE;
      base = NL_HEAD (f->u.ops);
      m = base != NULL ? NL_NEXT (base) : NULL;
      args = CALL_ARGS (n);
      if (base == NULL || base->code != N_ID || strcmp (base->u.s.s, pname) != 0) return FALSE;
      if (m == NULL || m->code != N_ID || strcmp (m->u.s.s, "count") != 0) return FALSE;
      if (args == NULL || args->code != N_LIST || NL_HEAD (args->u.ops) != NULL) return FALSE;
      return TRUE;
    }

    /* Rewrite every `PNAME.count()` in the subtree to an N_ID referencing the
       companion length parameter CNAME; returns how many were rewritten. */
    static int rewrite_param_count_calls (c2m_ctx_t c2m_ctx, node_t n, const char *pname,
                                          str_t cname) {
      int rewritten = 0;
      if (n == NULL) return 0;
      if (param_count_call_p (n, pname)) {
        /* In-place: N_CALL -> N_ID(cname).  Overwriting u.s aliases the (now
           abandoned) op list; the sibling op_link lives outside the union, so
           the caller's traversal stays valid. */
        n->code = N_ID;
        n->u.s = cname;
        n->attr = NULL;
        return 1;
      }
      if (count_walk_leaf_p (n->code)) return 0;
      for (node_t ch = NL_HEAD (n->u.ops); ch != NULL; ch = NL_NEXT (ch))
        rewritten += rewrite_param_count_calls (c2m_ctx, ch, pname, cname);
      return rewritten;
    }

    /* If P is a plain pointer parameter `T* name` (an N_POINTER declarator with
       no array/function suffix), return its identifier name, else NULL. */
    static const char *plain_pointer_param_name (node_t p) {
      node_t decl, id, list;
      int has_ptr = FALSE;
      if (p == NULL || p->code != N_SPEC_DECL) return NULL;
      decl = SPEC_DECL_DECL (p);
      if (decl == NULL || decl->code != N_DECL) return NULL;
      id = DECL_ID (decl);
      list = DECL_LIST (decl);
      if (id == NULL || id->code != N_ID || list == NULL) return NULL;
      for (node_t n = NL_HEAD (list->u.ops); n != NULL; n = NL_NEXT (n)) {
        if (n->code == N_FUNC || n->code == N_ARR) return NULL;
        if (n->code == N_POINTER) has_ptr = TRUE;
      }
      return has_ptr ? id->u.s.s : NULL;
    }

    /* Append hidden `int` length companions for any pointer parameter the body
       queries with `.count()`.  Must run before create_decl so the companions
       become part of the function's signature (proto, mangling, call sites). */
    static void expand_count_companion_params (c2m_ctx_t c2m_ctx, node_t func_def) {
      node_t declarator = FUNC_DEF_DECL (func_def);
      node_t block = FUNC_DEF_BLOCK (func_def);
      node_t decl_list, func, param_list, p;

      if (declarator == NULL || declarator->code != N_DECL) return;
      if (block == NULL || block->code != N_BLOCK) return;
      decl_list = DECL_LIST (declarator);
      if (decl_list == NULL) return;
      func = NL_HEAD (decl_list->u.ops);
      if (func == NULL || func->code != N_FUNC) return;
      param_list = NL_HEAD (func->u.ops);
      if (param_list == NULL || param_list->code != N_LIST) return;

      for (p = NL_HEAD (param_list->u.ops); p != NULL; p = NL_NEXT (p)) {
        const char *pname = plain_pointer_param_name (p);
        node_t companion;
        char cbuf[128];
        str_t cstr;
        if (pname == NULL) continue;
        snprintf (cbuf, sizeof (cbuf), "__seqlen_%s", pname);
        cstr = uniq_cstr (c2m_ctx, cbuf);
        if (rewrite_param_count_calls (c2m_ctx, block, pname, cstr) == 0) continue;
        /* Build `int <cstr>` (mirrors the K&R placeholder construction below). */
        companion = new_pos_node5 (c2m_ctx, N_SPEC_DECL, POS (p),
                                   new_node1 (c2m_ctx, N_SHARE,
                                              new_node1 (c2m_ctx, N_LIST,
                                                         new_pos_node (c2m_ctx, N_INT, POS (p)))),
                                   new_pos_node2 (c2m_ctx, N_DECL, POS (p),
                                                  new_str_node (c2m_ctx, N_ID, cstr, POS (p)),
                                                  new_node (c2m_ctx, N_LIST)),
                                   new_node (c2m_ctx, N_IGNORE), new_node (c2m_ctx, N_IGNORE),
                                   new_node (c2m_ctx, N_IGNORE));
        DLIST_INSERT_AFTER (node_t, param_list->u.ops, p, companion);
        p = companion; /* don't reconsider the freshly inserted companion */
      }
    }

    /* ---- printf-family format-string argument type checker --------------------
       Called after all CALL arguments are type-checked.  Scans the format string
       literal for %s/%S specifiers and verifies that the matching variadic argument
       is a string or pointer type.  Passing a bare integer for %s causes strlen()
       to dereference the integer value as a pointer, producing a JIT SIGSEGV.
       fmt_idx is the 0-based position of the format string in arg_list->u.ops
       (0 for printf, 1 for fprintf/sprintf/snprintf).                           */
    /* Peel `(const char *)"lit"` down to N_STR.  Wide strings are not folded. */
    static node_t check_string_lit_node (node_t n) {
      int g = 0;
      while (n != NULL && g++ < 8) {
        if (n->code == N_STR) return n;
        if (n->code == N_CAST) {
          n = NL_EL (n->u.ops, 1);
          continue;
        }
        break;
      }
      return NULL;
    }

    static void check_printf_format (c2m_ctx_t c2m_ctx, node_t arg_list, int fmt_idx) {
      node_t fmt_node = NL_EL (arg_list->u.ops, fmt_idx);
      if (fmt_node == NULL || fmt_node->code != N_STR) return;
      const char *fmt = fmt_node->u.s.s;
      if (fmt == NULL) return;

      int vararg_slot = 0; /* 0-based index of the current variadic arg after format */
      for (const char *p = fmt; *p != '\0'; p++) {
        if (*p != '%') continue;
        p++;
        if (*p == '\0') break;
        if (*p == '%') continue;              /* %% literal */
        while (*p && strchr ("-+ #0'", *p)) p++; /* flags */
        if (*p == '*') { p++; vararg_slot++; } /* width from arg */
        else while (*p >= '0' && *p <= '9') p++;
        if (*p == '.') {                       /* precision */
          p++;
          if (*p == '*') { p++; vararg_slot++; }
          else while (*p >= '0' && *p <= '9') p++;
        }
        /* length modifiers */
        if      (*p == 'h') { p++; if (*p == 'h') p++; }
        else if (*p == 'l') { p++; if (*p == 'l') p++; }
        else if (*p && strchr ("LztjqZ", *p)) p++;
        if (*p == '\0') break;

        char spec = *p;
        {
          node_t varg = NL_EL (arg_list->u.ops, fmt_idx + 1 + vararg_slot);
          if (varg != NULL && varg->attr != NULL) {
            struct expr *ae = (struct expr *) varg->attr;
            struct type *at = ae->type;
            if (at != NULL) {
              /* %s / %S — argument must be a pointer or string type */
              if ((spec == 's' || spec == 'S')
                  && at->mode != TM_PTR && at->mode != TM_ARR
                  && !string_type_p (at) && !builtin_string_type_p (at)) {
                error (c2m_ctx, POS (varg),
                       "format '%%%c' expects a string or pointer argument "
                       "but the corresponding argument has %s type "
                       "(passing a non-pointer for %%s crashes at runtime)",
                       spec, integer_type_p (at) ? "integer" : "non-pointer");
              }
              /* %d / %i / %u / %o / %x / %X — argument must not be a pointer or string */
              else if ((spec == 'd' || spec == 'i' || spec == 'u'
                        || spec == 'o' || spec == 'x' || spec == 'X')
                       && (at->mode == TM_PTR || at->mode == TM_ARR
                           || string_type_p (at) || builtin_string_type_p (at))) {
                error (c2m_ctx, POS (varg),
                       "format '%%%c' expects an integer argument "
                       "but the corresponding argument has pointer/string type "
                       "(printing a pointer with %%%c gives a raw address, not the value)",
                       spec, spec);
              }
            }
          }
        }
        vararg_slot++;
      }
    }

    /* `go` argument predicate (-ffibers): the cy_spawn8 trampoline passes args
       as raw I64 slots, so only GP-class values are allowed: integers, enums,
       bool, pointers (incl. String, class*, dict).  Floats and by-value
       aggregates (struct/union/class/array/slice) are rejected. */
    static int go_gp_type_p (c2m_ctx_t c2m_ctx, struct type *t) {
      if (t == NULL) return FALSE;
      switch (t->mode) {
      case TM_BASIC:
        return t->u.basic_type != TP_FLOAT && t->u.basic_type != TP_DOUBLE
               && t->u.basic_type != TP_LDOUBLE && t->u.basic_type != TP_VOID
               && t->u.basic_type != TP_UNDEF && t->u.basic_type != TP_GENERIC;
      case TM_ENUM:
      case TM_PTR:
      case TM_DICT: return TRUE;
      default: return FALSE;
      }
    }

    static void check (c2m_ctx_t c2m_ctx, node_t r, node_t context) {
      check_ctx_t check_ctx = c2m_ctx->check_ctx;
      node_t op1, op2;
      struct expr *e = NULL, *e1, *e2;
      struct type t, *t1, *t2, *assign_expr_type;
      int expr_attr_p, stmt_p;

      VARR_PUSH (node_t, context_stack, context);
      classify_node (r, &expr_attr_p, &stmt_p);
      switch (r->code) {
      case N_IGNORE:
      case N_STAR:
      case N_FIELD_ID: break; /* do nothing */
      case N_LIST: {
        /* The module's top-level list: track the item being checked so that
           lambda instantiation (filter/map/reduce) can inject synthesized
           FUNC_DEFs into the module before the current item. */
        int top_list_p = context != NULL && context->code == N_MODULE;
        node_t saved_module_item = curr_module_item, saved_item_list = module_item_list;

        if (top_list_p) module_item_list = r;
        if (top_list_p) module_items_root = r;
        for (node_t n = NL_HEAD (r->u.ops); n != NULL; n = NL_NEXT (n)) {
          if (top_list_p) curr_module_item = n;
          check (c2m_ctx, n, r);
        }
        if (top_list_p) {
          curr_module_item = saved_module_item;
          module_item_list = saved_item_list;
        }
        break;
      }
      case N_LAMBDA: {
        /* Typed non-capturing lambdas lower to static functions (thin C
           function pointers).  Capturing lambdas are only legal as direct
           arguments to Where/Filter/… (open-coded before this case runs).
           Untyped lambdas still need a seq.filter/map/reduce context. */
        node_t params = NL_HEAD (r->u.ops);
        VARR (cstr_t) * free_names;
        int nfree;
        MIR_alloc_t alloc;

        /* Re-check of an already-instantiated lambda: reuse attr and set outer
           `e` so the post-switch recovery path does not stamp TP_INT over a
           good func-ptr (Select/GroupBy pre-check args more than once). */
        if (r->attr != NULL) {
          e = r->attr;
          break;
        }
        alloc = c2m_alloc (c2m_ctx);
        VARR_CREATE (cstr_t, free_names, alloc, 4);
        lambda_collect_free_vars (c2m_ctx, r, free_names);
        nfree = (int) VARR_LENGTH (cstr_t, free_names);
        if (nfree > 0) {
          const char *fn = VARR_GET (cstr_t, free_names, 0);
          error (c2m_ctx, POS (r),
                 "lambda captures local '%s' but is not a direct argument to "
                 "Where/Filter/Map/ForEach/Any/All/Find/Sort/Select/CountWhere",
                 fn != NULL ? fn : "?");
          error (c2m_ctx, POS (r),
                 "hint: use xs.Where((T x) => …) inline, or pass a non-capturing function");
          VARR_DESTROY (cstr_t, free_names);
          break;
        }
        VARR_DESTROY (cstr_t, free_names);
        if (!lambda_typed_p (params)) {
          error (c2m_ctx, POS (r),
                 "untyped lambda is only supported as a filter/map/reduce argument"
                 " (add parameter types:  (int x) => ...)");
          break;
        }
        if (instantiate_typed_lambda (c2m_ctx, r) == NULL) {
          /* errors already reported */
          break;
        }
        /* Publish the func-ptr expr to the outer `e` so the post-switch
           recovery path does not overwrite attr with a dummy TP_INT. */
        e = r->attr;
        break;
      }
      case N_I:
      case N_L:
        e = create_basic_type_expr (c2m_ctx, r, r->code == N_I ? TP_INT : TP_LONG);
        e->const_p = TRUE;
        e->c.i_val = r->u.l;
        break;
      case N_LL:
        e = create_basic_type_expr (c2m_ctx, r, TP_LLONG);
        e->const_p = TRUE;
        e->c.i_val = r->u.ll;
        break;
      case N_U:
      case N_UL:
        e = create_basic_type_expr (c2m_ctx, r, r->code == N_U ? TP_UINT : TP_ULONG);
        e->const_p = TRUE;
        e->c.u_val = r->u.ul;
        break;
      case N_ULL:
        e = create_basic_type_expr (c2m_ctx, r, TP_ULLONG);
        e->const_p = TRUE;
        e->c.u_val = r->u.ull;
        break;
      case N_F:
        e = create_basic_type_expr (c2m_ctx, r, TP_FLOAT);
        e->const_p = TRUE;
        e->c.d_val = r->u.f;
        break;
      case N_D:
        e = create_basic_type_expr (c2m_ctx, r, TP_DOUBLE);
        e->const_p = TRUE;
        e->c.d_val = r->u.d;
        break;
      case N_LD:
        e = create_basic_type_expr (c2m_ctx, r, TP_LDOUBLE);
        e->const_p = TRUE;
        e->c.d_val = r->u.ld;
        break;
      case N_CH:
        e = create_basic_type_expr (c2m_ctx, r, TP_CHAR);
        e->const_p = TRUE;
        if (char_is_signed_p ())
          e->c.i_val = r->u.ch;
        else
          e->c.u_val = r->u.ch;
        break;
      case N_CH16:
      case N_CH32:
        e = create_basic_type_expr (c2m_ctx, r, get_uint_basic_type (r->code == N_CH16 ? 2 : 4));
        e->const_p = TRUE;
        e->c.u_val = r->u.ul;
        break;
      case N_STR:
      case N_STR16:
      case N_STR32:
      case N_STRING: {
        // TODO: Handle fstring in N_STRING
        struct arr_type *arr_type;
        int size = r->code == N_STR ? 1 : r->code == N_STR16 ? 2 : 4;

        e = create_expr (c2m_ctx, r);
        e->u.lvalue_node = r;
        e->type->mode = TM_ARR;
        e->type->pos_node = r;
        e->type->u.arr_type = arr_type = reg_malloc (c2m_ctx, sizeof (struct arr_type));
        clear_type_qual (&arr_type->ind_type_qual);
        arr_type->static_p = FALSE;
        arr_type->flex_p = 0;
        arr_type->flex_bound_member = NULL;
        arr_type->el_type = create_type (c2m_ctx, NULL);
        arr_type->el_type->pos_node = r;
        arr_type->el_type->mode = TM_BASIC;
        arr_type->el_type->u.basic_type = size == 1 ? TP_CHAR : get_uint_basic_type (size);
        arr_type->size = new_i_node (c2m_ctx, (long) r->u.s.len, POS (r));
        check (c2m_ctx, arr_type->size, NULL);
        break;
      }
      case N_ID: {
        node_t aux_node = NULL;
        decl_t decl;

        op1 = find_def (c2m_ctx, S_REGULARS, r, curr_scope, &aux_node);
        /* Inside a class method, an unqualified identifier that names a data
           member (resolved here as an N_MEMBER) or is otherwise unresolved but
           names a method (op1 == NULL — methods are registered under the class
           node, not the lexical chain) is treated as `this.<id>`. */
        if ((op1 == NULL || op1->code == N_MEMBER) && try_rewrite_implicit_this (c2m_ctx, r)) {
          /* `r` was rewritten in place to `this.<id>`; re-check it and adopt
             the field-access result. */
          check (c2m_ctx, r, context);
          e = r->attr;
          break;
        }
        e = create_expr (c2m_ctx, r);
        e->def_node = op1;
        if (op1 == NULL) {
          error (c2m_ctx, POS (r), "undeclared identifier %s", r->u.s.s);
        } else if (op1->code == N_IGNORE) {
          e->type->mode = TM_BASIC;
          e->type->u.basic_type = TP_INT;
        } else if (op1->code == N_SPEC_DECL) {
          decl = op1->attr;
          if (decl->decl_spec.typedef_p)
            error (c2m_ctx, POS (r), "typedef name %s as an operand", r->u.s.s);
          decl->used_p = TRUE;
          *e->type = *decl->decl_spec.type;
          if (e->type->mode != TM_FUNC) e->u.lvalue_node = op1;
        } else if (op1->code == N_CLASS) {
          // Bare class name used as namespace for static members (e.g. Fruit.variants or direct Fruit.Apple for const)
          e->type->mode = TM_CLASS;
          e->type->u.tag_type = op1;
          e->def_node = op1;
        } else if (op1->code == N_FUNC_DEF) {
          decl = op1->attr;
          decl->used_p = TRUE;
          assert (decl->decl_spec.type->mode == TM_FUNC);
          *e->type = *decl->decl_spec.type;
        } else if (op1->code == N_ENUM_CONST) {
          assert (aux_node && aux_node->code == N_ENUM);
          e->type->mode = TM_ENUM;
          e->type->pos_node = r;
          e->type->u.tag_type = aux_node;
          e->const_p = TRUE;
          e->c.i_val = ((struct enum_value *) op1->attr)->u.i_val;
        } else { /* it is a member reference inside struct/union */
          assert (op1->code == N_MEMBER);
        }
        break;
      }
      case N_COMMA:
        process_bin_ops (c2m_ctx, r, &op1, &op2, &e1, &e2, &t1, &t2, r);
        e = create_expr (c2m_ctx, r);
        *e->type = *e2->type;
        break;
      case N_ANDAND:
      case N_OROR:
        process_bin_ops (c2m_ctx, r, &op1, &op2, &e1, &e2, &t1, &t2, r);
        e = create_expr (c2m_ctx, r);
        e->type->mode = TM_BASIC;
        e->type->u.basic_type = TP_INT;
        if (!scalar_type_p (t1) || !scalar_type_p (t2)) {
          error (c2m_ctx, POS (r), "invalid operand types of %s", r->code == N_ANDAND ? "&&" : "||");
        } else if (e1->const_p) {
          int v;

          if (floating_type_p (t1))
            v = e1->c.d_val != 0.0;
          else if (signed_integer_type_p (t1))
            v = e1->c.i_val != 0;
          else
            v = e1->c.u_val != 0;
          if (v && r->code == N_OROR) {
            e->const_p = TRUE;
            e->c.i_val = v;
          } else if (!v && r->code == N_ANDAND) {
            e->const_p = TRUE;
            e->c.i_val = v;
          } else if (e2->const_p) {
            e->const_p = TRUE;
            if (floating_type_p (t2))
              v = e2->c.d_val != 0.0;
            else if (signed_integer_type_p (t2))
              v = e2->c.i_val != 0;
            else
              v = e2->c.u_val != 0;
            e->c.i_val = v;
          }
        }
        break;
      case N_EQ:
      case N_NE:
      case N_LT:
      case N_LE:
      case N_GT:
      case N_GE:
        process_bin_ops (c2m_ctx, r, &op1, &op2, &e1, &e2, &t1, &t2, r);
        e = create_expr (c2m_ctx, r);
        e->type->mode = TM_BASIC;
        e->type->u.basic_type = TP_INT;
        if ((r->code == N_EQ || r->code == N_NE)
            && ((t1->mode == TM_PTR && null_const_p (e2, t2))
                || (t2->mode == TM_PTR && null_const_p (e1, t1))
                /* built-in String compared against null */
                || (builtin_string_type_p (t1) && null_const_p (e2, t2))
                || (builtin_string_type_p (t2) && null_const_p (e1, t1))))
          ;
        else if (t1->mode == TM_PTR && t2->mode == TM_PTR) {
          if (!compatible_types_p (t1, t2, TRUE)
              && ((r->code != N_EQ && r->code != N_NE) || (!void_ptr_p (t1) && !void_ptr_p (t2)))) {
            (c2m_options->pedantic_p ? error : warning) (c2m_ctx, POS (r),
                                                         "incompatible pointer types in comparison");
          } else if (t1->u.ptr_type->type_qual.atomic_p || t2->u.ptr_type->type_qual.atomic_p) {
            error (c2m_ctx, POS (r), "pointer to atomic type as a comparison operand");
          } else if (e1->const_p && e2->const_p) {
            e->const_p = TRUE;
            e->c.i_val = (r->code == N_EQ   ? e1->c.u_val == e2->c.u_val
                          : r->code == N_NE ? e1->c.u_val != e2->c.u_val
                          : r->code == N_LT ? e1->c.u_val < e2->c.u_val
                          : r->code == N_LE ? e1->c.u_val <= e2->c.u_val
                          : r->code == N_GT ? e1->c.u_val > e2->c.u_val
                                            : e1->c.u_val >= e2->c.u_val);
          }
        } else if (arithmetic_type_p (t1) && arithmetic_type_p (t2)) {
          if (e1->const_p && e2->const_p) {
            t = arithmetic_conversion (t1, t2);
            convert_value (e1, &t);
            convert_value (e2, &t);
            e->const_p = TRUE;
            if (floating_type_p (&t))
              e->c.i_val = (r->code == N_EQ   ? e1->c.d_val == e2->c.d_val
                            : r->code == N_NE ? e1->c.d_val != e2->c.d_val
                            : r->code == N_LT ? e1->c.d_val < e2->c.d_val
                            : r->code == N_LE ? e1->c.d_val <= e2->c.d_val
                            : r->code == N_GT ? e1->c.d_val > e2->c.d_val
                                              : e1->c.d_val >= e2->c.d_val);
            else if (signed_integer_type_p (&t))
              e->c.i_val = (r->code == N_EQ   ? e1->c.i_val == e2->c.i_val
                            : r->code == N_NE ? e1->c.i_val != e2->c.i_val
                            : r->code == N_LT ? e1->c.i_val < e2->c.i_val
                            : r->code == N_LE ? e1->c.i_val <= e2->c.i_val
                            : r->code == N_GT ? e1->c.i_val > e2->c.i_val
                                              : e1->c.i_val >= e2->c.i_val);
            else
              e->c.i_val = (r->code == N_EQ   ? e1->c.u_val == e2->c.u_val
                            : r->code == N_NE ? e1->c.u_val != e2->c.u_val
                            : r->code == N_LT ? e1->c.u_val < e2->c.u_val
                            : r->code == N_LE ? e1->c.u_val <= e2->c.u_val
                            : r->code == N_GT ? e1->c.u_val > e2->c.u_val
                                              : e1->c.u_val >= e2->c.u_val);
          }
        } else if ((t1->mode == TM_PTR && integer_type_p (t2))
                   || (t2->mode == TM_PTR && integer_type_p (t1))) {
          warning (c2m_ctx, POS (r), "comparison of integer with a pointer");
        } else if ((t1->mode == TM_DICT && (integer_type_p (t2) || t2->mode == TM_DICT))
                   || (t2->mode == TM_DICT && integer_type_p (t1))) {
          /* dict is a pointer — allow comparison with integers and other dicts */
        } else if (builtin_string_type_p (t1) && builtin_string_type_p (t2)) {
          /* String == String: pointer-equality comparison is valid. */
        } else if (builtin_string_type_p (t1) && t2->mode == TM_PTR) {
          /* String == char* (e.g. comparing with a literal that decayed to ptr). */
        } else if (t1->mode == TM_PTR && builtin_string_type_p (t2)) {
          /* char* == String */
        } else if ((r->code == N_EQ || r->code == N_NE)
                   && (t1->mode == TM_CLASS || t1->mode == TM_STRUCT || t1->mode == TM_UNION)
                   && (t2->mode == TM_CLASS || t2->mode == TM_STRUCT || t2->mode == TM_UNION)
                   && compatible_types_p (t1, t2, TRUE)) {
          /* By-value class/struct == / != : lowered to a byte-wise memcmp in gen
             (see N_EQ/N_NE codegen).  This is shallow (raw bytes) equality, which
             is what generic collections like List<T> need to type-check and run
             for value element types.  NOTE: padding bytes participate, so it is
             only well-defined for classes whose storage is fully initialized
             (constructors should not leave padding holes that vary). */
        } else {
          error (c2m_ctx, POS (r), "invalid types of comparison operands");
        }
        break;
      case N_BITWISE_NOT:
      case N_NOT:
        process_unop (c2m_ctx, r, &op1, &e1, &t1, r);
        e = create_expr (c2m_ctx, r);
        e->type->mode = TM_BASIC;
        e->type->u.basic_type = TP_INT;
        if (r->code == N_BITWISE_NOT && !integer_type_p (t1)) {
          error (c2m_ctx, POS (r), "bitwise-not operand should be of an integer type");
        } else if (r->code == N_NOT && !scalar_type_p (t1)) {
          error (c2m_ctx, POS (r), "not operand should be of a scalar type");
        } else if (r->code == N_BITWISE_NOT) {
          t = integer_promotion (t1);
          e->type->u.basic_type = t.u.basic_type;
          if (e1->const_p) {
            convert_value (e1, &t);
            e->const_p = TRUE;
            if (signed_integer_type_p (&t))
              e->c.i_val = ~e1->c.i_val;
            else
              e->c.u_val = ~e1->c.u_val;
          }
        } else if (e1->const_p) {
          e->const_p = TRUE;
          if (floating_type_p (t1))
            e->c.i_val = e1->c.d_val == 0.0;
          else if (signed_integer_type_p (t1))
            e->c.i_val = e1->c.i_val == 0;
          else
            e->c.i_val = e1->c.u_val == 0;
        }
        break;
      case N_INC:
      case N_DEC:
      case N_POST_INC:
      case N_POST_DEC: {
        struct expr saved_expr;

        process_unop (c2m_ctx, r, &op1, &e1, &t1, NULL);
        saved_expr = *e1;
        t1 = e1->type = adjust_type (c2m_ctx, e1->type);
        get_int_node (c2m_ctx, &op2, &e2, &t2,
                      t1->mode != TM_PTR ? 1 : type_size (c2m_ctx, t1->u.ptr_type));
        e = check_assign_op (c2m_ctx, r, e1, e2, t1, t2);
        t2 = ((struct expr *) r->attr)->type;
        *e1 = saved_expr;
        t1 = e1->type;
        assign_expr_type = create_type (c2m_ctx, NULL);
        *assign_expr_type = *e->type;
        goto assign;
        break;
      }
      case N_ADD:
      case N_SUB:
        if (NL_NEXT (NL_HEAD (r->u.ops)) == NULL) { /* unary */
          process_unop (c2m_ctx, r, &op1, &e1, &t1, r);
          e = create_expr (c2m_ctx, r);
          e->type->mode = TM_BASIC;
          e->type->u.basic_type = TP_INT;
          if (!arithmetic_type_p (t1)) {
            error (c2m_ctx, POS (r), "unary + or - operand should be of an arithmentic type");
          } else {
            if (e1->const_p) e->const_p = TRUE;
            if (floating_type_p (t1)) {
              e->type->u.basic_type = t1->u.basic_type;
              if (e->const_p) e->c.d_val = (r->code == N_ADD ? +e1->c.d_val : -e1->c.d_val);
            } else {
              t = integer_promotion (t1);
              e->type->u.basic_type = t.u.basic_type;
              if (e1->const_p) {
                convert_value (e1, &t);
                if (signed_integer_type_p (&t))
                  e->c.i_val = (r->code == N_ADD ? +e1->c.i_val : -e1->c.i_val);
                else
                  e->c.u_val = (r->code == N_ADD ? +e1->c.u_val : -e1->c.u_val);
              }
            }
          }
          break;
        }
        /* falls through */
      case N_AND:
      case N_OR:
      case N_XOR:
      case N_LSH:
      case N_RSH:
      case N_MUL:
      case N_DIV:
      case N_MOD:
        process_bin_ops (c2m_ctx, r, &op1, &op2, &e1, &e2, &t1, &t2, r);
        /* String `+` concatenation (with basic-type auto-cast).  This only
           triggers when at least one operand is a string value (a built-in
           `String` or a string literal); the other operand may be another
           string value or an arithmetic (basic) value that is auto-cast to its
           textual form.  Because a plain `String`/string-literal is never a C11
           arithmetic/pointer expression here, ordinary C11 code is unaffected. */
        if (r->code == N_ADD) {
          int s1 = str_concat_string_operand_p (t1, op1);
          int s2 = str_concat_string_operand_p (t2, op2);

          if (s1 || s2) {
            /* char* / const char* is a NUL-terminated string value in the
               same in-memory form as String; accept it as a concat operand
               so `sb + host` works without an explicit (String) cast.
               Arithmetic operands (int, size_t, …) are accepted ONLY when
               at least one side is a genuine TP_STRING — this preserves C11
               pointer arithmetic for `"literal" + int` while still allowing
               `str + port` (str is TP_STRING). */
            int has_real_str = (builtin_string_type_p (t1)
                                || builtin_string_type_p (t2));
            int char_ptr1 = (t1 != NULL && t1->mode == TM_PTR
                             && t1->u.ptr_type != NULL
                             && char_type_p (t1->u.ptr_type));
            int char_ptr2 = (t2 != NULL && t2->mode == TM_PTR
                             && t2->u.ptr_type != NULL
                             && char_type_p (t2->u.ptr_type));
            int ok1 = s1 || char_ptr1 || (arithmetic_type_p (t1) && has_real_str);
            int ok2 = s2 || char_ptr2 || (arithmetic_type_p (t2) && has_real_str);

            if (ok1 && ok2) {
              debug (c2m_ctx, POS (r), "Overload N_ADD to N_CONCAT (String concat)");
              r->code = N_CONCAT;
              e = create_expr (c2m_ctx, r);
              e->type->mode = TM_BASIC;
              e->type->u.basic_type = TP_STRING;
              e->type->type_qual.const_p = 1;
              e->type->raw_size = 8;
              e->type->align = 8;
              e->type->pos_node = r;
              break;
            }
          }
        }
        e = check_assign_op (c2m_ctx, r, e1, e2, t1, t2);
        break;
      case N_AND_ASSIGN:
      case N_OR_ASSIGN:
      case N_XOR_ASSIGN:
      case N_LSH_ASSIGN:
      case N_RSH_ASSIGN:
      case N_ADD_ASSIGN:
      case N_SUB_ASSIGN:
      case N_MUL_ASSIGN:
      case N_DIV_ASSIGN:
      case N_MOD_ASSIGN: {
        struct expr saved_expr;

        process_bin_ops (c2m_ctx, r, &op1, &op2, &e1, &e2, &t1, &t2, NULL);
        saved_expr = *e1;
        t1 = e1->type = adjust_type (c2m_ctx, e1->type);
        t2 = e2->type = adjust_type (c2m_ctx, e2->type);
        e = check_assign_op (c2m_ctx, r, e1, e2, t1, t2);
        assign_expr_type = create_type (c2m_ctx, NULL);
        *assign_expr_type = *e->type;
        t2 = ((struct expr *) r->attr)->type;
        *e1 = saved_expr;
        t1 = e1->type;
        goto assign;
        break;
      }
      case N_ASSIGN:
        op1 = NL_HEAD (r->u.ops);
        op2 = NL_NEXT (op1);
        if (op2->code == N_LIST) {
          /* dict-literal assignment: lhs = { "k": v, ... } */
          check (c2m_ctx, op1, NULL);
          e1 = op1->attr;
          t1 = e1->type;
          if (t1->mode != TM_DICT)
            error (c2m_ctx, POS (r),
                   "brace initializer assignment is only valid for a dict");
          check_dict_init_list (c2m_ctx, op2, r);
          e = create_expr (c2m_ctx, r);
          *e->type = *t1;
          if (!e1->u.lvalue_node)
            error (c2m_ctx, POS (r), "lvalue required as left operand of assignment");
          break;
        }
        process_bin_ops (c2m_ctx, r, &op1, &op2, &e1, &e2, &t1, &t2, NULL);
        t2 = e2->type = adjust_type (c2m_ctx, e2->type);
        assign_expr_type = NULL;
      assign:
        e = create_expr (c2m_ctx, r);
        if (!e1->u.lvalue_node) {
          error (c2m_ctx, POS (r), "lvalue required as left operand of assignment");
        }
        /* Move-only collections (List/Map/Set): ban bare shallow assign which
           would alias the heap buffer and double-free.  Require `move` for
           lvalues; allow prvalue RHS from a by-value call return.
           Storage lvalue RHS (map.vals[i], field): rewrite to `.Copy()`. */
        if (class_is_move_only_collection_p (c2m_ctx, t1)
            && class_is_move_only_collection_p (c2m_ctx, t2)
            && op2->code != N_MOVE && op2->code != N_CALL
            && op2->code != N_STMTEXPR) {
          node_t pe = op2;
          while (pe != NULL && pe->code == N_CAST) pe = NL_EL (pe->u.ops, 1);
          int storage_p
            = (pe != NULL
               && (pe->code == N_IND || pe->code == N_FIELD || pe->code == N_DEREF_FIELD
                   || pe->code == N_DEREF));
          if (storage_p) {
            /* lhs = rhs.Copy() — unlink rhs before grafting it under Copy's
               field node (build_dot_call would otherwise steal the link and
               leave NL_REMOVE asserting). */
            NL_REMOVE (r->u.ops, op2);
            node_t copy_call
              = build_dot_call (c2m_ctx, POS (r), op2, "Copy", new_node (c2m_ctx, N_LIST));
            NL_APPEND (r->u.ops, copy_call);
            check (c2m_ctx, copy_call, r);
            op2 = copy_call;
            e2 = op2->attr;
            t2 = e2 != NULL ? e2->type : t2;
          } else {
            node_t cid = (t1->u.tag_type != NULL) ? TAG_ID (t1->u.tag_type) : NULL;
            error (c2m_ctx, POS (r),
                   "cannot assign collection '%s' (shallow copy would double-free); "
                   "use `move` to transfer ownership, or assign a by-value return",
                   (cid != NULL && cid->code == N_ID) ? cid->u.s.s : "?");
          }
        }
        check_assignment_types (c2m_ctx, t1, t2, e2, r);
        /* String arena scope: gen emits c2m_str_release_to at every return,
           and only a *returned* String is protected via release_keeping.
           Class fields go through c2m_str_own (private heap copy).  A plain
           store through an out-param `String *` does neither — the pointer
           written to the caller dangles as soon as this function returns:

               void f (String *out) {
                 *out = f"id={n}";   // UAF after return
               }

           Scope (keep noise down):
             - Only simple `*id = …` (out-param shape), not `*(p+i) = …`
               (List/Map buffer writes).
             - Suppress when the RHS is a non-tracked value (literal), an
               explicit detach, or a plain pointer load (`*p`, a[i], field)
               that is not itself a fresh arena allocation in this statement.
           Safe escapes: return-by-value, `detach <expr>` / `.detach()`,
           string literals, class-field assignment. */
        if (op1->code == N_DEREF && builtin_string_type_p (t1)) {
          node_t base = NL_HEAD (op1->u.ops);
          while (base != NULL && base->code == N_CAST)
            base = NL_EL (base->u.ops, 1);
          if (base != NULL && base->code == N_ID) {
            node_t rhs = op2;
            while (rhs != NULL && rhs->code == N_CAST)
              rhs = NL_EL (rhs->u.ops, 1);
            int safe_p = 0;
            if (rhs == NULL)
              safe_p = 1;
            else if (rhs->code == N_STR)
              safe_p = 1; /* string literal — not arena-tracked */
            else if (rhs->code == N_DETACH)
              safe_p = 1; /* explicit detach — removed from tracker */
            else if (rhs->code == N_DEREF || rhs->code == N_IND
                     || rhs->code == N_FIELD || rhs->code == N_DEREF_FIELD)
              /* Copying an existing pointer (e.g. List.TryGet) — not a
                 fresh f-string/concat in this statement.  Still UAF if the
                 source was allocated earlier in *this* function; that case
                 is rarer and needs flow-sensitive analysis. */
              safe_p = 1;
            else if (rhs->code == N_CALL) {
              node_t fn = NL_HEAD (rhs->u.ops);
              if (fn != NULL
                  && (fn->code == N_FIELD || fn->code == N_DEREF_FIELD)) {
                node_t mid = NL_EL (fn->u.ops, 1);
                if (mid != NULL && mid->code == N_ID
                    && strcmp (mid->u.s.s, "detach") == 0)
                  safe_p = 1;
              }
            }
            /* Opt out via `__attribute__((da_ignore))` on the enclosing
               function/method (list.h / map.h / set.h already annotate their
               methods).  No blanket ".h" silence — the attribute is the
               intentional contract for stdlib internals. */
            if (!safe_p) {
              int ignore_p = 0;
              if (curr_func_def != NULL && curr_func_def->attr != NULL
                  && curr_func_def->attr != (void *) ((intptr_t) -1)
                  && curr_func_def->attr != PRECHECK_DA_IGNORE) {
                decl_t fdecl = (decl_t) curr_func_def->attr;
                if (fdecl->da_ignore_p) ignore_p = 1;
              }
              if (!ignore_p)
                warning (c2m_ctx, POS (r),
                         "String stored through pointer may be freed when "
                         "this function returns (use-after-free); return the "
                         "String by value, or store `detach <expr>` / "
                         "`<expr>.detach()` so it survives the scope");
            }
          }
        }
        *e->type = *t1;
        if ((e->type2 = assign_expr_type) != NULL) set_type_layout (c2m_ctx, assign_expr_type);
        break;
      case N_IND:
        process_bin_ops (c2m_ctx, r, &op1, &op2, &e1, &e2, &t1, &t2, r);
        /* C allows `i[a]` as `a[i]` when one side is a pointer/array.  Do not
           swap when the primary is a class value (e.g. stack Map): `m["k"]`
           would otherwise become `"k"[m]` and report "array subscript is not
           an integer".  Heap Map* is already TM_PTR and skips this swap. */
        if (t1->mode != TM_PTR && t1->mode != TM_ARR && t1->mode != TM_DICT
            && t1->mode != TM_CLASS && t1->mode != TM_SLICE
            && (t2->mode == TM_PTR || t2->mode == TM_ARR)) {
          struct type *temp;
          node_t op;

          SWAP (t1, t2, temp);
          SWAP (e1, e2, e);
          SWAP (op1, op2, op);
          NL_REMOVE (r->u.ops, op1);
          NL_APPEND (r->u.ops, op1);
        }
        e = create_expr (c2m_ctx, r);
        e->u.lvalue_node = r;
        e->type->mode = TM_BASIC;
        e->type->u.basic_type = TP_INT;
        if (t1->mode == TM_DICT) {
          /* d["key"] — dict bracket subscript */
          e->type->mode = TM_DICT;
          if (!t2 || (t2->mode != TM_PTR && t2->mode != TM_ARR && !integer_type_p(t2))) {
            error (c2m_ctx, POS (r), "dict subscript must be a string or integer");
          }
        } else if (t1->mode == TM_SLICE) {
          /* slice[i] — filter/map result element access (read/write lvalue) */
          *e->type = *t1->u.ptr_type;
        } else if (t1->mode == TM_CLASS
                   || (t1->mode == TM_PTR && t1->u.ptr_type != NULL
                       && t1->u.ptr_type->mode == TM_CLASS)) {
          /* class[i] / Class*[i] — bracket subscript via Get/Set protocol.
             Developers expect uniform collection sugar:
               list[i] / list_ptr[i]  → Get/Set element
               map[k]  / map_ptr[k]  → Get/Set value
             A plain pointer to a class *without* Get (e.g. Point*, or the
             T* dense buffer of List<Point>) falls through to C raw indexing.
             Nested List-of-List dense slots must use *(data+i) in library code
             rather than data[i], so List* sugar stays ergonomic here. */
          struct type *cls = t1->mode == TM_PTR ? t1->u.ptr_type : t1;
          node_t get_def = find_class_protocol_method (c2m_ctx, cls->u.tag_type, "Get", 1, POS (r));
          if (get_def == NULL && t1->mode == TM_PTR) {
            /* Ordinary C pointer to class — dense element array indexing. */
            *e->type = *t1->u.ptr_type;
            if (incomplete_type_p (c2m_ctx, t1->u.ptr_type))
              error (c2m_ctx, POS (r), "pointer to incomplete type in array subscription");
            if (t2 != NULL && !integer_type_p (t2))
              error (c2m_ctx, POS (r), "array subscript is not an integer");
          } else if (get_def == NULL) {
            error (c2m_ctx, POS (r), "class type has no Get(int) method for [] subscript");
          } else {
            decl_t gd = get_def->attr;
            struct type *key_pt = NULL;
            if (gd != NULL && gd->decl_spec.type != NULL && gd->decl_spec.type->mode == TM_FUNC) {
              struct func_type *gft = gd->decl_spec.type->u.func_type;
              node_t gparam = NL_HEAD (gft->param_list->u.ops);
              struct decl_spec *gpds;
              if (gparam != NULL) gparam = NL_NEXT (gparam); /* skip implicit 'this' */
              gpds = get_param_decl_spec (gparam);
              if (gpds != NULL) key_pt = gpds->type;
              if (gft->ret_type != NULL) *e->type = *gft->ret_type;
            }
            /* Two flavours of the subscript protocol, distinguished by Get's key
               parameter type.  Integer-indexed collections (List/Set: Get(int))
               require an integer index.  Keyed collections (Map<K,V>: Get(K))
               accept any index assignable to the key type K — so map["name"]
               works for Map<String,V>. */
            if (key_pt == NULL || integer_type_p (key_pt)) {
              if (t2 != NULL && !integer_type_p (t2))
                error (c2m_ctx, POS (r), "class subscript index must be an integer");
            } else if (t2 != NULL) {
              node_t key_node = NL_EL (r->u.ops, 1);
              int ok = type_eq_p (key_pt, t2)
                       || compatible_types_p (key_pt, t2, TRUE)
                       || (builtin_string_type_p (key_pt)
                           && str_concat_string_operand_p (t2, key_node))
                       || (arithmetic_type_p (key_pt) && arithmetic_type_p (t2));
              if (!ok)
                error (c2m_ctx, POS (r), "class subscript key has incompatible type");
            }
            /* Prefer GetMut for *by-value class/struct/union* elements only:
               list[i].Method() / map[k].Method() mutate the buffer (not a Get
               copy).  Scalars and pointer elements keep Get — missing Map keys
               must still throw, and Map.GetMut would return NULL → crash on MEM. */
            {
              node_t getmut_def
                = find_class_protocol_method (c2m_ctx, cls->u.tag_type, "GetMut", 1, POS (r));
              int agg_el = (e->type->mode == TM_CLASS || e->type->mode == TM_STRUCT
                            || e->type->mode == TM_UNION);
              if (getmut_def != NULL && agg_el) {
                e->mut_sub_p = 1;
                e->def_node = getmut_def;
                if (getmut_def->attr != NULL) ((decl_t) getmut_def->attr)->used_p = TRUE;
              } else {
                e->def_node = get_def; /* stash Get for gen */
                if (get_def != NULL && get_def->attr != NULL)
                  ((decl_t) get_def->attr)->used_p = TRUE;
              }
            }
          }
        } else if (t1->mode != TM_PTR && t1->mode != TM_ARR) {
          error (c2m_ctx, POS (r), "subscripted value is neither array nor pointer");
        } else if (t1->mode == TM_PTR) {
          *e->type = *t1->u.ptr_type;
          if (incomplete_type_p (c2m_ctx, t1->u.ptr_type)) {
            error (c2m_ctx, POS (r), "pointer to incomplete type in array subscription");
          }
        } else {
          *e->type = *t1->u.arr_type->el_type;
          e->type->type_qual = t1->u.arr_type->ind_type_qual;
          if (incomplete_type_p (c2m_ctx, e->type)) {
            error (c2m_ctx, POS (r), "array type has incomplete element type");
          }
        }
        if (t1->mode != TM_DICT && t1->mode != TM_CLASS
            && !(t1->mode == TM_PTR && t1->u.ptr_type != NULL
                 && t1->u.ptr_type->mode == TM_CLASS)
            && t2 != NULL && !integer_type_p (t2)) {
          error (c2m_ctx, POS (r), "array subscript is not an integer");
        }
        break;
      case N_IN: {
        /* "key" in dict  —  existence check, result is int */
        process_bin_ops (c2m_ctx, r, &op1, &op2, &e1, &e2, &t1, &t2, r);
        e = create_expr (c2m_ctx, r);
        e->type->mode = TM_BASIC;
        e->type->u.basic_type = TP_INT;
        if (t2->mode != TM_DICT) {
          error (c2m_ctx, POS (r), "right operand of 'in' must be a dict");
        }
        break;
      }
      case N_ANY: {
        /* any<I>(expr): wrap a concrete C* into an erased Any<I>* handle.
           Children at parse: iface_id(0), struct_id(1, __Any_I), arg(2).
           Conformance is verified STRUCTURALLY (no `impl` required, Step 1.3);
           the node is then lowered to a call to a monomorphized factory
           __any_make_<I>_<C>(arg) whose thunks/factory are injected at module
           scope (Step 2.4).  After lowering r->ops = [iface_id, struct_id, call]. */
        node_t iface_id = NL_HEAD (r->u.ops);
        node_t struct_id = NL_NEXT (iface_id);
        node_t arg = NL_NEXT (struct_id);
        struct expr *ae;
        struct type *at;
        node_t c_tag, iface, callee, args, call;
        const char *cname, *missing = NULL, *factory_name = NULL;

        if (!arg->attr) check (c2m_ctx, arg, r);
        ae = arg->attr;
        at = ae->type;
        e = create_expr (c2m_ctx, r);
        e->type->mode = TM_UNDEF;
        if (at == NULL || at->mode != TM_PTR || at->u.ptr_type->mode != TM_CLASS) {
          error (c2m_ctx, POS (r), "any<%s>(x): argument must be a pointer to a class",
                 iface_id->u.s.s);
          break;
        }
        c_tag = at->u.ptr_type->u.tag_type;
        cname = class_type_name (at->u.ptr_type);
        iface = find_interface (c2m_ctx, iface_id->u.s.s);
        if (iface == NULL) {
          error (c2m_ctx, POS (r), "any<%s>: '%s' is not a declared interface",
                 iface_id->u.s.s, iface_id->u.s.s);
          break;
        }
        if (cname == NULL || !class_satisfies_interface_p (c2m_ctx, c_tag, iface, &missing)) {
          error (c2m_ctx, POS (r),
                 "any<%s>: class %s does not satisfy interface %s: missing %s()",
                 iface_id->u.s.s, cname ? cname : "?", iface_id->u.s.s,
                 missing ? missing : "method");
          break;
        }
        /* Monomorphize the (C, I) thunks + factory once and inject them into the
           module item list just before the item currently being checked, so their
           MIR is generated ahead of this call site.  Each item is checked through
           check_lambda_func_def, which saves/restores the enclosing function's
           check context (curr_func_def, func_decls_for_allocation, ...) — the same
           machinery sequence-lambda instantiation uses. */
        {
          node_t items = synthesize_any_thunks (c2m_ctx, iface_id->u.s.s, struct_id->u.s.s,
                                                cname, &factory_name);
          if (items != NULL && module_item_list != NULL && curr_module_item != NULL) {
            node_t arr[64];
            int n = 0;
            for (node_t it = NL_HEAD (items->u.ops); it != NULL && n < 64; it = NL_NEXT (it))
              arr[n++] = it;
            for (int k = 0; k < n; k++) {
              NL_REMOVE (items->u.ops, arr[k]);
              DLIST_INSERT_BEFORE (node_t, module_item_list->u.ops, curr_module_item, arr[k]);
              check_lambda_func_def (c2m_ctx, arr[k]);
            }
          }
        }
        if (factory_name == NULL) break;
        /* Lower to the factory call: factory_name(arg).  Move arg out of r into
           the call's arg list (a node lives in exactly one op list). */
        NL_REMOVE (r->u.ops, arg);
        callee = build_id (c2m_ctx, factory_name, POS (r));
        args = new_node (c2m_ctx, N_LIST);
        op_append (c2m_ctx, args, arg);
        call = new_pos_node2 (c2m_ctx, N_CALL, POS (r), callee, args);
        op_append (c2m_ctx, r, call);
        check (c2m_ctx, call, r);
        if (call->attr != NULL) *e->type = *((struct expr *) call->attr)->type;
        break;
      }
      case N_NEW: {
        /* new ClassName(args): allocate on the heap and run the constructor.
           children: type_id(0, N_ID of class), arg_list(1, N_LIST).
           Result type is a pointer to the class.  e->def_node is set to the
           constructor N_FUNC_DEF (or NULL when the class has none) for gen.

           new dict(size?)  is a special form handled here first: it creates a
           heap-arena-backed empty dict and returns a TM_DICT value.  The
           optional size argument (bytes) is passed through to gen. */
        node_t type_id = NL_HEAD (r->u.ops);
        node_t arg_list = NL_NEXT (type_id);
        node_t class_def, ctor_def, ctor_id, arg, param;
        char ctor_name[300];
        struct type *class_type;

        int has_named = FALSE;

        /* ── new dict(size?) ─────────────────────────────────────── */
        if (type_id->code == N_DICT) {
          e = create_expr (c2m_ctx, r);
          e->type->mode = TM_DICT;
          e->type->raw_size = sizeof (void *);
          e->type->align = sizeof (void *);
          /* Validate optional size argument if present */
          arg = NL_HEAD (arg_list->u.ops);
          if (arg != NULL) {
            check (c2m_ctx, arg, r);
            struct expr *ae = arg->attr;
            if (!integer_type_p (ae->type))
              error (c2m_ctx, POS (arg), "new dict() size must be an integer");
          }
          break;
        }

        class_def = find_def (c2m_ctx, S_REGULARS, type_id, curr_scope, NULL);
        e = create_expr (c2m_ctx, r);
        if (class_def == NULL || class_def->code != N_CLASS) {
          error (c2m_ctx, POS (r), "'new' requires a class type, '%s' is not a class",
                 type_id->u.s.s);
          e->type->mode = TM_UNDEF;
          break;
        }
        class_type = create_class_type (c2m_ctx, class_def);
        set_class_layout (c2m_ctx, class_def, class_type);
        e->type->mode = TM_PTR;
        e->type->u.ptr_type = class_type;
        set_type_layout (c2m_ctx, e->type);

        snprintf (ctor_name, sizeof (ctor_name), "__ctor_%s", type_id->u.s.s);
        ctor_id = build_id (c2m_ctx, ctor_name, POS (r));

        for (arg = NL_HEAD (arg_list->u.ops); arg != NULL; arg = NL_NEXT (arg))
          if (arg->code == N_FIELD_ID) has_named = TRUE;

        /* Look up all constructor overloads registered under __ctor_<ClassName>.
           Multiple constructors with different signatures are stored as overloads
           in the same symbol (exactly like regular method overloads). */
        symbol_t ctor_sym;
        int has_ctor_sym = find_overload_sym (c2m_ctx, ctor_id, curr_scope, &ctor_sym);
        ctor_def = has_ctor_sym ? ctor_sym.def_node : NULL;

        /* Named arguments (new Foo(b=2, a=1)): find the constructor whose
           parameter names match the supplied names (scanning all overloads when
           there are several), then reorder into positional order. */
        if (has_named) {
          if (!has_ctor_sym) {
            error (c2m_ctx, POS (r),
                   "named arguments require a constructor for class '%s'", type_id->u.s.s);
          } else {
            /* With multiple overloads, pick the one that owns all named params. */
            if (VARR_LENGTH (node_t, ctor_sym.defs) > 1) {
              ctor_def = NULL;
              for (size_t ci = 0; ci < VARR_LENGTH (node_t, ctor_sym.defs); ci++) {
                node_t cand = VARR_GET (node_t, ctor_sym.defs, ci);
                if (cand == NULL || cand->code != N_FUNC_DEF) continue;
                decl_t cd = cand->attr;
                if (cd == NULL || cd->decl_spec.type == NULL
                    || cd->decl_spec.type->mode != TM_FUNC) continue;
                struct func_type *cft = cd->decl_spec.type->u.func_type;
                int all_found = TRUE;
                for (node_t a2n = NL_HEAD (arg_list->u.ops); a2n != NULL; a2n = NL_NEXT (a2n)) {
                  if (a2n->code != N_FIELD_ID) continue;
                  node_t an = NL_HEAD (a2n->u.ops);
                  const char *aname = (an && an->code == N_ID) ? an->u.s.s : NULL;
                  if (!aname) { all_found = FALSE; break; }
                  int found_p = FALSE;
                  node_t cp = NL_HEAD (cft->param_list->u.ops);
                  if (cp != NULL) cp = NL_NEXT (cp); /* skip 'this' */
                  for (; cp != NULL; cp = NL_NEXT (cp)) {
                    node_t pdeclr = NL_EL (cp->u.ops, 1);
                    node_t pid = (pdeclr && pdeclr->code == N_DECL)
                                   ? NL_HEAD (pdeclr->u.ops) : NULL;
                    if (pid && pid->code == N_ID && strcmp (pid->u.s.s, aname) == 0)
                      { found_p = TRUE; break; }
                  }
                  if (!found_p) { all_found = FALSE; break; }
                }
                if (all_found) { ctor_def = cand; break; }
              }
              if (ctor_def == NULL) ctor_def = ctor_sym.def_node; /* fallback to first */
            }

            if (ctor_def == NULL || ctor_def->code != N_FUNC_DEF) {
              error (c2m_ctx, POS (r),
                     "named arguments require a constructor for class '%s'", type_id->u.s.s);
            } else {
              decl_t cdecl = ctor_def->attr;
              struct func_type *ft = cdecl->decl_spec.type->u.func_type;
              node_t reordered = new_node (c2m_ctx, N_LIST);
              node_t a2;
              param = NL_HEAD (ft->param_list->u.ops);
              if (param != NULL) param = NL_NEXT (param); /* skip 'this' */
              for (; param != NULL; param = NL_NEXT (param)) {
                node_t pdeclr = NL_EL (param->u.ops, 1);
                node_t pid = (pdeclr != NULL && pdeclr->code == N_DECL)
                               ? NL_HEAD (pdeclr->u.ops) : NULL;
                const char *pname = (pid != NULL && pid->code == N_ID) ? pid->u.s.s : NULL;
                node_t found_val = NULL, name_node = NULL;
                for (a2 = NL_HEAD (arg_list->u.ops); a2 != NULL; a2 = NL_NEXT (a2)) {
                  node_t an;
                  if (a2->code != N_FIELD_ID) continue;
                  an = NL_HEAD (a2->u.ops);
                  if (pname != NULL && an->code == N_ID && NL_NEXT (an) != NULL
                      && strcmp (an->u.s.s, pname) == 0) {
                    name_node = an;
                    found_val = NL_NEXT (an);
                    break;
                  }
                }
                if (found_val == NULL) {
                  error (c2m_ctx, POS (r),
                         "no argument provided for constructor parameter '%s'",
                         pname != NULL ? pname : "?");
                } else {
                  NL_REMOVE (a2->u.ops, found_val); /* detach value from its marker */
                  op_append (c2m_ctx, reordered, found_val);
                  (void) name_node;
                }
              }
              /* Any named marker that still owns its value names an unknown param. */
              for (a2 = NL_HEAD (arg_list->u.ops); a2 != NULL; a2 = NL_NEXT (a2)) {
                node_t an;
                if (a2->code != N_FIELD_ID) continue;
                an = NL_HEAD (a2->u.ops);
                if (NL_NEXT (an) != NULL)
                  error (c2m_ctx, POS (r), "unknown constructor parameter '%s'", an->u.s.s);
              }
              /* Replace arg_list contents with the positional value list. */
              for (a2 = NL_HEAD (arg_list->u.ops); a2 != NULL;) {
                node_t nx = NL_NEXT (a2);
                NL_REMOVE (arg_list->u.ops, a2);
                a2 = nx;
              }
              for (a2 = NL_HEAD (reordered->u.ops); a2 != NULL;) {
                node_t nx = NL_NEXT (a2);
                NL_REMOVE (reordered->u.ops, a2);
                op_append (c2m_ctx, arg_list, a2);
                a2 = nx;
              }
            }
          }
        }

        /* Type-check all arguments.  For positional calls this must happen
           before overload selection so each argument's type is known for
           scoring.  Named-arg calls have already been reordered above. */
        for (arg = NL_HEAD (arg_list->u.ops); arg != NULL; arg = NL_NEXT (arg))
          check (c2m_ctx, arg, r);

        /* For positional calls: pick the best-matching constructor overload now
           that all argument types are known.  Named-arg calls already set
           ctor_def during the reordering step above. */
        if (!has_named && has_ctor_sym) {
          ctor_def = select_method_overload (c2m_ctx, &ctor_sym, class_def, arg_list);
          /* Collection convenience: a short call passing a sized collection where
             a (T* items, int count) constructor is expected fills in the count,
             e.g. new List<T>(arr) == new List<T>(arr, arr.count()). */
          {
            node_t expanded = select_overload_with_count_expansion (c2m_ctx, r, &ctor_sym, TRUE);
            if (expanded != NULL) ctor_def = expanded;
          }
        }

        if (ctor_def != NULL && ctor_def->code == N_FUNC_DEF) {
          decl_t cdecl = ctor_def->attr;
          struct func_type *ft = cdecl->decl_spec.type->u.func_type;
          e->def_node = ctor_def;
          if (cdecl != NULL) cdecl->used_p = TRUE;
          param = NL_HEAD (ft->param_list->u.ops);
          if (param != NULL) param = NL_NEXT (param); /* skip implicit 'this' */
          arg = NL_HEAD (arg_list->u.ops);
          for (; arg != NULL && param != NULL;
               arg = NL_NEXT (arg), param = NL_NEXT (param)) {
            struct decl_spec *pds = get_param_decl_spec (param);
            check_assignment_types (c2m_ctx, pds->type, NULL, arg->attr, r);
          }
          if (arg != NULL)
            error (c2m_ctx, POS (r), "too many arguments to constructor of '%s'",
                   type_id->u.s.s);
          if (param != NULL)
            error (c2m_ctx, POS (r), "too few arguments to constructor of '%s'",
                   type_id->u.s.s);
        } else {
          e->def_node = NULL;
          if (NL_HEAD (arg_list->u.ops) != NULL)
            error (c2m_ctx, POS (r),
                   "class '%s' has no constructor but arguments were given",
                   type_id->u.s.s);
        }

        /* Brace-init:  new T{e1, e2, ...}  (init_list is the optional third
           child).  Protocol: the class must have an instance method 'Add'
           taking exactly one argument; each element is type-checked against
           that parameter.  Gen re-resolves Add via the same helper.

           Object-initializer form:  new T(args) { .field = value, ... } uses
           the same third child but with N_FIELD_ID(name, value) elements;
           each names a data member that is assigned post-construction. */
        {
          node_t init_list = NL_NEXT (arg_list);
          node_t first_init = (init_list != NULL) ? NL_HEAD (init_list->u.ops) : NULL;

          if (first_init != NULL && first_init->code == N_FIELD_ID) {
            /* Object initializer: type-check each `.field = value` against the
               named member's declared type. */
            for (node_t el = NL_HEAD (init_list->u.ops); el != NULL; el = NL_NEXT (el)) {
              node_t fname = (el->code == N_FIELD_ID) ? NL_HEAD (el->u.ops) : NULL;
              node_t fval = (fname != NULL) ? NL_NEXT (fname) : NULL;
              node_t mem;
              if (fname == NULL || fname->code != N_ID || fval == NULL) {
                error (c2m_ctx, POS (el),
                       "malformed object initializer for class '%s'", type_id->u.s.s);
                continue;
              }
              check (c2m_ctx, fval, el);
              mem = find_class_field_member (c2m_ctx, class_def, fname->u.s.s);
              if (mem == NULL) {
                error (c2m_ctx, POS (el),
                       "class '%s' has no field '%s' to initialize",
                       type_id->u.s.s, fname->u.s.s);
              } else {
                decl_t md = (decl_t) mem->attr;
                if (md != NULL && md->decl_spec.type != NULL)
                  check_assignment_types (c2m_ctx, md->decl_spec.type, NULL, fval->attr, el);
              }
            }
            break;
          }
          if (init_list != NULL) {
            node_t el, add_def;

            for (el = NL_HEAD (init_list->u.ops); el != NULL; el = NL_NEXT (el))
              check (c2m_ctx, el, r);
            add_def = find_class_protocol_method (c2m_ctx, class_def, "Add", 1, POS (r));
            if (add_def == NULL) {
              error (c2m_ctx, POS (r),
                     "class '%s' does not support brace initialization "
                     "(needs an 'Add' method taking one argument)",
                     type_id->u.s.s);
            } else {
              decl_t adecl = add_def->attr;
              struct func_type *aft;
              if (adecl != NULL) adecl->used_p = TRUE; /* midopt: gen emits Add calls */
              aft = adecl->decl_spec.type->u.func_type;
              node_t aparam = NL_HEAD (aft->param_list->u.ops);
              struct decl_spec *apds;

              if (aparam != NULL) aparam = NL_NEXT (aparam); /* skip 'this' */
              apds = get_param_decl_spec (aparam);
              if (!void_type_p (aft->ret_type) && !scalar_type_p (aft->ret_type)) {
                error (c2m_ctx, POS (r),
                       "brace initialization requires 'Add' of class '%s' to return "
                       "void or a scalar type",
                       type_id->u.s.s);
              } else {
                node_t el_next;
                for (el = NL_HEAD (init_list->u.ops); el != NULL; el = el_next) {
                  node_t coerced;
                  el_next = NL_NEXT (el);
                  /* Implicitly erase a concrete element into Any<I> when Add's
                     parameter is an erased handle (declarative builder sugar). */
                  if ((coerced = try_coerce_to_any (c2m_ctx, init_list, el, apds->type)) != NULL)
                    el = coerced;
                  check_assignment_types (c2m_ctx, apds->type, NULL, el->attr, el);
                }
              }
            }
          }
        }
        break;
      }
      case N_FORIN: {
        /* for (auto key[, val] in collection) body
           children: labels(0), key_id(1), val_id_or_ignore(2), collection(3), body(4) */
        node_t labels = NL_HEAD (r->u.ops);
        node_t key_id = NL_NEXT (labels);
        node_t val_id = NL_NEXT (key_id);
        node_t collection = NL_NEXT (val_id);
        node_t body = NL_NEXT (collection);
        node_t saved_loop = curr_loop;
        node_t saved_loop_switch = curr_loop_switch;

        check_labels (c2m_ctx, labels, r);
        create_node_scope (c2m_ctx, r);
        check (c2m_ctx, collection, r);
        e1 = collection->attr;
        int forin_arr_p = (e1 && (e1->type->mode == TM_ARR
                                 || (e1->type->mode == TM_PTR && e1->type->arr_type != NULL)));
        int forin_slice_p = (e1 && e1->type->mode == TM_SLICE);
        int forin_class_p
          = (e1 && !forin_arr_p
             && ((e1->type->mode == TM_PTR && e1->type->u.ptr_type->mode == TM_CLASS)
                 || e1->type->mode == TM_CLASS));
        if (e1 && e1->type->mode != TM_DICT && !forin_arr_p && !forin_slice_p && !forin_class_p)
          error (c2m_ctx, POS (r),
                 "for-in collection must be a dict, array, slice, or class with Count()/Get(int)");

        /* Determine element type for arrays/slices (needed for loop var type) */
        struct type *forin_el_type = NULL;
        if (forin_arr_p) {
          struct type *orig = (e1->type->mode == TM_ARR ? e1->type : e1->type->arr_type);
          forin_el_type = orig->u.arr_type->el_type;
        } else if (forin_slice_p) {
          forin_el_type = e1->type->u.ptr_type;
        }

        /* ----- declare auto loop variable(s) in the symbol table ----- */
        /* Helper macro to declare one forin loop variable */
        #define FORIN_DECL_VAR(id_node, var_type) do { \
          node_t _copy = copy_node(c2m_ctx, id_node); \
          node_t _sd = build_spec_decl(c2m_ctx, POS(id_node), \
              new_node(c2m_ctx, N_IGNORE), \
              new_pos_node2(c2m_ctx, N_DECL, POS(id_node), _copy, \
                            new_node(c2m_ctx, N_LIST)), \
              NULL, NULL, NULL); \
          decl_t _d = reg_malloc(c2m_ctx, sizeof(struct decl)); \
          init_decl(c2m_ctx, _d); \
          _d->scope = curr_scope; \
          init_decl_spec(&_d->decl_spec); \
          _d->decl_spec.type = (var_type); \
          set_type_layout(c2m_ctx, _d->decl_spec.type); \
          _d->reg_p = scalar_type_p(_d->decl_spec.type); \
          _sd->attr = _d; \
          /* Aggregate (class/struct/union) loop variables need a stack slot, \
             not a register; register them for frame allocation. */ \
          if (!_d->reg_p && curr_scope != top_scope) \
            VARR_PUSH (decl_t, func_decls_for_allocation, _d); \
          symbol_insert(c2m_ctx, S_REGULARS, _copy, curr_scope, _sd, NULL); \
        } while(0)

        if (forin_arr_p || forin_slice_p) {
          /* for (auto x in array) — single var gets element type.
             for (auto i, x in array) — i=index(int), x=element. */
          if (val_id->code == N_ID) {
            /* Two-variable form: key_id = index, val_id = element */
            struct type *idx_type = create_type(c2m_ctx, NULL);
            init_type(idx_type); idx_type->mode = TM_BASIC;
            idx_type->u.basic_type = TP_INT; idx_type->pos_node = key_id;
            FORIN_DECL_VAR(key_id, idx_type);
            struct type *el_copy = create_type(c2m_ctx, NULL);
            *el_copy = *forin_el_type; el_copy->pos_node = val_id;
            FORIN_DECL_VAR(val_id, el_copy);
          } else {
            /* Single-variable form: key_id = element */
            struct type *el_copy = create_type(c2m_ctx, NULL);
            *el_copy = *forin_el_type; el_copy->pos_node = key_id;
            FORIN_DECL_VAR(key_id, el_copy);
          }
        } else if (forin_class_p) {
          /* Class with the Count/Get iteration protocol (duck-typed):
               for (auto x in coll)    — x = coll->Get(i) for i in [0, coll->Count())
               for (auto i, x in coll) — i = index (int), x = element
             The element type is Get's return type.

             A keyed collection (Map<K,V>: Count()/KeyAt(int)/ValAt(int)) takes
             priority and binds (key, value) instead of (index, element):
               for (auto k in map)    — k = map->KeyAt(i)
               for (auto k, v in map) — k = key, v = value
             so iteration mirrors the built-in dict's `for (auto k, val in d)`. */
          struct type *cls_type = e1->type->mode == TM_PTR ? e1->type->u.ptr_type : e1->type;
          const char *cls_name = class_type_name (cls_type);
          node_t class_tag = cls_type->u.tag_type;
          node_t count_def = find_class_protocol_method (c2m_ctx, class_tag, "Count", 0, POS (r));
          node_t keyat_def = find_class_protocol_method (c2m_ctx, class_tag, "KeyAt", 1, POS (r));
          node_t valat_def = find_class_protocol_method (c2m_ctx, class_tag, "ValAt", 1, POS (r));
          node_t get_def = find_class_protocol_method (c2m_ctx, class_tag, "Get", 1, POS (r));
          struct type *el_type = NULL;

          if (cls_name == NULL) cls_name = "?";
          int map_forin = (count_def != NULL && keyat_def != NULL && valat_def != NULL);
          if (map_forin) {
            /* ---- Keyed (map) protocol: KeyAt(i)->K, ValAt(i)->V ---- */
            decl_t cd = count_def->attr, kd = keyat_def->attr, vd = valat_def->attr;
            struct type *kt = kd->decl_spec.type->u.func_type->ret_type;
            struct type *vt = vd->decl_spec.type->u.func_type->ret_type;

            if (!integer_type_p (cd->decl_spec.type->u.func_type->ret_type))
              error (c2m_ctx, POS (r),
                     "for-in: 'Count()' of class '%s' must return an integer type", cls_name);
            /* KeyAt/ValAt may return scalars, pointers, or by-value class/struct
               (same rules as List.Get — aggregate loop vars are stack slots). */
            if (kt == NULL
                || (!scalar_type_p (kt) && kt->mode != TM_CLASS && kt->mode != TM_STRUCT
                    && kt->mode != TM_UNION)) {
              error (c2m_ctx, POS (r),
                     "for-in: 'KeyAt(int)' of class '%s' must return a scalar, pointer, or "
                     "by-value class/struct type",
                     cls_name);
              kt = create_basic_type (c2m_ctx, TP_INT);
            }
            if (vt == NULL
                || (!scalar_type_p (vt) && vt->mode != TM_CLASS && vt->mode != TM_STRUCT
                    && vt->mode != TM_UNION)) {
              error (c2m_ctx, POS (r),
                     "for-in: 'ValAt(int)' of class '%s' must return a scalar, pointer, or "
                     "by-value class/struct type",
                     cls_name);
              vt = create_basic_type (c2m_ctx, TP_INT);
            }
            {
              struct type *k_copy = create_type (c2m_ctx, NULL);
              *k_copy = *kt; k_copy->pos_node = key_id;
              FORIN_DECL_VAR (key_id, k_copy);
            }
            if (val_id->code == N_ID) {
              struct type *v_copy = create_type (c2m_ctx, NULL);
              *v_copy = *vt; v_copy->pos_node = val_id;
              FORIN_DECL_VAR (val_id, v_copy);
            }
          } else if (count_def == NULL || get_def == NULL) {
            error (c2m_ctx, POS (r),
                   "class '%s' is not iterable (needs 'Count()' and 'Get(int)' methods)",
                   cls_name);
          } else {
            decl_t cd = count_def->attr, gd = get_def->attr;
            struct func_type *gft = gd->decl_spec.type->u.func_type;
            node_t gparam = NL_HEAD (gft->param_list->u.ops);
            struct decl_spec *gpds;

            if (!integer_type_p (cd->decl_spec.type->u.func_type->ret_type))
              error (c2m_ctx, POS (r),
                     "for-in: 'Count()' of class '%s' must return an integer type", cls_name);
            if (gparam != NULL) gparam = NL_NEXT (gparam); /* skip 'this' */
            gpds = get_param_decl_spec (gparam);
            if (gpds == NULL || !integer_type_p (gpds->type)) {
              error (c2m_ctx, POS (r),
                     "for-in: 'Get' of class '%s' must take a single integer index", cls_name);
            } else if (!scalar_type_p (gft->ret_type)
                       && gft->ret_type->mode != TM_CLASS
                       && gft->ret_type->mode != TM_STRUCT
                       && gft->ret_type->mode != TM_UNION) {
              error (c2m_ctx, POS (r),
                     "for-in: 'Get' of class '%s' must return a scalar, pointer, or by-value "
                     "class/struct type", cls_name);
            } else {
              el_type = gft->ret_type;
            }
          }
          /* Indexed (List/Set) protocol only — map vars were already declared. */
          if (!map_forin) {
            /* On protocol errors fall back to int so the body check can proceed. */
            if (el_type == NULL) el_type = create_basic_type (c2m_ctx, TP_INT);
            if (val_id->code == N_ID) {
              /* Two-variable form: key_id = index, val_id = element */
              struct type *idx_type = create_basic_type (c2m_ctx, TP_INT);
              idx_type->pos_node = key_id;
              FORIN_DECL_VAR (key_id, idx_type);
              struct type *el_copy = create_type (c2m_ctx, NULL);
              *el_copy = *el_type; el_copy->pos_node = val_id;
              FORIN_DECL_VAR (val_id, el_copy);
            } else {
              /* Single-variable form: key_id = element */
              struct type *el_copy = create_type (c2m_ctx, NULL);
              *el_copy = *el_type; el_copy->pos_node = key_id;
              FORIN_DECL_VAR (key_id, el_copy);
            }
          }
        } else {
          /* Dict: key_id = char* , value_id = TM_DICT */
          struct type *char_t = create_basic_type(c2m_ctx, TP_CHAR);
          struct type *kt = create_ptr_type(c2m_ctx, char_t);
          kt->pos_node = key_id;
          FORIN_DECL_VAR(key_id, kt);
          if (val_id->code == N_ID) {
            struct type *vt = create_type(c2m_ctx, NULL);
            vt->mode = TM_DICT; vt->pos_node = val_id;
            FORIN_DECL_VAR(val_id, vt);
          }
        }
        #undef FORIN_DECL_VAR

        curr_loop = curr_loop_switch = r;
        check (c2m_ctx, body, r);
        finish_scope (c2m_ctx);
        curr_loop_switch = saved_loop_switch;
        curr_loop = saved_loop;
        break;
      }
      case N_LABEL_ADDR:
        e = create_expr (c2m_ctx, r);
        e->type->mode = TM_PTR;
        e->type->u.ptr_type = &VOID_TYPE;
        VARR_PUSH (node_t, label_uses, r);
        break;
      case N_ADDR:
        process_unop (c2m_ctx, r, &op1, &e1, &t1, r);
        assert (t1->mode != TM_ARR);
        e = create_expr (c2m_ctx, r);
        if (op1->code == N_DEREF) {
          node_t deref_op = NL_HEAD (op1->u.ops);

          *e->type = *((struct expr *) deref_op->attr)->type;
          break;
        } else if (e1->type->mode == TM_PTR && e1->type->arr_type != NULL) {
          /* &array-lvalue is pointer-to-ARRAY, not the decayed element ptr
             (C11 6.5.3.2 / 8a6a6c57). */
          e->type->mode = TM_PTR;
          e->type->u.ptr_type = e1->type->arr_type;
          break;
        } else if (e1->type->mode == TM_PTR && e1->type->u.ptr_type->mode == TM_FUNC
                   && e1->type->func_type_before_adjustment_p) {
          *e->type = *e1->type;
          break;
        } else if (!e1->u.lvalue_node) {
          e->type->mode = TM_BASIC;
          e->type->u.basic_type = TP_INT;
          error (c2m_ctx, POS (r), "lvalue required as unary & operand");
          break;
        }
        if (op1->code == N_IND) {
          t2 = create_type (c2m_ctx, t1);
        } else if (op1->code == N_ID) {
          node_t decl_node = e1->u.lvalue_node;
          decl_t decl = decl_node->attr;

          decl->addr_p = TRUE;
          if (decl->decl_spec.register_p)
            error (c2m_ctx, POS (r), "address of register variable %s requested", op1->u.s.s);
          t2 = create_type (c2m_ctx, decl->decl_spec.type);
        } else if (e1->u.lvalue_node->code == N_MEMBER) {
          node_t declarator = NL_EL (e1->u.lvalue_node->u.ops, 1);
          node_t width = NL_NEXT (NL_NEXT (declarator));
          decl_t decl = e1->u.lvalue_node->attr;

          assert (declarator->code == N_DECL);
          if (width->code != N_IGNORE) {
            error (c2m_ctx, POS (r), "cannot take address of bit-field %s",
                   NL_HEAD (declarator->u.ops)->u.s.s);
          }
          t2 = create_type (c2m_ctx, decl->decl_spec.type);
          if (op1->code == N_DEREF_FIELD && (e2 = NL_HEAD (op1->u.ops)->attr)->const_p) {
            e->const_p = TRUE;
            e->c.u_val = e2->c.u_val + decl->offset;
          }
        } else if (e1->u.lvalue_node->code == N_COMPOUND_LITERAL) {
          t2 = t1;
        } else {
          assert (e1->u.lvalue_node->code == N_STR || e1->u.lvalue_node->code == N_STR16
                  || e1->u.lvalue_node->code == N_STR32);
          t2 = t1;
        }
        if (t2->mode == TM_ARR) {
          e->type = t2;
        } else {
          e->type->mode = TM_PTR;
          e->type->u.ptr_type = t2;
        }
        break;
      case N_DEREF:
        process_unop (c2m_ctx, r, &op1, &e1, &t1, r);
        e = create_expr (c2m_ctx, r);
        e->type->mode = TM_BASIC;
        e->type->u.basic_type = TP_INT;
        if (t1->mode != TM_PTR) {
          error (c2m_ctx, POS (r), "invalid type argument of unary *");
        } else {
          *e->type = *t1->u.ptr_type;
          e->u.lvalue_node = r;
        }
        break;
	      case N_FIELD:
	      case N_DEREF_FIELD: {
	        symbol_t sym;
	        decl_t decl;
	        node_t width, func=NULL, func_op;;
	        struct expr *width_expr, method;

	node_t base = NL_HEAD (r->u.ops);
	/* Two receiver shapes carry a string-ish base node here: the bare `String`
	   type keyword used as a static receiver (String.copy(...)) is an N_STRING
	   node, while an actual UTF-8 string literal used as an instance receiver
	   ("abc".lower()) is an N_STR node. */
	if (base != NULL && (base->code == N_STRING || base->code == N_STR)) {
	  node_t mem = NL_NEXT (base);
	  int literal_recv_p = base->code == N_STR;
	  assert (mem->code == N_ID);
	  if (get_string_method (mem->u.s.s, NULL, NULL) != SM_NONE) {
	    /* For a literal receiver, check the base so it carries a value/type; the
	       N_CALL handler then dispatches it as a String instance method.  For
	       the bare `String` keyword, leave the base unchecked (process_unop
	       would treat bare "String" as a char[] literal). */
	    if (literal_recv_p) check (c2m_ctx, base, r);
	    e = create_expr (c2m_ctx, r);
	    e->type->mode = TM_BASIC;
	    e->type->u.basic_type = TP_VOID;
	    e->u.lvalue_node = NULL;
	    break;
	  } else {
	    error (c2m_ctx, POS (r),
	           literal_recv_p ? "unknown String method '%s'"
	                          : "no static method '%s' on String",
	           mem->u.s.s);
	    break;
	  }
	}

/* Static dispatch on a class name: ClassName.method() where ClassName
   is an N_ID that resolves to a class type (not a variable).  This is
   what makes T::tableName() work after generic specialization: T is
   substituted with the concrete class name, and we treat the base
   N_ID as a static type reference rather than a variable.
   Mirrors the String.method() handling above. */
if (base != NULL && base->code == N_ID) {
  /* Only treat as static dispatch if the N_ID does NOT resolve to a
     regular variable/decl — i.e. it's purely a class name or a
     generic type parameter. */
  symbol_t vsym;
  int is_var = symbol_find (c2m_ctx, S_REGULARS, base, curr_scope, &vsym)
               && vsym.def_node != NULL
               && vsym.def_node->code != N_CLASS;
  if (!is_var) {
    symbol_t csym;
    int found_class = (symbol_find (c2m_ctx, S_TAG, base, NULL, &csym)
                       && csym.def_node != NULL
                       && csym.def_node->code == N_CLASS);
    /* Also check if it's a generic type parameter name by scanning
       the registered generic templates for a matching param name. */
    int is_tpname = 0;
    if (!found_class) {
      /* Check if base->u.s.s is a type parameter of any registered
         generic class template. */
      struct parse_ctx *parse_ctx = c2m_ctx->parse_ctx;
      if (parse_ctx != NULL && generic_templates != NULL) {
        VARR (generic_tmpl_t) *_gt = generic_templates;
        for (size_t ti = 0; ti < VARR_LENGTH (generic_tmpl_t, _gt); ti++) {
          generic_tmpl_t *t = &VARR_ADDR (generic_tmpl_t, _gt)[ti];
          if (t->class_node != NULL) {
            node_t params = NL_NEXT (NL_HEAD (t->class_node->u.ops));
            if (params != NULL && params->code == N_LIST) {
              for (node_t p = NL_HEAD (params->u.ops); p != NULL; p = NL_NEXT (p)) {
                if (p->code == N_ID && strcmp (p->u.s.s, base->u.s.s) == 0) {
                  is_tpname = 1;
                  break;
                }
              }
            }
          }
          if (is_tpname) break;
        }
      }
    }
    /* Also handle mangled generic specialization names like
       __generic_EntityOps_Patient that haven't been materialized yet. */
    int is_generic_spec = (!found_class && !is_tpname
                           && strncmp(base->u.s.s, "__generic_", 10) == 0);
    if (found_class || is_tpname || is_generic_spec) {
      node_t mem = NL_NEXT (base);
      if (mem != NULL && mem->code == N_ID) {
        if (found_class) {
          node_t class_node = csym.def_node;
          symbol_t msym;
          node_t mfunc = NULL, mfunc_op = NULL;
          if (symbol_find (c2m_ctx, S_REGULARS, mem, class_node, &msym)) {
            if (msym.def_node && msym.def_node->code == N_FUNC_DEF) {
              mfunc = msym.def_node;
            }
          }
          if (!mfunc)
            mfunc = find_def (c2m_ctx, S_REGULARS, mem, class_node, &mfunc_op);
          if (mfunc && mfunc->code == N_FUNC_DEF) {
            e = create_expr (c2m_ctx, r);
            e->type->mode = TM_BASIC;
            e->type->u.basic_type = TP_VOID;
            e->u.lvalue_node = NULL;
            struct expr *be = create_expr (c2m_ctx, base);
            be->type->mode = TM_CLASS;
            be->type->u.tag_type = class_node;
            be->def_node = class_node;
            be->u.lvalue_node = NULL;
            base->attr = be;
            break;
          }
          /* Not a static method — could be a static data member (e.g.
             Fruit.variants dict).  Fall through to the normal N_FIELD
             handling below instead of erroring, so static data members
             resolve via the regular member-access path. */
        } else {
          /* Generic type parameter or mangled generic specialization:
             set a void placeholder.  For type parameters, specialization
             will substitute T with the concrete class name.  For mangled
             specialization names, the class will be materialized later. */
          e = create_expr (c2m_ctx, r);
          e->type->mode = TM_BASIC;
          e->type->u.basic_type = TP_VOID;
          e->u.lvalue_node = NULL;
          break;
        }
      }
    }
  }
}

        process_unop (c2m_ctx, r, &op1, &e1, &t1, r);
	        e = create_expr (c2m_ctx, r);
	        e->type->mode = TM_BASIC;
	        e->type->u.basic_type = TP_INT;
	        op2 = NL_NEXT (op1);
	        assert (op2->code == N_ID);
	        if (r->code == N_DEREF_FIELD && t1->mode == TM_PTR) {
	          t1 = t1->u.ptr_type;
	        }
	        // Auto-dereference pointer-to-class for '.' access (e.g. this.member)
	        if (r->code == N_FIELD && t1->mode == TM_PTR && t1->u.ptr_type
	            && t1->u.ptr_type->mode == TM_CLASS) {
	          t1 = t1->u.ptr_type;
	          r->code = N_DEREF_FIELD; // rewrite so gen takes the pointer-deref path
	        }
	        /* Built-in String method access (s.length, s.substr, ...): the N_CALL
	           handler does the real work; here we just type the N_FIELD as a
	           placeholder so member resolution below doesn't reject it. */
	        if (builtin_string_type_p (t1) && get_string_method (op2->u.s.s, NULL, NULL) != SM_NONE) {
	          e->type->mode = TM_BASIC;
	          e->type->u.basic_type = TP_VOID;
	          e->u.lvalue_node = NULL;
	          break;
	        }
        /* Sequence lambda-method access (arr.filter, slice.map, lst.reduce,
           s.count): same placeholder treatment; N_CALL does the real work.
           A user-defined class method with the same name takes precedence. */
        if (get_seq_method (op2->u.s.s, NULL) != SEQM_NONE && !builtin_string_type_p (t1)) {
          struct seq_recv seq_sr;
          if (classify_seq_receiver (c2m_ctx, t1, POS (r), &seq_sr) != SEQ_RECV_NONE
              && (seq_sr.kind != SEQ_RECV_CLASS
                  || (!symbol_find (c2m_ctx, S_REGULARS, op2, seq_sr.cls_type->u.tag_type, &sym)
                      && find_def (c2m_ctx, S_REGULARS, op2, seq_sr.cls_type->u.tag_type, NULL)
                           == NULL))) {
            e->type->mode = TM_BASIC;
            e->type->u.basic_type = TP_VOID;
            e->u.lvalue_node = NULL;
            break;
          }
        }
        /* Built-in List<String>::join(delim) method access: same placeholder
           treatment as the String/seq methods; N_CALL lowers it to
           c2m_str_join.  List<String> defines no user `join`. */
        if (list_string_type_p (t1) && get_string_method (op2->u.s.s, NULL, NULL) == SM_JOIN
            && find_def (c2m_ctx, S_REGULARS, op2, t1->u.tag_type, NULL) == NULL) {
          e->type->mode = TM_BASIC;
          e->type->u.basic_type = TP_VOID;
          e->u.lvalue_node = NULL;
          break;
        }
        /* nameof/typeof pseudo-methods: works on any receiver type (enum, scalar,
           pointer, class...).  Placeholder void type; N_CALL does the real rewrite. */
        if (op2 != NULL && op2->code == N_ID
            && (strcmp (op2->u.s.s, "nameof") == 0 || strcmp (op2->u.s.s, "typeof") == 0)) {
          e->type->mode = TM_BASIC;
          e->type->u.basic_type = TP_VOID;
          e->u.lvalue_node = NULL;
          break;
        }
        /* Generic method access (Select<U>): placeholder; N_CALL monomorphizes. */
        if (op2 != NULL && op2->code == N_ID && t1->mode == TM_CLASS
            && t1->u.tag_type != NULL) {
          node_t tid = TAG_ID (t1->u.tag_type);
          const char *cn = (tid != NULL && tid->code == N_ID) ? tid->u.s.s : NULL;
          const char *base = NULL;
          generic_spec_t *gsp = (cn != NULL) ? find_generic_spec_by_name (c2m_ctx, cn) : NULL;
          if (gsp != NULL) base = gsp->orig_name;
          else if (cn != NULL) base = cn;
          if (base != NULL
              && get_generic_method_template (c2m_ctx, base, op2->u.s.s) != NULL) {
            e->type->mode = TM_BASIC;
            e->type->u.basic_type = TP_VOID;
            e->u.lvalue_node = NULL;
            break;
          }
        }
        if (t1->mode != TM_STRUCT && t1->mode != TM_UNION && t1->mode != TM_CLASS && t1->mode != TM_DICT) {
          error (c2m_ctx, POS (r), "request for member %s in something not a structure, union, class or dict",
                 op2->u.s.s);
          break;
        } else if (t1->mode != TM_DICT && !symbol_find (c2m_ctx, S_REGULARS, op2, t1->u.tag_type, &sym) &&
            !(func = find_def(c2m_ctx, S_REGULARS, op2, t1->u.tag_type, &func_op)) )  {
            /* TM_CLASS namespace: first look for a static or instance method
               (ClassName.method or obj.method).  Only fall back to variant
               sugar / error if nothing is found. */
            if (t1->mode == TM_CLASS) {
              node_t class_node = t1->u.tag_type;
              symbol_t msym;
              node_t mfunc = NULL, mfunc_op = NULL;
              if (symbol_find(c2m_ctx, S_REGULARS, op2, class_node, &msym)) {
                if (msym.def_node && msym.def_node->code == N_FUNC_DEF) {
                  mfunc = msym.def_node;
                } else if (msym.def_node && msym.def_node->code == N_MEMBER) {
                  decl = msym.def_node->attr;
                  if (decl) {
                    *e->type = *decl->decl_spec.type;
                    e->u.lvalue_node = msym.def_node;
                    r->attr = e;
                    break;
                  }
                }
              }
              if (!mfunc) {
                mfunc = find_def(c2m_ctx, S_REGULARS, op2, class_node, &mfunc_op);
              }
              if (mfunc && mfunc->code == N_FUNC_DEF) {
                func = mfunc;
                func_op = mfunc_op;
                /* fall through to the func handling below */
              } else {
                /* Declarative-dict variant sugar:  ClassName.Variant resolves
                   to the integer  variants["Variant"]["value"]  at compile
                   time, for use in constant contexts (case labels, etc.). */
                node_t v_id = build_id(c2m_ctx, "variants", POS(op2));
                symbol_t vsym2;
                node_t valnode = NULL;
                if (symbol_find(c2m_ctx, S_REGULARS, v_id, class_node, &vsym2)) {
                  node_t vd = vsym2.def_node;
                  node_t vi = vd ? MEMBER_INIT(vd) : NULL;
                  node_t found_sub = NULL;
                  if (vi && vi->code == N_LIST) {
                    for (node_t ii = NL_HEAD(vi->u.ops); ii != NULL; ii = NL_NEXT(ii)) {
                      if (ii->code != N_INIT) continue;
                      node_t dlist = NL_HEAD(ii->u.ops);
                      node_t fid = dlist ? NL_HEAD(dlist->u.ops) : NULL;
                      node_t kn = (fid && fid->code == N_FIELD_ID) ? NL_HEAD(fid->u.ops) : NULL;
                      if (kn && (kn->code == N_STR || kn->code == N_ID)
                          && strcmp(kn->u.s.s, op2->u.s.s) == 0) {
                        found_sub = NL_NEXT(dlist);
                        break;
                      }
                    }
                  }
                  if (found_sub && found_sub->code == N_LIST) {
                    for (node_t si = NL_HEAD(found_sub->u.ops); si != NULL; si = NL_NEXT(si)) {
                      if (si->code != N_INIT) continue;
                      node_t sdlist = NL_HEAD(si->u.ops);
                      node_t sfid = sdlist ? NL_HEAD(sdlist->u.ops) : NULL;
                      node_t sk = (sfid && sfid->code == N_FIELD_ID) ? NL_HEAD(sfid->u.ops) : NULL;
                      if (sk && (sk->code == N_STR || sk->code == N_ID)
                          && strcmp(sk->u.s.s, "value") == 0) {
                        valnode = NL_NEXT(sdlist);
                        break;
                      }
                    }
                  }
                }
                if (valnode != NULL && valnode->code != N_LIST) {
                  check(c2m_ctx, valnode, r); /* ensure const expr attr is set */
                  struct expr *vve = valnode->attr;
                  if (vve != NULL && vve->const_p && integer_type_p(vve->type)) {
                    e->const_p = TRUE;
                    e->c.i_val = vve->c.i_val;
                    e->type->mode = TM_BASIC;
                    e->type->u.basic_type = TP_INT;
                    e->u.lvalue_node = NULL;
                    r->attr = e;
                    break;
                  }
                }
                /* UFCS placeholder: free generic `f` may be called as `obj.f(...)`.
                   N_CALL rewrites to f(obj, ...).  Same void-placeholder pattern as
                   seq methods / List.join / nameof. */
                if (op2 != NULL && op2->code == N_ID
                    && ufcs_free_fn_candidate_p (c2m_ctx, op2->u.s.s)) {
                  e->type->mode = TM_BASIC;
                  e->type->u.basic_type = TP_VOID;
                  e->u.lvalue_node = NULL;
                  break;
                }
                error (c2m_ctx, POS (r), "class has no member %s", op2->u.s.s);
                break;
              }
            } else {
              error (c2m_ctx, POS (r), "%s has no member %s",
                     t1->mode == TM_STRUCT ? "struct" : t1->mode == TM_UNION ? "union" : "class",
                     op2->u.s.s);
              break;
            }
            break;
          }
         if (t1->mode == TM_DICT) {
           e = create_expr(c2m_ctx, r);
           /* Dict member access: every leaf stays TM_DICT (a tagged
              DictValue*), so chaining (`d.a.b.c`), `json(leaf)`, and a future
              typed-class binder can walk the subtree losslessly.  Scalar
              consumers (assignment to int/String, `(int)d.x`, varargs, etc.)
              already unwrap the union payload via maybe_unwrap_dict_value /
              the N_CAST path.

              Serialize with `d.json()` (method), not property `d.json` — a key
              named "json" (e.g. `d.items.json`) is real field access, same
              pattern as `d.length` key vs `d.length()` size. */
           e->type->mode = TM_DICT; /* keep TM_DICT for chaining */
           e->u.lvalue_node = r; /* allow assignment */
           r->attr = e;
           break;
         }
        /* The field-scope symbol_find above can resolve directly to an
           instance/static method (an N_FUNC_DEF), not just an N_MEMBER: methods
           are registered in the class scope under their plain name.  When that
           happens `func` was never assigned (find_def is short-circuited), so
           route the method into the func-handling path below.  Without this,
           obj.method() falls into the member branch and trips the N_MEMBER
           assertion.  The enclosing N_CALL re-resolves the exact overload. */
        if (func == NULL && sym.def_node != NULL && sym.def_node->code == N_FUNC_DEF)
          func = sym.def_node;
        if (func) {
          assert (func->code == N_FUNC_DEF);
#ifdef C2MIR_PREPRO_DEBUG
          printf("Found class method call\n");
#endif
          decl = func->attr;
          // proper: set N_FIELD expr type as pointer-to-function (normal for method)
          struct type *fnt = decl->decl_spec.type;
          e->type->mode = TM_PTR;
          e->type->u.ptr_type = fnt;
          e->type->func_type_before_adjustment_p = TRUE;
          e->def_node = func;
          if (decl != NULL) decl->used_p = TRUE;
          e->u.lvalue_node = NULL;
        } else {
          if(sym.def_node == NULL || sym.id == NULL) {
              error (c2m_ctx, POS (r), "undefined member %s", op2->u.s.s);
              break;
          }
          assert (sym.def_node->code == N_MEMBER);

          decl = sym.def_node->attr;
          if(decl) {
              *e->type = *decl->decl_spec.type;
              e->u.lvalue_node = sym.def_node;
              if ((width = NL_EL (sym.def_node->u.ops, 3))->code != N_IGNORE && e->type->mode == TM_BASIC
                  && (width_expr = width->attr)->const_p
                  && width_expr->c.i_val < (mir_llong) sizeof (mir_int) * MIR_CHAR_BIT)
                e->type->u.basic_type = TP_INT;
          } else {
              error (c2m_ctx, POS (r), "member has no type %s", op2->u.s.s);
              break;
          }
        }
        break;
      }
      case N_COND: {
        node_t op3;
        struct expr *e3;
        struct type *t3;
        int v = 0;
        /* Safe-navigation sentinel: the parser tags the N_COND it synthesizes
           for `recv?.member...` with attr == (void *) 2 (read BEFORE
           create_expr overwrites attr, mirroring the N_CAST lenient-bind
           sentinel).  It relaxes the else-arm (a synthesized int 0) against a
           void method call or a String member in the then-arm. */
        int safe_nav_p = (r->attr == (void *) (intptr_t) 2);

        process_bin_ops (c2m_ctx, r, &op1, &op2, &e1, &e2, &t1, &t2, r);
        op3 = NL_NEXT (op2);
        check (c2m_ctx, op3, r);
        e3 = op3->attr;
        e3->type = adjust_type (c2m_ctx, e3->type);
        t3 = e3->type;
        e = create_expr (c2m_ctx, r);
        e->type->mode = TM_BASIC;
        e->type->u.basic_type = TP_INT;
        if (!scalar_type_p (t1)) {
          error (c2m_ctx, POS (r), "condition should be of a scalar type");
          break;
        }
        if (e1->const_p) {
          if (floating_type_p (t1))
            v = e1->c.d_val != 0.0;
          else if (signed_integer_type_p (t1))
            v = e1->c.i_val != 0;
          else
            v = e1->c.u_val != 0;
        }
        if (arithmetic_type_p (t2) && arithmetic_type_p (t3)) {
          t = arithmetic_conversion (t2, t3);
          *e->type = t;
          if (e1->const_p) {
            if (v && e2->const_p) {
              e->const_p = TRUE;
              convert_value (e2, &t);
              e->c = e2->c;
            } else if (!v && e3->const_p) {
              e->const_p = TRUE;
              convert_value (e3, &t);
              e->c = e3->c;
            }
          }
          break;
        }
        if (safe_nav_p && void_type_p (t2)) {
          /* obj?.voidMethod(): result is void; the 0 else-arm is never used. */
          e->type->u.basic_type = TP_VOID;
          break;
        }
        if (safe_nav_p && builtin_string_type_p (t2)) {
          /* obj?.stringMember: NULL String when the receiver is null. */
          *e->type = *t2;
          break;
        }
        if (void_type_p (t2) && void_type_p (t3)) {
          e->type->u.basic_type = TP_VOID;
        } else if ((t2->mode == TM_STRUCT || t2->mode == TM_UNION)
                   && (t3->mode == TM_STRUCT || t3->mode == TM_UNION)
                   && t2->u.tag_type == t3->u.tag_type) {
          *e->type = *t2;
        } else if ((t2->mode == TM_PTR && null_const_p (e3, t3))
                   || (t3->mode == TM_PTR && null_const_p (e2, t2))) {
          e->type = null_const_p (e2, t2) ? t3 : t2;
        } else if (builtin_string_type_p (t2) && builtin_string_type_p (t3)) {
          /* String ? String : String  — String is a pointer-width basic type;
             modelled like the pointer case below so generic functions like
             Max<String> can use `?:` over String operands. */
          *e->type = *t2;
        } else if (t2->mode != TM_PTR || t3->mode != TM_PTR) {
          error (c2m_ctx, POS (r), "incompatible types in true and false parts of cond-expression");
          break;
        } else if (compatible_types_p (t2, t3, TRUE)) {
          t = composite_type (c2m_ctx, t2->u.ptr_type, t3->u.ptr_type);
          e->type->mode = TM_PTR;
          e->type->pos_node = r;
          e->type->u.ptr_type = create_type (c2m_ctx, &t);
          e->type->u.ptr_type->type_qual
            = type_qual_union (&t2->u.ptr_type->type_qual, &t3->u.ptr_type->type_qual);
          if ((t2->u.ptr_type->type_qual.atomic_p || t3->u.ptr_type->type_qual.atomic_p)
              && !null_const_p (e2, t2) && !null_const_p (e3, t3)) {
            error (c2m_ctx, POS (r),
                   "pointer to atomic type in true or false parts of cond-expression");
          }
        } else if (void_ptr_p (t2) || void_ptr_p (t3)) {
          e->type->mode = TM_PTR;
          e->type->pos_node = r;
          e->type->u.ptr_type = create_type (c2m_ctx, e3->type->u.ptr_type);
          e->type->u.ptr_type->pos_node = r;
          assert (!null_const_p (e2, t2) && !null_const_p (e3, t3));
          if (t2->u.ptr_type->type_qual.atomic_p || t3->u.ptr_type->type_qual.atomic_p) {
            error (c2m_ctx, POS (r),
                   "pointer to atomic type in true or false parts of cond-expression");
          }
          e->type->u.ptr_type->mode = TM_BASIC;
          e->type->u.ptr_type->u.basic_type = TP_VOID;
          e->type->u.ptr_type->type_qual
            = type_qual_union (&t2->u.ptr_type->type_qual, &t3->u.ptr_type->type_qual);
        } else {
          error (c2m_ctx, POS (r),
                 "incompatible pointer types in true and false parts of cond-expression");
          break;
        }
        if (e1->const_p) {
          if (v && e2->const_p) {
            e->const_p = TRUE;
            e->c = e2->c;
          } else if (!v && e3->const_p) {
            e->const_p = TRUE;
            e->c = e3->c;
          }
        }
        break;
      }
      case N_COALESCE: { /* a ?? b — a's value if non-zero/non-null, else b (a evaluated once) */
        process_bin_ops (c2m_ctx, r, &op1, &op2, &e1, &e2, &t1, &t2, r);
        e = create_expr (c2m_ctx, r);
        e->type->mode = TM_BASIC;
        e->type->u.basic_type = TP_INT;
        if (!scalar_type_p (t1)) {
          error (c2m_ctx, POS (r), "left operand of ?? should be of a scalar type");
          break;
        }
        if (arithmetic_type_p (t1) && arithmetic_type_p (t2)) {
          t = arithmetic_conversion (t1, t2);
          *e->type = t;
        } else if (builtin_string_type_p (t1)
                   && (builtin_string_type_p (t2) || t2->mode == TM_PTR
                       || null_const_p (e2, t2))) {
          /* String ?? String / ?? "literal" / ?? NULL — String is pointer-width. */
          *e->type = *t1;
        } else if (t1->mode == TM_PTR && builtin_string_type_p (t2)) {
          e->type = t1; /* char* ?? String */
        } else if (t1->mode == TM_PTR && null_const_p (e2, t2)) {
          e->type = t1;
        } else if (t2->mode == TM_PTR && null_const_p (e1, t1)) {
          e->type = t2;
        } else if (t1->mode != TM_PTR || t2->mode != TM_PTR) {
          error (c2m_ctx, POS (r), "incompatible types of ?? operands");
          break;
        } else if (compatible_types_p (t1, t2, TRUE)) {
          t = composite_type (c2m_ctx, t1->u.ptr_type, t2->u.ptr_type);
          e->type->mode = TM_PTR;
          e->type->pos_node = r;
          e->type->u.ptr_type = create_type (c2m_ctx, &t);
          e->type->u.ptr_type->type_qual
            = type_qual_union (&t1->u.ptr_type->type_qual, &t2->u.ptr_type->type_qual);
        } else if (void_ptr_p (t1) || void_ptr_p (t2)) {
          e->type->mode = TM_PTR;
          e->type->pos_node = r;
          e->type->u.ptr_type = create_type (c2m_ctx, t2->u.ptr_type);
          e->type->u.ptr_type->pos_node = r;
          e->type->u.ptr_type->mode = TM_BASIC;
          e->type->u.ptr_type->u.basic_type = TP_VOID;
          e->type->u.ptr_type->type_qual
            = type_qual_union (&t1->u.ptr_type->type_qual, &t2->u.ptr_type->type_qual);
        } else {
          error (c2m_ctx, POS (r), "incompatible pointer types of ?? operands");
        }
        break;
      }
      case N_ALIGNOF:
      case N_SIZEOF: {
        struct decl_spec *decl_spec;

        op1 = NL_HEAD (r->u.ops);
        check (c2m_ctx, op1, r);
        e = create_expr (c2m_ctx, r);
        *e->type = get_ptr_int_type (FALSE);
        if (r->code == N_ALIGNOF && op1->code == N_IGNORE) {
          error (c2m_ctx, POS (r), "_Alignof of non-type");
          break;
        }
        assert (op1->code == N_TYPE);
        decl_spec = op1->attr;
        if (incomplete_type_p (c2m_ctx, decl_spec->type)) {
          error (c2m_ctx, POS (r), "%s of incomplete type requested",
                 r->code == N_ALIGNOF ? "_Alignof" : "sizeof");
        } else if (decl_spec->type->mode == TM_FUNC) {
          error (c2m_ctx, POS (r), "%s of function type requested",
                 r->code == N_ALIGNOF ? "_Alignof" : "sizeof");
        } else {
          e->const_p = TRUE;
          e->c.i_val = (r->code == N_SIZEOF ? type_size (c2m_ctx, decl_spec->type)
                                            : (mir_size_t) type_align (decl_spec->type));
        }
        break;
      }
      case N_EXPR_SIZEOF:
        process_unop (c2m_ctx, r, &op1, &e1, &t1, r);
        e = create_expr (c2m_ctx, r);
        e->type->mode = TM_BASIC;
        e->type->u.basic_type = TP_INT;  // ???
        if (incomplete_type_p (c2m_ctx, t1)) {
          error (c2m_ctx, POS (r), "sizeof of incomplete type requested");
        } else if (t1->mode == TM_FUNC) {
          error (c2m_ctx, POS (r), "sizeof of function type requested");
        } else if (e1->u.lvalue_node && e1->u.lvalue_node->code == N_MEMBER) {
          node_t declarator = NL_EL (e1->u.lvalue_node->u.ops, 1);
          node_t width = NL_NEXT (NL_NEXT (declarator));

          assert (declarator->code == N_DECL);
          if (width->code != N_IGNORE) {
            error (c2m_ctx, POS (r), "sizeof applied to a bit-field %s",
                   NL_HEAD (declarator->u.ops)->u.s.s);
          }
        }
        e->const_p = TRUE;
        e->c.i_val = type_size (c2m_ctx, t1);
        break;
      case N_CAST: {
        struct decl_spec *decl_spec;
        int void_p;
        /* Snapshot the lenient-bind sentinel stashed by the parser BEFORE
           create_expr overwrites r->attr.  The sentinel is the literal
           pointer (void*)1 (see primary_expr cast block); ordinary casts
           have r->attr == NULL going into the checker. */
        int cast_lenient_p = (r->attr == (void *) (intptr_t) 1);

        process_type_bin_ops (c2m_ctx, r, &op1, &op2, &e2, &t2, r);
        e = create_expr (c2m_ctx, r);
        assert (op1->code == N_TYPE);
        decl_spec = op1->attr;
        *e->type = *decl_spec->type;
        void_p = void_type_p (decl_spec->type);

        /* Dict-to-aggregate bind cast: (T)d or (T)?d where T is a by-value
           class OR a plain struct.  Bypasses the standard "non-scalar
           conversion" rejection.  The gen side walks T's member list and
           builds T from the dict subtree; lenient_p == 1 (the `?` form)
           tolerates missing/mismatched fields, while the strict form throws
           KeyException on a missing required field.

           Unions are deliberately excluded: there is no well-defined mapping
           from one JSON object to multiple overlapping union members.  Use a
           tagged class with an explicit discriminator field instead. */
        if (t2->mode == TM_DICT
            && (decl_spec->type->mode == TM_CLASS
                || decl_spec->type->mode == TM_STRUCT)) {
          e->bind_p = TRUE;
          e->lenient_p = cast_lenient_p ? TRUE : FALSE;
          break;
        }
        /* A `?` cast that did NOT target a class/struct is malformed — the
           marker is only meaningful for dict-to-aggregate binds.  Surface as
           an error so users do not silently get a no-op `?`. */
        if (cast_lenient_p) {
          error (c2m_ctx, POS (r),
                 "the `?` (lenient) cast form is only valid when casting a dict to a class or struct");
        }

        /* Escape hatch for generic code: casting a class/struct VALUE to a
           pointer type (e.g. `(char*)this->vals[i]` inside a generic
           Map<K,V>::to_string()).  ClassyC monomorphizes *every* method of a
           generic class, so a method only meaningful for string/scalar
           elements is still type-checked when the class is instantiated with
           a by-value class element — where this cast is otherwise a hard
           error even though the method is never called for that type.  Allow
           it: gen reinterprets the value's storage (its first pointer word),
           which is harmless for the uncalled instantiation and a no-op for
           the string instantiations (String is scalar, so it never reaches
           here).  Caveat: this also relaxes the diagnostic for a genuine
           struct-to-pointer mistake in hand-written code. */
        int aggregate_to_ptr_p
          = (!void_p && decl_spec->type->mode == TM_PTR
             && (t2->mode == TM_CLASS || t2->mode == TM_STRUCT));
        if (!void_p && !scalar_type_p (decl_spec->type)) {
          error (c2m_ctx, POS (r), "conversion to non-scalar type requested");
        } else if (!void_p && !scalar_type_p (t2) && !void_type_p (t2)
                   && !aggregate_to_ptr_p) {
          error (c2m_ctx, POS (r), "conversion of non-scalar value requested");
        } else if (t2->mode == TM_PTR && floating_type_p (decl_spec->type)) {
          error (c2m_ctx, POS (r), "conversion of a pointer to floating value requested");
        } else if (decl_spec->type->mode == TM_PTR && floating_type_p (t2)) {
          error (c2m_ctx, POS (r), "conversion of floating point value to a pointer requested");
        } else if (e2->const_p && !void_p) {
          e->const_p = TRUE;
          cast_value (e, e2, decl_spec->type);
        }
        break;
      }
      case N_COMPOUND_LITERAL: {
        node_t list, n;
        decl_t decl;
        int n_spec_index, addr_p;

        op1 = NL_HEAD (r->u.ops);
        list = NL_NEXT (op1);
        assert (op1->code == N_TYPE && list->code == N_LIST);
        check (c2m_ctx, op1, r);
        decl = op1->attr;
        t1 = decl->decl_spec.type;
        check (c2m_ctx, list, r);
        decl->addr_p = TRUE;
        if (incomplete_type_p (c2m_ctx, t1)
            && (t1->mode != TM_ARR || t1->u.arr_type->size->code != N_IGNORE
                || incomplete_type_p (c2m_ctx, t1->u.arr_type->el_type))) {
          error (c2m_ctx, POS (r), "compound literal of incomplete type");
          break;
        }
        for (n_spec_index = (int) VARR_LENGTH (node_t, context_stack) - 1;
             n_spec_index >= 0 && (n = VARR_GET (node_t, context_stack, n_spec_index)) != NULL
             && n->code != N_SPEC_DECL;
             n_spec_index--)
          ;
        if (n_spec_index < (int) VARR_LENGTH (node_t, context_stack) - 1
            && (n_spec_index < 0
                || !get_compound_literal (VARR_GET (node_t, context_stack, n_spec_index + 1), &addr_p)
                || addr_p))
          check_initializer (c2m_ctx, NULL, &t1, list,
                             curr_scope == top_scope || decl->decl_spec.static_p
                               || decl->decl_spec.thread_local_p,
                             FALSE);
        decl->decl_spec.type = t1;
        e = create_expr (c2m_ctx, r);
        e->u.lvalue_node = r;
        *e->type = *t1;
        if (curr_scope != top_scope) VARR_PUSH (decl_t, func_decls_for_allocation, decl);
        break;
      }
      case N_CALL: {
        struct func_type *func_type = NULL; /* to remove an uninitialized warning */
        struct type *ret_type;
        node_t list, spec_list, decl, param_list, start_param, param, arg_list, first_arg, arg;
        node_t saved_scope = curr_scope;
        struct decl_spec *decl_spec;
        mir_size_t saved_call_arg_area_offset_before_args;
        struct type res_type;
        int builtin_call_p, alloca_p = FALSE, va_arg_p = FALSE, va_start_p = FALSE;
        int add_overflow_p = FALSE, sub_overflow_p = FALSE, mul_overflow_p = FALSE, expect_p = FALSE;
        int atomic_load_n_p = FALSE, atomic_store_n_p = FALSE, atomic_exchange_n_p = FALSE;
        int atomic_fetch_add_p = FALSE, atomic_fetch_sub_p = FALSE, atomic_fetch_and_p = FALSE;
        int atomic_fetch_or_p = FALSE, atomic_fetch_xor_p = FALSE;
        int atomic_cas_n_p = FALSE, atomic_fence_p = FALSE;
        int jcall_p = FALSE, jret_p = FALSE, prop_set_p = FALSE, prop_eq_p = FALSE, prop_ne_p = FALSE;
        int method_call_p = FALSE;
        int json_p = FALSE;

        op1 = NL_HEAD(r->u.ops);
        /* is_pointer<T>: compiler intrinsic that returns 1 if T is a pointer type,
           0 otherwise. Used by generic collection destructors to conditionally
           delete pointer elements when the collection owns them.
           Syntax: is_pointer<TypeArg>()
           Rewritten during check phase to an integer literal (1 or 0) based on the
           resolved type parameter. */
        /* is_move_only<T>: compiler intrinsic — 1 if T is a move-only
           collection (List/Map/Set instantiation), else 0.  Rewritten during
           check to an integer literal, same shape as is_pointer<T>. */
        if (op1->code == N_ID && strcmp (op1->u.s.s, "is_move_only") == 0) {
          node_t type_args = NL_NEXT (op1);
          if (type_args != NULL && type_args->code == N_LIST) {
            node_t type_arg = NL_HEAD (type_args->u.ops);
            node_t call_args = NL_NEXT (type_args);
            if (type_arg != NULL && NL_NEXT (type_arg) == NULL
                && call_args != NULL && NL_HEAD (call_args->u.ops) == NULL) {
              int mo = type_arg_move_only_p (type_arg);
              /* Replace the entire call expression with an integer literal.
                 Preserve r's op_link (sibling chain) as with is_pointer. */
              node_t lit = new_i_node (c2m_ctx, (long) mo, POS (r));
              check (c2m_ctx, lit, NULL);
              DLIST_LINK (node_t) saved_link = r->op_link;
              *r = *lit;
              r->op_link = saved_link;
              e = r->attr;
              break;
            }
          }
          /* Malformed is_move_only<...>(...); fall through to error */
        }
        if (op1->code == N_ID && strcmp (op1->u.s.s, "is_pointer") == 0) {
          node_t type_args = NL_NEXT (op1);
          if (type_args != NULL && type_args->code == N_LIST) {
            node_t type_arg = NL_HEAD (type_args->u.ops);
            node_t call_args = NL_NEXT (type_args);
            if (type_arg != NULL && NL_NEXT (type_arg) == NULL
                && call_args != NULL && NL_HEAD (call_args->u.ops) == NULL) {
              /* Check if type_arg represents a pointer type.
                 For type arguments, if it's a N_POINTER node or ends with *, it's a pointer.
                 For template parameters that have been resolved, check the resolved type. */
              int is_ptr = 0;
              if (type_arg->code == N_POINTER) {
                /* Direct pointer type like int* */
                is_ptr = 1;
              } else if (type_arg->code == N_ID) {
                /* Could be a template parameter T that resolved to a pointer type.
                   Look up the symbol and check its type. */
                node_t def = find_def (c2m_ctx, S_REGULARS, type_arg, curr_scope, NULL);
                if (def != NULL && def->attr != NULL) {
                  struct expr *e_attr = (struct expr *) def->attr;
                  if (e_attr->type != NULL && e_attr->type->mode == TM_PTR) {
                    is_ptr = 1;
                  }
                }
              }
              /* Replace the entire call expression with an integer literal.
                 Preserve r's op_link (sibling chain) — *r = *lit would
                 overwrite it with lit's (NULL) links, corrupting the
                 parent's child list and crashing gen. */
              node_t lit = new_i_node (c2m_ctx, (long) is_ptr, POS (r));
              check (c2m_ctx, lit, NULL);
              DLIST_LINK (node_t) saved_link = r->op_link;
              *r = *lit;
              r->op_link = saved_link;
              e = r->attr;
              break;
            }
          }
          /* Malformed is_pointer<...>(...); fall through to error */
        }
        /* nameof<T>() / typeof<T>(): type-level reflection to a string literal.
           nameof strips pointers (nameof<int*>() == "int"); typeof keeps them
           (typeof<int*>() == "int*").  Also:
             nameof(id)  — C#-style identifier name as a string
           specialize_node already folds these inside generic bodies. */
        if (op1->code == N_ID
            && (strcmp (op1->u.s.s, "nameof") == 0
                || strcmp (op1->u.s.s, "typeof") == 0)) {
          int is_typeof = (strcmp (op1->u.s.s, "typeof") == 0);
          node_t type_args = NL_NEXT (op1);
          if (type_args != NULL && type_args->code == N_LIST) {
            node_t type_arg = NL_HEAD (type_args->u.ops);
            node_t call_args = NL_NEXT (type_args);
            /* nameof<T>() / typeof<T>(): three children [id, tlist, empty-args] */
            if (type_arg != NULL && NL_NEXT (type_arg) == NULL
                && call_args != NULL && NL_HEAD (call_args->u.ops) == NULL) {
              const char *nm
                = type_arg_reflection_name (c2m_ctx, type_arg, is_typeof);
              rewrite_node_to_str (c2m_ctx, r, nm);
              e = r->attr;
              break;
            }
            /* nameof(id): two children [id, arglist] — C# nameof on an identifier */
            if (!is_typeof && call_args == NULL && type_arg != NULL
                && NL_NEXT (type_arg) == NULL) {
              if (type_arg->code == N_ID) {
                rewrite_node_to_str (c2m_ctx, r, type_arg->u.s.s);
                e = r->attr;
                break;
              }
              /* nameof(expr): if const enum, reverse-lookup the enumerator */
              check (c2m_ctx, type_arg, r);
              {
                struct expr *ae = type_arg->attr;
                const char *nm = NULL;
                if (ae != NULL && ae->type != NULL && ae->type->mode == TM_ENUM
                    && ae->const_p)
                  nm = enum_const_name_for_value (ae->type->u.tag_type, ae->c.i_val);
                if (nm != NULL) {
                  rewrite_node_to_str (c2m_ctx, r, nm);
                  e = r->attr;
                  break;
                }
              }
            }
          }
          /* Malformed nameof/typeof(...); fall through to error */
        }
        /* expr.nameof() / expr.typeof() — rewrite before call_nodes is populated
           so gen_mir_protos does not see a non-call node. */
        if (op1->code == N_FIELD || op1->code == N_DEREF_FIELD) {
          node_t obj = NL_HEAD (op1->u.ops);
          node_t mid = (obj != NULL) ? NL_NEXT (obj) : NULL;
          if (mid != NULL && mid->code == N_ID
              && (strcmp (mid->u.s.s, "nameof") == 0
                  || strcmp (mid->u.s.s, "typeof") == 0)) {
            int want_typeof = (strcmp (mid->u.s.s, "typeof") == 0);
            node_t arg_list_early = NL_NEXT (op1);
            struct expr *oe;
            const char *nm = NULL;

            if (arg_list_early == NULL || arg_list_early->code != N_LIST
                || NL_HEAD (arg_list_early->u.ops) != NULL) {
              error (c2m_ctx, POS (r), "%s() takes no arguments", mid->u.s.s);
              break;
            }
            if (obj->attr == NULL) check (c2m_ctx, obj, r);
            oe = obj->attr;
            if (want_typeof) {
              nm = (oe != NULL && oe->type != NULL)
                     ? type_reflection_name (c2m_ctx, oe->type, 1) : "?";
              rewrite_node_to_str (c2m_ctx, r, nm);
              e = r->attr;
              break;
            }
            /* nameof: enum const ID -> spelling; enum-typed value -> reverse map;
               other identifiers -> spelling (C# nameof). */
            if (obj->code == N_ID) {
              node_t def = (oe != NULL) ? oe->def_node : NULL;
              if (def == NULL)
                def = find_def (c2m_ctx, S_REGULARS, obj, curr_scope, NULL);
              if (def != NULL && def->code == N_ENUM_CONST) {
                rewrite_node_to_str (c2m_ctx, r, obj->u.s.s);
                e = r->attr;
                break;
              }
              if (oe == NULL || oe->type == NULL || oe->type->mode != TM_ENUM) {
                rewrite_node_to_str (c2m_ctx, r, obj->u.s.s);
                e = r->attr;
                break;
              }
              /* Fall through to enum reverse-map with this ID as the value. */
            }
            if (oe != NULL && oe->type != NULL && oe->type->mode == TM_ENUM) {
              if (oe->const_p) {
                nm = enum_const_name_for_value (oe->type->u.tag_type, oe->c.i_val);
                if (nm == NULL) nm = "?";
                rewrite_node_to_str (c2m_ctx, r, nm);
                e = r->attr;
                break;
              }
              {
                node_t sw = build_enum_nameof_expr (c2m_ctx, obj, oe->type->u.tag_type,
                                                    POS (r));
                DLIST_LINK (node_t) saved_link = r->op_link;
                check (c2m_ctx, sw, NULL);
                *r = *sw;
                r->op_link = saved_link;
                e = r->attr;
                break;
              }
            }
            error (c2m_ctx, POS (r),
                   "nameof() on this expression is not supported (use an identifier or enum value)");
            break;
          }
        }
        /* __destroy(x): compiler intrinsic used by generic collection templates to
           run an element's destructor before the backing buffer is freed.  It is
           rewritten here, in place, into either:
             - `(&x)->__dtor_T()`  when x is a by-value class type whose class has
               a user destructor (so `delete list` destroys each live element); or
             - a no-op  for every other element type (int, String, pointers, or a
               class without a destructor) — preserving existing semantics for
               List<int>, List<String>, List<char*>, etc.
           Keeping this knowledge in the template (which knows its buffer/length
           fields) avoids hard-coding collection internals into the compiler. */
        if (op1->code == N_ID && strcmp (op1->u.s.s, "__destroy") == 0
            && NL_NEXT (op1) != NULL && NL_HEAD (NL_NEXT (op1)->u.ops) != NULL
            && NL_NEXT (NL_HEAD (NL_NEXT (op1)->u.ops)) == NULL) {
          node_t darg = NL_HEAD (NL_NEXT (op1)->u.ops);
          check (c2m_ctx, darg, r);
          struct expr *de = darg->attr;
          struct type *dt = de != NULL ? de->type : NULL;
          node_t dtor_def = NULL;
          if (dt != NULL && dt->mode == TM_CLASS && dt->u.tag_type != NULL) {
            node_t cid = NL_HEAD (dt->u.tag_type->u.ops);
            if (cid != NULL && cid->code == N_ID) {
              char dtor_name[320];
              node_t dtor_id;
              snprintf (dtor_name, sizeof (dtor_name), "__dtor_%s", cid->u.s.s);
              dtor_id = build_id (c2m_ctx, dtor_name, POS (r));
              dtor_def = find_def (c2m_ctx, S_REGULARS, dtor_id, curr_scope, NULL);
              if (dtor_def != NULL && dtor_def->code != N_FUNC_DEF) dtor_def = NULL;
            }
          }
          /* Mark this as a builtin (so it is not pushed to call_nodes or routed
             through normal call resolution) and record the resolved destructor on
             the node's expr.  gen emits the on-the-fly dtor call (modeled on
             N_DELETE) or nothing when def_node is NULL. */
          e = create_expr (c2m_ctx, r);
          e->type->mode = TM_BASIC;
          e->type->u.basic_type = TP_VOID;
          e->builtin_call_p = TRUE;
          e->def_node = dtor_def;
          if (dtor_def != NULL && dtor_def->attr != NULL)
            ((decl_t) dtor_def->attr)->used_p = TRUE;
          break;
        }
        /* ClassName(args) value construction: temporary by-value class object.
           Enables brace-init `new List<Pt>{ Pt(1,2), Pt(3,4) }`, call args
           `take(Pt(1,2))`, and `xs.Add(Pt(1,2))`.  Result type is TM_CLASS;
           gen constructs into a stack temporary via the matching constructor
           (same protocol as N_NEW, but without malloc).  Marked builtin_call_p
           so the normal function-call path is skipped. */
        if (op1->code == N_ID) {
          node_t class_def = find_def (c2m_ctx, S_REGULARS, op1,
                                       skip_struct_scopes (curr_scope), NULL);
          if (class_def == NULL)
            class_def = find_def (c2m_ctx, S_TAG, op1,
                                 skip_struct_scopes (curr_scope), NULL);
          if (class_def != NULL && class_def->code == N_CLASS) {
            node_t val_arg_list = NL_NEXT (op1);
            node_t arg, param, ctor_def = NULL;
            char ctor_name[320];
            node_t ctor_id;
            symbol_t ctor_sym;
            int has_ctor_sym;
            struct type *class_type;

            if (val_arg_list == NULL) val_arg_list = new_node (c2m_ctx, N_LIST);
            for (arg = NL_HEAD (val_arg_list->u.ops); arg != NULL; arg = NL_NEXT (arg))
              check (c2m_ctx, arg, r);

            snprintf (ctor_name, sizeof (ctor_name), "__ctor_%s", op1->u.s.s);
            ctor_id = build_id (c2m_ctx, ctor_name, POS (r));
            has_ctor_sym = find_overload_sym (c2m_ctx, ctor_id, curr_scope, &ctor_sym);
            if (has_ctor_sym)
              ctor_def = select_method_overload (c2m_ctx, &ctor_sym, class_def, val_arg_list);

            e = create_expr (c2m_ctx, r);
            class_type = create_class_type (c2m_ctx, class_def);
            set_class_layout (c2m_ctx, class_def, class_type);
            e->type = class_type;
            e->builtin_call_p = TRUE;
            e->def_node = ctor_def;
            if (ctor_def != NULL && ctor_def->attr != NULL)
              ((decl_t) ctor_def->attr)->used_p = TRUE;

            if (ctor_def != NULL && ctor_def->code == N_FUNC_DEF) {
              decl_t cdecl = ctor_def->attr;
              struct func_type *ft = cdecl->decl_spec.type->u.func_type;
              param = NL_HEAD (ft->param_list->u.ops);
              if (param != NULL) param = NL_NEXT (param); /* skip 'this' */
              arg = NL_HEAD (val_arg_list->u.ops);
              for (; arg != NULL && param != NULL;
                   arg = NL_NEXT (arg), param = NL_NEXT (param)) {
                struct decl_spec *pds = get_param_decl_spec (param);
                check_assignment_types (c2m_ctx, pds->type, NULL, arg->attr, r);
              }
              if (arg != NULL)
                error (c2m_ctx, POS (r),
                       "too many arguments to constructor of '%s'", op1->u.s.s);
              if (param != NULL)
                error (c2m_ctx, POS (r),
                       "too few arguments to constructor of '%s'", op1->u.s.s);
            } else if (NL_HEAD (val_arg_list->u.ops) != NULL) {
              error (c2m_ctx, POS (r),
                     "class '%s' has no constructor matching these arguments",
                     op1->u.s.s);
            }
            /* Reserve stack call-arg area for the temporary (same as return-by-value). */
            if (curr_scope != top_scope)
              update_call_arg_area_offset (c2m_ctx, class_type, TRUE);
            break;
          }
        }
        if (op1->code == N_ID) {
          alloca_p = str_eq_p(op1->u.s.s, ALLOCA);
          add_overflow_p = strcmp(op1->u.s.s, ADD_OVERFLOW) == 0;
          sub_overflow_p = strcmp(op1->u.s.s, SUB_OVERFLOW) == 0;
          mul_overflow_p = strcmp(op1->u.s.s, MUL_OVERFLOW) == 0;
          expect_p = strcmp(op1->u.s.s, EXPECT) == 0;
          jcall_p = strcmp(op1->u.s.s, JCALL) == 0;
          jret_p = strcmp(op1->u.s.s, JRET) == 0;
          prop_set_p = strcmp(op1->u.s.s, PROP_SET) == 0;
          prop_eq_p = strcmp(op1->u.s.s, PROP_EQ) == 0;
          prop_ne_p = strcmp(op1->u.s.s, PROP_NE) == 0;
          json_p = strcmp(op1->u.s.s, BUILTIN_JSON) == 0;
          atomic_load_n_p = strcmp (op1->u.s.s, ATOMIC_LOAD_N) == 0;
          atomic_store_n_p = strcmp (op1->u.s.s, ATOMIC_STORE_N) == 0;
          atomic_exchange_n_p = strcmp (op1->u.s.s, ATOMIC_EXCHANGE_N) == 0;
          atomic_fetch_add_p = strcmp (op1->u.s.s, ATOMIC_FETCH_ADD) == 0;
          atomic_fetch_sub_p = strcmp (op1->u.s.s, ATOMIC_FETCH_SUB) == 0;
          atomic_fetch_and_p = strcmp (op1->u.s.s, ATOMIC_FETCH_AND) == 0;
          atomic_fetch_or_p = strcmp (op1->u.s.s, ATOMIC_FETCH_OR) == 0;
          atomic_fetch_xor_p = strcmp (op1->u.s.s, ATOMIC_FETCH_XOR) == 0;
          atomic_cas_n_p = strcmp (op1->u.s.s, ATOMIC_COMPARE_EXCHANGE_N) == 0;
          atomic_fence_p = strcmp (op1->u.s.s, ATOMIC_THREAD_FENCE) == 0;
        }
        /* Unqualified call to a method of the enclosing class: rewrite the
           callee `m(...)` to `this.m(...)` before the implicit-declaration
           fallback below treats `m` as an unknown global function (and before
           the regular-call path treats a resolved method as a plain function,
           dropping the implicit `this`).  try_rewrite_implicit_this only fires
           when `m` is genuinely a member/method of the enclosing class, so a
           real global function of the same name is left untouched.  We attempt
           it whenever `m` is unresolved or resolves to a member/method (not to
           a genuine local/global object, which would shadow the member). */
        if (op1->code == N_ID) {
          node_t cdef = find_def(c2m_ctx, S_REGULARS, op1, curr_scope, NULL);
          if ((cdef == NULL || cdef->code == N_MEMBER || cdef->code == N_FUNC_DEF)
              && try_rewrite_implicit_this(c2m_ctx, op1)) {
            /* op1 is now N_DEREF_FIELD(this, m); the method-call path handles it. */
          }
        }
        /* Generic function call-site inference:  Max(3, 5)  ->  Max<int>(3, 5).
           When the callee is an unresolved N_ID that names a registered generic
           function template, infer the type arguments from the call's argument
           types, materialize (or reuse) the specialization, and rewrite the
           callee N_ID in place to the mangled specialization name.  The call
           then falls through to ordinary function-call resolution.

           Only fires when no concrete function/decl with this name is in scope
           (so a real function shadows a same-named template, and a template
           name used as a value is left alone). */
        if (op1->code == N_ID && find_def(c2m_ctx, S_REGULARS, op1, curr_scope, NULL) == NULL
            && is_generic_fn_p (c2m_ctx, op1->u.s.s)) {
          parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
          generic_fn_tmpl_t *gtmpl = get_generic_fn_template (c2m_ctx, op1->u.s.s);
          arg_list = NL_NEXT (op1);
          /* Pre-check the arguments so their types are known for inference.
             Guard with attr==NULL so a re-check (e.g. dict-init walk) doesn't
             re-check and double-prepend. */
          for (arg = NL_HEAD (arg_list->u.ops); arg != NULL; arg = NL_NEXT (arg))
            if (arg->attr == NULL) check (c2m_ctx, arg, r);
          /* Infer each type parameter by scanning the template's parameter list
             in order and picking the first parameter whose declared type is
             exactly that type parameter (T).  For `T Max<T>(T a, T b)`, both
             params are T, so the first argument's type fixes T.  Multi-param
             templates (e.g. `Pair<K,V>(K k, V v)`) infer K from arg 0 and V
             from arg 1.  If a parameter's type is not a bare type parameter,
             it contributes no inference. */
          node_t inferred[4] = {NULL, NULL, NULL, NULL};
          int n_inferred = 0;
          int infer_ok = 1;
          if (gtmpl != NULL && gtmpl->func_node != NULL) {
            node_t tdecl = FUNC_DEF_DECL (gtmpl->func_node);
            node_t tdecl_list = DECL_LIST (tdecl);
            node_t tfunc = (tdecl_list != NULL) ? NL_HEAD (tdecl_list->u.ops) : NULL;
            /* tfunc is the first (and usually only) N_FUNC in the decoration list. */
            while (tfunc != NULL && tfunc->code != N_FUNC)
              tfunc = NL_NEXT (tfunc);
            if (tfunc != NULL) {
              node_t tparams = NL_HEAD (tfunc->u.ops); /* N_LIST of N_SPEC_DECL/N_ID/N_TYPE */
              node_t tp = NL_HEAD (tparams->u.ops);
              node_t ca = NL_HEAD (arg_list->u.ops);
              for (; tp != NULL && ca != NULL; tp = NL_NEXT (tp), ca = NL_NEXT (ca)) {
                /* Resolve the parameter's declared type-spec node: for N_SPEC_DECL
                   it's the (shared) spec list at op 0; for N_TYPE it's the type
                   list at op 0. */
                node_t specs = NULL;
                node_t tdeclr = NULL;
                if (tp->code == N_SPEC_DECL) {
                  specs = NL_HEAD (tp->u.ops);
                  tdeclr = NL_EL (tp->u.ops, 1);
                } else if (tp->code == N_TYPE) {
                  specs = NL_HEAD (tp->u.ops);
                  tdeclr = NL_EL (tp->u.ops, 1);
                }
                if (specs == NULL) continue;
                /* Unwrap N_SHARE. */
                if (specs->code == N_SHARE) specs = NL_HEAD (specs->u.ops);
                if (specs->code != N_LIST) continue;
                node_t only = NL_HEAD (specs->u.ops);
                if (only == NULL || only->code != N_ID || NL_NEXT (only) != NULL) continue;

                /* Function-pointer params `G(*fn)(T)` have return type G in the
                   specs — do NOT bind G to the argument expression type (the fn
                   pointer itself).  Infer G from the *return type* of the call
                   argument if it is a function/func-ptr. */
                int has_func_decl = 0;
                if (tdeclr != NULL && tdeclr->code == N_DECL) {
                  node_t dlist = DECL_LIST (tdeclr);
                  for (node_t d = (dlist != NULL) ? NL_HEAD (dlist->u.ops) : NULL;
                       d != NULL; d = NL_NEXT (d))
                    if (d->code == N_FUNC || d->code == N_POINTER) {
                      /* Pointer-to-function usually has N_POINTER then N_FUNC. */
                      if (d->code == N_FUNC) has_func_decl = 1;
                      if (d->code == N_POINTER) {
                        /* Look deeper for N_FUNC. */
                        for (node_t d2 = d; d2 != NULL;) {
                          if (d2->code == N_FUNC) { has_func_decl = 1; break; }
                          if (d2->code == N_POINTER || d2->code == N_LIST)
                            d2 = NL_HEAD (d2->u.ops);
                          else break;
                        }
                      }
                      if (has_func_decl) break;
                    }
                  /* Also walk whole decoration list. */
                  if (!has_func_decl && dlist != NULL)
                    for (node_t d = NL_HEAD (dlist->u.ops); d != NULL; d = NL_NEXT (d))
                      if (d->code == N_FUNC) { has_func_decl = 1; break; }
                }

                /* Is this N_ID one of the template's type parameters? */
                int pi = -1;
                for (int i = 0; i < gtmpl->n_type_params; i++)
                  if (gtmpl->type_params[i] != NULL
                      && strcmp (only->u.s.s, gtmpl->type_params[i]) == 0) { pi = i; break; }

                struct expr *ae = ca->attr;
                if (ae == NULL || ae->type == NULL) continue;

                if (has_func_decl && pi >= 0 && inferred[pi] == NULL) {
                  /* Infer return-type type-param from fn arg return type. */
                  struct type *ft = ae->type;
                  while (ft != NULL && ft->mode == TM_PTR) ft = ft->u.ptr_type;
                  if (ft != NULL && ft->mode == TM_FUNC && ft->u.func_type != NULL
                      && ft->u.func_type->ret_type != NULL) {
                    node_t targ = build_seq_type_arg (c2m_ctx, ft->u.func_type->ret_type,
                                                      POS (op1));
                    if (targ != NULL) {
                      inferred[pi] = targ;
                      if (pi + 1 > n_inferred) n_inferred = pi + 1;
                    }
                  }
                  continue; /* don't also try bare-arg binding */
                }

                if (pi < 0) {
                  /* Spec is a class id like __generic_List_T — extract T from the
                     call arg if it is List_concrete (or pointer-to). */
                  const char *spec_nm = only->u.s.s;
                  if (spec_nm != NULL && strncmp (spec_nm, "__generic_", 10) == 0) {
                    struct type *at = ae->type;
                    while (at != NULL && at->mode == TM_PTR) at = at->u.ptr_type;
                    if (at != NULL && at->mode == TM_CLASS && at->u.tag_type != NULL) {
                      node_t tid = TAG_ID (at->u.tag_type);
                      const char *an = (tid != NULL && tid->code == N_ID) ? tid->u.s.s : NULL;
                      /* Match __generic_List_<Arg> against template List_T. */
                      if (an != NULL && strncmp (an, "__generic_", 10) == 0) {
                        /* Find shared class base and map remaining segments to type params. */
                        for (size_t _gi = 0;
                             parse_ctx != NULL && generic_templates != NULL
                             && _gi < VARR_LENGTH (generic_tmpl_t, generic_templates); _gi++) {
                          generic_tmpl_t *_gt = &VARR_ADDR (generic_tmpl_t, generic_templates)[_gi];
                          char _pfx[256];
                          snprintf (_pfx, sizeof (_pfx), "__generic_%s_", _gt->name);
                          size_t _pl = strlen (_pfx);
                          if (strncmp (spec_nm, _pfx, _pl) != 0) continue;
                          if (strncmp (an, _pfx, _pl) != 0) continue;
                          /* Template open: remaining param names (T, …).
                             Concrete: remaining mangled args (int, …). */
                          const char *_open = spec_nm + _pl;
                          const char *_conc = an + _pl;
                          /* Simple single-param case (List_T / List_int / List_PilotP). */
                          if (_gt->n_type_params == 1) {
                            int _tpi = -1;
                            for (int i = 0; i < gtmpl->n_type_params; i++)
                              if (gtmpl->type_params[i]
                                  && strcmp (gtmpl->type_params[i], _open) == 0)
                                { _tpi = i; break; }
                            /* Template open form List_TP means element is T*
                               (the open type-param name still binds free-fn T). */
                            if (_tpi < 0) {
                              size_t _ol = strlen (_open);
                              while (_ol > 0 && _open[_ol - 1] == 'P') _ol--;
                              char _opn[64];
                              if (_ol > 0 && _ol < sizeof (_opn)) {
                                memcpy (_opn, _open, _ol); _opn[_ol] = '\0';
                                for (int i = 0; i < gtmpl->n_type_params; i++)
                                  if (gtmpl->type_params[i]
                                      && strcmp (gtmpl->type_params[i], _opn) == 0)
                                    { _tpi = i; break; }
                              }
                            }
                            if (_tpi >= 0 && inferred[_tpi] == NULL) {
                              node_t _tn = NULL;
                              pos_t _ip = POS (op1);
                              /* Prefer the specialization cache: args already
                                 carry N_POINTER wrappers for T=Pilot*, etc. */
                              generic_spec_t *_csp
                                = find_generic_spec_by_name (c2m_ctx, an);
                              if (_csp != NULL && _csp->n_args >= 1
                                  && _csp->args[0] != NULL) {
                                node_t a = _csp->args[0];
                                int pd = 0;
                                while (a != NULL && a->code == N_POINTER) {
                                  pd++;
                                  a = NL_HEAD (a->u.ops);
                                }
                                if (a != NULL) {
                                  if (a->code == N_ID)
                                    _tn = build_id (c2m_ctx, a->u.s.s, _ip);
                                  else
                                    _tn = new_pos_node (c2m_ctx, a->code, _ip);
                                  for (int d = 0; d < pd; d++)
                                    _tn = new_pos_node1 (c2m_ctx, N_POINTER, _ip,
                                                         _tn);
                                }
                              }
                              /* Mangle fallback: trailing P means pointer depth
                                 (PilotP -> Pilot*, intPP -> int**).  Never strip
                                 P into a bare value type — that used to specialize
                                 GroupBy for List<Pilot> when the call had
                                 List<Pilot*>, then SIGSEGV in the JIT. */
                              if (_tn == NULL) {
                                size_t _cl = strlen (_conc);
                                int _pd = 0;
                                while (_cl > 0 && _conc[_cl - 1] == 'P') {
                                  _pd++;
                                  _cl--;
                                }
                                char _cn[64];
                                if (_cl > 0 && _cl < sizeof (_cn)) {
                                  memcpy (_cn, _conc, _cl); _cn[_cl] = '\0';
                                  if (strcmp (_cn, "int") == 0)
                                    _tn = new_pos_node (c2m_ctx, N_INT, _ip);
                                  else if (strcmp (_cn, "String") == 0)
                                    _tn = new_pos_node (c2m_ctx, N_STRING, _ip);
                                  else if (strcmp (_cn, "double") == 0)
                                    _tn = new_pos_node (c2m_ctx, N_DOUBLE, _ip);
                                  else if (strcmp (_cn, "float") == 0)
                                    _tn = new_pos_node (c2m_ctx, N_FLOAT, _ip);
                                  else if (strcmp (_cn, "long") == 0)
                                    _tn = new_pos_node (c2m_ctx, N_LONG, _ip);
                                  else if (strcmp (_cn, "short") == 0)
                                    _tn = new_pos_node (c2m_ctx, N_SHORT, _ip);
                                  else if (strcmp (_cn, "char") == 0)
                                    _tn = new_pos_node (c2m_ctx, N_CHAR, _ip);
                                  else if (strcmp (_cn, "bool") == 0)
                                    _tn = new_pos_node (c2m_ctx, N_BOOL, _ip);
                                  else if (strcmp (_cn, "void") == 0)
                                    _tn = new_pos_node (c2m_ctx, N_VOID, _ip);
                                  else if (strcmp (_cn, "dict") == 0)
                                    _tn = new_pos_node (c2m_ctx, N_DICT, _ip);
                                  else if (strcmp (_cn, "unsigned") == 0)
                                    _tn = new_pos_node (c2m_ctx, N_UNSIGNED, _ip);
                                  else
                                    _tn = build_id (c2m_ctx, _cn, _ip);
                                  if (_tn != NULL)
                                    for (int d = 0; d < _pd; d++)
                                      _tn = new_pos_node1 (c2m_ctx, N_POINTER,
                                                           _ip, _tn);
                                }
                              }
                              if (_tn != NULL) {
                                inferred[_tpi] = _tn;
                                if (_tpi + 1 > n_inferred) n_inferred = _tpi + 1;
                              }
                            }
                          }
                          break;
                        }
                      }
                    }
                  }
                  continue;
                }

                /* Bare type-param param (T x): bind from arg type. Skip function
                   pointers (handled above). */
                if (has_func_decl) continue;
                if (inferred[pi] != NULL) continue;
                node_t targ = build_seq_type_arg (c2m_ctx, ae->type, POS (op1));
                if (targ == NULL) continue;
                inferred[pi] = targ;
                if (pi + 1 > n_inferred) n_inferred = pi + 1;
              }
            }
          }
          if (gtmpl != NULL) {
            /* Fill any unfilled slots (e.g. a type param not used in any
               parameter signature) with a fallback int arg so mangle/substitute
               still produce a valid name.  This is a best-effort fallback;
               correct inference requires the parameter to appear in the
               signature. */
            for (int i = 0; i < gtmpl->n_type_params; i++) {
              if (inferred[i] == NULL)
                inferred[i] = new_pos_node (c2m_ctx, N_INT, POS (op1));
              if (i + 1 > n_inferred) n_inferred = i + 1;
            }
          }
          if (gtmpl != NULL && infer_ok && n_inferred == gtmpl->n_type_params) {
            size_t pend_mark = VARR_LENGTH (node_t, pending_lambdas);
            node_t spec_id = get_or_create_generic_fn_specialization (
              c2m_ctx, op1->u.s.s, n_inferred, inferred, POS (op1));
            materialize_pending_specs (c2m_ctx, pend_mark);
            if (spec_id != NULL && spec_id->code == N_ID) {
              /* Rewrite the callee N_ID in place so the normal call-resolution
                 path below resolves the specialization as a regular function. */
              op1->u.s = spec_id->u.s;
              /* Clear any stale attr so check(op1) re-resolves the new name. */
              op1->attr = NULL;
            }
          } else {
            error (c2m_ctx, POS (op1),
                   "cannot infer type arguments for generic function '%s'",
                   op1->u.s.s);
          }
        }
        if (op1->code == N_ID && find_def(c2m_ctx, S_REGULARS, op1, curr_scope, NULL) == NULL) {
          va_arg_p = str_eq_p(op1->u.s.s, BUILTIN_VA_ARG);
          va_start_p = str_eq_p(op1->u.s.s, BUILTIN_VA_START);
          if (!va_arg_p && !va_start_p && !alloca_p && !json_p && !atomic_load_n_p
              && !atomic_store_n_p && !atomic_exchange_n_p && !atomic_fetch_add_p
              && !atomic_fetch_sub_p && !atomic_fetch_and_p && !atomic_fetch_or_p
              && !atomic_fetch_xor_p && !atomic_cas_n_p && !atomic_fence_p) {
            warning (c2m_ctx, POS (op1),
                   "implicit declaration of function '%s' — did you forget an #include?",
                   op1->u.s.s);
            spec_list = new_node1 (c2m_ctx, N_LIST, new_node (c2m_ctx, N_INT));
            list = new_node1 (c2m_ctx, N_LIST,
                              new_node1 (c2m_ctx, N_FUNC, new_node (c2m_ctx, N_LIST)));
            decl = build_shared_spec_decl (c2m_ctx, POS (op1), spec_list,
                                           build_decl (c2m_ctx, POS (op1),
                                                       copy_node (c2m_ctx, op1), list),
                                           NULL, NULL, NULL);
            curr_scope = top_scope;
            check(c2m_ctx, decl, NULL);
            curr_scope = saved_scope;
            assert(top_scope->code == N_MODULE);
            list = NL_HEAD(top_scope->u.ops);
            assert(list->code == N_LIST);
            op_prepend(c2m_ctx, list, decl);
          }
        }
        builtin_call_p = alloca_p || va_arg_p || va_start_p || add_overflow_p || sub_overflow_p
                         || mul_overflow_p || expect_p || jcall_p || jret_p || prop_set_p || prop_eq_p
                         || prop_ne_p || json_p || atomic_load_n_p || atomic_store_n_p
                         || atomic_exchange_n_p || atomic_fetch_add_p || atomic_fetch_sub_p
                         || atomic_fetch_and_p || atomic_fetch_or_p || atomic_fetch_xor_p
                         || atomic_cas_n_p || atomic_fence_p;
        /* Capturing HOF desugar must run before call_nodes is populated (same
           reason as nameof): the node is rewritten into an N_STMTEXPR, which is
           not a call for MIR proto generation. */
        if ((op1->code == N_FIELD || op1->code == N_DEREF_FIELD)
            && try_desugar_capturing_hof_call (c2m_ctx, r)) {
          e = r->attr;
          break;
        }
        if (!builtin_call_p || jcall_p) {
            VARR_PUSH(node_t, call_nodes, r);
            //printf("XXXXX call_nodes add uid=%d N_ID = %s \n", r->uid, op1->u.s.s);
        }
        arg_list = NL_NEXT(op1);
        if (builtin_call_p) {
          if (func_block_scope != NULL && (va_arg_p || va_start_p))
            ((struct node_scope *)func_block_scope->attr)->stack_var_p = TRUE;
          for (arg = NL_HEAD(arg_list->u.ops); arg != NULL; arg = NL_NEXT(arg))
            check(c2m_ctx, arg, r);
          init_type(&res_type);
          if (alloca_p) {
            res_type.mode = TM_PTR;
            res_type.u.ptr_type = &VOID_TYPE;
          } else {
            res_type.mode = TM_BASIC;
            res_type.u.basic_type = (va_arg_p || add_overflow_p || sub_overflow_p || mul_overflow_p
                                        || atomic_cas_n_p
                                      ? TP_INT
                                    : expect_p || prop_eq_p || prop_ne_p ? TP_LONG : TP_VOID);
          }
          ret_type = &res_type;
          if (builtin_call_p
              && ((va_start_p && NL_LENGTH(arg_list->u.ops) != 1)
                  || (alloca_p && NL_LENGTH(arg_list->u.ops) != 1)
                  || (add_overflow_p && NL_LENGTH(arg_list->u.ops) != 3)
                  || (sub_overflow_p && NL_LENGTH(arg_list->u.ops) != 3)
                  || (mul_overflow_p && NL_LENGTH(arg_list->u.ops) != 3)
                  || (expect_p && NL_LENGTH(arg_list->u.ops) != 2)
                  || (jret_p && NL_LENGTH(arg_list->u.ops) != 1)
                  || (va_arg_p && NL_LENGTH(arg_list->u.ops) != 2)
                  || (prop_set_p && NL_LENGTH(arg_list->u.ops) != 2)
                  || ((prop_eq_p || prop_ne_p) && NL_LENGTH(arg_list->u.ops) != 2)
                  || (json_p && NL_LENGTH(arg_list->u.ops) != 1)
                  || (atomic_load_n_p && NL_LENGTH (arg_list->u.ops) != 2)
                  || (atomic_store_n_p && NL_LENGTH (arg_list->u.ops) != 3)
                  || (atomic_exchange_n_p && NL_LENGTH (arg_list->u.ops) != 3)
                  || ((atomic_fetch_add_p || atomic_fetch_sub_p || atomic_fetch_and_p
                       || atomic_fetch_or_p || atomic_fetch_xor_p)
                      && NL_LENGTH (arg_list->u.ops) != 3)
                  || (atomic_cas_n_p && NL_LENGTH (arg_list->u.ops) != 6)
                  || (atomic_fence_p && NL_LENGTH (arg_list->u.ops) != 1))) {
            error(c2m_ctx, POS(op1), "wrong number of arguments in %s call", op1->u.s.s);
          } else {
            if (va_arg_p) {
              arg = NL_EL(arg_list->u.ops, 1);
              e2 = arg->attr;
              t2 = e2->type;
              if (t2->mode != TM_PTR)
                error(c2m_ctx, POS(arg), "wrong type of 2nd argument of %s call", BUILTIN_VA_ARG);
              else
                ret_type = t2->u.ptr_type;
            } else if (add_overflow_p || sub_overflow_p || mul_overflow_p) {
              arg = NL_EL(arg_list->u.ops, 2);
              e2 = arg->attr;
              t2 = e2->type;
              if (t2->mode != TM_PTR || !standard_integer_type_p(t2->u.ptr_type)
                  || t2->u.ptr_type->u.basic_type < TP_INT)
                error(c2m_ctx, POS(arg), "wrong type of 3rd argument of %s call",
                      add_overflow_p ? ADD_OVERFLOW : sub_overflow_p ? SUB_OVERFLOW : MUL_OVERFLOW);
              for (int i = 0; i < 2; i++) {
                arg = NL_EL(arg_list->u.ops, i);
                e2 = arg->attr;
                if (!integer_type_p(e2->type))
                  error(c2m_ctx, POS(arg), "non-integer type of %d argument of %s call", i,
                        add_overflow_p ? ADD_OVERFLOW : sub_overflow_p ? SUB_OVERFLOW : MUL_OVERFLOW);
              }
            } else if (expect_p) {
              for (int i = 0; i < 2; i++) {
                arg = NL_EL(arg_list->u.ops, i);
                e2 = arg->attr;
                if (!integer_type_p(e2->type))
                  error(c2m_ctx, POS(arg), "non-integer type of %d argument of %s call", i, EXPECT);
              }
            } else if (atomic_load_n_p || atomic_store_n_p || atomic_exchange_n_p || atomic_fetch_add_p
                       || atomic_fetch_sub_p || atomic_fetch_and_p || atomic_fetch_or_p
                       || atomic_fetch_xor_p || atomic_cas_n_p) {
              /* Arg0: T* (often _Atomic T *).  Return T for load/exchange/fetch_*;
                 void-ish for store; int (bool) for cas. */
              arg = NL_HEAD (arg_list->u.ops);
              e2 = arg->attr;
              t2 = e2->type;
              if (t2->mode != TM_PTR || t2->u.ptr_type == NULL
                  || !(integer_type_p (t2->u.ptr_type) || t2->u.ptr_type->mode == TM_PTR))
                error (c2m_ctx, POS (arg), "pointer to integer/pointer expected as 1st arg of %s",
                       op1->u.s.s);
              else if (atomic_load_n_p || atomic_exchange_n_p || atomic_fetch_add_p
                       || atomic_fetch_sub_p || atomic_fetch_and_p || atomic_fetch_or_p
                       || atomic_fetch_xor_p)
                ret_type = t2->u.ptr_type;
              if (atomic_cas_n_p) {
                arg = NL_EL (arg_list->u.ops, 1);
                e2 = arg->attr;
                if (e2->type->mode != TM_PTR)
                  error (c2m_ctx, POS (arg), "pointer expected as expected-value arg of %s",
                         ATOMIC_COMPARE_EXCHANGE_N);
              }
            } else if (jret_p) {
              arg = NL_HEAD(arg_list->u.ops);
              e2 = arg->attr;
              t2 = e2->type;
              if (t2->mode != TM_PTR)
                error(c2m_ctx, POS(arg), "non-pointer type of 1st argument of %s call", JRET);
            } else if (jcall_p) {
              arg = NL_HEAD(arg_list->u.ops);
              e2 = arg->attr;
              t2 = e2->type;
              if (t2->mode != TM_PTR || (t2 = t2->u.ptr_type)->mode != TM_FUNC) {
                error(c2m_ctx, POS(r), "calling non-function in %s", JCALL);
                break;
              }
              func_type = t2->u.func_type;
              ret_type = func_type->ret_type;
              if (!void_type_p(ret_type)) {
                error(c2m_ctx, POS(arg), "calling non-void function in %s", JCALL);
                break;
              }
            } else if (prop_set_p || prop_eq_p || prop_ne_p) {
              arg = NL_HEAD(arg_list->u.ops);
              e2 = arg->attr;
              if (!e2->u.lvalue_node)
                error(c2m_ctx, POS(r), "1st arg of %s should be lvalue", op1->u.s.s);
              arg = NL_NEXT(arg);
              e2 = arg->attr;
              t2 = e2->type;
              if (!e2->const_p || !integer_type_p(t2))
                error(c2m_ctx, POS(arg),
                      "property value in 2nd arg of %s call should be an integer constant",
                      op1->u.s.s);
            } else if (json_p) {
              /* json(dict) → String (serialize),  json(string) → dict (parse) */
              arg = NL_HEAD(arg_list->u.ops);
              e2 = arg->attr;
              t2 = e2->type;
              if (t2->mode == TM_DICT) {
                /* json(dict) → String (serialized JSON, works in f-strings and +).
                   TP_STRING is pointer-sized; set raw_size/align manually. */
                res_type.mode = TM_BASIC;
                res_type.u.basic_type = TP_STRING;
                res_type.raw_size = 8;
                res_type.align    = 8;
              } else if (t2->mode == TM_PTR || t2->mode == TM_ARR
                         || string_type_p(t2)) {
                /* json(string) → dict (parsed JSON) */
                res_type.mode = TM_DICT;
              } else {
                error(c2m_ctx, POS(arg),
                      "json() argument must be a dict or string, got unsupported type");
              }
              ret_type = &res_type;
            }
          }
        } else {
          check(c2m_ctx, op1, r);
          e1 = op1->attr;
          t1 = e1->type;
	          // Check if this is a method call (N_FIELD: obj.method, N_DEREF_FIELD: ptr->method)
	          if ((op1->code == N_FIELD || op1->code == N_DEREF_FIELD)) {
	            node_t obj = NL_HEAD(op1->u.ops);        // Object (obj or ptr)

	            if (obj->code == N_STRING) {
	              // Special case for built-in String identifier (static method):
	              // String.copy(p, len), etc.  No receiver value, static lookup via get_string_method.
	              // (A literal receiver like "abc".lower() is an N_STR node and is
	              //  handled as an instance method in the else branch below.)
	              node_t method_id = NL_NEXT(obj);
	              int nargs = 0;
	              enum str_method sm = get_string_method(method_id->u.s.s, &nargs, NULL);
	              if (sm == SM_NONE) {
	                error(c2m_ctx, POS(r), "unknown String static method '%s'", method_id->u.s.s);
	                break;
	              }
	              if (NL_LENGTH(arg_list->u.ops) != nargs) {
	                error(c2m_ctx, POS(r), "String method '%s' expects %d argument%s",
	                      method_id->u.s.s, nargs, nargs == 1 ? "" : "s");
	              }
	              for (arg = NL_HEAD(arg_list->u.ops); arg != NULL; arg = NL_NEXT(arg))
	                if (!arg->attr) check(c2m_ctx, arg, r);
	              init_type(&res_type);
	              res_type.mode = TM_BASIC;
	              if (sm == SM_COPY || sm == SM_ATTACH) {
	                res_type.u.basic_type = TP_STRING;
	                res_type.type_qual.const_p = 1;
	                res_type.raw_size = 8;
	                res_type.align = 8;
	              } else {
	                error(c2m_ctx, POS(r), "static String method '%s' not supported yet", method_id->u.s.s);
	                break;
	              }
	              ret_type = &res_type;
	              method_call_p = TRUE;
	                    } else if (obj->code == N_ID) {
	                      /* Static dispatch on a class name: ClassName.method(args).
	                         The N_FIELD checker already resolved this to a static method
	                         and set a void placeholder.  Here we resolve the actual
	                         function type and check arguments (no implicit 'this').

	                         Use find_def (which walks the scope chain from top_scope)
	                         rather than symbol_find with scope=NULL: a generic
	                         specialization's tag is inserted at top_scope (a non-NULL
	                         pointer), so a NULL-scope lookup misses it.

	                         Only enter this branch when obj is actually a class name
	                         (found via S_TAG) or a mangled __generic_ specialization
	                         name.  An N_ID that is a plain variable (e.g. `v` in
	                         `v->render()`) must fall through to the `else` branch
	                         below so it is handled as a method call on a value. */
	                      node_t class_node = find_def (c2m_ctx, S_TAG, obj, top_scope, NULL);
	                      int is_generic_spec = (strncmp(obj->u.s.s, "__generic_", 10) == 0);
	                      if ((class_node != NULL && class_node->code == N_CLASS)
	                          || is_generic_spec) {
	                        if (class_node != NULL && class_node->code == N_CLASS) {
	                node_t method_id = NL_NEXT(obj);
	                symbol_t msym;
	                node_t func_def = NULL;
	                /* Pre-check the user arguments so their types are known for
	                   overload resolution, then pick the best-matching static
	                   overload (several static methods may share a name,
	                   distinguished by arity/parameter types). */
	                for (arg = NL_HEAD(arg_list->u.ops); arg != NULL; arg = NL_NEXT(arg))
	                  if (!arg->attr) check(c2m_ctx, arg, r);
	                if (find_overload_sym(c2m_ctx, method_id, class_node, &msym))
	                  func_def = select_method_overload(c2m_ctx, &msym, class_node, arg_list);
	                if (!func_def)
	                  func_def = find_def(c2m_ctx, S_REGULARS, method_id, class_node, NULL);
	                if (!func_def || func_def->code != N_FUNC_DEF) {
	                  error(c2m_ctx, POS(r), "class %s has no static method %s",
	                        obj->u.s.s, method_id->u.s.s);
	                  break;
	                }
	                decl_t mdecl = func_def->attr;
	                struct type *ft = mdecl->decl_spec.type;
	                if (ft->mode != TM_FUNC) {
	                  error(c2m_ctx, POS(r), "static member %s is not a function", method_id->u.s.s);
	                  break;
	                }
	                func_type = ft->u.func_type;
	                ret_type = func_type->ret_type;
	                /* Point the N_FIELD expr at the resolved func_def so gen
	                   emits a direct call (no 'this'). */
	                {
	                  struct expr *fe = op1->attr;
	                  if (fe != NULL) {
	                    struct type *pf = create_type(c2m_ctx, NULL);
	                    pf->mode = TM_PTR;
	                    pf->u.ptr_type = ft;
	                    set_type_layout(c2m_ctx, pf);
	                    fe->type = pf;
	                    fe->def_node = func_def;
	                    if (func_def->attr != NULL
	                        && func_def->attr != (void *) ((intptr_t) -1))
	                      ((decl_t) func_def->attr)->used_p = TRUE;
	                  }
	                }
	                /* Check user args against params (no implicit 'this'). */
	                param_list = func_type->param_list;
	                param = NL_HEAD(param_list->u.ops);
	                for (arg = NL_HEAD(arg_list->u.ops); arg != NULL;) {
	                  node_t arg_next = NL_NEXT(arg);
	                  if (!arg->attr) check(c2m_ctx, arg, r);
	                  e2 = arg->attr;
	                  if (param == NULL) {
	                    if (!func_type->dots_p)
	                      error(c2m_ctx, POS(arg), "too many arguments in static method call");
	                    arg = arg_next;
	                    continue;
	                  }
	                  struct decl_spec *ds = get_param_decl_spec(param);
	                  check_assignment_types(c2m_ctx, ds->type, NULL, e2, r);
	                  param = NL_NEXT(param);
	                  arg = arg_next;
	                }
	                if (param != NULL)
	                  error(c2m_ctx, POS(r), "too few arguments in static method call");
	                method_call_p = TRUE;
	              }
	              /* If the class wasn't found via S_TAG, check if it's a
	                 generic specialization name (__generic_...) or a type
	                 parameter.  In that case, just set a void return type
	                 placeholder — the specialized body will re-check with
	                 the real class. */
	              else {
	                /* Generic specialization name not yet materialized.
	                   Set a permissive return type.  The N_FIELD expr needs
	                   a function-pointer type so gen_mir_protos doesn't assert. */
	                for (arg = NL_HEAD(arg_list->u.ops); arg != NULL; arg = NL_NEXT(arg))
	                  if (!arg->attr) check(c2m_ctx, arg, r);
	                init_type(&res_type);
	                res_type.mode = TM_BASIC;
	                res_type.u.basic_type = TP_LONG;
	                ret_type = &res_type;
	                /* Set the N_FIELD expr to a dummy function-pointer type. */
	                {
	                  struct expr *fe = op1->attr;
	                  if (fe != NULL) {
	                    struct type *pf = create_type(c2m_ctx, NULL);
	                    pf->mode = TM_PTR;
	                    struct type *ff = create_type(c2m_ctx, NULL);
	                    ff->mode = TM_FUNC;
	                    ff->u.func_type = c2m_alloc(c2m_ctx) ? NULL : NULL;
	                    pf->u.ptr_type = ff;
	                    set_type_layout(c2m_ctx, pf);
	                    fe->type = pf;
	                  }
	                }
	                method_call_p = TRUE;
	              }
	                      } /* end: obj is a class name or __generic_ spec */
	                      else goto method_call_on_value;
	                    } else {
	              method_call_on_value:;
	              struct type *obj_type = ((struct expr *)obj->attr)->type;
	              /* A bare UTF-8 string literal used as a method receiver
	                 ("abc".lower()) is an N_STR node: dispatch the built-in String
	                 instance methods on it just like a String-typed value. */
	              int str_literal_recv_p = (obj->code == N_STR);

	              // For N_DEREF_FIELD, dereference the pointer to get the actual type
	              if (op1->code == N_DEREF_FIELD) {
	                if (obj_type->mode != TM_PTR) {
	                  error (c2m_ctx, POS(r), "dereference operator applied to non-pointer in method call");
	                  break;
	                }
	                obj_type = obj_type->u.ptr_type;  // Get the pointed-to type
	              }

	              /* Sequence lambda-method call: receiver is an array, a slice, or
	                 a class with the Count()/Get(int) protocol — and, for classes,
	                 no user method shadows the builtin name. */
              {
                node_t seq_mid = NL_NEXT (obj);
                enum seq_method seqm = seq_mid != NULL && seq_mid->code == N_ID
                                         ? get_seq_method (seq_mid->u.s.s, NULL)
                                         : SEQM_NONE;
                struct seq_recv seq_sr;
                int seq_call_p = FALSE;

                if (seqm != SEQM_NONE && !builtin_string_type_p (obj_type)
                    && !str_literal_recv_p
                    && classify_seq_receiver (c2m_ctx, obj_type, POS (r), &seq_sr)
                         != SEQ_RECV_NONE)
                  seq_call_p
                    = (seq_sr.kind != SEQ_RECV_CLASS
                       || find_def (c2m_ctx, S_REGULARS, seq_mid, seq_sr.cls_type->u.tag_type,
                                    NULL)
                            == NULL);
                if (seq_call_p) {
                  if ((ret_type = check_seq_method_call (c2m_ctx, r, seqm, &seq_sr, arg_list))
                      == NULL)
                    break;
                  method_call_p = TRUE;
                  goto seq_method_done;
                }
              }

	              // Only proceed with method call logic if the object is a class (TM_CLASS)
	              if (obj_type->mode == TM_CLASS) {
		                node_t method_id = NL_NEXT(obj);  // Method name (N_ID)

		                /* Generic method monomorphization: xs->Select<String>(fn)
		                   or xs->Select(fn) with U inferred from fn's return type. */
		                if (method_id != NULL && method_id->code == N_ID) {
		                  node_t tag = obj_type->u.tag_type;
		                  node_t tid = (tag != NULL) ? TAG_ID (tag) : NULL;
		                  const char *cn = (tid != NULL && tid->code == N_ID) ? tid->u.s.s : NULL;
		                  generic_spec_t *gsp
		                    = (cn != NULL) ? find_generic_spec_by_name (c2m_ctx, cn) : NULL;
		                  const char *base = (gsp != NULL) ? gsp->orig_name : cn;
		                  generic_method_tmpl_t *mt
		                    = (base != NULL)
		                        ? get_generic_method_template (c2m_ctx, base, method_id->u.s.s)
		                        : NULL;
		                  if (mt != NULL && gsp != NULL) {
		                    node_t explicit_targs = NL_NEXT (method_id); /* third FIELD child */
		                    node_t margs[4] = {NULL, NULL, NULL, NULL};
		                    int n_margs = 0;
		                    int infer_ok = 1;

		                    /* Pre-check user args for inference. */
		                    for (node_t ua = NL_HEAD (arg_list->u.ops); ua != NULL;
		                         ua = NL_NEXT (ua))
		                      if (ua->attr == NULL) check (c2m_ctx, ua, r);

		                    if (explicit_targs != NULL && explicit_targs->code == N_LIST) {
		                      for (node_t ta = NL_HEAD (explicit_targs->u.ops);
		                           ta != NULL && n_margs < 4; ta = NL_NEXT (ta))
		                        margs[n_margs++] = ta;
		                    } else {
		                      /* Infer U from the first user arg that is a function
		                         pointer: U(*fn)(T) -> return type is U. */
		                      node_t first = NL_HEAD (arg_list->u.ops);
		                      if (first != NULL && first->attr != NULL) {
		                        struct expr *fe = first->attr;
		                        struct type *ft = fe->type;
		                        if (ft != NULL && ft->mode == TM_PTR && ft->u.ptr_type
		                            && ft->u.ptr_type->mode == TM_FUNC) {
		                          struct type *rt = ft->u.ptr_type->u.func_type->ret_type;
		                          node_t ta = build_seq_type_arg (c2m_ctx, rt, POS (r));
		                          if (ta != NULL) margs[n_margs++] = ta;
		                          else infer_ok = 0;
		                        } else {
		                          infer_ok = 0;
		                        }
		                      } else {
		                        infer_ok = 0;
		                      }
		                      /* Pad remaining method type params with int fallback. */
		                      while (n_margs < mt->n_type_params)
		                        margs[n_margs++]
		                          = new_pos_node (c2m_ctx, N_INT, POS (r));
		                    }
		                    if (n_margs < mt->n_type_params) {
		                      error (c2m_ctx, POS (r),
		                             "cannot infer type arguments for generic method '%s'",
		                             method_id->u.s.s);
		                      break;
		                    }
		                    if (!infer_ok && (explicit_targs == NULL
		                                   || explicit_targs->code != N_LIST)) {
		                      error (c2m_ctx, POS (r),
		                             "cannot infer type arguments for generic method '%s' "
		                             "(use explicit <Type> or pass a function pointer)",
		                             method_id->u.s.s);
		                      break;
		                    }
		                    {
		                                      parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
		                                      size_t pend_mark = VARR_LENGTH (node_t, pending_lambdas);
		                                      node_t spec_id = get_or_create_generic_method_specialization (
		                        c2m_ctx, base, cn, gsp->n_args, gsp->args, mt, n_margs, margs,
		                        POS (r));
		                      materialize_pending_specs (c2m_ctx, pend_mark);
		                      if (spec_id == NULL || spec_id->code != N_ID) {
		                        error (c2m_ctx, POS (r),
		                               "failed to specialize generic method '%s'",
		                               method_id->u.s.s);
		                        break;
		                      }
		                      /* Rewrite callee to free function; prepend this. */
		                      {
		                        node_t new_callee = build_id (c2m_ctx, spec_id->u.s.s, POS (r));
		                        /* Replace N_FIELD callee with N_ID. */
		                        NL_REMOVE (r->u.ops, op1);
		                        NL_PREPEND (r->u.ops, new_callee);
		                        op1 = new_callee;
		                        if (!mt->is_static && r->attr == NULL) {
		                          node_t this_arg = copy_node (c2m_ctx, obj);
		                          this_arg->attr = obj->attr;
		                          if (op1 /* keep */ && (NL_HEAD (r->u.ops))) {
		                            /* was DEREF_FIELD or FIELD before rewrite */
		                          }
		                          /* obj is already a pointer for -> form; for . form
		                             need address — obj_type is class value type only if
		                             not from DEREF_FIELD.  For method calls via
		                             pointer (List*), receiver is pointer. */
		                          struct expr *oex = obj->attr;
		                          if (oex != NULL && oex->type != NULL
		                              && oex->type->mode == TM_CLASS) {
		                            node_t addr = new_node1 (c2m_ctx, N_ADDR, this_arg);
		                            struct expr *ae = create_expr (c2m_ctx, addr);
		                            ae->type->mode = TM_PTR;
		                            ae->type->u.ptr_type = oex->type;
		                            set_type_layout (c2m_ctx, ae->type);
		                            this_arg = addr;
		                          }
		                          NL_PREPEND (arg_list->u.ops, this_arg);
		                        }
		                        /* Fall through to regular function call path. */
		                        check (c2m_ctx, op1, r);
		                        e1 = op1->attr;
		                        t1 = e1 != NULL ? e1->type : NULL;
		                        if (t1 == NULL || t1->mode != TM_PTR
		                            || t1->u.ptr_type == NULL
		                            || t1->u.ptr_type->mode != TM_FUNC) {
		                          error (c2m_ctx, POS (r),
		                                 "specialized generic method is not a function");
		                          break;
		                        }
		                        func_type = t1->u.ptr_type->u.func_type;
		                        ret_type = func_type->ret_type;
		                        /* Check args including this. */
		                        param_list = func_type->param_list;
		                        param = NL_HEAD (param_list->u.ops);
		                        for (arg = NL_HEAD (arg_list->u.ops); arg != NULL;) {
		                          node_t arg_next = NL_NEXT (arg);
		                          if (!arg->attr) check (c2m_ctx, arg, r);
		                          e2 = arg->attr;
		                          if (param == NULL) {
		                            if (!func_type->dots_p)
		                              error (c2m_ctx, POS (arg),
		                                     "too many arguments in generic method call");
		                            arg = arg_next;
		                            continue;
		                          }
		                          {
		                            struct decl_spec *ds = get_param_decl_spec (param);
		                            check_assignment_types (c2m_ctx, ds->type, NULL, e2, r);
		                          }
		                          param = NL_NEXT (param);
		                          arg = arg_next;
		                        }
		                        if (param != NULL)
		                          error (c2m_ctx, POS (r),
		                                 "too few arguments in generic method call");
		                        method_call_p = TRUE;
		                        goto seq_method_done;
		                      }
		                    }
		                  }
		                }

		                // Built-in List<String>::join(delim) -> String.  List<String>
		                // defines no user `join`, so intercept it here (before the
		                // normal method lookup) and lower to c2m_str_join in gen.
		                if (list_string_type_p(obj_type)
		                    && get_string_method(method_id->u.s.s, NULL, NULL) == SM_JOIN
		                    && find_def(c2m_ctx, S_REGULARS, method_id, obj_type->u.tag_type, NULL)
		                         == NULL) {
		                  if (NL_LENGTH(arg_list->u.ops) != 1)
		                    error(c2m_ctx, POS(r), "String method 'join' expects 1 argument");
		                  for (arg = NL_HEAD(arg_list->u.ops); arg != NULL; arg = NL_NEXT(arg))
		                    if (!arg->attr) check(c2m_ctx, arg, r);
		                  init_type(&res_type);
		                  res_type.mode = TM_BASIC;
		                  res_type.u.basic_type = TP_STRING;
		                  res_type.type_qual.const_p = 1;
		                  res_type.raw_size = 8;
		                  res_type.align = 8;
		                  ret_type = &res_type;
		                  method_call_p = TRUE;
		                  goto seq_method_done;
		                }

		                // Pre-check the user arguments so their types are known for
	                // overload resolution (this runs before the implicit 'this' is
	                // prepended below).
	                for (node_t ua = NL_HEAD(arg_list->u.ops); ua != NULL; ua = NL_NEXT(ua))
	                  if (!ua->attr) check(c2m_ctx, ua, r);

	                // Find the method in the class scope.  When several overloads
	                // share the name, pick the best match for the argument types.
	                symbol_t msym;
	                node_t func_def = NULL;
	                int have_msym = find_overload_sym(c2m_ctx, method_id, obj_type->u.tag_type, &msym);
	                if (have_msym)
	                  func_def = select_method_overload(c2m_ctx, &msym, obj_type->u.tag_type, arg_list);
	                if (!func_def)
	                  func_def = find_def(c2m_ctx, S_REGULARS, method_id, obj_type->u.tag_type, NULL);
	                /* Collection convenience: list->AddRange(arr) fills the count of a
	                   (T* items, int count) method, just like the constructor case. */
	                if (have_msym) {
	                  node_t expanded
	                    = select_overload_with_count_expansion(c2m_ctx, r, &msym, TRUE);
	                  if (expanded != NULL) func_def = expanded;
	                }
	                if (!func_def) {
	                  /* UFCS: rewrite `recv->F(args)` → `F(recv, args)` when F is a
	                     free generic function (e.g. list->GroupBy(fn)). */
	                  if (method_id != NULL && method_id->code == N_ID
	                      && ufcs_free_fn_candidate_p (c2m_ctx, method_id->u.s.s)) {
	                    node_t free_id = build_id (c2m_ctx, method_id->u.s.s, POS (r));
	                    node_t this_arg;
	                    node_t obj_copy = copy_node (c2m_ctx, obj);
	                    obj_copy->attr = obj->attr;
	                    if (op1->code == N_FIELD) {
	                      /* value.method(): pass &value */
	                      this_arg = new_node1 (c2m_ctx, N_ADDR, obj_copy);
	                      {
	                        struct expr *ae = create_expr (c2m_ctx, this_arg);
	                        ae->type->mode = TM_PTR;
	                        ae->type->u.ptr_type = obj_type;
	                        set_type_layout (c2m_ctx, ae->type);
	                      }
	                    } else {
	                      /* ptr->method(): receiver is already a pointer */
	                      this_arg = obj_copy;
	                    }
	                    /* Replace FIELD/DEREF_FIELD callee with free function name. */
	                    NL_REMOVE (r->u.ops, op1);
	                    NL_PREPEND (r->u.ops, free_id);
	                    NL_PREPEND (arg_list->u.ops, this_arg);
	                    r->attr = NULL;
	                    free_id->attr = NULL;
	                    /* Re-check as free generic call (inference + ordinary call path).
	                       call_nodes already contains r from the outer visit — skip
	                       double-push by detecting re-entry via a temporary flag: the
	                       inner check will push again; gen_mir_protos tolerates
	                       duplicate entries for the same rewritten N_ID call. */
	                    check (c2m_ctx, r, context);
	                    e = r->attr;
	                    break;
	                  }
	                  error(c2m_ctx, POS(r), "method '%s' not found in class", method_id->u.s.s);
	                  break;
	                }
	                if (func_def->code != N_FUNC_DEF) {
	                  /* Not a method but a data member.  If it is a function-pointer
	                     field (e.g. the synthesized Any<I> slots, or a delegate
	                     stored in a class), resolve `obj.fp(args)` as an ordinary
	                     indirect call through the field's value: no implicit 'this',
	                     dispatched via the loaded pointer at gen time.  This reuses
	                     the regular delegate-call path at seq_method_done. */
	                  if (t1->mode == TM_PTR && t1->u.ptr_type->mode == TM_FUNC) {
	                    func_type = t1->u.ptr_type->u.func_type;
	                    ret_type = func_type->ret_type;
	                    goto seq_method_done;
	                  }
	                  error(c2m_ctx, POS(r), "member '%s' is not a function", method_id->u.s.s);
	                  break;
	                }

	                // Get function type from method definition
	                decl_t decl = func_def->attr;
	                struct type *func_type_type = decl->decl_spec.type;
	                if (func_type_type->mode != TM_FUNC) {
	                  error(c2m_ctx, POS(r), "method '%s' does not have function type", method_id->u.s.s);
	                  break;
	                }
	                func_type = func_type_type->u.func_type;
	                ret_type = func_type->ret_type;

	                // Point the method-access node at the *resolved* overload so that
	                // proto generation (gen_mir_protos) and the call emission both use
	                // this overload's signature and function item, rather than the
	                // first-declared method that field resolution initially attached.
	                {
	                  struct expr *fe = op1->attr;
	                  if (fe != NULL) {
	                    struct type *pf = create_type(c2m_ctx, NULL);
	                    pf->mode = TM_PTR;
	                    pf->u.ptr_type = func_type_type;
	                    set_type_layout(c2m_ctx, pf);
	                    fe->type = pf;
	                    fe->def_node = func_def;
	                    if (func_def->attr != NULL
	                        && func_def->attr != (void *) ((intptr_t) -1))
	                      ((decl_t) func_def->attr)->used_p = TRUE;
	                  }
	                }

	                // Determine if this is a static class method (no implicit 'this').
	                int is_static_meth = decl->decl_spec.static_p;

	                // Detect whether the base expression is the class TYPE itself
	                // (e.g. ClassName.method()) rather than a class instance (obj.method()).
	                // A bare class-type reference has def_node == N_CLASS node (no lvalue).
	                struct expr *obj_expr_attr = obj->attr;
	                int is_type_ref = (obj_expr_attr != NULL
	                                   && obj_expr_attr->def_node != NULL
	                                   && obj_expr_attr->def_node->code == N_CLASS);

	                // Calling a non-static method on a class type (not an instance) is an error.
	                if (is_type_ref && !is_static_meth) {
	                  error(c2m_ctx, POS(r),
	                        "cannot call non-static method '%s' on class type (use an instance)",
	                        method_id->u.s.s);
	                  break;
	                }

	                if (!is_static_meth) {
	                  // Non-static method: prepend 'this' argument.
	                  // Guard: do this only on the first visit to this CALL node.
	                  // Re-checks (e.g. from check_dict_init_list + N_INIT tree walk
	                  // for dict initializers) would otherwise prepend multiple times.
	                  if (r->attr == NULL) {
	                    node_t this_arg;
	                    node_t obj_copy = copy_node(c2m_ctx, obj);
	                    obj_copy->attr = obj->attr;
	                    if (op1->code == N_FIELD) {
	                      // obj.method(): pass the address of the (value) class object.
	                      this_arg = new_node1(c2m_ctx, N_ADDR, obj_copy);
	                      struct expr *ae = create_expr(c2m_ctx, this_arg);
	                      ae->type->mode = TM_PTR;
	                      ae->type->u.ptr_type = obj_type;
	                      set_type_layout(c2m_ctx, ae->type);
	                    } else {
	                      // ptr->method(): the receiver is already a pointer value.
	                      this_arg = obj_copy;
	                    }
	                    // Prepend 'this' argument into the call's arg_list
	                    NL_PREPEND(arg_list->u.ops, this_arg);
	                  }
	                }
	                // For static methods: no 'this' is added; args are passed as-is.

	                // Check arguments against parameters.
	                // For non-static methods, param_list starts with the 'this' param;
	                // for static methods, param_list starts directly at the user params.
	                param_list = func_type->param_list;
	                param = NL_HEAD(param_list->u.ops);
	                for (arg = NL_HEAD(arg_list->u.ops); arg != NULL;) {
	                  node_t arg_next = NL_NEXT(arg);
	                  if (!arg->attr) check(c2m_ctx, arg, r);
	                  e2 = arg->attr;
	                  if (param == NULL) {
	                    if (!func_type->dots_p) {
	                      error(c2m_ctx, POS(arg), "too many arguments in method call");
	                    }
	                    arg = arg_next;
	                    continue;
	                  }
	                  struct decl_spec *decl_spec = get_param_decl_spec(param);
	                  /* Implicitly erase a concrete C* argument into Any<I> when the
	                     parameter is an erased handle, so `list->Add(new Button(...))`
	                     works without an explicit any<View>(...). */
	                  {
	                    node_t coerced = try_coerce_to_any(c2m_ctx, arg_list, arg, decl_spec->type);
	                    if (coerced != NULL) { arg = coerced; e2 = arg->attr; }
	                  }
	                  check_assignment_types(c2m_ctx, decl_spec->type, NULL, e2, r);
	                  param = NL_NEXT(param);
	                  arg = arg_next;
	                }
	                if (param != NULL) {
	                  error(c2m_ctx, POS(r), "too few arguments in method call");
	                }
	                method_call_p = TRUE;
	              } else if (builtin_string_type_p(obj_type) || str_literal_recv_p) {
	                // Built-in String method call: s.length(), s.substr(p,n), s.find(x),
	                // s.replace(p,n,x), s.empty().  Lowered to UTF-8 runtime calls in gen.
	                node_t method_id = NL_NEXT(obj);
	                int nargs = 0;
	                enum str_method sm = get_string_method(method_id->u.s.s, &nargs, NULL);
	                if (sm == SM_NONE) {
	                  error(c2m_ctx, POS(r), "unknown String method '%s'", method_id->u.s.s);
	                  break;
	                }
	                /* `replace` is overloaded by arity: replace(pos,len,repl) is the
	                   positional form, while replace(needle, repl) is a search-and-
	                   replace.  Disambiguate on the actual argument count. */
	                if (sm == SM_REPLACE && NL_LENGTH(arg_list->u.ops) == 2) {
	                  sm = SM_REPLACE_ALL;
	                  nargs = 2;
	                }
	                if (NL_LENGTH(arg_list->u.ops) != nargs) {
	                  error(c2m_ctx, POS(r), "String method '%s' expects %d argument%s",
	                        method_id->u.s.s, nargs, nargs == 1 ? "" : "s");
	                }
	                for (arg = NL_HEAD(arg_list->u.ops); arg != NULL; arg = NL_NEXT(arg))
	                  if (!arg->attr) check(c2m_ctx, arg, r);
	                init_type(&res_type);
	                res_type.mode = TM_BASIC;
	                res_type.u.basic_type = TP_INT; /* safe fallback: overridden by each case below */
	                switch (sm) {
	                case SM_LENGTH:
	                case SM_FIND:
	                  res_type.u.basic_type = get_uint_basic_type(sizeof(mir_size_t)); /* size_t */
	                  set_type_layout(c2m_ctx, &res_type);
	                  break;
	                case SM_EMPTY:
	                  res_type.u.basic_type = TP_INT;
	                  set_type_layout(c2m_ctx, &res_type);
	                  break;
	                case SM_UPPER:
	                case SM_LOWER:
	                case SM_SUBSTR:
	                case SM_REPLACE:
	                case SM_REPLACE_ALL:
	                  /* String result is a char*; size/align set as in the N_STRING
	                     path (set_type_layout/basic_type_size reject TP_STRING). */
	                  res_type.u.basic_type = TP_STRING;
	                  res_type.type_qual.const_p = 1;
	                  res_type.raw_size = 8;
	                  res_type.align = 8;
	                  break;
	                case SM_DETACH: {
	                  /* Returns untracked char* — caller owns, must free() it. */
	                  struct type *pt = create_type(c2m_ctx, NULL);
	                  pt->mode = TM_BASIC;
	                  pt->u.basic_type = TP_CHAR;
	                  set_type_layout(c2m_ctx, pt);
	                  init_type(&res_type);
	                  res_type.mode = TM_PTR;
	                  res_type.u.ptr_type = pt;
	                  res_type.raw_size = 8;
	                  res_type.align = 8;
	                  break;
	                }
	                case SM_STARTS_WITH:
	                case SM_ENDS_WITH:
	                case SM_CONTAINS:
	                  /* Returns int (0/1 boolean) */
	                  res_type.u.basic_type = TP_INT;
	                  set_type_layout(c2m_ctx, &res_type);
	                  break;
	                case SM_TRIM:
	                  /* Returns a fresh String (tracked allocation) */
	                  res_type.u.basic_type = TP_STRING;
	                  res_type.type_qual.const_p = 1;
	                  res_type.raw_size = 8;
	                  res_type.align = 8;
	                  break;
	                case SM_EQUALS:

	                  res_type.u.basic_type = TP_INT;
	                  set_type_layout(c2m_ctx, &res_type);
	                  break;
	                case SM_SPLIT: {
	                  /* s.split(delim) -> List<String>*: instantiate List<String>
	                     (so parts->Count()/delete work) and use it as the result. */
	                  struct type str_el;
	                  struct type *lp;
	                  init_type(&str_el);
	                  str_el.mode = TM_BASIC;
	                  str_el.u.basic_type = TP_STRING;
	                  str_el.type_qual.const_p = 1;
	                  str_el.raw_size = 8;
	                  str_el.align = 8;
	                  if ((lp = make_list_ptr_type(c2m_ctx, &str_el, POS(r))) == NULL) break;
	                  res_type = *lp;
	                  break;
	                }
	                case SM_JOIN:
	                  /* join is a List<String> method, not a String method. */
	                  error(c2m_ctx, POS(r),
	                        "'join' is a List<String> method; call it on a List<String>");
	                  break;
	                case SM_EXT: {
	                  /* Header-registered method: return type from the registry. */
	                  const builtin_method_t *bm
	                    = find_builtin_method ("String", method_id->u.s.s,
	                                           (int) NL_LENGTH (arg_list->u.ops));
	                  if (bm == NULL)
	                    bm = find_builtin_method ("String", method_id->u.s.s, -1);
	                  if (bm == NULL) { error (c2m_ctx, POS (r), "unknown String method"); break; }
	                  switch (bm->ret) {
	                  case BMR_SIZE:
	                    res_type.u.basic_type = get_uint_basic_type (sizeof (mir_size_t));
	                    set_type_layout (c2m_ctx, &res_type);
	                    break;
	                  case BMR_INT:
	                    res_type.u.basic_type = TP_INT;
	                    set_type_layout (c2m_ctx, &res_type);
	                    break;
	                  case BMR_STRING:
	                    res_type.u.basic_type = TP_STRING;
	                    res_type.type_qual.const_p = 1;
	                    res_type.raw_size = 8;
	                    res_type.align = 8;
	                    break;
	                  case BMR_CHAR_PTR: {
	                    struct type *pt = create_type (c2m_ctx, NULL);
	                    pt->mode = TM_BASIC;
	                    pt->u.basic_type = TP_CHAR;
	                    set_type_layout (c2m_ctx, pt);
	                    init_type (&res_type);
	                    res_type.mode = TM_PTR;
	                    res_type.u.ptr_type = pt;
	                    res_type.raw_size = 8;
	                    res_type.align = 8;
	                    break;
	                  }
	                  case BMR_LIST_STRING: {
	                    struct type str_el;
	                    struct type *lp;
	                    init_type (&str_el);
	                    str_el.mode = TM_BASIC;
	                    str_el.u.basic_type = TP_STRING;
	                    str_el.type_qual.const_p = 1;
	                    str_el.raw_size = 8;
	                    str_el.align = 8;
	                    if ((lp = make_list_ptr_type (c2m_ctx, &str_el, POS (r))) == NULL) break;
	                    res_type = *lp;
	                    break;
	                  }
	                  default: break;
	                  }
	                  break;
	                }
	                default: break;
	                }
	                ret_type = &res_type;
	                method_call_p = TRUE;
	              } else if (obj_type->mode == TM_DICT) {
	                /* Built-in dict methods (not properties — keys of the same
	                   name stay readable via bare field access):
	                     d.length() / d.count() → size
	                     d.type()               → DictType tag
	                     d.json()               → JSON String (serialize) */
	                node_t method_id = NL_NEXT(obj);
	                const char *mname = (method_id && method_id->code == N_ID)
	                                      ? method_id->u.s.s : "?";
	                if (strcmp(mname, "length") != 0 && strcmp(mname, "count") != 0
	                    && strcmp(mname, "type") != 0
	                    && strcmp(mname, "json") != 0) {
	                  error(c2m_ctx, POS(r),
	                        "unknown dict method '%s' (only 'length' / 'count' / 'type' / 'json' are built in)",
	                        mname);
	                  break;
	                }
	                if (NL_LENGTH(arg_list->u.ops) != 0)
	                  error(c2m_ctx, POS(r),
	                        "dict method '%s' takes no arguments", mname);
	                init_type(&res_type);
	                res_type.mode = TM_BASIC;
	                if (strcmp(mname, "type") == 0)
	                  res_type.u.basic_type = TP_INT;  /* DictType tag */
	                else if (strcmp(mname, "json") == 0) {
	                  /* String result; TP_STRING size not in basic_type_size. */
	                  res_type.u.basic_type = TP_STRING;
	                  res_type.type_qual.const_p = 1;
	                  res_type.raw_size = 8;
	                  res_type.align = 8;
	                } else
	                  res_type.u.basic_type = get_uint_basic_type(sizeof(mir_size_t)); /* size_t */
	                if (strcmp(mname, "json") != 0)
	                  set_type_layout(c2m_ctx, &res_type);
	                ret_type = &res_type;
	                method_call_p = TRUE;
	              } else {
	                // Not a class, treat as regular function call (e.g., struct delegate)
	                if (t1->mode != TM_PTR || (t1 = t1->u.ptr_type)->mode != TM_FUNC) {
	                  error(c2m_ctx, POS(r), "called object is not a function or function pointer");
	                  break;
	                }
	                func_type = t1->u.func_type;
	                ret_type = func_type->ret_type;
	              }
	            }
	          } else {
	            // Regular function call
	            if (t1->mode != TM_PTR || (t1 = t1->u.ptr_type)->mode != TM_FUNC) {
	              error(c2m_ctx, POS(r), "called object is not a function or function pointer");
	              break;
	            }
	            func_type = t1->u.func_type;
	            ret_type = func_type->ret_type;
	          }
        }

      seq_method_done:
        e = create_expr(c2m_ctx, r);
        *e->type = *ret_type;
        e->builtin_call_p = builtin_call_p;
        if ((ret_type->mode != TM_BASIC || ret_type->u.basic_type != TP_VOID)
            && incomplete_type_p(c2m_ctx, ret_type)) {
          error(c2m_ctx, POS(r), "function return type is incomplete");
        }
        if (ret_type->mode == TM_STRUCT || ret_type->mode == TM_UNION
            || ret_type->mode == TM_CLASS) {
          set_type_layout(c2m_ctx, ret_type);
          if (!builtin_call_p && curr_scope != top_scope)
            update_call_arg_area_offset(c2m_ctx, ret_type, TRUE);
        }
        if (builtin_call_p && !jcall_p) break;
        if (method_call_p) break; /* args already checked in method call path above */
        first_arg = jcall_p ? NL_EL(arg_list->u.ops, 1) : NL_HEAD(arg_list->u.ops);
        param_list = func_type->param_list;
        param = start_param = NL_HEAD(param_list->u.ops);
        if (void_param_p(start_param)) { /* f(void) */
          if (first_arg != NULL) error(c2m_ctx, POS(first_arg), "too many arguments in call");
          break;
        }
        saved_call_arg_area_offset_before_args = curr_call_arg_area_offset;
        for (arg = first_arg; arg != NULL;) {
          node_t arg_next = NL_NEXT(arg);
          check(c2m_ctx, arg, r);
          e2 = arg->attr;
          if (start_param == NULL || start_param->code == N_ID) { arg = arg_next; continue; } /* no params or ident list */
          if (param == NULL) {
            if (!func_type->dots_p) warning(c2m_ctx, POS(arg), "too many arguments (extra ignored)");
            start_param = NULL; /* ignore the rest args */
            arg = arg_next;
            continue;
          }
          assert(param->code == N_SPEC_DECL || param->code == N_TYPE);
          decl_spec = get_param_decl_spec(param);
          /* Implicitly erase a concrete C* argument into Any<I> when the
             parameter is an erased handle. */
          {
            node_t coerced = try_coerce_to_any(c2m_ctx, arg_list, arg, decl_spec->type);
            if (coerced != NULL) { arg = coerced; e2 = arg->attr; }
          }
          check_assignment_types(c2m_ctx, decl_spec->type, NULL, e2, r);
          param = NL_NEXT(param);
          arg = arg_next;
        }
        curr_call_arg_area_offset = saved_call_arg_area_offset_before_args;
        if (param != NULL) error(c2m_ctx, POS(r), "too few arguments");
        /* ---- printf-family format-string type checking ---- */
        if (func_type->dots_p && op1->code == N_ID) {
          const char *fn = op1->u.s.s;
          int fmt_pos = -1;
          if (strcmp (fn, "printf") == 0 || strcmp (fn, "vprintf") == 0)
            fmt_pos = 0;
          else if (strcmp (fn, "fprintf") == 0 || strcmp (fn, "sprintf") == 0
                   || strcmp (fn, "snprintf") == 0 || strcmp (fn, "dprintf") == 0
                   || strcmp (fn, "vfprintf") == 0 || strcmp (fn, "vsprintf") == 0
                   || strcmp (fn, "vsnprintf") == 0)
            fmt_pos = 1;
          if (fmt_pos >= 0)
            check_printf_format (c2m_ctx, arg_list, fmt_pos);
        }
        /* C11: fold strlen/strcmp/memcmp of string literals so
           `if (strlen("") == 0)` is a const if (midopt drops the dead arm;
           gen's push_const_val skips the call).  Same for GNU
           __builtin_strlen / __builtin_constant_p. */
        if (op1->code == N_ID && op1->u.s.s != NULL && first_arg != NULL) {
          const char *fn = op1->u.s.s;
          node_t s0, s1, a1, a2;
          struct expr *ne;
          int slen_p = (strcmp (fn, "strlen") == 0 || strcmp (fn, "__builtin_strlen") == 0);
          int scmp_p = (strcmp (fn, "strcmp") == 0 || strcmp (fn, "__builtin_strcmp") == 0);
          int ncmp_p = (strcmp (fn, "strncmp") == 0 || strcmp (fn, "__builtin_strncmp") == 0);
          int mcmp_p = (strcmp (fn, "memcmp") == 0 || strcmp (fn, "__builtin_memcmp") == 0);
          int bcp_p = (strcmp (fn, "__builtin_constant_p") == 0);

          if (slen_p) {
            s0 = check_string_lit_node (first_arg);
            if (s0 != NULL) {
              e->const_p = TRUE;
              e->c.u_val = (mir_ullong) strlen (s0->u.s.s);
            }
          } else if (scmp_p) {
            a1 = NL_NEXT (first_arg);
            s0 = check_string_lit_node (first_arg);
            s1 = a1 != NULL ? check_string_lit_node (a1) : NULL;
            if (s0 != NULL && s1 != NULL) {
              e->const_p = TRUE;
              e->c.i_val = strcmp (s0->u.s.s, s1->u.s.s);
            }
          } else if (ncmp_p || mcmp_p) {
            a1 = NL_NEXT (first_arg);
            a2 = a1 != NULL ? NL_NEXT (a1) : NULL;
            s0 = check_string_lit_node (first_arg);
            s1 = a1 != NULL ? check_string_lit_node (a1) : NULL;
            ne = a2 != NULL ? a2->attr : NULL;
            if (s0 != NULL && s1 != NULL && ne != NULL && ne->const_p && ne->type != NULL
                && integer_type_p (ne->type)) {
              mir_ullong n = signed_integer_type_p (ne->type)
                               ? (mir_ullong) (ne->c.i_val < 0 ? 0 : ne->c.i_val)
                               : ne->c.u_val;
              if (ncmp_p) {
                e->const_p = TRUE;
                e->c.i_val = strncmp (s0->u.s.s, s1->u.s.s, (size_t) n);
              } else if ((size_t) n <= s0->u.s.len && (size_t) n <= s1->u.s.len) {
                e->const_p = TRUE;
                e->c.i_val = memcmp (s0->u.s.s, s1->u.s.s, (size_t) n);
              }
            }
          } else if (bcp_p) {
            ne = first_arg->attr;
            e->const_p = TRUE;
            e->c.i_val = (ne != NULL && ne->const_p) ? 1 : 0;
          }
        }
        break;
      }
      case N_GENERIC: {
        node_t list, ga, ga2, type_name, type_name2, expr;
        node_t default_case = NULL, ga_case = NULL;
        struct decl_spec *decl_spec;

        op1 = NL_HEAD (r->u.ops);
        check (c2m_ctx, op1, r);
        e1 = op1->attr;
        t = *e1->type;
        if (integer_type_p (&t)) t = integer_promotion (&t);
        list = NL_NEXT (op1);
        for (ga = NL_HEAD (list->u.ops); ga != NULL; ga = NL_NEXT (ga)) {
          assert (ga->code == N_GENERIC_ASSOC);
          type_name = NL_HEAD (ga->u.ops);
          expr = NL_NEXT (type_name);
          check (c2m_ctx, type_name, r);
          check (c2m_ctx, expr, r);
          if (type_name->code == N_IGNORE) {
            if (default_case) error (c2m_ctx, POS (ga), "duplicate default case in _Generic");
            default_case = ga;
            continue;
          }
          assert (type_name->code == N_TYPE);
          decl_spec = type_name->attr;
          if (incomplete_type_p (c2m_ctx, decl_spec->type)) {
            error (c2m_ctx, POS (ga), "_Generic case has incomplete type");
          } else if (compatible_types_p (&t, decl_spec->type, TRUE)) {
            if (ga_case)
              error (c2m_ctx, POS (ga_case),
                     "_Generic expr type is compatible with more than one generic association type");
            ga_case = ga;
          } else {
            for (ga2 = NL_HEAD (list->u.ops); ga2 != ga; ga2 = NL_NEXT (ga2)) {
              type_name2 = NL_HEAD (ga2->u.ops);
              if (type_name2->code != N_IGNORE
                  && !(incomplete_type_p (c2m_ctx, t2 = ((struct decl_spec *) type_name2->attr)->type))
                  && compatible_types_p (t2, decl_spec->type, TRUE)) {
                error (c2m_ctx, POS (ga), "two or more compatible generic association types");
                break;
              }
            }
          }
        }
        e = create_expr (c2m_ctx, r);
        if (default_case == NULL && ga_case == NULL) {
          error (c2m_ctx, POS (r), "expression type is not compatible with generic association");
        } else { /* make compatible association a list head  */
          if (ga_case == NULL) ga_case = default_case;
          NL_REMOVE (list->u.ops, ga_case);
          NL_PREPEND (list->u.ops, ga_case);
          t2 = e->type;
          *e = *(struct expr *) NL_EL (ga_case->u.ops, 1)->attr;
          *t2 = *e->type;
          e->type = t2;
        }
        break;
      }
  case N_SPEC_DECL: {
    node_t specs = SPEC_DECL_SPECS (r);
    node_t declarator = SPEC_DECL_DECL (r);
    node_t attrs = SPEC_DECL_ATTRS (r);
    node_t asm_part = SPEC_DECL_ASM (r);
    node_t initializer = SPEC_DECL_INIT (r);
    node_t unshared_specs = UNSHARE (specs);
    struct decl_spec decl_spec = check_decl_spec (c2m_ctx, unshared_specs, r);
    int i;
    const char *asm_str = NULL;

    if (asm_part->code == N_ASM && decl_spec.register_p) {
      /* func can have asm which ignore here */
      if (initializer->code != N_IGNORE) {
        error (c2m_ctx, POS (r), "asm register decl with initializer");
      } else if (curr_scope != top_scope) {
        error (c2m_ctx, POS (r), "asm register decl should be at the top level");
      } else {
        asm_str = NL_HEAD (asm_part->u.ops)->u.s.s;
        for (i = 0; asm_str[i] != '\0' && _MIR_name_char_p (c2m_ctx->ctx, asm_str[i], i == 0); i++)
          ;
        if (asm_str[i] != '\0') {
          error (c2m_ctx, POS (r), "asm register name %s contains wrong char '%c'", asm_str,
                 asm_str[i]);
          asm_str = NULL;
        }
      }
    }
    /* auto type inference:  `auto x = init;` with no explicit type specifier.
           The declared type is taken from the initializer:
             - a scalar initializer yields its (decayed) expression type;
             - ClassName(args) / List<T>()  -> stack class value (RAII ctor/dtor);
             - a brace initializer is disambiguated syntactically:
                 * keyless list  `auto a = {1, 2, 3};`           -> array (int[3])
                 * keyed   list  `auto d = {"k": v, ...};`        -> dict
               (the keyed/keyless distinction leaves room for future generic
                container forms such as  `List<int> x = {1, 2, 3};`.) */
    if (decl_spec.auto_p && !specs_have_type_spec_p (specs)
        && declarator->code != N_IGNORE && initializer != NULL
        && initializer->code != N_IGNORE) {
      if (initializer->code == N_LIST) {
        /* Inspect the first element's designator list: a present N_FIELD_ID
           designator ("key": ...) means this is a dict literal; otherwise it
           is a positional (keyless) list, deduced as a homogeneous array. */
        node_t first = NL_HEAD (initializer->u.ops);
        node_t first_des = NULL, first_val = NULL;
        if (first != NULL && first->code == N_INIT) {
          node_t dl = NL_HEAD (first->u.ops);
          if (dl != NULL && dl->code == N_LIST) first_des = NL_HEAD (dl->u.ops);
          first_val = NL_NEXT (dl);
        }
        if (first != NULL && first_des == NULL && first_val != NULL) {
          /* keyless braced list -> array; element type from the first element */
          check (c2m_ctx, first_val, r);
          struct expr *ve = first_val->attr;
          if (ve != NULL && ve->type != NULL && ve->type->mode != TM_UNDEF) {
            struct type *el = create_type (c2m_ctx, ve->type);
            if (el->mode == TM_ARR) el = adjust_type (c2m_ctx, el); /* decay */
            struct type *at = create_type (c2m_ctx, NULL);
            at->mode = TM_ARR;
            at->pos_node = r;
            at->u.arr_type = reg_malloc (c2m_ctx, sizeof (struct arr_type));
            at->u.arr_type->el_type = el;
            at->u.arr_type->static_p = FALSE;
            at->u.arr_type->flex_p = 0;
            at->u.arr_type->flex_bound_member = NULL;
            clear_type_qual (&at->u.arr_type->ind_type_qual);
            /* unspecified size: check_initializer counts the elements and
               completes the type (exactly like `int a[] = {...};`). */
            at->u.arr_type->size = new_ignore (c2m_ctx);
            set_type_layout (c2m_ctx, at);
            decl_spec.type = at;
          }
        } else {
          /* keyed (or empty) braced list -> dict literal */
          struct type *it = create_type (c2m_ctx, NULL);
          it->mode = TM_DICT;
          it->pos_node = r;
          set_type_layout (c2m_ctx, it);
          decl_spec.type = it;
        }
      } else {
            /* Class-value construction as initializer:
                 auto p = Point(1, 2);
                 auto xs = List<int>();
                 auto m = Map<String,int>(16);
               The call is NOT a free-function call: the callee is a class type
               name (or mangled generic specialization `__generic_List_int`).
               Infer the class type here and leave the initializer as N_CALL so
               create_decl's RAII path lowers it to an in-place `var.__ctor_T(args)`
               and registers `~T` at scope exit — the same path as
               `List<int> xs = List<int>();` (typed form already works).

               Checking the call as a normal expression would fail with
               "called object is not a function", so resolve the class tag
               without type-checking the N_CALL itself. */
            node_t ctor_callee
              = (initializer->code == N_CALL) ? NL_HEAD (initializer->u.ops) : NULL;
            node_t class_def = NULL;
            if (ctor_callee != NULL && ctor_callee->code == N_ID) {
              class_def = find_def (c2m_ctx, S_REGULARS, ctor_callee,
                                    skip_struct_scopes (curr_scope), NULL);
              if (class_def == NULL)
                class_def = find_def (c2m_ctx, S_TAG, ctor_callee,
                                      skip_struct_scopes (curr_scope), NULL);
            }
            if (class_def != NULL && class_def->code == N_CLASS) {
              struct type *it = create_type (c2m_ctx, NULL);
              it->mode = TM_CLASS;
              it->u.tag_type = class_def;
              it->pos_node = r;
              set_class_layout (c2m_ctx, class_def, it);
              set_type_layout (c2m_ctx, it);
              decl_spec.type = it;
              /* initializer stays N_CALL → create_decl ctor_init_p path */
            } else {
              check (c2m_ctx, initializer, r);
              struct expr *ie = initializer->attr;
              if (ie != NULL && ie->type != NULL && ie->type->mode != TM_UNDEF) {
                struct type *it = create_type (c2m_ctx, ie->type);
                it->pos_node = r;
                if (it->mode == TM_ARR) it = adjust_type (c2m_ctx, it); /* decay */
                /* If declarator starts with a pointer (e.g., auto *p = expr;),
                   strip one pointer level from the inferred type to match
                   C++ auto semantics: auto *p = expr; where expr is T* -> p is T* */
                node_t decl_list = NL_EL (declarator->u.ops, 1);
                node_t first_decl = decl_list != NULL ? NL_HEAD (decl_list->u.ops) : NULL;
                if (first_decl != NULL && first_decl->code == N_POINTER
                    && it->mode == TM_PTR) {
                  it = it->u.ptr_type;  /* strip one pointer level */
                }
                set_type_layout (c2m_ctx, it);
                decl_spec.type = it;
              }
            }
          }
  }
    /* classyc extension: minimal VLA support.
       Lower a local declaration `T name[expr];` whose size expression is
       a non-const integer into `T *name = alloca(sizeof(*name) * (expr));`.
       This covers the common idiom (indexing, address-of, passing as a
       pointer) without disturbing stack-frame layout or sizeof codegen.
       Limitations vs. C99 VLAs:
         * `sizeof(name)` returns `sizeof(void *)`, not `n * sizeof(T)`.
         * Lifetime is the whole function (alloca), not the enclosing block.
       Only the head of the declarator chain is rewritten, so any inner
       declarators (e.g. `T name[n][C]` -> `T (*name)[C]`) compose correctly.
       Skipped when the user already supplied an initializer, or when the
       declaration is at file scope, static, extern, typedef, thread-local,
       or register. */
    if (declarator->code == N_DECL && initializer->code == N_IGNORE
        && curr_scope != top_scope
        && !decl_spec.typedef_p && !decl_spec.static_p
        && !decl_spec.extern_p && !decl_spec.thread_local_p
        && !decl_spec.register_p) {
      node_t dlist = NL_EL (declarator->u.ops, 1);
      node_t head = dlist != NULL ? NL_HEAD (dlist->u.ops) : NULL;
      if (head != NULL && head->code == N_ARR) {
        node_t s_static = NL_HEAD (head->u.ops);
        node_t s_qual = NL_NEXT (s_static);
        node_t size = NL_NEXT (s_qual);
        if (size != NULL && size->code != N_IGNORE && size->code != N_STAR) {
          unsigned saved_errs = n_errors;
          check (c2m_ctx, size, head);
          struct expr *se = size->attr;
          if (n_errors == saved_errs && se != NULL
              && integer_type_p (se->type) && !se->const_p) {
            node_t id = NL_HEAD (declarator->u.ops);
            pos_t pos = POS (head);
            /* Detach size from N_ARR so we can splice it into the alloca call. */
            NL_REMOVE (head->u.ops, size);
            /* sizeof(*name) -- not evaluated, so referencing `name` here is safe. */
            node_t deref = new_pos_node1 (c2m_ctx, N_DEREF, pos,
                                          copy_node_with_pos (c2m_ctx, id, pos));
            node_t sz = new_pos_node1 (c2m_ctx, N_EXPR_SIZEOF, pos, deref);
            node_t mul = new_pos_node2 (c2m_ctx, N_MUL, pos, sz, size);
            node_t arglist = new_pos_node (c2m_ctx, N_LIST, pos);
            NL_APPEND (arglist->u.ops, mul);
            node_t call = new_pos_node2 (c2m_ctx, N_CALL, pos,
                                         build_id (c2m_ctx, "alloca", pos),
                                         arglist);
            /* Replace head N_ARR with N_POINTER (no type qualifiers). */
            node_t ptr = new_pos_node1 (c2m_ctx, N_POINTER, pos,
                                        new_pos_node (c2m_ctx, N_LIST, pos));
            NL_REMOVE (dlist->u.ops, head);
            NL_PREPEND (dlist->u.ops, ptr);
            /* Install the alloca call as the SPEC_DECL_INIT slot (last op). */
            node_t old_init = SPEC_DECL_INIT (r);
            NL_REMOVE (r->u.ops, old_init);
            NL_APPEND (r->u.ops, call);
            initializer = call;
          }
        }
      }
    }
    if (declarator->code != N_IGNORE) {
      create_decl (c2m_ctx, curr_scope, r, decl_spec, initializer,
                   context != NULL && context->code == N_FUNC);
      decl_t decl = r->attr;
      // Refresh the (laid-out) class type for a by-value class variable, but
      // only when no pointer/array/function declarator was applied -- otherwise
      // this would discard the pointer from `ClassName *p` (needed by `new`).
      if (decl_spec.type->mode == TM_CLASS && decl->decl_spec.type->mode == TM_CLASS)
        decl->decl_spec.type = decl_spec.type;

      const char *antialias = check_attrs (c2m_ctx, r, decl, attrs, TRUE);
      if (decl->decl_spec.type->mode == TM_PTR && antialias != NULL)
        decl->decl_spec.type->antialias = MIR_alias (c2m_ctx->ctx, antialias);
      if (asm_str != NULL) {
        if (!scalar_type_p (decl->decl_spec.type)) {
          error (c2m_ctx, POS (r), "asm register decl should have a scalar type");
        } else {
          decl->reg_p = decl->asm_p = TRUE;
          decl->u.asm_str = asm_str;
        }
      } else if ((initializer == NULL || initializer->code == N_IGNORE)
                 && !decl->decl_spec.typedef_p && !decl->decl_spec.extern_p
                 && (decl->decl_spec.type->mode == TM_STRUCT
                     || decl->decl_spec.type->mode == TM_UNION)) {
        VARR_PUSH (node_t, possible_incomplete_decls, r);
      }

      /* --- Auto-defer candidate detection ---------------------------------
         Mark a local declaration as a candidate for automatic scope-bound
         cleanup when its initializer matches a resource-acquire pattern and
         the user has not opted out via `unowned`.  This sets only a flag on
         the decl; the actual `defer delete` synthesis is deferred to the
         upcoming ownership pass (see `src/ownership.c`).

         Eligibility (intentionally conservative; the ownership pass will
         refine each criterion as it gains flow information):
           - not under `unowned <decl>`
           - local scope (not top-level / global)
           - not typedef / extern / static / thread-local
           - simple identifier declarator (N_DECL with an N_ID)
           - initializer matches `is_resource_acquire` (today: `N_NEW`)
           - resulting type is a deletable category (TM_PTR or TM_DICT) */
      /* Persist the `unowned` opt-out on the decl so the ownership pass can
         skip this binding wholesale (leak checks AND -fauto-release), not
         just the `new`-acquire auto_defer flag below. */
      if (in_unowned_p) decl->unowned_p = TRUE;
      if (!in_unowned_p
          && initializer != NULL && initializer->code != N_IGNORE
          && declarator->code == N_DECL
          && decl->scope != top_scope
          && !decl->decl_spec.typedef_p && !decl->decl_spec.extern_p
          && !decl->decl_spec.static_p && !decl->decl_spec.thread_local_p
          && (decl->decl_spec.type->mode == TM_PTR
              || decl->decl_spec.type->mode == TM_DICT)
          && is_resource_acquire (initializer)) {
        node_t spec_id = NL_HEAD (declarator->u.ops);
        if (spec_id != NULL && spec_id->code == N_ID)
          decl->auto_defer_p = TRUE;
      }
      /* --- Managed-ownership marking (owned / move) --------------------------
         A local pointer binding joins the managed layer when the user wrote
         `owned` (in_owned_p) OR when it is initialized by `move <expr>` (which
         transfers ownership of an existing owned value into this binding).
         `unowned` always wins as an explicit opt-out. */
      if (!decl->unowned_p
          && declarator->code == N_DECL
          && decl->scope != top_scope
          && !decl->decl_spec.typedef_p && !decl->decl_spec.extern_p
          && !decl->decl_spec.static_p && !decl->decl_spec.thread_local_p
          && decl->decl_spec.type->mode == TM_PTR) {
        node_t mv_init = initializer;
        while (mv_init != NULL && mv_init->code == N_CAST)
          mv_init = NL_EL (mv_init->u.ops, 1);
        int init_is_move = (mv_init != NULL && mv_init->code == N_MOVE);
        if (in_owned_p || init_is_move) {
          node_t spec_id = NL_HEAD (declarator->u.ops);
          if (spec_id != NULL && spec_id->code == N_ID)
            decl->owned_p = TRUE;
        }
      }
    } else if (decl_spec.type->mode == TM_STRUCT || decl_spec.type->mode == TM_UNION ||
           decl_spec.type->mode == TM_CLASS) {
      if (NL_HEAD (decl_spec.type->u.tag_type->u.ops)->code != N_ID)
        error (c2m_ctx, POS (r), "unnamed struct/union with no instances");
    } else if (decl_spec.type->mode != TM_ENUM) {
      error (c2m_ctx, POS (r), "useless declaration");
    }
    /* We have at least one enum constant according to the syntax. */
    break;
  }
  case N_ST_ASSERT: {
    int ok_p;

    op1 = NL_HEAD (r->u.ops);
    check (c2m_ctx, op1, r);
    e1 = op1->attr;
    t1 = e1->type;
    if (!e1->const_p) {
      error (c2m_ctx, POS (r), "expression in static assertion is not constant");
    } else if (!integer_type_p (t1)) {
      error (c2m_ctx, POS (r), "expression in static assertion is not an integer");
    } else {
      if (signed_integer_type_p (t1))
        ok_p = e1->c.i_val != 0;
      else
        ok_p = e1->c.u_val != 0;
      if (!ok_p) {
        assert (NL_NEXT (op1) != NULL
                && (NL_NEXT (op1)->code == N_STR || NL_NEXT (op1)->code == N_STR16
                    || NL_NEXT (op1)->code == N_STR32));
        error (c2m_ctx, POS (r), "static assertion failed: \"%s\"",
               NL_NEXT (op1)->u.s.s);  // ???
      }
    }
    break;
  }
  case N_MEMBER: {
    struct type *type;
    node_t specs = MEMBER_SPECS (r);
    node_t declarator = MEMBER_DECL (r);
    node_t attrs = MEMBER_ATTRS (r);
    node_t const_expr = MEMBER_WIDTH (r);
    node_t init = MEMBER_INIT (r);
    node_t unshared_specs = UNSHARE (specs);
    struct decl_spec decl_spec = check_decl_spec (c2m_ctx, unshared_specs, r);

    create_decl (c2m_ctx, curr_scope, r, decl_spec, NULL, FALSE);
    type = ((decl_t) r->attr)->decl_spec.type;
    if (const_expr->code != N_IGNORE) {
      struct expr *cexpr;
      check (c2m_ctx, const_expr, r);
      cexpr = const_expr->attr;
      if (cexpr != NULL) {
        if (type->type_qual.atomic_p) error (c2m_ctx, POS (const_expr), "bit field with _Atomic");
        if (!cexpr->const_p) {
          error (c2m_ctx, POS (const_expr), "bit field is not a constant expr");
        } else if (!integer_type_p (type)
                   && (type->mode != TM_BASIC || type->u.basic_type != TP_BOOL)) {
          error (c2m_ctx, POS (const_expr),
                 "bit field type should be _Bool, a signed integer, or an unsigned integer type");
        } else if (!integer_type_p (cexpr->type)
                   && (cexpr->type->mode != TM_BASIC || cexpr->type->u.basic_type != TP_BOOL)) {
          error (c2m_ctx, POS (const_expr), "bit field width is not of an integer type");
        } else if (signed_integer_type_p (cexpr->type) && cexpr->c.i_val < 0) {
          error (c2m_ctx, POS (const_expr), "bit field width is negative");
        } else if (cexpr->c.i_val == 0 && declarator->code == N_DECL) {
          error (c2m_ctx, POS (const_expr), "zero bit field width for %s",
                 NL_HEAD (declarator->u.ops)->u.s.s);
        } else if ((!signed_integer_type_p (cexpr->type)
                    && cexpr->c.u_val > (mir_ullong) int_bit_size (type))
                   || (signed_integer_type_p (cexpr->type)
                       && cexpr->c.i_val > int_bit_size (type))) {
          error (c2m_ctx, POS (const_expr), "bit field width exceeds its type");
        }
      }
    }
    if (init && init ->code != N_IGNORE) {
#ifdef C2MIR_PREPRO_DEBUG
        fprintf(stderr, "N_MEMBER: got init\n");
#endif
        check (c2m_ctx, init, r);
        check_initializer (c2m_ctx, NULL, &type, init,
                           curr_scope == top_scope || decl_spec.static_p || decl_spec.thread_local_p,
                           TRUE);
    }
    if (declarator->code == N_IGNORE) {
      if (((decl_spec.type->mode != TM_STRUCT && decl_spec.type->mode != TM_UNION)
           || NL_HEAD (decl_spec.type->u.tag_type->u.ops)->code != N_IGNORE)
          && const_expr->code == N_IGNORE)
        error (c2m_ctx, POS (r), "no declarator in struct or union declaration");
    } else {
      node_t id = NL_HEAD (declarator->u.ops);
      const char *antialias = check_attrs (c2m_ctx, r, r->attr, attrs, TRUE);
      if (type->mode == TM_PTR && antialias != NULL)
        type->antialias = MIR_alias (c2m_ctx->ctx, antialias);
      if (type->mode == TM_FUNC) {
        error (c2m_ctx, POS (id), "field %s is declared as a function", id->u.s.s);
      } else if (incomplete_type_p (c2m_ctx, type)) {
        /* el_type is checked on completness in N_ARR */
        if (type->mode != TM_ARR || type->u.arr_type->size->code != N_IGNORE)
          error (c2m_ctx, POS (id), "field %s has incomplete type", id->u.s.s);
      }
    }
    break;
  }
  case N_INIT: {
    node_t des_list = NL_HEAD (r->u.ops), initializer = NL_NEXT (des_list);

    check (c2m_ctx, des_list, r);
    check (c2m_ctx, initializer, r);
    break;
  }
  case N_FUNC_DEF: {
    /* Skip generic function templates (sentinel attr): only their
       monomorphized specializations are real functions, checked via
       check_lambda_func_def when instantiated at a call site. */
    if (r->attr == (void *)((intptr_t)-1)) break;
    node_t specs = FUNC_DEF_SPECS (r);
    node_t declarator = FUNC_DEF_DECL (r);
    node_t declarations = FUNC_DEF_DECLS (r);
    node_t block = FUNC_DEF_BLOCK (r);
    node_t id = DECL_ID (declarator);
    struct decl_spec decl_spec = check_decl_spec(c2m_ctx, specs, r);
    node_t decl_node, p, next_p, param_list, param_id, param_declarator, func;
    symbol_t sym;
    struct node_scope *ns;

    // Access check_ctx from c2m_ctx
    check_ctx_t check_ctx = c2m_ctx->check_ctx;

    // Check if the function is a builtin
    if (str_eq_p(id->u.s.s, ALLOCA) || str_eq_p(id->u.s.s, BUILTIN_VA_START)
        || str_eq_p(id->u.s.s, BUILTIN_VA_ARG) || strcmp(id->u.s.s, ADD_OVERFLOW) == 0
        || strcmp(id->u.s.s, SUB_OVERFLOW) == 0 || strcmp(id->u.s.s, MUL_OVERFLOW) == 0
        || strcmp(id->u.s.s, EXPECT) == 0 || strcmp(id->u.s.s, JCALL) == 0
        || strcmp(id->u.s.s, JRET) == 0 || strcmp (id->u.s.s, ATOMIC_LOAD_N) == 0
        || strcmp (id->u.s.s, ATOMIC_STORE_N) == 0 || strcmp (id->u.s.s, ATOMIC_EXCHANGE_N) == 0
        || strcmp (id->u.s.s, ATOMIC_FETCH_ADD) == 0 || strcmp (id->u.s.s, ATOMIC_FETCH_SUB) == 0
        || strcmp (id->u.s.s, ATOMIC_FETCH_AND) == 0 || strcmp (id->u.s.s, ATOMIC_FETCH_OR) == 0
        || strcmp (id->u.s.s, ATOMIC_FETCH_XOR) == 0
        || strcmp (id->u.s.s, ATOMIC_COMPARE_EXCHANGE_N) == 0
        || strcmp (id->u.s.s, ATOMIC_THREAD_FENCE) == 0) {
        error(c2m_ctx, POS(id), "%s is a builtin function", id->u.s.s);
        break;
    }

    // Initialize function scope and state
    node_t saved_scope_before_func = curr_scope; /* save for assertion below */
    curr_func_scope_num = 0;
    create_node_scope(c2m_ctx, block);
    func_block_scope = curr_scope;
    curr_func_def = r;
    jump_ret_p = FALSE;
    curr_func_has_try = FALSE;
    curr_switch = curr_loop = curr_loop_switch = NULL;
    curr_call_arg_area_offset = 0;
    VARR_TRUNC(decl_t, func_decls_for_allocation, 0);

    /* Determine insertion scope: use the class node itself as the scope key
       for any method (static or instance) so bare identifiers never resolve
       to them.  Lookups in N_FIELD already use t1->u.tag_type directly. */
    node_t decl_scope = top_scope;
    if (curr_class != NULL) {
      node_t class_tag = (curr_class->code == N_CLASS) ? curr_class : NULL;
      if (!class_tag) {
        node_t cid = NL_HEAD(curr_class->u.ops);
        if (cid && cid->code == N_ID) {
          class_tag = find_def(c2m_ctx, S_REGULARS, cid, curr_scope, NULL);
          if (!class_tag) class_tag = find_def(c2m_ctx, S_TAG, cid, curr_scope, NULL);
        }
      }
      if (class_tag && class_tag->code == N_CLASS) {
        decl_scope = class_tag;
      }
    }
    /* Append hidden length companions for any `T* p` parameter the body queries
       with `p.count()`, before create_decl bakes the param list into the
       function's type/signature.  Scoped to class methods/constructors: their
       call sites (`new`, method calls) auto-supply the companion via
       count-argument expansion, and arr.ToList() lowering passes it directly. */
    if (curr_class != NULL) expand_count_companion_params (c2m_ctx, r);
    create_decl(c2m_ctx, decl_scope, r, decl_spec, NULL, FALSE);

    /* Lambda return-type inference: lambdas are emitted with 'static auto' specs
       and no explicit type specifier.  After create_decl sets the return type to
       the default TP_INT, reset it to TP_UNDEF so that N_RETURN can infer the
       real return type from the first 'return <expr>;' in the body. */
    if (decl_spec.auto_p && !specs_have_type_spec_p (specs)) {
      decl_t fdecl = r->attr;
      if (fdecl != NULL && fdecl->decl_spec.type != NULL
          && fdecl->decl_spec.type->mode == TM_FUNC) {
        struct type *rt = fdecl->decl_spec.type->u.func_type->ret_type;
        if (rt != NULL && rt->mode == TM_BASIC && rt->u.basic_type == TP_INT)
          rt->u.basic_type = TP_UNDEF;
      }
    }

    /* Record the enclosing class on the method's function type so gen can mangle
       the method's symbol name (Class_method__<params>).  The method's
       declarator was checked in the function block scope, so check_declarator
       could not see N_CLASS; curr_class is the reliable signal here, and it
       applies to both instance and static methods. */
    if (curr_class != NULL) {
      node_t class_node = NULL;
      if (curr_class->code == N_CLASS) {
        class_node = curr_class;
      } else {
        node_t cid = NL_HEAD (curr_class->u.ops);
        if (cid != NULL && cid->code == N_ID) {
          class_node = find_def (c2m_ctx, S_REGULARS, cid, curr_scope, NULL);
          if (class_node == NULL) class_node = find_def (c2m_ctx, S_TAG, cid, curr_scope, NULL);
        }
      }
      if (class_node != NULL && class_node->code == N_CLASS) {
        decl_t fdecl = r->attr;
        if (fdecl != NULL && fdecl->decl_spec.type != NULL
            && fdecl->decl_spec.type->mode == TM_FUNC)
          fdecl->decl_spec.type->u.func_type->class_scope = class_node;
      }
    }

    check(c2m_ctx, declarations, r);
    assert(declarator->code == N_DECL);
    func = NL_HEAD(NL_EL(declarator->u.ops, 1)->u.ops);
    if (n_errors != 0 && (func == NULL || func->code != N_FUNC)) break;
    assert(func != NULL && func->code == N_FUNC);
    param_list = NL_HEAD(func->u.ops);

    /* ClassyC: untyped (K&R identifier-list) parameters are not allowed in class
       members.  `Point(x, y)` must be written `Point(int x, int y)`.  A bare
       identifier parameter is parsed as an N_ID node; later passes (the implicit
       'this' prepend below, named-argument reordering in `new`, and overload
       selection) all assume N_SPEC_DECL/N_DECL parameter nodes and would read
       the N_ID's string bytes as a child list -> crash.  Diagnose here while
       param_list still holds only the user-written params, and replace each
       offender with a checked `int <name>` placeholder so the AST stays
       well-formed and we emit exactly one error per untyped parameter (no
       cascade).  Plain (non-class) C functions keep their legacy K&R support. */
    if (curr_class != NULL) {
        const char *kind = "method";
        if (id->code == N_ID && id->u.s.s != NULL) {
            if (strncmp(id->u.s.s, "__ctor_", 7) == 0) kind = "constructor";
            else if (strncmp(id->u.s.s, "__dtor_", 7) == 0) kind = "destructor";
        }
        for (p = NL_HEAD(param_list->u.ops); p != NULL; p = next_p) {
            next_p = NL_NEXT(p);
            if (p->code != N_ID) continue;
            error(c2m_ctx, POS(p), "%s parameter '%s' must have a type", kind, p->u.s.s);
            NL_REMOVE(param_list->u.ops, p);
            decl_node = new_pos_node5(c2m_ctx, N_SPEC_DECL, POS(p),
                                      new_node1(c2m_ctx, N_SHARE,
                                                new_node1(c2m_ctx, N_LIST,
                                                          new_pos_node(c2m_ctx, N_INT, POS(p)))),
                                      new_pos_node2(c2m_ctx, N_DECL, POS(p),
                                                    new_str_node(c2m_ctx, N_ID, p->u.s, POS(p)),
                                                    new_node(c2m_ctx, N_LIST)),
                                      new_node(c2m_ctx, N_IGNORE),
                                      new_node(c2m_ctx, N_IGNORE),
                                      new_node(c2m_ctx, N_IGNORE));
            NL_APPEND(param_list->u.ops, decl_node);
            check(c2m_ctx, decl_node, r);
        }
    }

        // If it's a class method and NOT a static method, add 'this' parameter.
        // Static class methods (decl_spec.static_p == TRUE) have no implicit receiver.
            if (curr_class && !decl_spec.static_p) {
            if (c2m_options->verbose_p) printf("CLASS SCOPE\n");
            if (c2m_options->verbose_p || c2m_options->debug_p) {
                printf("N_FUNC_DEF: entered with curr_class uid=%u code=%d has_attr=%d\n",
                       curr_class->uid, curr_class->code, curr_class->attr != NULL);
            }
                // Robust lookup + late-force attach: make sure the CLASS node (curr_class or found via symbol) has
                // a decl_t attr whose .decl_spec.type is a valid TM_CLASS type. This ensures 'this' is prepended
                // to the method's param_list regardless of earlier attach timing or overwrites.
                struct type *class_type = NULL;
                node_t class_def = curr_class;
                if (curr_class->code == N_CLASS) {
                    // use curr_class directly as the class tag node
                } else {
                    node_t class_id = NL_HEAD(curr_class->u.ops);
                    if (class_id && class_id->code == N_ID) {
                        class_def = find_def(c2m_ctx, S_REGULARS, class_id, curr_scope, NULL);
                        if (!class_def) class_def = find_def(c2m_ctx, S_TAG, class_id, curr_scope, NULL);
                    }
                }
        if (class_def && class_def->code == N_CLASS) {
                    struct type *ct = create_class_type (c2m_ctx, class_def);
                    set_class_layout(c2m_ctx, class_def, ct);
                    class_type = ct;
                }
                if (!class_type || class_type->mode != TM_CLASS) {
                    node_t class_id = (curr_class->code == N_CLASS) ? NL_HEAD(curr_class->u.ops) : NULL;
                    fprintf(stderr, "DEBUG: class_type lookup FAILED via curr_class (uid=%u code=%s) id=%s attr=%p type_mode=%d\n",
                           curr_class->uid,
                           curr_class->code == N_CLASS ? "N_CLASS" : "other",
                           (class_id && class_id->code==N_ID)? class_id->u.s.s : "<noid>",
                           (void*)curr_class->attr,
                           (class_type ? class_type->mode : -999));
                    warning(c2m_ctx, POS(r), "class type not found or invalid for method definition");
                    break;
                }

        // Create 'this' parameter type: pointer to class type
        struct type *this_type = create_ptr_type (c2m_ctx, class_type);

        // Create a parameter node for 'this'
        node_t this_param = build_spec_decl (c2m_ctx, POS (r),
                                             new_node (c2m_ctx, N_LIST),
                                             build_decl (c2m_ctx, POS (r),
                                                         build_id (c2m_ctx, "this", POS (r)), NULL),
                                             NULL, NULL, NULL);
        // Set the type for 'this' parameter
        struct decl_spec this_decl_spec;
        init_decl_spec (&this_decl_spec);
        this_decl_spec.type = this_type;
        // Create declaration for 'this' in the current function scope
        create_decl(c2m_ctx, curr_scope, this_param, this_decl_spec, NULL, FALSE);
        // Prepend 'this' to the parameter list
        NL_PREPEND(param_list->u.ops, this_param);
    }


    // Process parameter identifier list
    for (p = NL_HEAD(param_list->u.ops); p != NULL; p = next_p) {
        next_p = NL_NEXT(p);
        if (p->code != N_ID) break;
        NL_REMOVE(param_list->u.ops, p);
        if (!symbol_find(c2m_ctx, S_REGULARS, p, curr_scope, &sym)) {
            if (c2m_options->pedantic_p) {
                error(c2m_ctx, POS(p), "parameter %s has no type", p->u.s.s);
            } else {
                warning(c2m_ctx, POS(p), "type of parameter %s defaults to int", p->u.s.s);
                decl_node = new_pos_node5(c2m_ctx, N_SPEC_DECL, POS(p),
                                          new_node1(c2m_ctx, N_SHARE,
                                                    new_node1(c2m_ctx, N_LIST,
                                                              new_pos_node(c2m_ctx, N_INT, POS(p)))),
                                          new_pos_node2(c2m_ctx, N_DECL, POS(p),
                                                        new_str_node(c2m_ctx, N_ID, p->u.s, POS(p)),
                                                        new_node(c2m_ctx, N_LIST)),
                                          new_node(c2m_ctx, N_IGNORE),
                                          new_node(c2m_ctx, N_IGNORE),
                                          new_node(c2m_ctx, N_IGNORE));
                NL_APPEND(param_list->u.ops, decl_node);
                check(c2m_ctx, decl_node, r);
            }
        } else {
            struct decl_spec *decl_spec_ptr;
            decl_node = sym.def_node;
            assert(decl_node->code == N_SPEC_DECL);
            NL_REMOVE(declarations->u.ops, decl_node);
            NL_APPEND(param_list->u.ops, decl_node);
            param_declarator = NL_EL(decl_node->u.ops, 1);
            assert(param_declarator->code == N_DECL);
            param_id = NL_HEAD(param_declarator->u.ops);
            if (NL_NEXT(NL_NEXT(param_declarator))->code != N_IGNORE) {
                error(c2m_ctx, POS(p), "initialized parameter %s", param_id->u.s.s);
            }
            decl_spec_ptr = &((decl_t)decl_node->attr)->decl_spec;
            adjust_param_type(c2m_ctx, &decl_spec_ptr->type);
            decl_spec = *decl_spec_ptr;
            if (decl_spec.typedef_p || decl_spec.extern_p || decl_spec.static_p || decl_spec.auto_p
                || decl_spec.thread_local_p) {
                error(c2m_ctx, POS(param_id), "storage specifier in a function parameter %s",
                      param_id->u.s.s);
            }
        }
    }

    // Process remaining declarations
    for (p = NL_HEAD(declarations->u.ops); p != NULL; p = NL_NEXT(p)) {
        if (p->code == N_ST_ASSERT) continue;
        assert(p->code == N_SPEC_DECL);
        param_declarator = NL_EL(p->u.ops, 1);
        if (param_declarator->code == N_IGNORE) continue;
        assert(param_declarator->code == N_DECL);
        param_id = NL_HEAD(param_declarator->u.ops);
        assert(param_id->code == N_ID);
        error(c2m_ctx, POS(param_id), "declaration for parameter %s but no such parameter",
              param_id->u.s.s);
    }

    // Add function definition and check block
    add__func__def(c2m_ctx, block, id->u.s);
    /* Defer the body check for class methods/constructors/destructors when
       the module driver is in its "signatures first" phase.  We finish the
       function block scope so the outer class member walk continues with
       curr_scope back at the class scope, and we hold on to `block` so the
       drain pass can re-enter the function scope (its node_scope chain is
       still intact: scope state is just a node pointer chain on attrs).
       The drain at the end of N_MODULE then runs the body in proper context,
       by which time every sibling class's symbols are visible. */
    if (defer_method_bodies_p && curr_class != NULL) {
      MIR_alloc_t alloc = c2m_alloc (c2m_ctx);
      pending_body_t pend;
      size_t fda_len = VARR_LENGTH (decl_t, func_decls_for_allocation);
      pend.func_def = r;
      pend.block = block;
      pend.class_node = curr_class;
      VARR_CREATE (decl_t, pend.saved_fda, alloc, fda_len);
      for (size_t fi = 0; fi < fda_len; fi++)
        VARR_PUSH (decl_t, pend.saved_fda, VARR_GET (decl_t, func_decls_for_allocation, fi));
      /* Don't leak this method's allocation entries into subsequent class
         members; we will restore them per-body in the drain. */
      VARR_TRUNC (decl_t, func_decls_for_allocation, 0);
      VARR_PUSH (pending_body_t, pending_method_bodies, pend);
      finish_scope (c2m_ctx);                 /* pop back to class scope */
      func_block_scope = saved_scope_before_func;
      assert (curr_scope == saved_scope_before_func);
      break;
    }
    check(c2m_ctx, block, r);

    /* Auto/lambda return type fixup: if the return type is still TP_UNDEF after
       checking the body (no return-with-value found), default to void. */
    {
      decl_t fdecl = r->attr;
      if (fdecl != NULL && fdecl->decl_spec.auto_p) {
        struct type *ftype = fdecl->decl_spec.type;
        if (ftype != NULL && ftype->mode == TM_FUNC
            && ftype->u.func_type->ret_type != NULL
            && ftype->u.func_type->ret_type->mode == TM_BASIC
            && ftype->u.func_type->ret_type->u.basic_type == TP_UNDEF) {
          ftype->u.func_type->ret_type->u.basic_type = TP_VOID;
          set_type_layout (c2m_ctx, ftype->u.func_type->ret_type);
        }
      }
    }

    // Process label uses
    for (size_t i = 0; i < VARR_LENGTH(node_t, label_uses); i++) {
        node_t n = VARR_GET(node_t, label_uses, i);
        id = n->code == N_LABEL_ADDR ? NL_HEAD(n->u.ops) : NL_NEXT(NL_HEAD(n->u.ops));
        if (!symbol_find(c2m_ctx, S_LABEL, id, func_block_scope, &sym)) {
            error(c2m_ctx, POS(id), "undefined label %s", id->u.s.s);
        } else if (n->code == N_LABEL_ADDR) {
            ((struct expr *)n->attr)->u.label_addr_target = sym.def_node;
        } else {
            n->attr = sym.def_node;
        }
    }
    VARR_TRUNC(node_t, label_uses, 0);

    /* After checking the function body, curr_scope must be back to what
       it was before we created the function block scope.  For top-level
       functions that's top_scope; for class methods it's the class scope. */
    assert(curr_scope == saved_scope_before_func);
    func_block_scope = saved_scope_before_func;
    process_func_decls_for_allocation(c2m_ctx);
    ns = block->attr;
    ns->size = round_size(ns->size, MAX_ALIGNMENT);
    ns->size += ns->call_arg_area_size;
    break;
  }
  case N_TYPE: {
    struct type *type;
    node_t specs = NL_HEAD (r->u.ops);
    node_t abstract_declarator = NL_NEXT (specs);
    struct decl_spec decl_spec = check_decl_spec (c2m_ctx, specs, r); /* only spec_qual_list here */

    type = check_declarator (c2m_ctx, abstract_declarator, FALSE);
    assert (NL_HEAD (abstract_declarator->u.ops)->code == N_IGNORE);
    decl_spec.type = append_type (type, decl_spec.type);
    if (context && context->code == N_COMPOUND_LITERAL) {
      r->attr = reg_malloc (c2m_ctx, sizeof (struct decl));
      init_decl (c2m_ctx, r->attr);
      ((struct decl *) r->attr)->decl_spec = decl_spec;
      check_type (c2m_ctx, decl_spec.type, 0, FALSE);
      set_type_layout (c2m_ctx, decl_spec.type);
      check_decl_align (c2m_ctx, &((struct decl *) r->attr)->decl_spec);
    } else {
      r->attr = reg_malloc (c2m_ctx, sizeof (struct decl_spec));
      *((struct decl_spec *) r->attr) = decl_spec;
      check_type (c2m_ctx, decl_spec.type, 0, FALSE);
      set_type_layout (c2m_ctx, decl_spec.type);
      check_decl_align (c2m_ctx, r->attr);
    }
    break;
  }
  case N_STMTEXPR: {
    node_t block = NL_HEAD (r->u.ops);
    if (r->attr != NULL) {
      /* Already checked (auto-deduction + create_decl may visit twice). */
      e = r->attr;
      break;
    }
    if (c2m_options->pedantic_p) {
      error (c2m_ctx, POS (r), "statement expression is not a part of C11 standard");
      break;
    }
    {
      /* The inner block is a statement and would zero curr_call_arg_area_offset.
         Keep the enclosing expression's reservation so two aggregate
         stmtexprs in one expr (`({s;}).x - ({s;}).x`) get distinct slots. */
      mir_size_t saved_arg_off = curr_call_arg_area_offset;
      check (c2m_ctx, block, r);
      curr_call_arg_area_offset = saved_arg_off;
    }
    node_t last_stmt = NL_TAIL (NL_EL (block->u.ops, 1)->u.ops);
    e = create_expr (c2m_ctx, r);
    if (last_stmt != NULL && last_stmt->code == N_EXPR) {
      node_t expr = NL_EL (last_stmt->u.ops, 1);
      e1 = expr->attr;
      if (e1 == NULL || e1->type == NULL) break;
      t1 = e1->type;
      e->type = create_type (c2m_ctx, t1);
      set_type_layout (c2m_ctx, e->type);
    } else {
      /* GNU: `({ if (e) ...; })` is a void statement-expression.
         glibc <assert.h> uses this form when __GNUC__ is defined. */
      e->type = create_type (c2m_ctx, NULL);
      e->type->mode = TM_BASIC;
      e->type->u.basic_type = TP_VOID;
      set_type_layout (c2m_ctx, e->type);
      break;
    }
    /* Aggregate stmtexpr results need a temporary that outlives the block's
       defers (which may destroy the last-expression local).  Reserve space in
       the call-arg area — NOT in func_block_scope->size / local offsets.
       process_func_decls_for_allocation recalculates local offsets from decls
       only, so a check-time bump of fns->size is discarded and e->c.u_val would
       collide with the first stack local (e.g. overwriting the List that a
       capturing Where reads, then double-free on exit). */
    if (func_block_scope != NULL
        && (t1->mode == TM_STRUCT || t1->mode == TM_UNION || t1->mode == TM_CLASS)) {
      update_call_arg_area_offset (c2m_ctx, t1, TRUE);
      ((struct node_scope *) func_block_scope->attr)->stack_var_p = TRUE;
    }
    break;
  }
  case N_BLOCK:
    if (curr_scope != r)
      create_node_scope (c2m_ctx, r); /* it happens if it is the top func block */
    // Moved as labels aren't properly scoped
    check_labels (c2m_ctx, NL_HEAD (r->u.ops), r);
    check (c2m_ctx, NL_EL (r->u.ops, 1), r);
    finish_scope (c2m_ctx);
    break;
  case N_MODULE:
    create_node_scope (c2m_ctx, r);
    top_scope = curr_scope;
    /* Pre-register every top-level class so that members of one class can
       reference any other class regardless of source order.  This is the
       check-phase complement to pre_register_class_tpnames() (which only
       teaches the parser that the names are types).  Without this pass the
       N_ID resolution in check_decl_spec() would emit "unknown type X" when
       a class body mentions a sibling class declared further down in the
       same translation unit.  Plain C semantics for typedefs / structs /
       functions are unaffected: only N_CLASS nodes are pre-registered.

       Also pre-register bare N_CLASS specializations injected from
       pending_lambdas at parse end (e.g. __generic_Host_int).  Those are
       not wrapped in N_SPEC_DECL; skipping them breaks mutual generics
       (Host ↔ Peer, List ↔ ListView) whose method signatures name each other. */
    {
      node_t items = NL_HEAD (r->u.ops);
      if (items != NULL && items->code == N_LIST) {
        for (node_t it = NL_HEAD (items->u.ops); it != NULL; it = NL_NEXT (it)) {
          node_t classes[4];
          int n_cls = 0;
          if (it->code == N_CLASS) {
            classes[n_cls++] = it;
          } else if (it->code == N_SPEC_DECL) {
            node_t specs = NL_HEAD (it->u.ops);
            if (specs == NULL) continue;
            if (specs->code == N_SHARE) specs = NL_HEAD (specs->u.ops);
            if (specs == NULL || specs->code != N_LIST) continue;
            for (node_t s = NL_HEAD (specs->u.ops); s != NULL; s = NL_NEXT (s)) {
              if (s->code == N_CLASS && n_cls < 4) classes[n_cls++] = s;
            }
          } else {
            continue;
          }
          for (int ci = 0; ci < n_cls; ci++) {
            node_t s = classes[ci];
            /* Skip generic class templates (only their specializations are
               real types).  Sentinel attr matches type_spec template mark. */
            if (s->attr == (void *)((intptr_t)-1)) continue;
            node_t cid = NL_HEAD (s->u.ops);
            if (cid == NULL || cid->code != N_ID) continue;
            /* Idempotent: if find_def can already see it, skip.  Otherwise
               install the N_CLASS node as the forward symbol so later
               name-lookups inside earlier class bodies resolve to it.  The
               canonical insert in check_decl_spec()'s N_CLASS arm will run
               in due course; symbol_insert is HTAB_INSERT-with-replace so
               doing it here first does not double-bind. */
            if (find_def (c2m_ctx, S_REGULARS, cid, curr_scope, NULL) == NULL) {
              symbol_insert (c2m_ctx, S_REGULARS, cid, curr_scope, s, NULL);
              symbol_insert (c2m_ctx, S_TAG, cid, curr_scope, s, NULL);
              tpname_add (c2m_ctx, cid, curr_scope, TRUE);
            }
          }
        }
      }
    }
    /* Hoist local/nested classes that appeared as generic type args
       (List<Item*> with Item defined inside a function) into top_scope so
       monomorphized collection methods can resolve them.  Specializations
       of such Lists are injected before the enclosing function, so without
       this hoist they would check against "unknown type Item". */
    if (local_type_hoists != NULL) {
      for (size_t _hi = 0; _hi < VARR_LENGTH (local_type_hoist_t, local_type_hoists); _hi++) {
        local_type_hoist_t _h = VARR_GET (local_type_hoist_t, local_type_hoists, _hi);
        if (_h.id == NULL || _h.class_def == NULL) continue;
        if (find_def (c2m_ctx, S_REGULARS, _h.id, top_scope, NULL) == NULL) {
          symbol_insert (c2m_ctx, S_REGULARS, _h.id, top_scope, _h.class_def, NULL);
          symbol_insert (c2m_ctx, S_TAG, _h.id, top_scope, _h.class_def, NULL);
          tpname_add (c2m_ctx, _h.id, top_scope, TRUE);
        }
      }
      VARR_TRUNC (local_type_hoist_t, local_type_hoists, 0);
    }
    /* First pass: queue every class method body for deferred checking so
       that cross-class references (constructor lookups, sibling-class method
       calls, etc.) can resolve regardless of source order. */
    defer_method_bodies_p = TRUE;
    check (c2m_ctx, NL_HEAD (r->u.ops), r);
    defer_method_bodies_p = FALSE;

    /* Drain: each pending body is checked in a fresh function context with
       curr_scope re-entered at its preserved function block.  We snapshot
       a few per-method context fields, run the body (case N_BLOCK pops the
       scope on the way out, leaving curr_scope at the class scope), then
       restore curr_scope to the module scope and reset class context.

       We use index-based iteration because checking a body can synthesise
       new pending entries (lambdas declared inline, etc.) and we want to
       process those too without invalidating the iterator. */
    {
      node_t module_scope = curr_scope;
      size_t i = 0;
      while (i < VARR_LENGTH (pending_body_t, pending_method_bodies)) {
        pending_body_t pend = VARR_GET (pending_body_t, pending_method_bodies, i);
        i++;
        node_t fdef = pend.func_def;
        node_t block = pend.block;
        node_t saved_class = curr_class;
        struct node_scope *ns;

        /* Re-enter function block scope (its parent chain is still set). */
        curr_class = pend.class_node;
        curr_scope = block;
        func_block_scope = block;
        curr_func_def = fdef;
        jump_ret_p = FALSE;
        curr_func_has_try = FALSE;
        curr_switch = curr_loop = curr_loop_switch = NULL;
        curr_call_arg_area_offset = 0;
        VARR_TRUNC (decl_t, func_decls_for_allocation, 0);
        /* Re-seed the allocation accumulator with what the signature pass
           captured for this method (its own FUNC_DEF decl and the synthetic
           `this` parameter for instance methods).  Body locals will be
           pushed on top during the check below, and the whole set is laid
           out together by process_func_decls_for_allocation. */
        for (size_t fi = 0; fi < VARR_LENGTH (decl_t, pend.saved_fda); fi++)
          VARR_PUSH (decl_t, func_decls_for_allocation,
                     VARR_GET (decl_t, pend.saved_fda, fi));
        VARR_DESTROY (decl_t, pend.saved_fda);

        check (c2m_ctx, block, fdef);          /* N_BLOCK finish_scope pops to class scope */

        /* Auto/lambda return-type fixup (mirrors the inline path). */
        {
          decl_t fdecl = fdef->attr;
          if (fdecl != NULL && fdecl->decl_spec.auto_p) {
            struct type *ftype = fdecl->decl_spec.type;
            if (ftype != NULL && ftype->mode == TM_FUNC
                && ftype->u.func_type->ret_type != NULL
                && ftype->u.func_type->ret_type->mode == TM_BASIC
                && ftype->u.func_type->ret_type->u.basic_type == TP_UNDEF) {
              ftype->u.func_type->ret_type->u.basic_type = TP_VOID;
              set_type_layout (c2m_ctx, ftype->u.func_type->ret_type);
            }
          }
        }

        /* Resolve labels used in the body, same as the inline path. */
        for (size_t li = 0; li < VARR_LENGTH (node_t, label_uses); li++) {
          node_t n = VARR_GET (node_t, label_uses, li);
          symbol_t sym;
          node_t id = n->code == N_LABEL_ADDR ? NL_HEAD (n->u.ops)
                                              : NL_NEXT (NL_HEAD (n->u.ops));
          if (!symbol_find (c2m_ctx, S_LABEL, id, func_block_scope, &sym)) {
            error (c2m_ctx, POS (id), "undefined label %s", id->u.s.s);
          } else if (n->code == N_LABEL_ADDR) {
            ((struct expr *) n->attr)->u.label_addr_target = sym.def_node;
          } else {
            n->attr = sym.def_node;
          }
        }
        VARR_TRUNC (node_t, label_uses, 0);

        process_func_decls_for_allocation (c2m_ctx);
        ns = block->attr;
        ns->size = round_size (ns->size, MAX_ALIGNMENT);
        ns->size += ns->call_arg_area_size;

        /* Restore outer state.  curr_scope is now at the class scope (block's
           parent) thanks to N_BLOCK's finish_scope; pop directly to module. */
        curr_class = saved_class;
        curr_scope = module_scope;
        func_block_scope = module_scope;
      }
      VARR_TRUNC (pending_body_t, pending_method_bodies, 0);
    }

    finish_scope (c2m_ctx);
    break;
  case N_IF: {
    node_t labels = NL_HEAD (r->u.ops);
    node_t expr = NL_NEXT (labels);
    node_t if_stmt = NL_NEXT (expr);
    node_t else_stmt = NL_NEXT (if_stmt);

    check_labels (c2m_ctx, labels, r);
    check (c2m_ctx, expr, r);
    e1 = expr->attr;
    t1 = e1->type;
    if (!scalar_type_p (t1)) {
      error (c2m_ctx, POS (expr), "if-expr should be of a scalar type");
    }
    check (c2m_ctx, if_stmt, r);
    check (c2m_ctx, else_stmt, r);
    break;
  }
  case N_SWITCH: {
    node_t saved_switch = curr_switch;
    node_t saved_loop_switch = curr_loop_switch;
    node_t labels = NL_HEAD (r->u.ops);
    node_t expr = NL_NEXT (labels);
    node_t stmt = NL_NEXT (expr);
    struct type *type;
    struct switch_attr *switch_attr;
    case_t el;
    node_t case_expr, case_expr2, another_case_expr, another_case_expr2;
    struct expr *case_e, *case_e2, *another_case_e, *another_case_e2;
    int signed_p, skip_range_p;

    check_labels (c2m_ctx, labels, r);
    check (c2m_ctx, expr, r);
    type = ((struct expr *) expr->attr)->type;
    if (!integer_type_p (type)) {
      init_type (&t);
      t.mode = TM_BASIC;
      t.u.basic_type = TP_INT;
      error (c2m_ctx, POS (expr), "switch-expr is of non-integer type");
    } else {
      t = integer_promotion (type);
    }
    signed_p = signed_integer_type_p (type);
    curr_switch = curr_loop_switch = r;
    switch_attr = curr_switch->attr = reg_malloc (c2m_ctx, sizeof (struct switch_attr));
    switch_attr->type = t;
    switch_attr->ranges_p = FALSE;
    switch_attr->min_val_case = switch_attr->max_val_case = NULL;
    DLIST_INIT (case_t, ((struct switch_attr *) curr_switch->attr)->case_labels);
    check (c2m_ctx, stmt, r);
    for (case_t c = DLIST_HEAD (case_t, switch_attr->case_labels); c != NULL;
         c = DLIST_NEXT (case_t, c)) { /* process simple cases */
      if (c->case_node->code == N_DEFAULT || NL_EL (c->case_node->u.ops, 1) != NULL) continue;
      if (HTAB_DO (case_t, case_tab, c, HTAB_FIND, el)) {
        error (c2m_ctx, POS (c->case_node), "duplicate case value");
        continue;
      }
      HTAB_DO (case_t, case_tab, c, HTAB_INSERT, el);
      if (switch_attr->min_val_case == NULL) {
        switch_attr->min_val_case = switch_attr->max_val_case = c;
        continue;
      }
      case_e = NL_HEAD (c->case_node->u.ops)->attr;
      case_e2 = NL_HEAD (switch_attr->min_val_case->case_node->u.ops)->attr;
      if (signed_p ? case_e->c.i_val < case_e2->c.i_val : case_e->c.u_val < case_e2->c.u_val)
        switch_attr->min_val_case = c;
      case_e2 = NL_HEAD (switch_attr->max_val_case->case_node->u.ops)->attr;
      if (signed_p ? case_e->c.i_val > case_e2->c.i_val : case_e->c.u_val > case_e2->c.u_val)
        switch_attr->max_val_case = c;
    }
    HTAB_CLEAR (case_t, case_tab);
    /* Check range cases against *all* simple cases or range cases *before* it. */
    for (case_t c = DLIST_HEAD (case_t, switch_attr->case_labels); c != NULL;
         c = DLIST_NEXT (case_t, c)) {
      if (c->case_node->code == N_DEFAULT || (case_expr2 = NL_EL (c->case_node->u.ops, 1)) == NULL)
        continue;
      switch_attr->ranges_p = TRUE;
      case_expr = NL_HEAD (c->case_node->u.ops);
      case_e = case_expr->attr;
      case_e2 = case_expr2->attr;
      skip_range_p = FALSE;
      for (case_t c2 = DLIST_HEAD (case_t, switch_attr->case_labels); c2 != NULL;
           c2 = DLIST_NEXT (case_t, c2)) {
        if (c2->case_node->code == N_DEFAULT) continue;
        if (c2 == c) {
          skip_range_p = TRUE;
          continue;
        }
        another_case_expr = NL_HEAD (c2->case_node->u.ops);
        another_case_expr2 = NL_EL (c2->case_node->u.ops, 1);
        if (skip_range_p && another_case_expr2 != NULL) continue;
        another_case_e = another_case_expr->attr;
        assert (another_case_e->const_p && integer_type_p (another_case_e->type));
        if (another_case_expr2 == NULL) {
          if ((signed_p && case_e->c.i_val <= another_case_e->c.i_val
               && another_case_e->c.i_val <= case_e2->c.i_val)
              || (!signed_p && case_e->c.u_val <= another_case_e->c.u_val
                  && another_case_e->c.u_val <= case_e2->c.u_val)) {
            error (c2m_ctx, POS (c->case_node), "duplicate value in a range case");
            break;
          }
        } else {
          another_case_e2 = another_case_expr2->attr;
          assert (another_case_e2->const_p && integer_type_p (another_case_e2->type));
          if ((signed_p
               && ((case_e->c.i_val <= another_case_e->c.i_val
                    && another_case_e->c.i_val <= case_e2->c.i_val)
                   || (case_e->c.i_val <= another_case_e2->c.i_val
                       && another_case_e2->c.i_val <= case_e2->c.i_val)))
              || (!signed_p
                  && ((case_e->c.u_val <= another_case_e->c.u_val
                       && another_case_e->c.u_val <= case_e2->c.u_val)
                      || (case_e->c.u_val <= another_case_e2->c.u_val
                          && another_case_e2->c.u_val <= case_e2->c.u_val)))) {
            error (c2m_ctx, POS (c->case_node), "duplicate value in a range case");
            break;
          }
        }
      }
    }
    curr_switch = saved_switch;
    curr_loop_switch = saved_loop_switch;
    break;
  }
  case N_DO:
  case N_WHILE: {
    node_t labels = NL_HEAD (r->u.ops);
    node_t expr = NL_NEXT (labels);
    node_t stmt = NL_NEXT (expr);
    node_t saved_loop = curr_loop;
    node_t saved_loop_switch = curr_loop_switch;

    check_labels (c2m_ctx, labels, r);
    check (c2m_ctx, expr, r);
    e1 = expr->attr;
    t1 = e1->type;
    if (!scalar_type_p (t1)) {
      error (c2m_ctx, POS (expr), "while-expr should be of a scalar type");
    }
    curr_loop = curr_loop_switch = r;
    check (c2m_ctx, stmt, r);
    curr_loop_switch = saved_loop_switch;
    curr_loop = saved_loop;
    break;
  }
  case N_FOR: {
    node_t labels = NL_HEAD (r->u.ops);
    node_t init = NL_NEXT (labels);
    node_t cond = NL_NEXT (init);
    node_t iter = NL_NEXT (cond);
    node_t stmt = NL_NEXT (iter);
    decl_t decl;
    node_t saved_loop = curr_loop;
    node_t saved_loop_switch = curr_loop_switch;

    check_labels (c2m_ctx, labels, r);
    create_node_scope (c2m_ctx, r);
    curr_loop = curr_loop_switch = r;
    check (c2m_ctx, init, r);
    if (init->code == N_LIST) {
      for (node_t spec_decl = NL_HEAD (init->u.ops); spec_decl != NULL;
           spec_decl = NL_NEXT (spec_decl)) {
        assert (spec_decl->code == N_SPEC_DECL);
        decl = spec_decl->attr;
        if (decl->decl_spec.typedef_p || decl->decl_spec.extern_p || decl->decl_spec.static_p
            || decl->decl_spec.thread_local_p) {
          error (c2m_ctx, POS (spec_decl),
                 "wrong storage specifier of for-loop initial declaration");
          break;
        }
      }
    }
    if (cond->code != N_IGNORE) { /* non-empty condition: */
      check (c2m_ctx, cond, r);
      e1 = cond->attr;
      t1 = e1->type;
      if (!scalar_type_p (t1)) {
        error (c2m_ctx, POS (cond), "for-condition should be of a scalar type");
      }
    }
    check (c2m_ctx, iter, r);
    check (c2m_ctx, stmt, r);
    finish_scope (c2m_ctx);
    curr_loop_switch = saved_loop_switch;
    curr_loop = saved_loop;
    break;
  }
  case N_GOTO: {
    node_t labels = NL_HEAD (r->u.ops);

    check_labels (c2m_ctx, labels, r);
    VARR_PUSH (node_t, label_uses, r);
    break;
  }
  case N_INDIRECT_GOTO: {
    node_t labels = NL_HEAD (r->u.ops);
    node_t expr = NL_NEXT (labels);

    check_labels (c2m_ctx, labels, r);
    check (c2m_ctx, expr, r);
    e1 = expr->attr;
    if (e1->type->mode != TM_PTR) error (c2m_ctx, POS (r), "computed goto must be pointer type");
    break;
  }
  case N_CONTINUE:
  case N_BREAK: {
    node_t labels = NL_HEAD (r->u.ops);

    if (r->code == N_BREAK && curr_loop_switch == NULL) {
      error (c2m_ctx, POS (r), "break statement not within loop or switch");
    } else if (r->code == N_CONTINUE && curr_loop == NULL) {
      error (c2m_ctx, POS (r), "continue statement not within a loop");
    }
    check_labels (c2m_ctx, labels, r);
    break;
  }
  case N_RETURN: {
    node_t labels = NL_HEAD (r->u.ops);
    node_t expr = NL_NEXT (labels);
    decl_t decl = curr_func_def->attr;
    struct type *ret_type, *type = decl->decl_spec.type;

    assert (type->mode == TM_FUNC);
    check_labels (c2m_ctx, labels, r);
    check (c2m_ctx, expr, r);
    ret_type = type->u.func_type->ret_type;

    /* A slice points into the function's own stack frame (alloca); returning
       one would dangle.  Materialize it into a heap/static container instead. */
    if (expr->code != N_IGNORE && expr->attr != NULL
        && ((struct expr *) expr->attr)->type->mode == TM_SLICE) {
      error (c2m_ctx, POS (r),
             "cannot return a filter/map slice: it lives on this function's stack");
      break;
    }

    /* Lambda / auto return type inference: TP_UNDEF means "not yet known".
       On the first return-with-value we lock in the return type; subsequent
       returns use the normal compatibility check.  'return;' is left alone —
       TP_UNDEF will be fixed up to void after the body check completes. */
    if (ret_type != NULL && ret_type->mode == TM_BASIC
        && ret_type->u.basic_type == TP_UNDEF) {
      if (expr->code != N_IGNORE) {
        struct expr *re = (struct expr *) expr->attr;
        if (re != NULL && re->type != NULL) {
          struct type *inferred = create_type (c2m_ctx, re->type);
          type->u.func_type->ret_type = inferred;
          set_type_layout (c2m_ctx, inferred);
        }
      }
      /* 'return;' with TP_UNDEF: leave for void fixup after body check */
      break;
    }

    if (expr->code != N_IGNORE && void_type_p (ret_type)) {
      error (c2m_ctx, POS (r), "return with a value in function returning void");
    } else if (expr->code == N_IGNORE
               && (ret_type->mode != TM_BASIC || ret_type->u.basic_type != TP_VOID)) {
      error (c2m_ctx, POS (r), "return with no value in function returning non-void");
    } else if (expr->code != N_IGNORE) {
      /* Move-only collections: bare return of an lvalue would shallow-alias.
         - Local / parameter: C++-style implicit `return move a;`
         - Storage lvalue (field, array element of another object, e.g.
           map.vals[i]): rewrite to `return a.Copy()` so Get/ValAt of
           Map of List does a deep copy instead of stealing the slot. */
      if (expr->code != N_MOVE && expr->attr != NULL) {
        struct expr *re = expr->attr;
        if (re->type != NULL && class_is_move_only_collection_p (c2m_ctx, re->type)
            && re->u.lvalue_node != NULL) {
          node_t pe = expr;
          while (pe != NULL && pe->code == N_CAST) pe = NL_EL (pe->u.ops, 1);
          int storage_p
            = (pe != NULL
               && (pe->code == N_IND || pe->code == N_FIELD || pe->code == N_DEREF_FIELD
                   || pe->code == N_DEREF));
          /* Unlink first: N_MOVE / Copy both re-parent `expr`. */
          NL_REMOVE (r->u.ops, expr);
          if (storage_p) {
            /* return expr.Copy() */
            node_t copy_call
              = build_dot_call (c2m_ctx, POS (r), expr, "Copy", new_node (c2m_ctx, N_LIST));
            check (c2m_ctx, copy_call, r);
            NL_APPEND (r->u.ops, copy_call);
            expr = copy_call;
          } else {
            node_t m = new_pos_node1 (c2m_ctx, N_MOVE, POS (r), expr);
            struct expr *me = create_expr (c2m_ctx, m);
            me->type = re->type;
            if (curr_scope != top_scope)
              update_call_arg_area_offset (c2m_ctx, re->type, TRUE);
            NL_APPEND (r->u.ops, m);
            expr = m;
          }
        }
      }
      check_assignment_types (c2m_ctx, ret_type, NULL, expr->attr, r);
    }
    break;
  }
  case N_EXPR: {
    node_t labels = NL_HEAD (r->u.ops);
    node_t expr = NL_NEXT (labels);

    check_labels (c2m_ctx, labels, r);
    check (c2m_ctx, expr, r);
    break;
  }
  case N_DEFER: {
    /* defer <stmt>: the statement is checked here (in the current scope) but its
       code is emitted at every exit of the enclosing block (see gen). */
    node_t labels = NL_HEAD (r->u.ops);
    node_t stmt = NL_NEXT (labels);

    check_labels (c2m_ctx, labels, r);
    check (c2m_ctx, stmt, r);
    /* -fexceptions, `defer delete <class-ptr-expr>;`: also make this cleanup
       reachable via a runtime-callable thunk, so a `throw` that skips this
       scope's normal exit can still run it (see cyexc.h's cy__defer_stack
       and gen_defer_shadow_push). Mirrors the auto_release_call hook in
       ownership.c for the same N_DELETE shape. */
    if (c2m_options != NULL && c2m_options->exceptions_p && stmt->code == N_DELETE) {
      node_t del_expr = NL_EL (stmt->u.ops, 1);
      struct expr *de = del_expr != NULL ? del_expr->attr : NULL;
      if (de != NULL && de->type != NULL && de->type->mode == TM_PTR
          && de->type->u.ptr_type != NULL && de->type->u.ptr_type->mode == TM_CLASS
          && de->type->u.ptr_type->u.tag_type != NULL) {
        node_t tag_id = TAG_ID (de->type->u.ptr_type->u.tag_type);
        if (tag_id != NULL && tag_id->code == N_ID && tag_id->u.s.s != NULL)
          ensure_defer_thunk (c2m_ctx, tag_id->u.s.s, POS (stmt));
      }
    }
    break;
  }
  case N_GO: {
    /* go f(args); (-ffibers): the operand must be a direct call to a plain
       function with at most 8 GP-class arguments (cy_spawn8 trampoline ABI).
       The call itself is checked normally — gen discards its result and does
       not emit it; it emits cy_spawn8(fn, nargs, args...) instead. */
    node_t labels = NL_HEAD (r->u.ops);
    node_t call = NL_NEXT (labels);
    node_t func, args, fdef;
    struct expr *fe;
    int nargs = 0, direct_p = FALSE;

    check_labels (c2m_ctx, labels, r);
    if (call->code != N_CALL) {
      error (c2m_ctx, POS (r), "go: operand must be a function call (go f(args);)");
      break;
    }
    check (c2m_ctx, call, r);
    func = NL_HEAD (call->u.ops);
    fe = func->attr;
    fdef = fe != NULL ? fe->def_node : NULL;
    if (func->code == N_ID && fdef != NULL) {
      if (fdef->code == N_FUNC_DEF) {
        direct_p = TRUE;
      } else {
        decl_t fd = fdef->attr;
        if (fd != NULL && fd->decl_spec.type != NULL
            && fd->decl_spec.type->mode == TM_FUNC)
          direct_p = TRUE; /* extern function declaration */
      }
    }
    if (!direct_p) {
      error (c2m_ctx, POS (r),
             "go: only direct calls to plain functions are supported "
             "(wrap methods/function pointers in a plain function)");
      break;
    }
    args = NL_EL (call->u.ops, 1);
    for (node_t a = NL_HEAD (args->u.ops); a != NULL; a = NL_NEXT (a)) {
      struct expr *ae = a->attr;
      nargs++;
      if (!go_gp_type_p (c2m_ctx, ae != NULL ? ae->type : NULL)) {
        error (c2m_ctx, POS (r),
               "go: argument %d must have an integer or pointer type "
               "(floating-point and by-value aggregates cannot ride the fiber pack)",
               nargs);
        break;
      }
    }
    if (nargs > 8)
      error (c2m_ctx, POS (r), "go: too many arguments (%d, max 8)", nargs);
    break;
  }
  case N_AWAIT: {
    /* await [expr]; (-ffibers): evaluate the optional expression, then yield
       the fiber.  Pure yield — channel parking is explicit in Chan<T>. */
    node_t labels = NL_HEAD (r->u.ops);
    node_t expr = NL_NEXT (labels);

    check_labels (c2m_ctx, labels, r);
    if (expr != NULL) check (c2m_ctx, expr, r);
    break;
  }
  case N_DELETE: {
    /* delete <ptr>: run the pointed-to object's destructor (if its class defines
       one) and free the heap storage.  r->attr caches the resolved destructor
       N_FUNC_DEF (or NULL) for gen. */
    node_t labels = NL_HEAD (r->u.ops);
    node_t expr = NL_NEXT (labels);

    check_labels (c2m_ctx, labels, r);
    check (c2m_ctx, expr, r);
    e1 = expr->attr;
    t1 = e1->type;
    r->attr = NULL; /* destructor def_node, NULL = none */
    /* dict values are TM_DICT (a DictValue* pointer at MIR level); they are
       freed by dict_destroy() which handles both arena-backed and plain dicts. */
    if (t1->mode == TM_DICT) break;
    if (t1->mode != TM_PTR) {
      /* Non-pointer delete: in a generic template a statement like
         `delete this->data[i]` may be instantiated with T = a non-pointer
         (int, double, by-value class, ...).  Such a delete is only ever reached
         when guarded by `is_pointer<T>()` (which folds to 0 for non-pointers),
         so the statement is dead code.  Rather than reject the whole template,
         mark this N_DELETE as a no-op for gen.  This keeps `delete` polymorphic
         in generic ownership code while still being a real free for pointers. */
      r->attr = (void *) (intptr_t) -1; /* sentinel: gen emits nothing */
      break;
    } else {
      struct type *pt = t1->u.ptr_type;
      if (pt != NULL && pt->mode == TM_CLASS && pt->u.tag_type != NULL) {
        node_t class_def = pt->u.tag_type;
        node_t cid = NL_HEAD (class_def->u.ops);
        if (cid != NULL && cid->code == N_ID) {
          char dtor_name[300];
          node_t dtor_id, dtor_def;
          snprintf (dtor_name, sizeof (dtor_name), "__dtor_%s", cid->u.s.s);
          dtor_id = build_id (c2m_ctx, dtor_name, POS (r));
          dtor_def = find_def (c2m_ctx, S_REGULARS, dtor_id, curr_scope, NULL);
          if (dtor_def != NULL && dtor_def->code == N_FUNC_DEF) r->attr = dtor_def;
        }
      }
    }
    break;
  }
  case N_DETACH: {
    /* detach <expr>: the value is removed from the current scope's arena
       tracking set (String registry or object registry).  Result type is the
       inner expression's type — detach is a pass-through at the value level;
       only its side effect (the runtime un-track call) matters.

       N_DETACH classifies as an expression (see classify_node), so the check
       function's tail relies on the outer-scope `e` being non-NULL to skip
       the "error recovery: default to TP_INT" path.  Assign to the function-
       level `e` here (NOT a shadowing local) so that path is correctly
       suppressed when our type setup succeeds.

       We also store a runtime selector on r->attr's `def_node` slot via a
       sentinel pointer cast: but since `attr` is shared with the struct expr,
       we keep the runtime selector in `e->def_node` instead, which is
       otherwise unused for non-identifier expressions:
           e->def_node == (node_t)1 — string detach   (c2m_str_detach)
           e->def_node == (node_t)2 — object detach   (c2m_obj_detach)
           e->def_node == NULL    — no runtime call (untracked / scalar) */
    node_t inner = NL_HEAD (r->u.ops);
    struct expr *ie;
    struct type *it;

    check (c2m_ctx, inner, r);
    ie = inner->attr;
    if (ie == NULL || ie->type == NULL) {
      /* Inner expression failed to type-check; degrade gracefully and let
         the trailing fallback path stamp an int-typed error expr. */
      break;
    }
    it = ie->type;
    /* IMPORTANT: assign to the OUTER `e` declared at the top of check(),
       not a new local.  The trailing tail-block at the bottom of check()
       checks `if (e != NULL)` to decide whether to run adjust_type /
       set_type_layout / const-folding; if we shadowed with a local, it
       would treat the N_DETACH as an error-recovery case and replace our
       expr struct with a default TP_INT one (silent type corruption). */
    e = create_expr (c2m_ctx, r);
    /* Share the inner's type pointer so every type subfield — ptr_type,
       func_type, tag_type, type_qual, basic_type — is preserved verbatim. */
    e->type = it;
    /* Classify which detach runtime applies, if any. */
    if (builtin_string_type_p (it)
        || (it->mode == TM_ARR && it->pos_node != NULL && it->pos_node->code == N_STR)) {
      e->def_node = (node_t) (intptr_t) 1; /* string detach */
    } else if (it->mode == TM_PTR) {
      e->def_node = (node_t) (intptr_t) 2; /* object detach */
    } else if (it->mode == TM_BASIC || it->mode == TM_STRUCT
               || it->mode == TM_UNION || it->mode == TM_CLASS) {
      /* Detaching a non-tracked value is allowed (no-op) but worth flagging
         so users catch silly mistakes like `return detach 42;`.  Demoted to
         a soft warning so generic-template code that may instantiate detach
         on a scalar T still compiles. */
      warning (c2m_ctx, POS (r),
               "`detach` on a non-arena-tracked value is a no-op (only "
               "`String` and pointer-to-class values are arena-managed)");
      e->def_node = NULL;
    } else {
      e->def_node = NULL;
    }
    break;
  }
  case N_ATTACH: {
    /* attach <expr>;  — stub statement.  We check the inner expression so any
       errors surface (e.g. unknown identifier) but emit no runtime call at gen
       time.  Reserved for the future ownership / dataflow pass. */
    node_t labels = NL_HEAD (r->u.ops);
    node_t expr = NL_NEXT (labels);

    check_labels (c2m_ctx, labels, r);
    check (c2m_ctx, expr, r);
    /* No semantic effect today; keep the AST node so future passes can use it. */
    break;
  }
  case N_UNOWNED: {
    /* unowned <decl> — pure wrapper.  Recurse into the inner declaration so it
       gets type-checked normally; while recursing, raise `in_unowned_p` so the
       auto-defer-candidate detection in N_SPEC_DECL knows to *skip* these
       bindings (the user is taking manual responsibility).  The opt-out marker
       is preserved in the AST for the upcoming ownership pass to consult. */
    node_t inner = NL_HEAD (r->u.ops);
    int saved_unowned = in_unowned_p;
    in_unowned_p = TRUE;
    if (inner != NULL) check (c2m_ctx, inner, r);
    in_unowned_p = saved_unowned;
    break;
  }
  case N_OWNED: {
    /* owned <decl> — managed-ownership opt-in wrapper.  Recurse into the inner
       declaration with `in_owned_p` raised so N_SPEC_DECL records the binding
       as part of the managed layer (decl->owned_p).  The marker is preserved
       in the AST for the ownership pass to consult. */
    node_t inner = NL_HEAD (r->u.ops);
    int saved_owned = in_owned_p;
    in_owned_p = TRUE;
    if (inner != NULL) check (c2m_ctx, inner, r);
    in_owned_p = saved_owned;
    break;
  }
  case N_MOVE:
  case N_READONLY: {
    /* move <expr> / readonly <expr>:
       - Pointer form (owned layer): yield the same pointer value; ownership
         pass tracks move vs borrow.
       - Class-value form (`move` only): transfer a by-value class with a
         destructor (e.g. stack List) — result type is the class; gen zeros
         the source so its RAII dtor is a no-op.  Required for move-only
         assign/init of resource-owning classes.

       Like N_DETACH, these classify as expressions, so we MUST assign to the
       outer `e` (not a shadowing local) to suppress the trailing error-
       recovery TP_INT path. */
    node_t inner = NL_HEAD (r->u.ops);
    struct expr *ie;
    struct type *it;
    const char *kw = (r->code == N_MOVE) ? "move" : "readonly";

    check (c2m_ctx, inner, r);
    ie = inner->attr;
    if (ie == NULL || ie->type == NULL) break; /* degrade: tail stamps int expr */
    it = ie->type;
    if (r->code == N_MOVE && it->mode == TM_CLASS) {
      /* Classes with a dtor need an lvalue so gen can zero the source.
         POD classes (no dtor) still relocate via block_move + zero — same
         as nested List shells.  No warning: generic List/Map use `move` for
         every T and monomorph noise was drowning real diagnostics. */
      if (class_has_dtor_p (c2m_ctx, it) && ie->u.lvalue_node == NULL)
        error (c2m_ctx, POS (r),
               "`move` of a class value requires an lvalue source");
      /* Reserve stack space for the moved-value temporary emitted in gen. */
      if (curr_scope != top_scope)
        update_call_arg_area_offset (c2m_ctx, it, TRUE);
    } else if (r->code == N_READONLY && it->mode != TM_PTR) {
      /* readonly is pointer-only. */
      warning (c2m_ctx, POS (r),
               "`readonly` applies to owned pointers; this operand has no "
               "managed-ownership effect");
    }
    /* move of scalar/float/etc.: silent no-op (generic List/Map monomorphs). */
    e = create_expr (c2m_ctx, r);
    /* Share the inner's type pointer verbatim.  Result is a value, not an
       lvalue — create_expr already nulled u.lvalue_node. */
    e->type = it;
    break;
  }
  case N_INTERFACE:
    /* A pure compile-time contract: no type, no layout, no code.  Conformance
       (impl checks) is verified elsewhere; nothing to do here. */
    break;
  case N_CLASS: {
    /* Skip generic class templates — they have a sentinel attr and are only
       instantiated as specialized concrete classes via get_or_create_specialization. */
    if (r->attr == (void *)((intptr_t)-1)) break;

    node_t id = NL_HEAD (r->u.ops);
    node_t decl_list = NL_NEXT (id);
    struct type *class_type = create_type (c2m_ctx, NULL);
    class_type->mode = TM_CLASS;
    class_type->u.tag_type = process_tag (c2m_ctx, r, id, decl_list);
    class_type->pos_node = r;

    /* Register the class name in S_REGULAR so check_decl_spec N_ID can find it
       (needed for specialized classes injected directly into the module list). */
    if (id->code == N_ID) {
      symbol_insert (c2m_ctx, S_REGULARS, id, curr_scope, r, NULL);
    }

    // Always attach as decl_t (consistent with check_decl_spec N_CLASS path) so that
    // later when curr_class = this node, N_FUNC_DEF can do decl = curr_class->attr; class_type = decl->decl_spec.type
    if (!r->attr) {
      decl_t d = reg_malloc (c2m_ctx, sizeof (struct decl));
      init_decl (c2m_ctx, d);
      r->attr = d;
    }
    decl_t d = (decl_t) r->attr;
    d->decl_spec.type = class_type;

    set_class_layout (c2m_ctx, r, class_type);
    if (decl_list->code != N_IGNORE) {
      create_node_scope (c2m_ctx, r);
      node_t prev_class = curr_class;
      curr_class = r;
      check (c2m_ctx, decl_list, r);
      curr_class = prev_class;
      finish_scope (c2m_ctx);
      make_type_complete (c2m_ctx, class_type);
      /* Phase 1: structurally verify any `impl I, J` clauses. */
      verify_class_impls (c2m_ctx, r, class_type->u.tag_type);
    }
        break;
      }

      case N_THROW: {
        /* ops: [labels, id_expr, msg_expr?] */
        node_t id_expr  = NL_EL(r->u.ops, 1);
        node_t msg_expr = NL_EL(r->u.ops, 2);
        check(c2m_ctx, id_expr, r);                 /* exception id  (int)     */
        if (msg_expr != NULL && msg_expr->code != N_IGNORE)
          check(c2m_ctx, msg_expr, r);              /* message       (String)  */
        break;
      }

      case N_TRY: {
        /* ops: [labels, body_block, N_LIST:(N_CATCH)+] */
        node_t body       = NL_EL(r->u.ops, 1);
        node_t catch_list = NL_EL(r->u.ops, 2);

        /* Force this function's scalar locals/params into memory (see
           curr_func_has_try) so they survive the try's setjmp/longjmp. */
        curr_func_has_try = TRUE;
        check(c2m_ctx, body, r);                    /* walk try body block     */
        for (node_t cat = NL_HEAD(catch_list->u.ops); cat != NULL; cat = NL_NEXT(cat)) {
          node_t class_sel = NL_EL(cat->u.ops, 0);  /* class selector | N_IGNORE */
          node_t handler   = NL_EL(cat->u.ops, 2);  /* handler block            */
          /* A named class selector must resolve to a constant integer id (an
             enum constant from the exception prelude or any user constant).
             An absent selector or the base type `Exception` => catch-all. */
          if (class_sel->code == N_ID && strcmp(class_sel->u.s.s, "Exception") != 0)
            check(c2m_ctx, class_sel, cat);
          check(c2m_ctx, handler, cat);             /* declares catch var + body */
        }
        break;
      }

      default:
    printf("ERROR: invalid r->code = %d\n", r->code);
      abort ();
  }
  if (e != NULL) {
    node_t base;
    mir_llong offset;
    int deref;

    assert (!stmt_p);
    if (context && context->code != N_ALIGNOF && context->code != N_SIZEOF
        && context->code != N_EXPR_SIZEOF)
      e->type = adjust_type (c2m_ctx, e->type);
    set_type_layout (c2m_ctx, e->type);
    if (!e->const_p && check_const_addr_p (c2m_ctx, r, &base, &offset, &deref) && deref == 0) {
      if (base == NULL) {
        e->const_p = TRUE;
        e->c.i_val = offset;
      } else if (base->code == N_LABEL_ADDR) {
        e->const_addr_p = TRUE;
        e->c.i_val = offset;
      }
    }
    if (e->const_p) convert_value (e, e->type);
  } else if (stmt_p) {
    curr_call_arg_area_offset = 0;
  } else if (expr_attr_p) { /* it is an error -- define any expr and type: */
    assert (!stmt_p);
    e = create_expr (c2m_ctx, r);
    e->type->mode = TM_BASIC;
    e->type->u.basic_type = TP_INT;
  }
  VARR_POP (node_t, context_stack);
}

static void do_context (c2m_ctx_t c2m_ctx, node_t r) {
  check_ctx_t check_ctx = c2m_ctx->check_ctx;

  VARR_TRUNC (node_t, call_nodes, 0);
  VARR_TRUNC (node_t, possible_incomplete_decls, 0);
  check (c2m_ctx, r, NULL);
  for (size_t i = 0; i < VARR_LENGTH (node_t, possible_incomplete_decls); i++) {
    node_t spec_decl = VARR_GET (node_t, possible_incomplete_decls, i);
    decl_t decl = spec_decl->attr;
    if (incomplete_type_p (c2m_ctx, decl->decl_spec.type))
      error (c2m_ctx, POS (spec_decl), "incomplete struct or union");
  }
}

static void context_init (c2m_ctx_t c2m_ctx) {
  MIR_alloc_t alloc = c2m_alloc (c2m_ctx);
  check_ctx_t check_ctx;

  c2m_ctx->check_ctx = check_ctx = c2mir_calloc (c2m_ctx, sizeof (struct check_ctx));
  n_i1_node = new_i_node (c2m_ctx, 1, no_pos);
  VARR_CREATE (node_t, context_stack, alloc, 64);
  check (c2m_ctx, n_i1_node, NULL);
  func_block_scope = curr_scope = NULL;
  curr_class = NULL;
  VARR_CREATE (node_t, label_uses, alloc, 0);
  symbol_init (c2m_ctx);
  in_params_p = FALSE;
  curr_unnamed_anon_struct_union_member = NULL;
  HTAB_CREATE (case_t, case_tab, alloc, 100, case_hash, case_eq, NULL);
  VARR_CREATE (decl_t, func_decls_for_allocation, alloc, 1024);
  VARR_CREATE (node_t, possible_incomplete_decls, alloc, 512);
  defer_method_bodies_p = FALSE;
  VARR_CREATE (pending_body_t, pending_method_bodies, alloc, 32);
}

static void context_finish (c2m_ctx_t c2m_ctx) {
  check_ctx_t check_ctx;

  if (c2m_ctx == NULL || (check_ctx = c2m_ctx->check_ctx) == NULL) return;
  if (context_stack != NULL) VARR_DESTROY (node_t, context_stack);
  if (label_uses != NULL) VARR_DESTROY (node_t, label_uses);
  symbol_finish (c2m_ctx);
  if (case_tab != NULL) HTAB_DESTROY (case_t, case_tab);
  if (func_decls_for_allocation != NULL) VARR_DESTROY (decl_t, func_decls_for_allocation);
  if (possible_incomplete_decls != NULL) VARR_DESTROY (node_t, possible_incomplete_decls);
  if (pending_method_bodies != NULL) VARR_DESTROY (pending_body_t, pending_method_bodies);
  reg_free (c2m_ctx, c2m_ctx->check_ctx);
}

/* ------------------------ Context Checker Finish ---------------------------- */
