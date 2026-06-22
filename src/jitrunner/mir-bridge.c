/* mir-bridge.c — implementation of the MIR bridge for the JIT runner
 *
 * This file is compiled with gcc (not classyc) because it needs the real
 * MIR headers with all the DLIST macros, inline functions, etc.
 * It is linked into the final AOT binary together with libmir_static.a.
 *
 * Build:  gcc -O2 -I ext/mir -I include -c src/jitrunner/mir-bridge.c -o mir-bridge.o
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <sys/stat.h>

/* Include the dict + cstring runtimes so the import resolver can hand
   their addresses to JIT'd code. */
#include "dict.h"
#include "cstring.h"
#include "cobjarena.h"
#include "cyexc.h"

#include "mir.h"
#include "mir-gen.h"

/* ── Standard shared libraries for the import resolver ────────────── */

struct lib { char *name; void *handler; };
typedef struct lib lib_t;

#if defined(__x86_64__)
static lib_t std_libs[] = {
    {"/lib64/libc.so.6", NULL},
    {"/lib/x86_64-linux-gnu/libc.so.6", NULL},
    {"/lib64/libm.so.6", NULL},
    {"/lib/x86_64-linux-gnu/libm.so.6", NULL},
    {"/usr/lib64/libpthread.so.0", NULL},
    {"/lib/x86_64-linux-gnu/libpthread.so.0", NULL},
    {"/usr/lib/libc.so", NULL}
};
#elif defined(__aarch64__)
static lib_t std_libs[] = {
    {"/lib64/libc.so.6", NULL},
    {"/lib/aarch64-linux-gnu/libc.so.6", NULL},
    {"/lib64/libm.so.6", NULL},
    {"/lib/aarch64-linux-gnu/libm.so.6", NULL},
    {"/lib64/libpthread.so.0", NULL},
    {"/lib/aarch64-linux-gnu/libpthread.so.0", NULL}
};
#else
static lib_t std_libs[] = {
    {"/lib64/libc.so.6", NULL},
    {"/lib64/libm.so.6", NULL},
    {"/usr/lib/libc.so", NULL}
};
#endif

static void open_std_libs(void) {
    for (size_t i = 0; i < sizeof(std_libs) / sizeof(lib_t); i++)
        std_libs[i].handler = dlopen(std_libs[i].name, RTLD_LAZY);
}

static void close_std_libs(void) {
    for (size_t i = 0; i < sizeof(std_libs) / sizeof(lib_t); i++)
        if (std_libs[i].handler) dlclose(std_libs[i].handler);
}

static int libs_opened = 0;

static void ensure_std_libs(void) {
    if (!libs_opened) { open_std_libs(); libs_opened = 1; }
}

/* ── Import resolver (mirrors classyc-driver.c) ──────────────────── */

