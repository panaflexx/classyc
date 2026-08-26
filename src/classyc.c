/* This file is a part of MIR project.
   Copyright (C) 2018-2024 Vladimir Makarov <vmakarov.gcc@gmail.com>.
   Copyright (C) 2024-2026 Roger Davenport <rdavenpo@adamantine.org>
*/

/* C to MIR compiler.  It is a four pass compiler:
   o preprocessor pass generating tokens
   o parsing pass generating AST
   o context pass checking context constraints and augmenting AST
   o generation pass producing MIR

   The compiler implements C11 standard w/o C11 features:
   complex, variable size arrays.
   Atomics: MIR ALOAD/ASTORE/RMW/CAS (seq_cst); see CLASSY-ATOMICS.md.

   o class, String, dict extensions make it Classy
   o
   */


#include <assert.h>
#include <string.h>
#include <ctype.h>
#include <float.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <errno.h>
#include <setjmp.h>
#include <math.h>
#include <wchar.h>
#include <sys/stat.h>
#include <limits.h>
#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#include "mir-alloc.h"
#include "mir.h"
#include "time.h"

#include "classyc.h"
#define LOGGER_IMPL /* this TU owns the logger's diagnostic-sink storage */
#include "logger.h"

#if defined(__x86_64__) || defined(_M_AMD64)
#include "x86_64/cx86_64.h"
#elif defined(__aarch64__)
#include "aarch64/caarch64.h"
#elif defined(__PPC64__)
#include "ppc64/cppc64.h"
#elif defined(__s390x__)
#include "s390x/cs390x.h"
#elif defined(__riscv)
#include "riscv64/criscv64.h"
#else
#error "undefined or unsupported generation target for C"
#endif

#define SWAP(a1, a2, t) \
  do {                  \
    t = a1;             \
    a1 = a2;            \
    a2 = t;             \
  } while (0)

typedef enum {
  C_alloc_error,
  C_unfinished_comment,
  C_out_of_range_number,
  C_invalid_char_constant,
  C_no_string_end,
  C_invalid_str_constant,
  C_invalid_char,
} C_error_code_t;

DEF_VARR (char);

typedef struct pos {
  const char *fname;
  int lno, ln_pos;
} pos_t;

static const pos_t no_pos = {NULL, -1, -1};

typedef struct c2m_ctx *c2m_ctx_t;

int c2m_pending_extra_gt = 0;

/* Include-guard recognition (clang/gcc MultipleIncludeOpt):
   IG_EXPECT_IFNDEF → first content must be #ifndef GUARD
   IG_EXPECT_DEFINE → next directive must be #define GUARD
   IG_IN_BODY       → matching #endif at stream-start if-depth
   IG_AFTER_ENDIF   → only whitespace until EOF → record guard
   IG_FAILED / IG_OFF → not a simple guarded header */
enum {
  IG_OFF = 0,
  IG_EXPECT_IFNDEF,
  IG_EXPECT_DEFINE,
  IG_IN_BODY,
  IG_AFTER_ENDIF,
  IG_FAILED
};

typedef struct stream {
  FILE *f;                        /* the current file, NULL for top-level or string stream */
  const char *fname;              /* NULL only for preprocessor string stream */
  int (*getc_func) (c2m_ctx_t);   /* get function for top-level or string stream */
  VARR (char) * ln;               /* stream current line in reverse order */
  pos_t pos;                      /* includes file name used for reports */
  fpos_t fpos;                    /* file pos to resume file stream */
  const char *start, *curr;       /* non NULL only for string stream  */
  int ifs_length_at_stream_start; /* length of ifs at the stream start */
  int ig_state;                   /* include-guard FSM (IG_*) */
  const char *ig_macro;           /* interned guard macro name, or NULL */
} *stream_t;

DEF_VARR (stream_t);

typedef const char *char_ptr_t;
DEF_VARR (char_ptr_t);

typedef void *void_ptr_t;
DEF_VARR (void_ptr_t);

typedef struct {
  const char *s;
  size_t len;
} str_t;

typedef struct {
  str_t str;
  size_t key, flags;
} tab_str_t;

DEF_HTAB (tab_str_t);

typedef struct token *token_t;
DEF_VARR (token_t);

typedef struct node *node_t;

enum symbol_mode { S_REGULARS, S_TAG, S_LABEL };

DEF_VARR (node_t);

typedef struct {
  enum symbol_mode mode;
  node_t id;
  node_t scope;
  node_t def_node, aux_node;
  VARR (node_t) * defs;
} symbol_t;

DEF_HTAB (symbol_t);

struct init_object {
  struct type *container_type;
  int field_designator_p;
  union {
    mir_llong curr_index;
    node_t curr_member;
  } u;
};

typedef struct init_object init_object_t;
DEF_VARR (init_object_t);

typedef struct pre_ctx *pre_ctx_t;
typedef struct parse_ctx *parse_ctx_t;
typedef struct check_ctx *check_ctx_t;
typedef struct gen_ctx *gen_ctx_t;

DEF_VARR (pos_t);

struct c2m_ctx {
  MIR_context_t ctx;
  struct c2mir_options *options;
  jmp_buf env; /* put it first as it might need 16-byte malloc allignment */
  VARR (char_ptr_t) * headers;
  VARR (char_ptr_t) * system_headers;
  VARR (char_ptr_t) * fw_dirs;
  const char **header_dirs, **system_header_dirs, **fw_dir_list;
  void (*error_func) (c2m_ctx_t, C_error_code_t code, const char *message);
  VARR (void_ptr_t) * reg_memory;
  VARR (stream_t) * streams; /* stack of streams */
  stream_t cs, eof_s;        /* current stream and stream corresponding the last EOF */
  HTAB (tab_str_t) * str_tab;
  HTAB (tab_str_t) * str_key_tab;
  str_t empty_str;
  unsigned curr_uid;
  int (*c_getc) (void *); /* c2mir interface get function */
  void *c_getc_data;
  unsigned n_errors, n_warnings;
  VARR (char) * symbol_text, *temp_string;
  VARR (token_t) * recorded_tokens, *buffered_tokens;
  node_t top_scope;
  HTAB (symbol_t) * symbol_tab;
  VARR (pos_t) * node_positions;
  node_t analysis_root;     /* retained root when keep_syms_p was used */
  size_t analysis_reg_mark; /* reg_memory length at start of kept analysis run */
  int  analysis_kept_p;
  VARR (node_t) * call_nodes;
  VARR (node_t) * containing_anon_members;
  VARR (init_object_t) * init_object_path;
  char temp_str_buff[50];
  struct pre_ctx *pre_ctx;
  struct parse_ctx *parse_ctx;
  struct check_ctx *check_ctx;
  struct gen_ctx *gen_ctx;
};

typedef struct c2m_ctx *c2m_ctx_t;

#define c2m_options c2m_ctx->options
#define headers c2m_ctx->headers
#define system_headers c2m_ctx->system_headers
#define fw_dirs c2m_ctx->fw_dirs
#define header_dirs c2m_ctx->header_dirs
#define system_header_dirs c2m_ctx->system_header_dirs
#define fw_dir_list c2m_ctx->fw_dir_list
#define error_func c2m_ctx->error_func
#define reg_memory c2m_ctx->reg_memory
#define str_tab c2m_ctx->str_tab
#define streams c2m_ctx->streams
#define cs c2m_ctx->cs
#define eof_s c2m_ctx->eof_s
#define str_key_tab c2m_ctx->str_key_tab
#define empty_str c2m_ctx->empty_str
#define curr_uid c2m_ctx->curr_uid
#define c_getc c2m_ctx->c_getc
#define c_getc_data c2m_ctx->c_getc_data
#define n_errors c2m_ctx->n_errors
#define n_warnings c2m_ctx->n_warnings
#define symbol_text c2m_ctx->symbol_text
#define temp_string c2m_ctx->temp_string
#define recorded_tokens c2m_ctx->recorded_tokens
#define buffered_tokens c2m_ctx->buffered_tokens
#define top_scope c2m_ctx->top_scope
#define symbol_tab c2m_ctx->symbol_tab
#define node_positions c2m_ctx->node_positions
#define call_nodes c2m_ctx->call_nodes
#define containing_anon_members c2m_ctx->containing_anon_members
#define init_object_path c2m_ctx->init_object_path
#define temp_str_buff c2m_ctx->temp_str_buff

static inline c2m_ctx_t *c2m_ctx_loc (MIR_context_t ctx) {
  return (c2m_ctx_t *) ((void **) ctx + 1);
}

static void alloc_error (c2m_ctx_t c2m_ctx, const char *message) {
  error_func (c2m_ctx, C_alloc_error, message);
}

static const int max_nested_includes = 32;

#define MIR_VARR_ERROR alloc_error
#define MIR_HTAB_ERROR MIR_VARR_ERROR

#define FALSE 0
#define TRUE 1

#include "mir-varr.h"
#include "mir-dlist.h"
#include "mir-hash.h"
#include "mir-htab.h"

static mir_size_t round_size (mir_size_t size, mir_size_t round) {
  return (size + round - 1) / round * round;
}

/* Some abbreviations: */
#define NL_HEAD(list) DLIST_HEAD (node_t, list)
#define NL_TAIL(list) DLIST_TAIL (node_t, list)
#define NL_LENGTH(list) DLIST_LENGTH (node_t, list)
#define NL_NEXT(el) DLIST_NEXT (node_t, el)
#define NL_PREV(el) DLIST_PREV (node_t, el)
#define NL_REMOVE(list, el) DLIST_REMOVE (node_t, list, el)
#define NL_APPEND(list, el) DLIST_APPEND (node_t, list, el)
#define NL_PREPEND(list, el) DLIST_PREPEND (node_t, list, el)
#define NL_EL(list, n) DLIST_EL (node_t, list, n)

