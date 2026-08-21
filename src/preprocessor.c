/* --------------------------- Preprocessor -------------------------------- */

typedef struct macro {          /* macro definition: */
  token_t id;                   /* T_ID */
  VARR (token_t) * params;      /* (T_ID)* [N_DOTS], NULL means no params */
  VARR (token_t) * replacement; /* token*, NULL means a standard macro */
  int ignore_p;
} *macro_t;

DEF_VARR (macro_t);
DEF_HTAB (macro_t);

typedef struct ifstate {
  int skip_p, true_p, else_p; /* ??? flags that we are in a else part and in a false part */
  pos_t if_pos;               /* pos for #if and last #else, #elif */
} *ifstate_t;

DEF_VARR (ifstate_t);

typedef VARR (token_t) * token_arr_t;

DEF_VARR (token_arr_t);

typedef struct macro_call {
  macro_t macro;
  pos_t pos;
  /* Var array of arguments, each arg is var array of tokens, NULL for args absence: */
  VARR (token_arr_t) * args;
  int repl_pos;                 /* position in macro replacement */
  VARR (token_t) * repl_buffer; /* LIST:(token nodes)* */
} *macro_call_t;

DEF_VARR (macro_call_t);

/* Recorded include-guard: if `guard` is still #defined, re-#include of
   `fname` is a no-op.  Both pointers are uniq_cstr / macro-repr interned. */
typedef struct {
  const char *fname;
  const char *guard;
} include_guard_t;

DEF_HTAB (include_guard_t);

/* Path existence probe cache: interned absolute/full path → found (0/1).
   Avoids repeated access()/fopen for the same probe during include search. */
typedef struct {
  const char *path;
  int found;
} path_exist_t;

DEF_HTAB (path_exist_t);

/* #include resolve cache: (name, quote_p, from_dir) → winning path/content.
   Angle includes use from_dir == NULL.  Quote includes key by the including
   file's directory so relative lookup is correct. */
typedef struct {
  const char *name;     /* interned include spelling */
  const char *from_dir; /* interned dir of including file, or NULL for <> */
  int quote_p;
  const char *resolved; /* interned full path, or short name for builtins */
  const char *content;  /* non-NULL → in-memory standard include body */
} include_resolve_t;

DEF_HTAB (include_resolve_t);

struct pre_ctx {
  VARR (char_ptr_t) * once_include_files;
  VARR (token_t) * temp_tokens;
  HTAB (macro_t) * macro_tab;
  VARR (macro_t) * macros;
  HTAB (include_guard_t) * include_guard_tab;
  HTAB (path_exist_t) * path_exist_tab;
  HTAB (include_resolve_t) * include_resolve_tab;
  VARR (ifstate_t) * ifs; /* stack of ifstates */
  int no_out_p;           /* don't output lexs -- put them into buffer */
  int skip_if_part_p;
  token_t if_id; /* last processed token #if or #elif: used for error messages */
  char date_str[50], time_str[50], date_str_repr[50], time_str_repr[50];
  VARR (token_t) * output_buffer;
  VARR (macro_call_t) * macro_call_stack;
  VARR (token_t) * pre_expr;
  token_t pre_last_token;
  pos_t actual_pre_pos;
  unsigned long pptokens_num;
  void (*pre_out_token_func) (c2m_ctx_t c2m_ctx, token_t);
};

#define once_include_files pre_ctx->once_include_files
#define temp_tokens pre_ctx->temp_tokens
#define macro_tab pre_ctx->macro_tab
#define macros pre_ctx->macros
#define include_guard_tab pre_ctx->include_guard_tab
#define path_exist_tab pre_ctx->path_exist_tab
#define include_resolve_tab pre_ctx->include_resolve_tab
#define ifs pre_ctx->ifs
#define no_out_p pre_ctx->no_out_p
#define skip_if_part_p pre_ctx->skip_if_part_p
#define if_id pre_ctx->if_id
#define date_str pre_ctx->date_str
#define time_str pre_ctx->time_str
#define date_str_repr pre_ctx->date_str_repr
#define time_str_repr pre_ctx->time_str_repr
#define output_buffer pre_ctx->output_buffer
#define macro_call_stack pre_ctx->macro_call_stack
#define pre_expr pre_ctx->pre_expr
#define pre_last_token pre_ctx->pre_last_token
#define actual_pre_pos pre_ctx->actual_pre_pos
#define pptokens_num pre_ctx->pptokens_num
#define pre_out_token_func pre_ctx->pre_out_token_func

static int include_guard_eq (include_guard_t a, include_guard_t b, void *arg MIR_UNUSED) {
  return a.fname == b.fname;
}

static htab_hash_t include_guard_hash (include_guard_t g, void *arg MIR_UNUSED) {
  return (htab_hash_t) mir_hash64 ((uint64_t) (uintptr_t) g.fname, 0x9e);
}

static int path_exist_eq (path_exist_t a, path_exist_t b, void *arg MIR_UNUSED) {
  return strcmp (a.path, b.path) == 0;
}

static htab_hash_t path_exist_hash (path_exist_t e, void *arg MIR_UNUSED) {
  return (htab_hash_t) mir_hash (e.path, strlen (e.path), 0x51);
}

static int include_resolve_eq (include_resolve_t a, include_resolve_t b, void *arg MIR_UNUSED) {
  return a.name == b.name && a.from_dir == b.from_dir && a.quote_p == b.quote_p;
}

static htab_hash_t include_resolve_hash (include_resolve_t e, void *arg MIR_UNUSED) {
  uint64_t h = mir_hash_init (0x17);
  h = mir_hash_step (h, (uint64_t) (uintptr_t) e.name);
  h = mir_hash_step (h, (uint64_t) (uintptr_t) e.from_dir);
  h = mir_hash_step (h, (uint64_t) (unsigned) e.quote_p);
  return (htab_hash_t) mir_hash_finish (h);
}

static int macro_name_defined_p (c2m_ctx_t c2m_ctx, const char *repr) {
  pre_ctx_t pre_ctx = c2m_ctx->pre_ctx;
  struct token id_tok;
  struct macro macro_struct;
  macro_t tab_macro;

  if (repr == NULL) return FALSE;
  memset (&id_tok, 0, sizeof (id_tok));
  id_tok.code = T_ID;
  id_tok.repr = repr;
  macro_struct.id = &id_tok;
  return HTAB_DO (macro_t, macro_tab, &macro_struct, HTAB_FIND, tab_macro);
}

static void record_include_guard (c2m_ctx_t c2m_ctx, const char *fname, const char *guard) {
  pre_ctx_t pre_ctx = c2m_ctx->pre_ctx;
  include_guard_t g, tab_g;

  if (fname == NULL || guard == NULL || include_guard_tab == NULL) return;
  g.fname = fname;
  g.guard = guard;
  if (HTAB_DO (include_guard_t, include_guard_tab, g, HTAB_FIND, tab_g)) {
    /* Update guard name if re-recorded after #undef (same path). */
    if (tab_g.guard != guard) {
      HTAB_DO (include_guard_t, include_guard_tab, g, HTAB_REPLACE, tab_g);
    }
    return;
  }
  HTAB_DO (include_guard_t, include_guard_tab, g, HTAB_INSERT, tab_g);
}

static void ig_fail (stream_t s) {
  if (s != NULL && s->ig_state != IG_OFF && s->ig_state != IG_FAILED) {
    s->ig_state = IG_FAILED;
    s->ig_macro = NULL;
  }
}

/* Non-whitespace content before a complete guard (or after #endif) spoils
   the simple include-guard pattern. */
static void ig_note_significant_token (stream_t s, token_t t) {
  if (s == NULL || t == NULL) return;
  if (t->code == ' ' || t->code == '\n') return;
  if (s->ig_state == IG_EXPECT_IFNDEF || s->ig_state == IG_EXPECT_DEFINE
      || s->ig_state == IG_AFTER_ENDIF)
    ig_fail (s);
}

static int pre_skip_if_part_p (c2m_ctx_t c2m_ctx) {
  pre_ctx_t pre_ctx = c2m_ctx->pre_ctx;
  return pre_ctx != NULL && skip_if_part_p;
}

/* It is a token based prerpocessor.
   It is input preprocessor tokens and output is (parser) tokens */

static void add_to_temp_string (c2m_ctx_t c2m_ctx, const char *str) {
  size_t i, len;

  if ((len = VARR_LENGTH (char, temp_string)) != 0
      && VARR_GET (char, temp_string, len - 1) == '\0') {
    VARR_POP (char, temp_string);
  }
  len = strlen (str);
  for (i = 0; i < len; i++) VARR_PUSH (char, temp_string, str[i]);
  VARR_PUSH (char, temp_string, '\0');
}

static int macro_eq (macro_t macro1, macro_t macro2, void *arg MIR_UNUSED) {
  return macro1->id->repr == macro2->id->repr;
}

static htab_hash_t macro_hash (macro_t macro, void *arg MIR_UNUSED) {
  /* id->repr is always uniq_cstr-interned (see new_id_token); pointer
     identity matches macro_eq and avoids strlen + string hash on every
     identifier's macro lookup. */
  uintptr_t p = (uintptr_t) macro->id->repr;
  return (htab_hash_t) mir_hash64 ((uint64_t) p, 0x42);
}

static macro_t new_macro (c2m_ctx_t c2m_ctx, token_t id, VARR (token_t) * params,
                          VARR (token_t) * replacement);

static void new_std_macro (c2m_ctx_t c2m_ctx, const char *id_str) {
  new_macro (c2m_ctx, new_id_token (c2m_ctx, no_pos, id_str), NULL, NULL);
}

static void init_macros (c2m_ctx_t c2m_ctx) {
  MIR_alloc_t alloc = c2m_alloc (c2m_ctx);
  pre_ctx_t pre_ctx = c2m_ctx->pre_ctx;
  VARR (token_t) * params;

  VARR_CREATE (macro_t, macros, alloc, 2048);
  HTAB_CREATE (macro_t, macro_tab, alloc, 2048, macro_hash, macro_eq, NULL);
  /* Standard macros : */
  new_std_macro (c2m_ctx, "__DATE__");
  new_std_macro (c2m_ctx, "__TIME__");
  new_std_macro (c2m_ctx, "__FILE__");
  new_std_macro (c2m_ctx, "__LINE__");
  if (!c2m_options->pedantic_p) {
    VARR_CREATE (token_t, params, alloc, 1);
    VARR_PUSH (token_t, params, new_id_token (c2m_ctx, no_pos, "$"));
    new_macro (c2m_ctx, new_id_token (c2m_ctx, no_pos, "__has_include"), params, NULL);
    VARR_CREATE (token_t, params, alloc, 1);
    VARR_PUSH (token_t, params, new_id_token (c2m_ctx, no_pos, "$"));
    new_macro (c2m_ctx, new_id_token (c2m_ctx, no_pos, "__has_builtin"), params, NULL);
  }
}

static macro_t new_macro (c2m_ctx_t c2m_ctx, token_t id, VARR (token_t) * params,
                          VARR (token_t) * replacement) {
  pre_ctx_t pre_ctx = c2m_ctx->pre_ctx;
  macro_t tab_m, m = malloc (sizeof (struct macro));

  m->id = id;
  m->params = params;
  m->replacement = replacement;
  m->ignore_p = FALSE;
  assert (!HTAB_DO (macro_t, macro_tab, m, HTAB_FIND, tab_m));
  HTAB_DO (macro_t, macro_tab, m, HTAB_INSERT, tab_m);
  VARR_PUSH (macro_t, macros, m);
  return m;
}

static void finish_macros (c2m_ctx_t c2m_ctx) {
  pre_ctx_t pre_ctx = c2m_ctx->pre_ctx;
  if (macros != NULL) {
    while (VARR_LENGTH (macro_t, macros) != 0) {
      macro_t m = VARR_POP (macro_t, macros);

      if (m->params != NULL) VARR_DESTROY (token_t, m->params);
      if (m->replacement != NULL) VARR_DESTROY (token_t, m->replacement);
      free (m);
    }
    VARR_DESTROY (macro_t, macros);
  }
  if (macro_tab != NULL) HTAB_DESTROY (macro_t, macro_tab);
}

static macro_call_t new_macro_call (MIR_alloc_t alloc, macro_t m, pos_t pos) {
  macro_call_t mc = malloc (sizeof (struct macro_call));

  mc->macro = m;
  mc->pos = pos;
  mc->repl_pos = 0;
  mc->args = NULL;
  VARR_CREATE (token_t, mc->repl_buffer, alloc, 64);
  return mc;
}

static void free_macro_call (macro_call_t mc) {
  VARR_DESTROY (token_t, mc->repl_buffer);
  if (mc->args != NULL) {
    while (VARR_LENGTH (token_arr_t, mc->args) != 0) {
      VARR (token_t) *arg = VARR_POP (token_arr_t, mc->args);
      VARR_DESTROY (token_t, arg);
    }
    VARR_DESTROY (token_arr_t, mc->args);
  }
  free (mc);
}

static ifstate_t new_ifstate (int skip_p, int true_p, int else_p, pos_t if_pos) {
  ifstate_t ifstate = malloc (sizeof (struct ifstate));

  ifstate->skip_p = skip_p;
  ifstate->true_p = true_p;
  ifstate->else_p = else_p;
  ifstate->if_pos = if_pos;
  return ifstate;
}

static void pop_ifstate (c2m_ctx_t c2m_ctx) {
  pre_ctx_t pre_ctx = c2m_ctx->pre_ctx;
  ifstate_t ifstate = VARR_POP (ifstate_t, ifs);
  free (ifstate);
}

static void pre_init (c2m_ctx_t c2m_ctx) {
  MIR_alloc_t alloc = c2m_alloc (c2m_ctx);
  pre_ctx_t pre_ctx;
  time_t t, time_loc;
  struct tm *tm, tm_loc MIR_UNUSED;

  c2m_ctx->pre_ctx = pre_ctx = c2mir_calloc (c2m_ctx, sizeof (struct pre_ctx));
  no_out_p = skip_if_part_p = FALSE;
  t = time (&time_loc);
#if defined(_WIN32)
  tm = localtime (&t);
#else
  tm = localtime_r (&t, &tm_loc);
#endif
  if (tm == NULL) {
    strcpy (date_str_repr, "\"Unknown date\"");
    strcpy (time_str_repr, "\"Unknown time\"");
  } else {
    strftime (date_str_repr, sizeof (date_str), "\"%b %d %Y\"", tm);
    strftime (time_str_repr, sizeof (time_str), "\"%H:%M:%S\"", tm);
  }
  strcpy (date_str, date_str_repr + 1);
  date_str[strlen (date_str) - 1] = '\0';
  strcpy (time_str, time_str_repr + 1);
  time_str[strlen (time_str) - 1] = '\0';
  VARR_CREATE (char_ptr_t, once_include_files, alloc, 64);
  VARR_CREATE (token_t, temp_tokens, alloc, 128);
  VARR_CREATE (token_t, output_buffer, alloc, 2048);
  HTAB_CREATE (include_guard_t, include_guard_tab, alloc, 256, include_guard_hash, include_guard_eq,
               NULL);
  HTAB_CREATE (path_exist_t, path_exist_tab, alloc, 1024, path_exist_hash, path_exist_eq, NULL);
  HTAB_CREATE (include_resolve_t, include_resolve_tab, alloc, 512, include_resolve_hash,
               include_resolve_eq, NULL);
  init_macros (c2m_ctx);
  VARR_CREATE (ifstate_t, ifs, alloc, 512);
  VARR_CREATE (macro_call_t, macro_call_stack, alloc, 512);
}