static void *import_resolver(const char *name) {
    void *handler, *sym = NULL;

    ensure_std_libs();

    /* Search system shared libraries first */
    for (size_t i = 0; i < sizeof(std_libs) / sizeof(lib_t); i++)
        if ((handler = std_libs[i].handler) != NULL
            && (sym = dlsym(handler, name)) != NULL)
            break;

    if (sym) return sym;

    /* dl* functions */
    if (strcmp(name, "dlopen")  == 0) return dlopen;
    if (strcmp(name, "dlerror") == 0) return dlerror;
    if (strcmp(name, "dlclose") == 0) return dlclose;
    if (strcmp(name, "dlsym")  == 0) return dlsym;
    if (strcmp(name, "stat")   == 0) return stat;
    if (strcmp(name, "lstat")  == 0) return lstat;
    if (strcmp(name, "fstat")  == 0) return fstat;

    /* Dict runtime */
    if (strcmp(name, "dict_create_object")      == 0) return (void *)dict_create_object;
    if (strcmp(name, "dict_create_null")         == 0) return (void *)dict_create_null;
    if (strcmp(name, "dict_create_bool")         == 0) return (void *)dict_create_bool;
    if (strcmp(name, "dict_create_number")       == 0) return (void *)dict_create_number;
    if (strcmp(name, "dict_create_int64")        == 0) return (void *)dict_create_int64;
    if (strcmp(name, "dict_create_string")       == 0) return (void *)dict_create_string;
    if (strcmp(name, "dict_create_array")        == 0) return (void *)dict_create_array;
    if (strcmp(name, "dict_object_set")          == 0) return (void *)dict_object_set;
    if (strcmp(name, "dict_object_get")          == 0) return (void *)dict_object_get;
    if (strcmp(name, "dict_value_copy")          == 0) return (void *)dict_value_copy;
    if (strcmp(name, "dict_object_remove")       == 0) return (void *)dict_object_remove;
    if (strcmp(name, "dict_serialize_json")      == 0) return (void *)dict_serialize_json;
    if (strcmp(name, "dict_deserialize_json")    == 0) return (void *)dict_deserialize_json;
    if (strcmp(name, "dict_destroy")             == 0) return (void *)dict_destroy;
    if (strcmp(name, "dict_create_heap_arena")   == 0) return (void *)dict_create_heap_arena;
    if (strcmp(name, "dict_find_path")           == 0) return (void *)dict_find_path;
    if (strcmp(name, "dict_value_free")          == 0) return (void *)dict_value_free;
    if (strcmp(name, "dict_array_append")        == 0) return (void *)dict_array_append;
    if (strcmp(name, "dict_object_count")        == 0) return (void *)dict_object_count;
    if (strcmp(name, "dict_object_key_at")       == 0) return (void *)dict_object_key_at;
    if (strcmp(name, "dict_object_value_at")     == 0) return (void *)dict_object_value_at;
    if (strcmp(name, "dict_value_at")            == 0) return (void *)dict_value_at;

    /* String runtime */
    if (strcmp(name, "c2m_str_length")           == 0) return (void *)c2m_str_length;
    if (strcmp(name, "c2m_str_empty")            == 0) return (void *)c2m_str_empty;
    if (strcmp(name, "c2m_str_substr")           == 0) return (void *)c2m_str_substr;
    if (strcmp(name, "c2m_str_find")             == 0) return (void *)c2m_str_find;
    if (strcmp(name, "c2m_str_replace")          == 0) return (void *)c2m_str_replace;
    if (strcmp(name, "c2m_str_upper")            == 0) return (void *)c2m_str_upper;
    if (strcmp(name, "c2m_str_lower")            == 0) return (void *)c2m_str_lower;
    if (strcmp(name, "c2m_str_starts_with")      == 0) return (void *)c2m_str_starts_with;
    if (strcmp(name, "c2m_str_ends_with")        == 0) return (void *)c2m_str_ends_with;
    if (strcmp(name, "c2m_str_contains")         == 0) return (void *)c2m_str_contains;
    if (strcmp(name, "c2m_str_trim")             == 0) return (void *)c2m_str_trim;
    if (strcmp(name, "c2m_str_equals")           == 0) return (void *)c2m_str_equals;
    if (strcmp(name, "c2m_str_split")            == 0) return (void *)c2m_str_split;
    if (strcmp(name, "c2m_str_join")             == 0) return (void *)c2m_str_join;
    if (strcmp(name, "c2m_str_detach")           == 0) return (void *)c2m_str_detach;
    if (strcmp(name, "c2m_str_attach")           == 0) return (void *)c2m_str_attach;
    if (strcmp(name, "c2m_str_cleanup")          == 0) return (void *)c2m_str_cleanup;
    if (strcmp(name, "c2m_str_checkpoint")       == 0) return (void *)c2m_str_checkpoint;
    if (strcmp(name, "c2m_str_release_to")       == 0) return (void *)c2m_str_release_to;
    if (strcmp(name, "c2m_str_release_keeping")  == 0) return (void *)c2m_str_release_keeping;
    if (strcmp(name, "c2m_str_concat")           == 0) return (void *)c2m_str_concat;
    if (strcmp(name, "c2m_str_from_int")         == 0) return (void *)c2m_str_from_int;
    if (strcmp(name, "c2m_str_from_uint")        == 0) return (void *)c2m_str_from_uint;
    if (strcmp(name, "c2m_str_from_bool")        == 0) return (void *)c2m_str_from_bool;
    if (strcmp(name, "c2m_str_from_char")        == 0) return (void *)c2m_str_from_char;
    if (strcmp(name, "c2m_str_from_double")      == 0) return (void *)c2m_str_from_double;
    if (strcmp(name, "c2m_str_copy")             == 0) return (void *)c2m_str_copy;
    /* try/catch/throw exception runtime (cyexc.h) */
    if (strcmp(name, "cy_exc_push")              == 0) return (void *)cy_exc_push;
    if (strcmp(name, "cy_exc_pop")               == 0) return (void *)cy_exc_pop;
    if (strcmp(name, "cy_exc_current")           == 0) return (void *)cy_exc_current;
    if (strcmp(name, "cy_exc_throw")             == 0) return (void *)cy_exc_throw;
    if (strcmp(name, "cy_exc_active")            == 0) return (void *)cy_exc_active;
    if (strcmp(name, "_safety_trap")             == 0) return (void *)_safety_trap;

    fprintf(stderr, "[jitrunner] cannot resolve symbol: %s\n", name);
    return NULL;
}