/* ===== AST Node Child Accessor Macros =====
 * These provide named access to the children of common multi-child node types,
 * eliminating raw NL_HEAD/NL_NEXT/NL_EL chains scattered throughout parse/check/gen.
 *
 * N_SPEC_DECL layout: specs(0) declarator(1) attrs(2) asm_part(3) initializer(4)
 * N_MEMBER    layout: specs(0) declarator(1) attrs(2) width(3)    init(4)
 * N_FUNC_DEF  layout: specs(0) declarator(1) declarations(2) block(3)
 * N_CALL      layout: func(0) arg_list(1)
 * N_DECL      layout: id(0)   decl_list(1)
 * N_STRUCT/N_UNION/N_CLASS layout: id(0) member_list(1)
 */

/* N_SPEC_DECL children */
#define SPEC_DECL_SPECS(n) NL_HEAD ((n)->u.ops)
#define SPEC_DECL_DECL(n) NL_NEXT (SPEC_DECL_SPECS (n))
#define SPEC_DECL_ATTRS(n) NL_NEXT (SPEC_DECL_DECL (n))
#define SPEC_DECL_ASM(n) NL_NEXT (SPEC_DECL_ATTRS (n))
#define SPEC_DECL_INIT(n) NL_NEXT (SPEC_DECL_ASM (n))

/* N_MEMBER children */
#define MEMBER_SPECS(n) NL_HEAD ((n)->u.ops)
#define MEMBER_DECL(n) NL_NEXT (MEMBER_SPECS (n))
#define MEMBER_ATTRS(n) NL_NEXT (MEMBER_DECL (n))
#define MEMBER_WIDTH(n) NL_NEXT (MEMBER_ATTRS (n))
#define MEMBER_INIT(n) NL_NEXT (MEMBER_WIDTH (n))

/* N_FUNC_DEF children */
#define FUNC_DEF_SPECS(n) NL_HEAD ((n)->u.ops)
#define FUNC_DEF_DECL(n) NL_NEXT (FUNC_DEF_SPECS (n))
#define FUNC_DEF_DECLS(n) NL_NEXT (FUNC_DEF_DECL (n))
#define FUNC_DEF_BLOCK(n) NL_NEXT (FUNC_DEF_DECLS (n))

/* N_CALL children */
#define CALL_FUNC(n) NL_HEAD ((n)->u.ops)
#define CALL_ARGS(n) NL_NEXT (CALL_FUNC (n))

/* N_DECL children */
#define DECL_ID(n) NL_HEAD ((n)->u.ops)
#define DECL_LIST(n) NL_NEXT (DECL_ID (n))

/* Struct/union/class tag children: id(0) member_list(1) */
#define TAG_ID(n) NL_HEAD ((n)->u.ops)
#define TAG_MEMBER_LIST(n) NL_EL ((n)->u.ops, 1)

/* Unwrap N_SHARE: if node is N_SHARE return its child, otherwise return node itself */
#define UNSHARE(specs) ((specs)->code != N_SHARE ? (specs) : NL_HEAD ((specs)->u.ops))

enum basic_type {
  TP_UNDEF,
  TP_VOID,
  /* Integer types: the first should be BOOL and the last should be
     ULLONG.  The order is important -- do not change it.  */
  TP_BOOL,
  TP_CHAR,
  TP_SCHAR,
  TP_UCHAR,
  TP_SHORT,
  TP_USHORT,
  TP_INT,
  TP_UINT,
  TP_LONG,
  TP_ULONG,
  TP_LLONG,
  TP_ULLONG,
  TP_FLOAT,
  TP_DOUBLE,
  TP_LDOUBLE,
  TP_STRING,
  TP_GENERIC,
};

static void print_type (c2m_ctx_t c2m_ctx, FILE *f, struct type *type);

struct type_qual {
  unsigned int const_p : 1, restrict_p : 1, volatile_p : 1, atomic_p : 1; /* Type qualifiers */
};

static const struct type_qual zero_type_qual = {0, 0, 0, 0};
static void error_recovery (c2m_ctx_t c2m_ctx, int par_lev, const char *expected);

struct arr_type {
  unsigned int static_p : 1;
  /* Last data member of a struct/class: C99 `T a[]`, GNU `T a[0]`, or the
     C89 “struct hack” `T a[1]`.  Declared length is not the live bound. */
  unsigned int flex_p : 1;
  struct type *el_type;
  struct type_qual ind_type_qual;
  node_t size;
  /* Sibling integer member used as the live length/capacity (e.g. nAlloc),
     or NULL when we could not name one.  N_MEMBER node. */
  node_t flex_bound_member;
};

struct func_type {
  unsigned int dots_p : 1;
  struct type *ret_type;
  node_t param_list; /* w/o N_DOTS */
  MIR_item_t proto_item;
  node_t class_scope; // Added to track class scope
};

enum type_mode {
  TM_UNDEF,
  TM_BASIC,
  TM_ENUM,
  TM_PTR,
  TM_STRUCT,
  TM_UNION,
  TM_CLASS,
  TM_ARR,
  TM_FUNC,
  TM_DICT,
  TM_SLICE, /* filter/map result: pointer to {i64 len; i64 pad; el[0] data} on the stack.
               Scalar (one pointer) at MIR level; u.ptr_type holds the element type. */
};

struct type {
  node_t pos_node;       /* set up and used only for checking type correctness */
  struct type *arr_type; /* NULL or array type before its adjustment */
  MIR_alias_t antialias; /* it can be non-zero only for pointers */
  struct type_qual type_qual;
  enum type_mode mode;
  char func_type_before_adjustment_p;
  char unnamed_anon_struct_union_member_type_p;
  int align; /* type align, undefined if < 0  */
  /* Raw type size (w/o alignment type itself requirement but with
     element alignment requirements), undefined if mir_size_max.  */
  mir_size_t raw_size;
  union {
    enum basic_type basic_type; /* also integer type */
    node_t tag_type;            /* struct/union/enum */
    struct type *ptr_type;
    struct arr_type *arr_type;
    struct func_type *func_type;
  } u;
};

/*!*/ static struct type VOID_TYPE
  = {.raw_size = MIR_SIZE_MAX, .align = -1, .mode = TM_BASIC, .u = {.basic_type = TP_VOID}};

static void set_type_layout (c2m_ctx_t c2m_ctx, struct type *type);

static mir_size_t raw_type_size (c2m_ctx_t c2m_ctx, struct type *type) {
  if (type->raw_size == MIR_SIZE_MAX) set_type_layout (c2m_ctx, type);
  if (n_errors != 0 && type->raw_size == MIR_SIZE_MAX) {
    /* Use safe values for programs with errors: */
    type->raw_size = 0;
    type->align = 1;
  }
  assert (type->raw_size != MIR_SIZE_MAX);
  return type->raw_size;
}

typedef struct {
  const char *name, *content;
} string_include_t;

#if defined(__x86_64__) || defined(_M_AMD64)
#include "x86_64/cx86_64-code.c"
#elif defined(__aarch64__)
#include "aarch64/caarch64-code.c"
#elif defined(__PPC64__)
#include "ppc64/cppc64-code.c"
#elif defined(__s390x__)
#include "s390x/cs390x-code.c"
#elif defined(__riscv)
#include "riscv64/criscv64-code.c"
#else
#error "undefined or unsupported generation target for C"
#endif

static inline MIR_alloc_t c2m_alloc (c2m_ctx_t c2m_ctx) {
  return MIR_get_alloc (c2m_ctx->ctx);
}

static void *reg_malloc (c2m_ctx_t c2m_ctx, size_t s) {
  MIR_alloc_t alloc = c2m_alloc (c2m_ctx);
  void *mem = MIR_malloc (alloc, s);

  if (mem == NULL) alloc_error (c2m_ctx, "no memory");
  VARR_PUSH (void_ptr_t, reg_memory, mem);
  return mem;
}

static void *reg_free(c2m_ctx_t c2m_ctx, void *mem) {
  MIR_alloc_t alloc = c2m_alloc (c2m_ctx);
  MIR_free ( alloc, mem );
  return NULL;
}

static void reg_memory_pop (c2m_ctx_t c2m_ctx, size_t mark) {
  MIR_alloc_t alloc = c2m_alloc (c2m_ctx);
  while (VARR_LENGTH (void_ptr_t, reg_memory) > mark)
    MIR_free (alloc, VARR_POP (void_ptr_t, reg_memory));
}

static size_t MIR_UNUSED reg_memory_mark (c2m_ctx_t c2m_ctx) {
  return VARR_LENGTH (void_ptr_t, reg_memory);
}
static void reg_memory_finish (c2m_ctx_t c2m_ctx) {
  reg_memory_pop (c2m_ctx, 0);
  VARR_DESTROY (void_ptr_t, reg_memory);
}

static void reg_memory_init (c2m_ctx_t c2m_ctx) {
  MIR_alloc_t alloc = c2m_alloc (c2m_ctx);
  VARR_CREATE (void_ptr_t, reg_memory, alloc, 4096);
}

static int char_is_signed_p (void) { return MIR_CHAR_MAX == MIR_SCHAR_MAX; }

enum str_flag { FLAG_EXT = 1, FLAG_C89, FLAG_EXT89 };

static int str_eq (tab_str_t str1, tab_str_t str2, void *arg MIR_UNUSED) {
  return str1.str.len == str2.str.len && memcmp (str1.str.s, str2.str.s, str1.str.len) == 0;
}
static htab_hash_t str_hash (tab_str_t str, void *arg MIR_UNUSED) {
  return (htab_hash_t) mir_hash (str.str.s, str.str.len, 0x42);
}
static int str_key_eq (tab_str_t str1, tab_str_t str2, void *arg MIR_UNUSED) {
  return str1.key == str2.key;
}
static htab_hash_t str_key_hash (tab_str_t str, void *arg MIR_UNUSED) {
  return (htab_hash_t) mir_hash64 (str.key, 0x24);
}

