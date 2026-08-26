/* mir-bridge.c — implementation of the MIR bridge for the JIT runner
 * Compiled with gcc (not classyc) because it needs MIR macros.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#if defined(__linux__) || defined(__APPLE__)
#include <sys/mman.h>
#ifndef MAP_ANONYMOUS
#define MAP_ANON 1
#define MAP_ANONYMOUS MAP_ANON
#endif
#endif
#define DICT_CLASSYC_INTERNAL
#include "dict.h"
#include "cstring.h"
#include "cobjarena.h"
#include "cyexc.h"
#define CYFIBER_IMPLEMENTATION /* jitrunner is a "host tool" per cyfiber.h's own doc comment */
#include "cyfiber.h"
#include "mir.h"
#include "mir-dbinfo.h"
#include "mir-gen.h"
#include "mir-bridge.h"

struct lib { char *name; void *handler; };
typedef struct lib lib_t;
#if defined(__x86_64__)
static lib_t std_libs[] = {
    {"/lib64/libc.so.6", NULL}, {"/lib/x86_64-linux-gnu/libc.so.6", NULL},
    {"/lib64/libm.so.6", NULL}, {"/lib/x86_64-linux-gnu/libm.so.6", NULL},
    {"/usr/lib64/libpthread.so.0", NULL}, {"/lib/x86_64-linux-gnu/libpthread.so.0", NULL},
    {"/usr/lib/libc.so", NULL},
    /* Not a general `-l` mechanism — jitrunner has no CLI flag to request
       extra libraries, so a bmir built with `classyc -l foo` only resolves
       if foo is hardcoded here too. Added for jit_backend (sqlite3, crypto);
       widen this list (or add a real -l passthrough) as more get used. */
    {"/lib/x86_64-linux-gnu/libsqlite3.so.0", NULL}, {"/usr/lib64/libsqlite3.so.0", NULL},
    {"/lib/x86_64-linux-gnu/libcrypto.so.3", NULL}, {"/usr/lib64/libcrypto.so.3", NULL}
};
#elif defined(__aarch64__)
static lib_t std_libs[] = {
    {"/lib64/libc.so.6", NULL}, {"/lib/aarch64-linux-gnu/libc.so.6", NULL},
    {"/lib64/libm.so.6", NULL}, {"/lib/aarch64-linux-gnu/libm.so.6", NULL},
    {"/lib64/libpthread.so.0", NULL}, {"/lib/aarch64-linux-gnu/libpthread.so.0", NULL}
};
#else
static lib_t std_libs[] = {
#if defined(__APPLE__)
    {"/usr/lib/libSystem.dylib", NULL},
#endif
    {"/lib64/libc.so.6", NULL}, {"/lib64/libm.so.6", NULL}, {"/usr/lib/libc.so", NULL}
};
#endif
static void open_std_libs(void){ for(size_t i=0;i<sizeof(std_libs)/sizeof(lib_t);i++) std_libs[i].handler=dlopen(std_libs[i].name,RTLD_LAZY); }
static int libs_opened=0;
static void ensure_std_libs(void){ if(!libs_opened){ open_std_libs(); libs_opened=1; } }

/* [[registry("NAME")]] cross-module reflection (JIT linker set), ported from
 * classyc-driver.c's import_resolver/cyreg_anchor: classyc lowers each
 * [[registry("NAME")]] record (e.g. httpserve.h's ROUTE()/[[HttpGet]]) to an
 * exported ref_data symbol __cyreg_<NAME>__<var> pointing at the record, and
 * leaves __start_cyreg_<NAME>/__stop_cyreg_<NAME> as undefined imports for
 * whatever JIT driver loads the program to resolve -- the JIT analogue of
 * GNU ld's __start_/__stop_ linker-set symbols used in AOT. classyc's own
 * -eg/-el/-eb driver does this; jitrunner didn't, so any httpserve.h-based
 * program failed to load here with "cannot resolve symbol:
 * __start_cyreg_routes" even with zero routes registered (the registry
 * machinery is compiled in unconditionally). MIR_link's import_resolver
 * callback only takes a name, not the MIR_context_t, so g_cyreg_ctx (set in
 * jit_link) stands in for classyc-driver.c's static main_ctx. */
typedef struct { char *name; void **arr; size_t n; } cyreg_set_t;
static cyreg_set_t *cyreg_sets = NULL;
static size_t cyreg_nsets = 0, cyreg_cap = 0;
static MIR_context_t g_cyreg_ctx = NULL;

static cyreg_set_t *cyreg_get_set(const char *regname){
    for(size_t i=0;i<cyreg_nsets;i++) if(strcmp(cyreg_sets[i].name,regname)==0) return &cyreg_sets[i];

    char prefix[512];
    int plen = snprintf(prefix, sizeof prefix, "__cyreg_%s__", regname);
    void **arr = NULL;
    size_t n = 0, cap = 0;
    DLIST(MIR_module_t) *mlist = MIR_get_module_list(g_cyreg_ctx);
    for(MIR_module_t m=DLIST_HEAD(MIR_module_t,*mlist); m!=NULL; m=DLIST_NEXT(MIR_module_t,m)){
        for(MIR_item_t it=DLIST_HEAD(MIR_item_t,m->items); it!=NULL; it=DLIST_NEXT(MIR_item_t,it)){
            if(it->item_type != MIR_ref_data_item) continue;
            const char *nm = it->u.ref_data->name;
            if(nm==NULL || strncmp(nm,prefix,(size_t)plen)!=0) continue;
            void *rec = it->u.ref_data->ref_item!=NULL ? it->u.ref_data->ref_item->addr : NULL;
            if(n>=cap){ cap = cap ? cap*2 : 8; arr = realloc(arr, cap*sizeof(void*)); }
            arr[n++] = rec;
        }
    }
    if(arr==NULL) arr = calloc(1, sizeof(void*)); /* non-NULL, unique base even when empty */
    if(cyreg_nsets>=cyreg_cap){ cyreg_cap = cyreg_cap ? cyreg_cap*2 : 8; cyreg_sets = realloc(cyreg_sets, cyreg_cap*sizeof(cyreg_set_t)); }
    cyreg_sets[cyreg_nsets].name = strdup(regname);
    cyreg_sets[cyreg_nsets].arr = arr;
    cyreg_sets[cyreg_nsets].n = n;
    return &cyreg_sets[cyreg_nsets++];
}