/* ══════════════════════════════════════════════════════════════════════
   Public bridge API
   ══════════════════════════════════════════════════════════════════════ */

void *jit_init(void) {
    ensure_std_libs();
    return (void *)MIR_init();
}

void jit_finish(void *ctx) {
    if (ctx) MIR_finish((MIR_context_t)ctx);
}

int jit_read_bmir(void *ctx, char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    MIR_read((MIR_context_t)ctx, f);
    fclose(f);
    return 0;
}

/* ── Module / item iteration (wrapping DLIST macros) ─────────────── */

void *jit_first_module(void *ctx) {
    DLIST(MIR_module_t) *mlist = MIR_get_module_list((MIR_context_t)ctx);
    return (void *)DLIST_HEAD(MIR_module_t, *mlist);
}

void *jit_next_module(void *m) {
    if (!m) return NULL;
    return (void *)DLIST_NEXT(MIR_module_t, (MIR_module_t)m);
}

void *jit_first_item(void *m) {
    if (!m) return NULL;
    return (void *)DLIST_HEAD(MIR_item_t, ((MIR_module_t)m)->items);
}

void *jit_next_item(void *item) {
    if (!item) return NULL;
    return (void *)DLIST_NEXT(MIR_item_t, (MIR_item_t)item);
}

int jit_item_is_func(void *item) {
    if (!item) return 0;
    return ((MIR_item_t)item)->item_type == MIR_func_item;
}

char *jit_func_name(void *item) {
    if (!item) return NULL;
    MIR_item_t it = (MIR_item_t)item;
    if (it->item_type != MIR_func_item) return NULL;
    return (char *)it->u.func->name;
}

/* ── Load + link + gen ───────────────────────────────────────────── */

void jit_load_module(void *ctx, void *m) {
    MIR_load_module((MIR_context_t)ctx, (MIR_module_t)m);
}

void jit_gen_init(void *ctx) {
    MIR_gen_init((MIR_context_t)ctx);
}

void jit_gen_finish(void *ctx) {
    MIR_gen_finish((MIR_context_t)ctx);
}

void jit_link(void *ctx, int mode) {
    MIR_context_t mctx = (MIR_context_t)ctx;
    switch (mode) {
    case 1:  /* gen — full ahead-of-time gen for all functions */
        MIR_link(mctx, MIR_set_gen_interface, import_resolver);
        break;
    case 2:  /* interp */
        MIR_link(mctx, MIR_set_interp_interface, import_resolver);
        break;
    default: /* lazy (0 or anything else) */
        MIR_link(mctx, MIR_set_lazy_gen_interface, import_resolver);
        break;
    }
}

/* ── Execute ─────────────────────────────────────────────────────── */

void *jit_get_func_addr(void *item) {
    if (!item) return NULL;
    return ((MIR_item_t)item)->addr;
}

void *jit_gen_func(void *ctx, void *item) {
    if (!ctx || !item) return NULL;
    return MIR_gen((MIR_context_t)ctx, (MIR_item_t)item);
}
