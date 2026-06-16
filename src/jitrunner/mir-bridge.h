/* mir-bridge.h — thin C bridge exposing MIR JIT API to ClassyC code
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

/* ── Opaque handles ─────────────────────────────────────────────────── */
/* We treat MIR's pointer types as void* so ClassyC doesn't need the
   full MIR header.  The bridge .c file includes the real headers. */

typedef void *JIT_context;     /* MIR_context_t  */
typedef void *JIT_module;      /* MIR_module_t   */
typedef void *JIT_item;        /* MIR_item_t     */
typedef void *JIT_func;        /* MIR_func_t     */

/* ── Lifecycle ──────────────────────────────────────────────────────── */
JIT_context jit_init(void);
void        jit_finish(JIT_context ctx);

/* ── Load .bmir ─────────────────────────────────────────────────────── */
int         jit_read_bmir(JIT_context ctx, char *path);

/* ── Module / item iteration ────────────────────────────────────────── */
JIT_module  jit_first_module(JIT_context ctx);
JIT_module  jit_next_module(JIT_module m);
JIT_item    jit_first_item(JIT_module m);
JIT_item    jit_next_item(JIT_item item);

int         jit_item_is_func(JIT_item item);
char       *jit_func_name(JIT_item item);

/* ── Load + link + gen ──────────────────────────────────────────────── */
void        jit_load_module(JIT_context ctx, JIT_module m);
void        jit_gen_init(JIT_context ctx);
void        jit_gen_finish(JIT_context ctx);

/* set_interface is one of: 0=lazy (default), 1=gen, 2=interp */
void        jit_link(JIT_context ctx, int mode);

/* ── Execute ────────────────────────────────────────────────────────── */
/* Returns the function address after JIT compilation.
   For main(), cast to:  int (*)(int, char**, char**)  */
void       *jit_get_func_addr(JIT_item item);

/* Convenience: JIT-gen a single function, returns its native address. */
void       *jit_gen_func(JIT_context ctx, JIT_item item);

#endif /* JITRUNNER_MIR_BRIDGE_H */