static void *cyreg_anchor(const char *sym){
    const char *reg; int stop;
    if(strncmp(sym,"__start_cyreg_",14)==0){ reg=sym+14; stop=0; }
    else if(strncmp(sym,"__stop_cyreg_",13)==0){ reg=sym+13; stop=1; }
    else return NULL;
    cyreg_set_t *s = cyreg_get_set(reg);
    return stop ? (void*)(s->arr + s->n) : (void*)s->arr;
}
static void *import_resolver(const char *name){
    void *handler,*sym=NULL; ensure_std_libs();
    { void *a = cyreg_anchor(name); if(a != NULL) return a; }
    for(size_t i=0;i<sizeof(std_libs)/sizeof(lib_t);i++){ handler=std_libs[i].handler; if(handler){ sym=dlsym(handler,name); if(sym) break; } }
    if(sym) return sym;
    if(strcmp(name,"dlopen")==0) return dlopen; if(strcmp(name,"dlerror")==0) return dlerror;
    if(strcmp(name,"dlclose")==0) return dlclose; if(strcmp(name,"dlsym")==0) return dlsym;
    if(strcmp(name,"stat")==0) return stat; if(strcmp(name,"lstat")==0) return lstat; if(strcmp(name,"fstat")==0) return fstat;
    if(strcmp(name,"dict_create_object")==0) return (void*)dict_create_object;
    if(strcmp(name,"dict_create_null")==0) return (void*)dict_create_null;
    if(strcmp(name,"dict_create_bool")==0) return (void*)dict_create_bool;
    if(strcmp(name,"dict_create_number")==0) return (void*)dict_create_number;
    if(strcmp(name,"dict_create_int64")==0) return (void*)dict_create_int64;
    if(strcmp(name,"dict_create_string")==0) return (void*)dict_create_string;
    if(strcmp(name,"dict_create_array")==0) return (void*)dict_create_array;
    if(strcmp(name,"dict_object_set")==0) return (void*)dict_object_set;
    if(strcmp(name,"dict_object_get")==0) return (void*)dict_object_get;
    if(strcmp(name,"dict_value_copy")==0) return (void*)dict_value_copy;
    if(strcmp(name,"dict_object_remove")==0) return (void*)dict_object_remove;
    if(strcmp(name,"dict_serialize_json")==0) return (void*)dict_serialize_json;
    if(strcmp(name,"dict_serialize_json_heap")==0) return (void*)dict_serialize_json_heap;
    if(strcmp(name,"dict_deserialize_json")==0) return (void*)dict_deserialize_json;
    if(strcmp(name,"dict_destroy")==0) return (void*)dict_destroy;
    if(strcmp(name,"dict_create_heap_arena")==0) return (void*)dict_create_heap_arena;
    if(strcmp(name,"dict_find_path")==0) return (void*)dict_find_path;
    if(strcmp(name,"dict_value_free")==0) return (void*)dict_value_free;
    if(strcmp(name,"dict_array_append")==0) return (void*)dict_array_append;
    if(strcmp(name,"dict_object_count")==0) return (void*)dict_object_count;
    if(strcmp(name,"dict_object_key_at")==0) return (void*)dict_object_key_at;
    if(strcmp(name,"dict_object_value_at")==0) return (void*)dict_object_value_at;
    if(strcmp(name,"dict_value_at")==0) return (void*)dict_value_at;
    if(strcmp(name,"dict_is_array")==0) return (void*)dict_is_array;
    if(strcmp(name,"dict_iter_count")==0) return (void*)dict_iter_count;
    if(strcmp(name,"c2m_str_length")==0) return (void*)c2m_str_length;
    if(strcmp(name,"c2m_str_empty")==0) return (void*)c2m_str_empty;
    if(strcmp(name,"c2m_str_substr")==0) return (void*)c2m_str_substr;
    if(strcmp(name,"c2m_str_find")==0) return (void*)c2m_str_find;
    if(strcmp(name,"c2m_str_replace")==0) return (void*)c2m_str_replace;
    if(strcmp(name,"c2m_str_replace_all")==0) return (void*)c2m_str_replace_all;
    if(strcmp(name,"c2m_str_upper")==0) return (void*)c2m_str_upper;
    if(strcmp(name,"c2m_str_lower")==0) return (void*)c2m_str_lower;
    if(strcmp(name,"c2m_str_starts_with")==0) return (void*)c2m_str_starts_with;
    if(strcmp(name,"c2m_str_ends_with")==0) return (void*)c2m_str_ends_with;
    if(strcmp(name,"c2m_str_contains")==0) return (void*)c2m_str_contains;
    if(strcmp(name,"c2m_str_trim")==0) return (void*)c2m_str_trim;
    if(strcmp(name,"c2m_str_equals")==0) return (void*)c2m_str_equals;
    if(strcmp(name,"c2m_str_split")==0) return (void*)c2m_str_split;
    if(strcmp(name,"c2m_str_join")==0) return (void*)c2m_str_join;
    if(strcmp(name,"c2m_str_detach")==0) return (void*)c2m_str_detach;
    if(strcmp(name,"c2m_str_attach")==0) return (void*)c2m_str_attach;
    if(strcmp(name,"c2m_str_own")==0) return (void*)c2m_str_own;
    if(strcmp(name,"c2m_str_drop")==0) return (void*)c2m_str_drop;
    if(strcmp(name,"c2m_str_cleanup")==0) return (void*)c2m_str_cleanup;
    if(strcmp(name,"c2m_str_checkpoint")==0) return (void*)c2m_str_checkpoint;
    if(strcmp(name,"c2m_str_release_to")==0) return (void*)c2m_str_release_to;
    if(strcmp(name,"c2m_str_release_keeping")==0) return (void*)c2m_str_release_keeping;
    if(strcmp(name,"c2m_str_concat")==0) return (void*)c2m_str_concat;
    if(strcmp(name,"c2m_str_from_int")==0) return (void*)c2m_str_from_int;
    if(strcmp(name,"c2m_str_from_uint")==0) return (void*)c2m_str_from_uint;
    if(strcmp(name,"c2m_str_from_bool")==0) return (void*)c2m_str_from_bool;
    if(strcmp(name,"c2m_str_from_char")==0) return (void*)c2m_str_from_char;
    if(strcmp(name,"c2m_str_from_double")==0) return (void*)c2m_str_from_double;
    if(strcmp(name,"c2m_str_copy")==0) return (void*)c2m_str_copy;
    if(strcmp(name,"cy_exc_push")==0) return (void*)cy_exc_push;
    if(strcmp(name,"cy_exc_pop")==0) return (void*)cy_exc_pop;
    if(strcmp(name,"cy_exc_current")==0) return (void*)cy_exc_current;
    if(strcmp(name,"cy_exc_throw")==0) return (void*)cy_exc_throw;
    if(strcmp(name,"cy_exc_active")==0) return (void*)cy_exc_active;
    if(strcmp(name,"cy_exc_set_marks")==0) return (void*)cy_exc_set_marks;
    if(strcmp(name,"cy_exc_current_str_mark")==0) return (void*)cy_exc_current_str_mark;
    if(strcmp(name,"cy_exc_current_obj_mark")==0) return (void*)cy_exc_current_obj_mark;
    if(strcmp(name,"cy_exc_current_defer_mark")==0) return (void*)cy_exc_current_defer_mark;
    if(strcmp(name,"cy_defer_push")==0) return (void*)cy_defer_push;
    if(strcmp(name,"cy_defer_checkpoint")==0) return (void*)cy_defer_checkpoint;
    if(strcmp(name,"cy_defer_discard_one")==0) return (void*)cy_defer_discard_one;
    if(strcmp(name,"cy_defer_release_to")==0) return (void*)cy_defer_release_to;
    if(strcmp(name,"_safety_trap")==0) return (void*)_safety_trap;
    /* __builtin_unreachable: some C headers (e.g. an exhaustive switch with
     * no default case, seen while testing ~/src/GUI/cejson's JsonType
     * switch under jitrunner) end up with a call to this compiler builtin
     * for the "impossible" fallthrough path. It's a zero-arg, no-return
     * call -- _safety_trap takes 3 args (reason, file_id, line) so it's not
     * a compatible substitute here; abort() matches the real calling
     * convention and is the correct behavior if control genuinely reaches
     * a path the compiler was told never happens. */
    if(strcmp(name,"__builtin_unreachable")==0) return (void*)abort;
    if(strcmp(name,"cy_safe_alloc")==0) return (void*)cy_safe_alloc;
    if(strcmp(name,"cy_safe_free")==0) return (void*)cy_safe_free;
    if(strcmp(name,"cy_safe_deref")==0) return (void*)cy_safe_deref;
    if(strcmp(name,"cy_obj_track")==0) return (void*)cy_obj_track;
    if(strcmp(name,"cy_obj_note_free")==0) return (void*)cy_obj_note_free;
    if(strcmp(name,"cy_obj_check")==0) return (void*)cy_obj_check;
    /* Object arena (Any<I> handles / arena-managed collections; cobjarena.h) */
    if(strcmp(name,"c2m_obj_track")==0) return (void*)c2m_obj_track;
    if(strcmp(name,"c2m_obj_checkpoint")==0) return (void*)c2m_obj_checkpoint;
    if(strcmp(name,"c2m_obj_release_to")==0) return (void*)c2m_obj_release_to;
    if(strcmp(name,"c2m_obj_detach")==0) return (void*)c2m_obj_detach;
    if(strcmp(name,"c2m_obj_cleanup")==0) return (void*)c2m_obj_cleanup;
    /* Fiber / channel runtime (cyfiber.h, CYFIBER_IMPLEMENTATION above) */
    if(strcmp(name,"cy_sched_init")==0) return (void*)cy_sched_init;
    if(strcmp(name,"cy_sched_run")==0) return (void*)cy_sched_run;
    if(strcmp(name,"cy_sched_shutdown")==0) return (void*)cy_sched_shutdown;
    if(strcmp(name,"add_scheduler")==0) return (void*)add_scheduler;
    if(strcmp(name,"add_schedular")==0) return (void*)add_schedular;
    if(strcmp(name,"cy_spawn")==0) return (void*)cy_spawn;
    if(strcmp(name,"cy_spawn8")==0) return (void*)cy_spawn8;
    if(strcmp(name,"cy_yield")==0) return (void*)cy_yield;
    if(strcmp(name,"cy_self")==0) return (void*)cy_self;
    if(strcmp(name,"cy_fiber_outstanding")==0) return (void*)cy_fiber_outstanding;
    if(strcmp(name,"cy_sleep_ms")==0) return (void*)cy_sleep_ms;
    if(strcmp(name,"cy_chan_create")==0) return (void*)cy_chan_create;
    if(strcmp(name,"cy_chan_send_park")==0) return (void*)cy_chan_send_park;
    if(strcmp(name,"cy_chan_recv_park")==0) return (void*)cy_chan_recv_park;
    if(strcmp(name,"cy_chan_try_send")==0) return (void*)cy_chan_try_send;
    if(strcmp(name,"cy_chan_try_recv")==0) return (void*)cy_chan_try_recv;
    if(strcmp(name,"cy_chan_send_timeout")==0) return (void*)cy_chan_send_timeout;
    if(strcmp(name,"cy_chan_recv_timeout")==0) return (void*)cy_chan_recv_timeout;
    if(strcmp(name,"cy_chan_close")==0) return (void*)cy_chan_close;
    if(strcmp(name,"cy_chan_is_closed")==0) return (void*)cy_chan_is_closed;
    if(strcmp(name,"cy_chan_size")==0) return (void*)cy_chan_size;
    if(strcmp(name,"cy_chan_capacity")==0) return (void*)cy_chan_capacity;
    if(strcmp(name,"cy_chan_dispose")==0) return (void*)cy_chan_dispose;
    /* Emulated TLS (real MIR-library functions, statically linked via
       libmir_static.a -- dlsym against dlopen'd .so handles won't find
       them, so they need the same explicit-pointer treatment as the rest
       of this statically-compiled-in runtime). */
    if(strcmp(name,"mir.tls_addr")==0 || strcmp(name,"mir_tls_addr")==0) return (void*)mir_tls_addr;
    if(strcmp(name,"mir.tls_base")==0 || strcmp(name,"mir_tls_base")==0) return (void*)mir_tls_base;
    fprintf(stderr,"[jitrunner] cannot resolve symbol: %s\n",name); return NULL;
}
void *jit_init(void){
    ensure_std_libs();
    MIR_context_t ctx = MIR_init();
    /* Match classyc-driver.c: N source files → N MIR modules, and a class
       (or any header-inline helper) defined in a shared header is emitted
       by every TU that includes it. Without this, MIR_load_module aborts
       with "func … is prohibited for redefinition" — which is why a
       multi-TU [[HttpGet]] / httpserve.h app (http_crud, jit_backend)
       runs under classyc -eg but failed to load here. Identical copies
       get C++-inline / ODR semantics: linking to any one of them is
       correct. */
    MIR_set_func_redef_permission(ctx, TRUE);
    return (void*)ctx;
}
void jit_finish(void *ctx){ if(ctx) MIR_finish((MIR_context_t)ctx); }
int jit_read_bmir(void *ctx, char *path){ FILE *f=fopen(path,"rb"); if(!f) return -1; MIR_read((MIR_context_t)ctx,f); fclose(f); return 0; }
void *jit_first_module(void *ctx){ DLIST(MIR_module_t) *mlist=MIR_get_module_list((MIR_context_t)ctx); return (void*)DLIST_HEAD(MIR_module_t,*mlist); }
void *jit_next_module(void *m){ if(!m) return NULL; return (void*)DLIST_NEXT(MIR_module_t,(MIR_module_t)m); }
void *jit_first_item(void *m){ if(!m) return NULL; return (void*)DLIST_HEAD(MIR_item_t,((MIR_module_t)m)->items); }
void *jit_next_item(void *item){ if(!item) return NULL; return (void*)DLIST_NEXT(MIR_item_t,(MIR_item_t)item); }
int jit_item_is_func(void *item){ if(!item) return 0; return ((MIR_item_t)item)->item_type==MIR_func_item; }
char *jit_func_name(void *item){ if(!item) return NULL; MIR_item_t it=(MIR_item_t)item; if(it->item_type!=MIR_func_item) return NULL; return (char*)it->u.func->name; }
int jit_module_num_source_files(void *m){ if(!m) return 0; return (int)((MIR_module_t)m)->num_source_files; }
const char *jit_module_source_file(void *m,int fid){ if(!m) return NULL; MIR_module_t mod=(MIR_module_t)m; if(fid<0||(uint32_t)fid>mod->num_source_files) return NULL; return mod->source_files[fid]; }
void jit_load_module(void *ctx,void *m){ MIR_load_module((MIR_context_t)ctx,(MIR_module_t)m); }
void jit_gen_init(void *ctx){ MIR_gen_init((MIR_context_t)ctx); }
void jit_gen_finish(void *ctx){ MIR_gen_finish((MIR_context_t)ctx); }
void jit_link(void *ctx,int mode){
    MIR_context_t mctx=(MIR_context_t)ctx;
    g_cyreg_ctx = mctx; /* import_resolver's callback signature has no ctx param */
    /* Interp/debug path: keep real CALL/RET so step-over can track call_depth.
       MIR's process_inlines() would otherwise bake callees into the caller. */
    if(mode==2) MIR_set_no_inlines(1);
    else MIR_set_no_inlines(0);
    switch(mode){ case 1: MIR_link(mctx,MIR_set_gen_interface,import_resolver); break; case 2: MIR_link(mctx,MIR_set_interp_interface,import_resolver); break; default: MIR_link(mctx,MIR_set_lazy_gen_interface,import_resolver); break; }
}
void *jit_get_func_addr(void *item){ if(!item) return NULL; return ((MIR_item_t)item)->addr; }
size_t jit_func_code_len(void *item){ if(!item) return 0; MIR_item_t it=(MIR_item_t)item; if(it->item_type!=MIR_func_item) return 0; return it->u.func->machine_code_len; }
void *jit_gen_func(void *ctx,void *item){ if(!ctx||!item) return NULL; return MIR_gen((MIR_context_t)ctx,(MIR_item_t)item); }