static void pre_finish (c2m_ctx_t c2m_ctx) {
  pre_ctx_t pre_ctx;

  if (c2m_ctx == NULL || (pre_ctx = c2m_ctx->pre_ctx) == NULL) return;
  if (once_include_files != NULL) VARR_DESTROY (char_ptr_t, once_include_files);
  if (temp_tokens != NULL) VARR_DESTROY (token_t, temp_tokens);
  if (output_buffer != NULL) VARR_DESTROY (token_t, output_buffer);
  if (include_guard_tab != NULL) HTAB_DESTROY (include_guard_t, include_guard_tab);
  if (path_exist_tab != NULL) HTAB_DESTROY (path_exist_t, path_exist_tab);
  if (include_resolve_tab != NULL) HTAB_DESTROY (include_resolve_t, include_resolve_tab);
  finish_macros (c2m_ctx);
  if (ifs != NULL) {
    while (VARR_LENGTH (ifstate_t, ifs) != 0) pop_ifstate (c2m_ctx);
    VARR_DESTROY (ifstate_t, ifs);
  }
  if (macro_call_stack != NULL) {
    while (VARR_LENGTH (macro_call_t, macro_call_stack) != 0)
      free_macro_call (VARR_POP (macro_call_t, macro_call_stack));
    VARR_DESTROY (macro_call_t, macro_call_stack);
  }
  reg_free (c2m_ctx, c2m_ctx->pre_ctx);
}

static void add_include_stream (c2m_ctx_t c2m_ctx, const char *fname, const char *content,
                                pos_t err_pos) {
  pre_ctx_t pre_ctx = c2m_ctx->pre_ctx;
  FILE *f;
  include_guard_t g, tab_g;

  for (size_t i = 0; i < VARR_LENGTH (char_ptr_t, once_include_files); i++)
    if (strcmp (fname, VARR_GET (char_ptr_t, once_include_files, i)) == 0) return;
  assert (fname != NULL);
  /* MultipleIncludeOpt: if this path was previously a simple
     #ifndef G / #define G / ... / #endif header and G is still defined,
     skip re-opening and re-tokenizing the file. */
  g.fname = fname;
  g.guard = NULL;
  if (include_guard_tab != NULL
      && HTAB_DO (include_guard_t, include_guard_tab, g, HTAB_FIND, tab_g)
      && macro_name_defined_p (c2m_ctx, tab_g.guard))
    return;
  if (content == NULL && (f = fopen (fname, "rb")) == NULL) {
    if (c2m_options->message_file != NULL)
      error (c2m_ctx, err_pos, "error in opening file %s", fname);
    longjmp (c2m_ctx->env, 1);  // ???
  }
  if (content == NULL)
    add_stream (c2m_ctx, f, fname, NULL);
  else
    add_string_stream (c2m_ctx, fname, content);
  cs->ifs_length_at_stream_start = (int) VARR_LENGTH (ifstate_t, ifs);
  cs->ig_state = IG_EXPECT_IFNDEF;
  cs->ig_macro = NULL;
}

static void skip_nl (c2m_ctx_t c2m_ctx, token_t t,
                     VARR (token_t) * buffer) { /* skip until new line */
  if (t == NULL) t = get_next_pptoken (c2m_ctx);
  for (; t->code != '\n' && t->code != T_EOU; t = get_next_pptoken (c2m_ctx))  // ??>
    if (buffer != NULL) VARR_PUSH (token_t, buffer, t);
  unget_next_pptoken (c2m_ctx, t);
}

static const char *varg = "__VA_ARGS__";

static int find_param (VARR (token_t) * params, const char *name) {
  size_t len = VARR_LENGTH (token_t, params);
  token_t param;

  if (strcmp (name, varg) == 0 && len != 0 && VARR_LAST (token_t, params)->code == T_DOTS)
    return (int) len - 1;
  for (size_t i = 0; i < len; i++) {
    param = VARR_GET (token_t, params, i);
    if (strcmp (param->repr, name) == 0) return (int) i;
  }
  return -1;
}

static int params_eq_p (VARR (token_t) * params1, VARR (token_t) * params2) {
  token_t param1, param2;

  if (params1 == NULL || params2 == NULL) return params1 == params2;
  if (VARR_LENGTH (token_t, params1) != VARR_LENGTH (token_t, params2)) return FALSE;
  for (size_t i = 0; i < VARR_LENGTH (token_t, params1); i++) {
    param1 = VARR_GET (token_t, params1, i);
    param2 = VARR_GET (token_t, params2, i);
    if (strcmp (param1->repr, param2->repr) != 0) return FALSE;
  }
  return TRUE;
}

static int replacement_eq_p (VARR (token_t) * r1, VARR (token_t) * r2) {
  token_t el1, el2;

  if (VARR_LENGTH (token_t, r1) != VARR_LENGTH (token_t, r2)) return FALSE;
  for (size_t i = 0; i < VARR_LENGTH (token_t, r1); i++) {
    el1 = VARR_GET (token_t, r1, i);
    el2 = VARR_GET (token_t, r2, i);

    if (el1->code == ' ' && el2->code == ' ') continue;
    if (el1->node_code != el2->node_code) return FALSE;
    if (strcmp (el1->repr, el2->repr) != 0) return FALSE;
  }
  return TRUE;
}

static void define (c2m_ctx_t c2m_ctx) {
  MIR_alloc_t alloc = c2m_alloc (c2m_ctx);
  pre_ctx_t pre_ctx = c2m_ctx->pre_ctx;
  VARR (token_t) * repl, *params;
  token_t id, t;
  const char *name;
  macro_t m;
  struct macro macro_struct;

  t = get_next_pptoken (c2m_ctx);                      // ???
  if (t->code == ' ') t = get_next_pptoken (c2m_ctx);  // ??
  if (t->code != T_ID) {
    error (c2m_ctx, t->pos, "no ident after #define: %s", t->repr);
    skip_nl (c2m_ctx, t, NULL);
    return;
  }
  id = t;
  t = get_next_pptoken (c2m_ctx);
  VARR_CREATE (token_t, repl, alloc, 64);
  params = NULL;
  if (t->code == '(') {
    VARR_CREATE (token_t, params, alloc, 16);
    t = get_next_pptoken (c2m_ctx); /* skip '(' */
    if (t->code == ' ') t = get_next_pptoken (c2m_ctx);
    if (t->code != ')') {
      for (;;) {
        if (t->code == ' ') t = get_next_pptoken (c2m_ctx);
        if (t->code == T_ID) {
          if (find_param (params, t->repr) >= 0)
            error (c2m_ctx, t->pos, "repeated macro parameter %s", t->repr);
          if (params != NULL) VARR_PUSH (token_t, params, t);
        } else if (t->code == T_DOTS) {
          if (params != NULL) VARR_PUSH (token_t, params, t);
        } else {
          error (c2m_ctx, t->pos, "macro parameter is expected");
          break;
        }
        t = get_next_pptoken (c2m_ctx);
        if (t->code == ' ') t = get_next_pptoken (c2m_ctx);
        if (t->code == ')') break;
        if (VARR_LAST (token_t, params)->code == T_DOTS) {
          error (c2m_ctx, t->pos, "... is not the last parameter");
          break;
        }
        if (t->code == T_DOTS) continue;
        if (t->code != ',') {
          error (c2m_ctx, t->pos, "missed ,");
          continue;
        }
        t = get_next_pptoken (c2m_ctx);
      }
    }
    for (; t->code != '\n' && t->code != ')';) t = get_next_pptoken (c2m_ctx);
    if (t->code == ')') t = get_next_pptoken (c2m_ctx);
  }
  if (t->code == ' ') t = get_next_pptoken (c2m_ctx);
  for (; t->code != '\n'; t = get_next_pptoken (c2m_ctx)) {
    if (t->code == T_DBLNO) {
      if (VARR_LENGTH (token_t, repl) == 0) {
        error (c2m_ctx, t->pos, "## at the beginning of a macro expansion");
        continue;
      }
      t->code = T_RDBLNO;
    }
    if (repl != NULL) VARR_PUSH (token_t, repl, t);
  }
  unget_next_pptoken (c2m_ctx, t);
  if (VARR_LENGTH (token_t, repl) != 0 && (t = VARR_LAST (token_t, repl))->code == T_RDBLNO) {
    VARR_POP (token_t, repl);
    error (c2m_ctx, t->pos, "## at the end of a macro expansion");
  }
  name = id->repr;
  macro_struct.id = id;
  {
    int object_like_p = (params == NULL);

    if (!HTAB_DO (macro_t, macro_tab, &macro_struct, HTAB_FIND, m)) {
      if (strcmp (name, "defined") == 0) {
        error (c2m_ctx, id->pos, "macro definition of %s", name);
      } else {
        new_macro (c2m_ctx, id, params, repl);
        params = NULL;
      }
    } else if (m->replacement == NULL) {
      error (c2m_ctx, id->pos, "standard macro %s redefinition", name);
    } else {
      if (!params_eq_p (m->params, params) || !replacement_eq_p (m->replacement, repl)) {
        if (c2m_options->pedantic_p) {
          error (c2m_ctx, id->pos, "different macro redefinition of %s", name);
          error (c2m_ctx, m->id->pos, "previous definition of %s", m->id->repr);
        } else {
          VARR (token_t) * temp;
          warning (c2m_ctx, id->pos, "different macro redefinition of %s", name);
          warning (c2m_ctx, m->id->pos, "previous definition of %s", m->id->repr);
          SWAP (m->params, params, temp);
          SWAP (m->replacement, repl, temp);
        }
      }
      VARR_DESTROY (token_t, repl);
    }
    if (params != NULL) VARR_DESTROY (token_t, params);
    /* Include-guard: after #ifndef G, next directive must be object-like #define G
       while still inside that #ifndef (if-depth == stream_start + 1). */
    if (cs != NULL && cs->ig_state == IG_EXPECT_DEFINE && cs->ig_macro == id->repr && object_like_p
        && (int) VARR_LENGTH (ifstate_t, ifs) == cs->ifs_length_at_stream_start + 1) {
      cs->ig_state = IG_IN_BODY;
    } else if (cs != NULL
               && (cs->ig_state == IG_EXPECT_IFNDEF || cs->ig_state == IG_EXPECT_DEFINE)) {
      ig_fail (cs);
    }
  }
}

#ifdef C2MIR_PREPRO_DEBUG
static void print_output_buffer (c2m_ctx_t c2m_ctx) {
  pre_ctx_t pre_ctx = c2m_ctx->pre_ctx;
  fprintf (stderr, "output buffer:");
  for (size_t i = 0; i < (int) VARR_LENGTH (token_t, output_buffer); i++) {
    fprintf (stderr, " <%s>", get_token_str (VARR_GET (token_t, output_buffer, i)));
  }
  fprintf (stderr, "\n");
}
#endif

static void push_back (c2m_ctx_t c2m_ctx, VARR (token_t) * tokens) {
#ifdef C2MIR_PREPRO_DEBUG
  pre_ctx_t pre_ctx = c2m_ctx->pre_ctx;
  fprintf (stderr,
           "# push back (macro call depth %d):", VARR_LENGTH (macro_call_t, macro_call_stack));
#endif
  for (int i = (int) VARR_LENGTH (token_t, tokens) - 1; i >= 0; i--) {
#ifdef C2MIR_PREPRO_DEBUG
    fprintf (stderr, " <%s>", get_token_str (VARR_GET (token_t, tokens, i)));
#endif
    unget_next_pptoken (c2m_ctx, VARR_GET (token_t, tokens, i));
  }
#ifdef C2MIR_PREPRO_DEBUG
  fprintf (stderr, "\n");
  print_output_buffer (c2m_ctx);
#endif
}

static void copy_and_push_back (c2m_ctx_t c2m_ctx, VARR (token_t) * tokens, pos_t pos) {
#ifdef C2MIR_PREPRO_DEBUG
  pre_ctx_t pre_ctx = c2m_ctx->pre_ctx;
  fprintf (stderr, "# copy & push back (macro call depth %d):",
           VARR_LENGTH (macro_call_t, macro_call_stack));
#endif
  for (int i = (int) VARR_LENGTH (token_t, tokens) - 1; i >= 0; i--) {
#ifdef C2MIR_PREPRO_DEBUG
    fprintf (stderr, " <%s>", get_token_str (VARR_GET (token_t, tokens, i)));
#endif
    unget_next_pptoken (c2m_ctx, copy_token (c2m_ctx, VARR_GET (token_t, tokens, i), pos));
  }
#ifdef C2MIR_PREPRO_DEBUG
  fprintf (stderr, "\n");
  print_output_buffer (c2m_ctx);
#endif
}

/* Cheap readability probe (no fopen).  Cached via path_exist_tab when a
   c2m_ctx is available — see file_found_cached. */
static int path_readable_p (const char *name) {
#if defined(_WIN32)
  return _access (name, 4 /* R_OK */) == 0;
#else
  return access (name, R_OK) == 0;
#endif
}

static int file_found_p (const char *name) { return path_readable_p (name); }

/* Cached existence check.  FIND keys may be a temporary path (e.g.
   temp_string from get_full_name); on INSERT we copy the path into
   reg_memory so we do not pollute uniq_cstr with every failed probe.
   Misses and hits are both cached — include search fails far more often
   than it succeeds. */
static int file_found_cached (c2m_ctx_t c2m_ctx, const char *path) {
  pre_ctx_t pre_ctx = c2m_ctx->pre_ctx;
  path_exist_t key, el;
  size_t len;
  char *copy;

  if (path == NULL || path[0] == '\0') return FALSE;
  if (path_exist_tab == NULL) return path_readable_p (path);
  key.path = path;
  key.found = 0;
  if (HTAB_DO (path_exist_t, path_exist_tab, key, HTAB_FIND, el)) return el.found;
  key.found = path_readable_p (path) ? 1 : 0;
  len = strlen (path) + 1;
  copy = reg_malloc (c2m_ctx, len);
  memcpy (copy, path, len);
  key.path = copy;
  HTAB_DO (path_exist_t, path_exist_tab, key, HTAB_INSERT, el);
  return key.found;
}

static const char *get_full_name (c2m_ctx_t c2m_ctx, const char *base, const char *name,
                                  int dir_base_p) {
  const char *str, *last, *last2, *slash = "/", *slash2 = NULL;
  size_t len;

  VARR_TRUNC (char, temp_string, 0);
  if (base == NULL || *base == '\0') {
    assert (name != NULL && name[0] != '\0');
    return name;
  }
#ifdef _WIN32
  slash2 = "\\";
#endif
  if (dir_base_p) {
    len = strlen (base);
    assert (len > 0);
    add_to_temp_string (c2m_ctx, base);
    if (base[len - 1] != slash[0]) add_to_temp_string (c2m_ctx, slash);
  } else {
    last = strrchr (base, slash[0]);
    last2 = slash2 != NULL ? strrchr (base, slash2[0]) : NULL;
    if (last2 != NULL && (last == NULL || last2 > last)) last = last2;
    if (last != NULL) {
      for (str = base; str <= last; str++) VARR_PUSH (char, temp_string, *str);
      VARR_PUSH (char, temp_string, '\0');
    } else {
      add_to_temp_string (c2m_ctx, ".");
      add_to_temp_string (c2m_ctx, slash);
    }
  }
  add_to_temp_string (c2m_ctx, name);
  return VARR_ADDR (char, temp_string);
}