static str_t uniq_cstr (c2m_ctx_t c2m_ctx, const char *str);

static void str_init (c2m_ctx_t c2m_ctx) {
  MIR_alloc_t alloc = c2m_alloc (c2m_ctx);
  HTAB_CREATE (tab_str_t, str_tab, alloc, 1000, str_hash, str_eq, NULL);
  HTAB_CREATE (tab_str_t, str_key_tab, alloc, 200, str_key_hash, str_key_eq, NULL);
  empty_str = uniq_cstr (c2m_ctx, "");
}

static int str_exists_p (c2m_ctx_t c2m_ctx, const char *s, size_t len, tab_str_t *tab_str) {
  tab_str_t el, str;

  str.str.s = s;
  str.str.len = len;
  if (!HTAB_DO (tab_str_t, str_tab, str, HTAB_FIND, el)) return FALSE;
  *tab_str = el;
  return TRUE;
}

static tab_str_t str_add (c2m_ctx_t c2m_ctx, const char *s, size_t len, size_t key, size_t flags,
                          int key_p) {
  char *heap_s;
  tab_str_t el, str;

  if (str_exists_p (c2m_ctx, s, len, &el)) return el;
  heap_s = reg_malloc (c2m_ctx, len);
  memcpy (heap_s, s, len);
  str.str.s = heap_s;
  str.str.len = len;
  str.key = key;
  str.flags = flags;
  HTAB_DO (tab_str_t, str_tab, str, HTAB_INSERT, el);
  if (key_p) HTAB_DO (tab_str_t, str_key_tab, str, HTAB_INSERT, el);
  return str;
}

static const char *str_find_by_key (c2m_ctx_t c2m_ctx, size_t key) {
  tab_str_t el, str;

  str.key = key;
  if (!HTAB_DO (tab_str_t, str_key_tab, str, HTAB_FIND, el)) return NULL;
  return el.str.s;
}

static void str_finish (c2m_ctx_t c2m_ctx) {
  HTAB_DESTROY (tab_str_t, str_tab);
  HTAB_DESTROY (tab_str_t, str_key_tab);
}

static void *c2mir_calloc (c2m_ctx_t c2m_ctx, size_t size) {
  MIR_alloc_t alloc = c2m_alloc (c2m_ctx);
  void *res = MIR_calloc (alloc, 1, size);

  if (res == NULL) (*MIR_get_error_func (c2m_ctx->ctx)) (MIR_alloc_error, "no memory");
  return res;
}

void c2mir_init (MIR_context_t ctx) {
  MIR_alloc_t alloc = MIR_get_alloc (ctx);
  struct c2m_ctx **c2m_ctx_ptr = c2m_ctx_loc (ctx), *c2m_ctx;

  *c2m_ctx_ptr = c2m_ctx = MIR_calloc (alloc, 1, sizeof (struct c2m_ctx));
  if (c2m_ctx == NULL) (*MIR_get_error_func (ctx)) (MIR_alloc_error, "no memory");

  c2m_ctx->ctx = ctx;
  reg_memory_init (c2m_ctx);
  str_init (c2m_ctx);
}

void c2mir_finish (MIR_context_t ctx) {
  struct c2m_ctx **c2m_ctx_ptr = c2m_ctx_loc (ctx), *c2m_ctx = *c2m_ctx_ptr;

  str_finish (c2m_ctx);
  reg_memory_finish (c2m_ctx);
  reg_free (c2m_ctx, c2m_ctx);
  *c2m_ctx_ptr = NULL;
}

/* New Page */

/* Parser: tokens, AST construction, and translation-unit parsing.
   Included before preprocessor.c (which uses token_t defined here). */
#include "parser.c"
/* Preprocessor.  Single-TU build model: this file's body is pulled in here so
   it has visibility into all of classyc.c's internal types (c2m_ctx_t,
   token_t, pos_t, the T_* enum, VARR/HTAB machinery, error reporting, etc.).
   Must come AFTER the lexer/token definitions it depends on and BEFORE the
   parser, which consumes pre_out_token_func's token stream. */
#include "preprocessor.c"

/* New Page */
/* Context checker: types, monomorphization, const folding.  Same include
   model as preprocessor.c/parser.c — sits between the parser and the MIR
   generator, whose calls into check-time helpers keep their existing
   forward declarations. */
#include "check.c"

/* New Page */

/* MIR generator.  Same include model as preprocessor.c/parser.c/check.c —
   sits after the checker and before the debug-info/symbol-table helpers and
   c2mir_compile. */
#include "gen.c"

/* New Page */

static const char *get_node_name (node_code_t code) {
#define REP_SEP ;
#define C(n) \
  case N_##n: return #n
  switch (code) {
    C (IGNORE);
    REP8 (C, I, L, LL, U, UL, ULL, F, D);
    REP7 (C, LD, CH, CH16, CH32, STR, STR16, STR32);
    REP6 (C, ID, COMMA, ANDAND, OROR, EQ, STMTEXPR);
    REP8 (C, NE, LT, LE, GT, GE, ASSIGN, BITWISE_NOT, NOT);
    REP8 (C, AND, AND_ASSIGN, OR, OR_ASSIGN, XOR, XOR_ASSIGN, LSH, LSH_ASSIGN);
    REP8 (C, RSH, RSH_ASSIGN, ADD, ADD_ASSIGN, SUB, SUB_ASSIGN, MUL, MUL_ASSIGN);
    REP8 (C, DIV, DIV_ASSIGN, MOD, MOD_ASSIGN, IND, FIELD, ADDR, DEREF);
    REP8 (C, DEREF_FIELD, COND, INC, DEC, POST_INC, POST_DEC, ALIGNOF, SIZEOF);
    REP8 (C, EXPR_SIZEOF, CAST, COMPOUND_LITERAL, CALL, GENERIC, GENERIC_ASSOC, IF, SWITCH);
    REP8 (C, WHILE, DO, FOR, GOTO, INDIRECT_GOTO, CONTINUE, BREAK, RETURN);
    REP8 (C, EXPR, BLOCK, CASE, DEFAULT, LABEL, LABEL_ADDR, LIST, SPEC_DECL);
    REP8 (C, SHARE, TYPEDEF, EXTERN, STATIC, AUTO, REGISTER, THREAD_LOCAL, DECL);
    REP8 (C, VOID, CHAR, SHORT, INT, LONG, FLOAT, DOUBLE, SIGNED);
    REP8 (C, UNSIGNED, BOOL, STRUCT, UNION, ENUM, ENUM_CONST, MEMBER, CONST);
    REP8 (C, RESTRICT, VOLATILE, ATOMIC, INLINE, NO_RETURN, ALIGNAS, FUNC, STAR);
    REP8 (C, POINTER, DOTS, ARR, INIT, FIELD_ID, TYPE, ST_ASSERT, FUNC_DEF);
    REP7 (C, MODULE, ASM, ATTR, CLASS, STRING, CONCAT, DICT);
    C (IN); C (COALESCE); C (FORIN); C (NEW); C (DEFER); C (DELETE); C (LAMBDA); C (INTERFACE);
    C (ANY);
    C (TRY); C (CATCH); C (THROW);
    C (GO); C (AWAIT);
    /* Arena-ownership keywords (see Memory Management in README). */
    C (DETACH); C (ATTACH); C (UNOWNED);
    /* Managed-ownership keywords (owned/move/readonly layer). */
    C (MOVE); C (READONLY); C (OWNED);
  default: abort ();
  }
#undef C
#undef REP_SEP
}

static void print_char (FILE *f, mir_ulong ch) {
  if (ch >= 0x100) {
    fprintf (f, ch <= 0xFFFF ? "\\u%04x" : "\\U%08x", (unsigned int) ch);
  } else {
    if (ch == '"' || ch == '\"' || ch == '\\') fprintf (f, "\\");
    if (isprint (ch))
      fprintf (f, "%c", (unsigned int) ch);
    else
      fprintf (f, "\\%o", (unsigned int) ch);
  }
}

static void print_chars (FILE *f, const char *str, size_t len) {
  for (size_t i = 0; i < len; i++) print_char (f, str[i]);
}

static void print_chars16 (FILE *f, const char *str, size_t len) {
  for (size_t i = 0; i < len; i += 2) print_char (f, ((mir_char16 *) str)[i]);
}

static void print_chars32 (FILE *f, const char *str, size_t len) {
  for (size_t i = 0; i < len; i += 4) print_char (f, ((mir_char32 *) str)[i]);
}

static void print_node (c2m_ctx_t c2m_ctx, FILE *f, node_t n, int indent, int attr_p);

void debug_node (c2m_ctx_t c2m_ctx, node_t n) { print_node (c2m_ctx, stderr, n, 0, TRUE); }

static void print_ops (c2m_ctx_t c2m_ctx, FILE *f, node_t n, int indent, int attr_p) {
  int i;
  node_t op;

  for (i = 0; (op = get_op (n, i)) != NULL; i++) print_node (c2m_ctx, f, op, indent + 2, attr_p);
}

static void print_qual (FILE *f, struct type_qual type_qual) {
  if (type_qual.const_p) fprintf (f, ", const");
  if (type_qual.restrict_p) fprintf (f, ", restrict");
  if (type_qual.volatile_p) fprintf (f, ", volatile");
  if (type_qual.atomic_p) fprintf (f, ", atomic");
}