/* Debug query */
static MIR_dbinfo_t *func_dbinfo(void *item){ if(!item) return NULL; MIR_item_t it=(MIR_item_t)item; if(it->item_type!=MIR_func_item) return NULL; return it->u.func->dbinfo; }
int jit_func_has_debug(void *item){ MIR_dbinfo_t *db=func_dbinfo(item); return db!=NULL && (db->num_vars>0 || (db->line_map && db->line_map->num_entries>0)); }
int jit_func_num_linemap(void *item){ MIR_dbinfo_t *db=func_dbinfo(item); if(!db||!db->line_map) return 0; return (int)db->line_map->num_entries; }
JIT_line_entry *jit_func_linemap_ptr(void *item,int *out_count){ MIR_dbinfo_t *db=func_dbinfo(item); if(out_count) *out_count=0; if(!db||!db->line_map) return NULL; if(out_count) *out_count=(int)db->line_map->num_entries; return (JIT_line_entry*)db->line_map->entries; }
int jit_func_get_linemap(void *item,JIT_line_entry *out,int max_e){ int n=0; JIT_line_entry *ptr=jit_func_linemap_ptr(item,&n); if(!ptr||!out||max_e<=0) return 0; int c=n<max_e?n:max_e; memcpy(out,ptr,c*sizeof(JIT_line_entry)); return c; }
size_t jit_func_line_to_pc(void *item,int fid,int line){ int n=0; JIT_line_entry *lm=jit_func_linemap_ptr(item,&n); if(!lm) return (size_t)-1; size_t best=(size_t)-1; for(int i=0;i<n;i++){ if(fid>=0 && lm[i].file_id!=(uint16_t)fid) continue; if((int)lm[i].line<line) continue; if((int)lm[i].line==line) return lm[i].pc_offset; if(best==(size_t)-1) best=lm[i].pc_offset; } if(best!=(size_t)-1) return best; if(n>0&&fid<0) return lm[n-1].pc_offset; return (size_t)-1; }
int jit_func_pc_to_line(void *item,size_t pc,int *out_fid,int *out_line,int *out_col){ int n=0; JIT_line_entry *lm=jit_func_linemap_ptr(item,&n); if(!lm||n==0) return -1; int best=-1; for(int i=0;i<n;i++){ if(lm[i].pc_offset<=pc) best=i; else break; } if(best<0) return -1; if(out_fid) *out_fid=lm[best].file_id; if(out_line) *out_line=lm[best].line; if(out_col) *out_col=lm[best].col; return 0; }
int jit_func_num_vars(void *item){ MIR_dbinfo_t *db=func_dbinfo(item); return db?(int)db->num_vars:0; }
int jit_func_get_vars(void *item,JIT_var_info *out,int max_v){ MIR_dbinfo_t *db=func_dbinfo(item); if(!db||!out||max_v<=0) return 0; int n=(int)db->num_vars; int c=n<max_v?n:max_v; for(int i=0;i<c;i++){ MIR_dbvar_t *v=&db->vars[i]; out[i].name=v->source_name; out[i].is_param=v->is_param; out[i].decl_line=v->decl_line; out[i].decl_file_id=v->decl_file_id; out[i].type_id=v->type_id; out[i].mach_kind=v->mach_kind; out[i].mach_reg=v->mach_reg; out[i].mach_offset=v->mach_offset; out[i].mach_deref=v->mach_deref; out[i].mach_offset2=v->mach_offset2; } return c; }
void *jit_resolve_breakpoint(void *ctx,const char *file_path,int line,void **out_func,size_t *out_pc){
    if(!ctx||!file_path) return NULL; const char *want_base=strrchr(file_path,'/'); want_base=want_base?want_base+1:file_path;
    for(MIR_module_t mod=DLIST_HEAD(MIR_module_t,*MIR_get_module_list((MIR_context_t)ctx)); mod!=NULL; mod=DLIST_NEXT(MIR_module_t,mod)){
        int mfid=-1; for(uint32_t fid=1;fid<=mod->num_source_files;fid++){ const char *sf=mod->source_files[fid]; if(!sf) continue; const char *b=strrchr(sf,'/'); b=b?b+1:sf; if(strcmp(sf,file_path)==0||strcmp(b,want_base)==0|| (strlen(sf)>=strlen(file_path)&&strcmp(sf+strlen(sf)-strlen(file_path),file_path)==0)){ mfid=(int)fid; break; } }
        if(mfid<0) continue;
        for(MIR_item_t it=DLIST_HEAD(MIR_item_t,mod->items); it!=NULL; it=DLIST_NEXT(MIR_item_t,it)){ if(it->item_type!=MIR_func_item) continue; MIR_func_t f=it->u.func; if(!f->dbinfo||!f->dbinfo->line_map) continue; size_t pc=jit_func_line_to_pc(it,mfid,line); if(pc==(size_t)-1) continue; if(it->addr==NULL) continue; if(out_func) *out_func=it; if(out_pc) *out_pc=pc; return (char*)it->addr+pc; }
    } return NULL;
}