/* Interned directory of `fname` (including trailing slash), or NULL. */
static const char *include_dir_key (c2m_ctx_t c2m_ctx, const char *fname) {
  const char *last, *last2, *slash = "/", *slash2 = NULL;
  size_t n;

  if (fname == NULL || fname[0] == '\0') return NULL;
#ifdef _WIN32
  slash2 = "\\";
#endif
  last = strrchr (fname, slash[0]);
  last2 = slash2 != NULL ? strrchr (fname, slash2[0]) : NULL;
  if (last2 != NULL && (last == NULL || last2 > last)) last = last2;
  if (last == NULL) return uniq_cstr (c2m_ctx, "./").s;
  n = (size_t) (last - fname + 1);
  VARR_TRUNC (char, temp_string, 0);
  for (size_t i = 0; i < n; i++) VARR_PUSH (char, temp_string, fname[i]);
  VARR_PUSH (char, temp_string, '\0');
  return uniq_cstr (c2m_ctx, VARR_ADDR (char, temp_string)).s;
}

/* Clang-style framework include: <Foo/bar.h> → {Fdir}/Foo.framework/Headers/bar.h */
static const char *framework_resolve (c2m_ctx_t c2m_ctx, const char *name) {
  const char *slash, *rest;
  size_t fw_len, i;

  if (name == NULL || name[0] == '\0' || name[0] == '/' || fw_dir_list == NULL) return NULL;
  slash = strchr (name, '/');
  if (slash == NULL || slash == name || slash[1] == '\0') return NULL;
  fw_len = (size_t) (slash - name);
  rest = slash + 1;
  for (i = 0; fw_dir_list[i] != NULL; i++) {
    const char *dir = fw_dir_list[i];
    size_t dlen;
    if (dir == NULL || dir[0] == '\0') continue;
    dlen = strlen (dir);
    VARR_TRUNC (char, temp_string, 0);
    add_to_temp_string (c2m_ctx, dir);
    if (dir[dlen - 1] != '/') add_to_temp_string (c2m_ctx, "/");
    {
      size_t k;
      if (VARR_LENGTH (char, temp_string) != 0
          && VARR_LAST (char, temp_string) == '\0')
        VARR_POP (char, temp_string);
      for (k = 0; k < fw_len; k++) VARR_PUSH (char, temp_string, name[k]);
      VARR_PUSH (char, temp_string, '\0');
    }
    add_to_temp_string (c2m_ctx, ".framework/Headers/");
    add_to_temp_string (c2m_ctx, rest);
    if (file_found_cached (c2m_ctx, VARR_ADDR (char, temp_string)))
      return uniq_cstr (c2m_ctx, VARR_ADDR (char, temp_string)).s;
  }
  return NULL;
}

static void include_resolve_store (c2m_ctx_t c2m_ctx, const char *name, const char *from_dir,
                                   int quote_p, const char *resolved, const char *content) {
  pre_ctx_t pre_ctx = c2m_ctx->pre_ctx;
  include_resolve_t key, el;

  if (include_resolve_tab == NULL || name == NULL || resolved == NULL) return;
  key.name = name;
  key.from_dir = from_dir;
  key.quote_p = quote_p;
  key.resolved = resolved;
  key.content = content;
  if (HTAB_DO (include_resolve_t, include_resolve_tab, key, HTAB_FIND, el)) return;
  HTAB_DO (include_resolve_t, include_resolve_tab, key, HTAB_INSERT, el);
}

static const char *get_include_fname (c2m_ctx_t c2m_ctx, token_t t, const char **content) {
  pre_ctx_t pre_ctx = c2m_ctx->pre_ctx;
  const char *fullname, *name, *iname, *from_dir;
  int quote_p;
  include_resolve_t rkey, rel;

  *content = NULL;
  assert (t->code == T_STR || t->code == T_HEADER);
  name = t->node->u.s.s;
  quote_p = (t->repr[0] == '"');
  iname = uniq_cstr (c2m_ctx, name).s;
  from_dir = (quote_p && cs != NULL) ? include_dir_key (c2m_ctx, cs->fname) : NULL;

  /* Fast path: same #include spelling from the same context already resolved. */
  if (include_resolve_tab != NULL && name[0] != '/') {
    rkey.name = iname;
    rkey.from_dir = from_dir;
    rkey.quote_p = quote_p;
    rkey.resolved = NULL;
    rkey.content = NULL;
    if (HTAB_DO (include_resolve_t, include_resolve_tab, rkey, HTAB_FIND, rel)) {
      *content = rel.content;
      return rel.resolved;
    }
  }

  if (name[0] != '/') {
    if (quote_p) {
      /* Search relative to the current source dir */
      if (cs->fname != NULL) {
        fullname = get_full_name (c2m_ctx, cs->fname, name, FALSE);
        if (file_found_cached (c2m_ctx, fullname)) {
          const char *res = uniq_cstr (c2m_ctx, fullname).s;
          include_resolve_store (c2m_ctx, iname, from_dir, quote_p, res, NULL);
          return res;
        }
      }
      for (size_t i = 0; header_dirs[i] != NULL; i++) {
        fullname = get_full_name (c2m_ctx, header_dirs[i], name, TRUE);
        if (file_found_cached (c2m_ctx, fullname)) {
          const char *res = uniq_cstr (c2m_ctx, fullname).s;
          include_resolve_store (c2m_ctx, iname, from_dir, quote_p, res, NULL);
          return res;
        }
      }
    }
    for (size_t i = 0; i < sizeof (standard_includes) / sizeof (string_include_t); i++)
      if (standard_includes[i].name != NULL && strcmp (name, standard_includes[i].name) == 0) {
        *content = standard_includes[i].content;
        /* Builtin headers: resolve key is name itself (angle or quote). */
        include_resolve_store (c2m_ctx, iname, from_dir, quote_p, iname, *content);
        return iname;
      }
    for (size_t i = 0; system_header_dirs[i] != NULL; i++) {
      fullname = get_full_name (c2m_ctx, system_header_dirs[i], name, TRUE);
      if (file_found_cached (c2m_ctx, fullname)) {
        const char *res = uniq_cstr (c2m_ctx, fullname).s;
        include_resolve_store (c2m_ctx, iname, from_dir, quote_p, res, NULL);
        return res;
      }
    }
    {
      const char *fw = framework_resolve (c2m_ctx, name);
      if (fw != NULL) {
        include_resolve_store (c2m_ctx, iname, from_dir, quote_p, fw, NULL);
        return fw;
      }
    }
  }
  /* Absolute path or unresolved: return spelling; caller may open and fail. */
  return name[0] == '/' ? uniq_cstr (c2m_ctx, name).s : name;
}

static int digits_p (const char *str) {
  while ('0' <= *str && *str <= '9') str++;
  return *str == '\0';
}

static pos_t check_line_directive_args (c2m_ctx_t c2m_ctx, VARR (token_t) * buffer) {
  size_t i, len = VARR_LENGTH (token_t, buffer);
  token_t *buffer_arr = VARR_ADDR (token_t, buffer);
  const char *fname;
  pos_t pos;
  int lno;
  unsigned long long l;

  if (len == 0) return no_pos;
  i = buffer_arr[0]->code == ' ' ? 1 : 0;
  fname = buffer_arr[i]->pos.fname;
  if (i >= len || buffer_arr[i]->code != T_NUMBER) return no_pos;
  if (!digits_p (buffer_arr[i]->repr)) return no_pos;
  errno = 0;
  l = strtoll (buffer_arr[i]->repr, NULL, 10);
  lno = (int) l;
  if (errno || l > ((1ul << 31) - 1))
    error (c2m_ctx, buffer_arr[i]->pos, "#line with too big value: %s", buffer_arr[i]->repr);
  i++;
  if (i < len && buffer_arr[i]->code == ' ') i++;
  if (i < len && buffer_arr[i]->code == T_STR) {
    fname = buffer_arr[i]->node->u.s.s;
    i++;
  }
  if (i == len) {
    pos.fname = fname;
    pos.lno = lno;
    pos.ln_pos = 0;
    return pos;
  }
  return no_pos;
}

static void check_pragma (c2m_ctx_t c2m_ctx, token_t t, VARR (token_t) * tokens) {
  token_t *tokens_arr = VARR_ADDR (token_t, tokens);
  size_t i, tokens_len = VARR_LENGTH (token_t, tokens);

  i = 0;
  if (i < tokens_len && tokens_arr[i]->code == ' ') i++;
  if (i + 1 == tokens_len && tokens_arr[i]->code == T_ID
      && strcmp (tokens_arr[i]->repr, "once") == 0) {
    pre_ctx_t pre_ctx = c2m_ctx->pre_ctx;
    VARR_PUSH (char_ptr_t, once_include_files, cs->fname);
    return;
  }
  /* Darwin fcntl.h / netinet/in.h: `#pragma pack(4)` / `#pragma pack()`.
     Layout is ignored; accept so SDK headers are not a warning storm. */
  if (i < tokens_len && tokens_arr[i]->code == T_ID
      && strcmp (tokens_arr[i]->repr, "pack") == 0)
    return;
  if (i >= tokens_len || tokens_arr[i]->code != T_ID || strcmp (tokens_arr[i]->repr, "STDC") != 0) {
    warning (c2m_ctx, t->pos, "unknown pragma");
    return;
  }
  i++;
  if (i < tokens_len && tokens_arr[i]->code == ' ') i++;
  if (i >= tokens_len || tokens_arr[i]->code != T_ID) {
    error (c2m_ctx, t->pos, "wrong STDC pragma");
    return;
  }
  if (strcmp (tokens_arr[i]->repr, "FP_CONTRACT") != 0
      && strcmp (tokens_arr[i]->repr, "FENV_ACCESS") != 0
      && strcmp (tokens_arr[i]->repr, "CX_LIMITED_RANGE") != 0) {
    error (c2m_ctx, t->pos, "unknown STDC pragma %s", tokens_arr[i]->repr);
    return;
  }
  i++;
  if (i < tokens_len && tokens_arr[i]->code == ' ') i++;
  if (i >= tokens_len || tokens_arr[i]->code != T_ID) {
    error (c2m_ctx, t->pos, "wrong STDC pragma value");
    return;
  }
  if (strcmp (tokens_arr[i]->repr, "ON") != 0 && strcmp (tokens_arr[i]->repr, "OFF") != 0
      && strcmp (tokens_arr[i]->repr, "DEFAULT") != 0) {
    error (c2m_ctx, t->pos, "unknown STDC pragma value", tokens_arr[i]->repr);
    return;
  }
  i++;
  if (i < tokens_len && (tokens_arr[i]->code == ' ' || tokens_arr[i]->code == '\n')) i++;
  if (i < tokens_len) error (c2m_ctx, t->pos, "garbage at STDC pragma end");
}

static void pop_macro_call (c2m_ctx_t c2m_ctx) {
  pre_ctx_t pre_ctx = c2m_ctx->pre_ctx;
  macro_call_t mc;

  mc = VARR_POP (macro_call_t, macro_call_stack);
#ifdef C2MIR_PREPRO_DEBUG
  fprintf (stderr, "finish call of macro %s\n", mc->macro->id->repr);
#endif
  mc->macro->ignore_p = FALSE;
  free_macro_call (mc);
}

static void find_args (c2m_ctx_t c2m_ctx, macro_call_t mc) { /* we have just read a parenthesis */
  MIR_alloc_t alloc = c2m_alloc (c2m_ctx);
  macro_t m;
  token_t t;
  int va_p, level = 0;
  size_t params_len;
  VARR (token_arr_t) * args;
  VARR (token_t) * arg, *temp_arr;

  m = mc->macro;
  VARR_CREATE (token_arr_t, args, alloc, 16);
  VARR_CREATE (token_t, arg, alloc, 16);
  params_len = VARR_LENGTH (token_t, m->params);
  va_p = params_len == 1 && VARR_GET (token_t, m->params, 0)->code == T_DOTS;
#ifdef C2MIR_PREPRO_DEBUG
  fprintf (stderr, "# finding args of macro %s call:\n#    arg 0:", m->id->repr);
#endif
  for (int newln_p = FALSE;; newln_p = t->code == '\n') {
    t = get_next_pptoken (c2m_ctx);
#ifdef C2MIR_PREPRO_DEBUG
    fprintf (stderr, " <%s>%s", get_token_str (t), t->processed_p ? "*" : "");
#endif
    if (t->code == T_EOR) {
      t = get_next_pptoken (c2m_ctx);
#ifdef C2MIR_PREPRO_DEBUG
      fprintf (stderr, " <%s>", get_token_str (t), t->processed_p ? "*" : "");
#endif
      pop_macro_call (c2m_ctx);
    }
    if (t->code == T_EOFILE || t->code == T_EOU || t->code == T_EOR || t->code == T_BOA
        || t->code == T_EOA || (newln_p && t->code == '#'))
      break;
    if (level == 0 && t->code == ')') break;
    if (level == 0 && !va_p && t->code == ',') {
      VARR_PUSH (token_arr_t, args, arg);
#ifdef C2MIR_PREPRO_DEBUG
      fprintf (stderr, "\n#    arg %d:", VARR_LENGTH (token_arr_t, args));
#endif
      VARR_CREATE (token_t, arg, alloc, 16);
      if (VARR_LENGTH (token_arr_t, args) == params_len - 1
          && strcmp (VARR_GET (token_t, m->params, params_len - 1)->repr, "...") == 0)
        va_p = 1;
    } else {
      if (arg != NULL) VARR_PUSH (token_t, arg, t);
      if (t->code == ')')
        level--;
      else if (t->code == '(')
        level++;
    }
  }
#ifdef C2MIR_PREPRO_DEBUG
  fprintf (stderr, "\n");
#endif
  if (t->code != ')') {
    error (c2m_ctx, t->pos, "unfinished call of macro %s", m->id->repr);
#ifdef C2MIR_PREPRO_DEBUG
    fprintf (stderr, "# push back <%s>%s\n", get_token_str (t), t->processed_p ? "*" : "");
#endif
    unget_next_pptoken (c2m_ctx, t);
  }
  VARR_PUSH (token_arr_t, args, arg);
  if (params_len == 0 && VARR_LENGTH (token_arr_t, args) == 1) {
    token_arr_t arr = VARR_GET (token_arr_t, args, 0);

    if (VARR_LENGTH (token_t, arr) == 0
        || (VARR_LENGTH (token_t, arr) == 1 && VARR_GET (token_t, arr, 0)->code == ' ')) {
      temp_arr = VARR_POP (token_arr_t, args);
      VARR_DESTROY (token_t, temp_arr);
      mc->args = args;
      return;
    }
  }
  if (VARR_LENGTH (token_arr_t, args) > params_len) {
    arg = VARR_GET (token_arr_t, args, params_len);
    if (VARR_LENGTH (token_t, arg) != 0) t = VARR_GET (token_t, arg, 0);
    while (VARR_LENGTH (token_arr_t, args) > params_len) {
      temp_arr = VARR_POP (token_arr_t, args);
      VARR_DESTROY (token_t, temp_arr);
    }
    error (c2m_ctx, t->pos, "too many args for call of macro %s", m->id->repr);
  } else if (VARR_LENGTH (token_arr_t, args) == params_len - 1 && params_len > 0
             && VARR_GET (token_t, m->params, params_len - 1)->code == T_DOTS) {
    /* GCC: allow empty __VA_ARGS__ when the only missing arg is ... */
    VARR_CREATE (token_t, arg, alloc, 16);
    VARR_PUSH (token_arr_t, args, arg);
  } else if (VARR_LENGTH (token_arr_t, args) < params_len) {
    for (; VARR_LENGTH (token_arr_t, args) < params_len;) {
      VARR_CREATE (token_t, arg, alloc, 16);
      VARR_PUSH (token_arr_t, args, arg);
    }
    error (c2m_ctx, t->pos, "not enough args for call of macro %s", m->id->repr);
  }
  mc->args = args;
}