static void print_basic_type (FILE *f, enum basic_type basic_type) {
  switch (basic_type) {
  case TP_UNDEF: fprintf (f, "undef type"); break;
  case TP_VOID: fprintf (f, "void"); break;
  case TP_BOOL: fprintf (f, "bool"); break;
  case TP_CHAR: fprintf (f, "char"); break;
  case TP_SCHAR: fprintf (f, "signed char"); break;
  case TP_UCHAR: fprintf (f, "unsigned char"); break;
  case TP_SHORT: fprintf (f, "short"); break;
  case TP_USHORT: fprintf (f, "unsigned short"); break;
  case TP_INT: fprintf (f, "int"); break;
  case TP_UINT: fprintf (f, "unsigned int"); break;
  case TP_LONG: fprintf (f, "long"); break;
  case TP_ULONG: fprintf (f, "unsigned long"); break;
  case TP_LLONG: fprintf (f, "long long"); break;
  case TP_ULLONG: fprintf (f, "unsigned long long"); break;
  case TP_FLOAT: fprintf (f, "float"); break;
  case TP_DOUBLE: fprintf (f, "double"); break;
  case TP_LDOUBLE: fprintf (f, "long double"); break;
  case TP_STRING: fprintf (f, "String"); break;
  case TP_GENERIC: fprintf (f, "generic"); break;
  default: assert (FALSE);
  }
}
static void print_type (c2m_ctx_t c2m_ctx, FILE *f, struct type *type) {
  switch (type->mode) {
  case TM_UNDEF: fprintf (f, "undef type mode"); break;
  case TM_BASIC: print_basic_type (f, type->u.basic_type); break;
  case TM_ENUM: fprintf (f, "enum node %u", type->u.tag_type->uid); break;
  case TM_PTR:
    fprintf (f, "ptr (");
    print_type (c2m_ctx, f, type->u.ptr_type);
    fprintf (f, ")");
    if (type->arr_type != NULL) {
      fprintf (f, ", former ");
      print_type (c2m_ctx, f, type->arr_type);
    }
    if (type->func_type_before_adjustment_p) fprintf (f, ", former func");
    break;
  case TM_STRUCT: fprintf (f, "struct node %u", type->u.tag_type->uid); break;
  case TM_UNION: fprintf (f, "union node %u", type->u.tag_type->uid); break;
  case TM_CLASS: fprintf (f, "class node %u", type->u.tag_type->uid); break;
  case TM_ARR:
    fprintf (f, "array [%s", type->u.arr_type->static_p ? "static " : "");
    print_qual (f, type->u.arr_type->ind_type_qual);
    fprintf (f, "size node %u%s] (", type->u.arr_type->size->uid,
             type->u.arr_type->flex_p ? ", FAM" : "");
    print_type (c2m_ctx, f, type->u.arr_type->el_type);
    fprintf (f, ")");
    break;
  case TM_FUNC:
    fprintf (f, "func ");
    print_type (c2m_ctx, f, type->u.func_type->ret_type);
    fprintf (f, "(params node %u", type->u.func_type->param_list->uid);
    fprintf (f, type->u.func_type->dots_p ? ", ...)" : ")");
    break;
  case TM_DICT: fprintf (f, "dict"); break;
  case TM_SLICE:
    fprintf (f, "slice of ");
    print_type (c2m_ctx, f, type->u.ptr_type);
    break;
  default: assert (FALSE);
  }
  print_qual (f, type->type_qual);
  if (incomplete_type_p (c2m_ctx, type)) fprintf (f, ", incomplete");
  if (type->raw_size != MIR_SIZE_MAX)
    fprintf (f, ", raw size = %llu", (unsigned long long) type->raw_size);
  if (type->align >= 0) fprintf (f, ", align = %d", type->align);
  fprintf (f, " ");
}

static void print_decl_spec (c2m_ctx_t c2m_ctx, FILE *f, struct decl_spec *decl_spec) {
  if (decl_spec->typedef_p) fprintf (f, " typedef, ");
  if (decl_spec->extern_p) fprintf (f, " extern, ");
  if (decl_spec->static_p) fprintf (f, " static, ");
  if (decl_spec->auto_p) fprintf (f, " auto, ");
  if (decl_spec->register_p) fprintf (f, " register, ");
  if (decl_spec->thread_local_p) fprintf (f, " thread local, ");
  if (decl_spec->inline_p) fprintf (f, " inline, ");
  if (decl_spec->no_return_p) fprintf (f, " no return, ");
  if (decl_spec->align >= 0) fprintf (f, " align = %d, ", decl_spec->align);
  if (decl_spec->align_node != NULL)
    fprintf (f, " strictest align node %u, ", decl_spec->align_node->uid);
  if (decl_spec->linkage != N_IGNORE)
    fprintf (f, " %s linkage, ", decl_spec->linkage == N_STATIC ? "static" : "extern");
  print_type (c2m_ctx, f, decl_spec->type);
}

static void print_decl (c2m_ctx_t c2m_ctx, FILE *f, decl_t decl) {
  if (decl == NULL) return;
  fprintf (f, ": ");
  if (decl->scope != NULL) fprintf (f, "scope node = %u, ", decl->scope->uid);
  print_decl_spec (c2m_ctx, f, &decl->decl_spec);
  if (decl->addr_p) fprintf (f, ", addressable");
  if (decl->used_p) fprintf (f, ", used");
  if (decl->reg_p)
    fprintf (f, ", reg");
  else {
    fprintf (f, ", offset = %llu", (unsigned long long) decl->offset);
    if (decl->bit_offset >= 0) fprintf (f, ", bit offset = %d", decl->bit_offset);
  }
  if (decl->asm_p) fprintf (f, ", asm=%s", decl->u.asm_str);
}

static void print_expr (c2m_ctx_t c2m_ctx, FILE *f, struct expr *e) {
  if (e == NULL) return; /* e.g. N_ID which is not an expr */
  fprintf (f, ": ");
  if (e->u.lvalue_node) fprintf (f, "lvalue, ");
  print_type (c2m_ctx, f, e->type);
  if (e->const_p) {
    fprintf (f, ", const = ");
    if (!integer_type_p (e->type)) {
      fprintf (f, " %.*Lg\n", LDBL_MANT_DIG, (long double) e->c.d_val);
    } else if (signed_integer_type_p (e->type)) {
      fprintf (f, "%lld", (long long) e->c.i_val);
    } else {
      fprintf (f, "%llu", (unsigned long long) e->c.u_val);
    }
  }
}