/* GDB JIT */
typedef struct jit_code_entry{ struct jit_code_entry *next_entry; struct jit_code_entry *prev_entry; const char *symfile_addr; uint64_t symfile_size; } jit_code_entry_t;
typedef struct jit_descriptor{ uint32_t version; uint32_t action_flag; jit_code_entry_t *relevant_entry; jit_code_entry_t *first_entry; } jit_descriptor_t;
jit_descriptor_t __jit_debug_descriptor={1,0,NULL,NULL};
__attribute__((noinline,visibility("default"),noclone)) void __jit_debug_register_code(void){ __asm__ volatile("" ::: "memory"); }
int jit_gdb_register_code(const char *a,size_t s){ jit_code_entry_t *e=calloc(1,sizeof(*e)); if(!e) return -1; e->symfile_addr=a; e->symfile_size=s; e->next_entry=__jit_debug_descriptor.first_entry; if(e->next_entry) e->next_entry->prev_entry=e; __jit_debug_descriptor.first_entry=e; __jit_debug_descriptor.relevant_entry=e; __jit_debug_descriptor.action_flag=1; __jit_debug_register_code(); return 0; }
int jit_gdb_unregister_code(const char *a){ jit_code_entry_t *e=__jit_debug_descriptor.first_entry; while(e){ if(e->symfile_addr==a){ if(e->prev_entry) e->prev_entry->next_entry=e->next_entry; else __jit_debug_descriptor.first_entry=e->next_entry; if(e->next_entry) e->next_entry->prev_entry=e->prev_entry; __jit_debug_descriptor.relevant_entry=e; __jit_debug_descriptor.action_flag=2; __jit_debug_register_code(); free(e); return 0; } e=e->next_entry; } return -1; }