static token_t token_concat (c2m_ctx_t c2m_ctx, token_t t1, token_t t2) {
  token_t t, next;

  VARR_TRUNC (char, temp_string, 0);
  add_to_temp_string (c2m_ctx, t1->repr);
  add_to_temp_string (c2m_ctx, t2->repr);
  reverse (temp_string);
  set_string_stream (c2m_ctx, VARR_ADDR (char, temp_string), t1->pos, NULL);
  t = get_next_pptoken (c2m_ctx);
  next = get_next_pptoken (c2m_ctx);
  /* GCC: if ## does not form a single valid token (e.g. ,##_rc → "," + "_rc"),
     return NULL so do_concat just drops the ## (paste-failed but kept tokens). */
  if (next->code != T_EOU && next->code != T_EOFILE) {
    t = NULL;
    next = get_next_pptoken (c2m_ctx);
  }
  /* T_EOU means there is no more input at all; treat it like EOF for paste
     scanning so we don't spin reading past the end of the translation unit. */
  if (next->code != T_EOFILE && next->code != T_EOU) {
    error (c2m_ctx, t1->pos, "wrong result of ##: %s", reverse (temp_string));
    remove_string_stream (c2m_ctx);
  }
  return t;
}

static void add_token (VARR (token_t) * to, token_t t) {
  if ((t->code != ' ' && t->code != '\n') || VARR_LENGTH (token_t, to) == 0
      || (VARR_LAST (token_t, to)->code != ' ' && VARR_LAST (token_t, to)->code != '\n')) {
    if (to != NULL) VARR_PUSH (token_t, to, t);
  }
}

static void add_arg_tokens (VARR (token_t) * to, VARR (token_t) * from) {
  int start;

  for (start = (int) VARR_LENGTH (token_t, from) - 1; start >= 0; start--)
    if (VARR_GET (token_t, from, start)->code == T_BOA) break;
  assert (start >= 0);
  for (size_t i = start + 1; i < VARR_LENGTH (token_t, from); i++)
    add_token (to, VARR_GET (token_t, from, i));
  VARR_TRUNC (token_t, from, start);
}

static void add_tokens (VARR (token_t) * to, VARR (token_t) * from) {
  for (size_t i = 0; i < VARR_LENGTH (token_t, from); i++)
    add_token (to, VARR_GET (token_t, from, i));
}

static void del_tokens (VARR (token_t) * tokens, int from, int len) {
  int diff, tokens_len = (int) VARR_LENGTH (token_t, tokens);
  token_t *addr = VARR_ADDR (token_t, tokens);

  if (len < 0) len = tokens_len - from;
  assert (from + len <= tokens_len);
  if ((diff = tokens_len - from - len) > 0)
    memmove (addr + from, addr + from + len, diff * sizeof (token_t));
  VARR_TRUNC (token_t, tokens, tokens_len - len);
}

static VARR (token_t) * do_concat (c2m_ctx_t c2m_ctx, VARR (token_t) * tokens) {
  int i, j, k, empty_j_p, empty_k_p, len = (int) VARR_LENGTH (token_t, tokens);
  token_t t;

  for (i = len - 1; i >= 0; i--)
    if ((t = VARR_GET (token_t, tokens, i))->code == T_RDBLNO) {
      j = i + 1;
      k = i - 1;
      assert (k >= 0 && j < len);
      if (VARR_GET (token_t, tokens, j)->code == ' ' || VARR_GET (token_t, tokens, j)->code == '\n')
        j++;
      if (VARR_GET (token_t, tokens, k)->code == ' ' || VARR_GET (token_t, tokens, k)->code == '\n')
        k--;
      if (k < 0) error (c2m_ctx, t->pos, "## requires token before");
      if (j >= len) error (c2m_ctx, t->pos, "## requires token after");
      assert (k >= 0 && j < len);
      empty_j_p = VARR_GET (token_t, tokens, j)->code == T_PLM;
      empty_k_p = VARR_GET (token_t, tokens, k)->code == T_PLM;
      if (empty_j_p || empty_k_p) {
        if (!empty_j_p)
          j--;
        else if (j + 1 < len
                 && (VARR_GET (token_t, tokens, j + 1)->code == ' '
                     || VARR_GET (token_t, tokens, j + 1)->code == '\n'))
          j++;
        if (!empty_k_p)
          k++;
        else if (k != 0
                 && (VARR_GET (token_t, tokens, k - 1)->code == ' '
                     || VARR_GET (token_t, tokens, k - 1)->code == '\n'))
          k--;
        if (!empty_j_p || !empty_k_p) {
          del_tokens (tokens, k, j - k + 1);
        } else {
          del_tokens (tokens, k, j - k);
          t = new_token (c2m_ctx, t->pos, "", ' ', N_IGNORE);
          VARR_SET (token_t, tokens, k, t);
        }
      } else {
        t = token_concat (c2m_ctx, VARR_GET (token_t, tokens, k), VARR_GET (token_t, tokens, j));
        /* NULL: paste did not form one token — drop ## only (GCC-style). */
        if (t == NULL) {
          del_tokens (tokens, i, 1);
        } else {
          del_tokens (tokens, k + 1, j - k);
          VARR_SET (token_t, tokens, k, t);
        }
      }
      i = k;
      len = (int) VARR_LENGTH (token_t, tokens);
    }
  for (i = len - 1; i >= 0; i--) VARR_GET (token_t, tokens, i)->processed_p = TRUE;
  return tokens;
}

static void process_replacement (c2m_ctx_t c2m_ctx, macro_call_t mc) {
  macro_t m;
  token_t t, *m_repl;
  VARR (token_t) * arg;
  int i, m_repl_len, sharp_pos, copy_p, comma_pos, sharp_sharp_pos;

  m = mc->macro;
  sharp_pos = -1;
  comma_pos = -1;
  sharp_sharp_pos = -1;
  m_repl = VARR_ADDR (token_t, m->replacement);
  m_repl_len = (int) VARR_LENGTH (token_t, m->replacement);
  for (;;) {
    if (mc->repl_pos >= m_repl_len) {
      t = get_next_pptoken (c2m_ctx);
      unget_next_pptoken (c2m_ctx, t);
#ifdef C2MIR_PREPRO_DEBUG
      fprintf (stderr, "# push back <%s>\n", get_token_str (t));
#endif
      unget_next_pptoken (c2m_ctx, new_token (c2m_ctx, t->pos, "", T_EOR, N_IGNORE));
#ifdef C2MIR_PREPRO_DEBUG
      fprintf (stderr, "# push back <EOR>: mc=%lx\n", mc);
#endif
      push_back (c2m_ctx, do_concat (c2m_ctx, mc->repl_buffer));
      m->ignore_p = TRUE;
      return;
    }
    t = m_repl[mc->repl_pos++];
    copy_p = TRUE;
    if (t->code == T_ID) {
      i = find_param (m->params, t->repr);
      if (i >= 0) {
        arg = VARR_GET (token_arr_t, mc->args, i);
        if (sharp_pos >= 0) {
          del_tokens (mc->repl_buffer, sharp_pos, -1);
          if (VARR_LENGTH (token_t, arg) != 0
              && (VARR_GET (token_t, arg, 0)->code == ' '
                  || VARR_GET (token_t, arg, 0)->code == '\n'))
            del_tokens (arg, 0, 1);
          if (VARR_LENGTH (token_t, arg) != 0
              && (VARR_LAST (token_t, arg)->code == ' ' || VARR_LAST (token_t, arg)->code == '\n'))
            VARR_POP (token_t, arg);
          t = token_stringify (c2m_ctx, mc->macro->id, arg);
          copy_p = FALSE;
        } else if (sharp_sharp_pos >= 0 && comma_pos >= 0 && sharp_sharp_pos > comma_pos
                   && VARR_LENGTH (token_t, m->params) > 0
                   && VARR_LAST (token_t, m->params)->code == T_DOTS
                   && VARR_LENGTH (token_t, arg) == 0) {
          /* GCC: empty ,##__VA_ARGS__ eats the preceding comma. */
          del_tokens (mc->repl_buffer, comma_pos, -1);
          continue;
        } else if ((mc->repl_pos >= 2 && m_repl[mc->repl_pos - 2]->code == T_RDBLNO)
                   || (mc->repl_pos >= 3 && m_repl[mc->repl_pos - 2]->code == ' '
                       && m_repl[mc->repl_pos - 3]->code == T_RDBLNO)
                   || (mc->repl_pos < m_repl_len && m_repl[mc->repl_pos]->code == T_RDBLNO)
                   || (mc->repl_pos + 1 < m_repl_len && m_repl[mc->repl_pos + 1]->code == T_RDBLNO
                       && m_repl[mc->repl_pos]->code == ' ')) {
          if (VARR_LENGTH (token_t, arg) == 0
              || (VARR_LENGTH (token_t, arg) == 1
                  && (VARR_GET (token_t, arg, 0)->code == ' '
                      || VARR_GET (token_t, arg, 0)->code == '\n'))) {
            t = new_token (c2m_ctx, t->pos, "", T_PLM, N_IGNORE);
            copy_p = FALSE;
          } else {
            add_tokens (mc->repl_buffer, arg);
            continue;
          }
        } else {
          unget_next_pptoken (c2m_ctx, new_token (c2m_ctx, t->pos, "", T_EOA, N_IGNORE));
#ifdef C2MIR_PREPRO_DEBUG
          fprintf (stderr, "# push back <EOA> for macro %s call\n", mc->macro->id->repr);
#endif
          copy_and_push_back (c2m_ctx, arg, mc->pos);
          unget_next_pptoken (c2m_ctx, new_token (c2m_ctx, t->pos, "", T_BOA, N_IGNORE));
#ifdef C2MIR_PREPRO_DEBUG
          fprintf (stderr, "# push back <BOA> for macro %s call\n", mc->macro->id->repr);
#endif
          return;
        }
      }
    } else if (t->code == ',') {
      comma_pos = (int) VARR_LENGTH (token_t, mc->repl_buffer);
      sharp_pos = -1;
      sharp_sharp_pos = -1;
    } else if (t->code == T_RDBLNO) {
      sharp_sharp_pos = (int) VARR_LENGTH (token_t, mc->repl_buffer);
      sharp_pos = -1;
    } else if (t->code == '#') {
      sharp_pos = (int) VARR_LENGTH (token_t, mc->repl_buffer);
      sharp_sharp_pos = -1;
    } else if (t->code != ' ') {
      sharp_pos = -1;
      sharp_sharp_pos = -1;
      comma_pos = -1;
    }
    if (copy_p) t = copy_token (c2m_ctx, t, mc->pos);
    add_token (mc->repl_buffer, t);
  }
}

static void prepare_pragma_string (const char *repr, VARR (char) * to) {
  destringify (repr, to);
  reverse (to);
}

static int process_pragma (c2m_ctx_t c2m_ctx, token_t t) {
  pre_ctx_t pre_ctx = c2m_ctx->pre_ctx;
  token_t t1, t2;

  if (strcmp (t->repr, "_Pragma") != 0) return FALSE;
  VARR_TRUNC (token_t, temp_tokens, 0);
  t1 = get_next_pptoken (c2m_ctx);
  if (temp_tokens != NULL) VARR_PUSH (token_t, temp_tokens, t1);
  if (t1->code == ' ' || t1->code == '\n') {
    t1 = get_next_pptoken (c2m_ctx);
    if (temp_tokens != NULL) VARR_PUSH (token_t, temp_tokens, t1);
  }
  if (t1->code != '(') {
    push_back (c2m_ctx, temp_tokens);
    return FALSE;
  }
  t1 = get_next_pptoken (c2m_ctx);
  if (temp_tokens != NULL) VARR_PUSH (token_t, temp_tokens, t1);
  if (t1->code == ' ' || t1->code == '\n') {
    t1 = get_next_pptoken (c2m_ctx);
    if (temp_tokens != NULL) VARR_PUSH (token_t, temp_tokens, t1);
  }
  if (t1->code != T_STR) {
    push_back (c2m_ctx, temp_tokens);
    return FALSE;
  }
  t2 = t1;
  t1 = get_next_pptoken (c2m_ctx);
  if (temp_tokens != NULL) VARR_PUSH (token_t, temp_tokens, t1);
  if (t1->code == ' ' || t1->code == '\n') {
    t1 = get_next_pptoken (c2m_ctx);
    if (temp_tokens != NULL) VARR_PUSH (token_t, temp_tokens, t1);
  }
  if (t1->code != ')') {
    push_back (c2m_ctx, temp_tokens);
    return FALSE;
  }
  set_string_stream (c2m_ctx, t2->repr, t2->pos, prepare_pragma_string);
  VARR_TRUNC (token_t, temp_tokens, 0);
  for (t1 = get_next_pptoken (c2m_ctx); t1->code != T_EOFILE; t1 = get_next_pptoken (c2m_ctx))
    if (temp_tokens != NULL) VARR_PUSH (token_t, temp_tokens, t1);
  check_pragma (c2m_ctx, t2, temp_tokens);
  return TRUE;
}

static void flush_buffer (c2m_ctx_t c2m_ctx) {
  pre_ctx_t pre_ctx = c2m_ctx->pre_ctx;

  for (size_t i = 0; i < VARR_LENGTH (token_t, output_buffer); i++)
    pre_out_token_func (c2m_ctx, VARR_GET (token_t, output_buffer, i));
  VARR_TRUNC (token_t, output_buffer, 0);
}

static void out_token (c2m_ctx_t c2m_ctx, token_t t) {
  pre_ctx_t pre_ctx = c2m_ctx->pre_ctx;

  if (no_out_p || VARR_LENGTH (macro_call_t, macro_call_stack) != 0) {
    if (output_buffer != NULL) VARR_PUSH (token_t, output_buffer, t);
    return;
  }
  flush_buffer (c2m_ctx);
  pre_out_token_func (c2m_ctx, t);
}

struct val {
  int uns_p;
  union {
    mir_llong i_val;
    mir_ullong u_val;
  } u;
};

static void move_tokens (VARR (token_t) * to, VARR (token_t) * from) {
  VARR_TRUNC (token_t, to, 0);
  for (size_t i = 0; i < VARR_LENGTH (token_t, from); i++)
    if (to != NULL && from != NULL) VARR_PUSH (token_t, to, VARR_GET (token_t, from, i));
  VARR_TRUNC (token_t, from, 0);
}

static void reverse_move_tokens (VARR (token_t) * to, VARR (token_t) * from) {
  VARR_TRUNC (token_t, to, 0);
  while (VARR_LENGTH (token_t, from) != 0) VARR_PUSH (token_t, to, VARR_POP (token_t, from));
}

