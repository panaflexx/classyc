/* -------------------------- MIR generator start ----------------------------- */

static const char *FP_NAME = "fp";
static const char *RET_ADDR_NAME = "Ret_Addr";

/* New attribute for non-empty label LIST is a MIR label.  */

/* MIR var naming:
   {I|U|i|u|f|d}_<integer> -- temporary of I64, U64, I32, U32, F, D type
   {I|U|i|u|f|d}<scope_number>_<name> -- variable with of original <name> of the corresponding
   type in scope with <scope_number>
*/

#if MIR_PTR64
static const MIR_type_t MIR_POINTER_TYPE = MIR_T_I64;
#else
static const MIR_type_t MIR_POINTER_TYPE = MIR_T_I32;
#endif

struct op {
  decl_t decl;
  MIR_op_t mir_op;
};

typedef struct op op_t;

struct reg_var {
  const char *name;
  MIR_reg_t reg;
};

typedef struct reg_var reg_var_t;

DEF_HTAB (reg_var_t);
DEF_VARR (int);
DEF_VARR (MIR_type_t);
DEF_VARR (MIR_alias_t);

struct init_el {
  c2m_ctx_t c2m_ctx; /* for sorting */
  mir_size_t num, offset;
  decl_t member_decl; /* NULL for non-member initialization  */
  struct type *el_type, *container_type;
  node_t init;
};

typedef struct init_el init_el_t;
DEF_VARR (init_el_t);

DEF_VARR (MIR_op_t);
DEF_VARR (case_t);
DEF_HTAB (MIR_item_t);

struct gen_ctx {
  op_t zero_op, one_op, minus_one_op;
  MIR_item_t curr_func;
  DLIST (MIR_insn_t) slow_code_part;
  HTAB (reg_var_t) * reg_var_tab;
  int reg_free_mark;
  MIR_label_t continue_label, break_label;
  op_t top_gen_last_op;
  node_t stmtexpr_last_expr; /* value-producing last expr of current statement
                               expression (MIR #452) — saved/restored for nesting */
  struct {
    int res_ref_p; /* flag of returning an aggregate by reference */
    VARR (MIR_type_t) * ret_types;
    VARR (MIR_var_t) * arg_vars;
  } proto_info;
  VARR (init_el_t) * init_els;
  MIR_item_t memset_proto, memset_item;
  MIR_item_t memcpy_proto, memcpy_item;
  MIR_item_t memcmp_proto, memcmp_item;
  /* heap allocation for `new ClassName(...)` */
  MIR_item_t malloc_proto, malloc_item;
  MIR_item_t free_proto, free_item; /* heap release for `delete obj` */
  int new_proto_count; /* makes per-new constructor-call protos unique */
  /* `defer <stmt>` support: LIFO stack of pending statements for the current
     function, plus the stack depths to unwind to at `break`/`continue`. */
  VARR (node_t) * defer_stmts;
  size_t defer_break_mark, defer_continue_mark;
  /* dict runtime helpers */
  MIR_item_t dict_init_funcs[64]; /* generated __dict_init_* funcs */
  int dict_init_func_count;
  MIR_item_t dict_create_object_proto, dict_create_object_item;
  MIR_item_t dict_create_bool_proto, dict_create_bool_item;
  MIR_item_t dict_create_int64_proto, dict_create_int64_item;
  MIR_item_t dict_create_number_proto, dict_create_number_item;
  MIR_item_t dict_create_string_proto, dict_create_string_item;
  MIR_item_t dict_object_set_proto, dict_object_set_item;
  MIR_item_t dict_object_get_proto, dict_object_get_item;
  MIR_item_t dict_value_copy_proto, dict_value_copy_item;
  MIR_item_t dict_object_count_proto, dict_object_count_item;
  MIR_item_t dict_object_key_at_proto, dict_object_key_at_item;
  MIR_item_t dict_object_value_at_proto, dict_object_value_at_item;
  MIR_item_t dict_value_at_proto, dict_value_at_item;
  MIR_item_t dict_is_array_proto, dict_is_array_item;
  MIR_item_t dict_iter_count_proto, dict_iter_count_item;
  MIR_item_t dict_create_array_proto, dict_create_array_item;
  MIR_item_t dict_array_append_proto, dict_array_append_item;
  MIR_item_t dict_serialize_json_proto, dict_serialize_json_item;
  MIR_item_t dict_serialize_json_heap_proto, dict_serialize_json_heap_item;
  MIR_item_t dict_deserialize_json_proto, dict_deserialize_json_item;
  MIR_item_t dict_destroy_proto, dict_destroy_item;              /* delete d  */
  MIR_item_t dict_create_heap_arena_proto, dict_create_heap_arena_item; /* new dict() */
  /* String (UTF-8) runtime helpers */
  MIR_item_t str_length_proto, str_length_item;
  MIR_item_t str_empty_proto, str_empty_item;
  MIR_item_t str_substr_proto, str_substr_item;
  MIR_item_t str_find_proto, str_find_item;
  MIR_item_t str_replace_proto, str_replace_item;
  MIR_item_t str_replace_all_proto, str_replace_all_item;
  MIR_item_t str_upper_proto, str_upper_item;
  MIR_item_t str_lower_proto, str_lower_item;
  MIR_item_t str_starts_with_proto, str_starts_with_item;
  MIR_item_t str_ends_with_proto, str_ends_with_item;
  MIR_item_t str_contains_proto, str_contains_item;
  MIR_item_t str_trim_proto, str_trim_item;
  MIR_item_t str_split_proto, str_split_item;
  MIR_item_t str_join_proto, str_join_item;
  MIR_item_t str_equals_proto, str_equals_item;
  MIR_item_t str_detach_proto, str_detach_item;
  MIR_item_t str_attach_proto, str_attach_item;
  MIR_item_t str_own_proto, str_own_item;     /* c2m_str_own  — object-owned copy */
  MIR_item_t str_drop_proto, str_drop_item;   /* c2m_str_drop — free owned field  */
  MIR_item_t str_checkpoint_proto, str_checkpoint_item;
  MIR_item_t str_release_to_proto, str_release_to_item;
  MIR_item_t str_release_keeping_proto, str_release_keeping_item;
  MIR_item_t str_copy_proto, str_copy_item;
  /* String `+` concatenation / basic-type auto-cast helpers */
  MIR_item_t str_concat_proto, str_concat_item;
  MIR_item_t str_from_i64_proto;  /* shared char*(int64) proto */
  MIR_item_t str_from_double_proto;
  MIR_item_t str_from_int_item, str_from_uint_item;
  MIR_item_t str_from_bool_item, str_from_char_item, str_from_double_item;
  /* automatic String scope reclamation state for the current function */
  op_t str_scope_mark;
  int str_scope_active;
  /* object arena (Any<I> handle) scope reclamation for the current function */
  MIR_item_t obj_checkpoint_proto, obj_checkpoint_item;
  MIR_item_t obj_release_to_proto, obj_release_to_item;
  MIR_item_t obj_detach_proto, obj_detach_item;
  op_t obj_scope_mark;
  int obj_scope_active;
  /* PER-ITERATION arena scope reclamation for the innermost loop body.
     Layered inside the function-level str_scope_X / obj_scope_X state: a
     fresh checkpoint is taken at the top of each iteration and released at
     the bottom (and on continue/break), so a hot loop driving heap-String
     allocations through helpers (the classy-fetch.cy pattern) stays bounded
     without the user having to call c2m_str_checkpoint/release_to manually.
     The active flag is TRUE only while we are emitting code inside a loop
     body whose subtree allocates Strings (resp. Any<I> handles); the mark
     op is the MIR register holding the iteration checkpoint value.  Loop
     cases save/restore these around their body gen so nested loops compose. */
  op_t loop_str_scope_mark;
  int loop_str_scope_active;
  op_t loop_obj_scope_mark;
  int loop_obj_scope_active;
  /* The break_label of the LOOP that owns the current per-iter scope.
     We need this to disambiguate a `break` inside a switch nested in a loop:
     in that case break_label is the switchs target, but the field below is
     still the loop owners target, and they differ -- so N_BREAK must NOT
     emit a per-iter release (the break stays inside the loop body).  When
     they match, the break is exiting the loop owning the per-iter scope and
     the release is required.  Switch cases do not touch this field; loop
     cases save/restore it. */
  MIR_label_t loop_break_label_for_scope;
  /* try/catch/throw exception runtime - lazily imported on first use.
     setjmp-frame model: cy_exc_push() pushes a frame and returns its jmp_buf,
     the generator calls setjmp() inline, cy_exc_throw() records the exception
     and longjmps to the innermost frame, cy_exc_pop() unwinds one frame. */
  MIR_item_t cy_exc_push_proto, cy_exc_push_item;       /* void *cy_exc_push(void)        */
  MIR_item_t cy_exc_pop_proto, cy_exc_pop_item;         /* void  cy_exc_pop(void)         */
  MIR_item_t cy_exc_current_proto, cy_exc_current_item; /* void *cy_exc_current(void)     */
  MIR_item_t cy_exc_throw_proto, cy_exc_throw_item;     /* void  cy_exc_throw(id,msg,f,l) */
  MIR_item_t cy_setjmp_proto, cy_setjmp_item;           /* int   setjmp(void *buf)        */
  /* String/object arena marks banked into the exception-frame runtime state at
     try-entry (SHORTCOMINGS.md gotcha #10 fix). MIR-generated values live
     across setjmp/longjmp are NOT reliably preserved -- MIR-gen has no model
     of longjmp as an implicit second entry point into the function, so a
     value merely held in a local temp/register between the checkpoint and the
     exception-dispatch label can be clobbered by the try body's own codegen.
     Banking the marks into cy_exc's own frame-stack (plain C runtime state,
     unrelated to any JIT-generated register) and re-reading them fresh after
     the jump sidesteps the hazard entirely. */
  MIR_item_t cy_exc_set_marks_proto, cy_exc_set_marks_item;             /* void cy_exc_set_marks(str_mark,obj_mark,defer_mark) */
  MIR_item_t cy_exc_current_str_mark_proto, cy_exc_current_str_mark_item; /* size_t cy_exc_current_str_mark(void) */
  MIR_item_t cy_exc_current_obj_mark_proto, cy_exc_current_obj_mark_item; /* size_t cy_exc_current_obj_mark(void) */
  MIR_item_t cy_exc_current_defer_mark_proto, cy_exc_current_defer_mark_item; /* size_t cy_exc_current_defer_mark(void) */
  /* defer/owned cleanup thunk stack (cyexc.h) -- see the comment at
     exception_ensure_imports's rt_import calls for these. */
  MIR_item_t cy_defer_push_proto, cy_defer_push_item;   /* void cy_defer_push(fn,arg)     */
  MIR_item_t cy_defer_checkpoint_proto, cy_defer_checkpoint_item; /* size_t cy_defer_checkpoint(void) */
  MIR_item_t cy_defer_discard_one_proto, cy_defer_discard_one_item; /* void cy_defer_discard_one(void) */
  MIR_item_t cy_defer_release_to_proto, cy_defer_release_to_item; /* void cy_defer_release_to(mark) */
  MIR_item_t safety_trap_proto, safety_trap_item;       /* void  _safety_trap(reason,fid,line) */
  MIR_item_t cy_safe_alloc_proto, cy_safe_alloc_item;  /* void *cy_safe_alloc(size)           */
  MIR_item_t cy_safe_free_proto,  cy_safe_free_item;   /* void  cy_safe_free(ptr,line)        */
  MIR_item_t cy_safe_deref_proto, cy_safe_deref_item;  /* void  cy_safe_deref(ptr,line)       */
  /* -fobject-guards side-table UAF/double-free runtime (cy_obj_*) */
  MIR_item_t cy_obj_track_proto, cy_obj_track_item;         /* void cy_obj_track(ptr)          */
  MIR_item_t cy_obj_note_free_proto, cy_obj_note_free_item; /* void cy_obj_note_free(ptr,line) */
  MIR_item_t cy_obj_check_proto, cy_obj_check_item;         /* void cy_obj_check(ptr,line)     */
  /* -ffibers go/await runtime (cyfiber.h) — lazily imported on first use */
  MIR_item_t cy_spawn8_proto, cy_spawn8_item; /* void cy_spawn8(fn,nargs,a0..a7) */
  MIR_item_t cy_yield_proto, cy_yield_item;   /* void cy_yield(void)             */
  int exc_depth;                                        /* try nesting depth (label uids) */
  /* Midopt R-LICM memo: proven loop-invariant call nodes mapped to the op that
     holds their once-evaluated pre-header value (see gen_hoist_* helpers). */
#define GEN_HOIST_MAX 32
  node_t hoist_nodes[GEN_HOIST_MAX];
  op_t hoist_ops[GEN_HOIST_MAX];
  int hoist_n;
  VARR (MIR_op_t) * call_ops, *ret_ops, *switch_ops;
  VARR (case_t) * switch_cases;
  int curr_mir_proto_num;
  HTAB (MIR_item_t) * proto_tab;
  VARR (node_t) * node_stack;
  VARR (MIR_alias_t) * union_alias_done; /* union classes whose member conflicts are registered */
  /* Debug source location tracking for MIR instructions */
      uint16_t curr_src_file_id;
      uint32_t curr_src_line;
      uint16_t curr_src_col;
  };

#define zero_op gen_ctx->zero_op
#define one_op gen_ctx->one_op
#define minus_one_op gen_ctx->minus_one_op
#define curr_func gen_ctx->curr_func
#define slow_code_part gen_ctx->slow_code_part
#define reg_var_tab gen_ctx->reg_var_tab
#define reg_free_mark gen_ctx->reg_free_mark
#define continue_label gen_ctx->continue_label
#define break_label gen_ctx->break_label
#define top_gen_last_op gen_ctx->top_gen_last_op
#define stmtexpr_last_expr gen_ctx->stmtexpr_last_expr
#define proto_info gen_ctx->proto_info
#define init_els gen_ctx->init_els
#define memset_proto gen_ctx->memset_proto
#define memset_item gen_ctx->memset_item
#define memcpy_proto gen_ctx->memcpy_proto
#define memcpy_item gen_ctx->memcpy_item
#define memcmp_proto gen_ctx->memcmp_proto
#define memcmp_item gen_ctx->memcmp_item
#define malloc_proto gen_ctx->malloc_proto
#define malloc_item gen_ctx->malloc_item
#define free_proto gen_ctx->free_proto
#define free_item gen_ctx->free_item
#define defer_stmts gen_ctx->defer_stmts
#define defer_break_mark gen_ctx->defer_break_mark
#define defer_continue_mark gen_ctx->defer_continue_mark
#define new_proto_count gen_ctx->new_proto_count
#define dict_init_funcs gen_ctx->dict_init_funcs
#define dict_init_func_count gen_ctx->dict_init_func_count
#define dict_create_object_proto gen_ctx->dict_create_object_proto
#define dict_create_object_item gen_ctx->dict_create_object_item
#define dict_create_bool_proto gen_ctx->dict_create_bool_proto
#define dict_create_bool_item gen_ctx->dict_create_bool_item
#define dict_create_int64_proto gen_ctx->dict_create_int64_proto
#define dict_create_int64_item gen_ctx->dict_create_int64_item
#define dict_create_number_proto gen_ctx->dict_create_number_proto
#define dict_create_number_item gen_ctx->dict_create_number_item
#define dict_create_string_proto gen_ctx->dict_create_string_proto
#define dict_create_string_item gen_ctx->dict_create_string_item
#define dict_object_set_proto gen_ctx->dict_object_set_proto
#define dict_object_set_item gen_ctx->dict_object_set_item
#define dict_object_get_proto gen_ctx->dict_object_get_proto
#define dict_object_get_item gen_ctx->dict_object_get_item
#define dict_value_copy_proto gen_ctx->dict_value_copy_proto
#define dict_value_copy_item gen_ctx->dict_value_copy_item
#define dict_object_count_proto gen_ctx->dict_object_count_proto
#define dict_object_count_item gen_ctx->dict_object_count_item
#define dict_object_key_at_proto gen_ctx->dict_object_key_at_proto
#define dict_object_key_at_item gen_ctx->dict_object_key_at_item
#define dict_object_value_at_proto gen_ctx->dict_object_value_at_proto
#define dict_object_value_at_item gen_ctx->dict_object_value_at_item
#define dict_value_at_proto gen_ctx->dict_value_at_proto
#define dict_value_at_item gen_ctx->dict_value_at_item
#define dict_is_array_proto gen_ctx->dict_is_array_proto
#define dict_is_array_item gen_ctx->dict_is_array_item
#define dict_iter_count_proto gen_ctx->dict_iter_count_proto
#define dict_iter_count_item gen_ctx->dict_iter_count_item
#define dict_create_array_proto gen_ctx->dict_create_array_proto
#define dict_create_array_item gen_ctx->dict_create_array_item
#define dict_array_append_proto gen_ctx->dict_array_append_proto
#define dict_array_append_item gen_ctx->dict_array_append_item
#define dict_serialize_json_proto gen_ctx->dict_serialize_json_proto
#define dict_serialize_json_item gen_ctx->dict_serialize_json_item
#define dict_serialize_json_heap_proto gen_ctx->dict_serialize_json_heap_proto
#define dict_serialize_json_heap_item gen_ctx->dict_serialize_json_heap_item
#define dict_deserialize_json_proto gen_ctx->dict_deserialize_json_proto
#define dict_deserialize_json_item gen_ctx->dict_deserialize_json_item
#define dict_destroy_proto gen_ctx->dict_destroy_proto
#define dict_destroy_item  gen_ctx->dict_destroy_item
#define dict_create_heap_arena_proto gen_ctx->dict_create_heap_arena_proto
#define dict_create_heap_arena_item  gen_ctx->dict_create_heap_arena_item
#define str_length_proto gen_ctx->str_length_proto
#define str_length_item gen_ctx->str_length_item
#define str_empty_proto gen_ctx->str_empty_proto
#define str_empty_item gen_ctx->str_empty_item
#define str_substr_proto gen_ctx->str_substr_proto
#define str_substr_item gen_ctx->str_substr_item
#define str_find_proto gen_ctx->str_find_proto
#define str_find_item gen_ctx->str_find_item
#define str_replace_proto gen_ctx->str_replace_proto
#define str_replace_item gen_ctx->str_replace_item
#define str_replace_all_proto gen_ctx->str_replace_all_proto
#define str_replace_all_item gen_ctx->str_replace_all_item
#define str_upper_proto gen_ctx->str_upper_proto
#define str_upper_item gen_ctx->str_upper_item
#define str_lower_proto gen_ctx->str_lower_proto
#define str_lower_item gen_ctx->str_lower_item
#define str_starts_with_proto gen_ctx->str_starts_with_proto
#define str_starts_with_item gen_ctx->str_starts_with_item
#define str_ends_with_proto gen_ctx->str_ends_with_proto
#define str_ends_with_item gen_ctx->str_ends_with_item
#define str_contains_proto gen_ctx->str_contains_proto
#define str_contains_item gen_ctx->str_contains_item
#define str_trim_proto gen_ctx->str_trim_proto
#define str_trim_item gen_ctx->str_trim_item
#define str_split_proto gen_ctx->str_split_proto
#define str_split_item gen_ctx->str_split_item
#define str_join_proto gen_ctx->str_join_proto
#define str_join_item gen_ctx->str_join_item
#define str_equals_proto gen_ctx->str_equals_proto
#define str_equals_item gen_ctx->str_equals_item
#define str_detach_proto gen_ctx->str_detach_proto
#define str_detach_item gen_ctx->str_detach_item
#define str_own_proto gen_ctx->str_own_proto
#define str_own_item gen_ctx->str_own_item
#define str_drop_proto gen_ctx->str_drop_proto
#define str_drop_item gen_ctx->str_drop_item
#define str_attach_proto gen_ctx->str_attach_proto
#define str_attach_item gen_ctx->str_attach_item
#define str_checkpoint_proto gen_ctx->str_checkpoint_proto
#define str_checkpoint_item gen_ctx->str_checkpoint_item
#define str_release_to_proto gen_ctx->str_release_to_proto
#define str_release_to_item gen_ctx->str_release_to_item
#define str_release_keeping_proto gen_ctx->str_release_keeping_proto
#define str_release_keeping_item gen_ctx->str_release_keeping_item
#define str_copy_proto gen_ctx->str_copy_proto
#define str_copy_item gen_ctx->str_copy_item
#define str_concat_proto gen_ctx->str_concat_proto
#define str_concat_item gen_ctx->str_concat_item
#define str_from_i64_proto gen_ctx->str_from_i64_proto
#define str_from_double_proto gen_ctx->str_from_double_proto
#define str_from_int_item gen_ctx->str_from_int_item
#define str_from_uint_item gen_ctx->str_from_uint_item
#define str_from_bool_item gen_ctx->str_from_bool_item
#define str_from_char_item gen_ctx->str_from_char_item
#define str_from_double_item gen_ctx->str_from_double_item
#define str_scope_mark gen_ctx->str_scope_mark
#define str_scope_active gen_ctx->str_scope_active
#define obj_checkpoint_proto gen_ctx->obj_checkpoint_proto
#define obj_checkpoint_item gen_ctx->obj_checkpoint_item
#define obj_release_to_proto gen_ctx->obj_release_to_proto
#define obj_release_to_item gen_ctx->obj_release_to_item
#define obj_detach_proto gen_ctx->obj_detach_proto
#define obj_detach_item gen_ctx->obj_detach_item
#define obj_scope_mark gen_ctx->obj_scope_mark
#define obj_scope_active gen_ctx->obj_scope_active
#define loop_str_scope_mark gen_ctx->loop_str_scope_mark
#define loop_str_scope_active gen_ctx->loop_str_scope_active
#define loop_obj_scope_mark gen_ctx->loop_obj_scope_mark
#define loop_obj_scope_active gen_ctx->loop_obj_scope_active
#define loop_break_label_for_scope gen_ctx->loop_break_label_for_scope
#define cy_exc_push_proto gen_ctx->cy_exc_push_proto
#define cy_exc_push_item gen_ctx->cy_exc_push_item
#define cy_exc_pop_proto gen_ctx->cy_exc_pop_proto
#define cy_exc_pop_item gen_ctx->cy_exc_pop_item
#define cy_exc_current_proto gen_ctx->cy_exc_current_proto
#define cy_exc_current_item gen_ctx->cy_exc_current_item
#define cy_exc_throw_proto gen_ctx->cy_exc_throw_proto
#define cy_exc_throw_item gen_ctx->cy_exc_throw_item
#define cy_exc_set_marks_proto gen_ctx->cy_exc_set_marks_proto
#define cy_exc_set_marks_item gen_ctx->cy_exc_set_marks_item
#define cy_exc_current_str_mark_proto gen_ctx->cy_exc_current_str_mark_proto
#define cy_exc_current_str_mark_item gen_ctx->cy_exc_current_str_mark_item
#define cy_exc_current_obj_mark_proto gen_ctx->cy_exc_current_obj_mark_proto
#define cy_exc_current_obj_mark_item gen_ctx->cy_exc_current_obj_mark_item
#define cy_exc_current_defer_mark_proto gen_ctx->cy_exc_current_defer_mark_proto
#define cy_exc_current_defer_mark_item gen_ctx->cy_exc_current_defer_mark_item
#define cy_defer_push_proto gen_ctx->cy_defer_push_proto
#define cy_defer_push_item gen_ctx->cy_defer_push_item
#define cy_defer_checkpoint_proto gen_ctx->cy_defer_checkpoint_proto
#define cy_defer_checkpoint_item gen_ctx->cy_defer_checkpoint_item
#define cy_defer_discard_one_proto gen_ctx->cy_defer_discard_one_proto
#define cy_defer_discard_one_item gen_ctx->cy_defer_discard_one_item
#define cy_defer_release_to_proto gen_ctx->cy_defer_release_to_proto
#define cy_defer_release_to_item gen_ctx->cy_defer_release_to_item
#define cy_setjmp_proto gen_ctx->cy_setjmp_proto
#define cy_setjmp_item gen_ctx->cy_setjmp_item
#define safety_trap_proto gen_ctx->safety_trap_proto
#define safety_trap_item gen_ctx->safety_trap_item
#define cy_safe_alloc_proto gen_ctx->cy_safe_alloc_proto
#define cy_safe_alloc_item gen_ctx->cy_safe_alloc_item
#define cy_safe_free_proto gen_ctx->cy_safe_free_proto
#define cy_safe_free_item gen_ctx->cy_safe_free_item
#define cy_safe_deref_proto gen_ctx->cy_safe_deref_proto
#define cy_safe_deref_item gen_ctx->cy_safe_deref_item
#define cy_obj_track_proto gen_ctx->cy_obj_track_proto
#define cy_obj_track_item gen_ctx->cy_obj_track_item
#define cy_obj_note_free_proto gen_ctx->cy_obj_note_free_proto
#define cy_obj_note_free_item gen_ctx->cy_obj_note_free_item
#define cy_obj_check_proto gen_ctx->cy_obj_check_proto
#define cy_obj_check_item gen_ctx->cy_obj_check_item
#define cy_spawn8_proto gen_ctx->cy_spawn8_proto
#define cy_spawn8_item gen_ctx->cy_spawn8_item
#define cy_yield_proto gen_ctx->cy_yield_proto
#define cy_yield_item gen_ctx->cy_yield_item
#define exc_depth gen_ctx->exc_depth
#define hoist_nodes gen_ctx->hoist_nodes
#define hoist_ops gen_ctx->hoist_ops
#define hoist_n gen_ctx->hoist_n
#define call_ops gen_ctx->call_ops
#define ret_ops gen_ctx->ret_ops
#define switch_ops gen_ctx->switch_ops
#define switch_cases gen_ctx->switch_cases
#define curr_mir_proto_num gen_ctx->curr_mir_proto_num
#define proto_tab gen_ctx->proto_tab
#define node_stack gen_ctx->node_stack
#define union_alias_done gen_ctx->union_alias_done
#define curr_src_file_id gen_ctx->curr_src_file_id
#define curr_src_line gen_ctx->curr_src_line
#define curr_src_col gen_ctx->curr_src_col

static op_t new_op (decl_t decl, MIR_op_t mir_op) {
  op_t res;

  res.decl = decl;
  res.mir_op = mir_op;
  return res;
}

static htab_hash_t reg_var_hash (reg_var_t r, void *arg MIR_UNUSED) {
  return (htab_hash_t) mir_hash (r.name, strlen (r.name), 0x42);
}
static int reg_var_eq (reg_var_t r1, reg_var_t r2, void *arg MIR_UNUSED) {
  return strcmp (r1.name, r2.name) == 0;
}

static void init_reg_vars (c2m_ctx_t c2m_ctx) {
  MIR_alloc_t alloc = c2m_alloc (c2m_ctx);
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;

  reg_free_mark = 0;
  hoist_n = 0; /* R-LICM memo is per-function */
  HTAB_CREATE (reg_var_t, reg_var_tab, alloc, 128, reg_var_hash, reg_var_eq, NULL);
}

static void finish_curr_func_reg_vars (c2m_ctx_t c2m_ctx) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;

  reg_free_mark = 0;
  HTAB_CLEAR (reg_var_t, reg_var_tab);
}

static void finish_reg_vars (c2m_ctx_t c2m_ctx) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;

  if (reg_var_tab != NULL) HTAB_DESTROY (reg_var_t, reg_var_tab);
}

static reg_var_t get_reg_var (c2m_ctx_t c2m_ctx, MIR_type_t t, const char *reg_name,
                              const char *asm_str) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_context_t ctx = c2m_ctx->ctx;
  reg_var_t reg_var, el;
  char *str;
  MIR_reg_t reg;

  reg_var.name = reg_name;
  if (HTAB_DO (reg_var_t, reg_var_tab, reg_var, HTAB_FIND, el)) return el;
  t = t == MIR_T_I32 || t == MIR_T_U32 || t == MIR_T_U64 ? MIR_T_I64 : t;
  if (asm_str == NULL) {
    reg = (t != MIR_T_UNDEF ? MIR_new_func_reg (ctx, curr_func->u.func, t, reg_name)
                            : MIR_reg (ctx, reg_name, curr_func->u.func));
  } else {
    reg = MIR_new_global_func_reg (ctx, curr_func->u.func, t, reg_name, asm_str);
  }
  str = reg_malloc (c2m_ctx, (strlen (reg_name) + 1) * sizeof (char));
  strcpy (str, reg_name);
  reg_var.name = str;
  reg_var.reg = reg;
  HTAB_DO (reg_var_t, reg_var_tab, reg_var, HTAB_INSERT, el);
  return reg_var;
}

static int temp_reg_p (c2m_ctx_t c2m_ctx, MIR_op_t op) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_context_t ctx = c2m_ctx->ctx;

  return op.mode == MIR_OP_REG && MIR_reg_name (ctx, op.u.reg, curr_func->u.func)[1] == '_';
}

static MIR_type_t reg_type (c2m_ctx_t c2m_ctx, MIR_reg_t reg) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_context_t ctx = c2m_ctx->ctx;
  const char *n = MIR_reg_name (ctx, reg, curr_func->u.func);
  MIR_type_t res;

  if (strcmp (n, FP_NAME) == 0 || strcmp (n, RET_ADDR_NAME) == 0) return MIR_POINTER_TYPE;
  res = (n[0] == 'I'   ? MIR_T_I64
         : n[0] == 'U' ? MIR_T_U64
         : n[0] == 'i' ? MIR_T_I32
         : n[0] == 'u' ? MIR_T_U32
         : n[0] == 'f' ? MIR_T_F
         : n[0] == 'd' ? MIR_T_D
         : n[0] == 'D' ? MIR_T_LD
                       : MIR_T_BOUND);
  assert (res != MIR_T_BOUND);
  return res;
}

static op_t get_new_temp (c2m_ctx_t c2m_ctx, MIR_type_t t) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_context_t ctx = c2m_ctx->ctx;
  char reg_name[50];
  MIR_reg_t reg;

  assert (t == MIR_T_I64 || t == MIR_T_U64 || t == MIR_T_I32 || t == MIR_T_U32 || t == MIR_T_F
          || t == MIR_T_D || t == MIR_T_LD);
  sprintf (reg_name,
           t == MIR_T_I64   ? "I_%u"
           : t == MIR_T_U64 ? "U_%u"
           : t == MIR_T_I32 ? "i_%u"
           : t == MIR_T_U32 ? "u_%u"
           : t == MIR_T_F   ? "f_%u"
           : t == MIR_T_D   ? "d_%u"
                            : "D_%u",
           reg_free_mark++);
  reg = get_reg_var (c2m_ctx, t, reg_name, NULL).reg;
  return new_op (NULL, MIR_new_reg_op (ctx, reg));
}

static MIR_type_t get_int_mir_type (size_t size) {
  return size == 1 ? MIR_T_I8 : size == 2 ? MIR_T_I16 : size == 4 ? MIR_T_I32 : MIR_T_I64;
}

static int MIR_UNUSED get_int_mir_type_size (MIR_type_t t) {
  return (t == MIR_T_I8 || t == MIR_T_U8     ? 1
          : t == MIR_T_I16 || t == MIR_T_U16 ? 2
          : t == MIR_T_I32 || t == MIR_T_U32 ? 4
                                             : 8);
}

static MIR_type_t get_mir_type (c2m_ctx_t c2m_ctx, struct type *type) {
  size_t size = raw_type_size (c2m_ctx, type);
  if (type->mode == TM_DICT) return MIR_T_I64;  /* dict is a DictValue* pointer */
  if (type->mode == TM_SLICE) return MIR_T_I64; /* slice is a header pointer */
  int int_p = !floating_type_p (type), signed_p = signed_integer_type_p (type);

  //RSD: New
  if (type->mode == TM_CLASS || type->mode == TM_STRUCT || type->mode == TM_UNION)
        return MIR_T_UNDEF;  // Classes treated as aggregates like structs

  if (!scalar_type_p (type)) return MIR_T_UNDEF;
  assert (type->mode == TM_BASIC || type->mode == TM_PTR || type->mode == TM_ENUM);
  if (!int_p) {
    assert (size == 4 || size == 8 || size == sizeof (mir_ldouble));
    return size == 4 ? MIR_T_F : size == 8 ? MIR_T_D : MIR_T_LD;
  }
  assert (size <= 2 || size == 4 || size == 8);
  if (signed_p) return get_int_mir_type (size);
  return size == 1 ? MIR_T_U8 : size == 2 ? MIR_T_U16 : size == 4 ? MIR_T_U32 : MIR_T_U64;
}

static MIR_type_t promote_mir_int_type (MIR_type_t t) {
  return (t == MIR_T_I8 || t == MIR_T_I16   ? MIR_T_I32
          : t == MIR_T_U8 || t == MIR_T_U16 ? MIR_T_U32
                                            : t);
}

static MIR_type_t get_op_type (c2m_ctx_t c2m_ctx, op_t op) {
  switch (op.mir_op.mode) {
  case MIR_OP_MEM: return op.mir_op.u.mem.type;
  case MIR_OP_REG: return reg_type (c2m_ctx, op.mir_op.u.reg);
  case MIR_OP_INT: return MIR_T_I64;
  case MIR_OP_UINT: return MIR_T_U64;
  case MIR_OP_FLOAT: return MIR_T_F;
  case MIR_OP_DOUBLE: return MIR_T_D;
  case MIR_OP_LDOUBLE: return MIR_T_LD;
  default: assert (FALSE); return MIR_T_I64;
  }
}

static void push_val (VARR (char) * repr, mir_long val) {
  mir_long bound;

  for (bound = 10; val >= bound;) bound *= 10;
  while (bound != 1) {
    bound /= 10;
    VARR_PUSH (char, repr, '0' + val / bound);
    val %= bound;
  }
}

static void get_type_alias_name (c2m_ctx_t c2m_ctx, struct type *type, VARR (char) * name) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  enum basic_type basic_type;
  size_t i;

  switch (type->mode) {
  case TM_ENUM: basic_type = get_enum_basic_type (type); goto basic;
  case TM_BASIC:
    basic_type = type->u.basic_type;
  basic:
    switch (basic_type) {
    case TP_VOID: VARR_PUSH (char, name, 'v'); break;
    case TP_BOOL: VARR_PUSH (char, name, 'b'); break;
    case TP_CHAR:
    case TP_SCHAR:
    case TP_UCHAR: VARR_PUSH (char, name, 'c'); break;
    case TP_SHORT:
    case TP_USHORT: VARR_PUSH (char, name, 's'); break;
    case TP_INT:
    case TP_UINT: VARR_PUSH (char, name, 'i'); break;
    case TP_LONG:
    case TP_ULONG: VARR_PUSH (char, name, 'l'); break;
    case TP_LLONG:
    case TP_ULLONG: VARR_PUSH (char, name, 'L'); break;
    case TP_FLOAT: VARR_PUSH (char, name, 'f'); break;
    case TP_DOUBLE: VARR_PUSH (char, name, 'd'); break;
    case TP_LDOUBLE: VARR_PUSH (char, name, 'D'); break;
    case TP_STRING:
      /* A managed String is a pointer-sized value; alias it as pointer-to-char
         to match its runtime representation. */
      VARR_PUSH (char, name, 'p');
      VARR_PUSH (char, name, 'c');
      break;
    default: assert (FALSE);
    }
    break;
  case TM_PTR:
    VARR_PUSH (char, name, 'p');
    get_type_alias_name (c2m_ctx, type->u.ptr_type, name);
    break;
  case TM_STRUCT:
  case TM_UNION:
  case TM_CLASS:
    VARR_PUSH (char, name, type->mode == TM_STRUCT ? 'S' : type->mode == TM_CLASS ? 'C' : 'U');
    for (i = 0; i < VARR_LENGTH (node_t, node_stack); i++)
      if (VARR_GET (node_t, node_stack, i) == type->u.tag_type) break;
    if (i < VARR_LENGTH (node_t, node_stack)) {
      VARR_PUSH (char, name, 'r');
      push_val (name, (mir_long) i);
    } else {
      VARR_PUSH (node_t, node_stack, type->u.tag_type);
      for (node_t member = NL_HEAD (NL_EL (type->u.tag_type->u.ops, 1)->u.ops); member != NULL;
           member = NL_NEXT (member))
        if (member->code == N_MEMBER) {
          decl_t decl = member->attr;
          node_t width = NL_EL (member->u.ops, 3);
          struct expr *expr;

          get_type_alias_name (c2m_ctx, decl->decl_spec.type, name);
          if (width->code != N_IGNORE && (expr = width->attr)->const_p) {
            VARR_PUSH (char, name, 'w');
            push_val (name, (mir_long) expr->c.u_val);
            for (mir_ullong v = expr->c.u_val;;) {
              VARR_PUSH (char, name, v % 10 + '0');
              v /= 10;
              if (v == 0) break;
            }
          }
        }
    }
    VARR_PUSH (char, name, 'e');
    break;
  case TM_ARR:
    VARR_PUSH (char, name, 'A');
    get_type_alias_name (c2m_ctx, type->u.arr_type->el_type, name);
    break;
  case TM_FUNC:
    VARR_PUSH (char, name, 'F');
    get_type_alias_name (c2m_ctx, type->u.func_type->ret_type, name);
    for (node_t p = NL_HEAD (type->u.func_type->param_list->u.ops); p != NULL; p = NL_NEXT (p)) {
      struct decl_spec *ds = get_param_decl_spec (p);
      get_type_alias_name (c2m_ctx, ds->type, name);
    }
    VARR_PUSH (char, name, type->u.func_type->dots_p ? 'E' : 'e');
    break;
  case TM_DICT:
  case TM_SLICE:
    VARR_PUSH (char, name, 'p'); /* dict/slice is a pointer alias */
    VARR_PUSH (char, name, 'v'); /* to void (opaque) */
    break;
  default: assert (FALSE);
  }
}

static MIR_alias_t get_type_alias (c2m_ctx_t c2m_ctx, struct type *type);

/* A union access keeps the union's alias class while a pointer-deref access to
   the same memory carries the member type's own class, so the two classes must
   be declared conflicting (type punning through unions is valid C).  Walk the
   union's value-embedded member tree (pointers are leaves). */
static void add_union_member_conflicts (c2m_ctx_t c2m_ctx, struct type *type,
                                        MIR_alias_t union_alias) {
  MIR_context_t ctx = c2m_ctx->ctx;
  MIR_alias_t alias;

  switch (type->mode) {
  case TM_ARR: add_union_member_conflicts (c2m_ctx, type->u.arr_type->el_type, union_alias); break;
  case TM_STRUCT:
  case TM_CLASS:
  case TM_UNION:
    if (type->mode == TM_UNION && (alias = get_type_alias (c2m_ctx, type)) != 0)
      MIR_add_alias_conflict (ctx, union_alias, alias);
    for (node_t member = NL_HEAD (NL_EL (type->u.tag_type->u.ops, 1)->u.ops); member != NULL;
         member = NL_NEXT (member))
      if (member->code == N_MEMBER) {
        decl_t decl = member->attr;

        add_union_member_conflicts (c2m_ctx, decl->decl_spec.type, union_alias);
      }
    break;
  case TM_FUNC: break;
  default:
    if ((alias = get_type_alias (c2m_ctx, type)) != 0)
      MIR_add_alias_conflict (ctx, union_alias, alias);
    break;
  }
}

static MIR_alias_t get_type_alias (c2m_ctx_t c2m_ctx, struct type *type) {
  MIR_context_t ctx = c2m_ctx->ctx;
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_alias_t alias;

  switch (type->mode) {
  case TM_BASIC:
    if (type->u.basic_type != TP_CHAR && type->u.basic_type != TP_SCHAR
        && type->u.basic_type != TP_UCHAR)
      break;
    /* fall through */
  case TM_UNDEF:
  case TM_STRUCT:
  case TM_CLASS:
  case TM_ARR:
  case TM_FUNC: return 0;
  default: break;
  }
  VARR_TRUNC (node_t, node_stack, 0);
  VARR_TRUNC (char, temp_string, 0);
  get_type_alias_name (c2m_ctx, type, temp_string);
  VARR_PUSH (char, temp_string, '\0');
  alias = MIR_alias (ctx, VARR_ADDR (char, temp_string));
  if (type->mode == TM_UNION && alias != 0) {
    size_t i;

    for (i = 0; i < VARR_LENGTH (MIR_alias_t, union_alias_done); i++)
      if (VARR_GET (MIR_alias_t, union_alias_done, i) == alias) break;
    if (i >= VARR_LENGTH (MIR_alias_t, union_alias_done)) {
      VARR_PUSH (MIR_alias_t, union_alias_done, alias);
      for (node_t member = NL_HEAD (NL_EL (type->u.tag_type->u.ops, 1)->u.ops); member != NULL;
           member = NL_NEXT (member))
        if (member->code == N_MEMBER) {
          decl_t decl = member->attr;

          add_union_member_conflicts (c2m_ctx, decl->decl_spec.type, alias);
        }
    }
  }
  return alias;
}

static op_t gen (c2m_ctx_t c2m_ctx, node_t r, MIR_label_t true_label, MIR_label_t false_label,
                 int val_p, op_t *desirable_dest, int *expect_res);
#if !MIR_NO_DBINFO
static void dbinfo_emit_func_vars (c2m_ctx_t c2m_ctx, node_t func_def_node);
#endif

/* Pre-generate methods of block-scoped classes found under NODE so their MIR
   items exist before the enclosing function is opened.  Idempotent. */
static void gen_nested_class_methods_in (c2m_ctx_t c2m_ctx, node_t node) {
  if (node == NULL) return;
  switch (node->code) {
  case N_LIST:
    for (node_t el = NL_HEAD (node->u.ops); el != NULL; el = NL_NEXT (el))
      gen_nested_class_methods_in (c2m_ctx, el);
    break;
  case N_BLOCK:
    gen_nested_class_methods_in (c2m_ctx, NL_EL (node->u.ops, 1));
    break;
  case N_SPEC_DECL: {
    node_t specs = SPEC_DECL_SPECS (node);
    if (specs != NULL && specs->code == N_SHARE) specs = NL_HEAD (specs->u.ops);
    if (specs != NULL && specs->code == N_LIST) {
      for (node_t s = NL_HEAD (specs->u.ops); s != NULL; s = NL_NEXT (s)) {
        if (s->code == N_CLASS && s->attr != (void *) ((intptr_t) -1))
          gen_nested_class_methods_in (c2m_ctx, s);
      }
    }
    break;
  }
  case N_CLASS: {
    node_t decl_list = NL_NEXT (NL_HEAD (node->u.ops));
    if (decl_list == NULL || decl_list->code == N_IGNORE) break;
    for (node_t m = NL_HEAD (decl_list->u.ops); m != NULL; m = NL_NEXT (m)) {
      if (m->code != N_FUNC_DEF || m->attr == (void *) ((intptr_t) -1)) continue;
      decl_t md = m->attr;
      if (md != NULL && md->u.item != NULL) continue;
      gen (c2m_ctx, m, NULL, NULL, FALSE, NULL, NULL);
    }
    break;
  }
  case N_IF:
    gen_nested_class_methods_in (c2m_ctx, NL_EL (node->u.ops, 1));
    gen_nested_class_methods_in (c2m_ctx, NL_EL (node->u.ops, 2));
    gen_nested_class_methods_in (c2m_ctx, NL_EL (node->u.ops, 3));
    break;
  case N_WHILE: case N_DO: case N_SWITCH:
    gen_nested_class_methods_in (c2m_ctx, NL_EL (node->u.ops, 1));
    break;
  case N_FOR:
    gen_nested_class_methods_in (c2m_ctx, NL_EL (node->u.ops, 4));
    break;
  default: break;
  }
}

static op_t val_gen (c2m_ctx_t c2m_ctx, node_t r) {
  return gen (c2m_ctx, r, NULL, NULL, TRUE, NULL, NULL);
}

/* C11 compile-time truth of a scalar expr.  1 = known, *truth is 0/1.
   Shared by gen (skip dead if/?:/&& arms) and midopt (keep/safety). */
static int c11_const_truth (node_t n, int *truth) {
  struct expr *e;
  struct type *t;

  if (n == NULL || n->attr == NULL || n->attr == (void *) ((intptr_t) -1)) return 0;
  e = (struct expr *) n->attr;
  t = e->type;
  if (!e->const_p || t == NULL) return 0;
  if (floating_type_p (t))
    *truth = e->c.d_val != 0.0;
  else if (signed_integer_type_p (t))
    *truth = e->c.i_val != 0;
  else
    *truth = e->c.u_val != 0;
  return 1;
}

/* 1 = always true, 0 = always false, -1 = unknown.
   `x && 0` is always false after evaluating x (check must not mark that
   ANDAND const_p — gen would skip x). */
static int c11_cond_known_1 (node_t n, int depth) {
  int t, l, r;
  node_t a, b;

  if (n == NULL || depth > 32) return -1;
  if (c11_const_truth (n, &t)) return t ? 1 : 0;
  switch (n->code) {
  case N_ANDAND:
    a = NL_HEAD (n->u.ops);
    b = a != NULL ? NL_NEXT (a) : NULL;
    l = c11_cond_known_1 (a, depth + 1);
    if (l == 0) return 0;
    r = c11_cond_known_1 (b, depth + 1);
    if (r == 0) return 0;
    if (l == 1 && r == 1) return 1;
    return -1;
  case N_OROR:
    a = NL_HEAD (n->u.ops);
    b = a != NULL ? NL_NEXT (a) : NULL;
    l = c11_cond_known_1 (a, depth + 1);
    if (l == 1) return 1;
    r = c11_cond_known_1 (b, depth + 1);
    if (r == 1) return 1;
    if (l == 0 && r == 0) return 0;
    return -1;
  case N_NOT:
    t = c11_cond_known_1 (NL_HEAD (n->u.ops), depth + 1);
    if (t < 0) return -1;
    return t ? 0 : 1;
  case N_COMMA:
    return c11_cond_known_1 (NL_EL (n->u.ops, 1), depth + 1);
  case N_CAST:
    return c11_cond_known_1 (NL_EL (n->u.ops, 1), depth + 1);
  default:
    return -1;
  }
}

static int c11_cond_known (node_t n) { return c11_cond_known_1 (n, 0); }

/* Leaf nodes store a payload in the ops union — do not walk them. */
static int c11_node_has_ops (node_code_t code) {
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

/* C allows `goto` into a const-false if/for/while body (20040704-1, pr17078-1).
   Only skip a dead region when nothing can jump into it. */
static int c11_has_label_p (node_t n) {
  if (n == NULL) return 0;
  if (n->code == N_LABEL || n->code == N_CASE || n->code == N_DEFAULT) return 1;
  if (!c11_node_has_ops (n->code)) return 0;
  for (node_t c = NL_HEAD (n->u.ops); c != NULL; c = NL_NEXT (c))
    if (c11_has_label_p (c)) return 1;
  return 0;
}

static int c11_dead_skippable_p (node_t n) {
  return n == NULL || n->code == N_IGNORE || !c11_has_label_p (n);
}

static int push_const_val (c2m_ctx_t c2m_ctx, node_t r, op_t *res) {
  MIR_context_t ctx = c2m_ctx->ctx;
  struct expr *e = (struct expr *) r->attr;
  MIR_type_t mir_type;

  if (!e->const_p) return FALSE;
  if (floating_type_p (e->type)) {
    /* MIR support only IEEE float and double */
    mir_type = get_mir_type (c2m_ctx, e->type);
    *res = new_op (NULL, (mir_type == MIR_T_F   ? MIR_new_float_op (ctx, (float) e->c.d_val)
                          : mir_type == MIR_T_D ? MIR_new_double_op (ctx, e->c.d_val)
                                                : MIR_new_ldouble_op (ctx, e->c.d_val)));
  } else {
    /* `String` is a pointer-width handle (TM_BASIC tag), so a constant
       String (e.g. `(String)0`) is materialized like an unsigned pointer. */
    assert (integer_type_p (e->type) || e->type->mode == TM_PTR
            || builtin_string_type_p (e->type));
    *res = new_op (NULL, (signed_integer_type_p (e->type) ? MIR_new_int_op (ctx, e->c.i_val)
                                                          : MIR_new_uint_op (ctx, e->c.u_val)));
  }
  return TRUE;
}

static MIR_insn_code_t tp_mov (MIR_type_t t) {
  return t == MIR_T_F ? MIR_FMOV : t == MIR_T_D ? MIR_DMOV : t == MIR_T_LD ? MIR_LDMOV : MIR_MOV;
}

/* Stamp the current AST source location onto INSN when -g is on.
   emit_insn_opt used to skip this, so most generated instructions (and
   therefore DWARF line records / GDB stepping) had no location. */
static void emit_stamp (c2m_ctx_t c2m_ctx, MIR_insn_t insn) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;

  if (c2m_options->debug_info_p && curr_src_line != 0 && curr_src_file_id != 0)
    MIR_insn_set_source_loc (insn, curr_src_file_id, curr_src_line, curr_src_col);
}

static void emit_insn (c2m_ctx_t c2m_ctx, MIR_insn_t insn) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;

  emit_stamp (c2m_ctx, insn);
  MIR_append_insn (c2m_ctx->ctx, curr_func, insn);
}

/* BCOND T, L1; JMP L2; L1: => BNCOND T, L2; L1:
   JMP L; L: => L: */
static void emit_label_insn_opt (c2m_ctx_t c2m_ctx, MIR_insn_t insn) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_context_t ctx = c2m_ctx->ctx;
  MIR_insn_code_t rev_code;
  MIR_insn_t last, prev;

  assert (insn->code == MIR_LABEL);
  if ((last = DLIST_TAIL (MIR_insn_t, curr_func->u.func->insns)) != NULL
      && (prev = DLIST_PREV (MIR_insn_t, last)) != NULL && last->code == MIR_JMP
      && (rev_code = MIR_reverse_branch_code (prev->code)) != MIR_INSN_BOUND
      && prev->ops[0].mode == MIR_OP_LABEL && prev->ops[0].u.label == insn) {
    prev->ops[0] = last->ops[0];
    prev->code = rev_code;
    MIR_remove_insn (ctx, curr_func, last);
  }
  if ((last = DLIST_TAIL (MIR_insn_t, curr_func->u.func->insns)) != NULL && last->code == MIR_JMP
      && last->ops[0].mode == MIR_OP_LABEL && last->ops[0].u.label == insn) {
    MIR_remove_insn (ctx, curr_func, last);
  }
  MIR_append_insn (ctx, curr_func, insn);
}

/* Change t1 = expr; v = t1 to v = expr */
static void emit_insn_opt (c2m_ctx_t c2m_ctx, MIR_insn_t insn) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_context_t ctx = c2m_ctx->ctx;
  MIR_insn_t tail;
  int out_p;

  if ((insn->code == MIR_MOV || insn->code == MIR_FMOV || insn->code == MIR_DMOV
       || insn->code == MIR_LDMOV)
      && (tail = DLIST_TAIL (MIR_insn_t, curr_func->u.func->insns)) != NULL
      && MIR_insn_nops (ctx, tail) > 0 && temp_reg_p (c2m_ctx, insn->ops[1])
      && !temp_reg_p (c2m_ctx, insn->ops[0]) && temp_reg_p (c2m_ctx, tail->ops[0])
      && insn->ops[1].u.reg == tail->ops[0].u.reg) {
    MIR_insn_op_mode (ctx, tail, 0, &out_p);
    if (out_p) {
      tail->ops[0] = insn->ops[0];
      emit_stamp (c2m_ctx, insn);
      MIR_append_insn (ctx, curr_func, insn);
      MIR_remove_insn (ctx, curr_func, insn);
      return;
    }
  }
  emit_stamp (c2m_ctx, insn);
  MIR_append_insn (ctx, curr_func, insn);
}

static void emit1 (c2m_ctx_t c2m_ctx, MIR_insn_code_t code, MIR_op_t op1) {
  emit_insn_opt (c2m_ctx, MIR_new_insn (c2m_ctx->ctx, code, op1));
}
static void emit2 (c2m_ctx_t c2m_ctx, MIR_insn_code_t code, MIR_op_t op1, MIR_op_t op2) {
  emit_insn_opt (c2m_ctx, MIR_new_insn (c2m_ctx->ctx, code, op1, op2));
}
static void emit3 (c2m_ctx_t c2m_ctx, MIR_insn_code_t code, MIR_op_t op1, MIR_op_t op2,
                   MIR_op_t op3) {
  emit_insn_opt (c2m_ctx, MIR_new_insn (c2m_ctx->ctx, code, op1, op2, op3));
}

static void emit2_noopt (c2m_ctx_t c2m_ctx, MIR_insn_code_t code, MIR_op_t op1, MIR_op_t op2) {
  emit_insn (c2m_ctx, MIR_new_insn (c2m_ctx->ctx, code, op1, op2));
}

static void emit3_noopt (c2m_ctx_t c2m_ctx, MIR_insn_code_t code, MIR_op_t op1, MIR_op_t op2,
                         MIR_op_t op3) {
  emit_insn (c2m_ctx, MIR_new_insn (c2m_ctx->ctx, code, op1, op2, op3));
}

static void emit4_noopt (c2m_ctx_t c2m_ctx, MIR_insn_code_t code, MIR_op_t op1, MIR_op_t op2,
                         MIR_op_t op3, MIR_op_t op4) {
  emit_insn (c2m_ctx, MIR_new_insn (c2m_ctx->ctx, code, op1, op2, op3, op4));
}

static int type_atomic_p (struct type *t) { return t != NULL && t->type_qual.atomic_p; }

static int op_decl_atomic_p (op_t op) {
  return op.decl != NULL && op.decl->decl_spec.type != NULL
         && op.decl->decl_spec.type->type_qual.atomic_p;
}

/* Atomic load from a MIR mem operand into a new temp (seq_cst). */
static op_t atomic_load_mem (c2m_ctx_t c2m_ctx, op_t mem, MIR_type_t t) {
  op_t res = get_new_temp (c2m_ctx, promote_mir_int_type (t));
  assert (mem.mir_op.mode == MIR_OP_MEM);
  /* Keep width in the mem type so mir-gen/interp pick the right size. */
  mem.mir_op.u.mem.type = t;
  emit2_noopt (c2m_ctx, MIR_ALOAD, res.mir_op, mem.mir_op);
  return res;
}

static void atomic_store_mem (c2m_ctx_t c2m_ctx, op_t mem, op_t val) {
  assert (mem.mir_op.mode == MIR_OP_MEM);
  emit2_noopt (c2m_ctx, MIR_ASTORE, mem.mir_op, val.mir_op);
}

static op_t cast (c2m_ctx_t c2m_ctx, op_t op, MIR_type_t t, int new_op_p) {
  op_t res, interm;
  MIR_type_t op_type;
  MIR_insn_code_t insn_code = MIR_INSN_BOUND, insn_code2 = MIR_INSN_BOUND;

  assert (t == MIR_T_I8 || t == MIR_T_U8 || t == MIR_T_I16 || t == MIR_T_U16 || t == MIR_T_I32
          || t == MIR_T_U32 || t == MIR_T_I64 || t == MIR_T_U64 || t == MIR_T_F || t == MIR_T_D
          || t == MIR_T_LD);
  switch (op.mir_op.mode) {
  case MIR_OP_MEM:
    op_type = op.mir_op.u.mem.type;
    if (op_type == MIR_T_UNDEF) { /* ??? it is an array */

    } else if (op_type == MIR_T_I8 || op_type == MIR_T_U8 || op_type == MIR_T_I16
               || op_type == MIR_T_U16 || op_type == MIR_T_I32 || op_type == MIR_T_U32)
      op_type = MIR_T_I64;
    goto all_types;
  case MIR_OP_REG:
    op_type = reg_type (c2m_ctx, op.mir_op.u.reg);
  all_types:
    if (op_type == MIR_T_F) goto float_val;
    if (op_type == MIR_T_D) goto double_val;
    if (op_type == MIR_T_LD) goto ldouble_val;
    if (t == MIR_T_I64) {
      insn_code = (op_type == MIR_T_I32   ? MIR_EXT32
                   : op_type == MIR_T_U32 ? MIR_UEXT32
                   : op_type == MIR_T_F   ? MIR_F2I
                   : op_type == MIR_T_D   ? MIR_D2I
                   : op_type == MIR_T_LD  ? MIR_LD2I
                                          : MIR_INSN_BOUND);
    } else if (t == MIR_T_U64) {
      insn_code = (op_type == MIR_T_I32   ? MIR_EXT32
                   : op_type == MIR_T_U32 ? MIR_UEXT32
                   : op_type == MIR_T_F   ? MIR_F2I
                   : op_type == MIR_T_D   ? MIR_D2I
                   : op_type == MIR_T_LD  ? MIR_LD2I
                                          : MIR_INSN_BOUND);
    } else if (t == MIR_T_I32) {
      insn_code = (op_type == MIR_T_F    ? MIR_F2I
                   : op_type == MIR_T_D  ? MIR_D2I
                   : op_type == MIR_T_LD ? MIR_LD2I
                                         : MIR_INSN_BOUND);
    } else if (t == MIR_T_U32) {
      insn_code = (op_type == MIR_T_F    ? MIR_F2I
                   : op_type == MIR_T_D  ? MIR_D2I
                   : op_type == MIR_T_LD ? MIR_LD2I
                                         : MIR_INSN_BOUND);
    } else if (t == MIR_T_I16) {
      insn_code = (op_type == MIR_T_F    ? MIR_F2I
                   : op_type == MIR_T_D  ? MIR_D2I
                   : op_type == MIR_T_LD ? MIR_LD2I
                                         : MIR_INSN_BOUND);
      insn_code2 = MIR_EXT16;
    } else if (t == MIR_T_U16) {
      insn_code = (op_type == MIR_T_F    ? MIR_F2I
                   : op_type == MIR_T_D  ? MIR_D2I
                   : op_type == MIR_T_LD ? MIR_LD2I
                                         : MIR_INSN_BOUND);
      insn_code2 = MIR_UEXT16;
    } else if (t == MIR_T_I8) {
      insn_code = (op_type == MIR_T_F    ? MIR_F2I
                   : op_type == MIR_T_D  ? MIR_D2I
                   : op_type == MIR_T_LD ? MIR_LD2I
                                         : MIR_INSN_BOUND);
      insn_code2 = MIR_EXT8;
    } else if (t == MIR_T_U8) {
      insn_code = (op_type == MIR_T_F    ? MIR_F2I
                   : op_type == MIR_T_D  ? MIR_D2I
                   : op_type == MIR_T_LD ? MIR_LD2I
                                         : MIR_INSN_BOUND);
      insn_code2 = MIR_UEXT8;
    } else if (t == MIR_T_F) {
      insn_code = (op_type == MIR_T_I32   ? MIR_EXT32
                   : op_type == MIR_T_U32 ? MIR_UEXT32
                                          : MIR_INSN_BOUND);
      insn_code2 = (op_type == MIR_T_I64 || op_type == MIR_T_I32   ? MIR_I2F
                    : op_type == MIR_T_U64 || op_type == MIR_T_U32 ? MIR_UI2F
                                                                   : MIR_INSN_BOUND);
    } else if (t == MIR_T_D) {
      insn_code = (op_type == MIR_T_I32   ? MIR_EXT32
                   : op_type == MIR_T_U32 ? MIR_UEXT32
                                          : MIR_INSN_BOUND);
      insn_code2 = (op_type == MIR_T_I64 || op_type == MIR_T_I32   ? MIR_I2D
                    : op_type == MIR_T_U64 || op_type == MIR_T_U32 ? MIR_UI2D
                                                                   : MIR_INSN_BOUND);
    } else if (t == MIR_T_LD) {
      insn_code = (op_type == MIR_T_I32   ? MIR_EXT32
                   : op_type == MIR_T_U32 ? MIR_UEXT32
                                          : MIR_INSN_BOUND);
      insn_code2 = (op_type == MIR_T_I64 || op_type == MIR_T_I32   ? MIR_I2LD
                    : op_type == MIR_T_U64 || op_type == MIR_T_U32 ? MIR_UI2LD
                                                                   : MIR_INSN_BOUND);
    }
    break;
  case MIR_OP_INT:
    insn_code = (t == MIR_T_I8    ? MIR_EXT8
                 : t == MIR_T_U8  ? MIR_UEXT8
                 : t == MIR_T_I16 ? MIR_EXT16
                 : t == MIR_T_U16 ? MIR_UEXT16
                 : t == MIR_T_F   ? MIR_I2F
                 : t == MIR_T_D   ? MIR_I2D
                 : t == MIR_T_LD  ? MIR_I2LD
                                  : MIR_INSN_BOUND);
    break;
  case MIR_OP_UINT:
    insn_code = (t == MIR_T_I8    ? MIR_EXT8
                 : t == MIR_T_U8  ? MIR_UEXT8
                 : t == MIR_T_I16 ? MIR_EXT16
                 : t == MIR_T_U16 ? MIR_UEXT16
                 : t == MIR_T_F   ? MIR_UI2F
                 : t == MIR_T_D   ? MIR_UI2D
                 : t == MIR_T_LD  ? MIR_UI2LD
                                  : MIR_INSN_BOUND);
    break;
  case MIR_OP_FLOAT:
  float_val:
    insn_code = (t == MIR_T_I8 || t == MIR_T_U8 || t == MIR_T_I16 || t == MIR_T_U16
                     || t == MIR_T_I32 || t == MIR_T_U32 || t == MIR_T_I64 || t == MIR_T_U64
                   ? MIR_F2I
                 : t == MIR_T_D  ? MIR_F2D
                 : t == MIR_T_LD ? MIR_F2LD
                                 : MIR_INSN_BOUND);
    insn_code2 = (t == MIR_T_I8    ? MIR_EXT8
                  : t == MIR_T_U8  ? MIR_UEXT8
                  : t == MIR_T_I16 ? MIR_EXT16
                  : t == MIR_T_U16 ? MIR_UEXT16
                                   : MIR_INSN_BOUND);
    break;
  case MIR_OP_DOUBLE:
  double_val:
    insn_code = (t == MIR_T_I8 || t == MIR_T_U8 || t == MIR_T_I16 || t == MIR_T_U16
                     || t == MIR_T_I32 || t == MIR_T_U32 || t == MIR_T_I64 || t == MIR_T_U64
                   ? MIR_D2I
                 : t == MIR_T_F  ? MIR_D2F
                 : t == MIR_T_LD ? MIR_D2LD
                                 : MIR_INSN_BOUND);
    insn_code2 = (t == MIR_T_I8    ? MIR_EXT8
                  : t == MIR_T_U8  ? MIR_UEXT8
                  : t == MIR_T_I16 ? MIR_EXT16
                  : t == MIR_T_U16 ? MIR_UEXT16
                                   : MIR_INSN_BOUND);
    break;
  case MIR_OP_LDOUBLE:
  ldouble_val:
    insn_code = (t == MIR_T_I8 || t == MIR_T_U8 || t == MIR_T_I16 || t == MIR_T_U16
                     || t == MIR_T_I32 || t == MIR_T_U32 || t == MIR_T_I64 || t == MIR_T_U64
                   ? MIR_LD2I
                 : t == MIR_T_F ? MIR_LD2F
                 : t == MIR_T_D ? MIR_LD2D
                                : MIR_INSN_BOUND);
    insn_code2 = (t == MIR_T_I8    ? MIR_EXT8
                  : t == MIR_T_U8  ? MIR_UEXT8
                  : t == MIR_T_I16 ? MIR_EXT16
                  : t == MIR_T_U16 ? MIR_UEXT16
                                   : MIR_INSN_BOUND);
    break;
  default: break;
  }
  if (!new_op_p && insn_code == MIR_INSN_BOUND && insn_code2 == MIR_INSN_BOUND) return op;
  res = get_new_temp (c2m_ctx, t == MIR_T_I8 || t == MIR_T_U8 || t == MIR_T_I16 || t == MIR_T_U16
                                 ? MIR_T_I64
                                 : t);
  if (insn_code == MIR_INSN_BOUND && insn_code2 == MIR_INSN_BOUND) {
    emit2 (c2m_ctx, tp_mov (t), res.mir_op, op.mir_op);
  } else if (insn_code == MIR_INSN_BOUND) {
    emit2 (c2m_ctx, insn_code2, res.mir_op, op.mir_op);
  } else if (insn_code2 == MIR_INSN_BOUND) {
    emit2 (c2m_ctx, insn_code, res.mir_op, op.mir_op);
  } else {
    interm = get_new_temp (c2m_ctx, MIR_T_I64);
    emit2 (c2m_ctx, insn_code, interm.mir_op, op.mir_op);
    emit2 (c2m_ctx, insn_code2, res.mir_op, interm.mir_op);
  }
  return res;
}

static op_t promote (c2m_ctx_t c2m_ctx, op_t op, MIR_type_t t, int new_op_p) {
  assert (t == MIR_T_I64 || t == MIR_T_U64 || t == MIR_T_I32 || t == MIR_T_U32 || t == MIR_T_F
          || t == MIR_T_D || t == MIR_T_LD);
  return cast (c2m_ctx, op, t, new_op_p);
}

static op_t mem_to_address (c2m_ctx_t c2m_ctx, op_t mem, int reg_p) {
  MIR_context_t ctx = c2m_ctx->ctx;
  op_t temp;

  if (mem.mir_op.mode == MIR_OP_STR) {
    if (!reg_p) return mem;
    temp = get_new_temp (c2m_ctx, MIR_T_I64);
    emit2 (c2m_ctx, MIR_MOV, temp.mir_op, mem.mir_op);
    temp.mir_op.value_mode = MIR_OP_INT;
    return temp;
  }
  assert (mem.mir_op.mode == MIR_OP_MEM);
  if (mem.mir_op.u.mem.base == 0 && mem.mir_op.u.mem.index == 0) {
    if (!reg_p) {
      mem.mir_op.mode = MIR_OP_INT;
      mem.mir_op.u.i = mem.mir_op.u.mem.disp;
    } else {
      temp = get_new_temp (c2m_ctx, MIR_T_I64);
      emit2 (c2m_ctx, MIR_MOV, temp.mir_op, MIR_new_int_op (ctx, mem.mir_op.u.mem.disp));
      mem = temp;
    }
  } else if (mem.mir_op.u.mem.index == 0 && mem.mir_op.u.mem.disp == 0) {
    mem.mir_op.mode = MIR_OP_REG;
    mem.mir_op.u.reg = mem.mir_op.u.mem.base;
  } else if (mem.mir_op.u.mem.index == 0) {
    temp = get_new_temp (c2m_ctx, MIR_T_I64);
    emit3 (c2m_ctx, MIR_ADD, temp.mir_op, MIR_new_reg_op (ctx, mem.mir_op.u.mem.base),
           MIR_new_int_op (ctx, mem.mir_op.u.mem.disp));
    mem = temp;
  } else {
    temp = get_new_temp (c2m_ctx, MIR_T_I64);
    if (mem.mir_op.u.mem.scale != 1)
      emit3 (c2m_ctx, MIR_MUL, temp.mir_op, MIR_new_reg_op (ctx, mem.mir_op.u.mem.index),
             MIR_new_int_op (ctx, mem.mir_op.u.mem.scale));
    else
      emit2 (c2m_ctx, MIR_MOV, temp.mir_op, MIR_new_reg_op (ctx, mem.mir_op.u.mem.index));
    if (mem.mir_op.u.mem.base != 0)
      emit3 (c2m_ctx, MIR_ADD, temp.mir_op, temp.mir_op,
             MIR_new_reg_op (ctx, mem.mir_op.u.mem.base));
    if (mem.mir_op.u.mem.disp != 0)
      emit3 (c2m_ctx, MIR_ADD, temp.mir_op, temp.mir_op,
             MIR_new_int_op (ctx, mem.mir_op.u.mem.disp));
    mem = temp;
  }
  mem.mir_op.value_mode = MIR_OP_INT;
  return mem;
}

static op_t force_val (c2m_ctx_t c2m_ctx, op_t op, int arr_p) {
  MIR_context_t ctx = c2m_ctx->ctx;
  op_t temp_op;
  int sh;

  /* Named `_Atomic` object in memory: always use ALOAD (never plain MOV). */
  if (op.mir_op.mode == MIR_OP_MEM && op_decl_atomic_p (op)
      && integer_type_p (op.decl->decl_spec.type)) {
    MIR_type_t t = get_mir_type (c2m_ctx, op.decl->decl_spec.type);
    return atomic_load_mem (c2m_ctx, op, t);
  }
  if (arr_p && op.mir_op.mode == MIR_OP_MEM) {
    /* True array lvalue: decay to a pointer via address-of the storage.

       Adjusted array parameters (`T a[]` / `T a[N]` in a param list) are a
       different animal: `adjust_param_type` rewrites them to `T *` but keeps
       `type->arr_type` so sizeof/decay metadata survive.  Their stack (or
       register) slot already holds the *pointer value* passed by the caller —
       taking the slot's address would turn e.g. `char *argv[]` into `&argv`
       (a `char ***` bit pattern) and corrupt every `argv[i]` / `(void*)argv`.

       That path only appears when the parameter is forced into memory
       (`reg_p = 0`), which `try` does so values survive setjmp/longjmp.  With
       `reg_p = 1` the operand is a REG and this branch is skipped, which is
       why `main(argc, argv)` worked until a `try` was added in the same
       function. */
    if (op.decl != NULL && op.decl->decl_spec.type != NULL
        && op.decl->decl_spec.type->mode == TM_PTR)
      return op; /* MEM of a pointer value: keep as loadable rvalue */
    return mem_to_address (c2m_ctx, op, FALSE);
  }
  /* Issue-458: a narrow integer reg value is born from an extending
     operation, so reads need no extension except addr_p (2a157cc2).
     Uninitialized narrow autos are repaired at declaration (9c7e7f3b). */
  if (op.decl != NULL && op.decl->addr_p && op.mir_op.mode == MIR_OP_REG
      && integer_type_p (op.decl->decl_spec.type)) {
    MIR_type_t nt = get_mir_type (c2m_ctx, op.decl->decl_spec.type);
    if (nt == MIR_T_I8 || nt == MIR_T_U8 || nt == MIR_T_I16 || nt == MIR_T_U16)
      return cast (c2m_ctx, op, nt, TRUE);
  }
  if (op.decl == NULL || op.decl->bit_offset < 0) return op;
  assert (op.mir_op.mode == MIR_OP_MEM);
  temp_op = get_new_temp (c2m_ctx, MIR_T_I64);
  emit2 (c2m_ctx, MIR_MOV, temp_op.mir_op, op.mir_op); /* ??? */
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  sh = 64 - op.decl->bit_offset - op.decl->width;
#else
  sh = op.decl->bit_offset + (64 - type_size (c2m_ctx, op.decl->decl_spec.type) * MIR_CHAR_BIT);
#endif
  if (sh != 0) emit3 (c2m_ctx, MIR_LSH, temp_op.mir_op, temp_op.mir_op, MIR_new_int_op (ctx, sh));
  emit3 (c2m_ctx,
         signed_integer_type_p (op.decl->decl_spec.type)
             && (op.decl->decl_spec.type->mode != TM_ENUM
                 || op.decl->width >= (int) sizeof (mir_int) * MIR_CHAR_BIT)
           ? MIR_RSH
           : MIR_URSH,
         temp_op.mir_op, temp_op.mir_op, MIR_new_int_op (ctx, 64 - op.decl->width));
  return temp_op;
}

static void gen_unary_op (c2m_ctx_t c2m_ctx, node_t r, op_t *op, op_t *res) {
  MIR_type_t t;

  assert (!((struct expr *) r->attr)->const_p);
  *op = val_gen (c2m_ctx, NL_HEAD (r->u.ops));
  t = get_mir_type (c2m_ctx, ((struct expr *) r->attr)->type);
  *op = promote (c2m_ctx, *op, t, FALSE);
  *res = get_new_temp (c2m_ctx, t);
}

static void gen_assign_bin_op (c2m_ctx_t c2m_ctx, node_t r, struct type *assign_expr_type,
                               op_t *op1, op_t *op2, op_t *var) {
  MIR_type_t t;
  node_t e = NL_HEAD (r->u.ops);

  assert (!((struct expr *) r->attr)->const_p);
  t = get_mir_type (c2m_ctx, assign_expr_type);
  *op1 = gen (c2m_ctx, e, NULL, NULL, FALSE, NULL, NULL);
  *op2 = val_gen (c2m_ctx, NL_NEXT (e));
  *op2 = promote (c2m_ctx, *op2, t, FALSE);
  *var = *op1;
  *op1 = force_val (c2m_ctx, *op1, ((struct expr *) e->attr)->type->arr_type != NULL);
  *op1 = promote (c2m_ctx, *op1, t, TRUE);
}

static void gen_bin_op (c2m_ctx_t c2m_ctx, node_t r, op_t *op1, op_t *op2, op_t *res) {
  struct expr *e = (struct expr *) r->attr;
  MIR_type_t t = get_mir_type (c2m_ctx, e->type);

  assert (!e->const_p);
  *op1 = val_gen (c2m_ctx, NL_HEAD (r->u.ops));
  *op2 = val_gen (c2m_ctx, NL_EL (r->u.ops, 1));
  *op1 = promote (c2m_ctx, *op1, t, FALSE);
  *op2 = promote (c2m_ctx, *op2, t, FALSE);
  *res = get_new_temp (c2m_ctx, t);
}

static void gen_cmp_op (c2m_ctx_t c2m_ctx, node_t r, struct type *type, op_t *op1, op_t *op2,
                        op_t *res) {
  MIR_type_t t = get_mir_type (c2m_ctx, type), res_t = get_int_mir_type (sizeof (mir_int));

  assert (!((struct expr *) r->attr)->const_p);
  *op1 = val_gen (c2m_ctx, NL_HEAD (r->u.ops));
  *op2 = val_gen (c2m_ctx, NL_EL (r->u.ops, 1));
  *op1 = promote (c2m_ctx, *op1, t, FALSE);
  *op2 = promote (c2m_ctx, *op2, t, FALSE);
  *res = get_new_temp (c2m_ctx, res_t);
}

static MIR_insn_code_t get_mir_type_insn_code (c2m_ctx_t c2m_ctx, struct type *type, node_t r) {
  MIR_type_t t = get_mir_type (c2m_ctx, type);

  switch (r->code) {
  case N_INC:
  case N_POST_INC:
  case N_ADD:
  case N_ADD_ASSIGN:
    return (t == MIR_T_F                       ? MIR_FADD
            : t == MIR_T_D                     ? MIR_DADD
            : t == MIR_T_LD                    ? MIR_LDADD
            : t == MIR_T_I64 || t == MIR_T_U64 ? MIR_ADD
                                               : MIR_ADDS);
  case N_DEC:
  case N_POST_DEC:
  case N_SUB:
  case N_SUB_ASSIGN:
    return (t == MIR_T_F                       ? MIR_FSUB
            : t == MIR_T_D                     ? MIR_DSUB
            : t == MIR_T_LD                    ? MIR_LDSUB
            : t == MIR_T_I64 || t == MIR_T_U64 ? MIR_SUB
                                               : MIR_SUBS);
  case N_MUL:
  case N_MUL_ASSIGN:
    return (t == MIR_T_F                       ? MIR_FMUL
            : t == MIR_T_D                     ? MIR_DMUL
            : t == MIR_T_LD                    ? MIR_LDMUL
            : t == MIR_T_I64 || t == MIR_T_U64 ? MIR_MUL
                                               : MIR_MULS);
  case N_DIV:
  case N_DIV_ASSIGN:
    return (t == MIR_T_F     ? MIR_FDIV
            : t == MIR_T_D   ? MIR_DDIV
            : t == MIR_T_LD  ? MIR_LDDIV
            : t == MIR_T_I64 ? MIR_DIV
            : t == MIR_T_U64 ? MIR_UDIV
            : t == MIR_T_I32 ? MIR_DIVS
                             : MIR_UDIVS);
  case N_MOD:
  case N_MOD_ASSIGN:
    return (t == MIR_T_I64   ? MIR_MOD
            : t == MIR_T_U64 ? MIR_UMOD
            : t == MIR_T_I32 ? MIR_MODS
                             : MIR_UMODS);
  case N_AND:
  case N_AND_ASSIGN: return (t == MIR_T_I64 || t == MIR_T_U64 ? MIR_AND : MIR_ANDS);
  case N_OR:
  case N_OR_ASSIGN: return (t == MIR_T_I64 || t == MIR_T_U64 ? MIR_OR : MIR_ORS);
  case N_XOR:
  case N_XOR_ASSIGN: return (t == MIR_T_I64 || t == MIR_T_U64 ? MIR_XOR : MIR_XORS);
  case N_LSH:
  case N_LSH_ASSIGN: return (t == MIR_T_I64 || t == MIR_T_U64 ? MIR_LSH : MIR_LSHS);
  case N_RSH:
  case N_RSH_ASSIGN:
    return (t == MIR_T_I64   ? MIR_RSH
            : t == MIR_T_U64 ? MIR_URSH
            : t == MIR_T_I32 ? MIR_RSHS
                             : MIR_URSHS);
  case N_EQ:
    return (t == MIR_T_F                       ? MIR_FEQ
            : t == MIR_T_D                     ? MIR_DEQ
            : t == MIR_T_LD                    ? MIR_LDEQ
            : t == MIR_T_I64 || t == MIR_T_U64 ? MIR_EQ
                                               : MIR_EQS);
  case N_NE:
    return (t == MIR_T_F                       ? MIR_FNE
            : t == MIR_T_D                     ? MIR_DNE
            : t == MIR_T_LD                    ? MIR_LDNE
            : t == MIR_T_I64 || t == MIR_T_U64 ? MIR_NE
                                               : MIR_NES);
  case N_LT:
    return (t == MIR_T_F     ? MIR_FLT
            : t == MIR_T_D   ? MIR_DLT
            : t == MIR_T_LD  ? MIR_LDLT
            : t == MIR_T_I64 ? MIR_LT
            : t == MIR_T_U64 ? MIR_ULT
            : t == MIR_T_I32 ? MIR_LTS
                             : MIR_ULTS);
  case N_LE:
    return (t == MIR_T_F     ? MIR_FLE
            : t == MIR_T_D   ? MIR_DLE
            : t == MIR_T_LD  ? MIR_LDLE
            : t == MIR_T_I64 ? MIR_LE
            : t == MIR_T_U64 ? MIR_ULE
            : t == MIR_T_I32 ? MIR_LES
                             : MIR_ULES);
  case N_GT:
    return (t == MIR_T_F     ? MIR_FGT
            : t == MIR_T_D   ? MIR_DGT
            : t == MIR_T_LD  ? MIR_LDGT
            : t == MIR_T_I64 ? MIR_GT
            : t == MIR_T_U64 ? MIR_UGT
            : t == MIR_T_I32 ? MIR_GTS
                             : MIR_UGTS);
  case N_GE:
    return (t == MIR_T_F     ? MIR_FGE
            : t == MIR_T_D   ? MIR_DGE
            : t == MIR_T_LD  ? MIR_LDGE
            : t == MIR_T_I64 ? MIR_GE
            : t == MIR_T_U64 ? MIR_UGE
            : t == MIR_T_I32 ? MIR_GES
                             : MIR_UGES);
  default: assert (FALSE); return MIR_INSN_BOUND;
  }
}

static MIR_insn_code_t get_mir_insn_code (c2m_ctx_t c2m_ctx,
                                          node_t r) { /* result type is the same as op types */
  return get_mir_type_insn_code (c2m_ctx, ((struct expr *) r->attr)->type, r);
}

static MIR_insn_code_t get_compare_branch_code (MIR_insn_code_t code) {
#define B(n)                           \
  case MIR_##n: return MIR_B##n;       \
  case MIR_##n##S: return MIR_B##n##S; \
  case MIR_F##n: return MIR_FB##n;     \
  case MIR_D##n: return MIR_DB##n;     \
  case MIR_LD##n: return MIR_LDB##n;
#define BCMP(n)                    \
  B (n)                            \
  case MIR_U##n: return MIR_UB##n; \
  case MIR_U##n##S: return MIR_UB##n##S;
  switch (code) {
    B (EQ) B (NE) BCMP (LT) BCMP (LE) BCMP (GT) BCMP (GE) default : assert (FALSE);
    return MIR_INSN_BOUND;
  }
#undef B
#undef BCMP
}

static op_t force_reg (c2m_ctx_t c2m_ctx, op_t op, MIR_type_t t) {
  op_t res;

  if (op.mir_op.mode == MIR_OP_REG) return op;
  if (op.mir_op.mode == MIR_OP_MEM && op_decl_atomic_p (op)
      && integer_type_p (op.decl->decl_spec.type)) {
    MIR_type_t at = get_mir_type (c2m_ctx, op.decl->decl_spec.type);
    return atomic_load_mem (c2m_ctx, op, at);
  }
  res = get_new_temp (c2m_ctx, promote_mir_int_type (t));
  emit2 (c2m_ctx, MIR_MOV, res.mir_op, op.mir_op);
  return res;
}

/* Build mem:T[ptr_reg] for atomic builtins from a pointer value. */
static op_t atomic_ptr_to_mem (c2m_ctx_t c2m_ctx, op_t ptr, MIR_type_t t) {
  MIR_context_t ctx = c2m_ctx->ctx;
  ptr = force_reg (c2m_ctx, ptr, MIR_T_I64);
  assert (ptr.mir_op.mode == MIR_OP_REG);
  return new_op (NULL, MIR_new_mem_op (ctx, t, 0, ptr.mir_op.u.reg, 0, 1));
}

static op_t force_reg_or_mem (c2m_ctx_t c2m_ctx, op_t op, MIR_type_t t) {
  if (op.mir_op.mode == MIR_OP_REG || op.mir_op.mode == MIR_OP_MEM) return op;
  assert (op.mir_op.mode == MIR_OP_REF || op.mir_op.mode == MIR_OP_STR);
  return force_reg (c2m_ctx, op, t);
}

static void emit_label (c2m_ctx_t c2m_ctx, node_t r) {
  node_t labels = NL_HEAD (r->u.ops);

  assert (labels->code == N_LIST);
  if (NL_HEAD (labels->u.ops) == NULL) return;
  if (labels->attr == NULL) labels->attr = MIR_new_label (c2m_ctx->ctx);
  emit_label_insn_opt (c2m_ctx, labels->attr);
}

static MIR_label_t get_label (c2m_ctx_t c2m_ctx, node_t target) {
  node_t labels = NL_HEAD (target->u.ops);

  assert (labels->code == N_LIST && NL_HEAD (labels->u.ops) != NULL);
  if (labels->attr != NULL) return labels->attr;
  return labels->attr = MIR_new_label (c2m_ctx->ctx);
}

static void top_gen (c2m_ctx_t c2m_ctx, node_t r, MIR_label_t true_label, MIR_label_t false_label,
                     int *expect_res) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  top_gen_last_op = gen (c2m_ctx, r, true_label, false_label, FALSE, NULL, expect_res);
}

static op_t modify_for_block_move (c2m_ctx_t c2m_ctx, op_t mem, op_t index) {
  MIR_context_t ctx = c2m_ctx->ctx;
  op_t base;

  assert (mem.mir_op.u.mem.base != 0 && mem.mir_op.mode == MIR_OP_MEM
          && index.mir_op.mode == MIR_OP_REG);
  if (mem.mir_op.u.mem.index == 0) {
    mem.mir_op.u.mem.index = index.mir_op.u.reg;
    mem.mir_op.u.mem.scale = 1;
  } else {
    base = get_new_temp (c2m_ctx, MIR_T_I64);
    if (mem.mir_op.u.mem.scale != 1)
      emit3 (c2m_ctx, MIR_MUL, base.mir_op, MIR_new_reg_op (ctx, mem.mir_op.u.mem.index),
             MIR_new_int_op (ctx, mem.mir_op.u.mem.scale));
    else
      emit2 (c2m_ctx, MIR_MOV, base.mir_op, MIR_new_reg_op (ctx, mem.mir_op.u.mem.index));
    emit3 (c2m_ctx, MIR_ADD, base.mir_op, base.mir_op, MIR_new_reg_op (ctx, mem.mir_op.u.mem.base));
    mem.mir_op.u.mem.base = base.mir_op.u.reg;
    mem.mir_op.u.mem.index = index.mir_op.u.reg;
    mem.mir_op.u.mem.scale = 1;
  }
  mem.mir_op.u.mem.alias = mem.mir_op.u.mem.nonalias = 0;
  return mem;
}

static void gen_memcpy (c2m_ctx_t c2m_ctx, MIR_disp_t disp, MIR_reg_t base, op_t val,
                        mir_size_t len);

static void block_move (c2m_ctx_t c2m_ctx, op_t var, op_t val, mir_size_t size) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_context_t ctx = c2m_ctx->ctx;
  MIR_label_t repeat_label;
  op_t index;

  if (MIR_op_eq_p (ctx, var.mir_op, val.mir_op) || size == 0) return;
  if (size > 5) {
    var = mem_to_address (c2m_ctx, var, TRUE);
    assert (var.mir_op.mode == MIR_OP_REG);
    gen_memcpy (c2m_ctx, 0, var.mir_op.u.reg, val, size);
  } else {
    repeat_label = MIR_new_label (ctx);
    index = get_new_temp (c2m_ctx, MIR_T_I64);
    emit2 (c2m_ctx, MIR_MOV, index.mir_op, MIR_new_int_op (ctx, size));
    val = modify_for_block_move (c2m_ctx, val, index);
    var = modify_for_block_move (c2m_ctx, var, index);
    emit_label_insn_opt (c2m_ctx, repeat_label);
    emit3 (c2m_ctx, MIR_SUB, index.mir_op, index.mir_op, one_op.mir_op);
    assert (var.mir_op.mode == MIR_OP_MEM && val.mir_op.mode == MIR_OP_MEM);
    val.mir_op.u.mem.type = var.mir_op.u.mem.type = MIR_T_I8;
    emit2 (c2m_ctx, MIR_MOV, var.mir_op, val.mir_op);
    emit3 (c2m_ctx, MIR_BGT, MIR_new_label_op (ctx, repeat_label), index.mir_op, zero_op.mir_op);
  }
}

static const char *get_reg_var_name (c2m_ctx_t c2m_ctx, MIR_type_t promoted_type,
                                     const char *suffix, unsigned func_scope_num) {
  char prefix[50];

  sprintf (prefix,
           promoted_type == MIR_T_I64   ? "I%u_"
           : promoted_type == MIR_T_U64 ? "U%u_"
           : promoted_type == MIR_T_I32 ? "i%u_"
           : promoted_type == MIR_T_U32 ? "u%u_"
           : promoted_type == MIR_T_F   ? "f%u_"
           : promoted_type == MIR_T_D   ? "d%u_"
                                        : "D%u_",
           func_scope_num);
  VARR_TRUNC (char, temp_string, 0);
  add_to_temp_string (c2m_ctx, prefix);
  add_to_temp_string (c2m_ctx, suffix);
  return uniq_cstr (c2m_ctx, VARR_ADDR (char, temp_string)).s;
}

static const char *get_func_var_name (c2m_ctx_t c2m_ctx, const char *prefix, const char *suffix) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;

  assert (curr_func != NULL);
  VARR_TRUNC (char, temp_string, 0);
  add_to_temp_string (c2m_ctx, prefix);
  add_to_temp_string (c2m_ctx, curr_func->u.func->name);
  add_to_temp_string (c2m_ctx, "_");
  add_to_temp_string (c2m_ctx, suffix);
  return uniq_cstr (c2m_ctx, VARR_ADDR (char, temp_string)).s;
}

static const char *get_func_static_var_name (c2m_ctx_t c2m_ctx, const char *suffix, decl_t decl) {
  char prefix[50];
  unsigned func_scope_num = ((struct node_scope *) decl->scope->attr)->func_scope_num;

  sprintf (prefix, "S%u_", func_scope_num);
  return get_func_var_name (c2m_ctx, prefix, suffix);
}

static const char *get_param_name (c2m_ctx_t c2m_ctx, struct type *param_type, const char *name) {
  MIR_type_t type = (param_type->mode == TM_STRUCT || param_type->mode == TM_UNION
                       || param_type->mode == TM_CLASS
                       ? MIR_POINTER_TYPE
                       : get_mir_type (c2m_ctx, param_type));
  return get_reg_var_name (c2m_ctx, promote_mir_int_type (type), name, 0);
}

static void MIR_UNUSED simple_init_arg_vars (c2m_ctx_t c2m_ctx MIR_UNUSED,
                                             void *arg_info MIR_UNUSED) {}

static int simple_return_by_addr_p (c2m_ctx_t c2m_ctx MIR_UNUSED, struct type *ret_type) {
  return ret_type->mode == TM_STRUCT || ret_type->mode == TM_UNION || ret_type->mode == TM_CLASS;
}

static void MIR_UNUSED simple_add_res_proto (c2m_ctx_t c2m_ctx, struct type *ret_type,
                                             void *arg_info MIR_UNUSED,
                                             VARR (MIR_type_t) * res_types,
                                             VARR (MIR_var_t) * arg_vars) {
  MIR_var_t var;

  if (void_type_p (ret_type)) return;
  if (!simple_return_by_addr_p (c2m_ctx, ret_type)) {
    VARR_PUSH (MIR_type_t, res_types, get_mir_type (c2m_ctx, ret_type));
  } else {
    var.name = RET_ADDR_NAME;
    var.type = MIR_T_RBLK;
    /* Must match simple_add_call_res_op: MIR rejects RBLK size 0 vs 1. */
    var.size = type_size (c2m_ctx, ret_type);
    if (var.size == 0) var.size = 1;
    VARR_PUSH (MIR_var_t, arg_vars, var);
  }
}

static int MIR_UNUSED simple_add_call_res_op (c2m_ctx_t c2m_ctx, struct type *ret_type,
                                              void *arg_info MIR_UNUSED,
                                              size_t call_arg_area_offset) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_context_t ctx = c2m_ctx->ctx;
  MIR_type_t type;
  op_t temp;
  mir_size_t csize;

  if (void_type_p (ret_type)) return -1;
  if (!simple_return_by_addr_p (c2m_ctx, ret_type)) {
    type = promote_mir_int_type (get_mir_type (c2m_ctx, ret_type));
    temp = get_new_temp (c2m_ctx, type);
    VARR_PUSH (MIR_op_t, call_ops, temp.mir_op);
    return 1;
  }
  /* Aggregate returned via hidden pointer (RBLK).  Prefer a dedicated ALLOCA
     slot over the shared call-arg-area for class types: the call-arg-area is
     reclaimed when the inner call finishes (curr_call_arg_area_offset restore),
     so using that buffer as an argument to an outer call — e.g.
       list.Add(list.Get(i))
       List.Copy / Filter / Where monomorphizations
     — corrupts the still-live return value.  ALLOCA keeps each class return
     alive for the rest of the frame (C++ temporary-like storage).  Structs and
     unions without destructors keep the classic call-arg-area path for less
     stack growth; classes always use ALLOCA (they may have user ~T()). */
  temp = get_new_temp (c2m_ctx, MIR_T_I64);
  csize = type_size (c2m_ctx, ret_type);
  if (csize == 0) csize = 1;
  if (ret_type->mode == TM_CLASS) {
    (void) call_arg_area_offset;
    MIR_append_insn (ctx, curr_func,
                     MIR_new_insn (ctx, MIR_ALLOCA, temp.mir_op,
                                   MIR_new_int_op (ctx, (long long) csize)));
  } else {
    emit3 (c2m_ctx, MIR_ADD, temp.mir_op,
           MIR_new_reg_op (ctx, MIR_reg (ctx, FP_NAME, curr_func->u.func)),
           MIR_new_int_op (ctx, call_arg_area_offset));
  }
  temp.mir_op
    = MIR_new_mem_op (ctx, MIR_T_RBLK, csize, temp.mir_op.u.reg, 0, 1);
  VARR_PUSH (MIR_op_t, call_ops, temp.mir_op);
  return 0;
}

static op_t MIR_UNUSED simple_gen_post_call_res_code (c2m_ctx_t c2m_ctx MIR_UNUSED,
                                                      struct type *ret_type MIR_UNUSED, op_t res,
                                                      MIR_insn_t call MIR_UNUSED,
                                                      size_t call_ops_start MIR_UNUSED) {
  return res;
}

static void MIR_UNUSED simple_add_ret_ops (c2m_ctx_t c2m_ctx, struct type *ret_type, op_t val) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_context_t ctx = c2m_ctx->ctx;
  MIR_reg_t ret_addr_reg;
  op_t var;

  if (void_type_p (ret_type)) return;
  if (!simple_return_by_addr_p (c2m_ctx, ret_type)) {
    VARR_PUSH (MIR_op_t, ret_ops, val.mir_op);
  } else {
    ret_addr_reg = MIR_reg (ctx, RET_ADDR_NAME, curr_func->u.func);
    var = new_op (NULL, MIR_new_mem_op (ctx, MIR_T_I8, 0, ret_addr_reg, 0, 1));
    block_move (c2m_ctx, var, val, type_size (c2m_ctx, ret_type));
  }
}

static MIR_type_t MIR_UNUSED simple_target_get_blk_type (c2m_ctx_t c2m_ctx MIR_UNUSED,
                                                         struct type *arg_type MIR_UNUSED) {
  return MIR_T_BLK;
}

static void MIR_UNUSED simple_add_arg_proto (c2m_ctx_t c2m_ctx, const char *name,
                                             struct type *arg_type, void *arg_info MIR_UNUSED,
                                             VARR (MIR_var_t) * arg_vars) {
  MIR_var_t var;
  MIR_type_t type;

  type = (arg_type->mode == TM_STRUCT || arg_type->mode == TM_UNION || arg_type->mode == TM_CLASS
            ? MIR_T_BLK
            : get_mir_type (c2m_ctx, arg_type));
  var.name = name;
  var.type = type;
  if (type == MIR_T_BLK) {
    var.size = type_size (c2m_ctx, arg_type);
    if (var.size == 0) var.size = 1; /* keep in sync with call-site BLK disp */
  }
  VARR_PUSH (MIR_var_t, arg_vars, var);
}

static void MIR_UNUSED simple_add_call_arg_op (c2m_ctx_t c2m_ctx, struct type *arg_type,
                                               void *arg_info MIR_UNUSED, op_t arg) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_type_t type;
  mir_size_t bsize;

  type = (arg_type->mode == TM_STRUCT || arg_type->mode == TM_UNION || arg_type->mode == TM_CLASS
            ? MIR_T_BLK
            : get_mir_type (c2m_ctx, arg_type));
  if (type != MIR_T_BLK) {
    VARR_PUSH (MIR_op_t, call_ops, arg.mir_op);
  } else {
    assert (arg.mir_op.mode == MIR_OP_MEM);
    arg = mem_to_address (c2m_ctx, arg, TRUE);
    bsize = type_size (c2m_ctx, arg_type);
    if (bsize == 0) bsize = 1;
    VARR_PUSH (MIR_op_t, call_ops,
               MIR_new_mem_op (c2m_ctx->ctx, MIR_T_BLK, bsize, arg.mir_op.u.reg, 0, 1));
  }
}

static int MIR_UNUSED simple_gen_gather_arg (c2m_ctx_t c2m_ctx MIR_UNUSED,
                                             const char *name MIR_UNUSED,
                                             struct type *arg_type MIR_UNUSED,
                                             decl_t param_decl MIR_UNUSED,
                                             void *arg_info MIR_UNUSED) {
  return FALSE;
}

/* Can be used by target functions */
static MIR_UNUSED const char *gen_get_indexed_name (c2m_ctx_t c2m_ctx, const char *name,
                                                    int index) {
  assert (index >= 0 && index <= 9);
  VARR_TRUNC (char, temp_string, 0);
  VARR_PUSH_ARR (char, temp_string, name, strlen (name));
  VARR_PUSH (char, temp_string, '#');
  VARR_PUSH (char, temp_string, '0' + index);
  VARR_PUSH (char, temp_string, '\0');
  return _MIR_uniq_string (c2m_ctx->ctx, VARR_ADDR (char, temp_string));
}

/* Can be used by target functions */
static inline void MIR_UNUSED gen_multiple_load_store (c2m_ctx_t c2m_ctx, struct type *type,
                                                       MIR_op_t *var_ops, MIR_op_t mem_op,
                                                       int load_p) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_context_t ctx = c2m_ctx->ctx;
  MIR_op_t op, var_op;
  MIR_insn_t insn;
  int i, sh, size = (int) type_size (c2m_ctx, type);

  if (size == 0) return;
  if (type_align (type) == 8) {
    assert (size % 8 == 0);
    for (i = 0; size > 0; size -= 8, i++) {
      if (load_p) {
        insn = MIR_new_insn (ctx, MIR_MOV, var_ops[i],
                             MIR_new_mem_op (ctx, MIR_T_I64, mem_op.u.mem.disp + i * 8,
                                             mem_op.u.mem.base, mem_op.u.mem.index,
                                             mem_op.u.mem.scale));
      } else {
        insn = MIR_new_insn (ctx, MIR_MOV,
                             MIR_new_mem_op (ctx, MIR_T_I64, mem_op.u.mem.disp + i * 8,
                                             mem_op.u.mem.base, mem_op.u.mem.index,
                                             mem_op.u.mem.scale),
                             var_ops[i]);
      }
      MIR_append_insn (ctx, curr_func, insn);
    }
  } else {
    op = get_new_temp (c2m_ctx, MIR_T_I64).mir_op;
    if (load_p) {
      for (i = 0; i < size; i += 8) {
        var_op = var_ops[i / 8];
        insn = MIR_new_insn (ctx, MIR_MOV, var_op, MIR_new_int_op (ctx, 0));
        MIR_append_insn (ctx, curr_func, insn);
      }
    }
    for (i = 0; size > 0; size--, i++) {
      var_op = var_ops[i / 8];
      if (load_p) {
        insn
          = MIR_new_insn (ctx, MIR_MOV, op,
                          MIR_new_mem_op (ctx, MIR_T_U8, mem_op.u.mem.disp + i, mem_op.u.mem.base,
                                          mem_op.u.mem.index, mem_op.u.mem.scale));
        MIR_append_insn (ctx, curr_func, insn);
        if ((sh = i * 8 % 64) != 0) {
          insn = MIR_new_insn (ctx, MIR_LSH, op, op, MIR_new_int_op (ctx, sh));
          MIR_append_insn (ctx, curr_func, insn);
        }
        insn = MIR_new_insn (ctx, MIR_OR, var_op, var_op, op);
        MIR_append_insn (ctx, curr_func, insn);
      } else {
        if ((sh = i * 8 % 64) == 0)
          insn = MIR_new_insn (ctx, MIR_MOV, op, var_op);
        else
          insn = MIR_new_insn (ctx, MIR_URSH, op, var_op, MIR_new_int_op (ctx, sh));
        MIR_append_insn (ctx, curr_func, insn);
        insn
          = MIR_new_insn (ctx, MIR_MOV,
                          MIR_new_mem_op (ctx, MIR_T_U8, mem_op.u.mem.disp + i, mem_op.u.mem.base,
                                          mem_op.u.mem.index, mem_op.u.mem.scale),
                          op);
        MIR_append_insn (ctx, curr_func, insn);
      }
    }
  }
}

#if defined(__x86_64__) || defined(_M_AMD64)
#include "x86_64/cx86_64-ABI-code.c"
#elif defined(__aarch64__)
#include "aarch64/caarch64-ABI-code.c"
#elif defined(__PPC64__)
#include "ppc64/cppc64-ABI-code.c"
#elif defined(__s390x__)
#include "s390x/cs390x-ABI-code.c"
#elif defined(__riscv)
#include "riscv64/criscv64-ABI-code.c"
#else
typedef int target_arg_info_t; /* whatever */
/* Initiate ARG_INFO for generating call, prototype, or prologue. */
static void target_init_arg_vars (c2m_ctx_t c2m_ctx, target_arg_info_t *arg_info) {
  simple_init_arg_vars (c2m_ctx, arg_info);
}
/* Return true if result of RET_TYPE should be return by addr. */
static int target_return_by_addr_p (c2m_ctx_t c2m_ctx, struct type *ret_type) {
  return simple_return_by_addr_p (c2m_ctx, ret_type);
}
/* Add prototype result types to RES_TYPES or arg vars to ARG_VARS
   used to return value of RET_TYPES. */
static void target_add_res_proto (c2m_ctx_t c2m_ctx, struct type *ret_type,
                                  target_arg_info_t *arg_info, VARR (MIR_type_t) * res_types,
                                  VARR (MIR_var_t) * arg_vars) {
  simple_add_res_proto (c2m_ctx, ret_type, arg_info, res_types, arg_vars);
}
/* Generate code and result operands or an input operand to call_ops
   for returning call result of RET_TYPE.  Return -1 if no any call op
   was added, 0 if only input operand (result address) was added or
   number of added results. Use CALL_ARG_AREA_OFFSET for result
   address offset on the stack.  */
static int target_add_call_res_op (c2m_ctx_t c2m_ctx, struct type *ret_type,
                                   target_arg_info_t *arg_info, size_t call_arg_area_offset) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;

  return simple_add_call_res_op (c2m_ctx, ret_type, arg_info, call_arg_area_offset);
}
/* Generate code to gather returned values of CALL into RES.  Return
   value of RET_TYPE.  CALL_OPS_START is start index of all call
   operands in call_ops for given call. */
static op_t target_gen_post_call_res_code (c2m_ctx_t c2m_ctx, struct type *ret_type, op_t res,
                                           MIR_insn_t call, size_t call_ops_start) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;

  return simple_gen_post_call_res_code (c2m_ctx, ret_type, res, call, call_ops_start);
}
/* Generate code and add operands to ret_ops which return VAL of RET_TYPE. */
static void target_add_ret_ops (c2m_ctx_t c2m_ctx, struct type *ret_type, op_t val) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;

  simple_add_ret_ops (c2m_ctx, ret_type, val);
}
/* Return BLK type should be used for VA_BLOCK_ARG for accessing aggregate type ARG_TYPE.  */
static MIR_type_t target_get_blk_type (c2m_ctx_t c2m_ctx, struct type *arg_type) {
  return simple_target_get_blk_type (c2m_ctx, arg_type);
}
/* Add one or more vars to arg_vars which pass arg NAME of ARG_TYPE. */
static void target_add_arg_proto (c2m_ctx_t c2m_ctx, const char *name, struct type *arg_type,
                                  target_arg_info_t *arg_info, VARR (MIR_var_t) * arg_vars) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;

  simple_add_arg_proto (c2m_ctx, name, arg_type, arg_info, arg_vars);
}
/* Add operands to call_ops which pass ARG of ARG_TYPE. */
static void target_add_call_arg_op (c2m_ctx_t c2m_ctx, struct type *arg_type,
                                    target_arg_info_t *arg_info, op_t arg) {
  simple_add_call_arg_op (c2m_ctx, arg_type, arg_info, arg);
}
/* Add code to gather aggregate arg with NAME, ARG_TYPE and PARAM_DECL passed by non-block args.
   Return true if it was the case.  */
static int target_gen_gather_arg (c2m_ctx_t c2m_ctx, const char *name, struct type *arg_type,
                                  decl_t param_decl, target_arg_info_t *arg_info) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;

  return simple_gen_gather_arg (c2m_ctx, name, arg_type, param_decl, arg_info);
}
#endif

/* C11 6.5.2.2p6 default argument promotions for unprototyped calls. */
static struct type *default_arg_promoted_type (c2m_ctx_t c2m_ctx, struct type *type) {
  struct type *res;

  if (type->mode == TM_BASIC && type->u.basic_type == TP_FLOAT) {
    res = create_type (c2m_ctx, NULL);
    res->mode = TM_BASIC;
    res->u.basic_type = TP_DOUBLE;
  } else if (integer_type_p (type)) {
    struct type prom = integer_promotion (type);

    if (type->mode == TM_BASIC && prom.u.basic_type == type->u.basic_type) return type;
    res = create_type (c2m_ctx, &prom);
  } else {
    return type;
  }
  set_type_layout (c2m_ctx, res);
  return res;
}

/* first_actual_arg is the first CALL argument, or NULL for a definition. */
static void collect_args_and_func_types (c2m_ctx_t c2m_ctx, struct func_type *func_type,
                                         node_t first_actual_arg) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  node_t declarator, id, first_param, p;
  struct type *param_type;
  decl_t param_decl;
  const char *name;
  target_arg_info_t arg_info;

  first_param = NL_HEAD (func_type->param_list->u.ops);
  VARR_TRUNC (MIR_var_t, proto_info.arg_vars, 0);
  VARR_TRUNC (MIR_type_t, proto_info.ret_types, 0);
  proto_info.res_ref_p = FALSE;
  target_init_arg_vars (c2m_ctx, &arg_info);
  set_type_layout (c2m_ctx, func_type->ret_type);
  target_add_res_proto (c2m_ctx, func_type->ret_type, &arg_info, proto_info.ret_types,
                        proto_info.arg_vars);

  if (first_param != NULL && !void_param_p (first_param)) {
    for (p = first_param; p != NULL; p = NL_NEXT (p)) {
      if (p->code == N_TYPE) {
        name = "p";
        param_type = ((struct decl_spec *) p->attr)->type;
        param_decl = NULL;
      } else {
        declarator = NL_EL (p->u.ops, 1);
        assert (p->code == N_SPEC_DECL && declarator != NULL && declarator->code == N_DECL);
        id = NL_HEAD (declarator->u.ops);
        param_decl = p->attr;
        param_type = param_decl->decl_spec.type;
        name = get_param_name (c2m_ctx, param_type, id->u.s.s);
      }
      target_add_arg_proto (c2m_ctx, name, param_type, &arg_info, proto_info.arg_vars);
    }
  } else if (first_param == NULL && !func_type->dots_p) {
    /* Unprototyped (`T f();`): proto from this call site's actual args. */
    for (p = first_actual_arg; p != NULL; p = NL_NEXT (p)) {
      param_type = default_arg_promoted_type (c2m_ctx, ((struct expr *) p->attr)->type);
      set_type_layout (c2m_ctx, param_type);
      target_add_arg_proto (c2m_ctx, "p", param_type, &arg_info, proto_info.arg_vars);
    }
  }
}

static mir_size_t get_object_path_offset (c2m_ctx_t c2m_ctx) {
  init_object_t init_object;
  size_t offset = 0;

  for (size_t i = 0; i < VARR_LENGTH (init_object_t, init_object_path); i++) {
    init_object = VARR_GET (init_object_t, init_object_path, i);
    if (init_object.container_type->mode == TM_ARR) {  // ??? index < 0
      offset += (init_object.u.curr_index
                 * type_size (c2m_ctx, init_object.container_type->u.arr_type->el_type));
    } else {
      assert (init_object.container_type->mode == TM_STRUCT
              || init_object.container_type->mode == TM_UNION
              || init_object.container_type->mode == TM_CLASS);
      assert (init_object.u.curr_member->code == N_MEMBER);
      if (!anon_struct_union_type_member_p (init_object.u.curr_member))
        /* Members inside anon struct/union already have adjusted offset */
        offset += ((decl_t) init_object.u.curr_member->attr)->offset;
    }
  }
  return offset;
}

/* The function has the same structure as check_initializer.  Keep it this way. */
static void collect_init_els (c2m_ctx_t c2m_ctx, decl_t member_decl, struct type **type_ptr,
                              node_t initializer, int const_only_p, int top_p MIR_UNUSED) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  struct type *type = *type_ptr;
  struct expr *cexpr;
  node_t literal, des_list, curr_des, str, init, value;
  mir_llong MIR_UNUSED size_val = 0; /* to remove an uninitialized warning */
  size_t mark;
  symbol_t sym;
  init_el_t init_el;
  int addr_p = FALSE; /* to remove an uninitialized warning */
  int MIR_UNUSED found_p, MIR_UNUSED ok_p;
  init_object_t init_object;

  literal = get_compound_literal (initializer, &addr_p);
  if (literal != NULL && !addr_p && initializer->code != N_STR && initializer->code != N_STR16
      && initializer->code != N_STR32)
    initializer = NL_EL (literal->u.ops, 1);
check_one_value:
  if (initializer->code != N_LIST
      && !(initializer->code == N_STR && type->mode == TM_ARR
           && init_compatible_string_p (initializer, type->u.arr_type->el_type))) {
    cexpr = initializer->attr;
    /* static or thread local object initialization should be const expr or addr: */
    assert (initializer->code == N_STR || initializer->code == N_STR16
            || initializer->code == N_STR32 || !const_only_p || cexpr->const_p
            || cexpr->const_addr_p || (literal != NULL && addr_p));
    init_el.c2m_ctx = c2m_ctx;
    init_el.num = VARR_LENGTH (init_el_t, init_els);
    init_el.offset = get_object_path_offset (c2m_ctx);
    init_el.member_decl = member_decl;
    init_el.el_type = type;
    init_el.container_type = VARR_LENGTH (init_object_t, init_object_path) == 0
                               ? NULL
                               : VARR_LAST (init_object_t, init_object_path).container_type;
    init_el.init = initializer;
    VARR_PUSH (init_el_t, init_els, init_el);
    return;
  }
  init = NL_HEAD (initializer->u.ops);
  if (((str = initializer)->code == N_STR || str->code == N_STR16
       || str->code == N_STR32 /* string or string in parentheses  */
       || (init != NULL && init->code == N_INIT && NL_EL (initializer->u.ops, 1) == NULL
           && (des_list = NL_HEAD (init->u.ops))->code == N_LIST
           && NL_HEAD (des_list->u.ops) == NULL && NL_EL (init->u.ops, 1) != NULL
           && ((str = NL_EL (init->u.ops, 1))->code == N_STR || str->code == N_STR16
               || str->code == N_STR32)))
      && type->mode == TM_ARR && init_compatible_string_p (str, type->u.arr_type->el_type)) {
    init_el.c2m_ctx = c2m_ctx;
    init_el.num = VARR_LENGTH (init_el_t, init_els);
    init_el.offset = get_object_path_offset (c2m_ctx);
    init_el.member_decl = NULL;
    init_el.el_type = type;
    init_el.container_type = VARR_LENGTH (init_object_t, init_object_path) == 0
                               ? NULL
                               : VARR_LAST (init_object_t, init_object_path).container_type;
    init_el.init = str;
    VARR_PUSH (init_el_t, init_els, init_el);
    return;
  }
  if (init == NULL) return;
  assert (init->code == N_INIT);
  des_list = NL_HEAD (init->u.ops);
  assert (des_list->code == N_LIST);
  if (type->mode != TM_ARR && type->mode != TM_STRUCT && type->mode != TM_UNION && type->mode != TM_CLASS) {
    assert (NL_NEXT (init) == NULL && NL_HEAD (des_list->u.ops) == NULL);
    initializer = NL_NEXT (des_list);
    assert (top_p);
    top_p = FALSE;
    goto check_one_value;
  }
  mark = VARR_LENGTH (init_object_t, init_object_path);
  init_object.container_type = type;
  init_object.field_designator_p = FALSE;
  if (type->mode == TM_ARR) {
    size_val = get_arr_type_size (type);
    /* size_val is normally known from check, but it may be -1 for a class' trailing
       FLEXIBLE array member: create_decl restores that member to its incomplete
       form after check (so a later instance with a different length is sized
       correctly).  Element placement below is driven solely by the running index
       (update_init_object_path treats -1 as unbounded), so an unknown total size
       is fine here. */
    init_object.u.curr_index = -1;
  } else {
    init_object.u.curr_member = NULL;
  }
  VARR_PUSH (init_object_t, init_object_path, init_object);
  for (; init != NULL; init = NL_NEXT (init)) {
    assert (init->code == N_INIT);
    des_list = NL_HEAD (init->u.ops);
    value = NL_NEXT (des_list);
    assert ((value->code != N_LIST && value->code != N_COMPOUND_LITERAL) || type->mode == TM_ARR
            || type->mode == TM_STRUCT || type->mode == TM_UNION || type->mode == TM_CLASS);
    if ((curr_des = NL_HEAD (des_list->u.ops)) == NULL) {
      ok_p = update_path_and_do (c2m_ctx, TRUE, collect_init_els, mark, value, const_only_p, NULL,
                                 POS (init), "");
      assert (ok_p);
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
                    || init_object.container_type->mode == TM_UNION
                    || init_object.container_type->mode == TM_CLASS);
            decl_t el_decl = init_object.u.curr_member->attr;
            curr_type = el_decl->decl_spec.type;
          }
        }
        if (curr_des->code == N_FIELD_ID) {
          node_t id = NL_HEAD (curr_des->u.ops);

          /* field should be only in struct/union initializer */
          assert (curr_type->mode == TM_STRUCT || curr_type->mode == TM_UNION || curr_type->mode == TM_CLASS);
          found_p = symbol_find (c2m_ctx, S_REGULARS, id, curr_type->u.tag_type, &sym);
          assert (found_p); /* field should present */
          process_init_field_designator (c2m_ctx, sym.def_node, curr_type);
          ok_p = update_path_and_do (c2m_ctx, NL_NEXT (curr_des) == NULL, collect_init_els, mark,
                                     value, const_only_p, NULL, POS (init), "");
          assert (ok_p);
        } else {
          cexpr = curr_des->attr;
          /* index should be in array initializer and const expr of right type and value: */
          assert (curr_type->mode == TM_ARR && cexpr->const_p && integer_type_p (cexpr->type)
                  && !incomplete_type_p (c2m_ctx, curr_type)
                  && (arr_size_val = get_arr_type_size (curr_type)) >= 0
                  && (mir_ullong) arr_size_val > cexpr->c.u_val);
          init_object.u.curr_index = cexpr->c.i_val - 1;
          init_object.field_designator_p = FALSE;
          init_object.container_type = curr_type;
          VARR_PUSH (init_object_t, init_object_path, init_object);
          ok_p = update_path_and_do (c2m_ctx, NL_NEXT (curr_des) == NULL, collect_init_els, mark,
                                     value, const_only_p, NULL, POS (init), "");
          assert (ok_p);
        }
      }
    }
  }
  VARR_TRUNC (init_object_t, init_object_path, mark);
}

static int cmp_init_el (const void *p1, const void *p2) {
  const init_el_t *el1 = p1, *el2 = p2;
  int bit_offset1 = el1->member_decl == NULL || el1->member_decl->bit_offset < 0
                      ? 0
                      : el1->member_decl->bit_offset;
  int bit_offset2 = el2->member_decl == NULL || el2->member_decl->bit_offset < 0
                      ? 0
                      : el2->member_decl->bit_offset;

  if (el1->offset + bit_offset1 / MIR_CHAR_BIT < el2->offset + bit_offset2 / MIR_CHAR_BIT)
    return -1;
  else if (el1->offset + bit_offset1 / MIR_CHAR_BIT > el2->offset + bit_offset2 / MIR_CHAR_BIT)
    return 1;
  else if (el1->member_decl != NULL && el2->member_decl != NULL
           && el1->member_decl->bit_offset < el2->member_decl->bit_offset)
    return -1;
  else if (el1->member_decl != NULL && el2->member_decl != NULL
           && el1->member_decl->bit_offset > el2->member_decl->bit_offset)
    return 1;
  else if (el1->member_decl != NULL
           && type_size (el1->c2m_ctx, el1->member_decl->decl_spec.type) == 0)
    return -1;
  else if (el2->member_decl != NULL
           && type_size (el2->c2m_ctx, el2->member_decl->decl_spec.type) == 0)
    return 1;
  else if (el1->num < el2->num)
    return -1;
  else if (el1->num > el2->num)
    return 1;
  else
    return 0;
}

static void move_item_to_module_start (MIR_module_t module, MIR_item_t item) {
  DLIST_REMOVE (MIR_item_t, module->items, item);
  DLIST_PREPEND (MIR_item_t, module->items, item);
}

static void move_item_forward (c2m_ctx_t c2m_ctx, MIR_item_t item) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;

  assert (curr_func != NULL);
  if (DLIST_TAIL (MIR_item_t, curr_func->module->items) != item) return;
  DLIST_REMOVE (MIR_item_t, curr_func->module->items, item);
  DLIST_INSERT_BEFORE (MIR_item_t, curr_func->module->items, curr_func, item);
}

static void gen_memset (c2m_ctx_t c2m_ctx, MIR_disp_t disp, MIR_reg_t base, mir_size_t len) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_context_t ctx = c2m_ctx->ctx;
  MIR_type_t ret_type;
  MIR_var_t vars[3];
  MIR_op_t treg_op, args[6];
  MIR_module_t module;

  if (memset_item == NULL) {
    ret_type = get_int_mir_type (sizeof (mir_size_t));
    vars[0].name = "s";
    vars[0].type = get_int_mir_type (sizeof (mir_size_t));
    vars[1].name = "c";
    vars[1].type = get_int_mir_type (sizeof (mir_int));
    vars[2].name = "n";
    vars[2].type = get_int_mir_type (sizeof (mir_size_t));
    module = curr_func->module;
    memset_proto = MIR_new_proto_arr (ctx, "memset_p", 1, &ret_type, 3, vars);
    memset_item = MIR_new_import (ctx, "memset");
    move_item_to_module_start (module, memset_proto);
    move_item_to_module_start (module, memset_item);
  }
  args[0] = MIR_new_ref_op (ctx, memset_proto);
  args[1] = MIR_new_ref_op (ctx, memset_item);
  args[2] = get_new_temp (c2m_ctx, get_int_mir_type (sizeof (mir_size_t))).mir_op;
  if (disp == 0) {
    treg_op = MIR_new_reg_op (ctx, base);
  } else {
    treg_op = get_new_temp (c2m_ctx, get_int_mir_type (sizeof (mir_size_t))).mir_op;
    emit3 (c2m_ctx, MIR_ADD, treg_op, MIR_new_reg_op (ctx, base), MIR_new_int_op (ctx, disp));
  }
  args[3] = treg_op;
  args[4] = MIR_new_int_op (ctx, 0);
  args[5] = MIR_new_uint_op (ctx, len);
  emit_insn (c2m_ctx, MIR_new_insn_arr (ctx, MIR_CALL, 6 /* args + proto + func + res */, args));
}

static void gen_memcpy (c2m_ctx_t c2m_ctx, MIR_disp_t disp, MIR_reg_t base, op_t val,
                        mir_size_t len) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_context_t ctx = c2m_ctx->ctx;
  MIR_type_t ret_type;
  MIR_var_t vars[3];
  MIR_op_t treg_op, args[6];
  MIR_module_t module;

  if (val.mir_op.mode == MIR_OP_MEM && val.mir_op.u.mem.index == 0 && val.mir_op.u.mem.disp == disp
      && val.mir_op.u.mem.base == base)
    return;
  if (memcpy_item == NULL) {
    ret_type = get_int_mir_type (sizeof (mir_size_t));
    vars[0].name = "dest";
    vars[0].type = get_int_mir_type (sizeof (mir_size_t));
    vars[1].name = "src";
    vars[1].type = get_int_mir_type (sizeof (mir_size_t));
    vars[2].name = "n";
    vars[2].type = get_int_mir_type (sizeof (mir_size_t));
    module = curr_func->module;
    memcpy_proto = MIR_new_proto_arr (ctx, "memcpy_p", 1, &ret_type, 3, vars);
    memcpy_item = MIR_new_import (ctx, "memcpy");
    move_item_to_module_start (module, memcpy_proto);
    move_item_to_module_start (module, memcpy_item);
  }
  args[0] = MIR_new_ref_op (ctx, memcpy_proto);
  args[1] = MIR_new_ref_op (ctx, memcpy_item);
  args[2] = get_new_temp (c2m_ctx, get_int_mir_type (sizeof (mir_size_t))).mir_op;
  if (disp == 0) {
    treg_op = MIR_new_reg_op (ctx, base);
  } else {
    treg_op = get_new_temp (c2m_ctx, get_int_mir_type (sizeof (mir_size_t))).mir_op;
    emit3 (c2m_ctx, MIR_ADD, treg_op, MIR_new_reg_op (ctx, base), MIR_new_int_op (ctx, disp));
  }
  args[3] = treg_op;
  args[4] = mem_to_address (c2m_ctx, val, FALSE).mir_op;
  args[5] = MIR_new_uint_op (ctx, len);
  emit_insn (c2m_ctx, MIR_new_insn_arr (ctx, MIR_CALL, 6 /* args + proto + func + res */, args));
}

/* Emit `memcmp(&a, &b, len)` for two aggregate (class/struct/union) operands and
   return a temp register holding the int result (0 == equal).  Used to lower
   `==` / `!=` on by-value class/struct values (see N_EQ/N_NE in gen). */
static op_t gen_memcmp (c2m_ctx_t c2m_ctx, op_t a, op_t b, mir_size_t len) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_context_t ctx = c2m_ctx->ctx;
  MIR_type_t ret_type;
  MIR_var_t vars[3];
  MIR_op_t args[6];
  op_t res;

  if (memcmp_item == NULL) {
    MIR_module_t module = curr_func->module;
    ret_type = get_int_mir_type (sizeof (mir_int));
    vars[0].name = "s1";
    vars[0].type = get_int_mir_type (sizeof (mir_size_t));
    vars[1].name = "s2";
    vars[1].type = get_int_mir_type (sizeof (mir_size_t));
    vars[2].name = "n";
    vars[2].type = get_int_mir_type (sizeof (mir_size_t));
    memcmp_proto = MIR_new_proto_arr (ctx, "memcmp_p", 1, &ret_type, 3, vars);
    memcmp_item = MIR_new_import (ctx, "memcmp");
    move_item_to_module_start (module, memcmp_proto);
    move_item_to_module_start (module, memcmp_item);
  }
  res = get_new_temp (c2m_ctx, get_int_mir_type (sizeof (mir_int)));
  args[0] = MIR_new_ref_op (ctx, memcmp_proto);
  args[1] = MIR_new_ref_op (ctx, memcmp_item);
  args[2] = res.mir_op;
  args[3] = mem_to_address (c2m_ctx, a, FALSE).mir_op;
  args[4] = mem_to_address (c2m_ctx, b, FALSE).mir_op;
  args[5] = MIR_new_uint_op (ctx, len);
  emit_insn (c2m_ctx, MIR_new_insn_arr (ctx, MIR_CALL, 6 /* args + proto + func + res */, args));
  return res;
}

/* Runtime-helper call emission (defined with the dict helpers below). */
static op_t gen_rt_call (c2m_ctx_t c2m_ctx, MIR_item_t proto, MIR_item_t item, size_t nargs,
                         const MIR_op_t *arg_ops);
static void gen_rt_call_void (c2m_ctx_t c2m_ctx, MIR_item_t proto, MIR_item_t item, size_t nargs,
                              const MIR_op_t *arg_ops);
static void safety_ensure_imports (c2m_ctx_t c2m_ctx);

/* Emit a call to the C library malloc and return a temp register holding the
   resulting pointer.  Used to back `new ClassName(...)` heap allocations. */
static op_t gen_heap_alloc (c2m_ctx_t c2m_ctx, mir_size_t size) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_context_t ctx = c2m_ctx->ctx;
  MIR_op_t size_op = MIR_new_uint_op (ctx, size);

  /* In exceptions mode route the allocation through cy_safe_alloc.  NOTE: this
     is a plain malloc wrapper — there is NO object header (an earlier header-
     prefix "tagged allocator" was abandoned because prepending a header changes
     the pointer the program holds, which breaks every free path, external C
     interop, and alignment).  Use-after-free / double-free detection instead
     lives in the optional side-table object guards (`-fobject-guards`, see
     object_guard_ensure_imports / cy_obj_*), which never alter pointer layout. */
  if (c2m_options->exceptions_p) {
    safety_ensure_imports (c2m_ctx);
    return gen_rt_call (c2m_ctx, cy_safe_alloc_proto, cy_safe_alloc_item, 1, &size_op);
  }
  if (malloc_item == NULL) {
    MIR_type_t ret_type = MIR_T_I64;
    MIR_var_t var;
    MIR_module_t module = curr_func->module;
    var.name = "size";
    var.type = get_int_mir_type (sizeof (mir_size_t));
    malloc_proto = MIR_new_proto_arr (ctx, "__new_malloc_p", 1, &ret_type, 1, &var);
    malloc_item = MIR_new_import (ctx, "malloc");
    move_item_to_module_start (module, malloc_proto);
    move_item_to_module_start (module, malloc_item);
  }
  return gen_rt_call (c2m_ctx, malloc_proto, malloc_item, 1, &size_op);
}

/* free (ptr) : release a `new`-allocated object (used by `delete`).
   In exceptions mode, use cy_safe_free which checks for double-free. */
static void gen_heap_free (c2m_ctx_t c2m_ctx, MIR_op_t ptr, long line) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_context_t ctx = c2m_ctx->ctx;

  if (c2m_options->exceptions_p) {
    safety_ensure_imports (c2m_ctx);
    MIR_op_t args[2] = { ptr, MIR_new_int_op (ctx, line) };
    gen_rt_call_void (c2m_ctx, cy_safe_free_proto, cy_safe_free_item, 2, args);
    return;
  }
  if (free_item == NULL) {
    MIR_var_t var;
    MIR_module_t module = curr_func->module;
    var.name = "ptr";
    var.type = get_int_mir_type (sizeof (mir_size_t));
    free_proto = MIR_new_proto_arr (ctx, "__delete_free_p", 0, NULL, 1, &var);
    free_item = MIR_new_import (ctx, "free");
    move_item_to_module_start (module, free_proto);
    move_item_to_module_start (module, free_item);
  }
  gen_rt_call_void (c2m_ctx, free_proto, free_item, 1, &ptr);
}

/* Sentinel pushed on the defer stack when entering a `try` body: when unwound
   by gen_run_defers it emits a cy_exc_pop() call instead of gen'ing an AST
   node.  This makes a `return`/`break`/`continue` that leaves a try body pop
   the exception frame it entered (LIFO with any real defers registered inside
   the try), so a later `throw` never longjmps into a frame whose function has
   already returned.  A low integer never collides with a real heap node_t. */
#define CY_EXC_POP_MARKER ((node_t) (intptr_t) 2)

static void exception_ensure_imports (c2m_ctx_t c2m_ctx);
static MIR_item_t defer_shadow_thunk_item (c2m_ctx_t c2m_ctx, node_t delete_node);
static void gen_defer_shadow_push (c2m_ctx_t c2m_ctx, node_t delete_node);

/* Emit the deferred statements registered above stack depth `from`, in LIFO
   order, without popping them (the caller decides when to truncate the stack). */
static void gen_run_defers (c2m_ctx_t c2m_ctx, size_t from) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  size_t i = VARR_LENGTH (node_t, defer_stmts);

  while (i > from) {
    node_t d;
    i--;
    d = VARR_GET (node_t, defer_stmts, i);
    if (d == CY_EXC_POP_MARKER) {
      gen_rt_call_void (c2m_ctx, cy_exc_pop_proto, cy_exc_pop_item, 0, NULL);
    } else {
      /* Keep the exception-path shadow stack (cy__defer_stack, cyexc.h) in
         sync: a cleanup replayed here on a normal exit must not be re-invoked
         by a later throw's dispatch path.  Discard BEFORE running: if the
         cleanup itself throws, its own entry is already gone (no double-run)
         and the entries below it still unwind via cy_defer_release_to.  The
         item lookup is the exact condition under which the push was emitted
         (see gen_defer_shadow_push), so the two sets match by construction. */
      if (defer_shadow_thunk_item (c2m_ctx, d) != NULL) {
        exception_ensure_imports (c2m_ctx);
        gen_rt_call_void (c2m_ctx, cy_defer_discard_one_proto, cy_defer_discard_one_item, 0,
                          NULL);
      }
      gen (c2m_ctx, d, NULL, NULL, FALSE, NULL, NULL);
    }
  }
}

/* ========== Dict runtime call helpers ========== */

/* ───────── Runtime-helper import declaration ─────────
   Counterpart of gen_rt_call/gen_rt_call_void below: every runtime helper
   (dict_*, c2m_str_*, cy_*, _safety_trap) is declared the same way — one
   proto item plus one import item, both hoisted to the module start so
   references from any function resolve.  These helpers collapse that
   boilerplate (each *_ensure_imports below used to hand-write it per
   helper).

   ARG_SPEC is a space-separated list of parameter names.  Every parameter
   is I64 (pointers, sizes and ints all pass as I64) unless the name is
   prefixed with '.', which marks a double (e.g. ".v").  RES_P: 1 -> the
   helper returns one I64 value, 0 -> void.  PROTO_BASE names the proto
   item "__<proto_base>_p"; pass NULL to use NAME (it exists only for the
   few helpers whose historical proto name differs from the import name,
   e.g. import "setjmp" with proto "__cy_setjmp_p").  MIR interns both the
   proto name and the parameter names, so the stack buffers here are safe. */
#define RT_IMPORT_MAX_ARGS 10

/* Import-only variant: used when several imports share one proto
   (e.g. c2m_str_from_int/uint/bool/char all use __c2m_str_from_i64_p). */
static MIR_item_t rt_import_item (c2m_ctx_t c2m_ctx, const char *name) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_item_t item = MIR_new_import (c2m_ctx->ctx, name);

  move_item_to_module_start (curr_func->module, item);
  return item;
}

static void rt_import (c2m_ctx_t c2m_ctx, const char *name, const char *proto_base,
                       MIR_item_t *proto, MIR_item_t *item, int res_p, const char *arg_spec) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_context_t ctx = c2m_ctx->ctx;
  MIR_type_t res_type = MIR_T_I64;
  MIR_var_t vars[RT_IMPORT_MAX_ARGS];
  char names[128], proto_name[128];
  size_t nargs = 0;

  if (arg_spec != NULL && arg_spec[0] != '\0') {
    size_t len = strlen (arg_spec);

    assert (len < sizeof (names));
    memcpy (names, arg_spec, len + 1);
    for (char *p = names; *p != '\0';) {
      assert (nargs < RT_IMPORT_MAX_ARGS);
      vars[nargs].type = MIR_T_I64;
      if (*p == '.') {
        vars[nargs].type = MIR_T_D;
        p++;
      }
      vars[nargs].name = p;
      nargs++;
      while (*p != '\0' && *p != ' ') p++;
      while (*p == ' ') *p++ = '\0';
    }
  }
  snprintf (proto_name, sizeof (proto_name), "__%s_p", proto_base != NULL ? proto_base : name);
  *proto = MIR_new_proto_arr (ctx, proto_name, res_p ? 1 : 0, &res_type, nargs, vars);
  move_item_to_module_start (curr_func->module, *proto);
  *item = rt_import_item (c2m_ctx, name);
}

static void dict_ensure_imports (c2m_ctx_t c2m_ctx) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;

  if (dict_create_object_item != NULL) return; /* already imported */

  /* All DictValue* / char* / size_t / int values pass and return as I64;
     see include/dict_types.h for the C-level declarations. */
  rt_import (c2m_ctx, "dict_create_object", NULL, &dict_create_object_proto,
             &dict_create_object_item, 1, "");
  rt_import (c2m_ctx, "dict_create_bool", NULL, &dict_create_bool_proto, &dict_create_bool_item,
             1, "b");
  rt_import (c2m_ctx, "dict_create_int64", NULL, &dict_create_int64_proto,
             &dict_create_int64_item, 1, "n");
  rt_import (c2m_ctx, "dict_create_number", NULL, &dict_create_number_proto,
             &dict_create_number_item, 1, ".n"); /* takes a double */
  rt_import (c2m_ctx, "dict_create_string", NULL, &dict_create_string_proto,
             &dict_create_string_item, 1, "s");
  rt_import (c2m_ctx, "dict_object_set", NULL, &dict_object_set_proto, &dict_object_set_item, 1,
             "obj key val");
  rt_import (c2m_ctx, "dict_object_get", NULL, &dict_object_get_proto, &dict_object_get_item, 1,
             "obj key");
  /* dict_value_copy: deep clone */
  rt_import (c2m_ctx, "dict_value_copy", NULL, &dict_value_copy_proto, &dict_value_copy_item, 1,
             "src");
  rt_import (c2m_ctx, "dict_object_count", NULL, &dict_object_count_proto,
             &dict_object_count_item, 1, "obj");
  rt_import (c2m_ctx, "dict_object_key_at", NULL, &dict_object_key_at_proto,
             &dict_object_key_at_item, 1, "obj idx");
  rt_import (c2m_ctx, "dict_object_value_at", NULL, &dict_object_value_at_proto,
             &dict_object_value_at_item, 1, "obj idx");
  rt_import (c2m_ctx, "dict_value_at", NULL, &dict_value_at_proto, &dict_value_at_item, 1,
             "obj idx");
  /* dict_is_array: 1 iff d->type == DICT_ARRAY */
  rt_import (c2m_ctx, "dict_is_array", NULL, &dict_is_array_proto, &dict_is_array_item, 1, "d");
  /* dict_iter_count: array length for DICT_ARRAY, pair count for DICT_OBJECT, else 0 */
  rt_import (c2m_ctx, "dict_iter_count", NULL, &dict_iter_count_proto, &dict_iter_count_item, 1,
             "d");
  rt_import (c2m_ctx, "dict_create_array", NULL, &dict_create_array_proto,
             &dict_create_array_item, 1, "");
  rt_import (c2m_ctx, "dict_array_append", NULL, &dict_array_append_proto,
             &dict_array_append_item, 0, "arr val");
  rt_import (c2m_ctx, "dict_deserialize_json", NULL, &dict_deserialize_json_proto,
             &dict_deserialize_json_item, 1, "json");
  rt_import (c2m_ctx, "dict_serialize_json", NULL, &dict_serialize_json_proto,
             &dict_serialize_json_item, 1, "val buf len pretty");
  /* dict_serialize_json_heap: heap-allocating, right-sized variant used by the
     compiler's `d.json()` / `json(d)` codegen.  The returned pointer is
     plain-malloc'd; the compiler registers it with the String arena
     (c2m_str_attach) so the normal scope cleanup / return-protection path
     manages its lifetime. */
  rt_import (c2m_ctx, "dict_serialize_json_heap", NULL, &dict_serialize_json_heap_proto,
             &dict_serialize_json_heap_item, 1, "val pretty");
  /* dict_destroy: used by `delete d` for dict */
  rt_import (c2m_ctx, "dict_destroy", NULL, &dict_destroy_proto, &dict_destroy_item, 0, "val");
  /* dict_create_heap_arena: used by `new dict(size)` */
  rt_import (c2m_ctx, "dict_create_heap_arena", NULL, &dict_create_heap_arena_proto,
             &dict_create_heap_arena_item, 1, "bytes");
}

  /* Exception runtime helpers - lazily imported on first try/throw encountered. */
static void exception_ensure_imports (c2m_ctx_t c2m_ctx) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;

  if (cy_exc_throw_item != NULL) return;

  /* void *cy_exc_push(void) - push a frame, return a pointer to its jmp_buf. */
  rt_import (c2m_ctx, "cy_exc_push", NULL, &cy_exc_push_proto, &cy_exc_push_item, 1, "");
  /* void cy_exc_pop(void) - unwind one frame. */
  rt_import (c2m_ctx, "cy_exc_pop", NULL, &cy_exc_pop_proto, &cy_exc_pop_item, 0, "");
  /* void *cy_exc_current(void) - pointer to the current exception record. */
  rt_import (c2m_ctx, "cy_exc_current", NULL, &cy_exc_current_proto, &cy_exc_current_item, 1, "");
  /* void cy_exc_throw(unsigned id, const char *msg, const char *file, int line)
     records the exception and longjmps to the innermost frame (never returns). */
  rt_import (c2m_ctx, "cy_exc_throw", NULL, &cy_exc_throw_proto, &cy_exc_throw_item, 0,
             "id msg file line");
  /* int setjmp(void *buf) - libc; emitted inline in the try-containing function
     so it captures that frame.  Declared to return a 64-bit value (the int
     result is zero/sign-extended in the return register on supported ABIs). */
  rt_import (c2m_ctx, "setjmp", "cy_setjmp", &cy_setjmp_proto, &cy_setjmp_item, 1, "buf");
  /* void cy_exc_set_marks(size_t str_mark, size_t obj_mark, size_t defer_mark)
     - bank the String/object/defer-thunk-stack marks for the frame just
     pushed. */
  rt_import (c2m_ctx, "cy_exc_set_marks", NULL, &cy_exc_set_marks_proto, &cy_exc_set_marks_item,
             0, "str_mark obj_mark defer_mark");
  /* size_t cy_exc_current_str_mark(void) / cy_exc_current_obj_mark(void) -
     read back the marks banked for the currently-topmost frame.  Call before
     cy_exc_pop() on the dispatch path (it reads cy__exc_depth - 1). */
  rt_import (c2m_ctx, "cy_exc_current_str_mark", NULL, &cy_exc_current_str_mark_proto,
             &cy_exc_current_str_mark_item, 1, "");
  rt_import (c2m_ctx, "cy_exc_current_obj_mark", NULL, &cy_exc_current_obj_mark_proto,
             &cy_exc_current_obj_mark_item, 1, "");
  rt_import (c2m_ctx, "cy_exc_current_defer_mark", NULL, &cy_exc_current_defer_mark_proto,
             &cy_exc_current_defer_mark_item, 1, "");
  /* defer/owned cleanup thunk stack (cyexc.h): see the big comment there.
     cy_defer_push registers a (fn, arg) pair at the point a trackable
     cleanup (owned auto-release, `defer delete <class-ptr>`) is registered;
     checkpoint/discard_one keep it in sync with the normal AST-replay path;
     release_to is what the exception-dispatch path calls to actually run
     pending cleanups a `throw`'s longjmp would otherwise skip. */
  rt_import (c2m_ctx, "cy_defer_push", NULL, &cy_defer_push_proto, &cy_defer_push_item, 0,
             "fn arg");
  rt_import (c2m_ctx, "cy_defer_checkpoint", NULL, &cy_defer_checkpoint_proto,
             &cy_defer_checkpoint_item, 1, "");
  rt_import (c2m_ctx, "cy_defer_discard_one", NULL, &cy_defer_discard_one_proto,
             &cy_defer_discard_one_item, 0, "");
  rt_import (c2m_ctx, "cy_defer_release_to", NULL, &cy_defer_release_to_proto,
             &cy_defer_release_to_item, 0, "mark");
}

/* If NODE is a `delete <class-ptr-expr>;` statement whose class has a
   runtime-callable cleanup thunk (`__thunk_dtor_<C>`, synthesized at
   check/ownership time by ensure_defer_thunk), return the thunk's MIR item,
   creating a forward declaration for it when the thunk's definition sits
   later in the module than the function currently being generated (the
   ownership pass appends its thunks at the module end; a forward and the
   later real definition unify by name at MIR_finish_module time -- the same
   pattern gen_forward_class_methods uses for class methods).  Otherwise
   NULL.

   This lookup is the SINGLE source of truth for "does this defer_stmts
   entry have a shadow-stack entry": gen_defer_shadow_push emits
   cy_defer_push exactly when this returns non-NULL, and gen_run_defers
   emits cy_defer_discard_one under the same condition, so the pushed set
   and the discarded set are identical by construction. */
static MIR_item_t defer_shadow_thunk_item (c2m_ctx_t c2m_ctx, node_t delete_node) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_context_t ctx = c2m_ctx->ctx;
  node_t expr, thunk_id, thunk_def;
  struct expr *de;
  decl_t thunk_decl;
  const char *cname;
  char thunk_name[512];

  if (c2m_options == NULL || !c2m_options->exceptions_p) return NULL;
  if (delete_node == NULL || delete_node->code != N_DELETE) return NULL;
  if (delete_node->attr == (void *) (intptr_t) -1) return NULL; /* dead-code delete sentinel */
  expr = NL_EL (delete_node->u.ops, 1);
  de = expr != NULL ? (struct expr *) expr->attr : NULL;
  if (de == NULL || de->type == NULL || de->type->mode != TM_PTR) return NULL;
  cname = class_type_name (de->type->u.ptr_type);
  if (cname == NULL) return NULL;
  snprintf (thunk_name, sizeof (thunk_name), "__thunk_dtor_%s", cname);
  thunk_id = build_id (c2m_ctx, thunk_name, POS (delete_node));
  if (top_scope == NULL) return NULL;
  thunk_def = find_def (c2m_ctx, S_REGULARS, thunk_id, top_scope, NULL);
  if (thunk_def == NULL || thunk_def->code != N_FUNC_DEF || thunk_def->attr == NULL) return NULL;
  thunk_decl = (decl_t) thunk_def->attr;
  if (thunk_decl->u.item == NULL) thunk_decl->u.item = MIR_new_forward (ctx, thunk_name);
  return thunk_decl->u.item;
}

/* Registration-time half of the exception-path defer shadow stack: when NODE
   is a trackable `delete <class-ptr-expr>;`, evaluate the pointer expression
   NOW (Go-style: the deferred value is captured at registration, not at
   scope exit) and push (thunk, ptr) onto cy__defer_stack.  Normal syntactic
   exits still replay NODE via gen_run_defers (which discards the shadow
   entry); only a throw's longjmp -- which skips every syntactic exit --
   leaves the entry for the dispatch path's cy_defer_release_to.  No-op
   unless -fexceptions and the class has a synthesized thunk. */
static void gen_defer_shadow_push (c2m_ctx_t c2m_ctx, node_t delete_node) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_context_t ctx = c2m_ctx->ctx;
  MIR_item_t thunk = defer_shadow_thunk_item (c2m_ctx, delete_node);
  node_t expr;
  op_t pv;
  MIR_op_t push_args[2];

  if (thunk == NULL) return;
  exception_ensure_imports (c2m_ctx);
  expr = NL_EL (delete_node->u.ops, 1);
  pv = gen (c2m_ctx, expr, NULL, NULL, FALSE, NULL, NULL);
  pv = force_val (c2m_ctx, pv, FALSE);
  pv = force_reg (c2m_ctx, pv, MIR_T_I64);
  push_args[0] = MIR_new_ref_op (ctx, thunk);
  push_args[1] = pv.mir_op;
  gen_rt_call_void (c2m_ctx, cy_defer_push_proto, cy_defer_push_item, 2, push_args);
}

/* -ffibers go/await runtime (cyfiber.h, bound by the driver's import_resolver
   under JIT and by mir-aot-runtime.c CHANFIBERS under AOT).  Lazily imported
   on the first `go` / `await` in a TU — without -ffibers these never fire,
   so programs see no fiber runtime pollution. */
static void fiber_ensure_imports (c2m_ctx_t c2m_ctx) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;

  if (cy_spawn8_item != NULL) return;
  /* void cy_spawn8(void *fn, long nargs, long a0, …, long a7) */
  rt_import (c2m_ctx, "cy_spawn8", NULL, &cy_spawn8_proto, &cy_spawn8_item, 0,
             "fn nargs a0 a1 a2 a3 a4 a5 a6 a7");
  /* void cy_yield(void) */
  rt_import (c2m_ctx, "cy_yield", NULL, &cy_yield_proto, &cy_yield_item, 0, "");
}

/* Emit cy_exc_throw(id, msg, file, line).  Never returns (it longjmps or
   aborts); an unreachable label is appended so MIR's CFG stays well formed. */
static void gen_exception_throw_call (c2m_ctx_t c2m_ctx,
                                      MIR_op_t id_op,
                                      MIR_op_t msg_op) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_context_t ctx = c2m_ctx->ctx;
  MIR_op_t ops[4];

  exception_ensure_imports (c2m_ctx);
  ops[0] = id_op;
  ops[1] = msg_op;
  ops[2] = zero_op.mir_op;      /* file: null for now (future: __FILE__) */
  ops[3] = zero_op.mir_op;      /* line: 0                                */
  gen_rt_call_void (c2m_ctx, cy_exc_throw_proto, cy_exc_throw_item, 4, ops);

  MIR_label_t unreach = MIR_new_label (ctx);
  emit_label_insn_opt (c2m_ctx, unreach);
}

/* ── JIT safety guards (emitted only when -fexceptions is enabled) ──────────
   Each guard emits a conditional branch to a call to _safety_trap(reason, 0,
   line), which either throws a catchable exception (when inside a try block)
   or aborts with a diagnostic (uncaught fault).
   reason: 1=OOB, 2=null-ptr, 3=arithmetic (div-by-zero). */

static void safety_ensure_imports (c2m_ctx_t c2m_ctx) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;

  if (safety_trap_item != NULL) return;
  exception_ensure_imports (c2m_ctx); /* also pull in cy_exc_* */
  /* void _safety_trap(long reason, long file_id, long line) */
  rt_import (c2m_ctx, "_safety_trap", "safety_trap", &safety_trap_proto, &safety_trap_item, 0,
             "reason file_id line");
  /* void *cy_safe_alloc(uint64_t size) */
  rt_import (c2m_ctx, "cy_safe_alloc", NULL, &cy_safe_alloc_proto, &cy_safe_alloc_item, 1,
             "size");
  /* void cy_safe_free(void *ptr, long line) */
  rt_import (c2m_ctx, "cy_safe_free", NULL, &cy_safe_free_proto, &cy_safe_free_item, 0,
             "ptr line");
  /* void cy_safe_deref(void *ptr, long line) */
  rt_import (c2m_ctx, "cy_safe_deref", NULL, &cy_safe_deref_proto, &cy_safe_deref_item, 0,
             "ptr line");
}

/* Guard: if ptr_op == 0 (NULL), call _safety_trap(2, 0, line).
   ptr_op must already be an I64 register (use force_reg before calling). */
static void gen_null_check (c2m_ctx_t c2m_ctx, op_t ptr_op, long line) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_context_t ctx = c2m_ctx->ctx;
  MIR_label_t ok_label = MIR_new_label (ctx);
  MIR_op_t trap_args[3];
  safety_ensure_imports (c2m_ctx);
  /* BNE ok_label, ptr, 0 -- skip trap if ptr != NULL */
  emit3 (c2m_ctx, MIR_BNE, MIR_new_label_op (ctx, ok_label),
         ptr_op.mir_op, zero_op.mir_op);
  trap_args[0] = MIR_new_int_op (ctx, 2); /* reason: null-ptr */
  trap_args[1] = zero_op.mir_op;
  trap_args[2] = MIR_new_int_op (ctx, line);
  gen_rt_call_void (c2m_ctx, safety_trap_proto, safety_trap_item, 3, trap_args);
  emit_label_insn_opt (c2m_ctx, ok_label);
}

/* Guard: if divisor_op == 0, call _safety_trap(3, 0, line) (div-by-zero).
   divisor_op must be an I64 register.  Integer types only. */
static void gen_div_zero_check (c2m_ctx_t c2m_ctx, op_t divisor_op, long line) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_context_t ctx = c2m_ctx->ctx;
  MIR_label_t ok_label = MIR_new_label (ctx);
  MIR_op_t trap_args[3];
  safety_ensure_imports (c2m_ctx);
  /* BNE ok_label, divisor, 0 -- skip trap if divisor != 0 */
  emit3 (c2m_ctx, MIR_BNE, MIR_new_label_op (ctx, ok_label),
         divisor_op.mir_op, zero_op.mir_op);
  trap_args[0] = MIR_new_int_op (ctx, 3); /* reason: arithmetic */
  trap_args[1] = zero_op.mir_op;
  trap_args[2] = MIR_new_int_op (ctx, line);
  gen_rt_call_void (c2m_ctx, safety_trap_proto, safety_trap_item, 3, trap_args);
  emit_label_insn_opt (c2m_ctx, ok_label);
}

/* Guard: signed INT_MIN / -1 (and % ) overflow, which raises SIGFPE on x86.
   Traps reason 3 (arithmetic) when dividend == min_val AND divisor == -1.
   Both ops must be I64 registers (values are sign-extended, so the 32-bit
   INT32_MIN case is represented correctly as a negative I64). */
static void gen_div_overflow_check (c2m_ctx_t c2m_ctx, op_t dividend_op, op_t divisor_op,
                                    long long min_val, long line) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_context_t ctx = c2m_ctx->ctx;
  MIR_label_t ok_label = MIR_new_label (ctx);
  MIR_op_t trap_args[3];
  safety_ensure_imports (c2m_ctx);
  /* if divisor != -1 -> safe */
  emit3 (c2m_ctx, MIR_BNE, MIR_new_label_op (ctx, ok_label),
         divisor_op.mir_op, MIR_new_int_op (ctx, -1));
  /* divisor == -1: if dividend != min_val -> safe */
  emit3 (c2m_ctx, MIR_BNE, MIR_new_label_op (ctx, ok_label),
         dividend_op.mir_op, MIR_new_int_op (ctx, min_val));
  trap_args[0] = MIR_new_int_op (ctx, 3); /* reason: arithmetic */
  trap_args[1] = zero_op.mir_op;
  trap_args[2] = MIR_new_int_op (ctx, line);
  gen_rt_call_void (c2m_ctx, safety_trap_proto, safety_trap_item, 3, trap_args);
  emit_label_insn_opt (c2m_ctx, ok_label);
}

/* Guard: if (uint64_t)idx_op >= len_op, call _safety_trap(1, 0, line).
   Callers that want negative-signed-index protection must emit
   a BGE guard before calling this helper. */
static void gen_oob_check (c2m_ctx_t c2m_ctx, op_t idx_op, MIR_op_t len_op, long line) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_context_t ctx = c2m_ctx->ctx;
  MIR_label_t ok_label = MIR_new_label (c2m_ctx->ctx);
  MIR_op_t trap_args[3];
  safety_ensure_imports (c2m_ctx);
  /* UBLT ok_label, idx, len -- if (unsigned)idx < len skip trap (safe) */
  emit3 (c2m_ctx, MIR_UBLT, MIR_new_label_op (c2m_ctx->ctx, ok_label),
         idx_op.mir_op, len_op);
  trap_args[0] = MIR_new_int_op (c2m_ctx->ctx, 1); /* reason: out-of-bounds */
  trap_args[1] = zero_op.mir_op;
  trap_args[2] = MIR_new_int_op (c2m_ctx->ctx, line);
  gen_rt_call_void (c2m_ctx, safety_trap_proto, safety_trap_item, 3, trap_args);
  emit_label_insn_opt (c2m_ctx, ok_label);
}

/* Set by N_ADDR when its operand is N_IND: `&a[n]` is a valid one-past-end
   pointer, so the OOB check uses length+1.  Nested subscripts (e.g. the
   `a[i]` in `&a[i].f`) keep the strict bound. */
static int gen_ind_one_past_p;

/* Load an integer member at BASE_PTR+offset and widen it to i64. */
static op_t gen_load_member_i64 (c2m_ctx_t c2m_ctx, op_t base_ptr, decl_t member) {
  MIR_context_t ctx = c2m_ctx->ctx;
  MIR_type_t mt = get_mir_type (c2m_ctx, member->decl_spec.type);
  op_t tmp = get_new_temp (c2m_ctx, mt);
  MIR_op_t mem
    = MIR_new_mem_op (ctx, mt, (MIR_disp_t) member->offset, base_ptr.mir_op.u.reg, 0, 1);
  emit2 (c2m_ctx, MIR_MOV, tmp.mir_op, mem);
  if (mt == MIR_T_I64 || mt == MIR_T_U64) return tmp;
  return cast (c2m_ctx, tmp, MIR_T_I64, FALSE);
}

/* C-array OOB under -fexceptions.  Fixed-size arrays use the declared
   length.  Trailing FAM (`T a[1]`/`a[0]`/`a[]`):
     · value object (s.a[i])  — declared size + brace-init extra
     · pointer object (p->a[i]) — sibling capacity if we named one; else no
       static check (the declared 1 is not the live bound). */
static void gen_c_array_oob (c2m_ctx_t c2m_ctx, node_t r, node_t arr, struct type *arr_type,
                             op_t idx_op) {
  MIR_context_t ctx = c2m_ctx->ctx;
  struct arr_type *ainfo;
  op_t idx_check;
  node_t abase, sz_node;
  struct expr *sze;
  int one_past = gen_ind_one_past_p;

  if (!c2m_options->exceptions_p || arr_type == NULL) return;
  if (r->attr != NULL && ((struct expr *) r->attr)->elide_oob_p) return;
  ainfo = type_arr_info (arr_type);
  if (ainfo == NULL) return;
  idx_check = force_reg (c2m_ctx, idx_op, MIR_T_I64);
  abase = arr;
  while (abase != NULL && abase->code == N_CAST) abase = NL_EL (abase->u.ops, 1);

  if (ainfo->flex_p) {
    if (abase != NULL && abase->code == N_FIELD) {
      mir_llong len = 0;
      node_t obj;
      if (ainfo->size != NULL && ainfo->size->code != N_IGNORE && ainfo->size->attr != NULL) {
        sze = (struct expr *) ainfo->size->attr;
        if (sze->const_p && sze->c.i_val > 0) len = sze->c.i_val;
      }
      obj = NL_HEAD (abase->u.ops);
      if (obj != NULL && obj->attr != NULL) {
        struct expr *oe = (struct expr *) obj->attr;
        node_t def = oe->def_node != NULL ? oe->def_node : oe->u.lvalue_node;
        if (def != NULL && def->attr != NULL
            && (def->code == N_SPEC_DECL || def->code == N_MEMBER)) {
          decl_t od = (decl_t) def->attr;
          if (od->flex_extra_size > 0 && ainfo->el_type != NULL) {
            mir_size_t elsz = type_size (c2m_ctx, ainfo->el_type);
            if (elsz > 0) len += (mir_llong) (od->flex_extra_size / elsz);
          }
        }
      }
      if (len > 0)
        gen_oob_check (c2m_ctx, idx_check,
                       MIR_new_int_op (ctx, (long long) (one_past ? len + 1 : len)),
                       (long) POS (r).lno);
      return;
    }
    if (ainfo->flex_bound_member != NULL && ainfo->flex_bound_member->attr != NULL
        && abase != NULL && abase->code == N_DEREF_FIELD) {
      decl_t bd = (decl_t) ainfo->flex_bound_member->attr;
      node_t obj = NL_HEAD (abase->u.ops);
      if (obj != NULL && bd->decl_spec.type != NULL && integer_type_p (bd->decl_spec.type)) {
        op_t base = val_gen (c2m_ctx, obj);
        base = force_reg (c2m_ctx, base, MIR_T_I64);
        {
          op_t blen = gen_load_member_i64 (c2m_ctx, base, bd);
          if (one_past) {
            op_t lim = get_new_temp (c2m_ctx, MIR_T_I64);
            emit3 (c2m_ctx, MIR_ADD, lim.mir_op, blen.mir_op, MIR_new_int_op (ctx, 1));
            gen_oob_check (c2m_ctx, idx_check, lim.mir_op, (long) POS (r).lno);
          } else {
            gen_oob_check (c2m_ctx, idx_check, blen.mir_op, (long) POS (r).lno);
          }
        }
      }
    }
    return;
  }

  sz_node = ainfo->size;
  if (sz_node == NULL || sz_node->code == N_IGNORE || sz_node->attr == NULL) return;
  sze = (struct expr *) sz_node->attr;
  if (sze->const_p && sze->c.i_val > 0)
    gen_oob_check (c2m_ctx, idx_check,
                   MIR_new_int_op (ctx, (long long) (one_past ? sze->c.i_val + 1 : sze->c.i_val)),
                   (long) POS (r).lno);
}

/* Guard: shift count must be in [0, width_bits).  C11 §6.5.7/3 — negative or
   >= width is UB.  Emits _safety_trap(5, …) (shift out of range).
   count_op must be an I64 register.  width_bits is 8/16/32/64. */
static void gen_shift_range_check (c2m_ctx_t c2m_ctx, op_t count_op, int width_bits, long line) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_context_t ctx = c2m_ctx->ctx;
  MIR_label_t ok_label = MIR_new_label (ctx);
  MIR_label_t trap_label = MIR_new_label (ctx);
  MIR_op_t trap_args[3];
  op_t cnt = force_reg (c2m_ctx, count_op, MIR_T_I64);
  safety_ensure_imports (c2m_ctx);
  /* if count < 0 → trap */
  emit3 (c2m_ctx, MIR_BLT, MIR_new_label_op (ctx, trap_label), cnt.mir_op, zero_op.mir_op);
  /* if count >= width → trap; else ok */
  emit3 (c2m_ctx, MIR_BGE, MIR_new_label_op (ctx, trap_label), cnt.mir_op,
         MIR_new_int_op (ctx, width_bits));
  emit1 (c2m_ctx, MIR_JMP, MIR_new_label_op (ctx, ok_label));
  emit_label_insn_opt (c2m_ctx, trap_label);
  trap_args[0] = MIR_new_int_op (ctx, 5); /* reason: shift out of range */
  trap_args[1] = zero_op.mir_op;
  trap_args[2] = MIR_new_int_op (ctx, line);
  gen_rt_call_void (c2m_ctx, safety_trap_proto, safety_trap_item, 3, trap_args);
  emit_label_insn_opt (c2m_ctx, ok_label);
}

/* Guard: if size_op <= 0, call _safety_trap(3, 0, line) (arithmetic).
   Prevents negative/zero VLA sizes from reaching alloca (UB per C99/C11).
   size_op must be an I64 register.  Emitted only under -fexceptions. */
static void gen_vla_size_check (c2m_ctx_t c2m_ctx, op_t size_op, long line) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_context_t ctx = c2m_ctx->ctx;
  MIR_label_t ok_label = MIR_new_label (ctx);
  MIR_op_t trap_args[3];
  /* Always force to a fresh I64 reg so the subsequent BGT uses signed comparison
     semantics even if the alloca size came from a size_t (U64) multiply.  Negative
     VLA dim produces negative bits that we must detect as <=0. */
  op_t sz = get_new_temp (c2m_ctx, MIR_T_I64);
  emit2 (c2m_ctx, MIR_MOV, sz.mir_op, size_op.mir_op);
  safety_ensure_imports (c2m_ctx);
  /* BGT ok_label, size, 0  -- skip trap if size > 0 */
  emit3 (c2m_ctx, MIR_BGT, MIR_new_label_op (ctx, ok_label),
         sz.mir_op, zero_op.mir_op);
  trap_args[0] = MIR_new_int_op (ctx, 3); /* reason: arithmetic */
  trap_args[1] = zero_op.mir_op;
  trap_args[2] = MIR_new_int_op (ctx, line);
  gen_rt_call_void (c2m_ctx, safety_trap_proto, safety_trap_item, 3, trap_args);
  emit_label_insn_opt (c2m_ctx, ok_label);
}

/* Guard: if obj_op == 0 after a `new` allocation, throw RuntimeException("out of memory").
   obj_op must be an I64 register holding the malloc result. */
static void gen_oom_check (c2m_ctx_t c2m_ctx, op_t obj_op, long line) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_context_t ctx = c2m_ctx->ctx;
  MIR_label_t ok_label = MIR_new_label (ctx);
  MIR_op_t ops[4];
  exception_ensure_imports (c2m_ctx);
  /* BNE ok: skip throw if allocation succeeded */
  emit3 (c2m_ctx, MIR_BNE, MIR_new_label_op (ctx, ok_label),
         obj_op.mir_op, zero_op.mir_op);
  ops[0] = MIR_new_int_op (ctx, 4); /* CY_EXC_RUNTIME */
  ops[1] = MIR_new_str_op (ctx, (MIR_str_t){14, "out of memory"});
  ops[2] = zero_op.mir_op;
  ops[3] = MIR_new_int_op (ctx, line);
  gen_rt_call_void (c2m_ctx, cy_exc_throw_proto, cy_exc_throw_item, 4, ops);
  emit_label_insn_opt (c2m_ctx, ok_label); /* also serves as unreachable anchor after throw */
}

/* Guard: call cy_safe_deref(ptr, line) before a ptr->field access on a class pointer.
   Throws RuntimeException("use-after-free") if the object was already deleted. */
static void gen_class_deref_check (c2m_ctx_t c2m_ctx, op_t ptr_op, long line) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_context_t ctx = c2m_ctx->ctx;
  MIR_op_t args[2];
  safety_ensure_imports (c2m_ctx);
  args[0] = ptr_op.mir_op;
  args[1] = MIR_new_int_op (ctx, line);
  gen_rt_call_void (c2m_ctx, cy_safe_deref_proto, cy_safe_deref_item, 2, args);
}

/* ── -fobject-guards: side-table UAF/double-free runtime imports/emitters ──
   Layout-preserving object guards (see cyexc.h).  Only emitted when
   c2m_options->object_guards_p is set.  cy_obj_track registers a `new` object,
   cy_obj_note_free marks it dead + quarantines (used in place of free), and
   cy_obj_check verifies liveness before an ownership-CHECK dereference. */
static void object_guard_ensure_imports (c2m_ctx_t c2m_ctx) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;

  if (cy_obj_check_item != NULL) return;
  exception_ensure_imports (c2m_ctx); /* cy_obj_* may throw via cy_exc_throw */
  /* void cy_obj_track(void *ptr) */
  rt_import (c2m_ctx, "cy_obj_track", NULL, &cy_obj_track_proto, &cy_obj_track_item, 0, "ptr");
  /* void cy_obj_note_free(void *ptr, long line) */
  rt_import (c2m_ctx, "cy_obj_note_free", NULL, &cy_obj_note_free_proto, &cy_obj_note_free_item,
             0, "ptr line");
  /* void cy_obj_check(void *ptr, long line) */
  rt_import (c2m_ctx, "cy_obj_check", NULL, &cy_obj_check_proto, &cy_obj_check_item, 0,
             "ptr line");
}

/* cy_obj_track(ptr) — register a freshly `new`-allocated object as live. */
static void gen_obj_guard_track (c2m_ctx_t c2m_ctx, MIR_op_t ptr) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  object_guard_ensure_imports (c2m_ctx);
  gen_rt_call_void (c2m_ctx, cy_obj_track_proto, cy_obj_track_item, 1, &ptr);
}

/* cy_obj_note_free(ptr, line) — mark dead + quarantine (replaces free). */
static void gen_obj_guard_note_free (c2m_ctx_t c2m_ctx, MIR_op_t ptr, long line) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_context_t ctx = c2m_ctx->ctx;
  MIR_op_t args[2];
  object_guard_ensure_imports (c2m_ctx);
  args[0] = ptr;
  args[1] = MIR_new_int_op (ctx, line);
  gen_rt_call_void (c2m_ctx, cy_obj_note_free_proto, cy_obj_note_free_item, 2, args);
}

/* cy_obj_check(ptr, line) — throw use-after-free if ptr is a known-dead object. */
static void gen_obj_guard_check (c2m_ctx_t c2m_ctx, op_t ptr_op, long line) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_context_t ctx = c2m_ctx->ctx;
  MIR_op_t args[2];
  object_guard_ensure_imports (c2m_ctx);
  args[0] = ptr_op.mir_op;
  args[1] = MIR_new_int_op (ctx, line);
  gen_rt_call_void (c2m_ctx, cy_obj_check_proto, cy_obj_check_item, 2, args);
}

/* Emit: res = dict_create_object() */
/* ───────── Runtime-helper call emission ─────────
   The String/dict/object runtime helpers are all called the same way: a
   MIR_CALL whose operands are the helper's proto ref, its import ref, an
   optional single I64 result, then the argument ops.  These two helpers
   centralise that boilerplate (every `gen_dict_*`/`gen_str_*`/`gen_obj_*`
   wrapper below is a thin call to one of them).  Both allocate the result temp
   before filling the operand array, matching the hand-written call sites they
   replaced, so the emitted MIR is unchanged. */
#define GEN_RT_MAX_ARGS 10

/* res = (*proto/item)(arg_ops[0..nargs-1]) for a helper returning one I64 value.
   Returns the result temp; callers that ignore it (void-returning-but-discarded
   helpers) simply drop the return. */
static op_t gen_rt_call (c2m_ctx_t c2m_ctx, MIR_item_t proto, MIR_item_t item, size_t nargs,
                         const MIR_op_t *arg_ops) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_context_t ctx = c2m_ctx->ctx;
  MIR_op_t args[3 + GEN_RT_MAX_ARGS];
  op_t res = get_new_temp (c2m_ctx, MIR_T_I64);

  assert (nargs <= GEN_RT_MAX_ARGS);
  args[0] = MIR_new_ref_op (ctx, proto);
  args[1] = MIR_new_ref_op (ctx, item);
  args[2] = res.mir_op;
  for (size_t i = 0; i < nargs; i++) args[3 + i] = arg_ops[i];
  emit_insn (c2m_ctx, MIR_new_insn_arr (ctx, MIR_CALL, 3 + nargs, args));
  return res;
}

/* (*proto/item)(arg_ops[0..nargs-1]) for a helper returning void (proto nres 0). */
static void gen_rt_call_void (c2m_ctx_t c2m_ctx, MIR_item_t proto, MIR_item_t item, size_t nargs,
                              const MIR_op_t *arg_ops) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_context_t ctx = c2m_ctx->ctx;
  MIR_op_t args[2 + GEN_RT_MAX_ARGS];

  assert (nargs <= GEN_RT_MAX_ARGS);
  args[0] = MIR_new_ref_op (ctx, proto);
  args[1] = MIR_new_ref_op (ctx, item);
  for (size_t i = 0; i < nargs; i++) args[2 + i] = arg_ops[i];
  emit_insn (c2m_ctx, MIR_new_insn_arr (ctx, MIR_CALL, 2 + nargs, args));
}

/* Emit: res = dict_create_object() */
static op_t gen_dict_create_object (c2m_ctx_t c2m_ctx) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;

  dict_ensure_imports (c2m_ctx);
  return gen_rt_call (c2m_ctx, dict_create_object_proto, dict_create_object_item, 0, NULL);
}

/* Emit: dict_destroy(val)  — called by `delete d` for dict values. */
static void gen_dict_destroy (c2m_ctx_t c2m_ctx, MIR_op_t val_op) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  dict_ensure_imports (c2m_ctx);
  gen_rt_call_void (c2m_ctx, dict_destroy_proto, dict_destroy_item, 1, &val_op);
}

/* Emit: res = dict_create_heap_arena(bytes)  — called by `new dict(size?)`. */
static op_t gen_dict_create_heap_arena_call (c2m_ctx_t c2m_ctx, MIR_op_t size_op) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  dict_ensure_imports (c2m_ctx);
  return gen_rt_call (c2m_ctx, dict_create_heap_arena_proto, dict_create_heap_arena_item, 1,
                      &size_op);
}

/* Emit: res = dict_create_bool(val) — produces JSON true/false */
static op_t gen_dict_create_bool (c2m_ctx_t c2m_ctx, MIR_op_t val_op) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  dict_ensure_imports (c2m_ctx);
  return gen_rt_call (c2m_ctx, dict_create_bool_proto, dict_create_bool_item, 1, &val_op);
}

/* Emit: res = dict_create_int64(val) */
static op_t gen_dict_create_int64 (c2m_ctx_t c2m_ctx, MIR_op_t val_op) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  dict_ensure_imports (c2m_ctx);
  return gen_rt_call (c2m_ctx, dict_create_int64_proto, dict_create_int64_item, 1, &val_op);
}

/* Emit: res = dict_create_number(val) */
static op_t gen_dict_create_number (c2m_ctx_t c2m_ctx, MIR_op_t val_op) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  dict_ensure_imports (c2m_ctx);
  return gen_rt_call (c2m_ctx, dict_create_number_proto, dict_create_number_item, 1, &val_op);
}

/* Emit: res = dict_create_string(str_op) */
static op_t gen_dict_create_string (c2m_ctx_t c2m_ctx, MIR_op_t str_op) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  dict_ensure_imports (c2m_ctx);
  return gen_rt_call (c2m_ctx, dict_create_string_proto, dict_create_string_item, 1, &str_op);
}

/* Emit: dict_object_set(obj_op, key_str_op, val_op)  (int result discarded) */
static void gen_dict_object_set (c2m_ctx_t c2m_ctx, MIR_op_t obj_op, MIR_op_t key_op,
                                 MIR_op_t val_op) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_op_t a[3] = {obj_op, key_op, val_op};
  dict_ensure_imports (c2m_ctx);
  gen_rt_call (c2m_ctx, dict_object_set_proto, dict_object_set_item, 3, a);
}

/* Emit: res = dict_object_get(obj_op, key_str_op) */
static op_t gen_dict_object_get (c2m_ctx_t c2m_ctx, MIR_op_t obj_op, MIR_op_t key_op) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_op_t a[2] = {obj_op, key_op};
  dict_ensure_imports (c2m_ctx);
  return gen_rt_call (c2m_ctx, dict_object_get_proto, dict_object_get_item, 2, a);
}

/* Emit: res = dict_value_copy(src_op)  (deep clone of a DictValue*) */
static op_t gen_dict_value_copy (c2m_ctx_t c2m_ctx, MIR_op_t src_op) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  dict_ensure_imports (c2m_ctx);
  return gen_rt_call (c2m_ctx, dict_value_copy_proto, dict_value_copy_item, 1, &src_op);
}

/* Emit: res = dict_object_count(obj_op) */
static op_t gen_dict_object_count (c2m_ctx_t c2m_ctx, MIR_op_t obj_op) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  dict_ensure_imports (c2m_ctx);
  return gen_rt_call (c2m_ctx, dict_object_count_proto, dict_object_count_item, 1, &obj_op);
}

/* Emit: res = dict_object_key_at(obj_op, idx_op) */
static op_t gen_dict_object_key_at (c2m_ctx_t c2m_ctx, MIR_op_t obj_op, MIR_op_t idx_op) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_op_t a[2] = {obj_op, idx_op};
  dict_ensure_imports (c2m_ctx);
  return gen_rt_call (c2m_ctx, dict_object_key_at_proto, dict_object_key_at_item, 2, a);
}

/* Emit: res = dict_object_value_at(obj_op, idx_op) */
static op_t gen_dict_object_value_at (c2m_ctx_t c2m_ctx, MIR_op_t obj_op, MIR_op_t idx_op) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_op_t a[2] = {obj_op, idx_op};
  dict_ensure_imports (c2m_ctx);
  return gen_rt_call (c2m_ctx, dict_object_value_at_proto, dict_object_value_at_item, 2, a);
}

/* Emit: res = dict_value_at(obj_op, idx_op) — unified array/object index lookup */
static op_t gen_dict_value_at (c2m_ctx_t c2m_ctx, MIR_op_t obj_op, MIR_op_t idx_op) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_op_t a[2] = {obj_op, idx_op};
  dict_ensure_imports (c2m_ctx);
  return gen_rt_call (c2m_ctx, dict_value_at_proto, dict_value_at_item, 2, a);
}

/* Emit: res = dict_is_array(obj_op)   (used by for-in to dispatch on tag) */
static op_t gen_dict_is_array (c2m_ctx_t c2m_ctx, MIR_op_t obj_op) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  dict_ensure_imports (c2m_ctx);
  return gen_rt_call (c2m_ctx, dict_is_array_proto, dict_is_array_item, 1, &obj_op);
}

/* Emit: res = dict_iter_count(obj_op)  (array length OR object count) */
static op_t gen_dict_iter_count (c2m_ctx_t c2m_ctx, MIR_op_t obj_op) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  dict_ensure_imports (c2m_ctx);
  return gen_rt_call (c2m_ctx, dict_iter_count_proto, dict_iter_count_item, 1, &obj_op);
}

/* ---------------------------------------------------------------------------
   Dict -> by-value class bind cast: lower `(T)d` and `(T?)d` to a per-field
   walk over T's members (see JSONBINDING.md Phase 1).  The strict form throws
   KeyException on a missing field; the lenient `?` form leaves the field at
   its zero-initialized default.

     gen_dict_bind_emit_string_literal   - intern a C string as MIR data
     gen_dict_bind_throw_key_exception   - emit cy_exc_throw(KeyException, msg)
     gen_dict_bind_into                  - walk fields, recurse on classes
   --------------------------------------------------------------------------- */

/* Forward decls — the two helpers below sit *before* gen_dict_key_op and
   gen_dict_unwrap in this file (which they call into).  Declaring them up
   front lets us keep the bind code grouped with the other dict gen helpers. */
static MIR_op_t gen_dict_key_op (c2m_ctx_t c2m_ctx, const char *key_str, size_t len);
static op_t gen_dict_unwrap (c2m_ctx_t c2m_ctx, op_t dop);
/* c2m_str_own import setup (defined later) — the binder copies value-semantic
   String fields into class targets via str_own, matching the assignment path. */
static void string_ensure_imports (c2m_ctx_t c2m_ctx);

/* Intern STR as an anonymous string data item in the current module and return
   a ref op pointing to it.  Mirrors gen_dict_key_op's strategy so the string
   survives binary (.bmir) round-tripping. */
static MIR_op_t gen_dict_bind_emit_string_literal (c2m_ctx_t c2m_ctx, const char *str) {
  MIR_context_t ctx = c2m_ctx->ctx;
  MIR_module_t module = DLIST_TAIL (MIR_module_t, *MIR_get_module_list (ctx));
  char buff[50];
  size_t len = strlen (str) + 1;

  _MIR_get_temp_item_name (ctx, module, buff, sizeof (buff));
  MIR_item_t str_item = MIR_new_string_data (ctx, buff, (MIR_str_t){len, str});
  move_item_to_module_start (module, str_item);
  return MIR_new_ref_op (ctx, str_item);
}

/* Emit a `throw(KeyException, msg)` call with msg = "missing field 'F' in T".
   KeyException's enum value (8) is fixed by the exception prelude in
   add_standard_includes — see the `KeyException = 8` line. */
static void gen_dict_bind_throw_key_exception (c2m_ctx_t c2m_ctx,
                                               const char *class_name,
                                               const char *field_name) {
  /* Build the message string at compile time so it lives in static data. */
  size_t cn = strlen (class_name), fn = strlen (field_name);
  size_t need = cn + fn + 32; /* "missing field '' in " + nul */
  char *msg = reg_malloc (c2m_ctx, need);
  snprintf (msg, need, "missing field '%s' in %s", field_name, class_name);

  MIR_op_t msg_op = gen_dict_bind_emit_string_literal (c2m_ctx, msg);
  /* KeyException id == 8 (see exception_prelude). */
  MIR_op_t id_op = MIR_new_int_op (c2m_ctx->ctx, 8);
  gen_exception_throw_call (c2m_ctx, id_op, msg_op);
}

/* Phase 2: bind a dict array (DICT_ARRAY DictValue*) into a heap-allocated
   collection (List<T>* / Set<T>* / any class with a default ctor + Add(T)).

   CLS_PTR_TYPE is the field's declared type (TM_PTR -> TM_CLASS).  VAL_DV is
   the DictValue* for the field (already fetched from the parent dict object).
   Returns an op holding the new collection's pointer, ready to store into the
   field slot.

   Lowering:
     obj = malloc(sizeof(C)); memset(obj, 0, sizeof(C));
     C::C(obj);                       // default ctor (zero user params)
     n = dict_iter_count(val_dv);
     for (i = 0; i < n; i++) {
         el_dv = dict_value_at(val_dv, i);
         el    = unwrap(el_dv);        // scalar/String payload, or recurse for nested
         obj->Add(el);
     }

   The element type T is recovered from Add's first user parameter, so this
   works for any Add-protocol collection, not just List<T>.  Nested class
   elements recurse through gen_dict_bind_into; String elements take a private
   copy (c2m_str_own) so the bound object owns them, mirroring the String-field
   path.  Pointer-to-class elements (T = C*) allocate + zero the pointee and
   recurse gen_dict_bind_into into it, same as the by-value nested-class case
   but heap-allocated and passed by pointer. */
/* Forward declarations: gen_dict_bind_into (defined just below) recurses into
   nested class/struct elements; gen_class_method_call is defined later in the
   gen section. */
static void gen_dict_bind_into (c2m_ctx_t c2m_ctx, struct type *cls_type,
                                op_t src_dv_op, op_t dst_addr_op,
                                int lenient, pos_t pos);
/* Proven-safety flags for protocol/open-code call sites (see gen_class_method_call_dest). */
#define GEN_SAFE_SKIP_NULL 0x1 /* this already proven non-null (stack addr, prior check) */
#define GEN_SAFE_SKIP_OOB  0x2 /* index already proven in range (for-in / seq i in [0,n)) */
static op_t gen_class_method_call (c2m_ctx_t c2m_ctx, node_t func_def, struct type *this_type,
                                   op_t this_op, op_t *args, int n_args);
static op_t gen_class_method_call_flags (c2m_ctx_t c2m_ctx, node_t func_def,
                                         struct type *this_type, op_t this_op, op_t *args,
                                         int n_args, int safe_flags);
static op_t gen_dict_bind_collection_field (c2m_ctx_t c2m_ctx, struct type *cls_ptr_type,
                                            op_t val_dv, int lenient, pos_t pos) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_context_t ctx = c2m_ctx->ctx;
  struct type *cls_type = cls_ptr_type->u.ptr_type;
  node_t class_tag = cls_type->u.tag_type;
  const char *cname = class_type_name (cls_type);

  /* Find Add(T) to get the element type and the method to call. */
  node_t add_def = find_class_protocol_method (c2m_ctx, class_tag, "Add", 1, pos);
  if (add_def == NULL) {
    error (c2m_ctx, pos,
           "dict->collection bind: '%s' has no Add(T) method (Phase 2 needs the Add protocol)",
           cname != NULL ? cname : "<class>");
    return new_op (NULL, MIR_new_int_op (ctx, 0));
  }
  decl_t add_decl = add_def->attr;
  struct func_type *add_ft = add_decl->decl_spec.type->u.func_type;
  /* Add's param list: [this, T item].  Skip 'this' to reach the element param. */
  node_t add_param = NL_HEAD (add_ft->param_list->u.ops);
  if (add_param != NULL) add_param = NL_NEXT (add_param); /* skip this */
  if (add_param == NULL) {
    error (c2m_ctx, pos, "dict->collection bind: '%s' Add has no element parameter", cname != NULL ? cname : "<class>");
    return new_op (NULL, MIR_new_int_op (ctx, 0));
  }
  struct decl_spec *el_ds = get_param_decl_spec (add_param);
  struct type *el_type = el_ds->type;

  /* Find the default ctor (zero user params).  List<T> always has one; a
     user collection without a default ctor is a Phase-2 error (we can't
     construct it). */
  node_t ctor_def = NULL;
  if (cname != NULL) {
    char ctor_name[320];
    symbol_t ctor_sym;
    snprintf (ctor_name, sizeof (ctor_name), "__ctor_%s", cname);
    node_t ctor_id = build_id (c2m_ctx, ctor_name, pos);
    if (find_overload_sym (c2m_ctx, ctor_id, class_tag, &ctor_sym)) {
      for (size_t ci = 0; ci < VARR_LENGTH (node_t, ctor_sym.defs); ci++) {
        node_t cand = VARR_GET (node_t, ctor_sym.defs, ci);
        decl_t cd;
        struct func_type *cft;
        node_t cp;
        if (cand == NULL || cand->code != N_FUNC_DEF) continue;
        cd = cand->attr;
        if (cd == NULL || cd->decl_spec.type == NULL || cd->decl_spec.type->mode != TM_FUNC) continue;
        if (cd->decl_spec.static_p) continue;
        cft = cd->decl_spec.type->u.func_type;
        if (cft->class_scope != class_tag) continue;
        cp = NL_HEAD (cft->param_list->u.ops);
        if (cp != NULL) cp = NL_NEXT (cp); /* skip this */
        if (cp == NULL) { ctor_def = cand; break; } /* no user params */
      }
    }
  }
  if (ctor_def == NULL) {
    error (c2m_ctx, pos,
           "dict->collection bind: '%s' has no default constructor (Phase 2 needs a zero-arg ctor)",
           cname != NULL ? cname : "<class>");
    return new_op (NULL, MIR_new_int_op (ctx, 0));
  }

  /* 1. Allocate + zero-fill the collection object. */
  mir_size_t csize = type_size (c2m_ctx, cls_type);
  op_t obj = gen_heap_alloc (c2m_ctx, csize == 0 ? 1 : csize);
  if (csize > 0) gen_memset (c2m_ctx, 0, obj.mir_op.u.reg, csize);

  /* 2. Call the default ctor: obj.__ctor_C(). */
  gen_class_method_call (c2m_ctx, ctor_def, cls_ptr_type, obj, NULL, 0);

  /* 3. Loop over the dict array, unwrapping each element and calling Add. */
  op_t val_reg = force_reg (c2m_ctx, val_dv, MIR_T_I64);
  op_t n_reg = gen_dict_iter_count (c2m_ctx, val_reg.mir_op);
  n_reg = force_reg (c2m_ctx, n_reg, MIR_T_I64);
  op_t i_reg = get_new_temp (c2m_ctx, MIR_T_I64);
  emit2 (c2m_ctx, MIR_MOV, i_reg.mir_op, MIR_new_int_op (ctx, 0));
  MIR_label_t cond_label = MIR_new_label (ctx);
  MIR_label_t body_label = MIR_new_label (ctx);
  MIR_label_t end_label  = MIR_new_label (ctx);
  emit_label_insn_opt (c2m_ctx, cond_label);
  emit3 (c2m_ctx, MIR_BGE, MIR_new_label_op (ctx, end_label), i_reg.mir_op, n_reg.mir_op);
  emit1 (c2m_ctx, MIR_JMP, MIR_new_label_op (ctx, body_label));
  emit_label_insn_opt (c2m_ctx, body_label);
  {
    /* el_dv = dict_value_at(val_reg, i_reg) */
    op_t el_dv = gen_dict_value_at (c2m_ctx, val_reg.mir_op, i_reg.mir_op);
    el_dv = force_reg (c2m_ctx, el_dv, MIR_T_I64);
    /* Convert the element DictValue* to T, mirroring the field dispatch in
       gen_dict_bind_into. */
    op_t el_arg;
    if (el_type->mode == TM_CLASS || el_type->mode == TM_STRUCT) {
      /* Nested object: alloca + recurse gen_dict_bind_into, then pass by value.
         Add(T) for a by-value T takes the value, so build it in a temp and load. */
      mir_size_t esz = type_size (c2m_ctx, el_type);
      op_t el_addr = get_new_temp (c2m_ctx, MIR_T_I64);
      MIR_append_insn (ctx, curr_func,
                       MIR_new_insn (ctx, MIR_ALLOCA, el_addr.mir_op,
                                     MIR_new_int_op (ctx, (long) esz)));
      gen_memset (c2m_ctx, 0, el_addr.mir_op.u.reg, esz);
      gen_dict_bind_into (c2m_ctx, el_type, el_dv, el_addr, FALSE, pos);
      el_arg = new_op (NULL, MIR_new_mem_op (ctx, MIR_T_UNDEF, 0, el_addr.mir_op.u.reg, 0, 1));
    } else if (builtin_string_type_p (el_type)) {
      /* String element: take a private copy so the collection owns it. */
      op_t unwrapped = gen_dict_unwrap (c2m_ctx, el_dv);
      string_ensure_imports (c2m_ctx);
      MIR_op_t own_arg = force_reg (c2m_ctx, unwrapped, MIR_T_I64).mir_op;
      el_arg = gen_rt_call (c2m_ctx, str_own_proto, str_own_item, 1, &own_arg);
      el_arg = force_reg (c2m_ctx, el_arg, MIR_T_I64);
    } else if (el_type->mode == TM_PTR && el_type->u.ptr_type != NULL
               && el_type->u.ptr_type->mode == TM_CLASS) {
      /* Pointer-to-class element (T = C*), e.g. List<User*>: allocate + zero
         the pointee, recurse gen_dict_bind_into to fill its fields from the
         nested dict object, and pass the real pointer to Add(T).  This must
         be checked before the generic scalar_type_p branch below, since
         scalar_type_p() is true for every TM_PTR — falling through there
         would "unwrap" a nested-object DictValue* as if it held a raw scalar
         payload, handing Add() a bogus pointer (the historical bug: Count()
         came back right because Add() was still called N times, but each
         element was garbage and crashed on first use). The collection owns
         the allocation; deleting it must delete each element (matches
         List<T*> semantics for owned pointer elements).

         SIBLING: gen_dict_bind_into's own TM_PTR/TM_CLASS branch (pointer-to-
         collection *field*, not element) has the same ordering requirement
         relative to its own scalar_type_p branch, for the same reason. If you
         add a new pointer-to-class special case to one of these two
         functions' dispatch chains, add the matching case to the other and
         keep it ahead of scalar_type_p there too. */
      struct type *el_cls_type = el_type->u.ptr_type;
      mir_size_t el_csize = type_size (c2m_ctx, el_cls_type);
      op_t el_obj = gen_heap_alloc (c2m_ctx, el_csize == 0 ? 1 : el_csize);
      if (el_csize > 0) gen_memset (c2m_ctx, 0, el_obj.mir_op.u.reg, el_csize);
      gen_dict_bind_into (c2m_ctx, el_cls_type, el_dv, el_obj, lenient, pos);
      el_arg = el_obj;
    } else if (floating_type_p (el_type)) {
      /* Floating-point element (e.g. List<double>): the union payload at offset
         8 holds the value's raw bits (dict_create_number stores a `double`), so
         read it back with the element's float type directly rather than loading
         an I64 and emitting an invalid DMOV from an integer register. */
      op_t elp = force_reg (c2m_ctx, el_dv, MIR_T_I64);
      MIR_type_t el_mir_t = get_mir_type (c2m_ctx, el_type);
      MIR_type_t load_t = el_mir_t == MIR_T_LD ? MIR_T_D : el_mir_t;
      op_t fval = get_new_temp (c2m_ctx, load_t);
      emit2 (c2m_ctx, tp_mov (load_t), fval.mir_op,
             MIR_new_mem_op (ctx, load_t, 8, elp.mir_op.u.reg, 0, 1));
      el_arg = (el_mir_t == MIR_T_LD) ? cast (c2m_ctx, fval, el_mir_t, TRUE) : fval;
    } else if (scalar_type_p (el_type) || string_type_p (el_type)) {
      /* Scalar / pointer element: unwrap the union payload. */
      op_t unwrapped = gen_dict_unwrap (c2m_ctx, el_dv);
      /* Cast the I64 payload to the element's MIR type if needed. */
      MIR_type_t el_mir_t = get_mir_type (c2m_ctx, el_type);
      if (el_mir_t != MIR_T_I64 && el_mir_t != MIR_T_UNDEF) {
        op_t src = force_reg (c2m_ctx, unwrapped, MIR_T_I64);
        op_t dst = get_new_temp (c2m_ctx, el_mir_t);
        emit2 (c2m_ctx, tp_mov (el_mir_t), dst.mir_op, src.mir_op);
        el_arg = dst;
      } else {
        el_arg = force_reg (c2m_ctx, unwrapped, MIR_T_I64);
      }
    } else {
      /* Nothing above matched (e.g. a pointer-to-non-class element type, or
         some other exotic T): fail loudly at compile time instead of handing
         Add() a bogus unwrapped value that corrupts the collection at
         runtime. */
      error (c2m_ctx, pos,
             "dict->collection bind: unsupported element type for '%s' "
             "(Phase 3 supports scalars, String, nested class/struct by "
             "value, and pointer-to-class elements)",
             cname != NULL ? cname : "<class>");
      el_arg = new_op (NULL, MIR_new_int_op (ctx, 0));
    }
    gen_class_method_call (c2m_ctx, add_def, cls_ptr_type, obj, &el_arg, 1);
  }
  /* i++ and loop back to cond. */
  emit3 (c2m_ctx, MIR_ADD, i_reg.mir_op, i_reg.mir_op, MIR_new_int_op (ctx, 1));
  emit1 (c2m_ctx, MIR_JMP, MIR_new_label_op (ctx, cond_label));
  emit_label_insn_opt (c2m_ctx, end_label);

  return obj;
}

/* Walk CLS_TYPE's by-value members, reading each one from the source dict
   pointer SRC_DV_OP (a DictValue* in an I64 register) and writing it into the
   destination object whose base address is DST_ADDR_OP (also an I64 register).

   For each member m of class T:
     val_dv = dict_object_get(src, "m");
     if (val_dv == NULL) {
       lenient: continue;       // leave at zero-initialized default
       strict : throw KeyException("missing field 'm' in T");
     }
     dispatch on the *declared* member type:
       - scalar / string / pointer  ->  unwrap union payload, store into slot
       - nested class               ->  recurse with the same lenient flag
       - everything else            ->  compile error (Phase 1 unsupported)

   POS is used only for the error message on unsupported field types. */
static void gen_dict_bind_into (c2m_ctx_t c2m_ctx, struct type *cls_type,
                                op_t src_dv_op, op_t dst_addr_op,
                                int lenient, pos_t pos) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_context_t ctx = c2m_ctx->ctx;

  /* Both TM_CLASS and TM_STRUCT share the same `tag_type` shape and per-field
     N_MEMBER layout, so the walk is identical for both.  TM_UNION is rejected
     in the checker (no sensible mapping from a dict object). */
  assert (cls_type != NULL
          && (cls_type->mode == TM_CLASS || cls_type->mode == TM_STRUCT));
  const char *cls_name = (cls_type->mode == TM_CLASS)
                           ? class_type_name (cls_type) : NULL;
  if (cls_name == NULL) {
    /* Plain struct: the tag_type's first child is the N_ID for the tag name. */
    node_t tag_id = cls_type->u.tag_type ? NL_HEAD (cls_type->u.tag_type->u.ops) : NULL;
    cls_name = (tag_id != NULL && tag_id->code == N_ID) ? tag_id->u.s.s
             : (cls_type->mode == TM_STRUCT) ? "<struct>" : "<class>";
  }

  node_t class_node = cls_type->u.tag_type;
  node_t members = (class_node != NULL && class_node->u.ops.head != NULL)
                     ? NL_EL (class_node->u.ops, 1) : NULL;
  if (members == NULL || members->code != N_LIST) return;

  /* Force the source DictValue* into a fresh register once — dict_object_get
     takes it as the first arg every iteration. */
  src_dv_op = force_reg (c2m_ctx, src_dv_op, MIR_T_I64);
  dst_addr_op = force_reg (c2m_ctx, dst_addr_op, MIR_T_I64);

  for (node_t m = NL_HEAD (members->u.ops); m != NULL; m = NL_NEXT (m)) {
    if (m->code != N_MEMBER) continue;
    decl_t mdecl = m->attr;
    if (mdecl == NULL) continue;
    struct type *mtype = mdecl->decl_spec.type;
    if (mtype == NULL) continue;
    /* Skip method members (function-typed) — they are part of the class but
       are not data slots a dict could provide. */
    if (mtype->mode == TM_FUNC) continue;
    /* Static class members live outside the instance and aren't bind targets. */
    if (mdecl->decl_spec.static_p) continue;

    /* Extract the member name from the declarator: N_DECL(N_ID, ...) */
    node_t declarator = NL_EL (m->u.ops, 1);
    node_t mid = declarator ? NL_HEAD (declarator->u.ops) : NULL;
    if (mid == NULL || mid->code != N_ID) continue;
    const char *mname = mid->u.s.s;
    mir_size_t moff = mdecl->offset;

    /* field_addr = dst_addr + moff */
    op_t field_addr = get_new_temp (c2m_ctx, MIR_T_I64);
    emit3 (c2m_ctx, MIR_ADD, field_addr.mir_op, dst_addr_op.mir_op,
           MIR_new_int_op (ctx, (long) moff));

    /* val_dv = dict_object_get(src_dv, "mname") */
    MIR_op_t key_op = gen_dict_key_op (c2m_ctx, mname, strlen (mname) + 1);
    op_t val_dv = gen_dict_object_get (c2m_ctx, src_dv_op.mir_op, key_op);
    val_dv = force_reg (c2m_ctx, val_dv, MIR_T_I64);

    /* Branch on val_dv == NULL */
    MIR_label_t after_field = MIR_new_label (ctx);
    MIR_label_t present_label = MIR_new_label (ctx);
    emit3 (c2m_ctx, MIR_BNE, MIR_new_label_op (ctx, present_label),
           val_dv.mir_op, MIR_new_int_op (ctx, 0));
    /* --- missing-key path --- */
    if (lenient) {
      /* leave the slot at its zero-init default and move on */
      emit1 (c2m_ctx, MIR_JMP, MIR_new_label_op (ctx, after_field));
    } else {
      gen_dict_bind_throw_key_exception (c2m_ctx, cls_name, mname);
      /* unreachable: throw does not return.  Fall through is fine. */
      emit1 (c2m_ctx, MIR_JMP, MIR_new_label_op (ctx, after_field));
    }
    /* --- present-key path --- */
    emit_label_insn_opt (c2m_ctx, present_label);

    if (mtype->mode == TM_CLASS || mtype->mode == TM_STRUCT) {
      /* Nested class or struct: recurse with field_addr as the new
         destination base.  Identical layout walk regardless of kind. */
      gen_dict_bind_into (c2m_ctx, mtype, val_dv, field_addr, lenient, pos);
    } else if (builtin_string_type_p (mtype) && cls_type->mode == TM_CLASS) {
      /* Value-semantic String field (Option D): a `String` stored in a CLASS
         field is owned by the object.  Gated on builtin_string_type_p (the
         real `String`, not any char*) so it matches exactly what the
         destructor's gen_class_string_members_drop frees — a plain char*
         field falls through to the borrow path below and is never dropped.  The dict's string payload lives in the
         dict's arena, so we must NOT store that borrowed pointer — once the
         dict is freed the field would dangle (and the destructor's
         c2m_str_drop would free arena memory).  Instead take a private heap
         copy via c2m_str_own, exactly like `this.s = ...` does, so the bound
         object owns its strings and `delete obj` frees them.  The destination
         was zero-initialized before the walk, so there is no old buffer to
         drop.  This is what makes typed JSON binding "just work" C#-style:
         the source dict can be deleted right after the bind. */
      op_t unwrapped = gen_dict_unwrap (c2m_ctx, val_dv);
      string_ensure_imports (c2m_ctx);
      MIR_op_t own_arg = force_reg (c2m_ctx, unwrapped, MIR_T_I64).mir_op;
      op_t owned = gen_rt_call (c2m_ctx, str_own_proto, str_own_item, 1, &own_arg);
      owned = force_reg (c2m_ctx, owned, MIR_T_I64);
      MIR_type_t mir_t = get_mir_type (c2m_ctx, mtype);
      MIR_alias_t alias = get_type_alias (c2m_ctx, mtype);
      emit2 (c2m_ctx, tp_mov (mir_t),
             MIR_new_alias_mem_op (ctx, mir_t, 0, field_addr.mir_op.u.reg, 0, 1, alias, 0),
             owned.mir_op);
    } else if (mtype->mode == TM_PTR && mtype->u.ptr_type != NULL
               && mtype->u.ptr_type->mode == TM_CLASS) {
      /* Phase 2: pointer-to-collection field (List<T>* / Set<T>* / any class
         with a default ctor + Add(T)).  Build a heap collection from the dict
         array and store its pointer into the field.  The bound object owns the
         new collection (its destructor must `delete` it, as List<T>::~List does).

         Like the pointer-to-class branch below, this must stay ordered before
         the generic scalar_type_p branch (true for every TM_PTR) or a
         collection field would get "unwrapped" as a raw scalar instead of
         recursively bound.

         SIBLING: gen_dict_bind_collection_field has the matching pointer-to-
         class *element* case (as opposed to this function's pointer-to-
         collection *field* case), with the identical ordering requirement —
         see the comment there for the bug this ordering prevents. Keep new
         pointer-to-class dispatch cases in both functions in sync. */
      op_t coll = gen_dict_bind_collection_field (c2m_ctx, mtype, val_dv, lenient, pos);
      coll = force_reg (c2m_ctx, coll, MIR_T_I64);
      MIR_type_t mir_t = get_mir_type (c2m_ctx, mtype);
      MIR_alias_t alias = get_type_alias (c2m_ctx, mtype);
      emit2 (c2m_ctx, tp_mov (mir_t),
             MIR_new_alias_mem_op (ctx, mir_t, 0, field_addr.mir_op.u.reg, 0, 1, alias, 0),
             coll.mir_op);
    } else if (floating_type_p (mtype)) {
      /* Floating-point leaf (float/double/long double).  The union payload at
         offset 8 holds the value's raw bits (dict_create_number stores a
         `double` there), so read it back with the field's own float type
         instead of loading an I64 and emitting an invalid DMOV from an integer
         register.  A narrower `float` field reads the low 4 bytes; long double
         is stored as a double and widened on load. */
      MIR_type_t mir_t = get_mir_type (c2m_ctx, mtype);
      MIR_type_t load_t = mir_t == MIR_T_LD ? MIR_T_D : mir_t;
      MIR_alias_t alias = get_type_alias (c2m_ctx, mtype);
      op_t fval = get_new_temp (c2m_ctx, load_t);
      emit2 (c2m_ctx, tp_mov (load_t), fval.mir_op,
             MIR_new_mem_op (ctx, load_t, 8, val_dv.mir_op.u.reg, 0, 1));
      if (mir_t == MIR_T_LD) fval = cast (c2m_ctx, fval, mir_t, TRUE);
      emit2 (c2m_ctx, tp_mov (mir_t),
             MIR_new_alias_mem_op (ctx, mir_t, 0, field_addr.mir_op.u.reg, 0, 1, alias, 0),
             fval.mir_op);
    } else if (scalar_type_p (mtype) || string_type_p (mtype)) {
      /* Unwrap the union payload (offset 8 of DictValue) and store into the
         slot.  Same path as `(T)d.x` would take for a scalar leaf.  Struct
         String fields land here too: structs have no destructor, so they keep
         borrowing the dict-arena pointer (the dict must outlive them). */
      op_t unwrapped = gen_dict_unwrap (c2m_ctx, val_dv);
      MIR_type_t mir_t = get_mir_type (c2m_ctx, mtype);
      MIR_alias_t alias = get_type_alias (c2m_ctx, mtype);
      emit2 (c2m_ctx, tp_mov (mir_t),
             MIR_new_alias_mem_op (ctx, mir_t, 0, field_addr.mir_op.u.reg, 0, 1, alias, 0),
             unwrapped.mir_op);
    } else {
      error (c2m_ctx, pos,
             "dict->aggregate bind: unsupported field type for '%s.%s' "
             "(Phase 2 supports scalars, String, nested class/struct, and "
             "pointer-to-collection fields)",
             cls_name, mname);
    }
    emit_label_insn_opt (c2m_ctx, after_field);
  }
}

/* Create a named (uniquely-named) string-data item holding a dict key, returning
   a ref operand to it.  The data item is given a real name and moved to the
   module start so that it survives binary (.bmir) serialization: an anonymous
   (NULL-named) data item referenced by a ref operand would be written with an
   empty name and fail to resolve on read-back. */
static MIR_op_t gen_dict_key_op (c2m_ctx_t c2m_ctx, const char *key_str, size_t len) {
  MIR_context_t ctx = c2m_ctx->ctx;
  MIR_module_t module = DLIST_TAIL (MIR_module_t, *MIR_get_module_list (ctx));
  char buff[50];

  _MIR_get_temp_item_name (ctx, module, buff, sizeof (buff));
  MIR_item_t str_item = MIR_new_string_data (ctx, buff, (MIR_str_t){len, key_str});
  move_item_to_module_start (module, str_item);
  return MIR_new_ref_op (ctx, str_item);
}

/* Forward declarations (mutual recursion: object keys → nested arrays/objects) */
static op_t gen_dict_value_for_init (c2m_ctx_t c2m_ctx, node_t value);
static op_t gen_dict_from_init_list (c2m_ctx_t c2m_ctx, node_t value);
static void gen_dict_init_list (c2m_ctx_t c2m_ctx, MIR_op_t obj_op, node_t initializer);

/* True if an N_LIST initializer should lower to DICT_ARRAY rather than DICT_OBJECT.
   Square-bracket literals (`[1,2]`, `[]`) are marked DICT_INIT_ARRAY_MARK.
   Unkeyed brace lists (`{1,2,3}`) are arrays; keyed brace (`{ "k": v }`) and
   empty nested `{}` are objects. */
static int dict_init_list_is_array_p (node_t value) {
  node_t inner_init, inner_des_list, inner_des;
  if (value == NULL || value->code != N_LIST) return 0;
  if (value->attr == DICT_INIT_ARRAY_MARK) return 1;
  inner_init = NL_HEAD (value->u.ops);
  if (inner_init == NULL) return 0; /* empty `{}` → empty object */
  if (inner_init->code != N_INIT) return 0;
  inner_des_list = NL_HEAD (inner_init->u.ops);
  inner_des = inner_des_list != NULL ? NL_HEAD (inner_des_list->u.ops) : NULL;
  return (inner_des == NULL || inner_des->code != N_FIELD_ID);
}

/* Materialise a DICT_ARRAY or DICT_OBJECT from an N_LIST of N_INIT children. */
static op_t gen_dict_from_init_list (c2m_ctx_t c2m_ctx, node_t value) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;

  if (value == NULL || value->code != N_LIST) return gen_dict_create_object (c2m_ctx);

  if (dict_init_list_is_array_p (value)) {
    op_t arr = gen_rt_call (c2m_ctx, dict_create_array_proto, dict_create_array_item, 0, NULL);
    for (node_t elem_init = NL_HEAD (value->u.ops); elem_init != NULL; elem_init = NL_NEXT (elem_init)) {
      node_t elem_des_list, elem_value;
      op_t elem_op;
      if (elem_init->code != N_INIT) continue;
      elem_des_list = NL_HEAD (elem_init->u.ops);
      elem_value = NL_NEXT (elem_des_list);
      if (elem_value == NULL) continue;
      if (elem_value->code == N_LIST)
        elem_op = gen_dict_from_init_list (c2m_ctx, elem_value);
      else
        elem_op = gen_dict_value_for_init (c2m_ctx, elem_value);
      {
        MIR_op_t append_args[2] = {arr.mir_op, elem_op.mir_op};
        gen_rt_call_void (c2m_ctx, dict_array_append_proto, dict_array_append_item, 2, append_args);
      }
    }
    return arr;
  }

  {
    op_t child = gen_dict_create_object (c2m_ctx);
    gen_dict_init_list (c2m_ctx, child.mir_op, value);
    return child;
  }
}

/* Recursively generate dict initializer code.
   obj_op is the MIR register holding the parent dict object pointer.
   initializer is the N_LIST node containing N_INIT children. */
static void gen_dict_init_list (c2m_ctx_t c2m_ctx, MIR_op_t obj_op, node_t initializer) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;

  if (initializer->code != N_LIST) return;
  for (node_t init = NL_HEAD (initializer->u.ops); init != NULL; init = NL_NEXT (init)) {
    if (init->code != N_INIT) continue;
    node_t des_list = NL_HEAD (init->u.ops);
    node_t value = NL_NEXT (des_list);
    node_t des = NL_HEAD (des_list->u.ops);
    if (des == NULL || des->code != N_FIELD_ID) continue;
    node_t key_node = NL_HEAD (des->u.ops);
    /* key_node is N_STR (string key like "timeout") or N_ID (.field style) */
    const char *key_str = key_node->u.s.s;
    MIR_op_t key_op = gen_dict_key_op (c2m_ctx, key_str, strlen (key_str) + 1);

    if (value != NULL && value->code == N_LIST) {
      /* Nested object `{ "k": v }`, array `{1,2}` / `[1,2]`, or empty `{}` / `[]`. */
      op_t child = gen_dict_from_init_list (c2m_ctx, value);
      gen_dict_object_set (c2m_ctx, obj_op, key_op, child.mir_op);
    } else {
      /* scalar / expression value */
      op_t wrapped = gen_dict_value_for_init (c2m_ctx, value);
      gen_dict_object_set (c2m_ctx, obj_op, key_op, wrapped.mir_op);
    }
  }
}

/* Wrap a single initializer value as a DictValue*.  Used by gen_dict_init_list
   for both object key values and array elements. */
static op_t gen_dict_value_for_init (c2m_ctx_t c2m_ctx, node_t value) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_context_t ctx = c2m_ctx->ctx;
  struct expr *ve = value->attr;
  if (ve != NULL && ve->const_p && ve->type->mode == TM_BASIC
      && ve->type->u.basic_type == TP_BOOL) {
    return gen_dict_create_bool (c2m_ctx, MIR_new_int_op (ctx, ve->c.i_val));
  } else if (ve != NULL && ve->const_p && integer_type_p (ve->type)) {
    return gen_dict_create_int64 (c2m_ctx, MIR_new_int_op (ctx, ve->c.i_val));
  } else if (ve != NULL && ve->const_p && floating_type_p (ve->type)) {
    return gen_dict_create_number (c2m_ctx, MIR_new_double_op (ctx, ve->c.d_val));
  } else if (value->code == N_STR) {
    MIR_op_t si_op = gen_dict_key_op (c2m_ctx, value->u.s.s, value->u.s.len);
    return gen_dict_create_string (c2m_ctx, si_op);
  } else if (ve != NULL && ve->type->mode == TM_DICT) {
    /* dict-valued expression: store an owned deep copy, not a borrowed
       pointer (avoids aliasing / double-free with the source dict). */
    op_t v = val_gen (c2m_ctx, value);
    return gen_dict_value_copy (c2m_ctx, v.mir_op);
  } else if (ve != NULL && ve->type->mode == TM_PTR) {
    /* Pointer to DictValue* (e.g. result of List<dict>::ToDict or any
       expression returning struct DictValue*).  Treat exactly like a
       TM_DICT value: deep-copy so the initializer owns the dict. */
    struct type *pt = ve->type->u.ptr_type;
    if (pt && pt->mode == TM_STRUCT && pt->u.tag_type
        && TAG_ID (pt->u.tag_type) != NULL
        && strcmp (TAG_ID (pt->u.tag_type)->u.s.s, "DictValue") == 0) {
      op_t v = val_gen (c2m_ctx, value);
      return gen_dict_value_copy (c2m_ctx, v.mir_op);
    }
    /* fall through for other pointers (treated as int64 below) */
  } else if (ve != NULL && ve->type->mode == TM_BASIC
             && ve->type->u.basic_type == TP_BOOL) {
    /* runtime _Bool expression: wrap as JSON boolean */
    op_t v = val_gen (c2m_ctx, value);
    return gen_dict_create_bool (c2m_ctx, v.mir_op);
  } else if (ve != NULL
             && (builtin_string_type_p (ve->type)
                 || (ve->type->mode == TM_PTR && ve->type->u.ptr_type != NULL
                     && char_type_p (ve->type->u.ptr_type)))) {
    /* runtime String / char* value (e.g. p.symptomDescription): store as a
       JSON string.  dict_create_string copies the bytes, so the dict owns its
       own copy.  Without this, a String would fall through to the int64 branch
       below and the raw char* pointer would be stored as an integer. */
    op_t v = val_gen (c2m_ctx, value);
    return gen_dict_create_string (c2m_ctx, v.mir_op);
  }
  /* Fallthrough - runtime expression: evaluate then wrap as int64 */
  op_t v = val_gen (c2m_ctx, value);
  return gen_dict_create_int64 (c2m_ctx, v.mir_op);
}

/* Locate the named dict member (a member whose brace initializer is an N_LIST)
   of a class tag node, returning the matching N_MEMBER, or NULL. */
static node_t find_class_dict_member (node_t class_node, const char *member_name) {
  if (class_node == NULL) return NULL;
  node_t mlist = TAG_MEMBER_LIST (class_node);
  if (mlist == NULL || mlist->code != N_LIST) return NULL;
  for (node_t m = NL_HEAD (mlist->u.ops); m != NULL; m = NL_NEXT (m)) {
    if (m->code != N_MEMBER) continue;
    node_t mdecl = MEMBER_DECL (m);
    node_t mid = (mdecl != NULL && mdecl->code == N_DECL) ? DECL_ID (mdecl) : NULL;
    node_t minit = MEMBER_INIT (m);
    if (minit == NULL || minit->code != N_LIST) continue;
    if (member_name == NULL
        || (mid != NULL && mid->code == N_ID && strcmp (mid->u.s.s, member_name) == 0))
      return m;
  }
  return NULL;
}

/* Materialise a class's declarative dict member (ClassName.member) as a
   process-wide singleton.  A BSS slot holds the DictValue* and a generated
   __dict_init_* function (run from main) builds the object from the member's
   brace initializer.  Returns the BSS item, or NULL if there is no such member.
   The result is cached on the member's decl so repeated accesses reuse it. */
static MIR_item_t ensure_class_static_dict (c2m_ctx_t c2m_ctx, node_t class_node,
                                            const char *member_name) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_context_t ctx = c2m_ctx->ctx;
  node_t member = find_class_dict_member (class_node, member_name);

  if (member == NULL) return NULL;
  node_t init = MEMBER_INIT (member);
  decl_t mdec = member->attr;
  if (mdec != NULL && mdec->u.item != NULL) return mdec->u.item; /* already built */

  node_t cls_id = TAG_ID (class_node);
  const char *cls = (cls_id != NULL && cls_id->code == N_ID) ? cls_id->u.s.s : "anon";
  char gname[256];
  snprintf (gname, sizeof gname, "__class_%s_%s", cls, member_name);
  MIR_item_t bss = MIR_new_bss (ctx, gname, sizeof (void *));
  if (mdec != NULL) mdec->u.item = bss;

  {
    MIR_item_t saved_func = curr_func;
    int saved_reg_free_mark = reg_free_mark;
    char init_name[300];
    snprintf (init_name, sizeof init_name, "__dict_init_class_%s_%s", cls, member_name);
    reg_free_mark = 0;
    curr_func = MIR_new_func (ctx, init_name, 0, NULL, 0);
    if (dict_init_func_count < (int) (sizeof (dict_init_funcs) / sizeof (dict_init_funcs[0])))
      dict_init_funcs[dict_init_func_count++] = curr_func; /* main will call it */
    op_t obj = gen_dict_create_object (c2m_ctx);
    gen_dict_init_list (c2m_ctx, obj.mir_op, init);
    {
      op_t addr_temp = get_new_temp (c2m_ctx, MIR_T_I64);
      emit2 (c2m_ctx, MIR_MOV, addr_temp.mir_op, MIR_new_ref_op (ctx, bss));
      emit2 (c2m_ctx, MIR_MOV,
             MIR_new_mem_op (ctx, MIR_T_I64, 0, addr_temp.mir_op.u.reg, 0, 1), obj.mir_op);
    }
    emit_insn (c2m_ctx, MIR_new_ret_insn (ctx, 0));
    MIR_finish_func (ctx);
    HTAB_CLEAR (reg_var_t, reg_var_tab);
    curr_func = saved_func;
    reg_free_mark = saved_reg_free_mark;
  }
  return bss;
}

/* Given an op holding a DictValue*, load the 8-byte union payload (offset 8).
   The int64/double/string_value/object pointer members all overlap there, so a
   single load serves every scalar/string use of a dict value. */
static op_t gen_dict_unwrap (c2m_ctx_t c2m_ctx, op_t dop) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_context_t ctx = c2m_ctx->ctx;
  op_t ptr = force_reg (c2m_ctx, dop, MIR_T_I64);
  op_t res = get_new_temp (c2m_ctx, MIR_T_I64);
  /* NULL-guard: a missing dict key (dict_object_get -> NULL) or an explicit
     JSON null must not fault when its payload is read.  Yield 0 for a NULL
     DictValue* so `(int)d.absent` / `(long)d.absent` degrade to 0 rather than
     dereferencing offset 8 of NULL (SIGSEGV).  This is the lenient default the
     rest of the dict path already assumes (zero-fill on missing bind fields). */
  MIR_label_t done_label = MIR_new_label (ctx);
  emit2 (c2m_ctx, MIR_MOV, res.mir_op, MIR_new_int_op (ctx, 0));
  emit3 (c2m_ctx, MIR_BEQ, MIR_new_label_op (ctx, done_label), ptr.mir_op,
         MIR_new_int_op (ctx, 0));
  emit2 (c2m_ctx, MIR_MOV, res.mir_op,
         MIR_new_mem_op (ctx, MIR_T_I64, 8, ptr.mir_op.u.reg, 0, 1));
  emit_label_insn_opt (c2m_ctx, done_label);
  return res;
}

/* Read the DictType tag at offset 0 of a DictValue box WITHOUT unwrapping the
   union payload.  Backs the `d.type()` builtin: user code cannot read the tag
   via a cast (every dict->pointer cast unwraps to the payload at offset 8), so
   this exposes it directly as an int.  Values match include/dict.h's DictType
   enum (DICT_NULL=0 .. DICT_OBJECT=6). */
static op_t gen_dict_type_tag (c2m_ctx_t c2m_ctx, op_t dop) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_context_t ctx = c2m_ctx->ctx;
  op_t ptr = force_reg (c2m_ctx, dop, MIR_T_I64);
  op_t res = get_new_temp (c2m_ctx, MIR_T_I64);
  emit2 (c2m_ctx, MIR_MOV, res.mir_op,
         MIR_new_mem_op (ctx, MIR_T_I32, 0, ptr.mir_op.u.reg, 0, 1));
  return res;
}

/* When `src_node` evaluates to a dict value (a DictValue*) but the consuming
   context (`target`) is a non-dict type, unwrap the DictValue union payload
   (int64_value / string_value / number_value) so the right scalar is used.
   A dict-typed target keeps the box pointer for chaining / dict-to-dict copy.
   Returns the possibly-unwrapped operand. */
static op_t maybe_unwrap_dict_value (c2m_ctx_t c2m_ctx, op_t op, node_t src_node,
                                     struct type *target) {
  struct expr *se = src_node != NULL ? (struct expr *) src_node->attr : NULL;
  if (se != NULL && se->type != NULL && se->type->mode == TM_DICT && target != NULL
      && target->mode != TM_DICT)
    return gen_dict_unwrap (c2m_ctx, op);
  return op;
}

/* Import the UTF-8 String runtime helpers (cstring.h) into the current module,
   once per module.  All String values are char* (passed/returned as I64). */
static void string_ensure_imports (c2m_ctx_t c2m_ctx) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_context_t ctx = c2m_ctx->ctx;
  MIR_module_t module = curr_func->module;

  if (str_length_item != NULL) return; /* already imported */

  /* char* / size_t / int64_t all pass and return as I64.  See
     src/mir-aot-runtime.c and the String arena runtime for definitions. */
  rt_import (c2m_ctx, "c2m_str_length", NULL, &str_length_proto, &str_length_item, 1, "s");
  rt_import (c2m_ctx, "c2m_str_empty", NULL, &str_empty_proto, &str_empty_item, 1, "s");
  rt_import (c2m_ctx, "c2m_str_substr", NULL, &str_substr_proto, &str_substr_item, 1,
             "s pos len");
  rt_import (c2m_ctx, "c2m_str_find", NULL, &str_find_proto, &str_find_item, 1, "s needle");
  rt_import (c2m_ctx, "c2m_str_replace", NULL, &str_replace_proto, &str_replace_item, 1,
             "s pos len repl");
  rt_import (c2m_ctx, "c2m_str_replace_all", NULL, &str_replace_all_proto, &str_replace_all_item,
             1, "s needle repl");
  rt_import (c2m_ctx, "c2m_str_upper", NULL, &str_upper_proto, &str_upper_item, 1, "s");
  rt_import (c2m_ctx, "c2m_str_lower", NULL, &str_lower_proto, &str_lower_item, 1, "s");
  rt_import (c2m_ctx, "c2m_str_starts_with", NULL, &str_starts_with_proto, &str_starts_with_item,
             1, "s prefix");
  rt_import (c2m_ctx, "c2m_str_ends_with", NULL, &str_ends_with_proto, &str_ends_with_item, 1,
             "s suffix");
  rt_import (c2m_ctx, "c2m_str_contains", NULL, &str_contains_proto, &str_contains_item, 1,
             "s needle");
  rt_import (c2m_ctx, "c2m_str_trim", NULL, &str_trim_proto, &str_trim_item, 1, "s");
  /* c2m_str_split returns List<String>*; c2m_str_join takes it as opaque ptr. */
  rt_import (c2m_ctx, "c2m_str_split", NULL, &str_split_proto, &str_split_item, 1, "s delim");
  rt_import (c2m_ctx, "c2m_str_join", NULL, &str_join_proto, &str_join_item, 1, "list delim");
  rt_import (c2m_ctx, "c2m_str_equals", NULL, &str_equals_proto, &str_equals_item, 1, "s other");
  rt_import (c2m_ctx, "c2m_str_detach", NULL, &str_detach_proto, &str_detach_item, 1, "s");
  /* c2m_str_own: fresh untracked owned heap copy */
  rt_import (c2m_ctx, "c2m_str_own", NULL, &str_own_proto, &str_own_item, 1, "s");
  /* c2m_str_drop: free an object-owned String field */
  rt_import (c2m_ctx, "c2m_str_drop", NULL, &str_drop_proto, &str_drop_item, 0, "s");
  rt_import (c2m_ctx, "c2m_str_attach", NULL, &str_attach_proto, &str_attach_item, 1, "s");
  rt_import (c2m_ctx, "c2m_str_checkpoint", NULL, &str_checkpoint_proto, &str_checkpoint_item, 1,
             "");
  rt_import (c2m_ctx, "c2m_str_release_to", NULL, &str_release_to_proto, &str_release_to_item, 0,
             "mark");
  /* c2m_str_release_keeping returns the kept pointer; the result is ignored. */
  rt_import (c2m_ctx, "c2m_str_release_keeping", NULL, &str_release_keeping_proto,
             &str_release_keeping_item, 0, "mark keep");
  rt_import (c2m_ctx, "c2m_str_copy", NULL, &str_copy_proto, &str_copy_item, 1, "p len");
  rt_import (c2m_ctx, "c2m_str_concat", NULL, &str_concat_proto, &str_concat_item, 1, "a b");

  /* char *c2m_str_from_int/uint/bool/char(int64_t v) — one shared proto. */
  {
    MIR_type_t i64_t = MIR_T_I64;
    MIR_var_t v;

    v.name = "v";
    v.type = MIR_T_I64;
    str_from_i64_proto = MIR_new_proto_arr (ctx, "__c2m_str_from_i64_p", 1, &i64_t, 1, &v);
    move_item_to_module_start (module, str_from_i64_proto);
    str_from_int_item = rt_import_item (c2m_ctx, "c2m_str_from_int");
    str_from_uint_item = rt_import_item (c2m_ctx, "c2m_str_from_uint");
    str_from_bool_item = rt_import_item (c2m_ctx, "c2m_str_from_bool");
    str_from_char_item = rt_import_item (c2m_ctx, "c2m_str_from_char");
  }

  /* char *c2m_str_from_double(double v) */
  rt_import (c2m_ctx, "c2m_str_from_double", NULL, &str_from_double_proto, &str_from_double_item,
             1, ".v");
}

/* Emit: res = c2m_str_concat(a, b).  Both operands are char* (I64). */
static op_t gen_str_concat_call (c2m_ctx_t c2m_ctx, MIR_op_t a, MIR_op_t b) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_op_t ops[2] = {a, b};
  string_ensure_imports (c2m_ctx);
  return gen_rt_call (c2m_ctx, str_concat_proto, str_concat_item, 2, ops);
}

/* Emit: res = <one-arg String conversion helper>(val).  `proto`/`item` select
   the helper (all int-family helpers share str_from_i64_proto; the double
   helper uses str_from_double_proto). */
static op_t gen_str_from_call (c2m_ctx_t c2m_ctx, MIR_item_t proto, MIR_item_t item,
                               MIR_op_t val) {
  return gen_rt_call (c2m_ctx, proto, item, 1, &val);
}

/* Generate a single operand of a String `+` concatenation as a char* (I64).
   String-valued operands are produced directly; arithmetic (basic) operands are
   auto-cast to their textual representation via the c2m_str_from_* runtime. */
static op_t gen_string_concat_operand (c2m_ctx_t c2m_ctx, node_t op) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  struct expr *e = op->attr;
  struct type *t = e == NULL ? NULL : e->type;
  op_t v;

  string_ensure_imports (c2m_ctx);
  if (str_concat_string_operand_p (t, op) || (t != NULL && t->mode == TM_PTR)
      || (t != NULL && t->mode == TM_ARR)) {
    /* Already a string/char* value.  val_gen yields the pointer value for a
       String/char* variable, the data address for a string literal, and the
       decayed address for an array; force_reg lands it in a register. */
    return force_reg (c2m_ctx, val_gen (c2m_ctx, op), MIR_T_I64);
  }
  /* Arithmetic basic type — auto-cast to a String. */
  if (floating_type_p (t)) {
    v = promote (c2m_ctx, val_gen (c2m_ctx, op), MIR_T_D, FALSE);
    return gen_str_from_call (c2m_ctx, str_from_double_proto, str_from_double_item, v.mir_op);
  }
  if (t != NULL && t->mode == TM_BASIC && t->u.basic_type == TP_BOOL) {
    v = promote (c2m_ctx, val_gen (c2m_ctx, op), MIR_T_I64, FALSE);
    return gen_str_from_call (c2m_ctx, str_from_i64_proto, str_from_bool_item, v.mir_op);
  }
  if (char_type_p (t)) {
    v = promote (c2m_ctx, val_gen (c2m_ctx, op), MIR_T_I64, FALSE);
    return gen_str_from_call (c2m_ctx, str_from_i64_proto, str_from_char_item, v.mir_op);
  }
  v = promote (c2m_ctx, val_gen (c2m_ctx, op), MIR_T_I64, FALSE);
  if (t != NULL && integer_type_p (t) && !signed_integer_type_p (t))
    return gen_str_from_call (c2m_ctx, str_from_i64_proto, str_from_uint_item, v.mir_op);
  return gen_str_from_call (c2m_ctx, str_from_i64_proto, str_from_int_item, v.mir_op);
}

/* Lazy import of a pure I64->I64 string runtime helper by name (for SM_EXT
   methods registered via [[builtin_method]] without a dedicated gen_ctx slot). */
static op_t gen_string_call_by_rt_name (c2m_ctx_t c2m_ctx, const char *rt_name,
                                        MIR_op_t *vals, int n) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_context_t ctx = c2m_ctx->ctx;
  MIR_module_t module = curr_func->module;
  MIR_type_t res_t = MIR_T_I64;
  MIR_var_t vars[5];
  char pname[128];
  MIR_item_t proto, item;
  int i;

  assert (rt_name != NULL && n >= 0 && n <= 5);
  for (i = 0; i < n; i++) {
    vars[i].name = "a";
    vars[i].type = MIR_T_I64;
  }
  snprintf (pname, sizeof (pname), "__bm_%s_p", rt_name);
  proto = MIR_new_proto_arr (ctx, pname, 1, &res_t, (size_t) n, vars);
  item = MIR_new_import (ctx, rt_name);
  move_item_to_module_start (module, proto);
  move_item_to_module_start (module, item);
  return gen_rt_call (c2m_ctx, proto, item, (size_t) n, vals);
}

/* Emit a call to the String runtime helper for method `sm` with `n` value
   operands.  ext_rt_name is required when sm == SM_EXT (header-registered). */
static op_t gen_string_call_named (c2m_ctx_t c2m_ctx, enum str_method sm,
                                   const char *ext_rt_name, MIR_op_t *vals, int n) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_item_t proto = NULL, item = NULL;

  if (sm == SM_EXT) {
    assert (ext_rt_name != NULL);
    return gen_string_call_by_rt_name (c2m_ctx, ext_rt_name, vals, n);
  }

  string_ensure_imports (c2m_ctx); /* must run before reading the proto/item refs */
  switch (sm) {
  case SM_LENGTH:      proto = str_length_proto;      item = str_length_item;      break;
  case SM_EMPTY:       proto = str_empty_proto;       item = str_empty_item;       break;
  case SM_SUBSTR:      proto = str_substr_proto;      item = str_substr_item;      break;
  case SM_FIND:        proto = str_find_proto;        item = str_find_item;        break;
  case SM_REPLACE:     proto = str_replace_proto;     item = str_replace_item;     break;
  case SM_REPLACE_ALL: proto = str_replace_all_proto;  item = str_replace_all_item;  break;
  case SM_UPPER:       proto = str_upper_proto;       item = str_upper_item;       break;
  case SM_LOWER:       proto = str_lower_proto;       item = str_lower_item;       break;
  case SM_DETACH:      proto = str_detach_proto;      item = str_detach_item;      break;
  case SM_ATTACH:      proto = str_attach_proto;      item = str_attach_item;      break;
  case SM_COPY:        proto = str_copy_proto;        item = str_copy_item;        break;
  case SM_STARTS_WITH: proto = str_starts_with_proto; item = str_starts_with_item; break;
  case SM_ENDS_WITH:   proto = str_ends_with_proto;   item = str_ends_with_item;   break;
  case SM_CONTAINS:    proto = str_contains_proto;    item = str_contains_item;    break;
  case SM_TRIM:        proto = str_trim_proto;        item = str_trim_item;        break;
  case SM_SPLIT:       proto = str_split_proto;       item = str_split_item;       break;
  case SM_JOIN:        proto = str_join_proto;        item = str_join_item;        break;
  case SM_EQUALS:      proto = str_equals_proto;      item = str_equals_item;       break;
  default:             assert (0);                                                 break;
  }
  return gen_rt_call (c2m_ctx, proto, item, (size_t) n, vals);
}

static op_t gen_string_call (c2m_ctx_t c2m_ctx, enum str_method sm, MIR_op_t *vals, int n) {
  return gen_string_call_named (c2m_ctx, sm, NULL, vals, n);
}

/* ── Value-semantic String fields (Option D) ──────────────────────────────
   A `String` stored in a CLASS field is owned by the object: assignment
   copies into a private heap buffer (c2m_str_own) and the destructor frees it
   (c2m_str_drop).  This gives C#/Java-like "strings just live with the object"
   semantics without a GC, by keeping every owned field pointing at either NULL
   or a freeable heap buffer.

   class_string_member_store_p: TRUE when `assign` writes a `String` member of
   a class object (`this.s = ...`, `obj.s = ...`, `obj->s = ...`).  Struct/dict
   members and non-String fields are excluded — only classes participate. */
static int class_string_member_store_p (node_t assign) {
  node_t lhs, parent;
  struct expr *pe, *fe;
  struct type *pt;
  if (assign == NULL || assign->code != N_ASSIGN) return 0;
  lhs = NL_HEAD (assign->u.ops);
  if (lhs == NULL || (lhs->code != N_FIELD && lhs->code != N_DEREF_FIELD)) return 0;
  parent = NL_HEAD (lhs->u.ops);
  pe = parent != NULL ? (struct expr *) parent->attr : NULL;
  pt = pe != NULL ? pe->type : NULL;
  if (pt == NULL) return 0;
  if (lhs->code == N_DEREF_FIELD && pt->mode == TM_PTR) pt = pt->u.ptr_type;
  if (pt == NULL || pt->mode != TM_CLASS) return 0;
  fe = (struct expr *) lhs->attr;
  return fe != NULL && builtin_string_type_p (fe->type);
}

/* Free every owned `String` member of the class object at `obj_ptr_op`.
   Emitted at object destruction (before the heap is freed) so each value-
   semantic String field is reclaimed exactly once.  No-op for non-class
   types or classes without String members.  `obj_ptr_op` must hold the
   object's address.

   Recurses into nested BY-VALUE class members (e.g. a class field of class
   type), so an object tree of value-semantic strings "just lives and dies"
   with the root object — a `String` on a nested class field is freed exactly
   once when the enclosing object is destroyed.  Class *pointer* members are
   NOT followed (they are separate objects with their own lifetime). */
static void gen_class_string_members_drop (c2m_ctx_t c2m_ctx, MIR_op_t obj_ptr_op,
                                           struct type *cls) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_context_t ctx = c2m_ctx->ctx;
  node_t tag, member_list;
  op_t base;
  if (cls == NULL || cls->mode != TM_CLASS || cls->u.tag_type == NULL) return;
  tag = cls->u.tag_type;
  member_list = TAG_MEMBER_LIST (tag);
  if (member_list == NULL || member_list->code != N_LIST) return;
  /* Cheap pre-scan: skip the whole dance unless there is a String member or a
     nested by-value class member that might itself contain one. */
  {
    int any = 0;
    for (node_t m = NL_HEAD (member_list->u.ops); m != NULL; m = NL_NEXT (m)) {
      decl_t md;
      struct type *mt;
      if (m->code != N_MEMBER) continue;
      md = (decl_t) m->attr;
      if (md == NULL || (mt = md->decl_spec.type) == NULL) continue;
      if (builtin_string_type_p (mt) || mt->mode == TM_CLASS) { any = 1; break; }
    }
    if (!any) return;
  }
  string_ensure_imports (c2m_ctx);
  base = force_reg (c2m_ctx, new_op (NULL, obj_ptr_op), MIR_T_I64);
  for (node_t m = NL_HEAD (member_list->u.ops); m != NULL; m = NL_NEXT (m)) {
    decl_t md;
    struct type *mt;
    if (m->code != N_MEMBER) continue;
    md = (decl_t) m->attr;
    if (md == NULL || (mt = md->decl_spec.type) == NULL) continue;
    if (builtin_string_type_p (mt)) {
      op_t fld = get_new_temp (c2m_ctx, MIR_T_I64);
      MIR_op_t darg;
      emit2 (c2m_ctx, MIR_MOV, fld.mir_op,
             MIR_new_mem_op (ctx, MIR_T_I64, (MIR_disp_t) md->offset, base.mir_op.u.reg, 0, 1));
      darg = fld.mir_op;
      gen_rt_call_void (c2m_ctx, str_drop_proto, str_drop_item, 1, &darg);
    } else if (mt->mode == TM_CLASS) {
      /* Nested by-value class field: recurse with its address (base+offset). */
      op_t nested = get_new_temp (c2m_ctx, MIR_T_I64);
      emit3 (c2m_ctx, MIR_ADD, nested.mir_op, base.mir_op,
             MIR_new_int_op (ctx, (long) md->offset));
      gen_class_string_members_drop (c2m_ctx, nested.mir_op, mt);
    }
  }
}

/* Import the object-arena runtime helpers (c2m_obj_checkpoint / release_to) the
   first time the automatic scope reclamation needs them.  c2m_obj_track itself
   is referenced from generated factory source, so it is imported through the
   normal extern path. */
static void object_ensure_imports (c2m_ctx_t c2m_ctx) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;

  if (obj_checkpoint_item != NULL) return;

  /* size_t c2m_obj_checkpoint(void) */
  rt_import (c2m_ctx, "c2m_obj_checkpoint", NULL, &obj_checkpoint_proto, &obj_checkpoint_item, 1,
             "");
  /* void c2m_obj_release_to(size_t mark) */
  rt_import (c2m_ctx, "c2m_obj_release_to", NULL, &obj_release_to_proto, &obj_release_to_item, 0,
             "mark");
  /* void *c2m_obj_detach(void *p)  (result ignored) */
  rt_import (c2m_ctx, "c2m_obj_detach", NULL, &obj_detach_proto, &obj_detach_item, 1, "p");
}

/* c2m_obj_detach(p) : remove p from the object arena without destroying it, so a
   subsequent explicit `delete` (or escape) does not double-free at scope exit. */
static void gen_obj_detach (c2m_ctx_t c2m_ctx, MIR_op_t p) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  object_ensure_imports (c2m_ctx);
  gen_rt_call (c2m_ctx, obj_detach_proto, obj_detach_item, 1, &p); /* result ignored */
}

/* res = c2m_obj_checkpoint() : current object-arena high-water mark. */
static op_t gen_obj_checkpoint (c2m_ctx_t c2m_ctx) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  object_ensure_imports (c2m_ctx);
  return gen_rt_call (c2m_ctx, obj_checkpoint_proto, obj_checkpoint_item, 0, NULL);
}

/* c2m_obj_release_to(mark) : destroy every Any<I> handle tracked since mark. */
static void gen_obj_release_to (c2m_ctx_t c2m_ctx, MIR_op_t mark) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  object_ensure_imports (c2m_ctx);
  gen_rt_call_void (c2m_ctx, obj_release_to_proto, obj_release_to_item, 1, &mark);
}

/* res = c2m_str_checkpoint() : current allocation high-water mark. */
static op_t gen_str_checkpoint (c2m_ctx_t c2m_ctx) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  string_ensure_imports (c2m_ctx);
  return gen_rt_call (c2m_ctx, str_checkpoint_proto, str_checkpoint_item, 0, NULL);
}

/* c2m_str_release_to(mark) : free every String allocated since the mark. */
static void gen_str_release_to (c2m_ctx_t c2m_ctx, MIR_op_t mark) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  string_ensure_imports (c2m_ctx);
  gen_rt_call_void (c2m_ctx, str_release_to_proto, str_release_to_item, 1, &mark);
}

/* c2m_str_release_keeping(mark, keep) : free everything since the mark except
   `keep`, which is retained for the enclosing scope (safe String return). */
static void gen_str_release_keeping (c2m_ctx_t c2m_ctx, MIR_op_t mark, MIR_op_t keep) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_op_t ops[2] = {mark, keep};
  string_ensure_imports (c2m_ctx);
  gen_rt_call_void (c2m_ctx, str_release_keeping_proto, str_release_keeping_item, 2, ops);
}

/* Serialize a dict to a JSON String that participates in the String arena.

   The naive implementation (alloca-buffer + dict_serialize_json into it) hands
   back a *stack pointer* whose lifetime ends with the producing function's
   frame.  That works in trivial expressions but fails the moment the value
   has to survive `return`, get stored on a heap object, cross a `try` boundary,
   or interact with `defer`/exception cleanup.

   Instead we delegate sizing and allocation to the runtime's
   `dict_serialize_json_heap`, which doubles a heap buffer until the JSON fits
   and returns a right-sized, plain-malloc'd pointer.  We then register that
   pointer with the String arena via `c2m_str_attach`.  After that:

     - the buffer survives across function returns,
     - the function-level `c2m_str_release_to` reclaims it at scope exit,
     - `c2m_str_release_keeping` protects it when it is the returned value,
     - `String.detach()` cleanly hands it to a longer-lived owner,
     - and the compiler imposes no fixed size cap on the output.

   `dict_val_op` is the MIR operand carrying the source DictValue*; the result
   is the tracked `char*` (a MIR I64 register).
*/
static op_t gen_dict_serialize_to_tracked_string (c2m_ctx_t c2m_ctx, MIR_op_t dict_val_op) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_context_t ctx = c2m_ctx->ctx;

  dict_ensure_imports (c2m_ctx);
  string_ensure_imports (c2m_ctx);

  /* res = dict_serialize_json_heap(val, 0)   -- second arg is pretty=0 */
  MIR_op_t heap_args[2] = { dict_val_op, MIR_new_int_op (ctx, 0) };
  op_t res = gen_rt_call (c2m_ctx, dict_serialize_json_heap_proto,
                          dict_serialize_json_heap_item, 2, heap_args);
  res = force_reg (c2m_ctx, res, MIR_T_I64);

  /* tracked = c2m_str_attach(res) — register with the String arena tracker
     so scope cleanup / return protection / detach all behave correctly.
     c2m_str_attach is NULL-safe, so a runtime allocation failure (res == NULL)
     simply propagates as a NULL String to the caller. */
  op_t tracked = gen_rt_call (c2m_ctx, str_attach_proto, str_attach_item, 1, &res.mir_op);
  return force_reg (c2m_ctx, tracked, MIR_T_I64);
}

/* True for AST leaf nodes whose `u` union holds a scalar (not an op list), so
   the generic walker below must not treat `u.ops` as children. */
static int node_is_leaf_p (node_code_t c) {
  switch (c) {
  case N_IGNORE:
  case N_I: case N_L: case N_LL: case N_U: case N_UL: case N_ULL:
  case N_F: case N_D: case N_LD:
  case N_CH: case N_CH16: case N_CH32:
  case N_STR: case N_STR16: case N_STR32: case N_ID:
    return TRUE;
  default:
    return FALSE;
  }
}

/* Does this subtree contain any expression that allocates a tracked String?
   Covers:
     - the `+` concat operator (N_CONCAT) -- always heap-allocates;
     - any N_CALL whose result type is `String` (TP_STRING) -- this catches
       *cross-function* allocations transparently: a helper that internally
       does `+`, .substr/.upper/..., String.copy, json(), List<String>.join,
       or any other String-returning operation will be seen as allocating by
       its caller.  Without this, a `main` that does all its work through
       helpers gets no automatic scope reclamation, and memory grows across
       loop iterations (the classy-fetch.cy pattern);
     - the older specific N_CALL patterns kept as a defensive fast-path for
       cases where the call expr's `attr` (type info) may not yet be set in
       some specialised paths.
   Used to decide whether a function body or a loop body needs automatic
   scope reclamation via checkpoint/release_to.
   Nested function definitions are not descended into -- their allocations
   belong to their own scope. */
static int subtree_allocates_string_p (node_t n) {
  if (n == NULL || node_is_leaf_p (n->code)) return FALSE;
  /* N_CONCAT is the `String +` operator node -- always heap-allocates. */
  if (n->code == N_CONCAT) return TRUE;
  if (n->code == N_CALL) {
    /* General rule: any call returning a `String` produces a tracked
       allocation in the caller's scope (return values are protected by
       release_keeping at the callee's N_RETURN).  This subsumes the
       method-specific patterns below and catches calls into user helpers,
       library wrappers, json(...), List<String>.join(...), etc. */
    struct expr *ce = (struct expr *) n->attr;
    if (ce != NULL && builtin_string_type_p (ce->type)) return TRUE;
    node_t f = NL_HEAD (n->u.ops);
    if (f != NULL && (f->code == N_FIELD || f->code == N_DEREF_FIELD)) {
      node_t obj = NL_HEAD (f->u.ops);
      node_t m = obj == NULL ? NULL : NL_NEXT (obj);
      if (obj != NULL && obj->code == N_STRING && m != NULL && m->code == N_ID) {
        enum str_method sm = get_string_method (m->u.s.s, NULL, NULL);
        /* Both copy (allocates) and attach (registers external ptr) add to
           the tracker and need a scope checkpoint for cleanup. */
        if (sm == SM_COPY || sm == SM_ATTACH) return TRUE;
      }
      /* An allocating instance method on a UTF-8 string literal ("abc".upper())
         also produces a tracked String and needs a scope checkpoint. */
      if (obj != NULL && obj->code == N_STR && m != NULL && m->code == N_ID) {
        enum str_method sm = get_string_method (m->u.s.s, NULL, NULL);
        if (sm == SM_SUBSTR || sm == SM_REPLACE || sm == SM_UPPER
            || sm == SM_LOWER || sm == SM_TRIM)
          return TRUE;
      }
      struct expr *oe = obj == NULL ? NULL : obj->attr;
      if (oe != NULL && builtin_string_type_p (oe->type) && m != NULL && m->code == N_ID) {
        enum str_method sm = get_string_method (m->u.s.s, NULL, NULL);
        if (sm == SM_SUBSTR || sm == SM_REPLACE
            || sm == SM_UPPER || sm == SM_LOWER || sm == SM_TRIM) return TRUE;
      }
      /* List<String>.join(delim) -> String : the receiver is a List<String>
         (not a String/N_STRING), so the cases above miss it.  The general
         attr-type check above already covers it when typing has populated
         the call's expr, but be defensive in case it has not. */
      if (oe != NULL && list_string_type_p (oe->type) && m != NULL && m->code == N_ID
          && get_string_method (m->u.s.s, NULL, NULL) == SM_JOIN) return TRUE;
    }
  }
  for (node_t c = NL_HEAD (n->u.ops); c != NULL; c = NL_NEXT (c)) {
    if (c->code == N_FUNC_DEF) continue; /* separate scope */
    if (subtree_allocates_string_p (c)) return TRUE;
  }
  return FALSE;
}

/* TRUE if the subtree constructs an Any<I> handle (any<I>(...)), i.e. tracks an
   object in the scope-bound arena that needs releasing at scope exit. */
static int subtree_allocates_object_p (node_t n) {
  if (n == NULL || node_is_leaf_p (n->code)) return FALSE;
  if (n->code == N_ANY) return TRUE;
  for (node_t c = NL_HEAD (n->u.ops); c != NULL; c = NL_NEXT (c)) {
    if (c->code == N_FUNC_DEF) continue; /* separate scope */
    if (subtree_allocates_object_p (c)) return TRUE;
  }
  return FALSE;
}

/* Does this subtree contain an assignment whose LHS is a `String`-typed
   identifier or a tracked-pointer-typed identifier?  Such an assignment can
   smuggle a tracked allocation from inside the iteration into a variable
   whose lifetime spans iterations -- so a per-iter release would dangle the
   variable.  We use this as a conservative signal to DISABLE per-iter
   reclamation for the loop body: correctness over peak memory.

   This is over-conservative (catches body-local reassignments too), but the
   common bounded patterns -- `String t = helper(i);` (init not assign),
   `Http.get(...)` returning a class pointer + `defer delete`, dict/header
   lookups consumed inline by printf -- all stay eligible and remain bounded.

   Limits:
     - We do NOT try to detect escape through method calls (e.g.
       `list->Add(s)` storing a String into an outer-scope collection).
       Such patterns will still dangle.  Users in doubt can wrap their loop
       in `{ ... }` to defeat detection -- or, as before, keep using the
       manual c2m_str_checkpoint/release_to runtime calls.
     - We DO descend through nested blocks but stop at nested function
       definitions (their allocations belong to their own scope). */
static int subtree_assigns_tracked_id_p (node_t n) {
  if (n == NULL || node_is_leaf_p (n->code)) return FALSE;
  if (n->code == N_ASSIGN) {
    node_t lhs = NL_HEAD (n->u.ops);
    if (lhs != NULL && lhs->code == N_ID) {
      struct expr *le = (struct expr *) lhs->attr;
      if (le != NULL && builtin_string_type_p (le->type)) return TRUE;
    }
  }
  for (node_t c = NL_HEAD (n->u.ops); c != NULL; c = NL_NEXT (c)) {
    if (c->code == N_FUNC_DEF) continue; /* separate scope */
    if (subtree_assigns_tracked_id_p (c)) return TRUE;
  }
  return FALSE;
}

/* TRUE if the subtree stores a tracked `String` into a String-holding
   collection via a method call, e.g. `names->Add(s)` / `ages->Set(key, v)`
   where the receiver is a `List<String>` / `Set<String>` / `Map<String,...>`
   (or `Map<...,String>`).  Such a store smuggles an arena-tracked String out
   of the loop iteration into a collection whose lifetime spans iterations, so
   a per-iteration `c2m_str_release_to` would free the buffer the collection
   still points at — a use-after-free once the collection is read after the
   loop.  Like `subtree_assigns_tracked_id_p`, we use this to DISABLE per-iter
   reclamation for the loop body and fall back to function-scope cleanup
   (correctness over peak memory).

   Detection is deliberately narrow — it only fires when the receiver is a
   generic collection specialization whose element/key/value set includes
   `String` (mangled class name `__generic_*_String*`) AND an argument is a
   String value.  That keeps bounded patterns that merely *pass* a String to a
   non-retaining callee (e.g. `printf("%s", s)`, `Http.get(BASE + name)`) out
   of scope, so the classy-fetch-style tight loops stay per-iteration bounded. */
static int subtree_retains_string_in_collection_p (node_t n) {
  if (n == NULL || node_is_leaf_p (n->code)) return FALSE;
  if (n->code == N_CALL) {
    node_t callee = NL_HEAD (n->u.ops);
    if (callee != NULL && (callee->code == N_FIELD || callee->code == N_DEREF_FIELD)) {
      node_t recv = NL_HEAD (callee->u.ops);
      struct expr *re = recv == NULL ? NULL : (struct expr *) recv->attr;
      const char *cname = NULL;
      if (re != NULL && re->type != NULL) {
        const struct type *t = re->type;
        if (t->mode == TM_PTR) t = t->u.ptr_type;
        cname = class_type_name (t);
      }
      if (cname != NULL && strncmp (cname, "__generic_", 10) == 0
          && strstr (cname, "_String") != NULL) {
        node_t args = NL_NEXT (callee);
        if (args != NULL && args->code == N_LIST)
          for (node_t a = NL_HEAD (args->u.ops); a != NULL; a = NL_NEXT (a)) {
            struct expr *ae = (struct expr *) a->attr;
            if (ae != NULL && builtin_string_type_p (ae->type)) return TRUE;
          }
      }
    }
  }
  for (node_t c = NL_HEAD (n->u.ops); c != NULL; c = NL_NEXT (c)) {
    if (c->code == N_FUNC_DEF) continue; /* separate scope */
    if (subtree_retains_string_in_collection_p (c)) return TRUE;
  }
  return FALSE;
}

/* TRUE if the subtree stores a tracked `Any<I>` handle into a
   handle-holding collection via a method call, e.g. `shapes->Add(any<Shape>(new
   Circle(r)))` / `byName->Set(key, h)` where the receiver is a `List<Any<I>*>`
   / `Set<Any<I>*>` / `Map<K, Any<I>*>`.  Mirrors
   subtree_retains_string_in_collection_p exactly, but for the object arena:
   the collection can outlive both a single loop iteration (per-iter release
   would free an element still referenced by the collection) and the
   enclosing function itself (a returned collection's contents would be freed
   out from under the caller by the function-level release).  Used to gate
   BOTH the per-iteration loop arena and the function-level arena for the
   object side -- see gen_loop_body_scope_enter and the function-entry
   checkpoint in the N_BLOCK gen case.

   Detection is deliberately narrow, same rationale as the String version:
   only fires when the receiver is a generic collection specialized on an
   Any<I> handle type (mangled class name `__generic_*__Any_*`) AND an
   argument's type is a pointer to a synthesized `__Any_<Interface>` erasure
   class. That keeps ordinary handle usage (method calls on the handle,
   passing it to a plain helper function to read) out of scope, so those
   patterns stay eligible for automatic reclamation. */
static int subtree_retains_object_in_collection_p (node_t n) {
  if (n == NULL || node_is_leaf_p (n->code)) return FALSE;
  if (n->code == N_CALL) {
    node_t callee = NL_HEAD (n->u.ops);
    if (callee != NULL && (callee->code == N_FIELD || callee->code == N_DEREF_FIELD)) {
      node_t recv = NL_HEAD (callee->u.ops);
      struct expr *re = recv == NULL ? NULL : (struct expr *) recv->attr;
      const char *cname = NULL;
      if (re != NULL && re->type != NULL) {
        const struct type *t = re->type;
        if (t->mode == TM_PTR) t = t->u.ptr_type;
        cname = class_type_name (t);
      }
      if (cname != NULL && strncmp (cname, "__generic_", 10) == 0
          && strstr (cname, "__Any_") != NULL) {
        node_t args = NL_NEXT (callee);
        if (args != NULL && args->code == N_LIST)
          for (node_t a = NL_HEAD (args->u.ops); a != NULL; a = NL_NEXT (a)) {
            struct expr *ae = (struct expr *) a->attr;
            const char *acn = NULL;
            if (ae != NULL && ae->type != NULL && ae->type->mode == TM_PTR)
              acn = class_type_name (ae->type->u.ptr_type);
            if (acn != NULL && strncmp (acn, "__Any_", 6) == 0) return TRUE;
          }
      }
    }
  }
  for (node_t c = NL_HEAD (n->u.ops); c != NULL; c = NL_NEXT (c)) {
    if (c->code == N_FUNC_DEF) continue; /* separate scope */
    if (subtree_retains_object_in_collection_p (c)) return TRUE;
  }
  return FALSE;
}

/* Per-loop-body arena scope helpers --------------------------------------
   Layered inside the function-level str_scope_X / obj_scope_X state: each
   loop iteration emits a fresh checkpoint at the top of the body and a
   release at the bottom (and on continue/break).  This keeps per-iteration
   allocations bounded for tight loops driven by helper-call allocations
   (e.g. the classy-fetch.cy pattern -- a tight for(i<5) loop in main()
   whose iterations allocate Strings only through helper calls like
   Http.get(POKE_API + name)).  The function-level scope still owns the
   wider lifetime: N_RETURN unconditionally releases back to the function
   mark, which covers anything a loop missed (e.g. on break).

   The helpers update loop_str_scope_X / loop_obj_scope_X only; the
   function-level str_scope_X / obj_scope_X are untouched, so N_RETURN's
   release_keeping for returned Strings keeps working unchanged.
   N_BREAK/N_CONTINUE consult loop_X_scope_active and emit a release before
   they jump, so the iteration that called break/continue is reclaimed. */
static void gen_loop_body_scope_enter (c2m_ctx_t c2m_ctx, node_t body,
                                       MIR_label_t this_loop_break_label,
                                       int *str_was_active, op_t *str_saved_mark,
                                       int *obj_was_active, op_t *obj_saved_mark,
                                       MIR_label_t *saved_loop_break_label) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  *str_was_active = loop_str_scope_active;
  *str_saved_mark = loop_str_scope_mark;
  *obj_was_active = loop_obj_scope_active;
  *obj_saved_mark = loop_obj_scope_mark;
  *saved_loop_break_label = loop_break_label_for_scope;
  loop_str_scope_active = FALSE;
  loop_obj_scope_active = FALSE;
  loop_break_label_for_scope = this_loop_break_label;
  if (body == NULL) return;
  /* Conservatism: if the body assigns a tracked-type identifier (e.g.
     `outerString = helper(i);`), the assigned value may escape the
     iteration and our release would dangle the variable.  Skip per-iter
     reclamation in that case -- the function-level scope still cleans up
     at return, so correctness is preserved (only the per-iter memory win
     is lost). */
  int safe_for_per_iter = !subtree_assigns_tracked_id_p (body)
                          && !subtree_retains_string_in_collection_p (body);
  int safe_for_per_iter_obj = safe_for_per_iter
                              && !subtree_retains_object_in_collection_p (body);
  if (safe_for_per_iter && subtree_allocates_string_p (body)) {
    loop_str_scope_mark = gen_str_checkpoint (c2m_ctx);
    loop_str_scope_active = TRUE;
  }
  if (safe_for_per_iter_obj && subtree_allocates_object_p (body)) {
    loop_obj_scope_mark = gen_obj_checkpoint (c2m_ctx);
    loop_obj_scope_active = TRUE;
  }
}

/* Emit release back to this iteration's mark (called at the bottom of the
   body on fall-through, and before jumping for `break`/`continue`).  Safe
   to call when not active (no-op). */
static void gen_loop_body_scope_release (c2m_ctx_t c2m_ctx) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  if (loop_obj_scope_active) gen_obj_release_to (c2m_ctx, loop_obj_scope_mark.mir_op);
  if (loop_str_scope_active) gen_str_release_to (c2m_ctx, loop_str_scope_mark.mir_op);
}

/* Restore the enclosing loop's per-iter scope state (used after a loop
   case finishes emitting code for its body and back-edge). */
static void gen_loop_body_scope_leave (c2m_ctx_t c2m_ctx,
                                       int str_was_active, op_t str_saved_mark,
                                       int obj_was_active, op_t obj_saved_mark,
                                       MIR_label_t saved_loop_break_label) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  loop_str_scope_active = str_was_active;
  loop_str_scope_mark = str_saved_mark;
  loop_obj_scope_active = obj_was_active;
  loop_obj_scope_mark = obj_saved_mark;
  loop_break_label_for_scope = saved_loop_break_label;
}

static void emit_scalar_assign (c2m_ctx_t c2m_ctx, op_t var, op_t *val, MIR_type_t t,
                                int ignore_others_p) {
  if (var.decl == NULL || var.decl->bit_offset < 0) {
    if (var.mir_op.mode == MIR_OP_MEM && op_decl_atomic_p (var)) {
      MIR_type_t at = get_mir_type (c2m_ctx, var.decl->decl_spec.type);
      var.mir_op.u.mem.type = at;
      atomic_store_mem (c2m_ctx, var, *val);
    } else {
      emit2_noopt (c2m_ctx, tp_mov (t), var.mir_op, val->mir_op);
    }
  } else {
    MIR_context_t ctx = c2m_ctx->ctx;
    int width = var.decl->width;
    uint64_t mask, mask2;
    op_t temp_op1, temp_op2, temp_op3, temp_op4;
    size_t MIR_UNUSED size = type_size (c2m_ctx, var.decl->decl_spec.type) * MIR_CHAR_BIT;

    assert (var.mir_op.mode == MIR_OP_MEM); /*???*/
    mask = 0xffffffffffffffff >> (64 - width);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    mask2 = ~(mask << var.decl->bit_offset);
#else
    mask2 = ~(mask << (size - var.decl->bit_offset - width));
#endif
    temp_op1 = get_new_temp (c2m_ctx, MIR_T_I64);
    temp_op2 = get_new_temp (c2m_ctx, MIR_T_I64);
    temp_op3 = get_new_temp (c2m_ctx, MIR_T_I64);
    if (!ignore_others_p) {
      emit2_noopt (c2m_ctx, MIR_MOV, temp_op2.mir_op, var.mir_op);
      emit3 (c2m_ctx, MIR_AND, temp_op2.mir_op, temp_op2.mir_op, MIR_new_uint_op (ctx, mask2));
    }
    if (!signed_integer_type_p (var.decl->decl_spec.type)) {
      emit2 (c2m_ctx, MIR_MOV, temp_op1.mir_op, val->mir_op);
      *val = temp_op3;
    } else {
      emit3 (c2m_ctx, MIR_LSH, temp_op1.mir_op, val->mir_op, MIR_new_int_op (ctx, 64 - width));
      emit3 (c2m_ctx, MIR_RSH, temp_op1.mir_op, temp_op1.mir_op, MIR_new_int_op (ctx, 64 - width));
      *val = temp_op1;
    }
    emit3 (c2m_ctx, MIR_AND, temp_op3.mir_op, temp_op1.mir_op, MIR_new_uint_op (ctx, mask));
    temp_op4 = get_new_temp (c2m_ctx, MIR_T_I64);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if (var.decl->bit_offset == 0) {
      temp_op4 = temp_op3;
    } else {
      emit3 (c2m_ctx, MIR_LSH, temp_op4.mir_op, temp_op3.mir_op,
             MIR_new_int_op (ctx, var.decl->bit_offset));
    }
#else
    if (size - var.decl->bit_offset - width == 0) {
      temp_op4 = temp_op3;
    } else {
      emit3 (c2m_ctx, MIR_LSH, temp_op4.mir_op, temp_op3.mir_op,
             MIR_new_int_op (ctx, size - var.decl->bit_offset - width));
    }
#endif
    if (!ignore_others_p) {
      emit3 (c2m_ctx, MIR_OR, temp_op4.mir_op, temp_op4.mir_op, temp_op2.mir_op);
    }
    emit2 (c2m_ctx, MIR_MOV, var.mir_op, temp_op4.mir_op);
  }
}

static void add_bit_field (c2m_ctx_t c2m_ctx, uint64_t *u, uint64_t v, decl_t member_decl) {
  uint64_t mask, mask2;
  int bit_offset = member_decl->bit_offset, width = member_decl->width;
  size_t MIR_UNUSED size = type_size (c2m_ctx, member_decl->decl_spec.type) * MIR_CHAR_BIT;

  mask = 0xffffffffffffffff >> (64 - width);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  mask2 = ~(mask << bit_offset);
#else
  mask2 = ~(mask << (size - bit_offset - width));
#endif
  *u &= mask2;
  if (signed_integer_type_p (member_decl->decl_spec.type)) {
    v <<= (64 - width);
    v = (int64_t) v >> (64 - width);
  }
  v &= mask;
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  v <<= bit_offset;
#else
  v <<= size - bit_offset - width;
#endif
  *u |= v;
}

static MIR_item_t get_mir_str_op_data (c2m_ctx_t c2m_ctx, MIR_str_t str) {
  MIR_context_t ctx = c2m_ctx->ctx;
  MIR_item_t data;
  char buff[50];
  MIR_module_t module = DLIST_TAIL (MIR_module_t, *MIR_get_module_list (ctx));

  _MIR_get_temp_item_name (ctx, module, buff, sizeof (buff));
  data = MIR_new_string_data (ctx, buff, str);
  move_item_to_module_start (module, data);
  return data;
}

static MIR_item_t get_string_data (c2m_ctx_t c2m_ctx, node_t n) {
  MIR_context_t ctx = c2m_ctx->ctx;
  MIR_item_t data;
  char buff[50];
  MIR_module_t module = DLIST_TAIL (MIR_module_t, *MIR_get_module_list (ctx));

  _MIR_get_temp_item_name (ctx, module, buff, sizeof (buff));
  if (n->code == N_STR) {
    data = MIR_new_string_data (ctx, buff, (MIR_str_t){n->u.s.len, n->u.s.s});
  } else {
    assert (n->code == N_STR16 || n->code == N_STR32);
    if (n->code == N_STR16) {
      data = MIR_new_data (ctx, buff, MIR_T_U16, n->u.s.len / 2, n->u.s.s);
    } else {
      data = MIR_new_data (ctx, buff, MIR_T_U32, n->u.s.len / 4, n->u.s.s);
    }
  }
  move_item_to_module_start (module, data);
  return data;
}

static void gen_initializer (c2m_ctx_t c2m_ctx, size_t init_start, op_t var,
                             const char *global_name, mir_size_t size, int local_p) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_context_t ctx = c2m_ctx->ctx;
  op_t val;
  size_t str_len;
  mir_size_t data_size, el_size, offset = 0, rel_offset = 0, start_offset;
  init_el_t init_el, next_init_el;
  MIR_reg_t base;
  MIR_type_t t;
  MIR_item_t data = NULL; /* to remove a warning */
  struct expr *e;
  int tls_p
    = !local_p && var.decl != NULL && var.decl->decl_spec.thread_local_p;

  if (var.mir_op.mode == MIR_OP_REG) { /* scalar initialization: */
    assert (local_p && offset == 0 && VARR_LENGTH (init_el_t, init_els) - init_start == 1);
    init_el = VARR_GET (init_el_t, init_els, init_start);
    val = val_gen (c2m_ctx, init_el.init);
    val = maybe_unwrap_dict_value (c2m_ctx, val, init_el.init, init_el.el_type);
    t = get_op_type (c2m_ctx, var);
    val = cast (c2m_ctx, val, get_mir_type (c2m_ctx, init_el.el_type), FALSE);
    emit_scalar_assign (c2m_ctx, var, &val, t, FALSE);
  } else if (local_p) { /* local variable initialization: */
    assert (var.mir_op.mode == MIR_OP_MEM && var.mir_op.u.mem.index == 0); /*???*/
    offset = var.mir_op.u.mem.disp;
    base = var.mir_op.u.mem.base;
    for (size_t i = init_start; i < VARR_LENGTH (init_el_t, init_els); i++) {
      init_el = VARR_GET (init_el_t, init_els, i);
      t = get_mir_type (c2m_ctx, init_el.el_type);
      if (rel_offset < init_el.offset) { /* fill the gap: */
        gen_memset (c2m_ctx, offset + rel_offset, base, init_el.offset - rel_offset);
        rel_offset = init_el.offset;
      }
      if (t == MIR_T_UNDEF)
        val = new_op (NULL, MIR_new_mem_op (ctx, t, offset + rel_offset, base, 0, 1));
      val = gen (c2m_ctx, init_el.init, NULL, NULL, t != MIR_T_UNDEF,
                 t != MIR_T_UNDEF ? NULL : &val, NULL);
      if (!scalar_type_p (init_el.el_type)) {
        /* Use type_size (aligned sizeof), not raw_type_size: trailing padding is
           part of the object for array stride / by-value BLK ABI (Ship 32 vs 28). */
        mir_size_t s = init_el.init->code == N_STR     ? init_el.init->u.s.len
                       : init_el.init->code == N_STR16 ? init_el.init->u.s.len / 2
                       : init_el.init->code == N_STR32 ? init_el.init->u.s.len / 4
                                                       : type_size (c2m_ctx, init_el.el_type);
        gen_memcpy (c2m_ctx, offset + rel_offset, base, val, s);
        rel_offset = init_el.offset + s;
      } else {
        MIR_op_t mem
          = MIR_new_alias_mem_op (ctx, t, offset + init_el.offset, base, 0, 1,
                                  get_type_alias (c2m_ctx,
                                                  init_el.container_type != NULL
                                                      && init_el.container_type->mode == TM_UNION
                                                    ? init_el.container_type
                                                    : init_el.el_type),
                                  0);
        val = cast (c2m_ctx, val, get_mir_type (c2m_ctx, init_el.el_type), FALSE);
        emit_scalar_assign (c2m_ctx, new_op (init_el.member_decl, mem), &val, t,
                            i == init_start || rel_offset == init_el.offset);
        rel_offset = init_el.offset + _MIR_type_size (ctx, t);
      }
    }
    if (rel_offset < size) /* fill the tail: */
      gen_memset (c2m_ctx, offset + rel_offset, base, size - rel_offset);
  } else {
    VARR (MIR_op_t) * pregen_vals;
    MIR_alloc_t alloc = c2m_alloc (c2m_ctx);

    assert (var.mir_op.mode == MIR_OP_REF);
    /* MIR #448 / PR middle-end/24109: pre-compute element values so compound-literal
       storage is emitted before this object's data stream (not into the middle). */
    VARR_CREATE (MIR_op_t, pregen_vals, alloc, VARR_LENGTH (init_el_t, init_els) - init_start);
    for (size_t k = init_start; k < VARR_LENGTH (init_el_t, init_els); k++) {
      init_el_t pe = VARR_GET (init_el_t, init_els, k);
      struct expr *pex = pe.init->attr;
      MIR_op_t pv;

      pv.mode = MIR_OP_UNDEF;
      if (!pex->const_addr_p) {
        if (pex->const_p) {
          convert_value (pex, pe.el_type);
          pex->type = pe.el_type;
        }
        pv = val_gen (c2m_ctx, pe.init).mir_op;
      }
      VARR_PUSH (MIR_op_t, pregen_vals, pv);
    }
    for (size_t i = init_start; i < VARR_LENGTH (init_el_t, init_els); i++) {
      init_el = VARR_GET (init_el_t, init_els, i);
      if (i != init_start && init_el.offset == VARR_GET (init_el_t, init_els, i - 1).offset
          && (init_el.member_decl == NULL || init_el.member_decl->bit_offset < 0))
        continue;
      e = init_el.init->attr;
      if (!e->const_addr_p) {
        val.decl = NULL;
        val.mir_op = VARR_GET (MIR_op_t, pregen_vals, i - init_start);
        assert (val.mir_op.mode == MIR_OP_INT || val.mir_op.mode == MIR_OP_UINT
                || val.mir_op.mode == MIR_OP_FLOAT || val.mir_op.mode == MIR_OP_DOUBLE
                || val.mir_op.mode == MIR_OP_LDOUBLE || val.mir_op.mode == MIR_OP_STR
                || val.mir_op.mode == MIR_OP_REF);
      }
      if (rel_offset < init_el.offset) { /* fill the gap: */
        data = tls_p ? MIR_new_tls_bss (ctx, global_name, init_el.offset - rel_offset)
                     : MIR_new_bss (ctx, global_name, init_el.offset - rel_offset);
        if (global_name != NULL) var.decl->u.item = data;
        global_name = NULL;
      }
      t = get_mir_type (c2m_ctx, init_el.el_type);
      if (e->const_addr_p) {
        node_t def;

        if ((def = e->def_node) == NULL) { /* constant address */
          mir_size_t s = e->c.i_val;
          data = tls_p ? MIR_new_tls_data (ctx, global_name, MIR_T_P, 1, &s)
                       : MIR_new_data (ctx, global_name, MIR_T_P, 1, &s);
          data_size = _MIR_type_size (ctx, MIR_T_P);
        } else if (def->code == N_LABEL_ADDR) {
          /* Label addresses in TLS are not supported in N1; fall through as normal data. */
          data = MIR_new_lref_data (ctx, global_name,
                                    get_label (c2m_ctx,
                                               ((struct expr *) def->attr)->u.label_addr_target),
                                    NULL, e->c.i_val);
          data_size = _MIR_type_size (ctx, t);
        } else {
          if (def->code != N_STR && def->code != N_STR16 && def->code != N_STR32) {
            data = ((decl_t) def->attr)->u.item;
          } else {
            data = get_string_data (c2m_ctx, def);
          }
          data = MIR_new_ref_data (ctx, global_name, data, e->c.i_val);
          data_size = _MIR_type_size (ctx, t);
        }
      } else if (val.mir_op.mode == MIR_OP_REF) {
        data = MIR_new_ref_data (ctx, global_name, val.mir_op.u.ref, 0);
        data_size = _MIR_type_size (ctx, t);
      } else if (val.mir_op.mode != MIR_OP_STR) {
        union {
          int8_t i8;
          uint8_t u8;
          int16_t i16;
          uint16_t u16;
          int32_t i32;
          uint32_t u32;
          int64_t i64;
          uint64_t u64;
          float f;
          double d;
          long double ld;
          uint8_t data[8];
        } u;
        start_offset = 0;
        el_size = data_size = _MIR_type_size (ctx, t);
        if (init_el.member_decl != NULL && init_el.member_decl->bit_offset >= 0) {
          uint64_t uval = 0;

          assert (val.mir_op.mode == MIR_OP_INT || val.mir_op.mode == MIR_OP_UINT);
          assert (init_el.member_decl->bit_offset % 8 == 0); /* first in the group of bitfields */
          start_offset = init_el.member_decl->bit_offset / 8;
          add_bit_field (c2m_ctx, &uval, val.mir_op.u.u, init_el.member_decl);
          for (; i + 1 < VARR_LENGTH (init_el_t, init_els); i++, init_el = next_init_el) {
            next_init_el = VARR_GET (init_el_t, init_els, i + 1);
            if (next_init_el.offset != init_el.offset) break;
            if (next_init_el.member_decl->bit_offset == init_el.member_decl->bit_offset) continue;
            val = val_gen (c2m_ctx, next_init_el.init);
            assert (val.mir_op.mode == MIR_OP_INT || val.mir_op.mode == MIR_OP_UINT);
            add_bit_field (c2m_ctx, &uval, val.mir_op.u.u, next_init_el.member_decl);
          }
          val.mir_op.u.u = uval;
          if (i + 1 < VARR_LENGTH (init_el_t, init_els)
              && next_init_el.offset - init_el.offset < data_size)
            data_size = next_init_el.offset - init_el.offset;
        }
        switch (t) {
        case MIR_T_I8: u.i8 = (int8_t) val.mir_op.u.i; break;
        case MIR_T_U8: u.u8 = (uint8_t) val.mir_op.u.u; break;
        case MIR_T_I16: u.i16 = (int16_t) val.mir_op.u.i; break;
        case MIR_T_U16: u.u16 = (uint16_t) val.mir_op.u.u; break;
        case MIR_T_I32: u.i32 = (int32_t) val.mir_op.u.i; break;
        case MIR_T_U32: u.u32 = (uint32_t) val.mir_op.u.u; break;
        case MIR_T_I64: u.i64 = val.mir_op.u.i; break;
        case MIR_T_U64: u.u64 = val.mir_op.u.u; break;
        case MIR_T_F: u.f = val.mir_op.u.f; break;
        case MIR_T_D: u.d = val.mir_op.u.d; break;
        case MIR_T_LD: u.ld = val.mir_op.u.ld; break;
        default: assert (FALSE);
        }
        if (start_offset == 0 && data_size == el_size) {
          data = tls_p ? MIR_new_tls_data (ctx, global_name, t, 1, &u)
                       : MIR_new_data (ctx, global_name, t, 1, &u);
        } else if (tls_p) {
          /* TLS: emit whole object as byte image when bitfield packing applies. */
          data = MIR_new_tls_data (ctx, global_name, MIR_T_U8, data_size - start_offset,
                                   &u.data[start_offset]);
        } else {
          for (mir_size_t byte_num = start_offset; byte_num < data_size; byte_num++) {
            if (byte_num == start_offset)
              data = MIR_new_data (ctx, global_name, MIR_T_U8, 1, &u.data[byte_num]);
            else
              MIR_new_data (ctx, NULL, MIR_T_U8, 1, &u.data[byte_num]);
          }
        }
      } else if (init_el.el_type->mode == TM_ARR) {
        data_size = raw_type_size (c2m_ctx, init_el.el_type);
        str_len = val.mir_op.u.str.len;
        if (data_size < str_len) {
          data = tls_p ? MIR_new_tls_data (ctx, global_name, MIR_T_U8, data_size, val.mir_op.u.str.s)
                       : MIR_new_data (ctx, global_name, MIR_T_U8, data_size, val.mir_op.u.str.s);
        } else if (tls_p) {
          data = MIR_new_tls_data (ctx, global_name, MIR_T_U8, str_len, val.mir_op.u.str.s);
          if (data_size > str_len) MIR_new_tls_bss (ctx, NULL, data_size - str_len);
        } else {
          data = MIR_new_string_data (ctx, global_name, val.mir_op.u.str);
          if (data_size > str_len) MIR_new_bss (ctx, NULL, data_size - str_len);
        }
      } else {
        data = get_mir_str_op_data (c2m_ctx, val.mir_op.u.str);
        data = MIR_new_ref_data (ctx, global_name, data, 0);
        data_size = _MIR_type_size (ctx, t);
      }
      if (global_name != NULL) var.decl->u.item = data;
      global_name = NULL;
      rel_offset = init_el.offset + data_size;
    }
    if (rel_offset < size || size == 0) { /* fill the tail: */
      data = tls_p ? MIR_new_tls_bss (ctx, global_name, size - rel_offset)
                   : MIR_new_bss (ctx, global_name, size - rel_offset);
      if (global_name != NULL) var.decl->u.item = data;
    }
    VARR_DESTROY (MIR_op_t, pregen_vals);
  }
}

/* If `attrs` (a declaration's N_LIST of N_ATTR) carries `registry("NAME")`,
   return NAME (the section/registry key); otherwise NULL.  Used to lower the
   C23 `[[registry("...")]]` marker into a cross-module registry entry. */
static const char *registry_attr_name (node_t attrs) {
  if (attrs == NULL || attrs->code != N_LIST) return NULL;
  for (node_t a = NL_HEAD (attrs->u.ops); a != NULL; a = NL_NEXT (a)) {
    if (a->code != N_ATTR) continue;
    node_t id = NL_HEAD (a->u.ops);
    if (id == NULL || id->code != N_ID || strcmp (id->u.s.s, "registry") != 0) continue;
    node_t arglist = NL_NEXT (id);
    if (arglist == NULL || arglist->code != N_LIST) return NULL;
    node_t arg = NL_HEAD (arglist->u.ops);
    if (arg == NULL) return NULL;
    if (arg->code == N_STR || arg->code == N_ID) return arg->u.s.s;
    return NULL;
  }
  return NULL;
}

static MIR_item_t get_ref_item (c2m_ctx_t c2m_ctx, node_t def, const char *name) {
  MIR_context_t ctx = c2m_ctx->ctx;
  struct decl *decl = def->attr;

  if (def->code == N_FUNC_DEF
      || (def->code == N_SPEC_DECL && NL_EL (def->u.ops, 1)->code == N_DECL
          && decl->scope == top_scope && decl->decl_spec.type->mode != TM_FUNC
          && !decl->decl_spec.typedef_p && !decl->decl_spec.extern_p))
    return (decl->decl_spec.linkage == N_EXTERN ? MIR_new_export (ctx, name)
                                                : MIR_new_forward (ctx, name));
  return NULL;
}

static void emit_bin_op (c2m_ctx_t c2m_ctx, node_t r, struct type *type, op_t res, op_t op1,
                         op_t op2) {
  MIR_context_t ctx = c2m_ctx->ctx;
  op_t temp;

  if (type->mode == TM_PTR) { /* ptr +/- int */
    assert (r->code == N_ADD || r->code == N_SUB || r->code == N_ADD_ASSIGN
            || r->code == N_SUB_ASSIGN);
    if (((struct expr *) NL_HEAD (r->u.ops)->attr)->type->mode != TM_PTR) /* int + ptr */
      SWAP (op1, op2, temp);
    if (op2.mir_op.mode == MIR_OP_INT || op2.mir_op.mode == MIR_OP_UINT) {
      op2 = new_op (NULL,
                    MIR_new_int_op (ctx, op2.mir_op.u.i * type_size (c2m_ctx, type->u.ptr_type)));
    } else {
      temp = get_new_temp (c2m_ctx, get_mir_type (c2m_ctx, type));
      emit3 (c2m_ctx, sizeof (mir_size_t) == 8 ? MIR_MUL : MIR_MULS, temp.mir_op, op2.mir_op,
             MIR_new_int_op (ctx, type_size (c2m_ctx, type->u.ptr_type)));
      op2 = temp;
    }
  }
  emit3 (c2m_ctx, get_mir_type_insn_code (c2m_ctx, type, r), res.mir_op, op1.mir_op, op2.mir_op);
  if (type->mode != TM_PTR
      && (type = ((struct expr *) NL_HEAD (r->u.ops)->attr)->type)->mode
           == TM_PTR) { /* ptr - ptr */
    assert (r->code == N_SUB || r->code == N_SUB_ASSIGN);
    emit3 (c2m_ctx, sizeof (mir_size_t) == 8 ? MIR_DIV : MIR_DIVS, res.mir_op, res.mir_op,
           MIR_new_int_op (ctx, type_size (c2m_ctx, type->u.ptr_type)));
  }
}

static int signed_case_compare (const void *v1, const void *v2) {
  case_t c1 = *(const case_t *) v1, c2 = *(const case_t *) v2;
  struct expr *e1 = NL_HEAD (c1->case_node->u.ops)->attr;
  struct expr *e2 = NL_HEAD (c2->case_node->u.ops)->attr;

  assert (e1->c.i_val != e2->c.i_val);
  return e1->c.i_val < e2->c.i_val ? -1 : 1;
}

static int unsigned_case_compare (const void *v1, const void *v2) {
  case_t c1 = *(const case_t *) v1, c2 = *(const case_t *) v2;
  struct expr *e1 = NL_HEAD (c1->case_node->u.ops)->attr;
  struct expr *e2 = NL_HEAD (c2->case_node->u.ops)->attr;

  assert (e1->c.u_val != e2->c.u_val);
  return e1->c.u_val < e2->c.u_val ? -1 : 1;
}

static void make_cond_val (c2m_ctx_t c2m_ctx, node_t r, MIR_label_t true_label,
                           MIR_label_t false_label, op_t *res) {
  MIR_context_t ctx = c2m_ctx->ctx;
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  struct type *type = ((struct expr *) r->attr)->type;
  MIR_label_t end_label = MIR_new_label (ctx);
  *res = get_new_temp (c2m_ctx, get_mir_type (c2m_ctx, type));
  emit_label_insn_opt (c2m_ctx, true_label);
  emit2 (c2m_ctx, MIR_MOV, res->mir_op, one_op.mir_op);
  emit1 (c2m_ctx, MIR_JMP, MIR_new_label_op (ctx, end_label));
  emit_label_insn_opt (c2m_ctx, false_label);
  emit2 (c2m_ctx, MIR_MOV, res->mir_op, zero_op.mir_op);
  emit_label_insn_opt (c2m_ctx, end_label);
}

/* ===== Class-method name mangling =====
 * Class methods are lowered to free functions whose MIR symbol name encodes the
 * class, the method name, and the (user) parameter types.  This avoids global
 * symbol collisions between same-named methods of different classes (visible in
 * AOT `nm` output) and lets same-named methods of one class coexist as distinct
 * symbols, which is what enables method overloading.  The scheme is an
 * Itanium-flavoured shorthand; names stay within [A-Za-z0-9_] so they are safe
 * for every object format.  e.g.  Point::withX(int) -> `Point_withX__i`. */
static void append_type_mangle (c2m_ctx_t c2m_ctx, VARR (char) * b, struct type *t) {
  char numbuf[32];
  if (t == NULL) { VARR_PUSH (char, b, 'X'); return; }
  switch (t->mode) {
  case TM_BASIC: {
    char c;
    switch (t->u.basic_type) {
    case TP_VOID: c = 'v'; break;
    case TP_BOOL: c = 'b'; break;
    case TP_CHAR: c = 'c'; break;
    case TP_SCHAR: c = 'a'; break;
    case TP_UCHAR: c = 'h'; break;
    case TP_SHORT: c = 's'; break;
    case TP_USHORT: c = 't'; break;
    case TP_INT: c = 'i'; break;
    case TP_UINT: c = 'j'; break;
    case TP_LONG: c = 'l'; break;
    case TP_ULONG: c = 'm'; break;
    case TP_LLONG: c = 'x'; break;
    case TP_ULLONG: c = 'y'; break;
    case TP_FLOAT: c = 'f'; break;
    case TP_DOUBLE: c = 'd'; break;
    case TP_LDOUBLE: c = 'e'; break;
    case TP_STRING: c = 'S'; break;
    default: c = 'X'; break;
    }
    VARR_PUSH (char, b, c);
    break;
  }
  case TM_PTR:
    VARR_PUSH (char, b, 'P');
    append_type_mangle (c2m_ctx, b, t->u.ptr_type);
    break;
  case TM_ARR:
    VARR_PUSH (char, b, 'A');
    append_type_mangle (c2m_ctx, b, t->u.arr_type->el_type);
    break;
  case TM_ENUM:
    VARR_PUSH (char, b, 'i'); /* enum mangles like int */
    break;
  case TM_STRUCT:
  case TM_UNION:
  case TM_CLASS: {
    node_t id = (t->u.tag_type != NULL) ? TAG_ID (t->u.tag_type) : NULL;
    char tag = t->mode == TM_CLASS ? 'C' : t->mode == TM_UNION ? 'U' : 'T';
    VARR_PUSH (char, b, tag);
    if (id != NULL && id->code == N_ID) {
      int n = snprintf (numbuf, sizeof numbuf, "%u", (unsigned) strlen (id->u.s.s));
      for (int i = 0; i < n; i++) VARR_PUSH (char, b, numbuf[i]);
      for (const char *p = id->u.s.s; *p != '\0'; p++) VARR_PUSH (char, b, *p);
    } else {
      VARR_PUSH (char, b, '0');
    }
    break;
  }
  case TM_DICT: VARR_PUSH (char, b, 'D'); break;
  case TM_SLICE:
    VARR_PUSH (char, b, 'Q');
    append_type_mangle (c2m_ctx, b, t->u.ptr_type);
    break;
  case TM_FUNC: VARR_PUSH (char, b, 'F'); break;
  default: VARR_PUSH (char, b, 'X'); break;
  }
}

/* True if `param` (an N_SPEC_DECL parameter) is the implicit `this` receiver. */
static int param_is_this_p (node_t param) {
  node_t decl, id;
  if (param == NULL || param->code != N_SPEC_DECL) return FALSE;
  decl = SPEC_DECL_DECL (param);
  if (decl == NULL || decl->code != N_DECL) return FALSE;
  id = DECL_ID (decl);
  return id != NULL && id->code == N_ID && strcmp (id->u.s.s, "this") == 0;
}

/* Build the mangled MIR symbol name for class method `method` of `class_name`
   into `out`.  The implicit leading `this` parameter is skipped; `v` denotes an
   empty (or (void)) user parameter list. */
static void build_method_mir_name (c2m_ctx_t c2m_ctx, char *out, size_t outsz,
                                   const char *class_name, const char *method,
                                   struct func_type *ft) {
  MIR_alloc_t alloc = c2m_alloc (c2m_ctx);
  VARR (char) * b;
  node_t param;
  int any = FALSE;

  VARR_CREATE (char, b, alloc, 64);
  for (const char *p = class_name; *p != '\0'; p++) VARR_PUSH (char, b, *p);
  VARR_PUSH (char, b, '_');
  for (const char *p = method; *p != '\0'; p++) VARR_PUSH (char, b, *p);
  VARR_PUSH (char, b, '_');
  VARR_PUSH (char, b, '_');
  param = (ft != NULL) ? NL_HEAD (ft->param_list->u.ops) : NULL;
  if (param_is_this_p (param)) param = NL_NEXT (param);
  for (; param != NULL; param = NL_NEXT (param)) {
    struct decl_spec *ds;
    if (param->code != N_SPEC_DECL) continue;
    if (void_param_p (param)) continue; /* (void) */
    ds = get_param_decl_spec (param);
    if (ds == NULL) continue;
    append_type_mangle (c2m_ctx, b, ds->type);
    any = TRUE;
  }
  if (!any) VARR_PUSH (char, b, 'v');
  VARR_PUSH (char, b, '\0');
  snprintf (out, outsz, "%s", VARR_ADDR (char, b));
  VARR_DESTROY (char, b);
}

/* Build a MIR proto item for direct/indirect calls to a function of type FT
   and move it to the module start.  Shared by check-resolved class-method
   calls and the filter/map/reduce callback calls. */
static MIR_item_t gen_func_proto_item (c2m_ctx_t c2m_ctx, struct func_type *ft) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_context_t ctx = c2m_ctx->ctx;
  MIR_item_t proto;
  char pname[64];

  collect_args_and_func_types (c2m_ctx, ft, NULL);
  sprintf (pname, "__methproto%d", new_proto_count++);
  proto = MIR_new_proto_arr (ctx, pname, VARR_LENGTH (MIR_type_t, proto_info.ret_types),
                             VARR_ADDR (MIR_type_t, proto_info.ret_types),
                             VARR_LENGTH (MIR_var_t, proto_info.arg_vars),
                             VARR_ADDR (MIR_var_t, proto_info.arg_vars));
  move_item_to_module_start (curr_func->module, proto);
  return proto;
}

/* Compute the MIR-level name for a function definition.

   Three name shapes, matching the original inline mangling in case N_FUNC_DEF:
     - Ordinary methods:   Class_method__<param-types>  (via build_method_mir_name)
     - Constructors/dtors: __ctor_Class__<params>_<uid>  (uid for overload uniqueness)
     - Free functions:     base_name verbatim

   Extracted so the pre-gen forward-declaration pass (gen_forward_class_methods)
   can compute the same names that N_FUNC_DEF gen will later use, ensuring the
   forward item and the eventual definition share a MIR symbol name (which is
   what lets MIR_finish_module resolve cross-class call refs that are emitted
   *before* the callee's class is processed in source order). */
/* TRUE for any monomorphized generic specialization's MIR name: a generic
   class specialization or one of its methods (`__generic_List_int`,
   `__generic_List_int_EnsureCapacity__i`, `__generic_QueryBuilder_User_...`),
   a generic free function (`__genfn_Max_intint`), or a generic method
   independent of class specialization (`__genmeth_...`). Every translation
   unit that instantiates a given specialization regenerates its own
   complete, identical copy from the shared template/header -- there is
   never a legitimate cross-module reference to *another* TU's copy, unlike
   ordinary extern-linkage functions/methods. See export_p check below. */
static int generic_specialization_mir_name_p (const char *nm) {
  return nm != NULL
         && (strncmp (nm, "__generic_", 10) == 0
             || strncmp (nm, "__genfn_", 8) == 0
             || strncmp (nm, "__genmeth_", 10) == 0);
}

static void mangle_func_def_mir_name (c2m_ctx_t c2m_ctx, node_t func_def,
                                       char *out, size_t outsz) {
  node_t declarator = FUNC_DEF_DECL (func_def);
  node_t id = DECL_ID (declarator);
  const char *base_name = id->u.s.s;
  decl_t func_decl = func_def->attr;
  struct type *decl_type = func_decl->decl_spec.type;
  struct func_type *ft = decl_type->u.func_type;
  int is_method = FALSE;
  const char *class_name = NULL;

  if (ft != NULL && ft->class_scope != NULL && ft->class_scope->code == N_CLASS) {
    node_t class_id = TAG_ID (ft->class_scope);
    if (class_id != NULL && class_id->code == N_ID) {
      is_method = TRUE;
      class_name = class_id->u.s.s;
    }
  }

  if (is_method && class_name != NULL
      && strncmp (base_name, "__ctor_", 7) != 0
      && strncmp (base_name, "__dtor_", 7) != 0) {
    build_method_mir_name (c2m_ctx, out, outsz, class_name, base_name, ft);
  } else if (is_method && class_name != NULL) {
    /* Ctor/dtor: base name + "__" + param-type-suffix + UID. */
    MIR_alloc_t alloc = c2m_alloc (c2m_ctx);
    VARR (char) * b;
    node_t param;
    int any = FALSE;
    VARR_CREATE (char, b, alloc, 128);
    for (const char *p = base_name; *p != '\0'; p++) VARR_PUSH (char, b, *p);
    VARR_PUSH (char, b, '_'); VARR_PUSH (char, b, '_');
    param = (ft != NULL) ? NL_HEAD (ft->param_list->u.ops) : NULL;
    if (param_is_this_p (param)) param = NL_NEXT (param);
    for (; param != NULL; param = NL_NEXT (param)) {
      struct decl_spec *ds;
      if (param->code != N_SPEC_DECL) continue;
      if (void_param_p (param)) continue;
      ds = get_param_decl_spec (param);
      if (ds == NULL) continue;
      append_type_mangle (c2m_ctx, b, ds->type);
      any = TRUE;
    }
    if (!any) VARR_PUSH (char, b, 'v');
    {
      char uid_buf[24];
      snprintf (uid_buf, sizeof uid_buf, "_%u", func_def->uid);
      for (const char *p = uid_buf; *p != '\0'; p++) VARR_PUSH (char, b, *p);
    }
    VARR_PUSH (char, b, '\0');
    snprintf (out, outsz, "%s", VARR_ADDR (char, b));
    VARR_DESTROY (char, b);
  } else {
    snprintf (out, outsz, "%s", base_name);
  }
}

/* Pre-gen forward declarations for every class method, constructor, and
   destructor in the translation unit.  Run once at the entry of gen_mir,
   before the main top_gen() recursion.

   Why this exists: with the check-phase cross-class fix, a method body in
   class A may legally call `new B(...)` or `bptr->m()` even when class B is
   declared *after* A in source order.  At gen time, A's body is emitted
   first and tries to reference B's method/ctor MIR item, which has not been
   created yet -- the emitted ref op carries u.ref == NULL, and the runtime
   then segfaults dereferencing the null pointer.

   The fix is to create a MIR_new_forward item per class method up front and
   stash it in `decl->u.item`.  All subsequent ref ops emit a non-null pointer
   to the forward item.  When N_FUNC_DEF gen later calls MIR_new_func with the
   same mangled name, the real definition is created and assigned to
   `decl->u.item`; refs emitted afterwards point straight at the real item,
   refs emitted earlier point at the forward.  MIR_finish_module / link then
   resolves all forward items by name to the real definitions, so both shapes
   of ref reach the same code at runtime.

   We deliberately limit this to class methods only (`ft->class_scope != NULL`)
   so free functions still follow ordinary C ordering semantics -- a free
   function called before its definition still needs an explicit prototype,
   as in C11. */
static void gen_forward_methods_for_class (c2m_ctx_t c2m_ctx, node_t class_node) {
  MIR_context_t ctx = c2m_ctx->ctx;
  node_t class_id, decl_list;

  if (class_node == NULL || class_node->code != N_CLASS) return;
  /* Skip generic class templates -- only their concrete specializations
     generate code.  Sentinel attr matches the marker set in type_spec. */
  if (class_node->attr == (void *) ((intptr_t) -1)) return;

  class_id = NL_HEAD (class_node->u.ops);
  if (class_id == NULL || class_id->code != N_ID) return;
  decl_list = NL_NEXT (class_id);
  if (decl_list == NULL || decl_list->code != N_LIST) return;

  for (node_t m = NL_HEAD (decl_list->u.ops); m != NULL; m = NL_NEXT (m)) {
    if (m->code != N_FUNC_DEF) continue;
    /* Template / open generic-method sentinels are not real decl_t attrs. */
    if (m->attr == NULL || m->attr == (void *) ((intptr_t) -1)) continue;
    decl_t mdecl = m->attr;
    if (mdecl->midopt_dead_p) continue; /* midopt P0: no forward for dead methods */
    if (mdecl->u.item != NULL) continue;  /* already (forward-)declared */
    struct type *mtype = mdecl->decl_spec.type;
    if (mtype == NULL || mtype->mode != TM_FUNC) continue;

    char fname[256] = {0};
    mangle_func_def_mir_name (c2m_ctx, m, fname, sizeof fname);
    mdecl->u.item = MIR_new_forward (ctx, fname);
  }
}

static void gen_forward_class_methods (c2m_ctx_t c2m_ctx, node_t module) {
  node_t items;

  if (module == NULL || module->code != N_MODULE) return;
  items = NL_HEAD (module->u.ops);
  if (items == NULL || items->code != N_LIST) return;

  for (node_t it = NL_HEAD (items->u.ops); it != NULL; it = NL_NEXT (it)) {
    /* Source classes live under N_SPEC_DECL; parse-injected generic
       specializations (__generic_Host_int etc.) are bare N_CLASS items. */
    if (it->code == N_CLASS) {
      gen_forward_methods_for_class (c2m_ctx, it);
      continue;
    }
    if (it->code != N_SPEC_DECL) continue;
    node_t specs = NL_HEAD (it->u.ops);
    if (specs == NULL) continue;
    if (specs->code == N_SHARE) specs = NL_HEAD (specs->u.ops);
    if (specs == NULL || specs->code != N_LIST) continue;

    for (node_t s = NL_HEAD (specs->u.ops); s != NULL; s = NL_NEXT (s))
      if (s->code == N_CLASS) gen_forward_methods_for_class (c2m_ctx, s);
  }
}

/* Emit a call FUNC_OP(args...) through PROTO for a function of type FT, where
   FUNC_OP is a func-item ref or a function address value and ARGS holds the
   already-evaluated argument values for every parameter (including 'this' for
   methods).  Scalar args are promoted/cast to the parameter types; aggregate
   args must already be memory ops.  Returns the call result (a meaningless op
   for void functions). */
static op_t gen_funcptr_call (c2m_ctx_t c2m_ctx, MIR_item_t proto, struct func_type *ft,
                              MIR_op_t func_op, op_t *args, int n_args, op_t *agg_dest,
                              int mir_inline_p) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_context_t ctx = c2m_ctx->ctx;
  size_t ops_start = VARR_LENGTH (MIR_op_t, call_ops);
  target_arg_info_t arg_info;
  node_t param;
  op_t res = zero_op;
  int i, n;

  int is_agg_ret = (ft->ret_type->mode == TM_STRUCT || ft->ret_type->mode == TM_UNION
                    || ft->ret_type->mode == TM_CLASS);
  int agg_by_addr_p = 0;
  int need_post_scatter_p = 0;
  VARR_PUSH (MIR_op_t, call_ops, MIR_new_ref_op (ctx, proto));
  VARR_PUSH (MIR_op_t, call_ops, func_op);
  target_init_arg_vars (c2m_ctx, &arg_info);
  if (agg_dest != NULL && is_agg_ret && target_return_by_addr_p (c2m_ctx, ft->ret_type)) {
    /* Aggregate returned via a hidden pointer: instead of routing through the
       call arg area (which the synthetic for-in/protocol call sites never sized
       during check), construct the result directly in the caller-supplied
       destination slot.  Push an RBLK operand pointing at &agg_dest. */
    op_t addr = mem_to_address (c2m_ctx, *agg_dest, TRUE);
    mir_size_t rsz = type_size (c2m_ctx, ft->ret_type);
    if (rsz == 0) rsz = 1;
    MIR_op_t rblk = MIR_new_mem_op (ctx, MIR_T_RBLK, rsz, addr.mir_op.u.reg, 0, 1);
    VARR_PUSH (MIR_op_t, call_ops, rblk);
    res = *agg_dest;
    n = 0;
    agg_by_addr_p = 1;
  } else {
    n = target_add_call_res_op (c2m_ctx, ft->ret_type, &arg_info, 0);
    if (n == 0) {
      /* RBLK return (classes always; large structs).  Expose the buffer as a
         plain MEM op.  Previously we left res as zero_op when agg_dest was
         NULL — MIR then saw a garbage/undeclared reg in the caller. */
      res = new_op (NULL, VARR_LAST (MIR_op_t, call_ops));
      assert (res.mir_op.mode == MIR_OP_MEM && res.mir_op.u.mem.type == MIR_T_RBLK);
      res.mir_op = MIR_new_mem_op (ctx, MIR_T_UNDEF, 0, res.mir_op.u.mem.base, 0, 1);
    } else if (n > 0 && is_agg_ret) {
      /* Small aggregate in registers: need a memory slot to scatter into. */
      if (agg_dest != NULL) {
        res = *agg_dest;
      } else {
        mir_size_t csize = type_size (c2m_ctx, ft->ret_type);
        if (csize == 0) csize = 1;
        res = get_new_temp (c2m_ctx, MIR_T_I64);
        MIR_append_insn (ctx, curr_func,
                         MIR_new_insn (ctx, MIR_ALLOCA, res.mir_op,
                                       MIR_new_int_op (ctx, (long long) csize)));
        res.mir_op = MIR_new_mem_op (ctx, MIR_T_UNDEF, 0, res.mir_op.u.reg, 0, 1);
      }
      need_post_scatter_p = 1;
    } else if (n > 0) {
      assert (n == 1);
      res = new_op (NULL, VARR_LAST (MIR_op_t, call_ops));
    }
  }
  param = NL_HEAD (ft->param_list->u.ops);
  for (i = 0; i < n_args; i++) {
    op_t av = args[i];
    struct decl_spec *pds = param != NULL ? get_param_decl_spec (param) : NULL;
    struct type *pt = pds != NULL ? pds->type : NULL;

    if (pt == NULL) { /* unprototyped callback: pass promoted scalar as-is */
      av = promote (c2m_ctx, av, MIR_T_I64, FALSE);
      VARR_PUSH (MIR_op_t, call_ops, av.mir_op);
      continue;
    }
    if (scalar_type_p (pt))
      av = promote (c2m_ctx, av, promote_mir_int_type (get_mir_type (c2m_ctx, pt)), FALSE);
    target_add_call_arg_op (c2m_ctx, pt, &arg_info, av);
    param = NL_NEXT (param);
  }
  {
    MIR_insn_t ci = MIR_new_insn_arr (ctx, mir_inline_p ? MIR_INLINE : MIR_CALL,
                                      VARR_LENGTH (MIR_op_t, call_ops) - ops_start,
                                      VARR_ADDR (MIR_op_t, call_ops) + ops_start);
    emit_insn (c2m_ctx, ci);
    if (need_post_scatter_p && !agg_by_addr_p) {
      /* Small aggregate returned in registers: scatter into res (agg_dest or
         a fresh ALLOCA slot). */
      target_gen_post_call_res_code (c2m_ctx, ft->ret_type, res, ci, ops_start);
    }
  }
  VARR_TRUNC (MIR_op_t, call_ops, ops_start);
  return res;
}

/* Look up a simple instance field by name on CLASS_TAG (N_CLASS). */
static decl_t find_class_field_by_name (node_t class_tag, const char *name) {
  node_t mlist, mem, md, mid;
  decl_t dd;

  if (class_tag == NULL || name == NULL || class_tag->code != N_CLASS) return NULL;
  mlist = TAG_MEMBER_LIST (class_tag);
  if (mlist == NULL || mlist->code != N_LIST) return NULL;
  for (mem = NL_HEAD (mlist->u.ops); mem != NULL; mem = NL_NEXT (mem)) {
    if (mem->code != N_MEMBER || mem->attr == NULL) continue;
    dd = (decl_t) mem->attr;
    md = MEMBER_DECL (mem);
    if (md == NULL || md->code != N_DECL) continue;
    mid = NL_HEAD (md->u.ops);
    if (mid != NULL && mid->code == N_ID && mid->u.s.s != NULL
        && strcmp (mid->u.s.s, name) == 0)
      return dd;
  }
  return NULL;
}

static const char *func_def_simple_name (node_t func_def) {
  node_t declarator, id;
  if (func_def == NULL || func_def->code != N_FUNC_DEF) return NULL;
  declarator = FUNC_DEF_DECL (func_def);
  if (declarator == NULL || declarator->code != N_DECL) return NULL;
  id = NL_HEAD (declarator->u.ops);
  if (id == NULL || id->code != N_ID) return NULL;
  return id->u.s.s;
}

/* Resolve dense buffer fields on CLASS_TAG:
     List:  data (T*) + length (int) [+ capacity]
     Set:   dense (T*) + count (int) [+ capacity]
     Map:   keys (K*) + vals (V*) + count (int)  — not a single-buffer layout
   Returns 1 if List/Set-style single buffer found; sets *arr_out / *len_out. */
static int find_dense_buffer_fields (node_t class_tag, decl_t *arr_out, decl_t *len_out,
                                     decl_t *cap_out) {
  decl_t data_f, dense_f, len_f, count_f, cap_f, keys_f, vals_f;
  decl_t arr_f, sz_f;

  if (arr_out) *arr_out = NULL;
  if (len_out) *len_out = NULL;
  if (cap_out) *cap_out = NULL;
  if (class_tag == NULL || class_tag->code != N_CLASS) return 0;

  data_f = find_class_field_by_name (class_tag, "data");
  dense_f = find_class_field_by_name (class_tag, "dense");
  len_f = find_class_field_by_name (class_tag, "length");
  count_f = find_class_field_by_name (class_tag, "count");
  cap_f = find_class_field_by_name (class_tag, "capacity");
  keys_f = find_class_field_by_name (class_tag, "keys");
  vals_f = find_class_field_by_name (class_tag, "vals");

  /* Map layout wins for KeyAt/ValAt path — not a single dense buffer. */
  if (count_f != NULL && keys_f != NULL && vals_f != NULL
      && keys_f->decl_spec.type != NULL && keys_f->decl_spec.type->mode == TM_PTR
      && vals_f->decl_spec.type != NULL && vals_f->decl_spec.type->mode == TM_PTR)
    return 0;

  arr_f = NULL;
  if (data_f != NULL && data_f->decl_spec.type != NULL && data_f->decl_spec.type->mode == TM_PTR
      && data_f->decl_spec.type->u.ptr_type != NULL)
    arr_f = data_f;
  else if (dense_f != NULL && dense_f->decl_spec.type != NULL
           && dense_f->decl_spec.type->mode == TM_PTR
           && dense_f->decl_spec.type->u.ptr_type != NULL)
    arr_f = dense_f;

  sz_f = NULL;
  if (len_f != NULL && len_f->decl_spec.type != NULL && len_f->decl_spec.type->mode == TM_BASIC)
    sz_f = len_f;
  else if (count_f != NULL && count_f->decl_spec.type != NULL
           && count_f->decl_spec.type->mode == TM_BASIC)
    sz_f = count_f;

  if (arr_f == NULL || sz_f == NULL) return 0;
  if (arr_out) *arr_out = arr_f;
  if (len_out) *len_out = sz_f;
  if (cap_out
      && cap_f != NULL && cap_f->decl_spec.type != NULL
      && cap_f->decl_spec.type->mode == TM_BASIC)
    *cap_out = cap_f;
  return 1;
}

/* Pure predicate: can FUNC_DEF's body be open-coded as a dense List/Set/Map
   accessor with N_ARGS user args?  No MIR; safe to call before evaluating
   the receiver (N_CALL path uses this to avoid gen_class_method_call fallback). */
static int dense_accessor_open_codeable_p (node_t func_def, int n_args) {
  decl_t mdecl;
  struct func_type *ft;
  node_t class_tag;
  const char *nm;
  decl_t data_f, len_f, cap_f, keys_f, vals_f, count_f;
  int list_p, map_p;

  if (func_def == NULL || func_def->code != N_FUNC_DEF) return 0;
  mdecl = func_def->attr;
  if (mdecl == NULL || mdecl->decl_spec.type == NULL
      || mdecl->decl_spec.type->mode != TM_FUNC)
    return 0;
  ft = mdecl->decl_spec.type->u.func_type;
  if (ft == NULL || ft->class_scope == NULL || ft->class_scope->code != N_CLASS) return 0;
  class_tag = ft->class_scope;
  nm = func_def_simple_name (func_def);
  if (nm == NULL) return 0;

  keys_f = find_class_field_by_name (class_tag, "keys");
  vals_f = find_class_field_by_name (class_tag, "vals");
  count_f = find_class_field_by_name (class_tag, "count");
  map_p = (count_f != NULL && count_f->decl_spec.type != NULL
           && count_f->decl_spec.type->mode == TM_BASIC && keys_f != NULL
           && keys_f->decl_spec.type != NULL && keys_f->decl_spec.type->mode == TM_PTR
           && vals_f != NULL && vals_f->decl_spec.type != NULL
           && vals_f->decl_spec.type->mode == TM_PTR
           && keys_f->decl_spec.type->u.ptr_type != NULL
           && vals_f->decl_spec.type->u.ptr_type != NULL);
  list_p = !map_p && find_dense_buffer_fields (class_tag, &data_f, &len_f, &cap_f);

  if (!list_p && !map_p) return 0;

  if (list_p) {
    if ((strcmp (nm, "Count") == 0 || strcmp (nm, "IsEmpty") == 0) && n_args == 0)
      return 1;
    if (strcmp (nm, "Capacity") == 0 && n_args == 0 && cap_f != NULL) return 1;
    if ((strcmp (nm, "Get") == 0 || strcmp (nm, "GetMut") == 0) && n_args == 1) {
      struct type *el = data_f->decl_spec.type->u.ptr_type;
      return (el->mode != TM_CLASS && el->mode != TM_STRUCT && el->mode != TM_UNION);
    }
    if ((strcmp (nm, "First") == 0 || strcmp (nm, "Last") == 0
         || strcmp (nm, "FirstMut") == 0 || strcmp (nm, "LastMut") == 0)
        && n_args == 0) {
      struct type *el = data_f->decl_spec.type->u.ptr_type;
      int mut = (strcmp (nm, "FirstMut") == 0 || strcmp (nm, "LastMut") == 0);
      /* FirstMut/LastMut return T* — OK for class/struct elements.
         First/Last load by value — only scalar/pointer elements. */
      return mut || (el->mode != TM_CLASS && el->mode != TM_STRUCT && el->mode != TM_UNION);
    }
    return 0;
  }
  /* map_p */
  if ((strcmp (nm, "Count") == 0 || strcmp (nm, "IsEmpty") == 0) && n_args == 0) return 1;
  if ((strcmp (nm, "KeyAt") == 0 || strcmp (nm, "ValAt") == 0 || strcmp (nm, "ValMut") == 0)
      && n_args == 1) {
    struct type *el = (strcmp (nm, "KeyAt") == 0) ? keys_f->decl_spec.type->u.ptr_type
                                                  : vals_f->decl_spec.type->u.ptr_type;
    if (strcmp (nm, "ValMut") == 0) return 1;
    return (el->mode != TM_CLASS && el->mode != TM_STRUCT && el->mode != TM_UNION);
  }
  return 0;
}

/* Open-code dense collection accessors when layout is known:
     List/Set:  length + data (+ capacity) → Count/IsEmpty/Capacity/Get/GetMut/
                First/Last/FirstMut/LastMut (scalar/pointer elements only)
     Map:       count + keys + vals       → Count/IsEmpty/KeyAt/ValAt/ValMut
                (scalar/pointer K,V only for by-value loads)
   SAFE_FLAGS: GEN_SAFE_SKIP_NULL / GEN_SAFE_SKIP_OOB when caller proved them.
   Returns 1 and sets *res_out on success; 0 with no MIR emitted on failure. */
static int try_open_code_dense_accessor (c2m_ctx_t c2m_ctx, node_t func_def, op_t this_op,
                                         op_t *args, int n_args, op_t *agg_dest MIR_UNUSED,
                                         op_t *res_out, int safe_flags) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_context_t ctx = c2m_ctx->ctx;
  decl_t mdecl;
  struct func_type *ft;
  node_t class_tag;
  const char *nm;
  decl_t data_f, len_f, cap_f, keys_f, vals_f, count_f;
  int list_p, map_p;
  MIR_alias_t calias;
  MIR_type_t ct;
  op_t this_r, count_v, base, idx, off, addr, el;

  if (res_out == NULL || !dense_accessor_open_codeable_p (func_def, n_args)) return 0;
  /* Indexed accessors need the index op. */
  if (n_args == 1 && args == NULL) return 0;

  mdecl = func_def->attr;
  ft = mdecl->decl_spec.type->u.func_type;
  class_tag = ft->class_scope;
  nm = func_def_simple_name (func_def);

  keys_f = find_class_field_by_name (class_tag, "keys");
  vals_f = find_class_field_by_name (class_tag, "vals");
  count_f = find_class_field_by_name (class_tag, "count");
  map_p = (count_f != NULL && keys_f != NULL && vals_f != NULL
           && keys_f->decl_spec.type != NULL && keys_f->decl_spec.type->mode == TM_PTR
           && vals_f->decl_spec.type != NULL && vals_f->decl_spec.type->mode == TM_PTR);
  list_p = !map_p && find_dense_buffer_fields (class_tag, &data_f, &len_f, &cap_f);

  this_r = force_reg (c2m_ctx, this_op, MIR_T_I64);
  if (c2m_options->exceptions_p && !(safe_flags & GEN_SAFE_SKIP_NULL))
    gen_null_check (c2m_ctx, this_r, (long) POS (func_def).lno);

  if (list_p) {
    decl_t cnt_f = len_f;
    ct = get_mir_type (c2m_ctx, cnt_f->decl_spec.type);
    calias = get_type_alias (c2m_ctx, cnt_f->decl_spec.type);

    if (strcmp (nm, "Count") == 0) {
      *res_out = get_new_temp (c2m_ctx, promote_mir_int_type (ct));
      emit2 (c2m_ctx, tp_mov (ct), res_out->mir_op,
             MIR_new_alias_mem_op (ctx, ct, (MIR_disp_t) cnt_f->offset, this_r.mir_op.u.reg, 0, 1,
                                   calias, 0));
      return 1;
    }
    if (strcmp (nm, "IsEmpty") == 0) {
      count_v = get_new_temp (c2m_ctx, promote_mir_int_type (ct));
      emit2 (c2m_ctx, tp_mov (ct), count_v.mir_op,
             MIR_new_alias_mem_op (ctx, ct, (MIR_disp_t) cnt_f->offset, this_r.mir_op.u.reg, 0, 1,
                                   calias, 0));
      *res_out = get_new_temp (c2m_ctx, MIR_T_I64);
      emit3 (c2m_ctx, MIR_EQ, res_out->mir_op, count_v.mir_op, MIR_new_int_op (ctx, 0));
      return 1;
    }
    if (strcmp (nm, "Capacity") == 0) {
      MIR_type_t cpt = get_mir_type (c2m_ctx, cap_f->decl_spec.type);
      MIR_alias_t cpalias = get_type_alias (c2m_ctx, cap_f->decl_spec.type);
      *res_out = get_new_temp (c2m_ctx, promote_mir_int_type (cpt));
      emit2 (c2m_ctx, tp_mov (cpt), res_out->mir_op,
             MIR_new_alias_mem_op (ctx, cpt, (MIR_disp_t) cap_f->offset, this_r.mir_op.u.reg, 0, 1,
                                   cpalias, 0));
      return 1;
    }

    /* Indexed / first / last access into data[] */
    {
      struct type *el_type = data_f->decl_spec.type->u.ptr_type;
      mir_size_t el_size = type_size (c2m_ctx, el_type);
      MIR_type_t el_mir = get_mir_type (c2m_ctx, el_type);
      MIR_alias_t dalias = get_type_alias (c2m_ctx, data_f->decl_spec.type);
      int want_mut = (strcmp (nm, "GetMut") == 0 || strcmp (nm, "FirstMut") == 0
                      || strcmp (nm, "LastMut") == 0);
      int want_last = (strcmp (nm, "Last") == 0 || strcmp (nm, "LastMut") == 0);
      int want_first = (strcmp (nm, "First") == 0 || strcmp (nm, "FirstMut") == 0);

      count_v = get_new_temp (c2m_ctx, promote_mir_int_type (ct));
      emit2 (c2m_ctx, tp_mov (ct), count_v.mir_op,
             MIR_new_alias_mem_op (ctx, ct, (MIR_disp_t) cnt_f->offset, this_r.mir_op.u.reg, 0, 1,
                                   calias, 0));
      if (want_first || want_last) {
        /* Empty → OOB.  SKIP_OOB is not used for First/Last (no index proof). */
        if (c2m_options->exceptions_p) {
          MIR_label_t ok = MIR_new_label (ctx);
          emit3 (c2m_ctx, MIR_BGT, MIR_new_label_op (ctx, ok), count_v.mir_op,
                 MIR_new_int_op (ctx, 0));
          {
            MIR_op_t trap_args[3];
            safety_ensure_imports (c2m_ctx);
            trap_args[0] = MIR_new_int_op (ctx, 1);
            trap_args[1] = zero_op.mir_op;
            trap_args[2] = MIR_new_int_op (ctx, (long) POS (func_def).lno);
            gen_rt_call_void (c2m_ctx, safety_trap_proto, safety_trap_item, 3, trap_args);
          }
          emit_label_insn_opt (c2m_ctx, ok);
        }
        if (want_last) {
          idx = get_new_temp (c2m_ctx, MIR_T_I64);
          emit3 (c2m_ctx, MIR_SUB, idx.mir_op, count_v.mir_op, MIR_new_int_op (ctx, 1));
        } else {
          idx = get_new_temp (c2m_ctx, MIR_T_I64);
          emit2 (c2m_ctx, MIR_MOV, idx.mir_op, MIR_new_int_op (ctx, 0));
        }
      } else {
        idx = force_reg (c2m_ctx, args[0], MIR_T_I64);
        if (c2m_options->exceptions_p && !(safe_flags & GEN_SAFE_SKIP_OOB))
          gen_oob_check (c2m_ctx, idx, count_v.mir_op, (long) POS (func_def).lno);
      }
      base = get_new_temp (c2m_ctx, MIR_T_I64);
      emit2 (c2m_ctx, MIR_MOV, base.mir_op,
             MIR_new_alias_mem_op (ctx, MIR_T_I64, (MIR_disp_t) data_f->offset, this_r.mir_op.u.reg,
                                   0, 1, dalias, 0));
      off = get_new_temp (c2m_ctx, MIR_T_I64);
      addr = get_new_temp (c2m_ctx, MIR_T_I64);
      emit3 (c2m_ctx, MIR_MUL, off.mir_op, idx.mir_op, MIR_new_int_op (ctx, (long long) el_size));
      emit3 (c2m_ctx, MIR_ADD, addr.mir_op, base.mir_op, off.mir_op);
      if (want_mut) {
        *res_out = addr;
        return 1;
      }
      el = get_new_temp (c2m_ctx, promote_mir_int_type (el_mir));
      emit2 (c2m_ctx, tp_mov (el_mir), el.mir_op,
             MIR_new_mem_op (ctx, el_mir, 0, addr.mir_op.u.reg, 0, 1));
      *res_out = el;
      return 1;
    }
  }

  /* Map layout: count + keys + vals */
  {
    ct = get_mir_type (c2m_ctx, count_f->decl_spec.type);
    calias = get_type_alias (c2m_ctx, count_f->decl_spec.type);
    if (strcmp (nm, "Count") == 0) {
      *res_out = get_new_temp (c2m_ctx, promote_mir_int_type (ct));
      emit2 (c2m_ctx, tp_mov (ct), res_out->mir_op,
             MIR_new_alias_mem_op (ctx, ct, (MIR_disp_t) count_f->offset, this_r.mir_op.u.reg, 0, 1,
                                   calias, 0));
      return 1;
    }
    if (strcmp (nm, "IsEmpty") == 0) {
      count_v = get_new_temp (c2m_ctx, promote_mir_int_type (ct));
      emit2 (c2m_ctx, tp_mov (ct), count_v.mir_op,
             MIR_new_alias_mem_op (ctx, ct, (MIR_disp_t) count_f->offset, this_r.mir_op.u.reg, 0, 1,
                                   calias, 0));
      *res_out = get_new_temp (c2m_ctx, MIR_T_I64);
      emit3 (c2m_ctx, MIR_EQ, res_out->mir_op, count_v.mir_op, MIR_new_int_op (ctx, 0));
      return 1;
    }
    {
      int is_key = (strcmp (nm, "KeyAt") == 0);
      int is_mut = (strcmp (nm, "ValMut") == 0);
      decl_t arr_f = is_key ? keys_f : vals_f;
      struct type *el_type = arr_f->decl_spec.type->u.ptr_type;
      mir_size_t el_size = type_size (c2m_ctx, el_type);
      MIR_type_t el_mir = get_mir_type (c2m_ctx, el_type);
      MIR_alias_t aalias = get_type_alias (c2m_ctx, arr_f->decl_spec.type);

      count_v = get_new_temp (c2m_ctx, promote_mir_int_type (ct));
      emit2 (c2m_ctx, tp_mov (ct), count_v.mir_op,
             MIR_new_alias_mem_op (ctx, ct, (MIR_disp_t) count_f->offset, this_r.mir_op.u.reg, 0, 1,
                                   calias, 0));
      idx = force_reg (c2m_ctx, args[0], MIR_T_I64);
      if (c2m_options->exceptions_p && !(safe_flags & GEN_SAFE_SKIP_OOB))
        gen_oob_check (c2m_ctx, idx, count_v.mir_op, (long) POS (func_def).lno);
      base = get_new_temp (c2m_ctx, MIR_T_I64);
      emit2 (c2m_ctx, MIR_MOV, base.mir_op,
             MIR_new_alias_mem_op (ctx, MIR_T_I64, (MIR_disp_t) arr_f->offset, this_r.mir_op.u.reg,
                                   0, 1, aalias, 0));
      off = get_new_temp (c2m_ctx, MIR_T_I64);
      addr = get_new_temp (c2m_ctx, MIR_T_I64);
      emit3 (c2m_ctx, MIR_MUL, off.mir_op, idx.mir_op, MIR_new_int_op (ctx, (long long) el_size));
      emit3 (c2m_ctx, MIR_ADD, addr.mir_op, base.mir_op, off.mir_op);
      if (is_mut) {
        *res_out = addr;
        return 1;
      }
      el = get_new_temp (c2m_ctx, promote_mir_int_type (el_mir));
      emit2 (c2m_ctx, tp_mov (el_mir), el.mir_op,
             MIR_new_mem_op (ctx, el_mir, 0, addr.mir_op.u.reg, 0, 1));
      *res_out = el;
      return 1;
    }
  }
  return 0;
}

/* Emit a direct call THIS_OP->method(args...) for a check-resolved class
   method FUNC_DEF.  Used by the brace-init protocol (new T{...} → Add calls),
   the for-in Count/Get iteration protocol, and filter/map/reduce over class
   receivers, where no N_CALL node exists in the AST.  THIS_TYPE is the
   receiver pointer type, ARGS holds N_ARGS already-evaluated user argument
   values.  SAFE_FLAGS elides null/OOB when the caller has already proved them.
   Aggregate return types are rejected during check. */
#define GEN_METHOD_MAX_ARGS 8
/* R2 by-ref table: for-in element loop vars proven read-only by midopt are
   bound as pointers into the collection buffer instead of per-iteration
   block copies.  Push/pop is balanced around each N_FORIN's body gen. */
#define GEN_BYREF_MAX 32
#define GEN_BYREF_NOREG ((MIR_reg_t) -1)
static struct { decl_t d; MIR_reg_t reg; } gen_byref_tab[GEN_BYREF_MAX];
static int gen_byref_n = 0;

static void gen_byref_push (decl_t d, MIR_reg_t reg) {
  if (d == NULL || gen_byref_n >= GEN_BYREF_MAX) return;
  gen_byref_tab[gen_byref_n].d = d;
  gen_byref_tab[gen_byref_n].reg = reg;
  gen_byref_n++;
}

static MIR_reg_t gen_byref_find (decl_t d) {
  int i;
  if (d == NULL) return GEN_BYREF_NOREG;
  for (i = gen_byref_n - 1; i >= 0; i--)
    if (gen_byref_tab[i].d == d) return gen_byref_tab[i].reg;
  return GEN_BYREF_NOREG;
}

static op_t gen_class_method_call_dest (c2m_ctx_t c2m_ctx, node_t func_def,
                                        struct type *this_type MIR_UNUSED, op_t this_op,
                                        op_t *args, int n_args, op_t *agg_dest, int safe_flags) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_context_t ctx = c2m_ctx->ctx;
  decl_t mdecl;
  struct func_type *ft;
  MIR_item_t proto;
  op_t all_args[GEN_METHOD_MAX_ARGS + 1];
  op_t res = zero_op;

  assert (func_def != NULL && func_def->code == N_FUNC_DEF);
  mdecl = func_def->attr;
  assert (mdecl != NULL && mdecl->decl_spec.type != NULL
          && mdecl->decl_spec.type->mode == TM_FUNC);
  ft = mdecl->decl_spec.type->u.func_type;
  assert (ft != NULL);

  /* Prefer open-coded dense List/Set/Map accessors over a real call. */
  if (try_open_code_dense_accessor (c2m_ctx, func_def, this_op, args, n_args, agg_dest, &res,
                                    safe_flags))
    return res;

  proto = gen_func_proto_item (c2m_ctx, ft);

  assert (n_args <= GEN_METHOD_MAX_ARGS);
  /* One null check on the receiver at the call site (unless proven).  Method
     bodies treat `this` as DEREF_GUARD_SAFE so they do not re-check fields. */
  if (c2m_options->exceptions_p && !(safe_flags & GEN_SAFE_SKIP_NULL)) {
    this_op = force_reg (c2m_ctx, this_op, MIR_T_I64);
    gen_null_check (c2m_ctx, this_op, (long) POS (func_def).lno);
  }
  all_args[0] = this_op; /* 'this' is the first parameter of the method */
  for (int j = 0; j < n_args; j++) all_args[j + 1] = args[j];
  return gen_funcptr_call (c2m_ctx, proto, ft, MIR_new_ref_op (ctx, mdecl->u.item), all_args,
                           n_args + 1, agg_dest, mdecl->decl_spec.inline_p);
}

static op_t gen_class_method_call_flags (c2m_ctx_t c2m_ctx, node_t func_def,
                                         struct type *this_type, op_t this_op, op_t *args,
                                         int n_args, int safe_flags) {
  return gen_class_method_call_dest (c2m_ctx, func_def, this_type, this_op, args, n_args, NULL,
                                     safe_flags);
}

static op_t gen_class_method_call (c2m_ctx_t c2m_ctx, node_t func_def, struct type *this_type,
                                   op_t this_op, op_t *args, int n_args) {
  return gen_class_method_call_dest (c2m_ctx, func_def, this_type, this_op, args, n_args, NULL, 0);
}

/* ---- Sequence lambda methods: MIR lowering ----

   filter:  cap = len(recv); out = alloca(HDR + cap*elsz); k = 0;
            for i in [0,len): el = recv[i]; if (cb(el)) out[k++] = el;
            out.len = k
   map:     out = alloca(HDR + len*outsz); out.len = len;
            for i in [0,len): out[i] = cb(recv[i])
   reduce:  acc = init; for i in [0,len): acc = cb(acc, recv[i])
   count:   len(recv)

   recv is a C array (compile-time length), a slice (len in its header), or a
   class instance (len = Count(), element = Get(i)).  The alloca happens once,
   before the loop, in the enclosing function's frame. */
static op_t gen_seq_method_call (c2m_ctx_t c2m_ctx, node_t r, enum seq_method sm) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_context_t ctx = c2m_ctx->ctx;
  node_t field = NL_HEAD (r->u.ops);
  node_t arg_list = NL_NEXT (field);
  node_t obj = NL_HEAD (field->u.ops);
  struct expr *obj_e = obj->attr;
  struct type *obj_type = obj_e->type;
  struct seq_recv sr;
  struct type *this_type = NULL;
  op_t base = zero_op, this_reg = zero_op;
  op_t n_save = get_new_temp (c2m_ctx, MIR_T_I64);

  classify_seq_receiver (c2m_ctx, obj_type, POS (r), &sr);
  assert (sr.kind != SEQ_RECV_NONE);

  /* --- receiver: element base address (arrays/slices) or 'this' + length --- */
  if (sr.kind == SEQ_RECV_ARR) {
    op_t arr_op = gen (c2m_ctx, obj, NULL, NULL, FALSE, NULL, NULL);
    if (arr_op.mir_op.mode == MIR_OP_MEM) {
      base = mem_to_address (c2m_ctx, arr_op, TRUE);
    } else {
      base = get_new_temp (c2m_ctx, MIR_T_I64);
      emit2 (c2m_ctx, MIR_MOV, base.mir_op, arr_op.mir_op);
    }
    emit2 (c2m_ctx, MIR_MOV, n_save.mir_op,
           MIR_new_int_op (ctx, sr.static_len < 0 ? 0 : (long) sr.static_len));
  } else if (sr.kind == SEQ_RECV_SLICE) {
    op_t ptr = force_reg (c2m_ctx, val_gen (c2m_ctx, obj), MIR_T_I64);
    emit2 (c2m_ctx, MIR_MOV, n_save.mir_op,
           MIR_new_mem_op (ctx, MIR_T_I64, 0, ptr.mir_op.u.reg, 0, 1));
    base = get_new_temp (c2m_ctx, MIR_T_I64);
    emit3 (c2m_ctx, MIR_ADD, base.mir_op, ptr.mir_op, MIR_new_int_op (ctx, SLICE_HDR_SIZE));
  } else { /* SEQ_RECV_CLASS: evaluate the receiver pointer once */
    op_t cnt;
    this_reg = get_new_temp (c2m_ctx, MIR_T_I64);
    if (obj_type->mode == TM_PTR) {
      op_t cv = val_gen (c2m_ctx, obj);
      emit2 (c2m_ctx, MIR_MOV, this_reg.mir_op, cv.mir_op);
      this_type = obj_type;
    } else { /* class lvalue: use its address — never null */
      op_t cv = gen (c2m_ctx, obj, NULL, NULL, FALSE, NULL, NULL);
      if (cv.mir_op.mode == MIR_OP_MEM) cv = mem_to_address (c2m_ctx, cv, TRUE);
      emit2 (c2m_ctx, MIR_MOV, this_reg.mir_op, cv.mir_op);
      this_type = create_ptr_type (c2m_ctx, sr.cls_type);
    }
    /* One null check for pointer receivers; stack address is non-null. */
    {
      int seq_sf = GEN_SAFE_SKIP_NULL;
      if (obj_type->mode == TM_PTR && c2m_options->exceptions_p) {
        gen_null_check (c2m_ctx, force_reg (c2m_ctx, this_reg, MIR_T_I64), (long) POS (r).lno);
      }
      cnt = gen_class_method_call_flags (c2m_ctx, sr.count_def, this_type, this_reg, NULL, 0,
                                         seq_sf);
    }
    emit2 (c2m_ctx, MIR_MOV, n_save.mir_op, cnt.mir_op);
  }

  if (sm == SEQM_COUNT) return n_save;

  if (sm == SEQM_TOLIST) {
    /* Lower arr.ToList() to:  obj = malloc(sizeof(List<T>)); memset(obj,0);
       List::List(obj, base, n_save).  The element base is in `base` and the
       element count in `n_save` (set above for ARR/SLICE receivers).  The
       List<T>* result type and the resolved array-view constructor were
       computed during check (the ctor is stashed on the field node's expr). */
    struct expr *re = r->attr;
    struct expr *fe = field->attr;
    node_t ctor_def = fe != NULL ? fe->def_node : NULL;
    struct type *list_ptr_t = re != NULL ? re->type : NULL;
    struct type *class_type
      = (list_ptr_t != NULL && list_ptr_t->mode == TM_PTR) ? list_ptr_t->u.ptr_type : NULL;
    decl_t cdecl;
    struct func_type *ft;
    MIR_item_t proto;
    char pname[64];
    size_t ops_start;
    node_t param;
    mir_size_t csize;
    op_t obj;

    assert (ctor_def != NULL && class_type != NULL);
    csize = type_size (c2m_ctx, class_type);
    obj = gen_heap_alloc (c2m_ctx, csize == 0 ? 1 : csize);
    if (csize > 0) gen_memset (c2m_ctx, 0, obj.mir_op.u.reg, csize);

    cdecl = ctor_def->attr;
    ft = cdecl->decl_spec.type->u.func_type;
    collect_args_and_func_types (c2m_ctx, ft, NULL);
    sprintf (pname, "__tolistproto%d", new_proto_count++);
    proto = MIR_new_proto_arr (ctx, pname,
                               VARR_LENGTH (MIR_type_t, proto_info.ret_types),
                               VARR_ADDR (MIR_type_t, proto_info.ret_types),
                               VARR_LENGTH (MIR_var_t, proto_info.arg_vars),
                               VARR_ADDR (MIR_var_t, proto_info.arg_vars));
    move_item_to_module_start (curr_func->module, proto);

    ops_start = VARR_LENGTH (MIR_op_t, call_ops);
    VARR_PUSH (MIR_op_t, call_ops, MIR_new_ref_op (ctx, proto));
    VARR_PUSH (MIR_op_t, call_ops, MIR_new_ref_op (ctx, cdecl->u.item));
    {
      target_arg_info_t new_arg_info;
      struct decl_spec *pds;
      target_init_arg_vars (c2m_ctx, &new_arg_info);
      /* 'this' */
      target_add_call_arg_op (c2m_ctx, list_ptr_t, &new_arg_info, obj);
      param = NL_HEAD (ft->param_list->u.ops);
      if (param != NULL) param = NL_NEXT (param); /* skip 'this' */
      /* items: T* — pass the element base address directly */
      pds = param != NULL ? get_param_decl_spec (param) : NULL;
      target_add_call_arg_op (c2m_ctx, pds != NULL ? pds->type : list_ptr_t, &new_arg_info, base);
      if (param != NULL) param = NL_NEXT (param);
      /* count: int — promote the I64 length register to the parameter type */
      pds = param != NULL ? get_param_decl_spec (param) : NULL;
      {
        op_t cnt_arg = n_save;
        if (pds != NULL)
          cnt_arg = promote (c2m_ctx, n_save,
                             promote_mir_int_type (get_mir_type (c2m_ctx, pds->type)), FALSE);
        target_add_call_arg_op (c2m_ctx, pds != NULL ? pds->type : NULL, &new_arg_info, cnt_arg);
      }
    }
    emit_insn (c2m_ctx,
               MIR_new_insn_arr (ctx, MIR_CALL, VARR_LENGTH (MIR_op_t, call_ops) - ops_start,
                                 VARR_ADDR (MIR_op_t, call_ops) + ops_start));
    VARR_TRUNC (MIR_op_t, call_ops, ops_start);
    return obj;
  }

  struct type *el_type = sr.el_type;
  MIR_type_t el_mir_t = get_mir_type (c2m_ctx, el_type);
  MIR_type_t el_reg_t = promote_mir_int_type (el_mir_t);
  mir_size_t el_size = type_size (c2m_ctx, el_type);

  /* --- callback: resolve its type, build its proto, evaluate it once --- */
  node_t cb_node = sm == SEQM_REDUCE ? NL_EL (arg_list->u.ops, 1) : NL_HEAD (arg_list->u.ops);
  struct expr *cb_e = cb_node->attr;
  struct func_type *cb_ft = cb_e->type->mode == TM_PTR && cb_e->type->u.ptr_type->mode == TM_FUNC
                              ? cb_e->type->u.ptr_type->u.func_type
                              : cb_e->type->u.func_type;
  MIR_item_t cb_proto = gen_func_proto_item (c2m_ctx, cb_ft);
  op_t cb_addr = val_gen (c2m_ctx, cb_node);

  /* --- output slice (filter/map) / accumulator (reduce) --- */
  op_t res_ptr = zero_op, k_reg = zero_op, acc = zero_op;
  struct type *out_type = el_type;
  MIR_type_t out_mir_t = el_mir_t, out_reg_t = el_reg_t;
  mir_size_t out_size = el_size;
  MIR_type_t acc_mir_t = MIR_T_I64;

  if (sm == SEQM_MAP) {
    out_type = cb_ft->ret_type;
    out_mir_t = get_mir_type (c2m_ctx, out_type);
    out_reg_t = promote_mir_int_type (out_mir_t);
    out_size = type_size (c2m_ctx, out_type);
  }
  if (sm == SEQM_FILTER || sm == SEQM_MAP) {
    op_t bytes = get_new_temp (c2m_ctx, MIR_T_I64);
    emit3 (c2m_ctx, MIR_MUL, bytes.mir_op, n_save.mir_op, MIR_new_int_op (ctx, (long) out_size));
    emit3 (c2m_ctx, MIR_ADD, bytes.mir_op, bytes.mir_op, MIR_new_int_op (ctx, SLICE_HDR_SIZE));
    res_ptr = get_new_temp (c2m_ctx, MIR_T_I64);
    emit2 (c2m_ctx, MIR_ALLOCA, res_ptr.mir_op, bytes.mir_op);
    k_reg = get_new_temp (c2m_ctx, MIR_T_I64);
    emit2 (c2m_ctx, MIR_MOV, k_reg.mir_op, MIR_new_int_op (ctx, 0));
  } else { /* SEQM_REDUCE: acc = init */
    node_t init_node = NL_HEAD (arg_list->u.ops);
    struct expr *ie = init_node->attr;
    op_t iv;

    acc_mir_t = promote_mir_int_type (get_mir_type (c2m_ctx, ie->type));
    iv = promote (c2m_ctx, val_gen (c2m_ctx, init_node), acc_mir_t, FALSE);
    acc = get_new_temp (c2m_ctx, acc_mir_t);
    emit2 (c2m_ctx, tp_mov (acc_mir_t), acc.mir_op, iv.mir_op);
  }

  /* --- loop --- */
  MIR_label_t loop_label = MIR_new_label (ctx), end_label = MIR_new_label (ctx);
  op_t i_reg = get_new_temp (c2m_ctx, MIR_T_I64);

  emit2 (c2m_ctx, MIR_MOV, i_reg.mir_op, MIR_new_int_op (ctx, 0));
  emit_label_insn_opt (c2m_ctx, loop_label);
  emit3 (c2m_ctx, MIR_BGE, MIR_new_label_op (ctx, end_label), i_reg.mir_op, n_save.mir_op);

  /* el = recv[i]  — for class: i proven in [0,n) by loop; this already checked. */
  op_t el_op;
  if (sr.kind == SEQ_RECV_CLASS) {
    el_op = gen_class_method_call_flags (c2m_ctx, sr.get_def, this_type, this_reg, &i_reg, 1,
                                         GEN_SAFE_SKIP_NULL | GEN_SAFE_SKIP_OOB);
  } else {
    op_t addr = get_new_temp (c2m_ctx, MIR_T_I64);
    emit3 (c2m_ctx, MIR_MUL, addr.mir_op, i_reg.mir_op, MIR_new_int_op (ctx, (long) el_size));
    emit3 (c2m_ctx, MIR_ADD, addr.mir_op, addr.mir_op, base.mir_op);
    el_op = get_new_temp (c2m_ctx, el_reg_t);
    emit2 (c2m_ctx, tp_mov (el_reg_t), el_op.mir_op,
           MIR_new_mem_op (ctx, el_mir_t, 0, addr.mir_op.u.reg, 0, 1));
  }

  switch (sm) {
  case SEQM_FILTER: {
    op_t keep = gen_funcptr_call (c2m_ctx, cb_proto, cb_ft, cb_addr.mir_op, &el_op, 1, NULL, 0);
    MIR_label_t skip_label = MIR_new_label (ctx);
    op_t daddr = get_new_temp (c2m_ctx, MIR_T_I64);

    emit3 (c2m_ctx, MIR_BEQ, MIR_new_label_op (ctx, skip_label), keep.mir_op,
           MIR_new_int_op (ctx, 0));
    emit3 (c2m_ctx, MIR_MUL, daddr.mir_op, k_reg.mir_op, MIR_new_int_op (ctx, (long) el_size));
    emit3 (c2m_ctx, MIR_ADD, daddr.mir_op, daddr.mir_op, res_ptr.mir_op);
    emit2 (c2m_ctx, tp_mov (el_reg_t),
           MIR_new_mem_op (ctx, el_mir_t, SLICE_HDR_SIZE, daddr.mir_op.u.reg, 0, 1),
           el_op.mir_op);
    emit3 (c2m_ctx, MIR_ADD, k_reg.mir_op, k_reg.mir_op, MIR_new_int_op (ctx, 1));
    emit_label_insn_opt (c2m_ctx, skip_label);
    break;
  }
  case SEQM_MAP: {
    op_t v = gen_funcptr_call (c2m_ctx, cb_proto, cb_ft, cb_addr.mir_op, &el_op, 1, NULL, 0);
    op_t daddr = get_new_temp (c2m_ctx, MIR_T_I64);

    emit3 (c2m_ctx, MIR_MUL, daddr.mir_op, i_reg.mir_op, MIR_new_int_op (ctx, (long) out_size));
    emit3 (c2m_ctx, MIR_ADD, daddr.mir_op, daddr.mir_op, res_ptr.mir_op);
    emit2 (c2m_ctx, tp_mov (out_reg_t),
           MIR_new_mem_op (ctx, out_mir_t, SLICE_HDR_SIZE, daddr.mir_op.u.reg, 0, 1), v.mir_op);
    break;
  }
  case SEQM_REDUCE: {
    op_t cb_args[2], v;

    cb_args[0] = acc;
    cb_args[1] = el_op;
    v = gen_funcptr_call (c2m_ctx, cb_proto, cb_ft, cb_addr.mir_op, cb_args, 2, NULL, 0);
    emit2 (c2m_ctx, tp_mov (acc_mir_t), acc.mir_op,
           promote (c2m_ctx, v, acc_mir_t, FALSE).mir_op);
    break;
  }
  default: break;
  }

  emit3 (c2m_ctx, MIR_ADD, i_reg.mir_op, i_reg.mir_op, MIR_new_int_op (ctx, 1));
  emit1 (c2m_ctx, MIR_JMP, MIR_new_label_op (ctx, loop_label));
  emit_label_insn_opt (c2m_ctx, end_label);

  if (sm == SEQM_REDUCE) return acc;
  /* store the final element count into the slice header */
  emit2 (c2m_ctx, MIR_MOV, MIR_new_mem_op (ctx, MIR_T_I64, 0, res_ptr.mir_op.u.reg, 0, 1),
         sm == SEQM_FILTER ? k_reg.mir_op : n_save.mir_op);
  return res_ptr;
}

/* ── Midopt R-LICM memo (loop-invariant pure-call hoist) ──
   The N_FOR gen emits its condition twice: as a pre-header guard (runs once at
   entry) and at the loop bottom (runs every iteration).  When midopt proves a
   bound call like `recv.Count()` invariant, gen evaluates it on the first
   (pre-header) emission and memoizes the result op here; the loop-bottom
   emission reuses it instead of re-calling.  Temp reg names increase
   monotonically within a function body (top_gen never resets reg_free_mark),
   so the cached reg survives and its pre-header def dominates the reuse. */
static int gen_hoist_lookup (c2m_ctx_t c2m_ctx, node_t call, op_t *out) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  int i;
  for (i = 0; i < hoist_n; i++)
    if (hoist_nodes[i] == call) { *out = hoist_ops[i]; return 1; }
  return 0;
}

static void gen_hoist_store (c2m_ctx_t c2m_ctx, node_t call, op_t op) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  int i;
  for (i = 0; i < hoist_n; i++)
    if (hoist_nodes[i] == call) return;   /* already memoized */
  if (hoist_n >= GEN_HOIST_MAX) return;   /* cache full: skip (correctness-safe) */
  hoist_nodes[hoist_n] = call;
  hoist_ops[hoist_n] = op;
  hoist_n++;
}

/* ── P0: class prvalue temporary destruction ─────────────────────────────
   `ClassName(args)` value temporaries live in the reusable call-arg area
   and were never destroyed: `take(Box(7))` and `Pt(1,2).getX()` leaked any
   resource the temp owned (BY-VALUE.md P0, refined: Add/brace-init were
   already balanced — the container adopts the temp's bits).

   Fix: when a call consumes a class prvalue (as a by-value argument or as
   the receiver of a method call), emit `~T` for the temp right after the
   call.  Two deliberate exceptions (temp kept alive, as before):
     - adopting protocol methods (List/Set/Map Add/Set/Insert/Push/Enqueue
       on a move-only collection): the container now owns the value and its
       dtor will run __destroy — destroying the temp too would double-free;
     - calls returning a pointer (e.g. `Owns(1).str()`, `p.withX(3)`
       chaining): the result may alias the temp's storage. */

/* TRUE iff NODE is a `ClassName(args)` value-construction call producing a
   stack temporary (same guard as the prvalue gen in case N_CALL). */
static int class_prvalue_call_p (node_t n) {
  struct expr *e;
  if (n == NULL || n->code != N_CALL) return FALSE;
  e = n->attr;
  return (e != NULL && e->builtin_call_p && e->type != NULL && e->type->mode == TM_CLASS
          && NL_HEAD (n->u.ops) != NULL && NL_HEAD (n->u.ops)->code == N_ID);
}

/* Emit `__dtor_<Class>(addr)` for a class prvalue temporary. */
static void gen_class_temp_dtor (c2m_ctx_t c2m_ctx, struct type *class_type, op_t addr) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_context_t ctx = c2m_ctx->ctx;
  node_t ddef;
  decl_t dd;
  struct func_type *dft;
  MIR_item_t dproto;
  char dpname[64];
  size_t dops;

  if (class_type == NULL || class_type->mode != TM_CLASS) return;
  ddef = find_class_dtor_def (c2m_ctx, class_type->u.tag_type);
  if (ddef == NULL || ddef->code != N_FUNC_DEF || ddef->attr == NULL) return;
  dd = ddef->attr;
  if (dd->u.item == NULL) return;
  dft = dd->decl_spec.type->u.func_type;
  collect_args_and_func_types (c2m_ctx, dft, NULL);
  sprintf (dpname, "__tempdtorproto%d", new_proto_count++);
  dproto = MIR_new_proto_arr (ctx, dpname,
                              VARR_LENGTH (MIR_type_t, proto_info.ret_types),
                              VARR_ADDR (MIR_type_t, proto_info.ret_types),
                              VARR_LENGTH (MIR_var_t, proto_info.arg_vars),
                              VARR_ADDR (MIR_var_t, proto_info.arg_vars));
  move_item_to_module_start (curr_func->module, dproto);
  dops = VARR_LENGTH (MIR_op_t, call_ops);
  VARR_PUSH (MIR_op_t, call_ops, MIR_new_ref_op (ctx, dproto));
  VARR_PUSH (MIR_op_t, call_ops, MIR_new_ref_op (ctx, dd->u.item));
  VARR_PUSH (MIR_op_t, call_ops, addr.mir_op);
  emit_insn (c2m_ctx,
             MIR_new_insn_arr (ctx, MIR_CALL,
                               VARR_LENGTH (MIR_op_t, call_ops) - dops,
                               VARR_ADDR (MIR_op_t, call_ops) + dops));
  VARR_TRUNC (MIR_op_t, call_ops, dops);
}

static op_t gen (c2m_ctx_t c2m_ctx, node_t r, MIR_label_t true_label, MIR_label_t false_label,
                 int val_p, op_t *desirable_dest, int *expect_res) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  check_ctx_t check_ctx = c2m_ctx->check_ctx; /* check and gen share curr_scope */
  MIR_context_t ctx = c2m_ctx->ctx;
  op_t res, op1, op2, op3, var, val;
  MIR_type_t t = MIR_T_UNDEF; /* to remove an uninitialized warning */
  MIR_insn_code_t insn_code;
  MIR_type_t mir_type;
  struct expr *e = NULL; /* to remove an uninitialized warning */
  struct type *type;
  decl_t decl;
  long double ld;
  long long ll;
  unsigned long long ull;
  int expr_attr_p, stmt_p;

  /* Update source location from the AST node for debug info (only with -g) */
  if (c2m_options->debug_info_p) {
    pos_t p = POS (r);
    if (p.lno > 0 && p.fname != NULL) {
      MIR_module_t mod = DLIST_TAIL (MIR_module_t, *MIR_get_module_list (ctx));
      if (mod != NULL) {
        curr_src_file_id = MIR_module_add_source_file (ctx, mod, p.fname);
        curr_src_line = (uint32_t) p.lno;
        curr_src_col = (uint16_t) (p.ln_pos > 0 ? p.ln_pos : 0);
      }
    }
  }
  classify_node (r, &expr_attr_p, &stmt_p);
  assert ((true_label == NULL && false_label == NULL && expect_res == NULL)
          || (true_label != NULL && false_label != NULL));
  assert (!val_p || desirable_dest == NULL);
  if (expect_res != NULL) *expect_res = 0; /* no expected result */
  /* Midopt R-LICM: reuse a proven loop-invariant pure call's pre-header value
     (value context only — the cond gens its operands as values). */
  if (r->code == N_CALL && true_label == NULL && false_label == NULL) {
    struct expr *ce = r->attr;
    if (ce != NULL && ce->hoist_call_p && gen_hoist_lookup (c2m_ctx, r, &res)) return res;
  }
  if (r->code != N_ANDAND && r->code != N_OROR && expr_attr_p && push_const_val (c2m_ctx, r, &res))
    goto finish;
  switch (r->code) {
  case N_LIST:
    for (node_t n = NL_HEAD (r->u.ops); n != NULL; n = NL_NEXT (n))
      gen (c2m_ctx, n, true_label, false_label, val_p, NULL, expect_res);
    break;
  case N_IGNORE: break; /* do nothing */
  case N_I:
  case N_L: ll = r->u.l; goto int_val;
  case N_LL:
    ll = r->u.ll;
  int_val:
    res = new_op (NULL, MIR_new_int_op (ctx, ll));
    break;
  case N_U:
  case N_UL: ull = r->u.ul; goto uint_val;
  case N_ULL:
    ull = r->u.ull;
  uint_val:
    res = new_op (NULL, MIR_new_uint_op (ctx, ull));
    break;
  case N_F: ld = r->u.f; goto float_val;
  case N_D: ld = r->u.d; goto float_val;
  case N_LD:
    ld = r->u.ld;
  float_val:
    mir_type = get_mir_type (c2m_ctx, ((struct expr *) r->attr)->type);
    res = new_op (NULL, (mir_type == MIR_T_F   ? MIR_new_float_op (ctx, (float) ld)
                         : mir_type == MIR_T_D ? MIR_new_double_op (ctx, ld)
                                               : MIR_new_ldouble_op (ctx, ld)));
    break;
  case N_CH: ll = r->u.ch; goto int_val;
  case N_CH16:
  case N_CH32: ll = r->u.ul; goto int_val;
  case N_STR16:
  case N_STR32: res = new_op (NULL, MIR_new_ref_op (ctx, get_string_data (c2m_ctx, r))); break;
  case N_STR:
  case N_STRING: // TODO: handle fstring
    res
      = new_op (NULL,
                MIR_new_str_op (ctx, (MIR_str_t){r->u.s.len, r->u.s.s}));  //???what to do with decl
                                                                           // and str in initializer
    break;
  case N_COMMA:
    gen (c2m_ctx, NL_HEAD (r->u.ops), NULL, NULL, FALSE, NULL, NULL);
    res = gen (c2m_ctx, NL_EL (r->u.ops, 1), true_label, false_label,
               true_label == NULL && !void_type_p (((struct expr *) r->attr)->type), NULL,
               expect_res);
    if (true_label != NULL) {
      true_label = false_label = NULL;
      val_p = FALSE;
    }
    break;
  case N_ANDAND:
  case N_OROR:
    if (!push_const_val (c2m_ctx, r, &res)) {
      MIR_label_t temp_label = MIR_new_label (ctx), t_label = true_label, f_label = false_label;
      int make_val_p = t_label == NULL;

      if (make_val_p) {
        t_label = MIR_new_label (ctx);
        f_label = MIR_new_label (ctx);
      }
      assert (t_label != NULL && f_label != NULL);
      gen (c2m_ctx, NL_HEAD (r->u.ops), r->code == N_ANDAND ? temp_label : t_label,
           r->code == N_ANDAND ? f_label : temp_label, FALSE, NULL, NULL);
      emit_label_insn_opt (c2m_ctx, temp_label);
      gen (c2m_ctx, NL_EL (r->u.ops, 1), t_label, f_label, FALSE, NULL, NULL);
      if (make_val_p) make_cond_val (c2m_ctx, r, t_label, f_label, &res);
      true_label = false_label = NULL;
    } else if (true_label != NULL) {
      int true_p;

      assert (res.mir_op.mode == MIR_OP_INT || res.mir_op.mode == MIR_OP_UINT
              || res.mir_op.mode == MIR_OP_FLOAT || res.mir_op.mode == MIR_OP_DOUBLE);
      true_p = ((res.mir_op.mode == MIR_OP_INT && res.mir_op.u.i != 0)
                || (res.mir_op.mode == MIR_OP_UINT && res.mir_op.u.u != 0)
                || (res.mir_op.mode == MIR_OP_FLOAT && res.mir_op.u.f != 0.0f)
                || (res.mir_op.mode == MIR_OP_DOUBLE && res.mir_op.u.d != 0.0));
      emit1 (c2m_ctx, MIR_JMP, MIR_new_label_op (ctx, true_p ? true_label : false_label));
      true_label = false_label = NULL;
    }
    break;
  case N_BITWISE_NOT:
    gen_unary_op (c2m_ctx, r, &op1, &res);
    t = get_mir_type (c2m_ctx, ((struct expr *) r->attr)->type);
    emit3 (c2m_ctx, t == MIR_T_I64 || t == MIR_T_U64 ? MIR_XOR : MIR_XORS, res.mir_op, op1.mir_op,
           minus_one_op.mir_op);
    break;
  case N_NOT:
    if (true_label != NULL) {
      gen (c2m_ctx, NL_HEAD (r->u.ops), false_label, true_label, FALSE, NULL, NULL);
      true_label = false_label = NULL;
    } else {
      MIR_label_t end_label = MIR_new_label (ctx);
      MIR_label_t t_label = MIR_new_label (ctx), f_label = MIR_new_label (ctx);

      res = get_new_temp (c2m_ctx, MIR_T_I64);
      gen (c2m_ctx, NL_HEAD (r->u.ops), t_label, f_label, FALSE, NULL, NULL);
      emit_label_insn_opt (c2m_ctx, t_label);
      emit2 (c2m_ctx, MIR_MOV, res.mir_op, zero_op.mir_op);
      emit1 (c2m_ctx, MIR_JMP, MIR_new_label_op (ctx, end_label));
      emit_label_insn_opt (c2m_ctx, f_label);
      emit2 (c2m_ctx, MIR_MOV, res.mir_op, one_op.mir_op);
      emit_label_insn_opt (c2m_ctx, end_label);
    }
    break;
  case N_ADD:
  case N_SUB:
    if (NL_NEXT (NL_HEAD (r->u.ops)) == NULL) { /* unary */
      MIR_insn_code_t ic = get_mir_insn_code (c2m_ctx, r);

      gen_unary_op (c2m_ctx, r, &op1, &res);
      if (r->code == N_ADD) {
        ic = (ic == MIR_FADD    ? MIR_FMOV
              : ic == MIR_DADD  ? MIR_DMOV
              : ic == MIR_LDADD ? MIR_LDMOV
                                : MIR_MOV);
        emit2 (c2m_ctx, ic, res.mir_op, op1.mir_op);
      } else {
        ic = (ic == MIR_FSUB    ? MIR_FNEG
              : ic == MIR_DSUB  ? MIR_DNEG
              : ic == MIR_LDSUB ? MIR_LDNEG
              : ic == MIR_SUB   ? MIR_NEG
                                : MIR_NEGS);
        emit2 (c2m_ctx, ic, res.mir_op, op1.mir_op);
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
    gen_bin_op (c2m_ctx, r, &op1, &op2, &res);
    /* Integer division-by-zero guard — only for / and %, not &, |, <<, * etc. */
    if (c2m_options->exceptions_p && (r->code == N_DIV || r->code == N_MOD)
        && integer_type_p (((struct expr *) r->attr)->type)) {
      struct type *rt = ((struct expr *) r->attr)->type;
      node_t den_n = NL_EL (r->u.ops, 1);
      struct expr *den_e = den_n != NULL ? den_n->attr : NULL;
      /* Constant non-zero divisor: the trap is dead. */
      int den_known_nz = den_e != NULL && den_e->const_p && den_e->c.i_val != 0;
      op_t div_reg = force_reg (c2m_ctx, op2, MIR_T_I64);
      if (!den_known_nz)
        gen_div_zero_check (c2m_ctx, div_reg, (long) POS (r).lno);
      /* Signed MIN / -1 overflow (SIGFPE) guard. */
      if (signed_integer_type_p (rt)) {
        mir_size_t sz = type_size (c2m_ctx, rt);
        long long minv = sz >= 8 ? (-9223372036854775807LL - 1) : (long long) (-2147483647 - 1);
        op_t dvd_reg = force_reg (c2m_ctx, op1, MIR_T_I64);
        gen_div_overflow_check (c2m_ctx, dvd_reg, div_reg, minv, (long) POS (r).lno);
      }
    }
    /* Shift amount range: count in [0, width) of the (promoted) left type. */
    if (c2m_options->exceptions_p && (r->code == N_LSH || r->code == N_RSH)
        && integer_type_p (((struct expr *) r->attr)->type)) {
      struct type *rt = ((struct expr *) r->attr)->type;
      mir_size_t sz = type_size (c2m_ctx, rt);
      int width = (int) (sz * MIR_CHAR_BIT);
      if (width < 8) width = 8;
      if (width > 64) width = 64;
      node_t cnt_n = NL_EL (r->u.ops, 1);
      struct expr *cnt_e = cnt_n != NULL ? cnt_n->attr : NULL;
      /* Constant count already in range: the trap is dead.  This is the
         common case for libc inline helpers (bswap, etc.). */
      if (!(cnt_e != NULL && cnt_e->const_p && cnt_e->c.i_val >= 0
            && cnt_e->c.i_val < width)) {
        op_t cnt_reg = force_reg (c2m_ctx, op2, MIR_T_I64);
        gen_shift_range_check (c2m_ctx, cnt_reg, width, (long) POS (r).lno);
      }
    }
    emit_bin_op (c2m_ctx, r, ((struct expr *) r->attr)->type, res, op1, op2);
    break;
  case N_EQ:
  case N_NE:
  case N_LT:
  case N_LE:
  case N_GT:
  case N_GE: {
    struct type *type1 = ((struct expr *) NL_HEAD (r->u.ops)->attr)->type;
    struct type *type2 = ((struct expr *) NL_EL (r->u.ops, 1)->attr)->type;
    struct type type_s, ptr_type_s = get_ptr_int_type (FALSE);
    /* By-value aggregate equality (`a == b` / `a != b` where a, b are
       class/struct/union values): lower to memcmp(&a, &b, sizeof) and compare
       the int result against 0.  Shallow, byte-wise equality. */
    if ((r->code == N_EQ || r->code == N_NE)
        && (type1->mode == TM_CLASS || type1->mode == TM_STRUCT || type1->mode == TM_UNION)
        && (type2->mode == TM_CLASS || type2->mode == TM_STRUCT || type2->mode == TM_UNION)) {
      op_t a = gen (c2m_ctx, NL_HEAD (r->u.ops), NULL, NULL, FALSE, NULL, NULL);
      op_t b = gen (c2m_ctx, NL_EL (r->u.ops, 1), NULL, NULL, FALSE, NULL, NULL);
      op_t cmp = gen_memcmp (c2m_ctx, a, b, type_size (c2m_ctx, type1));
      MIR_insn_code_t cc = (r->code == N_EQ ? MIR_EQS : MIR_NES);
      if (true_label == NULL) {
        res = get_new_temp (c2m_ctx, MIR_T_I64);
        emit3 (c2m_ctx, cc, res.mir_op, cmp.mir_op, MIR_new_int_op (ctx, 0));
      } else {
        cc = (r->code == N_EQ ? MIR_BEQS : MIR_BNES);
        emit3 (c2m_ctx, cc, MIR_new_label_op (ctx, true_label), cmp.mir_op,
               MIR_new_int_op (ctx, 0));
        emit1 (c2m_ctx, MIR_JMP, MIR_new_label_op (ctx, false_label));
        true_label = false_label = NULL;
      }
      break;
    }
    /* dict, slice and managed String are all pointer-sized identity values;
       compare them as integers/pointers (arithmetic_conversion only accepts
       arithmetic operands). */
#define CMP_PTR_LIKE(t)                                                   \
  ((t)->mode == TM_PTR || (t)->mode == TM_DICT || (t)->mode == TM_SLICE  \
   || builtin_string_type_p (t))
    type_s = arithmetic_conversion (CMP_PTR_LIKE (type1) ? &ptr_type_s : type1,
                                    CMP_PTR_LIKE (type2) ? &ptr_type_s : type2);
#undef CMP_PTR_LIKE
    set_type_layout (c2m_ctx, &type_s);
    gen_cmp_op (c2m_ctx, r, &type_s, &op1, &op2, &res);
    insn_code = get_mir_type_insn_code (c2m_ctx, &type_s, r);
    if (true_label == NULL) {
      emit3 (c2m_ctx, insn_code, res.mir_op, op1.mir_op, op2.mir_op);
    } else {
      insn_code = get_compare_branch_code (insn_code);
      emit3 (c2m_ctx, insn_code, MIR_new_label_op (ctx, true_label), op1.mir_op, op2.mir_op);
      emit1 (c2m_ctx, MIR_JMP, MIR_new_label_op (ctx, false_label));
      true_label = false_label = NULL;
    }
    break;
  }
  case N_POST_INC:
  case N_POST_DEC: {
    type = ((struct expr *) r->attr)->type2;
    t = get_mir_type (c2m_ctx, type);
    var = gen (c2m_ctx, NL_HEAD (r->u.ops), NULL, NULL, FALSE, NULL, NULL);
    /* Atomic integer ++/--: single RMW (AADD/ASUB). */
    if (var.mir_op.mode == MIR_OP_MEM && type_atomic_p (type) && integer_type_p (type)
        && type->mode != TM_PTR) {
      MIR_type_t at = t;
      op_t delta = promote (c2m_ctx, one_op, at, FALSE);
      op_t oldv = get_new_temp (c2m_ctx, promote_mir_int_type (at));
      var.mir_op.u.mem.type = at;
      delta = force_reg (c2m_ctx, delta, promote_mir_int_type (at));
      emit3_noopt (c2m_ctx, r->code == N_POST_INC ? MIR_AADD : MIR_ASUB, oldv.mir_op, var.mir_op,
                   delta.mir_op);
      if (val_p || true_label != NULL) {
        res = oldv;
      }
      break;
    }
    op1 = force_val (c2m_ctx, var, FALSE);
    if (val_p || true_label != NULL) {
      res = get_new_temp (c2m_ctx, t);
      emit2 (c2m_ctx, tp_mov (t), res.mir_op, op1.mir_op);
    }
    val = promote (c2m_ctx, op1, t, TRUE);
    op2 = promote (c2m_ctx,
                   type->mode != TM_PTR
                     ? one_op
                     : new_op (NULL, MIR_new_int_op (ctx, type_size (c2m_ctx, type->u.ptr_type))),
                   t, FALSE);
    emit3 (c2m_ctx, get_mir_insn_code (c2m_ctx, r), val.mir_op, val.mir_op, op2.mir_op);
    t = promote_mir_int_type (t);
    goto assign;
  }
  case N_INC:
  case N_DEC: {
    type = ((struct expr *) r->attr)->type2;
    t = get_mir_type (c2m_ctx, type);
    var = gen (c2m_ctx, NL_HEAD (r->u.ops), NULL, NULL, FALSE, NULL, NULL);
    if (var.mir_op.mode == MIR_OP_MEM && type_atomic_p (type) && integer_type_p (type)
        && type->mode != TM_PTR) {
      MIR_type_t at = t;
      op_t delta = promote (c2m_ctx, one_op, at, FALSE);
      op_t oldv = get_new_temp (c2m_ctx, promote_mir_int_type (at));
      var.mir_op.u.mem.type = at;
      delta = force_reg (c2m_ctx, delta, promote_mir_int_type (at));
      emit3_noopt (c2m_ctx, r->code == N_INC ? MIR_AADD : MIR_ASUB, oldv.mir_op, var.mir_op,
                   delta.mir_op);
      t = promote_mir_int_type (at);
      res = get_new_temp (c2m_ctx, t);
      emit3 (c2m_ctx, r->code == N_INC ? MIR_ADD : MIR_SUB, res.mir_op, oldv.mir_op, delta.mir_op);
      break;
    }
    val = promote (c2m_ctx, force_val (c2m_ctx, var, FALSE), t, TRUE);
    op2 = promote (c2m_ctx,
                   type->mode != TM_PTR
                     ? one_op
                     : new_op (NULL, MIR_new_int_op (ctx, type_size (c2m_ctx, type->u.ptr_type))),
                   t, FALSE);
    t = promote_mir_int_type (t);
    res = get_new_temp (c2m_ctx, t);
    emit3 (c2m_ctx, get_mir_insn_code (c2m_ctx, r), val.mir_op, val.mir_op, op2.mir_op);
    goto assign;
  }
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
    /* Atomic RMW for += -= &= |= ^= on integer _Atomic lvalues. */
    node_t lhs_e = NL_HEAD (r->u.ops);
    struct type *lhs_t = ((struct expr *) lhs_e->attr)->type;
    MIR_insn_code_t acode = MIR_INSN_BOUND;
    if (type_atomic_p (lhs_t) && integer_type_p (lhs_t) && lhs_t->mode != TM_PTR) {
      if (r->code == N_ADD_ASSIGN) acode = MIR_AADD;
      else if (r->code == N_SUB_ASSIGN) acode = MIR_ASUB;
      else if (r->code == N_AND_ASSIGN) acode = MIR_AAND;
      else if (r->code == N_OR_ASSIGN) acode = MIR_AOR;
      else if (r->code == N_XOR_ASSIGN) acode = MIR_AXOR;
    }
    if (acode != MIR_INSN_BOUND) {
      MIR_type_t at = get_mir_type (c2m_ctx, lhs_t);
      MIR_insn_code_t bin = (acode == MIR_AADD   ? MIR_ADD
                             : acode == MIR_ASUB ? MIR_SUB
                             : acode == MIR_AAND ? MIR_AND
                             : acode == MIR_AOR  ? MIR_OR
                                                 : MIR_XOR);
      var = gen (c2m_ctx, lhs_e, NULL, NULL, FALSE, NULL, NULL);
      op2 = val_gen (c2m_ctx, NL_NEXT (lhs_e));
      op2 = promote (c2m_ctx, op2, at, FALSE);
      op2 = force_reg (c2m_ctx, op2, promote_mir_int_type (at));
      if (var.mir_op.mode == MIR_OP_MEM) {
        op_t oldv = get_new_temp (c2m_ctx, promote_mir_int_type (at));
        var.mir_op.u.mem.type = at;
        emit3_noopt (c2m_ctx, acode, oldv.mir_op, var.mir_op, op2.mir_op);
        t = promote_mir_int_type (at);
        res = get_new_temp (c2m_ctx, t);
        emit3 (c2m_ctx, bin, res.mir_op, oldv.mir_op, op2.mir_op);
        break;
      }
    }
    gen_assign_bin_op (c2m_ctx, r, ((struct expr *) r->attr)->type2, &val, &op2, &var);
    /* Integer division-by-zero guard for /= and %= (exceptions mode only). */
    if (c2m_options->exceptions_p && (r->code == N_DIV_ASSIGN || r->code == N_MOD_ASSIGN)
        && integer_type_p (((struct expr *) r->attr)->type2)) {
      struct type *rt = ((struct expr *) r->attr)->type2;
      node_t den_n = NL_EL (r->u.ops, 1);
      struct expr *den_e = den_n != NULL ? den_n->attr : NULL;
      int den_known_nz = den_e != NULL && den_e->const_p && den_e->c.i_val != 0;
      op_t div_reg = force_reg (c2m_ctx, op2, MIR_T_I64);
      if (!den_known_nz)
        gen_div_zero_check (c2m_ctx, div_reg, (long) POS (r).lno);
      /* Signed MIN / -1 overflow (SIGFPE) guard. */
      if (signed_integer_type_p (rt)) {
        mir_size_t sz = type_size (c2m_ctx, rt);
        long long minv = sz >= 8 ? (-9223372036854775807LL - 1) : (long long) (-2147483647 - 1);
        op_t dvd_reg = force_reg (c2m_ctx, val, MIR_T_I64);
        gen_div_overflow_check (c2m_ctx, dvd_reg, div_reg, minv, (long) POS (r).lno);
      }
    }
    if (c2m_options->exceptions_p
        && (r->code == N_LSH_ASSIGN || r->code == N_RSH_ASSIGN)
        && integer_type_p (((struct expr *) r->attr)->type2)) {
      struct type *rt = ((struct expr *) r->attr)->type2;
      mir_size_t sz = type_size (c2m_ctx, rt);
      int width = (int) (sz * MIR_CHAR_BIT);
      if (width < 8) width = 8;
      if (width > 64) width = 64;
      node_t cnt_n = NL_EL (r->u.ops, 1);
      struct expr *cnt_e = cnt_n != NULL ? cnt_n->attr : NULL;
      if (!(cnt_e != NULL && cnt_e->const_p && cnt_e->c.i_val >= 0
            && cnt_e->c.i_val < width)) {
        op_t cnt_reg = force_reg (c2m_ctx, op2, MIR_T_I64);
        gen_shift_range_check (c2m_ctx, cnt_reg, width, (long) POS (r).lno);
      }
    }
    emit_bin_op (c2m_ctx, r, ((struct expr *) r->attr)->type2, val, val, op2);
    t = get_op_type (c2m_ctx, var);
    t = promote_mir_int_type (t);
    res = get_new_temp (c2m_ctx, t);
    goto assign;
    break;
  }
  case N_ASSIGN: {
    node_t lhs = NL_HEAD (r->u.ops);
    node_t rhs_node = NL_EL (r->u.ops, 1);
    /* class[i] = val — bracket subscript write via Set(int,T) protocol.
       Intercept N_ASSIGN when the LHS is N_IND on a class-typed receiver. */
    if (lhs->code == N_IND) {
      node_t ind_arr = NL_HEAD (lhs->u.ops);
      struct type *ind_arr_t = ((struct expr *) ind_arr->attr)->type;
      int class_ind_p = (ind_arr_t->mode == TM_CLASS
                         || (ind_arr_t->mode == TM_PTR && ind_arr_t->u.ptr_type != NULL
                             && ind_arr_t->u.ptr_type->mode == TM_CLASS));
      if (class_ind_p) {
        struct type *cls_type = ind_arr_t->mode == TM_PTR ? ind_arr_t->u.ptr_type : ind_arr_t;
        struct type *this_type
          = ind_arr_t->mode == TM_PTR ? ind_arr_t : create_ptr_type (c2m_ctx, cls_type);
        node_t set_def = find_class_protocol_method (c2m_ctx, cls_type->u.tag_type, "Set", 2, POS (r));
        if (set_def != NULL) {
          /* Evaluate the receiver */
          op_t this_op;
          if (ind_arr_t->mode == TM_PTR) {
            this_op = val_gen (c2m_ctx, ind_arr);
          } else {
            this_op = gen (c2m_ctx, ind_arr, NULL, NULL, FALSE, NULL, NULL);
            if (this_op.mir_op.mode == MIR_OP_MEM)
              this_op = mem_to_address (c2m_ctx, this_op, TRUE);
          }
          /* Evaluate the index.  Integer indices widen to I64 (List/Set
             Set(int,T)); a non-integer key (Map<String,V>: Set(String,V)) is
             passed through for gen_funcptr_call to coerce to the key type. */
          op_t idx_op = val_gen (c2m_ctx, NL_EL (lhs->u.ops, 1));
          if (integer_type_p (((struct expr *) NL_EL (lhs->u.ops, 1)->attr)->type))
            idx_op = cast (c2m_ctx, idx_op, MIR_T_I64, FALSE);
          /* Evaluate the rhs value */
          op_t val_op = val_gen (c2m_ctx, rhs_node);
          /* Call: this->Set(index, value) */
          op_t set_args[2] = { idx_op, val_op };
          gen_class_method_call (c2m_ctx, set_def, this_type, this_op, set_args, 2);
          res = val_op;
          break;
        }
        /* If no Set method exists, fall through to the normal assign path
           (will likely produce a sensible error at runtime or already errored in check). */
      }
    }
    /* Dict-literal assignment: d = { "k": v, ... }.  Build a fresh dict object,
       populate it from the initializer list, then store the pointer into the
       lhs lvalue. */
    if (rhs_node->code == N_LIST
        && ((struct expr *) r->attr)->type->mode == TM_DICT) {
      op_t obj = gen_dict_create_object (c2m_ctx);
      gen_dict_init_list (c2m_ctx, obj.mir_op, rhs_node);
      /* When the lhs is itself a dict element (d.key / d["key"]) the new object
         must be inserted into the parent dict; otherwise it is stored into the
         lhs (dict) variable. */
      /* Only treat as dict-element store when the *parent* of the
         field access is itself a dict object — not when a class/struct
         member merely has type TM_DICT. */
      int lhs_is_dict_elem = FALSE;
      if (lhs->code == N_IND
          && ((struct expr *) NL_HEAD (lhs->u.ops)->attr)->type->mode == TM_DICT) {
        lhs_is_dict_elem = TRUE;
      } else if (lhs->code == N_FIELD || lhs->code == N_DEREF_FIELD) {
        node_t _par = NL_HEAD (lhs->u.ops);
        struct expr *_pe = _par ? (struct expr *) _par->attr : NULL;
        struct type *_pt = (_pe && _pe->type) ? _pe->type : NULL;
        if (_pt && lhs->code == N_DEREF_FIELD && _pt->mode == TM_PTR)
          _pt = _pt->u.ptr_type;
        if (_pt && _pt->mode == TM_DICT)
          lhs_is_dict_elem = TRUE;
      }
      if (lhs_is_dict_elem) {
        MIR_op_t key_op;
        if (lhs->code == N_IND) {
          node_t dict_node = NL_HEAD (lhs->u.ops);
          node_t key_node = NL_EL (lhs->u.ops, 1);
          op1 = val_gen (c2m_ctx, dict_node);
          key_op = val_gen (c2m_ctx, key_node).mir_op;
        } else {
          node_t parent_node = NL_HEAD (lhs->u.ops);
          node_t key_id = NL_NEXT (parent_node);
          op1 = val_gen (c2m_ctx, parent_node);
          const char *key_str = key_id->u.s.s;
          key_op = gen_dict_key_op (c2m_ctx, key_str, strlen (key_str) + 1);
        }
        gen_dict_object_set (c2m_ctx, op1.mir_op, key_op, obj.mir_op);
      } else {
        var = gen (c2m_ctx, lhs, NULL, NULL, FALSE, NULL, NULL);
        emit2 (c2m_ctx, MIR_MOV, var.mir_op, obj.mir_op);
      }
      res = obj;
      break;
    }
    /* Dict assignment: d.key = val  OR  d["key"] = val
       Only intercept when the *parent* of the field access is itself a dict
       object — not when a class/struct member merely has type TM_DICT. */
    {
      int dict_field_assign = FALSE;
      if (((struct expr *) r->attr)->type->mode == TM_DICT) {
        if (lhs->code == N_IND
            && ((struct expr *) NL_HEAD(lhs->u.ops)->attr)->type->mode == TM_DICT) {
          dict_field_assign = TRUE;
        } else if (lhs->code == N_FIELD || lhs->code == N_DEREF_FIELD) {
          node_t _par = NL_HEAD (lhs->u.ops);
          struct expr *_pe = _par ? (struct expr *) _par->attr : NULL;
          struct type *_pt = (_pe && _pe->type) ? _pe->type : NULL;
          if (_pt && lhs->code == N_DEREF_FIELD && _pt->mode == TM_PTR)
            _pt = _pt->u.ptr_type;
          if (_pt && _pt->mode == TM_DICT)
            dict_field_assign = TRUE;
        }
      }
    if (dict_field_assign) {
      MIR_op_t key_op;
      if (lhs->code == N_IND) {
        /* d["key"] = val  or  d[var] = val */
        node_t dict_node = NL_HEAD (lhs->u.ops);
        node_t key_node = NL_EL (lhs->u.ops, 1);
        op1 = val_gen (c2m_ctx, dict_node);
        op_t key_val = val_gen (c2m_ctx, key_node);
        key_op = key_val.mir_op;
      } else {
        /* d.key = val */
        node_t parent_node = NL_HEAD (lhs->u.ops);
        node_t key_id = NL_NEXT (parent_node);
        assert (key_id->code == N_ID);
        op1 = val_gen (c2m_ctx, parent_node);
        const char *key_str = key_id->u.s.s;
        key_op = gen_dict_key_op (c2m_ctx, key_str, strlen (key_str) + 1);
      }
      op2 = val_gen (c2m_ctx, rhs_node);
      struct expr *rhs_expr = rhs_node->attr;
      op_t wrapped;
      if (rhs_expr != NULL && rhs_expr->type->mode == TM_DICT) {
        wrapped = gen_dict_value_copy (c2m_ctx, op2.mir_op);
      } else if (rhs_node->code == N_STR
                 || (rhs_expr != NULL
                     && (builtin_string_type_p (rhs_expr->type)
                         || rhs_expr->type->mode == TM_PTR))) {
        /* String / char* / literal: store via dict_create_string, which COPIES
           the bytes so the dict owns its own buffer.  Without the
           builtin_string_type_p check a ClassyC `String` (not TM_PTR) fell
           through to the int64 branch below — the raw pointer was stored as a
           number (garbage json) and dangled once its arena scope was released.
           Mirrors gen_dict_value_for_init's runtime-String handling. */
        wrapped = gen_dict_create_string (c2m_ctx, op2.mir_op);
      } else if (rhs_expr != NULL && rhs_expr->const_p && floating_type_p (rhs_expr->type)) {
        wrapped = gen_dict_create_number (c2m_ctx, op2.mir_op);
      } else if (rhs_expr != NULL && rhs_expr->type->mode == TM_BASIC
                 && rhs_expr->type->u.basic_type == TP_BOOL) {
        wrapped = gen_dict_create_bool (c2m_ctx, op2.mir_op);
      } else {
        wrapped = gen_dict_create_int64 (c2m_ctx, op2.mir_op);
      }
      gen_dict_object_set (c2m_ctx, op1.mir_op, key_op, wrapped.mir_op);
      res = wrapped;
      break;
    }
    }
    var = gen (c2m_ctx, lhs, NULL, NULL, FALSE, NULL, NULL);
    t = get_op_type (c2m_ctx, var);
    op2 = gen (c2m_ctx, rhs_node, NULL, NULL, t != MIR_T_UNDEF,
               t != MIR_T_UNDEF ? NULL : &var, NULL);
    op2 = maybe_unwrap_dict_value (c2m_ctx, op2, rhs_node, ((struct expr *) r->attr)->type);
    if ((!val_p && true_label == NULL) || t == MIR_T_UNDEF) {
      res = var;
      val = op2;
    } else {
      t = promote_mir_int_type (t);
      val = promote (c2m_ctx, op2, t, TRUE);
      res = get_new_temp (c2m_ctx, t);
    }
  }
  assign: /* t/val is promoted type/new value of assign expression */
    if (scalar_type_p (((struct expr *) r->attr)->type)) {
      struct type *asg_t = ((struct expr *) r->attr)->type;
      assert (t != MIR_T_UNDEF);
      val = cast (c2m_ctx, val, get_mir_type (c2m_ctx, asg_t), FALSE);
      /* Value-semantic String field (Option D): when storing a String into a
         class member, the object takes a private owned copy and frees the old
         buffer.  This keeps the field pointing at a freeable heap buffer (or
         NULL), so the destructor's c2m_str_drop is always safe — strings live
         and die with the object, no GC. */
      if (class_string_member_store_p (r)) {
        string_ensure_imports (c2m_ctx);
        MIR_op_t own_arg = force_reg (c2m_ctx, val, MIR_T_I64).mir_op;
        op_t owned = gen_rt_call (c2m_ctx, str_own_proto, str_own_item, 1, &own_arg);
        owned = force_reg (c2m_ctx, owned, MIR_T_I64);
        /* Free the field's previous buffer (NULL-safe) before overwriting. */
        op_t oldv = get_new_temp (c2m_ctx, MIR_T_I64);
        emit2 (c2m_ctx, MIR_MOV, oldv.mir_op, var.mir_op);
        MIR_op_t drop_arg = oldv.mir_op;
        gen_rt_call_void (c2m_ctx, str_drop_proto, str_drop_item, 1, &drop_arg);
        val = owned;
      }
      if (var.mir_op.mode == MIR_OP_MEM && type_atomic_p (asg_t) && integer_type_p (asg_t)
          && !floating_type_p (asg_t)) {
        MIR_type_t at = get_mir_type (c2m_ctx, asg_t);
        var.mir_op.u.mem.type = at;
        val = force_reg (c2m_ctx, val, promote_mir_int_type (at));
        atomic_store_mem (c2m_ctx, var, val);
      } else {
        emit_scalar_assign (c2m_ctx, var, &val, t, FALSE);
      }
      if ((val_p || true_label != NULL) && r->code != N_POST_INC && r->code != N_POST_DEC)
        emit2_noopt (c2m_ctx, tp_mov (t), res.mir_op, val.mir_op);
    } else { /* block move */
      struct type *at = ((struct expr *) r->attr)->type;
      mir_size_t size = type_size (c2m_ctx, at);

      assert (r->code == N_ASSIGN);
      /* Note: for move-assign of List/Map/Set, the RHS is `move src` (or a
         prvalue call return) which transfers buffer ownership; destroy the
         LHS first so its previous buffer is not leaked.  Done only for
         move-only collections — ordinary by-value class elements (Pt) use
         plain block_move as List does internally. */
      if (class_is_move_only_collection_p (c2m_ctx, at)
          && NL_EL (r->u.ops, 1) != NULL
          && (NL_EL (r->u.ops, 1)->code == N_MOVE
              || NL_EL (r->u.ops, 1)->code == N_CALL
              || NL_EL (r->u.ops, 1)->code == N_STMTEXPR)) {
        node_t ddef = find_class_dtor_def (c2m_ctx, at->u.tag_type);
        if (ddef != NULL && ddef->code == N_FUNC_DEF && ddef->attr != NULL) {
          decl_t dd = ddef->attr;
          if (dd->u.item != NULL) {
            struct func_type *dft = dd->decl_spec.type->u.func_type;
            MIR_item_t dproto;
            char dpname[64];
            size_t dops;
            op_t addr = mem_to_address (c2m_ctx, var, TRUE);
            collect_args_and_func_types (c2m_ctx, dft, NULL);
            sprintf (dpname, "__assigndtorproto%d", new_proto_count++);
            dproto = MIR_new_proto_arr (ctx, dpname,
                                        VARR_LENGTH (MIR_type_t, proto_info.ret_types),
                                        VARR_ADDR (MIR_type_t, proto_info.ret_types),
                                        VARR_LENGTH (MIR_var_t, proto_info.arg_vars),
                                        VARR_ADDR (MIR_var_t, proto_info.arg_vars));
            move_item_to_module_start (curr_func->module, dproto);
            dops = VARR_LENGTH (MIR_op_t, call_ops);
            VARR_PUSH (MIR_op_t, call_ops, MIR_new_ref_op (ctx, dproto));
            VARR_PUSH (MIR_op_t, call_ops, MIR_new_ref_op (ctx, dd->u.item));
            VARR_PUSH (MIR_op_t, call_ops, addr.mir_op);
            emit_insn (c2m_ctx,
                       MIR_new_insn_arr (ctx, MIR_CALL,
                                         VARR_LENGTH (MIR_op_t, call_ops) - dops,
                                         VARR_ADDR (MIR_op_t, call_ops) + dops));
            VARR_TRUNC (MIR_op_t, call_ops, dops);
          }
        }
      }
      block_move (c2m_ctx, var, val, size);
    }
    break;
  case N_ID: {
        e = r->attr;
        assert (!e->const_p);

        // Debug: Print basic ID information
        if (c2m_options->verbose_p) {
            fprintf(stderr, "DEBUG N_ID: Processing ID '%s'\n", r->u.s.s ? r->u.s.s : "NULL");
            fprintf(stderr, "  lvalue_node: %p\n", e->u.lvalue_node);
            fprintf(stderr, "  def_node: %p\n", e->def_node);
            if (e->def_node) {
                fprintf(stderr, "  def_node->code: %d\n", e->def_node->code);
            }
            if (e->type) {
                fprintf(stderr, "  type: %p\n", e->type);
            }
        }
        if (e->u.lvalue_node == NULL) {
            if (c2m_options->verbose_p)
                fprintf(stderr, "  Branch: lvalue_node == NULL (function reference)\n");
            // Check if this ID might be a method that needs mangling
            if (e->def_node && e->def_node->code == N_FUNC_DEF) {
                if (c2m_options->verbose_p)
                    fprintf(stderr, "  Found N_FUNC_DEF, checking for method...\n");
                decl_t func_decl = e->def_node->attr;
                node_t current_scope = func_decl->scope;
                int found_class = FALSE;
                int scope_level = 0;

                // Check if this function is defined in a class scope
                if( func_decl ) {
                    res = new_op (NULL, MIR_new_ref_op (ctx, func_decl->u.item));
                }

                if (!found_class) {
                    // Not a method, use original logic
                    res = new_op (NULL, MIR_new_ref_op (ctx, ((decl_t) e->def_node->attr)->u.item));
                }
            } else {
                res = new_op (NULL, MIR_new_ref_op (ctx, ((decl_t) e->def_node->attr)->u.item));
            }
        } else if (((decl = e->u.lvalue_node->attr)->scope == top_scope || decl->decl_spec.static_p
                    || decl->decl_spec.linkage != N_IGNORE)
                   && !decl->asm_p) {
            t = get_mir_type (c2m_ctx, e->type);
            res = get_new_temp (c2m_ctx, MIR_T_I64);
            emit2 (c2m_ctx, MIR_MOV, res.mir_op, MIR_new_ref_op (ctx, decl->u.item));
            res = new_op (decl, MIR_new_alias_mem_op (ctx, t, 0, res.mir_op.u.reg, 0, 1,
                                                      get_type_alias (c2m_ctx, e->type), 0));
        } else if (!decl->reg_p) {
            t = get_mir_type (c2m_ctx, e->type);
            /* R2: by-ref for-in loop var — read through the element pointer
               (the frame slot is intentionally never written). */
            if (decl->byref_p) {
              MIR_reg_t br = gen_byref_find (decl);
              if (br != GEN_BYREF_NOREG) {
                res = new_op (decl, MIR_new_alias_mem_op (ctx, t, 0, br, 0, 1,
                                                          get_type_alias (c2m_ctx, e->type), 0));
                break;
              }
            }
            res = new_op (decl, MIR_new_alias_mem_op (ctx, t, decl->offset,
                                                      MIR_reg (ctx, FP_NAME, curr_func->u.func), 0, 1,
                                                      get_type_alias (c2m_ctx, e->type), 0));
        } else {
            const char *name;
            reg_var_t reg_var;

            t = get_mir_type (c2m_ctx, e->type);
            assert (t != MIR_T_UNDEF);
            t = promote_mir_int_type (t);
            name = get_reg_var_name (c2m_ctx, t, r->u.s.s,
                                     ((struct node_scope *) decl->scope->attr)->func_scope_num);
            reg_var = get_reg_var (c2m_ctx, t, name, decl->u.asm_str);
            res = new_op (decl, MIR_new_reg_op (ctx, reg_var.reg));
        }
        break;
    }
  case N_IND: {
    MIR_type_t ind_t;
    node_t arr = NL_HEAD (r->u.ops);
    struct type *el_type = ((struct expr *) r->attr)->type;
    struct type *arr_type = ((struct expr *) arr->attr)->type;
    mir_size_t size = type_size (c2m_ctx, el_type);

    if (arr_type->mode == TM_DICT) {
      /* d["key"] or d[var] — dict bracket subscript read */
      op1 = val_gen (c2m_ctx, arr);
      op2 = val_gen (c2m_ctx, NL_EL (r->u.ops, 1));
      if (integer_type_p (((struct expr *) NL_EL (r->u.ops, 1)->attr)->type)) {
        /* integer index: dispatch to unified array/object lookup */
        res = gen_dict_value_at (c2m_ctx, op1.mir_op, op2.mir_op);
      } else {
        res = gen_dict_object_get (c2m_ctx, op1.mir_op, op2.mir_op);
      }
      break;
    }
    if ((arr_type->mode == TM_CLASS
         || (arr_type->mode == TM_PTR && arr_type->u.ptr_type != NULL
             && arr_type->u.ptr_type->mode == TM_CLASS))
        && (((struct expr *) r->attr)->def_node != NULL
            || find_class_protocol_method (c2m_ctx,
                 (arr_type->mode == TM_PTR ? arr_type->u.ptr_type : arr_type)->u.tag_type,
                 "Get", 1, POS (r)) != NULL)) {
      /* class[i] / Class*[i] — Get/Set protocol when the class has Get.
         List/Map pointer sugar matches value receivers; plain T* without Get
         falls through to raw C indexing below. */
      struct type *cls_type = arr_type->mode == TM_PTR ? arr_type->u.ptr_type : arr_type;
      struct type *this_type
        = arr_type->mode == TM_PTR ? arr_type : create_ptr_type (c2m_ctx, cls_type);
      node_t get_def = ((struct expr *) r->attr)->def_node;
      /* Only trust a stashed protocol method if it really is one — a stale/garbage
         def_node (historically from uninit create_expr) must not hijack T* indexing. */
      if (get_def != NULL
          && (get_def->code != N_FUNC_DEF || get_def->attr == NULL
              || ((decl_t) get_def->attr)->decl_spec.type == NULL
              || ((decl_t) get_def->attr)->decl_spec.type->mode != TM_FUNC))
        get_def = NULL;
      if (get_def == NULL)
        get_def = find_class_protocol_method (c2m_ctx, cls_type->u.tag_type, "Get", 1, POS (r));
      if (get_def == NULL) {
        /* Fall through to raw pointer/array indexing (T* / class-by-value buffer). */
      } else {
      /* Evaluate the receiver (this pointer) */
      op_t this_op;
      if (arr_type->mode == TM_PTR) {
        this_op = val_gen (c2m_ctx, arr);
      } else { /* class lvalue: pass its address */
        this_op = gen (c2m_ctx, arr, NULL, NULL, FALSE, NULL, NULL);
        if (this_op.mir_op.mode == MIR_OP_MEM)
          this_op = mem_to_address (c2m_ctx, this_op, TRUE);
      }
      /* Evaluate the index.  Integer indices are widened to I64 (List/Set
         Get(int)); a non-integer key (Map<String,V>: Get(String)) is passed
         through and coerced to the key parameter type by gen_funcptr_call. */
      op_t idx_op = val_gen (c2m_ctx, NL_EL (r->u.ops, 1));
      if (integer_type_p (((struct expr *) NL_EL (r->u.ops, 1)->attr)->type))
        idx_op = cast (c2m_ctx, idx_op, MIR_T_I64, FALSE);
      /* GetMut path: true lvalue into the dense buffer (fleet[0].Boost mutates). */
      {
        /* Safe flags: value receiver is &slot (never null); ownership/midopt
           may have stamped SAFE on a pointer receiver; midopt's IV proof sets
           elide_oob_p when the index is guarded by the loop bound. */
        int sub_flags = 0;
        struct expr *ind_e = (struct expr *) r->attr;
        if (arr_type->mode != TM_PTR)
          sub_flags |= GEN_SAFE_SKIP_NULL;
        else if (ind_e->own_deref_class == DEREF_GUARD_SAFE)
          sub_flags |= GEN_SAFE_SKIP_NULL;
        if (ind_e->elide_oob_p) sub_flags |= GEN_SAFE_SKIP_OOB;
        if (((struct expr *) r->attr)->mut_sub_p) {
          op_t ptr_op = gen_class_method_call_flags (c2m_ctx, get_def, this_type, this_op,
                                                     &idx_op, 1, sub_flags);
          ptr_op = force_reg (c2m_ctx, ptr_op, MIR_T_I64);
          {
            MIR_type_t el_mir = get_mir_type (c2m_ctx, el_type);
            if (el_type->mode == TM_CLASS || el_type->mode == TM_STRUCT
                || el_type->mode == TM_UNION) {
              res = new_op (NULL,
                            MIR_new_alias_mem_op (ctx, MIR_T_UNDEF, 0, ptr_op.mir_op.u.reg, 0, 1,
                                                  get_type_alias (c2m_ctx, el_type), 0));
            } else {
              res = new_op (NULL, MIR_new_mem_op (ctx, el_mir, 0, ptr_op.mir_op.u.reg, 0, 1));
            }
          }
          break;
        }
        /* Call: result = this->Get(index)  (by-value copy) */
        res = gen_class_method_call_flags (c2m_ctx, get_def, this_type, this_op, &idx_op, 1,
                                           sub_flags);
        break;
      }
      }
    }
    if (arr_type->mode == TM_SLICE) {
      /* slice[i]: element memory at slice_ptr + SLICE_HDR_SIZE + i*el_size */
      op_t sbase = get_new_temp (c2m_ctx, MIR_T_I64);

      t = get_mir_type (c2m_ctx, el_type);
      op1 = force_reg (c2m_ctx, val_gen (c2m_ctx, arr), MIR_T_I64);
      op2 = val_gen (c2m_ctx, NL_EL (r->u.ops, 1));
      ind_t = get_mir_type (c2m_ctx, ((struct expr *) NL_EL (r->u.ops, 1)->attr)->type);
      op2 = force_reg (c2m_ctx, cast (c2m_ctx, op2,
                                      ind_t == MIR_T_U32 || ind_t == MIR_T_U64 ? MIR_T_U64
                                                                               : MIR_T_I64,
                                      FALSE),
                       MIR_T_I64);
      /* Slice bounds guard: load count from slice header[0] and check idx < count. */
      if (c2m_options->exceptions_p) {
        op_t slice_len = get_new_temp (c2m_ctx, MIR_T_I64);
        emit2 (c2m_ctx, MIR_MOV, slice_len.mir_op,
               MIR_new_mem_op (ctx, MIR_T_I64, 0, op1.mir_op.u.reg, 0, 1));
        gen_oob_check (c2m_ctx, op2, slice_len.mir_op, (long) POS (r).lno);
      }
      emit3 (c2m_ctx, MIR_ADD, sbase.mir_op, op1.mir_op, MIR_new_int_op (ctx, SLICE_HDR_SIZE));
      if (size <= MIR_MAX_SCALE) {
        res = new_op (NULL, MIR_new_mem_op (ctx, t, 0, sbase.mir_op.u.reg, op2.mir_op.u.reg,
                                            (MIR_scale_t) size));
      } else {
        op_t off = get_new_temp (c2m_ctx, MIR_T_I64);
        emit3 (c2m_ctx, MIR_MUL, off.mir_op, op2.mir_op, MIR_new_int_op (ctx, (long) size));
        emit3 (c2m_ctx, MIR_ADD, sbase.mir_op, sbase.mir_op, off.mir_op);
        res = new_op (NULL, MIR_new_mem_op (ctx, t, 0, sbase.mir_op.u.reg, 0, 1));
      }
      break;
    }
    t = get_mir_type (c2m_ctx, el_type);
    op1 = val_gen (c2m_ctx, arr);
    op2 = val_gen (c2m_ctx, NL_EL (r->u.ops, 1));
    ind_t = get_mir_type (c2m_ctx, ((struct expr *) NL_EL (r->u.ops, 1)->attr)->type);
#if MIR_PTR32
    op2 = force_reg (c2m_ctx, op2, ind_t);
#else
    if (op2.mir_op.mode != MIR_OP_REG) {
      op2 = force_reg (c2m_ctx, op2, ind_t);
    } else if (ind_t != MIR_T_I64 && ind_t != MIR_T_U64) {
      op2 = cast (c2m_ctx, op2, ind_t == MIR_T_I32 ? MIR_T_I64 : MIR_T_U64, FALSE);
    }
#endif
    /* Static C array bounds guard (exceptions mode only).  Fixed-size arrays
       use the declared length; trailing FAM (`T a[1]`/`a[0]`/`a[]`) uses a
       sibling capacity if we named one, never the placeholder 1. */
    gen_c_array_oob (c2m_ctx, r, arr, arr_type, op2);
    if (el_type->mode == TM_PTR && el_type->arr_type != NULL) { /* elem is an array */
      size = type_size (c2m_ctx, el_type->arr_type);
    }
    if (arr_type->mode == TM_PTR && arr_type->arr_type != NULL) {
      /* Indexing a decayed/adjusted array.  Always materialize the base pointer
         value in a register before building the element mem op.

         force_reg_or_mem used to allow a raw MEM base, which is correct when
         that MEM is *array storage* (true local/global arrays after address
         arithmetic lands as a reg anyway).  For adjusted parameters forced
         into the frame by `try` (`char *argv[]` → stack slot of a `char **`),
         the MEM is the slot holding the pointer — keeping it as the base would
         index the stack frame (`&argv[i]`) instead of the pointed-to array
         (`argv[i]`).  Loading the pointer with force_reg fixes both shapes. */
      op1 = force_reg (c2m_ctx, op1, MIR_T_I64);
      assert (op1.mir_op.mode == MIR_OP_REG);
    } else {
      op1 = force_reg (c2m_ctx, op1, MIR_T_I64);
      assert (op1.mir_op.mode == MIR_OP_REG);
      /* Null guard when indexing a genuine pointer (not a decayed array whose
         base is always a valid stack/global address): p[i] on a null p would
         otherwise segfault. */
      if (c2m_options->exceptions_p && arr_type->mode == TM_PTR && arr_type->arr_type == NULL
          && ((struct expr *) r->attr)->own_deref_class != DEREF_GUARD_SAFE)
        gen_null_check (c2m_ctx, op1, (long) POS (r).lno);
      /* -fobject-guards: liveness check at ownership-CHECK (MaybeOwned) sites. */
      if (c2m_options->object_guards_p
          && ((struct expr *) r->attr)->own_deref_class == DEREF_GUARD_CHECK)
        gen_obj_guard_check (c2m_ctx, op1, (long) POS (r).lno);
    }
    res = op1;
    res.decl = NULL;
    if (res.mir_op.mode == MIR_OP_REG)
      res.mir_op = MIR_new_alias_mem_op (ctx, t, 0, res.mir_op.u.reg, 0, 1,
                                         get_type_alias (c2m_ctx, el_type), arr_type->antialias);
    if (res.mir_op.u.mem.base == 0 && size == 1) {
      res.mir_op.u.mem.base = op2.mir_op.u.reg;
    } else if (res.mir_op.u.mem.index == 0 && size <= MIR_MAX_SCALE) {
      res.mir_op.u.mem.index = op2.mir_op.u.reg;
      res.mir_op.u.mem.scale = (MIR_scale_t) size;
    } else {
      op_t temp_op;

      temp_op = get_new_temp (c2m_ctx, MIR_T_I64);
      emit3 (c2m_ctx, MIR_MUL, temp_op.mir_op, op2.mir_op, MIR_new_int_op (ctx, size));
      if (res.mir_op.u.mem.base != 0)
        emit3 (c2m_ctx, MIR_ADD, temp_op.mir_op, temp_op.mir_op,
               MIR_new_reg_op (ctx, res.mir_op.u.mem.base));
      res.mir_op.u.mem.base = temp_op.mir_op.u.reg;
    }
    res.mir_op.u.mem.type = t;
    break;
  }
  case N_LABEL_ADDR: {
    node_t target;

    e = r->attr;
    type = e->type;
    target = e->u.label_addr_target;
    t = get_mir_type (c2m_ctx, type);
    res = get_new_temp (c2m_ctx, t);
    emit2 (c2m_ctx, MIR_LADDR, res.mir_op, MIR_new_label_op (ctx, get_label (c2m_ctx, target)));
    break;
  }
  case N_LAMBDA: {
    /* An untyped lambda instantiated at its filter/map/reduce call site:
       evaluates to the generated static function (a func-item ref). */
    decl_t lam_decl;

    e = r->attr;
    assert (e != NULL && e->def_node != NULL && e->def_node->attr != NULL);
    lam_decl = e->def_node->attr;
    assert (lam_decl->u.item != NULL);
    res = new_op (NULL, MIR_new_ref_op (ctx, lam_decl->u.item));
    break;
  }
  case N_ADDR: {
    int add_p = FALSE;
    int saved_one_past = gen_ind_one_past_p;
    node_t addr_op = NL_HEAD (r->u.ops);

    /* `&a[n]` is a valid one-past-end pointer; allow idx == length. */
    gen_ind_one_past_p = (addr_op != NULL && addr_op->code == N_IND);
    op1 = gen (c2m_ctx, addr_op, NULL, NULL, FALSE, NULL, NULL);
    gen_ind_one_past_p = saved_one_past;
    type = ((struct expr *) r->attr)->type;
    t = get_mir_type (c2m_ctx, type);
    if (op1.mir_op.mode == MIR_OP_REG && type->mode == TM_PTR && scalar_type_p (type->u.ptr_type)) {
      MIR_insn_code_t code;
      res = get_new_temp (c2m_ctx, t);
      switch (get_mir_type (c2m_ctx, type->u.ptr_type)) {
      case MIR_T_I8:
      case MIR_T_U8: code = MIR_ADDR8; break;
      case MIR_T_I16:
      case MIR_T_U16: code = MIR_ADDR16; break;
      case MIR_T_I32:
      case MIR_T_U32: code = MIR_ADDR32; break;
      default: code = MIR_ADDR; break;
      }
      emit2 (c2m_ctx, code, res.mir_op, MIR_new_reg_op (ctx, op1.mir_op.u.reg));
      break;
    } else if (op1.mir_op.mode == MIR_OP_REG || op1.mir_op.mode == MIR_OP_REF
               || op1.mir_op.mode == MIR_OP_STR) { /* array or func */
      res = op1;
      res.decl = NULL;
      break;
    }
    assert (op1.mir_op.mode == MIR_OP_MEM);
    res = get_new_temp (c2m_ctx, t);
    if (op1.mir_op.u.mem.index != 0) {
      emit3 (c2m_ctx, MIR_MUL, res.mir_op, MIR_new_reg_op (ctx, op1.mir_op.u.mem.index),
             MIR_new_int_op (ctx, op1.mir_op.u.mem.scale));
      add_p = TRUE;
    }
    if (op1.mir_op.u.mem.disp != 0) {
      if (add_p)
        emit3 (c2m_ctx, MIR_ADD, res.mir_op, res.mir_op,
               MIR_new_int_op (ctx, op1.mir_op.u.mem.disp));
      else
        emit2 (c2m_ctx, MIR_MOV, res.mir_op, MIR_new_int_op (ctx, op1.mir_op.u.mem.disp));
      add_p = TRUE;
    }
    if (op1.mir_op.u.mem.base != 0) {
      if (add_p)
        emit3 (c2m_ctx, MIR_ADD, res.mir_op, res.mir_op,
               MIR_new_reg_op (ctx, op1.mir_op.u.mem.base));
      else
        emit2 (c2m_ctx, MIR_MOV, res.mir_op, MIR_new_reg_op (ctx, op1.mir_op.u.mem.base));
    }
    break;
  }
	case N_DEREF:
		    op1 = val_gen (c2m_ctx, NL_HEAD (r->u.ops));
		    op1 = force_reg (c2m_ctx, op1, MIR_T_I64);
		    assert (op1.mir_op.mode == MIR_OP_REG);
		    if (r->attr != NULL) {
		      struct expr *e = (struct expr *) r->attr;
		      type = e->type;
		      if (type != NULL && type->mode == TM_PTR
		          && type->u.ptr_type != NULL && type->u.ptr_type->mode == TM_FUNC && type->func_type_before_adjustment_p) {
		        res = op1;
		      } else {
		        /* Match N_DEREF_FIELD / N_IND: only elide the null guard when ownership
		           proved the receiver live and non-null (DEREF_GUARD_SAFE), or when the
		           receiver is the method/ctor parameter `this` (call sites emit the
		           single null trap).  CHECK/DEFAULT still trap. */
		        node_t drecv = NL_HEAD (r->u.ops);
		        int this_recv_p = (drecv != NULL && drecv->code == N_ID && drecv->u.s.s != NULL
		                           && strcmp (drecv->u.s.s, "this") == 0);
		        if (e->own_deref_class != DEREF_GUARD_SAFE && !this_recv_p) {
		          if (c2m_options->verbose_p && e->own_deref_class == DEREF_GUARD_DEFAULT)
		            warning (c2m_ctx, POS (r), "possible null dereference (ownership analysis could not prove the pointer non-null)");
		          if (c2m_options->exceptions_p)
		            gen_null_check (c2m_ctx, op1, (long) POS (r).lno);
		        }
		        if (c2m_options->object_guards_p && e->own_deref_class == DEREF_GUARD_CHECK)
		          gen_obj_guard_check (c2m_ctx, op1, (long) POS (r).lno);
		        struct expr *op_e = NL_HEAD (r->u.ops)->attr;
		        t = get_mir_type (c2m_ctx, type);
		        op1.mir_op = MIR_new_alias_mem_op (ctx, t, 0, op1.mir_op.u.reg, 0, 1,
		                                           get_type_alias (c2m_ctx, type), op_e ? op_e->type->antialias : 0);
		        res = new_op (NULL, op1.mir_op);
		      }
		    } else {
		      /* No expr attr (syntax-only or early error path) – emit plain deref without ownership guards */
		      struct expr *op_e = NL_HEAD (r->u.ops)->attr;
		      struct type *t2 = op_e ? op_e->type : NULL;
		      t = get_mir_type (c2m_ctx, t2);
		      op1.mir_op = MIR_new_alias_mem_op (ctx, t, 0, op1.mir_op.u.reg, 0, 1,
		                                         get_type_alias (c2m_ctx, t2), op_e ? op_e->type->antialias : 0);
		      res = new_op (NULL, op1.mir_op);
		    }
		    break;
  case N_FIELD:
  case N_DEREF_FIELD: {
    node_t def_node;
    MIR_alias_t alias;

    e = r->attr;
    {
      node_t obj_node = NL_HEAD (r->u.ops);
      node_t key_node = NL_NEXT (obj_node);
      struct expr *obj_e = obj_node->attr;

      /* ClassName.member where the object is the class itself (not an
         instance) and member is a declarative dict: load the singleton. */
      if (obj_node->code == N_ID && obj_e != NULL && obj_e->def_node != NULL
          && obj_e->def_node->code == N_CLASS && obj_e->type != NULL
          && obj_e->type->mode == TM_CLASS && e->type != NULL && e->type->mode == TM_DICT
          && key_node != NULL && key_node->code == N_ID) {
        MIR_item_t bss = ensure_class_static_dict (c2m_ctx, obj_e->def_node, key_node->u.s.s);
        if (bss != NULL) {
          op_t addr = get_new_temp (c2m_ctx, MIR_T_I64);
          emit2 (c2m_ctx, MIR_MOV, addr.mir_op, MIR_new_ref_op (ctx, bss));
          res = get_new_temp (c2m_ctx, MIR_T_I64);
          emit2 (c2m_ctx, MIR_MOV, res.mir_op,
                 MIR_new_mem_op (ctx, MIR_T_I64, 0, addr.mir_op.u.reg, 0, 1));
          break;
        }
      }

      /* Dict member access:  obj.key  where obj is a dict value. */
      if (obj_e != NULL && obj_e->type != NULL && obj_e->type->mode == TM_DICT
          && key_node != NULL && key_node->code == N_ID) {
        const char *key_str = key_node->u.s.s;
        /* Bare d.json is ordinary key lookup (same as d.length).  Serialize with
           d.json() — see N_CALL dict-method path below. */
        op1 = val_gen (c2m_ctx, obj_node);
        MIR_op_t key_op = gen_dict_key_op (c2m_ctx, key_str, strlen (key_str) + 1);
        op_t got = gen_dict_object_get (c2m_ctx, op1.mir_op, key_op);
        /* A nested dict stays a DictValue* (for chaining); a scalar/string
           result is unwrapped to its union payload. */
        res = (e->type != NULL && e->type->mode == TM_DICT)
                ? got : gen_dict_unwrap (c2m_ctx, got);
        break;
      }
    }
    if (e->def_node && e->def_node->code == N_FUNC_DEF) {
      // method access: produce MIR ref to the function item (with mangled name)
      decl_t mdecl = (decl_t) e->def_node->attr;
      MIR_item_t fitem = mdecl ? mdecl->u.item : NULL;
      if (fitem) {
        res = new_op (NULL, MIR_new_ref_op (ctx, fitem));
        break;
      }
      /* Midopt may have pruned a method still named in residual AST (e.g. a
         monomorph branch that is never taken at runtime).  Prefer a clear
         diagnostic over falling through to the N_MEMBER path. */
      if (mdecl != NULL && mdecl->midopt_dead_p) {
        warning (c2m_ctx, POS (r),
                 "internal: reference to midopt-dead method (null MIR item)");
        res = zero_op;
        break;
      }
    }
    def_node = e->u.lvalue_node;
    assert (def_node != NULL && def_node->code == N_MEMBER);
    decl = def_node->attr;
    op1 = gen (c2m_ctx, NL_HEAD (r->u.ops), NULL, NULL, r->code == N_DEREF_FIELD, NULL, NULL);
    t = get_mir_type (c2m_ctx, decl->decl_spec.type);
    if (r->code == N_FIELD) {
      assert (op1.mir_op.mode == MIR_OP_MEM);
      alias = (op1.mir_op.u.mem.alias != 0 && MIR_alias_name (ctx, op1.mir_op.u.mem.alias)[0] == 'U'
                 ? op1.mir_op.u.mem.alias
                 : get_type_alias (c2m_ctx, e->type));
      op1.mir_op
        = MIR_new_alias_mem_op (ctx, t, op1.mir_op.u.mem.disp + decl->offset, op1.mir_op.u.mem.base,
                                op1.mir_op.u.mem.index, op1.mir_op.u.mem.scale, alias,
                                decl->decl_spec.type->antialias);
    } else {
      struct expr *left = NL_HEAD (r->u.ops)->attr;
      assert (left->type->mode == TM_PTR);
      op1 = force_reg (c2m_ctx, op1, MIR_T_I64);
      assert (op1.mir_op.mode == MIR_OP_REG);
      /* Null-pointer guard before ptr->field access.  Elided when ownership
         proved the receiver live (DEREF_GUARD_SAFE) or the receiver is the
         method/ctor `this` parameter (call site emits the single null trap). */
      {
        node_t frecv = NL_HEAD (r->u.ops);
        int this_recv_p = (frecv != NULL && frecv->code == N_ID && frecv->u.s.s != NULL
                           && strcmp (frecv->u.s.s, "this") == 0);
        if (c2m_options->exceptions_p
            && ((struct expr *) r->attr)->own_deref_class != DEREF_GUARD_SAFE
            && !this_recv_p)
          gen_null_check (c2m_ctx, op1, (long) POS (r).lno);
      }
      /* -fobject-guards: liveness check at ownership-CHECK (MaybeOwned) sites. */
      if (c2m_options->object_guards_p
          && ((struct expr *) r->attr)->own_deref_class == DEREF_GUARD_CHECK)
        gen_obj_guard_check (c2m_ctx, op1, (long) POS (r).lno);
      /* Note: use-after-free detection via cy_safe_deref is available in the
         runtime but not auto-emitted here.  Without intercepting ALL malloc calls
         (including runtime-internal ones) the per-deref check produces false
         positives when a non-tracked allocation reuses a freed address.  The
         null-out after `delete` (below) catches the direct same-variable case. */
      op1.mir_op
        = MIR_new_alias_mem_op (ctx, t, decl->offset, op1.mir_op.u.reg, 0, 1,
                                get_type_alias (c2m_ctx, left->type->u.ptr_type->mode == TM_UNION
                                                           ? left->type->u.ptr_type
                                                           : e->type),
                                decl->decl_spec.type->antialias);
    }
    res = new_op (decl, op1.mir_op);
    break;
  }
  case N_COND: {
    node_t cond = NL_HEAD (r->u.ops);
    node_t true_expr = NL_NEXT (cond);
    node_t false_expr = NL_NEXT (true_expr);
    MIR_label_t cond_true_label = MIR_new_label (ctx), cond_false_label = MIR_new_label (ctx);
    MIR_label_t end_label = MIR_new_label (ctx);
    struct type *cond_res_type = ((struct expr *) r->attr)->type;
    op_t addr;
    int void_p = void_type_p (cond_res_type), cond_expect_res, cond_known;
    mir_size_t size = type_size (c2m_ctx, cond_res_type);

    cond_known = c11_cond_known (cond);
    if ((cond_known == 1 && c11_dead_skippable_p (false_expr))
        || (cond_known == 0 && c11_dead_skippable_p (true_expr))) {
      gen (c2m_ctx, cond, NULL, NULL, FALSE, NULL, NULL);
      res = gen (c2m_ctx, cond_known == 1 ? true_expr : false_expr, true_label, false_label,
                 val_p, desirable_dest, expect_res);
      break;
    }
    if (!void_p) t = get_mir_type (c2m_ctx, cond_res_type);
    gen (c2m_ctx, cond, cond_true_label, cond_false_label, FALSE, NULL, &cond_expect_res);
    emit_label_insn_opt (c2m_ctx, cond_true_label);
    op1 = gen (c2m_ctx, true_expr, NULL, NULL, !void_p && t != MIR_T_UNDEF, NULL, NULL);
    if (!void_p) {
      if (t != MIR_T_UNDEF) {
        res = get_new_temp (c2m_ctx, t);
        op1 = cast (c2m_ctx, op1, t, FALSE);
        emit2 (c2m_ctx, tp_mov (t), res.mir_op, op1.mir_op);
      } else if (desirable_dest == NULL) {
        res = get_new_temp (c2m_ctx, MIR_T_I64);
        addr = mem_to_address (c2m_ctx, op1, FALSE);
        emit2 (c2m_ctx, MIR_MOV, res.mir_op, addr.mir_op);
      } else {
        block_move (c2m_ctx, *desirable_dest, op1, size);
        res = *desirable_dest;
      }
    }
    emit1 (c2m_ctx, MIR_JMP, MIR_new_label_op (ctx, end_label));
    emit_label_insn_opt (c2m_ctx, cond_false_label);
    op1 = gen (c2m_ctx, false_expr, NULL, NULL, !void_p && t != MIR_T_UNDEF, NULL, NULL);
    if (!void_p) {
      if (t != MIR_T_UNDEF) {
        op1 = cast (c2m_ctx, op1, t, FALSE);
        emit2 (c2m_ctx, tp_mov (t), res.mir_op, op1.mir_op);
      } else if (desirable_dest == NULL) {
        addr = mem_to_address (c2m_ctx, op1, FALSE);
        emit2 (c2m_ctx, MIR_MOV, res.mir_op, addr.mir_op);
        res = new_op (NULL, MIR_new_mem_op (ctx, MIR_T_I8, 0, res.mir_op.u.reg, 0, 1));
      } else {
        block_move (c2m_ctx, res, op1, size);
      }
    }
    emit_label_insn_opt (c2m_ctx, end_label);
    break;
  }
  case N_COALESCE: { /* a ?? b — a's value if non-zero/non-null, else b; a evaluated once */
    node_t val_expr = NL_HEAD (r->u.ops);
    node_t def_expr = NL_EL (r->u.ops, 1);
    struct type *res_type = ((struct expr *) r->attr)->type;
    MIR_label_t end_label = MIR_new_label (ctx);
    int coal_known = c11_cond_known (val_expr);

    if (coal_known == 1 && c11_dead_skippable_p (def_expr)) {
      res = val_gen (c2m_ctx, val_expr);
      break;
    }
    if (coal_known == 0) {
      gen (c2m_ctx, val_expr, NULL, NULL, FALSE, NULL, NULL);
      res = val_gen (c2m_ctx, def_expr);
      break;
    }
    t = get_mir_type (c2m_ctx, res_type);
    res = get_new_temp (c2m_ctx, t);
    op1 = val_gen (c2m_ctx, val_expr);
    op1 = cast (c2m_ctx, op1, t, FALSE);
    emit2 (c2m_ctx, tp_mov (t), res.mir_op, op1.mir_op);
    /* keep a's value when it is non-zero/non-null */
    if (t == MIR_T_F)
      emit3 (c2m_ctx, MIR_FBNE, MIR_new_label_op (ctx, end_label), res.mir_op,
             MIR_new_float_op (ctx, 0.0f));
    else if (t == MIR_T_D)
      emit3 (c2m_ctx, MIR_DBNE, MIR_new_label_op (ctx, end_label), res.mir_op,
             MIR_new_double_op (ctx, 0.0));
    else if (t == MIR_T_LD)
      emit3 (c2m_ctx, MIR_LDBNE, MIR_new_label_op (ctx, end_label), res.mir_op,
             MIR_new_ldouble_op (ctx, 0.0));
    else
      emit2 (c2m_ctx, MIR_BT, MIR_new_label_op (ctx, end_label), res.mir_op);
    op1 = val_gen (c2m_ctx, def_expr);
    op1 = cast (c2m_ctx, op1, t, FALSE);
    emit2 (c2m_ctx, tp_mov (t), res.mir_op, op1.mir_op);
    emit_label_insn_opt (c2m_ctx, end_label);
    break;
  }
  case N_ALIGNOF:
  case N_SIZEOF:
  case N_EXPR_SIZEOF: assert (FALSE); break;
  case N_CAST: {
    struct expr *cast_e = (struct expr *) r->attr;
    assert (!cast_e->const_p);
    type = cast_e->type;
    /* Dict-to-class bind cast: (T)d / (T?)d — lowered to a per-field walk
       over T's members.  Strict (no `?`) throws KeyException on a missing
       field; lenient (`?`) leaves the field at zero.  Source-level checking
       (target is TM_CLASS, source is TM_DICT) happens in the N_CAST checker;
       here we just need to produce a class-shaped result. */
    if (cast_e->bind_p) {
      node_t src_node = NL_EL (r->u.ops, 1);
      op_t src_op = val_gen (c2m_ctx, src_node);
      op_t src_reg = force_reg (c2m_ctx, src_op, MIR_T_I64);

      mir_size_t cls_size = type_size (c2m_ctx, type);
      /* alloca a fresh buffer for the result.  A future optimization could
         honor `desirable_dest` to skip the buffer when the consumer is an
         aggregate assignment, but the straightforward alloca path is correct
         in every context (initializer, function argument, return). */
      op_t dst_addr = get_new_temp (c2m_ctx, MIR_T_I64);
      MIR_append_insn (ctx, curr_func,
                       MIR_new_insn (ctx, MIR_ALLOCA, dst_addr.mir_op,
                                     MIR_new_int_op (ctx, (long) cls_size)));
      /* Zero-fill so any field we skip (lenient missing, or a member declared
         but not populated) reads as 0 / NULL rather than uninitialized junk. */
      gen_memset (c2m_ctx, 0, dst_addr.mir_op.u.reg, cls_size);

      gen_dict_bind_into (c2m_ctx, type, src_reg, dst_addr,
                          cast_e->lenient_p, POS (r));

      /* Result: the class value stored at dst_addr.  An aggregate MEM op with
         MIR_T_UNDEF and a zero displacement is what every other class-by-value
         producer (call return, compound literal) hands back; the consumer
         (assignment / init / argument passing) handles the block move. */
      res = new_op (NULL, MIR_new_mem_op (ctx, MIR_T_UNDEF, 0,
                                          dst_addr.mir_op.u.reg, 0, 1));
      break;
    }
    op1 = gen (c2m_ctx, NL_EL (r->u.ops, 1), NULL, NULL, !void_type_p (type), NULL, NULL);
    if (void_type_p (type)) {
      res = op1;
      res.decl = NULL;
      res.mir_op.mode = MIR_OP_UNDEF;
    } else {
      /* Class/struct VALUE cast to a pointer type (the generic-code escape
         hatch allowed in the N_CAST checker): reinterpret the aggregate as a
         pointer by taking the address of its storage.  Without this the
         aggregate MEM op (MIR_T_UNDEF) flows into a scalar MOV and MIR
         rejects it ("wrong type memory").  This path is only reached for
         by-value class/struct operands — String is scalar and never lands
         here — so it doesn't affect string/scalar casts. */
      {
        node_t src_node = NL_EL (r->u.ops, 1);
        struct expr *src_e = src_node ? (struct expr *) src_node->attr : NULL;
        if (src_e && src_e->type
            && (src_e->type->mode == TM_CLASS || src_e->type->mode == TM_STRUCT)
            && type->mode == TM_PTR && op1.mir_op.mode == MIR_OP_MEM) {
          res = mem_to_address (c2m_ctx, op1, TRUE);
          break;
        }
      }
      /* If source is a DictValue* (TM_DICT), unwrap the union payload first.
         This extracts int64_value for integer targets, string_value for pointer
         targets.  Offset 8 in DictValue is the start of the value union. */
      t = get_mir_type (c2m_ctx, type);
      {
        node_t src_node = NL_EL (r->u.ops, 1);
        struct expr *src_e = src_node ? (struct expr *) src_node->attr : NULL;
        if (src_e && src_e->type && src_e->type->mode == TM_DICT
            && type->mode != TM_DICT) {
          if (floating_type_p (type)) {
            /* Floating target (e.g. `(double)d.price`): the union payload holds
               the value's raw bits (dict_create_number stores a `double`), so
               read offset 8 with the float type directly rather than unwrapping
               an I64 and doing a bogus int->float numeric conversion. */
            MIR_type_t load_t = (t == MIR_T_LD) ? MIR_T_D : t;
            op_t dvp = force_reg (c2m_ctx, op1, MIR_T_I64);
            op_t fval = get_new_temp (c2m_ctx, load_t);
            /* NULL-guard (missing key / JSON null): yield 0.0 instead of
               dereferencing offset 8 of NULL, mirroring gen_dict_unwrap. */
            MIR_label_t fdone = MIR_new_label (ctx);
            emit2 (c2m_ctx, tp_mov (load_t), fval.mir_op,
                   load_t == MIR_T_F ? MIR_new_float_op (ctx, 0.0f)
                                     : MIR_new_double_op (ctx, 0.0));
            emit3 (c2m_ctx, MIR_BEQ, MIR_new_label_op (ctx, fdone), dvp.mir_op,
                   MIR_new_int_op (ctx, 0));
            emit2 (c2m_ctx, tp_mov (load_t), fval.mir_op,
                   MIR_new_mem_op (ctx, load_t, 8, dvp.mir_op.u.reg, 0, 1));
            emit_label_insn_opt (c2m_ctx, fdone);
            res = (t == MIR_T_LD) ? cast (c2m_ctx, fval, t, TRUE) : fval;
            break;
          }
          op1 = gen_dict_unwrap (c2m_ctx, op1);
        }
      }
      res = cast (c2m_ctx, op1, t, TRUE);
    }
    break;
  }
  case N_COMPOUND_LITERAL: {
    const char *global_name = NULL;
    char buff[50];
    node_t type_name = NL_HEAD (r->u.ops);
    struct expr *expr = r->attr;
    MIR_module_t module = DLIST_TAIL (MIR_module_t, *MIR_get_module_list (ctx));
    size_t init_start;

    decl = type_name->attr;
    if (decl->scope == top_scope) {
      assert (decl->u.item == NULL);
      _MIR_get_temp_item_name (ctx, module, buff, sizeof (buff));
      global_name = buff;
    }
    init_start = VARR_LENGTH (init_el_t, init_els);
    collect_init_els (c2m_ctx, NULL, &decl->decl_spec.type, NL_EL (r->u.ops, 1),
                      decl->scope == top_scope || decl->decl_spec.linkage == N_STATIC
                        || decl->decl_spec.linkage == N_EXTERN || decl->decl_spec.static_p
                        || decl->decl_spec.thread_local_p,
                      TRUE);
    qsort (VARR_ADDR (init_el_t, init_els) + init_start,
           VARR_LENGTH (init_el_t, init_els) - init_start, sizeof (init_el_t), cmp_init_el);
    if (decl->scope == top_scope || decl->decl_spec.static_p || decl->decl_spec.thread_local_p) {
      var = new_op (decl, MIR_new_ref_op (ctx, NULL));
    } else {
      t = get_mir_type (c2m_ctx, expr->type);
      var = new_op (decl, MIR_new_alias_mem_op (ctx, t, decl->offset,
                                                MIR_reg (ctx, FP_NAME, curr_func->u.func), 0, 1,
                                                get_type_alias (c2m_ctx, expr->type), 0));
    }
    int local_p
      = decl->scope != top_scope && !decl->decl_spec.static_p && !decl->decl_spec.thread_local_p;
    gen_initializer (c2m_ctx, init_start, var, global_name,
                     type_size (c2m_ctx, decl->decl_spec.type),
                     local_p);
    VARR_TRUNC (init_el_t, init_els, init_start);
    if (var.mir_op.mode == MIR_OP_REF) var.mir_op.u.ref = var.decl->u.item;
    res = var;
    break;
  }
  case N_ANY: {
    /* any<I>(expr) was lowered in check to a factory call (3rd child); emit it. */
    node_t call = NL_EL (r->u.ops, 2);
    if (call != NULL && call->code == N_CALL)
      res = gen (c2m_ctx, call, true_label, false_label, val_p, desirable_dest, expect_res);
    break;
  }
  case N_NEW: {
    /* new ClassName(args): obj = malloc(sizeof(Class)); memset 0; ctor(obj,args). */
    struct expr *ne = r->attr;
    node_t type_id = NL_HEAD (r->u.ops);
    /* ── new dict(size?) ─────────────────────────────────────────────── */
    if (type_id->code == N_DICT) {
      node_t size_arg = NL_HEAD (NL_NEXT (type_id)->u.ops);
      MIR_op_t size_op = size_arg
        ? val_gen (c2m_ctx, size_arg).mir_op
        : MIR_new_int_op (c2m_ctx->ctx, 0); /* 0 = use default in dict_create_heap_arena */
      res = gen_dict_create_heap_arena_call (c2m_ctx, size_op);
      break;
    }
    node_t arg_list = NL_NEXT (type_id);
    node_t ctor_def = ne->def_node;
    struct type *class_type = ne->type->u.ptr_type;
    mir_size_t csize = type_size (c2m_ctx, class_type);
    op_t obj = gen_heap_alloc (c2m_ctx, csize == 0 ? 1 : csize);
    /* OOM guard: throw RuntimeException("out of memory") if malloc returned NULL. */
    if (c2m_options->exceptions_p)
      gen_oom_check (c2m_ctx, obj, (long) POS (r).lno);

    if (csize > 0) gen_memset (c2m_ctx, 0, obj.mir_op.u.reg, csize);
    if (ctor_def != NULL) {
      decl_t cdecl = ctor_def->attr;
      struct func_type *ft = cdecl->decl_spec.type->u.func_type;
      MIR_item_t proto;
      char pname[64];
      size_t ops_start;
      node_t param;

      collect_args_and_func_types (c2m_ctx, ft, NULL);
      sprintf (pname, "__ctorproto%d", new_proto_count++);
      proto = MIR_new_proto_arr (ctx, pname,
                                 VARR_LENGTH (MIR_type_t, proto_info.ret_types),
                                 VARR_ADDR (MIR_type_t, proto_info.ret_types),
                                 VARR_LENGTH (MIR_var_t, proto_info.arg_vars),
                                 VARR_ADDR (MIR_var_t, proto_info.arg_vars));
      move_item_to_module_start (curr_func->module, proto);
      ops_start = VARR_LENGTH (MIR_op_t, call_ops);
      VARR_PUSH (MIR_op_t, call_ops, MIR_new_ref_op (ctx, proto));
      VARR_PUSH (MIR_op_t, call_ops, MIR_new_ref_op (ctx, cdecl->u.item));
      {
        /* Use target_add_call_arg_op for all constructor args so that
           aggregate (class/struct) args get proper BLK memory operands. */
        target_arg_info_t new_arg_info;
        target_init_arg_vars (c2m_ctx, &new_arg_info);
        /* 'this' pointer — push directly and record register usage. */
        target_add_call_arg_op (c2m_ctx, ne->type, &new_arg_info, obj);
        param = NL_HEAD (ft->param_list->u.ops);
        if (param != NULL) param = NL_NEXT (param); /* skip 'this' */
        for (node_t a = NL_HEAD (arg_list->u.ops); a != NULL; a = NL_NEXT (a)) {
          struct type *a_type = ((struct expr *) a->attr)->type;
          int is_agg = (a_type->mode == TM_STRUCT || a_type->mode == TM_UNION
                        || a_type->mode == TM_CLASS);
          op_t av = gen (c2m_ctx, a, NULL, NULL, !is_agg, NULL, NULL);
          if (param != NULL) {
            struct decl_spec *pds = get_param_decl_spec (param);
            a_type = pds->type;
            is_agg = (a_type->mode == TM_STRUCT || a_type->mode == TM_UNION
                      || a_type->mode == TM_CLASS);
            if (!is_agg && scalar_type_p (a_type))
              av = promote (c2m_ctx, av,
                            promote_mir_int_type (get_mir_type (c2m_ctx, a_type)), FALSE);
            param = NL_NEXT (param);
          }
          target_add_call_arg_op (c2m_ctx, a_type, &new_arg_info, av);
        }
      }
      emit_insn (c2m_ctx,
                 MIR_new_insn_arr (ctx, MIR_CALL,
                                   VARR_LENGTH (MIR_op_t, call_ops) - ops_start,
                                   VARR_ADDR (MIR_op_t, call_ops) + ops_start));
      VARR_TRUNC (MIR_op_t, call_ops, ops_start);
    }
    /* Brace-init:  new T{e1, e2, ...} — emit one obj->Add(e) call per element
       (resolved through the same protocol helper as check).

       Object-initializer:  new T(args) { .field = value, ... } — store each
       value into the named member of the freshly-constructed object (after
       the constructor ran), reusing the same field-store conventions as the
       dict->class binder (value-semantic String ownership, scalar coercion,
       by-value aggregate copy). */
    {
      node_t init_list = NL_NEXT (arg_list);
      node_t first_init = (init_list != NULL) ? NL_HEAD (init_list->u.ops) : NULL;

      if (first_init != NULL && first_init->code == N_FIELD_ID) {
        op_t base = force_reg (c2m_ctx, obj, MIR_T_I64);
        for (node_t el = NL_HEAD (init_list->u.ops); el != NULL; el = NL_NEXT (el)) {
          node_t fname = NL_HEAD (el->u.ops);
          node_t fval = (fname != NULL) ? NL_NEXT (fname) : NULL;
          node_t mem = find_class_field_member (c2m_ctx, class_type->u.tag_type,
                                                 fname->u.s.s);
          decl_t md = (mem != NULL) ? (decl_t) mem->attr : NULL;
          struct type *mtype;
          mir_size_t moff;
          if (md == NULL || fval == NULL) continue; /* validated during check */
          mtype = md->decl_spec.type;
          moff = md->offset;
          if (mtype->mode == TM_STRUCT || mtype->mode == TM_CLASS
              || mtype->mode == TM_UNION) {
            /* By-value aggregate field: block-copy from the value's address. */
            op_t src = gen (c2m_ctx, fval, NULL, NULL, FALSE, NULL, NULL);
            MIR_op_t dst_mem = MIR_new_mem_op (ctx, MIR_T_I8, (MIR_disp_t) moff,
                                               base.mir_op.u.reg, 0, 1);
            block_move (c2m_ctx, new_op (NULL, dst_mem), src, type_size (c2m_ctx, mtype));
          } else if (builtin_string_type_p (mtype)) {
            /* Value-semantic String member: own a private heap copy (c2m_str_own)
               so `delete obj` frees it, exactly like `obj->s = ...`. */
            op_t v = val_gen (c2m_ctx, fval);
            MIR_op_t own_arg;
            op_t owned;
            MIR_type_t mt = get_mir_type (c2m_ctx, mtype);
            MIR_alias_t alias = get_type_alias (c2m_ctx, mtype);
            string_ensure_imports (c2m_ctx);
            own_arg = force_reg (c2m_ctx, v, MIR_T_I64).mir_op;
            owned = gen_rt_call (c2m_ctx, str_own_proto, str_own_item, 1, &own_arg);
            owned = force_reg (c2m_ctx, owned, MIR_T_I64);
            emit2 (c2m_ctx, tp_mov (mt),
                   MIR_new_alias_mem_op (ctx, mt, (MIR_disp_t) moff, base.mir_op.u.reg,
                                         0, 1, alias, 0),
                   owned.mir_op);
          } else {
            /* Scalar / pointer / char* field: coerce to the member type and store. */
            op_t v = val_gen (c2m_ctx, fval);
            MIR_type_t mt = get_mir_type (c2m_ctx, mtype);
            MIR_op_t dst_mem = MIR_new_alias_mem_op (ctx, mt, (MIR_disp_t) moff,
                                                     base.mir_op.u.reg, 0, 1,
                                                     get_type_alias (c2m_ctx, mtype), 0);
            v = cast (c2m_ctx, v, mt, FALSE);
            emit_scalar_assign (c2m_ctx, new_op (md, dst_mem), &v, mt, FALSE);
          }
        }
      } else if (init_list != NULL && NL_HEAD (init_list->u.ops) != NULL) {
        node_t add_def
          = find_class_protocol_method (c2m_ctx, class_type->u.tag_type, "Add", 1, POS (r));

        assert (add_def != NULL); /* validated during check */
        for (node_t el = NL_HEAD (init_list->u.ops); el != NULL; el = NL_NEXT (el)) {
          struct expr *ee = el->attr;
          int el_agg_p = (ee->type->mode == TM_STRUCT || ee->type->mode == TM_UNION
                          || ee->type->mode == TM_CLASS);
          op_t av = gen (c2m_ctx, el, NULL, NULL, !el_agg_p, NULL, NULL);

          gen_class_method_call (c2m_ctx, add_def, ne->type, obj, &av, 1);
        }
      }
    }
    /* -fobject-guards: register this `new` class object in the liveness side
       table so use-after-free / double-free through it is detectable. */
    if (c2m_options->object_guards_p)
      gen_obj_guard_track (c2m_ctx, obj.mir_op);
    res = obj;
    break;
  }
  case N_CALL: {
    node_t func = NL_HEAD (r->u.ops), param_list, param, args = NL_EL (r->u.ops, 1), first_arg;
    struct decl_spec *decl_spec;
    size_t ops_start;
    struct expr *call_expr = r->attr, *func_expr;
    struct type *func_type = NULL; /* to remove an uninitialized warning */
    MIR_item_t proto_item;
    MIR_insn_t call_insn, label;
    mir_size_t saved_call_arg_area_offset_before_args, arg_area_offset;
    int va_arg_p = call_expr->builtin_call_p && str_eq_p (func->u.s.s, BUILTIN_VA_ARG);
    int va_start_p = call_expr->builtin_call_p && str_eq_p (func->u.s.s, BUILTIN_VA_START);
    int alloca_p = call_expr->builtin_call_p && str_eq_p (func->u.s.s, ALLOCA);
    int add_overflow_p = call_expr->builtin_call_p && strcmp (func->u.s.s, ADD_OVERFLOW) == 0;
    int sub_overflow_p = call_expr->builtin_call_p && strcmp (func->u.s.s, SUB_OVERFLOW) == 0;
    int mul_overflow_p = call_expr->builtin_call_p && strcmp (func->u.s.s, MUL_OVERFLOW) == 0;
    int expect_p = call_expr->builtin_call_p && strcmp (func->u.s.s, EXPECT) == 0;
    int jcall_p = call_expr->builtin_call_p && strcmp (func->u.s.s, JCALL) == 0;
    int jret_p = call_expr->builtin_call_p && strcmp (func->u.s.s, JRET) == 0;
    int prop_set_p = call_expr->builtin_call_p && strcmp (func->u.s.s, PROP_SET) == 0;
    int prop_eq_p = call_expr->builtin_call_p && strcmp (func->u.s.s, PROP_EQ) == 0;
    int prop_ne_p = call_expr->builtin_call_p && strcmp (func->u.s.s, PROP_NE) == 0;
    int json_p = call_expr->builtin_call_p && strcmp (func->u.s.s, BUILTIN_JSON) == 0;
    int atomic_load_n_p
      = call_expr->builtin_call_p && strcmp (func->u.s.s, ATOMIC_LOAD_N) == 0;
    int atomic_store_n_p
      = call_expr->builtin_call_p && strcmp (func->u.s.s, ATOMIC_STORE_N) == 0;
    int atomic_exchange_n_p
      = call_expr->builtin_call_p && strcmp (func->u.s.s, ATOMIC_EXCHANGE_N) == 0;
    int atomic_fetch_add_p
      = call_expr->builtin_call_p && strcmp (func->u.s.s, ATOMIC_FETCH_ADD) == 0;
    int atomic_fetch_sub_p
      = call_expr->builtin_call_p && strcmp (func->u.s.s, ATOMIC_FETCH_SUB) == 0;
    int atomic_fetch_and_p
      = call_expr->builtin_call_p && strcmp (func->u.s.s, ATOMIC_FETCH_AND) == 0;
    int atomic_fetch_or_p
      = call_expr->builtin_call_p && strcmp (func->u.s.s, ATOMIC_FETCH_OR) == 0;
    int atomic_fetch_xor_p
      = call_expr->builtin_call_p && strcmp (func->u.s.s, ATOMIC_FETCH_XOR) == 0;
    int atomic_cas_n_p
      = call_expr->builtin_call_p && strcmp (func->u.s.s, ATOMIC_COMPARE_EXCHANGE_N) == 0;
    int atomic_fence_p
      = call_expr->builtin_call_p && strcmp (func->u.s.s, ATOMIC_THREAD_FENCE) == 0;
    int atomic_builtin_p
      = atomic_load_n_p || atomic_store_n_p || atomic_exchange_n_p || atomic_fetch_add_p
        || atomic_fetch_sub_p || atomic_fetch_and_p || atomic_fetch_or_p || atomic_fetch_xor_p
        || atomic_cas_n_p || atomic_fence_p;
    int builtin_call_p = alloca_p || va_arg_p || va_start_p, inline_p = FALSE;
    node_t block = FUNC_DEF_BLOCK (curr_func_def);
    struct node_scope *ns = block->attr;
    target_arg_info_t arg_info;
    int n, struct_p;
    type = call_expr->type;
    /* __destroy(x) intrinsic (resolved in check): run x's destructor in place if
       its class has one, else emit nothing.  Modeled on the N_DELETE dtor call,
       minus the heap free (the buffer free is the template's responsibility). */
    /* ClassName(args) value temporary: construct into call-arg-area stack slot. */
    if (call_expr->builtin_call_p && call_expr->type != NULL
        && call_expr->type->mode == TM_CLASS && func->code == N_ID) {
      node_t ctor_def = call_expr->def_node;
      mir_size_t csize = type_size (c2m_ctx, call_expr->type);
      mir_size_t arg_area_offset = curr_call_arg_area_offset + ns->size - ns->call_arg_area_size;
      op_t base, this_ptr;

      /* Stack address of the temporary slot. */
      base = get_new_temp (c2m_ctx, MIR_T_I64);
      emit3 (c2m_ctx, MIR_ADD, base.mir_op,
             MIR_new_reg_op (ctx, MIR_reg (ctx, FP_NAME, curr_func->u.func)),
             MIR_new_int_op (ctx, arg_area_offset));
      update_call_arg_area_offset (c2m_ctx, call_expr->type, FALSE);
      if (csize > 0) gen_memset (c2m_ctx, 0, base.mir_op.u.reg, csize);
      this_ptr = base;

      if (ctor_def != NULL && ctor_def->code == N_FUNC_DEF) {
        decl_t cdecl = ctor_def->attr;
        struct func_type *ft = cdecl->decl_spec.type->u.func_type;
        MIR_item_t proto;
        char pname[64];
        size_t cops_start;
        node_t param;
        target_arg_info_t ctor_arg_info;
        struct type *this_ptr_type = create_type (c2m_ctx, NULL);

        this_ptr_type->mode = TM_PTR;
        this_ptr_type->u.ptr_type = call_expr->type;
        set_type_layout (c2m_ctx, this_ptr_type);

        collect_args_and_func_types (c2m_ctx, ft, NULL);
        sprintf (pname, "__valctorproto%d", new_proto_count++);
        proto = MIR_new_proto_arr (ctx, pname,
                                   VARR_LENGTH (MIR_type_t, proto_info.ret_types),
                                   VARR_ADDR (MIR_type_t, proto_info.ret_types),
                                   VARR_LENGTH (MIR_var_t, proto_info.arg_vars),
                                   VARR_ADDR (MIR_var_t, proto_info.arg_vars));
        move_item_to_module_start (curr_func->module, proto);
        cops_start = VARR_LENGTH (MIR_op_t, call_ops);
        VARR_PUSH (MIR_op_t, call_ops, MIR_new_ref_op (ctx, proto));
        VARR_PUSH (MIR_op_t, call_ops, MIR_new_ref_op (ctx, cdecl->u.item));
        target_init_arg_vars (c2m_ctx, &ctor_arg_info);
        target_add_call_arg_op (c2m_ctx, this_ptr_type, &ctor_arg_info, this_ptr);
        param = NL_HEAD (ft->param_list->u.ops);
        if (param != NULL) param = NL_NEXT (param);
        for (node_t a = NL_HEAD (args->u.ops); a != NULL; a = NL_NEXT (a)) {
          struct type *a_type = ((struct expr *) a->attr)->type;
          int is_agg = (a_type->mode == TM_STRUCT || a_type->mode == TM_UNION
                        || a_type->mode == TM_CLASS);
          op_t av = gen (c2m_ctx, a, NULL, NULL, !is_agg, NULL, NULL);
          if (param != NULL) {
            struct decl_spec *pds = get_param_decl_spec (param);
            a_type = pds->type;
            is_agg = (a_type->mode == TM_STRUCT || a_type->mode == TM_UNION
                      || a_type->mode == TM_CLASS);
            if (!is_agg && scalar_type_p (a_type))
              av = promote (c2m_ctx, av,
                            promote_mir_int_type (get_mir_type (c2m_ctx, a_type)), FALSE);
            param = NL_NEXT (param);
          }
          target_add_call_arg_op (c2m_ctx, a_type, &ctor_arg_info, av);
        }
        emit_insn (c2m_ctx,
                   MIR_new_insn_arr (ctx, MIR_CALL,
                                     VARR_LENGTH (MIR_op_t, call_ops) - cops_start,
                                     VARR_ADDR (MIR_op_t, call_ops) + cops_start));
        VARR_TRUNC (MIR_op_t, call_ops, cops_start);
      }
      /* Yield the temporary as a memory aggregate (value-semantic class result). */
      res = new_op (NULL, MIR_new_mem_op (ctx, MIR_T_UNDEF, 0, base.mir_op.u.reg, 0, 1));
      break;
    }
    if (func->code == N_ID && strcmp (func->u.s.s, "__destroy") == 0
        && call_expr->builtin_call_p) {
      node_t dtor_def = call_expr->def_node;
      if (dtor_def != NULL && dtor_def->code == N_FUNC_DEF) {
        node_t darg = NL_HEAD (args->u.ops);
        op_t lv = gen (c2m_ctx, darg, NULL, NULL, FALSE, NULL, NULL);
        op_t addr = mem_to_address (c2m_ctx, lv, TRUE);
        decl_t cdecl = dtor_def->attr;
        struct func_type *dft = cdecl->decl_spec.type->u.func_type;
        MIR_item_t dproto;
        char dpname[64];
        size_t dops_start;
        collect_args_and_func_types (c2m_ctx, dft, NULL);
        sprintf (dpname, "__dtorproto%d", new_proto_count++);
        dproto = MIR_new_proto_arr (ctx, dpname,
                                    VARR_LENGTH (MIR_type_t, proto_info.ret_types),
                                    VARR_ADDR (MIR_type_t, proto_info.ret_types),
                                    VARR_LENGTH (MIR_var_t, proto_info.arg_vars),
                                    VARR_ADDR (MIR_var_t, proto_info.arg_vars));
        move_item_to_module_start (curr_func->module, dproto);
        dops_start = VARR_LENGTH (MIR_op_t, call_ops);
        VARR_PUSH (MIR_op_t, call_ops, MIR_new_ref_op (ctx, dproto));
        VARR_PUSH (MIR_op_t, call_ops, MIR_new_ref_op (ctx, cdecl->u.item));
        VARR_PUSH (MIR_op_t, call_ops, addr.mir_op); /* implicit 'this' */
        emit_insn (c2m_ctx,
                   MIR_new_insn_arr (ctx, MIR_CALL,
                                     VARR_LENGTH (MIR_op_t, call_ops) - dops_start,
                                     VARR_ADDR (MIR_op_t, call_ops) + dops_start));
        VARR_TRUNC (MIR_op_t, call_ops, dops_start);
      }
      break;
    }
    if (add_overflow_p || sub_overflow_p || mul_overflow_p) {
      op1 = val_gen (c2m_ctx, NL_HEAD (args->u.ops));
      op2 = val_gen (c2m_ctx, NL_EL (args->u.ops, 1));
      op3 = val_gen (c2m_ctx, NL_EL (args->u.ops, 2));
      e = NL_EL (args->u.ops, 2)->attr;
      assert (e->type->mode == TM_PTR && standard_integer_type_p (e->type->u.ptr_type));
      t = get_mir_type (c2m_ctx, e->type->u.ptr_type);
      assert (op3.mir_op.mode == MIR_OP_REG);
      MIR_append_insn (ctx, curr_func,
                       MIR_new_insn (ctx,
                                     t == MIR_T_I32 || t == MIR_T_U32
                                       ? (add_overflow_p ? MIR_ADDOS
                                          : sub_overflow_p ? MIR_SUBOS
                                          : t == MIR_T_I32 ? MIR_MULOS
                                                           : MIR_UMULOS)
                                       : (add_overflow_p ? MIR_ADDO
                                          : sub_overflow_p ? MIR_SUBO
                                          : t == MIR_T_I64 ? MIR_MULO
                                                           : MIR_UMULO),
                                     MIR_new_mem_op (ctx, t, 0, op3.mir_op.u.reg, 0, 1), op1.mir_op,
                                     op2.mir_op));
      if (true_label != NULL) {
        MIR_op_t lab_op = MIR_new_label_op (ctx, true_label);
        emit1 (c2m_ctx, t == MIR_T_I32 || t == MIR_T_I64 ? MIR_BO : MIR_UBO, lab_op);
        emit1 (c2m_ctx, MIR_JMP, MIR_new_label_op (ctx, false_label));
      } else {
        label = MIR_new_label (ctx);
        res = get_new_temp (c2m_ctx, MIR_T_I64);
        emit1 (c2m_ctx, t == MIR_T_I32 || t == MIR_T_I64 ? MIR_BO : MIR_UBO,
               MIR_new_label_op (ctx, label));
        emit2 (c2m_ctx, MIR_MOV, res.mir_op, MIR_new_int_op (ctx, 0));
        emit_label_insn_opt (c2m_ctx, label);
      }
      true_label = false_label = NULL;
      break;
    }
    if (expect_p) {
      e = NL_EL (args->u.ops, 1)->attr;
      if (e->const_p && true_label != NULL && expect_res != NULL)
        *expect_res = e->c.u_val == 0 ? -1 : 1;
      res = gen (c2m_ctx, NL_HEAD (args->u.ops), true_label, false_label, val_p, desirable_dest,
                 NULL);
      true_label = false_label = NULL;
      val_p = FALSE;
      break;
    }
    if (atomic_builtin_p) {
      /* seq_cst only; trailing order args are ignored.
         Value results leave true_label set so finish can BT/BF for
         `while (!atomic_load(...))` etc.  Void ops clear labels. */
      if (atomic_fence_p) {
        emit_insn (c2m_ctx, MIR_new_insn (ctx, MIR_AFENCE));
        res = zero_op;
        true_label = false_label = NULL;
        val_p = FALSE;
        break;
      }
      {
        node_t a0 = NL_HEAD (args->u.ops);
        struct type *pt = ((struct expr *) a0->attr)->type;
        struct type *et = (pt != NULL && pt->mode == TM_PTR) ? pt->u.ptr_type : NULL;
        MIR_type_t at = et != NULL ? get_mir_type (c2m_ctx, et) : MIR_T_I64;
        op_t ptr = val_gen (c2m_ctx, a0);
        op_t mem = atomic_ptr_to_mem (c2m_ctx, ptr, at);
        if (atomic_load_n_p) {
          res = atomic_load_mem (c2m_ctx, mem, at);
        } else if (atomic_store_n_p) {
          op_t v = val_gen (c2m_ctx, NL_EL (args->u.ops, 1));
          v = force_reg (c2m_ctx, promote (c2m_ctx, v, at, FALSE), promote_mir_int_type (at));
          atomic_store_mem (c2m_ctx, mem, v);
          res = zero_op;
          true_label = false_label = NULL;
          val_p = FALSE;
        } else if (atomic_exchange_n_p) {
          op_t v = val_gen (c2m_ctx, NL_EL (args->u.ops, 1));
          v = force_reg (c2m_ctx, promote (c2m_ctx, v, at, FALSE), promote_mir_int_type (at));
          res = get_new_temp (c2m_ctx, promote_mir_int_type (at));
          emit3_noopt (c2m_ctx, MIR_AXCHG, res.mir_op, mem.mir_op, v.mir_op);
        } else if (atomic_fetch_add_p || atomic_fetch_sub_p || atomic_fetch_and_p
                   || atomic_fetch_or_p || atomic_fetch_xor_p) {
          MIR_insn_code_t ac = (atomic_fetch_add_p   ? MIR_AADD
                                : atomic_fetch_sub_p ? MIR_ASUB
                                : atomic_fetch_and_p ? MIR_AAND
                                : atomic_fetch_or_p  ? MIR_AOR
                                                     : MIR_AXOR);
          op_t v = val_gen (c2m_ctx, NL_EL (args->u.ops, 1));
          v = force_reg (c2m_ctx, promote (c2m_ctx, v, at, FALSE), promote_mir_int_type (at));
          res = get_new_temp (c2m_ctx, promote_mir_int_type (at));
          emit3_noopt (c2m_ctx, ac, res.mir_op, mem.mir_op, v.mir_op);
        } else if (atomic_cas_n_p) {
          /* __atomic_compare_exchange_n(ptr, expected*, desired, weak, s, f)
             Returns bool; on failure updates *expected to the observed value. */
          op_t exp_ptr = val_gen (c2m_ctx, NL_EL (args->u.ops, 1));
          op_t des = val_gen (c2m_ctx, NL_EL (args->u.ops, 2));
          op_t exp_mem, oldv, exp_val, ok;
          MIR_label_t ok_lab, done_lab;
          exp_ptr = force_reg (c2m_ctx, exp_ptr, MIR_T_I64);
          exp_mem = new_op (NULL, MIR_new_mem_op (ctx, at, 0, exp_ptr.mir_op.u.reg, 0, 1));
          exp_val = get_new_temp (c2m_ctx, promote_mir_int_type (at));
          emit2 (c2m_ctx, MIR_MOV, exp_val.mir_op, exp_mem.mir_op);
          des = force_reg (c2m_ctx, promote (c2m_ctx, des, at, FALSE), promote_mir_int_type (at));
          oldv = get_new_temp (c2m_ctx, promote_mir_int_type (at));
          emit4_noopt (c2m_ctx, MIR_ACAS, oldv.mir_op, mem.mir_op, exp_val.mir_op, des.mir_op);
          ok = get_new_temp (c2m_ctx, MIR_T_I64);
          ok_lab = MIR_new_label (ctx);
          done_lab = MIR_new_label (ctx);
          emit3 (c2m_ctx, MIR_BEQ, MIR_new_label_op (ctx, ok_lab), oldv.mir_op, exp_val.mir_op);
          /* fail: *expected = old */
          emit2_noopt (c2m_ctx, MIR_MOV, exp_mem.mir_op, oldv.mir_op);
          emit2 (c2m_ctx, MIR_MOV, ok.mir_op, MIR_new_int_op (ctx, 0));
          emit1 (c2m_ctx, MIR_JMP, MIR_new_label_op (ctx, done_lab));
          emit_label_insn_opt (c2m_ctx, ok_lab);
          emit2 (c2m_ctx, MIR_MOV, ok.mir_op, MIR_new_int_op (ctx, 1));
          emit_label_insn_opt (c2m_ctx, done_lab);
          res = ok;
        }
      }
      /* Keep true_label for load/fetch/cas/exchange so finish emits BT/BF. */
      break;
    }
    if (jret_p) {
      op1 = val_gen (c2m_ctx, NL_HEAD (args->u.ops));
      emit1 (c2m_ctx, MIR_JRET, op1.mir_op);
      true_label = false_label = NULL;
      val_p = FALSE;
      break;
    }
    if (prop_set_p) {
      op1 = gen (c2m_ctx, NL_HEAD (args->u.ops), NULL, NULL, FALSE, NULL, NULL);
      op2 = val_gen (c2m_ctx, NL_EL (args->u.ops, 1));
      emit2 (c2m_ctx, MIR_PRSET, op1.mir_op, op2.mir_op);
      true_label = false_label = NULL;
      val_p = FALSE;
      break;
    }
    if (prop_eq_p || prop_ne_p) {
      MIR_label_t t_label = true_label, f_label = false_label;
      int make_val_p = t_label == NULL;
      if (make_val_p) {
        t_label = MIR_new_label (ctx);
        f_label = MIR_new_label (ctx);
      }
      node_t arg = NL_HEAD (args->u.ops);
      op1 = gen (c2m_ctx, arg, NULL, NULL, FALSE, NULL, NULL);
      arg = NL_NEXT (arg);
      op2 = val_gen (c2m_ctx, arg);
      emit3 (c2m_ctx, prop_eq_p ? MIR_PRBEQ : MIR_PRBNE, MIR_new_label_op (ctx, t_label),
             op1.mir_op, op2.mir_op);
      emit1 (c2m_ctx, MIR_JMP, MIR_new_label_op (ctx, f_label));
      if (make_val_p) make_cond_val (c2m_ctx, r, t_label, f_label, &res);
      true_label = false_label = NULL;
      val_p = FALSE;
      break;
    }
    if (json_p) {
      /* json(expr) builtin:
         - json(dict)   → dict_serialize_json(d, alloca(4096), 4096, 0)  returns char*
         - json(string) → dict_deserialize_json(s)                     returns dict */
      node_t arg_node = NL_HEAD (args->u.ops);
      struct expr *arg_e = arg_node->attr;
      op1 = val_gen (c2m_ctx, arg_node);
      dict_ensure_imports (c2m_ctx);
      if (arg_e->type->mode == TM_DICT) {
        /* Serialize: route through the String arena so the result survives
           across returns / try-block cleanup (see gen_dict_serialize_to_tracked_string). */
        res = gen_dict_serialize_to_tracked_string (c2m_ctx, op1.mir_op);
      } else {
        /* Deserialize: call dict_deserialize_json */
        MIR_op_t call_args[4];
        res = get_new_temp (c2m_ctx, MIR_T_I64);
        call_args[0] = MIR_new_ref_op (ctx, dict_deserialize_json_proto);
        call_args[1] = MIR_new_ref_op (ctx, dict_deserialize_json_item);
        call_args[2] = res.mir_op;  /* result: dict */
        call_args[3] = op1.mir_op;  /* json string */
        emit_insn (c2m_ctx, MIR_new_insn_arr (ctx, MIR_CALL, 4, call_args));
      }
      break;
    }
    /* Sequence lambda-method call (arr/slice/List .filter/.map/.reduce/.count):
       lowered to an inline MIR loop.  Identified by the check-phase placeholder
       (void-typed field expr) so user class methods with these names are not
       intercepted. */
    if (func->code == N_FIELD || func->code == N_DEREF_FIELD) {
      node_t mobj = NL_HEAD (func->u.ops);
      node_t mid = NL_NEXT (mobj);
      struct expr *fe = func->attr;
      enum seq_method seqm = mid != NULL && mid->code == N_ID
                               ? get_seq_method (mid->u.s.s, NULL)
                               : SEQM_NONE;

      if (seqm != SEQM_NONE && mobj->code != N_STRING && fe != NULL && fe->type != NULL
          && fe->type->mode == TM_BASIC && fe->type->u.basic_type == TP_VOID
          && (mobj->attr == NULL
              || !builtin_string_type_p (((struct expr *) mobj->attr)->type))) {
        res = gen_seq_method_call (c2m_ctx, r, seqm);
        goto finish;
      }
    }
    /* Built-in dict methods (not key access):
         d.length() / d.count() → dict_iter_count
         d.type()               → DictType tag
         d.json()               → serialize to arena String
       Bare d.length / d.json stay ordinary key lookups. */
    if (func->code == N_FIELD || func->code == N_DEREF_FIELD) {
      node_t mobj = NL_HEAD (func->u.ops);
      node_t mid  = NL_NEXT (mobj);
      struct expr *obj_e = mobj->attr;
      if (mid != NULL && mid->code == N_ID && obj_e != NULL
          && obj_e->type != NULL && obj_e->type->mode == TM_DICT
          && (strcmp (mid->u.s.s, "length") == 0 || strcmp (mid->u.s.s, "count") == 0)
          && (args == NULL || NL_LENGTH (args->u.ops) == 0)) {
        op_t recv = val_gen (c2m_ctx, mobj);
        res = gen_dict_iter_count (c2m_ctx, recv.mir_op);
        goto finish;
      }
      if (mid != NULL && mid->code == N_ID && obj_e != NULL
          && obj_e->type != NULL && obj_e->type->mode == TM_DICT
          && strcmp (mid->u.s.s, "type") == 0
          && (args == NULL || NL_LENGTH (args->u.ops) == 0)) {
        /* d.type() -> DictType tag at offset 0 (no payload unwrap). */
        op_t recv = val_gen (c2m_ctx, mobj);
        res = gen_dict_type_tag (c2m_ctx, recv);
        goto finish;
      }
      if (mid != NULL && mid->code == N_ID && obj_e != NULL
          && obj_e->type != NULL && obj_e->type->mode == TM_DICT
          && strcmp (mid->u.s.s, "json") == 0
          && (args == NULL || NL_LENGTH (args->u.ops) == 0)) {
        /* d.json() — serialize; result is arena-tracked String. */
        op_t recv = val_gen (c2m_ctx, mobj);
        res = gen_dict_serialize_to_tracked_string (c2m_ctx, recv.mir_op);
        goto finish;
      }
    }
    /* Safe dense-accessor open-code for N_CALL: only intercept when the
       receiver class has a dense List/Map layout *and* the method is open-
       codeable.  Evaluate receiver/args once, open-code, never fall through
       (which would re-evaluate and break GetMut chaining).  Non-dense classes
       (Box.Count, etc.) use the normal call path — do not route them through
       gen_class_method_call (proto/arity differs from N_CALL's proto_item). */
    if (func->code == N_FIELD || func->code == N_DEREF_FIELD) {
      struct expr *fe = func->attr;
      node_t acc_def = NULL;
      if (fe != NULL && fe->def_node != NULL && fe->def_node->code == N_FUNC_DEF
          && fe->def_node != (node_t) (intptr_t) 1
          && fe->def_node != (node_t) (intptr_t) 2)
        acc_def = fe->def_node;
      /* Monomorph method calls often lack def_node on the field expr: resolve
         the accessor by name+arity from the receiver class (same predicate as
         the class-subscript path).  The dense-layout check below keeps this
         from hijacking user classes that merely name a method Get/Count. */
      if (acc_def == NULL) {
        node_t mobj0 = NL_HEAD (func->u.ops);
        node_t mid0 = mobj0 != NULL ? NL_NEXT (mobj0) : NULL;
        struct expr *obj0_e = mobj0 != NULL ? mobj0->attr : NULL;
        struct type *ct0 = obj0_e != NULL ? obj0_e->type : NULL;
        struct type *cls0
          = ct0 != NULL && ct0->mode == TM_CLASS
              ? ct0
              : (ct0 != NULL && ct0->mode == TM_PTR && ct0->u.ptr_type != NULL
                 && ct0->u.ptr_type->mode == TM_CLASS)
                  ? ct0->u.ptr_type
                  : NULL;
        int na0 = args != NULL && NL_HEAD (args->u.ops) != NULL
                    ? (int) NL_LENGTH (args->u.ops) - 1
                    : 0; /* args[0] is the injected receiver */
        if (cls0 != NULL && cls0->u.tag_type != NULL && mid0 != NULL
            && mid0->code == N_ID)
          acc_def = find_class_protocol_method (c2m_ctx, cls0->u.tag_type, mid0->u.s.s, na0,
                                                POS (r));
      }
      if (acc_def != NULL) {
        /* Method calls carry the injected receiver as args[0] (check prepends
           N_ADDR(obj) for value receivers, the pointer for ->): the accessor
           arity and the open-code args start after it. */
        node_t arg0 = args != NULL ? NL_HEAD (args->u.ops) : NULL;
        int na = arg0 != NULL ? (int) NL_LENGTH (args->u.ops) - 1 : 0;
        if (dense_accessor_open_codeable_p (acc_def, na)) {
          node_t mobj = NL_HEAD (func->u.ops);
          struct expr *obj_e = mobj != NULL ? mobj->attr : NULL;
          struct expr *call_e = (struct expr *) r->attr;
          op_t this_op, uargs[GEN_METHOD_MAX_ARGS], oc_res;
          int i = 0, sflags = 0;
          node_t arg;

          /* midopt proved the index in range for this receiver (IV loop). */
          if (call_e != NULL && call_e->elide_oob_p) sflags |= GEN_SAFE_SKIP_OOB;
          if (func->code == N_DEREF_FIELD) {
            this_op = val_gen (c2m_ctx, mobj);
            /* Ownership/midopt may have stamped SAFE on the receiver expr. */
            if (obj_e != NULL && obj_e->own_deref_class == DEREF_GUARD_SAFE)
              sflags |= GEN_SAFE_SKIP_NULL;
          } else if (obj_e != NULL && obj_e->type != NULL && obj_e->type->mode == TM_CLASS) {
            /* Value-class receiver: `this` is &stack_slot — never null. */
            op_t tmp = gen (c2m_ctx, mobj, NULL, NULL, FALSE, NULL, NULL);
            if (tmp.mir_op.mode == MIR_OP_MEM)
              this_op = mem_to_address (c2m_ctx, tmp, TRUE);
            else
              this_op = force_reg (c2m_ctx, tmp, MIR_T_I64);
            sflags |= GEN_SAFE_SKIP_NULL;
          } else {
            this_op = val_gen (c2m_ctx, mobj);
            if (obj_e != NULL && obj_e->own_deref_class == DEREF_GUARD_SAFE)
              sflags |= GEN_SAFE_SKIP_NULL;
          }
          for (arg = (arg0 != NULL ? NL_NEXT (arg0) : NULL);
               arg != NULL && i < GEN_METHOD_MAX_ARGS; arg = NL_NEXT (arg))
            uargs[i++] = val_gen (c2m_ctx, arg);
          /* Const non-negative index into a dense buffer: still need dynamic
             length for full OOB proof — leave OOB on unless SKIP later. */
          if (try_open_code_dense_accessor (c2m_ctx, acc_def, this_op, uargs, i, NULL,
                                            &oc_res, sflags)) {
            res = oc_res;
            /* P0: destroy a prvalue receiver temp (e.g. `List<int>().Count()`),
               unless the accessor returns a pointer that may alias the temp's
               storage (GetMut & co. — keep the temp alive, as before). */
            if (class_prvalue_call_p (mobj)) {
              decl_t acc_d = acc_def->attr;
              struct type *acc_ret
                = (acc_d != NULL && acc_d->decl_spec.type != NULL)
                    ? acc_d->decl_spec.type->u.func_type->ret_type
                    : NULL;
              struct expr *mobj_e2 = mobj->attr;
              if ((acc_ret == NULL || acc_ret->mode != TM_PTR) && mobj_e2 != NULL
                  && class_has_dtor_p (c2m_ctx, mobj_e2->type))
                gen_class_temp_dtor (c2m_ctx, mobj_e2->type, this_op);
            }
            goto finish;
          }
          /* Predicate said open-codeable but emit failed — fall through to
             normal N_CALL (re-eval).  Should not happen for dense layouts. */
        }
      }
    }
    /* Built-in String method call: lower s.method(...) to a UTF-8 runtime call. */
	    if (func->code == N_FIELD || func->code == N_DEREF_FIELD) {
	      node_t mobj = NL_HEAD (func->u.ops);
	      if (mobj->code == N_STRING) {
	        /* Static built-in String method: String.copy(p, len) etc. No receiver.
	           (The bare `String` keyword is an N_STRING node; a string literal
	           receiver like "abc".lower() is an N_STR node and is lowered as an
	           instance method below.) */
	        node_t method_id = NL_NEXT (mobj);
	        enum str_method sm = get_string_method (method_id->u.s.s, NULL, NULL);
	        if (sm != SM_NONE) {
	          MIR_op_t vals[4];
	          int i = 0;
	          for (node_t arg = (args ? NL_HEAD (args->u.ops) : NULL); arg; arg = NL_NEXT (arg)) {
	        if (sm == SM_COPY) {
				  MIR_op_t vtmp;
				  vtmp = (i == 0
                        ? val_gen (c2m_ctx, arg)
                        : promote (c2m_ctx, val_gen (c2m_ctx, arg), MIR_T_I64, FALSE)
                      ).mir_op;
				  vals[i++] = vtmp;
	            } else if (sm == SM_ATTACH) {
	              /* attach: single char* arg passed as I64 pointer */
	              vals[i++] = val_gen (c2m_ctx, arg).mir_op;
	            } else {
	              vals[i++] = val_gen (c2m_ctx, arg).mir_op;
	            }
	          }
	          res = gen_string_call (c2m_ctx, sm, vals, i);
	          goto finish;
	        }
	      }
	      struct expr *obj_e = mobj->attr;
	      if ((obj_e != NULL && builtin_string_type_p (obj_e->type))
	          || mobj->code == N_STR) {
        node_t method_id = NL_NEXT (mobj);
        enum str_method sm = get_string_method (method_id->u.s.s, NULL, NULL);
        /* replace(needle, repl) (2 args) is search-and-replace; replace(pos,
           len, repl) (3 args) is the positional form.  Mirror the check-phase
           disambiguation here. */
        if (sm == SM_REPLACE && args != NULL && NL_LENGTH (args->u.ops) == 2)
          sm = SM_REPLACE_ALL;
        if (sm != SM_NONE) {
          MIR_op_t vals[4];
          op_t obj_op = val_gen (c2m_ctx, mobj);
          vals[0] = obj_op.mir_op;
          switch (sm) {
          case SM_LENGTH:
            res = gen_string_call (c2m_ctx, SM_LENGTH, vals, 1);
            break;
          case SM_EMPTY:
            res = gen_string_call (c2m_ctx, SM_EMPTY, vals, 1);
            break;
          case SM_FIND:
            vals[1] = val_gen (c2m_ctx, NL_HEAD (args->u.ops)).mir_op;
            res = gen_string_call (c2m_ctx, SM_FIND, vals, 2);
            break;
          case SM_SUBSTR:
            {
              op_t pos_op = promote (c2m_ctx, val_gen (c2m_ctx, NL_HEAD (args->u.ops)), MIR_T_I64, FALSE);
              op_t len_op = promote (c2m_ctx, val_gen (c2m_ctx, NL_EL (args->u.ops, 1)), MIR_T_I64, FALSE);
              /* Force signed semantics: reject negative pos/len at runtime
                 (prevents the "negative becomes huge unsigned" silent failure).
                 Emit guard before the call; reuse the OOB safety trap path. */
              if (c2m_options->exceptions_p) {
                /* If either is negative, trap (reason OOB).  Simple constant-foldable
                   checks are left to the backend; we emit unconditional compare. */
                MIR_label_t ok1 = MIR_new_label (c2m_ctx->ctx);
                MIR_op_t zero = zero_op.mir_op;
                emit3 (c2m_ctx, MIR_BGE, MIR_new_label_op (c2m_ctx->ctx, ok1), pos_op.mir_op, zero);
                /* negative pos */
                gen_oob_check (c2m_ctx, pos_op, zero, (long) POS (r).lno); /* reuse for negative-as-OOB */
                emit_label_insn_opt (c2m_ctx, ok1);
                MIR_label_t ok2 = MIR_new_label (c2m_ctx->ctx);
                emit3 (c2m_ctx, MIR_BGE, MIR_new_label_op (c2m_ctx->ctx, ok2), len_op.mir_op, zero);
                gen_oob_check (c2m_ctx, len_op, zero, (long) POS (r).lno);
                emit_label_insn_opt (c2m_ctx, ok2);
              }
              vals[1] = pos_op.mir_op;
              vals[2] = len_op.mir_op;
              res = gen_string_call (c2m_ctx, SM_SUBSTR, vals, 3);
            }
            break;
          case SM_REPLACE:
            {
              op_t pos_op = promote (c2m_ctx, val_gen (c2m_ctx, NL_HEAD (args->u.ops)), MIR_T_I64, FALSE);
              op_t len_op = promote (c2m_ctx, val_gen (c2m_ctx, NL_EL (args->u.ops, 1)), MIR_T_I64, FALSE);
              op_t repl_op = val_gen (c2m_ctx, NL_EL (args->u.ops, 2));
              /* Force signed semantics for the positional form of replace */
              if (c2m_options->exceptions_p) {
                MIR_label_t ok1 = MIR_new_label (c2m_ctx->ctx);
                MIR_op_t zero = zero_op.mir_op;
                emit3 (c2m_ctx, MIR_BGE, MIR_new_label_op (c2m_ctx->ctx, ok1), pos_op.mir_op, zero);
                gen_oob_check (c2m_ctx, pos_op, zero, (long) POS (r).lno);
                emit_label_insn_opt (c2m_ctx, ok1);
                MIR_label_t ok2 = MIR_new_label (c2m_ctx->ctx);
                emit3 (c2m_ctx, MIR_BGE, MIR_new_label_op (c2m_ctx->ctx, ok2), len_op.mir_op, zero);
                gen_oob_check (c2m_ctx, len_op, zero, (long) POS (r).lno);
                emit_label_insn_opt (c2m_ctx, ok2);
              }
              vals[1] = pos_op.mir_op;
              vals[2] = len_op.mir_op;
              vals[3] = repl_op.mir_op;
              res = gen_string_call (c2m_ctx, SM_REPLACE, vals, 4);
            }
            /* replace mutates in place: write the new pointer back into the
               receiver when it is a plain assignable lvalue. */
            {
              op_t lval = gen (c2m_ctx, mobj, NULL, NULL, FALSE, NULL, NULL);
              if (lval.mir_op.mode == MIR_OP_REG
                  || (lval.mir_op.mode == MIR_OP_MEM && lval.mir_op.u.mem.index == 0))
                emit2 (c2m_ctx, MIR_MOV, lval.mir_op, res.mir_op);
            }
            break;
          case SM_REPLACE_ALL:
            /* search-and-replace: replace(needle, repl) -> fresh String.
               Pure transform (no in-place writeback); caller assigns it. */
            vals[1] = val_gen (c2m_ctx, NL_HEAD (args->u.ops)).mir_op;
            vals[2] = val_gen (c2m_ctx, NL_EL (args->u.ops, 1)).mir_op;
            res = gen_string_call (c2m_ctx, SM_REPLACE_ALL, vals, 3);
            break;
          case SM_UPPER:
            res = gen_string_call (c2m_ctx, SM_UPPER, vals, 1);
            break;
          case SM_LOWER:
            res = gen_string_call (c2m_ctx, SM_LOWER, vals, 1);
            break;
          case SM_DETACH:
            res = gen_string_call (c2m_ctx, SM_DETACH, vals, 1);
            break;
          case SM_STARTS_WITH:
            vals[1] = val_gen (c2m_ctx, NL_HEAD (args->u.ops)).mir_op;
            res = gen_string_call (c2m_ctx, SM_STARTS_WITH, vals, 2);
            break;
          case SM_ENDS_WITH:
            vals[1] = val_gen (c2m_ctx, NL_HEAD (args->u.ops)).mir_op;
            res = gen_string_call (c2m_ctx, SM_ENDS_WITH, vals, 2);
            break;
          case SM_CONTAINS:
            vals[1] = val_gen (c2m_ctx, NL_HEAD (args->u.ops)).mir_op;
            res = gen_string_call (c2m_ctx, SM_CONTAINS, vals, 2);
            break;
          case SM_TRIM:
            res = gen_string_call (c2m_ctx, SM_TRIM, vals, 1);
            break;
          case SM_EQUALS:
            vals[1] = val_gen (c2m_ctx, NL_HEAD (args->u.ops)).mir_op;
            res = gen_string_call (c2m_ctx, SM_EQUALS, vals, 2);
            break;
          case SM_SPLIT:
            vals[1] = val_gen (c2m_ctx, NL_HEAD (args->u.ops)).mir_op;
            res = gen_string_call (c2m_ctx, SM_SPLIT, vals, 2);
            break;
          case SM_EXT: {
            /* Header-registered method: receiver + N args as I64, call rt by name. */
            int ui = 1;
            const builtin_method_t *bm;
            int an = args != NULL ? (int) NL_LENGTH (args->u.ops) : 0;
            bm = find_builtin_method ("String", method_id->u.s.s, an);
            if (bm == NULL) bm = find_builtin_method ("String", method_id->u.s.s, -1);
            assert (bm != NULL && bm->rt_name != NULL);
            for (node_t arg = args ? NL_HEAD (args->u.ops) : NULL; arg != NULL && ui < 4;
                 arg = NL_NEXT (arg))
              vals[ui++] = val_gen (c2m_ctx, arg).mir_op;
            res = gen_string_call_named (c2m_ctx, SM_EXT, bm->rt_name, vals, ui);
            break;
          }
          default: break;
          }
          goto finish;
        }
      }
    }
    /* Built-in List<String>::join(delim) -> String.  The receiver is the
       List<String> pointer (or a value, whose address we take), passed as the
       opaque first argument to c2m_str_join. */
    if (func->code == N_FIELD || func->code == N_DEREF_FIELD) {
      node_t mobj = NL_HEAD (func->u.ops);
      node_t mid = NL_NEXT (mobj);
      struct expr *obj_e = mobj->attr;
      if (mid != NULL && mid->code == N_ID
          && get_string_method (mid->u.s.s, NULL, NULL) == SM_JOIN
          && obj_e != NULL && list_string_type_p (obj_e->type)) {
        MIR_op_t vals[2];
        op_t recv;
        if (func->code == N_DEREF_FIELD)
          recv = val_gen (c2m_ctx, mobj); /* receiver already a pointer value */
        else
          recv = mem_to_address (c2m_ctx, gen (c2m_ctx, mobj, NULL, NULL, FALSE, NULL, NULL), TRUE);
        vals[0] = force_reg (c2m_ctx, recv, MIR_T_I64).mir_op;
        vals[1] = val_gen (c2m_ctx, NL_HEAD (args->u.ops)).mir_op;
        res = gen_string_call (c2m_ctx, SM_JOIN, vals, 2);
        goto finish;
      }
    }
    first_arg = NL_HEAD (args->u.ops);
    if (jcall_p) {
      func = NL_HEAD (args->u.ops);
      first_arg = NL_EL (args->u.ops, 1);
      assert (void_type_p (type));
    }
    ops_start = VARR_LENGTH (MIR_op_t, call_ops);
    if (!builtin_call_p || jcall_p) {
      func_expr = func->attr;
      func_type = func_expr->type;
      assert (func_type->mode == TM_PTR && func_type->u.ptr_type->mode == TM_FUNC);
      func_type = func_type->u.ptr_type;
      proto_item = ((struct expr *) r->attr)->call_proto_item;
      if (proto_item == NULL) proto_item = func_type->u.func_type->proto_item;
      VARR_PUSH (MIR_op_t, call_ops, MIR_new_ref_op (ctx, proto_item));
      op1 = val_gen (c2m_ctx, func);
      if (!jcall_p && op1.mir_op.mode == MIR_OP_REF && func->code == N_ID
          && ((decl_t) func_expr->def_node->attr)->decl_spec.inline_p)
        inline_p = TRUE;
      if (op1.mir_op.u.ref == NULL) {
          warning (c2m_ctx, POS (func), "MIR_OP_REF has null func reference");
          //break;
      }

      VARR_PUSH (MIR_op_t, call_ops, op1.mir_op);
    }
    target_init_arg_vars (c2m_ctx, &arg_info);
    arg_area_offset = curr_call_arg_area_offset + ns->size - ns->call_arg_area_size;
    if ((n = target_add_call_res_op (c2m_ctx, type, &arg_info, arg_area_offset)) < 0) {
      /* pass nothing */
    } else if (n == 0) { /* by addr */
      /* Class returns use ALLOCA (see simple_add_call_res_op) — do not burn
         call-arg-area for them (would also reopen the reuse-clobber bug). */
      if ((!builtin_call_p || jcall_p) && type->mode != TM_CLASS)
        update_call_arg_area_offset (c2m_ctx, type, FALSE);
      res = new_op (NULL, VARR_LAST (MIR_op_t, call_ops));
      assert (res.mir_op.mode == MIR_OP_MEM && res.mir_op.u.mem.type == MIR_T_RBLK);
      res.mir_op = MIR_new_mem_op (ctx, MIR_T_UNDEF, 0, res.mir_op.u.mem.base, 0, 1);
      t = MIR_T_I64;
    } else if (type->mode == TM_STRUCT || type->mode == TM_UNION
               || type->mode == TM_CLASS) { /* passed in regs */
      if (!va_arg_p) {
        if (type->mode == TM_CLASS) {
          /* Mirror the ALLOCA path used for by-addr class returns. */
          mir_size_t csize = type_size (c2m_ctx, type);
          if (csize == 0) csize = 1;
          res = get_new_temp (c2m_ctx, MIR_T_I64);
          MIR_append_insn (ctx, curr_func,
                           MIR_new_insn (ctx, MIR_ALLOCA, res.mir_op,
                                         MIR_new_int_op (ctx, (long long) csize)));
          res.mir_op = MIR_new_mem_op (ctx, MIR_T_UNDEF, 0, res.mir_op.u.reg, 0, 1);
        } else {
          res = get_new_temp (c2m_ctx, MIR_T_I64);
          emit3 (c2m_ctx, MIR_ADD, res.mir_op,
                 MIR_new_reg_op (ctx, MIR_reg (ctx, FP_NAME, curr_func->u.func)),
                 MIR_new_int_op (ctx, arg_area_offset));
          if (!builtin_call_p) update_call_arg_area_offset (c2m_ctx, type, FALSE);
          res.mir_op = MIR_new_mem_op (ctx, MIR_T_UNDEF, 0, res.mir_op.u.reg, 0, 1);
        }
        t = MIR_T_I64;
      }
    } else if (n > 0) {
      assert (n == 1);
      t = promote_mir_int_type (get_mir_type (c2m_ctx, type));
      res = new_op (NULL, VARR_LAST (MIR_op_t, call_ops));
    }
    saved_call_arg_area_offset_before_args = curr_call_arg_area_offset;
    if (va_arg_p) {
      op1 = get_new_temp (c2m_ctx, MIR_T_I64);
      op2 = val_gen (c2m_ctx, NL_HEAD (args->u.ops));
      if (op2.mir_op.mode == MIR_OP_MEM) {
#ifndef _WIN32
        if (op2.mir_op.u.mem.type == MIR_T_UNDEF)
#endif
          op2 = mem_to_address (c2m_ctx, op2, FALSE);
      }
      if (type->mode == TM_STRUCT || type->mode == TM_UNION || type->mode == TM_CLASS) {
        if (desirable_dest == NULL) {
          /* MIR #449: rvalue struct/union/class va_arg needs storage (not NULL). */
          res = get_new_temp (c2m_ctx, MIR_T_I64);
          MIR_append_insn (ctx, curr_func,
                           MIR_new_insn (ctx, MIR_ALLOCA, res.mir_op,
                                         MIR_new_int_op (ctx, type_size (c2m_ctx, type))));
        } else {
          assert (desirable_dest->mir_op.mode == MIR_OP_MEM);
          res = mem_to_address (c2m_ctx, *desirable_dest, TRUE);
        }
        MIR_append_insn (ctx, curr_func,
                         MIR_new_insn (ctx, MIR_VA_BLOCK_ARG, res.mir_op, op2.mir_op,
                                       MIR_new_int_op (ctx, type_size (c2m_ctx, type)),
                                       MIR_new_int_op (ctx, target_get_blk_type (c2m_ctx, type)
                                                              - MIR_T_BLK)));
        if (desirable_dest != NULL)
          res = *desirable_dest;
        else
          res.mir_op = MIR_new_mem_op (ctx, MIR_T_UNDEF, 0, res.mir_op.u.reg, 0, 1);
      } else {
        MIR_append_insn (ctx, curr_func,
                         MIR_new_insn (ctx, MIR_VA_ARG, op1.mir_op, op2.mir_op,
                                       MIR_new_mem_op (ctx, t, 0, 0, 0, 1)));
        op2 = get_new_temp (c2m_ctx, t);
        MIR_append_insn (ctx, curr_func,
                         MIR_new_insn (ctx, tp_mov (t), op2.mir_op,
                                       MIR_new_alias_mem_op (ctx, t, 0, op1.mir_op.u.reg, 0, 1,
                                                             get_type_alias (c2m_ctx, type), 0)));
        if (res.mir_op.mode == MIR_OP_REG) {
          res = op2;
        } else {
          assert (res.mir_op.mode == MIR_OP_MEM);
          res.mir_op.u.mem.base = op2.mir_op.u.reg;
        }
      }
    } else if (va_start_p) {
      op1 = val_gen (c2m_ctx, NL_HEAD (args->u.ops));
      if (op1.mir_op.mode == MIR_OP_MEM) {
#ifndef _WIN32
        if (op1.mir_op.u.mem.type == MIR_T_UNDEF)
#endif
          op1 = mem_to_address (c2m_ctx, op1, FALSE);
      }
      MIR_append_insn (ctx, curr_func, MIR_new_insn (ctx, MIR_VA_START, op1.mir_op));
	    } else if (alloca_p) {
	      res = get_new_temp (c2m_ctx, t);
	      node_t size_arg = NL_HEAD (args->u.ops);
	      /* Guard on the original dimension (signed) if the alloca arg is a sizeof*dim mul.
	         This preserves negative values that would be lost after unsigned promotion in the byte-size. */
	      if (c2m_options->exceptions_p && size_arg && size_arg->code == N_MUL) {
	        node_t dim_node = NL_EL (size_arg->u.ops, 1);  /* second operand of mul */
	        if (dim_node) {
	          op_t dim_op = val_gen (c2m_ctx, dim_node);
	          op_t dim64 = force_reg (c2m_ctx, dim_op, MIR_T_I64);
	          gen_vla_size_check (c2m_ctx, dim64, (long) POS (r).lno);
	        }
	      }
	      op1 = val_gen (c2m_ctx, size_arg);
	      MIR_append_insn (ctx, curr_func, MIR_new_insn (ctx, MIR_ALLOCA, res.mir_op, op1.mir_op));
	    } else {
      param_list = func_type->u.func_type->param_list;
      param = NL_HEAD (param_list->u.ops);
      int arg_num = 0;
      /* P0: class-prvalue args/receiver temps to destroy after this call. */
      node_t pv_nodes[32];
      op_t pv_ops[32];
      int pv_recv_p[32], n_pv = 0;
      /* Pointer-returning calls may alias the temp in their result — keep
         the temp alive (conservative; same as before P0). */
      int ret_ptr_p = func_type->u.func_type->ret_type != NULL
                      && func_type->u.func_type->ret_type->mode == TM_PTR;
      /* Adopting protocol method on a move-only collection?  The container
         takes over the value — its dtor owns it, do not also destroy the
         temp (would double-free). */
      int adopting_call_p = FALSE;
      if (func->code == N_FIELD || func->code == N_DEREF_FIELD) {
        node_t mobj = NL_HEAD (func->u.ops);
        node_t mid = mobj != NULL ? NL_NEXT (mobj) : NULL;
        struct expr *mobj_e = mobj != NULL ? mobj->attr : NULL;
        struct type *mt = mobj_e != NULL ? mobj_e->type : NULL;
        struct type *mc = (mt != NULL && mt->mode == TM_CLASS) ? mt
                          : (mt != NULL && mt->mode == TM_PTR && mt->u.ptr_type != NULL
                             && mt->u.ptr_type->mode == TM_CLASS)
                            ? mt->u.ptr_type
                            : NULL;
        if (mc != NULL && mid != NULL && mid->code == N_ID && mid->u.s.s != NULL
            && class_is_move_only_collection_p (c2m_ctx, mc)
            && (strcmp (mid->u.s.s, "Add") == 0 || strcmp (mid->u.s.s, "Set") == 0
                || strcmp (mid->u.s.s, "Insert") == 0 || strcmp (mid->u.s.s, "Push") == 0
                || strcmp (mid->u.s.s, "Enqueue") == 0))
          adopting_call_p = TRUE;
      }
      /* Instance method: param list starts with the synthetic `this` pointer.
         Null-check that receiver once at the call site (body treats `this` as
         DEREF_GUARD_SAFE).  Elide when ownership stamped SAFE on the receiver
         expr, or when the receiver is `&obj` (always non-null address). */
      int method_this_null_check_p = FALSE;
      if (c2m_options->exceptions_p && param != NULL && param->code == N_SPEC_DECL) {
        node_t this_declr = SPEC_DECL_DECL (param);
        node_t this_id = (this_declr != NULL && this_declr->code == N_DECL)
                           ? DECL_ID (this_declr) : NULL;
        if (this_id != NULL && this_id->code == N_ID && this_id->u.s.s != NULL
            && strcmp (this_id->u.s.s, "this") == 0)
          method_this_null_check_p = TRUE;
      }
      for (node_t arg = first_arg; arg != NULL; arg = NL_NEXT (arg)) {
        struct type *arg_type;
        arg_num++;
        e = arg->attr;
        struct_p = e->type->mode == TM_STRUCT || e->type->mode == TM_UNION || e->type->mode == TM_CLASS;
        op2 = gen (c2m_ctx, arg, NULL, NULL, !struct_p, NULL, NULL);
        /* P0: capture class-prvalue temps (by-value arg, or the injected
           N_ADDR receiver of a method call on a prvalue) for destruction
           right after the call. */
        if (n_pv < 32 && !ret_ptr_p) {
          node_t pvn = NULL;
          if (arg->code == N_ADDR && class_prvalue_call_p (NL_HEAD (arg->u.ops)))
            pvn = NL_HEAD (arg->u.ops); /* method receiver temp */
          else if (struct_p && class_prvalue_call_p (arg))
            pvn = arg; /* by-value class argument temp */
          if (pvn != NULL
              && class_has_dtor_p (c2m_ctx, ((struct expr *) pvn->attr)->type)) {
            pv_nodes[n_pv] = pvn;
            pv_ops[n_pv] = op2;
            pv_recv_p[n_pv] = (arg->code == N_ADDR);
            n_pv++;
          }
        }
        if (method_this_null_check_p && arg_num == 1 && !struct_p) {
          int elide = (arg->code == N_ADDR)
                      || (e != NULL && e->own_deref_class == DEREF_GUARD_SAFE);
          if (!elide) {
            op2 = force_reg (c2m_ctx, op2, MIR_T_I64);
            gen_null_check (c2m_ctx, op2, (long) POS (r).lno);
          }
        }
        assert (param != NULL || NL_HEAD (param_list->u.ops) == NULL
                || func_type->u.func_type->dots_p);
        arg_type = e->type;
        /* A dict value (DictValue*) passed where the callee does not expect a
           dict must be unwrapped to its scalar/string payload first.  This
           covers variadic arguments (e.g. printf("%s", d.name) — pass the
           char* payload, not the box pointer) as well as plain pointer or
           integer parameters.  A dict argument bound to a dict parameter keeps
           its box so the callee can operate on it. */
        if (!struct_p && e->type->mode == TM_DICT) {
          struct type *target = NULL;
          if (param != NULL) target = get_param_decl_spec (param)->type;
          if (target == NULL || target->mode != TM_DICT)
            op2 = gen_dict_unwrap (c2m_ctx, op2);
        }
        if (struct_p) {
        } else if (param != NULL) {
          assert (param->code == N_SPEC_DECL || param->code == N_TYPE);
          decl_spec = get_param_decl_spec (param);
          arg_type = decl_spec->type;
          t = get_mir_type (c2m_ctx, arg_type);
          t = promote_mir_int_type (t);
          if (param != NULL && arg_type->mode == TM_PTR
              && e->type->mode == TM_CLASS && op2.mir_op.mode == MIR_OP_MEM)
            op2 = mem_to_address (c2m_ctx, op2, FALSE);
          op2 = promote (c2m_ctx, op2, t, FALSE);
        } else if (NL_HEAD (param_list->u.ops) == NULL && !func_type->u.func_type->dots_p) {
          arg_type = default_arg_promoted_type (c2m_ctx, arg_type);
          t = get_mir_type (c2m_ctx, arg_type);
          t = promote_mir_int_type (t);
          op2 = promote (c2m_ctx, op2, t == MIR_T_F ? MIR_T_D : t, FALSE);
        } else {
          t = get_mir_type (c2m_ctx, e->type);
          t = promote_mir_int_type (t);
          op2 = promote (c2m_ctx, op2, t == MIR_T_F ? MIR_T_D : t, FALSE);
        }
        target_add_call_arg_op (c2m_ctx, arg_type, &arg_info, op2);
        if (param != NULL) param = NL_NEXT (param);
      }
      call_insn = MIR_new_insn_arr (ctx,
                                    (jcall_p ? MIR_JCALL
                                     : inline_p ? MIR_INLINE
                                                : MIR_CALL),
                                    VARR_LENGTH (MIR_op_t, call_ops) - ops_start,
                                    VARR_ADDR (MIR_op_t, call_ops) + ops_start);
      MIR_append_insn (ctx, curr_func, call_insn);
      res = target_gen_post_call_res_code (c2m_ctx, func_type->u.func_type->ret_type, res,
                                           call_insn, ops_start);
      /* P0: destroy class-prvalue temporaries consumed by this call (LIFO),
         before the call-arg area is reused.  Adopted values (Add/Set/…) are
         owned by the container now; the receiver temp is always destroyed. */
      for (int _pvi = n_pv - 1; _pvi >= 0; _pvi--) {
        struct type *pt;
        op_t addr;
        if (adopting_call_p && !pv_recv_p[_pvi]) continue;
        pt = ((struct expr *) pv_nodes[_pvi]->attr)->type;
        addr = pv_ops[_pvi].mir_op.mode == MIR_OP_MEM
                 ? mem_to_address (c2m_ctx, pv_ops[_pvi], TRUE)
                 : pv_ops[_pvi];
        gen_class_temp_dtor (c2m_ctx, pt, addr);
      }
    }
    curr_call_arg_area_offset = saved_call_arg_area_offset_before_args;
    VARR_TRUNC (MIR_op_t, call_ops, ops_start);
    break;
  }
  case N_GENERIC: {
    node_t list = NL_EL (r->u.ops, 1);
    node_t ga_case = NL_HEAD (list->u.ops);
    node_t ga_expr = NL_EL (ga_case->u.ops, 1);
    struct type *gt = ((struct expr *) r->attr)->type;

    /* first element is now a compatible generic association case.
       A void association (statement-context `_Generic(..., T: f())`) has
       no value — val_gen would force_val a void call result. */
    if (void_type_p (gt)) {
      gen (c2m_ctx, ga_expr, NULL, NULL, FALSE, NULL, NULL);
      res = zero_op;
    } else {
      op1 = val_gen (c2m_ctx, ga_expr);
      t = get_mir_type (c2m_ctx, gt);
      res = promote (c2m_ctx, op1, t, TRUE);
    }
    break;
  }
      case N_SPEC_DECL: {  // ??? export and defintion with external declaration
        node_t specs = SPEC_DECL_SPECS (r);
        node_t declarator = SPEC_DECL_DECL (r);
        node_t attrs = SPEC_DECL_ATTRS (r);
        node_t asm_part = SPEC_DECL_ASM (r);
        node_t initializer = SPEC_DECL_INIT (r);
        node_t id, curr_node;
        symbol_t sym;
        decl_t curr_decl;
        size_t i, init_start;
        const char *name;

        decl = (decl_t) r->attr;

        /* Visit embedded N_CLASS (and N_STRUCT/N_UNION) from the spec list so that
           class methods' N_FUNC_DEF nodes are processed by gen. This ensures their
           MIR function items (with mangled names) are created before method calls
           are generated. (N_CLASS case will iterate members and gen the FUNC_DEFs.) */
        if (specs != NULL) {
          node_t share = specs;
          if (share->code == N_SHARE) {
        node_t slist = NL_HEAD (share->u.ops);
        if (slist != NULL && slist->code == N_LIST) {
          for (node_t spec = NL_HEAD (slist->u.ops); spec != NULL; spec = NL_NEXT (spec)) {
            if (spec->code == N_CLASS) {
              gen (c2m_ctx, spec, NULL, NULL, FALSE, NULL, NULL);
            }
          }
        }
          }
        }

        if (declarator != NULL && declarator->code != N_IGNORE && decl->u.item == NULL) {
      id = NL_HEAD (declarator->u.ops);
      name = (decl->scope != top_scope && decl->decl_spec.static_p
                ? get_func_static_var_name (c2m_ctx, id->u.s.s, decl)
                : id->u.s.s);
      if (decl->asm_p) {
      } else if (decl->used_p && decl->scope != top_scope && decl->decl_spec.linkage == N_STATIC) {
        decl->u.item = MIR_new_forward (ctx, name);
        move_item_forward (c2m_ctx, decl->u.item);
      } else if (decl->used_p && decl->decl_spec.linkage != N_IGNORE) {
        if (symbol_find (c2m_ctx, S_REGULARS, id,
                         decl->decl_spec.linkage == N_EXTERN ? top_scope : decl->scope, &sym)
            && (decl->u.item = get_ref_item (c2m_ctx, sym.def_node, name)) == NULL) {
          for (i = 0; i < VARR_LENGTH (node_t, sym.defs); i++)
            if ((decl->u.item = get_ref_item (c2m_ctx, VARR_GET (node_t, sym.defs, i), name))
                != NULL)
              break;
        }
        if (decl->u.item == NULL) decl->u.item = MIR_new_import (ctx, name);
        if (decl->scope != top_scope) move_item_forward (c2m_ctx, decl->u.item);
      }
      if (declarator->code == N_DECL && decl->decl_spec.type->mode != TM_FUNC
          && !decl->decl_spec.typedef_p && !decl->decl_spec.extern_p && !decl->asm_p) {
        if (initializer->code == N_IGNORE) {
          if (decl->decl_spec.thread_local_p
              && (decl->scope == top_scope || decl->decl_spec.static_p)) {
            decl->u.item
              = MIR_new_tls_bss (ctx, name, raw_type_size (c2m_ctx, decl->decl_spec.type));
          } else if (decl->scope != top_scope && decl->decl_spec.static_p) {
            decl->u.item = MIR_new_bss (ctx, name, raw_type_size (c2m_ctx, decl->decl_spec.type));
          } else if (decl->scope == top_scope
                     && symbol_find (c2m_ctx, S_REGULARS, id, top_scope, &sym)
                     && ((curr_decl = sym.def_node->attr)->u.item == NULL
                         || (curr_decl->u.item->item_type != MIR_bss_item
                             && curr_decl->u.item->item_type != MIR_tls_bss_item))) {
            for (i = 0; i < VARR_LENGTH (node_t, sym.defs); i++) {
              curr_node = VARR_GET (node_t, sym.defs, i);
              curr_decl = curr_node->attr;
              if ((curr_decl->u.item != NULL
                   && (curr_decl->u.item->item_type == MIR_bss_item
                       || curr_decl->u.item->item_type == MIR_tls_bss_item))
                  || SPEC_DECL_INIT (curr_node)->code != N_IGNORE)
                break;
            }
            if (i >= VARR_LENGTH (node_t, sym.defs)) /* No item yet or no decl with intializer: */
              decl->u.item = MIR_new_bss (ctx, name, raw_type_size (c2m_ctx, decl->decl_spec.type));
          } else if (decl->scope != top_scope && !decl->decl_spec.static_p
                     && !decl->decl_spec.thread_local_p && decl->used_p
                     && integer_type_p (decl->decl_spec.type)) {
            /* Uninitialized narrow auto reg: extend once at birth (9c7e7f3b /
               pr34099-2).  Memory-homed decls already extend via typed loads. */
            MIR_type_t vt = get_mir_type (c2m_ctx, decl->decl_spec.type);
            if (vt == MIR_T_I8 || vt == MIR_T_U8 || vt == MIR_T_I16 || vt == MIR_T_U16) {
              if (id->attr == NULL) {
                node_t saved_scope = curr_scope;

                curr_scope = decl->scope;
                check (c2m_ctx, id, NULL);
                curr_scope = saved_scope;
              }
              op_t vr = gen (c2m_ctx, id, NULL, NULL, FALSE, NULL, NULL);
              if (vr.mir_op.mode == MIR_OP_REG)
                emit2 (c2m_ctx,
                       vt == MIR_T_I8    ? MIR_EXT8
                       : vt == MIR_T_U8  ? MIR_UEXT8
                       : vt == MIR_T_I16 ? MIR_EXT16
                                         : MIR_UEXT16,
                       vr.mir_op, vr.mir_op);
            }
          }
          /* Emit default member initializers for local class-typed variables.
             Also handles arrays of class objects (e.g. `Mixed m[15];`): each
             element is initialized in turn.  Without this, array elements keep
             garbage in their members (notably String members, which then crash
             at runtime when dereferenced). */
          if (decl->scope != top_scope && !decl->decl_spec.static_p) {
            struct type *var_type = decl->decl_spec.type;
            struct type *elem_type = var_type;
            while (elem_type->mode == TM_ARR) elem_type = elem_type->u.arr_type->el_type;
            if (elem_type->mode == TM_CLASS && elem_type->u.tag_type != NULL) {
              node_t tag = elem_type->u.tag_type;
              node_t member_list = TAG_MEMBER_LIST (tag);
              if (member_list && member_list->code != N_IGNORE) {
                mir_size_t elem_size = type_size (c2m_ctx, elem_type);
                mir_size_t total_size = type_size (c2m_ctx, var_type);
                mir_size_t n_elems = elem_size == 0 ? 0 : total_size / elem_size;
                /* Ensure the id node has been checked so gen can resolve it */
                if (id->attr == NULL) {
                  node_t saved_scope = curr_scope;
                  curr_scope = decl->scope;
                  check(c2m_ctx, id, NULL);
                  curr_scope = saved_scope;
                }
                /* Get the variable's base address (fp + offset) */
                var = gen(c2m_ctx, id, NULL, NULL, FALSE, NULL, NULL);
                assert(var.mir_op.mode == MIR_OP_MEM);
                /* Zero the whole object/array first so that members without an
                   explicit default initializer (e.g. an unset String member)
                   are well-defined null/zero rather than stack garbage.  A
                   garbage String pointer would otherwise crash at runtime when
                   dereferenced (printf %s, strlen, ...). */
                if (var.mir_op.u.mem.index == 0 && total_size != 0)
                  gen_memset (c2m_ctx, var.mir_op.u.mem.disp, var.mir_op.u.mem.base,
                              total_size);
                for (mir_size_t ei = 0; ei < n_elems; ei++) {
                  mir_size_t elem_off = ei * elem_size;
                  for (node_t m = NL_HEAD(member_list->u.ops); m != NULL; m = NL_NEXT(m)) {
                    if (m->code != N_MEMBER) continue;
                    node_t m_init = MEMBER_INIT (m);
                    if (!m_init || m_init->code == N_IGNORE) continue;
                    decl_t m_decl = m->attr;
                    if (!m_decl) continue;
                    struct type *m_type = m_decl->decl_spec.type;
                    struct expr *init_expr = m_init->attr;
                    if (!init_expr || !m_type) continue;
                    /* Emit store: *(var_base + elem_off + member_offset) = init_value */
                    MIR_type_t mt = get_mir_type(c2m_ctx, m_type);
                    op_t init_val;
                    if (init_expr->const_p && integer_type_p(m_type)) {
                      /* Constant integer: emit directly without gen */
                      init_val = new_op(NULL, MIR_new_int_op(ctx, init_expr->c.i_val));
                    } else {
                      init_val = gen(c2m_ctx, m_init, NULL, NULL, TRUE, NULL, NULL);
                    }
                    init_val = promote(c2m_ctx, init_val, mt, FALSE);
                    MIR_op_t dst = MIR_new_alias_mem_op(ctx, mt,
                        var.mir_op.u.mem.disp + (MIR_disp_t)(elem_off + m_decl->offset),
                        var.mir_op.u.mem.base, 0, 1,
                        get_type_alias(c2m_ctx, m_type), 0);
                    emit2(c2m_ctx, tp_mov(mt), dst, init_val.mir_op);
                  }
                }
              }
            }
          }
          /* RAII for automatic (stack) class objects: run the constructor now
             (after the storage has been zeroed / default-member-initialized
             above) and register the destructor to run at scope exit via the
             defer machinery (LIFO, also unwound on return/break/continue).
             Both calls were synthesized and type-checked in create_decl. */
          if (decl->ctor_call != NULL)
            gen (c2m_ctx, decl->ctor_call, NULL, NULL, FALSE, NULL, NULL);
          if (decl->dtor_call != NULL)
            VARR_PUSH (node_t, defer_stmts, decl->dtor_call);
          /* Null-initialize uninitialized local String variables, so that
             `String x;` is a well-defined null String (x == null is true and
             x.empty() is safe). */
          if (decl->scope != top_scope && !decl->decl_spec.static_p
              && builtin_string_type_p (decl->decl_spec.type)) {
            if (id->attr == NULL) {
              node_t saved_scope = curr_scope;
              curr_scope = decl->scope;
              check (c2m_ctx, id, NULL);
              curr_scope = saved_scope;
            }
            var = gen (c2m_ctx, id, NULL, NULL, FALSE, NULL, NULL);
            if (var.mir_op.mode == MIR_OP_REG
                || (var.mir_op.mode == MIR_OP_MEM && var.mir_op.u.mem.index == 0))
              emit2 (c2m_ctx, MIR_MOV, var.mir_op, MIR_new_int_op (ctx, 0));
          }
          /* Null-initialize uninitialized local pointer variables so that a
             later dereference is a well-defined null-pointer access instead of
             a wild read of stack garbage.  Combined with the -fexceptions null
             guard this turns `int *p; *p;` into a catchable safety trap (see
             bugs/010-uninit-read.cy).  Ownership's definite-assignment pass
             additionally warns at the read site.  Never touch the synthesized
             method receiver `this` (create_decl stores it as a block-scoped
             pointer SPEC_DECL without an initializer). */
          if (decl->scope != top_scope && decl->scope != NULL
              && decl->scope->code == N_BLOCK
              && !decl->decl_spec.static_p
              && !decl->decl_spec.extern_p
              && decl->decl_spec.type != NULL
              && decl->decl_spec.type->mode == TM_PTR
              && !builtin_string_type_p (decl->decl_spec.type)
              && id != NULL && id->code == N_ID
              && id->u.s.s != NULL && strcmp (id->u.s.s, "this") != 0) {
            if (id->attr == NULL) {
              node_t saved_scope = curr_scope;
              curr_scope = decl->scope;
              check (c2m_ctx, id, NULL);
              curr_scope = saved_scope;
            }
            var = gen (c2m_ctx, id, NULL, NULL, FALSE, NULL, NULL);
            if (var.mir_op.mode == MIR_OP_REG
                || (var.mir_op.mode == MIR_OP_MEM && var.mir_op.u.mem.index == 0))
              emit2 (c2m_ctx, MIR_MOV, var.mir_op, MIR_new_int_op (ctx, 0));
          }
        } else if (initializer->code != N_IGNORE && decl->decl_spec.type->mode == TM_DICT
                   && decl->scope == top_scope) {
          /* Global dict initializer: generate a module-init function that builds the
             dict at runtime and stores the pointer into the global BSS slot. */
          decl->u.item = MIR_new_bss (ctx, name, sizeof (void *));
          {
            /* Save current function context and register state */
            MIR_item_t saved_func = curr_func;
            int saved_reg_free_mark = reg_free_mark;
            char init_name[256];
            snprintf (init_name, sizeof init_name, "__dict_init_%s", name);
            reg_free_mark = 0;
            curr_func = MIR_new_func (ctx, init_name, 0, NULL, 0);
            dict_init_funcs[dict_init_func_count++] = curr_func; /* remember for main to call */

            /* obj = dict_create_object() */
            op_t obj = gen_dict_create_object (c2m_ctx);
            /* populate from initializer AST */
            gen_dict_init_list (c2m_ctx, obj.mir_op, initializer);
            /* Store obj pointer into the global BSS variable */
            {
              MIR_item_t bss_item = decl->u.item;
              op_t addr_temp = get_new_temp (c2m_ctx, MIR_T_I64);
              emit2 (c2m_ctx, MIR_MOV, addr_temp.mir_op, MIR_new_ref_op (ctx, bss_item));
              emit2 (c2m_ctx, MIR_MOV,
                     MIR_new_mem_op (ctx, MIR_T_I64, 0, addr_temp.mir_op.u.reg, 0, 1),
                     obj.mir_op);
            }
            emit_insn (c2m_ctx, MIR_new_ret_insn (ctx, 0));
            MIR_finish_func (ctx);
            /* Restore gen state: clear reg vars from init func */
            HTAB_CLEAR (reg_var_t, reg_var_tab);
            curr_func = saved_func;
            reg_free_mark = saved_reg_free_mark;
          }
        } else if (initializer->code != N_IGNORE && decl->decl_spec.type->mode == TM_DICT
                   && decl->scope != top_scope && initializer->code == N_LIST) {
          /* Local dict brace initializer: dict d = { "k": v, ... };
             Emit dict creation calls inline in the current function. */
          if (id->attr == NULL) {
            node_t saved_scope = curr_scope;
            curr_scope = decl->scope;
            check (c2m_ctx, id, NULL);
            curr_scope = saved_scope;
          }
          var = gen (c2m_ctx, id, NULL, NULL, FALSE, NULL, NULL);
          op_t obj = gen_dict_create_object (c2m_ctx);
          gen_dict_init_list (c2m_ctx, obj.mir_op, initializer);
          /* Store obj pointer into the local variable */
          emit2 (c2m_ctx, MIR_MOV, var.mir_op.mode == MIR_OP_REG ? var.mir_op
                 : var.mir_op, obj.mir_op);
        } else if (initializer->code != N_IGNORE && decl->decl_spec.type->mode == TM_DICT
                   && decl->scope != top_scope) {
          /* Local dict scalar initializer: dict d = expr;
             Evaluate the expression and store the pointer. */
          if (id->attr == NULL) {
            node_t saved_scope = curr_scope;
            curr_scope = decl->scope;
            check (c2m_ctx, id, NULL);
            curr_scope = saved_scope;
          }
          var = gen (c2m_ctx, id, NULL, NULL, FALSE, NULL, NULL);
          op_t val_op = val_gen (c2m_ctx, initializer);
          emit2 (c2m_ctx, MIR_MOV, var.mir_op, val_op.mir_op);
        } else if (initializer->code == N_NEW && decl->scope != top_scope) {
          /* Local:  ClassType *p = new ClassType(...);
             The result of `new` is a heap pointer (an ordinary scalar value);
             evaluate it and store directly, bypassing the aggregate
             collect_init_els path (which is for constant/braced initializers). */
          if (id->attr == NULL) {
            node_t saved_scope = curr_scope;
            curr_scope = decl->scope;
            check (c2m_ctx, id, NULL);
            curr_scope = saved_scope;
          }
          var = gen (c2m_ctx, id, NULL, NULL, FALSE, NULL, NULL);
          op_t val_op = val_gen (c2m_ctx, initializer);
          emit2 (c2m_ctx, MIR_MOV, var.mir_op, val_op.mir_op);
        } else if (initializer->code == N_MOVE && decl->scope != top_scope
                   && decl->decl_spec.type->mode == TM_CLASS) {
          /* Local:  List<int> b = move a;  (class-value ownership transfer)
             Evaluate the move (zeros the source) and block-copy into `b`.
             Register the RAII dtor (create_decl always synthesizes it for
             stack class values with a user destructor). */
          if (id->attr == NULL) {
            node_t saved_scope = curr_scope;
            curr_scope = decl->scope;
            check (c2m_ctx, id, NULL);
            curr_scope = saved_scope;
          }
          var = gen (c2m_ctx, id, NULL, NULL, FALSE, NULL, NULL);
          {
            op_t mval = gen (c2m_ctx, initializer, NULL, NULL, FALSE, NULL, NULL);
            mir_size_t csize = type_size (c2m_ctx, decl->decl_spec.type);
            if (csize > 0) block_move (c2m_ctx, var, mval, csize);
          }
          if (decl->dtor_call != NULL)
            VARR_PUSH (node_t, defer_stmts, decl->dtor_call);
        } else if ((initializer->code == N_CALL || initializer->code == N_STMTEXPR)
                   && decl->scope != top_scope
                   && decl->decl_spec.type->mode == TM_CLASS
                   && class_is_move_only_collection_p (c2m_ctx, decl->decl_spec.type)) {
          /* Local:  auto xs = make();  /  List xs = src.Take(3);
             / capturing HOF desugar ({ …; move r; })
             By-value collection return: evaluate the call or stmtexpr,
             block-copy into the local, then RAII-register ~List/~Map/~Set so the
             buffer is freed at scope exit.  This is the first-class idiom — no
             `owned auto` for everyday LINQ pipelines. */
          if (id->attr == NULL) {
            node_t saved_scope = curr_scope;
            curr_scope = decl->scope;
            check (c2m_ctx, id, NULL);
            curr_scope = saved_scope;
          }
          var = gen (c2m_ctx, id, NULL, NULL, FALSE, NULL, NULL);
          {
            op_t mval = gen (c2m_ctx, initializer, NULL, NULL, FALSE, NULL, NULL);
            mir_size_t csize = type_size (c2m_ctx, decl->decl_spec.type);
            if (csize > 0) block_move (c2m_ctx, var, mval, csize);
          }
          if (decl->dtor_call != NULL)
            VARR_PUSH (node_t, defer_stmts, decl->dtor_call);
        } else if (initializer->code != N_IGNORE) {  // ??? general code
          init_start = VARR_LENGTH (init_el_t, init_els);
          collect_init_els (c2m_ctx, NULL, &decl->decl_spec.type, initializer,
                            decl->decl_spec.linkage == N_STATIC
                              || decl->decl_spec.linkage == N_EXTERN || decl->decl_spec.static_p
                              || decl->decl_spec.thread_local_p,
                            TRUE);
          qsort (VARR_ADDR (init_el_t, init_els) + init_start,
                 VARR_LENGTH (init_el_t, init_els) - init_start, sizeof (init_el_t), cmp_init_el);
          if (id->attr == NULL) {
            node_t saved_scope = curr_scope;

            curr_scope = decl->scope;
            check (c2m_ctx, id, NULL);
            curr_scope = saved_scope;
          }
          if (decl->scope == top_scope || decl->decl_spec.static_p
              || decl->decl_spec.thread_local_p) {
            var = new_op (decl, MIR_new_ref_op (ctx, NULL));
          } else {
            var = gen (c2m_ctx, id, NULL, NULL, FALSE, NULL, NULL);
            assert (var.decl != NULL
                    && (var.mir_op.mode == MIR_OP_REG
                        || (var.mir_op.mode == MIR_OP_MEM && var.mir_op.u.mem.index == 0)));
          }
          int local_p = (decl->scope != top_scope && !decl->decl_spec.static_p
                         && !decl->decl_spec.thread_local_p);
          gen_initializer (c2m_ctx, init_start, var, name,
                           type_size (c2m_ctx, decl->decl_spec.type),
                           local_p);
          VARR_TRUNC (init_el_t, init_els, init_start);
        }
        /* Managed-ownership (`owned`/`move`) and -fauto-release: register the
           synthesized scope-exit release for this binding.  Done here — after
           the whole initializer if/else chain — so it fires for BOTH
           initialized (`owned auto x = new T();`, `auto y = move x;`,
           `char *p = malloc(n);`) and uninitialized declarators.  The
           ownership pass set decl->auto_release_call to a checked `delete p;`
           / `release(p);` node; routing it through defer_stmts makes it unwind
           at scope exit and on every return/break/continue. */
        if (decl->auto_release_call != NULL) {
          /* -fexceptions: shadow-stack registration too (pointer captured by
             value here), so a throw's longjmp can still run the release --
             see gen_defer_shadow_push.  No-op for non-class `release(p);`
             shapes. */
          gen_defer_shadow_push (c2m_ctx, decl->auto_release_call);
          VARR_PUSH (node_t, defer_stmts, decl->auto_release_call);
        }
        if (decl->u.item != NULL && decl->scope == top_scope && !decl->decl_spec.static_p) {
          MIR_new_export (ctx, name);
        } else if (decl->u.item != NULL && decl->scope != top_scope && decl->decl_spec.static_p) {
          MIR_item_t item = MIR_new_forward (ctx, name);

          move_item_forward (c2m_ctx, item);
        }
        /* [[registry("NAME")]] lowering: emit an exported pointer to this
           top-level record under a discoverable, prefixed symbol
           (__cyreg_<NAME>__<var>).  The JIT driver enumerates these across
           modules to synthesize __start_cyreg_<NAME>/__stop_cyreg_<NAME>; in
           AOT they land in an ELF/Mach-O section with the same anchors. */
        if (decl->u.item != NULL && decl->scope == top_scope) {
          const char *reg = registry_attr_name (attrs);
          if (reg != NULL) {
            /* Emit one exported-address entry per record: `__cyreg_<NAME>__...`
               is a pointer to the record.  The JIT driver enumerates these by
               scanning module items; b2obj/b2objmac place them into an ELF/
               Mach-O section `cyreg_<NAME>` so the system linker provides the
               `__start_/__stop_` anchors.  A process-global counter keeps the
               names unique across modules within a single (JIT) invocation;
               in AOT they are emitted as local symbols so per-object repeats
               are harmless.  NAME must not contain `__` (it delimits the
               registry key from the per-record suffix). */
            static int cyreg_counter = 0;
            char rn[512];
            snprintf (rn, sizeof rn, "__cyreg_%s__%s_%d", reg, name, cyreg_counter++);
            MIR_new_ref_data (ctx, rn, decl->u.item, 0);
          }
        }
      }
    }
    break;
  }
  case N_ST_ASSERT: /* do nothing */ break;
  case N_INIT: break;  // ???
  case N_INTERFACE: break; /* compile-time contract only; emits no code */
  case N_CLASS: {
    /* Skip generic templates — only their monomorphized specializations are generated */
    if (r->attr == (void *)((intptr_t)-1)) break;
#ifdef C2MIR_PREPRO_DEBUG
    printf("gen processing N_CLASS\n");
#endif
    node_t id = NL_HEAD(r->u.ops);
    node_t decl_list = NL_NEXT(id);

    if (c2m_options->debug_p) {
      printf("DEBUG: Processing CLASS node %p\n", (void*)r);
      if (id->code == N_ID) {
        printf("DEBUG: Class name = %s\n", id->u.s.s);
      }
    }

    if (decl_list->code != N_IGNORE) {
      for (node_t member = NL_HEAD(decl_list->u.ops); member != NULL; member = NL_NEXT(member)) {
        if (member->code == N_FUNC_DEF) {
          if (c2m_options->debug_p) {
            printf("DEBUG: Found method in class, generating code\n");
          }
          /* Nested classes: methods are pre-generated by gen_nested_class_methods_in
             before the enclosing MIR function opens.  While that outer function is
             open, skip re-entry here (MIR forbids nested funcs); otherwise emit. */
          if (curr_func != NULL) continue;
          gen (c2m_ctx, member, true_label, false_label, val_p, desirable_dest, expect_res);
        }
      }
      /* Build process-wide singletons for declarative dict members so that
         ClassName.member (and the ClassName.Variant sugar) have storage.
         Done here, between top-level definitions, where no MIR function is
         open for construction. */
      for (node_t member = NL_HEAD(decl_list->u.ops); member != NULL; member = NL_NEXT(member)) {
        if (member->code != N_MEMBER) continue;
        node_t mdecl = MEMBER_DECL (member);
        node_t mid = (mdecl != NULL && mdecl->code == N_DECL) ? DECL_ID (mdecl) : NULL;
        node_t minit = MEMBER_INIT (member);
        if (minit != NULL && minit->code == N_LIST && mid != NULL && mid->code == N_ID)
          ensure_class_static_dict (c2m_ctx, r, mid->u.s.s);
      }
    }
    break;
  }
  case N_FUNC_DEF: {
    /* Skip generic function templates (sentinel attr): only their
       monomorphized specializations are generated. */
    if (r->attr == (void *)((intptr_t)-1)) break;
    /* Midopt P0: unreachable class methods — no MIR body. */
    if (r->attr != NULL && ((decl_t) r->attr)->midopt_dead_p) break;
    node_t decl_specs = FUNC_DEF_SPECS (r);
    node_t declarator = FUNC_DEF_DECL (r);
    node_t decls = FUNC_DEF_DECLS (r);
    node_t stmt = FUNC_DEF_BLOCK (r);
    struct node_scope *ns = stmt->attr;
    decl_t param_decl, func_decl = r->attr;
    struct type *decl_type = func_decl->decl_spec.type;
    node_t first_param, param, param_declarator, param_id;
    struct type *param_type;
    MIR_insn_t insn;
    MIR_type_t res_type, param_mir_type;
    MIR_reg_t fp_reg, param_reg;
    target_arg_info_t arg_info;
    char name[256] = {0};

    // Detect class methods reliably via the function type's recorded class scope
    // (set in check_declarator).  Methods are mangled as `Class_method__<params>`
    // so they do not collide with global functions or with same-named methods of
    // other classes, and so overloads of one class become distinct symbols.
    int is_method = FALSE;
    const char *class_name = NULL;
    node_t id = DECL_ID (declarator);
    const char *base_name = id->u.s.s;
    struct func_type *gen_ft = decl_type->u.func_type;

    if (gen_ft->class_scope != NULL && gen_ft->class_scope->code == N_CLASS) {
      node_t class_id = TAG_ID (gen_ft->class_scope);
      if (class_id != NULL && class_id->code == N_ID) {
        is_method = TRUE;
        class_name = class_id->u.s.s;
      }
    }

    assert (declarator != NULL && declarator->code == N_DECL
            && NL_HEAD (declarator->u.ops)->code == N_ID);
    assert (decl_type->mode == TM_FUNC);
    reg_free_mark = 0;
    stmtexpr_last_expr = NULL;
    curr_func_def = r;
    curr_call_arg_area_offset = 0;
    collect_args_and_func_types (c2m_ctx, decl_type->u.func_type, NULL);

    /* Mangled MIR name for this function/method; uses the same encoding as
       the pre-gen forward-declaration pass (see gen_forward_class_methods)
       so refs emitted before vs after this point all resolve to the same
       MIR item at link time. */
    mangle_func_def_mir_name (c2m_ctx, r, name, sizeof (name));

    /* Block-scoped classes defined in this function body have methods that
       must become MIR functions.  MIR forbids opening a new function while
       another is open, so walk the body now (before MIR_new_func for the
       outer function) and generate any nested-class methods. */
    if (stmt != NULL && stmt->code == N_BLOCK && curr_func == NULL)
      gen_nested_class_methods_in (c2m_ctx, stmt);

    curr_func = ((decl_type->u.func_type->dots_p
                    ? MIR_new_vararg_func_arr
                    : MIR_new_func_arr) (ctx, name,
                                         VARR_LENGTH (MIR_type_t, proto_info.ret_types),
                                         VARR_ADDR (MIR_type_t, proto_info.ret_types),
                                         VARR_LENGTH (MIR_var_t, proto_info.arg_vars),
                                         VARR_ADDR (MIR_var_t, proto_info.arg_vars)));
    func_decl->u.item = curr_func;
    DLIST_INIT (MIR_insn_t, slow_code_part);
    if (ns->stack_var_p /* we can have empty struct only with size 0 and still need a frame: */
        || ns->size > 0) {
      fp_reg = MIR_new_func_reg (ctx, curr_func->u.func, MIR_T_I64, FP_NAME);
      MIR_append_insn (ctx, curr_func,
                       MIR_new_insn (ctx, MIR_ALLOCA, MIR_new_reg_op (ctx, fp_reg),
                                     MIR_new_int_op (ctx, ns->size)));
    }
    for (size_t i = 0; i < VARR_LENGTH (MIR_var_t, proto_info.arg_vars); i++)
      get_reg_var (c2m_ctx, MIR_T_UNDEF, VARR_GET (MIR_var_t, proto_info.arg_vars, i).name, NULL);
    target_init_arg_vars (c2m_ctx, &arg_info);
    if ((first_param = NL_HEAD (decl_type->u.func_type->param_list->u.ops)) != NULL
        && !void_param_p (first_param)) {
      for (param = first_param; param != NULL; param = NL_NEXT (param)) {
        param_declarator = NL_EL (param->u.ops, 1);
        assert (param_declarator != NULL && param_declarator->code == N_DECL);
        param_decl = param->attr;
        param_id = NL_HEAD (param_declarator->u.ops);
        param_type = param_decl->decl_spec.type;
        assert (!param_decl->reg_p
                || (param_type->mode != TM_STRUCT && param_type->mode != TM_UNION
                    && param_type->mode != TM_CLASS));
        const char *param_name = get_param_name (c2m_ctx, param_type, param_id->u.s.s);
        if (target_gen_gather_arg (c2m_ctx, param_name, param_type, param_decl, &arg_info)) continue;
        if (param_decl->reg_p) continue;
        if (param_type->mode == TM_STRUCT
            || param_type->mode == TM_UNION
            || param_type->mode == TM_CLASS) { /* block pass for aggregates */
          param_reg = get_reg_var (c2m_ctx, MIR_POINTER_TYPE, param_name, NULL).reg;
          val = new_op (NULL, MIR_new_mem_op (ctx, MIR_T_UNDEF, 0, param_reg, 0, 1));
          var
            = new_op (param_decl, MIR_new_mem_op (ctx, MIR_T_UNDEF, param_decl->offset,
                                                  MIR_reg (ctx, FP_NAME, curr_func->u.func), 0, 1));
          block_move (c2m_ctx, var, val, type_size (c2m_ctx, param_type));
        } else {
          param_mir_type = get_mir_type (c2m_ctx, param_type);
          emit2 (c2m_ctx, tp_mov (param_mir_type),
                 MIR_new_alias_mem_op (ctx, param_mir_type, param_decl->offset,
                                       MIR_reg (ctx, FP_NAME, curr_func->u.func), 0, 1,
                                       get_type_alias (c2m_ctx, param_type), 0),
                 MIR_new_reg_op (ctx, get_reg_var (c2m_ctx, MIR_T_UNDEF, param_name, NULL).reg));
        }
      }
    }
    /* Inject dict module init calls at the start of main() */
    if (dict_init_func_count > 0 && strcmp (name, "main") == 0) {
      MIR_item_t init_proto = MIR_new_proto_arr (ctx, "__dict_init_p", 0, NULL, 0, NULL);
      move_item_to_module_start (curr_func->module, init_proto);
      for (int di = 0; di < dict_init_func_count; di++) {
        MIR_op_t init_args[2];
        init_args[0] = MIR_new_ref_op (ctx, init_proto);
        init_args[1] = MIR_new_ref_op (ctx, dict_init_funcs[di]);
        emit_insn (c2m_ctx, MIR_new_insn_arr (ctx, MIR_CALL, 2, init_args));
      }
      dict_init_func_count = 0; /* consumed */
    }
    /* Automatic String scope reclamation: if the body allocates Strings
       (substr/replace), take an allocation checkpoint here and release back to
       it at every exit so per-call temporaries don't accumulate.  Returned
       Strings are protected (see N_RETURN).  Saved/restored for re-entrancy. */
    {
      int saved_str_scope_active = str_scope_active;
      op_t saved_str_scope_mark = str_scope_mark;
      int saved_obj_scope_active = obj_scope_active;
      op_t saved_obj_scope_mark = obj_scope_mark;
      /* Per-loop nested scope state: always FALSE entering a function body
         (we are not yet inside any loop).  Loop cases turn it on/off around
         their body gen and save/restore it for nesting. */
      int saved_loop_str_scope_active = loop_str_scope_active;
      op_t saved_loop_str_scope_mark = loop_str_scope_mark;
      int saved_loop_obj_scope_active = loop_obj_scope_active;
      op_t saved_loop_obj_scope_mark = loop_obj_scope_mark;
      str_scope_active = FALSE;
      obj_scope_active = FALSE;
      loop_str_scope_active = FALSE;
      loop_obj_scope_active = FALSE;
      loop_break_label_for_scope = NULL;
      /* Skip when the body retains a String into a collection anywhere
         (list->Add(s) / map[k] = s on a String-typed collection): such a
         collection may escape this function (e.g. via return) with the
         String still referenced, so releasing at this function's exit would
         free it out from under whoever ends up holding the collection --
         same reasoning as the object-arena gate below, and the same bug
         shape as the Any<I> collection-escape fix (SHORTCOMINGS.md).
         subtree_retains_string_in_collection_p already existed (used only
         for the per-loop gate below) but was never applied here. */
      if (subtree_allocates_string_p (stmt)
          && !subtree_retains_string_in_collection_p (stmt)) {
        str_scope_mark = gen_str_checkpoint (c2m_ctx);
        str_scope_active = TRUE;
      }
      /* Automatic object-arena (Any<I> handle) reclamation: checkpoint here and
         release at every exit so handles that don't escape are destroyed
         (running ~__Any_I, freeing the wrapped concrete) when the scope ends.
         Skip entirely when the body retains a handle into a collection
         (subtree_retains_object_in_collection_p): such a collection may be
         returned (or otherwise escape) with the handle still referenced, and
         releasing at this function's exit would free it out from under
         whoever ends up holding the collection. Handles built by a function
         like that fall back to ordinary heap-pointer semantics -- the
         collection (or its final owner) is responsible for them, same as any
         other List<T*> of owned pointers. */
      if (subtree_allocates_object_p (stmt)
          && !subtree_retains_object_in_collection_p (stmt)) {
        obj_scope_mark = gen_obj_checkpoint (c2m_ctx);
        obj_scope_active = TRUE;
      }
      gen (c2m_ctx, stmt, NULL, NULL, FALSE, NULL, NULL);
      if ((insn = DLIST_TAIL (MIR_insn_t, curr_func->u.func->insns)) == NULL
          || (insn->code != MIR_RET && insn->code != MIR_JRET && insn->code != MIR_JMP)) {
        /* fall-through exit: reclaim before the synthesized return */
        if (obj_scope_active) gen_obj_release_to (c2m_ctx, obj_scope_mark.mir_op);
        if (str_scope_active) gen_str_release_to (c2m_ctx, str_scope_mark.mir_op);
      }
      str_scope_active = saved_str_scope_active;
      str_scope_mark = saved_str_scope_mark;
      obj_scope_active = saved_obj_scope_active;
      obj_scope_mark = saved_obj_scope_mark;
      loop_str_scope_active = saved_loop_str_scope_active;
      loop_str_scope_mark = saved_loop_str_scope_mark;
      loop_obj_scope_active = saved_loop_obj_scope_active;
      loop_obj_scope_mark = saved_loop_obj_scope_mark;
    }
    if ((insn = DLIST_TAIL (MIR_insn_t, curr_func->u.func->insns)) == NULL
        || (insn->code != MIR_RET && insn->code != MIR_JRET && insn->code != MIR_JMP)) {
      /* The body may have called other functions/methods, each of which
         repopulates the shared proto_info for its own signature.  Restore this
         function's return signature before synthesizing the fall-through return,
         so e.g. a void function ending in a non-void method call still emits
         `ret` (0 operands) rather than a stray `ret <val>`. */
      collect_args_and_func_types (c2m_ctx, decl_type->u.func_type, NULL);
      if (VARR_LENGTH (MIR_type_t, proto_info.ret_types) == 0) {
        if (jump_ret_p)
          emit1 (c2m_ctx, MIR_JRET, MIR_new_int_op (ctx, 0));
        else
          emit_insn (c2m_ctx, MIR_new_ret_insn (ctx, 0));
      } else {
        VARR_TRUNC (MIR_op_t, ret_ops, 0);
        for (size_t i = 0; i < VARR_LENGTH (MIR_type_t, proto_info.ret_types); i++) {
          res_type = VARR_GET (MIR_type_t, proto_info.ret_types, i);
          if (res_type == MIR_T_D) {
            VARR_PUSH (MIR_op_t, ret_ops, MIR_new_double_op (ctx, 0.0));
          } else if (res_type == MIR_T_LD) {
            VARR_PUSH (MIR_op_t, ret_ops, MIR_new_ldouble_op (ctx, 0.0));
          } else if (res_type == MIR_T_F) {
            VARR_PUSH (MIR_op_t, ret_ops, MIR_new_float_op (ctx, 0.0));
          } else {
            VARR_PUSH (MIR_op_t, ret_ops, MIR_new_int_op (ctx, 0));
          }
        }
        emit_insn (c2m_ctx, MIR_new_insn_arr (ctx, MIR_RET, VARR_LENGTH (MIR_op_t, ret_ops),
                                              VARR_ADDR (MIR_op_t, ret_ops)));
      }
    }
    while ((insn = DLIST_HEAD (MIR_insn_t, slow_code_part)) != NULL) {
      DLIST_REMOVE (MIR_insn_t, slow_code_part, insn);
      DLIST_APPEND (MIR_insn_t, curr_func->u.func->insns, insn);
    }
    // RSD Ensure all function references in instructions are valid
    for (MIR_insn_t insn = DLIST_HEAD(MIR_insn_t, curr_func->u.func->insns);
         insn != NULL; insn = DLIST_NEXT(MIR_insn_t, insn)) {
      for (size_t i = 0; i < insn->nops; i++) {
        if (insn->ops[i].mode == MIR_OP_REF && insn->ops[i].u.ref == NULL) {
          warning(c2m_ctx, no_pos,
                  "Unresolved function reference in MIR generation (in %s)",
                  curr_func != NULL && curr_func->u.func != NULL ? curr_func->u.func->name : "?");
          break;
        }
      }
    }
#if !MIR_NO_DBINFO
    if (c2m_options->debug_info_p) dbinfo_emit_func_vars (c2m_ctx, r);
#endif
    MIR_finish_func (ctx);
    /* MIR_finish_func clears MIR's open-func state; keep gen_ctx in sync so a
       later N_CLASS (methods of List specializations, nested classes) is not
       treated as "nested" and skipped. */
    curr_func = NULL;

    // NEW: Export with the appropriate name
    if (func_decl->decl_spec.linkage == N_EXTERN) {
        if (is_method && class_name != NULL) {
            /* Monomorphized generic specializations (List<T>/Map<K,V>/Set<T>
               and any user generic class) are regenerated independently and
               identically in every TU that instantiates them -- exporting
               them makes each such TU globally define the same MIR symbol
               name, so a program built from 2+ files sharing an
               instantiation fails to load ("func ... is prohibited for
               redefinition" at MIR_load_module, mir.c) even though the
               compile itself succeeds. Keep them module-local instead;
               ordinary methods still need real export for cross-file calls. */
            if (!generic_specialization_mir_name_p (class_name)
                && !generic_specialization_mir_name_p (name))
                MIR_new_export (ctx, name); // Use the mangled name for methods
        } else if (!generic_specialization_mir_name_p (DECL_ID (declarator)->u.s.s)) {
            MIR_new_export (ctx, DECL_ID (declarator)->u.s.s); // Use original name for regular functions
        }
    }



    finish_curr_func_reg_vars (c2m_ctx);
    break;
  }
  case N_STMTEXPR: {
    /* MIR #452: statement-expression value correctness. */
    struct type *stmtexpr_type = ((struct expr *) r->attr)->type;
    node_t block = NL_HEAD (r->u.ops);
    node_t last_stmt = NL_TAIL (NL_EL (block->u.ops, 1)->u.ops);
    node_t saved_last_expr = stmtexpr_last_expr;

    stmtexpr_last_expr
      = (last_stmt != NULL && last_stmt->code == N_EXPR) ? NL_EL (last_stmt->u.ops, 1) : NULL;
    {
      /* Inner N_EXPR statements reset curr_call_arg_area_offset; restore so
         this result's copy gets a unique slot (MIR #452). */
      mir_size_t saved_arg_off = curr_call_arg_area_offset;
      gen (c2m_ctx, block, NULL, NULL, FALSE, NULL, NULL);
      curr_call_arg_area_offset = saved_arg_off;
    }
    stmtexpr_last_expr = saved_last_expr;
    res = top_gen_last_op;
    /* Copy aggregate results into a call-arg-area temp.  Locals live at low
       FP offsets; call-arg area sits after them (ns->size - call_arg_area_size).
       Using a stale local-style offset (old se->c.u_val) collided with the
       first stack variable — fatal for capturing HOF open-code that both
       reads and returns a List. */
    if (stmtexpr_type != NULL
        && (stmtexpr_type->mode == TM_STRUCT || stmtexpr_type->mode == TM_UNION
            || stmtexpr_type->mode == TM_CLASS)) {
      mir_size_t size = type_size (c2m_ctx, stmtexpr_type);
      struct node_scope *ns
        = (struct node_scope *) FUNC_DEF_BLOCK (curr_func_def)->attr;
      mir_size_t arg_area_offset
        = curr_call_arg_area_offset + ns->size - ns->call_arg_area_size;
      op_t base = get_new_temp (c2m_ctx, MIR_T_I64);
      emit3 (c2m_ctx, MIR_ADD, base.mir_op,
             MIR_new_reg_op (ctx, MIR_reg (ctx, FP_NAME, curr_func->u.func)),
             MIR_new_int_op (ctx, arg_area_offset));
      update_call_arg_area_offset (c2m_ctx, stmtexpr_type, FALSE);
      op_t tmp
        = new_op (NULL, MIR_new_mem_op (ctx, MIR_T_UNDEF, 0, base.mir_op.u.reg, 0, 1));
      if (size > 0) block_move (c2m_ctx, tmp, res, size);
      res = tmp;
    }
    break;
  }
  case N_BLOCK: {
    size_t defer_mark = VARR_LENGTH (node_t, defer_stmts);
    MIR_insn_t last;
    emit_label (c2m_ctx, r);
    gen (c2m_ctx, NL_EL (r->u.ops, 1), NULL, NULL, FALSE, NULL, NULL);
    /* Run this block's deferred statements (LIFO) on the fall-through exit.
       Exits via return/break/continue already ran them at the jump, so skip if
       the block ends in a terminator. */
    last = DLIST_TAIL (MIR_insn_t, curr_func->u.func->insns);
    if (last == NULL
        || (last->code != MIR_RET && last->code != MIR_JRET && last->code != MIR_JMP))
      gen_run_defers (c2m_ctx, defer_mark);
    VARR_TRUNC (node_t, defer_stmts, defer_mark);
    break;
  }
  case N_MODULE: gen (c2m_ctx, NL_HEAD (r->u.ops), NULL, NULL, FALSE, NULL, NULL); break;  // ???
  case N_IF: {
    node_t expr = NL_EL (r->u.ops, 1);
    node_t if_stmt = NL_NEXT (expr);
    node_t else_stmt = NL_NEXT (if_stmt);
    MIR_label_t if_label = MIR_new_label (ctx), else_label = MIR_new_label (ctx);
    MIR_label_t end_label = MIR_new_label (ctx);
    int cond_expect_res, cond_known;
    MIR_insn_t insn, last = NULL;

    assert (false_label == NULL && true_label == NULL);
    emit_label (c2m_ctx, r);
    /* C11: skip the dead arm so midopt can prune methods only named there
       (is_pointer<T>(), strlen("…"), `x && 0`, `_Generic` lives in N_GENERIC). */
    cond_known = c11_cond_known (expr);
    if (cond_known == 1 && c11_dead_skippable_p (else_stmt)) {
      gen (c2m_ctx, expr, NULL, NULL, FALSE, NULL, NULL);
      gen (c2m_ctx, if_stmt, NULL, NULL, FALSE, NULL, NULL);
      break;
    }
    if (cond_known == 0 && c11_dead_skippable_p (if_stmt)) {
      gen (c2m_ctx, expr, NULL, NULL, FALSE, NULL, NULL);
      if (else_stmt != NULL && else_stmt->code != N_IGNORE)
        gen (c2m_ctx, else_stmt, NULL, NULL, FALSE, NULL, NULL);
      break;
    }
    top_gen (c2m_ctx, expr, if_label, else_label, &cond_expect_res);
    if (cond_expect_res == 0 || cond_expect_res == 1) { /* fall through on true */
      emit_label_insn_opt (c2m_ctx, if_label);
      gen (c2m_ctx, if_stmt, NULL, NULL, FALSE, NULL, NULL);
      if (cond_expect_res == 0) {
        emit1 (c2m_ctx, MIR_JMP, MIR_new_label_op (ctx, end_label));
        emit_label_insn_opt (c2m_ctx, else_label);
        gen (c2m_ctx, else_stmt, NULL, NULL, FALSE, NULL, NULL);
      } else {
        last = DLIST_TAIL (MIR_insn_t, curr_func->u.func->insns);
        MIR_append_insn (ctx, curr_func, else_label);
        gen (c2m_ctx, else_stmt, NULL, NULL, FALSE, NULL, NULL);
      }
    } else { /* fall on false */
      emit_label_insn_opt (c2m_ctx, else_label);
      gen (c2m_ctx, else_stmt, NULL, NULL, FALSE, NULL, NULL);
      last = DLIST_TAIL (MIR_insn_t, curr_func->u.func->insns);
      MIR_append_insn (ctx, curr_func, if_label);
      gen (c2m_ctx, if_stmt, NULL, NULL, FALSE, NULL, NULL);
    }
    if (last != NULL) { /* move to slow part of code */
      while ((insn = DLIST_NEXT (MIR_insn_t, last)) != NULL) {
        DLIST_REMOVE (MIR_insn_t, curr_func->u.func->insns, insn);
        DLIST_APPEND (MIR_insn_t, slow_code_part, insn);
      }
      DLIST_APPEND (MIR_insn_t, slow_code_part,
                    MIR_new_insn (ctx, MIR_JMP, MIR_new_label_op (ctx, end_label)));
    }
    emit_label_insn_opt (c2m_ctx, end_label);
    break;
  }
  case N_SWITCH: {
    node_t expr = NL_EL (r->u.ops, 1);
    node_t stmt = NL_NEXT (expr);
    struct switch_attr *switch_attr = r->attr;
    op_t case_reg_op;
    struct expr *e2;
    case_t c;
    MIR_label_t saved_break_label = break_label;
    size_t saved_defer_break_mark = defer_break_mark;
    int signed_p, short_p;
    size_t len;
    mir_ullong range = 0;

    assert (false_label == NULL && true_label == NULL);
    emit_label (c2m_ctx, r);
    break_label = MIR_new_label (ctx);
    defer_break_mark = VARR_LENGTH (node_t, defer_stmts);
    case_reg_op = val_gen (c2m_ctx, expr);
    type = ((struct expr *) expr->attr)->type;
    signed_p = signed_integer_type_p (type);
    mir_type = get_mir_type (c2m_ctx, type);
    short_p = mir_type != MIR_T_I64 && mir_type != MIR_T_U64;
    case_reg_op = force_reg (c2m_ctx, case_reg_op, mir_type);
    if (switch_attr->min_val_case != NULL) {
      e = NL_HEAD (switch_attr->min_val_case->case_node->u.ops)->attr;
      e2 = NL_HEAD (switch_attr->max_val_case->case_node->u.ops)->attr;
      range = signed_p ? (mir_ullong) (e2->c.i_val - e->c.i_val) : e2->c.u_val - e->c.u_val;
    }
    len = DLIST_LENGTH (case_t, switch_attr->case_labels);
    if (!switch_attr->ranges_p && len > 4 && range != 0 && range / len < 3) { /* use MIR_SWITCH */
      mir_ullong curr_val, prev_val, n;
      op_t index = get_new_temp (c2m_ctx, MIR_T_I64);
      MIR_label_t label = break_label;

      c = DLIST_TAIL (case_t, switch_attr->case_labels);
      if (c->case_node->code == N_DEFAULT) {
        assert (DLIST_NEXT (case_t, c) == NULL);
        label = get_label (c2m_ctx, c->case_target_node);
      }
      emit3 (c2m_ctx, short_p ? MIR_SUBS : MIR_SUB, index.mir_op, case_reg_op.mir_op,
             signed_p ? MIR_new_int_op (ctx, e->c.i_val) : MIR_new_uint_op (ctx, e->c.u_val));
      emit3 (c2m_ctx, short_p ? MIR_UBGTS : MIR_UBGT, MIR_new_label_op (ctx, label), index.mir_op,
             MIR_new_uint_op (ctx, range));
      if (short_p) emit2 (c2m_ctx, MIR_UEXT32, index.mir_op, index.mir_op);
      VARR_TRUNC (case_t, switch_cases, 0);
      for (c = DLIST_HEAD (case_t, switch_attr->case_labels);
           c != NULL && c->case_node->code != N_DEFAULT; c = DLIST_NEXT (case_t, c))
        VARR_PUSH (case_t, switch_cases, c);
      qsort (VARR_ADDR (case_t, switch_cases), VARR_LENGTH (case_t, switch_cases), sizeof (case_t),
             signed_p ? signed_case_compare : unsigned_case_compare);
      VARR_TRUNC (MIR_op_t, switch_ops, 0);
      VARR_PUSH (MIR_op_t, switch_ops, index.mir_op);
      for (size_t i = 0; i < VARR_LENGTH (case_t, switch_cases); i++) {
        c = VARR_GET (case_t, switch_cases, i);
        e2 = NL_HEAD (c->case_node->u.ops)->attr;
        curr_val = signed_p ? (mir_ullong) (e2->c.i_val - e->c.i_val) : e2->c.u_val - e->c.u_val;
        if (i != 0) {
          for (n = prev_val + 1; n < curr_val; n++)
            VARR_PUSH (MIR_op_t, switch_ops, MIR_new_label_op (ctx, label));
        }
        VARR_PUSH (MIR_op_t, switch_ops,
                   MIR_new_label_op (ctx, get_label (c2m_ctx, c->case_target_node)));
        prev_val = curr_val;
      }
      emit_insn (c2m_ctx, MIR_new_insn_arr (ctx, MIR_SWITCH, VARR_LENGTH (MIR_op_t, switch_ops),
                                            VARR_ADDR (MIR_op_t, switch_ops)));
    } else {
      for (c = DLIST_HEAD (case_t, switch_attr->case_labels); c != NULL;
           c = DLIST_NEXT (case_t, c)) {
        MIR_label_t cont_label, label = get_label (c2m_ctx, c->case_target_node);
        node_t case_expr, case_expr2;

        if (c->case_node->code == N_DEFAULT) {
          assert (DLIST_NEXT (case_t, c) == NULL);
          emit1 (c2m_ctx, MIR_JMP, MIR_new_label_op (ctx, label));
          break;
        }
        case_expr = NL_HEAD (c->case_node->u.ops);
        case_expr2 = NL_NEXT (case_expr);
        e = case_expr->attr;
        assert (e->const_p && integer_type_p (e->type));
        if (case_expr2 == NULL) {
          emit3 (c2m_ctx, short_p ? MIR_BEQS : MIR_BEQ, MIR_new_label_op (ctx, label),
                 case_reg_op.mir_op, MIR_new_int_op (ctx, e->c.i_val));
        } else {
          e2 = case_expr2->attr;
          assert (e2->const_p && integer_type_p (e2->type));
          cont_label = MIR_new_label (ctx);
          if (signed_p) {
            emit3 (c2m_ctx, short_p ? MIR_BLTS : MIR_BLT, MIR_new_label_op (ctx, cont_label),
                   case_reg_op.mir_op, MIR_new_int_op (ctx, e->c.i_val));
            emit3 (c2m_ctx, short_p ? MIR_BLES : MIR_BLE, MIR_new_label_op (ctx, label),
                   case_reg_op.mir_op, MIR_new_int_op (ctx, e2->c.i_val));
          } else {
            emit3 (c2m_ctx, short_p ? MIR_UBLTS : MIR_UBLT, MIR_new_label_op (ctx, cont_label),
                   case_reg_op.mir_op, MIR_new_int_op (ctx, e->c.i_val));
            emit3 (c2m_ctx, short_p ? MIR_UBLES : MIR_UBLE, MIR_new_label_op (ctx, label),
                   case_reg_op.mir_op, MIR_new_int_op (ctx, e2->c.i_val));
          }
          emit_label_insn_opt (c2m_ctx, cont_label);
        }
      }
      if (c == NULL) /* no default: */
        emit1 (c2m_ctx, MIR_JMP, MIR_new_label_op (ctx, break_label));
    }
    top_gen (c2m_ctx, stmt, NULL, NULL, NULL);
    emit_label_insn_opt (c2m_ctx, break_label);
    break_label = saved_break_label;
    defer_break_mark = saved_defer_break_mark;
    break;
  }
  case N_DO: {
    node_t expr = NL_EL (r->u.ops, 1);
    node_t stmt = NL_NEXT (expr);
    MIR_label_t saved_continue_label = continue_label, saved_break_label = break_label;
    size_t saved_defer_break_mark = defer_break_mark;
    size_t saved_defer_continue_mark = defer_continue_mark;
    MIR_label_t start_label = MIR_new_label (ctx);
    /* Per-iteration arena scope state (str/obj). */
    int saved_loop_str_active, saved_loop_obj_active;
    op_t saved_loop_str_mark, saved_loop_obj_mark;
    MIR_label_t saved_loop_break_label;

    assert (false_label == NULL && true_label == NULL);
    continue_label = MIR_new_label (ctx);
    break_label = MIR_new_label (ctx);
    defer_break_mark = defer_continue_mark = VARR_LENGTH (node_t, defer_stmts);
    emit_label (c2m_ctx, r);
    emit_label_insn_opt (c2m_ctx, start_label);
    /* Take per-iter checkpoint at the top of the body so the next iteration
       starts fresh (released at continue_label below).  Position matters:
       the checkpoint MIR call is emitted between start_label and the body,
       so the back-edge jump to start_label runs it again each iteration. */
    gen_loop_body_scope_enter (c2m_ctx, stmt, break_label,
                               &saved_loop_str_active, &saved_loop_str_mark,
                               &saved_loop_obj_active, &saved_loop_obj_mark,
                               &saved_loop_break_label);
    gen (c2m_ctx, stmt, NULL, NULL, FALSE, NULL, NULL);
    emit_label_insn_opt (c2m_ctx, continue_label);
    /* Release the iteration's allocations before the back-edge condition. */
    gen_loop_body_scope_release (c2m_ctx);
    top_gen (c2m_ctx, expr, start_label, break_label, NULL);
    emit_label_insn_opt (c2m_ctx, break_label);
    gen_loop_body_scope_leave (c2m_ctx,
                               saved_loop_str_active, saved_loop_str_mark,
                               saved_loop_obj_active, saved_loop_obj_mark,
                               saved_loop_break_label);
    continue_label = saved_continue_label;
    break_label = saved_break_label;
    defer_break_mark = saved_defer_break_mark;
    defer_continue_mark = saved_defer_continue_mark;
    break;
  }
  case N_WHILE: {
    node_t expr = NL_EL (r->u.ops, 1);
    node_t stmt = NL_NEXT (expr);
    MIR_label_t stmt_label;
    if (c11_cond_known (expr) == 0 && c11_dead_skippable_p (stmt)) {
      emit_label (c2m_ctx, r);
      gen (c2m_ctx, expr, NULL, NULL, FALSE, NULL, NULL);
      break;
    }
    stmt_label = MIR_new_label (ctx);
    MIR_label_t saved_continue_label = continue_label, saved_break_label = break_label;
    size_t saved_defer_break_mark = defer_break_mark;
    size_t saved_defer_continue_mark = defer_continue_mark;
    int saved_loop_str_active, saved_loop_obj_active;
    op_t saved_loop_str_mark, saved_loop_obj_mark;
    MIR_label_t saved_loop_break_label;

    assert (false_label == NULL && true_label == NULL);
    continue_label = MIR_new_label (ctx);
    break_label = MIR_new_label (ctx);
    defer_break_mark = defer_continue_mark = VARR_LENGTH (node_t, defer_stmts);
    emit_label (c2m_ctx, r);
    emit_label_insn_opt (c2m_ctx, continue_label);
    top_gen (c2m_ctx, expr, stmt_label, break_label, NULL);
    emit_label_insn_opt (c2m_ctx, stmt_label);
    /* Checkpoint at top of body: hit by both initial entry and back-edge.
       Note: `continue` jumps to continue_label which re-evaluates the cond
       BEFORE re-entering stmt_label; N_CONTINUE itself emits the release. */
    gen_loop_body_scope_enter (c2m_ctx, stmt, break_label,
                               &saved_loop_str_active, &saved_loop_str_mark,
                               &saved_loop_obj_active, &saved_loop_obj_mark,
                               &saved_loop_break_label);
    gen (c2m_ctx, stmt, NULL, NULL, FALSE, NULL, NULL);
    /* Release before the back-edge cond eval. */
    gen_loop_body_scope_release (c2m_ctx);
    top_gen (c2m_ctx, expr, stmt_label, break_label, NULL);
    emit_label_insn_opt (c2m_ctx, break_label);
    gen_loop_body_scope_leave (c2m_ctx,
                               saved_loop_str_active, saved_loop_str_mark,
                               saved_loop_obj_active, saved_loop_obj_mark,
                               saved_loop_break_label);
    continue_label = saved_continue_label;
    break_label = saved_break_label;
    defer_break_mark = saved_defer_break_mark;
    defer_continue_mark = saved_defer_continue_mark;
    break;
  }
  case N_FOR: {
    node_t init = NL_EL (r->u.ops, 1);
    node_t cond = NL_NEXT (init);
    node_t iter = NL_NEXT (cond);
    node_t stmt = NL_NEXT (iter);
    MIR_label_t stmt_label;
    if (cond != NULL && cond->code != N_IGNORE && c11_cond_known (cond) == 0
        && c11_dead_skippable_p (stmt) && c11_dead_skippable_p (iter)) {
      emit_label (c2m_ctx, r);
      top_gen (c2m_ctx, init, NULL, NULL, NULL);
      gen (c2m_ctx, cond, NULL, NULL, FALSE, NULL, NULL);
      break;
    }
    stmt_label = MIR_new_label (ctx);
    MIR_label_t saved_continue_label = continue_label, saved_break_label = break_label;
    size_t saved_defer_break_mark = defer_break_mark;
    size_t saved_defer_continue_mark = defer_continue_mark;
    int saved_loop_str_active, saved_loop_obj_active;
    op_t saved_loop_str_mark, saved_loop_obj_mark;
    MIR_label_t saved_loop_break_label;
    int saved_hoist_n = hoist_n; /* scope R-LICM memo entries to this loop */

    assert (false_label == NULL && true_label == NULL);
    continue_label = MIR_new_label (ctx);
    break_label = MIR_new_label (ctx);
    defer_break_mark = defer_continue_mark = VARR_LENGTH (node_t, defer_stmts);
    emit_label (c2m_ctx, r);
    top_gen (c2m_ctx, init, NULL, NULL, NULL);
    if (cond->code != N_IGNORE) /* non-empty condition: */
      top_gen (c2m_ctx, cond, stmt_label, break_label, NULL);
    emit_label_insn_opt (c2m_ctx, stmt_label);
    /* Per-iter checkpoint at top of body (after cond ok, before body). */
    gen_loop_body_scope_enter (c2m_ctx, stmt, break_label,
                               &saved_loop_str_active, &saved_loop_str_mark,
                               &saved_loop_obj_active, &saved_loop_obj_mark,
                               &saved_loop_break_label);
    gen (c2m_ctx, stmt, NULL, NULL, FALSE, NULL, NULL);
    emit_label_insn_opt (c2m_ctx, continue_label);
    /* Release before iter+cond.  `continue` jumps to continue_label and the
       N_CONTINUE handler already emitted its own release; this second
       release is idempotent (release_to is a no-op when the tracker is
       already at or below the mark). */
    gen_loop_body_scope_release (c2m_ctx);
    top_gen (c2m_ctx, iter, NULL, NULL, NULL);
    if (cond->code == N_IGNORE) { /* empty condition: */
      emit1 (c2m_ctx, MIR_JMP, MIR_new_label_op (ctx, stmt_label));
    } else {
      top_gen (c2m_ctx, cond, stmt_label, break_label, NULL);
    }
    emit_label_insn_opt (c2m_ctx, break_label);
    gen_loop_body_scope_leave (c2m_ctx,
                               saved_loop_str_active, saved_loop_str_mark,
                               saved_loop_obj_active, saved_loop_obj_mark,
                               saved_loop_break_label);
    continue_label = saved_continue_label;
    break_label = saved_break_label;
    defer_break_mark = saved_defer_break_mark;
    defer_continue_mark = saved_defer_continue_mark;
    hoist_n = saved_hoist_n; /* drop this loop's R-LICM memo entries */
    break;
  }
  case N_IN: {
    /* "key" in dict  →  dict_object_get(dict, key) != 0 */
    node_t key_node = NL_HEAD (r->u.ops);
    node_t dict_node = NL_EL (r->u.ops, 1);
    op1 = val_gen (c2m_ctx, key_node);  /* key (string) */
    op2 = val_gen (c2m_ctx, dict_node); /* dict */
    op_t got = gen_dict_object_get (c2m_ctx, op2.mir_op, op1.mir_op);
    if (true_label != NULL) {
      emit2 (c2m_ctx, MIR_BT, MIR_new_label_op (ctx, true_label), got.mir_op);
      emit1 (c2m_ctx, MIR_JMP, MIR_new_label_op (ctx, false_label));
      true_label = false_label = NULL;
    } else {
      res = get_new_temp (c2m_ctx, MIR_T_I64);
      emit3 (c2m_ctx, MIR_NE, res.mir_op, got.mir_op, MIR_new_int_op (ctx, 0));
    }
    break;
  }
  case N_FORIN: {
    /* for (auto var[, var2] in collection) body
       children: labels(0), key_id(1), val_id_or_ignore(2), collection(3), body(4) */
    node_t labels  = NL_HEAD (r->u.ops);
    node_t key_id  = NL_NEXT (labels);
    node_t val_id  = NL_NEXT (key_id);
    node_t coll    = val_id ? NL_NEXT (val_id) : NULL;
    node_t body    = coll ? NL_NEXT (coll) : NULL;
    if (!coll || !body) break; /* malformed FORIN node */
    MIR_label_t loop_label = MIR_new_label (ctx);
    MIR_label_t body_label = MIR_new_label (ctx);
    MIR_label_t saved_continue_label = continue_label;
    MIR_label_t saved_break_label = break_label;
    size_t saved_defer_break_mark = defer_break_mark;
    size_t saved_defer_continue_mark = defer_continue_mark;
    int saved_loop_str_active, saved_loop_obj_active;
    op_t saved_loop_str_mark, saved_loop_obj_mark;
    MIR_label_t saved_loop_break_label;
    continue_label = MIR_new_label (ctx);
    break_label = MIR_new_label (ctx);
    defer_break_mark = defer_continue_mark = VARR_LENGTH (node_t, defer_stmts);
    unsigned fsn = r->attr ? ((struct node_scope *) r->attr)->func_scope_num : 0;
    int saved_byref_n = gen_byref_n; /* R2: pop this loop's bindings after the body */

    assert (false_label == NULL && true_label == NULL);
    emit_label (c2m_ctx, r);

    struct expr *coll_e = coll->attr;
    struct type *coll_type = coll_e ? coll_e->type : NULL;
    int arr_forin = (coll_type && (coll_type->mode == TM_ARR
                     || (coll_type->mode == TM_PTR && coll_type->arr_type != NULL)));
    int slice_forin = (coll_type && coll_type->mode == TM_SLICE);
    int class_forin
      = (coll_type && !arr_forin && !slice_forin
         && ((coll_type->mode == TM_PTR && coll_type->u.ptr_type->mode == TM_CLASS)
             || coll_type->mode == TM_CLASS));
    op_t i_reg = {0}; /* loop counter — shared by all branches */

    if (arr_forin || slice_forin) {
      /* ---- Array / slice for-in ---- */
      struct type *el_type;
      op_t base_reg = get_new_temp (c2m_ctx, MIR_T_I64);
      op_t n_save = get_new_temp (c2m_ctx, MIR_T_I64);

      if (arr_forin) {
        struct type *orig_arr = (coll_type->mode == TM_ARR ? coll_type : coll_type->arr_type);
        el_type = orig_arr->u.arr_type->el_type;
        /* base = address of array, n = compile-time array length */
        op_t arr_op = gen (c2m_ctx, coll, NULL, NULL, FALSE, NULL, NULL);
        if (arr_op.mir_op.mode == MIR_OP_MEM)
          base_reg = mem_to_address (c2m_ctx, arr_op, TRUE);
        else
          emit2 (c2m_ctx, MIR_MOV, base_reg.mir_op, arr_op.mir_op);
        emit2 (c2m_ctx, MIR_MOV, n_save.mir_op,
               MIR_new_int_op (ctx, get_arr_type_size (orig_arr)));
      } else {
        /* slice: n is in the header, elements start after it */
        el_type = coll_type->u.ptr_type;
        op_t ptr = force_reg (c2m_ctx, val_gen (c2m_ctx, coll), MIR_T_I64);
        emit2 (c2m_ctx, MIR_MOV, n_save.mir_op,
               MIR_new_mem_op (ctx, MIR_T_I64, 0, ptr.mir_op.u.reg, 0, 1));
        emit3 (c2m_ctx, MIR_ADD, base_reg.mir_op, ptr.mir_op,
               MIR_new_int_op (ctx, SLICE_HDR_SIZE));
      }
      MIR_type_t el_mir_t = get_mir_type (c2m_ctx, el_type);
      mir_size_t el_size = type_size (c2m_ctx, el_type);

      /* i = 0 */
      i_reg = get_new_temp (c2m_ctx, MIR_T_I64);
      emit2 (c2m_ctx, MIR_MOV, i_reg.mir_op, MIR_new_int_op (ctx, 0));

      /* loop_label: if i >= n goto break */
      emit_label_insn_opt (c2m_ctx, loop_label);
      emit3 (c2m_ctx, MIR_BGE, MIR_new_label_op (ctx, break_label),
             i_reg.mir_op, n_save.mir_op);
      emit_label_insn_opt (c2m_ctx, body_label);

      /* Compute element address: base + i * el_size */
      op_t offset_reg = get_new_temp (c2m_ctx, MIR_T_I64);
      emit3 (c2m_ctx, MIR_MUL, offset_reg.mir_op, i_reg.mir_op,
             MIR_new_int_op (ctx, (long) el_size));
      op_t addr_reg = get_new_temp (c2m_ctx, MIR_T_I64);
      emit3 (c2m_ctx, MIR_ADD, addr_reg.mir_op, base_reg.mir_op, offset_reg.mir_op);

      if (val_id->code == N_ID) {
        /* Two-var form: key_id = i (index), val_id = arr[i] (element) */
        MIR_type_t idx_mir_t = promote_mir_int_type (MIR_T_I32);
        const char *iname = get_reg_var_name (c2m_ctx, idx_mir_t, key_id->u.s.s, fsn);
        reg_var_t ivar = get_reg_var (c2m_ctx, idx_mir_t, iname, NULL);
        emit2 (c2m_ctx, tp_mov (idx_mir_t), MIR_new_reg_op (ctx, ivar.reg), i_reg.mir_op);

        MIR_type_t val_t = promote_mir_int_type (el_mir_t);
        const char *vname = get_reg_var_name (c2m_ctx, val_t, val_id->u.s.s, fsn);
        reg_var_t vvar = get_reg_var (c2m_ctx, val_t, vname, NULL);
        emit2 (c2m_ctx, tp_mov (val_t),
               MIR_new_reg_op (ctx, vvar.reg),
               MIR_new_mem_op (ctx, el_mir_t, 0, addr_reg.mir_op.u.reg, 0, 1));
      } else {
        /* Single-var form: key_id = arr[i] (element) */
        MIR_type_t var_t = promote_mir_int_type (el_mir_t);
        const char *kname = get_reg_var_name (c2m_ctx, var_t, key_id->u.s.s, fsn);
        reg_var_t kvar = get_reg_var (c2m_ctx, var_t, kname, NULL);
        emit2 (c2m_ctx, tp_mov (var_t),
               MIR_new_reg_op (ctx, kvar.reg),
               MIR_new_mem_op (ctx, el_mir_t, 0, addr_reg.mir_op.u.reg, 0, 1));
      }
    } else if (class_forin) {
      /* ---- Class iteration protocol for-in ----
         Index protocol (List/Set):  x = coll->Get(i)        (i in [0, Count()))
         Dense path (List/Set with data+length fields): load length/data once,
         then *(data + i) — no Count/Get calls (Phase C3).
         Keyed protocol (Map<K,V>):  k = coll->KeyAt(i), v = coll->ValAt(i) */
      struct type *cls_type = coll_type->mode == TM_PTR ? coll_type->u.ptr_type : coll_type;
      struct type *this_type
        = coll_type->mode == TM_PTR ? coll_type : create_ptr_type (c2m_ctx, cls_type);
      node_t count_def
        = find_class_protocol_method (c2m_ctx, cls_type->u.tag_type, "Count", 0, POS (r));
      node_t get_def
        = find_class_protocol_method (c2m_ctx, cls_type->u.tag_type, "Get", 1, POS (r));
      node_t keyat_def
        = find_class_protocol_method (c2m_ctx, cls_type->u.tag_type, "KeyAt", 1, POS (r));
      node_t valat_def
        = find_class_protocol_method (c2m_ctx, cls_type->u.tag_type, "ValAt", 1, POS (r));
      int map_proto = (keyat_def != NULL && valat_def != NULL);
      decl_t data_field = NULL, len_field = NULL;
      int dense_list_p = 0;

      /* validated during check */
      assert (count_def != NULL && (map_proto || get_def != NULL));
      /* Evaluate the receiver pointer once, before the loop. */
      op_t this_reg = get_new_temp (c2m_ctx, MIR_T_I64);
      int forin_sf = GEN_SAFE_SKIP_NULL; /* after check below (or stack) */
      if (coll_type->mode == TM_PTR) {
        op_t cv = val_gen (c2m_ctx, coll);
        emit2 (c2m_ctx, MIR_MOV, this_reg.mir_op, cv.mir_op);
        if (c2m_options->exceptions_p)
          gen_null_check (c2m_ctx, force_reg (c2m_ctx, this_reg, MIR_T_I64),
                          (long) POS (r).lno);
      } else { /* class lvalue: iterate over its address — never null */
        op_t cv = gen (c2m_ctx, coll, NULL, NULL, FALSE, NULL, NULL);
        if (cv.mir_op.mode == MIR_OP_MEM) cv = mem_to_address (c2m_ctx, cv, TRUE);
        emit2 (c2m_ctx, MIR_MOV, this_reg.mir_op, cv.mir_op);
      }

      /* Detect List (`data`+`length`) / Set (`dense`+`count`) dense buffer. */
      if (!map_proto && cls_type->u.tag_type != NULL)
        dense_list_p
          = find_dense_buffer_fields (cls_type->u.tag_type, &data_field, &len_field, NULL);

      /* Dense Map: count + keys + vals arrays (insertion-ordered). */
      decl_t map_keys_f = NULL, map_vals_f = NULL, map_cnt_f = NULL;
      int dense_map_p = 0;
      if (map_proto && cls_type->u.tag_type != NULL) {
        map_keys_f = find_class_field_by_name (cls_type->u.tag_type, "keys");
        map_vals_f = find_class_field_by_name (cls_type->u.tag_type, "vals");
        map_cnt_f = find_class_field_by_name (cls_type->u.tag_type, "count");
        dense_map_p = (map_cnt_f != NULL && map_cnt_f->decl_spec.type != NULL
                       && map_cnt_f->decl_spec.type->mode == TM_BASIC && map_keys_f != NULL
                       && map_keys_f->decl_spec.type != NULL
                       && map_keys_f->decl_spec.type->mode == TM_PTR
                       && map_keys_f->decl_spec.type->u.ptr_type != NULL
                       && map_vals_f != NULL && map_vals_f->decl_spec.type != NULL
                       && map_vals_f->decl_spec.type->mode == TM_PTR
                       && map_vals_f->decl_spec.type->u.ptr_type != NULL);
      }

      op_t n_save = get_new_temp (c2m_ctx, MIR_T_I64);
      op_t data_base = {0};
      op_t keys_base = {0}, vals_base = {0};
      struct type *dense_el_type = NULL;
      mir_size_t dense_el_size = 0;
      MIR_type_t dense_el_mir = MIR_T_I64;
      struct type *dense_k_type = NULL, *dense_v_type = NULL;
      mir_size_t dense_k_size = 0, dense_v_size = 0;
      MIR_type_t dense_k_mir = MIR_T_I64, dense_v_mir = MIR_T_I64;

      if (dense_list_p) {
        /* n = this->length; data_base = this->data */
        MIR_type_t lt = get_mir_type (c2m_ctx, len_field->decl_spec.type);
        MIR_alias_t lalias = get_type_alias (c2m_ctx, len_field->decl_spec.type);
        MIR_alias_t dalias = get_type_alias (c2m_ctx, data_field->decl_spec.type);
        op_t raw_len = get_new_temp (c2m_ctx, promote_mir_int_type (lt));
        emit2 (c2m_ctx, tp_mov (lt), raw_len.mir_op,
               MIR_new_alias_mem_op (ctx, lt, (MIR_disp_t) len_field->offset,
                                     this_reg.mir_op.u.reg, 0, 1, lalias, 0));
        emit2 (c2m_ctx, MIR_MOV, n_save.mir_op, raw_len.mir_op);
        data_base = get_new_temp (c2m_ctx, MIR_T_I64);
        emit2 (c2m_ctx, MIR_MOV, data_base.mir_op,
               MIR_new_alias_mem_op (ctx, MIR_T_I64, (MIR_disp_t) data_field->offset,
                                     this_reg.mir_op.u.reg, 0, 1, dalias, 0));
        dense_el_type = data_field->decl_spec.type->u.ptr_type;
        dense_el_size = type_size (c2m_ctx, dense_el_type);
        dense_el_mir = get_mir_type (c2m_ctx, dense_el_type);
      } else if (dense_map_p) {
        MIR_type_t ct = get_mir_type (c2m_ctx, map_cnt_f->decl_spec.type);
        MIR_alias_t calias = get_type_alias (c2m_ctx, map_cnt_f->decl_spec.type);
        MIR_alias_t kalias = get_type_alias (c2m_ctx, map_keys_f->decl_spec.type);
        MIR_alias_t valias = get_type_alias (c2m_ctx, map_vals_f->decl_spec.type);
        op_t raw_cnt = get_new_temp (c2m_ctx, promote_mir_int_type (ct));
        emit2 (c2m_ctx, tp_mov (ct), raw_cnt.mir_op,
               MIR_new_alias_mem_op (ctx, ct, (MIR_disp_t) map_cnt_f->offset,
                                     this_reg.mir_op.u.reg, 0, 1, calias, 0));
        emit2 (c2m_ctx, MIR_MOV, n_save.mir_op, raw_cnt.mir_op);
        keys_base = get_new_temp (c2m_ctx, MIR_T_I64);
        vals_base = get_new_temp (c2m_ctx, MIR_T_I64);
        emit2 (c2m_ctx, MIR_MOV, keys_base.mir_op,
               MIR_new_alias_mem_op (ctx, MIR_T_I64, (MIR_disp_t) map_keys_f->offset,
                                     this_reg.mir_op.u.reg, 0, 1, kalias, 0));
        emit2 (c2m_ctx, MIR_MOV, vals_base.mir_op,
               MIR_new_alias_mem_op (ctx, MIR_T_I64, (MIR_disp_t) map_vals_f->offset,
                                     this_reg.mir_op.u.reg, 0, 1, valias, 0));
        dense_k_type = map_keys_f->decl_spec.type->u.ptr_type;
        dense_v_type = map_vals_f->decl_spec.type->u.ptr_type;
        dense_k_size = type_size (c2m_ctx, dense_k_type);
        dense_v_size = type_size (c2m_ctx, dense_v_type);
        dense_k_mir = get_mir_type (c2m_ctx, dense_k_type);
        dense_v_mir = get_mir_type (c2m_ctx, dense_v_type);
      } else {
        /* n = coll->Count() — this already null-checked (or stack). */
        op_t n_res
          = gen_class_method_call_flags (c2m_ctx, count_def, this_type, this_reg, NULL, 0,
                                         forin_sf);
        emit2 (c2m_ctx, MIR_MOV, n_save.mir_op, n_res.mir_op);
      }

      /* i = 0 */
      i_reg = get_new_temp (c2m_ctx, MIR_T_I64);
      emit2 (c2m_ctx, MIR_MOV, i_reg.mir_op, MIR_new_int_op (ctx, 0));

      /* loop_label: if i >= n goto break */
      emit_label_insn_opt (c2m_ctx, loop_label);
      emit3 (c2m_ctx, MIR_BGE, MIR_new_label_op (ctx, break_label), i_reg.mir_op, n_save.mir_op);
      emit_label_insn_opt (c2m_ctx, body_label);

      /* Store SRC into the loop variable VAR_NODE using VAR_NODE's *declared*
         MIR type so the register name matches what N_ID reads in the body (same
         approach as the dict branch below). */
      #define STORE_FORIN_VAR(var_node, src_op) do {                                  \
        symbol_t _vs; MIR_type_t _vt = MIR_T_I64;                                      \
        if (symbol_find (c2m_ctx, S_REGULARS, (var_node), r, &_vs) && _vs.def_node != NULL \
            && _vs.def_node->attr != NULL)                                            \
          _vt = promote_mir_int_type (                                                \
                  get_mir_type (c2m_ctx, ((decl_t) _vs.def_node->attr)->decl_spec.type)); \
        const char *_vn = get_reg_var_name (c2m_ctx, _vt, (var_node)->u.s.s, fsn);     \
        reg_var_t _vv = get_reg_var (c2m_ctx, _vt, _vn, NULL);                         \
        emit2 (c2m_ctx, tp_mov (_vt), MIR_new_reg_op (ctx, _vv.reg), (src_op).mir_op); \
      } while (0)

      /* Compute the stack-slot MEM op for an aggregate (class/struct/union) loop
         variable, or set has_agg=0 for scalar/pointer element types.  The loop
         var N_ID is never checked, so we build its MEM op straight from the
         decl's frame offset. */
      #define FORIN_AGG_DEST(var_node, dst_out, vty_out, has_agg_out) do {             \
        symbol_t _vs; (vty_out) = NULL; (has_agg_out) = 0;                            \
        if (symbol_find (c2m_ctx, S_REGULARS, (var_node), r, &_vs) && _vs.def_node != NULL \
            && _vs.def_node->attr != NULL) {                                          \
          decl_t _vd = (decl_t) _vs.def_node->attr;                                   \
          (vty_out) = _vd->decl_spec.type;                                            \
          if ((vty_out)->mode == TM_CLASS || (vty_out)->mode == TM_STRUCT             \
              || (vty_out)->mode == TM_UNION) {                                       \
            (has_agg_out) = 1;                                                        \
            (dst_out) = new_op (_vd, MIR_new_alias_mem_op (ctx, MIR_T_UNDEF,          \
                                _vd->offset, MIR_reg (ctx, FP_NAME, curr_func->u.func), \
                                0, 1, get_type_alias (c2m_ctx, (vty_out)), 0));        \
          }                                                                           \
        }                                                                             \
      } while (0)

      /* i proven in [0,n) by loop header → SKIP_OOB on indexed protocol calls. */
      forin_sf |= GEN_SAFE_SKIP_OOB;

      if (dense_map_p) {
        /* Dense Map: k = keys[i]; v = vals[i] — no KeyAt/ValAt, no OOB. */
        op_t k_off = get_new_temp (c2m_ctx, MIR_T_I64);
        op_t k_addr = get_new_temp (c2m_ctx, MIR_T_I64);
        op_t k_agg; struct type *k_vty; int k_is_agg;
        FORIN_AGG_DEST (key_id, k_agg, k_vty, k_is_agg);
        emit3 (c2m_ctx, MIR_MUL, k_off.mir_op, i_reg.mir_op,
               MIR_new_int_op (ctx, (long long) dense_k_size));
        emit3 (c2m_ctx, MIR_ADD, k_addr.mir_op, keys_base.mir_op, k_off.mir_op);
        if (k_is_agg) {
          op_t src_mem
            = new_op (NULL, MIR_new_mem_op (ctx, MIR_T_UNDEF, 0, k_addr.mir_op.u.reg, 0, 1));
          block_move (c2m_ctx, k_agg, src_mem, dense_k_size);
        } else {
          MIR_type_t load_t = promote_mir_int_type (dense_k_mir);
          op_t k_res = get_new_temp (c2m_ctx, load_t);
          emit2 (c2m_ctx, tp_mov (dense_k_mir), k_res.mir_op,
                 MIR_new_mem_op (ctx, dense_k_mir, 0, k_addr.mir_op.u.reg, 0, 1));
          STORE_FORIN_VAR (key_id, k_res);
        }
        if (val_id->code == N_ID) {
          op_t v_off = get_new_temp (c2m_ctx, MIR_T_I64);
          op_t v_addr = get_new_temp (c2m_ctx, MIR_T_I64);
          op_t v_agg; struct type *v_vty; int v_is_agg;
          FORIN_AGG_DEST (val_id, v_agg, v_vty, v_is_agg);
          emit3 (c2m_ctx, MIR_MUL, v_off.mir_op, i_reg.mir_op,
                 MIR_new_int_op (ctx, (long long) dense_v_size));
          emit3 (c2m_ctx, MIR_ADD, v_addr.mir_op, vals_base.mir_op, v_off.mir_op);
          if (v_is_agg) {
            /* R2 borrow: midopt proved the value var read-only over an
               unmutated Map — bind by reference into the vals buffer. */
            symbol_t bsym;
            if (symbol_find (c2m_ctx, S_REGULARS, val_id, r, &bsym)
                && bsym.def_node != NULL && bsym.def_node->attr != NULL
                && ((decl_t) bsym.def_node->attr)->byref_p) {
              gen_byref_push ((decl_t) bsym.def_node->attr, v_addr.mir_op.u.reg);
            } else {
              op_t src_mem
                = new_op (NULL, MIR_new_mem_op (ctx, MIR_T_UNDEF, 0, v_addr.mir_op.u.reg, 0, 1));
              block_move (c2m_ctx, v_agg, src_mem, dense_v_size);
            }
          } else {
            MIR_type_t load_t = promote_mir_int_type (dense_v_mir);
            op_t v_res = get_new_temp (c2m_ctx, load_t);
            emit2 (c2m_ctx, tp_mov (dense_v_mir), v_res.mir_op,
                   MIR_new_mem_op (ctx, dense_v_mir, 0, v_addr.mir_op.u.reg, 0, 1));
            STORE_FORIN_VAR (val_id, v_res);
          }
        }
      } else if (map_proto) {
        /* Keyed protocol fallback (non-dense map layout). */
        {
          op_t k_agg; struct type *k_vty; int k_is_agg;
          FORIN_AGG_DEST (key_id, k_agg, k_vty, k_is_agg);
          op_t k_res = k_is_agg
            ? gen_class_method_call_dest (c2m_ctx, keyat_def, this_type, this_reg, &i_reg, 1,
                                          &k_agg, forin_sf)
            : gen_class_method_call_flags (c2m_ctx, keyat_def, this_type, this_reg, &i_reg, 1,
                                           forin_sf);
          if (!k_is_agg) STORE_FORIN_VAR (key_id, k_res);
        }
        if (val_id->code == N_ID) {
          op_t v_agg; struct type *v_vty; int v_is_agg;
          FORIN_AGG_DEST (val_id, v_agg, v_vty, v_is_agg);
          op_t v_res = v_is_agg
            ? gen_class_method_call_dest (c2m_ctx, valat_def, this_type, this_reg, &i_reg, 1,
                                          &v_agg, forin_sf)
            : gen_class_method_call_flags (c2m_ctx, valat_def, this_type, this_reg, &i_reg, 1,
                                           forin_sf);
          if (!v_is_agg) STORE_FORIN_VAR (val_id, v_res);
        }
      } else if (dense_list_p) {
        /* Dense List/Set: el = *(data + i); no Get/Count calls; i in bounds. */
        node_t el_var = val_id->code == N_ID ? val_id : key_id;
        op_t agg_dst; struct type *el_vty; int el_agg;
        op_t off = get_new_temp (c2m_ctx, MIR_T_I64);
        op_t addr = get_new_temp (c2m_ctx, MIR_T_I64);
        FORIN_AGG_DEST (el_var, agg_dst, el_vty, el_agg);
        emit3 (c2m_ctx, MIR_MUL, off.mir_op, i_reg.mir_op,
               MIR_new_int_op (ctx, (long long) dense_el_size));
        emit3 (c2m_ctx, MIR_ADD, addr.mir_op, data_base.mir_op, off.mir_op);
        if (val_id->code == N_ID) {
          MIR_type_t idx_t = promote_mir_int_type (MIR_T_I32);
          const char *iname = get_reg_var_name (c2m_ctx, idx_t, key_id->u.s.s, fsn);
          reg_var_t ivar = get_reg_var (c2m_ctx, idx_t, iname, NULL);
          emit2 (c2m_ctx, tp_mov (idx_t), MIR_new_reg_op (ctx, ivar.reg), i_reg.mir_op);
        }
        if (el_agg) {
          /* R2 borrow: midopt proved the loop var read-only over an unmutated
             collection — bind it by reference (pointer into the buffer)
             instead of a per-iteration block copy. */
          symbol_t bsym;
          if (symbol_find (c2m_ctx, S_REGULARS, el_var, r, &bsym)
              && bsym.def_node != NULL && bsym.def_node->attr != NULL
              && ((decl_t) bsym.def_node->attr)->byref_p) {
            gen_byref_push ((decl_t) bsym.def_node->attr, addr.mir_op.u.reg);
          } else {
            op_t src_mem
              = new_op (NULL, MIR_new_mem_op (ctx, MIR_T_UNDEF, 0, addr.mir_op.u.reg, 0, 1));
            block_move (c2m_ctx, agg_dst, src_mem, dense_el_size);
          }
        } else {
          MIR_type_t load_t = promote_mir_int_type (dense_el_mir);
          op_t el_res = get_new_temp (c2m_ctx, load_t);
          emit2 (c2m_ctx, tp_mov (dense_el_mir), el_res.mir_op,
                 MIR_new_mem_op (ctx, dense_el_mir, 0, addr.mir_op.u.reg, 0, 1));
          STORE_FORIN_VAR (el_var, el_res);
        }
      } else {
        /* elem = coll->Get(i) with proven this + index. */
        node_t el_var = val_id->code == N_ID ? val_id : key_id;
        op_t agg_dst; struct type *el_vty; int el_agg;
        FORIN_AGG_DEST (el_var, agg_dst, el_vty, el_agg);
        op_t el_res = el_agg
          ? gen_class_method_call_dest (c2m_ctx, get_def, this_type, this_reg, &i_reg, 1, &agg_dst,
                                        forin_sf)
          : gen_class_method_call_flags (c2m_ctx, get_def, this_type, this_reg, &i_reg, 1,
                                         forin_sf);
        if (val_id->code == N_ID) {
          MIR_type_t idx_t = promote_mir_int_type (MIR_T_I32);
          const char *iname = get_reg_var_name (c2m_ctx, idx_t, key_id->u.s.s, fsn);
          reg_var_t ivar = get_reg_var (c2m_ctx, idx_t, iname, NULL);
          emit2 (c2m_ctx, tp_mov (idx_t), MIR_new_reg_op (ctx, ivar.reg), i_reg.mir_op);
        }
        if (!el_agg) STORE_FORIN_VAR (el_var, el_res);
      }
      #undef STORE_FORIN_VAR
      #undef FORIN_AGG_DEST
    } else {
      /* ---- Dict for-in ----
         Dict carries a runtime tag (DICT_OBJECT vs DICT_ARRAY).  Use
         `dict_iter_count` for the unified bound and `dict_is_array` to
         dispatch the per-iteration variable bindings between
           OBJECT:  key = dict_object_key_at(i)   val = dict_object_value_at(i)
           ARRAY:   key = i (index)               val = dict_value_at(i)  (element)
         Single-var form maps to `key` per the existing dict-forin convention
         (key for objects, element for arrays). */
      op_t coll_val = val_gen (c2m_ctx, coll);
      op_t coll_reg = get_new_temp (c2m_ctx, MIR_T_I64);
      emit2 (c2m_ctx, MIR_MOV, coll_reg.mir_op, coll_val.mir_op);
      /* Compute is_array tag once: dict identity does not change inside the
         loop, so a single tag test outside the loop suffices. */
      op_t is_arr_call = gen_dict_is_array (c2m_ctx, coll_reg.mir_op);
      op_t is_arr_save = get_new_temp (c2m_ctx, MIR_T_I64);
      emit2 (c2m_ctx, MIR_MOV, is_arr_save.mir_op, is_arr_call.mir_op);
      op_t n_reg = gen_dict_iter_count (c2m_ctx, coll_reg.mir_op);
      op_t n_save = get_new_temp (c2m_ctx, MIR_T_I64);
      emit2 (c2m_ctx, MIR_MOV, n_save.mir_op, n_reg.mir_op);
      i_reg = get_new_temp (c2m_ctx, MIR_T_I64);
      emit2 (c2m_ctx, MIR_MOV, i_reg.mir_op, MIR_new_int_op (ctx, 0));
      emit_label_insn_opt (c2m_ctx, loop_label);
      emit3 (c2m_ctx, MIR_BGE, MIR_new_label_op (ctx, break_label),
             i_reg.mir_op, n_save.mir_op);
      emit_label_insn_opt (c2m_ctx, body_label);

      /* Resolve the destination registers for the loop variable(s) using
         their *declared* MIR types so they match the readers in the body. */
      MIR_type_t kt = MIR_T_I64;
      {
        symbol_t ksym;
        if (symbol_find (c2m_ctx, S_REGULARS, key_id, r, &ksym) && ksym.def_node != NULL
            && ksym.def_node->attr != NULL)
          kt = promote_mir_int_type (
                 get_mir_type (c2m_ctx, ((decl_t) ksym.def_node->attr)->decl_spec.type));
      }
      const char *kname = get_reg_var_name (c2m_ctx, kt, key_id->u.s.s, fsn);
      reg_var_t kvar = get_reg_var (c2m_ctx, kt, kname, NULL);

      reg_var_t vvar = {0};
      MIR_type_t vt = MIR_T_I64;
      int two_var = (val_id->code == N_ID);
      if (two_var) {
        symbol_t vsym;
        if (symbol_find (c2m_ctx, S_REGULARS, val_id, r, &vsym) && vsym.def_node != NULL
            && vsym.def_node->attr != NULL)
          vt = promote_mir_int_type (
                 get_mir_type (c2m_ctx, ((decl_t) vsym.def_node->attr)->decl_spec.type));
        const char *vname = get_reg_var_name (c2m_ctx, vt, val_id->u.s.s, fsn);
        vvar = get_reg_var (c2m_ctx, vt, vname, NULL);
      }

      /* Runtime dispatch on dict tag.
           if (is_array) goto arr_body;
           // object path:
           key_var = dict_object_key_at(d, i)
           if (two_var) val_var = dict_object_value_at(d, i)
           goto after_dispatch
         arr_body:
           // array path:
           if (two_var) key_var = i; val_var = dict_value_at(d, i)
           else         key_var = dict_value_at(d, i)
         after_dispatch: */
      MIR_label_t arr_body_label = MIR_new_label (ctx);
      MIR_label_t after_dispatch_label = MIR_new_label (ctx);
      emit3 (c2m_ctx, MIR_BNE, MIR_new_label_op (ctx, arr_body_label),
             is_arr_save.mir_op, MIR_new_int_op (ctx, 0));

      /* ---- object path ---- */
      {
        op_t key_reg = gen_dict_object_key_at (c2m_ctx, coll_reg.mir_op, i_reg.mir_op);
        emit2 (c2m_ctx, MIR_MOV, MIR_new_reg_op (ctx, kvar.reg), key_reg.mir_op);
        if (two_var) {
          op_t val_reg = gen_dict_object_value_at (c2m_ctx, coll_reg.mir_op, i_reg.mir_op);
          emit2 (c2m_ctx, MIR_MOV, MIR_new_reg_op (ctx, vvar.reg), val_reg.mir_op);
        }
      }
      emit1 (c2m_ctx, MIR_JMP, MIR_new_label_op (ctx, after_dispatch_label));

      /* ---- array path ---- */
      emit_label_insn_opt (c2m_ctx, arr_body_label);
      {
        op_t elem_reg = gen_dict_value_at (c2m_ctx, coll_reg.mir_op, i_reg.mir_op);
        if (two_var) {
          /* k = i (index), v = element */
          emit2 (c2m_ctx, MIR_MOV, MIR_new_reg_op (ctx, kvar.reg), i_reg.mir_op);
          emit2 (c2m_ctx, MIR_MOV, MIR_new_reg_op (ctx, vvar.reg), elem_reg.mir_op);
        } else {
          /* single var = element (DictValue*).  The declared variable type
             is char* (per the type checker), but at MIR level both are I64
             pointers; the dict-typed body code paths consume it correctly. */
          emit2 (c2m_ctx, MIR_MOV, MIR_new_reg_op (ctx, kvar.reg), elem_reg.mir_op);
        }
      }
      emit_label_insn_opt (c2m_ctx, after_dispatch_label);
    }

    /* Per-iter checkpoint right before the body (key/val regs already bound).
       Released at continue_label below; the for-in back-edge is the i_reg++
       and unconditional jump to loop_label, so every iteration re-enters the
       body via body_label and takes a fresh checkpoint. */
    gen_loop_body_scope_enter (c2m_ctx, body, break_label,
                               &saved_loop_str_active, &saved_loop_str_mark,
                               &saved_loop_obj_active, &saved_loop_obj_mark,
                               &saved_loop_break_label);
    /* gen body */
    gen (c2m_ctx, body, NULL, NULL, FALSE, NULL, NULL);
    gen_byref_n = saved_byref_n; /* pop this loop's by-ref bindings */
    /* continue_label: i_reg++ */
    emit_label_insn_opt (c2m_ctx, continue_label);
    /* Release iteration's per-loop allocations before back-edge. */
    gen_loop_body_scope_release (c2m_ctx);
    emit3 (c2m_ctx, MIR_ADD, i_reg.mir_op, i_reg.mir_op, MIR_new_int_op (ctx, 1));
    emit1 (c2m_ctx, MIR_JMP, MIR_new_label_op (ctx, loop_label));
    emit_label_insn_opt (c2m_ctx, break_label);
    gen_loop_body_scope_leave (c2m_ctx,
                               saved_loop_str_active, saved_loop_str_mark,
                               saved_loop_obj_active, saved_loop_obj_mark,
                               saved_loop_break_label);

    continue_label = saved_continue_label;
    break_label = saved_break_label;
    defer_break_mark = saved_defer_break_mark;
    defer_continue_mark = saved_defer_continue_mark;
    break;
  }
  case N_GOTO: {
    node_t target = r->attr;

    assert (false_label == NULL && true_label == NULL);
    emit_label (c2m_ctx, r);
    emit1 (c2m_ctx, MIR_JMP, MIR_new_label_op (ctx, get_label (c2m_ctx, target)));
    break;
  }
  case N_INDIRECT_GOTO: {
    node_t expr = NL_EL (r->u.ops, 1);

    assert (false_label == NULL && true_label == NULL);
    emit_label (c2m_ctx, r);
    val = val_gen (c2m_ctx, expr);
    emit1 (c2m_ctx, MIR_JMPI, val.mir_op);
    break;
  }
  case N_CONTINUE:
    assert (false_label == NULL && true_label == NULL);
    emit_label (c2m_ctx, r);
    gen_run_defers (c2m_ctx, defer_continue_mark); /* unwind loop-body defers */
    /* Reclaim this iteration's per-loop arena allocations before jumping to
       the loop header.  Safe (no-op) if no per-iter scope is active. */
    gen_loop_body_scope_release (c2m_ctx);
    emit1 (c2m_ctx, MIR_JMP, MIR_new_label_op (ctx, continue_label));
    break;
  case N_BREAK:
    assert (false_label == NULL && true_label == NULL);
    emit_label (c2m_ctx, r);
    gen_run_defers (c2m_ctx, defer_break_mark); /* unwind loop/switch-body defers */
    /* Per-iter release on break: the iteration that called break would
       otherwise leak its allocations until the function-level release fires
       at return.  Skip the release when `break` exits a switch but stays
       inside the loop owning the per-iter scope (current break_label is the
       switch's, not the loop's). */
    if (break_label == loop_break_label_for_scope)
      gen_loop_body_scope_release (c2m_ctx);
    emit1 (c2m_ctx, MIR_JMP, MIR_new_label_op (ctx, break_label));
    break;
  case N_RETURN: {
    decl_t func_decl = curr_func_def->attr;
    struct type *func_type = func_decl->decl_spec.type;
    struct type *ret_type = func_type->u.func_type->ret_type;
    int scalar_p = ret_type->mode != TM_STRUCT && ret_type->mode != TM_UNION && ret_type->mode != TM_CLASS;
    int ret_by_addr_p = target_return_by_addr_p (c2m_ctx, ret_type);

    assert (false_label == NULL && true_label == NULL);
    emit_label (c2m_ctx, r);
    if (NL_EL (r->u.ops, 1)->code == N_IGNORE) {
      gen_run_defers (c2m_ctx, 0); /* run all pending defers before returning */
      if (obj_scope_active) gen_obj_release_to (c2m_ctx, obj_scope_mark.mir_op);
      if (str_scope_active) gen_str_release_to (c2m_ctx, str_scope_mark.mir_op);
      emit_insn (c2m_ctx, MIR_new_ret_insn (ctx, 0));
      break;
    }
    if (ret_by_addr_p) {
      MIR_reg_t ret_addr_reg = MIR_reg (ctx, RET_ADDR_NAME, curr_func->u.func);

      var = new_op (NULL, MIR_new_mem_op (ctx, MIR_T_I8, 0, ret_addr_reg, 0, 1));
    }
    val = gen (c2m_ctx, NL_EL (r->u.ops, 1), NULL, NULL, !ret_by_addr_p && scalar_p,
               !ret_by_addr_p || scalar_p ? NULL : &var, NULL);
    if (!ret_by_addr_p && scalar_p)
      val = maybe_unwrap_dict_value (c2m_ctx, val, NL_EL (r->u.ops, 1), ret_type);
    if (!ret_by_addr_p && scalar_p) {
      t = get_mir_type (c2m_ctx, ret_type);
      t = promote_mir_int_type (t);
      val = promote (c2m_ctx, val, t, FALSE);
    }
    gen_run_defers (c2m_ctx, 0); /* run all pending defers after computing result */
    /* Reclaim this scope's Any<I> handles before returning.  A handle returned
       up the stack (e.g. `return any<I>(new C(...));`) is protected by detaching
       it from this scope's object arena first, handing ownership to the caller
       so the release below does not destroy it — mirrors gen_str_release_keeping
       for Strings.  detach is a safe no-op for pointers that were never tracked.
       NOTE: returning a *collection* of handles still frees the contained
       handles; that case needs explicit management. */
    if (obj_scope_active) {
      if (!ret_by_addr_p && scalar_p && ret_type->mode == TM_PTR)
        gen_obj_detach (c2m_ctx, val.mir_op);
      gen_obj_release_to (c2m_ctx, obj_scope_mark.mir_op);
    }
    /* Reclaim this scope's Strings before returning; protect a returned String
       so it survives into the caller's scope (no use-after-free). */
    if (str_scope_active) {
      if (builtin_string_type_p (ret_type))
        gen_str_release_keeping (c2m_ctx, str_scope_mark.mir_op, val.mir_op);
      else
        gen_str_release_to (c2m_ctx, str_scope_mark.mir_op);
    }
    VARR_TRUNC (MIR_op_t, ret_ops, 0);
    target_add_ret_ops (c2m_ctx, func_type->u.func_type->ret_type, val);
    emit_insn (c2m_ctx, MIR_new_insn_arr (ctx, MIR_RET, VARR_LENGTH (MIR_op_t, ret_ops),
                                          VARR_ADDR (MIR_op_t, ret_ops)));
    break;
  }
  case N_EXPR: {
    node_t e = NL_EL (r->u.ops, 1);

    assert (false_label == NULL && true_label == NULL);
    emit_label (c2m_ctx, r);
    if (e == stmtexpr_last_expr)
      /* MIR #452: last expr of statement expression — value context for post ++/--. */
      top_gen_last_op = gen (c2m_ctx, e, NULL, NULL, TRUE, NULL, NULL);
    else
      top_gen (c2m_ctx, e, NULL, NULL, NULL);
    break;
  }
  case N_DEFER: {
    /* Register the deferred statement; its code is emitted (LIFO) at scope exit
       by gen_run_defers from N_BLOCK / N_RETURN / N_BREAK / N_CONTINUE. */
    node_t defer_body = NL_EL (r->u.ops, 1);

    assert (false_label == NULL && true_label == NULL);
    /* -fexceptions: a trackable `defer delete <class-ptr>;` also goes onto the
       runtime shadow stack now (value captured here), so a throw's longjmp --
       which skips every syntactic exit gen_run_defers is emitted at -- can
       still run it.  No-op for any other defer body shape. */
    gen_defer_shadow_push (c2m_ctx, defer_body);
    VARR_PUSH (node_t, defer_stmts, defer_body);
    break;
  }
  case N_GO: {
    /* go f(args); → cy_spawn8((void *) f, nargs, a0, …, a7).  check() validated
       the shape (direct plain-function call, ≤8 GP-class args); the args are
       captured BY VALUE into the heap pack — the fiber never touches the
       spawner's stack. */
    node_t call = NL_EL (r->u.ops, 1);
    node_t func = NL_HEAD (call->u.ops);
    node_t args = NL_EL (call->u.ops, 1);
    struct expr *fe = func->attr;
    decl_t fd = fe->def_node->attr;
    MIR_op_t aops[10];
    size_t nargs = 0;

    assert (false_label == NULL && true_label == NULL);
    fiber_ensure_imports (c2m_ctx);
    aops[0] = MIR_new_ref_op (ctx, fd->u.item);
    for (node_t a = NL_HEAD (args->u.ops); a != NULL; a = NL_NEXT (a)) {
      op_t av = gen (c2m_ctx, a, NULL, NULL, TRUE, NULL, NULL);
      av = force_val (c2m_ctx, av, FALSE);
      if (get_op_type (c2m_ctx, av) != MIR_T_I64)
        av = promote (c2m_ctx, av, MIR_T_I64, FALSE);
      av = force_reg (c2m_ctx, av, MIR_T_I64);
      aops[2 + nargs++] = av.mir_op;
    }
    aops[1] = MIR_new_int_op (ctx, (mir_llong) nargs);
    for (size_t i = nargs; i < 8; i++) aops[2 + i] = MIR_new_int_op (ctx, 0);
    gen_rt_call_void (c2m_ctx, cy_spawn8_proto, cy_spawn8_item, 10, aops);
    break;
  }
  case N_AWAIT: {
    /* await [expr]; → evaluate the optional expression (result discarded),
       then cy_yield() — a pure cooperative yield / fiber state check. */
    node_t e = NL_EL (r->u.ops, 1);

    assert (false_label == NULL && true_label == NULL);
    if (e != NULL) top_gen (c2m_ctx, e, NULL, NULL, NULL);
    fiber_ensure_imports (c2m_ctx);
    gen_rt_call_void (c2m_ctx, cy_yield_proto, cy_yield_item, 0, NULL);
    break;
  }
  case N_DELETE: {
    /* delete <ptr>: run the destructor (if any) then free the heap object.
       delete <dict>: call dict_destroy(d) which handles arena-backed and
       plain dicts uniformly (frees arena in one shot or recurses). */
    node_t expr = NL_EL (r->u.ops, 1);
    node_t dtor_def = (node_t) r->attr; /* resolved in check, NULL if none */

    /* Non-pointer delete (dead code in a generic ownership branch): emit nothing.
       See the N_DELETE check phase for why this sentinel exists. */
    if (r->attr == (void *) (intptr_t) -1) break;

    assert (false_label == NULL && true_label == NULL);
    emit_label (c2m_ctx, r);
    /* Obtain both the lvalue (for null-out after free) and the pointer value. */
    op_t lval_for_null = gen (c2m_ctx, expr, NULL, NULL, FALSE, NULL, NULL);
    op1 = force_val (c2m_ctx, lval_for_null, FALSE);
    op1 = force_reg (c2m_ctx, op1, MIR_T_I64);

    /* ── dict: delegate entirely to dict_destroy ────────────────────── */
    {
      struct expr *del_e = expr->attr;
      if (del_e && del_e->type && del_e->type->mode == TM_DICT) {
        gen_dict_destroy (c2m_ctx, op1.mir_op);
        break;
      }
      /* Explicitly deleting an arena-managed Any<I> handle: detach it first so
         the scope-exit release does not free it a second time. */
      if (del_e != NULL && del_e->type != NULL && del_e->type->mode == TM_PTR) {
        const char *cn = class_type_name (del_e->type->u.ptr_type);
        if (cn != NULL && strncmp (cn, "__Any_", 6) == 0)
          gen_obj_detach (c2m_ctx, op1.mir_op);
      }
    }
    /* Null-safe delete: skip the destructor + free when the pointer is NULL.
       This makes `delete null;` a no-op and — crucially — makes the synthesized
       scope-exit delete of a `move`d-out owned binding (whose source `move`
       nulled) a harmless no-op, so single-owner cleanup never double-frees. */
    MIR_label_t del_skip = MIR_new_label (ctx);
    emit3 (c2m_ctx, MIR_BEQ, MIR_new_label_op (ctx, del_skip), op1.mir_op,
           MIR_new_int_op (ctx, 0));
    if (dtor_def != NULL) {
      decl_t cdecl = dtor_def->attr;
      struct func_type *ft = cdecl->decl_spec.type->u.func_type;
      MIR_item_t proto;
      char pname[64];
      size_t ops_start;

      collect_args_and_func_types (c2m_ctx, ft, NULL);
      sprintf (pname, "__dtorproto%d", new_proto_count++);
      proto = MIR_new_proto_arr (ctx, pname,
                                 VARR_LENGTH (MIR_type_t, proto_info.ret_types),
                                 VARR_ADDR (MIR_type_t, proto_info.ret_types),
                                 VARR_LENGTH (MIR_var_t, proto_info.arg_vars),
                                 VARR_ADDR (MIR_var_t, proto_info.arg_vars));
      move_item_to_module_start (curr_func->module, proto);
      ops_start = VARR_LENGTH (MIR_op_t, call_ops);
      VARR_PUSH (MIR_op_t, call_ops, MIR_new_ref_op (ctx, proto));
      VARR_PUSH (MIR_op_t, call_ops, MIR_new_ref_op (ctx, cdecl->u.item));
      VARR_PUSH (MIR_op_t, call_ops, op1.mir_op); /* implicit 'this' */
      emit_insn (c2m_ctx,
                 MIR_new_insn_arr (ctx, MIR_CALL,
                                   VARR_LENGTH (MIR_op_t, call_ops) - ops_start,
                                   VARR_ADDR (MIR_op_t, call_ops) + ops_start));
      VARR_TRUNC (MIR_op_t, call_ops, ops_start);
    }
    /* Value-semantic String fields (Option D): free the object's owned String
       members AFTER the user destructor ran (it may still read them) and
       BEFORE the heap is released.  No-op unless the deleted type is a class
       with String members. */
    int del_is_class_obj = FALSE;
    {
      struct expr *del_e2 = (struct expr *) expr->attr;
      struct type *cls = (del_e2 != NULL && del_e2->type != NULL
                          && del_e2->type->mode == TM_PTR)
                           ? del_e2->type->u.ptr_type : NULL;
      del_is_class_obj = (cls != NULL && cls->mode == TM_CLASS);
      gen_class_string_members_drop (c2m_ctx, op1.mir_op, cls);
    }
    /* -fobject-guards: for `new` class objects, mark the address dead and
       quarantine it (the runtime frees it later on ring eviction) instead of
       returning it to malloc immediately — this both catches double-free and
       keeps the address from being reused while still tracked (no false-positive
       UAF).  Non-class heap frees keep the normal path. */
    if (c2m_options->object_guards_p && del_is_class_obj)
      gen_obj_guard_note_free (c2m_ctx, op1.mir_op, (long) POS (r).lno);
    else
      gen_heap_free (c2m_ctx, op1.mir_op, (long) POS (r).lno);
    emit_label_insn_opt (c2m_ctx, del_skip); /* null pointers land here (no-op) */
    /* Use-after-free mitigation: null out the deleted pointer's lvalue so that
       any subsequent access through the same variable hits the null guard. */
    if (c2m_options->exceptions_p
        && (lval_for_null.mir_op.mode == MIR_OP_MEM
            || lval_for_null.mir_op.mode == MIR_OP_REG)) {
      MIR_op_t lval_mir = lval_for_null.mir_op;
      /* Adjust mem type to I64 for the pointer-sized store. */
      if (lval_mir.mode == MIR_OP_MEM) lval_mir.u.mem.type = MIR_T_I64;
      emit2 (c2m_ctx, MIR_MOV, lval_mir, zero_op.mir_op);
    }
    break;
  }
  case N_DETACH: {
    /* detach <expr>: evaluate the inner expression, remove the resulting
       pointer/String from the current scope's arena tracking set, and yield
       the same value.  The check pass stored a runtime selector on the expr
       struct's def_node slot:
           (node_t)1 — string detach   (c2m_str_detach)
           (node_t)2 — object detach   (c2m_obj_detach)
           NULL     — no runtime call (untracked or scalar value)
       For all three cases the result value equals the inner value (both
       runtime functions return their argument; the untracked case is a pure
       pass-through). */
    node_t inner = NL_HEAD (r->u.ops);
    struct expr *de = (struct expr *) r->attr;
    intptr_t kind = de != NULL ? (intptr_t) de->def_node : 0;
    MIR_op_t arg;

    op1 = gen (c2m_ctx, inner, NULL, NULL, TRUE, NULL, NULL);
    op1 = force_val (c2m_ctx, op1, FALSE);
    op1 = force_reg (c2m_ctx, op1, MIR_T_I64);
    arg = op1.mir_op;
    if (kind == 1) {
      /* c2m_str_detach(s) — returns the same pointer, now untracked. */
      res = gen_string_call (c2m_ctx, SM_DETACH, &arg, 1);
    } else if (kind == 2) {
      /* c2m_obj_detach(p) — result equals the argument; we ignore the call's
         return value and reuse op1 directly so callers see the same MIR op. */
      gen_obj_detach (c2m_ctx, arg);
      res = op1;
    } else {
      /* No-op: not arena-tracked.  Just yield the inner value. */
      res = op1;
    }
    break;
  }
  case N_ATTACH:
    /* attach <expr>;  — stub.  Generate the inner expression for side effects
       only (e.g. to surface evaluation errors at runtime), then drop the
       result.  No runtime tracking call is emitted yet; this is reserved for
       a future ownership-flow pass. */
    assert (false_label == NULL && true_label == NULL);
    emit_label (c2m_ctx, r);
    top_gen (c2m_ctx, NL_EL (r->u.ops, 1), NULL, NULL, NULL);
    break;
  case N_UNOWNED:
    /* unowned <decl>  — pure wrapper.  Recurse into the inner declaration list
       (an N_LIST of N_SPEC_DECL nodes); the opt-out marker is preserved in the
       AST for future passes but has no codegen effect today. */
    (void) gen (c2m_ctx, NL_HEAD (r->u.ops), true_label, false_label, FALSE, NULL, NULL);
    break;
  case N_OWNED:
    /* owned <decl>  — managed-ownership wrapper.  Recurse into the inner
       declaration list; the scope-exit release for an owned binding is wired
       through decl->auto_release_call by the ownership pass (same path as a
       synthesized `defer delete`), so there is no extra codegen here. */
    (void) gen (c2m_ctx, NL_HEAD (r->u.ops), true_label, false_label, FALSE, NULL, NULL);
    break;
  case N_MOVE: {
    /* move <expr>:
       Pointer form: yield the pointer, then NULL the source lvalue so a later
       delete of the source is a no-op (single-ownership transfer).
       Class-value form: block-copy the aggregate into a stack temp, then zero
       the source object so its RAII destructor frees nothing.
       Scalar / other: plain value pass-through (check already warns).  Do NOT
       zero the source — List/Map use `move` generically when relocating
       elements, and I64-null of an int slot would clobber the next element. */
    node_t inner = NL_HEAD (r->u.ops);
    node_t src = inner;
    struct expr *me = r->attr;
    while (src != NULL && src->code == N_CAST) src = NL_EL (src->u.ops, 1);

    if (me != NULL && me->type != NULL && me->type->mode == TM_CLASS) {
      mir_size_t csize = type_size (c2m_ctx, me->type);
      op_t src_lv = gen (c2m_ctx, inner, NULL, NULL, FALSE, NULL, NULL);
      /* Temporary aggregate holding the moved value. */
      mir_size_t arg_area_offset
        = curr_call_arg_area_offset
          + ((struct node_scope *) FUNC_DEF_BLOCK (curr_func_def)->attr)->size
          - ((struct node_scope *) FUNC_DEF_BLOCK (curr_func_def)->attr)->call_arg_area_size;
      op_t base = get_new_temp (c2m_ctx, MIR_T_I64);
      emit3 (c2m_ctx, MIR_ADD, base.mir_op,
             MIR_new_reg_op (ctx, MIR_reg (ctx, FP_NAME, curr_func->u.func)),
             MIR_new_int_op (ctx, arg_area_offset));
      update_call_arg_area_offset (c2m_ctx, me->type, FALSE);
      res = new_op (NULL, MIR_new_mem_op (ctx, MIR_T_UNDEF, 0, base.mir_op.u.reg, 0, 1));
      if (csize > 0) {
        block_move (c2m_ctx, res, src_lv, csize);
        /* Zero the source so ~T is a no-op (List: data=NULL, length=0). */
        if (src_lv.mir_op.mode == MIR_OP_MEM) {
          op_t saddr = mem_to_address (c2m_ctx, src_lv, TRUE);
          gen_memset (c2m_ctx, 0, saddr.mir_op.u.reg, csize);
        }
      }
      break;
    }

    /* Scalar / float / other non-class: `move` is a pure value pass-through
       (check already warned).  Do not force through I64 temps — that breaks
       MIR MOV for double/float (List of double etc.). */
    if (me == NULL || me->type == NULL || me->type->mode != TM_PTR) {
      res = gen (c2m_ctx, inner, NULL, NULL, TRUE, NULL, NULL);
      res = force_val (c2m_ctx, res, FALSE);
      break;
    }

    op_t mval = gen (c2m_ctx, inner, NULL, NULL, TRUE, NULL, NULL);
    mval = force_val (c2m_ctx, mval, FALSE);
    /* Pointer form: copy into a fresh temp first, then null the source. */
    res = get_new_temp (c2m_ctx, MIR_T_I64);
    emit2 (c2m_ctx, MIR_MOV, res.mir_op, mval.mir_op);
    if (src != NULL
        && (src->code == N_ID || src->code == N_DEREF_FIELD || src->code == N_FIELD
            || src->code == N_DEREF || src->code == N_IND)) {
      op_t lval = gen (c2m_ctx, src, NULL, NULL, FALSE, NULL, NULL);
      if (lval.mir_op.mode == MIR_OP_MEM || lval.mir_op.mode == MIR_OP_REG) {
        MIR_op_t lm = lval.mir_op;
        if (lm.mode == MIR_OP_MEM) lm.u.mem.type = MIR_T_I64;
        emit2 (c2m_ctx, MIR_MOV, lm, zero_op.mir_op);
      }
    }
    break;
  }
  case N_READONLY: {
    /* readonly <expr>: a non-owning borrow — a plain value pass-through.  The
       source keeps ownership; the view never releases anything. */
    res = gen (c2m_ctx, NL_HEAD (r->u.ops), NULL, NULL, TRUE, NULL, NULL);
    res = force_val (c2m_ctx, res, FALSE);
    break;
  }
  case N_CONCAT: {
    /* String `+` concatenation.  Each operand is lowered to a char* (string
       operands directly, basic arithmetic operands auto-cast via the
       c2m_str_from_* runtime), then joined with c2m_str_concat, which returns a
       fresh tracked String buffer.  Nested concatenations compose naturally
       because each N_CONCAT result is itself a String value. */
    node_t op1_node = NL_HEAD (r->u.ops);
    node_t op2_node = NL_EL (r->u.ops, 1);
    op_t s1 = gen_string_concat_operand (c2m_ctx, op1_node);
    op_t s2 = gen_string_concat_operand (c2m_ctx, op2_node);

    res = gen_str_concat_call (c2m_ctx, s1.mir_op, s2.mir_op);
    break;
  }

  case N_THROW: {
    /* throw(id_expr [, msg_expr]) -> cy_exc_throw(id, msg, file, line), which
       records the exception and longjmps to the innermost active try frame
       (never returning).  With -fno-exceptions the throw is ignored. */
    node_t id_node  = NL_EL (r->u.ops, 1);
    node_t msg_node = NL_EL (r->u.ops, 2);

    assert (false_label == NULL && true_label == NULL);
    emit_label (c2m_ctx, r);
    if (!c2m_options->exceptions_p) { res = zero_op; break; }

    op_t id_op = val_gen (c2m_ctx, id_node);
    id_op = force_reg (c2m_ctx, id_op, MIR_T_I64);    /* exception id -> i64 */

    /* Message operand: if present lower to a char* in a register, else null. */
    op_t msg_op;
    if (msg_node != NULL && msg_node->code != N_IGNORE) {
      msg_op = gen (c2m_ctx, msg_node, NULL, NULL, TRUE, NULL, NULL);
      msg_op = force_reg_or_mem (c2m_ctx, msg_op, MIR_T_I64);
    } else {
      msg_op = zero_op;  /* NULL message */
    }

    gen_exception_throw_call (c2m_ctx, id_op.mir_op, msg_op.mir_op);
    res = zero_op;
    break;
  }

  case N_TRY: {
    /* try <body-block> ( catch(Class? var) <handler-block> )+

       setjmp-frame lowering (exceptions enabled):
         buf = cy_exc_push();
         if (setjmp(buf) != 0) goto dispatch;     // exception was thrown
         <body>; cy_exc_pop(); goto after;        // normal completion
       dispatch:
         cy_exc_pop();                             // leave this frame
         tid = cy_exc_current()->id;
         per clause: if (tid == clause_id) goto handler_k;  (catch-all: goto)
         <no match>: re-throw to the enclosing frame
       handler_k:
         memcpy(clause `Exception var`, *cy_exc_current());
         <handler-block>; goto after;
       after:

       With -fno-exceptions: emit the body block only; catch clauses are dead.

       AST: N_TRY(labels, body_block, N_LIST:(N_CATCH)+)
            N_CATCH(class_sel|N_IGNORE, var_id, handler_block) */
    node_t body       = NL_EL (r->u.ops, 1);   /* try-body N_BLOCK  */
    node_t catch_list = NL_EL (r->u.ops, 2);   /* N_LIST of N_CATCH */

    assert (false_label == NULL && true_label == NULL);
    emit_label (c2m_ctx, r);

    if (!c2m_options->exceptions_p) {
      gen (c2m_ctx, body, NULL, NULL, FALSE, NULL, NULL);
      res = zero_op;
      break;
    }

    exception_ensure_imports (c2m_ctx);

    {
#define CY_MAX_CATCH 64
      MIR_label_t hlabels[CY_MAX_CATCH];
      MIR_label_t dispatch_label = MIR_new_label (ctx);
      MIR_label_t after_try      = MIR_new_label (ctx);
      int n_clauses = 0, k, active, saw_catchall = 0;
      node_t cat;

      for (cat = NL_HEAD (catch_list->u.ops); cat != NULL; cat = NL_NEXT (cat))
        if (n_clauses < CY_MAX_CATCH) hlabels[n_clauses++] = MIR_new_label (ctx);

      /* buf = cy_exc_push(); r = setjmp(buf); if (r != 0) goto dispatch */
      op_t buf = gen_rt_call (c2m_ctx, cy_exc_push_proto, cy_exc_push_item, 0, NULL);
      buf = force_reg (c2m_ctx, buf, MIR_T_I64);
      op_t sj = gen_rt_call (c2m_ctx, cy_setjmp_proto, cy_setjmp_item, 1, &buf.mir_op);
      emit2 (c2m_ctx, MIR_BT, MIR_new_label_op (ctx, dispatch_label), sj.mir_op);

      /* Register a frame-pop marker on the defer stack so a return/break/
         continue leaving the body pops this exception frame (see
         CY_EXC_POP_MARKER).  It unwinds LIFO with any real defers declared
         inside the try body. */
      size_t try_defer_mark = VARR_LENGTH (node_t, defer_stmts);
      VARR_PUSH (node_t, defer_stmts, CY_EXC_POP_MARKER);

      /* SHORTCOMINGS.md gotcha #10 fix (String/object arena half): the
         String and object (Any<I>) arenas are global thread-local
         high-water-mark stacks (checkpoint() reads the count, release_to(m)
         truncates back to it) -- unlike defer_stmts, they don't care which
         function pushed an entry, so releasing back to a mark taken here
         correctly reclaims everything allocated since this try started, no
         matter how many call frames deep the throw happened in.

         The marks are banked into cy_exc's own frame-stack via
         cy_exc_set_marks() (called here, before the try body can throw)
         rather than kept in a local temp across the try body: a value merely
         held in a MIR-generated register/temp between here and the
         exception-dispatch label is NOT reliably preserved across a
         setjmp/longjmp span -- MIR-gen has no model of longjmp as an
         implicit second entry point into this code, so it can (and does)
         place such a value in a register that the try body's own codegen
         then clobbers. Banking into cy_exc's plain-C-runtime frame array and
         re-reading fresh after the jump (cy_exc_current_str_mark /
         cy_exc_current_obj_mark, called on the dispatch path below, before
         cy_exc_pop) sidesteps that hazard entirely. */
      {
        op_t str_mark = gen_str_checkpoint (c2m_ctx);
        op_t obj_mark = gen_obj_checkpoint (c2m_ctx);
        op_t defer_mark
          = gen_rt_call (c2m_ctx, cy_defer_checkpoint_proto, cy_defer_checkpoint_item, 0, NULL);
        MIR_op_t mark_args[3];
        mark_args[0] = str_mark.mir_op;
        mark_args[1] = obj_mark.mir_op;
        mark_args[2] = defer_mark.mir_op;
        gen_rt_call_void (c2m_ctx, cy_exc_set_marks_proto, cy_exc_set_marks_item, 3, mark_args);
      }

      /* --- normal path: run protected body, unwind frame, done --- */
      gen (c2m_ctx, body, NULL, NULL, FALSE, NULL, NULL);
      {
        /* Only pop + jump on fall-through.  If the body ended in a terminator
           (return/break/continue already ran the marker and jumped), emitting
           another pop would be dead code and a double-pop. */
        MIR_insn_t last = DLIST_TAIL (MIR_insn_t, curr_func->u.func->insns);
        if (last == NULL
            || (last->code != MIR_RET && last->code != MIR_JRET && last->code != MIR_JMP)) {
          gen_run_defers (c2m_ctx, try_defer_mark); /* emits cy_exc_pop via marker */
          emit1 (c2m_ctx, MIR_JMP, MIR_new_label_op (ctx, after_try));
        }
      }
      /* Remove the marker from the compile-time defer stack; the exception
         (dispatch) path below pops the frame explicitly at runtime. */
      VARR_TRUNC (node_t, defer_stmts, try_defer_mark);

      /* --- exception path: read back the banked marks (must happen BEFORE
         cy_exc_pop(), which decrements the depth these are indexed by), pop
         our frame, reclaim Strings/Any<I> handles allocated since try-entry
         (anywhere on the call chain -- see the comment above), then dispatch
         on the thrown id --- */
      emit_label_insn_opt (c2m_ctx, dispatch_label);
      op_t dispatch_str_mark
        = gen_rt_call (c2m_ctx, cy_exc_current_str_mark_proto, cy_exc_current_str_mark_item, 0, NULL);
      op_t dispatch_obj_mark
        = gen_rt_call (c2m_ctx, cy_exc_current_obj_mark_proto, cy_exc_current_obj_mark_item, 0, NULL);
      op_t dispatch_defer_mark
        = gen_rt_call (c2m_ctx, cy_exc_current_defer_mark_proto, cy_exc_current_defer_mark_item, 0,
                        NULL);
      gen_rt_call_void (c2m_ctx, cy_exc_pop_proto, cy_exc_pop_item, 0, NULL);
      /* Run pending defer/owned/RAII cleanup thunks registered since try-entry
         (anywhere on the call chain) BEFORE releasing the String/object
         arenas: a deferred cleanup may itself touch a String or Any<I>
         handle that's about to be reclaimed below. */
      gen_rt_call_void (c2m_ctx, cy_defer_release_to_proto, cy_defer_release_to_item, 1,
                        &dispatch_defer_mark.mir_op);
      gen_str_release_to (c2m_ctx, dispatch_str_mark.mir_op);
      gen_obj_release_to (c2m_ctx, dispatch_obj_mark.mir_op);
      op_t excp = gen_rt_call (c2m_ctx, cy_exc_current_proto, cy_exc_current_item, 0, NULL);
      excp = force_reg (c2m_ctx, excp, MIR_T_I64);
      op_t tid = get_new_temp (c2m_ctx, MIR_T_I64);
      emit2 (c2m_ctx, MIR_MOV, tid.mir_op,
             MIR_new_mem_op (ctx, MIR_T_U32, 0, excp.mir_op.u.reg, 0, 1));

      active = 0;
      for (cat = NL_HEAD (catch_list->u.ops), k = 0;
           cat != NULL && k < CY_MAX_CATCH; cat = NL_NEXT (cat), k++) {
        node_t class_sel = NL_EL (cat->u.ops, 0);
        int catchall = (class_sel->code != N_ID || strcmp (class_sel->u.s.s, "Exception") == 0);
        active = k + 1;
        if (catchall) {
          emit1 (c2m_ctx, MIR_JMP, MIR_new_label_op (ctx, hlabels[k]));
          saw_catchall = 1;
          break; /* subsequent clauses are unreachable */
        } else {
          struct expr *ce = class_sel->attr;
          mir_llong cid = (ce != NULL && ce->const_p) ? ce->c.i_val : 0;
          emit3 (c2m_ctx, MIR_BEQ, MIR_new_label_op (ctx, hlabels[k]),
                 tid.mir_op, MIR_new_int_op (ctx, cid));
        }
      }

      /* No clause matched: re-throw the current exception to the outer frame. */
      if (!saw_catchall) {
        op_t e2 = gen_rt_call (c2m_ctx, cy_exc_current_proto, cy_exc_current_item, 0, NULL);
        e2 = force_reg (c2m_ctx, e2, MIR_T_I64);
        op_t rid = get_new_temp (c2m_ctx, MIR_T_I64);
        op_t rmsg = get_new_temp (c2m_ctx, MIR_T_I64);
        emit2 (c2m_ctx, MIR_MOV, rid.mir_op,
               MIR_new_mem_op (ctx, MIR_T_U32, 0, e2.mir_op.u.reg, 0, 1));
        emit2 (c2m_ctx, MIR_MOV, rmsg.mir_op,
               MIR_new_mem_op (ctx, MIR_T_I64, 8, e2.mir_op.u.reg, 0, 1));
        gen_exception_throw_call (c2m_ctx, rid.mir_op, rmsg.mir_op);
      }

      /* --- handler bodies --- */
      for (cat = NL_HEAD (catch_list->u.ops), k = 0;
           cat != NULL && k < active; cat = NL_NEXT (cat), k++) {
        node_t handler = NL_EL (cat->u.ops, 2);   /* N_BLOCK */
        emit_label_insn_opt (c2m_ctx, hlabels[k]);

        /* Populate this clause's `Exception var` from *cy_exc_current().  The
           synthesized declaration is the first item of the handler block. */
        node_t blist = NL_EL (handler->u.ops, 1);
        node_t decl_node = (blist == NULL) ? NULL : NL_HEAD (blist->u.ops);
        decl_t evar = (decl_node != NULL && decl_node->code == N_SPEC_DECL)
                        ? (decl_t) decl_node->attr : NULL;
        if (evar != NULL && !evar->reg_p) {
          op_t src = gen_rt_call (c2m_ctx, cy_exc_current_proto, cy_exc_current_item, 0, NULL);
          src = force_reg (c2m_ctx, src, MIR_T_I64);
          op_t src_mem = new_op (NULL, MIR_new_mem_op (ctx, MIR_T_I8, 0, src.mir_op.u.reg, 0, 1));
          gen_memcpy (c2m_ctx, (MIR_disp_t) evar->offset,
                      MIR_reg (ctx, FP_NAME, curr_func->u.func),
                      src_mem, type_size (c2m_ctx, evar->decl_spec.type));
        }
        gen (c2m_ctx, handler, NULL, NULL, FALSE, NULL, NULL);
        emit1 (c2m_ctx, MIR_JMP, MIR_new_label_op (ctx, after_try));
      }

      emit_label_insn_opt (c2m_ctx, after_try);
      res = zero_op;
      break;
#undef CY_MAX_CATCH
    }
  }

  default: abort ();
  }
finish:
  if (true_label != NULL) {
    MIR_op_t lab_op = MIR_new_label_op (ctx, true_label);

    type = ((struct expr *) r->attr)->type;
    if (!floating_type_p (type)) {
      res = promote (c2m_ctx, force_val (c2m_ctx, res, type->arr_type != NULL), MIR_T_I64, FALSE);
      emit2 (c2m_ctx, MIR_BT, lab_op, res.mir_op);
    } else if (type->u.basic_type == TP_FLOAT) {
      emit3 (c2m_ctx, MIR_FBNE, lab_op, res.mir_op, MIR_new_float_op (ctx, 0.0));
    } else if (type->u.basic_type == TP_DOUBLE) {
      emit3 (c2m_ctx, MIR_DBNE, lab_op, res.mir_op, MIR_new_double_op (ctx, 0.0));
    } else {
      assert (type->u.basic_type == TP_LDOUBLE);
      emit3 (c2m_ctx, MIR_LDBNE, lab_op, res.mir_op, MIR_new_ldouble_op (ctx, 0.0));
    }
    emit1 (c2m_ctx, MIR_JMP, MIR_new_label_op (ctx, false_label));
  } else if (val_p) {
    struct type *vt = ((struct expr *) r->attr)->type;
    /* `_Atomic` rvalue from *ptr / field without a named decl: ALOAD. */
    if (res.mir_op.mode == MIR_OP_MEM && type_atomic_p (vt) && integer_type_p (vt)
        && !op_decl_atomic_p (res)) {
      MIR_type_t at = get_mir_type (c2m_ctx, vt);
      res = atomic_load_mem (c2m_ctx, res, at);
    } else {
      res = force_val (c2m_ctx, res, vt->arr_type != NULL);
    }
  }
  /* Midopt R-LICM: memoize this loop-invariant call's finalized value so the
     loop-bottom condition emission reuses it instead of re-calling. */
  if (r->code == N_CALL && true_label == NULL && false_label == NULL) {
    struct expr *ce = r->attr;
    if (ce != NULL && ce->hoist_call_p) gen_hoist_store (c2m_ctx, r, res);
  }
  if (stmt_p) curr_call_arg_area_offset = 0;
  return res;
}

static htab_hash_t proto_hash (MIR_item_t pi, void *arg MIR_UNUSED) {
  MIR_proto_t p = pi->u.proto;
  MIR_var_t *args = VARR_ADDR (MIR_var_t, p->args);
  uint64_t h = mir_hash_init (42);

  h = mir_hash_step (h, p->nres);
  h = mir_hash_step (h, p->vararg_p);
  for (uint32_t i = 0; i < p->nres; i++) h = mir_hash_step (h, p->res_types[i]);
  for (size_t i = 0; i < VARR_LENGTH (MIR_var_t, p->args); i++) {
    h = mir_hash_step (h, args[i].type);
    h = mir_hash_step (h, mir_hash (args[i].name, strlen (args[i].name), 24));
    if (MIR_all_blk_type_p (args[i].type)) h = mir_hash_step (h, args[i].size);
  }
  return (htab_hash_t) mir_hash_finish (h);
}

static int proto_eq (MIR_item_t pi1, MIR_item_t pi2, void *arg MIR_UNUSED) {
  MIR_proto_t p1 = pi1->u.proto, p2 = pi2->u.proto;

  if (p1->nres != p2->nres || p1->vararg_p != p2->vararg_p
      || VARR_LENGTH (MIR_var_t, p1->args) != VARR_LENGTH (MIR_var_t, p2->args))
    return FALSE;
  for (uint32_t i = 0; i < p1->nres; i++)
    if (p1->res_types[i] != p2->res_types[i]) return FALSE;

  MIR_var_t *args1 = VARR_ADDR (MIR_var_t, p1->args), *args2 = VARR_ADDR (MIR_var_t, p2->args);

  for (size_t i = 0; i < VARR_LENGTH (MIR_var_t, p1->args); i++)
    if (args1[i].type != args2[i].type || strcmp (args1[i].name, args2[i].name) != 0
        || (MIR_all_blk_type_p (args1[i].type) && args1[i].size != args2[i].size))
      return FALSE;
  return TRUE;
}

static MIR_item_t get_mir_proto (c2m_ctx_t c2m_ctx, int vararg_p) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  struct MIR_item pi, *el;
  struct MIR_proto p;
  char buff[30];

  pi.u.proto = &p;
  p.vararg_p = vararg_p;
  p.nres = (uint32_t) VARR_LENGTH (MIR_type_t, proto_info.ret_types);
  p.res_types = VARR_ADDR (MIR_type_t, proto_info.ret_types);
  p.args = proto_info.arg_vars;
  if (HTAB_DO (MIR_item_t, proto_tab, &pi, HTAB_FIND, el)) return el;
  sprintf (buff, "proto%d", curr_mir_proto_num++);
  el = (vararg_p ? MIR_new_vararg_proto_arr
                 : MIR_new_proto_arr) (c2m_ctx->ctx, buff, p.nres, p.res_types,
                                       VARR_LENGTH (MIR_var_t, proto_info.arg_vars),
                                       VARR_ADDR (MIR_var_t, proto_info.arg_vars));
  HTAB_DO (MIR_item_t, proto_tab, el, HTAB_INSERT, el);
  return el;
}

static void gen_mir_protos (c2m_ctx_t c2m_ctx) {
  MIR_alloc_t alloc = c2m_alloc (c2m_ctx);
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  node_t call, func, op1, arg_list, first_arg;
  struct type *type;
  struct func_type *func_type;

  debug(c2m_ctx, no_pos, "gen_mir_protos: generating protos for call_nodes\n");
  curr_mir_proto_num = 0;
  HTAB_CREATE (MIR_item_t, proto_tab, alloc, 512, proto_hash, proto_eq, NULL);
  for (size_t i = 0; i < VARR_LENGTH (node_t, call_nodes); i++) {
    call = VARR_GET (node_t, call_nodes, i);
    /* Call nodes can be rewritten in place (e.g. capturing HOF → N_STMTEXPR). */
    if (call->code != N_CALL) continue;
    op1 = NL_HEAD (call->u.ops);
    arg_list = NL_NEXT (op1);
    if (op1->code == N_ID && strcmp (op1->u.s.s, JCALL) == 0) {
      func = NL_HEAD (arg_list->u.ops);
      first_arg = NL_EL (arg_list->u.ops, 1);
    } else {
      func = op1;
      first_arg = (arg_list != NULL) ? NL_HEAD (arg_list->u.ops) : NULL;
    }
    /* Built-in String method calls (s.length(), s.substr(), ...) are lowered to
       runtime helper calls in gen and have no user-level C prototype. */
    if (func->code == N_FIELD || func->code == N_DEREF_FIELD) {
      node_t sobj = NL_HEAD (func->u.ops);
      struct expr *sobj_e = sobj->attr;
      node_t smethod = NL_NEXT (sobj);
      /* Skip static String calls (String.copy where sobj is the bare N_STRING
         keyword), instance String method calls (s.length() etc.), and methods
         called directly on a UTF-8 string literal ("abc".lower(), sobj is N_STR). */
      if (smethod != NULL && smethod->code == N_ID
          && get_string_method (smethod->u.s.s, NULL, NULL) != SM_NONE
          && (sobj->code == N_STRING || sobj->code == N_STR
              || (sobj_e != NULL && builtin_string_type_p (sobj_e->type))))
        continue;
      /* Sequence lambda methods (arr.filter/map/reduce/count) are lowered to an
         inline MIR loop in gen; the check phase marks them with a void-typed
         field placeholder.  They have no user-level C prototype either. */
      if (smethod != NULL && smethod->code == N_ID
          && get_seq_method (smethod->u.s.s, NULL) != SEQM_NONE) {
        struct expr *sfe = func->attr;
        if (sfe != NULL && sfe->type != NULL && sfe->type->mode == TM_BASIC
            && sfe->type->u.basic_type == TP_VOID)
          continue;
      }
      /* Built-in List<String>::join(delim) is also lowered to a runtime call
         (c2m_str_join) in gen, marked by the same void-typed field placeholder
         and carrying no user-level C prototype. */
      if (smethod != NULL && smethod->code == N_ID
          && get_string_method (smethod->u.s.s, NULL, NULL) == SM_JOIN) {
        struct expr *sfe = func->attr;
        if (sfe != NULL && sfe->type != NULL && sfe->type->mode == TM_BASIC
            && sfe->type->u.basic_type == TP_VOID)
          continue;
      }
      /* Built-in dict methods (d.length/count/type/json) are lowered in gen
         and carry no user-level C prototype. */
      if (smethod != NULL && smethod->code == N_ID && sobj_e != NULL
          && sobj_e->type != NULL && sobj_e->type->mode == TM_DICT
          && (strcmp (smethod->u.s.s, "length") == 0
              || strcmp (smethod->u.s.s, "count") == 0
              || strcmp (smethod->u.s.s, "type") == 0
              || strcmp (smethod->u.s.s, "json") == 0))
        continue;
    }
    type = ((struct expr *) func->attr)->type;
    assert (type->mode == TM_PTR && type->u.ptr_type->mode == TM_FUNC);
    set_type_layout (c2m_ctx, type);
    func_type = type->u.ptr_type->u.func_type;
    assert (func_type->param_list->code == N_LIST);
    collect_args_and_func_types (c2m_ctx, func_type, first_arg);
    if (!func_type->dots_p && NL_HEAD (func_type->param_list->u.ops) == NULL) {
      /* Unprototyped: per-call-site VARARG proto with fixed actual args. */
      ((struct expr *) call->attr)->call_proto_item = get_mir_proto (c2m_ctx, TRUE);
    } else {
      func_type->proto_item = get_mir_proto (c2m_ctx, func_type->dots_p);
    }
  }
  HTAB_DESTROY (MIR_item_t, proto_tab);
}

static void gen_finish (c2m_ctx_t c2m_ctx) {
  gen_ctx_t gen_ctx;

  if (c2m_ctx == NULL || (gen_ctx = c2m_ctx->gen_ctx) == NULL) return;
  finish_reg_vars (c2m_ctx);
  if (proto_info.arg_vars != NULL) VARR_DESTROY (MIR_var_t, proto_info.arg_vars);
  if (proto_info.ret_types != NULL) VARR_DESTROY (MIR_type_t, proto_info.ret_types);
  if (call_ops != NULL) VARR_DESTROY (MIR_op_t, call_ops);
  if (ret_ops != NULL) VARR_DESTROY (MIR_op_t, ret_ops);
  if (switch_ops != NULL) VARR_DESTROY (MIR_op_t, switch_ops);
  if (switch_cases != NULL) VARR_DESTROY (case_t, switch_cases);
  if (init_els != NULL) VARR_DESTROY (init_el_t, init_els);
  if (node_stack != NULL) VARR_DESTROY (node_t, node_stack);
  if (defer_stmts != NULL) VARR_DESTROY (node_t, defer_stmts);
  if (union_alias_done != NULL) VARR_DESTROY (MIR_alias_t, union_alias_done);
  if(c2m_ctx->gen_ctx) {
      reg_free (c2m_ctx, c2m_ctx->gen_ctx);
  }
}

/* ---- Debug info emission (-g): type + variable tables ---- */
#if !MIR_NO_DBINFO

static MIR_dbtype_id_t dbinfo_lower_type (c2m_ctx_t c2m_ctx, MIR_module_t mod, struct type *type);
static MIR_dbencoding_t basic_type_encoding (enum basic_type bt) {
  switch (bt) {
  case TP_BOOL: return MIR_DBENC_BOOLEAN;
  case TP_CHAR: return char_is_signed_p () ? MIR_DBENC_SIGNED_CHAR : MIR_DBENC_UNSIGNED_CHAR;
  case TP_SCHAR: return MIR_DBENC_SIGNED_CHAR;
  case TP_UCHAR: return MIR_DBENC_UNSIGNED_CHAR;
  case TP_SHORT: case TP_INT: case TP_LONG: case TP_LLONG: return MIR_DBENC_SIGNED;
  case TP_USHORT: case TP_UINT: case TP_ULONG: case TP_ULLONG: return MIR_DBENC_UNSIGNED;
  case TP_FLOAT: case TP_DOUBLE: case TP_LDOUBLE: return MIR_DBENC_FLOAT;
  case TP_STRING: return MIR_DBENC_UTF;
  default: return MIR_DBENC_NONE;
  }
}

static const char *basic_type_dbname (enum basic_type bt) {
  switch (bt) {
  case TP_VOID: return "void";
  case TP_BOOL: return "_Bool";
  case TP_CHAR: return "char";
  case TP_SCHAR: return "signed char";
  case TP_UCHAR: return "unsigned char";
  case TP_SHORT: return "short";
  case TP_USHORT: return "unsigned short";
  case TP_INT: return "int";
  case TP_UINT: return "unsigned int";
  case TP_LONG: return "long";
  case TP_ULONG: return "unsigned long";
  case TP_LLONG: return "long long";
  case TP_ULLONG: return "unsigned long long";
  case TP_FLOAT: return "float";
  case TP_DOUBLE: return "double";
  case TP_LDOUBLE: return "long double";
  case TP_STRING: return "String";
  default: return "?";
  }
}

/* Simple recursion depth guard to break cycles (self-referential structs). */
static int dbinfo_depth = 0;
#define DBINFO_MAX_DEPTH 32

/* ---- Debug-type cache (deduplication) ----
   Without caching, every variable declaration re-emits the full type tree,
   leading to millions of duplicate entries for common types like char/int.
   Cache key = (struct type *, qualifier bits).
   16K slots (256 KB BSS) handles even very large single-file compilers
   like classyc.c (~700 struct references × qualifier/pointer variants). */
#define DBTYPE_CACHE_BITS 14
#define DBTYPE_CACHE_SIZE (1 << DBTYPE_CACHE_BITS)
#define DBTYPE_CACHE_MASK (DBTYPE_CACHE_SIZE - 1)

typedef struct {
  struct type *type;      /* NULL = empty slot */
  uint8_t quals;
  MIR_dbtype_id_t id;
} dbtype_cache_entry_t;

static dbtype_cache_entry_t dbtype_cache[DBTYPE_CACHE_SIZE];
static MIR_module_t dbtype_cache_module = NULL;

static void dbtype_cache_reset (MIR_module_t mod) {
  if (dbtype_cache_module != mod) {
    memset (dbtype_cache, 0, sizeof (dbtype_cache));
    dbtype_cache_module = mod;
  }
}

static inline uint8_t dbtype_qual_bits (struct type *t) {
  return (uint8_t) ((t->type_qual.const_p ? 1 : 0) | (t->type_qual.volatile_p ? 2 : 0)
                    | (t->type_qual.restrict_p ? 4 : 0) | (t->type_qual.atomic_p ? 8 : 0));
}

static inline uint32_t dbtype_cache_hash (struct type *t, uint8_t quals) {
  uintptr_t p = (uintptr_t) t;
  return (uint32_t) ((p >> 3) ^ (p >> 17) ^ quals) & DBTYPE_CACHE_MASK;
}

static MIR_dbtype_id_t dbtype_cache_lookup (struct type *t, uint8_t quals, int *found) {
  uint32_t h = dbtype_cache_hash (t, quals);
  for (int i = 0; i < 32; i++) {
    uint32_t idx = (h + i) & DBTYPE_CACHE_MASK;
    if (dbtype_cache[idx].type == NULL) { *found = 0; return 0; }
    if (dbtype_cache[idx].type == t && dbtype_cache[idx].quals == quals) {
      *found = 1;
      return dbtype_cache[idx].id;
    }
  }
  *found = 0;
  return 0;
}

static void dbtype_cache_insert (struct type *t, uint8_t quals, MIR_dbtype_id_t id) {
  uint32_t h = dbtype_cache_hash (t, quals);
  for (int i = 0; i < 32; i++) {
    uint32_t idx = (h + i) & DBTYPE_CACHE_MASK;
    if (dbtype_cache[idx].type == NULL
        || (dbtype_cache[idx].type == t && dbtype_cache[idx].quals == quals)) {
      dbtype_cache[idx].type = t;
      dbtype_cache[idx].quals = quals;
      dbtype_cache[idx].id = id;
      return;
    }
  }
  /* Too many collisions — skip caching this entry */
}

static MIR_dbtype_id_t dbinfo_lower_type_impl (c2m_ctx_t c2m_ctx, MIR_module_t mod, struct type *type);

static MIR_dbtype_id_t dbinfo_lower_type (c2m_ctx_t c2m_ctx, MIR_module_t mod, struct type *type) {
  if (type == NULL || dbinfo_depth >= DBINFO_MAX_DEPTH) return 0;

  dbtype_cache_reset (mod);
  uint8_t quals = dbtype_qual_bits (type);
  int found;
  MIR_dbtype_id_t cached = dbtype_cache_lookup (type, quals, &found);
  if (found) return cached;

  dbinfo_depth++;
  MIR_dbtype_id_t result = dbinfo_lower_type_impl (c2m_ctx, mod, type);
  dbinfo_depth--;

  dbtype_cache_insert (type, quals, result);
  return result;
}

static MIR_dbtype_id_t dbinfo_lower_type_impl (c2m_ctx_t c2m_ctx, MIR_module_t mod, struct type *type) {
  MIR_context_t ctx = c2m_ctx->ctx;
  MIR_dbtype_t dt;
  memset (&dt, 0, sizeof (dt));

  /* Apply qualifiers as wrappers */
  if (type->type_qual.const_p) {
    struct type_qual saved = type->type_qual;
    type->type_qual.const_p = 0;
    MIR_dbtype_id_t inner = dbinfo_lower_type (c2m_ctx, mod, type);
    type->type_qual = saved;
    dt.kind = MIR_DBT_CONST;
    dt.name = NULL;
    dt.byte_size = type->raw_size != MIR_SIZE_MAX ? (uint32_t) type->raw_size : 0;
    dt.u.ref.target_id = inner;
    return MIR_dbinfo_add_type (ctx, mod, &dt);
  }
  if (type->type_qual.volatile_p) {
    struct type_qual saved = type->type_qual;
    type->type_qual.volatile_p = 0;
    MIR_dbtype_id_t inner = dbinfo_lower_type (c2m_ctx, mod, type);
    type->type_qual = saved;
    dt.kind = MIR_DBT_VOLATILE;
    dt.name = NULL;
    dt.byte_size = type->raw_size != MIR_SIZE_MAX ? (uint32_t) type->raw_size : 0;
    dt.u.ref.target_id = inner;
    return MIR_dbinfo_add_type (ctx, mod, &dt);
  }

  switch (type->mode) {
  case TM_BASIC: {
    enum basic_type bt = type->u.basic_type;
    const char *nm;
    uint32_t sz, al;
    MIR_dbencoding_t enc;
    if (bt == TP_VOID) return 0;
    nm = basic_type_dbname (bt);
    sz = (bt == TP_STRING) ? (uint32_t) sizeof (void *) : (uint32_t) basic_type_size (bt);
    al = (bt == TP_STRING) ? (uint32_t) sizeof (void *) : (uint32_t) basic_type_align (bt);
    enc = basic_type_encoding (bt);
    /* Pointer-identity cache misses for the many fresh `struct type`s
       create_type() allocates for `int` / `String`.  Reuse an existing
       DIE with the same name/size/encoding so gdb is not flooded. */
    if (mod->dbtypes != NULL) {
      uint32_t i;
      for (i = 1; i < mod->dbtypes->num_types; i++) {
        MIR_dbtype_t *t = &mod->dbtypes->types[i];
        if (t->kind == MIR_DBT_BASE && t->u.base.encoding == enc && t->byte_size == sz
            && t->name != NULL && strcmp (t->name, nm) == 0)
          return t->id;
      }
    }
    dt.kind = MIR_DBT_BASE;
    dt.name = nm;
    dt.byte_size = sz;
    dt.align = al;
    dt.u.base.encoding = enc;
    return MIR_dbinfo_add_type (ctx, mod, &dt);
  }
  case TM_PTR: {
    MIR_dbtype_id_t target = dbinfo_lower_type (c2m_ctx, mod, type->u.ptr_type);
    dt.kind = MIR_DBT_PTR;
    dt.name = NULL;
    dt.byte_size = sizeof (void *);
    dt.align = sizeof (void *);
    dt.u.ref.target_id = target;
    return MIR_dbinfo_add_type (ctx, mod, &dt);
  }
  case TM_ARR: {
    struct arr_type *at = type->u.arr_type;
    MIR_dbtype_id_t el = dbinfo_lower_type (c2m_ctx, mod, at->el_type);
    dt.kind = MIR_DBT_ARRAY;
    dt.name = NULL;
    dt.byte_size = type->raw_size != MIR_SIZE_MAX ? (uint32_t) type->raw_size : 0;
    dt.align = type->align >= 0 ? (uint32_t) type->align : 0;
    dt.u.array.element_id = el;
    /* Compute element count from sizes */
    if (at->el_type->raw_size != MIR_SIZE_MAX && at->el_type->raw_size > 0
        && type->raw_size != MIR_SIZE_MAX)
      dt.u.array.count = (int64_t) (type->raw_size / at->el_type->raw_size);
    else
      dt.u.array.count = -1;
    return MIR_dbinfo_add_type (ctx, mod, &dt);
  }
  case TM_STRUCT:
  case TM_UNION:
  case TM_CLASS: {
    node_t tag = type->u.tag_type;
    node_t tag_id = TAG_ID (tag);
    const char *tname = (tag_id != NULL && tag_id->code == N_ID) ? tag_id->u.s.s : NULL;
    node_t member_list = TAG_MEMBER_LIST (tag);
    dt.kind = (type->mode == TM_UNION) ? MIR_DBT_UNION : MIR_DBT_STRUCT;
    dt.name = tname;
    dt.byte_size = type->raw_size != MIR_SIZE_MAX ? (uint32_t) type->raw_size : 0;
    dt.align = type->align >= 0 ? (uint32_t) type->align : 0;
    /* Named complete aggregates: reuse the first DIE with the same
       name/kind/size so Request/Response do not appear N times. */
    if (tname != NULL && dt.byte_size != 0 && mod->dbtypes != NULL) {
      uint32_t i;
      for (i = 1; i < mod->dbtypes->num_types; i++) {
        MIR_dbtype_t *t = &mod->dbtypes->types[i];
        if (t->kind == dt.kind && t->byte_size == dt.byte_size && t->name != NULL
            && strcmp (t->name, tname) == 0)
          return t->id;
      }
    }
    dt.u.aggregate.num_members = 0;
    dt.u.aggregate.members = NULL;
    /* Count members */
    uint32_t n = 0;
    if (member_list != NULL && member_list->code != N_IGNORE)
      for (node_t m = NL_HEAD (member_list->u.ops); m != NULL; m = NL_NEXT (m))
        if (m->code == N_MEMBER && m->attr != NULL) n++;
    if (n > 0) {
      MIR_dbmember_t *mbrs = reg_malloc (c2m_ctx, sizeof (MIR_dbmember_t) * n);
      uint32_t idx = 0;
      for (node_t m = NL_HEAD (member_list->u.ops); m != NULL; m = NL_NEXT (m)) {
        if (m->code != N_MEMBER || m->attr == NULL) continue;
        decl_t md = m->attr;
        node_t m_decl = MEMBER_DECL (m);
        node_t m_id = (m_decl != NULL && m_decl->code == N_DECL) ? NL_HEAD (m_decl->u.ops) : NULL;
        mbrs[idx].name = (m_id != NULL && m_id->code == N_ID) ? m_id->u.s.s : NULL;
        mbrs[idx].type_id = dbinfo_lower_type (c2m_ctx, mod, md->decl_spec.type);
        mbrs[idx].byte_offset = (uint32_t) md->offset;
        mbrs[idx].byte_size = (md->decl_spec.type->raw_size != MIR_SIZE_MAX)
                                ? (uint32_t) md->decl_spec.type->raw_size : 0;
        mbrs[idx].bit_offset = (int16_t) md->bit_offset;
        mbrs[idx].bit_size = (md->width >= 0) ? (int16_t) md->width : 0;
        idx++;
      }
      dt.u.aggregate.num_members = idx;
      dt.u.aggregate.members = mbrs;
    }
    return MIR_dbinfo_add_type (ctx, mod, &dt);
  }
  case TM_ENUM: {
    node_t tag = type->u.tag_type;
    node_t tag_id = TAG_ID (tag);
    dt.kind = MIR_DBT_ENUM;
    dt.name = (tag_id != NULL && tag_id->code == N_ID) ? tag_id->u.s.s : NULL;
    dt.byte_size = type->raw_size != MIR_SIZE_MAX ? (uint32_t) type->raw_size : 0;
    dt.align = type->align >= 0 ? (uint32_t) type->align : 0;
    /* Underlying integer type */
    enum basic_type ebt = get_enum_basic_type (type);
    MIR_dbtype_t base_dt;
    memset (&base_dt, 0, sizeof (base_dt));
    base_dt.kind = MIR_DBT_BASE;
    base_dt.name = basic_type_dbname (ebt);
    base_dt.byte_size = (uint32_t) basic_type_size (ebt);
    base_dt.align = (uint32_t) basic_type_align (ebt);
    base_dt.u.base.encoding = basic_type_encoding (ebt);
    dt.u.enumeration.underlying_id = MIR_dbinfo_add_type (ctx, mod, &base_dt);
    /* Count enumerators */
    node_t enum_list = TAG_MEMBER_LIST (tag);
    uint32_t ne = 0;
    if (enum_list != NULL && enum_list->code != N_IGNORE)
      for (node_t e = NL_HEAD (enum_list->u.ops); e != NULL; e = NL_NEXT (e))
        if (e->code == N_ENUM_CONST) ne++;
    dt.u.enumeration.num_enumerators = ne;
    dt.u.enumeration.enumerators = NULL;
    if (ne > 0) {
      MIR_dbenumerator_t *ens = reg_malloc (c2m_ctx, sizeof (MIR_dbenumerator_t) * ne);
      uint32_t ei = 0;
      for (node_t e = NL_HEAD (enum_list->u.ops); e != NULL; e = NL_NEXT (e)) {
        if (e->code != N_ENUM_CONST) continue;
        node_t eid = NL_HEAD (e->u.ops);
        struct enum_value *ev = e->attr;
        ens[ei].name = (eid != NULL && eid->code == N_ID) ? eid->u.s.s : "?";
        ens[ei].value = ev ? ev->u.i_val : 0;
        ei++;
      }
      dt.u.enumeration.enumerators = ens;
    }
    return MIR_dbinfo_add_type (ctx, mod, &dt);
  }
  case TM_FUNC: {
    struct func_type *ft = type->u.func_type;
    MIR_dbtype_id_t ret_id = dbinfo_lower_type (c2m_ctx, mod, ft->ret_type);
    dt.kind = MIR_DBT_FUNC;
    dt.name = NULL;
    dt.byte_size = 0;
    dt.u.func.return_id = ret_id;
    dt.u.func.variadic = ft->dots_p;
    /* Count params */
    uint32_t np = 0;
    if (ft->param_list != NULL)
      for (node_t p = NL_HEAD (ft->param_list->u.ops); p != NULL; p = NL_NEXT (p))
        if (p->code == N_SPEC_DECL && !void_param_p (p)) np++;
    dt.u.func.num_params = np;
    dt.u.func.param_ids = NULL;
    if (np > 0) {
      MIR_dbtype_id_t *pids = reg_malloc (c2m_ctx, sizeof (MIR_dbtype_id_t) * np);
      uint32_t pi = 0;
      for (node_t p = NL_HEAD (ft->param_list->u.ops); p != NULL; p = NL_NEXT (p)) {
        if (p->code != N_SPEC_DECL || void_param_p (p)) continue;
        struct decl_spec *ds = get_param_decl_spec (p);
        pids[pi++] = ds ? dbinfo_lower_type (c2m_ctx, mod, ds->type) : 0;
      }
      dt.u.func.param_ids = pids;
    }
    return MIR_dbinfo_add_type (ctx, mod, &dt);
  }
  case TM_DICT:
  case TM_SLICE: {
    /* Opaque pointer types */
    dt.kind = MIR_DBT_PTR;
    dt.name = (type->mode == TM_DICT) ? "dict" : "slice";
    dt.byte_size = sizeof (void *);
    dt.align = sizeof (void *);
    dt.u.ref.target_id = 0; /* void* */
    return MIR_dbinfo_add_type (ctx, mod, &dt);
  }
  default: return 0;
  }
}

/* Emit MIR_dbvar_t records for all parameters and locals of the current function. */
/* Emit one source-level variable (parameter or local) into the function's
   debug info.  The MIR register name is reconstructed with the *same* rules
   used during code generation (see the N_ID case in gen() and get_param_name),
   so the code generator can later resolve it to a concrete machine location. */
static void dbinfo_emit_var (c2m_ctx_t c2m_ctx, MIR_module_t mod, MIR_func_t func, node_t id,
                             decl_t d, int is_param) {
  MIR_context_t ctx = c2m_ctx->ctx;
  struct type *type = d->decl_spec.type;
  pos_t pos = POS (id);
  struct node_scope *ns = d->scope ? d->scope->attr : NULL;
  unsigned scope_num = ns ? ns->func_scope_num : 0;
  MIR_dbvar_t dv;

  memset (&dv, 0, sizeof (dv));
  dv.source_name = id->u.s.s;
  dv.type_id = dbinfo_lower_type (c2m_ctx, mod, type);
  dv.scope_num = scope_num;
  dv.decl_line = pos.lno > 0 ? (uint32_t) pos.lno : 0;
  dv.decl_col = pos.ln_pos > 0 ? (uint16_t) pos.ln_pos : 0;
  dv.decl_file_id = (pos.fname != NULL) ? MIR_module_add_source_file (ctx, mod, pos.fname) : 0;
  dv.is_param = is_param ? 1 : 0;
  if (d->reg_p) {
    /* Scalar kept in a MIR register.  Reconstruct the register name exactly as
       gen()/get_param_name() does so mir-gen can match and resolve it. */
    const char *mir_name;
    if (is_param) {
      mir_name = get_param_name (c2m_ctx, type, id->u.s.s);
    } else {
      MIR_type_t t = promote_mir_int_type (get_mir_type (c2m_ctx, type));
      mir_name = get_reg_var_name (c2m_ctx, t, id->u.s.s, scope_num);
    }
    dv.loc_kind = MIR_DBLOC_REG;
    dv.loc.reg_name = mir_name;
  } else {
    /* Aggregate / address-taken variable living in the frame at [fp + offset]. */
    dv.loc_kind = MIR_DBLOC_FRAME;
    dv.loc.frame_offset = (int64_t) d->offset;
  }
  MIR_dbinfo_add_var (ctx, func, &dv);
}

/* True for a declaration that has its own storage outside the function frame
   (globals, static and extern locals): those are not described here. */
static int dbinfo_local_skip_p (c2m_ctx_t c2m_ctx, decl_t d) {
  if (d == NULL || d->decl_spec.typedef_p) return 1;
  if (d->decl_spec.type == NULL || d->decl_spec.type->mode == TM_FUNC) return 1;
  if (d->scope == top_scope || d->decl_spec.static_p
      || d->decl_spec.linkage != N_IGNORE)
    return 1;
  return 0;
}

/* Recursively collect local variable declarations from the statement subtree
   rooted at N.  Only the statement node types that can introduce declarations
   are traversed; nested function/lambda bodies are intentionally not entered. */
static void dbinfo_walk_stmt (c2m_ctx_t c2m_ctx, MIR_module_t mod, MIR_func_t func, node_t n) {
  if (n == NULL || n->code == N_IGNORE) return;
  switch (n->code) {
  case N_SPEC_DECL: {
    decl_t d = n->attr;
    node_t declr = NL_EL (n->u.ops, 1);
    node_t id = (declr != NULL && declr->code == N_DECL) ? NL_HEAD (declr->u.ops) : NULL;
    if (id != NULL && id->code == N_ID && !dbinfo_local_skip_p (c2m_ctx, d))
      dbinfo_emit_var (c2m_ctx, mod, func, id, d, 0);
    break;
  }
  case N_LIST:
    for (node_t e = NL_HEAD (n->u.ops); e != NULL; e = NL_NEXT (e))
      dbinfo_walk_stmt (c2m_ctx, mod, func, e);
    break;
  case N_BLOCK:
    dbinfo_walk_stmt (c2m_ctx, mod, func, NL_EL (n->u.ops, 1)); /* declaration|stmt list */
    break;
  case N_IF: { /* labels, expr, then, else? */
    node_t then_s = NL_EL (n->u.ops, 2);
    dbinfo_walk_stmt (c2m_ctx, mod, func, then_s);
    dbinfo_walk_stmt (c2m_ctx, mod, func, NL_NEXT (then_s));
    break;
  }
  case N_SWITCH:
  case N_WHILE:
  case N_DO: /* labels, expr, stmt (DO: labels, stmt, expr) */
    dbinfo_walk_stmt (c2m_ctx, mod, func, NL_EL (n->u.ops, 2));
    dbinfo_walk_stmt (c2m_ctx, mod, func, NL_EL (n->u.ops, 1));
    break;
  case N_FOR: /* labels, init, cond, iter, stmt */
    dbinfo_walk_stmt (c2m_ctx, mod, func, NL_EL (n->u.ops, 1)); /* init declarations */
    dbinfo_walk_stmt (c2m_ctx, mod, func, NL_EL (n->u.ops, 4)); /* body */
    break;
  case N_FORIN: /* labels, var_id, val_id, collection, body */
    dbinfo_walk_stmt (c2m_ctx, mod, func, NL_EL (n->u.ops, 4));
    break;
  case N_DEFER: /* labels, stmt */
    dbinfo_walk_stmt (c2m_ctx, mod, func, NL_EL (n->u.ops, 1));
    break;
  case N_ATTACH: /* labels, expr — stub statement, walk inner for line info */
    dbinfo_walk_stmt (c2m_ctx, mod, func, NL_EL (n->u.ops, 1));
    break;
  case N_UNOWNED: /* wrapper around an inner declaration list */
  case N_OWNED:   /* managed-ownership wrapper around an inner declaration list */
    dbinfo_walk_stmt (c2m_ctx, mod, func, NL_HEAD (n->u.ops));
    break;
  case N_TRY: { /* labels, body_block, catch_list */
    dbinfo_walk_stmt (c2m_ctx, mod, func, NL_EL (n->u.ops, 1)); /* try-body block */
    node_t catch_list = NL_EL (n->u.ops, 2);
    if (catch_list != NULL)
      for (node_t cat = NL_HEAD (catch_list->u.ops); cat != NULL; cat = NL_NEXT (cat))
        dbinfo_walk_stmt (c2m_ctx, mod, func, NL_EL (cat->u.ops, 2)); /* handler block */
    break;
  }
  case N_THROW:  /* labels, id_expr, msg_expr */
    break;
  default:
    break;
  }
}

static void dbinfo_emit_func_vars (c2m_ctx_t c2m_ctx, node_t func_def_node) {
  gen_ctx_t gen_ctx = c2m_ctx->gen_ctx;
  MIR_context_t ctx = c2m_ctx->ctx;
  MIR_module_t mod = DLIST_TAIL (MIR_module_t, *MIR_get_module_list (ctx));
  MIR_func_t func = curr_func->u.func;
  decl_t func_decl = func_def_node->attr;
  struct type *decl_type = func_decl->decl_spec.type;
  struct func_type *ft = decl_type->u.func_type;

  if (mod == NULL || func == NULL) return;

  /* Parameters */
  if (ft->param_list != NULL) {
    for (node_t p = NL_HEAD (ft->param_list->u.ops); p != NULL; p = NL_NEXT (p)) {
      if (p->code != N_SPEC_DECL || void_param_p (p)) continue;
      decl_t pd = p->attr;
      if (pd == NULL) continue;
      node_t p_decl = NL_EL (p->u.ops, 1);
      node_t p_id = (p_decl != NULL && p_decl->code == N_DECL) ? NL_HEAD (p_decl->u.ops) : NULL;
      if (p_id == NULL || p_id->code != N_ID) continue;
      dbinfo_emit_var (c2m_ctx, mod, func, p_id, pd, 1);
    }
  }

  /* Locals: walk the function body AST so every declaration is described with
     its real source name, type and declaration position. */
  dbinfo_walk_stmt (c2m_ctx, mod, func, FUNC_DEF_BLOCK (func_def_node));
}

#endif /* !MIR_NO_DBINFO */

static void gen_mir (c2m_ctx_t c2m_ctx, node_t r) {
  MIR_alloc_t alloc = c2m_alloc (c2m_ctx);
  gen_ctx_t gen_ctx;
  MIR_context_t ctx = c2m_ctx->ctx;

  c2m_ctx->gen_ctx = gen_ctx = c2mir_calloc (c2m_ctx, sizeof (struct gen_ctx));
  zero_op = new_op (NULL, MIR_new_int_op (ctx, 0));
  one_op = new_op (NULL, MIR_new_int_op (ctx, 1));
  minus_one_op = new_op (NULL, MIR_new_int_op (ctx, -1));
  init_reg_vars (c2m_ctx);
  VARR_CREATE (MIR_var_t, proto_info.arg_vars, alloc, 32);
  VARR_CREATE (MIR_type_t, proto_info.ret_types, alloc, 16);
  gen_mir_protos (c2m_ctx);
  VARR_CREATE (MIR_op_t, call_ops, alloc, 32);
  VARR_CREATE (MIR_op_t, ret_ops, alloc, 8);
  VARR_CREATE (MIR_op_t, switch_ops, alloc, 128);
  VARR_CREATE (case_t, switch_cases, alloc, 64);
  VARR_CREATE (init_el_t, init_els, alloc, 128);
  VARR_CREATE (node_t, node_stack, alloc, 8);
  VARR_CREATE (node_t, defer_stmts, alloc, 16);
  VARR_CREATE (MIR_alias_t, union_alias_done, alloc, 8);
  memset_proto = memset_item = memcpy_proto = memcpy_item = NULL;
  memcmp_proto = memcmp_item = NULL;
  cy_spawn8_proto = cy_spawn8_item = cy_yield_proto = cy_yield_item = NULL;
  /* Forward-declare every class method MIR item before generating any body,
     so cross-class refs emitted while gen-ing an earlier class still have a
     non-null MIR_op_t target (resolved to the real definition at
     MIR_finish_module time).  Only class methods participate -- free
     functions still follow C11 "declare before use" rules. */
  gen_forward_class_methods (c2m_ctx, r);
  top_gen (c2m_ctx, r, NULL, NULL, NULL);
  gen_finish (c2m_ctx);
}

/* ------------------------- MIR generator finish ----------------------------- */