/* Self debug */
int jit_self_page_protect(void *addr,int make_rw){
#if defined(__linux__)||defined(__APPLE__)
    long pagesize=sysconf(_SC_PAGESIZE); if(pagesize<=0) pagesize=4096; uintptr_t page=(uintptr_t)addr & ~(uintptr_t)(pagesize-1);
    int prot=make_rw?(PROT_READ|PROT_WRITE):(PROT_READ|PROT_EXEC);
    if(mprotect((void*)page,(size_t)pagesize,prot)!=0){ if(mprotect((void*)page,(size_t)pagesize,PROT_READ|PROT_WRITE|PROT_EXEC)!=0) return -1; } return 0;
#else
    (void)addr; (void)make_rw; return -1;
#endif
}
int jit_self_set_breakpoint(void *addr,unsigned char *saved){ if(!addr) return -1; jit_self_page_protect(addr,1); unsigned char *p=addr; if(saved) *saved=*p; *p=0xCC; __builtin___clear_cache((char*)addr,(char*)addr+1); jit_self_page_protect(addr,0); return 0; }
int jit_self_clear_breakpoint(void *addr,unsigned char saved){ if(!addr) return -1; jit_self_page_protect(addr,1); unsigned char *p=addr; *p=saved; __builtin___clear_cache((char*)addr,(char*)addr+1); jit_self_page_protect(addr,0); return 0; }

