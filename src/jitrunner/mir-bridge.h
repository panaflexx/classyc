/* mir-bridge.h — thin C bridge exposing MIR JIT API to ClassyC code + debug helpers
 *
 * ClassyC compiles to .bmir which is linked AOT via classyc-aot.sh --with-mir.
 * The MIR symbols live in libmir_static.a.  This header provides the extern
 * declarations so that our ClassyC source can call them directly.
 *
 * We also wrap the handful of struct-walking / DLIST macros in plain C
 * functions, since the ClassyC front-end cannot expand MIR's generic macros.
 */

#ifndef JITRUNNER_MIR_BRIDGE_H
#define JITRUNNER_MIR_BRIDGE_H

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

/* ── Opaque handles ─────────────────────────────────────────────────── */
typedef void *JIT_context;
typedef void *JIT_module;
typedef void *JIT_item;
typedef void *JIT_func;

/* ── Debug types (stable C ABI) ─────────────────────────────────────── */
typedef struct {
    uint32_t pc_offset;
    uint32_t line;
    uint16_t col;
    uint16_t file_id;
} JIT_line_entry;

typedef struct {
    const char *name;
    int is_param;
    uint32_t decl_line;
    uint16_t decl_file_id;
    uint32_t type_id;
    int mach_kind;
    uint16_t mach_reg;
    int32_t mach_offset;
    uint8_t mach_deref;
    int32_t mach_offset2;
} JIT_var_info;

/* ── Lifecycle ──────────────────────────────────────────────────────── */
JIT_context jit_init(void);
void        jit_finish(JIT_context ctx);
int         jit_read_bmir(JIT_context ctx, char *path);

JIT_module  jit_first_module(JIT_context ctx);
JIT_module  jit_next_module(JIT_module m);
JIT_item    jit_first_item(JIT_module m);
JIT_item    jit_next_item(JIT_item item);
int         jit_item_is_func(JIT_item item);
char       *jit_func_name(JIT_item item);
int         jit_module_num_source_files(JIT_module m);
const char *jit_module_source_file(JIT_module m, int file_id);

void        jit_load_module(JIT_context ctx, JIT_module m);
void        jit_gen_init(JIT_context ctx);
void        jit_gen_finish(JIT_context ctx);
void        jit_link(JIT_context ctx, int mode);

void       *jit_get_func_addr(JIT_item item);
size_t      jit_func_code_len(JIT_item item);
void       *jit_gen_func(JIT_context ctx, JIT_item item);

/* ── Debug query API ────────────────────────────────────────────────── */
int         jit_func_has_debug(JIT_item item);
int         jit_func_num_linemap(JIT_item item);
int         jit_func_get_linemap(JIT_item item, JIT_line_entry *out, int max_entries);
JIT_line_entry *jit_func_linemap_ptr(JIT_item item, int *out_count);
size_t      jit_func_line_to_pc(JIT_item item, int file_id, int line);
int         jit_func_pc_to_line(JIT_item item, size_t pc_offset, int *out_file_id, int *out_line, int *out_col);
int         jit_func_num_vars(JIT_item item);
int         jit_func_get_vars(JIT_item item, JIT_var_info *out, int max_vars);
void       *jit_resolve_breakpoint(JIT_context ctx, const char *file_path, int line, void **out_func, size_t *out_pc_offset);

/* ── GDB JIT interface ──────────────────────────────────────────────── */
int jit_gdb_register_code(const char *symfile_addr, size_t symfile_size);
int jit_gdb_unregister_code(const char *symfile_addr);

/* ── Self-debug (INT3) ──────────────────────────────────────────────── */
int jit_self_set_breakpoint(void *addr, unsigned char *saved_byte);
int jit_self_clear_breakpoint(void *addr, unsigned char saved_byte);
int jit_self_page_protect(void *addr, int make_rw);

/* ── Interp debugger (cooperative, Linux+macOS 10.12, no ptrace) ────── */
typedef struct { char file[512]; int line; int enabled; } JIT_breakpoint;
typedef struct JIT_interp_dbg_state JIT_interp_dbg_state_t;

JIT_interp_dbg_state_t *jit_interp_dbg_new(void);
void  jit_interp_dbg_free(JIT_interp_dbg_state_t *st);
void  jit_interp_dbg_set_state(JIT_interp_dbg_state_t *st);
int   jit_interp_dbg_add_bp(JIT_interp_dbg_state_t *st, const char *file, int line);
void  jit_interp_dbg_clear_bps(JIT_interp_dbg_state_t *st);
void  jit_interp_dbg_set_break_cb(JIT_interp_dbg_state_t *st, void (*cb)(void *user, void *func_item, int file_id, int line, int col), void *user);
void  jit_interp_dbg_continue(JIT_interp_dbg_state_t *st);
void  jit_interp_dbg_step_in(JIT_interp_dbg_state_t *st);
void  jit_interp_dbg_next(JIT_interp_dbg_state_t *st);
int   jit_interp_dbg_current(JIT_interp_dbg_state_t *st, char *out_file, int file_cap, int *out_line, int *out_col, char **out_func_name);
void  interp_child_on_break_simple(void *user, void *func_item, int file_id, int line, int col);

#endif