static void transform_to_header (c2m_ctx_t c2m_ctx, VARR (token_t) * buffer) {
  size_t i, j, k;
  token_t t;
  pos_t pos;

  for (i = 0; i < VARR_LENGTH (token_t, buffer) && VARR_GET (token_t, buffer, i)->code == ' '; i++)
    ;
  if (i >= VARR_LENGTH (token_t, buffer)) return;
  if ((t = VARR_GET (token_t, buffer, i))->node_code != N_LT) return;
  pos = t->pos;
  for (j = i + 1;
       j < VARR_LENGTH (token_t, buffer) && VARR_GET (token_t, buffer, j)->node_code != N_GT; j++)
    ;
  if (j >= VARR_LENGTH (token_t, buffer)) return;
  VARR_TRUNC (char, symbol_text, 0);
  VARR_TRUNC (char, temp_string, 0);
  VARR_PUSH (char, symbol_text, '<');
  for (k = i + 1; k < j; k++) {
    t = VARR_GET (token_t, buffer, k);
    for (const char *s = t->repr; *s != 0; s++) {
      VARR_PUSH (char, symbol_text, *s);
      VARR_PUSH (char, temp_string, *s);
    }
  }
  VARR_PUSH (char, symbol_text, '>');
  VARR_PUSH (char, symbol_text, '\0');
  VARR_PUSH (char, temp_string, '\0');
  del_tokens (buffer, (int) i, (int) (j - i));
  t = new_node_token (c2m_ctx, pos, VARR_ADDR (char, symbol_text), T_HEADER,
                      new_str_node (c2m_ctx, N_STR,
                                    uniq_cstr (c2m_ctx, VARR_ADDR (char, temp_string)), pos));
  VARR_SET (token_t, buffer, i, t);
}

static void processing (c2m_ctx_t c2m_ctx, int ignore_directive_p);

static struct val eval_expr (c2m_ctx_t c2m_ctx, VARR (token_t) * buffer, token_t if_token);

/* Fast-scan false #if regions: skip non-directive lines without creating
   tokens / string interning.  Handles // and block comments, simple string
   and char literals, and backslash-newline.  On success leaves '#' as the
   next character for get_next_pptoken.  Returns 1 if a directive '#' was
   found, 0 if the stream hit EOF (next get_next_pptoken yields T_EOFILE). */
static int pre_fast_skip_to_directive (c2m_ctx_t c2m_ctx) {
  int c, c2, in_block = 0;

  for (;;) {
    if (!in_block) {
      /* Leading whitespace on a logical line: */
      for (;;) {
        c = cs_get (c2m_ctx);
        if (c == ' ' || c == '\t' || c == '\f' || c == '\v' || c == '\r') continue;
        break;
      }
      if (c == EOF) return 0;
      if (c == '\n') continue;
      if (c == '#') {
        cs_unget (c2m_ctx, c);
        return 1;
      }
      if (c == '/') {
        c2 = cs_get (c2m_ctx);
        if (c2 == '/') {
          while ((c = cs_get (c2m_ctx)) != EOF && c != '\n') {
            if (c == '\\') {
              c2 = cs_get (c2m_ctx);
              if (c2 == EOF) return 0;
              if (c2 == '\n' || c2 == '\r') continue; /* continued // comment */
              /* else consume c2 as part of comment */
            }
          }
          if (c == EOF) return 0;
          continue;
        } else if (c2 == '*') {
          in_block = 1;
          continue;
        } else {
          if (c2 != EOF) cs_unget (c2m_ctx, c2);
          /* fall through: non-directive line starting with / */
        }
      }
    }

    /* Skip rest of line (or through block comment), tracking strings. */
    for (;;) {
      if (in_block) {
        c = cs_get (c2m_ctx);
        if (c == EOF) return 0;
        if (c == '*') {
          c2 = cs_get (c2m_ctx);
          if (c2 == '/') {
            in_block = 0;
            break; /* resume scanning after comment (same line) */
          }
          if (c2 != EOF) cs_unget (c2m_ctx, c2);
        }
        continue;
      }
      c = cs_get (c2m_ctx);
      if (c == EOF) return 0;
      if (c == '\n') break;
      if (c == '\\') {
        c2 = cs_get (c2m_ctx);
        if (c2 == EOF) return 0;
        if (c2 == '\n' || c2 == '\r') continue; /* line splice */
        continue;
      }
      if (c == '/' ) {
        c2 = cs_get (c2m_ctx);
        if (c2 == '/') {
          while ((c = cs_get (c2m_ctx)) != EOF && c != '\n') {
            if (c == '\\') {
              c2 = cs_get (c2m_ctx);
              if (c2 == EOF) return 0;
            }
          }
          if (c == EOF) return 0;
          break;
        } else if (c2 == '*') {
          in_block = 1;
          continue;
        } else if (c2 != EOF) {
          cs_unget (c2m_ctx, c2);
        }
        continue;
      }
      if (c == '"' || c == '\'') {
        int quote = c;
        while ((c = cs_get (c2m_ctx)) != EOF && c != quote && c != '\n') {
          if (c == '\\') {
            c2 = cs_get (c2m_ctx);
            if (c2 == EOF) return 0;
          }
        }
        if (c == EOF) return 0;
        if (c == '\n') break;
        continue;
      }
    }
  }
}

static const char *get_header_name (c2m_ctx_t c2m_ctx, VARR (token_t) * buffer, pos_t err_pos,
                                    const char **content) {
  size_t i;

  *content = NULL;
  transform_to_header (c2m_ctx, buffer);
  i = 0;
  if (VARR_LENGTH (token_t, buffer) != 0 && VARR_GET (token_t, buffer, 0)->code == ' ') i++;
  if (i != VARR_LENGTH (token_t, buffer) - 1
      || (VARR_GET (token_t, buffer, i)->code != T_STR
          && VARR_GET (token_t, buffer, i)->code != T_HEADER)) {
    error (c2m_ctx, err_pos, "wrong #include");
    return NULL;
  }
  return get_include_fname (c2m_ctx, VARR_GET (token_t, buffer, i), content);
}

static void process_directive (c2m_ctx_t c2m_ctx) {
  MIR_alloc_t alloc = c2m_alloc (c2m_ctx);
  pre_ctx_t pre_ctx = c2m_ctx->pre_ctx;
  token_t t, t1;
  int true_p;
  VARR (token_t) * temp_buffer;
  pos_t pos;
  struct macro macro;
  macro_t tab_macro;
  const char *name;

  t = get_next_pptoken (c2m_ctx);
  if (t->code == '\n') return;
  if (t->code == ' ') t = get_next_pptoken (c2m_ctx);
  if (t->code != T_ID) {
    if (!skip_if_part_p) error (c2m_ctx, t->pos, "wrong directive name %s", t->repr);
    skip_nl (c2m_ctx, NULL, NULL);
    return;
  }

  VARR_CREATE (token_t, temp_buffer, alloc, 64);
  if (strcmp (t->repr, "ifdef") == 0 || strcmp (t->repr, "ifndef") == 0) {
    t1 = t;
    if (VARR_LENGTH (ifstate_t, ifs) != 0 && VARR_LAST (ifstate_t, ifs)->skip_p) {
      skip_if_part_p = true_p = TRUE;
      skip_nl (c2m_ctx, NULL, NULL);
      if (cs != NULL && cs->ig_state == IG_EXPECT_IFNDEF) ig_fail (cs);
    } else {
      token_t guard_id = NULL;

      t = get_next_pptoken (c2m_ctx);
      skip_if_part_p = FALSE;
      if (t->code == ' ') t = get_next_pptoken (c2m_ctx);
      if (t->code != T_ID) {
        error (c2m_ctx, t->pos, "wrong #%s", t1->repr);
      } else {
        macro.id = t;
        guard_id = t;
        skip_if_part_p = HTAB_DO (macro_t, macro_tab, &macro, HTAB_FIND, tab_macro);
      }
      t = get_next_pptoken (c2m_ctx);
      if (t->code != '\n') {
        error (c2m_ctx, t1->pos, "garbage at the end of #%s", t1->repr);
        skip_nl (c2m_ctx, NULL, NULL);
      }
      if (strcmp (t1->repr, "ifdef") == 0) skip_if_part_p = !skip_if_part_p;
      true_p = !skip_if_part_p;
      /* First non-ws content of an include must be #ifndef GUARD for the opt. */
      if (cs != NULL && cs->ig_state == IG_EXPECT_IFNDEF
          && (int) VARR_LENGTH (ifstate_t, ifs) == cs->ifs_length_at_stream_start
          && strcmp (t1->repr, "ifndef") == 0 && guard_id != NULL) {
        cs->ig_macro = guard_id->repr;
        cs->ig_state = IG_EXPECT_DEFINE;
      } else if (cs != NULL
                 && (cs->ig_state == IG_EXPECT_IFNDEF || cs->ig_state == IG_EXPECT_DEFINE)) {
        ig_fail (cs);
      }
    }
    VARR_PUSH (ifstate_t, ifs, new_ifstate (skip_if_part_p, true_p, FALSE, t1->pos));
  } else if (strcmp (t->repr, "endif") == 0 || strcmp (t->repr, "else") == 0) {
    t1 = t;
    t = get_next_pptoken (c2m_ctx);
    if (t->code != '\n') {
      error (c2m_ctx, t1->pos, "garbage at the end of #%s", t1->repr);
      skip_nl (c2m_ctx, NULL, NULL);
    }
    if ((int) VARR_LENGTH (ifstate_t, ifs) <= cs->ifs_length_at_stream_start)
      error (c2m_ctx, t1->pos, "unmatched #%s", t1->repr);
    else if (strcmp (t1->repr, "endif") == 0) {
      pop_ifstate (c2m_ctx);
      skip_if_part_p = VARR_LENGTH (ifstate_t, ifs) == 0 ? 0 : VARR_LAST (ifstate_t, ifs)->skip_p;
      /* Matching #endif for the outermost #ifndef of this include file. */
      if (cs != NULL && cs->ig_state == IG_IN_BODY
          && (int) VARR_LENGTH (ifstate_t, ifs) == cs->ifs_length_at_stream_start)
        cs->ig_state = IG_AFTER_ENDIF;
    } else if (VARR_LAST (ifstate_t, ifs)->else_p) {
      error (c2m_ctx, t1->pos, "repeated #else");
      VARR_LAST (ifstate_t, ifs)->skip_p = 1;
      skip_if_part_p = TRUE;
      ig_fail (cs);
    } else {
      /* #else at guard level breaks the simple include-guard pattern. */
      if (cs != NULL && cs->ig_state == IG_IN_BODY
          && (int) VARR_LENGTH (ifstate_t, ifs) == cs->ifs_length_at_stream_start + 1)
        ig_fail (cs);
      skip_if_part_p = VARR_LAST (ifstate_t, ifs)->true_p;
      VARR_LAST (ifstate_t, ifs)->true_p = TRUE;
      VARR_LAST (ifstate_t, ifs)->skip_p = skip_if_part_p;
      VARR_LAST (ifstate_t, ifs)->else_p = FALSE;
    }
  } else if (strcmp (t->repr, "if") == 0 || strcmp (t->repr, "elif") == 0) {
    if_id = t;
    /* #if / #elif is never the simple include-guard opener (#ifndef only). */
    if (cs != NULL
        && (cs->ig_state == IG_EXPECT_IFNDEF || cs->ig_state == IG_EXPECT_DEFINE
            || (cs->ig_state == IG_IN_BODY
                && strcmp (t->repr, "elif") == 0
                && (int) VARR_LENGTH (ifstate_t, ifs) == cs->ifs_length_at_stream_start + 1)))
      ig_fail (cs);
    if (strcmp (t->repr, "elif") == 0 && VARR_LENGTH (ifstate_t, ifs) == 0) {
      error (c2m_ctx, t->pos, "#elif without #if");
    } else if (strcmp (t->repr, "elif") == 0 && VARR_LAST (ifstate_t, ifs)->else_p) {
      error (c2m_ctx, t->pos, "#elif after #else");
      skip_if_part_p = TRUE;
    } else if (strcmp (t->repr, "if") == 0 && VARR_LENGTH (ifstate_t, ifs) != 0
               && VARR_LAST (ifstate_t, ifs)->skip_p) {
      skip_if_part_p = true_p = TRUE;
      skip_nl (c2m_ctx, NULL, NULL);
      VARR_PUSH (ifstate_t, ifs, new_ifstate (skip_if_part_p, true_p, FALSE, t->pos));
    } else if (strcmp (t->repr, "elif") == 0 && VARR_LAST (ifstate_t, ifs)->true_p) {
      VARR_LAST (ifstate_t, ifs)->skip_p = skip_if_part_p = TRUE;
      skip_nl (c2m_ctx, NULL, NULL);
    } else {
      struct val val;

      skip_if_part_p = FALSE; /* for eval expr */
      skip_nl (c2m_ctx, NULL, temp_buffer);
      val = eval_expr (c2m_ctx, temp_buffer, t);
      true_p = val.uns_p ? val.u.u_val != 0 : val.u.i_val != 0;
      skip_if_part_p = !true_p;
      if (strcmp (t->repr, "if") == 0)
        VARR_PUSH (ifstate_t, ifs, new_ifstate (skip_if_part_p, true_p, FALSE, t->pos));
      else {
        VARR_LAST (ifstate_t, ifs)->skip_p = skip_if_part_p;
        VARR_LAST (ifstate_t, ifs)->true_p = true_p;
      }
    }
  } else if (strcmp (t->repr, "elifdef") == 0 || strcmp (t->repr, "elifndef") == 0) {
    /* C23: `#elifdef NAME` / `#elifndef NAME` — `#elif defined(NAME)` spelling. */
    t1 = t;
    if (cs != NULL && cs->ig_state == IG_IN_BODY
        && (int) VARR_LENGTH (ifstate_t, ifs) == cs->ifs_length_at_stream_start + 1)
      ig_fail (cs);
    if (VARR_LENGTH (ifstate_t, ifs) == 0) {
      error (c2m_ctx, t->pos, "#%s without #if", t->repr);
    } else if (VARR_LAST (ifstate_t, ifs)->else_p) {
      error (c2m_ctx, t->pos, "#%s after #else", t->repr);
      skip_if_part_p = TRUE;
    } else if (VARR_LAST (ifstate_t, ifs)->true_p) {
      VARR_LAST (ifstate_t, ifs)->skip_p = skip_if_part_p = TRUE;
      skip_nl (c2m_ctx, NULL, NULL);
    } else {
      t = get_next_pptoken (c2m_ctx);
      if (t->code == ' ') t = get_next_pptoken (c2m_ctx);
      if (t->code != T_ID) {
        error (c2m_ctx, t1->pos, "wrong #%s", t1->repr);
        skip_if_part_p = TRUE;
        skip_nl (c2m_ctx, t, NULL);
      } else {
        macro.id = t;
        true_p = HTAB_DO (macro_t, macro_tab, &macro, HTAB_FIND, tab_macro);
        if (strcmp (t1->repr, "elifndef") == 0) true_p = !true_p;
        skip_if_part_p = !true_p;
        VARR_LAST (ifstate_t, ifs)->skip_p = skip_if_part_p;
        VARR_LAST (ifstate_t, ifs)->true_p = true_p;
        t = get_next_pptoken (c2m_ctx);
        if (t->code != '\n') {
          error (c2m_ctx, t1->pos, "garbage at the end of #%s", t1->repr);
          skip_nl (c2m_ctx, NULL, NULL);
        }
      }
    }
  } else if (skip_if_part_p) {
    skip_nl (c2m_ctx, NULL, NULL);
  } else if (strcmp (t->repr, "define") == 0) {
    define (c2m_ctx);
  } else if (strcmp (t->repr, "include") == 0) {
    if (cs != NULL
        && (cs->ig_state == IG_EXPECT_IFNDEF || cs->ig_state == IG_EXPECT_DEFINE
            || cs->ig_state == IG_AFTER_ENDIF))
      ig_fail (cs);
    const char *content;

    t = get_next_include_pptoken (c2m_ctx);
    if (t->code == ' ') t = get_next_include_pptoken (c2m_ctx);
    t1 = get_next_pptoken (c2m_ctx);
    if ((t->code == T_STR || t->code == T_HEADER) && t1->code == '\n')
      name = get_include_fname (c2m_ctx, t, &content);
    else {
      if (temp_buffer != NULL) VARR_PUSH (token_t, temp_buffer, t);
      skip_nl (c2m_ctx, t1, temp_buffer);
      unget_next_pptoken (c2m_ctx, new_token (c2m_ctx, t->pos, "", T_EOP, N_IGNORE));
      push_back (c2m_ctx, temp_buffer);
      if (n_errors != 0) VARR_TRUNC (macro_call_t, macro_call_stack, 0); /* can be non-empty */
      assert (VARR_LENGTH (macro_call_t, macro_call_stack) == 0 && !no_out_p);
      no_out_p = TRUE;
      processing (c2m_ctx, TRUE);
      no_out_p = FALSE;
      move_tokens (temp_buffer, output_buffer);
      if ((name = get_header_name (c2m_ctx, temp_buffer, t->pos, &content)) == NULL) {
        error (c2m_ctx, t->pos, "wrong #include");
        goto ret;
      }
    }
    if ((int) VARR_LENGTH (stream_t, streams) >= max_nested_includes + 1) {
      error (c2m_ctx, t->pos, "more %d include levels", VARR_LENGTH (stream_t, streams) - 1);
      goto ret;
    }
    add_include_stream (c2m_ctx, name, content, t->pos);
  } else if (strcmp (t->repr, "line") == 0) {
    if (cs != NULL
        && (cs->ig_state == IG_EXPECT_IFNDEF || cs->ig_state == IG_EXPECT_DEFINE
            || cs->ig_state == IG_AFTER_ENDIF))
      ig_fail (cs);
    skip_nl (c2m_ctx, NULL, temp_buffer);
    unget_next_pptoken (c2m_ctx, new_token (c2m_ctx, t->pos, "", T_EOP, N_IGNORE));
    push_back (c2m_ctx, temp_buffer);
    if (n_errors != 0) VARR_TRUNC (macro_call_t, macro_call_stack, 0); /* can be non-empty */
    assert (VARR_LENGTH (macro_call_t, macro_call_stack) == 0 && !no_out_p);
    no_out_p = TRUE;
    processing (c2m_ctx, TRUE);
    no_out_p = 0;
    move_tokens (temp_buffer, output_buffer);
    pos = check_line_directive_args (c2m_ctx, temp_buffer);
    if (pos.lno < 0) {
      error (c2m_ctx, t->pos, "wrong #line");
    } else {
      change_stream_pos (c2m_ctx, pos);
    }
  } else if (strcmp (t->repr, "error") == 0) {
    VARR_TRUNC (char, temp_string, 0);
    add_to_temp_string (c2m_ctx, "#error");
    for (t1 = get_next_pptoken (c2m_ctx); t1->code != '\n'; t1 = get_next_pptoken (c2m_ctx))
      add_to_temp_string (c2m_ctx, t1->repr);
    error (c2m_ctx, t->pos, "%s", VARR_ADDR (char, temp_string));
  } else if (!c2m_options->pedantic_p && strcmp (t->repr, "warning") == 0) {
    VARR_TRUNC (char, temp_string, 0);
    add_to_temp_string (c2m_ctx, "#warning");
    for (t1 = get_next_pptoken (c2m_ctx); t1->code != '\n'; t1 = get_next_pptoken (c2m_ctx))
      add_to_temp_string (c2m_ctx, t1->repr);
    warning (c2m_ctx, t->pos, "%s", VARR_ADDR (char, temp_string));
  } else if (strcmp (t->repr, "pragma") == 0) {
    skip_nl (c2m_ctx, NULL, temp_buffer);
    check_pragma (c2m_ctx, t, temp_buffer);
  } else if (strcmp (t->repr, "undef") == 0) {
    t = get_next_pptoken (c2m_ctx);
    if (t->code == ' ') t = get_next_pptoken (c2m_ctx);
    if (t->code == '\n') {
      error (c2m_ctx, t->pos, "no ident after #undef");
      goto ret;
    }
    if (t->code != T_ID) {
      error (c2m_ctx, t->pos, "no ident after #undef");
      skip_nl (c2m_ctx, t, NULL);
      goto ret;
    }
    if (strcmp (t->repr, "defined") == 0) {
      error (c2m_ctx, t->pos, "#undef of %s", t->repr);
    } else {
      macro.id = t;
      if (HTAB_DO (macro_t, macro_tab, &macro, HTAB_FIND, tab_macro)) {
        if (tab_macro->replacement == NULL)
          error (c2m_ctx, t->pos, "#undef of standard macro %s", t->repr);
        else
          HTAB_DO (macro_t, macro_tab, &macro, HTAB_DELETE, tab_macro);
      }
    }
  }
ret:
  VARR_DESTROY (token_t, temp_buffer);
}