/* ── Interp debugger ─────────────────────────────────────── */
extern void _mir_interp_set_debug_hook(void *hook,void *user);
extern void _mir_interp_set_step_mode(int on);
extern int dap_logger_fd;

struct JIT_interp_dbg_state{
    JIT_breakpoint *bps; int num_bps; int cap_bps;
    /* step_mode: 0=off  1=step-in (every line)  2=step-over (depth-limited) */
    int step_mode; int next_depth; int call_depth; int paused;
    /* Suppress re-breaking on the same source line until execution leaves it.
       Without this, continue after a BP immediately re-fires on the same insn. */
    int last_break_fid; int last_break_line; int suppress_same_line;
    void (*on_break)(void *user,void *func_item,int file_id,int line,int col);
    void *cb_user;
};
static __thread MIR_item_t g_cur_func=NULL;
static __thread int g_cur_fid=0,g_cur_line=0,g_cur_col=0;
static __thread int g_in_hook=0;
static struct JIT_interp_dbg_state *g_active=NULL;
static int suffix_match(const char *a,const char *b){ if(!a||!b) return 0; if(strcmp(a,b)==0) return 1; const char *pa=strrchr(a,'/'); pa=pa?pa+1:a; const char *pb=strrchr(b,'/'); pb=pb?pb+1:b; if(strcmp(pa,pb)==0) return 1; size_t la=strlen(a),lb=strlen(b); if(la>=lb && strcmp(a+la-lb,b)==0) return 1; if(lb>=la && strcmp(b+lb-la,a)==0) return 1; return 0; }
static int interp_line_hook(void *func_item,void *insn_ptr,void *user){
    if(g_in_hook) return 0; struct JIT_interp_dbg_state *st=(struct JIT_interp_dbg_state*)user;
    if(!st||!insn_ptr) return 0;
    MIR_insn_t insn=(MIR_insn_t)insn_ptr;
    /* Prefer the insn's owning function when available.  Nested MIR_interp
       calls share a process-global curr_func pointer which can be stale;
       insn->ops don't carry ownership, but the MIR func is on module items
       and the insn file/line are authoritative for BP matching. */
    MIR_item_t it=(MIR_item_t)func_item;
    if(it && it->item_type!=MIR_func_item) it=NULL;
    int fid=insn->source_file_id,line=insn->source_line,col=insn->source_col; if(line==0) return 0;
    g_cur_func=it; g_cur_fid=fid; g_cur_line=line; g_cur_col=col;

    /* Resolve source file: prefer insn's module via func_item, else scan
       active modules is unavailable here — fall back to NULL (match any). */
    const char *sf=NULL;
    if(it){
        MIR_module_t mod=it->module;
        if(mod && fid>=0 && (uint32_t)fid<=mod->num_source_files) sf=mod->source_files[fid];
    }
    /* Also try g_active's modules is overkill; basename match on bp file alone
       is allowed when sf is unknown (file[0] empty check already; when sf is
       NULL we treat file match as satisfied so line-only BPs still work). */

    /* Stay quiet on the same source line until execution leaves it.
       Applies to both continue (avoid re-hitting same BP) and step
       (advance to a different source line). */
    if (st->suppress_same_line) {
        if (fid != st->last_break_fid || line != st->last_break_line)
            st->suppress_same_line = 0;
        else
            return 0;
    }

    int should=0;
    int stop_reason_step=0;
    if(st->step_mode==1){
        /* step-in: stop on every new source line */
        should=1; stop_reason_step=1;
    } else if(st->step_mode==2){
        /* step-over: only stop once we are back at or above the step frame */
        if(st->call_depth <= st->next_depth){ should=1; stop_reason_step=1; }
        else if (getenv("CLASSYC_DEBUG_STEP"))
            fprintf(stderr, "step-over skip line=%d depth=%d next_depth=%d\n",
                    line, st->call_depth, st->next_depth);
    } else {
        for(int i=0;i<st->num_bps;i++){
            JIT_breakpoint *bp=&st->bps[i];
            if(!bp->enabled) continue;
            if(bp->line!=line) continue;
            /* File match: accept if bp has no path, or sf unknown, or suffix matches. */
            if(bp->file[0]!='\0' && sf && !suffix_match(sf,bp->file)) continue;
            should=1; break;
        }
    }
    if(!should) return 0;
    g_in_hook=1; st->paused=1;
    st->last_break_fid = fid; st->last_break_line = line; st->suppress_same_line = 1;
    /* Clear step intent BEFORE the callback so a resume that re-enables
       step (stepIn/next) is not wiped by this function after on_break. */
    if(st->step_mode){ st->step_mode=0; _mir_interp_set_step_mode(0); st->next_depth=0; }
    /* Tag step stops for the parent (different marker line prefix). */
    if (stop_reason_step)
        st->paused = 2; /* 2 = stepped (reused as flag for on_break via g_active) */
    if(st->on_break) st->on_break(st->cb_user, it ? (void*)it : func_item, fid, line, col);
    st->paused=0; g_in_hook=0; return 0;
}
extern void _mir_interp_set_call_depth_hook(void *hook, void *user);
static void call_depth_hook(int delta, void *user){
    (void)user;
    if(!g_active) return;
    g_active->call_depth += delta;
    if(g_active->call_depth < 0) g_active->call_depth = 0;
    if (getenv("CLASSYC_DEBUG_STEP") && dap_logger_fd < 0)
        fprintf(stderr, "call_depth delta=%d now=%d step_mode=%d next_depth=%d\n",
                delta, g_active->call_depth, g_active->step_mode, g_active->next_depth);
    if (dap_logger_fd >= 0)
        dprintf(dap_logger_fd, "call_depth delta=%d now=%d step_mode=%d next_depth=%d\n",
                delta, g_active->call_depth, g_active->step_mode, g_active->next_depth);
}

