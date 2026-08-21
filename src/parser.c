/* Parser.  Single-TU build model: this file's body is pulled into classyc.c
   BEFORE the preprocessor.c include (the preprocessor's macro structures use
   token_t defined here).  It has visibility into classyc.c's internal types
   (c2m_ctx_t, node_t, the N_* enum, VARR/HTAB machinery, etc.). */

/* Defined in preprocessor.c, which is included after this file. */
static void pre_init (c2m_ctx_t c2m_ctx);
static void pre_finish (c2m_ctx_t c2m_ctx);
static void add_to_temp_string (c2m_ctx_t c2m_ctx, const char *str);
static void varr_str_push (VARR (char) * to, const char *s);

/* ------------------------- Parser Start ------------------------------ */

/* Parser is manually written parser with back-tracing to keep original
   grammar close to C11 standard grammar as possible.  It has a
   rudimentary syntax error recovery based on stop symbols ';' and
   '}'.  The input is parse tokens and the output is the following AST
   nodes (the AST root is transl_unit):

const : N_I | N_L | N_LL | N_U | N_UL | N_ULL | N_F | N_D | N_LD
      | N_CH | N_CH16 | N_CH32 | N_STR | N_STR16 | N_STR32
expr : const | N_ID | N_LABEL_ADDR (N_ID) | N_ADD (expr)
     | N_SUB (expr) | N_ADD (expr, expr) | N_SUB (expr, expr)
     | N_MUL (expr, expr) | N_DIV (expr, expr) | N_MOD (expr, expr)
     | N_LSH (expr, expr) | N_RSH (expr, expr)
     | N_NOT (expr) | N_BITWISE_NOT (expr)
     | N_INC (expr) | N_DEC (expr) | N_POST_INC (expr)| N_POST_DEC (expr)
     | N_ALIGNOF (type_name?) | N_SIZEOF (type_name) | N_EXPR_SIZEOF (expr)
     | N_CAST (type_name, expr) | N_COMMA (expr, expr) | N_ANDAND (expr, expr)
     | N_OROR (expr, expr) | N_EQ (expr, expr) | N_NE (expr, expr)
     | N_LT (expr, expr) | N_LE (expr, expr) | N_GT (expr, expr) | N_GE (expr, expr)
     | N_AND (expr, expr) | N_OR (expr, expr) | N_XOR (expr, expr)
     | N_ASSIGN (expr, expr) | N_ADD_ASSIGN (expr, expr) | N_SUB_ASSIGN (expr, expr)
     | N_MUL_ASSIGN (expr, expr) | N_DIV_ASSIGN (expr, expr) | N_MOD_ASSIGN (expr, expr)
     | N_LSH_ASSIGN (expr, expr) | N_RSH_ASSIGN (expr, expr)
     | N_AND_ASSIGN (expr, expr) | N_OR_ASSIGN (expr, expr) | N_XOR_ASSIGN (expr, expr)
     | N_DEREF (expr) | | N_ADDR (expr) | N_IND (expr, expr) | N_FIELD (expr, N_ID)
     | N_DEREF_FIELD (expr, N_ID) | N_COND (expr, expr, expr)
     | N_COMPOUND_LITERAL (type_name, initializer) | N_CALL (expr, N_LIST:(expr)*)
     | N_GENERIC (expr, N_LIST:(N_GENERIC_ASSOC (type_name?, expr))+ )
     | N_STMTEXPR (compound_stmt)
label: N_CASE(expr) | N_CASE(expr,expr) | N_DEFAULT | N_LABEL(N_ID)
stmt: compound_stmt | N_IF(N_LIST:(label)*, expr, stmt, stmt?)
    | N_SWITCH(N_LIST:(label)*, expr, stmt) | (N_WHILE|N_DO) (N_LIST:(label)*, expr, stmt)
    | N_FOR(N_LIST:(label)*,(N_LIST: declaration+ | expr)?, expr?, expr?, stmt)
    | N_GOTO(N_LIST:(label)*, N_ID) | N_INDIRECT_GOTO(N_LIST:(label)*, expr)
    | (N_CONTINUE|N_BREAK) (N_LIST:(label)*)
    | N_RETURN(N_LIST:(label)*, expr?) | N_EXPR(N_LIST:(label)*, expr)
compound_stmt: N_BLOCK(N_LIST:(label)*, N_LIST:(declaration | stmt)*)
asm: N_ASM(N_STR | N_STR16 | N_STR32)
attr_arg: const | N_ID
attr: N_ATTR(N_ID, NLIST:(attr_arg)*)
attrs: N_LIST:(attrs)*
declaration: N_SPEC_DECL(N_SHARE(declaration_specs), declarator?, attrs?, asm?, initializer?)
           | st_assert
st_assert: N_ST_ASSERT(const_expr, N_STR | N_STR16 | N_STR32)
declaration_specs: N_LIST:(align_spec|sc_spec|type_qual|func_spec|type_spec|attr)*
align_spec: N_ALIGNAS(type_name|const_expr)
sc_spec: N_TYPEDEF|N_EXTERN|N_STATIC|N_AUTO|N_REGISTER|N_THREAD_LOCAL
type_qual: N_CONST|N_RESTRICT|N_VOLATILE|N_ATOMIC
func_spec: N_INLINE|N_NO_RETURN
type_spec: N_VOID|N_CHAR|N_SHORT|N_INT|N_LONG|N_FLOAT|N_DOUBLE|N_SIGNED|N_UNSIGNED|N_BOOL
         | (N_STRUCT|N_UNION|N_CLASS) (N_ID?, struct_declaration_list?)
         | N_ENUM(N_ID?, N_LIST?: N_ENUM_COST(N_ID, const_expr?)*) | typedef_name
struct_declaration_list: N_LIST: struct_declaration*
struct_declaration: st_assert | N_MEMBER(N_SHARE(spec_qual_list), declarator?, attrs?, const_expr?)
spec_qual_list: N_LIST:(type_qual|type_spec)*
declarator: the same as direct declarator
direct_declarator: N_DECL(N_ID,
                          N_LIST:(N_POINTER(type_qual_list) | N_FUNC(id_list|parameter_list)
                                            | N_ARR(N_STATIC?, type_qual_list,
                                                    (assign_expr|N_STAR)?))*)
pointer: N_LIST: N_POINTER(type_qual_list)*
type_qual_list : N_LIST: type_qual*
parameter_type_list: N_LIST:(N_SPEC_DECL(declaration_specs, declarator, attrs?, ignore, ignore)
                             | N_TYPE(declaration_specs, abstract_declarator))+ [N_DOTS]
id_list: N_LIST: N_ID*
initializer: assign_expr | initialize_list
initializer_list: N_LIST: N_INIT(N_LIST:(const_expr | N_FIELD_ID (N_ID))* initializer)*
type_name: N_TYPE(spec_qual_list, abstract_declarator)
abstract_declarator: the same as abstract direct declarator
abstract_direct_declarator: N_DECL(ignore,
                                   N_LIST:(N_POINTER(type_qual_list) | N_FUNC(parameter_list)
                                           | N_ARR(N_STATIC?, type_qual_list,
                                                   (assign_expr|N_STAR)?))*)
typedef_name: N_ID
transl_unit: N_MODULE(N_LIST:(declaration
                              | N_FUNC_DEF(declaration_specs, declarator,
                                           N_LIST: declaration*, compound_stmt))*)

Here ? means it can be N_IGNORE, * means 0 or more elements in the list, + means 1 or more.

*/

#define REP_SEP ,
#define T_EL(t) T_##t
typedef enum {
  T_NUMBER = 256,
  REP8 (T_EL, CH, STR, ID, ASSIGN, DIVOP, ADDOP, SH, CMP),
  REP8 (T_EL, EQNE, ANDAND, OROR, INCDEC, ARROW, UNOP, DOTS, BOOL),
  REP8 (T_EL, COMPLEX, ALIGNOF, ALIGNAS, ATOMIC, GENERIC, NO_RETURN, STATIC_ASSERT, THREAD_LOCAL),
  REP8 (T_EL, THREAD, AUTO, BREAK, CASE, CHAR, CONST, CONTINUE, DEFAULT),
  REP8 (T_EL, DO, DOUBLE, ELSE, ENUM, EXTERN, FLOAT, FOR, GOTO),
  REP8 (T_EL, IF, INLINE, INT, LONG, REGISTER, RESTRICT, RETURN, SHORT),
  REP8 (T_EL, SIGNED, SIZEOF, STATIC, STRUCT, CLASS, SWITCH, TYPEDEF, TYPEOF),
  REP8 (T_EL, DICT, STRING, UNION, UNSIGNED, VOID, VOLATILE, WHILE, EOFILE),
  T_IN, /* 'in' keyword for dict key-check and for-in loops */
  T_FAT_ARROW, /* => fat-arrow token for lambda expressions */
  T_QDOT,      /* ?. safe-navigation member access */
  T_QQ,        /* ?? null-coalescing operator */
  /* tokens existing in preprocessor only: */
  T_HEADER,         /* include header */
  T_NO_MACRO_IDENT, /* ??? */
  T_DBLNO,          /* ## */
  T_PLM,
  T_RDBLNO, /* placemarker, ## in replacement list */
  T_BOA,    /* begin of argument */
  T_EOA,
  T_EOR, /* end of argument and macro replacement */
  T_EOP, /* end of processing */
  T_EOU, /* end of translation unit */
} token_code_t;

static token_code_t FIRST_KW = T_BOOL, LAST_KW = T_IN;

#define NODE_EL(n) N_##n

typedef enum {
  REP8 (NODE_EL, IGNORE, I, L, LL, U, UL, ULL, F),
  REP8 (NODE_EL, D, LD, CH, CH16, CH32, STR, STR16, STR32),
  REP5 (NODE_EL, ID, COMMA, ANDAND, OROR, STMTEXPR),
  REP8 (NODE_EL, EQ, NE, LT, LE, GT, GE, ASSIGN, BITWISE_NOT),
  REP8 (NODE_EL, NOT, AND, AND_ASSIGN, OR, OR_ASSIGN, XOR, XOR_ASSIGN, LSH),
  REP8 (NODE_EL, LSH_ASSIGN, RSH, RSH_ASSIGN, ADD, ADD_ASSIGN, SUB, SUB_ASSIGN, MUL),
  REP8 (NODE_EL, MUL_ASSIGN, DIV, DIV_ASSIGN, MOD, MOD_ASSIGN, IND, FIELD, ADDR),
  REP8 (NODE_EL, DEREF, DEREF_FIELD, COND, INC, DEC, POST_INC, POST_DEC, ALIGNOF),
  REP8 (NODE_EL, SIZEOF, EXPR_SIZEOF, CAST, COMPOUND_LITERAL, CALL, GENERIC, GENERIC_ASSOC, IF),
  REP8 (NODE_EL, SWITCH, WHILE, DO, FOR, GOTO, INDIRECT_GOTO, CONTINUE, BREAK),
  REP8 (NODE_EL, RETURN, EXPR, BLOCK, CASE, DEFAULT, LABEL, LABEL_ADDR, LIST),
  REP8 (NODE_EL, SPEC_DECL, SHARE, TYPEDEF, EXTERN, STATIC, AUTO, REGISTER, THREAD_LOCAL),
  REP8 (NODE_EL, DECL, VOID, CHAR, SHORT, INT, LONG, FLOAT, DOUBLE),
  REP8 (NODE_EL, SIGNED, UNSIGNED, BOOL, STRUCT, UNION, ENUM, ENUM_CONST, MEMBER),
  REP8 (NODE_EL, CONST, RESTRICT, VOLATILE, ATOMIC, INLINE, NO_RETURN, ALIGNAS, FUNC),
  REP8 (NODE_EL, STAR, POINTER, DOTS, ARR, INIT, FIELD_ID, TYPE, ST_ASSERT),
  REP8 (NODE_EL, FUNC_DEF, MODULE, ASM, ATTR, CLASS, STRING, CONCAT, DICT),
  N_IN,     /* "key" in dict — existence check */
  N_COALESCE, /* a ?? b — null-coalescing: a's value if non-zero/non-null, else b.
                 a is evaluated exactly once (see the N_COALESCE cases in
                 check/gen); ?. desugars to a marked N_COND instead. */
  N_FORIN,  /* for (auto var in dict) loop */
  N_NEW,    /* new ClassName(args) — heap allocation + constructor call */
  N_DEFER,  /* defer <stmt> — run statement at enclosing scope exit (LIFO) */
  N_DELETE, /* delete <ptr> — run destructor (if any) then free the heap object */
  N_DETACH, /* detach <expr>  — remove the value from the current scope's arena
               tracking set (String or object handle); returns the same value,
               now caller-owned.  Pairs with `defer` as its inverse on the
               scope's cleanup ledger.  Falls through unchanged for values that
               aren't tracked (literals, raw pointers, integers, etc.). */
  N_ATTACH, /* attach <expr>;  — adopt an externally-owned value into the
               current scope's arena.  STUB for now: parses and type-checks but
               emits no runtime call (reserved for the future dataflow /
               ownership-check pass).  Mirrors `detach` as the opposite ledger op. */
  N_UNOWNED, /* unowned <decl>  — wrap a declaration to opt out of any future
                automatic scope-bound cleanup.  Parses and is recorded in the
                AST so users can start writing it now; no semantic effect until
                auto-`defer delete` lands.  Wraps the inner declaration list. */
  N_MOVE,    /* move <expr>  — transfer single ownership OUT of an owned binding
                into the receiver.  Yields the same pointer value; the source
                binding is left as a read-only view (any subsequent ownership
                operation on it — move, delete, releasing call — is an error).
                Part of the opt-in `owned`/`move`/`readonly` managed layer. */
  N_READONLY,/* readonly <expr>  — borrow a read-only view of an owned object.
                Yields the same pointer value but confers NO ownership: the
                view never releases the object.  A view may be held anywhere —
                a local, a global, or an object field; it just must not be used
                after its owner is gone.  Inverse intent of `move`. */
  N_OWNED,   /* owned <decl>  — wrap a declaration to opt INTO the managed,
                single-owner, move-only lifetime (the GC-like layer).  The
                binding's resource is guaranteed to be released exactly once at
                the end of the owning scope unless ownership is `move`d out.
                Mirror of `unowned`; wraps the inner declaration list. */
  N_LAMBDA, /* (params) => body — anonymous function (future: untyped/generic lambdas) */
  N_INTERFACE, /* interface Name { sig; ... } — named structural method-set contract */
  N_ANY,    /* any<I>(expr) — wrap a concrete C* as an erased Any<I>* handle */
    N_TRY,      /* try block { ... } followed by one or more catch clauses    */
    N_CATCH,    /* catch(Class? var) { handler } — a single clause of an N_TRY */
    N_THROW,    /* throw(id, message) → cy_exc_throw(longjmp frame)            */
    N_GO,       /* go f(args); — spawn a fiber (-ffibers) → cy_spawn8 pack     */
    N_AWAIT     /* await [expr]; — pure cooperative yield (-ffibers) → cy_yield */
  } node_code_t;

#undef REP_SEP

DEF_DLIST_LINK (node_t);
DEF_DLIST_TYPE (node_t);

struct node {
  node_code_t code;
  unsigned uid;
  void *attr; /* used a scope for parser and as an attribute after */
  DLIST_LINK (node_t) op_link;
  union {
    str_t s;
    mir_char ch;
    mir_long l;
    mir_llong ll;
    mir_ulong ul; /* includes CH16 and CH32 */
    mir_ullong ull;
    mir_float f;
    mir_double d;
    mir_ldouble ld;
    DLIST (node_t) ops;
  } u;
};

static pos_t get_node_pos (c2m_ctx_t c2m_ctx, node_t n) {
  if(n)
      return VARR_GET (pos_t, node_positions, n->uid);
  else {
      pos_t empty = {0,};
      return empty;
  }
}

#define POS(n) get_node_pos (c2m_ctx, n)

static void set_node_pos (c2m_ctx_t c2m_ctx, node_t n, pos_t pos) {
  while (n->uid >= VARR_LENGTH (pos_t, node_positions)) VARR_PUSH (pos_t, node_positions, no_pos);
  VARR_SET (pos_t, node_positions, n->uid, pos);
}

DEF_DLIST_CODE (node_t, op_link);

struct token {
  int code : 16; /* token_code_t and EOF */
  int processed_p : 16;
  pos_t pos;
  node_code_t node_code;
  node_t node;
  const char *repr;
};

static node_t add_pos (c2m_ctx_t c2m_ctx, node_t n, pos_t p) {
  if (POS (n).lno < 0) set_node_pos (c2m_ctx, n, p);
  return n;
}

static node_t op_append (c2m_ctx_t c2m_ctx, node_t n, node_t op) {
  if (op == NULL) return n;  /* guard against NULL ops in error-recovery paths */
  NL_APPEND (n->u.ops, op);
  return add_pos (c2m_ctx, n, POS (op));
}

static node_t op_prepend (c2m_ctx_t c2m_ctx, node_t n, node_t op) {
  NL_PREPEND (n->u.ops, op);
  return add_pos (c2m_ctx, n, POS (op));
}

static void op_flat_append (c2m_ctx_t c2m_ctx, node_t n, node_t op) {
  if (op->code != N_LIST) {
    op_append (c2m_ctx, n, op);
    return;
  }
  for (node_t next_el, el = NL_HEAD (op->u.ops); el != NULL; el = next_el) {
    next_el = NL_NEXT (el);
    NL_REMOVE (op->u.ops, el);
    op_append (c2m_ctx, n, el);
  }
}

static node_t new_node (c2m_ctx_t c2m_ctx, node_code_t nc) {
  node_t n = reg_malloc (c2m_ctx, sizeof (struct node));

  n->code = nc;
  n->uid = curr_uid++;
  DLIST_INIT (node_t, n->u.ops);
  n->attr = NULL;
  set_node_pos (c2m_ctx, n, no_pos);
  return n;
}

static node_t copy_node_with_pos (c2m_ctx_t c2m_ctx, node_t n, pos_t pos) {
  node_t r = new_node (c2m_ctx, n->code);

  set_node_pos (c2m_ctx, r, pos);
  r->u = n->u;
  return r;
}

static node_t copy_node (c2m_ctx_t c2m_ctx, node_t n) {
  return copy_node_with_pos (c2m_ctx, n, POS (n));
}

static node_t new_pos_node (c2m_ctx_t c2m_ctx, node_code_t nc, pos_t p) {
  return add_pos (c2m_ctx, new_node (c2m_ctx, nc), p);
}
static node_t new_node1 (c2m_ctx_t c2m_ctx, node_code_t nc, node_t op1) {
  return op_append (c2m_ctx, new_node (c2m_ctx, nc), op1);
}
static node_t new_pos_node1 (c2m_ctx_t c2m_ctx, node_code_t nc, pos_t p, node_t op1) {
  return add_pos (c2m_ctx, new_node1 (c2m_ctx, nc, op1), p);
}
static node_t new_node2 (c2m_ctx_t c2m_ctx, node_code_t nc, node_t op1, node_t op2) {
  return op_append (c2m_ctx, new_node1 (c2m_ctx, nc, op1), op2);
}
static node_t new_pos_node2 (c2m_ctx_t c2m_ctx, node_code_t nc, pos_t p, node_t op1, node_t op2) {
  return add_pos (c2m_ctx, new_node2 (c2m_ctx, nc, op1, op2), p);
}
static node_t new_node3 (c2m_ctx_t c2m_ctx, node_code_t nc, node_t op1, node_t op2, node_t op3) {
  return op_append (c2m_ctx, new_node2 (c2m_ctx, nc, op1, op2), op3);
}
static node_t new_pos_node3 (c2m_ctx_t c2m_ctx, node_code_t nc, pos_t p, node_t op1, node_t op2,
                             node_t op3) {
  return add_pos (c2m_ctx, new_node3 (c2m_ctx, nc, op1, op2, op3), p);
}
static node_t new_node4 (c2m_ctx_t c2m_ctx, node_code_t nc, node_t op1, node_t op2, node_t op3,
                         node_t op4) {
  return op_append (c2m_ctx, new_node3 (c2m_ctx, nc, op1, op2, op3), op4);
}
static node_t new_pos_node4 (c2m_ctx_t c2m_ctx, node_code_t nc, pos_t p, node_t op1, node_t op2,
                             node_t op3, node_t op4) {
  return add_pos (c2m_ctx, new_node4 (c2m_ctx, nc, op1, op2, op3, op4), p);
}
static node_t new_node5 (c2m_ctx_t c2m_ctx, node_code_t nc, node_t op1, node_t op2, node_t op3,
                         node_t op4, node_t op5) {
  return op_append (c2m_ctx, new_node4 (c2m_ctx, nc, op1, op2, op3, op4), op5);
}
static node_t new_pos_node5 (c2m_ctx_t c2m_ctx, node_code_t nc, pos_t p, node_t op1, node_t op2,
                             node_t op3, node_t op4, node_t op5) {
  return add_pos (c2m_ctx, new_node5 (c2m_ctx, nc, op1, op2, op3, op4, op5), p);
}
static node_t new_ch_node (c2m_ctx_t c2m_ctx, int ch, pos_t p) {
  node_t n = new_pos_node (c2m_ctx, N_CH, p);
  n->u.ch = ch;
  return n;
}
static node_t new_ch16_node (c2m_ctx_t c2m_ctx, mir_ulong ch, pos_t p) {
  node_t n = new_pos_node (c2m_ctx, N_CH16, p);
  n->u.ul = ch;
  return n;
}
static node_t new_ch32_node (c2m_ctx_t c2m_ctx, mir_ulong ch, pos_t p) {
  node_t n = new_pos_node (c2m_ctx, N_CH32, p);
  n->u.ul = ch;
  return n;
}
static node_t new_i_node (c2m_ctx_t c2m_ctx, long l, pos_t p) {
  node_t n = new_pos_node (c2m_ctx, N_I, p);
  n->u.l = l;
  return n;
}
static node_t new_l_node (c2m_ctx_t c2m_ctx, long l, pos_t p) {
  node_t n = new_pos_node (c2m_ctx, N_L, p);
  n->u.l = l;
  return n;
}
static node_t new_ll_node (c2m_ctx_t c2m_ctx, long long ll, pos_t p) {
  node_t n = new_pos_node (c2m_ctx, N_LL, p);
  n->u.ll = ll;
  return n;
}
static node_t new_u_node (c2m_ctx_t c2m_ctx, unsigned long ul, pos_t p) {
  node_t n = new_pos_node (c2m_ctx, N_U, p);
  n->u.ul = ul;
  return n;
}
static node_t new_ul_node (c2m_ctx_t c2m_ctx, unsigned long ul, pos_t p) {
  node_t n = new_pos_node (c2m_ctx, N_UL, p);
  n->u.ul = ul;
  return n;
}
static node_t new_ull_node (c2m_ctx_t c2m_ctx, unsigned long long ull, pos_t p) {
  node_t n = new_pos_node (c2m_ctx, N_ULL, p);
  n->u.ull = ull;
  return n;
}
static node_t new_f_node (c2m_ctx_t c2m_ctx, float f, pos_t p) {
  node_t n = new_pos_node (c2m_ctx, N_F, p);
  n->u.f = f;
  return n;
}
static node_t new_d_node (c2m_ctx_t c2m_ctx, double d, pos_t p) {
  node_t n = new_pos_node (c2m_ctx, N_D, p);
  n->u.d = d;
  return n;
}
static node_t new_ld_node (c2m_ctx_t c2m_ctx, long double ld, pos_t p) {
  node_t n = new_pos_node (c2m_ctx, N_LD, p);
  n->u.ld = ld;
  return n;
}
static node_t new_str_node (c2m_ctx_t c2m_ctx, node_code_t nc, str_t s, pos_t p) {
  node_t n = new_pos_node (c2m_ctx, nc, p);
  n->u.s = s;
  return n;
}

static node_t get_op (node_t n, int nop) {
  n = NL_HEAD (n->u.ops);
  for (; nop > 0; nop--) n = NL_NEXT (n);
  return n;
}

static str_t uniq_cstr (c2m_ctx_t c2m_ctx, const char *str) {
  return str_add (c2m_ctx, str, strlen (str) + 1, T_STR, 0, FALSE).str;
}
static str_t uniq_str (c2m_ctx_t c2m_ctx, const char *str, size_t len) {
  return str_add (c2m_ctx, str, len, T_STR, 0, FALSE).str;
}

/* ===== AST Construction Helpers =====
 * These reduce boilerplate when building common AST node patterns.
 * NULL arguments are auto-filled with N_IGNORE placeholder nodes.
 */

/* Shorthand for the very common N_IGNORE placeholder node */
static node_t new_ignore (c2m_ctx_t c2m_ctx) { return new_node (c2m_ctx, N_IGNORE); }

/* Build an N_SPEC_DECL node.  NULL args become N_IGNORE. */
static node_t build_spec_decl (c2m_ctx_t c2m_ctx, pos_t pos, node_t specs, node_t declarator,
                               node_t attrs, node_t asm_part, node_t initializer) {
  if (attrs == NULL) attrs = new_ignore (c2m_ctx);
  if (asm_part == NULL) asm_part = new_ignore (c2m_ctx);
  if (initializer == NULL) initializer = new_ignore (c2m_ctx);
  return new_pos_node5 (c2m_ctx, N_SPEC_DECL, pos, specs, declarator, attrs, asm_part, initializer);
}

/* Build N_SPEC_DECL with N_SHARE-wrapped specs (shared across multiple declarators). */
static node_t build_shared_spec_decl (c2m_ctx_t c2m_ctx, pos_t pos, node_t shared_specs,
                                      node_t declarator, node_t attrs, node_t asm_part,
                                      node_t initializer) {
  return build_spec_decl (c2m_ctx, pos, new_node1 (c2m_ctx, N_SHARE, shared_specs), declarator,
                          attrs, asm_part, initializer);
}

/* Build an N_MEMBER node.  NULL args become N_IGNORE. */
static node_t build_member (c2m_ctx_t c2m_ctx, pos_t pos, node_t specs, node_t declarator,
                            node_t attrs, node_t width, node_t init) {
  if (attrs == NULL) attrs = new_ignore (c2m_ctx);
  if (width == NULL) width = new_ignore (c2m_ctx);
  if (init == NULL) init = new_ignore (c2m_ctx);
  return new_pos_node5 (c2m_ctx, N_MEMBER, pos, specs, declarator, attrs, width, init);
}

/* Build N_MEMBER with N_SHARE-wrapped specs. */
static node_t build_shared_member (c2m_ctx_t c2m_ctx, pos_t pos, node_t shared_specs,
                                   node_t declarator, node_t attrs, node_t width, node_t init) {
  return build_member (c2m_ctx, pos, new_node1 (c2m_ctx, N_SHARE, shared_specs), declarator, attrs,
                       width, init);
}

/* Build an N_FUNC_DEF node: specs, declarator, declarations, block. */
static node_t build_func_def (c2m_ctx_t c2m_ctx, pos_t pos, node_t specs, node_t declarator,
                              node_t declarations, node_t block) {
  return new_pos_node4 (c2m_ctx, N_FUNC_DEF, pos, specs, declarator, declarations, block);
}

/* Build an N_DECL node from an identifier and declaration list.
   If list is NULL, an empty N_LIST is created. */
static node_t build_decl (c2m_ctx_t c2m_ctx, pos_t pos, node_t id, node_t list) {
  if (list == NULL) list = new_node (c2m_ctx, N_LIST);
  return new_pos_node2 (c2m_ctx, N_DECL, pos, id, list);
}

/* Build a named N_ID node from a C string. */
static node_t build_id (c2m_ctx_t c2m_ctx, const char *name, pos_t pos) {
  return new_str_node (c2m_ctx, N_ID, uniq_cstr (c2m_ctx, name), pos);
}

static token_t new_token (c2m_ctx_t c2m_ctx, pos_t pos, const char *repr, int token_code,
                          node_code_t node_code) {
  token_t token = reg_malloc (c2m_ctx, sizeof (struct token));

  if(repr == NULL)
      printf("new_token: null str\nn");

  token->code = token_code;
  token->processed_p = FALSE;
  token->pos = pos;
  token->repr = repr;
  token->node_code = node_code;
  token->node = NULL;
  return token;
}

static token_t copy_token (c2m_ctx_t c2m_ctx, token_t t, pos_t pos) {
  token_t token = new_token (c2m_ctx, pos, t->repr, t->code, t->node_code);

  if (t->node != NULL) token->node = copy_node_with_pos (c2m_ctx, t->node, pos);
  return token;
}

static token_t new_token_wo_uniq_repr (c2m_ctx_t c2m_ctx, pos_t pos, const char *repr,
                                       int token_code, node_code_t node_code) {
  return new_token (c2m_ctx, pos, uniq_cstr (c2m_ctx, repr).s, token_code, node_code);
}

static token_t new_node_token (c2m_ctx_t c2m_ctx, pos_t pos, const char *repr, int token_code,
                               node_t node) {
  token_t token = new_token_wo_uniq_repr (c2m_ctx, pos, repr, token_code, N_IGNORE);

  token->node = node;
  return token;
}

static void print_pos (FILE *f, pos_t pos, int col_p) {
  if (pos.lno < 0) return;
  fprintf (f, "%s:%d", pos.fname, pos.lno);
  if (col_p) fprintf (f, ":%d: ", pos.ln_pos);
}

static const char *get_token_name (c2m_ctx_t c2m_ctx, int token_code) {
  const char *s;

  switch (token_code) {
  case T_NUMBER: return "number";
  case T_CH: return "char constant";
  case T_STR: return "string value";
  case T_STRING: return "string type";
  case T_ID: return "identifier";
  case T_ASSIGN: return "assign op";
  case T_DIVOP: return "/ or %";
  case T_ADDOP: return "+ or -";
  case T_SH: return "shift op";
  case T_CMP: return "comparison op";
  case T_EQNE: return "equality op";
  case T_ANDAND: return "&&";
  case T_OROR: return "||";
  case T_INCDEC: return "++ or --";
  case T_ARROW: return "->";
  case T_QDOT: return "?.";
  case T_QQ: return "??";
  case T_UNOP: return "unary op";
  case T_DOTS: return "...";
  default:
    if ((s = str_find_by_key (c2m_ctx, token_code)) != NULL) return s;
    if (isprint (token_code))
      sprintf (temp_str_buff, "%c", token_code);
    else
      sprintf (temp_str_buff, "%d", token_code);
    return temp_str_buff;
  }
}

/* Shared back-end for error()/warning(): forward a structured diagnostic to the
   logger sink (for the LSP/editors) and, when a message stream exists, print the
   colored "file:line:col: <label> -- message" line.  Printing keeps using
   vfprintf so long messages are never truncated; the sink copy is formatted into
   a bounded buffer (only when a sink is registered). */
static void vdiag (c2m_ctx_t c2m_ctx, pos_t pos, int error_p, const char *label,
                   const char *label_color, const char *format, va_list args) {
  FILE *f = c2m_options->message_file;

  if (log_diag_active ()) {
    char buf[2048];
    va_list ap;
    log_diag_t d;

    va_copy (ap, args);
    vsnprintf (buf, sizeof buf, format, ap);
    va_end (ap);
    d.file = pos.fname;
    d.line = pos.lno;
    d.col = pos.ln_pos;
    d.error_p = error_p;
    d.message = buf;
    log_emit_diag (&d);
  }
  if (f != NULL) {
    log_print_diag_prefix (f, pos.lno < 0 ? NULL : pos.fname, pos.lno, pos.ln_pos, label,
                           label_color);
    vfprintf (f, format, args);
    fprintf (f, "\n");
  }
}

static void error (c2m_ctx_t c2m_ctx, pos_t pos, const char *format, ...) {
  va_list args;

  n_errors++;
  va_start (args, format);
  vdiag (c2m_ctx, pos, TRUE, "error", LOG_BRED, format, args);
  va_end (args);
}

static void warning (c2m_ctx_t c2m_ctx, pos_t pos, const char *format, ...) {
  va_list args;

  n_warnings++;
  if (c2m_options->ignore_warnings_p) return;
  va_start (args, format);
  vdiag (c2m_ctx, pos, FALSE, "warning", LOG_BYELLOW, format, args);
  va_end (args);
}

static void debug (c2m_ctx_t c2m_ctx, pos_t pos, const char *format, ...) {
  va_list args;
  FILE *f;

  return;

  if ((f = c2m_options->message_file) == NULL) return;
  n_warnings++;
  if (!c2m_options->ignore_warnings_p) {
    va_start (args, format);
    print_pos (f, pos, TRUE);
    fprintf (f, "debug -- ");
    vfprintf (f, format, args);
    va_end (args);
    fprintf (f, "\n");
  }
}

#define TAB_STOP 8

static void init_streams (c2m_ctx_t c2m_ctx) {
  MIR_alloc_t alloc = c2m_alloc (c2m_ctx);
  cs = eof_s = NULL;
  VARR_CREATE (stream_t, streams, alloc, 32);
}

static void free_stream (c2m_ctx_t c2m_ctx, stream_t s) {
  if(s->ln)
      VARR_DESTROY (char, s->ln);
  MIR_free (MIR_get_alloc (c2m_ctx->ctx), s);
}

static void finish_streams (c2m_ctx_t c2m_ctx) {
  if (eof_s != NULL) free_stream (c2m_ctx, eof_s);
  if (streams == NULL) return;
  while (VARR_LENGTH (stream_t, streams) != 0) free_stream (c2m_ctx, VARR_POP (stream_t, streams));
  VARR_DESTROY (stream_t, streams);
}

static stream_t new_stream (MIR_alloc_t alloc, FILE *f, const char *fname, int (*getc_func) (c2m_ctx_t)) {
  stream_t s = MIR_malloc (alloc, sizeof (struct stream));

  VARR_CREATE (char, s->ln, alloc, 128);
  s->f = f;
  s->fname = s->pos.fname = fname;
  s->pos.lno = 0;
  s->pos.ln_pos = 0;
  s->ifs_length_at_stream_start = 0;
  s->start = s->curr = NULL;
  s->getc_func = getc_func;
  s->ig_state = IG_OFF;
  s->ig_macro = NULL;
  return s;
}

static void add_stream (c2m_ctx_t c2m_ctx, FILE *f, const char *fname,
                        int (*getc_func) (c2m_ctx_t)) {
  assert (fname != NULL);
  MIR_alloc_t alloc = c2m_alloc (c2m_ctx);
  stream_t existing = NULL;
  stream_t s;
  size_t i;
  VARR_FOREACH_ELEM (stream_t, streams, i, s) {
    if (s->fname != NULL && strcmp (s->fname, fname) == 0) {
      existing = s;
      break;
    }
  }
  if (cs != NULL && cs->f != NULL && cs->f != stdin) {
    fgetpos (cs->f, &cs->fpos);
    fclose (cs->f);
    cs->f = NULL;
  }
  if (existing != NULL) {
    // FIXME existing streams crash because c2m_ctx->cs->getc_fund is undefined
    //cs = existing;
    //printf("add_stream: Should using existing stream %s\n", fname);
  }
  cs = new_stream (alloc, f, fname, getc_func);
  VARR_PUSH (stream_t, streams, cs);
  //print_streams(c2m_ctx);
}

static int str_getc (c2m_ctx_t c2m_ctx) {
  if (*cs->curr == '\0') return EOF;
  return *cs->curr++;
}

static void add_string_stream (c2m_ctx_t c2m_ctx, const char *pos_fname, const char *str) {
  add_stream (c2m_ctx, NULL, pos_fname, str_getc);
  cs->start = cs->curr = str;
}

static int string_stream_p (stream_t s) { return s->getc_func != NULL; }

static void change_stream_pos (c2m_ctx_t c2m_ctx, pos_t pos) { cs->pos = pos; }

static void remove_trigraphs (c2m_ctx_t c2m_ctx) {
  int len = (int) VARR_LENGTH (char, cs->ln);
  char *addr = VARR_ADDR (char, cs->ln);
  int i, start, to, ch;

  for (i = to = 0; i < len; i++, to++) {
    addr[to] = addr[i];
    for (start = i; i < len && addr[i] == '?'; i++, to++) addr[to] = addr[i];
    if (i >= len) break;
    if (i < start + 2) {
      addr[to] = addr[i];
      continue;
    }
    switch (addr[i]) {
    case '=': ch = '#'; break;
    case '(': ch = '['; break;
    case '/': ch = '\\'; break;
    case ')': ch = ']'; break;
    case '\'': ch = '^'; break;
    case '<': ch = '{'; break;
    case '!': ch = '|'; break;
    case '>': ch = '}'; break;
    case '-': ch = '~'; break;
    default: addr[to] = addr[i]; continue;
    }
    to -= 2;
    addr[to] = ch;
  }
  VARR_TRUNC (char, cs->ln, to);
}

/* Map `??X` to its C trigraph replacement, or -1 if X is not a trigraph. */
static int trigraph_replacement (int c) {
  switch (c) {
  case '=': return '#';
  case '(': return '[';
  case '/': return '\\';
  case ')': return ']';
  case '\'': return '^';
  case '<': return '{';
  case '!': return '|';
  case '>': return '}';
  case '-': return '~';
  default: return -1;
  }
}

static int ln_get (c2m_ctx_t c2m_ctx) {
  if (cs->f == NULL) return cs->getc_func (c2m_ctx); /* top level */
  return fgetc (cs->f);
}

static char *reverse (VARR (char) * v) {
  char *addr = VARR_ADDR (char, v);
  int i, j, temp, last = (int) VARR_LENGTH (char, v) - 1;

  if (last >= 0 && addr[last] == '\0') last--;
  for (i = last, j = 0; i > j; i--, j++) SWAP (addr[i], addr[j], temp);
  return addr;
}

static int get_line (c2m_ctx_t c2m_ctx) { /* translation phase 1 and 2 */
  int c, eof_p = 0;

  VARR_TRUNC (char, cs->ln, 0);
  for (c = ln_get (c2m_ctx); c != EOF && c != '\n'; c = ln_get (c2m_ctx))
    VARR_PUSH (char, cs->ln, c);
  if (VARR_LENGTH (char, cs->ln) != 0 && VARR_LAST (char, cs->ln) == '\r') VARR_POP (char, cs->ln);
  eof_p = c == EOF;
  if (eof_p) {
    if (VARR_LENGTH (char, cs->ln) == 0) return FALSE;
    if (c != '\n')
      (c2m_options->pedantic_p ? error : warning) (c2m_ctx, cs->pos, "no end of line at file end");
  }
  /* Trigraphs are pedantic-mode only (matching modern C/GCC defaults): the
     `??` null-coalescing operator would otherwise turn `x??(y)` into `x[y`. */
  if (c2m_options->pedantic_p) remove_trigraphs (c2m_ctx);
  VARR_PUSH (char, cs->ln, '\n');
  reverse (cs->ln);
  return TRUE;
}

static int cs_get (c2m_ctx_t c2m_ctx) {
  size_t len = VARR_LENGTH (char, cs->ln);

  for (;;) {
    if (len == 2 && VARR_GET (char, cs->ln, 1) == '\\') {
      assert (VARR_GET (char, cs->ln, 0) == '\n');
    } else if (len > 0) {
      cs->pos.ln_pos++;
      return VARR_POP (char, cs->ln);
    }
    if (cs->fname == NULL || !get_line (c2m_ctx)) return EOF;
    len = VARR_LENGTH (char, cs->ln);
    assert (len > 0);
    cs->pos.ln_pos = 0;
    cs->pos.lno++;
  }
}

static void cs_unget (c2m_ctx_t c2m_ctx, int c) {
  cs->pos.ln_pos--;
  VARR_PUSH (char, cs->ln, c);
}

static void set_string_stream (c2m_ctx_t c2m_ctx, const char *str, pos_t pos,
                               void (*transform) (const char *, VARR (char) *)) {
  MIR_alloc_t alloc = c2m_alloc (c2m_ctx);
  /* read from string str */
  cs = new_stream (alloc, NULL, NULL, NULL);
  VARR_PUSH (stream_t, streams, cs);
  cs->pos = pos;
  if (transform != NULL) {
    transform (str, cs->ln);
  } else {
    for (; *str != '\0'; str++) VARR_PUSH (char, cs->ln, *str);
  }
}

static void remove_string_stream (c2m_ctx_t c2m_ctx) {
  assert (cs->f == NULL);
  if (VARR_LENGTH (stream_t, streams) <= 1)
    return; /* parent stream was already popped by EOF handling; leave string stream alive */
  free_stream (c2m_ctx, VARR_POP (stream_t, streams));
  cs = VARR_LAST (stream_t, streams);
}

#define MAX_UTF8 0x1FFFFF

/* We use UTF-32 for 32-bit wchars and UTF-16 for 16-bit wchar (LE/BE
   depending on endianess of the target), UTF-8 for anything else. */
static void push_str_char (VARR (char) * temp, uint64_t ch, int type) {
  int i, len = 0;

  switch (type) {
  case 'f': /* f"..." interpolated string: narrow UTF-8 bytes, like a plain string */
  case ' ':
    if (ch <= 0xFF) {
      VARR_PUSH (char, temp, (char) ch);
      return;
    }
    /* Fall through */
  case '8':
    if (ch <= 0x7F) {
      VARR_PUSH (char, temp, (char) ch);
    } else if (ch <= 0x7FF) {
      VARR_PUSH (char, temp, (char) (0xC0 | (ch >> 6)));
      VARR_PUSH (char, temp, (char) (0x80 | (ch & 0x3F)));
    } else if (ch <= 0xFFFF) {
      VARR_PUSH (char, temp, (char) (0xE0 | (ch >> 12)));
      VARR_PUSH (char, temp, (char) (0x80 | ((ch >> 6) & 0x3F)));
      VARR_PUSH (char, temp, (char) (0x80 | (ch & 0x3F)));
    } else {
      assert (ch <= MAX_UTF8);
      VARR_PUSH (char, temp, (char) (0xF0 | (ch >> 18)));
      VARR_PUSH (char, temp, (char) (0x80 | ((ch >> 12) & 0x3F)));
      VARR_PUSH (char, temp, (char) (0x80 | ((ch >> 6) & 0x3F)));
      VARR_PUSH (char, temp, (char) (0x80 | (ch & 0x3F)));
    }
    return;
  case 'L':
    if (sizeof (mir_wchar) == 4) goto U;
    /* Fall through */
  case 'u': len = 2; break;
  case 'U':
  U:
    len = 4;
    break;
  default: assert (FALSE);
  }
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  for (i = 0; i < len; i++) VARR_PUSH (char, temp, (ch >> i * 8) & 0xff);
#else
  for (i = len - 1; i >= 0; i--) VARR_PUSH (char, temp, (ch >> i * 8) & 0xff);
#endif
}

static int pre_skip_if_part_p (c2m_ctx_t c2m_ctx);

static void set_string_val (c2m_ctx_t c2m_ctx, token_t t, VARR (char) * temp, int type) {
  int i, str_len;
  int64_t curr_c, last_c = -1;
  uint64_t max_char = (type == 'u'   ? UINT16_MAX
                       : type == 'U' ? UINT32_MAX
                       : type == 'L' ? MIR_WCHAR_MAX
                                     : MIR_UCHAR_MAX);
  int start = type == ' ' ? 0 : type == '8' ? 2 : 1;
  int string_p = t->repr[start] == '"';
  const char *str;

  assert (t->code == T_STR || t->code == T_CH);
  str = t->repr;
  VARR_TRUNC (char, temp, 0);
  str_len = (int) strlen (str);
  assert (str_len >= start + 2 && (str[start] == '"' || str[start] == '\'')
          && str[start] == str[str_len - 1]);
  for (i = start + 1; i < str_len - 1; i++) {
    if (!string_p && last_c >= 0 && !pre_skip_if_part_p (c2m_ctx))
      error (c2m_ctx, t->pos, "multibyte character");
    last_c = curr_c = (unsigned char) str[i];
    /* Trigraphs in string/char literals (translation phase 1) even when
       source-level `??` is the null-coalescing operator.  `??/` becomes `\`
       and is then processed as an escape. */
    if (curr_c == '?' && i + 2 < str_len - 1 && str[i + 1] == '?') {
      int repl = trigraph_replacement ((unsigned char) str[i + 2]);
      if (repl >= 0) {
        curr_c = last_c = repl;
        i += 2;
      }
    }
    if (curr_c != '\\') {
      push_str_char (temp, curr_c, type);
      continue;
    }
    last_c = curr_c = str[++i];
    switch (curr_c) {
    case 'a': last_c = curr_c = '\a'; break;
    case 'b': last_c = curr_c = '\b'; break;
    case 'n': last_c = curr_c = '\n'; break;
    case 'f': last_c = curr_c = '\f'; break;
    case 'r': last_c = curr_c = '\r'; break;
    case 't': last_c = curr_c = '\t'; break;
    case 'v': last_c = curr_c = '\v'; break;
    case '\\':
    case '\'':
    case '\?':
    case '\"': break;
    case 'e':
      if (!pre_skip_if_part_p (c2m_ctx))
        (c2m_options->pedantic_p ? error : warning) (c2m_ctx, t->pos,
                                                     "non-standard escape sequence \\e");
      last_c = curr_c = '\033';
      break;
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7': {
      uint64_t v = curr_c - '0';

      curr_c = str[++i];
      if (!isdigit ((int) curr_c) || curr_c == '8' || curr_c == '9') {
        i--;
      } else {
        v = v * 8 + curr_c - '0';
        curr_c = str[++i];
        if (!isdigit ((int) curr_c) || curr_c == '8' || curr_c == '9')
          i--;
        else
          v = v * 8 + curr_c - '0';
      }
      last_c = curr_c = v;
      break;
    }
    case 'x':
    case 'X': {
      int first_p = TRUE;
      uint64_t v = 0;

      for (i++;; i++) {
        curr_c = str[i];
        if (!isxdigit ((int) curr_c)) break;
        first_p = FALSE;
        if (v <= UINT32_MAX) {
          v *= 16;
          v += (isdigit ((int) curr_c)   ? curr_c - '0'
                : islower ((int) curr_c) ? curr_c - 'a' + 10
                                         : curr_c - 'A' + 10);
        }
      }
      if (first_p) {
        if (!pre_skip_if_part_p (c2m_ctx))
          error (c2m_ctx, t->pos, "wrong hexadecimal char %c", curr_c);
      } else if (v > max_char) {
        if (!pre_skip_if_part_p (c2m_ctx))
          (c2m_options->pedantic_p ? error : warning) (c2m_ctx, t->pos,
                                                       "too big hexadecimal char 0x%x", v);
        curr_c = max_char;
      }
      last_c = curr_c = v;
      i--;
      break;
    }
    case 'u':
    case 'U': {
      int n, start_c = (int) curr_c, digits_num = curr_c == 'u' ? 4 : 8;
      uint64_t v = 0;

      for (i++, n = 0; n < digits_num; i++, n++) {
        curr_c = str[i];
        if (!isxdigit ((int) curr_c)) break;
        v *= 16;
        v += (isdigit ((int) curr_c)   ? curr_c - '0'
              : islower ((int) curr_c) ? curr_c - 'a' + 10
                                       : curr_c - 'A' + 10);
      }
      last_c = curr_c = v;
      if (n < digits_num) {
        if (!pre_skip_if_part_p (c2m_ctx))
          error (c2m_ctx, t->pos, "unfinished \\%c<hex-digits>", start_c);
      } else if (v > max_char && (!string_p || (type != ' ' && type != '8') || v > MAX_UTF8)) {
        if (!pre_skip_if_part_p (c2m_ctx))
          (c2m_options->pedantic_p ? error : warning) (c2m_ctx, t->pos,
                                                       "too big universal char 0x%lx in \\%c",
                                                       (unsigned long) v, start_c);
        last_c = curr_c = max_char;
      } else if ((0xD800 <= v && v <= 0xDFFF)
                 || (v < 0xA0 && v != 0x24 && v != 0x40 && v != 0x60)) {
        if (!pre_skip_if_part_p (c2m_ctx)) {
          error (c2m_ctx, t->pos, "usage of reserved value 0x%lx in \\%c", (unsigned long) v,
                 start_c);
          curr_c = -1;
        }
      }
      if (n < digits_num) i--;
      break;
    }
    default:
      if (!pre_skip_if_part_p (c2m_ctx)) {
        error (c2m_ctx, t->pos, "wrong escape char 0x%x", curr_c);
        curr_c = -1;
      }
    }
    if (!string_p || curr_c >= 0) push_str_char (temp, curr_c, type);
  }
  push_str_char (temp, '\0', type);
  if (string_p)
    t->node->u.s = uniq_str (c2m_ctx, VARR_ADDR (char, temp), VARR_LENGTH (char, temp));
  else if (last_c < 0) {
    if (!pre_skip_if_part_p (c2m_ctx)) error (c2m_ctx, t->pos, "empty char constant");
  } else if (type == 'U' || type == 'u' || type == 'L') {
    t->node->u.ul = (mir_ulong) last_c;
  } else {
    t->node->u.ch = (mir_char) last_c;
  }
}

static token_t new_id_token (c2m_ctx_t c2m_ctx, pos_t pos, const char *id_str) {
  token_t token;
  if(id_str == NULL)
      printf("new_id_token: null id str\nn");

  str_t str = uniq_cstr (c2m_ctx, id_str);

  token = new_token (c2m_ctx, pos, str.s, T_ID, N_IGNORE);
  token->node = new_str_node (c2m_ctx, N_ID, str, pos);
  // NOTE RSD - for time reason new_str_node sets the tail to some odd value
  // This prevents a crash in N_CONCAT string checking
  token->node->u.ops.tail= NULL;
  return token;
}

static token_t get_next_pptoken_1 (c2m_ctx_t c2m_ctx, int header_p) {
  int start_c, curr_c, nl_p, comment_char, wide_type;
  pos_t pos;

  if (cs->fname != NULL && VARR_LENGTH (token_t, buffered_tokens) != 0)
    return VARR_POP (token_t, buffered_tokens);
  VARR_TRUNC (char, symbol_text, 0);
  for (;;) {
    curr_c = cs_get (c2m_ctx);
    /* Process sequence of white spaces/comments: */
    for (comment_char = -1, nl_p = FALSE;; curr_c = cs_get (c2m_ctx)) {
      switch (curr_c) {
      case '\t':
        cs->pos.ln_pos = (int) round_size ((mir_size_t) cs->pos.ln_pos, TAB_STOP);
        /* fall through */
      case ' ':
      case '\f':
      case '\r':
      case '\v': break;
      case '\n':
        if (comment_char < 0) {
          nl_p = TRUE;
          pos = cs->pos;
        } else if (comment_char == '/') {
          comment_char = -1;
          nl_p = TRUE;
          pos = cs->pos;
        }
        cs->pos.ln_pos = 0;
        break;
	      case '/':
	        if (comment_char >= 0) break;
	        curr_c = cs_get (c2m_ctx);
	        if (curr_c == '/' || curr_c == '*') {
	          VARR_PUSH (char, symbol_text, '/');
	          comment_char = curr_c;
	          break;
	        }
	        cs_unget (c2m_ctx, curr_c);
	        curr_c = '/';
	        goto end_ws;
	      case '*':
	        if (comment_char < 0) goto end_ws;
	        if (comment_char != '*') break;
	        curr_c = cs_get (c2m_ctx);
	        if (curr_c == '/') {
	          comment_char = -1;
	          VARR_PUSH (char, symbol_text, '*');
	        } else {
	          cs_unget (c2m_ctx, curr_c);
	          curr_c = '*';
	        }
	        break;
	      default:
	        if (comment_char < 0) goto end_ws;
	        if (curr_c == EOF) {
	          error_func (c2m_ctx, C_unfinished_comment, "unfinished comment");
	          goto end_ws;
	        }
	        break;
	      }
	      VARR_PUSH (char, symbol_text, curr_c);
	    }
	  end_ws:
	    if (VARR_LENGTH (char, symbol_text) != 0) {
	      cs_unget (c2m_ctx, curr_c);
	      /* Optimization: whitespace/comment runs are discarded by pptoken2token anyway.
	         Avoid expensive uniq_cstr + reg_malloc + hash lookup for the full sequence.
	         Use a static short sentinel; the actual text is irrelevant. */
	      static const char ws_space[] = " ";
	      static const char ws_nl[] = "\n";
	      const char *ws = nl_p ? ws_nl : ws_space;
	      return new_token (c2m_ctx, nl_p ? pos : cs->pos, ws, nl_p ? '\n' : ' ', N_IGNORE);
	    }
    if (header_p && (curr_c == '<' || curr_c == '\"')) {
      int stop;

      pos = cs->pos;
      VARR_TRUNC (char, temp_string, 0);
      for (stop = curr_c == '<' ? '>' : '\"';;) {
        VARR_PUSH (char, symbol_text, curr_c);
        curr_c = cs_get (c2m_ctx);
        VARR_PUSH (char, temp_string, curr_c);
        if (curr_c == stop || curr_c == '\n' || curr_c == EOF) break;
      }
      if (curr_c == stop) {
        VARR_PUSH (char, symbol_text, curr_c);
        VARR_PUSH (char, symbol_text, '\0');
        VARR_POP (char, temp_string);
        VARR_PUSH (char, temp_string, '\0');
        return new_node_token (c2m_ctx, pos, VARR_ADDR (char, symbol_text), T_HEADER,
                               new_str_node (c2m_ctx, N_STR,
                                             uniq_cstr (c2m_ctx, VARR_ADDR (char, temp_string)),
                                             pos));
      } else {
        VARR_PUSH (char, symbol_text, curr_c);
        for (size_t i = 0; i < VARR_LENGTH (char, symbol_text); i++)
          cs_unget (c2m_ctx, VARR_GET (char, symbol_text, i));
        curr_c = (stop == '>' ? '<' : '\"');
      }
    }
    switch (start_c = curr_c) {
    case '\\':
      curr_c = cs_get (c2m_ctx);
      assert (curr_c != '\n');
      cs_unget (c2m_ctx, curr_c);
      return new_token (c2m_ctx, cs->pos, "\\", '\\', N_IGNORE);
    case '~': return new_token (c2m_ctx, cs->pos, "~", T_UNOP, N_BITWISE_NOT);
    case '+':
    case '-':
      pos = cs->pos;
      curr_c = cs_get (c2m_ctx);
      if (curr_c == start_c) {
        if (start_c == '+')
          return new_token (c2m_ctx, pos, "++", T_INCDEC, N_INC);
        else
          return new_token (c2m_ctx, pos, "--", T_INCDEC, N_DEC);
      } else if (curr_c == '=') {
        if (start_c == '+')
          return new_token (c2m_ctx, pos, "+=", T_ASSIGN, N_ADD_ASSIGN);
        else
          return new_token (c2m_ctx, pos, "-=", T_ASSIGN, N_SUB_ASSIGN);
      } else if (start_c == '-' && curr_c == '>') {
        return new_token (c2m_ctx, pos, "->", T_ARROW, N_DEREF_FIELD);
      } else {
        cs_unget (c2m_ctx, curr_c);
        if (start_c == '+')
          return new_token (c2m_ctx, pos, "+", T_ADDOP, N_ADD);
        else
          return new_token (c2m_ctx, pos, "-", T_ADDOP, N_SUB);
      }
      assert (FALSE);
    case '=':
      pos = cs->pos;
      curr_c = cs_get (c2m_ctx);
      if (curr_c == '=') {
        return new_token (c2m_ctx, pos, "==", T_EQNE, N_EQ);
      } else if (curr_c == '>') {
        return new_token (c2m_ctx, pos, "=>", T_FAT_ARROW, N_IGNORE);
      } else {
        cs_unget (c2m_ctx, curr_c);
        return new_token (c2m_ctx, pos, "=", '=', N_ASSIGN);
      }
      assert (FALSE);
    case '<':
    case '>':
      pos = cs->pos;
      curr_c = cs_get (c2m_ctx);
      if (curr_c == start_c) {
        curr_c = cs_get (c2m_ctx);
        if (curr_c == '=') {
          if (start_c == '<')
            return new_token (c2m_ctx, pos, "<<=", T_ASSIGN, N_LSH_ASSIGN);
          else
            return new_token (c2m_ctx, pos, ">>=", T_ASSIGN, N_RSH_ASSIGN);
        } else {
          cs_unget (c2m_ctx, curr_c);
          if (start_c == '<')
            return new_token (c2m_ctx, pos, "<<", T_SH, N_LSH);
          else
            return new_token (c2m_ctx, pos, ">>", T_SH, N_RSH);
        }
      } else if (curr_c == '=') {
        if (start_c == '<')
          return new_token (c2m_ctx, pos, "<=", T_CMP, N_LE);
        else
          return new_token (c2m_ctx, pos, ">=", T_CMP, N_GE);
      } else if (start_c == '<' && curr_c == ':') {
        return new_token (c2m_ctx, pos, "<:", '[', N_IGNORE);
      } else if (start_c == '<' && curr_c == '%') {
        return new_token (c2m_ctx, pos, "<%", '{', N_IGNORE);
      } else {
        cs_unget (c2m_ctx, curr_c);
        if (start_c == '<')
          return new_token (c2m_ctx, pos, "<", T_CMP, N_LT);
        else
          return new_token (c2m_ctx, pos, ">", T_CMP, N_GT);
      }
      assert (FALSE);
    case '*':
      pos = cs->pos;
      curr_c = cs_get (c2m_ctx);
      if (curr_c == '=') {
        return new_token (c2m_ctx, pos, "*=", T_ASSIGN, N_MUL_ASSIGN);
      } else {
        cs_unget (c2m_ctx, curr_c);
        return new_token (c2m_ctx, pos, "*", '*', N_MUL);
      }
      assert (FALSE);
    case '/':
      pos = cs->pos;
      curr_c = cs_get (c2m_ctx);
      assert (curr_c != '/' && curr_c != '*');
      if (curr_c == '=') return new_token (c2m_ctx, pos, "/=", T_ASSIGN, N_DIV_ASSIGN);
      assert (curr_c != '*' && curr_c != '/'); /* we already processed comments */
      cs_unget (c2m_ctx, curr_c);
      return new_token (c2m_ctx, pos, "/", T_DIVOP, N_DIV);
    case '%':
      pos = cs->pos;
      curr_c = cs_get (c2m_ctx);
      if (curr_c == '=') {
        return new_token (c2m_ctx, pos, "%=", T_ASSIGN, N_MOD_ASSIGN);
      } else if (curr_c == '>') {
        return new_token (c2m_ctx, pos, "%>", '}', N_IGNORE);
      } else if (curr_c == ':') {
        curr_c = cs_get (c2m_ctx);
        if (curr_c != '%') {
          cs_unget (c2m_ctx, curr_c);
          return new_token (c2m_ctx, pos, "%:", '#', N_IGNORE);
        } else {
          curr_c = cs_get (c2m_ctx);
          if (curr_c == ':')
            return new_token (c2m_ctx, pos, "%:%:", T_DBLNO, N_IGNORE);
          else {
            cs_unget (c2m_ctx, '%');
            cs_unget (c2m_ctx, curr_c);
            return new_token (c2m_ctx, pos, "%:", '#', N_IGNORE);
          }
        }
      } else {
        cs_unget (c2m_ctx, curr_c);
        return new_token (c2m_ctx, pos, "%", T_DIVOP, N_MOD);
      }
      assert (FALSE);
    case '&':
    case '|':
      pos = cs->pos;
      curr_c = cs_get (c2m_ctx);
      if (curr_c == '=') {
        if (start_c == '&')
          return new_token (c2m_ctx, pos, "&=", T_ASSIGN, N_AND_ASSIGN);
        else
          return new_token (c2m_ctx, pos, "|=", T_ASSIGN, N_OR_ASSIGN);
      } else if (curr_c == start_c) {
        if (start_c == '&')
          return new_token (c2m_ctx, pos, "&&", T_ANDAND, N_ANDAND);
        else
          return new_token (c2m_ctx, pos, "||", T_OROR, N_OROR);
      } else {
        cs_unget (c2m_ctx, curr_c);
        if (start_c == '&')
          return new_token (c2m_ctx, pos, "&", start_c, N_AND);
        else
          return new_token (c2m_ctx, pos, "|", start_c, N_OR);
      }
      assert (FALSE);
    case '^':
    case '!':
      pos = cs->pos;
      curr_c = cs_get (c2m_ctx);
      if (curr_c == '=') {
        if (start_c == '^')
          return new_token (c2m_ctx, pos, "^=", T_ASSIGN, N_XOR_ASSIGN);
        else
          return new_token (c2m_ctx, pos, "!=", T_EQNE, N_NE);
      } else {
        cs_unget (c2m_ctx, curr_c);
        if (start_c == '^')
          return new_token (c2m_ctx, pos, "^", '^', N_XOR);
        else
          return new_token (c2m_ctx, pos, "!", T_UNOP, N_NOT);
      }
      assert (FALSE);
    case ';': return new_token (c2m_ctx, cs->pos, ";", curr_c, N_IGNORE);
    case '?':
      pos = cs->pos;
      curr_c = cs_get (c2m_ctx);
      if (curr_c == '?') return new_token (c2m_ctx, pos, "??", T_QQ, N_COALESCE);
      if (curr_c == '.') {
        curr_c = cs_get (c2m_ctx);
        if (!isdigit (curr_c)) {
          cs_unget (c2m_ctx, curr_c);
          return new_token (c2m_ctx, pos, "?.", T_QDOT, N_IGNORE);
        }
        /* `cond ? .5 : x` — the dot starts a float literal, not safe-nav. */
        cs_unget (c2m_ctx, curr_c);
        cs_unget (c2m_ctx, '.');
      } else {
        cs_unget (c2m_ctx, curr_c);
      }
      return new_token (c2m_ctx, pos, "?", '?', N_IGNORE);
    case '(': return new_token (c2m_ctx, cs->pos, "(", curr_c, N_IGNORE);
    case ')': return new_token (c2m_ctx, cs->pos, ")", curr_c, N_IGNORE);
    case '{': return new_token (c2m_ctx, cs->pos, "{", curr_c, N_IGNORE);
    case '}': return new_token (c2m_ctx, cs->pos, "}", curr_c, N_IGNORE);
    case ']': return new_token (c2m_ctx, cs->pos, "]", curr_c, N_IGNORE);
    case EOF: {
      pos = cs->pos;
      if (eof_s != NULL) free_stream (c2m_ctx, eof_s);
      if (eof_s != cs && cs->f != stdin && cs->f != NULL) {
        fclose (cs->f);
        cs->f = NULL;
      }
      /* If this is the last stream, leave it on the stack so cs stays valid for
         any continued reads (e.g. error-recovery loops in token_concat).  The
         stream will be cleaned up by finish_streams at the end. */
      if (VARR_LENGTH (stream_t, streams) <= 1) {
        eof_s = NULL;
        return new_token (c2m_ctx, pos, "<EOU>", T_EOU, N_IGNORE);
      }
      eof_s = VARR_POP (stream_t, streams);
      cs = VARR_LAST (stream_t, streams);
      if (cs->f == NULL && cs->fname != NULL && !string_stream_p (cs)) {
        if ((cs->f = fopen (cs->fname, "rb")) == NULL) {
          if (c2m_options->message_file != NULL)
            fprintf (c2m_options->message_file, "cannot reopen file %s -- good bye\n", cs->fname);
          longjmp (c2m_ctx->env, 1);  // ???
        }
        fsetpos (cs->f, &cs->fpos);
      }
      return new_token (c2m_ctx, cs->pos, "<EOF>", T_EOFILE, N_IGNORE);
    }
    case ':':
      curr_c = cs_get (c2m_ctx);
      if (curr_c == '>') {
        return new_token (c2m_ctx, cs->pos, ":>", ']', N_IGNORE);
      } else {
        cs_unget (c2m_ctx, curr_c);
        return new_token (c2m_ctx, cs->pos, ":", ':', N_IGNORE);
      }
    case '#':
      curr_c = cs_get (c2m_ctx);
      if (curr_c == '#') {
        return new_token (c2m_ctx, cs->pos, "##", T_DBLNO, N_IGNORE);
      } else {
        cs_unget (c2m_ctx, curr_c);
        return new_token (c2m_ctx, cs->pos, "#", '#', N_IGNORE);
      }
    case ',': return new_token (c2m_ctx, cs->pos, ",", ',', N_COMMA);
    case '[': return new_token (c2m_ctx, cs->pos, "[", '[', N_IND);
    case '.':
      pos = cs->pos;
      curr_c = cs_get (c2m_ctx);
      if (curr_c == '.') {
        curr_c = cs_get (c2m_ctx);
        if (curr_c == '.') {
          return new_token (c2m_ctx, pos, "...", T_DOTS, N_IGNORE);
        } else {
          cs_unget (c2m_ctx, '.');
          cs_unget (c2m_ctx, curr_c);
          return new_token (c2m_ctx, pos, ".", '.', N_FIELD);
        }
      } else if (!isdigit (curr_c)) {
        cs_unget (c2m_ctx, curr_c);
        return new_token (c2m_ctx, pos, ".", '.', N_FIELD);
      }
      cs_unget (c2m_ctx, curr_c);
      curr_c = '.';
      /* falls through */
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9': {
      pos = cs->pos;
      VARR_TRUNC (char, symbol_text, 0);
      for (;;) {
        VARR_PUSH (char, symbol_text, curr_c);
        curr_c = cs_get (c2m_ctx);
        if (curr_c == 'e' || curr_c == 'E' || curr_c == 'p' || curr_c == 'P') {
          int c = cs_get (c2m_ctx);

          if (c == '+' || c == '-') {
            VARR_PUSH (char, symbol_text, curr_c);
            curr_c = c;
          } else {
            cs_unget (c2m_ctx, c);
          }
        } else if (!isdigit (curr_c) && !isalpha (curr_c) && curr_c != '_' && curr_c != '.')
          break;
      }
      VARR_PUSH (char, symbol_text, '\0');
      cs_unget (c2m_ctx, curr_c);
      return new_token_wo_uniq_repr (c2m_ctx, pos, VARR_ADDR (char, symbol_text), T_NUMBER,
                                     N_IGNORE);
    }
    case '\'':
    case '\"':
      wide_type = ' ';
    literal: {
      token_t t;
      int stop = curr_c;

      pos = cs->pos;
      VARR_PUSH (char, symbol_text, curr_c);
      for (curr_c = cs_get (c2m_ctx); curr_c != stop && curr_c != '\n' && curr_c != EOF;
           curr_c = cs_get (c2m_ctx)) {
        if (curr_c == '\0') {
          warning (c2m_ctx, pos, "null character in %s literal ignored",
                   stop == '"' ? "string" : "char");
        } else {
          VARR_PUSH (char, symbol_text, curr_c);
        }
        if (curr_c != '\\') continue;
        curr_c = cs_get (c2m_ctx);
        if (curr_c == '\n' || curr_c == EOF) break;
        if (curr_c == '\0') {
          warning (c2m_ctx, pos, "null character in %s literal ignored",
                   stop == '"' ? "string" : "char");
        } else {
          VARR_PUSH (char, symbol_text, curr_c);
        }
      }
      VARR_PUSH (char, symbol_text, curr_c);
      if (curr_c == stop) {
        if (stop == '\'' && VARR_LENGTH (char, symbol_text) == 1)
          error (c2m_ctx, pos, "empty character");
      } else {
        if (curr_c == '\n') cs_unget (c2m_ctx, '\n');
        error (c2m_ctx, pos, "unterminated %s", stop == '"' ? "string" : "char");
        VARR_PUSH (char, symbol_text, stop);
      }
      VARR_PUSH (char, symbol_text, '\0');
      if (wide_type == 'U' || (sizeof (mir_wchar) == 4 && wide_type == 'L')) {
        t = (stop == '\"' ? new_node_token (c2m_ctx, pos, VARR_ADDR (char, symbol_text), T_STR,
                                            new_str_node (c2m_ctx, N_STR32, empty_str, pos))
                          : new_node_token (c2m_ctx, pos, VARR_ADDR (char, symbol_text), T_CH,
                                            new_ch32_node (c2m_ctx, ' ', pos)));
      } else if (wide_type == 'u' || wide_type == 'L') {
        t = (stop == '\"' ? new_node_token (c2m_ctx, pos, VARR_ADDR (char, symbol_text), T_STR,
                                            new_str_node (c2m_ctx, N_STR16, empty_str, pos))
                          : new_node_token (c2m_ctx, pos, VARR_ADDR (char, symbol_text), T_CH,
                                            new_ch16_node (c2m_ctx, ' ', pos)));
      } else if (wide_type == 'f') { //fstring
        t = (stop == '\"' ? new_node_token (c2m_ctx, pos, VARR_ADDR (char, symbol_text), T_STR,
                                            new_str_node (c2m_ctx, N_STRING, empty_str, pos))
                          : new_node_token (c2m_ctx, pos, VARR_ADDR (char, symbol_text), T_CH,
                                            new_ch16_node (c2m_ctx, ' ', pos)));
      } else {
        t = (stop == '\"' ? new_node_token (c2m_ctx, pos, VARR_ADDR (char, symbol_text), T_STR,
                                            new_str_node (c2m_ctx, N_STR, empty_str, pos))
                          : new_node_token (c2m_ctx, pos, VARR_ADDR (char, symbol_text), T_CH,
                                            new_ch_node (c2m_ctx, ' ', pos)));
      }
      set_string_val (c2m_ctx, t, symbol_text, wide_type);
      return t;
    }
	      default:
	        if (isalpha (curr_c) || curr_c == '_') {
	          // Unicode, fstring
	          if (curr_c == 'L' || curr_c == 'u' || curr_c == 'U' || curr_c == 'f') {
	            wide_type = curr_c;
	            if ((curr_c = cs_get (c2m_ctx)) == '"' || curr_c == '\'') {
	              VARR_PUSH (char, symbol_text, wide_type);
	              goto literal;
	            } else if (wide_type == 'u' && curr_c == '8') {
	              wide_type = '8';
	              if ((curr_c = cs_get (c2m_ctx)) == '"') {
	                VARR_PUSH (char, symbol_text, 'u');
	                VARR_PUSH (char, symbol_text, '8');
	                goto literal;
	              }
	              cs_unget (c2m_ctx, curr_c);
	              curr_c = '8';
	            }
	            cs_unget (c2m_ctx, curr_c);
	            curr_c = wide_type;
	          }
	          pos = cs->pos;
	          /* Fast-path for the common ID case: most identifiers are short.
	             Collect into a small stack buffer first; fall back to VARR only
	             for unusually long names.  This avoids hundreds of thousands of
	             VARR_PUSH / VARR_ADDR calls on typical workloads. */
	          char idbuf[64];
	          size_t idlen = 0;
	          int use_varr = 0;
	          do {
	            if (!use_varr) {
	              if (idlen < sizeof(idbuf) - 1) {
	                idbuf[idlen++] = curr_c;
	              } else {
	                use_varr = 1;
	                /* flush what we have */
	                for (size_t k = 0; k < idlen; k++)
	                  VARR_PUSH (char, symbol_text, idbuf[k]);
	                VARR_PUSH (char, symbol_text, curr_c);
	                idlen = 0; /* not used after switch */
	              }
	            } else {
	              VARR_PUSH (char, symbol_text, curr_c);
	            }
	            curr_c = cs_get (c2m_ctx);
	          } while (isalnum (curr_c) || curr_c == '_');
	          cs_unget (c2m_ctx, curr_c);
	          if (use_varr) {
	            VARR_PUSH (char, symbol_text, '\0');
	            return new_id_token (c2m_ctx, pos, VARR_ADDR (char, symbol_text));
	          } else {
	            idbuf[idlen] = '\0';
	            return new_id_token (c2m_ctx, pos, idbuf);
	          }
	        } else {
        VARR_PUSH (char, symbol_text, curr_c);
        VARR_PUSH (char, symbol_text, '\0');
        return new_token_wo_uniq_repr (c2m_ctx, cs->pos, VARR_ADDR (char, symbol_text), curr_c,
                                       N_IGNORE);
      }
    }
  }
}

static token_t get_next_pptoken (c2m_ctx_t c2m_ctx) { return get_next_pptoken_1 (c2m_ctx, FALSE); }

static token_t get_next_include_pptoken (c2m_ctx_t c2m_ctx) {
  return get_next_pptoken_1 (c2m_ctx, TRUE);
}

#ifdef C2MIR_PREPRO_DEBUG
static const char *get_token_str (token_t t) {
  switch (t->code) {
  case T_EOFILE: return "EOF";
  case T_DBLNO: return "DBLNO";
  case T_PLM: return "PLM";
  case T_RDBLNO: return "RDBLNO";
  case T_BOA: return "BOA";
  case T_EOA: return "EOA";
  case T_EOR: return "EOR";
  case T_EOP: return "EOP";
  case T_EOU: return "EOU";
  default: return t->repr;
  }
}
#endif

static void unget_next_pptoken (c2m_ctx_t c2m_ctx, token_t t) {
  if (buffered_tokens != NULL) VARR_PUSH (token_t, buffered_tokens, t);
}

static const char *stringify (const char *str, VARR (char) * to) {
  VARR_TRUNC (char, to, 0);
  VARR_PUSH (char, to, '"');
  for (; *str != '\0'; str++) {
    if (*str == '\"' || *str == '\\') VARR_PUSH (char, to, '\\');
    VARR_PUSH (char, to, *str);
  }
  VARR_PUSH (char, to, '"');
  return VARR_ADDR (char, to);
}

static void destringify (const char *repr, VARR (char) * to) {
  int i, repr_len = (int) strlen (repr);

  VARR_TRUNC (char, to, 0);
  if (repr_len == 0) return;
  i = repr[0] == '"' ? 1 : 0;
  if (i == 1 && repr_len == 1) return;
  if (repr[repr_len - 1] == '"') repr_len--;
  for (; i < repr_len; i++)
    if (repr[i] != '\\' || i + 1 >= repr_len || (repr[i + 1] != '\\' && repr[i + 1] != '"'))
      VARR_PUSH (char, to, repr[i]);
}

/* TS - vector, T defines position for empty vector */
static token_t token_stringify (c2m_ctx_t c2m_ctx, token_t t, VARR (token_t) * ts) {
  if (VARR_LENGTH (token_t, ts) != 0) t = VARR_GET (token_t, ts, 0);
  t = new_node_token (c2m_ctx, t->pos, "", T_STR, new_str_node (c2m_ctx, N_STR, empty_str, t->pos));
  VARR_TRUNC (char, temp_string, 0);
  for (const char *s = t->repr; *s != 0; s++) VARR_PUSH (char, temp_string, *s);
  VARR_PUSH (char, temp_string, '"');
  for (size_t i = 0; i < VARR_LENGTH (token_t, ts); i++)
    if (VARR_GET (token_t, ts, i)->code == ' ' || VARR_GET (token_t, ts, i)->code == '\n') {
      VARR_PUSH (char, temp_string, ' ');
    } else {
      for (const char *s = VARR_GET (token_t, ts, i)->repr; *s != 0; s++) {
        int c = VARR_LENGTH (token_t, ts) == i + 1 ? '\0' : VARR_GET (token_t, ts, i + 1)->repr[0];

        /* It is an implementation defined behaviour analogous GCC/Clang (see set_string_val): */
        if (*s == '\"'
            || (*s == '\\' && c != '\\' && c != 'a' && c != 'b' && c != 'f' && c != 'n' && c != 'r'
                && c != 'v' && c != 't' && c != '?' && c != 'e' && !('0' <= c && c <= '7')
                && c != 'x' && c != 'X'))
          VARR_PUSH (char, temp_string, '\\');
        VARR_PUSH (char, temp_string, *s);
      }
    }
  VARR_PUSH (char, temp_string, '"');
  VARR_PUSH (char, temp_string, '\0');
  t->repr = uniq_cstr (c2m_ctx, VARR_ADDR (char, temp_string)).s;
  set_string_val (c2m_ctx, t, temp_string, ' ');
  return t;
}

static node_t get_int_node_from_repr (c2m_ctx_t c2m_ctx, const char *repr, char **stop, int base,
                                      int uns_p, int long_p, int llong_p, pos_t pos) {
  mir_ullong ull = strtoull (repr, stop, base);

  if (llong_p) {
    if (!uns_p && (base == 10 || ull <= MIR_LLONG_MAX)) return new_ll_node (c2m_ctx, ull, pos);
    return new_ull_node (c2m_ctx, ull, pos);
  }
  if (long_p) {
    if (!uns_p && ull <= MIR_LONG_MAX) return new_l_node (c2m_ctx, (long) ull, pos);
    if (ull <= MIR_ULONG_MAX) return new_ul_node (c2m_ctx, (unsigned long) ull, pos);
    if (!uns_p && (base == 10 || ull <= MIR_LLONG_MAX)) return new_ll_node (c2m_ctx, ull, pos);
    return new_ull_node (c2m_ctx, ull, pos);
  }
  if (uns_p) {
    if (ull <= MIR_UINT_MAX) return new_u_node (c2m_ctx, (unsigned long) ull, pos);
    if (ull <= MIR_ULONG_MAX) return new_ul_node (c2m_ctx, (unsigned long) ull, pos);
    return new_ull_node (c2m_ctx, ull, pos);
  }
  if (ull <= MIR_INT_MAX) return new_i_node (c2m_ctx, (long) ull, pos);
  if (base != 10 && ull <= MIR_UINT_MAX) return new_u_node (c2m_ctx, (unsigned long) ull, pos);
  if (ull <= MIR_LONG_MAX) return new_l_node (c2m_ctx, (long) ull, pos);
  if (ull <= MIR_ULONG_MAX) return new_ul_node (c2m_ctx, (unsigned long) ull, pos);
  if (base == 10 || ull <= MIR_LLONG_MAX) return new_ll_node (c2m_ctx, ull, pos);
  return new_ull_node (c2m_ctx, ull, pos);
}

/* Fast keyword classification for the common identifier case in pptoken2token.
   Avoids a full str_add / htab lookup for every identifier.  The table is
   a simple length+prefix switch; correctness is verified by the existing
   kw_add path that populates the real str_tab at init time. */
static token_code_t fast_keyword (const char *s, size_t len) {
  /* Bucketed by length, then memcmp of exactly `len` bytes (s is NUL-terminated
     so the read never runs past the identifier).  Every mapping here must match
     the kw_add table in parse_init; anything not listed falls through to the
     slow str_tab path, so this stays correct even if it is not exhaustive. */
#define KW(lit, tc)             \
  if (memcmp (s, (lit), len) == 0) return (tc)
  switch (len) {
  case 2:
    KW ("if", T_IF);
    KW ("do", T_DO);
    break;
  case 3:
    KW ("int", T_INT);
    KW ("for", T_FOR);
    break;
  case 4:
    KW ("char", T_CHAR);
    KW ("void", T_VOID);
    KW ("auto", T_AUTO);
    KW ("case", T_CASE);
    KW ("else", T_ELSE);
    KW ("enum", T_ENUM);
    KW ("dict", T_DICT);
    KW ("long", T_LONG);
    KW ("goto", T_GOTO);
    break;
  case 5:
    KW ("class", T_CLASS);
    KW ("const", T_CONST);
    KW ("float", T_FLOAT);
    KW ("short", T_SHORT);
    KW ("union", T_UNION);
    KW ("while", T_WHILE);
    KW ("break", T_BREAK);
    break;
  case 6:
    KW ("double", T_DOUBLE);
    KW ("return", T_RETURN);
    KW ("struct", T_STRUCT);
    KW ("switch", T_SWITCH);
    KW ("typeof", T_TYPEOF);
    KW ("sizeof", T_SIZEOF);
    KW ("extern", T_EXTERN);
    KW ("static", T_STATIC);
    KW ("signed", T_SIGNED);
    KW ("inline", T_INLINE);
    KW ("String", T_STRING);
    break;
  case 7:
    KW ("typedef", T_TYPEDEF);
    KW ("default", T_DEFAULT);
    break;
  case 8:
    KW ("unsigned", T_UNSIGNED);
    KW ("register", T_REGISTER);
    KW ("restrict", T_RESTRICT);
    KW ("continue", T_CONTINUE);
    KW ("volatile", T_VOLATILE);
    break;
  }
  return T_STR; /* not a keyword */
#undef KW
}

static token_t pptoken2token (c2m_ctx_t c2m_ctx, token_t t, int id2kw_p) {
  assert (t->code != T_HEADER && t->code != T_BOA && t->code != T_EOA && t->code != T_EOR
          && t->code != T_EOP && t->code != T_EOFILE && t->code != T_EOU && t->code != T_PLM
          && t->code != T_RDBLNO);
  if (t->code == T_NO_MACRO_IDENT) t->code = T_ID;
  if (t->code == T_ID && id2kw_p) {
    size_t id_len = strlen (t->repr);
    token_code_t kw = fast_keyword (t->repr, id_len);
    if (kw != T_STR) {
      t->code = kw;
      t->node_code = N_IGNORE;
      t->node = NULL;
      return t;
    }
    /* fall back to the full table (rare non-keyword path) */
    tab_str_t str = str_add (c2m_ctx, t->repr, id_len + 1, T_STR, 0, FALSE);
    if (str.key != T_STR) {
      t->code = (int) str.key;
      t->node_code = N_IGNORE;
      t->node = NULL;
    }
    return t;
  } else if (t->code == ' ' || t->code == '\n') {
    return NULL;
  } else if (t->code == T_NUMBER) {
    int i, base = 10, float_p = FALSE, double_p = FALSE, ldouble_p = FALSE;
    int uns_p = FALSE, long_p = FALSE, llong_p = FALSE;
    const char *repr = t->repr, *start = t->repr;
    char *stop;
    int last = (int) strlen (repr) - 1;

    assert (last >= 0);
    if (repr[0] == '0' && (repr[1] == 'x' || repr[1] == 'X')) {
      base = 16;
    } else if (repr[0] == '0' && (repr[1] == 'b' || repr[1] == 'B')) {
      (c2m_options->pedantic_p ? error : warning) (c2m_ctx, t->pos,
                                                   "binary number is not a standard: %s", t->repr);
      base = 2;
      start += 2;
    } else if (repr[0] == '0') {
      base = 8;
    }
    for (i = 0; i <= last; i++) {
      if (repr[i] == '.') {
        double_p = TRUE;
      } else if (repr[i] == 'p' || repr[i] == 'P') {
        double_p = TRUE;
      } else if ((repr[i] == 'e' || repr[i] == 'E') && base != 16) {
        double_p = TRUE;
      }
    }
    if (last >= 2
        && (strcmp (&repr[last - 2], "LLU") == 0 || strcmp (&repr[last - 2], "ULL") == 0
            || strcmp (&repr[last - 2], "llu") == 0 || strcmp (&repr[last - 2], "ull") == 0
            || strcmp (&repr[last - 2], "LLu") == 0 || strcmp (&repr[last - 2], "uLL") == 0
            || strcmp (&repr[last - 2], "llU") == 0 || strcmp (&repr[last - 2], "Ull") == 0)) {
      llong_p = uns_p = TRUE;
      last -= 3;
    } else if (last >= 1
               && (strcmp (&repr[last - 1], "LL") == 0 || strcmp (&repr[last - 1], "ll") == 0)) {
      llong_p = TRUE;
      last -= 2;
    } else if (last >= 1
               && (strcmp (&repr[last - 1], "LU") == 0 || strcmp (&repr[last - 1], "UL") == 0
                   || strcmp (&repr[last - 1], "lu") == 0 || strcmp (&repr[last - 1], "ul") == 0
                   || strcmp (&repr[last - 1], "Lu") == 0 || strcmp (&repr[last - 1], "uL") == 0
                   || strcmp (&repr[last - 1], "lU") == 0 || strcmp (&repr[last - 1], "Ul") == 0)) {
      long_p = uns_p = TRUE;
      last -= 2;
    } else if (strcmp (&repr[last], "L") == 0 || strcmp (&repr[last], "l") == 0) {
      long_p = TRUE;
      last--;
    } else if (strcmp (&repr[last], "U") == 0 || strcmp (&repr[last], "u") == 0) {
      uns_p = TRUE;
      last--;
    } else if (double_p && (strcmp (&repr[last], "F") == 0 || strcmp (&repr[last], "f") == 0)) {
      float_p = TRUE;
      double_p = FALSE;
      last--;
    }
    if (double_p) {
      if (uns_p || llong_p) {
        error (c2m_ctx, t->pos, "wrong number: %s", repr);
      } else if (long_p) {
        ldouble_p = TRUE;
        double_p = FALSE;
      }
    }
    errno = 0;
    if (float_p) {
      t->node = new_f_node (c2m_ctx, strtof (start, &stop), t->pos);
    } else if (double_p) {
      t->node = new_d_node (c2m_ctx, strtod (start, &stop), t->pos);
    } else if (ldouble_p) {
      t->node = new_ld_node (c2m_ctx, strtold (start, &stop), t->pos);
    } else {
      t->node
        = get_int_node_from_repr (c2m_ctx, start, &stop, base, uns_p, long_p, llong_p, t->pos);
    }
    if (stop != &repr[last + 1]) {
      if (c2m_options->message_file != NULL)
        fprintf (c2m_options->message_file, "%s:%s:%s\n", repr, stop, &repr[last + 1]);
      error (c2m_ctx, t->pos, "wrong number: %s", t->repr);
    } else if (errno) {
      if (float_p || double_p || ldouble_p) {
        warning (c2m_ctx, t->pos, "number %s is out of range -- using IEEE infinity", t->repr);
      } else {
        (c2m_options->pedantic_p ? error : warning) (c2m_ctx, t->pos, "number %s is out of range",
                                                     t->repr);
      }
    }
  }
  return t;
}


typedef struct {
  node_t id, scope;
  int typedef_p;
} tpname_t;

DEF_HTAB (tpname_t);

/* Generic class template registry */
typedef struct {
  const char *name;           /* original class name, e.g. "List" */
  node_t class_node;          /* N_CLASS template (unchecked, with T placeholders) */
  int n_type_params;          /* number of type parameters */
  const char *type_params[4]; /* parameter names: ["T", ...] */
} generic_tmpl_t;

DEF_VARR (generic_tmpl_t);

/* Specializations requested while the target template was still incomplete
   (class Name<T>; only).  Queued as mangled placeholders; drained when the
   completing class Name<T> { ... } body is registered. */
typedef struct {
  const char *base_name;
  int n_args;
  node_t args[4];
  pos_t pos;
} generic_deferred_spec_t;

DEF_VARR (generic_deferred_spec_t);

/* Specialization cache: tracks which List<String>, List<int>, etc. have been created.
   n_args/args retain the concrete type arguments so method specialization can
   re-bind class type params (T) from a receiver like `__generic_List_int`. */
typedef struct {
  const char *orig_name;  /* "List" */
  const char *spec_name;  /* "__generic_List_String" */
  int n_args;
  node_t args[4];
} generic_spec_t;

DEF_VARR (generic_spec_t);

/* Pending cross-generic reference: when specialize_node resolves a
   cross-reference like `Is<T>` inside `As<T>`'s body, it records the
   referenced template name + concrete args here.  get_or_create_specialization
   drains this after specialize_node returns, creating the referenced
   specializations without recursive calls that corrupt state. */
typedef struct {
  const char *ref_name;     /* "Is" */
  int n_args;               /* 1 */
  node_t args[4];           /* [N_ID("Drawable")] */
  pos_t pos;
} generic_crossref_t;

DEF_VARR (generic_crossref_t);
typedef const char *cstr_t;
DEF_VARR (cstr_t);

/* Generic function template registry.

   A generic function is declared like:

       T Max<T>(T a, T b) { return a > b ? a : b; }

   The template's N_FUNC_DEF is stored verbatim (with type-parameter N_ID
   placeholders like "T" in its specs, parameter types, and body).  At a call
   site `Max(3, 5)`, the checker infers T=int from the argument types,
   deep-copies the template with specialize_node (substituting T -> int),
   renames the function to a mangled specialization name
   (e.g. "__genfn_Max_int"), and injects the specialization into the module
   so it is checked and generated like any other function.  Subsequent calls
   with the same inferred type arguments reuse the cached specialization.

   This mirrors the generic-class machinery (generic_tmpl_t /
   generic_spec_t / specialize_node / pending_lambdas) and reuses the same
   specialize_node deep-copy + substitution walker. */
typedef struct {
  const char *name;           /* original function name, e.g. "Max" */
  node_t func_node;          /* N_FUNC_DEF template (unchecked, with T placeholders) */
  int n_type_params;          /* number of type parameters */
  const char *type_params[4]; /* parameter names: ["T", ...] */
} generic_fn_tmpl_t;

DEF_VARR (generic_fn_tmpl_t);

/* Specialization cache for generic functions: tracks which Max<int>,
   Max<String>, etc. have been materialized so repeated call sites reuse the
   same mangled function instead of re-instantiating. */
typedef struct {
  const char *orig_name;  /* "Max" */
  const char *spec_name;  /* "__genfn_Max_int" */
} generic_fn_spec_t;

DEF_VARR (generic_fn_spec_t);

/* Generic *method* templates: class methods with their own type parameters,
   e.g. `List<U>* Select<U>(U(*fn)(T))` on `class List<T>`.  The method lives
   in the class template AST (T open, U open).  At a call site on a specialized
   receiver (`List<int>* xs`) we monomorphize both the class type args and the
   method type args into a free function with an explicit `this` parameter:
     `__genmeth_List_int_Select_String(__generic_List_int *this, String(*fn)(int))`
   Call sites rewrite to that free function (see N_CALL method path). */
typedef struct {
  const char *class_name;      /* base template class name, e.g. "List" */
  const char *method_name;     /* "Select" */
  node_t func_node;            /* N_FUNC_DEF template from the class body */
  int n_type_params;           /* method type params only (U, not class T) */
  const char *type_params[4];
  int is_static;               /* 1 = no implicit this */
  int da_ignore_p;             /* trailing __attribute__((da_ignore)) on template */
} generic_method_tmpl_t;

DEF_VARR (generic_method_tmpl_t);

typedef struct {
  const char *spec_name;       /* "__genmeth_List_int_Select_String" */
} generic_method_spec_t;

DEF_VARR (generic_method_spec_t);

/* Interface registry (Phase 1): a named, STRUCTURAL method-set contract.
   Recording the signatures by name lets later phases ask "does class C satisfy
   interface I?" structurally — the same duck-typing the compiler already does
   for List's Count/Get/Add, just named.  Emits no runtime type or layout. */
typedef struct {
  const char *name; /* interface name, e.g. "Greeter" */
  node_t node;      /* N_INTERFACE: id(0), member_list(1 = N_LIST of N_MEMBER) */
} iface_t;

DEF_VARR (iface_t);

/* ── type_kind: compile-time category of a type for memory / copy policy ──
 *
 * Shared fact used by specialization gates, list/map element policy (later),
 * ownership analysis, midopt, and diagnostics.  Not a nameof/typeof string —
 * a semantic lattice of how T may be stored and destroyed.
 *
 *   TK_POD              scalars/enums/no-dtor pure data; bitwise multi-copy OK
 *   TK_QUIET_VALUE      dtor present but does not release unique resources
 *                       (counting/log-only, or [[copyable_no_release]] waiver)
 *   TK_ARENA_VALUE      String-like; known arena / field copy policy
 *   TK_UNIQUE_RESOURCE  dtor frees/deletes unique stuff — NOT List-by-value
 *   TK_MOVE_ONLY        List/Map/Set shells — transfer with move, not assign
 *   TK_POINTER          raw pointer type (elements handled via .owns() path)
 *   TK_OPAQUE           cannot prove quiet vs unique — reject by-value elements
 *
 * Payoff:
 *   Bug detection  — reject List<Owns>, double-free shapes, wrong owns()
 *   Fun errors     — kind-specific messages with the right fix-it
 *   Optimization   — POD/QUIET: memcpy + elide redundant destroys; UNIQUE:
 *                    force pointer path; MOVE_ONLY: keep move-only checks
 *   Ownership      — skip quiet by-value locals; track UNIQUE/pointers only
 */
typedef enum {
  TK_POD = 0,
  TK_QUIET_VALUE,
  TK_ARENA_VALUE,
  TK_UNIQUE_RESOURCE,
  TK_MOVE_ONLY,
  TK_POINTER,
  TK_OPAQUE,
} type_kind_t;

/* Per-class parse-time cache: N_CLASS node → kind + attributes. */
typedef struct class_type_meta {
  node_t class_node;
  type_kind_t kind;
  unsigned copyable_no_release_p : 1; /* [[copyable_no_release]] on the class */
  unsigned kind_valid_p : 1;
  unsigned has_user_dtor_p : 1;
  unsigned dtor_releases_p : 1; /* dtor AST calls free/delete/… */
} class_type_meta_t;

DEF_VARR (class_type_meta_t);

struct parse_ctx {
  int record_level;
  size_t next_token_index;
  token_t curr_token;
  node_t curr_scope;
  node_t curr_class;
  HTAB (tpname_t) * tpname_tab;
  VARR (node_t) * pending_lambdas; /* lambda N_FUNC_DEFs waiting for module injection */
  unsigned lambda_uid;             /* counter for unique lambda names */
  VARR (generic_tmpl_t) * generic_templates; /* registered generic class templates */
  VARR (generic_spec_t) * generic_specs;     /* created specializations (dedup cache) */
  VARR (generic_crossref_t) * generic_crossrefs; /* pending cross-generic refs */
  VARR (generic_deferred_spec_t) * generic_deferred_specs; /* incomplete-template wait queue */
  VARR (iface_t) * interfaces;               /* registered interface contracts */
  VARR (generic_fn_tmpl_t) * generic_fn_templates; /* registered generic function templates */
  VARR (generic_fn_spec_t) * generic_fn_specs;     /* generic function specialization cache */
  VARR (generic_method_tmpl_t) * generic_method_templates;
  VARR (generic_method_spec_t) * generic_method_specs;
  VARR (cstr_t) * generic_in_progress;
  VARR (class_type_meta_t) * class_type_metas; /* type_kind cache + class attrs */
  VARR (node_t) * parsed_classes;      /* every N_CLASS definition/forward decl (parse-time) */
  /* Method type params currently being parsed (Select<U>): treated like outer
     class type params for nested-specialization placeholder purposes. */
  int n_method_type_params;
  const char *method_type_params[4];
  /* When a reserved keyword (e.g. `class`) is seen where an identifier is
     required, remember it.  Top-level TRY(declaration) often rewinds so that
     by the time error_recovery runs, curr_token is no longer on the keyword
     and the generic "syntax error on struct/int" message is useless. */
  const char *reserved_id_kw; /* NULL, or e.g. "class" */
  pos_t reserved_id_pos;
  /* Free-function trail: TRY(declaration) consumes
     `void f() __attribute__((da_ignore))` then fails on `{` and rewinds the
     token stream — but the free-function path re-parses the declarator and
     must still learn about da_ignore.  Set when declaration sees
     attrs+`{` so the function-definition branch can apply PRECHECK_DA_IGNORE. */
  int pending_func_da_ignore;
};

#define record_level parse_ctx->record_level
#define next_token_index parse_ctx->next_token_index
#define curr_token parse_ctx->curr_token
#define curr_scope parse_ctx->curr_scope
#define tpname_tab parse_ctx->tpname_tab
#define pending_lambdas parse_ctx->pending_lambdas
#define lambda_uid parse_ctx->lambda_uid
#define generic_templates parse_ctx->generic_templates
#define generic_specs parse_ctx->generic_specs
#define generic_crossrefs parse_ctx->generic_crossrefs
#define generic_deferred_specs parse_ctx->generic_deferred_specs
#define interfaces parse_ctx->interfaces
#define generic_fn_templates parse_ctx->generic_fn_templates
#define generic_fn_specs parse_ctx->generic_fn_specs
#define generic_method_templates parse_ctx->generic_method_templates
#define generic_method_specs parse_ctx->generic_method_specs
#define generic_in_progress parse_ctx->generic_in_progress
#define class_type_metas parse_ctx->class_type_metas
#define parsed_classes parse_ctx->parsed_classes
#define n_method_type_params parse_ctx->n_method_type_params
#define method_type_params parse_ctx->method_type_params

static struct node err_struct;
static const node_t err_node = &err_struct;

static void read_token (c2m_ctx_t c2m_ctx) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;

  curr_token = VARR_GET (token_t, recorded_tokens, next_token_index);
  next_token_index++;
}

static size_t record_start (c2m_ctx_t c2m_ctx) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;

  assert (next_token_index > 0 && record_level >= 0);
  record_level++;
  return next_token_index - 1;
}

static void record_stop (c2m_ctx_t c2m_ctx, size_t mark, int restore_p) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;

  assert (record_level > 0);
  record_level--;
  if (!restore_p) return;
  next_token_index = mark;
  read_token (c2m_ctx);
}

/* Stash a reserved-keyword-as-identifier misuse.  Kept until the next
   error_recovery / syntax_error so the message can be reported at the right
   place even after TRY rewinds the token stream. */
static void note_reserved_as_identifier (c2m_ctx_t c2m_ctx, const char *kw) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;

  if (parse_ctx == NULL || kw == NULL) return;
  if (parse_ctx->reserved_id_kw != NULL) return; /* keep the first site */
  parse_ctx->reserved_id_kw = kw;
  parse_ctx->reserved_id_pos = curr_token->pos;
}

/* Emit the stashed reserved-keyword diagnostic, if any.  Returns 1 if it
   reported (caller should skip a less-specific syntax_error). */
static int report_reserved_as_identifier (c2m_ctx_t c2m_ctx) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  const char *kw;
  pos_t pos;

  if (parse_ctx == NULL || parse_ctx->reserved_id_kw == NULL) return 0;
  kw = parse_ctx->reserved_id_kw;
  pos = parse_ctx->reserved_id_pos;
  parse_ctx->reserved_id_kw = NULL;
  error (c2m_ctx, pos,
         "'%s' is a reserved keyword and cannot be used as an identifier "
         "(ClassyC, like C++, reserves '%s' for class definitions — rename "
         "the variable, parameter, or member)",
         kw, kw);
  return 1;
}

/* If curr_token is a hard keyword commonly mistaken for an identifier, note
   it for a clearer diagnostic.  Returns 1 when a note was recorded. */
static int note_if_reserved_identifier_token (c2m_ctx_t c2m_ctx) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;

  if (parse_ctx == NULL || curr_token == NULL) return 0;
  if (curr_token->code == T_CLASS) {
    note_reserved_as_identifier (c2m_ctx, "class");
    return 1;
  }
  return 0;
}

static void syntax_error (c2m_ctx_t c2m_ctx, const char *expected_name) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  FILE *f = c2m_options->message_file;
  pos_t pos = curr_token->pos;
  const char *tok = get_token_name (c2m_ctx, curr_token->code);
  /* Prefer a stashed reserved-keyword misuse (often at a better location after
     TRY rewind) over a generic "syntax error on struct/int" message. */
  if (report_reserved_as_identifier (c2m_ctx)) return;
  /* curr_token itself is the reserved word (e.g. `p->class`, PT(T_ID) fails). */
  if (curr_token->code == T_CLASS) {
    note_reserved_as_identifier (c2m_ctx, "class");
    if (report_reserved_as_identifier (c2m_ctx)) return;
  }
  /* When the offending token is a bare identifier, surface its spelling and
     a hint that it isn't a known type.  This catches the very common case of
     a method returning/parameter-taking a class/typedef that hasn't been
     declared yet (or is misspelled / missing an #include): the previous
     message only said "syntax error on class (expected ...)" at a position
     far from the real culprit. */
  const char *id_name = NULL;
  if (curr_token->code == T_ID && curr_token->node != NULL
      && curr_token->node->code == N_ID) {
    id_name = curr_token->node->u.s.s;
  }

  n_errors++;
  if (log_diag_active ()) {
    char buf[512];
    log_diag_t d;

    if (id_name != NULL)
      snprintf (buf, sizeof buf,
                "syntax error on identifier '%s' (expected '%s'); '%s' is not a declared type",
                id_name, expected_name, id_name);
    else
      snprintf (buf, sizeof buf, "syntax error on %s (expected '%s')", tok, expected_name);
    d.file = pos.fname;
    d.line = pos.lno;
    d.col = pos.ln_pos;
    d.error_p = TRUE;
    d.message = buf;
    log_emit_diag (&d);
  }
  if (f != NULL) {
    int color = log_color_enabled (f);
    fputs (log_c (color, LOG_BOLD), f);
    if (pos.lno >= 0) fprintf (f, "%s:%d:%d: ", pos.fname, pos.lno, pos.ln_pos);
    if (id_name != NULL) {
      fprintf (f, "%ssyntax error%s on identifier %s'%s'%s",
               log_c (color, LOG_BRED), log_c (color, LOG_RESET),
               log_c (color, LOG_BOLD), id_name, log_c (color, LOG_RESET));
      fprintf (f, " (expected '%s%s%s')\n", log_c (color, LOG_BGREEN), expected_name,
               log_c (color, LOG_RESET));
      fprintf (f, "%s  note:%s '%s' is not a declared type, class, or typedef"
                  " \u2014 forward declaration order matters for typedefs/structs"
                  " (classes are pre-registered file-wide).\n",
               log_c (color, LOG_BOLD), log_c (color, LOG_RESET), id_name);
    } else {
      fprintf (f, "%ssyntax error%s on %s%s%s", log_c (color, LOG_BRED), log_c (color, LOG_RESET),
               log_c (color, LOG_BOLD), tok, log_c (color, LOG_RESET));
      fprintf (f, " (expected '%s%s%s'):\n", log_c (color, LOG_BGREEN), expected_name,
               log_c (color, LOG_RESET));
    }
  }
}

static int tpname_eq (tpname_t tpname1, tpname_t tpname2, void *arg MIR_UNUSED) {
  return tpname1.id->u.s.s == tpname2.id->u.s.s && tpname1.scope == tpname2.scope;
}

static htab_hash_t tpname_hash (tpname_t tpname, void *arg MIR_UNUSED) {
  return (htab_hash_t) (mir_hash_finish (
    mir_hash_step (mir_hash_step (mir_hash_init (0x42), (uint64_t) tpname.id->u.s.s),
                   (uint64_t) tpname.scope)));
}

static void tpname_init (c2m_ctx_t c2m_ctx) {
  MIR_alloc_t alloc = c2m_alloc (c2m_ctx);
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;

  HTAB_CREATE (tpname_t, tpname_tab, alloc, 1000, tpname_hash, tpname_eq, NULL);
}

static int tpname_find (c2m_ctx_t c2m_ctx, node_t id, node_t scope, tpname_t *res) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  int found_p;
  tpname_t el, tpname;

  tpname.id = id;
  tpname.scope = scope;
  found_p = HTAB_DO (tpname_t, tpname_tab, tpname, HTAB_FIND, el);
  if (res != NULL && found_p) *res = el;
  return found_p;
}

static tpname_t tpname_add (c2m_ctx_t c2m_ctx, node_t id, node_t scope, int typedef_p) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  tpname_t el, tpname;

  tpname.id = id;
  tpname.scope = scope;
  tpname.typedef_p = typedef_p;
  if (HTAB_DO (tpname_t, tpname_tab, tpname, HTAB_FIND, el)) return el;
  HTAB_DO (tpname_t, tpname_tab, tpname, HTAB_INSERT, el);
  return el;
}

static void tpname_finish (c2m_ctx_t c2m_ctx) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;

  if (tpname_tab != NULL) HTAB_DESTROY (tpname_t, tpname_tab);
}

const char* get_filename(const char* path) {
    const char* filename = strrchr(path, '/');
#ifdef _WIN32
    // Also check for Windows separator if not found.
    if (filename == NULL) {
        filename = strrchr(path, '\\');
    }
#endif
    // If no separator, the whole path is the filename; otherwise, skip the separator.
    return (filename != NULL) ? filename + 1 : path;
}

static void tpname_dump_one (tpname_t tpname, void *arg) {
  c2m_ctx_t c2m_ctx = (c2m_ctx_t) arg;
  pos_t pos = no_pos;

  if (tpname.id != NULL)
    pos = POS(tpname.id);

  fprintf (stderr, "%-24s\t %s (scope %-16s)  at %-16s:%d:%d\n",
           tpname.id->u.s.s,
           tpname.typedef_p ? "typedef" : "-------",
           tpname.scope ? tpname.scope->u.s.s : "NONE",
           pos.fname != NULL ? get_filename(pos.fname) : "<unknown>",
           pos.lno, pos.ln_pos);
}

static void tpname_dump (c2m_ctx_t c2m_ctx, FILE *f) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  fprintf (stderr, "==== TYPE NAME DUMP ====\n");
  HTAB_FOREACH_ELEM (tpname_t, tpname_tab, tpname_dump_one, c2m_ctx);
  fprintf (stderr, "=====================\n");
}

#define P(f)                                                 \
  do {                                                       \
    if ((r = (f) (c2m_ctx, no_err_p)) == err_node) return r; \
  } while (0)
#define PA(f, a)                                                \
  do {                                                          \
    if ((r = (f) (c2m_ctx, no_err_p, a)) == err_node) return r; \
  } while (0)
#define PTFAIL(t)                                                               \
  do {                                                                          \
    if (record_level == 0) syntax_error (c2m_ctx, get_token_name (c2m_ctx, t)); \
    return err_node;                                                            \
  } while (0)

#define PT(t)               \
  do {                      \
    if (!M (t)) PTFAIL (t); \
  } while (0)

#define PTP(t, pos)               \
  do {                            \
    if (!MP (t, pos)) PTFAIL (t); \
  } while (0)

#define PTN(t)                  \
  do {                          \
    if (!MN (t, r)) PTFAIL (t); \
  } while (0)

#define PE(f, l)                                           \
  do {                                                     \
    if ((r = (f) (c2m_ctx, no_err_p)) == err_node) goto l; \
  } while (0)
#define PAE(f, a, l)                                          \
  do {                                                        \
    if ((r = (f) (c2m_ctx, no_err_p, a)) == err_node) goto l; \
  } while (0)
#define PTE(t, pos, l)        \
  do {                        \
    if (!MP (t, pos)) goto l; \
  } while (0)

typedef node_t (*nonterm_func_t) (c2m_ctx_t c2m_ctx, int);
typedef node_t (*nonterm_arg_func_t) (c2m_ctx_t c2m_ctx, int, node_t);

#define D(f) static node_t f (c2m_ctx_t c2m_ctx, int no_err_p MIR_UNUSED)
#define DA(f) static node_t f (c2m_ctx_t c2m_ctx, int no_err_p, node_t arg)

/* Forward decl: primary_expr (expression-context generic instantiation)
   composes postfix operators via post_expr_part, which is defined later. */
static node_t post_expr_part (c2m_ctx_t c2m_ctx, int no_err_p, node_t arg);

/* Forward decl: C23 `[[...]]` attribute specifier, referenced by
   try_attr_spec (defined earlier) but implemented alongside D(attr). */
static node_t c23_attr_spec (c2m_ctx_t c2m_ctx, int no_err_p MIR_UNUSED);

/* Built-in method registry (header-extensible via [[builtin_method(...)]]).
   Defined with get_string_method; used from declaration parsing + parse_init. */
static void builtin_methods_init (MIR_alloc_t alloc);
static void builtin_methods_finish (void);
static void register_builtin_methods_from_attrs (c2m_ctx_t c2m_ctx, node_t attrs);

#define C(c) (curr_token->code == c)

static int match (c2m_ctx_t c2m_ctx, int c, pos_t *pos, node_code_t *node_code, node_t *node) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;

  if (curr_token->code != c) return FALSE;
  if (pos != NULL) *pos = curr_token->pos;
  if (node_code != NULL) *node_code = curr_token->node_code;
  if (node != NULL) *node = curr_token->node;
  read_token (c2m_ctx);
  return TRUE;
}

#define M(c) match (c2m_ctx, c, NULL, NULL, NULL)
#define MP(c, pos) match (c2m_ctx, c, &(pos), NULL, NULL)
#define MC(c, pos, code) match (c2m_ctx, c, &(pos), &(code), NULL)
#define MN(c, node) match (c2m_ctx, c, NULL, NULL, &(node))

/* Match a context-sensitive ("soft") keyword: an ordinary identifier with the
   given spelling, consumed only in grammar positions that expect it.  This lets
   words like `in` act as keywords (dict membership, for-in) while remaining
   usable as normal C identifiers (variables, parameters, members) elsewhere. */
static int match_soft_kw (c2m_ctx_t c2m_ctx, const char *kw, pos_t *pos, node_t *node) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;

  if (curr_token->code != T_ID || curr_token->repr == NULL
      || strcmp (curr_token->repr, kw) != 0)
    return FALSE;
  if (pos != NULL) *pos = curr_token->pos;
  if (node != NULL) *node = curr_token->node;
  read_token (c2m_ctx);
  return TRUE;
}

#define C_SOFT(kw) (curr_token->code == T_ID && curr_token->repr != NULL \
                    && strcmp (curr_token->repr, kw) == 0)
#define M_SOFT(kw) match_soft_kw (c2m_ctx, kw, NULL, NULL)
#define MP_SOFT(kw, pos) match_soft_kw (c2m_ctx, kw, &(pos), NULL)

static node_t try_f (c2m_ctx_t c2m_ctx, nonterm_func_t f) {
  size_t mark = record_start (c2m_ctx);
  node_t r = (f) (c2m_ctx, TRUE);

  record_stop (c2m_ctx, mark, r == err_node);
  return r;
}

static node_t try_arg_f (c2m_ctx_t c2m_ctx, nonterm_arg_func_t f, node_t arg) {
  size_t mark = record_start (c2m_ctx);
  node_t r = (f) (c2m_ctx, TRUE, arg);

  record_stop (c2m_ctx, mark, r == err_node);
  return r;
}

#define TRY(f) try_f (c2m_ctx, f)
#define TRY_A(f, arg) try_arg_f (c2m_ctx, f, arg)

/* ─────────────────────────── Generics helpers ─────────────────────────── */

/* Defined later in the check phase; used when specializing over local classes. */
static node_t find_def (c2m_ctx_t c2m_ctx, enum symbol_mode mode, node_t id, node_t scope,
                        node_t *aux_node);
static void symbol_insert (c2m_ctx_t c2m_ctx, enum symbol_mode mode, node_t id, node_t scope,
                           node_t def_node, node_t aux_node);
static int symbol_find (c2m_ctx_t c2m_ctx, enum symbol_mode mode, node_t id, node_t scope,
                        symbol_t *res);

/* Local/nested classes used as generic type args (List<Item*> with Item defined
   inside a function) are not yet visible at top_scope when the specialization is
   first checked (it is injected before the enclosing function).  Collect them
   here during specialization creation and apply at N_MODULE check start. */
typedef struct { node_t id; node_t class_def; } local_type_hoist_t;
DEF_VARR (local_type_hoist_t);
static VARR (local_type_hoist_t) *local_type_hoists;

/* Returns 1 if `name` is a registered generic class template. */
static int is_generic_class_p (c2m_ctx_t c2m_ctx, const char *name) {
  if (c2m_ctx->parse_ctx == NULL) return 0;
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  VARR (generic_tmpl_t) *gt = generic_templates; /* macro: parse_ctx->generic_templates */
  if (gt == NULL) return 0;
  for (size_t i = 0; i < VARR_LENGTH (generic_tmpl_t, gt); i++)
    if (strcmp (VARR_GET (generic_tmpl_t, gt, i).name, name) == 0) return 1;
  return 0;
}

/* Returns a pointer to the template for `name`, or NULL if not found. */
static generic_tmpl_t *get_generic_template (c2m_ctx_t c2m_ctx, const char *name) {
  if (c2m_ctx->parse_ctx == NULL) return NULL;
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  VARR (generic_tmpl_t) *gt = generic_templates; /* macro: parse_ctx->generic_templates */
  if (gt == NULL) return NULL;
  for (size_t i = 0; i < VARR_LENGTH (generic_tmpl_t, gt); i++) {
    generic_tmpl_t *t = &VARR_ADDR (generic_tmpl_t, gt)[i];
    if (strcmp (t->name, name) == 0) return t;
  }
  return NULL;
}

/* Index of template `name`, or (size_t)-1.  Used when completing a forward decl. */
static size_t get_generic_template_index (c2m_ctx_t c2m_ctx, const char *name) {
  if (c2m_ctx->parse_ctx == NULL || name == NULL) return (size_t)-1;
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  VARR (generic_tmpl_t) *gt = generic_templates;
  if (gt == NULL) return (size_t)-1;
  for (size_t i = 0; i < VARR_LENGTH (generic_tmpl_t, gt); i++) {
    if (strcmp (VARR_GET (generic_tmpl_t, gt, i).name, name) == 0) return i;
  }
  return (size_t)-1;
}

/* True if the template is registered but still incomplete (class Name<T>; with
   no body yet, or mid-parse pre-registration before '}' ). */
static int generic_template_incomplete_p (generic_tmpl_t *t) {
  return t != NULL && t->class_node == NULL;
}

/* Phase 2: Any<I> erased-handle synthesis (defined after find_interface).
   Forward-declared here because parse_generic_type_arg / type_spec reference
   them while parsing type arguments. */
static node_t synthesize_any_class (c2m_ctx_t c2m_ctx, const char *iface_name, pos_t pos);
static node_t parse_any_instantiation (c2m_ctx_t c2m_ctx, pos_t pos);
static node_t parse_synth_func_def (c2m_ctx_t c2m_ctx);

/* Pretty-print a mangled specialization id when it is a simple
   `__generic_Base_Arg0[_ArgN]*` form (no nested mangled args).  e.g.
   __generic_List_String -> List<String>, __generic_List_intP -> List<int*>.
   Complex / nested mangled names fall through unchanged. */
static const char *pretty_generic_type_name (c2m_ctx_t c2m_ctx, const char *s) {
  char base[128];
  char args[8][96];
  int n_args = 0, i;
  const char *p, *us;
  size_t blen;

  if (s == NULL || strncmp (s, "__generic_", 10) != 0) return s;
  p = s + 10;
  us = strchr (p, '_');
  if (us == NULL || us == p) return s;
  blen = (size_t) (us - p);
  if (blen == 0 || blen >= sizeof (base)) return s;
  memcpy (base, p, blen);
  base[blen] = '\0';
  p = us + 1;
  while (*p != '\0' && n_args < 8) {
    size_t alen, baselen, k;
    int ptr_depth = 0;
    us = strchr (p, '_');
    alen = us != NULL ? (size_t) (us - p) : strlen (p);
    if (alen == 0 || alen >= sizeof (args[0])) return s;
    /* Nested mangle embedded as an arg starts with "_generic" after the join
       underscore — reject and keep the raw name. */
    if (alen >= 9 && strncmp (p, "_generic", 8) == 0) return s;
    baselen = alen;
    while (baselen > 0 && p[baselen - 1] == 'P') {
      ptr_depth++;
      baselen--;
    }
    if (baselen == 0) return s;
    memcpy (args[n_args], p, baselen);
    args[n_args][baselen] = '\0';
    for (k = 0; k < (size_t) ptr_depth && baselen + k + 1 < sizeof (args[0]); k++)
      args[n_args][baselen + k] = '*';
    args[n_args][baselen + ptr_depth] = '\0';
    n_args++;
    if (us == NULL) break;
    p = us + 1;
  }
  if (n_args == 0) return s;
  VARR_TRUNC (char, temp_string, 0);
  add_to_temp_string (c2m_ctx, base);
  add_to_temp_string (c2m_ctx, "<");
  for (i = 0; i < n_args; i++) {
    if (i > 0) add_to_temp_string (c2m_ctx, ",");
    add_to_temp_string (c2m_ctx, args[i]);
  }
  add_to_temp_string (c2m_ctx, ">");
  return uniq_cstr (c2m_ctx, VARR_ADDR (char, temp_string)).s;
}

/* Convert a parse-time type-argument AST node into a nameof/typeof string.
   keep_ptr_p=0 (nameof): strip pointer wrappers — nameof<int*>() == "int".
   keep_ptr_p=1 (typeof): keep stars — typeof<int*>() == "int*".
   ID nodes that are generic specializations are pretty-printed when simple. */
static const char *type_arg_reflection_name (c2m_ctx_t c2m_ctx, node_t a, int keep_ptr_p) {
  int ptr_depth = 0;
  const char *nm = NULL;

  if (a == NULL) return "?";
  while (a != NULL && a->code == N_POINTER) {
    ptr_depth++;
    a = NL_HEAD (a->u.ops);
  }
  if (a == NULL) return "?";
  switch (a->code) {
  case N_STRING:   nm = "String"; break;
  case N_INT:      nm = "int"; break;
  case N_DOUBLE:   nm = "double"; break;
  case N_FLOAT:    nm = "float"; break;
  case N_CHAR:     nm = "char"; break;
  case N_LONG:     nm = "long"; break;
  case N_SHORT:    nm = "short"; break;
  case N_UNSIGNED: nm = "unsigned"; break;
  case N_VOID:     nm = "void"; break;
  case N_DICT:     nm = "dict"; break;
  case N_BOOL:     nm = "bool"; break;
  case N_ID:       nm = pretty_generic_type_name (c2m_ctx, a->u.s.s); break;
  case N_ENUM: {
    node_t id = TAG_ID (a);
    nm = (id != NULL && id->code == N_ID) ? id->u.s.s : "enum";
    break;
  }
  case N_STRUCT: {
    node_t id = TAG_ID (a);
    nm = (id != NULL && id->code == N_ID) ? id->u.s.s : "struct";
    break;
  }
  case N_CLASS: {
    node_t id = TAG_ID (a);
    nm = (id != NULL && id->code == N_ID)
           ? pretty_generic_type_name (c2m_ctx, id->u.s.s) : "class";
    break;
  }
  case N_UNION: {
    node_t id = TAG_ID (a);
    nm = (id != NULL && id->code == N_ID) ? id->u.s.s : "union";
    break;
  }
  default:         nm = "?"; break;
  }
  if (!keep_ptr_p || ptr_depth == 0) return nm;
  {
    size_t nlen = strlen (nm);
    char buf[256];
    if (nlen + (size_t) ptr_depth + 1 >= sizeof (buf)) return nm;
    memcpy (buf, nm, nlen);
    for (int i = 0; i < ptr_depth; i++) buf[nlen + (size_t) i] = '*';
    buf[nlen + (size_t) ptr_depth] = '\0';
    return uniq_cstr (c2m_ctx, buf).s;
  }
}

/* Move-only collection type arg?  Mirrors class_is_move_only_collection_p's
   name check on the (possibly materialized) type node: __generic_List_ /
   __generic_Map_ / __generic_Set_ instantiations (pointers peeled first).
   Backs the is_move_only<T>() intrinsic. */
static int type_arg_move_only_p (node_t a) {
  const char *nm = NULL;
  while (a != NULL && a->code == N_POINTER) {
    /* Pointers (List<int>* included) are copied bitwise — never move-only. */
    return 0;
  }
  if (a == NULL) return 0;
  if (a->code == N_ID) {
    nm = a->u.s.s;
  } else if (a->code == N_CLASS) {
    node_t id = TAG_ID (a);
    if (id != NULL && id->code == N_ID) nm = id->u.s.s;
  }
  if (nm == NULL) return 0;
  return (strncmp (nm, "__generic_List_", 15) == 0
          || strncmp (nm, "__generic_Map_", 14) == 0
          || strncmp (nm, "__generic_Set_", 14) == 0);
}

/* Build a mangled specialization name, e.g. List + String -> __generic_List_String */
static const char *mangle_generic_name (c2m_ctx_t c2m_ctx,
                                         const char *base_name,
                                         int n_args, node_t *args) {
  VARR_TRUNC (char, temp_string, 0);
  add_to_temp_string (c2m_ctx, "__generic_");
  add_to_temp_string (c2m_ctx, base_name);
  for (int i = 0; i < n_args; i++) {
    add_to_temp_string (c2m_ctx, "_");
    node_t a = args[i];
    /* Count and strip pointer wrappers: int* -> "intP", int** -> "intPP" */
    int ptr_depth = 0;
    while (a->code == N_POINTER) {
      ptr_depth++;
      a = NL_HEAD (a->u.ops); /* unwrap to the base type */
    }
    const char *arg_name;
    switch (a->code) {
    case N_STRING:   arg_name = "String"; break;
    case N_INT:      arg_name = "int"; break;
    case N_DOUBLE:   arg_name = "double"; break;
    case N_FLOAT:    arg_name = "float"; break;
    case N_CHAR:     arg_name = "char"; break;
    case N_LONG:     arg_name = "long"; break;
    case N_SHORT:    arg_name = "short"; break;
    case N_UNSIGNED: arg_name = "unsigned"; break;
    case N_VOID:     arg_name = "void"; break;
    case N_DICT:     arg_name = "dict"; break;
    case N_BOOL:     arg_name = "bool"; break;
    case N_ID:       arg_name = a->u.s.s; break;
    default:         arg_name = "T"; break;
    }
    add_to_temp_string (c2m_ctx, arg_name);
    for (int p = 0; p < ptr_depth; p++)
      add_to_temp_string (c2m_ctx, "P");
  }
  return uniq_cstr (c2m_ctx, VARR_ADDR (char, temp_string)).s;
}

/* Returns 1 for nodes that carry scalar data in u rather than children in u.ops. */
static int generic_node_has_scalar_data (node_code_t code) {
  switch (code) {
  case N_I: case N_L: case N_LL: case N_U: case N_UL: case N_ULL:
  case N_F: case N_D: case N_LD: case N_CH: case N_CH16: case N_CH32:
  case N_STR: case N_STR16: case N_STR32:
    return 1;
  default:
    return 0;
  }
}

/* True if `attrs` is an N_LIST of N_ATTR (or a single N_ATTR, or nested
   lists from merging multiple __attribute__((...)) specs) naming
   `da_ignore` / `classyc_da_ignore`.  Used by the parser (method/function
   trailing attrs) and by create_decl to set decl->da_ignore_p. */
static int attr_list_has_da_ignore (node_t attrs) {
  if (attrs == NULL) return 0;
  if (attrs->code == N_ATTR) {
    node_t aname = NL_HEAD (attrs->u.ops);
    return (aname != NULL && aname->code == N_ID && aname->u.s.s != NULL
            && (strcmp (aname->u.s.s, "da_ignore") == 0
                || strcmp (aname->u.s.s, "classyc_da_ignore") == 0));
  }
  if (attrs->code != N_LIST) return 0;
  for (node_t aa = NL_HEAD (attrs->u.ops); aa != NULL; aa = NL_NEXT (aa)) {
    if (aa->code == N_LIST) {
      if (attr_list_has_da_ignore (aa)) return 1;
      continue;
    }
    if (aa->code != N_ATTR) continue;
    node_t aname = NL_HEAD (aa->u.ops);
    if (aname != NULL && aname->code == N_ID && aname->u.s.s != NULL
        && (strcmp (aname->u.s.s, "da_ignore") == 0
            || strcmp (aname->u.s.s, "classyc_da_ignore") == 0))
      return 1;
  }
  return 0;
}

/* True if `attrs` names `copyable_no_release`: the user asserts the class
   is safe to store by value in List/Set/Map — bitwise copies may each run
   ~T, so the dtor must not release unique resources (e.g. counting/log-only).
   Same list shapes as attr_list_has_da_ignore. */
static int attr_list_has_copyable_no_release (node_t attrs) {
  if (attrs == NULL) return 0;
  if (attrs->code == N_ATTR) {
    node_t aname = NL_HEAD (attrs->u.ops);
    return (aname != NULL && aname->code == N_ID && aname->u.s.s != NULL
            && strcmp (aname->u.s.s, "copyable_no_release") == 0);
  }
  if (attrs->code != N_LIST) return 0;
  for (node_t aa = NL_HEAD (attrs->u.ops); aa != NULL; aa = NL_NEXT (aa)) {
    if (aa->code == N_LIST) {
      if (attr_list_has_copyable_no_release (aa)) return 1;
      continue;
    }
    if (aa->code != N_ATTR) continue;
    node_t aname = NL_HEAD (aa->u.ops);
    if (aname != NULL && aname->code == N_ID && aname->u.s.s != NULL
        && strcmp (aname->u.s.s, "copyable_no_release") == 0)
      return 1;
  }
  return 0;
}

/* ── type_kind core (parse-time AST + check-time type API) ─────────────── */

static const char *type_kind_name (type_kind_t k) {
  switch (k) {
  case TK_POD: return "POD";
  case TK_QUIET_VALUE: return "QUIET_VALUE";
  case TK_ARENA_VALUE: return "ARENA_VALUE";
  case TK_UNIQUE_RESOURCE: return "UNIQUE_RESOURCE";
  case TK_MOVE_ONLY: return "MOVE_ONLY";
  case TK_POINTER: return "POINTER";
  case TK_OPAQUE: return "OPAQUE";
  default: return "?";
  }
}

/* By-value List/Set/Map elements: only kinds that tolerate multi-owner
   bitwise copies (each live slot may run ~T). */
static int type_kind_ok_byvalue_element_p (type_kind_t k) {
  return k == TK_POD || k == TK_QUIET_VALUE || k == TK_ARENA_VALUE;
}

/* Ownership pass cares about heap / unique resources, not quiet DTOs. */
static int type_kind_tracks_resource_p (type_kind_t k) {
  return k == TK_UNIQUE_RESOURCE || k == TK_MOVE_ONLY || k == TK_POINTER;
}

/* Midopt / gen: may use bulk memcpy of element slots without deep clone. */
static int type_kind_bitwise_copy_safe_p (type_kind_t k) {
  return k == TK_POD || k == TK_QUIET_VALUE || k == TK_ARENA_VALUE || k == TK_POINTER;
}

static class_type_meta_t *class_type_meta_find_ctx (c2m_ctx_t c2m_ctx, node_t class_node) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  if (class_node == NULL || parse_ctx == NULL || class_type_metas == NULL) return NULL;
  for (size_t i = 0; i < VARR_LENGTH (class_type_meta_t, class_type_metas); i++) {
    class_type_meta_t *m = &VARR_ADDR (class_type_meta_t, class_type_metas)[i];
    if (m->class_node == class_node) return m;
  }
  return NULL;
}

static class_type_meta_t *class_type_meta_find_by_name (c2m_ctx_t c2m_ctx, const char *name) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  if (name == NULL || parse_ctx == NULL || class_type_metas == NULL) return NULL;
  for (size_t i = 0; i < VARR_LENGTH (class_type_meta_t, class_type_metas); i++) {
    class_type_meta_t *m = &VARR_ADDR (class_type_meta_t, class_type_metas)[i];
    node_t cid;
    if (m->class_node == NULL) continue;
    cid = TAG_ID (m->class_node);
    if (cid != NULL && cid->code == N_ID && cid->u.s.s != NULL
        && strcmp (cid->u.s.s, name) == 0)
      return m;
  }
  return NULL;
}

/* Walk dtor body: true if any call looks like a unique-resource release.
   Shallow structural walk of blocks/lists/calls only. */
static int ast_calls_unique_release_p (node_t n) {
  node_t ch;
  if (n == NULL) return 0;
  if (n->code == N_DELETE) return 1;
  if (n->code == N_CALL) {
    node_t callee = CALL_FUNC (n);
    node_t args;
    const char *fn;
    if (callee != NULL && callee->code == N_ID && callee->u.s.s != NULL) {
      fn = callee->u.s.s;
      if (strcmp (fn, "free") == 0 || strcmp (fn, "fclose") == 0
          || strcmp (fn, "pclose") == 0 || strcmp (fn, "closedir") == 0
          || strcmp (fn, "munmap") == 0 || strcmp (fn, "dlclose") == 0
          || strcmp (fn, "close") == 0)
        return 1;
    }
    args = CALL_ARGS (n);
    if (args != NULL && args->code == N_LIST)
      for (ch = NL_HEAD (args->u.ops); ch != NULL; ch = NL_NEXT (ch))
        if (ast_calls_unique_release_p (ch)) return 1;
    return 0;
  }
  if (n->code == N_FUNC_DEF)
    return ast_calls_unique_release_p (FUNC_DEF_BLOCK (n));
  if (n->code == N_BLOCK || n->code == N_LIST) {
    for (ch = NL_HEAD (n->u.ops); ch != NULL; ch = NL_NEXT (ch))
      if (ast_calls_unique_release_p (ch)) return 1;
    return 0;
  }
  /* Expression statements wrap the call: N_EXPR (attrs, expr) */
  if (n->code == N_EXPR || n->code == N_RETURN) {
    for (ch = NL_HEAD (n->u.ops); ch != NULL; ch = NL_NEXT (ch))
      if (ast_calls_unique_release_p (ch)) return 1;
  }
  return 0;
}

static int class_ast_find_dtor (node_t class_node, node_t *dtor_out) {
  node_t mlist, mem, mid;
  if (dtor_out) *dtor_out = NULL;
  if (class_node == NULL || class_node->code != N_CLASS) return 0;
  mlist = TAG_MEMBER_LIST (class_node);
  if (mlist == NULL || mlist->code != N_LIST) return 0;
  for (mem = NL_HEAD (mlist->u.ops); mem != NULL; mem = NL_NEXT (mem)) {
    if (mem->code != N_FUNC_DEF) continue;
    mid = DECL_ID (FUNC_DEF_DECL (mem));
    if (mid != NULL && mid->code == N_ID && mid->u.s.s != NULL
        && strncmp (mid->u.s.s, "__dtor_", 7) == 0) {
      if (dtor_out) *dtor_out = mem;
      return 1;
    }
  }
  return 0;
}

/* Declarator is a pointer field: N_DECL with N_POINTER in the decl list. */
static int declarator_has_pointer_p (node_t decl) {
  node_t dl, x;
  if (decl == NULL) return 0;
  if (decl->code == N_POINTER) return 1;
  if (decl->code != N_DECL) return 0;
  dl = NL_EL (decl->u.ops, 1); /* declarator list */
  if (dl == NULL) return 0;
  if (dl->code == N_POINTER) return 1;
  if (dl->code != N_LIST) return 0;
  for (x = NL_HEAD (dl->u.ops); x != NULL; x = NL_NEXT (x)) {
    if (x->code == N_POINTER) return 1;
    if (x->code == N_FUNC) return 0; /* method, not a data field */
  }
  return 0;
}

/* One-level scan of a type-spec list for String. */
static int specs_look_string_p (node_t specs) {
  node_t ch;
  if (specs == NULL) return 0;
  if (specs->code == N_SHARE) specs = NL_HEAD (specs->u.ops);
  if (specs == NULL) return 0;
  if (specs->code == N_STRING) return 1;
  if (specs->code == N_ID && specs->u.s.s != NULL
      && strcmp (specs->u.s.s, "String") == 0)
    return 1;
  if (specs->code != N_LIST) return 0;
  for (ch = NL_HEAD (specs->u.ops); ch != NULL; ch = NL_NEXT (ch)) {
    if (ch->code == N_STRING) return 1;
    if (ch->code == N_ID && ch->u.s.s != NULL && strcmp (ch->u.s.s, "String") == 0)
      return 1;
  }
  return 0;
}

/* One-level scan for nested UNIQUE/OPAQUE/MOVE_ONLY class type names. */
static int specs_name_unique_class_p (c2m_ctx_t c2m_ctx, node_t specs) {
  node_t ch;
  if (specs == NULL) return 0;
  if (specs->code == N_SHARE) specs = NL_HEAD (specs->u.ops);
  if (specs == NULL) return 0;
  if (specs->code == N_ID && specs->u.s.s != NULL) {
    class_type_meta_t *m = class_type_meta_find_by_name (c2m_ctx, specs->u.s.s);
    if (m != NULL && m->kind_valid_p
        && (m->kind == TK_UNIQUE_RESOURCE || m->kind == TK_OPAQUE
            || m->kind == TK_MOVE_ONLY))
      return 1;
    return 0;
  }
  if (specs->code != N_LIST) return 0;
  for (ch = NL_HEAD (specs->u.ops); ch != NULL; ch = NL_NEXT (ch)) {
    if (ch->code == N_ID && ch->u.s.s != NULL) {
      class_type_meta_t *m = class_type_meta_find_by_name (c2m_ctx, ch->u.s.s);
      if (m != NULL && m->kind_valid_p
          && (m->kind == TK_UNIQUE_RESOURCE || m->kind == TK_OPAQUE
              || m->kind == TK_MOVE_ONLY))
        return 1;
    }
  }
  return 0;
}

static void class_ast_scan_fields (c2m_ctx_t c2m_ctx, node_t class_node,
                                   int *has_raw_ptr_p, int *has_string_p,
                                   int *has_unique_nested_p) {
  node_t mlist, mem;
  *has_raw_ptr_p = *has_string_p = *has_unique_nested_p = 0;
  if (class_node == NULL) return;
  mlist = TAG_MEMBER_LIST (class_node);
  if (mlist == NULL || mlist->code != N_LIST) return;
  for (mem = NL_HEAD (mlist->u.ops); mem != NULL; mem = NL_NEXT (mem)) {
    node_t specs, declr;
    if (mem->code != N_MEMBER) continue;
    declr = MEMBER_DECL (mem);
    if (declr == NULL || declr->code == N_IGNORE) continue;
    /* Methods: N_DECL with N_FUNC in the declarator list — skip. */
    if (declr->code == N_DECL) {
      node_t dl = NL_EL (declr->u.ops, 1);
      int is_method = 0;
      if (dl != NULL && dl->code == N_LIST)
        for (node_t x = NL_HEAD (dl->u.ops); x != NULL; x = NL_NEXT (x))
          if (x->code == N_FUNC) { is_method = 1; break; }
      if (is_method) continue;
    }
    specs = MEMBER_SPECS (mem);
    if (declarator_has_pointer_p (declr) && !specs_look_string_p (specs))
      *has_raw_ptr_p = 1;
    if (specs_look_string_p (specs)) *has_string_p = 1;
    if (specs_name_unique_class_p (c2m_ctx, specs)) *has_unique_nested_p = 1;
  }
}

/* Classify a class from its AST (available at parse end of the class body). */
static type_kind_t class_compute_kind_from_ast (c2m_ctx_t c2m_ctx, node_t class_node,
                                                int copyable_no_release_p,
                                                int *has_dtor_out, int *dtor_rel_out) {
  node_t cid, dtor = NULL;
  const char *nm;
  int has_dtor, dtor_rel, has_raw_ptr, has_string, has_unique_nested;

  if (has_dtor_out) *has_dtor_out = 0;
  if (dtor_rel_out) *dtor_rel_out = 0;
  if (class_node == NULL || class_node->code != N_CLASS) return TK_OPAQUE;

  cid = TAG_ID (class_node);
  nm = (cid != NULL && cid->code == N_ID) ? cid->u.s.s : NULL;
  /* Monomorphized collection shells (and template mangles). */
  if (nm != NULL
      && (strncmp (nm, "__generic_List_", 15) == 0
          || strncmp (nm, "__generic_Map_", 14) == 0
          || strncmp (nm, "__generic_Set_", 14) == 0))
    return TK_MOVE_ONLY;

  /* Explicit waiver: multi-owner bitwise copy + multi-dtor is intentional. */
  if (copyable_no_release_p) {
    has_dtor = class_ast_find_dtor (class_node, &dtor);
    if (has_dtor_out) *has_dtor_out = has_dtor;
    if (dtor_rel_out) *dtor_rel_out = 0;
    return TK_QUIET_VALUE;
  }

  has_dtor = class_ast_find_dtor (class_node, &dtor);
  dtor_rel = (has_dtor && dtor != NULL && ast_calls_unique_release_p (dtor));
  if (has_dtor_out) *has_dtor_out = has_dtor;
  if (dtor_rel_out) *dtor_rel_out = dtor_rel;

  class_ast_scan_fields (c2m_ctx, class_node, &has_raw_ptr, &has_string,
                         &has_unique_nested);

  if (!has_dtor) {
    /* No dtor: double-free via ~T cannot happen. Nested UNIQUE values still
       poison the fold. Raw pointer fields without dtor are treated as borrowed
       (POD) so classic C DTOs with char* views stay List-friendly. */
    if (has_unique_nested) return TK_UNIQUE_RESOURCE;
    if (has_string && !has_raw_ptr) return TK_ARENA_VALUE;
    return TK_POD;
  }

  /* User dtor present. */
  if (dtor_rel || has_unique_nested) return TK_UNIQUE_RESOURCE;
  /* Raw pointer field + dtor: likely unique owner (Owns { char* p; ~ free }). */
  if (has_raw_ptr) return TK_UNIQUE_RESOURCE;
  /* Side-effect-only dtor (counting/log) + safe fields → auto QUIET.
     This is what lets test/counting classes avoid the attribute. */
  if (!dtor_rel) return TK_QUIET_VALUE;
  return TK_OPAQUE;
}

/* Register / update meta when a class is parsed. */
static void class_type_meta_register (c2m_ctx_t c2m_ctx, node_t class_node,
                                      int copyable_no_release_p) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  class_type_meta_t m, *existing;
  int has_dtor = 0, dtor_rel = 0;

  if (parse_ctx == NULL || class_node == NULL || class_node->code != N_CLASS) return;
  if (class_type_metas == NULL) return;

  existing = class_type_meta_find_ctx (c2m_ctx, class_node);
  memset (&m, 0, sizeof (m));
  m.class_node = class_node;
  m.copyable_no_release_p = copyable_no_release_p ? 1 : 0;
  m.kind = class_compute_kind_from_ast (c2m_ctx, class_node, copyable_no_release_p,
                                        &has_dtor, &dtor_rel);
  m.kind_valid_p = 1;
  m.has_user_dtor_p = has_dtor ? 1 : 0;
  m.dtor_releases_p = dtor_rel ? 1 : 0;
  if (existing != NULL) {
    *existing = m;
    return;
  }
  VARR_PUSH (class_type_meta_t, class_type_metas, m);
}

/* Human-facing fix-it for by-value element rejection. */
static const char *type_kind_byvalue_fixit (type_kind_t k) {
  switch (k) {
  case TK_UNIQUE_RESOURCE:
    return "this type's destructor releases unique resources; "
           "use List<T*>.owns() / Map with .ownsValues(), or redesign as a pure DTO";
  case TK_MOVE_ONLY:
    return "move-only collection shells cannot be stored as by-value elements of another "
           "collection that bitwise-copies them; nest via pointers or redesign";
  case TK_OPAQUE:
    return "cannot prove the destructor is free of unique-resource release; "
           "use List<T*>.owns(), make the type a DTO (no resource dtor), or mark "
           "[[copyable_no_release]] only if multi-destroy is intentional (counting/log-only)";
  case TK_POINTER:
    return "use pointer element types with .owns() when the list should delete pointees";
  default:
    return "use pointer elements with .owns()/.ownsValues(), or mark [[copyable_no_release]] "
           "if the destructor does not release unique resources";
  }
}

/* Pre-check marker stashed on N_FUNC_DEF->attr before create_decl runs.
   Template sentinel is (void*)(intptr_t)-1; this means the definition carried
   __attribute__((da_ignore)) and create_decl should set decl->da_ignore_p. */
#define PRECHECK_DA_IGNORE ((void *) (intptr_t) 2)

/* ── HTTP route attributes ───────────────────────────────────────────────
   ASP.NET / Spring-style C23 attributes on handler functions:

     [[HttpGet("/health")]]
     static Response* health(Request* req) { ... }

   Lowers to the same registry static that ROUTE("GET","/health",health)
   produces, so route_dispatch / __start_cyreg_routes keep working. */

/* If ATTRS contains HttpGet/Post/Put/Delete/Patch(path) or
   HttpRoute(method, path), write method/path and return 1. */
static int http_route_from_attrs (node_t attrs, const char **method_out,
                                  const char **path_out) {
  if (attrs == NULL) return 0;
  if (attrs->code == N_LIST) {
    for (node_t a = NL_HEAD (attrs->u.ops); a != NULL; a = NL_NEXT (a)) {
      if (a->code == N_LIST) {
        if (http_route_from_attrs (a, method_out, path_out)) return 1;
        continue;
      }
      if (a->code == N_ATTR && http_route_from_attrs (a, method_out, path_out))
        return 1;
    }
    return 0;
  }
  if (attrs->code != N_ATTR) return 0;
  node_t id = NL_HEAD (attrs->u.ops);
  if (id == NULL || id->code != N_ID || id->u.s.s == NULL) return 0;
  const char *name = id->u.s.s;
  node_t arglist = NL_NEXT (id);
  if (arglist == NULL || arglist->code != N_LIST) return 0;
  node_t a0 = NL_HEAD (arglist->u.ops);
  if (a0 == NULL || (a0->code != N_STR && a0->code != N_ID)) return 0;

  if (strcmp (name, "HttpGet") == 0) {
    *method_out = "GET"; *path_out = a0->u.s.s; return 1;
  }
  if (strcmp (name, "HttpPost") == 0) {
    *method_out = "POST"; *path_out = a0->u.s.s; return 1;
  }
  if (strcmp (name, "HttpPut") == 0) {
    *method_out = "PUT"; *path_out = a0->u.s.s; return 1;
  }
  if (strcmp (name, "HttpDelete") == 0) {
    *method_out = "DELETE"; *path_out = a0->u.s.s; return 1;
  }
  if (strcmp (name, "HttpPatch") == 0) {
    *method_out = "PATCH"; *path_out = a0->u.s.s; return 1;
  }
  if (strcmp (name, "HttpRoute") == 0) {
    node_t a1 = NL_NEXT (a0);
    if (a1 == NULL || (a1->code != N_STR && a1->code != N_ID)) return 0;
    *method_out = a0->u.s.s;
    *path_out = a1->u.s.s;
    return 1;
  }
  return 0;
}

/* Build the AST for:
     [[registry("routes")]] static RouteReg __cy_route_<fn>_<n>
       = { method, path, fn };
   Same shape the ROUTE() macro expands to. */
static node_t make_http_route_reg (c2m_ctx_t c2m_ctx, pos_t pos,
                                   const char *method, const char *path,
                                   const char *fn_name) {
  static int counter = 0;
  char vname[256];
  snprintf (vname, sizeof vname, "__cy_route_%s_%d",
            fn_name != NULL ? fn_name : "anon", counter++);

  node_t specs = new_node (c2m_ctx, N_LIST);
  op_append (c2m_ctx, specs, new_pos_node (c2m_ctx, N_STATIC, pos));
  op_append (c2m_ctx, specs, build_id (c2m_ctx, "RouteReg", pos));

  node_t decl = build_decl (c2m_ctx, pos, build_id (c2m_ctx, vname, pos), NULL);

  node_t reg_args = new_node (c2m_ctx, N_LIST);
  op_append (c2m_ctx, reg_args,
             new_str_node (c2m_ctx, N_STR, uniq_cstr (c2m_ctx, "routes"), pos));
  node_t reg_attr
    = new_node2 (c2m_ctx, N_ATTR, build_id (c2m_ctx, "registry", pos), reg_args);
  node_t attrs = new_node (c2m_ctx, N_LIST);
  op_append (c2m_ctx, attrs, reg_attr);

  node_t init_list = new_node (c2m_ctx, N_LIST);
  op_append (c2m_ctx, init_list,
             new_node2 (c2m_ctx, N_INIT, new_node (c2m_ctx, N_LIST),
                        new_str_node (c2m_ctx, N_STR, uniq_cstr (c2m_ctx, method),
                                      pos)));
  op_append (c2m_ctx, init_list,
             new_node2 (c2m_ctx, N_INIT, new_node (c2m_ctx, N_LIST),
                        new_str_node (c2m_ctx, N_STR, uniq_cstr (c2m_ctx, path),
                                      pos)));
  op_append (c2m_ctx, init_list,
             new_node2 (c2m_ctx, N_INIT, new_node (c2m_ctx, N_LIST),
                        build_id (c2m_ctx, fn_name, pos)));

  return build_shared_spec_decl (c2m_ctx, pos, specs, decl, attrs, NULL,
                                 init_list);
}

/* Deep-copy `n`, substituting type-parameter N_IDs with concrete type-arg nodes.
   Also renames `orig_name` -> `spec_name` and __ctor_/dtor_ accordingly. */
static node_t specialize_node (c2m_ctx_t c2m_ctx, node_t n,
                                const char *orig_name, const char *spec_name,
                                int n_params, const char **params, node_t *args) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  if (n == NULL) return NULL;

  if (n->code == N_ID) {
    const char *s = n->u.s.s;
    /* Substitute type parameter */
    for (int i = 0; i < n_params; i++) {
      if (strcmp (s, params[i]) == 0) {
        node_t a = args[i];
        /* For pointer type args (N_POINTER wrapping a base type), unwrap to
           the base type for the specifier substitution.  The pointer level(s)
           are accounted for by _spec_add_ptr_to_decl below. */
        while (a->code == N_POINTER) a = NL_HEAD (a->u.ops);
        node_t fresh = new_node (c2m_ctx, a->code);
        set_node_pos (c2m_ctx, fresh, POS (n));
        if (a->code == N_ID) fresh->u.s = a->u.s; /* copy the identifier string */
        return fresh;
      }
    }
    /* Self-referential generic placeholder: __generic_<orig>_<P0>[_<P1>...] e.g.
       __generic_List_T inside List<T>'s body, or __generic_Map_K_V inside
       Map<K,V>'s body, gets stored as-is in the template AST.  When the template
       is specialised (T -> String, or K -> String, V -> int), resolve each
       embedded type-parameter name to its concrete type argument and re-mangle so
       it becomes the concrete spec name (__generic_List_String /
       __generic_Map_String_int).  Works for any parameter count; assumes
       type-parameter names contain no '_' (so the joined names tokenize cleanly).

       Also handles CROSS-references to other generic classes: e.g. `Is<T>.Of(h)`
       inside `As<T>`'s body parses as `__generic_Is_T.Of(h)`.  When `As<Circle>`
       is specialised, `__generic_Is_T` must resolve to `__generic_Is_Circle`.
       We scan every registered generic template (not just orig_name) for a
       matching prefix, then resolve the embedded params the same way.
       Self-references (orig_name) just resolve the name; cross-references
       (other templates) are recorded in generic_crossrefs for deferred
       materialization by get_or_create_specialization. */
    if (n_params > 0 && strncmp (s, "__generic_", 10) == 0) {
      VARR (generic_tmpl_t) *_gt = generic_templates;
      if (_gt != NULL) {
        /* Resolve one mangled type-arg segment at *_pcur (advance *_pcur).
           Handles plain params ("T", "VP") AND nested placeholders
           ("__generic_List_VP") so Map<G, List<V>*>* rewrites correctly:
             __generic_Map_G___generic_List_VP  ->  __generic_Map_int___generic_List_intP */
        node_t (*_resolve_seg) (c2m_ctx_t, const char **, int, const char **, node_t *,
                                 pos_t) = NULL;
        /* Local recursive lambda-via-goto-style helper as nested function not in C:
           use a iterative stack-free approach inline per known template arity. */
        for (size_t _ti = 0; _ti < VARR_LENGTH (generic_tmpl_t, _gt); _ti++) {
          generic_tmpl_t *_t = &VARR_ADDR (generic_tmpl_t, _gt)[_ti];
          char _pfx[512];
          snprintf (_pfx, sizeof (_pfx), "__generic_%s_", _t->name);
          size_t _plen = strlen (_pfx);
          if (strncmp (s, _pfx, _plen) != 0) continue;

          /* Parse exactly _t->n_type_params mangled args from the suffix. */
          node_t _resolved[4];
          int _nr = 0, _ok = 1;
          const char *_cur = s + _plen;
          while (_nr < _t->n_type_params && *_cur != '\0' && _ok) {
            node_t _res = NULL;
            int _depth = 0;

            /* Nested generic placeholder as a single type arg. */
            if (strncmp (_cur, "__generic_", 10) == 0) {
              int _found_nested = 0;
              for (size_t _ni = 0; _ni < VARR_LENGTH (generic_tmpl_t, _gt); _ni++) {
                generic_tmpl_t *_nt = &VARR_ADDR (generic_tmpl_t, _gt)[_ni];
                char _npfx[512];
                snprintf (_npfx, sizeof (_npfx), "__generic_%s_", _nt->name);
                size_t _nplen = strlen (_npfx);
                if (strncmp (_cur, _npfx, _nplen) != 0) continue;
                /* Parse nested args (simple segments only — one level of nesting is
                   the common GroupBy / Select case). */
                node_t _nres[4];
                int _nn = 0, _nok = 1;
                const char *_ncur = _cur + _nplen;
                while (_nn < _nt->n_type_params && *_ncur != '\0' && _nok) {
                  /* Nested type-args are simple segments.  Trailing 'P' on a
                     segment that matches a type-param name are OUTER pointers on
                     the nested generic (List_V* mangles as List_VP), not pointer
                     type-args to List — GroupBy returns Map<G, List<V>*>. */
                  if (strncmp (_ncur, "__generic_", 10) == 0) { _nok = 0; break; }
                  const char *_nus = strchr (_ncur, '_');
                  size_t _nlen = _nus != NULL ? (size_t) (_nus - _ncur) : strlen (_ncur);
                  /* Prefer matching a type-param as a prefix of the segment;
                     leftover trailing 'P's become outer pointer depth on the
                     nested type as a whole (after all type-args are parsed). */
                  int _npi = -1;
                  size_t _nbase = 0;
                  int _nouter_p = 0;
                  for (int _i = 0; _i < n_params; _i++) {
                    if (params[_i] == NULL) continue;
                    size_t _plen = strlen (params[_i]);
                    if (_plen == 0 || _plen > _nlen) continue;
                    if (strncmp (_ncur, params[_i], _plen) != 0) continue;
                    /* Rest of segment must be only 'P's (or empty). */
                    int _rest_ok = 1;
                    for (size_t _k = _plen; _k < _nlen; _k++)
                      if (_ncur[_k] != 'P') { _rest_ok = 0; break; }
                    if (!_rest_ok) continue;
                    _npi = _i;
                    _nbase = _plen;
                    _nouter_p = (int) (_nlen - _plen);
                    break;
                  }
                  if (_npi >= 0) {
                    _nres[_nn++] = args[_npi];
                    /* Only the LAST type-arg's trailing P's count as outer on
                       the nested generic (e.g. List_VP).  Intermediate args keep
                       them as pointer typeargs if ever multi-arg nested. */
                    if (_nn == _nt->n_type_params) _depth += _nouter_p;
                    else {
                      node_t _r = args[_npi];
                      for (int _d = 0; _d < _nouter_p; _d++)
                        _r = new_pos_node1 (c2m_ctx, N_POINTER, POS (n), _r);
                      _nres[_nn - 1] = _r;
                    }
                  } else {
                    /* Concrete segment (int/String/…); peel trailing P's as ptr. */
                    size_t _cbase = _nlen;
                    int _cd = 0;
                    while (_cbase > 0 && _ncur[_cbase - 1] == 'P') { _cd++; _cbase--; }
                    char _seg[256];
                    if (_cbase == 0 || _cbase >= sizeof (_seg)) { _nok = 0; break; }
                    memcpy (_seg, _ncur, _cbase); _seg[_cbase] = '\0';
                    node_t _id = build_id (c2m_ctx, _seg, POS (n));
                    for (int _d = 0; _d < _cd; _d++)
                      _id = new_pos_node1 (c2m_ctx, N_POINTER, POS (n), _id);
                    _nres[_nn++] = _id;
                  }
                  _ncur = _nus != NULL ? _nus + 1 : _ncur + _nlen;
                }
                if (!_nok || _nn != _nt->n_type_params) continue;
                /* Any further bare 'P' after nested name. */
                while (*_ncur == 'P') { _depth++; _ncur++; }
                const char *_nested_name
                  = mangle_generic_name (c2m_ctx, _nt->name, _nn, _nres);
                /* Materialize nested class (List_int). */
                if (strcmp (_nt->name, orig_name) != 0) {
                  generic_crossref_t _ncr;
                  _ncr.ref_name = _nt->name;
                  _ncr.n_args = _nn;
                  for (int _ci = 0; _ci < _nn; _ci++) _ncr.args[_ci] = _nres[_ci];
                  _ncr.pos = POS (n);
                  VARR_PUSH (generic_crossref_t, generic_crossrefs, _ncr);
                }
                /* Rebuild as pointer-to-nested when mangled as List_VP. */
                _res = build_id (c2m_ctx, _nested_name, POS (n));
                for (int _d = 0; _d < _depth; _d++)
                  _res = new_pos_node1 (c2m_ctx, N_POINTER, POS (n), _res);
                _cur = _ncur;
                if (*_cur == '_') _cur++;
                _found_nested = 1;
                break;
              }
              if (!_found_nested) { _ok = 0; break; }
            } else {
              /* Simple segment: type-param name + optional trailing P's. */
              const char *_us = strchr (_cur, '_');
              size_t _len = _us != NULL ? (size_t) (_us - _cur) : strlen (_cur);
              size_t _base_len = _len;
              _depth = 0;
              while (_base_len > 0 && _cur[_base_len - 1] == 'P') { _depth++; _base_len--; }
              int _pi = -1;
              for (int _i = 0; _i < n_params; _i++)
                if (params[_i] != NULL && strlen (params[_i]) == _base_len
                    && strncmp (_cur, params[_i], _base_len) == 0) { _pi = _i; break; }
              if (_pi < 0) {
                /* Already-concrete (int/String/...) — rebuild as ID (+ pointers). */
                char _seg[256];
                if (_base_len == 0 || _base_len >= sizeof (_seg)) { _ok = 0; break; }
                memcpy (_seg, _cur, _base_len); _seg[_base_len] = '\0';
                _res = build_id (c2m_ctx, _seg, POS (n));
                for (int _d = 0; _d < _depth; _d++)
                  _res = new_pos_node1 (c2m_ctx, N_POINTER, POS (n), _res);
              } else {
                _res = args[_pi];
                for (int _d = 0; _d < _depth; _d++)
                  _res = new_pos_node1 (c2m_ctx, N_POINTER, POS (n), _res);
              }
              _cur = _us != NULL ? _us + 1 : _cur + _len;
            }
            if (_res == NULL || _nr >= 4) { _ok = 0; break; }
            _resolved[_nr++] = _res;
          }
          if (_ok && _nr == _t->n_type_params) {
            const char *_new = mangle_generic_name (c2m_ctx, _t->name, _nr, _resolved);
            if (strcmp (_t->name, orig_name) != 0) {
              /* Cross-reference to a different generic class: record for
                 deferred materialization (avoids recursive
                 get_or_create_specialization that corrupts state). */
              generic_crossref_t _cr;
              _cr.ref_name = _t->name;
              _cr.n_args = _nr;
              for (int _ci = 0; _ci < _nr; _ci++) _cr.args[_ci] = _resolved[_ci];
              _cr.pos = POS (n);
              VARR_PUSH (generic_crossref_t, generic_crossrefs, _cr);
            }
            return build_id (c2m_ctx, _new, POS (n));
          }
          /* Prefix matched but arity/args failed — try next template with same
             prefix length rare; continue scan. */
          (void)_resolve_seg;
        }
      }
    }
    /* Rename __ctor_Orig -> __ctor_Spec */
    char buf[512], nbuf[512];
    snprintf (buf, sizeof (buf), "__ctor_%s", orig_name);
    if (strcmp (s, buf) == 0) {
      snprintf (nbuf, sizeof (nbuf), "__ctor_%s", spec_name);
      return build_id (c2m_ctx, nbuf, POS (n));
    }
    snprintf (buf, sizeof (buf), "__dtor_%s", orig_name);
    if (strcmp (s, buf) == 0) {
      snprintf (nbuf, sizeof (nbuf), "__dtor_%s", spec_name);
      return build_id (c2m_ctx, nbuf, POS (n));
    }
    /* Rename the class name itself */
    if (strcmp (s, orig_name) == 0)
      return build_id (c2m_ctx, spec_name, POS (n));
    /* Any other identifier: copy as-is */
    return build_id (c2m_ctx, s, POS (n));
  }

  /* is_pointer<T>() intrinsic: substitute the type parameter PRESERVING pointer
     wrapping (unlike the declarator path above, which unwraps N_POINTER and
     re-adds the level elsewhere).  is_pointer needs the full type to decide
     whether T resolved to a pointer, so we copy the matching arg verbatim. */
  if (n->code == N_CALL) {
    node_t callee = NL_HEAD (n->u.ops);
    if (callee != NULL && callee->code == N_ID && callee->u.s.s != NULL
        && (strcmp (callee->u.s.s, "is_pointer") == 0
            || strcmp (callee->u.s.s, "is_move_only") == 0)) {
      node_t tlist = NL_NEXT (callee);
      node_t targ = (tlist != NULL) ? NL_HEAD (tlist->u.ops) : NULL;
      if (targ != NULL && targ->code == N_ID) {
        for (int i = 0; i < n_params; i++) {
          if (params[i] != NULL && strcmp (targ->u.s.s, params[i]) == 0) {
            /* Deep-copy the full type arg (keeps N_POINTER wrapping intact). */
            node_t arg_copy = copy_node (c2m_ctx, args[i]);
            node_t new_tlist = new_node1 (c2m_ctx, N_LIST, arg_copy);
            node_t new_alist = new_node (c2m_ctx, N_LIST);
            node_t new_id = build_id (c2m_ctx, callee->u.s.s, POS (callee));
            return new_pos_node3 (c2m_ctx, N_CALL, POS (n), new_id, new_tlist, new_alist);
          }
        }
      }
    }
    /* nameof<T>() / typeof<T>() intrinsics: substitute the whole call with an
       N_STR of the concrete type-arg name.  nameof strips pointer wrappers
       (nameof<int*>() == "int"); typeof keeps them (typeof<int*>() == "int*").
       Used for RTTI (Is<T>/As<T>) and JSON type-dispatch in List/Map. */
    if (callee != NULL && callee->code == N_ID && callee->u.s.s != NULL
        && (strcmp (callee->u.s.s, "nameof") == 0
            || strcmp (callee->u.s.s, "typeof") == 0)) {
      int keep_ptr = (strcmp (callee->u.s.s, "typeof") == 0);
      node_t tlist = NL_NEXT (callee);
      node_t targ = (tlist != NULL) ? NL_HEAD (tlist->u.ops) : NULL;
      if (targ != NULL && targ->code == N_ID) {
        for (int i = 0; i < n_params; i++) {
          if (params[i] != NULL && strcmp (targ->u.s.s, params[i]) == 0) {
            const char *nm = type_arg_reflection_name (c2m_ctx, args[i], keep_ptr);
            return new_str_node (c2m_ctx, N_STR, uniq_cstr (c2m_ctx, nm), POS (n));
          }
        }
      }
    }
  }

  /* Allocate a new node of the same kind */
  node_t cp = new_node (c2m_ctx, n->code);
  set_node_pos (c2m_ctx, cp, POS (n));
  /* Preserve pre-check da_ignore marker on method definitions so monomorphized
     List/Map methods keep __attribute__((da_ignore)).  Template sentinel (-1)
     is NOT copied (specializations are real functions). */
  if (n->code == N_FUNC_DEF && n->attr == PRECHECK_DA_IGNORE)
    cp->attr = PRECHECK_DA_IGNORE;

  if (generic_node_has_scalar_data (n->code)) {
    /* Literal value node: copy the scalar union member */
    cp->u = n->u;
  } else {
    /* Structural node: recursively specialize children */
    for (node_t child = NL_HEAD (n->u.ops); child != NULL; child = NL_NEXT (child)) {
      node_t child_cp = specialize_node (c2m_ctx, child, orig_name, spec_name,
                                          n_params, params, args);
      if (child_cp != NULL) op_append (c2m_ctx, cp, child_cp);
    }
  }
  /* ── Pointer type argument fixup ────────────────────────────────────────
     When a type parameter T has a pointer type argument (e.g. T=char*), the
     substitution above placed just the base type (char) into the type
     specifier list.  We now need to inject N_POINTER nodes into the
     declarator's decoration list so that `T data` becomes `char *data` and
     `T* data` becomes `char **data`.  This applies to N_MEMBER (class
     fields), N_SPEC_DECL (named parameters, locals), N_FUNC_DEF (return
     types), and N_TYPE (abstract declarators: cast/sizeof type-names and the
     unnamed parameters of function-pointer types such as `int(*cmp)(T,T)`).
     Without the N_TYPE case, `T` used as a function-pointer parameter, in a
     `(T*)` cast, or in `sizeof(T)` would lose the pointer level and resolve to
     a by-value `Doc` instead of `Doc *`.  We scan the type-specifier list to
     see which type param was used (matched by node code against the base-type
     arg) and check the original arg for pointer depth. */
  if ((cp->code == N_MEMBER || cp->code == N_SPEC_DECL || cp->code == N_FUNC_DEF
       || cp->code == N_TYPE)
      && n_params > 0) {
    /* Determine the spec list and the declarator in both original and copy. */
    node_t orig_specs = NL_HEAD (n->u.ops);      /* original spec list */
    node_t cp_decl = NL_NEXT (NL_HEAD (cp->u.ops)); /* copied declarator */
    /* Unwrap N_SHARE to find the real spec list (e.g. `T* data;` shares
       specifiers across multiple members via SHARE nodes). */
    if (orig_specs != NULL && orig_specs->code == N_SHARE)
      orig_specs = NL_HEAD (orig_specs->u.ops);
    if (orig_specs != NULL && orig_specs->code == N_LIST
        && cp_decl != NULL && cp_decl->code == N_DECL) {
      /* Check if any child of the original spec list was a type-param N_ID. */
      for (node_t os = NL_HEAD (orig_specs->u.ops); os != NULL; os = NL_NEXT (os)) {
        if (os->code != N_ID) continue;
        for (int pi = 0; pi < n_params; pi++) {
          if (params[pi] == NULL || strcmp (os->u.s.s, params[pi]) != 0) continue;
          /* Count pointer depth in the original arg. */
          node_t aa = args[pi];
          int pd = 0;
          while (aa->code == N_POINTER) { pd++; aa = NL_HEAD (aa->u.ops); }
          if (pd == 0) break; /* not a pointer type arg — nothing to fix up */
          /* Inject pd pointer levels into the declarator's decoration list.
             Skip FUNC_DEF nodes whose specs are just the return type — the
             pointer must go AFTER any existing N_FUNC in the list (matching
             how the parser places pointer return types). */
          node_t decl_list = DECL_LIST (cp_decl);
          if (decl_list != NULL && decl_list->code == N_LIST) {
            for (int pp = 0; pp < pd; pp++)
              op_append (c2m_ctx, decl_list,
                         new_pos_node1 (c2m_ctx, N_POINTER, POS (n),
                                        new_node (c2m_ctx, N_LIST)));
          }
          break;
        }
      }
    }
  }
  return cp;
}

static node_t parse_generic_instantiation (c2m_ctx_t c2m_ctx, const char *base_name, pos_t pos);

/* Parse one generic type argument after '<': a primitive keyword or an N_ID class name. */
static node_t parse_generic_type_arg (c2m_ctx_t c2m_ctx) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  node_t r;
  pos_t pos = curr_token->pos;
  node_t base = NULL;

  if (MP (T_STRING,   pos)) base = new_pos_node (c2m_ctx, N_STRING, pos);
  else if (MP (T_INT,      pos)) base = new_pos_node (c2m_ctx, N_INT,    pos);
  else if (MP (T_DOUBLE,   pos)) base = new_pos_node (c2m_ctx, N_DOUBLE, pos);
  else if (MP (T_FLOAT,    pos)) base = new_pos_node (c2m_ctx, N_FLOAT,  pos);
  else if (MP (T_CHAR,     pos)) base = new_pos_node (c2m_ctx, N_CHAR,   pos);
  else if (MP (T_LONG,     pos)) base = new_pos_node (c2m_ctx, N_LONG,   pos);
  else if (MP (T_SHORT,    pos)) base = new_pos_node (c2m_ctx, N_SHORT,  pos);
  else if (MP (T_UNSIGNED, pos)) base = new_pos_node (c2m_ctx, N_UNSIGNED, pos);
  else if (MP (T_VOID,     pos)) base = new_pos_node (c2m_ctx, N_VOID,   pos);
  else if (MP (T_DICT,     pos)) base = new_pos_node (c2m_ctx, N_DICT,   pos);
  else if (MP (T_BOOL,     pos)) base = new_pos_node (c2m_ctx, N_BOOL,   pos);
  else if (MN (T_ID, r)) {
    if (strcmp (r->u.s.s, "Any") == 0 && C (T_CMP) && curr_token->node_code == N_LT) {
      base = parse_any_instantiation (c2m_ctx, POS (r));
    } else if (C (T_CMP) && curr_token->node_code == N_LT && is_generic_class_p(c2m_ctx, r->u.s.s)) {
      base = parse_generic_instantiation(c2m_ctx, r->u.s.s, POS(r));
    } else {
      base = r;
    }
  }
  if (base == NULL) return NULL;
  while (MP ('*', pos)) {
    base = new_pos_node1 (c2m_ctx, N_POINTER, pos, base);
  }
  return base;
}

/* Get or create the specialization of `base_name` with the given type args.
   Pushes the specialized N_CLASS onto pending_lambdas if it's new.
   Returns an N_ID for the mangled class name. */
static node_t get_or_create_specialization (c2m_ctx_t c2m_ctx,
                                             const char *base_name,
                                             int n_args, node_t *args,
                                             pos_t pos) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;

  /* Nested / self / cross references while parsing a generic class body.

     While curr_class is a generic being defined, type arguments may still be
     that class's type parameters (T, K, V, T*, ...).  Materialising those as
     real specialisations would leave unresolved type names (unknown type K)
     and corrupt later codegen.  Instead:

       1. Param-based args (self or cross):
            List<T> / List<T*> inside List, Is<T> inside As, List<K> inside Map
          -> return mangled placeholder ("__generic_List_T", "__generic_List_K",
             "__generic_List_TP", ...).  specialize_node rewrites the placeholder
             to the concrete name when the OUTER template is instantiated, and
             records cross-generic materialisations in generic_crossrefs.

       2. Fully concrete args (no outer type params):
            List<String> inside List<T>'s SelectString, etc.
          -> also return the mangled name as a type-name placeholder, and queue
             the concrete specialisation on generic_crossrefs.  The caller drains
             that queue once the outer template's class_node is back-filled (the
             self-template's body is still NULL mid-parse, so materialisation has
             to wait). */
  if (parse_ctx != NULL && parse_ctx->curr_class != NULL && n_args > 0) {
    /* Find the current class's template entry to get its type params.  During
       body parsing the template's class_node may still be NULL (pre-registered),
       so match by name from the curr_class node's id. */
    VARR (generic_tmpl_t) *gt = generic_templates;
    const char *curr_cls_name = NULL;
    int curr_n_params = 0;
    const char **curr_params = NULL;
    node_t _cc = parse_ctx->curr_class;
    const char *_cname = (_cc != NULL && _cc->code == N_ID) ? _cc->u.s.s : NULL;
    for (size_t _si = 0; gt != NULL && _cname != NULL
         && _si < VARR_LENGTH (generic_tmpl_t, gt); _si++) {
      generic_tmpl_t *_t = VARR_ADDR (generic_tmpl_t, gt) + _si;
      if (strcmp (_t->name, _cname) == 0) {
        curr_cls_name = _t->name;
        curr_n_params = _t->n_type_params;
        curr_params = _t->type_params;
        break;
      }
    }
    /* Abstract type args: class type params and/or currently open method type
       params (Select<U> body with List<U>). */
    if ((curr_cls_name != NULL && curr_n_params > 0)
        || n_method_type_params > 0) {
      int _any_curr_param = 0;
      for (int _j = 0; _j < n_args; _j++) {
        node_t _pa = args[_j];
        while (_pa != NULL && _pa->code == N_POINTER) _pa = NL_HEAD (_pa->u.ops);
        if (_pa == NULL || _pa->code != N_ID) continue;
        for (int _k = 0; _k < curr_n_params; _k++) {
          if (curr_params[_k] != NULL && strcmp (_pa->u.s.s, curr_params[_k]) == 0) {
            _any_curr_param = 1;
            break;
          }
        }
        /* Also treat open method type params (U in Select<U>) as abstract so
           List<U> inside a method template does not materialize a real class. */
        if (!_any_curr_param && n_method_type_params > 0) {
          for (int _k = 0; _k < n_method_type_params; _k++) {
            if (method_type_params[_k] != NULL
                && strcmp (_pa->u.s.s, method_type_params[_k]) == 0) {
              _any_curr_param = 1;
              break;
            }
          }
        }
        if (_any_curr_param) break;
      }
      if (_any_curr_param) {
        /* Self-ref or cross-ref with unresolved outer type params — placeholder only. */
        const char *mangled = mangle_generic_name (c2m_ctx, base_name, n_args, args);
        return build_id (c2m_ctx, mangled, pos);
      }
      /* Fully concrete nested instantiation while inside a generic body
         (e.g. List<String> inside List<T>).  Defer real specialisation until
         the outer template body is complete. */
      {
        const char *mangled = mangle_generic_name (c2m_ctx, base_name, n_args, args);
        node_t ph_id = build_id (c2m_ctx, mangled, pos);
        tpname_add (c2m_ctx, ph_id, top_scope != NULL ? top_scope : curr_scope, TRUE);
        generic_crossref_t cr;
        cr.ref_name = base_name;
        cr.n_args = n_args;
        for (int _ci = 0; _ci < n_args && _ci < 4; _ci++) cr.args[_ci] = args[_ci];
        cr.pos = pos;
        VARR_PUSH (generic_crossref_t, generic_crossrefs, cr);
        return ph_id;
      }
    }
  }

  /* Guard: single-letter uppercase type-arg IDs (T, U, K, V, ...) that are not
     registered generic class templates are open type parameters — e.g. List<U>
     in the return type of Select<U>(...) before method type params are opened.
     Never materialize List_U as a real class. */
  {
    int _any_open = 0;
    for (int _j = 0; _j < n_args; _j++) {
      node_t _pa = args[_j];
      while (_pa != NULL && _pa->code == N_POINTER) _pa = NL_HEAD (_pa->u.ops);
      if (_pa == NULL || _pa->code != N_ID) continue;
      const char *nm = _pa->u.s.s;
      if (nm == NULL || nm[0] == '\0') continue;
      if (strncmp (nm, "__generic_", 10) == 0) continue;
      if (is_generic_class_p (c2m_ctx, nm)) continue;
      /* Active method type param? */
      for (int _k = 0; _k < n_method_type_params; _k++)
        if (method_type_params[_k] && strcmp (nm, method_type_params[_k]) == 0)
          { _any_open = 1; break; }
      if (_any_open) break;
      /* Single uppercase letter type-param convention (T, U, K, V, ...). */
      if (nm[0] >= 'A' && nm[0] <= 'Z' && nm[1] == '\0') { _any_open = 1; break; }
    }
    if (_any_open) {
      const char *mangled = mangle_generic_name (c2m_ctx, base_name, n_args, args);
      return build_id (c2m_ctx, mangled, pos);
    }
  }

  generic_tmpl_t *tmpl = get_generic_template (c2m_ctx, base_name);
  if (tmpl == NULL) {
    error (c2m_ctx, pos, "'%s' is not a generic class", base_name);
    return build_id (c2m_ctx, base_name, pos);
  }
  if (n_args != tmpl->n_type_params) {
    error (c2m_ctx, pos,
           "generic class '%s' expects %d type argument(s), got %d",
           base_name, tmpl->n_type_params, n_args);
    return build_id (c2m_ctx, base_name, pos);
  }
  /* Incomplete template: either still only `class Name<T>;`, or mid-parse
     body.  Do not error — return a mangled placeholder and queue the concrete
     args so the specialization is created once the body is registered.
     Needed for List.View() → ListView while ListView is still forward-decl'd
     (List_String monomorphization at end of List body drains before ListView's
     completing definition is seen). */
  if (tmpl->class_node == NULL) {
    const char *ph = mangle_generic_name (c2m_ctx, base_name, n_args, args);
    node_t ph_id = build_id (c2m_ctx, ph, pos);
    tpname_add (c2m_ctx, ph_id, top_scope != NULL ? top_scope : curr_scope, TRUE);
    {
      int dup = 0;
      for (size_t di = 0; generic_deferred_specs != NULL
           && di < VARR_LENGTH (generic_deferred_spec_t, generic_deferred_specs); di++) {
        generic_deferred_spec_t *d
          = &VARR_ADDR (generic_deferred_spec_t, generic_deferred_specs)[di];
        if (strcmp (d->base_name, base_name) != 0 || d->n_args != n_args) continue;
        /* Dedup by mangled name of args (cheap: same n_args + same base). */
        const char *dm = mangle_generic_name (c2m_ctx, d->base_name, d->n_args, d->args);
        if (strcmp (dm, ph) == 0) { dup = 1; break; }
      }
      if (!dup && generic_deferred_specs != NULL) {
        generic_deferred_spec_t ds;
        ds.base_name = base_name;
        ds.n_args = n_args;
        for (int ai = 0; ai < 4; ai++) ds.args[ai] = (ai < n_args) ? args[ai] : NULL;
        ds.pos = pos;
        VARR_PUSH (generic_deferred_spec_t, generic_deferred_specs, ds);
      }
    }
    return ph_id;
  }

  const char *spec_name = mangle_generic_name (c2m_ctx, base_name, n_args, args);

  /* Check cache: already created? */
  for (size_t i = 0; i < VARR_LENGTH (generic_spec_t, generic_specs); i++) {
    if (strcmp (VARR_GET (generic_spec_t, generic_specs, i).spec_name, spec_name) == 0)
      return build_id (c2m_ctx, spec_name, pos);
  }
  if (generic_in_progress != NULL) {
    for (size_t i = 0; i < VARR_LENGTH (cstr_t, generic_in_progress); i++) {
      cstr_t ip = VARR_GET (cstr_t, generic_in_progress, i);
      if (ip && strcmp(ip, spec_name)==0) return build_id(c2m_ctx, spec_name, pos);
    }
  }
  /* P1 by-value element gate (type_kind): List/Set/Map store class elements
     BY VALUE and bitwise-copy them (Add/Sort/Insert/Where/Copy can alias two
     slots).  Only POD / QUIET_VALUE / ARENA_VALUE are safe.  Pointer elements
     and nested __generic_* collections are exempt (separate owns() path). */
  if (strcmp (base_name, "List") == 0 || strcmp (base_name, "Set") == 0
      || strcmp (base_name, "Map") == 0) {
    int n_check = strcmp (base_name, "Map") == 0 ? 2 : 1;
    for (int ai = 0; ai < n_check && ai < n_args; ai++) {
      node_t ta = args[ai];
      class_type_meta_t *meta = NULL;
      type_kind_t ek;
      int is_ptr = 0;
      while (ta != NULL && ta->code == N_POINTER) {
        is_ptr = 1;
        ta = NL_HEAD (ta->u.ops);
      }
      if (is_ptr || ta == NULL || ta->code != N_ID || ta->u.s.s == NULL) continue;
      if (strncmp (ta->u.s.s, "__generic_", 10) == 0) continue; /* nested collection */
      /* Prefer the last meta for this name (definition over forward decl). */
      if (class_type_metas != NULL) {
        for (size_t pi = 0; pi < VARR_LENGTH (class_type_meta_t, class_type_metas); pi++) {
          class_type_meta_t *m
            = &VARR_ADDR (class_type_meta_t, class_type_metas)[pi];
          node_t cid = m->class_node != NULL ? TAG_ID (m->class_node) : NULL;
          if (cid != NULL && cid->code == N_ID && cid->u.s.s != NULL
              && strcmp (cid->u.s.s, ta->u.s.s) == 0 && m->kind_valid_p)
            meta = m;
        }
      }
      if (meta == NULL) continue; /* unknown / incomplete — allow; check may re-gate */
      ek = meta->kind;
      if (type_kind_ok_byvalue_element_p (ek)) continue;
      error (c2m_ctx, pos,
             "type '%s' is %s and cannot be a by-value %s element; "
             "%s stores elements by bitwise copy (Add/Sort/Where/Copy), so each "
             "copy would run ~%s.  %s",
             ta->u.s.s, type_kind_name (ek), base_name, base_name, ta->u.s.s,
             type_kind_byvalue_fixit (ek));
    }
  }

  VARR_PUSH (cstr_t, generic_in_progress, spec_name);

  /* Collect local/nested class type args for later hoist into top_scope
     (applied at N_MODULE check start -- see apply_local_type_hoists). */
  if (local_type_hoists != NULL && curr_scope != NULL) {
    for (int _ai = 0; _ai < n_args; _ai++) {
      node_t _ta = args[_ai];
      symbol_t _sym;
      node_t _cdef = NULL;
      while (_ta != NULL && _ta->code == N_POINTER) _ta = NL_HEAD (_ta->u.ops);
      if (_ta == NULL || _ta->code != N_ID || _ta->u.s.s == NULL) continue;
      if (strncmp (_ta->u.s.s, "__generic_", 10) == 0) continue;
      /* Nested class tags are pre-inserted into the parse-time curr_scope
         (the enclosing block).  Only look there -- parent walk needs
         struct node_scope which is not yet defined at this point in the file. */
      if (symbol_find (c2m_ctx, S_REGULARS, _ta, curr_scope, &_sym)
          && _sym.def_node != NULL && _sym.def_node->code == N_CLASS)
        _cdef = _sym.def_node;
      else if (symbol_find (c2m_ctx, S_TAG, _ta, curr_scope, &_sym)
               && _sym.def_node != NULL && _sym.def_node->code == N_CLASS)
        _cdef = _sym.def_node;
      if (_cdef == NULL) continue;
      /* Dedup. */
      {
        int _dup = 0;
        for (size_t _hi = 0; _hi < VARR_LENGTH (local_type_hoist_t, local_type_hoists); _hi++) {
          local_type_hoist_t _h = VARR_GET (local_type_hoist_t, local_type_hoists, _hi);
          if (_h.class_def == _cdef) { _dup = 1; break; }
          if (_h.id != NULL && _h.id->u.s.s != NULL && _ta->u.s.s != NULL
              && strcmp (_h.id->u.s.s, _ta->u.s.s) == 0) { _dup = 1; break; }
        }
        if (!_dup) {
          local_type_hoist_t _h;
          _h.id = _ta;
          _h.class_def = _cdef;
          VARR_PUSH (local_type_hoist_t, local_type_hoists, _h);
        }
      }
    }
  }

  /* Deep-copy the template with type substitution */
  size_t _xref_mark = VARR_LENGTH (generic_crossref_t, generic_crossrefs);
  node_t spec_class = specialize_node (c2m_ctx, tmpl->class_node,
                                        base_name, spec_name,
                                        tmpl->n_type_params, tmpl->type_params, args);
  if (spec_class == NULL) {
    error (c2m_ctx, pos, "cannot instantiate generic class '%s'", base_name);
    if (generic_in_progress != NULL && VARR_LENGTH (cstr_t, generic_in_progress) > 0)
      VARR_TRUNC (cstr_t, generic_in_progress, VARR_LENGTH (cstr_t, generic_in_progress)-1);
    return build_id (c2m_ctx, base_name, pos);
  }

  /* Drain cross-generic references that specialize_node recorded (e.g.
     Is<Drawable> referenced from within As<Drawable>'s body).  Each is
     materialized now, after the outer specialization is complete, avoiding
     recursive get_or_create_specialization calls that corrupt state. */
  while (VARR_LENGTH (generic_crossref_t, generic_crossrefs) > _xref_mark) {
    generic_crossref_t _cr = VARR_POP (generic_crossref_t, generic_crossrefs);
    (void) get_or_create_specialization (c2m_ctx, _cr.ref_name, _cr.n_args, _cr.args, _cr.pos);
  }

  /* Register spec_name as a tpname so declarations like `List<String> x` work */
  node_t spec_id_node = build_id (c2m_ctx, spec_name, pos);
  tpname_add (c2m_ctx, spec_id_node, curr_scope, TRUE);

  /* Record in specialization cache (keep type args for method generics).
     Intern orig_name: callers may pass stack buffers (expression-context
     List<T>()); method-generic lookup needs a stable string. */
  generic_spec_t gs;
  gs.orig_name = uniq_cstr (c2m_ctx, base_name).s;
  gs.spec_name = spec_name;
  gs.n_args = n_args;
  for (int _ai = 0; _ai < 4; _ai++) gs.args[_ai] = (_ai < n_args) ? args[_ai] : NULL;
  VARR_PUSH (generic_spec_t, generic_specs, gs);

  /* Generic methods (Select<U> etc.) stay open over method type params after
     class specialisation.  Mark their copies on the specialized class with the
     template sentinel so check skips them; call sites monomorphize instead. */
  if (spec_class->code == N_CLASS && generic_method_templates != NULL) {
    node_t mlist = TAG_MEMBER_LIST (spec_class);
    if (mlist != NULL && mlist->code == N_LIST) {
      for (node_t mem = NL_HEAD (mlist->u.ops); mem != NULL; mem = NL_NEXT (mem)) {
        if (mem->code != N_FUNC_DEF) continue;
        node_t mid = DECL_ID (FUNC_DEF_DECL (mem));
        if (mid == NULL || mid->code != N_ID) continue;
        for (size_t mi = 0; mi < VARR_LENGTH (generic_method_tmpl_t, generic_method_templates);
             mi++) {
          generic_method_tmpl_t *mt
            = &VARR_ADDR (generic_method_tmpl_t, generic_method_templates)[mi];
          if (strcmp (mt->class_name, base_name) == 0
              && strcmp (mt->method_name, mid->u.s.s) == 0) {
            mem->attr = (void *)((intptr_t)-1);
            break;
          }
        }
      }
    }
  }

  /* Inject before the containing top-level item */
  VARR_PUSH (node_t, pending_lambdas, spec_class);

  if (generic_in_progress != NULL && VARR_LENGTH (cstr_t, generic_in_progress) > 0)
    VARR_TRUNC (cstr_t, generic_in_progress, VARR_LENGTH (cstr_t, generic_in_progress)-1);

  return build_id (c2m_ctx, spec_name, pos);
}

/* Parse `< TypeArg1, TypeArg2 >` for a generic class instantiation.
   Returns N_ID for the mangled specialization name, or NULL on error. */
static node_t parse_generic_instantiation (c2m_ctx_t c2m_ctx,
                                            const char *base_name, pos_t pos) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  /* Caller already checked: curr_token is T_CMP/N_LT */
  M (T_CMP); /* consume '<' */

  node_t type_args[4];
  int n_args = 0;
  do {
    node_t arg = parse_generic_type_arg (c2m_ctx);
    if (arg == NULL) break;
    if (n_args < 4) type_args[n_args++] = arg;
  } while (M (','));

  if (C (T_CMP) && curr_token->node_code == N_GT) {
    M (T_CMP);
  } else if (C (T_SH) && (curr_token->node_code == N_RSH || curr_token->node_code == N_RSH_ASSIGN)) {
    extern int c2m_pending_extra_gt;
    c2m_pending_extra_gt = 1;
    M (T_SH);
  } else if (C (T_CMP) && curr_token->node_code == N_GE) {
    M (T_CMP);
  } else {
    extern int c2m_pending_extra_gt;
    if (c2m_pending_extra_gt) {
      c2m_pending_extra_gt = 0;
    } else {
      error (c2m_ctx, pos, "expected '>' to close generic type argument list");
    }
  }

  return get_or_create_specialization (c2m_ctx, base_name, n_args, type_args, pos);
}

/* ─────────────────── Generic function helpers ───────────────────────── */

/* Forward declarations: defined later (in the check phase).  The call-site
   resolver needs them to materialize a freshly-instantiated specialization. */
static void materialize_pending_specs (c2m_ctx_t c2m_ctx, size_t mark);

/* Returns 1 if `name` is a registered generic function template. */
static int is_generic_fn_p (c2m_ctx_t c2m_ctx, const char *name) {
  if (c2m_ctx->parse_ctx == NULL) return 0;
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  VARR (generic_fn_tmpl_t) *gt = generic_fn_templates;
  if (gt == NULL) return 0;
  for (size_t i = 0; i < VARR_LENGTH (generic_fn_tmpl_t, gt); i++)
    if (strcmp (VARR_GET (generic_fn_tmpl_t, gt, i).name, name) == 0) return 1;
  return 0;
}

/* Returns a pointer to the generic function template for `name`, or NULL. */
static generic_fn_tmpl_t *get_generic_fn_template (c2m_ctx_t c2m_ctx, const char *name) {
  if (c2m_ctx->parse_ctx == NULL) return NULL;
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  VARR (generic_fn_tmpl_t) *gt = generic_fn_templates;
  if (gt == NULL) return NULL;
  for (size_t i = 0; i < VARR_LENGTH (generic_fn_tmpl_t, gt); i++) {
    generic_fn_tmpl_t *t = &VARR_ADDR (generic_fn_tmpl_t, gt)[i];
    if (strcmp (t->name, name) == 0) return t;
  }
  return NULL;
}

/* UFCS (Uniform Function Call Syntax) for free generic functions:
 *   list->GroupBy(fn)  ↔  GroupBy(list, fn)
 * Instance methods always win member lookup; UFCS is only a fallback when the
 * class has no such method and a free generic template with that name exists.
 * (Non-generic free-function UFCS can be added later.) */
static int ufcs_free_fn_candidate_p (c2m_ctx_t c2m_ctx, const char *name) {
  if (name == NULL || name[0] == '\0') return 0;
  return is_generic_fn_p (c2m_ctx, name);
}

/* Build a mangled specialization name for a generic function, e.g.
   Max + int -> "__genfn_Max_int".  Reuses the same arg-name mapping as
   mangle_generic_name so the spelling of primitive/class args is consistent
   across generic classes and functions. */
static const char *mangle_generic_fn_name (c2m_ctx_t c2m_ctx,
                                           const char *base_name,
                                           int n_args, node_t *args) {
  VARR_TRUNC (char, temp_string, 0);
  add_to_temp_string (c2m_ctx, "__genfn_");
  add_to_temp_string (c2m_ctx, base_name);
  for (int i = 0; i < n_args; i++) {
    add_to_temp_string (c2m_ctx, "_");
    node_t a = args[i];
    int ptr_depth = 0;
    while (a->code == N_POINTER) { ptr_depth++; a = NL_HEAD (a->u.ops); }
    const char *arg_name;
    switch (a->code) {
    case N_STRING:   arg_name = "String"; break;
    case N_INT:      arg_name = "int"; break;
    case N_DOUBLE:   arg_name = "double"; break;
    case N_FLOAT:    arg_name = "float"; break;
    case N_CHAR:     arg_name = "char"; break;
    case N_LONG:     arg_name = "long"; break;
    case N_SHORT:    arg_name = "short"; break;
    case N_UNSIGNED: arg_name = "unsigned"; break;
    case N_VOID:     arg_name = "void"; break;
    case N_DICT:     arg_name = "dict"; break;
    case N_BOOL:     arg_name = "bool"; break;
    case N_ID:       arg_name = a->u.s.s; break;
    default:         arg_name = "T"; break;
    }
    add_to_temp_string (c2m_ctx, arg_name);
    for (int p = 0; p < ptr_depth; p++) add_to_temp_string (c2m_ctx, "P");
  }
  return uniq_cstr (c2m_ctx, VARR_ADDR (char, temp_string)).s;
}

/* Get or create the specialization of generic function `base_name` with the
   given (already-inferred) type args.  Returns an N_ID for the mangled
   specialization name.  Pushes the specialized N_FUNC_DEF onto pending_lambdas
   if it's new; the caller is responsible for draining via
   materialize_pending_specs so the specialization is checked and injected
   into the module before the call site is fully resolved. */
static node_t get_or_create_generic_fn_specialization (c2m_ctx_t c2m_ctx,
                                                       const char *base_name,
                                                       int n_args, node_t *args,
                                                       pos_t pos) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;

  generic_fn_tmpl_t *tmpl = get_generic_fn_template (c2m_ctx, base_name);
  if (tmpl == NULL) {
    error (c2m_ctx, pos, "'%s' is not a generic function", base_name);
    return build_id (c2m_ctx, base_name, pos);
  }
  if (n_args != tmpl->n_type_params) {
    error (c2m_ctx, pos,
           "generic function '%s' expects %d type argument(s), got %d",
           base_name, tmpl->n_type_params, n_args);
    return build_id (c2m_ctx, base_name, pos);
  }
  if (tmpl->func_node == NULL) {
    error (c2m_ctx, pos,
           "cannot instantiate generic function '%s': its definition failed to parse",
           base_name);
    return build_id (c2m_ctx, base_name, pos);
  }

  const char *spec_name = mangle_generic_fn_name (c2m_ctx, base_name, n_args, args);

  /* Check cache: already created? */
  for (size_t i = 0; i < VARR_LENGTH (generic_fn_spec_t, generic_fn_specs); i++) {
    if (strcmp (VARR_GET (generic_fn_spec_t, generic_fn_specs, i).spec_name, spec_name) == 0)
      return build_id (c2m_ctx, spec_name, pos);
  }

  /* Deep-copy the template N_FUNC_DEF with type substitution.  specialize_node
     also renames the function name (orig_name -> spec_name) and any
     __ctor_/__dtor_ references (none for a free function).  The function name
     N_ID inside the declarator matches orig_name and is rewritten to spec_name. */
  {
    size_t _xref_mark = VARR_LENGTH (generic_crossref_t, generic_crossrefs);
    node_t spec_fn = specialize_node (c2m_ctx, tmpl->func_node,
                                      base_name, spec_name,
                                      tmpl->n_type_params, tmpl->type_params, args);
    if (spec_fn == NULL) {
      error (c2m_ctx, pos, "cannot instantiate generic function '%s'", base_name);
      return build_id (c2m_ctx, base_name, pos);
    }
    /* Nested generic types in the free-fn body (e.g. Map<G, List<T>*> inside
       ListGroupBy) leave crossrefs that must materialise before the specialized
       function is type-checked.  Same drain as generic method specialization. */
    while (VARR_LENGTH (generic_crossref_t, generic_crossrefs) > _xref_mark) {
      generic_crossref_t _cr = VARR_POP (generic_crossref_t, generic_crossrefs);
      (void) get_or_create_specialization (c2m_ctx, _cr.ref_name, _cr.n_args, _cr.args,
                                           _cr.pos);
    }

    /* Record in specialization cache */
    generic_fn_spec_t gs;
    gs.orig_name = base_name;
    gs.spec_name = spec_name;
    VARR_PUSH (generic_fn_spec_t, generic_fn_specs, gs);

    /* Inject before the containing top-level item (materialized by the caller
       via materialize_pending_specs, which calls check_lambda_func_def on it). */
    VARR_PUSH (node_t, pending_lambdas, spec_fn);

    return build_id (c2m_ctx, spec_name, pos);
  }
}

/* Look up a registered generic method template for class.method. */
static generic_method_tmpl_t *get_generic_method_template (c2m_ctx_t c2m_ctx,
                                                           const char *class_name,
                                                           const char *method_name) {
  parse_ctx_t parse_ctx;
  if (c2m_ctx->parse_ctx == NULL || class_name == NULL || method_name == NULL) return NULL;
  parse_ctx = c2m_ctx->parse_ctx;
  if (generic_method_templates == NULL) return NULL;
  for (size_t i = 0; i < VARR_LENGTH (generic_method_tmpl_t, generic_method_templates); i++) {
    generic_method_tmpl_t *t = &VARR_ADDR (generic_method_tmpl_t, generic_method_templates)[i];
    if (strcmp (t->class_name, class_name) == 0 && strcmp (t->method_name, method_name) == 0)
      return t;
  }
  return NULL;
}

/* Find specialization cache entry by mangled class name. */
static generic_spec_t *find_generic_spec_by_name (c2m_ctx_t c2m_ctx, const char *spec_name) {
  parse_ctx_t parse_ctx;
  if (c2m_ctx->parse_ctx == NULL || spec_name == NULL) return NULL;
  parse_ctx = c2m_ctx->parse_ctx;
  if (generic_specs == NULL) return NULL;
  for (size_t i = 0; i < VARR_LENGTH (generic_spec_t, generic_specs); i++) {
    generic_spec_t *s = &VARR_ADDR (generic_spec_t, generic_specs)[i];
    if (strcmp (s->spec_name, spec_name) == 0) return s;
  }
  return NULL;
}

/* Mangle: List + [int] + Select + [String] -> __genmeth_List_int_Select_String */
static const char *mangle_generic_method_name (c2m_ctx_t c2m_ctx,
                                               const char *class_name, int n_cargs,
                                               node_t *cargs,
                                               const char *method_name, int n_margs,
                                               node_t *margs) {
  VARR_TRUNC (char, temp_string, 0);
  add_to_temp_string (c2m_ctx, "__genmeth_");
  add_to_temp_string (c2m_ctx, class_name);
  for (int i = 0; i < n_cargs; i++) {
    add_to_temp_string (c2m_ctx, "_");
    node_t a = cargs[i];
    int ptr_depth = 0;
    while (a != NULL && a->code == N_POINTER) { ptr_depth++; a = NL_HEAD (a->u.ops); }
    const char *an = "T";
    if (a != NULL) {
      switch (a->code) {
      case N_STRING: an = "String"; break;
      case N_INT: an = "int"; break;
      case N_DOUBLE: an = "double"; break;
      case N_FLOAT: an = "float"; break;
      case N_CHAR: an = "char"; break;
      case N_LONG: an = "long"; break;
      case N_SHORT: an = "short"; break;
      case N_UNSIGNED: an = "unsigned"; break;
      case N_VOID: an = "void"; break;
      case N_DICT: an = "dict"; break;
      case N_BOOL: an = "bool"; break;
      case N_ID: an = a->u.s.s; break;
      default: break;
      }
    }
    add_to_temp_string (c2m_ctx, an);
    for (int p = 0; p < ptr_depth; p++) add_to_temp_string (c2m_ctx, "P");
  }
  add_to_temp_string (c2m_ctx, "_");
  add_to_temp_string (c2m_ctx, method_name);
  for (int i = 0; i < n_margs; i++) {
    add_to_temp_string (c2m_ctx, "_");
    node_t a = margs[i];
    int ptr_depth = 0;
    while (a != NULL && a->code == N_POINTER) { ptr_depth++; a = NL_HEAD (a->u.ops); }
    const char *an = "T";
    if (a != NULL) {
      switch (a->code) {
      case N_STRING: an = "String"; break;
      case N_INT: an = "int"; break;
      case N_DOUBLE: an = "double"; break;
      case N_FLOAT: an = "float"; break;
      case N_CHAR: an = "char"; break;
      case N_LONG: an = "long"; break;
      case N_SHORT: an = "short"; break;
      case N_UNSIGNED: an = "unsigned"; break;
      case N_VOID: an = "void"; break;
      case N_DICT: an = "dict"; break;
      case N_BOOL: an = "bool"; break;
      case N_ID: an = a->u.s.s; break;
      default: break;
      }
    }
    add_to_temp_string (c2m_ctx, an);
    for (int p = 0; p < ptr_depth; p++) add_to_temp_string (c2m_ctx, "P");
  }
  return uniq_cstr (c2m_ctx, VARR_ADDR (char, temp_string)).s;
}

/* Prepend `ClassSpec *this` to a specialized method free-function's param list. */
static void prepend_this_param_to_func (c2m_ctx_t c2m_ctx, node_t func_def,
                                        const char *class_spec_name, pos_t pos) {
  node_t declarator, decl_list, func, param_list;
  node_t class_id, specs, ptr_node, this_id, this_declr, this_param;

  if (func_def == NULL || class_spec_name == NULL) return;
  declarator = FUNC_DEF_DECL (func_def);
  if (declarator == NULL) return;
  decl_list = DECL_LIST (declarator);
  if (decl_list == NULL) return;
  func = NL_HEAD (decl_list->u.ops);
  while (func != NULL && func->code != N_FUNC) func = NL_NEXT (func);
  if (func == NULL) return;
  param_list = NL_HEAD (func->u.ops);
  if (param_list == NULL || param_list->code != N_LIST) return;

  class_id = build_id (c2m_ctx, class_spec_name, pos);
  specs = new_node1 (c2m_ctx, N_LIST, class_id);
  ptr_node = new_pos_node1 (c2m_ctx, N_POINTER, pos, new_node (c2m_ctx, N_LIST));
  this_id = build_id (c2m_ctx, "this", pos);
  this_declr = new_pos_node2 (c2m_ctx, N_DECL, pos, this_id,
                              new_node1 (c2m_ctx, N_LIST, ptr_node));
  this_param = build_spec_decl (c2m_ctx, pos, specs, this_declr, NULL, NULL, NULL);
  NL_PREPEND (param_list->u.ops, this_param);
}

/* Materialize a generic method as a free function with explicit this.
   Returns N_ID of the mangled specialization name. */
static node_t get_or_create_generic_method_specialization (
    c2m_ctx_t c2m_ctx, const char *class_name, const char *class_spec_name,
    int n_cargs, node_t *cargs, generic_method_tmpl_t *mt,
    int n_margs, node_t *margs, pos_t pos) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  const char *spec_name;
  node_t all_params_nodes[8];
  const char *all_param_names[8];
  int n_all, i;
  generic_tmpl_t *ctmpl;

  if (mt == NULL || mt->func_node == NULL) return NULL;
  ctmpl = get_generic_template (c2m_ctx, class_name);
  if (ctmpl == NULL) return NULL;

  spec_name = mangle_generic_method_name (c2m_ctx, class_name, n_cargs, cargs,
                                          mt->method_name, n_margs, margs);

  if (generic_method_specs != NULL) {
    for (size_t si = 0; si < VARR_LENGTH (generic_method_spec_t, generic_method_specs); si++) {
      if (strcmp (VARR_GET (generic_method_spec_t, generic_method_specs, si).spec_name,
                  spec_name)
          == 0)
        return build_id (c2m_ctx, spec_name, pos);
    }
  }

  /* Combine class type params + method type params for substitution. */
  n_all = 0;
  for (i = 0; i < ctmpl->n_type_params && n_all < 8; i++) {
    all_param_names[n_all] = ctmpl->type_params[i];
    all_params_nodes[n_all] = (i < n_cargs) ? cargs[i] : new_pos_node (c2m_ctx, N_INT, pos);
    n_all++;
  }
  for (i = 0; i < mt->n_type_params && n_all < 8; i++) {
    all_param_names[n_all] = mt->type_params[i];
    all_params_nodes[n_all] = (i < n_margs) ? margs[i] : new_pos_node (c2m_ctx, N_INT, pos);
    n_all++;
  }

  {
    size_t _xref_mark = VARR_LENGTH (generic_crossref_t, generic_crossrefs);
    node_t spec_fn = specialize_node (c2m_ctx, mt->func_node, mt->method_name, spec_name,
                                      n_all, all_param_names, all_params_nodes);
    if (spec_fn == NULL) {
      error (c2m_ctx, pos, "cannot instantiate generic method '%s.%s'",
             class_name, mt->method_name);
      return NULL;
    }
    /* Clear template sentinel from the specialized copy; re-apply da_ignore
       so create_decl sets decl->da_ignore_p on the monomorphized free fn. */
    spec_fn->attr = mt->da_ignore_p ? PRECHECK_DA_IGNORE : NULL;
    if (!mt->is_static)
      prepend_this_param_to_func (c2m_ctx, spec_fn, class_spec_name, pos);

    /* Materialize any nested concrete specializations produced in the body. */
    while (VARR_LENGTH (generic_crossref_t, generic_crossrefs) > _xref_mark) {
      generic_crossref_t _cr = VARR_POP (generic_crossref_t, generic_crossrefs);
      (void) get_or_create_specialization (c2m_ctx, _cr.ref_name, _cr.n_args, _cr.args,
                                           _cr.pos);
    }

    {
      generic_method_spec_t ms;
      ms.spec_name = spec_name;
      VARR_PUSH (generic_method_spec_t, generic_method_specs, ms);
    }
    VARR_PUSH (node_t, pending_lambdas, spec_fn);
    return build_id (c2m_ctx, spec_name, pos);
  }
}

/* ─────────────────────────── Generics helpers end ─────────────────────── */

D (compound_stmt);
D (lambda_expr);
D (param_type_list);

/* Expressions: */
D (type_name);
D (expr);
D (assign_expr);
D (initializer_list);

/* Assemble a lambda N_FUNC_DEF:  static auto LNAME (PLIST) BODY.
   The 'auto' return type becomes TP_UNDEF during check and is inferred from
   the body's return statements.  Shared by parse-time typed lambdas and
   check-time instantiation of untyped lambdas (N_LAMBDA). */
static node_t build_lambda_func_def (c2m_ctx_t c2m_ctx, const char *lname, node_t plist,
                                     node_t body, pos_t pos) {
  node_t specs = new_node (c2m_ctx, N_LIST);
  op_append (c2m_ctx, specs, new_pos_node (c2m_ctx, N_STATIC, pos));
  op_append (c2m_ctx, specs, new_pos_node (c2m_ctx, N_AUTO, pos));

  /* Declarator: LNAME ( param_list ) */
  node_t lam_id = build_id (c2m_ctx, lname, pos);
  node_t func_node = new_pos_node1 (c2m_ctx, N_FUNC, pos, plist);
  node_t decl_inner = new_node1 (c2m_ctx, N_LIST, func_node);
  node_t declarator = new_pos_node2 (c2m_ctx, N_DECL, pos, lam_id, decl_inner);

  return new_pos_node4 (c2m_ctx, N_FUNC_DEF, pos, specs, declarator,
                        new_node (c2m_ctx, N_LIST), /* empty K&R decl list */
                        body);
}

/* Parse a lambda body after '=>': either a block { ... } or a single
   expression auto-wrapped in { return <expr>; }.  Shared by typed lambdas
   (lambda_expr) and untyped/shorthand lambdas (N_LAMBDA nodes). */
static node_t parse_lambda_body (c2m_ctx_t c2m_ctx, pos_t pos) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  node_t body, r;

  if (C ('{')) {
    body = compound_stmt (c2m_ctx, FALSE);
    if (body == err_node) body = new_node (c2m_ctx, N_IGNORE);
  } else {
    r = assign_expr (c2m_ctx, FALSE);
    if (r == err_node) r = new_node (c2m_ctx, N_IGNORE);
    /* Wrap: { return <expr>; } */
    node_t ret_stmt = new_pos_node2 (c2m_ctx, N_RETURN, pos,
                                      new_node (c2m_ctx, N_LIST), r);
    node_t blist = new_node (c2m_ctx, N_LIST);
    op_append (c2m_ctx, blist, ret_stmt);
    body = new_pos_node2 (c2m_ctx, N_BLOCK, pos,
                           new_node (c2m_ctx, N_LIST), blist);
    body->attr = NULL; /* scope set during check */
  }
  return body;
}

/* Lambda expression: ( typed-param-list? ) => expr-or-block
   Always called via TRY(); returns err_node if this is not a lambda.

   All lambdas (typed and untyped) are deferred to check as N_LAMBDA so
   free-variable analysis can detect captures before any static hoist.
   Non-capturing lambdas become static funcs at check time; capturing ones
   are only legal as direct args to recognized HOFs (Where/Filter/…). */
D (lambda_expr) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  node_t plist, r, body;
  pos_t pos;

  if (!C ('(')) return err_node;
  pos = curr_token->pos;
  M ('('); /* consume '(' */

  if (C (')')) {
    /* Zero-arg lambda: () => body */
    M (')');
    plist = new_node (c2m_ctx, N_LIST);
  } else {
    /* Try a typed parameter list first; on failure fall back to an untyped
       identifier list  (a, b, ...).  Either way ')' '=>' must follow. */
    size_t mark = record_start (c2m_ctx);
    if ((plist = TRY (param_type_list)) != err_node && M (')') && C (T_FAT_ARROW)) {
      record_stop (c2m_ctx, mark, FALSE); /* commit typed list */
    } else {
      record_stop (c2m_ctx, mark, TRUE); /* rewind to after '(' */
      plist = new_node (c2m_ctx, N_LIST);
      for (;;) {
        if (!MN (T_ID, r)) return err_node;
        op_append (c2m_ctx, plist, r);
        if (!M (',')) break;
      }
      if (!M (')')) return err_node;
    }
  }

  /* Commit only when we see '=>' immediately after ')' */
  if (!C (T_FAT_ARROW)) return err_node;
  M (T_FAT_ARROW); /* consume '=>' — committed from here on */

  body = parse_lambda_body (c2m_ctx, pos);
  return new_pos_node2 (c2m_ctx, N_LAMBDA, pos, plist, body);
}

D (par_type_name) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  node_t r;

  PT ('(');
  P (type_name);
  PT (')');
  return r;
}

D (primary_expr) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  node_t r, n, op, gn, list;
  pos_t pos;

  /* GNU extended asm as an expression/statement:
       __asm__ __volatile__ ("bswap %0" : "+r" (x));
     Skip the operand lists; used by Darwin libkern and glibc barriers. */
  if (C (T_ID) && curr_token->repr != NULL
      && (strcmp (curr_token->repr, "__asm__") == 0 || strcmp (curr_token->repr, "__asm") == 0
          || strcmp (curr_token->repr, "asm") == 0)) {
    size_t asm_mark = record_start (c2m_ctx);
    pos_t asm_pos = curr_token->pos;
    read_token (c2m_ctx);
    while (C (T_VOLATILE)
           || (C (T_ID) && curr_token->repr != NULL
               && (strcmp (curr_token->repr, "goto") == 0
                   || strcmp (curr_token->repr, "__volatile__") == 0
                   || strcmp (curr_token->repr, "__volatile") == 0)))
      read_token (c2m_ctx);
    if (C ('(')) {
      int depth = 0;
      int ok = 1;
      do {
        if (C ('(')) {
          read_token (c2m_ctx);
          depth++;
        } else if (C (')')) {
          read_token (c2m_ctx);
          depth--;
        } else if (C (T_EOFILE)) {
          ok = 0;
          break;
        } else {
          read_token (c2m_ctx);
        }
      } while (depth > 0);
      if (ok) {
        record_stop (c2m_ctx, asm_mark, FALSE);
        return new_i_node (c2m_ctx, 0, asm_pos);
      }
    }
    record_stop (c2m_ctx, asm_mark, TRUE);
  }

  /* Lambda: ( typed-params ) => body  — try before other '(' handling */
  if (C ('(')) {
    node_t lr = TRY (lambda_expr);
    if (lr != err_node) return lr;
    /* Not a lambda — token stream rewound, fall through to normal handling */
  }

  /* is_pointer<T>(): compiler intrinsic. Parse the type argument list specially
     so the '<' '>' are not treated as comparison operators. Builds an N_CALL with
     children [is_pointer_id, N_LIST(type_arg), N_LIST(empty)] which check() rewrites
     to an integer literal based on whether T resolves to a pointer type. */
  if (C (T_ID) && curr_token->repr != NULL && strcmp (curr_token->repr, "is_pointer") == 0) {
    size_t ip_mark = record_start (c2m_ctx);
    node_t ip_id;
    pos_t ip_pos = curr_token->pos;
    MN (T_ID, ip_id);
    if (C (T_CMP) && curr_token->node_code == N_LT) {
      M (T_CMP); /* consume '<' */
      node_t targ = parse_generic_type_arg (c2m_ctx);
      if (targ != NULL && C (T_CMP) && curr_token->node_code == N_GT) {
        M (T_CMP); /* consume '>' */
        if (M ('(') && M (')')) {
          record_stop (c2m_ctx, ip_mark, FALSE); /* commit */
          node_t tlist = new_node1 (c2m_ctx, N_LIST, targ);
          node_t alist = new_node (c2m_ctx, N_LIST);
          return new_pos_node3 (c2m_ctx, N_CALL, ip_pos, ip_id, tlist, alist);
        }
      }
    }
    record_stop (c2m_ctx, ip_mark, TRUE); /* not is_pointer<T>(): rewind */
  }

  /* is_move_only<T>(): compiler intrinsic (sibling of is_pointer<T>()) that
     returns 1 when T is a move-only collection (List/Map/Set instantiation),
     0 otherwise.  Lets generic collection bodies pick a Copy-via-Get path for
     move-only elements and a zero-copy buffer path for everything else.
     Parses like is_pointer<T>() so '<' '>' are not comparison operators. */
  if (C (T_ID) && curr_token->repr != NULL && strcmp (curr_token->repr, "is_move_only") == 0) {
    size_t im_mark = record_start (c2m_ctx);
    node_t im_id;
    pos_t im_pos = curr_token->pos;
    MN (T_ID, im_id);
    if (C (T_CMP) && curr_token->node_code == N_LT) {
      M (T_CMP); /* consume '<' */
      node_t targ = parse_generic_type_arg (c2m_ctx);
      if (targ != NULL) {
        extern int c2m_pending_extra_gt;
        /* The closing '>' may arrive split from a '>>' after a nested
           instantiation (is_move_only<List<int>>()) — consume the pending one. */
        if (c2m_pending_extra_gt) {
          c2m_pending_extra_gt = 0;
          if (M ('(') && M (')')) {
            record_stop (c2m_ctx, im_mark, FALSE); /* commit */
            node_t tlist = new_node1 (c2m_ctx, N_LIST, targ);
            node_t alist = new_node (c2m_ctx, N_LIST);
            return new_pos_node3 (c2m_ctx, N_CALL, im_pos, im_id, tlist, alist);
          }
        } else if (C (T_CMP) && curr_token->node_code == N_GT) {
          M (T_CMP); /* consume '>' */
          if (M ('(') && M (')')) {
            record_stop (c2m_ctx, im_mark, FALSE); /* commit */
            node_t tlist = new_node1 (c2m_ctx, N_LIST, targ);
            node_t alist = new_node (c2m_ctx, N_LIST);
            return new_pos_node3 (c2m_ctx, N_CALL, im_pos, im_id, tlist, alist);
          }
        }
      }
    }
    record_stop (c2m_ctx, im_mark, TRUE); /* not is_move_only<T>(): rewind */
  }

  /* nameof<T>(): compile-time reflection intrinsic.  Returns the C-level name
     of T as a string literal (char[N]).  Inside a generic class body, T is a
     type parameter; specialize_node substitutes nameof<T>() with an N_STR
     whose value is the concrete type-arg name at instantiation time.  Used
     outside a generic (e.g. nameof<Circle>()) it resolves in check() to the
     class/interface name.  Parses like is_pointer<T>() so '<' '>' are not
     comparison operators. */
  if (C (T_ID) && curr_token->repr != NULL && strcmp (curr_token->repr, "nameof") == 0) {
    size_t nm_mark = record_start (c2m_ctx);
    node_t nm_id;
    pos_t nm_pos = curr_token->pos;
    MN (T_ID, nm_id);
    if (C (T_CMP) && curr_token->node_code == N_LT) {
      M (T_CMP); /* consume '<' */
      node_t targ = parse_generic_type_arg (c2m_ctx);
      if (targ != NULL && C (T_CMP) && curr_token->node_code == N_GT) {
        M (T_CMP); /* consume '>' */
        if (M ('(') && M (')')) {
          record_stop (c2m_ctx, nm_mark, FALSE); /* commit */
          node_t tlist = new_node1 (c2m_ctx, N_LIST, targ);
          node_t alist = new_node (c2m_ctx, N_LIST);
          return new_pos_node3 (c2m_ctx, N_CALL, nm_pos, nm_id, tlist, alist);
        }
      }
    }
    record_stop (c2m_ctx, nm_mark, TRUE); /* not nameof<T>(): rewind */
  }

  /* typeof<T>(): companion to nameof<T>().  Same parse shape; check()/specialize
     keep pointer depth so typeof<int*>() == "int*".  typeof is a keyword
     (T_TYPEOF), so match the keyword rather than a bare identifier. */
  if (C (T_TYPEOF)) {
    size_t ty_mark = record_start (c2m_ctx);
    pos_t ty_pos = curr_token->pos;
    M (T_TYPEOF);
    if (C (T_CMP) && curr_token->node_code == N_LT) {
      M (T_CMP); /* consume '<' */
      node_t targ = parse_generic_type_arg (c2m_ctx);
      if (targ != NULL && C (T_CMP) && curr_token->node_code == N_GT) {
        M (T_CMP); /* consume '>' */
        if (M ('(') && M (')')) {
          record_stop (c2m_ctx, ty_mark, FALSE); /* commit */
          node_t ty_id = build_id (c2m_ctx, "typeof", ty_pos);
          node_t tlist = new_node1 (c2m_ctx, N_LIST, targ);
          node_t alist = new_node (c2m_ctx, N_LIST);
          return new_pos_node3 (c2m_ctx, N_CALL, ty_pos, ty_id, tlist, alist);
        }
      }
    }
    record_stop (c2m_ctx, ty_mark, TRUE); /* not typeof<T>(): rewind */
  }

  /* Shorthand untyped lambda:  x => body  (single parameter, no parens).
     The parameter type is inferred at the call site during check. */
  if (C (T_ID)) {
    size_t mark = record_start (c2m_ctx);
    node_t pid;
    pos = curr_token->pos;
    MN (T_ID, pid);
    if (C (T_FAT_ARROW)) {
      record_stop (c2m_ctx, mark, FALSE); /* commit */
      M (T_FAT_ARROW);
      node_t plist = new_node1 (c2m_ctx, N_LIST, pid);
      node_t body = parse_lambda_body (c2m_ctx, pos);
      return new_pos_node2 (c2m_ctx, N_LAMBDA, pos, plist, body);
    }
    record_stop (c2m_ctx, mark, TRUE); /* not a lambda: rewind */
  }

  /* Generic class instantiation in expression context:  Name<TypeArg>.method()
     or Name<TypeArg>(args).  Without this, `Is<T>.Of(h)` parses as
     `(Is < T) > (.Of(h))` because '<' is treated as comparison.  When a bare
     identifier is a registered generic class and '<' follows, monomorphize via
     parse_generic_instantiation (which returns an N_ID naming the specialized
     class), then run post_expr_part so '.method()' / '(args)' compose.
     Rewinds on mismatch so `Name` stays a plain identifier. */
  if (C (T_ID) && curr_token->repr != NULL
      && is_generic_class_p (c2m_ctx, curr_token->repr)) {
    size_t g_mark = record_start (c2m_ctx);
    pos_t g_pos = curr_token->pos;
    /* Intern the name: get_or_create_specialization stores orig_name for the
       life of the compilation unit (method-generic monomorph uses it).  A
       stack buffer here left dangling pointers so stack `List<T>()` could not
       resolve Select while `new List<T>()` (interned id) could. */
    const char *g_name = uniq_cstr (c2m_ctx, curr_token->repr).s;
    node_t g_id;
    MN (T_ID, g_id);
    if (C (T_CMP) && curr_token->node_code == N_LT) {
      record_stop (c2m_ctx, g_mark, FALSE); /* commit: this is Name<...> */
      r = parse_generic_instantiation (c2m_ctx, g_name, g_pos);
      PA (post_expr_part, r);
      return r;
    }
    record_stop (c2m_ctx, g_mark, TRUE); /* not Name<...>: rewind */
  }

  /* Explicit type arguments on a generic function call (Max<int>(3, 5)) are
     not yet supported at the call site — type inference (Max(3, 5)) handles
     the common case.  The parse-time detection of `Name<` for generic classes
     above does not fire for generic functions because materializing a
     specialization at parse time would not be checked before the call site
     (the check phase processes module items in order, and the specialization
     would be injected after the calling item).  Inference at check time avoids
     this by materializing in-place via materialize_pending_specs. */

  	if (MN (T_ID, r) || MN (T_NUMBER, r) || MN (T_CH, r) || MN (T_STR, r)) {
  	    return r;
  	  } else if (MP (T_STRING, pos)) {
  	    r = new_pos_node (c2m_ctx, N_STRING, pos);
  	    return r;
  	  } else if (MP (T_ANDAND, pos)) {
    PTN (T_ID);
    return new_pos_node1 (c2m_ctx, N_LABEL_ADDR, pos, r);
  } else if (M ('(')) {
    if (C ('{')) {
      P (compound_stmt);
      r = new_node1 (c2m_ctx, N_STMTEXPR, r);
    } else {
      P (expr);
    }
    if (M (')')) return r;
  } else if (MP (T_GENERIC, pos)) {
    PT ('(');
    P (assign_expr);
    PT (',');
    list = new_node (c2m_ctx, N_LIST);
    n = new_pos_node2 (c2m_ctx, N_GENERIC, pos, r, list);
    for (;;) { /* generic-assoc-list , generic-association */
      if (MP (T_DEFAULT, pos)) {
        op = new_node (c2m_ctx, N_IGNORE);
      } else {
        P (type_name);
        op = r;
        pos = POS (op);
      }
      PT (':');
      P (assign_expr);
      gn = new_pos_node2 (c2m_ctx, N_GENERIC_ASSOC, pos, op, r);
      op_append (c2m_ctx, list, gn);
      if (!M (',')) break;
    }
    PT (')');
    return n;
  }
  return err_node;
}

/* Deep-copy an expression subtree at parse time (attrs are not set yet, so
   only code/pos/payload/children need duplicating).  Used by the `?.`
   desugar to duplicate the receiver into the null guard. */
static node_t parse_copy_expr (c2m_ctx_t c2m_ctx, node_t n) {
  node_t r;

  if (n == NULL) return NULL;
  if (generic_node_has_scalar_data (n->code) || n->code == N_ID || n->code == N_STRING
      || n->code == N_IGNORE)
    return copy_node (c2m_ctx, n);
  r = new_node (c2m_ctx, n->code);
  set_node_pos (c2m_ctx, r, POS (n));
  for (node_t c = NL_HEAD (n->u.ops); c != NULL; c = NL_NEXT (c))
    op_append (c2m_ctx, r, parse_copy_expr (c2m_ctx, c));
  return r;
}

DA (post_expr_part) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  node_t r, n, op, list;
  node_code_t code;
  pos_t pos;

  r = arg;
  for (;;) {
    if (MC (T_INCDEC, pos, code)) {
      code = code == N_INC ? N_POST_INC : N_POST_DEC;
      op = r;
      r = NULL;
    } else if (MC ('.', pos, code) || MC (T_ARROW, pos, code)) {
      op = r;
      /* Accept identifier method/field names, plus keywords that double as
         reflection methods (typeof is a token keyword; nameof is a plain id). */
      if (MN (T_ID, r)) {
        /* normal */
      } else if (C (T_TYPEOF)) {
        pos_t idpos = curr_token->pos;
        M (T_TYPEOF);
        r = build_id (c2m_ctx, "typeof", idpos);
      } else {
        /* e.g. `p->class` — class is reserved, not a field name */
        note_if_reserved_identifier_token (c2m_ctx);
        return err_node;
      }
      // Build the N_FIELD now, then continue the loop so that a following '('
      // will be handled by the call branch, making N_CALL( N_FIELD(base, id), arglist )
      n = new_pos_node1 (c2m_ctx, code, pos, op);
      if (r != NULL) op_append (c2m_ctx, n, r);
      /* Method type args: obj.Select<String>(fn).  Speculative parse of
         `<TypeArg,...>` immediately after the method id; only commit when a
         following '(' confirms a call.  Stored as a third N_FIELD child
         (N_LIST of type args) so check can read them without changing N_CALL. */
      if (C (T_CMP) && curr_token->node_code == N_LT) {
        size_t mta_mark = record_start (c2m_ctx);
        node_t targs = new_node (c2m_ctx, N_LIST);
        int ok = 1;
        M (T_CMP); /* '<' */
        do {
          node_t ta = parse_generic_type_arg (c2m_ctx);
          if (ta == NULL) { ok = 0; break; }
          op_append (c2m_ctx, targs, ta);
        } while (M (','));
        if (ok && C (T_CMP) && curr_token->node_code == N_GT) {
          M (T_CMP); /* '>' */
          if (C ('(')) {
            record_stop (c2m_ctx, mta_mark, FALSE);
            op_append (c2m_ctx, n, targs);
          } else {
            record_stop (c2m_ctx, mta_mark, TRUE);
          }
        } else {
          record_stop (c2m_ctx, mta_mark, TRUE);
        }
      }
      r = n;
      continue;
    } else if (MP (T_QDOT, pos)) {
      /* Safe navigation: recv?.member... desugars to
           recv ? recv->member <rest of postfix chain> : 0
         with the WHOLE remaining chain inside the guard (C# semantics: a null
         receiver short-circuits everything after it, so a?.b.c and a?.m(x)
         are safe).  The receiver subtree is duplicated into the guard, so it
         is evaluated twice — keep receivers simple (a variable or field).
         The N_COND is tagged via the attr sentinel (void *) 2 so the checker
         can allow void-returning methods and String members against the
         synthesized 0 else-arm (see the N_COND case in check ()). */
      node_t recv = r, inner;

      if (MN (T_ID, r)) {
        /* normal member/method name */
      } else if (C (T_TYPEOF)) {
        pos_t idpos = curr_token->pos;
        M (T_TYPEOF);
        r = build_id (c2m_ctx, "typeof", idpos);
      } else {
        note_if_reserved_identifier_token (c2m_ctx);
        return err_node;
      }
      inner
        = new_pos_node2 (c2m_ctx, N_DEREF_FIELD, pos, parse_copy_expr (c2m_ctx, recv), r);
      PA (post_expr_part, inner); /* r = remaining postfix chain applied to inner */
      n = new_pos_node3 (c2m_ctx, N_COND, pos, recv, r, new_i_node (c2m_ctx, 0, pos));
      n->attr = (void *) (intptr_t) 2; /* safe-navigation marker for the checker */
      return n;
    } else if (MC ('[', pos, code)) {
      op = r;
      P (expr);
      PT (']');
    } else if (!MP ('(', pos)) {
      break;
    } else {
      // Regular function call
      op = r;
      r = NULL;
      code = N_CALL;
      list = new_node (c2m_ctx, N_LIST);
      if (!C (')')) {
        for (;;) {
          P (assign_expr);
          op_append (c2m_ctx, list, r);
          if (!M (',')) break;
        }
      }
      r = list;
      PT (')');
    }
    n = new_pos_node1 (c2m_ctx, code, pos, op);
    if (r != NULL) op_append (c2m_ctx, n, r);
    r = n;
  }
  return r;
}


D (post_expr) {
  node_t r;

  P (primary_expr);
  PA (post_expr_part, r);
  return r;
}

D (unary_expr) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  node_t r, t;
  node_code_t code;
  pos_t pos;

  /* any<Interface>(expr) intrinsic:  wrap a concrete C* as an erased Any<I>*
     handle.  `any` is a soft keyword: only treated specially when followed by
     `< Identifier > (`, so it stays usable as an ordinary C identifier. */
  if (C_SOFT ("any")) {
    size_t mark = record_start (c2m_ctx);
    pos_t apos = curr_token->pos;
    node_t iface_id = NULL;
    M_SOFT ("any");
    if (C (T_CMP) && curr_token->node_code == N_LT) {
      M (T_CMP); /* consume '<' */
      if (MN (T_ID, iface_id) && C (T_CMP) && curr_token->node_code == N_GT) {
        M (T_CMP); /* consume '>' */
        if (C ('(')) {
          node_t arg, struct_id, iface_node_id;
          record_stop (c2m_ctx, mark, FALSE); /* commit: this is any<I>(...) */
          M ('(');
          P (assign_expr);
          arg = r;
          PT (')');
          /* Synthesize __Any_<I> if needed; keep iface + handle names on the node. */
          struct_id = synthesize_any_class (c2m_ctx, iface_id->u.s.s, apos);
          iface_node_id = build_id (c2m_ctx, iface_id->u.s.s, apos);
          r = new_pos_node3 (c2m_ctx, N_ANY, apos, iface_node_id, struct_id, arg);
          PA (post_expr_part, r);
          return r;
        }
      }
    }
    record_stop (c2m_ctx, mark, TRUE); /* not any<I>(...): rewind, `any` is an ident */
  }

  /* new-expression:  new ClassName ( arg-list? )   — heap allocation + ctor.
     `new` is a soft keyword: only treated specially when immediately followed
     by `Identifier (`, so it stays usable as an ordinary C identifier. */
  if (C_SOFT ("new")) {
    size_t mark = record_start (c2m_ctx);
    pos_t npos = curr_token->pos;
    node_t tid = NULL;
    M_SOFT ("new");
    /* new dict(size?)  — heap-arena-backed empty dict.
       `size` is an optional byte-count for the arena; 0 means "use default". */
    if (C (T_DICT)) {
      pos_t dpos = curr_token->pos;
      size_t dmark = record_start (c2m_ctx);
      M (T_DICT);
      if (C ('(')) {
        record_stop (c2m_ctx, dmark, FALSE); /* commit */
        record_stop (c2m_ctx, mark, FALSE);
        M ('(');
        node_t size_arg = new_node (c2m_ctx, N_LIST);
        if (!C (')')) {
          P (assign_expr);
          op_append (c2m_ctx, size_arg, r);
        }
        PT (')');
        r = new_pos_node2 (c2m_ctx, N_NEW, npos,
                           new_pos_node (c2m_ctx, N_DICT, dpos), size_arg);
        PA (post_expr_part, r);
        return r;
      }
      record_stop (c2m_ctx, dmark, TRUE); /* not new dict(): rewind */
    }
    if (MN (T_ID, tid)) {
      /* Generic new: new List<String>(...) - specialize before checking '(' */
      if (is_generic_class_p (c2m_ctx, tid->u.s.s)
          && C (T_CMP) && curr_token->node_code == N_LT) {
        tid = parse_generic_instantiation (c2m_ctx, tid->u.s.s, POS (tid));
      }
      if (C ('(')) {
      node_t args;
      record_stop (c2m_ctx, mark, FALSE); /* commit: this is a new-expression */
      M ('(');
      args = new_node (c2m_ctx, N_LIST);
      if (!C (')')) {
        for (;;) {
          /* Named argument:  name = value .  Stored as N_FIELD_ID(name, value);
             the constructor call resolves it to positional order in check.
             A single '=' (not '==') after an identifier marks it. */
          if (C (T_ID)) {
            size_t am = record_start (c2m_ctx);
            node_t nm;
            pos_t nmpos = curr_token->pos;
            MN (T_ID, nm);
            if (M ('=')) {
              record_stop (c2m_ctx, am, FALSE); /* commit: named argument */
              P (assign_expr);
              op_append (c2m_ctx, args,
                         new_pos_node2 (c2m_ctx, N_FIELD_ID, nmpos, nm, r));
              if (!M (',')) break;
              continue;
            }
            record_stop (c2m_ctx, am, TRUE); /* not named: rewind */
          }
          P (assign_expr);
          op_append (c2m_ctx, args, r);
          if (!M (',')) break;
        }
      }
      PT (')');
      /* Optional object-initializer block (C/C++23-style designated fields):
           new T(args) { .field = value, .field = value }
         Each `.name = expr` runs as a post-construction field assignment on
         the freshly-allocated object.  Stored as the optional third N_NEW
         child: an N_LIST of N_FIELD_ID(name, value) nodes (the same shape as
         named constructor arguments).  Distinguished from the collection
         brace-init `new T{e1, e2}` by the leading `.` designators. */
      if (C ('{')) {
        node_t inits = new_node (c2m_ctx, N_LIST);
        M ('{');
        if (!C ('}')) {
          for (;;) {
            node_t fnm;
            pos_t fpos;
            PT ('.');
            fpos = curr_token->pos;
            if (!MN (T_ID, fnm)) PTFAIL (T_ID);
            PT ('=');
            P (assign_expr);
            op_append (c2m_ctx, inits,
                       new_pos_node2 (c2m_ctx, N_FIELD_ID, fpos, fnm, r));
            if (!M (',')) break;
            if (C ('}')) break; /* allow trailing comma */
          }
        }
        PT ('}');
        r = new_pos_node3 (c2m_ctx, N_NEW, npos, tid, args, inits);
      } else {
        r = new_pos_node2 (c2m_ctx, N_NEW, npos, tid, args);
      }
      PA (post_expr_part, r); /* allow chaining: new Foo(..).method(..) */
      return r;
      } /* end if C('(') */
      else if (C ('{')) {
        /* Brace-init:  new T{e1, e2, ...}  — sugar for the zero-arg constructor
           followed by one obj->Add(e) call per element (duck-typed protocol:
           any class with an Add method taking one argument supports it).
           children: type_id(0), ctor arg_list(1, empty), init_list(2). */
        node_t args, inits;
        record_stop (c2m_ctx, mark, FALSE); /* commit: this is a new-expression */
        M ('{');
        args = new_node (c2m_ctx, N_LIST); /* empty ctor argument list */
        inits = new_node (c2m_ctx, N_LIST);
        if (!C ('}')) {
          for (;;) {
            P (assign_expr);
            op_append (c2m_ctx, inits, r);
            if (!M (',')) break;
            if (C ('}')) break; /* allow trailing comma */
          }
        }
        PT ('}');
        r = new_pos_node3 (c2m_ctx, N_NEW, npos, tid, args, inits);
        PA (post_expr_part, r); /* allow chaining: new Foo{..}->method(..) */
        return r;
      } /* end if C('{') */
    } /* end if MN(T_ID, tid) */
    record_stop (c2m_ctx, mark, TRUE); /* not a new-expression: rewind */
  }

  if ((r = TRY (par_type_name)) != err_node) {
    /* Lenient dict-to-class bind cast: `(T)? expr` is the optional `?` marker
       sitting between the closing paren and the cast operand.  We place it
       outside the parens (rather than `(T?)`) because the latter spelling
       collides with lambda_expr / param_type_list's lookahead during the
       primary_expr TRY chain and corrupts the parse for declarations that
       follow a class cast.  Semantics are identical: the checker still sees
       `(T)` as a cast targeting a class, with the lenient bit threaded
       through expr->lenient_p. */
    int cast_lenient_p = M ('?');
    t = r;
    if (!MP ('{', pos)) {
      P (unary_expr);
      r = new_node2 (c2m_ctx, N_CAST, t, r);
      if (cast_lenient_p) {
        /* Stash a marker that survives until the checker by hanging the
           lenient bit on the N_CAST node itself via a one-shot attr.  We
           reuse the unused `attr` slot here; the checker overwrites it with
           the proper expr struct (and copies lenient_p into expr->lenient_p
           first).  Using a sentinel pointer keeps memory ownership clear. */
        r->attr = (void *) (intptr_t) 1;
      }
    } else {
      P (initializer_list);
      if (!M ('}')) return err_node;
      r = new_pos_node2 (c2m_ctx, N_COMPOUND_LITERAL, pos, t, r);
      PA (post_expr_part, r);
    }
    return r;
  } else if (MP (T_SIZEOF, pos)) {
    if ((r = TRY (par_type_name)) != err_node) {
      r = new_pos_node1 (c2m_ctx, N_SIZEOF, pos, r);
      return r;
    }
    code = N_EXPR_SIZEOF;
  } else if (MP (T_ALIGNOF, pos)) {
    if ((r = TRY (par_type_name)) != err_node) {
      r = new_pos_node1 (c2m_ctx, N_ALIGNOF, pos, r);
    } else {
      P (unary_expr); /* error recovery */
      r = new_pos_node1 (c2m_ctx, N_ALIGNOF, pos, new_node (c2m_ctx, N_IGNORE));
    }
    return r;
  } else if (!MC (T_INCDEC, pos, code) && !MC (T_UNOP, pos, code) && !MC (T_ADDOP, pos, code)
             && !MC ('*', pos, code) && !MC ('&', pos, code)) {
    P (post_expr);
    return r;
  } else if (code == N_AND) {
    code = N_ADDR;
  } else if (code == N_MUL) {
    code = N_DEREF;
  }
  P (unary_expr);
  r = new_pos_node1 (c2m_ctx, code, pos, r);
  return r;
}

static node_t left_op (c2m_ctx_t c2m_ctx, int no_err_p, int token, int token2, nonterm_func_t f) {
  node_code_t code;
  node_t r, n;
  pos_t pos;

  P (f);
  while (MC (token, pos, code) || (token2 >= 0 && MC (token2, pos, code))) {
    n = new_pos_node1 (c2m_ctx, code, pos, r);
    P (f);
    op_append (c2m_ctx, n, r);
    r = n;
  }
  return r;
}

static node_t right_op (c2m_ctx_t c2m_ctx, int no_err_p, int token, int token2, nonterm_func_t left,
                        nonterm_func_t right) {
  node_code_t code;
  node_t r, n;
  pos_t pos;

  P (left);
  if (MC (token, pos, code) || (token2 >= 0 && MC (token2, pos, code))) {
    n = new_pos_node1 (c2m_ctx, code, pos, r);
    P (right);
    op_append (c2m_ctx, n, r);
    r = n;
  }
  return r;
}

D (mul_expr) { return left_op (c2m_ctx, no_err_p, T_DIVOP, '*', unary_expr); }
D (add_expr) { return left_op (c2m_ctx, no_err_p, T_ADDOP, -1, mul_expr); }
D (sh_expr) { return left_op (c2m_ctx, no_err_p, T_SH, -1, add_expr); }
D (rel_expr) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  node_t r, n;
  pos_t pos;
  P (sh_expr);
  for (;;) {
    node_code_t code;
    if (MC (T_CMP, pos, code)) {
      n = new_pos_node1 (c2m_ctx, code, pos, r);
      P (sh_expr);
      op_append (c2m_ctx, n, r);
      r = n;
    } else if (MP_SOFT ("in", pos)) {
      /* "key" in dict  —  dict key-existence check (soft keyword) */
      n = new_pos_node1 (c2m_ctx, N_IN, pos, r);
      P (sh_expr);
      op_append (c2m_ctx, n, r);
      r = n;
    } else
      break;
  }
  return r;
}
D (eq_expr) { return left_op (c2m_ctx, no_err_p, T_EQNE, -1, rel_expr); }
D (and_expr) { return left_op (c2m_ctx, no_err_p, '&', -1, eq_expr); }
D (xor_expr) { return left_op (c2m_ctx, no_err_p, '^', -1, and_expr); }
D (or_expr) { return left_op (c2m_ctx, no_err_p, '|', -1, xor_expr); }
D (land_expr) { return left_op (c2m_ctx, no_err_p, T_ANDAND, -1, or_expr); }
D (lor_expr) { return left_op (c2m_ctx, no_err_p, T_OROR, -1, land_expr); }
/* a ?? b — null-coalescing: binds tighter than ?:, looser than ||, and is
   right-associative (a ?? b ?? c == a ?? (b ?? c)), matching C#. */
D (coalesce_expr) { return right_op (c2m_ctx, no_err_p, T_QQ, -1, lor_expr, coalesce_expr); }

D (cond_expr) {
  node_t r, n;
  pos_t pos;

  P (coalesce_expr);
  if (!MP ('?', pos)) return r;
  n = new_pos_node1 (c2m_ctx, N_COND, pos, r);
  P (expr);
  op_append (c2m_ctx, n, r);
  if (!M (':')) return err_node;
  P (cond_expr);
  op_append (c2m_ctx, n, r);
  return n;
}

#define const_expr cond_expr

D (assign_expr) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  node_t r, n;
  pos_t pos;
  node_code_t code;

  /* detach <expr>  — arena escape.  Soft keyword: only treated specially when
     followed by a parseable assign_expr.  Binds at assign_expr level so the
     keyword wraps the whole RHS:
         return detach (String)"x#" + i;
     parses as `return detach((String)"x#" + i);`, not
     `return (detach (String)"x#") + i;`.  `detach` itself stays usable as an
     ordinary identifier elsewhere via the C_SOFT / rollback mechanism. */
  if (C_SOFT ("detach")) {
    size_t mark = record_start (c2m_ctx);
    pos_t dpos = curr_token->pos;
    node_t inner;
    M_SOFT ("detach");
    /* Guard against ordinary-identifier uses: `detach;`, `detach,`, `detach=`,
       `detach.x`, `detach[i]`, `detach)`.  We DO accept `detach (expr)` and
       `detach (Type)expr` because `(` here starts a parenthesised expression
       or a cast — both are legitimate operands for the keyword (e.g.
       `return detach (String)"x#" + i;`).  A user who declared `detach` as an
       identifier and writes `detach(arg)` will have that re-interpreted as
       `detach(arg)` the keyword form — acceptable, since `detach` is now a
       reserved soft keyword for arena escape. */
    if (!C (';') && !C (',') && !C (')') && !C (']') && !C ('}')
        && !C ('=') && !C (T_ASSIGN) && !C ('.') && !C ('[')
        && (inner = TRY (assign_expr)) != err_node) {
      record_stop (c2m_ctx, mark, FALSE); /* commit */
      return new_pos_node1 (c2m_ctx, N_DETACH, dpos, inner);
    }
    record_stop (c2m_ctx, mark, TRUE); /* rewind: `detach` is an ordinary id */
  }

  /* move <expr>  — transfer single ownership out of an owned binding.  Same
     soft-keyword discipline as `detach`: wraps the whole RHS at assign_expr
     level so `auto y = move x;` parses as `auto y = move(x);`, and `move`
     stays usable as an ordinary identifier elsewhere (rewind on the guard). */
  if (C_SOFT ("move")) {
    size_t mark = record_start (c2m_ctx);
    pos_t dpos = curr_token->pos;
    node_t inner;
    M_SOFT ("move");
    if (!C (';') && !C (',') && !C (')') && !C (']') && !C ('}')
        && !C ('=') && !C (T_ASSIGN) && !C ('.') && !C ('[')
        && (inner = TRY (assign_expr)) != err_node) {
      record_stop (c2m_ctx, mark, FALSE); /* commit */
      return new_pos_node1 (c2m_ctx, N_MOVE, dpos, inner);
    }
    record_stop (c2m_ctx, mark, TRUE); /* rewind: `move` is an ordinary id */
  }

  /* readonly <expr>  — borrow a read-only view of an owned object.  Soft
     keyword, same shape as `move`/`detach`. */
  if (C_SOFT ("readonly")) {
    size_t mark = record_start (c2m_ctx);
    pos_t dpos = curr_token->pos;
    node_t inner;
    M_SOFT ("readonly");
    if (!C (';') && !C (',') && !C (')') && !C (']') && !C ('}')
        && !C ('=') && !C (T_ASSIGN) && !C ('.') && !C ('[')
        && (inner = TRY (assign_expr)) != err_node) {
      record_stop (c2m_ctx, mark, FALSE); /* commit */
      return new_pos_node1 (c2m_ctx, N_READONLY, dpos, inner);
    }
    record_stop (c2m_ctx, mark, TRUE); /* rewind: `readonly` is an ordinary id */
  }

  P (cond_expr);
  if (MC (T_ASSIGN, pos, code) || MC ('=', pos, code)) {
    n = new_pos_node1 (c2m_ctx, code, pos, r);
    if (code == N_ASSIGN && C ('{')) {
      /* dict-literal assignment: lhs = { "k": v, ... }.  An ordinary C
         expression can never start with '{', so this is unambiguous.  The
         braces are parsed with the same initializer_list grammar used for
         dict declarations, producing an N_LIST of N_INIT children. */
      PT ('{');
      P (initializer_list);
      if (M (',')) {
      }
      PT ('}');
    } else {
      P (assign_expr);
    }
    op_append (c2m_ctx, n, r);
    r = n;
  }
  return r;
}
D (expr) { return right_op (c2m_ctx, no_err_p, ',', -1, assign_expr, expr); }

/* Declarations: */
D (attr_spec);
DA (declaration_specs);
D (sc_spec);
DA (type_spec);
D (struct_declaration_list);
D (struct_declaration);
D (spec_qual_list);
D (type_qual);
D (func_spec);
D (align_spec);
D (declarator);
D (direct_declarator);
D (pointer);
D (type_qual_list);
D (param_type_list);
D (id_list);
D (abstract_declarator);
D (direct_abstract_declarator);
D (typedef_name);
D (initializer);
D (st_assert);

D (asm_spec) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  node_t r, id;

  PTN (T_ID);
  if (strcmp (r->u.s.s, "__asm") != 0 && strcmp (r->u.s.s, "__asm__") != 0
      && strcmp (r->u.s.s, "asm") != 0)
    PTFAIL (T_ID);
  id = r;
  PT ('(');
  PTN (T_STR);
  /* Adjacent strings are already concatenated in pre_out; keep a loop so
     `__asm("_" "open")` still works if that path is skipped. */
  while (C (T_STR)) PTN (T_STR);
  PT (')');
  return new_pos_node1 (c2m_ctx, N_ASM, POS (id), r);
}

static void attr_list_append (c2m_ctx_t c2m_ctx, node_t *dst, node_t src) {
  if (src == NULL || src == err_node) return;
  if (*dst == NULL || *dst == err_node) {
    *dst = src;
    return;
  }
  if ((*dst)->code == N_LIST && src->code == N_LIST) {
    for (node_t a = NL_HEAD (src->u.ops); a != NULL; ) {
      node_t next = NL_NEXT (a);
      NL_REMOVE (src->u.ops, a);
      op_append (c2m_ctx, *dst, a);
      a = next;
    }
  } else {
    node_t merged = new_node (c2m_ctx, N_LIST);
    op_append (c2m_ctx, merged, *dst);
    op_append (c2m_ctx, merged, src);
    *dst = merged;
  }
}

static node_t try_attr_spec (c2m_ctx_t c2m_ctx, pos_t pos, node_t *asm_part) {
  node_t r, list = NULL;

  if (c2m_options->pedantic_p) return NULL;
  if (asm_part != NULL) *asm_part = NULL;
  /* GNU/Darwin headers stack several `__attribute__((...))` and/or `__asm`
     after a declarator.  Collect all of them; callers treat err_node as "none". */
  for (;;) {
    if (asm_part != NULL && *asm_part == NULL && (r = TRY (asm_spec)) != err_node) {
      *asm_part = r;
      continue;
    }
    if ((r = TRY (c23_attr_spec)) != err_node) {
      attr_list_append (c2m_ctx, &list, r);
      continue;
    }
    if ((r = TRY (attr_spec)) != err_node) {
      attr_list_append (c2m_ctx, &list, r);
      continue;
    }
    break;
  }
  (void) pos;
  return list != NULL ? list : err_node;
}

D (declaration) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  int typedef_p;
  node_t op, list, decl, spec, r, attrs, asm_part = NULL;
  pos_t pos, last_pos;
  int unowned_p = FALSE;
  pos_t unowned_pos = no_pos;
  int owned_p = FALSE;
  pos_t owned_pos = no_pos;

  /* owned <decl>  — declaration opts INTO the managed, single-owner, move-only
     lifetime layer.  Soft keyword mirroring `unowned`: rewind if the rest does
     not look like a real declaration, so `int owned = 5;` stays legal. */
  if (C_SOFT ("owned")) {
    size_t mark = record_start (c2m_ctx);
    owned_pos = curr_token->pos;
    M_SOFT ("owned");
    if (curr_token->code == T_INT  || curr_token->code == T_CHAR
        || curr_token->code == T_DOUBLE || curr_token->code == T_FLOAT
        || curr_token->code == T_VOID   || curr_token->code == T_STRING
        || curr_token->code == T_LONG   || curr_token->code == T_SHORT
        || curr_token->code == T_UNSIGNED || curr_token->code == T_SIGNED
        || curr_token->code == T_STRUCT || curr_token->code == T_CLASS
        || curr_token->code == T_UNION  || curr_token->code == T_ENUM
        || curr_token->code == T_CONST  || curr_token->code == T_VOLATILE
        || curr_token->code == T_DICT   || curr_token->code == T_BOOL
        || curr_token->code == T_AUTO
        || curr_token->code == T_ID) {
      record_stop (c2m_ctx, mark, FALSE); /* commit */
      owned_p = TRUE;
    } else {
      record_stop (c2m_ctx, mark, TRUE);  /* rewind: ordinary identifier use */
    }
  }

  /* unowned <decl>  — declaration opts out of any future automatic scope-bound
     cleanup.  Today this is a no-op (manual cleanup is still the default), but
     we capture it in the AST as N_UNOWNED so the future auto-defer-delete pass
     can read it.  Soft keyword: rewind if the rest doesn't look like a real
     declaration, so `int unowned = 5;` and similar uses remain legal. */
  if (C_SOFT ("unowned")) {
    size_t mark = record_start (c2m_ctx);
    unowned_pos = curr_token->pos;
    M_SOFT ("unowned");
    /* Heuristic: a declaration after `unowned` must start with something that
       can begin a type — a type keyword, a class/typedef identifier, or one of
       the recognized type-builder soft tokens.  Rewind otherwise. */
    if (curr_token->code == T_INT  || curr_token->code == T_CHAR
        || curr_token->code == T_DOUBLE || curr_token->code == T_FLOAT
        || curr_token->code == T_VOID   || curr_token->code == T_STRING
        || curr_token->code == T_LONG   || curr_token->code == T_SHORT
        || curr_token->code == T_UNSIGNED || curr_token->code == T_SIGNED
        || curr_token->code == T_STRUCT || curr_token->code == T_CLASS
        || curr_token->code == T_UNION  || curr_token->code == T_ENUM
        || curr_token->code == T_CONST  || curr_token->code == T_VOLATILE
        || curr_token->code == T_DICT   || curr_token->code == T_BOOL
        || curr_token->code == T_AUTO
        /* allow `unowned ClassName x = ...` and `unowned List<T>* x = ...` */
        || curr_token->code == T_ID) {
      record_stop (c2m_ctx, mark, FALSE); /* commit */
      unowned_p = TRUE;
    } else {
      record_stop (c2m_ctx, mark, TRUE);  /* rewind: ordinary identifier use */
    }
  }

  if (C (T_STATIC_ASSERT)) {
    P (st_assert);
  } else if (MP (';', pos)) {
    r = new_node (c2m_ctx, N_LIST);
    if (curr_scope == top_scope && c2m_options->pedantic_p)
      warning (c2m_ctx, pos, "extra ; outside of a function");
  } else {
    /* Leading attributes (C23 `[[...]]` or GCC `__attribute__`) appear BEFORE
       the declaration specifiers, e.g. `[[registry("routes")]] static T x`.
       Capture them so they can be merged onto each init-declarator's attrs
       slot (previously the return value was discarded). */
    node_t lead_attrs = try_attr_spec (c2m_ctx, curr_token->pos, NULL);
    if (lead_attrs == err_node) lead_attrs = NULL;
    PA (declaration_specs, curr_scope == top_scope ? (node_t) 1 : NULL);
    spec = r;
    last_pos = POS (spec);

    // Check if this is a class declaration that should be treated like a typedef
    int is_class_typedef = FALSE;
    for (node_t spec_node = NL_HEAD (spec->u.ops); spec_node != NULL; spec_node = NL_NEXT (spec_node)) {
      if (spec_node->code == N_CLASS) {
        node_t class_id = NL_HEAD (spec_node->u.ops);
        if (class_id->code == N_ID) {
          is_class_typedef = TRUE;
          // Add implicit typedef to the spec list
          node_t typedef_node = new_pos_node (c2m_ctx, N_TYPEDEF, POS (spec_node));
          op_prepend (c2m_ctx, spec, typedef_node);
          break;
        }
      }
    }

    /* Parse-time class registry + type_kind: specialization runs before class
       symbols enter check-time scopes, so the by-value gate and ownership
       consult class_type_metas.  [[copyable_no_release]] forces QUIET_VALUE. */
    if (is_class_typedef) {
      int marked = (lead_attrs != NULL && attr_list_has_copyable_no_release (lead_attrs));
      for (node_t spec_node = NL_HEAD (spec->u.ops); spec_node != NULL;
           spec_node = NL_NEXT (spec_node)) {
        if (spec_node->code == N_CLASS) {
          VARR_PUSH (node_t, parsed_classes, spec_node);
          class_type_meta_register (c2m_ctx, spec_node, marked);
          break;
        }
      }
    }

    /* ── Missing-semicolon detection after class definitions ─────────────────
       A class body must be followed by ';' or a declarator.  If the next token
       is instead the unambiguous start of a new declaration (a type keyword,
       storage specifier, or EOF), the author almost certainly forgot the ';'.
       We emit a clear, targeted diagnostic and recover by treating the class as
       if ';' were present, so subsequent declarations continue to parse cleanly
       rather than producing a cascade of confusing secondary errors.

       Example fix:  class Foo { ... }   →   class Foo { ... };          ── */
    if (is_class_typedef && !C (';')) {
      int next_is_new_decl
        = (curr_token->code == T_INT    || curr_token->code == T_CHAR
           || curr_token->code == T_DOUBLE  || curr_token->code == T_FLOAT
           || curr_token->code == T_VOID    || curr_token->code == T_STRING
           || curr_token->code == T_LONG    || curr_token->code == T_SHORT
           || curr_token->code == T_UNSIGNED || curr_token->code == T_SIGNED
           || curr_token->code == T_STRUCT  || curr_token->code == T_CLASS
           || curr_token->code == T_UNION   || curr_token->code == T_ENUM
           || curr_token->code == T_STATIC  || curr_token->code == T_EXTERN
           || curr_token->code == T_INLINE  || curr_token->code == T_TYPEDEF
           || curr_token->code == T_CONST   || curr_token->code == T_VOLATILE
           || curr_token->code == T_DICT    || curr_token->code == T_EOFILE);
      if (next_is_new_decl) {
        /* Extract the class name for a helpful message */
        const char *cname = NULL;
        for (node_t sn = NL_HEAD (spec->u.ops); sn != NULL; sn = NL_NEXT (sn)) {
          if (sn->code == N_CLASS) {
            node_t cid = NL_HEAD (sn->u.ops);
            if (cid && cid->code == N_ID) cname = cid->u.s.s;
            break;
          }
        }
        error (c2m_ctx, POS (spec),
               "missing ';' after definition of class '%s' "
               "-- classyc requires ';' after every class body "
               "(e.g. \"class %s { ... };\")",
               cname ? cname : "<anonymous>",
               cname ? cname : "Name");
        /* Synthesise the missing ';': build an anonymous class declaration
           and return immediately.  We do NOT consume a token because the
           current token is the start of the next real declaration. */
        list = new_node (c2m_ctx, N_LIST);
        op_append (c2m_ctx, list,
                   build_shared_spec_decl (c2m_ctx, last_pos, spec,
                                           new_ignore (c2m_ctx), NULL, NULL, NULL));
        return list;
      }
    }

    list = new_node (c2m_ctx, N_LIST);
    if (C (';')) {
      // For class declarations without declarators, create proper type definition
      if (is_class_typedef) {
        // For class declarations, just mark the declaration part as IGNORE
        // (Unlike structs which might have declarators)
        op_append(c2m_ctx, list, build_shared_spec_decl(c2m_ctx, last_pos, spec, new_ignore(c2m_ctx), NULL, NULL, NULL));
      } else {
        op_append(c2m_ctx, list, new_node5(c2m_ctx, N_SPEC_DECL, spec, new_ignore(c2m_ctx), new_ignore(c2m_ctx), new_ignore(c2m_ctx), new_ignore(c2m_ctx)));
      }
    } else {
      // Regular declarator processing
      for (op = NL_HEAD (spec->u.ops); op != NULL; op = NL_NEXT (op))
        if (op->code == N_TYPEDEF) break;
      typedef_p = op != NULL || is_class_typedef;

      for (;;) { /* init-declarator */
        P (declarator);
        decl = r;
        last_pos = POS (decl);
        assert (decl->code == N_DECL);
        op = NL_HEAD (decl->u.ops);
        tpname_add (c2m_ctx, op, curr_scope, typedef_p);
        attrs = try_attr_spec (c2m_ctx, last_pos, &asm_part);
        if (attrs == err_node) attrs = NULL;
        /* Merge leading attrs (before the specs) with trailing attrs (after the
           declarator).  Both are N_LIST(of N_ATTR); prepend the leading ones. */
        if (lead_attrs != NULL) {
          if (attrs == NULL) {
            attrs = lead_attrs;
          } else {
            for (node_t a = NL_HEAD (lead_attrs->u.ops); a != NULL; ) {
              node_t nexta = NL_NEXT (a);
              NL_REMOVE (lead_attrs->u.ops, a);
              op_append (c2m_ctx, attrs, a);
              a = nexta;
            }
          }
        }
        if (attrs == NULL) attrs = new_node (c2m_ctx, N_IGNORE);
        /* Header-extensible builtins: [[builtin_method("String", "m", "rt", n, "ret")]] */
        if (attrs != NULL && attrs->code == N_LIST)
          register_builtin_methods_from_attrs (c2m_ctx, attrs);
        if (asm_part == NULL) asm_part = new_node (c2m_ctx, N_IGNORE);
        /* Function definition with trailing attrs:
             void f() __attribute__((da_ignore)) { ... }
           TRY(declaration) would consume the attrs then fail on `{` and
           rewind tokens; the free-function path re-parses the declarator
           and would miss da_ignore.  Stash a sticky flag for it. */
        if (C ('{') && attrs != NULL && attr_list_has_da_ignore (attrs))
          parse_ctx->pending_func_da_ignore = 1;
        if (M ('=')) {
          P (initializer);
        } else {
          r = new_node (c2m_ctx, N_IGNORE);
        }
        op_append (c2m_ctx, list,
                   new_pos_node5 (c2m_ctx, N_SPEC_DECL, POS (decl),
                                  new_node1 (c2m_ctx, N_SHARE, spec), decl, attrs, asm_part, r));
        if (!M (',')) break;
      }
    }
    r = list;
    PT (';');
  }
  /* Wrap a committed `unowned` declaration so future passes (auto-defer-delete,
     ownership-flow analysis) can see the opt-out marker.  Today: pass-through. */
  if (unowned_p && r != NULL && r != err_node)
    r = new_pos_node1 (c2m_ctx, N_UNOWNED, unowned_pos, r);
  /* Wrap a committed `owned` declaration so the ownership pass can see the
     managed-lifetime opt-in marker. */
  if (owned_p && r != NULL && r != err_node)
    r = new_pos_node1 (c2m_ctx, N_OWNED, owned_pos, r);
  return r;
}

D (attr) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  node_t r, res, list;

  if (C (')') || C (',')) /* empty */
    return NULL;
  if (FIRST_KW <= (token_code_t) curr_token->code && (token_code_t) curr_token->code <= LAST_KW) {
    token_t kw = curr_token;
    PT (curr_token->code);
    r = new_str_node (c2m_ctx, N_ID, uniq_cstr (c2m_ctx, kw->repr), kw->pos);
  } else {
    PTN (T_ID);
  }
  list = new_node (c2m_ctx, N_LIST);
  res = new_node2 (c2m_ctx, N_ATTR, r, list);
  if (C ('(')) {
    PT ('(');
    while (!C (')')) {
      if (C (T_EOFILE)) PTFAIL (')');
      if (C (T_NUMBER) || C (T_CH) || C (T_STR)) {
        PTN (curr_token->code);
        op_append (c2m_ctx, list, r);
      } else if (C (T_ID)
                 || (FIRST_KW <= (token_code_t) curr_token->code
                     && (token_code_t) curr_token->code <= LAST_KW)) {
        if (FIRST_KW <= (token_code_t) curr_token->code
            && (token_code_t) curr_token->code <= LAST_KW) {
          token_t kw = curr_token;
          PT (curr_token->code);
          r = new_str_node (c2m_ctx, N_ID, uniq_cstr (c2m_ctx, kw->repr), kw->pos);
        } else {
          PTN (T_ID);
        }
        op_append (c2m_ctx, list, r);
        /* GNU keyword-arg: availability(..., introduced=10.10) */
        if (C ('=')) {
          int d = 0;
          M ('=');
          while (!C (T_EOFILE) && (d > 0 || (!C (',') && !C (')')))) {
            if (C ('(')) {
              M ('(');
              d++;
            } else if (C (')')) {
              M (')');
              d--;
            } else {
              read_token (c2m_ctx);
            }
          }
        }
      } else if (C ('(')) {
        int d = 0;
        do {
          if (C ('(')) {
            M ('(');
            d++;
          } else if (C (')')) {
            M (')');
            d--;
          } else if (C (T_EOFILE)) {
            PTFAIL (')');
          } else {
            read_token (c2m_ctx);
          }
        } while (d > 0);
      } else {
        read_token (c2m_ctx);
      }
      if (!C (')') && C (',')) PT (',');
    }
    PT (')');
  }
  return res;
}

D (attr_spec) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  node_t list, r;

  PTN (T_ID);
  /* libc can define empty __attribute__ for non-GNU C compiler -- define also __mirc_attribute__ */
  if (strcmp (r->u.s.s, "__attribute__") != 0 && strcmp (r->u.s.s, "__mirc_attribute__") != 0)
    PTFAIL (T_ID);
  PT ('(');
  PT ('(');
  list = new_node (c2m_ctx, N_LIST);
  for (;;) {
    P (attr);
    op_append (c2m_ctx, list, r);
    if (C (')')) break;
    PT (',');
  }
  PT (')');
  PT (')');
  return list;
}

/* C23 attribute specifier: `[[ attr, attr(args), ... ]]`.  Reuses the D(attr)
   parser (shared with __attribute__) so both spellings produce the same
   N_ATTR(N_ID, N_LIST:(arg)*) node shape.  Wrapped in TRY by callers, so a
   real leading subscript never mis-parses as an attribute. */
D (c23_attr_spec) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  node_t list, r;

  PT ('[');
  PT ('[');   /* second '[' absent => not a C23 attribute; TRY rewinds */
  list = new_node (c2m_ctx, N_LIST);
  for (;;) {
    if (C (']')) break;
    P (attr);
    op_append (c2m_ctx, list, r);
    if (C (']')) break;
    PT (',');
  }
  PT (']');
  PT (']');
  return list;
}

DA (declaration_specs) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  node_t list, r, prev_type_spec = NULL;
  int first_p, auto_seen = FALSE;
  pos_t pos = curr_token->pos, spec_pos;

  list = new_node (c2m_ctx, N_LIST);
  for (first_p = arg == NULL;; first_p = FALSE) {
    spec_pos = curr_token->pos;
    if (C (T_ALIGNAS)) {
      P (align_spec);
    } else if ((r = TRY (sc_spec)) != err_node) {
      if (r != NULL && r->code == N_AUTO) auto_seen = TRUE;
    } else if ((r = TRY (type_qual)) != err_node) {
    } else if ((r = TRY (func_spec)) != err_node) {
    } else if (first_p) {
      PA (type_spec, prev_type_spec);
      prev_type_spec = r;
    } else if ((r = TRY_A (type_spec, prev_type_spec)) != err_node) {
      prev_type_spec = r;
      /* A named class definition with a body is a complete standalone type.
         Stop the type-spec loop here — continuing would greedily consume the
         type keyword from the *next* declaration when a ';' is missing after
         the class body, e.g. "class Foo { } int y;" would eat the `int`. */
      if (r->code == N_CLASS) {
        node_t _cid   = NL_HEAD (r->u.ops);
        node_t _cbody = _cid ? NL_NEXT (_cid) : NULL;
        if (_cid && _cid->code == N_ID && _cbody && _cbody->code != N_IGNORE) {
          op_append (c2m_ctx, list, r);
          break; /* named class with body: stop type-spec accumulation */
        }
      }
    } else if ((r = try_attr_spec (c2m_ctx, spec_pos, FALSE)) != err_node && r != NULL) {
      continue; /* ignore attrs for declaration specs (type attrs) */
    } else
      break;
    op_append (c2m_ctx, list, r);
  }
  if (prev_type_spec == NULL && arg != NULL && !auto_seen) {
    if (c2m_options->pedantic_p) warning (c2m_ctx, pos, "type defaults to int");
    r = new_pos_node (c2m_ctx, N_INT, pos);
    op_append (c2m_ctx, list, r);
  }
  /* `auto x = init;` with no type spec: leave the type unspecified so the
     semantic phase can infer it from the initializer. */
  return list;
}

D (sc_spec) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  node_t r;
  pos_t pos;

  if (MP (T_TYPEDEF, pos)) {
    r = new_pos_node (c2m_ctx, N_TYPEDEF, pos);
  } else if (MP (T_EXTERN, pos)) {
    r = new_pos_node (c2m_ctx, N_EXTERN, pos);
  } else if (MP (T_STATIC, pos)) {
    r = new_pos_node (c2m_ctx, N_STATIC, pos);
  } else if (MP (T_AUTO, pos)) {
    r = new_pos_node (c2m_ctx, N_AUTO, pos);
  } else if (MP (T_REGISTER, pos)) {
    r = new_pos_node (c2m_ctx, N_REGISTER, pos);
  } else if (MP (T_THREAD_LOCAL, pos)) {
    /* Real TLS: MIR_tls_* items + mir_tls_addr (see TLS-IMPLEMENTATION.md). */
    r = new_pos_node (c2m_ctx, N_THREAD_LOCAL, pos);
  } else {
    if (record_level == 0) syntax_error (c2m_ctx, "a storage specifier");
    return err_node;
  }
  return r;
}


/* Build an N_FUNC_DEF for a constructor or destructor parsed inside a class body:
   `void <mangled> (plist) <block>`.  The implicit `this` parameter is prepended
   later (like any other method) and the real ctor/dtor lowering happens in
   check/gen.  `cpos` is the name position and `ppos` the parameter-list pos. */
static node_t build_ctor_dtor_def (c2m_ctx_t c2m_ctx, const char *mangled, node_t plist,
                                   node_t block, pos_t cpos, pos_t ppos) {
  node_t func_node = new_pos_node1 (c2m_ctx, N_FUNC, ppos, plist);
  node_t decl_list = new_node1 (c2m_ctx, N_LIST, func_node);
  node_t id = build_id (c2m_ctx, mangled, cpos);
  node_t declr = new_pos_node2 (c2m_ctx, N_DECL, cpos, id, decl_list);
  node_t spec = new_node1 (c2m_ctx, N_LIST, new_pos_node (c2m_ctx, N_VOID, cpos));
  return build_func_def (c2m_ctx, cpos, spec, declr, new_node (c2m_ctx, N_LIST), block);
}

D (class_member_declaration) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  node_t r, spec, decl;

  if (c2m_options->debug_p) printf("class_member_declaration\n");

  /* Destructor:  ~ClassName ( ) { body }   (no return type, no params).
     Detected when a '~' is followed by the enclosing class name and '('.
     Lowered to a method named "__dtor_<ClassName>" returning void; the implicit
     'this' parameter is prepended later like any other method.  Invoked by
     `delete obj` (and may be invoked automatically via `defer delete obj`). */
  if (parse_ctx->curr_class != NULL && C (T_UNOP)
      && curr_token->node_code == N_BITWISE_NOT) {
    size_t mark = record_start (c2m_ctx);
    pos_t cpos = curr_token->pos;
    M (T_UNOP); /* consume '~' */
    if (C (T_ID) && curr_token->repr != NULL
        && strcmp (curr_token->repr, parse_ctx->curr_class->u.s.s) == 0) {
      M (T_ID); /* consume the class name */
      if (C ('(')) {
        node_t plist;
        pos_t ppos;
        char dtor_name[300];
        record_stop (c2m_ctx, mark, FALSE); /* commit: this is a destructor */
        MP ('(', ppos);
        PT (')'); /* destructors take no parameters */
        plist = new_node (c2m_ctx, N_LIST);
        snprintf (dtor_name, sizeof (dtor_name), "__dtor_%s",
                  parse_ctx->curr_class->u.s.s);
        P (compound_stmt);
        return build_ctor_dtor_def (c2m_ctx, dtor_name, plist, r, cpos, ppos);
      }
    }
    record_stop (c2m_ctx, mark, TRUE); /* not a destructor: rewind */
  }

  /* Constructor:  ClassName ( params? ) { body }   (no return type).
     Detected when an identifier matching the enclosing class name is followed
     by '('.  Lowered to a method named "__ctor_<ClassName>" returning void; the
     implicit 'this' parameter is prepended later like any other method. */
  if (parse_ctx->curr_class != NULL && C (T_ID) && curr_token->repr != NULL
      && strcmp (curr_token->repr, parse_ctx->curr_class->u.s.s) == 0) {
    size_t mark = record_start (c2m_ctx);
    pos_t cpos = curr_token->pos;
    M (T_ID); /* consume the class name */
    if (C ('(')) {
      node_t plist;
      pos_t ppos;
      char ctor_name[300];
      record_stop (c2m_ctx, mark, FALSE); /* commit: this is a constructor */
      MP ('(', ppos);
      if ((r = TRY (param_type_list)) != err_node) {
        plist = r;
      } else {
        P (id_list);
        plist = r;
      }
      PT (')');
      snprintf (ctor_name, sizeof (ctor_name), "__ctor_%s",
                parse_ctx->curr_class->u.s.s);
      P (compound_stmt);
      return build_ctor_dtor_def (c2m_ctx, ctor_name, plist, r, cpos, ppos);
    }
    record_stop (c2m_ctx, mark, TRUE); /* not a constructor: rewind */
  }

  // Try static assertion first
  if (C(T_STATIC_ASSERT)) {
    P(st_assert);
    return r;
  }

  // Check for `static` keyword — static class method (no implicit `this` parameter).
  // We consume it here and inject N_STATIC into the spec list so that
  // check_decl_spec will see static_p = TRUE on the resulting N_FUNC_DEF.
  int is_static_method = FALSE;
  pos_t static_kw_pos;
  if (MP(T_STATIC, static_kw_pos)) {
    is_static_method = TRUE;
  }

  // Parse declaration specifiers (return type, etc.)
  P(spec_qual_list);
  spec = r;

  // If the method was prefixed with `static`, prepend N_STATIC to the spec list
  // so that check_decl_spec sets decl_spec.static_p = TRUE.
  if (is_static_method) {
    node_t static_node = new_pos_node(c2m_ctx, N_STATIC, static_kw_pos);
    NL_PREPEND(spec->u.ops, static_node);
  }

  // Try to parse a declarator
  if ((r = TRY(declarator)) != err_node) {
    decl = r;

    /* Accept trailing cv-qualifiers and/or GCC/C23 attributes on a method
       definition: `Ret name() const __attribute__((...)) {}` or the
       `__attribute__((da_ignore))` used in list.h for the definitely-assigned
       analysis and String-scope out-param warnings.  Attributes are parsed
       via try_attr_spec (GCC + C23).  Only commit when ultimately followed
       by '{' so a malformed data member is still diagnosed below.
       Trailing attrs are KEPT (merged into trail_attrs) so create_decl can
       set decl->da_ignore_p — previously they were parse-and-discard. */
    node_t trail_attrs = NULL;
    {
      size_t tail_mark = record_start(c2m_ctx);
      int saw_tail = 0;
      for (;;) {
        if (C(T_CONST) || C(T_VOLATILE)) {
          while (C(T_CONST) || C(T_VOLATILE)) {
            if (!M(T_CONST)) M(T_VOLATILE);
          }
          saw_tail = 1;
          continue;
        }
        {
          node_t asmp = NULL;
          node_t ar = try_attr_spec(c2m_ctx, curr_token->pos, &asmp);
          if (ar != err_node && (ar != NULL || asmp != NULL)) {
            saw_tail = 1;
            if (ar != NULL && ar != err_node) {
              if (trail_attrs == NULL)
                trail_attrs = ar;
              else {
                /* Flatten multiple __attribute__((...)) into one N_LIST. */
                if (trail_attrs->code == N_LIST && ar->code == N_LIST) {
                  for (node_t a = NL_HEAD (ar->u.ops); a != NULL; ) {
                    node_t next = NL_NEXT (a);
                    NL_REMOVE (ar->u.ops, a);
                    op_append (c2m_ctx, trail_attrs, a);
                    a = next;
                  }
                } else {
                  node_t merged = new_node (c2m_ctx, N_LIST);
                  op_append (c2m_ctx, merged, trail_attrs);
                  op_append (c2m_ctx, merged, ar);
                  trail_attrs = merged;
                }
              }
            }
            continue;
          }
        }
        break;
      }
      if (saw_tail) {
        record_stop(c2m_ctx, tail_mark, !C('{'));
      } else {
        record_stop(c2m_ctx, tail_mark, TRUE);
      }
    }

    // Check if this is a function definition (has a compound statement)
    if (C('{')) {
      /* Recover method-level type params (Select<U>) stashed on the declarator
         by direct_declarator.  Set parse_ctx so nested List<U> stays a placeholder
         while the body is parsed. */
      int meth_n_tp = 0;
      const char *meth_tps[4] = {NULL, NULL, NULL, NULL};
      if (decl->attr != NULL) {
        int *carrier = (int *) decl->attr;
        meth_n_tp = carrier[0];
        const char **carr_tps = (const char **) (carrier + 1);
        for (int i = 0; i < 4; i++) meth_tps[i] = carr_tps[i];
        decl->attr = NULL;
      }
      int saved_mtp = n_method_type_params;
      const char *saved_mtps[4];
      for (int i = 0; i < 4; i++) saved_mtps[i] = method_type_params[i];
      if (meth_n_tp > 0) {
        n_method_type_params = meth_n_tp;
        for (int i = 0; i < 4; i++) method_type_params[i] = meth_tps[i];
      }
      P(compound_stmt);
      node_t body = r;
      n_method_type_params = saved_mtp;
      for (int i = 0; i < 4; i++) method_type_params[i] = saved_mtps[i];

      node_t fdef = build_func_def(c2m_ctx, POS(decl), spec, decl,
                                    new_node(c2m_ctx, N_LIST), body);
      int method_da_ignore
        = (trail_attrs != NULL && attr_list_has_da_ignore (trail_attrs));
      /* Register as a generic method template.  Marked with sentinel so class
         check skips the unsubstitued U body; call sites monomorphize. */
      if (meth_n_tp > 0 && parse_ctx->curr_class != NULL
          && parse_ctx->curr_class->code == N_ID) {
        node_t mid = DECL_ID (decl);
        if (mid != NULL && mid->code == N_ID) {
          generic_method_tmpl_t mt;
          mt.class_name = parse_ctx->curr_class->u.s.s;
          mt.method_name = mid->u.s.s;
          mt.func_node = fdef;
          mt.n_type_params = meth_n_tp;
          for (int i = 0; i < 4; i++) mt.type_params[i] = meth_tps[i];
          mt.is_static = is_static_method;
          mt.da_ignore_p = method_da_ignore;
          VARR_PUSH (generic_method_tmpl_t, generic_method_templates, mt);
          fdef->attr = (void *)((intptr_t)-1); /* template: skip check/gen */
        }
      } else if (method_da_ignore) {
        /* Stash for create_decl → decl->da_ignore_p (see PRECHECK_DA_IGNORE). */
        fdef->attr = PRECHECK_DA_IGNORE;
      } else if (trail_attrs != NULL) {
        fdef->attr = trail_attrs; /* other attrs: create_decl scans for da_ignore */
      }
      return fdef;
    } else {
      // One or more data members sharing `spec`, comma-separated:
      //   type d1 [= init], d2 [= init], ... ;
      // Each declarator becomes its own N_MEMBER; the shared spec is wrapped in
      // N_SHARE by build_shared_member.  Returns an N_LIST that the caller
      // flattens, so a single declaration can yield several members.
      node_t members = new_node (c2m_ctx, N_LIST);
      for (;;) {
        node_t member_init;
        if (M('=')) {
          P(initializer);
          member_init = r;
        } else {
          member_init = new_node(c2m_ctx, N_IGNORE);
        }
        op_append (c2m_ctx, members,
                   build_shared_member (c2m_ctx, POS(decl), spec, decl, NULL, NULL,
                                        member_init));
        if (!M(',')) break;
        P(declarator);
        decl = r;
      }
      PT(';');
      return members;
    }
  }

  // If no declarator, just a type declaration
  PT(';');
  return build_shared_member(c2m_ctx, POS(spec), spec, decl, NULL, NULL, NULL);
}

D (class_member_list) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  node_t list, r;

#ifdef C2MIR_PREPRO_DEBUG
  fprintf(stderr, "class_member_list: class %s\n", parse_ctx->curr_class->u.s.s);
#endif

  list = new_node(c2m_ctx, N_LIST);

  while (!C('}') && !C(T_EOFILE)) {
    P(class_member_declaration);
    /* A data-member declaration may yield several members (comma-separated);
       flatten the returned N_LIST so each becomes a top-level class member. */
    op_flat_append(c2m_ctx, list, r);
  }

  //class_add_this(c2m_ctx, list);

  return list;
}

static struct type *create_type (c2m_ctx_t c2m_ctx, struct type *copy);

static void symbol_insert (c2m_ctx_t c2m_ctx, enum symbol_mode mode, node_t id, node_t scope,
                           node_t def_node, node_t aux_node);
// Updated for T_CLASS
DA (type_spec) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  node_t op1, op2, op3, op4, r;
  int struct_p, id_p = FALSE;
  pos_t pos;

  if (MP (T_VOID, pos)) {
    r = new_pos_node (c2m_ctx, N_VOID, pos);
  } else if (MP (T_CHAR, pos)) {
    r = new_pos_node (c2m_ctx, N_CHAR, pos);
  } else if (MP (T_STRING, pos)) {
    r = new_pos_node (c2m_ctx, N_STRING, pos);
  } else if (MP (T_DICT, pos)) {
    r = new_pos_node (c2m_ctx, N_DICT, pos);
    // dict type will be set to TM_DICT in check_decl_spec / create_type path
  } else if (MP (T_SHORT, pos)) {
    r = new_pos_node (c2m_ctx, N_SHORT, pos);
  } else if (MP (T_INT, pos)) {
    r = new_pos_node (c2m_ctx, N_INT, pos);
  } else if (MP (T_LONG, pos)) {
    r = new_pos_node (c2m_ctx, N_LONG, pos);
  } else if (MP (T_FLOAT, pos)) {
    r = new_pos_node (c2m_ctx, N_FLOAT, pos);
  } else if (MP (T_DOUBLE, pos)) {
    r = new_pos_node (c2m_ctx, N_DOUBLE, pos);
  } else if (MP (T_SIGNED, pos)) {
    r = new_pos_node (c2m_ctx, N_SIGNED, pos);
  } else if (MP (T_UNSIGNED, pos)) {
    r = new_pos_node (c2m_ctx, N_UNSIGNED, pos);
  } else if (MP (T_BOOL, pos) || (arg == NULL && MP_SOFT ("bool", pos))) {
    /* `bool` is a soft keyword: matched as a type specifier only when it is the
       sole type specifier (arg == NULL), so it stays usable as a normal
       identifier elsewhere (e.g. `int bool = 1;`) and is compatible with
       <stdbool.h>. */
    r = new_pos_node (c2m_ctx, N_BOOL, pos);
  } else if (MP (T_COMPLEX, pos)) {
    if (record_level == 0) error (c2m_ctx, pos, "complex numbers are not supported");
    return err_node;
  } else if (MP (T_ATOMIC, pos)) { /* atomic-type-specifier */
    PT ('(');
    P (type_name);
    PT (')');
    error (c2m_ctx, pos, "Atomic0Atomic types are not supported");
  } else if ((struct_p = MP(T_STRUCT, pos) ? 1 : (MP(T_UNION, pos) ? 2 : (MP(T_CLASS, pos) ? 3 : 0)))) {
    /* struct-or-union-or-class-specifier */
    if (!MN (T_ID, op1)) {
      op1 = new_node (c2m_ctx, N_IGNORE);
    } else {
      id_p = TRUE;
    }

    /* Generic class definition: class List<T> { ... }
       Detect <T,...> after the class name and register type params as temp typedefs. */
    int n_type_params = 0;
    const char *type_params[4] = {NULL, NULL, NULL, NULL};
    /* Index into generic_templates for the pre-registration done before body parsing.
       (size_t)-1 means no pre-registration was done. */
    size_t generic_tmpl_preidx = (size_t)-1;
    /* Mark into generic_crossrefs when starting a generic class body: concrete
       nested specialisations (List<String> inside List<T>) push here, and are
       drained once class_node is back-filled. */
    size_t generic_body_xref_mark = 0;
    /* Optional structural-conformance clause (Phase 1): class C impl A, B { ... }.
       Collected as an N_LIST of interface-name N_IDs and attached as the third
       child of the N_CLASS node; conformance is verified structurally in check.
       NULL means no clause was written. */
    node_t impl_list = NULL;
    if (id_p && struct_p == 3 && C (T_CMP) && curr_token->node_code == N_LT) {
      M (T_CMP); /* consume '<' */
      do {
        if (!C (T_ID)) break;
        node_t tp_id; MN (T_ID, tp_id);
        if (n_type_params < 4) {
          type_params[n_type_params++] = tp_id->u.s.s;
          /* Register as a temporary typedef so the class body can reference it */
          tpname_add (c2m_ctx, tp_id, curr_scope, TRUE);
        }
      } while (M (',') && n_type_params < 4);
      if (C (T_CMP) && curr_token->node_code == N_GT) M (T_CMP); /* consume '>' */
    }

    /* `impl Iface1, Iface2` — optional, classes only.  `impl` is a soft keyword
       so it stays usable as an ordinary identifier elsewhere. */
    if (struct_p == 3 && C_SOFT ("impl")) {
      M_SOFT ("impl");
      impl_list = new_pos_node (c2m_ctx, N_LIST, pos);
      do {
        node_t iface_id;
        if (!MN (T_ID, iface_id)) break;
        op_append (c2m_ctx, impl_list, iface_id);
      } while (M (','));
    }

    if (M ('{')) {
      if (!C ('}') && !M (';')) {
        if (struct_p == 3) { // T_CLASS == 3
          node_t last_class = parse_ctx->curr_class;
          parse_ctx->curr_class = op1;
          /* Register the class name as a typedef *before* parsing the body so a
             method/member can refer to the class's own type (e.g. a method that
             returns `ClassName *` for Go-style chaining, or a self-pointer). */
          if (id_p) tpname_add (c2m_ctx, op1, curr_scope, TRUE);
          /* Pre-register the generic template (class_node = NULL placeholder) so
             that self-referential generic types like `List<T>*` used as parameter
             or return types inside the class body are recognised by
             is_generic_class_p() / parse_generic_instantiation() during parsing.
             get_or_create_specialization() detects the NULL class_node and returns
             a mangled placeholder name instead of materialising a real class;
             specialize_node() later resolves the placeholder to the concrete name.

             If a prior `class Name<T>;` forward declaration already registered
             the name with class_node == NULL, complete that slot instead of
             pushing a second template (which would hide the real body). */
          if (n_type_params > 0 && id_p) {
            size_t exist_i = get_generic_template_index (c2m_ctx, op1->u.s.s);
            if (exist_i != (size_t)-1) {
              generic_tmpl_t *exist
                = VARR_ADDR (generic_tmpl_t, generic_templates) + exist_i;
              if (exist->class_node != NULL) {
                error (c2m_ctx, pos, "redefinition of generic class '%s'", op1->u.s.s);
              } else if (exist->n_type_params != n_type_params) {
                error (c2m_ctx, pos,
                       "generic class '%s' forward-declared with %d type parameter(s), "
                       "defined with %d",
                       op1->u.s.s, exist->n_type_params, n_type_params);
              } else {
                /* Completing a forward declaration: reuse the slot, refresh
                   type-param names from this definition. */
                for (int _i = 0; _i < 4; _i++)
                  exist->type_params[_i] = type_params[_i];
                exist->n_type_params = n_type_params;
                generic_tmpl_preidx = exist_i;
              }
            } else {
              generic_tmpl_t pre;
              pre.name        = op1->u.s.s;
              pre.class_node  = NULL; /* back-filled after body is parsed */
              pre.n_type_params = n_type_params;
              for (int _i = 0; _i < 4; _i++) pre.type_params[_i] = type_params[_i];
              VARR_PUSH (generic_tmpl_t, generic_templates, pre);
              generic_tmpl_preidx = VARR_LENGTH (generic_tmpl_t, generic_templates) - 1;
            }
            generic_body_xref_mark = VARR_LENGTH (generic_crossref_t, generic_crossrefs);
          }
          P (class_member_list);
          parse_ctx->curr_class = last_class;
        } else {
          P (struct_declaration_list);
        }
      } else {
        (c2m_options->pedantic_p ? error : warning) (c2m_ctx, pos, "empty struct/union/class");
        r = new_node (c2m_ctx, N_LIST);
      }
      PT ('}');
    } else if (!id_p) {
      return err_node;
    } else if (struct_p == 3 && n_type_params > 0) {
      /* Generic class forward declaration:  class ListView<T>;
         Registers the template name (incomplete, class_node == NULL) so peer
         classes may use ListView<T> as a return/parameter type.  A later
         class ListView<T> { ... } completes the same registry slot. */
      if (id_p) tpname_add (c2m_ctx, op1, curr_scope, TRUE);
      {
        size_t exist_i = get_generic_template_index (c2m_ctx, op1->u.s.s);
        if (exist_i != (size_t)-1) {
          generic_tmpl_t *exist
            = VARR_ADDR (generic_tmpl_t, generic_templates) + exist_i;
          if (exist->n_type_params != n_type_params) {
            error (c2m_ctx, pos,
                   "generic class '%s' redeclared with %d type parameter(s) "
                   "(was %d)",
                   op1->u.s.s, n_type_params, exist->n_type_params);
          }
          /* Redundant forward after forward, or after complete definition: OK. */
        } else {
          generic_tmpl_t tmpl;
          tmpl.name = op1->u.s.s;
          tmpl.class_node = NULL; /* incomplete until body definition */
          tmpl.n_type_params = n_type_params;
          for (int _i = 0; _i < 4; _i++) tmpl.type_params[_i] = type_params[_i];
          VARR_PUSH (generic_tmpl_t, generic_templates, tmpl);
        }
      }
      /* Emit a lightweight N_CLASS with no members so the declaration is a
         valid type-specifier; attr sentinel marks it as a template shell. */
      r = new_node (c2m_ctx, N_IGNORE);
    } else {
      r = new_node (c2m_ctx, N_IGNORE);
    }

    if (struct_p == 1) {
      r = new_pos_node2 (c2m_ctx, N_STRUCT, pos, op1, r);
    } else if (struct_p == 2) {
      r = new_pos_node2 (c2m_ctx, N_UNION, pos, op1, r);
    } else if (struct_p == 3) {
      r = new_pos_node2 (c2m_ctx, N_CLASS, pos, op1, r);
      /* Attach the optional impl clause as the third child (id(0), members(1),
         impl_list(2)).  TAG_ID / TAG_MEMBER_LIST still address ops 0 and 1. */
      if (impl_list != NULL) op_append (c2m_ctx, r, impl_list);
      if (n_type_params > 0) {
        /* Generic class template: store in registry, mark with sentinel attr.
           The base name is registered as a tpname so List<X> can be parsed later.

           Forward declarations (no '{') already registered with class_node NULL
           above; do not overwrite a complete template with this empty shell. */
        if (id_p) tpname_add (c2m_ctx, op1, curr_scope, TRUE);
        if (generic_tmpl_preidx != (size_t)-1) {
          /* Back-fill the class_node into the pre-registered / forward entry. */
          (VARR_ADDR (generic_tmpl_t, generic_templates) + generic_tmpl_preidx)->class_node = r;
          /* Drain specializations that waited on this incomplete template
             (e.g. ListView_String requested while only class ListView<T>;). */
          if (generic_deferred_specs != NULL && id_p) {
            const char *done_name = op1->u.s.s;
            size_t di = 0;
            while (di < VARR_LENGTH (generic_deferred_spec_t, generic_deferred_specs)) {
              generic_deferred_spec_t d
                = VARR_GET (generic_deferred_spec_t, generic_deferred_specs, di);
              if (strcmp (d.base_name, done_name) != 0) {
                di++;
                continue;
              }
              /* Swap-remove then re-request (template is now complete). */
              size_t last = VARR_LENGTH (generic_deferred_spec_t, generic_deferred_specs) - 1;
              if (di != last)
                VARR_SET (generic_deferred_spec_t, generic_deferred_specs, di,
                          VARR_GET (generic_deferred_spec_t, generic_deferred_specs, last));
              VARR_POP (generic_deferred_spec_t, generic_deferred_specs);
              (void) get_or_create_specialization (c2m_ctx, d.base_name, d.n_args,
                                                   d.args, d.pos);
            }
          }
        } else {
          /* Body-less forward already registered; or non-body path.  Only push
             if the name is still unknown (should not happen for n_type_params>0
             with a body — body path always sets preidx). */
          size_t exist_i = get_generic_template_index (c2m_ctx, op1->u.s.s);
          if (exist_i == (size_t)-1) {
            generic_tmpl_t tmpl;
            tmpl.name = op1->u.s.s;
            tmpl.class_node = r;
            tmpl.n_type_params = n_type_params;
            for (int _i = 0; _i < 4; _i++) tmpl.type_params[_i] = type_params[_i];
            VARR_PUSH (generic_tmpl_t, generic_templates, tmpl);
          } else {
            generic_tmpl_t *exist
              = VARR_ADDR (generic_tmpl_t, generic_templates) + exist_i;
            /* Completing definition without preidx only when body path failed
               to set it; if still incomplete and we have a real member list,
               fill in.  Forward-only shell (N_IGNORE members) must not clobber. */
            if (exist->class_node == NULL
                && NL_HEAD (r->u.ops) != NULL
                && TAG_MEMBER_LIST (r) != NULL
                && TAG_MEMBER_LIST (r)->code != N_IGNORE) {
              exist->class_node = r;
            }
          }
        }
        /* Materialise concrete nested specialisations deferred during the body
           (e.g. List<String> referenced from List<T>.SelectString).  class_node
           is now set, so self- and cross-materialisation can deep-copy the
           finished templates.  curr_class must stay cleared so we take the
           normal specialisation path rather than re-queuing placeholders. */
        if (generic_tmpl_preidx != (size_t)-1) {
          node_t _saved_cls = parse_ctx->curr_class;
          parse_ctx->curr_class = NULL;
          while (VARR_LENGTH (generic_crossref_t, generic_crossrefs) > generic_body_xref_mark) {
            generic_crossref_t _cr = VARR_POP (generic_crossref_t, generic_crossrefs);
            (void) get_or_create_specialization (c2m_ctx, _cr.ref_name, _cr.n_args,
                                                 _cr.args, _cr.pos);
          }
          parse_ctx->curr_class = _saved_cls;
        }
        /* Mark the N_CLASS as a template so check/gen can skip it */
        r->attr = (void *)((intptr_t)-1); /* sentinel: template, not a real class */
      } else {
        /* Normal (non-generic) class */
        if (id_p) {
          tpname_add (c2m_ctx, op1, curr_scope, TRUE);
          /* Pre-register the tag at parse so members can mention their own type.
             process_tag accepts the same node later without a redecl error. */
          if (curr_scope != NULL)
            symbol_insert (c2m_ctx, S_TAG, op1, curr_scope, r, new_node (c2m_ctx, N_IGNORE));
        }
      }
    }
  } else if (MP (T_ENUM, pos)) { /* enum-specifier */
    if (!MN (T_ID, op1)) {
      op1 = new_node (c2m_ctx, N_IGNORE);
    } else {
      id_p = TRUE;
    }
    op2 = new_node (c2m_ctx, N_LIST);
    if (M ('{')) { /* enumerator-list */
      for (;;) {   /* enumerator */
        PTN (T_ID);
        op3 = r; /* enumeration-constant */
        if (!M ('=')) {
          op4 = new_node (c2m_ctx, N_IGNORE);
        } else {
          P (const_expr);
          op4 = r;
        }
        op_append (c2m_ctx, op2, new_node2 (c2m_ctx, N_ENUM_CONST, op3, op4));
        if (!M (',')) break;
        if (C ('}')) break;
      }
      PT ('}');
    } else if (!id_p) {
      return err_node;
    } else {
      op2 = new_node (c2m_ctx, N_IGNORE);
    }
    r = new_pos_node2 (c2m_ctx, N_ENUM, pos, op1, op2);
    /* ClassyC ergonomics (C++-like): a *named* enum definition also registers
       as a bare type name so `Faction x` works like `enum Faction x`.  Only
       definitions (with a body) register the name; bare `enum Faction` uses
       still parse via the ENUM keyword.  Classes already do the same via
       tpname_add for their tag. */
    if (id_p && op2->code != N_IGNORE)
      tpname_add (c2m_ctx, op1, curr_scope, TRUE);
  } else if (arg == NULL) {
    /* Any<Interface> erased handle: `Any` is not a registered type name, so
       intercept it here before typedef_name.  On first reference the __Any_<I>
       class is synthesized; the result is its mangled N_ID type name, after
       which the usual pointer/declarator parsing applies (Any<View>* etc.). */
    if (C (T_ID)) {
      size_t any_mark = record_start (c2m_ctx);
      node_t any_id;
      if (MN (T_ID, any_id) && strcmp (any_id->u.s.s, "Any") == 0
          && C (T_CMP) && curr_token->node_code == N_LT) {
        record_stop (c2m_ctx, any_mark, FALSE); /* commit */
        return parse_any_instantiation (c2m_ctx, POS (any_id));
      }
      record_stop (c2m_ctx, any_mark, TRUE); /* not Any<...>: rewind */
    }
    P (typedef_name);
    /* Generic type instantiation: TypeName<TypeArg>
       If the matched type name is a registered generic class and '<' follows,
       monomorphize and return the specialized class name. */
    if (r != err_node && r->code == N_ID
        && is_generic_class_p (c2m_ctx, r->u.s.s)
        && C (T_CMP) && curr_token->node_code == N_LT) {
      r = parse_generic_instantiation (c2m_ctx, r->u.s.s, POS (r));
    }
  } else {
    r = err_node;
  }
  return r;
}

D (struct_declaration_list) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  node_t r, res, el, next_el;

  res = new_node (c2m_ctx, N_LIST);
  for (;;) {
    P (struct_declaration);
    if (r->code != N_LIST) {
      op_append (c2m_ctx, res, r);
    } else {
      for (el = NL_HEAD (r->u.ops); el != NULL; el = next_el) {
        next_el = NL_NEXT (el);
        NL_REMOVE (r->u.ops, el);
        op_append (c2m_ctx, res, el);
      }
    }
    if (C ('}')) break;
  }
  return res;
}

D (struct_declaration) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  node_t list, spec, op, r, attrs;

  if (C (T_STATIC_ASSERT)) {
    P (st_assert);
  } else {
    P (spec_qual_list);
    spec = r;
    list = new_node (c2m_ctx, N_LIST);
    if (M (';')) {
      op = new_pos_node4 (c2m_ctx, N_MEMBER, POS (spec), new_node1 (c2m_ctx, N_SHARE, spec),
                          new_node (c2m_ctx, N_IGNORE), new_node (c2m_ctx, N_IGNORE),
                          new_node (c2m_ctx, N_IGNORE));
      op_append (c2m_ctx, list, op);
    } else {     /* struct-declarator-list */
      for (;;) { /* struct-declarator */
        if (!C (':')) {
          P (declarator);
          attrs = try_attr_spec (c2m_ctx, curr_token->pos, NULL);
          op = r;
        } else {
          attrs = err_node;
          op = new_node (c2m_ctx, N_IGNORE);
        }
        if (attrs == err_node) attrs = new_node (c2m_ctx, N_IGNORE);
        if (M (':')) {
          P (const_expr);
        } else {
          r = new_node (c2m_ctx, N_IGNORE);
        }
        op = new_pos_node4 (c2m_ctx, N_MEMBER, POS (op), new_node1 (c2m_ctx, N_SHARE, spec), op,
                            attrs, r);
        op_append (c2m_ctx, list, op);
        if (!M (',')) break;
      }
      PT (';');
    }
    r = list;
  }
  return r;
}

/* Class bodies are parsed by type_spec -> class_member_list; a standalone
   `class_declaration` external-definition rule used to exist here but never
   produced a real node (every class definition is already handled by
   `declaration`), so it was removed along with its private helpers. */

D (spec_qual_list) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  node_t list, op, r, arg = NULL, at;
  int first_p;

  list = new_node (c2m_ctx, N_LIST);
  for (first_p = TRUE;; first_p = FALSE) {
    if (C (T_CONST) || C (T_RESTRICT) || C (T_VOLATILE) || C (T_ATOMIC)) {
      P (type_qual);
      op = r;
    } else if ((op = TRY_A (type_spec, arg)) != err_node) {
      arg = op;
    } else if ((at = try_attr_spec (c2m_ctx, curr_token->pos, NULL)) != err_node
               && at != NULL) {
      continue; /* GNU: `__attribute__((unused)) long pad;` in a struct */
    } else if (first_p) {
      return err_node;
    } else {
      break;
    }
    op_append (c2m_ctx, list, op);
  }
  return list;
}

D (type_qual) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  node_t r;
  pos_t pos;

  if (MP (T_CONST, pos)) {
    r = new_pos_node (c2m_ctx, N_CONST, pos);
  } else if (MP (T_RESTRICT, pos)) {
    r = new_pos_node (c2m_ctx, N_RESTRICT, pos);
  } else if (MP (T_VOLATILE, pos)) {
    r = new_pos_node (c2m_ctx, N_VOLATILE, pos);
  } else if (MP (T_ATOMIC, pos)) {
    r = new_pos_node (c2m_ctx, N_ATOMIC, pos);
  } else {
    if (record_level == 0) syntax_error (c2m_ctx, "a type qualifier");
    r = err_node;
  }
  return r;
}

D (func_spec) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  node_t r;
  pos_t pos;

  if (MP (T_INLINE, pos)) {
    r = new_pos_node (c2m_ctx, N_INLINE, pos);
  } else if (MP (T_NO_RETURN, pos)) {
    r = new_pos_node (c2m_ctx, N_NO_RETURN, pos);
  } else {
    if (record_level == 0) syntax_error (c2m_ctx, "a function specifier");
    r = err_node;
  }
  return r;
}

D (align_spec) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  node_t r;
  pos_t pos;

  PTP (T_ALIGNAS, pos);
  PT ('(');
  if ((r = TRY (type_name)) != err_node) {
  } else {
    P (const_expr);
  }
  PT (')');
  r = new_pos_node1 (c2m_ctx, N_ALIGNAS, pos, r);
  return r;
}

D (declarator) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  node_t list, p = NULL, r, el, next_el;

  if (C ('*')) {
    P (pointer);
    p = r;
  }
  P (direct_declarator);
  if (p != NULL) {
    list = NL_NEXT (NL_HEAD (r->u.ops));
    assert (list->code == N_LIST);
    for (el = NL_HEAD (p->u.ops); el != NULL; el = next_el) {
      next_el = NL_NEXT (el);
      NL_REMOVE (p->u.ops, el);
      op_append (c2m_ctx, list, el);
    }
  }
  return r;
}

D (direct_declarator) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  node_t list, tql, ae, res, r;
  pos_t pos, static_pos;

  if (MN (T_ID, r)) {
    res = new_node2 (c2m_ctx, N_DECL, r, new_node (c2m_ctx, N_LIST));
    /* Generic function template declarator:  Name<T,...>(params) { ... }
       Detect `<` after the function name and parse a comma-separated list of
       type-parameter identifiers, requiring a matching `>` followed by `(`
       (the parameter list of a function).  The clause is communicated to
       transl_unit via the parse_ctx side channel (n_func_type_params /
       func_type_params) so the N_FUNC_DEF can be registered as a template.
       Speculative: if the shape doesn't match, rewind and treat `<` as an
       ordinary token (e.g. a comparison in an expression-like initializer). */
    if (C (T_CMP) && curr_token->node_code == N_LT) {
      size_t g_mark = record_start (c2m_ctx);
      pos_t g_pos = curr_token->pos;
      int n_tp = 0;
      const char *tps[4] = {NULL, NULL, NULL, NULL};
      M (T_CMP); /* consume '<' */
      int ok = 1;
      do {
        node_t tp_id;
        if (!MN (T_ID, tp_id)) { ok = 0; break; }
        if (n_tp < 4) {
          tps[n_tp++] = tp_id->u.s.s;
          /* Register as a temporary typedef so the function signature and body
             can reference the type parameter by name. */
          tpname_add (c2m_ctx, tp_id, curr_scope, TRUE);
        }
      } while (M (',') && n_tp < 4);
      if (ok && C (T_CMP) && curr_token->node_code == N_GT) {
        M (T_CMP); /* consume '>' */
        if (C ('(')) {
          /* Commit: this is a generic function declarator.  Stash the type
             params on the N_DECL node's attr so transl_unit can recover them
             after the parameter-list parse (which re-enters direct_declarator
             for each parameter and would clobber a parse_ctx side channel). */
          record_stop (c2m_ctx, g_mark, FALSE);
          /* Allocate a small carrier struct in reg-memory so it survives until
             transl_unit reads it.  Layout: { int n; const char *tps[4]; }. */
          int *carrier = reg_malloc (c2m_ctx, sizeof (int) + 4 * sizeof (const char *));
          carrier[0] = n_tp;
          const char **carr_tps = (const char **) (carrier + 1);
          for (int i = 0; i < 4; i++) carr_tps[i] = tps[i];
          res->attr = carrier;
          (void) g_pos;
        } else {
          record_stop (c2m_ctx, g_mark, TRUE); /* rewind */
        }
      } else {
        record_stop (c2m_ctx, g_mark, TRUE); /* rewind */
      }
    }
  } else if (M ('(')) {
    P (declarator);
    res = r;
    PT (')');
  } else {
    /* e.g. `int class = 1;` or `long **(*class)(int)` — `class` is a keyword.
       Note it even under TRY so error_recovery can report a useful message
       after the speculative parse rewinds. */
    note_if_reserved_identifier_token (c2m_ctx);
    return err_node;
  }
  list = NL_NEXT (NL_HEAD (res->u.ops));
  assert (list->code == N_LIST);
  for (;;) {
    if (MP ('(', pos)) {
      if ((r = TRY (param_type_list)) != err_node) {
      } else {
        P (id_list);
      }
      PT (')');
      op_append (c2m_ctx, list, new_pos_node1 (c2m_ctx, N_FUNC, pos, r));
    } else if (M ('[')) {
      int static_p = FALSE;

      if (MP (T_STATIC, static_pos)) {
        static_p = TRUE;
      }
      if (!C (T_CONST) && !C (T_RESTRICT) && !C (T_VOLATILE) && !C (T_ATOMIC)) {
        tql = new_node (c2m_ctx, N_LIST);
      } else {
        P (type_qual_list);
        tql = r;
        if (!static_p && M (T_STATIC)) {
          static_p = TRUE;
        }
      }
      if (static_p) {
        P (assign_expr);
        ae = r;
      } else if (MP ('*', pos)) {
        ae = new_pos_node (c2m_ctx, N_STAR, pos);
      } else if (!C (']')) {
        P (assign_expr);
        ae = r;
      } else {
        ae = new_node (c2m_ctx, N_IGNORE);
      }
      PT (']');
      op_append (c2m_ctx, list,
                 new_node3 (c2m_ctx, N_ARR,
                            static_p ? new_pos_node (c2m_ctx, N_STATIC, static_pos)
                                     : new_node (c2m_ctx, N_IGNORE),
                            tql, ae));
    } else
      break;
  }
  return res;
}

D (pointer) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  node_t op, r, at;
  pos_t pos;

  PTP ('*', pos);
  /* GNU: `void *__attribute__((malloc)) f(void)` — attrs sit after `*`. */
  while ((at = try_attr_spec (c2m_ctx, curr_token->pos, NULL)) != err_node && at != NULL) {
  }
  if (C (T_CONST) || C (T_RESTRICT) || C (T_VOLATILE) || C (T_ATOMIC)) {
    P (type_qual_list);
    while ((at = try_attr_spec (c2m_ctx, curr_token->pos, NULL)) != err_node && at != NULL) {
    }
  } else {
    r = new_node (c2m_ctx, N_LIST);
  }
  op = new_pos_node1 (c2m_ctx, N_POINTER, pos, r);
  if (C ('*')) {
    P (pointer);
  } else {
    r = new_node (c2m_ctx, N_LIST);
  }
  op_append (c2m_ctx, r, op);
  return r;
}

D (type_qual_list) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  node_t list, r;

  list = new_node (c2m_ctx, N_LIST);
  for (;;) {
    P (type_qual);
    op_append (c2m_ctx, list, r);
    if (!C (T_CONST) && !C (T_RESTRICT) && !C (T_VOLATILE) && !C (T_ATOMIC)) break;
  }
  return list;
}

D (param_type_abstract_declarator) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  node_t r = err_node;

  P (abstract_declarator);
  if (C (',') || C (')')) return r;
  return err_node;
}

D (param_type_list) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  node_t list, attrs, op1, op2, r = err_node;
  int comma_p;
  pos_t pos;

  list = new_node (c2m_ctx, N_LIST);
  if (C (')')) return list;
  for (;;) { /* parameter-list, parameter-declaration */
    PA (declaration_specs, NULL);
    op1 = r;
    if (C (',') || C (')')) {
      r = new_node2 (c2m_ctx, N_TYPE, op1,
                     new_node2 (c2m_ctx, N_DECL, new_node (c2m_ctx, N_IGNORE),
                                new_node (c2m_ctx, N_LIST)));
    } else if ((op2 = TRY (param_type_abstract_declarator)) != err_node) {
      /* Try param_type_abstract_declarator first for possible func
         type case ("<res_type> (<typedef_name>)") which can conflict with declarator ("<res_type>
         (<new decl identifier>)")  */
      r = new_node2 (c2m_ctx, N_TYPE, op1, op2);
    } else {
      P (declarator);
      attrs = try_attr_spec (c2m_ctx, curr_token->pos, NULL);
      if (attrs == err_node) attrs = new_node (c2m_ctx, N_IGNORE);
      r = new_pos_node5 (c2m_ctx, N_SPEC_DECL, POS (op2), op1, r, attrs,
                         new_node (c2m_ctx, N_IGNORE), new_node (c2m_ctx, N_IGNORE));
    }
    op_append (c2m_ctx, list, r);
    comma_p = FALSE;
    if (!M (',')) break;
    comma_p = TRUE;
    if (C (T_DOTS)) break;
  }
  if (comma_p) {
    PTP (T_DOTS, pos);
    op_append (c2m_ctx, list, new_pos_node (c2m_ctx, N_DOTS, pos));
  }
  return list;
}

D (id_list) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  node_t list, r;

  list = new_node (c2m_ctx, N_LIST);
  if (C (')')) return list;
  for (;;) {
    PTN (T_ID);
    op_append (c2m_ctx, list, r);
    if (!M (',')) break;
  }
  return list;
}

D (abstract_declarator) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  node_t list, p = NULL, r, el, next_el;

  if (C ('*')) {
    P (pointer);
    p = r;
    if ((r = TRY (direct_abstract_declarator)) == err_node)
      r = new_pos_node2 (c2m_ctx, N_DECL, POS (p), new_node (c2m_ctx, N_IGNORE),
                         new_node (c2m_ctx, N_LIST));
  } else {
    P (direct_abstract_declarator);
  }
  if (p != NULL) {
    list = NL_NEXT (NL_HEAD (r->u.ops));
    assert (list->code == N_LIST);
    for (el = NL_HEAD (p->u.ops); el != NULL; el = next_el) {
      next_el = NL_NEXT (el);
      NL_REMOVE (p->u.ops, el);
      op_append (c2m_ctx, list, el);
    }
  }
  return r;
}

D (par_abstract_declarator) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  node_t r;

  PT ('(');
  P (abstract_declarator);
  PT (')');
  return r;
}

D (direct_abstract_declarator) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  node_t res, list, tql, ae, r;
  pos_t pos, pos2 = no_pos;

  if ((res = TRY (par_abstract_declarator)) != err_node) {
    if (!C ('(') && !C ('[')) return res;
  } else {
    res = new_node2 (c2m_ctx, N_DECL, new_node (c2m_ctx, N_IGNORE), new_node (c2m_ctx, N_LIST));
  }
  list = NL_NEXT (NL_HEAD (res->u.ops));
  assert (list->code == N_LIST);
  for (;;) {
    if (MP ('(', pos)) {
      P (param_type_list);
      PT (')');
      op_append (c2m_ctx, list, new_pos_node1 (c2m_ctx, N_FUNC, pos, r));
    } else {
      PTP ('[', pos);
      if (MP ('*', pos2)) {
        r = new_pos_node3 (c2m_ctx, N_ARR, pos, new_node (c2m_ctx, N_IGNORE),
                           new_node (c2m_ctx, N_IGNORE), new_pos_node (c2m_ctx, N_STAR, pos2));
      } else {
        int static_p = FALSE;

        if (MP (T_STATIC, pos2)) {
          static_p = TRUE;
        }
        if (!C (T_CONST) && !C (T_RESTRICT) && !C (T_VOLATILE) && !C (T_ATOMIC)) {
          tql = new_node (c2m_ctx, N_LIST);
        } else {
          P (type_qual_list);
          tql = r;
          if (!static_p && M (T_STATIC)) {
            static_p = TRUE;
          }
        }
        if (!C (']')) {
          P (assign_expr);
          ae = r;
        } else {
          ae = new_node (c2m_ctx, N_IGNORE);
        }
        r = new_pos_node3 (c2m_ctx, N_ARR, pos,
                           static_p ? new_pos_node (c2m_ctx, N_STATIC, pos2)
                                    : new_node (c2m_ctx, N_IGNORE),
                           tql, ae);
      }
      PT (']');
      op_append (c2m_ctx, list, r);
    }
    if (!C ('(') && !C ('[')) break;
  }
  add_pos (c2m_ctx, res, POS (list));
  return res;
}

D (typedef_name) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  node_t scope, r, def;
  tpname_t tpn;
  symbol_t sym;

  PTN (T_ID);
  for (scope = curr_scope;; scope = scope->attr) {
    if (tpname_find (c2m_ctx, r, scope, &tpn)) {
      //printf("Found type name: %s\n", r->u.s.s);

      if (!tpn.typedef_p) break;
      return r;
    }
    if (scope == NULL) break;
  }
  return err_node;
}

/* Marker on N_LIST nodes that came from `[ e1, e2, ... ]` dict/array literals
   (as opposed to unkeyed brace lists or object `{ "k": v }` lists).  Used by
   gen_dict_init_list so empty `[]` stays an array while empty nested `{}` is an
   object. */
#define DICT_INIT_ARRAY_MARK ((void *) (intptr_t) 0xD1C7A77Au)

D (initializer) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  node_t r;

  if (M ('[')) {
    /* Dict/JSON array literal: [ e1, e2, ... ]  →  N_LIST of N_INIT with empty
       designators, marked DICT_INIT_ARRAY_MARK.  Nested objects/arrays recurse
       through P(initializer).  Used as a value in dict brace-init:
         { "powers": [1, 2, 3], "rows": [ { "a": 1 }, { "a": 2 } ] } */
    node_t list = new_node (c2m_ctx, N_LIST);
    list->attr = DICT_INIT_ARRAY_MARK;
    if (!C (']')) {
      for (;;) {
        node_t empty_des = new_node (c2m_ctx, N_LIST);
        P (initializer);
        op_append (c2m_ctx, list, new_node2 (c2m_ctx, N_INIT, empty_des, r));
        if (!M (',')) break;
        if (C (']')) break;
      }
    }
    PT (']');
    return list;
  } else if (!M ('{')) {
    P (assign_expr);
  } else {
    P (initializer_list);
    if (M (',')) {
    }
    PT ('}');
  }
  return r;
}

D (initializer_list) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  node_t list, list2, r;
  int first_p;

  list = new_node (c2m_ctx, N_LIST);
  if (C ('}')) {
    /* Empty {} is valid for dict (and accepted as a GCC extension for other
       aggregate types).  Only flag it as an error in strict pedantic mode. */
    if (c2m_options->pedantic_p)
      error (c2m_ctx, curr_token->pos, "empty initializer list");
    return list;
  }
  for (;;) { /* designation */
    list2 = new_node (c2m_ctx, N_LIST);
    for (first_p = TRUE;; first_p = FALSE) { /* designator-list, designator */
      if (M ('[')) {
        P (const_expr);
        PT (']');
      } else if (M ('.')) {
        PTN (T_ID);
        r = new_node1 (c2m_ctx, N_FIELD_ID, r);
      } else if (C (T_STR)) {
        /* Only accept a bare string as a dict key when followed by ':'.
           This keeps backward-compatibility with positional initializers
           that contain string constants (e.g. compound literals). */
        size_t mark = record_start (c2m_ctx);
        MN (T_STR, r);
        if (C (':')) {
          r = new_node1 (c2m_ctx, N_FIELD_ID, r);
        } else {
          /* Not a dict key – rewind so the string is treated as a value. */
          record_stop (c2m_ctx, mark, TRUE);
          break;
        }
      } else
        break;
      op_append (c2m_ctx, list2, r);
    }
    if (!first_p) {
      if (!M (':')) PT ('=');
    }
    P (initializer);
    op_append (c2m_ctx, list, new_node2 (c2m_ctx, N_INIT, list2, r));
    if (!M (',')) break;
    if (C ('}')) break;
  }
  return list;
}

D (type_name) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  node_t op, r;

  P (spec_qual_list);
  op = r;
  if (!C (')') && !C (':')) {
    P (abstract_declarator);
  } else {
    r = new_pos_node2 (c2m_ctx, N_DECL, POS (op), new_node (c2m_ctx, N_IGNORE),
                       new_node (c2m_ctx, N_LIST));
  }
  return new_node2 (c2m_ctx, N_TYPE, op, r);
}

D (st_assert) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  node_t op1, r;
  pos_t pos;

  PTP (T_STATIC_ASSERT, pos);
  PT ('(');
  P (const_expr);
  op1 = r;
  PT (',');
  PTN (T_STR);
  PT (')');
  PT (';');
  r = new_pos_node2 (c2m_ctx, N_ST_ASSERT, pos, op1, r);
  return r;
}

/* Statements: */

D (label) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  node_t r, n;
  pos_t pos;

  if (MP (T_CASE, pos)) {
    P (expr);
    n = new_pos_node1 (c2m_ctx, N_CASE, pos, r);
    if (M (T_DOTS)) {
      P (expr);
      op_append (c2m_ctx, n, r);
    }
    r = n;
  } else if (MP (T_DEFAULT, pos)) {
    r = new_pos_node (c2m_ctx, N_DEFAULT, pos);
  } else {
    PTN (T_ID);
    r = new_node1 (c2m_ctx, N_LABEL, r);
  }
  PT (':');
  return r;
}

D (stmt) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  node_t l, n, op1, op2, op3, r;
  pos_t pos;

  l = new_node (c2m_ctx, N_LIST);
  while ((op1 = TRY (label)) != err_node) {
    op_append (c2m_ctx, l, op1);
  }
  /* defer <statement> : record a statement to be executed (in LIFO order) when
     the enclosing block exits.  `defer` is a soft keyword, so it stays usable as
     an ordinary identifier; it is only treated specially when it begins a real
     statement (the token right after it does not look like an operator/`;`). */
  if (C_SOFT ("defer")) {
    size_t mark = record_start (c2m_ctx);
    pos_t dpos = curr_token->pos;
    M_SOFT ("defer");
    if (!C ('=') && !C (';') && !C ('(') && !C ('[') && !C ('.')
        && (op1 = TRY (stmt)) != err_node) {
      record_stop (c2m_ctx, mark, FALSE); /* commit: this is a defer statement */
      return new_pos_node2 (c2m_ctx, N_DEFER, dpos, l, op1);
    }
    record_stop (c2m_ctx, mark, TRUE); /* not a defer statement: rewind */
  }
  /* delete <ptr> ; : run the object's destructor (if any) and free it.  Also a
     soft keyword, committed only when followed by an lvalue expression + ';'. */
  if (C_SOFT ("delete")) {
    size_t mark = record_start (c2m_ctx);
    pos_t dpos = curr_token->pos;
    M_SOFT ("delete");
    if (!C ('=') && !C (';') && (op1 = TRY (unary_expr)) != err_node && C (';')) {
      record_stop (c2m_ctx, mark, FALSE); /* commit: this is a delete statement */
      PT (';');
      return new_pos_node2 (c2m_ctx, N_DELETE, dpos, l, op1);
    }
    record_stop (c2m_ctx, mark, TRUE); /* not a delete statement: rewind */
  }
  /* attach <expr> ; : adopt the value into the current scope's arena.  STUB
     for now — the AST node is recorded and the inner expression is evaluated
     for side effects, but no runtime registration is performed.  Reserved for
     the future dataflow / ownership-check pass; pairs with `detach` as the
     inverse op on the cleanup ledger.  Soft keyword (same rollback discipline
     as `defer`/`delete`). */
  if (C_SOFT ("attach")) {
    size_t mark = record_start (c2m_ctx);
    pos_t apos = curr_token->pos;
    M_SOFT ("attach");
    if (!C ('=') && !C (';') && (op1 = TRY (assign_expr)) != err_node && C (';')) {
      record_stop (c2m_ctx, mark, FALSE); /* commit: this is an attach statement */
      PT (';');
      return new_pos_node2 (c2m_ctx, N_ATTACH, apos, l, op1);
    }
    record_stop (c2m_ctx, mark, TRUE); /* not an attach statement: rewind */
  }
  /* go <call-expr> ; : spawn a fiber running the call (opt-in, -ffibers).
     Soft keyword with the same rollback discipline as defer/delete, so `go`
     stays usable as an ordinary identifier when -ffibers is off (or when an
     operator follows).  The operand must check as a direct plain-function
     call with ≤8 GP-class args (see check N_GO). */
  if (c2m_options->fibers_p && C_SOFT ("go")) {
    size_t mark = record_start (c2m_ctx);
    pos_t gpos = curr_token->pos;
    M_SOFT ("go");
    if (!C ('=') && !C (';') && !C ('(') && !C ('[') && !C ('.')
        && (op1 = TRY (expr)) != err_node && C (';')) {
      record_stop (c2m_ctx, mark, FALSE); /* commit: this is a go statement */
      PT (';');
      return new_pos_node2 (c2m_ctx, N_GO, gpos, l, op1);
    }
    record_stop (c2m_ctx, mark, TRUE); /* not a go statement: rewind */
  }
  /* Detect `go` used as a fiber statement without -ffibers and give a clear
     diagnostic instead of falling through to a confusing "expected ';'" error.
     We preserve the soft-keyword rollback discipline: `go` followed by `=`, `;`,
     `(`, `[`, or `.` is still an ordinary identifier use.  Recover by parsing
     the operand as a normal expression statement so compilation can continue. */
  if (!c2m_options->fibers_p && C_SOFT ("go")) {
    size_t mark = record_start (c2m_ctx);
    pos_t gpos = curr_token->pos;
    M_SOFT ("go");
    if (!C ('=') && !C (';') && !C ('(') && !C ('[') && !C ('.')) {
      error (c2m_ctx, gpos, "use -ffibers to enable `go` / `await` fiber syntax");
      if (C (';')) {
        op1 = new_node (c2m_ctx, N_IGNORE);
      } else {
        P (expr);
        op1 = r;
      }
      record_stop (c2m_ctx, mark, FALSE); /* commit recovery parse */
      PT (';');
      return new_pos_node2 (c2m_ctx, N_EXPR, gpos, l, op1);
    }
    record_stop (c2m_ctx, mark, TRUE); /* not a go statement: rewind */
  }
  /* await [expr] ; : pure cooperative yield point (opt-in, -ffibers).  The
     optional expression is evaluated first (e.g. a try_recv), then the fiber
     yields.  Channel parking itself is explicit (Chan<T> send/recv park
     internally); await is only the scheduler state check + yield. */
  if (c2m_options->fibers_p && C_SOFT ("await")) {
    size_t mark = record_start (c2m_ctx);
    pos_t apos = curr_token->pos;
    M_SOFT ("await");
    if (C (';')) {
      record_stop (c2m_ctx, mark, FALSE); /* commit: bare yield */
      PT (';');
      return new_pos_node1 (c2m_ctx, N_AWAIT, apos, l);
    }
    if (!C ('=') && !C ('(') && !C ('[') && !C ('.')
        && (op1 = TRY (expr)) != err_node && C (';')) {
      record_stop (c2m_ctx, mark, FALSE); /* commit: await <expr> */
      PT (';');
      return new_pos_node2 (c2m_ctx, N_AWAIT, apos, l, op1);
    }
    record_stop (c2m_ctx, mark, TRUE); /* not an await statement: rewind */
  }
  /* Detect `await` used as a fiber statement without -ffibers.  Unlike `go`,
     bare `await;` is valid fiber syntax, so we also catch that case; `await`
     followed by `=`, `(`, `[`, or `.` is still an ordinary identifier use. */
  if (!c2m_options->fibers_p && C_SOFT ("await")) {
    size_t mark = record_start (c2m_ctx);
    pos_t apos = curr_token->pos;
    M_SOFT ("await");
    if (!C ('=') && !C ('(') && !C ('[') && !C ('.')) {
      error (c2m_ctx, apos, "use -ffibers to enable `go` / `await` fiber syntax");
      if (C (';')) {
        op1 = new_node (c2m_ctx, N_IGNORE);
      } else {
        P (expr);
        op1 = r;
      }
      record_stop (c2m_ctx, mark, FALSE); /* commit recovery parse */
      PT (';');
      return new_pos_node2 (c2m_ctx, N_EXPR, apos, l, op1);
    }
    record_stop (c2m_ctx, mark, TRUE); /* not an await statement: rewind */
  }
  if (C ('{')) {
    P (compound_stmt);
    if (NL_HEAD (l->u.ops) != NULL) { /* replace empty label list */
      assert (NL_HEAD (r->u.ops)->code == N_LIST && NL_HEAD (NL_HEAD (r->u.ops)->u.ops) == NULL);
      NL_REMOVE (r->u.ops, NL_HEAD (r->u.ops));
      NL_PREPEND (r->u.ops, l);
    }
  } else if (MP (T_IF, pos)) { /* selection-statement */
    PT ('(');
    P (expr);
    op1 = r;
    PT (')');
    P (stmt);
    op2 = r;
    if (!M (T_ELSE)) {
      r = new_node (c2m_ctx, N_IGNORE);
    } else {
      P (stmt);
    }
    r = new_pos_node4 (c2m_ctx, N_IF, pos, l, op1, op2, r);
  } else if (MP (T_SWITCH, pos)) { /* selection-statement */
    PT ('(');
    P (expr);
    op1 = r;
    PT (')');
    P (stmt);
    r = new_pos_node3 (c2m_ctx, N_SWITCH, pos, l, op1, r);
  } else if (MP (T_WHILE, pos)) { /* iteration-statement */
    PT ('(');
    P (expr);
    op1 = r;
    PT (')');
    P (stmt);
    r = new_pos_node3 (c2m_ctx, N_WHILE, pos, l, op1, r);
  } else if (M (T_DO)) { /* iteration-statement */
    P (stmt);
    op1 = r;
    PTP (T_WHILE, pos);
    PT ('(');
    P (expr);
    PT (')');
    PT (';');
    r = new_pos_node3 (c2m_ctx, N_DO, pos, l, r, op1);
  } else if (MP (T_FOR, pos)) { /* iteration-statement */
    PT ('(');
    /* Check for  for (auto var in expr)  or  for (auto key, val in expr) */
    if (C (T_AUTO)) {
      size_t forin_mark = record_start (c2m_ctx);
      M (T_AUTO);
      node_t var_id = NULL, val_id = NULL;
      int is_forin = FALSE;
      if (C (T_ID)) {
        MN (T_ID, r); var_id = r;
        if (M (',')) {
          /* for (auto key, val in expr) */
          if (C (T_ID)) { MN (T_ID, r); val_id = r; }
        }
        if (M_SOFT ("in")) { is_forin = TRUE; }
      }
      if (is_forin) {
        /* Commit — parse as for-in */
        record_stop (c2m_ctx, forin_mark, FALSE);
        P (expr);
        op1 = r; /* collection expression */
        PT (')');
        P (stmt);
        /* N_FORIN(labels, var_id, val_id_or_ignore, collection, body) */
        n = new_pos_node5 (c2m_ctx, N_FORIN, pos, l, var_id,
                           val_id ? val_id : new_node (c2m_ctx, N_IGNORE),
                           op1, r);
        r = n;
      } else {
        /* Not a for-in — rewind and fall through to normal for */
        record_stop (c2m_ctx, forin_mark, TRUE);
        goto normal_for;
      }
    } else {
    /* ── Typed for-in:  for (<type> id in expr)                       ──
                       for (<type1> id1, <type2> id2 in expr)         ──
       Desugar to an `auto` for-in plus typed copies at the head of the body,
       so the existing array/dict element coercion on assignment performs the
       conversion (no new N_FORIN check/gen paths needed):
         for (String s in arr)        =>  for (auto __v in arr) { String s = __v; <body> }
         for (int i, String s in d)   =>  for (auto __k, __v in d)
                                              { int i = __k; String s = __v; <body> }
       Detection backtracks: if the type/declarator(s) are not followed by the
       soft keyword `in`, we rewind and fall through to the normal for-loop. */
    {
      size_t tf_mark = record_start (c2m_ctx);
      node_t ts1 = declaration_specs (c2m_ctx, TRUE, NULL);
      node_t td1 = (ts1 != err_node) ? declarator (c2m_ctx, TRUE) : err_node;
      node_t ts2 = NULL, td2 = NULL;
      int tok = FALSE, ttwo = FALSE;

      if (ts1 != err_node && td1 != err_node) {
        if (M (',')) {
          ts2 = declaration_specs (c2m_ctx, TRUE, NULL);
          td2 = (ts2 != err_node) ? declarator (c2m_ctx, TRUE) : err_node;
          if (ts2 != err_node && td2 != err_node && C_SOFT ("in")) { tok = TRUE; ttwo = TRUE; }
        } else if (C_SOFT ("in")) {
          tok = TRUE;
        }
      }
      if (tok) {
        node_t coll, fin_k, fin_v, body_items, body_block;
        char kbuf[64], vbuf[64];
        unsigned uid = lambda_uid++;
        record_stop (c2m_ctx, tf_mark, FALSE); /* commit */
        M_SOFT ("in");
        P (expr); coll = r;
        PT (')');
        P (stmt); /* original body */

        /* Always lower to a *two-var* auto for-in.  The value variable then
           carries its natural element/value type (TM_DICT for dicts, the
           element type for arrays / Count-Get collections), independent of the
           single-var dict convention; the typed copies injected at the head of
           the body perform the coercion to the user-declared type(s).  For the
           single-var form the key/index var is an unused throwaway. */
        snprintf (kbuf, sizeof (kbuf), "__forin_k_%u", uid);
        snprintf (vbuf, sizeof (vbuf), "__forin_v_%u", uid);
        fin_k = build_id (c2m_ctx, kbuf, pos);
        fin_v = build_id (c2m_ctx, vbuf, pos);
        body_items = new_node (c2m_ctx, N_LIST);
        if (ttwo) {
          /* T1 id1 = __k;  (object key / array index) */
          op_append (c2m_ctx, body_items,
                     new_pos_node5 (c2m_ctx, N_SPEC_DECL, POS (td1),
                                    new_node1 (c2m_ctx, N_SHARE, ts1), td1,
                                    new_node (c2m_ctx, N_IGNORE), new_node (c2m_ctx, N_IGNORE),
                                    build_id (c2m_ctx, kbuf, pos)));
          /* T2 id2 = __v;  (value / element) */
          op_append (c2m_ctx, body_items,
                     new_pos_node5 (c2m_ctx, N_SPEC_DECL, POS (td2),
                                    new_node1 (c2m_ctx, N_SHARE, ts2), td2,
                                    new_node (c2m_ctx, N_IGNORE), new_node (c2m_ctx, N_IGNORE),
                                    build_id (c2m_ctx, vbuf, pos)));
        } else {
          /* single-var: T id = __v;  (the value / element) */
          op_append (c2m_ctx, body_items,
                     new_pos_node5 (c2m_ctx, N_SPEC_DECL, POS (td1),
                                    new_node1 (c2m_ctx, N_SHARE, ts1), td1,
                                    new_node (c2m_ctx, N_IGNORE), new_node (c2m_ctx, N_IGNORE),
                                    build_id (c2m_ctx, vbuf, pos)));
        }
        op_append (c2m_ctx, body_items, r); /* original body last */
        body_block = new_pos_node2 (c2m_ctx, N_BLOCK, pos, new_node (c2m_ctx, N_LIST),
                                    body_items);
        body_block->attr = NULL; /* scope established during check */
        n = new_pos_node5 (c2m_ctx, N_FORIN, pos, l, fin_k, fin_v, coll, body_block);
        r = n;
        goto forin_typed_done;
      }
      record_stop (c2m_ctx, tf_mark, TRUE); /* rewind: not a typed for-in */
    }
    normal_for:;
    n = new_pos_node (c2m_ctx, N_FOR, pos);
    n->attr = curr_scope;
    curr_scope = n;
    if ((r = TRY (declaration)) != err_node) {
      op1 = r;
      curr_scope = n->attr;
    } else {
      curr_scope = n->attr;
      if (!M (';')) {
        P (expr);
        op1 = r;
        PT (';');
      } else {
        op1 = new_node (c2m_ctx, N_IGNORE);
      }
    }
    if (M (';')) {
      op2 = new_node (c2m_ctx, N_IGNORE);
    } else {
      P (expr);
      op2 = r;
      PT (';');
    }
    if (C (')')) {
      op3 = new_node (c2m_ctx, N_IGNORE);
    } else {
      P (expr);
      op3 = r;
    }
    PT (')');
    P (stmt);
    op_append (c2m_ctx, n, l);
    op_append (c2m_ctx, n, op1);
    op_append (c2m_ctx, n, op2);
    op_append (c2m_ctx, n, op3);
    op_append (c2m_ctx, n, r);
    r = n;
    forin_typed_done:;
    } /* end normal_for else */
  } else if (MP (T_GOTO, pos)) { /* jump-statement */
    int indirect_p = FALSE;
    if (!M ('*')) {
      PTN (T_ID);
    } else {
      indirect_p = TRUE;
      P (expr);
    }
    PT (';');
    r = new_pos_node2 (c2m_ctx, indirect_p ? N_INDIRECT_GOTO : N_GOTO, pos, l, r);
  } else if (MP (T_CONTINUE, pos)) { /* continue-statement */
    PT (';');
    r = new_pos_node1 (c2m_ctx, N_CONTINUE, pos, l);
  } else if (MP (T_BREAK, pos)) { /* break-statement */
    PT (';');
    r = new_pos_node1 (c2m_ctx, N_BREAK, pos, l);
  } else if (MP (T_RETURN, pos)) { /* return-statement */
    if (M (';')) {
      r = new_node (c2m_ctx, N_IGNORE);
    } else {
      P (expr);
      PT (';');
    }
    r = new_pos_node2 (c2m_ctx, N_RETURN, pos, l, r);
/* ── Exception: throw(id_expr) / throw(id_expr, msg_expr) ─────── */
  } else if (C_SOFT("throw")) {
    size_t mark = record_start(c2m_ctx);

    M_SOFT("throw");
    pos_t tpos = curr_token->pos;

    if (!C('(')) {
        error(c2m_ctx, tpos,
              "throw must be written as throw(id, msg);");
        record_stop(c2m_ctx, mark, TRUE);
        return err_node;
    }
    /* Use assign_expr (not expr) so the top-level ',' separates the two
       arguments rather than being parsed as a comma-operator expression. */
    PT('('); P(assign_expr); op1 = r;          /* id expr  */
    if (M(',')) { P(assign_expr); op2 = r; }   /* msg expr */
    else      op2 = new_node(c2m_ctx, N_IGNORE);
    PT(')'); PT(';');
    record_stop(c2m_ctx, mark, FALSE);
    r = new_pos_node3(c2m_ctx, N_THROW, tpos, l, op1, op2);
/* ── Exception: try { body } catch(Class? var) { handler } ... ──
   Layout: N_TRY(labels, body_block, N_LIST:(N_CATCH)+)
           N_CATCH(class_sel | N_IGNORE, var_id, handler_block)
   Each handler block gets a synthesized leading `Exception <var>;` decl. */
  } else if (C_SOFT("try")) {
    pos_t tpos = curr_token->pos;
    node_t body_block, catch_list;

    M_SOFT("try");
    if (!C('{')) { syntax_error(c2m_ctx, "expected '{' after try"); return err_node; }
    P(compound_stmt);                 /* try-body block (own scope) */
    body_block = r;
    catch_list = new_pos_node(c2m_ctx, N_LIST, tpos);
    if (!C_SOFT("catch")) {
      syntax_error(c2m_ctx, "expected 'catch' after try block");
      return err_node;
    }
    while (C_SOFT("catch")) {          /* one or more catch clauses */
      node_t class_sel, var_id, handler_block, specs, declr, sd, blist;
      const char *vname; pos_t vpos;

      M_SOFT("catch");
      PT('(');
      if (curr_token->code != T_ID) {
        syntax_error(c2m_ctx, "expected identifier in catch(...)");
        return err_node;
      }
      { /* First id is either a class selector or, alone, the variable. */
        const char *n1 = curr_token->repr; pos_t p1 = curr_token->pos;
        read_token(c2m_ctx);
        if (curr_token->code == T_ID) {            /* "Class var" */
          class_sel = build_id(c2m_ctx, n1, p1);
          vname = curr_token->repr; vpos = curr_token->pos;
          read_token(c2m_ctx);
        } else {                                   /* "var" -> catch-all */
          class_sel = new_node(c2m_ctx, N_IGNORE);
          vname = n1; vpos = p1;
        }
      }
      var_id = build_id(c2m_ctx, vname, vpos);
      PT(')');
      if (!C('{')) { syntax_error(c2m_ctx, "expected '{' after catch(...)"); return err_node; }
      P(compound_stmt);               /* handler block (own scope) */
      handler_block = r;
      /* Prepend "Exception <var>;" so the handler can read e.id / e.msg. */
      specs = new_node(c2m_ctx, N_LIST);
      op_append(c2m_ctx, specs, build_id(c2m_ctx, "Exception", vpos));
      declr = new_pos_node2(c2m_ctx, N_DECL, vpos, build_id(c2m_ctx, vname, vpos),
                            new_node(c2m_ctx, N_LIST));
      sd = build_spec_decl(c2m_ctx, vpos, specs, declr, NULL, NULL, NULL);
      blist = NL_EL(handler_block->u.ops, 1);    /* N_BLOCK: [labels, items] */
      op_prepend(c2m_ctx, blist, sd);
      op_append(c2m_ctx, catch_list,
                new_pos_node3(c2m_ctx, N_CATCH, tpos, class_sel, var_id, handler_block));
    }
    r = new_pos_node3(c2m_ctx, N_TRY, tpos, l, body_block, catch_list);
  } else { /* expression-statement */
    if (C (';')) {
      r = new_node (c2m_ctx, N_IGNORE);
    } else {
      P (expr);
    }
    PT (';');
    r = new_pos_node2 (c2m_ctx, N_EXPR, POS (r), l, r);
  }
  return r;
}

static void error_recovery (c2m_ctx_t c2m_ctx, int par_lev, const char *expected) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;

  /* Prefer "class is reserved" over "syntax error on struct" after TRY rewind. */
  if (!report_reserved_as_identifier (c2m_ctx)) syntax_error (c2m_ctx, expected);
  if (c2m_options->debug_p) fprintf (stderr, "error recovery: skipping");
  for (;;) {
    if (curr_token->code == T_EOFILE || (par_lev == 0 && curr_token->code == ';')) break;
    if (curr_token->code == '{') {
      par_lev++;
    } else if (curr_token->code == '}') {
      if (--par_lev <= 0) break;
    }
    if (c2m_options->debug_p)
      fprintf (stderr, " %s(%d:%d)", get_token_name (c2m_ctx, curr_token->code),
               curr_token->pos.lno, curr_token->pos.ln_pos);
    read_token (c2m_ctx);
  }
  if (c2m_options->debug_p) fprintf (stderr, " %s\n", get_token_name (c2m_ctx, curr_token->code));
  if (curr_token->code != T_EOFILE) read_token (c2m_ctx);
}

D (compound_stmt) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  node_t n, list, r;
  pos_t pos;

  PTE ('{', pos, err0);
  list = new_node (c2m_ctx, N_LIST);
  n = new_pos_node2 (c2m_ctx, N_BLOCK, pos, new_node (c2m_ctx, N_LIST), list);
  n->attr = curr_scope;
  curr_scope = n;
  while (!C ('}') && !C (T_EOFILE)) { /* block-item-list, block_item */
    if ((r = TRY (declaration)) != err_node) {
    } else {
      PE (stmt, err1);
    }
    op_flat_append (c2m_ctx, list, r);
    continue;
  err1:
    error_recovery (c2m_ctx, 1, "<statement>");
  }
  curr_scope = n->attr;
  if (C (T_EOFILE)) {
    error (c2m_ctx, pos, "unfinished compound statement");
    return err_node;
  }
  PT ('}');
  return n;
err0:
  error_recovery (c2m_ctx, 0, "{");
  return err_node;
}

/* ─────────────────────────── Interfaces (Phase 1) ───────────────────────── */

/* Register an interface contract by name (idempotent; first definition wins). */
static void register_interface (c2m_ctx_t c2m_ctx, const char *name, node_t node) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  iface_t e;

  if (interfaces == NULL) return;
  for (size_t i = 0; i < VARR_LENGTH (iface_t, interfaces); i++)
    if (strcmp (VARR_GET (iface_t, interfaces, i).name, name) == 0) return;
  e.name = name;
  e.node = node;
  VARR_PUSH (iface_t, interfaces, e);
  if (c2m_options->debug_p)
    fprintf (stderr, "interface registered: %s (%lu method(s))\n", name,
             (unsigned long) NL_LENGTH (TAG_MEMBER_LIST (node)->u.ops));
}

/* Look up a registered interface by name; returns its N_INTERFACE node or NULL. */
static node_t find_interface (c2m_ctx_t c2m_ctx, const char *name) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;

  if (interfaces == NULL || name == NULL) return NULL;
  for (size_t i = 0; i < VARR_LENGTH (iface_t, interfaces); i++)
    if (strcmp (VARR_GET (iface_t, interfaces, i).name, name) == 0)
      return VARR_GET (iface_t, interfaces, i).node;
  return NULL;
}

/* ───────────────────── Any<I> erased-handle synthesis (Phase 2) ──────────── */

/* Map a primitive type-specifier node code to its C keyword (NULL if not one). */
static const char *any_basic_type_kw (node_code_t c) {
  switch (c) {
  case N_VOID:     return "void";
  case N_CHAR:     return "char";
  case N_SHORT:    return "short";
  case N_INT:      return "int";
  case N_LONG:     return "long";
  case N_FLOAT:    return "float";
  case N_DOUBLE:   return "double";
  case N_SIGNED:   return "signed";
  case N_UNSIGNED: return "unsigned";
  case N_BOOL:     return "bool";
  case N_STRING:   return "String";
  case N_DICT:     return "dict";
  default:         return NULL;
  }
}

/* Append the C source for a type-specifier list (the return type of a method or
   the type of a parameter) to SB.  Class names ride through as plain N_IDs. */
static void any_append_specs (c2m_ctx_t c2m_ctx MIR_UNUSED, VARR (char) * sb, node_t spec_list) {
  int first = 1;
  if (spec_list != NULL && spec_list->code == N_SHARE) spec_list = NL_HEAD (spec_list->u.ops);
  if (spec_list != NULL && spec_list->code == N_LIST)
    for (node_t s = NL_HEAD (spec_list->u.ops); s != NULL; s = NL_NEXT (s)) {
      const char *kw = (s->code == N_ID) ? s->u.s.s : any_basic_type_kw (s->code);
      if (kw == NULL) continue;
      if (!first) VARR_PUSH (char, sb, ' ');
      for (const char *p = kw; *p != '\0'; p++) VARR_PUSH (char, sb, *p);
      first = 0;
    }
  if (first)
    for (const char *p = "void"; *p != '\0'; p++) VARR_PUSH (char, sb, *p);
}

/* Number of pointer levels (N_POINTER nodes) in a declarator decoration list. */
static int any_ptr_depth (node_t decl_list) {
  int n = 0;
  if (decl_list != NULL && decl_list->code == N_LIST)
    for (node_t d = NL_HEAD (decl_list->u.ops); d != NULL; d = NL_NEXT (d))
      if (d->code == N_POINTER) n++;
  return n;
}

/* TRUE if a spec list denotes exactly `void` (used for void-return detection). */
static int any_specs_are_void (node_t spec_list) {
  node_t s;
  if (spec_list != NULL && spec_list->code == N_SHARE) spec_list = NL_HEAD (spec_list->u.ops);
  if (spec_list == NULL || spec_list->code != N_LIST) return 0;
  s = NL_HEAD (spec_list->u.ops);
  return s != NULL && s->code == N_VOID && NL_NEXT (s) == NULL;
}

/* Extract method shape from an interface N_MEMBER.  Returns 1 and fills the out
   params (return-type spec list, method-name N_ID, return pointer depth, and the
   parameter N_LIST) for a function prototype member; 0 otherwise. */
static int any_extract_method (node_t m, node_t *spec_out, node_t *mid_out,
                               int *ret_ptr_out, node_t *plist_out) {
  node_t spec, declr, mid, decl_list, func = NULL;
  int ret_ptr = 0;

  if (m->code != N_MEMBER) return 0;
  spec = NL_HEAD (m->u.ops);
  declr = NL_EL (m->u.ops, 1);
  if (declr == NULL || declr->code != N_DECL) return 0;
  mid = NL_HEAD (declr->u.ops);
  if (mid == NULL || mid->code != N_ID) return 0;
  decl_list = NL_NEXT (mid);
  if (decl_list != NULL && decl_list->code == N_LIST)
    for (node_t d = NL_HEAD (decl_list->u.ops); d != NULL; d = NL_NEXT (d)) {
      if (d->code == N_POINTER) ret_ptr++;
      else if (d->code == N_FUNC) func = d;
    }
  if (func == NULL) return 0;
  *spec_out = spec;
  *mid_out = mid;
  *ret_ptr_out = ret_ptr;
  *plist_out = NL_HEAD (func->u.ops);
  return 1;
}

/* TRUE if a parameter N_LIST means "no parameters": empty, or a single abstract
   `void` (i.e. `f(void)`). */
static int any_plist_empty_p (node_t plist) {
  node_t p, pspec, pdeclr, pid, pdecorate;

  if (plist == NULL || plist->code != N_LIST) return 1;
  p = NL_HEAD (plist->u.ops);
  if (p == NULL) return 1;
  if (NL_NEXT (p) != NULL || (p->code != N_TYPE && p->code != N_SPEC_DECL)) return 0;
  pspec = NL_HEAD (p->u.ops);
  pdeclr = NL_EL (p->u.ops, 1);
  pid = (pdeclr != NULL && pdeclr->code == N_DECL) ? NL_HEAD (pdeclr->u.ops) : NULL;
  pdecorate = (pid != NULL) ? NL_NEXT (pid) : NULL;
  return any_specs_are_void (pspec) && (pid == NULL || pid->code == N_IGNORE)
         && any_ptr_depth (pdecorate) == 0;
}

/* Append the source for one parameter (its type plus pointer stars) to SB and,
   when NAME_OUT is non-NULL, also append " __aIDX" so accessor declarations get
   synthesized parameter names. */
static void any_append_param (c2m_ctx_t c2m_ctx, VARR (char) * sb, node_t pn, int idx,
                              int with_name) {
  node_t pspec, pdeclr, pdecorate;
  int p_ptr;
  char nb[32];

  pspec = NL_HEAD (pn->u.ops);
  pdeclr = NL_EL (pn->u.ops, 1);
  pdecorate = (pdeclr != NULL && pdeclr->code == N_DECL) ? NL_NEXT (NL_HEAD (pdeclr->u.ops)) : NULL;
  p_ptr = any_ptr_depth (pdecorate);
  any_append_specs (c2m_ctx, sb, pspec);
  for (int i = 0; i < p_ptr; i++) VARR_PUSH (char, sb, '*');
  if (with_name) {
    snprintf (nb, sizeof (nb), " __a%d", idx);
    for (const char *p = nb; *p != '\0'; p++) VARR_PUSH (char, sb, *p);
  }
}

/* Lex a complete ClassyC source fragment SRC into tokens appended to
   recorded_tokens; returns the index of the first appended token.  Reuses the
   string-stream re-lexer (same machinery as f-string expansion / ## concat). */
static size_t lex_source_into_tokens (c2m_ctx_t c2m_ctx, const char *src, pos_t pos) {
  MIR_alloc_t alloc = c2m_alloc (c2m_ctx);
  VARR (char) * buf;
  size_t start = VARR_LENGTH (token_t, recorded_tokens);

  VARR_CREATE (char, buf, alloc, 256);
  for (const char *p = src; *p != '\0'; p++) VARR_PUSH (char, buf, *p);
  VARR_PUSH (char, buf, '\0');
  reverse (buf); /* the string-stream consumer reads characters in reverse */
  set_string_stream (c2m_ctx, VARR_ADDR (char, buf), pos, NULL);
  for (;;) {
    token_t pt = get_next_pptoken (c2m_ctx);
    token_t cv;
    if (pt->code == T_EOFILE || pt->code == T_EOU) break;
    if ((cv = pptoken2token (c2m_ctx, pt, TRUE)) == NULL) continue;
    VARR_PUSH (token_t, recorded_tokens, cv);
  }
  VARR_DESTROY (char, buf);
  return start;
}

/* Step 2.1 — synthesize the erased handle class __Any_<I> on first reference.
   Layout: `void* data; void (*dtor)(void*);` plus one `(*<m>_fn)(...)` slot per
   interface method, one accessor method per slot that forwards through it, and a
   destructor that calls dtor.  Built as ClassyC source and reparsed through the
   normal class path so all existing layout/scope/check/gen machinery applies.
   Returns an N_ID naming the synthesized class. */
static node_t synthesize_any_class (c2m_ctx_t c2m_ctx, const char *iface_name, pos_t pos) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  MIR_alloc_t alloc = c2m_alloc (c2m_ctx);
  char struct_name[256];
  const char *struct_name_u;
  node_t iface, members, cls;
  VARR (char) * sb;
  size_t tstart, saved_idx;
  token_t saved_tok;
  node_t saved_scope, saved_class;

  snprintf (struct_name, sizeof (struct_name), "__Any_%s", iface_name);
  struct_name_u = uniq_cstr (c2m_ctx, struct_name).s;

  /* Dedup: the handle is synthesized once per interface (cached in the generic
     specialization registry, keyed by the mangled class name). */
  for (size_t i = 0; i < VARR_LENGTH (generic_spec_t, generic_specs); i++)
    if (strcmp (VARR_GET (generic_spec_t, generic_specs, i).spec_name, struct_name_u) == 0)
      return build_id (c2m_ctx, struct_name_u, pos);

  iface = find_interface (c2m_ctx, iface_name);
  if (iface == NULL || iface->code != N_INTERFACE) {
    error (c2m_ctx, pos, "Any<%s>: '%s' is not a declared interface", iface_name, iface_name);
    return build_id (c2m_ctx, struct_name_u, pos);
  }
  members = TAG_MEMBER_LIST (iface);

#define SB_PUTS(s) varr_str_push (sb, (s))

  VARR_CREATE (char, sb, alloc, 1024);
  SB_PUTS ("class ");
  SB_PUTS (struct_name_u);
  SB_PUTS (" {\n");
  SB_PUTS ("void* data;\n");
  SB_PUTS ("void (*dtor)(void*);\n");

  /* One function-pointer slot per interface method:
        <ret> (*<name>_fn)(void* [, <ptype>...]); */
  for (node_t m = NL_HEAD (members->u.ops); m != NULL; m = NL_NEXT (m)) {
    node_t spec, mid, plist;
    int ret_ptr, idx = 0;
    if (!any_extract_method (m, &spec, &mid, &ret_ptr, &plist)) continue;
    any_append_specs (c2m_ctx, sb, spec);
    VARR_PUSH (char, sb, ' ');
    for (int p = 0; p < ret_ptr; p++) VARR_PUSH (char, sb, '*');
    SB_PUTS ("(*");
    SB_PUTS (mid->u.s.s);
    SB_PUTS ("_fn)(void*");
    if (!any_plist_empty_p (plist))
      for (node_t pn = NL_HEAD (plist->u.ops); pn != NULL; pn = NL_NEXT (pn)) {
        if (pn->code != N_SPEC_DECL && pn->code != N_TYPE) continue;
        SB_PUTS (", ");
        any_append_param (c2m_ctx, sb, pn, idx++, FALSE);
      }
    SB_PUTS (");\n");
  }

  /* One accessor method per interface method, forwarding through its slot:
        <ret> <name>(<params>) { [return] this.<name>_fn(this.data [, __aN...]); } */
  for (node_t m = NL_HEAD (members->u.ops); m != NULL; m = NL_NEXT (m)) {
    node_t spec, mid, plist;
    int ret_ptr, idx, nparams = 0, is_void;
    if (!any_extract_method (m, &spec, &mid, &ret_ptr, &plist)) continue;
    is_void = (ret_ptr == 0 && any_specs_are_void (spec));
    any_append_specs (c2m_ctx, sb, spec);
    VARR_PUSH (char, sb, ' ');
    for (int p = 0; p < ret_ptr; p++) VARR_PUSH (char, sb, '*');
    SB_PUTS (mid->u.s.s);
    VARR_PUSH (char, sb, '(');
    idx = 0;
    if (!any_plist_empty_p (plist))
      for (node_t pn = NL_HEAD (plist->u.ops); pn != NULL; pn = NL_NEXT (pn)) {
        if (pn->code != N_SPEC_DECL && pn->code != N_TYPE) continue;
        if (idx > 0) SB_PUTS (", ");
        any_append_param (c2m_ctx, sb, pn, idx, TRUE);
        idx++;
      }
    nparams = idx;
    SB_PUTS (") { ");
    if (is_void) {
      SB_PUTS ("if (this.");
      SB_PUTS (mid->u.s.s);
      SB_PUTS ("_fn) this.");
    } else {
      SB_PUTS ("return this.");
    }
    SB_PUTS (mid->u.s.s);
    SB_PUTS ("_fn(this.data");
    for (int a = 0; a < nparams; a++) {
      char nb[32];
      snprintf (nb, sizeof (nb), ", __a%d", a);
      SB_PUTS (nb);
    }
    SB_PUTS ("); }\n");
  }

  /* Destructor: owns the concrete object, freeing it through dtor. */
  SB_PUTS ("~");
  SB_PUTS (struct_name_u);
  SB_PUTS ("() { if (this.dtor) this.dtor(this.data); }\n");
  SB_PUTS ("};\n");
  /* Per-interface destructor thunk used by the object arena: `delete handle`
     runs ~__Any_I (freeing the wrapped concrete) then frees the handle. */
  SB_PUTS ("static void __anyfree_");
  SB_PUTS (iface_name);
  SB_PUTS ("(void* __p) { delete (");
  SB_PUTS (struct_name_u);
  SB_PUTS ("*)__p; }\n;\n"); /* trailing ';' is spare lookahead */
  VARR_PUSH (char, sb, '\0');

  if (c2m_options->debug_p)
    fprintf (stderr, "=== synthesized Any<%s> ===\n%s\n", iface_name, VARR_ADDR (char, sb));

  /* Lex the synthesized source and reparse it (the handle class plus its arena
     destructor thunk), with a fresh top-level parse scope so they register
     globally. */
  tstart = lex_source_into_tokens (c2m_ctx, VARR_ADDR (char, sb), pos);
  VARR_DESTROY (char, sb);

  saved_idx = next_token_index;
  saved_tok = curr_token;
  saved_scope = curr_scope;
  saved_class = parse_ctx->curr_class;
  curr_scope = NULL;
  parse_ctx->curr_class = NULL;
  next_token_index = tstart;
  read_token (c2m_ctx);
  cls = new_node (c2m_ctx, N_LIST);
  while (!C (';') && !C (T_EOFILE)) {
    node_t item = TRY (declaration);
    if (item == err_node) item = parse_synth_func_def (c2m_ctx);
    if (item == NULL || item == err_node) break;
    op_flat_append (c2m_ctx, cls, item);
  }
  curr_scope = saved_scope;
  parse_ctx->curr_class = saved_class;
  next_token_index = saved_idx;
  curr_token = saved_tok;

  /* Cache and enqueue for module-level injection (so their MIR is generated). */
  {
    generic_spec_t gs;
    gs.orig_name = "Any";
    gs.spec_name = struct_name_u;
    gs.n_args = 0;
    for (int _ai = 0; _ai < 4; _ai++) gs.args[_ai] = NULL;
    VARR_PUSH (generic_spec_t, generic_specs, gs);
  }
  for (node_t it = NL_HEAD (cls->u.ops); it != NULL;) {
    node_t nxt = NL_NEXT (it);
    NL_REMOVE (cls->u.ops, it);
    VARR_PUSH (node_t, pending_lambdas, it);
    it = nxt;
  }

  return build_id (c2m_ctx, struct_name_u, pos);
#undef SB_PUTS
}

/* Parse `< Interface >` after the `Any` keyword and synthesize __Any_<I>.
   Returns an N_ID for the synthesized class name (Step 2.1 + 2.2). */
static node_t parse_any_instantiation (c2m_ctx_t c2m_ctx, pos_t pos) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  node_t iface_id = NULL;

  M (T_CMP); /* consume '<' */
  if (!MN (T_ID, iface_id)) {
    error (c2m_ctx, pos, "expected an interface name in Any<...>");
    return build_id (c2m_ctx, "Any", pos);
  }
  if (C (T_CMP) && curr_token->node_code == N_GT)
    M (T_CMP); /* consume '>' */
  else
    error (c2m_ctx, pos, "expected '>' to close Any<...>");
  return synthesize_any_class (c2m_ctx, iface_id->u.s.s, pos);
}

/* Parse one synthesized top-level function definition from the current token
   position (used to reparse generated thunks/factories).  Mirrors the
   function-definition branch of transl_unit but without its error-label macros.
   Returns the N_FUNC_DEF node, or err_node on failure. */
static node_t parse_synth_func_def (c2m_ctx_t c2m_ctx) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  node_t ds, d, dl, r, func, param_list, p, par_declarator, id, fd;

  ds = declaration_specs (c2m_ctx, FALSE, (node_t) 1);
  if (ds == err_node) return err_node;
  d = declarator (c2m_ctx, FALSE);
  if (d == err_node) return err_node;
  dl = new_node (c2m_ctx, N_LIST);
  d->attr = curr_scope;
  curr_scope = d;
  while (!C ('{') && !C (T_EOFILE)) {
    r = declaration (c2m_ctx, FALSE);
    if (r == err_node) { curr_scope = d->attr; return err_node; }
    op_flat_append (c2m_ctx, dl, r);
  }
  func = NL_HEAD (NL_EL (d->u.ops, 1)->u.ops);
  if (func != NULL && func->code == N_FUNC) {
    param_list = NL_HEAD (func->u.ops);
    for (p = NL_HEAD (param_list->u.ops); p != NULL; p = NL_NEXT (p)) {
      if (p->code == N_ID) {
        tpname_add (c2m_ctx, p, curr_scope, FALSE);
      } else if (p->code == N_SPEC_DECL) {
        par_declarator = NL_EL (p->u.ops, 1);
        id = NL_HEAD (par_declarator->u.ops);
        tpname_add (c2m_ctx, id, curr_scope, FALSE);
      }
    }
  }
  r = compound_stmt (c2m_ctx, FALSE);
  if (r == err_node) { curr_scope = d->attr; return err_node; }
  fd = new_pos_node4 (c2m_ctx, N_FUNC_DEF, POS (d), ds, d, dl, r);
  curr_scope = d->attr;
  return fd;
}

/* Per-CLASS thunk dedup.  The forwarding/destructor thunks emitted by
   synthesize_any_thunks are keyed on the concrete class only (not the
   interface), so erasing the same class to two interfaces would otherwise
   re-define them and abort MIR with "Repeated item declaration".  We reuse the
   generic-spec registry as a name set: return TRUE the first time THUNK_NAME is
   seen (caller should emit the definition) and record it; return FALSE on any
   later occurrence (caller should emit a forward declaration only). */
static int any_thunk_register_p (c2m_ctx_t c2m_ctx, const char *thunk_name) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  for (size_t i = 0; i < VARR_LENGTH (generic_spec_t, generic_specs); i++)
    if (strcmp (VARR_GET (generic_spec_t, generic_specs, i).spec_name, thunk_name) == 0)
      return FALSE;
  generic_spec_t gs;
  gs.orig_name = "__thunk";
  gs.spec_name = uniq_cstr (c2m_ctx, thunk_name).s;
  gs.n_args = 0;
  for (int _ai = 0; _ai < 4; _ai++) gs.args[_ai] = NULL;
  VARR_PUSH (generic_spec_t, generic_specs, gs);
  return TRUE;
}

/* Step 2.4 — monomorphize the per-(C, I) thunks and factory for any<I>(C*).
   Generates, as ClassyC source reparsed through the normal paths:
     static <R> __thunk_<m>_<C>(void* p, ...) { [return] ((C*)p)->m(...); }
     static void __thunk_dtor_<C>(void* p)     { delete (C*)p; }
     static __Any_<I>* __any_make_<I>_<C>(C* o) { heap handle + slot fills; }
   Returns an N_LIST of the new top-level items to inject (NULL if already
   generated for this (C, I)).  *FACTORY_NAME_OUT receives the factory name in
   both cases.  No closures: each thunk captures only the concrete TYPE. */
static node_t synthesize_any_thunks (c2m_ctx_t c2m_ctx, const char *iface_name,
                                     const char *struct_name, const char *concrete_name,
                                     const char **factory_name_out) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  MIR_alloc_t alloc = c2m_alloc (c2m_ctx);
  char fname[512];
  const char *factory_name;
  node_t iface, members, items;
  VARR (char) * sb;
  size_t tstart, saved_idx;
  token_t saved_tok;
  node_t saved_scope, saved_class;

  snprintf (fname, sizeof (fname), "__any_make_%s_%s", iface_name, concrete_name);
  factory_name = uniq_cstr (c2m_ctx, fname).s;
  if (factory_name_out != NULL) *factory_name_out = factory_name;

  /* Dedup: generate the (C, I) thunks + factory at most once. */
  for (size_t i = 0; i < VARR_LENGTH (generic_spec_t, generic_specs); i++)
    if (strcmp (VARR_GET (generic_spec_t, generic_specs, i).spec_name, factory_name) == 0)
      return NULL;

  iface = find_interface (c2m_ctx, iface_name);
  if (iface == NULL || iface->code != N_INTERFACE) return NULL;
  members = TAG_MEMBER_LIST (iface);

#define SB_PUTS(s) varr_str_push (sb, (s))

  VARR_CREATE (char, sb, alloc, 1024);
  SB_PUTS ("void* malloc(unsigned long);\n");
  SB_PUTS ("void* c2m_obj_track(void*, void(*)(void*));\n");

  /* The forwarding thunks and the destructor thunk are keyed on the concrete
     CLASS only (e.g. __thunk_area_Square, __thunk_dtor_Square), independent of
     the interface.  Erasing the same class to a second interface must NOT
     re-emit their definitions (MIR would abort with "Repeated item
     declaration").  We therefore record each emitted thunk name in the spec
     registry and, on a repeat, emit a forward declaration only so the factory's
     references still resolve.  Returns TRUE if a definition still needs emitting
     (i.e. this is the first time we see THUNK_NAME). */
#define THUNK_FIRST_TIME(thunk_name)                                          \
  any_thunk_register_p (c2m_ctx, (thunk_name))

  /* One forwarding thunk per interface method. */
  for (node_t m = NL_HEAD (members->u.ops); m != NULL; m = NL_NEXT (m)) {
    node_t spec, mid, plist;
    int ret_ptr, idx, nparams, is_void, define_p;
    char thunk_name[512];
    if (!any_extract_method (m, &spec, &mid, &ret_ptr, &plist)) continue;
    is_void = (ret_ptr == 0 && any_specs_are_void (spec));
    snprintf (thunk_name, sizeof (thunk_name), "__thunk_%s_%s", mid->u.s.s,
              concrete_name);
    define_p = THUNK_FIRST_TIME (thunk_name);
    SB_PUTS ("static ");
    any_append_specs (c2m_ctx, sb, spec);
    VARR_PUSH (char, sb, ' ');
    for (int p = 0; p < ret_ptr; p++) VARR_PUSH (char, sb, '*');
    SB_PUTS ("__thunk_");
    SB_PUTS (mid->u.s.s);
    VARR_PUSH (char, sb, '_');
    SB_PUTS (concrete_name);
    SB_PUTS ("(void* __p");
    idx = 0;
    if (!any_plist_empty_p (plist))
      for (node_t pn = NL_HEAD (plist->u.ops); pn != NULL; pn = NL_NEXT (pn)) {
        if (pn->code != N_SPEC_DECL && pn->code != N_TYPE) continue;
        SB_PUTS (", ");
        any_append_param (c2m_ctx, sb, pn, idx++, TRUE);
      }
    nparams = idx;
    if (!define_p) {
      /* Already defined for this class via another interface: declare only. */
      SB_PUTS (");\n");
      continue;
    }
    SB_PUTS (") { ");
    if (!is_void) SB_PUTS ("return ");
    SB_PUTS ("((");
    SB_PUTS (concrete_name);
    SB_PUTS ("*)__p)->");
    SB_PUTS (mid->u.s.s);
    VARR_PUSH (char, sb, '(');
    for (int a = 0; a < nparams; a++) {
      char nb[32];
      snprintf (nb, sizeof (nb), "%s__a%d", a == 0 ? "" : ", ", a);
      SB_PUTS (nb);
    }
    SB_PUTS ("); }\n");
  }

  /* Destructor thunk: owns and frees the concrete object (per CLASS). */
  {
    char dtor_name[512];
    snprintf (dtor_name, sizeof (dtor_name), "__thunk_dtor_%s", concrete_name);
    if (THUNK_FIRST_TIME (dtor_name)) {
      SB_PUTS ("static void __thunk_dtor_");
      SB_PUTS (concrete_name);
      SB_PUTS ("(void* __p) { delete (");
      SB_PUTS (concrete_name);
      SB_PUTS ("*)__p; }\n");
    } else {
      /* Already defined for this class: forward declaration only. */
      SB_PUTS ("static void __thunk_dtor_");
      SB_PUTS (concrete_name);
      SB_PUTS ("(void* __p);\n");
    }
  }

  /* Factory: allocate the handle and wire up its slots. */
  SB_PUTS ("static ");
  SB_PUTS (struct_name);
  SB_PUTS ("* ");
  SB_PUTS (factory_name);
  SB_PUTS ("(");
  SB_PUTS (concrete_name);
  SB_PUTS ("* __obj) {\n");
  SB_PUTS (struct_name);
  SB_PUTS ("* __h = (");
  SB_PUTS (struct_name);
  SB_PUTS ("*)malloc(sizeof(");
  SB_PUTS (struct_name);
  SB_PUTS ("));\n");
  SB_PUTS ("__h->data = (void*)__obj;\n");
  SB_PUTS ("__h->dtor = __thunk_dtor_");
  SB_PUTS (concrete_name);
  SB_PUTS (";\n");
  for (node_t m = NL_HEAD (members->u.ops); m != NULL; m = NL_NEXT (m)) {
    node_t spec, mid, plist;
    int ret_ptr;
    if (!any_extract_method (m, &spec, &mid, &ret_ptr, &plist)) continue;
    SB_PUTS ("__h->");
    SB_PUTS (mid->u.s.s);
    SB_PUTS ("_fn = __thunk_");
    SB_PUTS (mid->u.s.s);
    VARR_PUSH (char, sb, '_');
    SB_PUTS (concrete_name);
    SB_PUTS (";\n");
  }
  /* Register the handle in the scope-bound object arena so it is reclaimed
     automatically when its scope exits (running ~__Any_I, which frees the
     wrapped concrete object via the dtor slot). */
  SB_PUTS ("c2m_obj_track(__h, __anyfree_");
  SB_PUTS (iface_name);
  SB_PUTS (");\n");
  SB_PUTS ("return __h;\n}\n;\n"); /* trailing ';' is a spare sentinel token */
  VARR_PUSH (char, sb, '\0');

  if (c2m_options->debug_p)
    fprintf (stderr, "=== synthesized thunks/factory for any<%s>(%s*) ===\n%s\n",
             iface_name, concrete_name, VARR_ADDR (char, sb));

  /* Lex and reparse the items (a malloc prototype declaration, the thunks, and
     the factory function definition). */
  tstart = lex_source_into_tokens (c2m_ctx, VARR_ADDR (char, sb), no_pos);
  VARR_DESTROY (char, sb);

  saved_idx = next_token_index;
  saved_tok = curr_token;
  saved_scope = curr_scope;
  saved_class = parse_ctx->curr_class;
  curr_scope = NULL;
  parse_ctx->curr_class = NULL;
  next_token_index = tstart;
  read_token (c2m_ctx);
  items = new_node (c2m_ctx, N_LIST);
  while (!C (';') && !C (T_EOFILE)) {
    node_t item = TRY (declaration);
    if (item == err_node) item = parse_synth_func_def (c2m_ctx);
    if (item == NULL || item == err_node) break;
    op_flat_append (c2m_ctx, items, item);
  }
  curr_scope = saved_scope;
  parse_ctx->curr_class = saved_class;
  next_token_index = saved_idx;
  curr_token = saved_tok;

  /* Cache so the (C, I) thunks + factory are emitted only once. */
  {
    generic_spec_t gs;
    gs.orig_name = iface_name;
    gs.spec_name = factory_name;
    gs.n_args = 0;
    for (int _ai = 0; _ai < 4; _ai++) gs.args[_ai] = NULL;
    VARR_PUSH (generic_spec_t, generic_specs, gs);
  }
  return items;
#undef SB_PUTS
}

/* Parse (but do not yet inject/check) the top-level source for a per-class
   defer-cleanup thunk -- the synthesis half of ensure_defer_thunk (defined
   later, once module_item_list/check_lambda_func_def are available; split
   across the file for that reason -- see its comment):

     static void __thunk_dtor_<C>(void* p) { delete (C*)p; }

   Full delete (dtor + string-member drop + object-guard-or-free).  Uses
   the exact name any<I>(C*) erasure already synthesizes, so the two
   features share one definition for a class used both ways (deduped
   via any_thunk_register_p's registry, whichever registers first).

   Sets *NAME_OUT to the interned thunk name unconditionally.  Returns NULL
   (nothing to inject) when the name was already registered -- by an earlier
   call or an any<I>(C*) erasure of the same class -- or when CONCRETE_NAME
   does not resolve to a class visible at top_scope (the thunk body's cast
   could never check; see below).  Otherwise returns the parsed N_LIST of
   items (one top-level function def) for the caller to inject and check. */
static node_t synthesize_defer_thunk_items (c2m_ctx_t c2m_ctx,
                                            const char *concrete_name, pos_t pos,
                                            const char **name_out) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  MIR_alloc_t alloc = c2m_alloc (c2m_ctx);
  char thunk_name[512];
  VARR (char) * sb;
  node_t items, cid;
  size_t tstart, saved_idx;
  token_t saved_tok;
  node_t saved_scope, saved_class;

  snprintf (thunk_name, sizeof (thunk_name), "__thunk_dtor_%s", concrete_name);
  *name_out = uniq_cstr (c2m_ctx, thunk_name).s;
  if (!any_thunk_register_p (c2m_ctx, thunk_name)) return NULL; /* already exists */

  /* The thunk body names the class in a cast: `delete (C*)__p;`.  That makes
     two preconditions for the reparse below, both visible only when C is a
     mangled name no user source ever spells (__generic_Map_String_int,
     __Any_Shape, ...):

     1. CHECK: the name must resolve to a class definition from top_scope
        (check_lambda_func_def checks the thunk with curr_scope = top_scope).
        Specializations get their S_TAG/S_REGULARS symbols when materialized,
        so this holds for any class the caller could hold a checked pointer
        to -- but verify, because a thunk that fails to check would leave the
        name registered with no definition.

     2. PARSE: the name must be a *parser* typename reachable from the
        reparse's NULL scope.  typedef_name()'s scope walk from
        curr_scope == NULL sees only NULL-scope entries, and the mangled
        name's original tpname_add used whatever scope was live when the
        specialization was created (often a since-popped function scope).
        Seed the NULL-scope entry explicitly. */
  cid = build_id (c2m_ctx, concrete_name, pos);
  if (top_scope == NULL
      || find_def (c2m_ctx, S_TAG, cid, top_scope, NULL) == NULL)
    return NULL;
  if (!tpname_find (c2m_ctx, cid, NULL, NULL)) tpname_add (c2m_ctx, cid, NULL, TRUE);

#define SB_PUTS(s) varr_str_push (sb, (s))
  VARR_CREATE (char, sb, alloc, 256);
  SB_PUTS ("static void ");
  SB_PUTS (thunk_name);
  SB_PUTS ("(void* __p) { delete (");
  SB_PUTS (concrete_name);
  /* The trailing run of ';' tokens is more than spare lookahead for the
     reparse loop: they are error-recovery sync points.  If the synthesized
     text ever fails to parse, the parser's skip-to-';' recovery must find a
     sentinel INSIDE this token run -- the synthesized tokens are the tail of
     recorded_tokens, so a skip that overruns them reads past the varr and
     aborts (mir-varr assert), corrupting the whole compile. */
  SB_PUTS ("*)__p; }\n;\n;\n;\n;\n");
  VARR_PUSH (char, sb, '\0');

  if (c2m_options->debug_p)
    fprintf (stderr, "=== synthesized defer thunk %s ===\n%s\n", thunk_name, VARR_ADDR (char, sb));

  tstart = lex_source_into_tokens (c2m_ctx, VARR_ADDR (char, sb), pos);
  VARR_DESTROY (char, sb);
#undef SB_PUTS

  saved_idx = next_token_index;
  saved_tok = curr_token;
  saved_scope = curr_scope;
  saved_class = parse_ctx->curr_class;
  curr_scope = NULL;
  parse_ctx->curr_class = NULL;
  next_token_index = tstart;
  read_token (c2m_ctx);
  items = new_node (c2m_ctx, N_LIST);
  while (!C (';') && !C (T_EOFILE)) {
    node_t item = TRY (declaration);
    if (item == err_node) item = parse_synth_func_def (c2m_ctx);
    if (item == NULL || item == err_node) break;
    op_flat_append (c2m_ctx, items, item);
  }
  curr_scope = saved_scope;
  parse_ctx->curr_class = saved_class;
  next_token_index = saved_idx;
  curr_token = saved_tok;
  return items;
}

/* interface Name { ret meth(params); ... }
   Parsed like a struct body (each prototype becomes an N_MEMBER carrying a
   function declarator).  Builds N_INTERFACE { id(0), member_list(1) } and
   registers it.  Emits no runtime type, vtable, or layout. */
D (interface_declaration) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  node_t id, members, r;
  pos_t pos;

  if (!MP_SOFT ("interface", pos)) return err_node;
  PTN (T_ID); /* interface name -> r */
  id = r;
  PT ('{');
  if (C ('}')) {
    members = new_node (c2m_ctx, N_LIST);
  } else {
    P (struct_declaration_list); /* r = N_LIST of N_MEMBER prototypes */
    members = r;
  }
  PT ('}');
  M (';'); /* optional trailing semicolon */
  r = new_pos_node2 (c2m_ctx, N_INTERFACE, pos, id, members);
  register_interface (c2m_ctx, id->u.s.s, r);
  return r;
}

D (transl_unit) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  node_t list, ds, d, dl, r, func, param_list, p, par_declarator, id;

  read_token (c2m_ctx);
  list = new_node (c2m_ctx, N_LIST);
  while (!C (T_EOFILE)) { /* external-declaration */
    node_t http_route_extra = NULL; /* optional [[HttpGet]]-synthesized RouteReg */
    if (C_SOFT ("interface")) {
      PE (interface_declaration, err);
    } else if ((r = TRY (declaration)) != err_node) {
      // Successfully parsed a declaration or class definition (e.g. "int x;"
      // or "class MyClass { ... };") -- declaration handles class bodies too.
    } else {
      // Attempt to parse a function definition.
      // Leading C23 attrs: [[HttpGet("/path")]] static Response* f(...) { }
      node_t lead_attrs = try_attr_spec (c2m_ctx, curr_token->pos, NULL);
      if (lead_attrs == err_node) lead_attrs = NULL;
      PAE (declaration_specs, (node_t) 1, err);
      ds = r;
      PE (declarator, err);
      d = r;
      /* Recover the generic-function `<T,...>` clause (if any) stashed on the
         N_DECL node's attr by direct_declarator.  Using a node-carried carrier
         instead of a parse_ctx side channel survives the parameter-list parse,
         which re-enters direct_declarator for each parameter.  NULL/0 means this
         is an ordinary function. */
      int fn_n_tp = 0;
      const char *fn_tps[4] = {NULL, NULL, NULL, NULL};
      if (d->attr != NULL) {
        int *carrier = (int *) d->attr;
        fn_n_tp = carrier[0];
        const char **carr_tps = (const char **) (carrier + 1);
        for (int i = 0; i < 4; i++) fn_tps[i] = carr_tps[i];
        d->attr = NULL; /* clear the carrier before check sets the real attr */
      }
      dl = new_node (c2m_ctx, N_LIST);
      d->attr = curr_scope;
      curr_scope = d;
      /* Trailing GCC/C23 attributes or __asm may appear between the declarator
         and the function body: `void foo() __attribute__((da_ignore)) {}`.
         Collect them so create_decl can set decl->da_ignore_p. */
      node_t free_trail_attrs = NULL;
      for (;;) {
        node_t asmp = NULL;
        node_t ar = try_attr_spec (c2m_ctx, curr_token->pos, &asmp);
        if (ar != err_node && (ar != NULL || asmp != NULL)) {
          if (ar != NULL && ar != err_node) {
            if (free_trail_attrs == NULL)
              free_trail_attrs = ar;
            else if (free_trail_attrs->code == N_LIST && ar->code == N_LIST) {
              for (node_t a = NL_HEAD (ar->u.ops); a != NULL; ) {
                node_t next = NL_NEXT (a);
                NL_REMOVE (ar->u.ops, a);
                op_append (c2m_ctx, free_trail_attrs, a);
                a = next;
              }
            } else {
              node_t merged = new_node (c2m_ctx, N_LIST);
              op_append (c2m_ctx, merged, free_trail_attrs);
              op_append (c2m_ctx, merged, ar);
              free_trail_attrs = merged;
            }
          }
          continue;
        }
        break;
      }
      while (!C ('{')) { /* declaration-list */
        PE (declaration, decl_err);
        op_flat_append (c2m_ctx, dl, r);
      }
      func = NL_HEAD (NL_EL (d->u.ops, 1)->u.ops);
      if (func == NULL || func->code != N_FUNC) {
        id = NL_HEAD (d->u.ops);
        error (c2m_ctx, POS (id), "non-function declaration %s before '{'", id->u.s.s);
      } else {
        param_list = NL_HEAD (func->u.ops);
        for (p = NL_HEAD (param_list->u.ops); p != NULL; p = NL_NEXT (p)) {
          if (p->code == N_ID) {
            tpname_add (c2m_ctx, p, curr_scope, FALSE);
          } else if (p->code == N_SPEC_DECL) {
            par_declarator = NL_EL (p->u.ops, 1);
            id = NL_HEAD (par_declarator->u.ops);
            tpname_add (c2m_ctx, id, curr_scope, FALSE);
          }
        }
      }
      P (compound_stmt);
      r = new_pos_node4 (c2m_ctx, N_FUNC_DEF, POS (d), ds, d, dl, r);
      curr_scope = d->attr;
      /* Generic function template: register the N_FUNC_DEF verbatim and mark it
         with the sentinel attr so check/gen skip it (only its monomorphized
         specializations are real functions).  Mirrors the N_CLASS template
         handling above. */
      if (fn_n_tp > 0) {
        node_t fn_id = NL_HEAD (d->u.ops); /* N_DECL's id(0) */
        const char *fn_name = (fn_id != NULL && fn_id->code == N_ID) ? fn_id->u.s.s : NULL;
        if (fn_name != NULL) {
          generic_fn_tmpl_t tmpl;
          tmpl.name = fn_name;
          tmpl.func_node = r;
          tmpl.n_type_params = fn_n_tp;
          for (int i = 0; i < 4; i++) tmpl.type_params[i] = fn_tps[i];
          VARR_PUSH (generic_fn_tmpl_t, generic_fn_templates, tmpl);
          r->attr = (void *)((intptr_t)-1); /* sentinel: template, not a real function */
        }
      } else if ((free_trail_attrs != NULL
                  && attr_list_has_da_ignore (free_trail_attrs))
                 || (lead_attrs != NULL && attr_list_has_da_ignore (lead_attrs))
                 || parse_ctx->pending_func_da_ignore) {
        r->attr = PRECHECK_DA_IGNORE;
        parse_ctx->pending_func_da_ignore = 0;
      } else if (free_trail_attrs != NULL) {
        r->attr = free_trail_attrs;
      }
      parse_ctx->pending_func_da_ignore = 0;

      /* [[HttpGet("/path")]] (etc.) → synthesize
         [[registry("routes")]] static RouteReg __cy_route_... = {...}; */
      if (lead_attrs != NULL && fn_n_tp == 0) {
        const char *hm = NULL, *hp = NULL;
        if (http_route_from_attrs (lead_attrs, &hm, &hp)) {
          node_t fn_id = NL_HEAD (d->u.ops);
          const char *fn_name
            = (fn_id != NULL && fn_id->code == N_ID) ? fn_id->u.s.s : "handler";
          http_route_extra
            = make_http_route_reg (c2m_ctx, POS (d), hm, hp, fn_name);
        }
      }
    }
    /* Inject any lambdas defined while parsing this top-level item (they must
       precede the item in the module so their MIR functions are generated first). */
    for (size_t li = 0; li < VARR_LENGTH (node_t, pending_lambdas); li++) {
      node_t pend = VARR_GET (node_t, pending_lambdas, li);
      if (pend != NULL) op_append (c2m_ctx, list, pend);
    }
    VARR_TRUNC (node_t, pending_lambdas, 0);
    op_flat_append (c2m_ctx, list, r);
    if (http_route_extra != NULL)
      op_flat_append (c2m_ctx, list, http_route_extra);
    continue;
  decl_err:
    curr_scope = d->attr;
  err:
    error_recovery (c2m_ctx, 0, "<declaration>, <class>, or <function_definition>");
  }
  return new_node1 (c2m_ctx, N_MODULE, list);
}

static void fatal_error (c2m_ctx_t c2m_ctx, C_error_code_t code MIR_UNUSED, const char *message) {
  if (c2m_options->message_file != NULL) fprintf (c2m_options->message_file, "%s\n", message);
  longjmp (c2m_ctx->env, 1);
}

static void kw_add (c2m_ctx_t c2m_ctx, const char *name, token_code_t tc, size_t flags) {
  str_add (c2m_ctx, name, strlen (name) + 1, tc, flags, TRUE);
}

static void parse_init (c2m_ctx_t c2m_ctx) {
  MIR_alloc_t alloc = c2m_alloc (c2m_ctx);
  parse_ctx_t parse_ctx;

  c2m_ctx->parse_ctx = parse_ctx = c2mir_calloc (c2m_ctx, sizeof (struct parse_ctx));
  curr_scope = NULL;
  error_func = fatal_error;
  record_level = 0;
  curr_uid = 0;
  init_streams (c2m_ctx);
  VARR_CREATE (token_t, recorded_tokens, alloc, 32);
  VARR_CREATE (token_t, buffered_tokens, alloc, 32);
  pre_init (c2m_ctx);
  kw_add (c2m_ctx, "_Bool", T_BOOL, 0);
  /* `bool` is a soft (context-sensitive) keyword recognized only as a type
     specifier (see type_spec), so it stays usable as an ordinary C identifier
     everywhere else and remains compatible with <stdbool.h>. */
  kw_add (c2m_ctx, "_Complex", T_COMPLEX, 0);
  kw_add (c2m_ctx, "_Alignas", T_ALIGNAS, 0);
  kw_add (c2m_ctx, "_Alignof", T_ALIGNOF, 0);
  kw_add (c2m_ctx, "_Atomic", T_ATOMIC, 0);
  kw_add (c2m_ctx, "_Generic", T_GENERIC, 0);
  kw_add (c2m_ctx, "_Noreturn", T_NO_RETURN, 0);
  kw_add (c2m_ctx, "_Static_assert", T_STATIC_ASSERT, 0);
  kw_add (c2m_ctx, "_Thread_local", T_THREAD_LOCAL, 0);
  kw_add (c2m_ctx, "auto", T_AUTO, 0);
  kw_add (c2m_ctx, "break", T_BREAK, 0);
  kw_add (c2m_ctx, "case", T_CASE, 0);
  kw_add (c2m_ctx, "char", T_CHAR, 0);
  kw_add (c2m_ctx, "String", T_STRING, 0); // lowercase string is too common... perhaps _string?
  kw_add (c2m_ctx, "const", T_CONST, 0);
  kw_add (c2m_ctx, "continue", T_CONTINUE, 0);
  kw_add (c2m_ctx, "default", T_DEFAULT, 0);
  kw_add (c2m_ctx, "do", T_DO, 0);
  kw_add (c2m_ctx, "double", T_DOUBLE, 0);
  kw_add (c2m_ctx, "else", T_ELSE, 0);
  kw_add (c2m_ctx, "enum", T_ENUM, 0);
  kw_add (c2m_ctx, "dict", T_DICT, 0);
  /* `in` is a context-sensitive (soft) keyword recognized only in the dict
     membership operator and for-in loops, so it stays usable as an ordinary
     C identifier (variable/parameter/member name) everywhere else. */
  kw_add (c2m_ctx, "extern", T_EXTERN, 0);
  kw_add (c2m_ctx, "float", T_FLOAT, 0);
  kw_add (c2m_ctx, "for", T_FOR, 0);
  kw_add (c2m_ctx, "goto", T_GOTO, 0);
  kw_add (c2m_ctx, "if", T_IF, 0);
  kw_add (c2m_ctx, "inline", T_INLINE, FLAG_EXT89);
  kw_add (c2m_ctx, "int", T_INT, 0);
  kw_add (c2m_ctx, "long", T_LONG, 0);
  kw_add (c2m_ctx, "register", T_REGISTER, 0);
  kw_add (c2m_ctx, "restrict", T_RESTRICT, FLAG_C89);
  kw_add (c2m_ctx, "return", T_RETURN, 0);
  kw_add (c2m_ctx, "short", T_SHORT, 0);
  kw_add (c2m_ctx, "signed", T_SIGNED, 0);
  kw_add (c2m_ctx, "sizeof", T_SIZEOF, 0);
  kw_add (c2m_ctx, "static", T_STATIC, 0);
  kw_add (c2m_ctx, "struct", T_STRUCT, 0);
  kw_add (c2m_ctx, "class", T_CLASS, 0);
  kw_add (c2m_ctx, "switch", T_SWITCH, 0);
  kw_add (c2m_ctx, "typedef", T_TYPEDEF, 0);
  kw_add (c2m_ctx, "typeof", T_TYPEOF, FLAG_EXT);
  kw_add (c2m_ctx, "union", T_UNION, 0);
  kw_add (c2m_ctx, "unsigned", T_UNSIGNED, 0);
  kw_add (c2m_ctx, "void", T_VOID, 0);
  kw_add (c2m_ctx, "volatile", T_VOLATILE, 0);
  kw_add (c2m_ctx, "while", T_WHILE, 0);
  kw_add (c2m_ctx, "__restrict", T_RESTRICT, FLAG_EXT);
  kw_add (c2m_ctx, "__restrict__", T_RESTRICT, FLAG_EXT);
  kw_add (c2m_ctx, "__inline", T_INLINE, FLAG_EXT);
  kw_add (c2m_ctx, "__inline__", T_INLINE, FLAG_EXT);
  tpname_init (c2m_ctx);
  VARR_CREATE (node_t, pending_lambdas, alloc, 8);
  lambda_uid = 0;
  VARR_CREATE (generic_tmpl_t, generic_templates, alloc, 4);
  VARR_CREATE (generic_deferred_spec_t, generic_deferred_specs, alloc, 4);
  VARR_CREATE (generic_spec_t, generic_specs, alloc, 8);
  VARR_CREATE (generic_crossref_t, generic_crossrefs, alloc, 4);
  VARR_CREATE (local_type_hoist_t, local_type_hoists, alloc, 4);
  VARR_CREATE (iface_t, interfaces, alloc, 4);
  VARR_CREATE (generic_fn_tmpl_t, generic_fn_templates, alloc, 4);
  VARR_CREATE (generic_fn_spec_t, generic_fn_specs, alloc, 8);
  VARR_CREATE (generic_method_tmpl_t, generic_method_templates, alloc, 4);
  VARR_CREATE (generic_method_spec_t, generic_method_specs, alloc, 8);
  VARR_CREATE (cstr_t, generic_in_progress, alloc, 8);
  VARR_CREATE (class_type_meta_t, class_type_metas, alloc, 16);
  VARR_CREATE (node_t, parsed_classes, alloc, 16);
  n_method_type_params = 0;
  for (int i = 0; i < 4; i++) method_type_params[i] = NULL;
  builtin_methods_init (alloc);
}

/* ClassyC exception prelude — injected into every translation unit so that
   try/catch/throw type-check without the user including anything.  `Exception`
   is the value type bound to a `catch` variable; its layout must match
   cy_exception_t in the runtime (include/cyexc.h).  The named classes are plain
   integer ids: `throw(NullException, msg)` and `catch(NullException e)` both
   resolve them as ordinary constants, and users can extend the set with their
   own `enum { MyException = 100 };`. */
static const char *const exception_prelude =
  "typedef struct { unsigned int id; const char *msg; const char *file; int line; } Exception;\n"
  "enum {\n"
  "  AnyException = 0, NullException = 1, OutOfBoundsException = 2,\n"
  "  ArithmeticException = 3, RuntimeException = 4, IOException = 5,\n"
  "  ValueException = 6, TypeException = 7, KeyException = 8\n"
  "};\n";

static void add_standard_includes (c2m_ctx_t c2m_ctx) {
  const char *str, *name;

  for (size_t i = 0; i < sizeof (standard_includes) / sizeof (string_include_t); i++) {
    if ((name = standard_includes[i].name) != NULL) continue;
    str = standard_includes[i].content;
    add_string_stream (c2m_ctx, "<environment>", str);
  }
  add_string_stream (c2m_ctx, "<exception-prelude>", exception_prelude);
  /* Header-extensible String builtin table (re-registers stock methods + documents
     the [[builtin_method]] form).  Resolves via auto-discovered include/. */
  add_string_stream (c2m_ctx, "<string-builtins>",
                     "#include \"string_builtins.h\"\n");
  /* Attribute spelling that survives glibc: when the host is not treated as
     GCC/clang, sys/cdefs.h empties `__attribute__(xyz)`, so
     `__attribute__((da_ignore))` vanishes after `#include <stdio.h>`.
     The parser also accepts `__mirc_attribute__` (same shape); ClassyC
     list/map/set use that form.  CLASSYC_DA_IGNORE is a convenience for
     user code that must include system headers first. */
  add_string_stream (c2m_ctx, "<classyc-attrs>",
                     "#define CLASSYC_DA_IGNORE __mirc_attribute__((da_ignore))\n");
}

/* Pre-scan the post-preprocessor token stream and register every `class NAME`
   that appears at file scope (brace_depth == 0) as a tpname *before* any class
   body is parsed.

   Rationale: ClassyC's parser already registers a class's own name before it
   descends into the body so that self-referential return types like
   `ClassName*` work (see the tpname_add at the `class { ... }` site).  But a
   *sibling* class declared later in the file is still unknown at that point,
   so members like `Statement* prepare(...)` inside `class Sqlite { ... }`
   fail to parse if `class Statement` appears below.

   By collecting every file-scope `class NAME` token pair up front and inserting
   each NAME into tpname_tab at the (NULL) file scope, methods of one class can
   freely mention any other class regardless of source order.  C11 semantics for
   plain functions, typedefs and structs are untouched: ordinary C identifiers
   still need a real preceding declaration before use.

   Nested `class` declarations (brace_depth > 0) are intentionally ignored;
   they continue to self-register during normal parsing.

   Implementation note: tpname_add() is idempotent (early-returns if the
   {id-string, scope} pair is already present), so it is safe to call it here
   even though the parser may add the same name again when it reaches the
   class header.  We pass the very same N_ID node the parser will see, which
   guarantees pointer-equal `id->u.s.s` for the htab hash/eq predicates. */
static void pre_register_class_tpnames (c2m_ctx_t c2m_ctx) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  size_t n = VARR_LENGTH (token_t, recorded_tokens);
  int brace_depth = 0;

  if (n < 2) return;
  for (size_t i = 0; i + 1 < n; i++) {
    token_t t = VARR_GET (token_t, recorded_tokens, i);
    int c = t->code;
    if (c == '{') { brace_depth++; continue; }
    if (c == '}') { if (brace_depth > 0) brace_depth--; continue; }
    if (brace_depth != 0) continue;        /* only file-scope class decls */
    if (c != T_CLASS) continue;
    token_t id_tok = VARR_GET (token_t, recorded_tokens, i + 1);
    if (id_tok->code != T_ID || id_tok->node == NULL) continue;
    /* curr_scope is NULL at parse start (set by parse_init) — that is the
       file scope used by the rest of the parser for top-level classes. */
    tpname_add (c2m_ctx, id_tok->node, curr_scope, TRUE);
  }
}

/* Pre-scan for generic function templates:  T Name<T, U>(params) { ... }
   At file scope, detect the shape  ID '<' ID (',' ID)* '>' '('  and pre-register
   each type-parameter identifier as a tpname, so the return type and parameter
   types (which mention T before the '<' clause is parsed) resolve.

   Mirrors pre_register_class_tpnames.  Conservative: only fires at brace_depth
   0 (file scope) and only when the full `<...>(` shape matches, so an ordinary
   comparison `a < b` at file scope (rare, and not followed by `> (`) is left
   alone.  tpname_add is idempotent. */
static void pre_register_generic_fn_tpnames (c2m_ctx_t c2m_ctx) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
  size_t n = VARR_LENGTH (token_t, recorded_tokens);
  int brace_depth = 0;

  if (n < 4) return;
  for (size_t i = 0; i < n; i++) {
    token_t t = VARR_GET (token_t, recorded_tokens, i);
    int c = t->code;
    if (c == '{') { brace_depth++; continue; }
    if (c == '}') { if (brace_depth > 0) brace_depth--; continue; }
    if (brace_depth != 0) continue;
    /* Need: ID '<' ... '>' '('  with the '<' immediately after an ID. */
    if (c != T_CMP || t->node_code != N_LT) continue;
    if (i == 0) continue;
    token_t prev = VARR_GET (token_t, recorded_tokens, i - 1);
    if (prev->code != T_ID || prev->node == NULL) continue;
    /* Walk the type-param list: ID (',' ID)* '>' */
    size_t j = i + 1;
    if (j >= n) continue;
    token_t tt = VARR_GET (token_t, recorded_tokens, j);
    if (tt->code != T_ID || tt->node == NULL) continue;
    /* Collect param ids until '>' */
    size_t params[4];
    int np = 0;
    for (;;) {
      tt = VARR_GET (token_t, recorded_tokens, j);
      if (tt->code != T_ID || tt->node == NULL) break;
      if (np < 4) params[np++] = j;
      j++;
      if (j >= n) { np = 0; break; }
      token_t nx = VARR_GET (token_t, recorded_tokens, j);
      if (nx->code == ',') { j++; continue; }
      if (nx->code == T_CMP && nx->node_code == N_GT) { j++; break; }
      np = 0; break;
    }
    if (np == 0) continue;
    /* Require '(' immediately after '>' */
    if (j >= n) continue;
    token_t after = VARR_GET (token_t, recorded_tokens, j);
    if (after->code != '(') continue;
    /* Register each type-param id as a tpname at file scope. */
    for (int k = 0; k < np; k++) {
      token_t pt = VARR_GET (token_t, recorded_tokens, params[k]);
      tpname_add (c2m_ctx, pt->node, curr_scope, TRUE);
    }
  }
}

static node_t parse (c2m_ctx_t c2m_ctx) {
  parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;

  next_token_index = 0;
  pre_register_class_tpnames (c2m_ctx);
  pre_register_generic_fn_tpnames (c2m_ctx);
  return transl_unit (c2m_ctx, FALSE);
}

static void parse_finish (c2m_ctx_t c2m_ctx) {
  if (c2m_ctx == NULL || c2m_ctx->parse_ctx == NULL) return;
  if (recorded_tokens != NULL) VARR_DESTROY (token_t, recorded_tokens);
  if (buffered_tokens != NULL) VARR_DESTROY (token_t, buffered_tokens);
  pre_finish (c2m_ctx);
  tpname_finish (c2m_ctx);
  {
    parse_ctx_t parse_ctx = c2m_ctx->parse_ctx;
    if (pending_lambdas != NULL)
      VARR_DESTROY (node_t, pending_lambdas);
    if (interfaces != NULL) VARR_DESTROY (iface_t, interfaces);
    if (generic_templates != NULL)
      VARR_DESTROY (generic_tmpl_t, generic_templates);
    if (generic_specs != NULL)
      VARR_DESTROY (generic_spec_t, generic_specs);
    if (generic_crossrefs != NULL)
      VARR_DESTROY (generic_crossref_t, generic_crossrefs);
    if (generic_deferred_specs != NULL)
      VARR_DESTROY (generic_deferred_spec_t, generic_deferred_specs);
    if (local_type_hoists != NULL)
      VARR_DESTROY (local_type_hoist_t, local_type_hoists);
    if (generic_fn_templates != NULL)
      VARR_DESTROY (generic_fn_tmpl_t, generic_fn_templates);
    if (generic_fn_specs != NULL)
      VARR_DESTROY (generic_fn_spec_t, generic_fn_specs);
    if (generic_method_templates != NULL)
      VARR_DESTROY (generic_method_tmpl_t, generic_method_templates);
    if (generic_method_specs != NULL)
      VARR_DESTROY (generic_method_spec_t, generic_method_specs);
    if (generic_in_progress != NULL)
      VARR_DESTROY (cstr_t, generic_in_progress);
  }
  builtin_methods_finish ();
  finish_streams (c2m_ctx);
  reg_free (c2m_ctx, c2m_ctx->parse_ctx);
}

#undef P
#undef PT
#undef PTP
#undef PTN
#undef PE
#undef PTE
#undef D
#undef M
#undef MP
#undef MC
#undef MN
#undef TRY
#undef C

/* ------------------------- Parser End ------------------------------ */