static void print_node (c2m_ctx_t c2m_ctx, FILE *f, node_t n, int indent, int attr_p) {
  int i;
  int color = log_color_enabled (f);

  fprintf (f, "%s%6u:%s ", log_c (color, LOG_GRAY), n->uid, log_c (color, LOG_RESET));
  for (i = 0; i < indent; i++) fprintf (f, " ");
  if (n == err_node) {
    fprintf (f, "%s<error>%s\n", log_c (color, LOG_BRED), log_c (color, LOG_RESET));
    return;
  }
  fprintf (f, "%s%s%s %s(", log_c (color, LOG_BCYAN), get_node_name (n->code),
           log_c (color, LOG_RESET), log_c (color, LOG_GRAY));
  print_pos (f, POS (n), FALSE);
  fprintf (f, ")%s", log_c (color, LOG_RESET));
  switch (n->code) {
  case N_IGNORE: fprintf (f, "ignore\n"); break;
  case N_I: fprintf (f, " %lld", (long long) n->u.l); goto expr;
  case N_L: fprintf (f, " %lldl", (long long) n->u.l); goto expr;
  case N_LL: fprintf (f, " %lldll", (long long) n->u.ll); goto expr;
  case N_U: fprintf (f, " %lluu", (unsigned long long) n->u.ul); goto expr;
  case N_UL: fprintf (f, " %lluul", (unsigned long long) n->u.ul); goto expr;
  case N_ULL: fprintf (f, " %lluull", (unsigned long long) n->u.ull); goto expr;
  case N_F: fprintf (f, " %.*g", FLT_MANT_DIG, (double) n->u.f); goto expr;
  case N_D: fprintf (f, " %.*g", DBL_MANT_DIG, (double) n->u.d); goto expr;
  case N_LD: fprintf (f, " %.*Lg", LDBL_MANT_DIG, (long double) n->u.ld); goto expr;
  case N_CH:
  case N_CH16:
  case N_CH32:
    fprintf (f, " %s%s'", log_c (color, LOG_GREEN),
             n->code == N_CH ? "" : n->code == N_CH16 ? "u" : "U");
    print_char (f, n->u.ch);
    fprintf (f, "'%s", log_c (color, LOG_RESET));
    goto expr;
  case N_STR:
  case N_STR16:
  case N_STR32:
    fprintf (f, " %s%s\"", log_c (color, LOG_GREEN),
             n->code == N_STR ? "" : n->code == N_STR16 ? "u" : "U");
    (n->code == N_STR     ? print_chars
     : n->code == N_STR16 ? print_chars16
                          : print_chars32) (f, n->u.s.s, n->u.s.len);
    fprintf (f, "\"%s", log_c (color, LOG_RESET));
    goto expr;
  case N_ID:
    fprintf (f, " %s%s%s", log_c (color, LOG_YELLOW), n->u.s.s, log_c (color, LOG_RESET));
  expr:
    if (attr_p && n->attr != NULL) print_expr (c2m_ctx, f, n->attr);
    fprintf (f, "\n");
    break;
  case N_COMMA:
  case N_ANDAND:
  case N_OROR:
  case N_COALESCE:
  case N_EQ:
  case N_NE:
  case N_LT:
  case N_LE:
  case N_GT:
  case N_GE:
  case N_ASSIGN:
  case N_BITWISE_NOT:
  case N_NOT:
  case N_AND:
  case N_AND_ASSIGN:
  case N_OR:
  case N_OR_ASSIGN:
  case N_XOR:
  case N_XOR_ASSIGN:
  case N_LSH:
  case N_LSH_ASSIGN:
  case N_RSH:
  case N_RSH_ASSIGN:
  case N_ADD:
  case N_ADD_ASSIGN:
  case N_SUB:
  case N_SUB_ASSIGN:
  case N_MUL:
  case N_MUL_ASSIGN:
  case N_DIV:
  case N_DIV_ASSIGN:
  case N_MOD:
  case N_MOD_ASSIGN:
  case N_IND:
  case N_FIELD:
  case N_ADDR:
  case N_DEREF:
  case N_DEREF_FIELD:
  case N_COND:
  case N_INC:
  case N_DEC:
  case N_POST_INC:
  case N_POST_DEC:
  case N_ALIGNOF:
  case N_SIZEOF:
  case N_EXPR_SIZEOF:
  case N_CAST:
  case N_COMPOUND_LITERAL:
  case N_CALL:
  case N_GENERIC:
  case N_STMTEXPR:
  case N_LABEL_ADDR:
  case N_IN:
  case N_NEW:
  case N_ANY:
  case N_LAMBDA:
    if (attr_p && n->attr != NULL) print_expr (c2m_ctx, f, n->attr);
    fprintf (f, "\n");
    print_ops (c2m_ctx, f, n, indent, attr_p);
    break;
  case N_GENERIC_ASSOC:
  case N_IF:
  case N_WHILE:
  case N_DO:
  case N_FORIN:
  case N_CONTINUE:
  case N_BREAK:
  case N_RETURN:
  case N_DEFER:
  case N_DELETE:
  case N_DETACH:
  case N_ATTACH:
  case N_UNOWNED:
  case N_MOVE:
  case N_READONLY:
  case N_OWNED:
  case N_TRY:
  case N_CATCH:
  case N_THROW:
  case N_EXPR:
  case N_CASE:
  case N_DEFAULT:
  case N_LABEL:
  case N_SHARE:
  case N_TYPEDEF:
  case N_EXTERN:
  case N_STATIC:
  case N_AUTO:
  case N_REGISTER:
  case N_THREAD_LOCAL:
  case N_DECL:
  case N_VOID:
  case N_CHAR:
  case N_STRING:
  case N_SHORT:
  case N_INT:
  case N_LONG:
  case N_FLOAT:
  case N_DOUBLE:
  case N_SIGNED:
  case N_UNSIGNED:
  case N_BOOL:
  case N_CONST:
  case N_RESTRICT:
  case N_VOLATILE:
  case N_ATOMIC:
  case N_INLINE:
  case N_NO_RETURN:
  case N_ALIGNAS:
  case N_STAR:
  case N_POINTER:
  case N_DOTS:
  case N_ARR:
  case N_INIT:
  case N_FIELD_ID:
  case N_TYPE:
  case N_CONCAT:
  case N_ST_ASSERT:
  case N_ASM:
  case N_DICT:
    fprintf (f, "\n");
    print_ops (c2m_ctx, f, n, indent, attr_p);
    break;
  case N_ATTR:
    fprintf (f, "\n");
    print_ops (c2m_ctx, f, n, indent, attr_p);
    break;
  case N_INTERFACE: /* id(0), member_list(1) — no attr/scope */
    fprintf (f, "\n");
    print_ops (c2m_ctx, f, n, indent, attr_p);
    break;
  case N_LIST:
    if (attr_p && n->attr != NULL) {
      fprintf (f, ": ");
      print_decl_spec (c2m_ctx, f, (struct decl_spec *) n->attr);
    }
    fprintf (f, "\n");
    print_ops (c2m_ctx, f, n, indent, attr_p);
    break;
  case N_SPEC_DECL:
  case N_MEMBER:
  case N_FUNC_DEF:
    if (attr_p && n->attr != NULL) print_decl (c2m_ctx, f, (decl_t) n->attr);
    fprintf (f, "\n");
    print_ops (c2m_ctx, f, n, indent, attr_p);
    break;
  case N_FUNC:
    if (!attr_p || n->attr == NULL) {
      fprintf (f, "\n");
      print_ops (c2m_ctx, f, n, indent, attr_p);
      break;
    }
    /* fall through */
  case N_CLASS:
  case N_STRUCT:
  case N_UNION:
  case N_MODULE:
  case N_BLOCK:
  case N_FOR:
    if (n->code == N_CLASS && n->attr == (void *) ((intptr_t) -1)) {
      /* Generic class template: sentinel attr, never checked/generated. */
      fprintf (f, ": generic template\n");
      print_ops (c2m_ctx, f, n, indent, attr_p);
      break;
    }
    if (!attr_p
        || ((n->code == N_STRUCT || n->code == N_UNION || n->code == N_CLASS)
            && (NL_EL (n->u.ops, 1) == NULL || NL_EL (n->u.ops, 1)->code == N_IGNORE)))
      fprintf (f, "\n");
    else if (n->code == N_MODULE)
      fprintf (f, ": the top scope");
    else if (n->attr != NULL && ((struct node_scope *) n->attr)->scope != NULL)
      fprintf (f, ": higher scope node %u", ((struct node_scope *) n->attr)->scope->uid);
    if (n->code == N_STRUCT || n->code == N_UNION || n->code == N_CLASS)
      fprintf (f, "\n");
    else if (attr_p && n->attr != NULL)
      fprintf (f, ", size = %llu, offset = %llu\n",
               (unsigned long long) ((struct node_scope *) n->attr)->size,
               (unsigned long long) ((struct node_scope *) n->attr)->offset);
    print_ops (c2m_ctx, f, n, indent, attr_p);
    break;
  case N_SWITCH:
    if (attr_p && n->attr != NULL) {
      fprintf (f, ": ");
      print_type (c2m_ctx, f, &((struct switch_attr *) n->attr)->type);
    }
    fprintf (f, "\n");
    print_ops (c2m_ctx, f, n, indent, attr_p);
    break;
  case N_GOTO:
    if (attr_p && n->attr != NULL) fprintf (f, ": target node %u\n", ((node_t) n->attr)->uid);
    print_ops (c2m_ctx, f, n, indent, attr_p);
    break;
  case N_INDIRECT_GOTO: print_ops (c2m_ctx, f, n, indent, attr_p); break;
  case N_ENUM:
    if (attr_p && n->attr != NULL) {
      fprintf (f, ": enum_basic_type = ");
      print_basic_type (f, ((struct enum_type *) n->attr)->enum_basic_type);
      fprintf (f, "\n");
    }
    print_ops (c2m_ctx, f, n, indent, attr_p);
    break;
  case N_ENUM_CONST:
    if (attr_p && n->attr != NULL)  // ???!!!
      fprintf (f, ": val = %lld\n", (long long) ((struct enum_value *) n->attr)->u.i_val);
    print_ops (c2m_ctx, f, n, indent, attr_p);
    break;
  default: abort ();
  }
}

/* True if dir exists and looks like the ClassyC include tree (has list.h or cstring.h). */
static int classyc_include_dir_p (const char *dir) {
  char probe[PATH_MAX];
  struct stat st;
  size_t n;

  if (dir == NULL || dir[0] == '\0') return 0;
  if (stat (dir, &st) != 0 || !S_ISDIR (st.st_mode)) return 0;
  n = strlen (dir);
  if (n + 16 >= sizeof (probe)) return 0;
  memcpy (probe, dir, n);
  probe[n] = '/';
  strcpy (probe + n + 1, "list.h");
  if (stat (probe, &st) == 0 && S_ISREG (st.st_mode)) return 1;
  strcpy (probe + n + 1, "cstring.h");
  return stat (probe, &st) == 0 && S_ISREG (st.st_mode);
}

/* Push dir into header search lists if not already present.  `path` must outlive
   the compile (caller owns lifetime — typically MIR_malloc). */
static void push_include_dir (c2m_ctx_t c2m_ctx, const char *path) {
  size_t i, n;

  if (path == NULL || path[0] == '\0') return;
  n = VARR_LENGTH (char_ptr_t, headers);
  for (i = 0; i < n; i++) {
    const char *p = VARR_GET (char_ptr_t, headers, i);
    if (p != NULL && strcmp (p, path) == 0) return;
  }
  n = VARR_LENGTH (char_ptr_t, system_headers);
  for (i = 0; i < n; i++) {
    const char *p = VARR_GET (char_ptr_t, system_headers, i);
    if (p != NULL && strcmp (p, path) == 0) return;
  }
  VARR_PUSH (char_ptr_t, headers, path);
  VARR_PUSH (char_ptr_t, system_headers, path);
}

/* Resolve executable directory into out (no trailing slash).  Returns 0 on success. */
static int classyc_exe_dir (char *out, size_t out_sz) {
  char path[PATH_MAX], resolved[PATH_MAX];
  char *slash;

  if (out_sz < 2) return -1;
  path[0] = '\0';
#if defined(__APPLE__)
  {
    uint32_t sz = (uint32_t) sizeof (path);
    if (_NSGetExecutablePath (path, &sz) != 0) return -1;
  }
#elif defined(__linux__)
  {
    ssize_t n = readlink ("/proc/self/exe", path, sizeof (path) - 1);
    if (n < 0) return -1;
    path[n] = '\0';
  }
#else
  (void) path;
  return -1;
#endif
  if (realpath (path, resolved) != NULL) {
    strncpy (path, resolved, sizeof (path) - 1);
    path[sizeof (path) - 1] = '\0';
  }
  slash = strrchr (path, '/');
  if (slash == NULL) return -1;
  *slash = '\0';
  if (strlen (path) + 1 > out_sz) return -1;
  strcpy (out, path);
  return 0;
}