JIT_interp_dbg_state_t *jit_interp_dbg_new(void){ return calloc(1,sizeof(JIT_interp_dbg_state_t)); }
void jit_interp_dbg_free(JIT_interp_dbg_state_t *st){
    if(!st) return; free(st->bps); free(st);
    if(g_active==st){
        _mir_interp_set_debug_hook(NULL,NULL);
        _mir_interp_set_step_mode(0);
        _mir_interp_set_call_depth_hook(NULL,NULL);
        g_active=NULL;
    }
}
void jit_interp_dbg_set_state(JIT_interp_dbg_state_t *st){
    g_active=st;
    _mir_interp_set_debug_hook((void*)interp_line_hook,st);
    _mir_interp_set_step_mode(st && st->step_mode?1:0);
    _mir_interp_set_call_depth_hook(st ? (void*)call_depth_hook : NULL, NULL);
}

/* Scan loaded MIR for nearest executable source line >= requested (same file). */
int jit_nearest_source_line(JIT_context ctx, const char *file, int line){
    if(!ctx||!file||line<=0) return line;
    int best_ge = -1; /* smallest line >= requested */
    int best_any = -1; /* closest overall */
    for(void *mod=jit_first_module(ctx); mod; mod=jit_next_module(mod)){
        for(void *item=jit_first_item(mod); item; item=jit_next_item(item)){
            if(!jit_item_is_func(item)) continue;
            MIR_item_t it=(MIR_item_t)item;
            MIR_func_t fn=it->u.func;
            for(MIR_insn_t insn=DLIST_HEAD(MIR_insn_t,fn->insns); insn; insn=DLIST_NEXT(MIR_insn_t,insn)){
                if(insn->source_line==0) continue;
                const char *sf=NULL;
                MIR_module_t m=it->module;
                if(m && insn->source_file_id>=0 && (uint32_t)insn->source_file_id<=m->num_source_files)
                    sf=m->source_files[insn->source_file_id];
                if(sf && !suffix_match(sf,file)) continue;
                int L=(int)insn->source_line;
                if(L==line) return line; /* exact */
                if(L>line && (best_ge<0 || L<best_ge)) best_ge=L;
                {
                    int d1 = L>line ? L-line : line-L;
                    int d0 = best_any<0 ? 0x7fffffff : (best_any>line ? best_any-line : line-best_any);
                    if(best_any<0 || d1<d0) best_any=L;
                }
            }
        }
    }
    if(best_ge>0) return best_ge;
    if(best_any>0) return best_any;
    return line;
}