static int pre_match (c2m_ctx_t c2m_ctx, int c, pos_t *pos, node_code_t *node_code, node_t *node) {
  pre_ctx_t pre_ctx = c2m_ctx->pre_ctx;
  token_t t;

  if (VARR_LENGTH (token_t, pre_expr) == 0) return FALSE;
  t = VARR_LAST (token_t, pre_expr);
  if (t->code != c) return FALSE;
  if (pos != NULL) *pos = t->pos;
  if (node_code != NULL) *node_code = t->node_code;
  if (node != NULL) *node = t->node;
  VARR_POP (token_t, pre_expr);
  return TRUE;
}

static node_t pre_cond_expr (c2m_ctx_t c2m_ctx);

/* Expressions: */
static node_t pre_primary_expr (c2m_ctx_t c2m_ctx) {
  node_t r, n;

  if (pre_match (c2m_ctx, T_CH, NULL, NULL, &r)) return r;
  if (pre_match (c2m_ctx, T_NUMBER, NULL, NULL, &n)) {
    if (!pre_match (c2m_ctx, '(', NULL, NULL, NULL)) return n;
    if (!pre_match (c2m_ctx, ')', NULL, NULL, NULL)) {
      for (;;) {
        if ((r = pre_cond_expr (c2m_ctx)) == NULL) return NULL;
        if (pre_match (c2m_ctx, ')', NULL, NULL, NULL)) break;
        if (!pre_match (c2m_ctx, ',', NULL, NULL, NULL)) return NULL;
      }
    }
    return new_pos_node (c2m_ctx, N_IGNORE, POS (n)); /* error only during evaluation */
  }
  if (pre_match (c2m_ctx, '(', NULL, NULL, NULL)) {
    if ((r = pre_cond_expr (c2m_ctx)) == NULL) return NULL;
    if (pre_match (c2m_ctx, ')', NULL, NULL, NULL)) return r;
  }
  return NULL;
}

static node_t pre_unary_expr (c2m_ctx_t c2m_ctx) {
  node_t r;
  node_code_t code;
  pos_t pos;

  if (!pre_match (c2m_ctx, T_UNOP, &pos, &code, NULL)
      && !pre_match (c2m_ctx, T_ADDOP, &pos, &code, NULL))
    return pre_primary_expr (c2m_ctx);
  if ((r = pre_unary_expr (c2m_ctx)) == NULL) return r;
  r = new_pos_node1 (c2m_ctx, code, pos, r);
  return r;
}

static node_t pre_left_op (c2m_ctx_t c2m_ctx, int token, int token2,
                           node_t (*f) (c2m_ctx_t c2m_ctx)) {
  node_code_t code;
  node_t r, n;
  pos_t pos;

  if ((r = f (c2m_ctx)) == NULL) return r;
  while (pre_match (c2m_ctx, token, &pos, &code, NULL)
         || (token2 >= 0 && pre_match (c2m_ctx, token2, &pos, &code, NULL))) {
    n = new_pos_node1 (c2m_ctx, code, pos, r);
    if ((r = f (c2m_ctx)) == NULL) return r;
    op_append (c2m_ctx, n, r);
    r = n;
  }
  return r;
}

static node_t pre_mul_expr (c2m_ctx_t c2m_ctx) {
  return pre_left_op (c2m_ctx, T_DIVOP, '*', pre_unary_expr);
}
static node_t pre_add_expr (c2m_ctx_t c2m_ctx) {
  return pre_left_op (c2m_ctx, T_ADDOP, -1, pre_mul_expr);
}
static node_t pre_sh_expr (c2m_ctx_t c2m_ctx) {
  return pre_left_op (c2m_ctx, T_SH, -1, pre_add_expr);
}
static node_t pre_rel_expr (c2m_ctx_t c2m_ctx) {
  return pre_left_op (c2m_ctx, T_CMP, -1, pre_sh_expr);
}
static node_t pre_eq_expr (c2m_ctx_t c2m_ctx) {
  return pre_left_op (c2m_ctx, T_EQNE, -1, pre_rel_expr);
}
static node_t pre_and_expr (c2m_ctx_t c2m_ctx) {
  return pre_left_op (c2m_ctx, '&', -1, pre_eq_expr);
}
static node_t pre_xor_expr (c2m_ctx_t c2m_ctx) {
  return pre_left_op (c2m_ctx, '^', -1, pre_and_expr);
}
static node_t pre_or_expr (c2m_ctx_t c2m_ctx) {
  return pre_left_op (c2m_ctx, '|', -1, pre_xor_expr);
}
static node_t pre_land_expr (c2m_ctx_t c2m_ctx) {
  return pre_left_op (c2m_ctx, T_ANDAND, -1, pre_or_expr);
}
static node_t pre_lor_expr (c2m_ctx_t c2m_ctx) {
  return pre_left_op (c2m_ctx, T_OROR, -1, pre_land_expr);
}

static node_t pre_cond_expr (c2m_ctx_t c2m_ctx) {
  node_t r, n;
  pos_t pos;

  if ((r = pre_lor_expr (c2m_ctx)) == NULL) return r;
  if (!pre_match (c2m_ctx, '?', &pos, NULL, NULL)) return r;
  n = new_pos_node1 (c2m_ctx, N_COND, pos, r);
  if ((r = pre_cond_expr (c2m_ctx)) == NULL) return r;
  op_append (c2m_ctx, n, r);
  if (!pre_match (c2m_ctx, ':', NULL, NULL, NULL)) return NULL;
  if ((r = pre_cond_expr (c2m_ctx)) == NULL) return r;
  op_append (c2m_ctx, n, r);
  return n;
}

static node_t parse_pre_expr (c2m_ctx_t c2m_ctx, VARR (token_t) * expr) {
  pre_ctx_t pre_ctx = c2m_ctx->pre_ctx;
  node_t r;
  token_t t;

  pre_expr = expr;
  t = VARR_LAST (token_t, expr);
  if ((r = pre_cond_expr (c2m_ctx)) != NULL && VARR_LENGTH (token_t, expr) == 0) return r;
  if (VARR_LENGTH (token_t, expr) != 0) t = VARR_POP (token_t, expr);
  error (c2m_ctx, t->pos, "wrong preprocessor expression");
  return NULL;
}

static void replace_defined (c2m_ctx_t c2m_ctx, VARR (token_t) * expr_buffer) {
  pre_ctx_t pre_ctx = c2m_ctx->pre_ctx;
  size_t i, j, k, len;
  token_t t, id;
  const char *res;
  struct macro macro_struct;
  macro_t tab_macro;

  for (i = 0; i < VARR_LENGTH (token_t, expr_buffer); i++) {
    /* Change defined ident and defined (ident) */
    t = VARR_GET (token_t, expr_buffer, i);
    if (t->code == T_ID && strcmp (t->repr, "defined") == 0) {
      j = i + 1;
      len = VARR_LENGTH (token_t, expr_buffer);
      if (j < len && VARR_GET (token_t, expr_buffer, j)->code == ' ') j++;
      if (j >= len) continue;
      if ((id = VARR_GET (token_t, expr_buffer, j))->code == T_ID) {
        macro_struct.id = id;
        res = HTAB_DO (macro_t, macro_tab, &macro_struct, HTAB_FIND, tab_macro) ? "1" : "0";
        VARR_SET (token_t, expr_buffer, i,
                  new_token (c2m_ctx, t->pos, res, T_NUMBER, N_IGNORE));  // ???
        del_tokens (expr_buffer, (int) i + 1, (int) (j - i));
        continue;
      }
      if (j >= len || VARR_GET (token_t, expr_buffer, j)->code != '(') continue;
      j++;
      if (j < len && VARR_GET (token_t, expr_buffer, j)->code == ' ') j++;
      if (j >= len || VARR_GET (token_t, expr_buffer, j)->code != T_ID) continue;
      k = j;
      j++;
      if (j < len && VARR_GET (token_t, expr_buffer, j)->code == ' ') j++;
      if (j >= len || VARR_GET (token_t, expr_buffer, j)->code != ')') continue;
      macro_struct.id = VARR_GET (token_t, expr_buffer, k);
      res = HTAB_DO (macro_t, macro_tab, &macro_struct, HTAB_FIND, tab_macro) ? "1" : "0";
      VARR_SET (token_t, expr_buffer, i,
                new_token (c2m_ctx, t->pos, res, T_NUMBER, N_IGNORE));  // ???
      del_tokens (expr_buffer, (int) i + 1, (int) (j - i));
    }
  }
}

static struct val eval (c2m_ctx_t c2m_ctx, node_t tree);

static struct val eval_expr (c2m_ctx_t c2m_ctx, VARR (token_t) * expr_buffer, token_t if_token) {
  MIR_alloc_t alloc = c2m_alloc (c2m_ctx);
  pre_ctx_t pre_ctx = c2m_ctx->pre_ctx;
  size_t i, j;
  token_t t, ppt;
  VARR (token_t) * temp_buffer;
  node_t tree;

  replace_defined (c2m_ctx, expr_buffer);
  if (VARR_LENGTH (macro_call_t, macro_call_stack) != 0)
    error (c2m_ctx, if_token->pos, "#if/#elif inside a macro call");
  assert (VARR_LENGTH (token_t, output_buffer) == 0 && !no_out_p);
  /* macro substitution */
  unget_next_pptoken (c2m_ctx, new_token (c2m_ctx, if_token->pos, "", T_EOP, N_IGNORE));
  push_back (c2m_ctx, expr_buffer);
  no_out_p = TRUE;
  processing (c2m_ctx, TRUE);
  replace_defined (c2m_ctx, output_buffer);
  no_out_p = FALSE;
  reverse_move_tokens (expr_buffer, output_buffer);
  VARR_CREATE (token_t, temp_buffer, alloc, VARR_LENGTH (token_t, expr_buffer));
  for (i = j = 0; i < VARR_LENGTH (token_t, expr_buffer); i++) {
    int change_p = TRUE;

    /* changing PP tokens to tokens and idents to "0" */
    ppt = VARR_GET (token_t, expr_buffer, i);
    t = pptoken2token (c2m_ctx, ppt, FALSE);
    if (t == NULL || t->code == ' ' || t->code == '\n') continue;
    if (t->code == T_NUMBER
        && (t->node->code == N_F || t->node->code == N_D || t->node->code == N_LD)) {
      error (c2m_ctx, ppt->pos, "floating point in #if/#elif: %s", ppt->repr);
    } else if (t->code == T_STR) {
      error (c2m_ctx, ppt->pos, "string in #if/#elif: %s", ppt->repr);
    } else if (t->code != T_ID) {
      change_p = FALSE;
    }
    if (change_p)
      t = new_node_token (c2m_ctx, ppt->pos, "0", T_NUMBER, new_ll_node (c2m_ctx, 0, ppt->pos));
    if (temp_buffer != NULL) VARR_PUSH (token_t, temp_buffer, t);
  }
  no_out_p = TRUE;
  if (VARR_LENGTH (token_t, temp_buffer) != 0) {
    tree = parse_pre_expr (c2m_ctx, temp_buffer);
  } else {
    error (c2m_ctx, if_token->pos, "empty preprocessor expression");
    tree = NULL;
  }
  no_out_p = FALSE;
  VARR_DESTROY (token_t, temp_buffer);
  if (tree == NULL) {
    struct val val;

    val.uns_p = FALSE;
    val.u.i_val = 0;
    return val;
  }
  return eval (c2m_ctx, tree);
}