/* If base/rel is a ClassyC include dir, MIR_malloc a copy and push it. */
static void try_push_include_rel (c2m_ctx_t c2m_ctx, MIR_alloc_t alloc, const char *base,
                                  const char *rel) {
  char cand[PATH_MAX];
  char *copy;
  size_t n;

  if (base == NULL || rel == NULL) return;
  if (snprintf (cand, sizeof (cand), "%s/%s", base, rel) >= (int) sizeof (cand)) return;
  if (!classyc_include_dir_p (cand)) return;
  n = strlen (cand) + 1;
  copy = MIR_malloc (alloc, n);
  if (copy == NULL) return;
  memcpy (copy, cand, n);
  push_include_dir (c2m_ctx, copy);
}

static void init_include_dirs (c2m_ctx_t c2m_ctx) {
  MIR_alloc_t alloc = c2m_alloc (c2m_ctx);
  int MIR_UNUSED added_p = FALSE;
  char exe_dir[PATH_MAX], cwd[PATH_MAX];
  int i;

  VARR_CREATE (char_ptr_t, headers, alloc, 0);
  VARR_CREATE (char_ptr_t, system_headers, alloc, 0);
  VARR_CREATE (char_ptr_t, fw_dirs, alloc, 0);
  for (size_t j = 0; j < c2m_options->include_dirs_num; j++) {
    VARR_PUSH (char_ptr_t, headers, c2m_options->include_dirs[j]);
    VARR_PUSH (char_ptr_t, system_headers, c2m_options->include_dirs[j]);
  }
  if (c2m_options->framework_dirs != NULL) {
    for (size_t j = 0; j < c2m_options->framework_dirs_num; j++) {
      const char *fd = c2m_options->framework_dirs[j];
      if (fd != NULL && fd[0] != '\0') VARR_PUSH (char_ptr_t, fw_dirs, fd);
    }
  }

  /* Auto-discover ClassyC's own include/ (so -I include is usually unnecessary).
     Prefer paths next to the binary (build tree: bin/classyc → ../include;
     install: .../bin → ../include or share), then cwd.  Extra paths: CFLAGS -I… */
  if (classyc_exe_dir (exe_dir, sizeof (exe_dir)) == 0) {
    static const char *const rels[]
      = {"../include", "../include/classyc", "include", "../../include",
         "../share/classyc/include", NULL};
    for (i = 0; rels[i] != NULL; i++) try_push_include_rel (c2m_ctx, alloc, exe_dir, rels[i]);
  }
  if (getcwd (cwd, sizeof (cwd)) != NULL) try_push_include_rel (c2m_ctx, alloc, cwd, "include");

  /* Site packages (installed ClassyC libs / third-party cy headers). */
#if defined(__APPLE__) || defined(__unix__)
  {
    struct stat st;
    if (stat ("/usr/local/cy", &st) == 0 && S_ISDIR (st.st_mode))
      push_include_dir (c2m_ctx, "/usr/local/cy");
  }
#endif

  VARR_PUSH (char_ptr_t, headers, NULL);
#if defined(__APPLE__) || defined(__unix__)
  VARR_PUSH (char_ptr_t, system_headers, "/usr/local/include");
#endif
#ifdef ADDITIONAL_INCLUDE_PATH
  if (ADDITIONAL_INCLUDE_PATH[0] != 0) {
    added_p = TRUE;
    VARR_PUSH (char_ptr_t, system_headers, ADDITIONAL_INCLUDE_PATH);
  }
#endif
#if defined(__APPLE__)
  if (!added_p) {
    static const char *const apple_sdk_includes[] = {
      "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include",
      "/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/include",
      NULL};
    struct stat st;
    int sdk_added = FALSE;

    for (i = 0; apple_sdk_includes[i] != NULL; i++) {
      if (stat (apple_sdk_includes[i], &st) == 0 && S_ISDIR (st.st_mode)) {
        VARR_PUSH (char_ptr_t, system_headers, apple_sdk_includes[i]);
        sdk_added = TRUE;
      }
    }
    if (!sdk_added)
      VARR_PUSH (char_ptr_t, system_headers, apple_sdk_includes[0]);
  }
#endif
#if defined(__linux__) && defined(__x86_64__)
  VARR_PUSH (char_ptr_t, system_headers, "/usr/include/x86_64-linux-gnu");
#elif defined(__linux__) && defined(__aarch64__)
  VARR_PUSH (char_ptr_t, system_headers, "/usr/include/aarch64-linux-gnu");
#elif defined(__linux__) && defined(__PPC64__)
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  VARR_PUSH (char_ptr_t, system_headers, "/usr/include/powerpc64le-linux-gnu");
#else
  VARR_PUSH (char_ptr_t, system_headers, "/usr/include/powerpc64-linux-gnu");
#endif
#elif defined(__linux__) && defined(__s390x__)
  VARR_PUSH (char_ptr_t, system_headers, "/usr/include/s390x-linux-gnu");
#elif defined(__linux__) && defined(__riscv)
  VARR_PUSH (char_ptr_t, system_headers, "/usr/include/riscv64-linux-gnu");
#endif
#if defined(__APPLE__) || defined(__unix__)
  VARR_PUSH (char_ptr_t, system_headers, "/usr/include");
#endif
#if defined(__APPLE__)
  /* Default -F roots.  Headers live in the SDK; the runtime image is under
     /System/Library/Frameworks (see the JIT -framework loader). */
  {
    static const char *const apple_fw_dirs[] = {
      "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/System/Library/Frameworks",
      "/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/System/Library/Frameworks",
      "/System/Library/Frameworks",
      "/Library/Frameworks",
      NULL};
    struct stat st;
    int fi;
    for (fi = 0; apple_fw_dirs[fi] != NULL; fi++) {
      size_t k, n;
      if (stat (apple_fw_dirs[fi], &st) != 0 || !S_ISDIR (st.st_mode)) continue;
      n = VARR_LENGTH (char_ptr_t, fw_dirs);
      for (k = 0; k < n; k++) {
        const char *p = VARR_GET (char_ptr_t, fw_dirs, k);
        if (p != NULL && strcmp (p, apple_fw_dirs[fi]) == 0) break;
      }
      if (k == n) VARR_PUSH (char_ptr_t, fw_dirs, apple_fw_dirs[fi]);
    }
  }
#endif
  VARR_PUSH (char_ptr_t, system_headers, NULL);
  VARR_PUSH (char_ptr_t, fw_dirs, NULL);
  header_dirs = (const char **) VARR_ADDR (char_ptr_t, headers);
  system_header_dirs = (const char **) VARR_ADDR (char_ptr_t, system_headers);
  fw_dir_list = (const char **) VARR_ADDR (char_ptr_t, fw_dirs);
}

static int check_id_p (c2m_ctx_t c2m_ctx, const char *str) {
  int ok_p;

  if ((ok_p = isalpha (str[0]) || str[0] == '_')) {
    for (size_t i = 1; str[i] != '\0'; i++)
      if (!isalnum (str[i]) && str[i] != '_') {
        ok_p = FALSE;
        break;
      }
  }
  if (!ok_p && c2m_options->message_file != NULL)
    fprintf (c2m_options->message_file, "macro name %s is not an identifier\n", str);
  return ok_p;
}

static void define_cmd_macro (c2m_ctx_t c2m_ctx, const char *name, const char *def) {
  MIR_alloc_t alloc = c2m_alloc (c2m_ctx);
  pre_ctx_t pre_ctx = c2m_ctx->pre_ctx;
  pos_t pos;
  token_t t, id;
  struct macro macro;
  macro_t tab_m;
  VARR (token_t) * repl;

  pos.fname = COMMAND_LINE_SOURCE_NAME;
  pos.lno = 1;
  pos.ln_pos = 0;
  VARR_CREATE (token_t, repl, alloc, 16);
  id = new_id_token (c2m_ctx, pos, name);
  VARR_TRUNC (char, temp_string, 0);
  for (; *def != '\0'; def++) VARR_PUSH (char, temp_string, *def);
  VARR_PUSH (char, temp_string, '\0');
  reverse (temp_string);
  set_string_stream (c2m_ctx, VARR_ADDR (char, temp_string), pos, NULL);
  while ((t = get_next_pptoken (c2m_ctx))->code != T_EOFILE && t->code != T_EOU)
    if (repl != NULL) VARR_PUSH (token_t, repl, t);
  if (check_id_p (c2m_ctx, id->repr)) {
    macro.id = id;
    if (HTAB_DO (macro_t, macro_tab, &macro, HTAB_FIND, tab_m)) {
      if (!replacement_eq_p (tab_m->replacement, repl) && c2m_options->message_file != NULL)
        fprintf (c2m_options->message_file,
                 "warning -- redefinition of macro %s on the command line\n", id->repr);
      HTAB_DO (macro_t, macro_tab, &macro, HTAB_DELETE, tab_m);
    }
    new_macro (c2m_ctx, macro.id, NULL, repl);
  }
}

static void undefine_cmd_macro (c2m_ctx_t c2m_ctx, const char *name) {
  pre_ctx_t pre_ctx = c2m_ctx->pre_ctx;
  pos_t pos;
  token_t id;
  struct macro macro;
  macro_t tab_m;

  pos.fname = COMMAND_LINE_SOURCE_NAME;
  pos.lno = 1;
  pos.ln_pos = 0;
  id = new_id_token (c2m_ctx, pos, name);
  if (check_id_p (c2m_ctx, id->repr)) {
    macro.id = id;
    HTAB_DO (macro_t, macro_tab, &macro, HTAB_DELETE, tab_m);
  }
}

static void process_macro_commands (c2m_ctx_t c2m_ctx) {
  for (size_t i = 0; i < c2m_options->macro_commands_num; i++)
    if (c2m_options->macro_commands[i].def)
      define_cmd_macro (c2m_ctx, c2m_options->macro_commands[i].name,
                        c2m_options->macro_commands[i].def);
    else
      undefine_cmd_macro (c2m_ctx, c2m_options->macro_commands[i].name);
}