int jit_interp_dbg_add_bp(JIT_interp_dbg_state_t *st,const char *file,int line){
    if(!st||!file) return -1;
    if(st->num_bps>=st->cap_bps){
        int nc=st->cap_bps?st->cap_bps*2:16;
        void *nb=realloc(st->bps,nc*sizeof(JIT_breakpoint));
        if(!nb) return -1; st->bps=nb; st->cap_bps=nc;
    }
    JIT_breakpoint *bp=&st->bps[st->num_bps++];
    strncpy(bp->file,file,sizeof(bp->file)-1); bp->file[sizeof(bp->file)-1]='\0';
    bp->line=line; bp->enabled=1;
    return 0;
}
/* Like add_bp, but remap line to nearest executable loc in ctx first. */
int jit_interp_dbg_add_bp_resolved(JIT_interp_dbg_state_t *st, JIT_context ctx, const char *file, int line){
    int resolved = jit_nearest_source_line(ctx, file, line);
    if (dap_logger_fd>=0 && resolved!=line)
        dprintf(dap_logger_fd, "BP resolve %s:%d → %d\n", file, line, resolved);
    return jit_interp_dbg_add_bp(st, file, resolved);
}
void jit_interp_dbg_clear_bps(JIT_interp_dbg_state_t *st){ if(st) st->num_bps=0; }
void jit_interp_dbg_set_break_cb(JIT_interp_dbg_state_t *st,void (*cb)(void*,void*,int,int,int),void *user){ if(!st) return; st->on_break=cb; st->cb_user=user; }
void jit_interp_dbg_continue(JIT_interp_dbg_state_t *st){ if(!st) return; st->step_mode=0; st->paused=0; st->next_depth=0; _mir_interp_set_step_mode(0); }
void jit_interp_dbg_step_in(JIT_interp_dbg_state_t *st){ if(!st) return; st->step_mode=1; st->paused=0; st->next_depth=0; _mir_interp_set_step_mode(1); }
void jit_interp_dbg_next(JIT_interp_dbg_state_t *st){ if(!st) return; st->step_mode=2; st->next_depth=st->call_depth; st->paused=0; _mir_interp_set_step_mode(1); }
int jit_interp_dbg_current(JIT_interp_dbg_state_t *st,char *out_file,int file_cap,int *out_line,int *out_col,char **out_func_name){
    if(!st) return -1; if(out_line) *out_line=g_cur_line; if(out_col) *out_col=g_cur_col;
    if(out_func_name){ MIR_item_t cur=(MIR_item_t)g_cur_func; *out_func_name=cur&&cur->item_type==MIR_func_item?(char*)cur->u.func->name:NULL; }
    if(out_file&&file_cap>0){ MIR_item_t cur=(MIR_item_t)g_cur_func; const char *fname=NULL; if(cur){ MIR_module_t mod=cur->module; if(mod && g_cur_fid>=0 && (uint32_t)g_cur_fid<=mod->num_source_files) fname=mod->source_files[g_cur_fid]; } if(fname){ strncpy(out_file,fname,file_cap-1); out_file[file_cap-1]='\0'; } else out_file[0]='\0'; } return 0;
}

int g_interp_child_ctrl_fd_from_env=-1;
int g_dap_ctrl_write_fd=-1;

/* Safe byte write for the parent→child debug control pipe.
   Kept in plain C so ClassyC cannot collide with File::write. */
int dap_ctrl_write_byte(int fd, int byte){
    if (fd < 0) return -1;
    unsigned char b = (unsigned char)byte;
    for (;;) {
        ssize_t n = write(fd, &b, 1);
        if (n == 1) return 0;
        if (n < 0) {
            int e = errno;
            if (e == EINTR) continue;
            if (dap_logger_fd >= 0) dprintf(dap_logger_fd, "dap_ctrl_write_byte fd=%d errno=%d\n", fd, e);
            return -1;
        }
        /* n==0: treat as error */
        return -1;
    }
}

void interp_child_on_break_simple(void *user, void *func_item, int file_id, int line, int col){
 void *ctx=user; char fb[1024]; fb[0]='\0';
 void *mod=jit_first_module(ctx);
 while(mod){ int n=jit_module_num_source_files(mod); if(file_id>=0 && file_id<=n){ const char *sf=jit_module_source_file(mod,file_id); if(sf){ strncpy(fb,sf,sizeof(fb)-1); break; } } mod=jit_next_module(mod); }
 /* paused==2 means this stop came from step_mode (set above in the hook). */
 int is_step = (g_active && g_active->paused == 2) ? 1 : 0;
 fprintf(stderr,"%s %s:%d:%d\n", is_step ? "__DAP_STEP__" : "__DAP_BRK__",
         fb[0]?fb:"?", line, col); fflush(stderr);
 if (dap_logger_fd>=0) dprintf(dap_logger_fd, "CHILD %s %s:%d ctrl=%d\n",
                                is_step?"STEP":"BREAK", fb, line, g_interp_child_ctrl_fd_from_env);
 int ctrl_fd=g_interp_child_ctrl_fd_from_env; if(ctrl_fd<0){ char *s=getenv("CLASSYC_DEBUG_CTRL_FD"); if(s&&s[0]) ctrl_fd=atoi(s); }
 if (dap_logger_fd>=0) dprintf(dap_logger_fd, "CHILD waiting ctrl_fd=%d\n", ctrl_fd);
 if(ctrl_fd>=0){
   char cmd=0;
   while(1){
     long r=read(ctrl_fd,&cmd,1);
     if (dap_logger_fd>=0) dprintf(dap_logger_fd, "CHILD read ret=%ld cmd=%c\n", r, cmd);
     if(r==1) break;
     if(r==0) break;
     int e=*__errno_location();
     if(e==4||e==11){ usleep(1000); continue; }
     break;
   }
   /* Apply resume mode to g_active — the line hook checks st->step_mode,
      not only the global MIR flag. */
   if (cmd=='s') {
     jit_interp_dbg_step_in(g_active);
   } else if (cmd=='n') {
     jit_interp_dbg_next(g_active);
   } else {
     jit_interp_dbg_continue(g_active);
   }
   if (dap_logger_fd>=0)
     dprintf(dap_logger_fd, "CHILD resuming cmd=%c step_mode=%d next_depth=%d call_depth=%d\n",
             cmd, g_active?g_active->step_mode:0,
             g_active?g_active->next_depth:0, g_active?g_active->call_depth:0);
 }
}