static int eval_binop_operands (c2m_ctx_t c2m_ctx, node_t tree, struct val *v1, struct val *v2) {
  *v1 = eval (c2m_ctx, NL_HEAD (tree->u.ops));
  *v2 = eval (c2m_ctx, NL_EL (tree->u.ops, 1));
  if (v1->uns_p && !v2->uns_p) {
    v2->uns_p = TRUE;
    v2->u.u_val = v2->u.i_val;
  } else if (!v1->uns_p && v2->uns_p) {
    v1->uns_p = TRUE;
    v1->u.u_val = v1->u.i_val;
  }
  return v1->uns_p;
}

static struct val eval (c2m_ctx_t c2m_ctx, node_t tree) {
  int cond;
  struct val res, v1, v2;

#define UNOP(op)                                \
  do {                                          \
    v1 = eval (c2m_ctx, NL_HEAD (tree->u.ops)); \
    res = v1;                                   \
    if (res.uns_p)                              \
      res.u.u_val = op res.u.u_val;             \
    else                                        \
      res.u.i_val = op res.u.i_val;             \
  } while (0)

#define BINOP(op)                                              \
  do {                                                         \
    res.uns_p = eval_binop_operands (c2m_ctx, tree, &v1, &v2); \
    if (res.uns_p)                                             \
      res.u.u_val = v1.u.u_val op v2.u.u_val;                  \
    else                                                       \
      res.u.i_val = v1.u.i_val op v2.u.i_val;                  \
  } while (0)

  switch (tree->code) {
  case N_IGNORE:
    error (c2m_ctx, POS (tree), "wrong preprocessor expression");
    res.uns_p = FALSE;
    res.u.i_val = 0;
    break;
  case N_CH:
    res.uns_p = !char_is_signed_p () || MIR_CHAR_MAX > MIR_INT_MAX;
    if (res.uns_p)
      res.u.u_val = tree->u.ch;
    else
      res.u.i_val = tree->u.ch;
    break;
  case N_CH16:
  case N_CH32:
    res.uns_p = TRUE;
    res.u.u_val = tree->u.ul;
    break;
  case N_I:
  case N_L:
    res.uns_p = FALSE;
    res.u.i_val = tree->u.l;
    break;
  case N_LL:
    res.uns_p = FALSE;
    res.u.i_val = tree->u.ll;
    break;
  case N_U:
  case N_UL:
    res.uns_p = TRUE;
    res.u.u_val = tree->u.ul;
    break;
  case N_ULL:
    res.uns_p = TRUE;
    res.u.u_val = tree->u.ull;
    break;
  case N_BITWISE_NOT: UNOP (~); break;
  case N_NOT: UNOP (!); break;
  case N_EQ: BINOP (==); break;
  case N_NE: BINOP (!=); break;
  case N_LT: BINOP (<); break;
  case N_LE: BINOP (<=); break;
  case N_GT: BINOP (>); break;
  case N_GE: BINOP (>=); break;
  case N_ADD:
    if (NL_EL (tree->u.ops, 1) == NULL) {
      UNOP (+);
    } else {
      BINOP (+);
    }
    break;
  case N_SUB:
    if (NL_EL (tree->u.ops, 1) == NULL) {
      UNOP (-);
    } else {
      BINOP (-);
    }
    break;
  case N_AND: BINOP (&); break;
  case N_OR: BINOP (|); break;
  case N_XOR: BINOP (^); break;
  case N_LSH: BINOP (<<); break;
  case N_RSH: BINOP (>>); break;
  case N_MUL: BINOP (*); break;
  case N_DIV:
  case N_MOD: {
    int zero_p;

    res.uns_p = eval_binop_operands (c2m_ctx, tree, &v1, &v2);
    if (res.uns_p) {
      res.u.u_val = ((zero_p = v2.u.u_val == 0) ? 1
                     : tree->code == N_DIV      ? v1.u.u_val / v2.u.u_val
                                                : v1.u.u_val % v2.u.u_val);
    } else {
      res.u.i_val = ((zero_p = v2.u.i_val == 0) ? 1
                     : tree->code == N_DIV      ? v1.u.i_val / v2.u.i_val
                                                : v1.u.i_val % v2.u.i_val);
    }
    if (zero_p)
      error (c2m_ctx, POS (tree), "division (%s) by zero in preporocessor",
             tree->code == N_DIV ? "/" : "%");
    break;
  }
  case N_ANDAND:
  case N_OROR:
    v1 = eval (c2m_ctx, NL_HEAD (tree->u.ops));
    cond = v1.uns_p ? v1.u.u_val != 0 : v1.u.i_val != 0;
    if (tree->code == N_ANDAND ? cond : !cond) {
      v2 = eval (c2m_ctx, NL_EL (tree->u.ops, 1));
      cond = v2.uns_p ? v2.u.u_val != 0 : v2.u.i_val != 0;
    }
    res.uns_p = FALSE;
    res.u.i_val = cond;
    break;
  case N_COND:
    v1 = eval (c2m_ctx, NL_HEAD (tree->u.ops));
    cond = v1.uns_p ? v1.u.u_val != 0 : v1.u.i_val != 0;
    res = eval (c2m_ctx, NL_EL (tree->u.ops, cond ? 1 : 2));
    break;
  default:
    res.uns_p = FALSE;
    res.u.i_val = 0;
    assert (FALSE);
  }
  return res;
}

static macro_call_t try_param_macro_call (c2m_ctx_t c2m_ctx, macro_t m, token_t macro_id) {
  MIR_alloc_t alloc = c2m_alloc (c2m_ctx);
  pre_ctx_t pre_ctx = c2m_ctx->pre_ctx;
  macro_call_t mc;
  token_t t1 = get_next_pptoken (c2m_ctx), t2 = NULL;

  while (t1->code == T_EOR) {
    pop_macro_call (c2m_ctx);
    t1 = get_next_pptoken (c2m_ctx);
  }
  if (t1->code == ' ' || t1->code == '\n') {
    t2 = t1;
    t1 = get_next_pptoken (c2m_ctx);
  }
  if (t1->code != '(') { /* no args: it is not a macro call */
    unget_next_pptoken (c2m_ctx, t1);
    if (t2 != NULL) unget_next_pptoken (c2m_ctx, t2);
    out_token (c2m_ctx, macro_id);
    return NULL;
  }
  mc = new_macro_call (alloc, m, macro_id->pos);
  find_args (c2m_ctx, mc);
  VARR_PUSH (macro_call_t, macro_call_stack, mc);
  return mc;
}

#define ADD_OVERFLOW "__builtin_add_overflow"
#define SUB_OVERFLOW "__builtin_sub_overflow"
#define MUL_OVERFLOW "__builtin_mul_overflow"
#define EXPECT "__builtin_expect"
#define JCALL "__builtin_jcall"
#define JRET "__builtin_jret"
#define PROP_SET "__builtin_prop_set"
#define PROP_EQ "__builtin_prop_eq"
#define PROP_NE "__builtin_prop_ne"
/* GNU-style atomics (seq_cst; order args ignored in v1).  See CLASSY-ATOMICS.md. */
#define ATOMIC_LOAD_N "__atomic_load_n"
#define ATOMIC_STORE_N "__atomic_store_n"
#define ATOMIC_EXCHANGE_N "__atomic_exchange_n"
#define ATOMIC_FETCH_ADD "__atomic_fetch_add"
#define ATOMIC_FETCH_SUB "__atomic_fetch_sub"
#define ATOMIC_FETCH_AND "__atomic_fetch_and"
#define ATOMIC_FETCH_OR "__atomic_fetch_or"
#define ATOMIC_FETCH_XOR "__atomic_fetch_xor"
#define ATOMIC_COMPARE_EXCHANGE_N "__atomic_compare_exchange_n"
#define ATOMIC_THREAD_FENCE "__atomic_thread_fence"

static void processing (c2m_ctx_t c2m_ctx, int ignore_directive_p) {
  MIR_alloc_t alloc = c2m_alloc (c2m_ctx);
  pre_ctx_t pre_ctx = c2m_ctx->pre_ctx;
  token_t t;
  struct macro macro_struct;
  macro_t m;
  macro_call_t mc;
  int newln_p;

  for (newln_p = TRUE;;) { /* Main loop. */
    /* False #if body: skip whole lines until the next directive or EOF
       without tokenizing identifiers (big win on system headers). */
    if (skip_if_part_p && newln_p && !ignore_directive_p
        && VARR_LENGTH (macro_call_t, macro_call_stack) == 0
        && (buffered_tokens == NULL || VARR_LENGTH (token_t, buffered_tokens) == 0)) {
      if (pre_fast_skip_to_directive (c2m_ctx)) {
        t = get_next_pptoken (c2m_ctx);
        if (t->code == '#') {
          process_directive (c2m_ctx);
          newln_p = TRUE;
          continue;
        }
        /* Rare: '#' was consumed by something else — fall through. */
      } else {
        t = get_next_pptoken (c2m_ctx);
        goto handle_eof_token;
      }
    }

    t = get_next_pptoken (c2m_ctx);
    if (t->code == T_EOP) return; /* end of processing */
    if (newln_p && !ignore_directive_p && t->code == '#') {
      process_directive (c2m_ctx);
      continue;
    }
    if (t->code == '\n') {
      newln_p = TRUE;
      out_token (c2m_ctx, t);
      continue;
    } else if (t->code == ' ') {
      out_token (c2m_ctx, t);
      continue;
    } else if (t->code == T_EOFILE || t->code == T_EOU) {
    handle_eof_token:
      if ((int) VARR_LENGTH (ifstate_t, ifs)
          > (eof_s == NULL ? 0 : eof_s->ifs_length_at_stream_start)) {
        error (c2m_ctx, VARR_LAST (ifstate_t, ifs)->if_pos, "unfinished #if");
      }
      if (t->code == T_EOU) return;
      /* Record include-guard for the stream that just ended (eof_s). */
      if (eof_s != NULL && eof_s->ig_state == IG_AFTER_ENDIF && eof_s->ig_macro != NULL
          && eof_s->fname != NULL)
        record_include_guard (c2m_ctx, eof_s->fname, eof_s->ig_macro);
      while ((int) VARR_LENGTH (ifstate_t, ifs) > eof_s->ifs_length_at_stream_start)
        pop_ifstate (c2m_ctx);
      skip_if_part_p = VARR_LENGTH (ifstate_t, ifs) == 0 ? 0 : VARR_LAST (ifstate_t, ifs)->skip_p;
      newln_p = TRUE;
      continue;
    } else if (skip_if_part_p) {
      skip_nl (c2m_ctx, t, NULL);
      newln_p = TRUE;
      continue;
    }
    newln_p = FALSE;
    ig_note_significant_token (cs, t);
    if (t->code == T_EOR) {  // finish macro call
      pop_macro_call (c2m_ctx);
      continue;
    } else if (t->code == T_EOA) { /* arg end: add the result to repl_buffer */
      mc = VARR_LAST (macro_call_t, macro_call_stack);
      add_arg_tokens (mc->repl_buffer, output_buffer);
#ifdef C2MIR_PREPRO_DEBUG
      fprintf (stderr, "adding processed arg to output buffer\n");
#endif
      process_replacement (c2m_ctx, mc);
      continue;
    } else if (t->code != T_ID) {
      out_token (c2m_ctx, t);
      continue;
    }
    macro_struct.id = t;
    if (!HTAB_DO (macro_t, macro_tab, &macro_struct, HTAB_FIND, m)) {
      if (!process_pragma (c2m_ctx, t)) out_token (c2m_ctx, t);
      continue;
    }
    if (m->replacement == NULL) { /* standard macro */
      if (strcmp (t->repr, "__STDC__") == 0) {
        out_token (c2m_ctx, new_node_token (c2m_ctx, t->pos, "1", T_NUMBER,
                                            new_i_node (c2m_ctx, 1, t->pos)));
      } else if (strcmp (t->repr, "__STDC_HOSTED__") == 0) {
        out_token (c2m_ctx, new_node_token (c2m_ctx, t->pos, "1", T_NUMBER,
                                            new_i_node (c2m_ctx, 1, t->pos)));
      } else if (strcmp (t->repr, "__STDC_VERSION__") == 0) {
        out_token (c2m_ctx, new_node_token (c2m_ctx, t->pos, "201112L", T_NUMBER,
                                            new_l_node (c2m_ctx, 201112, t->pos)));  // ???
      } else if (strcmp (t->repr, "__FILE__") == 0) {
        stringify (t->pos.fname, temp_string);
        VARR_PUSH (char, temp_string, '\0');
        t = new_node_token (c2m_ctx, t->pos, VARR_ADDR (char, temp_string), T_STR,
                            new_str_node (c2m_ctx, N_STR, empty_str, t->pos));
        set_string_val (c2m_ctx, t, temp_string, ' ');
        out_token (c2m_ctx, t);
      } else if (strcmp (t->repr, "__LINE__") == 0) {
        char str[50];

        sprintf (str, "%d", t->pos.lno);
        out_token (c2m_ctx, new_node_token (c2m_ctx, t->pos, str, T_NUMBER,
                                            new_i_node (c2m_ctx, t->pos.lno, t->pos)));
      } else if (strcmp (t->repr, "__DATE__") == 0) {
        t = new_node_token (c2m_ctx, t->pos, date_str_repr, T_STR,
                            new_str_node (c2m_ctx, N_STR, uniq_cstr (c2m_ctx, date_str), t->pos));
        out_token (c2m_ctx, t);
      } else if (strcmp (t->repr, "__TIME__") == 0) {
        t = new_node_token (c2m_ctx, t->pos, time_str_repr, T_STR,
                            new_str_node (c2m_ctx, N_STR, uniq_cstr (c2m_ctx, time_str), t->pos));
        out_token (c2m_ctx, t);
      } else if (strcmp (t->repr, "__has_include") == 0) {
        int res;
        VARR (token_t) * arg;
        const char *name, *content;
        FILE *f;

        if ((mc = try_param_macro_call (c2m_ctx, m, t)) != NULL) {
          unget_next_pptoken (c2m_ctx, new_token (c2m_ctx, t->pos, "", T_EOR, N_IGNORE));
          if (VARR_LENGTH (token_arr_t, mc->args) != 1) {
            res = 0;
          } else {
            arg = VARR_LAST (token_arr_t, mc->args);
            if ((name = get_header_name (c2m_ctx, arg, t->pos, &content)) != NULL) {
              res = content != NULL || ((f = fopen (name, "r")) != NULL && !fclose (f)) ? 1 : 0;
            } else {
              error (c2m_ctx, t->pos, "wrong arg of predefined __has_include");
              res = 0;
            }
          }
          m->ignore_p = TRUE;
          unget_next_pptoken (c2m_ctx, new_node_token (c2m_ctx, t->pos, res ? "1" : "0", T_NUMBER,
                                                       new_i_node (c2m_ctx, res, t->pos)));
        }
      } else if (strcmp (t->repr, "__has_builtin") == 0) {
        int res;
        size_t i, len;
        VARR (token_t) * arg;

        res = 0;
        if ((mc = try_param_macro_call (c2m_ctx, m, t)) != NULL) {
          unget_next_pptoken (c2m_ctx, new_token (c2m_ctx, t->pos, "", T_EOR, N_IGNORE));
          if (VARR_LENGTH (token_arr_t, mc->args) != 1) {
            error (c2m_ctx, t->pos, "wrong number of args for __has_builtin");
          } else {
            arg = VARR_LAST (token_arr_t, mc->args);
            len = VARR_LENGTH (token_t, arg);
            i = 0;
            if (i < len && VARR_GET (token_t, arg, i)->code == ' ') i++;
            if (i >= len || (t = VARR_GET (token_t, arg, i))->code != T_ID) {
              error (c2m_ctx, t->pos, "__has_builtin requires identifier");
            } else {
              i++;
              if (i < len && VARR_GET (token_t, arg, i)->code == ' ') i++;
              if (i != len)
                error (c2m_ctx, t->pos, "garbage after identifier in __has_builtin");
              else
                res = (strcmp (t->repr, ADD_OVERFLOW) == 0 || strcmp (t->repr, SUB_OVERFLOW) == 0
                       || strcmp (t->repr, MUL_OVERFLOW) == 0 || strcmp (t->repr, EXPECT) == 0
                       || strcmp (t->repr, JCALL) == 0 || strcmp (t->repr, JRET) == 0
                       || strcmp (t->repr, PROP_SET) == 0 || strcmp (t->repr, PROP_EQ) == 0
                       || strcmp (t->repr, PROP_NE) == 0
                       || strcmp (t->repr, ATOMIC_LOAD_N) == 0
                       || strcmp (t->repr, ATOMIC_STORE_N) == 0
                       || strcmp (t->repr, ATOMIC_EXCHANGE_N) == 0
                       || strcmp (t->repr, ATOMIC_FETCH_ADD) == 0
                       || strcmp (t->repr, ATOMIC_FETCH_SUB) == 0
                       || strcmp (t->repr, ATOMIC_FETCH_AND) == 0
                       || strcmp (t->repr, ATOMIC_FETCH_OR) == 0
                       || strcmp (t->repr, ATOMIC_FETCH_XOR) == 0
                       || strcmp (t->repr, ATOMIC_COMPARE_EXCHANGE_N) == 0
                       || strcmp (t->repr, ATOMIC_THREAD_FENCE) == 0);
            }
          }
          m->ignore_p = TRUE;
          unget_next_pptoken (c2m_ctx, new_node_token (c2m_ctx, t->pos, res ? "1" : "0", T_NUMBER,
                                                       new_i_node (c2m_ctx, res, t->pos)));
        }
      } else {
        assert (FALSE);
      }
      continue;
    }
    if (m->ignore_p) {
      t->code = T_NO_MACRO_IDENT;
      out_token (c2m_ctx, t);
      continue;
    }
    if (m->params == NULL) { /* macro without parameters */
      unget_next_pptoken (c2m_ctx, new_token (c2m_ctx, t->pos, "", T_EOR, N_IGNORE));
#ifdef C2MIR_PREPRO_DEBUG
      fprintf (stderr, "# push back <EOR>\n");
#endif
      mc = new_macro_call (alloc, m, t->pos);
      add_tokens (mc->repl_buffer, m->replacement);
      copy_and_push_back (c2m_ctx, do_concat (c2m_ctx, mc->repl_buffer), mc->pos);
      m->ignore_p = TRUE;
      VARR_PUSH (macro_call_t, macro_call_stack, mc);
    } else if ((mc = try_param_macro_call (c2m_ctx, m, t)) != NULL) { /* macro with parameters */
      process_replacement (c2m_ctx, mc);
    }
  }
}