static void compile_init (c2m_ctx_t c2m_ctx, struct c2mir_options *ops, int (*getc_func) (void *),
                          void *getc_data) {
  MIR_alloc_t alloc = c2m_alloc (c2m_ctx);
  c2m_options = ops;
  n_errors = n_warnings = 0;
  c_getc = getc_func;
  c_getc_data = getc_data;
  VARR_CREATE (char, symbol_text, alloc, 128);
  VARR_CREATE (char, temp_string, alloc, 128);
  VARR_CREATE (pos_t, node_positions, alloc, 128);
  parse_init (c2m_ctx);
  context_init (c2m_ctx);
  init_include_dirs (c2m_ctx);
  process_macro_commands (c2m_ctx);
  VARR_CREATE (node_t, call_nodes, alloc, 128); /* used in context and gen */
  VARR_CREATE (node_t, containing_anon_members, alloc, 8);
  VARR_CREATE (init_object_t, init_object_path, alloc, 8);
}

static void compile_finish (c2m_ctx_t c2m_ctx) {
  if (symbol_text != NULL) VARR_DESTROY (char, symbol_text);
  if (temp_string != NULL) VARR_DESTROY (char, temp_string);
  if (node_positions != NULL) VARR_DESTROY (pos_t, node_positions);
  parse_finish (c2m_ctx);
  context_finish (c2m_ctx);
  if (headers != NULL) VARR_DESTROY (char_ptr_t, headers);
  if (system_headers != NULL) VARR_DESTROY (char_ptr_t, system_headers);
  if (fw_dirs != NULL) VARR_DESTROY (char_ptr_t, fw_dirs);
  if (call_nodes != NULL) VARR_DESTROY (node_t, call_nodes);
  if (containing_anon_members != NULL) VARR_DESTROY (node_t, containing_anon_members);
  if (init_object_path != NULL) VARR_DESTROY (init_object_t, init_object_path);
}

#include "real-time.h"

/* Return the time (usec) elapsed since *prev and advance *prev to now.  Used to
   measure each compile stage; the durations are reported together on one line at
   the end of c2mir_compile (see the -v summary). */
static double stage_time (double *prev) {
  double now = real_usec_time ();
  double dur = now - *prev;

  *prev = now;
  return dur;
}

static const char *get_module_name (c2m_ctx_t c2m_ctx) {
  sprintf (temp_str_buff, "M%ld", (long) c2m_options->module_num);
  return temp_str_buff;
}

static int top_level_getc (c2m_ctx_t c2m_ctx) { return c_getc (c_getc_data); }

/* Ownership / lifetime pass.  Single-TU build model: this file's body is
   pulled in here so it has visibility into all of classyc.c's internal
   types (c2m_ctx_t, node_t, decl_t, the N_* enum, traversal macros, etc.).
   Must come AFTER the definitions it depends on (struct decl, struct
   c2mir_options) and BEFORE c2mir_compile, which calls ownership_run. */
#include "ownership.c"

/* Mid-level optimizer (check → gen).  Same include model as ownership.c.
   See src/midopt.c and GEN-OPT.md Phases A/B. */
#include "midopt.c"

int c2mir_compile (MIR_context_t ctx, struct c2mir_options *ops, int (*getc_func) (void *),
                   void *getc_data, const char *source_name, FILE *output_file) {
  struct c2m_ctx *c2m_ctx = *c2m_ctx_loc (ctx);
  double start_time = real_usec_time ();
  double prev_time = start_time; /* advanced per stage by stage_time() */
  double t_init = 0, t_pre = 0, t_parse = 0, t_check = 0, t_ownership = 0, t_midopt = 0,
         t_gen = 0;
  node_t r;
  unsigned n_error_before;
  MIR_module_t m;

  if (c2m_ctx == NULL) return 0;

  /* If a previous analysis was retained (keep_syms_p), release it before starting a new run. */
  if (c2m_ctx->analysis_kept_p) {
    c2mir_release_analysis (ctx);
  }

  if (setjmp (c2m_ctx->env)) {
    compile_finish (c2m_ctx);
    c2m_ctx->analysis_kept_p = 0;
    return 0;
  }
  compile_init (c2m_ctx, ops, getc_func, getc_data);

  int keep_analysis = (ops && ops->no_gen_p && ops->keep_syms_p);
  size_t keep_mark = 0;
  if (keep_analysis) {
    keep_mark = reg_memory_mark (c2m_ctx);
  }

  t_init = stage_time (&prev_time);
  add_stream (c2m_ctx, NULL, source_name, top_level_getc);
  if (!c2m_options->no_prepro_p) add_standard_includes (c2m_ctx);
  pre (c2m_ctx);
  t_pre = stage_time (&prev_time);
  if (!c2m_options->prepro_only_p) {
    r = parse (c2m_ctx);
    t_parse = stage_time (&prev_time);
    if (c2m_options->verbose_p && c2m_options->message_file != NULL && n_errors)
      fprintf (c2m_options->message_file, "parse - FAIL\n");
    if (!c2m_options->syntax_only_p) {
      n_error_before = n_errors;
      do_context (c2m_ctx, r);
      t_check = stage_time (&prev_time);
      if (c2m_options->debug_p) {
          symbol_dump(c2m_ctx, stderr); /* -d: dump all symbols */
          tpname_dump(c2m_ctx, stderr);
      }
      if (n_errors > n_error_before) {
        if (c2m_options->debug_p) print_node (c2m_ctx, c2m_options->message_file, r, 0, FALSE);
        if (c2m_options->verbose_p && c2m_options->message_file != NULL)
          fprintf (c2m_options->message_file, "check - FAIL\n");
      } else if (c2m_options->no_gen_p) {
        /* analyze-only (LSP): stop after the context checker, no MIR generated */
        if (c2m_options->debug_p) print_node (c2m_ctx, c2m_options->message_file, r, 0, TRUE);
      } else {
        if (c2m_options->debug_p) print_node (c2m_ctx, c2m_options->message_file, r, 0, TRUE);
        /* Ownership pass: runs between check and gen.  Today: observation
           only — walks the AST, counts auto-defer candidates, and (under
           -v) prints them.  Future: synthesizes `defer delete` for owned
           bindings and emits leak / UAF / double-free diagnostics.  See
           src/ownership.c for the planned state machine. */
        n_error_before = n_errors;
        if (!c2m_options->no_ownership_p) {
          ownership_run (c2m_ctx, r);
        } else if (c2m_options->verbose_p && c2m_options->message_file != NULL) {
          fprintf (c2m_options->message_file, "ownership - SKIPPED (-fno-ownership)\n");
        }
        t_ownership = stage_time (&prev_time);
        if (n_errors > n_error_before) {
          if (c2m_options->verbose_p && c2m_options->message_file != NULL)
            fprintf (c2m_options->message_file, "ownership - FAIL\n");
          /* Fall through to gen_mir anyway today; once the pass emits real
             errors we'll guard gen the same way `check` is guarded above. */
        }
        /* Midopt: dead class-method pruning + static safety elision stamps.
           Runs after ownership so SAFE/CHECK attributes are available. */
        midopt_run (c2m_ctx, r);
        t_midopt = stage_time (&prev_time);
        m = MIR_new_module (ctx, get_module_name (c2m_ctx));
        gen_mir (c2m_ctx, r);
        if (c2m_options->dump_mir_stats_p)
          midopt_dump_mir_stats (c2m_ctx, m);
        if ((c2m_options->asm_p || c2m_options->object_p) && n_errors == 0) {
          if (strcmp (source_name, COMMAND_LINE_SOURCE_NAME) == 0) {
            MIR_output_module (ctx, c2m_options->message_file, m);
          } else if (output_file != NULL) {
            (c2m_options->asm_p ? MIR_output_module : MIR_write_module) (ctx, output_file, m);
            if (ferror (output_file) || fclose (output_file)) {
              fprintf (c2m_options->message_file, "C2MIR error in writing mir for source file %s\n",
                       source_name);
              n_errors++;
            }
          }
        }
        MIR_finish_module (ctx);
        t_gen = stage_time (&prev_time);
      }
    }
  }
  if (keep_analysis && n_errors == 0) {
    c2m_ctx->analysis_root = r;
    c2m_ctx->analysis_reg_mark = keep_mark;
    c2m_ctx->analysis_kept_p = 1;
    /* do not call compile_finish -- retain symbol table, node_positions, root for LSP */
  } else {
    compile_finish (c2m_ctx);
    c2m_ctx->analysis_kept_p = 0;
  }
  if (c2m_options->verbose_p && c2m_options->message_file != NULL) {
    /* One-line per-stage timing summary (skipped stages read 0). */
    FILE *f = c2m_options->message_file;
    int color = log_color_enabled (f);
    const char *names[]
      = {"init", "preprocess", "parse", "check", "ownership", "midopt", "generate", "total"};
    double vals[] = {t_init, t_pre, t_parse, t_check, t_ownership, t_midopt, t_gen,
                     real_usec_time () - start_time};

    fprintf (f, "  %stimings (usec):%s", log_c (color, LOG_BOLD), log_c (color, LOG_RESET));
    for (int i = 0; i < 8; i++)
      fprintf (f, " %s%s%s=%s%.0f%s", log_c (color, LOG_CYAN), names[i], log_c (color, LOG_RESET),
               log_c (color, LOG_BOLD), vals[i], log_c (color, LOG_RESET));
    fprintf (f, "\n");
  }
  return n_errors == 0;
}

/* Retained analysis API (for LSP go-to-definition etc.) */
#undef symbol_tab
#undef node_positions
#undef top_scope
#undef parse_ctx
#undef check_ctx

void c2mir_release_analysis (MIR_context_t ctx) {
  struct c2m_ctx *c2m_ctx = *c2m_ctx_loc (ctx);
  if (c2m_ctx == NULL || !c2m_ctx->analysis_kept_p) return;

  /* Everything allocated since the mark lives in reg_memory; pop frees it all. */
  reg_memory_pop (c2m_ctx, c2m_ctx->analysis_reg_mark);

  /* Null pointers into the now-freed region so the next compile will recreate clean structures. */
  c2m_ctx->symbol_tab = NULL;
  c2m_ctx->analysis_root = NULL;
  c2m_ctx->analysis_reg_mark = 0;
  c2m_ctx->analysis_kept_p = 0;
  c2m_ctx->node_positions = NULL;
  c2m_ctx->top_scope = NULL;
  c2m_ctx->parse_ctx = NULL;
  c2m_ctx->check_ctx = NULL;
}

	struct node *c2mir_get_analysis_root (MIR_context_t ctx) {
	  struct c2m_ctx *c2m_ctx = *c2m_ctx_loc (ctx);
	  if (c2m_ctx == NULL || !c2m_ctx->analysis_kept_p) return NULL;
	  return c2m_ctx->analysis_root;
	}

	/* Visitor for HTAB_FOREACH_ELEM over the whole symbol table (used by definition lookup).
	   Matches by bare name regardless of scope, so class members are discoverable. */
	static void find_any_member_visitor (symbol_t sym, void *arg) {
	  struct {
	    const char *name;
	    node_t def;
	  } *s = arg;
	  if (s->def != NULL) return;  /* keep the first match */
	  if (sym.id != NULL && sym.id->code == N_ID && sym.id->u.s.s != NULL
	      && strcmp (sym.id->u.s.s, s->name) == 0) {
	    s->def = sym.def_node;
	  }
	}

	int c2mir_find_definition (MIR_context_t ctx, const char *ident, c2mir_pos_t *out) {
	  struct c2m_ctx *c2m_ctx = *c2m_ctx_loc (ctx);
	  if (c2m_ctx == NULL) {
	    log_debug("c2mir_find_definition: no c2m_ctx");
	    return 0;
	  }
	  if (!c2m_ctx->analysis_kept_p) {
	    log_debug("c2mir_find_definition: analysis not kept (keep_syms_p was not used)");
	    return 0;
	  }
	  if (ident == NULL || out == NULL) {
	    log_debug("c2mir_find_definition: bad args (ident=%p out=%p)", (void*)ident, (void*)out);
	    return 0;
	  }

	  log_debug("c2mir_find_definition: looking for '%s' (kept_p=%d top_scope=%p)",
	            ident, c2m_ctx->analysis_kept_p, (void*)c2m_ctx->top_scope);

	  /* Build a transient N_ID (allocated from the current reg_memory; will be freed on next release). */
	  node_t id_node = build_id (c2m_ctx, ident, no_pos);

	  /* Try ordinary symbol (functions, globals, vars, class methods registered at class scope). */
	  node_t def = find_def (c2m_ctx, S_REGULARS, id_node, c2m_ctx->top_scope, NULL);

	  const char *kind = "S_REGULAR";
	  if (def == NULL) {
	    def = find_def (c2m_ctx, S_TAG, id_node, c2m_ctx->top_scope, NULL);
	    kind = "S_TAG";
	  }

	  if (def == NULL) {
	    /* Fallback: walk the full symbol table (members live under class scopes) */
	    struct {
	      const char *name;
	      node_t def;
	    } search = { ident, NULL };
	    HTAB_FOREACH_ELEM (symbol_t, c2m_ctx->symbol_tab, find_any_member_visitor, &search);
	    if (search.def) {
	      def = search.def;
	      kind = "member";
	    }
	  }

	  if (def == NULL) {
	    log_debug("c2mir_find_definition: '%s' NOT FOUND", ident);
	    return 0;
	  }

	  pos_t p = POS (def);
	  out->fname  = p.fname;
	  out->lno    = p.lno > 0 ? p.lno : 1;
	  out->ln_pos = p.ln_pos > 0 ? p.ln_pos : 1;

	  log_debug("c2mir_find_definition: FOUND '%s' (%s) at %s:%d:%d",
	            ident, kind,
	            out->fname ? out->fname : "<unknown>",
	            out->lno, out->ln_pos);

	  return 1;
	}

	/* Extended lookup for obj.member (or obj->member).
	   - Resolve receiver (global var or class name) to its declared type/class_node.
	   - Look up member name inside that class scope.
	   - Falls back to NULL (caller should try plain c2mir_find_definition on the member name). */
	int c2mir_find_member_definition (MIR_context_t ctx,
	                                  const char *receiver,
	                                  const char *member,
	                                  c2mir_pos_t *out) {
	  struct c2m_ctx *c2m_ctx = *c2m_ctx_loc (ctx);
	  if (c2m_ctx == NULL || !c2m_ctx->analysis_kept_p) return 0;
	  if (receiver == NULL || member == NULL || out == NULL) return 0;

	  log_debug("c2mir_find_member_definition: receiver='%s' member='%s'", receiver, member);

	  /* 1. locate the receiver identifier at global (top) scope */
	  node_t rid = build_id (c2m_ctx, receiver, no_pos);
	  node_t recv_def = find_def (c2m_ctx, S_REGULARS, rid, c2m_ctx->top_scope, NULL);
	  if (!recv_def)
	    recv_def = find_def (c2m_ctx, S_TAG, rid, c2m_ctx->top_scope, NULL);

	  if (!recv_def) {
	    log_debug("c2mir_find_member_definition: receiver '%s' not found at top scope", receiver);
	    return 0;
	  }

	  /* 2. derive class_node from the receiver's declared type (or if receiver is the class itself) */
	  node_t class_node = NULL;
	  if (recv_def->code == N_CLASS) {
	    class_node = recv_def;
	  } else if (recv_def->attr) {
	    decl_t d = (decl_t) recv_def->attr;
	    if (d && d->decl_spec.type && d->decl_spec.type->mode == TM_CLASS) {
	      class_node = d->decl_spec.type->u.tag_type;
	    }
	  }

	  if (!class_node) {
	    log_debug("c2mir_find_member_definition: receiver '%s' does not have a class type", receiver);
	    return 0;
	  }

	  /* 3. look up the member name inside the class scope */
	  node_t mid = build_id (c2m_ctx, member, no_pos);
	  node_t mdef = find_def (c2m_ctx, S_REGULARS, mid, class_node, NULL);
	  const char *kind = "member (class scope)";
	  if (!mdef) {
	    mdef = find_def (c2m_ctx, S_TAG, mid, class_node, NULL);
	    kind = "member tag (class scope)";
	  }

	  if (!mdef) {
	    log_debug("c2mir_find_member_definition: member '%s' NOT FOUND inside class", member);
	    return 0;
	  }

	  pos_t p = POS (mdef);
	  out->fname  = p.fname;
	  out->lno    = p.lno > 0 ? p.lno : 1;
	  out->ln_pos = p.ln_pos > 0 ? p.ln_pos : 1;

	  log_debug("c2mir_find_member_definition: FOUND %s.%s (%s) at %s:%d:%d",
		        receiver, member, kind,
		        out->fname ? out->fname : "<unknown>", out->lno, out->ln_pos);

	  return 1;
	}

/* ─── Reference finder: walk the AST collecting all N_ID nodes matching ident ─── */

static void collect_id_refs (c2m_ctx_t c2m_ctx, node_t n, const char *ident,
                            c2mir_pos_t **out_arr, size_t *out_count, size_t *out_cap) {
  if (n == NULL) return;

  /* Check if this node is an N_ID with the target name. */
  if (n->code == N_ID && n->u.s.s != NULL && strcmp (n->u.s.s, ident) == 0) {
    size_t cnt = *out_count, cap = *out_cap;
    if (cnt >= cap) {
      cap = cap ? cap * 2 : 64;
      *out_arr = realloc (*out_arr, cap * sizeof (c2mir_pos_t));
      *out_cap = cap;
    }
    pos_t p = POS (n);
    (*out_arr)[cnt].fname  = p.fname;
    (*out_arr)[cnt].lno    = p.lno > 0 ? p.lno : 1;
    (*out_arr)[cnt].ln_pos = p.ln_pos > 0 ? p.ln_pos : 1;
    (*out_count)++;
  }

  /* Recurse into children via the ops doubly-linked list. */
  for (node_t ch = NL_HEAD (n->u.ops); ch != NULL; ch = NL_NEXT (ch))
    collect_id_refs (c2m_ctx, ch, ident, out_arr, out_count, out_cap);
}

int c2mir_find_references (MIR_context_t ctx, const char *ident, c2mir_pos_t **out_refs) {
  struct c2m_ctx *c2m_ctx = *c2m_ctx_loc (ctx);
  if (c2m_ctx == NULL || !c2m_ctx->analysis_kept_p) {
    log_debug("c2mir_find_references: analysis not available");
    return 0;
  }
  if (ident == NULL || out_refs == NULL || ident[0] == '\0') {
    return 0;
  }

  *out_refs = NULL;
  size_t count = 0, cap = 0;

  node_t root = c2m_ctx->analysis_root;
  collect_id_refs (c2m_ctx, root, ident, out_refs, &count, &cap);

  log_debug("c2mir_find_references: found %zu reference(s) for '%s'", count, ident);
  return (int) count;
}

void c2mir_free_references (c2mir_pos_t *refs) {
  free (refs);
}

/* Local Variables:                */
/* mode: c                         */
/* page-delimiter: "/\\* New Page" */
/* End:                            */