static void pre_text_out (c2m_ctx_t c2m_ctx, token_t t) { /* NULL means end of output */
  pre_ctx_t pre_ctx = c2m_ctx->pre_ctx;
  int i;
  FILE *f = c2m_options->prepro_output_file;

  if (t == NULL && pre_last_token != NULL && pre_last_token->code == '\n') {
    fprintf (f, "\n");
    return;
  }
  if (t->code == '\n') {
    pre_last_token = t;
    return;
  }
  if (actual_pre_pos.fname != t->pos.fname || actual_pre_pos.lno != t->pos.lno) {
    if (actual_pre_pos.fname == t->pos.fname && actual_pre_pos.lno < t->pos.lno
        && actual_pre_pos.lno + 4 >= t->pos.lno) {
      for (; actual_pre_pos.lno != t->pos.lno; actual_pre_pos.lno++) fprintf (f, "\n");
    } else {
      if (pre_last_token != NULL) fprintf (f, "\n");
      fprintf (f, "#line %d", t->pos.lno);
      if (actual_pre_pos.fname != t->pos.fname) {
        stringify (t->pos.fname, temp_string);
        VARR_PUSH (char, temp_string, '\0');
        fprintf (f, " %s", VARR_ADDR (char, temp_string));
      }
      fprintf (f, "\n");
    }
    for (i = 0; i < t->pos.ln_pos - 1; i++) fprintf (f, " ");
    actual_pre_pos = t->pos;
  }
  fprintf (f, "%s", t->code == ' ' ? " " : t->repr);
  pre_last_token = t;
}

/* f-string (interpolated string) support.
 *
 * A narrow string literal with an `f` prefix, e.g.
 *
 *     f"hello {name}, you are {age} years old"
 *
 * is lowered, at token-recording time, into an ordinary String concatenation
 * expression that the existing parser / `+` overload / N_CONCAT codegen already
 * handle:
 *
 *     ( (String)"hello " + (name) + ", you are " + (age) + " years old" )
 *
 * The leading `(String)` cast anchors the chain as a genuine `String`, so that
 * interpolated arithmetic operands (int, bool, char, ...) are auto-cast to text
 * (see the N_ADD -> N_CONCAT overload in `check`).  `{{` and `}}` denote literal
 * `{` / `}` characters.  Each `{ ... }` holds an arbitrary C expression.
 *
 * Implementation: build the replacement as source text and re-lex it (the same
 * technique `token_concat` uses for `##`), recording the resulting parser tokens
 * in place of the single f-string token.  Note: macros are not expanded inside
 * `{ ... }` (the interpolated text is lexed directly, not run back through the
 * preprocessor). */

/* Append a NUL-terminated string's characters (without the terminator) to a
   VARR(char).  Shared by the source-text builders below (f-strings, Any class
   and thunk synthesis). */
static void varr_str_push (VARR (char) * to, const char *s) {
  for (; *s != '\0'; s++) VARR_PUSH (char, to, *s);
}

static void fstring_record_expansion (c2m_ctx_t c2m_ctx, token_t fstr) {
  MIR_alloc_t alloc = c2m_alloc (c2m_ctx);
  const char *raw = fstr->repr; /* e.g.  f"hello {name}"  */
  size_t len = strlen (raw);
  VARR (char) * exp;
  size_t i, end;

#define FS_PUT_S(s) varr_str_push (exp, (s))
#define FS_PUT_C(ch) VARR_PUSH (char, exp, (char) (ch))

  VARR_CREATE (char, exp, alloc, 256);
  FS_PUT_S ("((String)\"");
  i = 2;          /* skip the leading  f"  */
  end = len - 1;  /* index of the closing "  */
  while (i < end) {
    char ch = raw[i];
    if (ch == '{' && i + 1 < end && raw[i + 1] == '{') { FS_PUT_C ('{'); i += 2; continue; }
    if (ch == '}' && i + 1 < end && raw[i + 1] == '}') { FS_PUT_C ('}'); i += 2; continue; }
    if (ch == '{') { /* start of an interpolated expression */
      int depth = 1;
      FS_PUT_S ("\"+(");   /* close the literal, open the expression */
      i++;
      while (i < end && depth > 0) {
        char ec = raw[i];
        /* A backslash inside { ... } only escaped the surrounding f-string
           literal (e.g.  \"  used to embed a quote in  f"{ f(\"x\") }" ).
           Undo that so the expression is valid C source again. */
        if (ec == '\\' && i + 1 < end) {
          char nx = raw[i + 1];
          if (nx == '"') FS_PUT_C ('"');
          else if (nx == '\\') FS_PUT_C ('\\');
          else { FS_PUT_C ('\\'); FS_PUT_C (nx); }
          i += 2;
          continue;
        }
        if (ec == '{') {
          depth++;
        } else if (ec == '}') {
          if (--depth == 0) { i++; break; }
        }
        FS_PUT_C (ec);
        i++;
      }
      FS_PUT_S (")+\"");   /* close the expression, reopen a literal */
      continue;
    }
    if (ch == '\\' && i + 1 < end) { /* keep escape sequences intact */
      FS_PUT_C ('\\');
      FS_PUT_C (raw[i + 1]);
      i += 2;
      continue;
    }
    FS_PUT_C (ch);
    i++;
  }
  FS_PUT_S ("\")");
  VARR_PUSH (char, exp, '\0');

  /* Re-lex the replacement text.  The string-stream consumer reads characters
     in reverse (see cs_get), so the buffer must be reversed first -- exactly as
     token_concat does. */
  reverse (exp);
  set_string_stream (c2m_ctx, VARR_ADDR (char, exp), fstr->pos, NULL);
  for (;;) {
    token_t pt = get_next_pptoken (c2m_ctx);
    token_t cv;
    if (pt->code == T_EOFILE || pt->code == T_EOU) break; /* string stream exhausted */
    if ((cv = pptoken2token (c2m_ctx, pt, TRUE)) == NULL) continue; /* whitespace */
    VARR_PUSH (token_t, recorded_tokens, cv);
  }
  VARR_DESTROY (char, exp);
#undef FS_PUT_S
#undef FS_PUT_C
}

static void pre_out (c2m_ctx_t c2m_ctx, token_t t) {
  pre_ctx_t pre_ctx = c2m_ctx->pre_ctx;

  if (t == NULL) {
    t = new_token (c2m_ctx, pre_last_token == NULL ? no_pos : pre_last_token->pos, "<EOF>",
                   T_EOFILE, N_IGNORE);
  } else {
    assert (t->code != T_EOU && t->code != EOF);
    pre_last_token = t;
    if ((t = pptoken2token (c2m_ctx, t, TRUE)) == NULL) return;
  }
  if (t->code == T_STR && t->node != NULL && t->node->code == N_STRING
      && t->repr != NULL && t->repr[0] == 'f' && t->repr[1] == '"') {
    /* f"..." interpolated string: expand into a String concat expression. */
    fstring_record_expansion (c2m_ctx, t);
    return;
  }
  if (t->code == T_STR && VARR_LENGTH (token_t, recorded_tokens) != 0
      && VARR_LAST (token_t, recorded_tokens)->code == T_STR) { /* concat strings */
    token_t last_t = VARR_POP (token_t, recorded_tokens);
    int type = ' ', last_t_quot_off = 0, t_quot_off = 0, err_p = FALSE;
    const char *s;

    VARR_TRUNC (char, temp_string, 0);
    if (last_t->repr[0] == 'u' && last_t->repr[1] == '8') {
      err_p = t->repr[0] != '\"' && (t->repr[0] != 'u' || t->repr[1] != '8');
      last_t_quot_off = 2;
    } else if (last_t->repr[0] == 'L' || last_t->repr[0] == 'u' || last_t->repr[0] == 'U') {
      err_p = t->repr[0] != '\"' && (t->repr[0] != last_t->repr[0] || t->repr[1] == '8');
      last_t_quot_off = 1;
    }
    if (t->repr[0] == 'u' && t->repr[1] == '8') {
      err_p = last_t->repr[0] != '\"' && (last_t->repr[0] != 'u' || last_t->repr[1] != '8');
      t_quot_off = 2;
    } else if (t->repr[0] == 'L' || t->repr[0] == 'u' || t->repr[0] == 'U') {
      err_p = last_t->repr[0] != '\"' && (t->repr[0] != last_t->repr[0] || last_t->repr[1] == '8');
      t_quot_off = 1;
    }
    if (err_p) error (c2m_ctx, t->pos, "concatenation of different type string literals");
    if (sizeof (mir_wchar) == 4 && (last_t->repr[0] == 'L' || t->repr[0] == 'L')) {
      type = 'L';
    } else if (last_t->repr[0] == 'U' || t->repr[0] == 'U') {
      type = 'U';
    } else if (last_t->repr[0] == 'L' || t->repr[0] == 'L') {
      type = 'L';
    } else if ((last_t->repr[0] == 'u' && last_t->repr[1] == '8')
               || (t->repr[0] == 'u' && t->repr[1] == '8')) {
      VARR_PUSH (char, temp_string, 'u');
      type = '8';
    } else if ((last_t->repr[0] == 'u' || t->repr[0] == 'u')) {
      type = 'u';
    }
    if (type != ' ') VARR_PUSH (char, temp_string, type);
    for (s = last_t->repr + last_t_quot_off; *s != 0; s++) VARR_PUSH (char, temp_string, *s);
    assert (VARR_LAST (char, temp_string) == '"');
    VARR_POP (char, temp_string);
    for (s = t->repr + t_quot_off + 1; *s != 0; s++) VARR_PUSH (char, temp_string, *s);
    t = last_t;
    assert (VARR_LAST (char, temp_string) == '"');
    VARR_PUSH (char, temp_string, '\0');
    t->repr = uniq_cstr (c2m_ctx, VARR_ADDR (char, temp_string)).s;
    set_string_val (c2m_ctx, t, temp_string, type);
  }
  if (recorded_tokens != NULL) VARR_PUSH (token_t, recorded_tokens, t);
}

static void common_pre_out (c2m_ctx_t c2m_ctx, token_t t) {
  pre_ctx_t pre_ctx = c2m_ctx->pre_ctx;

  pptokens_num++;
  (c2m_options->prepro_only_p ? pre_text_out : pre_out) (c2m_ctx, t);
}

static void pre (c2m_ctx_t c2m_ctx) {
  pre_ctx_t pre_ctx = c2m_ctx->pre_ctx;

  pre_last_token = NULL;
  actual_pre_pos.fname = NULL;
  actual_pre_pos.lno = 0;
  actual_pre_pos.ln_pos = 0;
  pre_out_token_func = common_pre_out;
  pptokens_num = 0;
  VARR_TRUNC (char_ptr_t, once_include_files, 0);
  if (include_guard_tab != NULL) HTAB_CLEAR (include_guard_t, include_guard_tab);
  /* Path/resolve caches are valid for the whole compile unit (include search
     paths don't change mid-TU).  Keep them across a single pre() run; they are
     created empty in pre_init and destroyed in pre_finish.  If pre() is ever
     re-entered with a fresh pre_ctx they start empty again. */
  if (!c2m_options->no_prepro_p) {
    processing (c2m_ctx, FALSE);
  } else {
    for (;;) {
      token_t t = get_next_pptoken (c2m_ctx);

      if (t->code == T_EOFILE || t->code == T_EOU) break;
      pre_out_token_func (c2m_ctx, t);
    }
  }
  pre_out_token_func (c2m_ctx, NULL);
  if (c2m_options->verbose_p && c2m_options->message_file != NULL)
    fprintf (c2m_options->message_file, "    preprocessor tokens -- %lu, parse tokens -- %lu\n",
             pptokens_num, (unsigned long) VARR_LENGTH (token_t, recorded_tokens));
}

/* ------------------------- Preprocessor End ------------------------------ */
