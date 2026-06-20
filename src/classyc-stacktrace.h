/* classyc-stacktrace.h — Decorated stack trace for ClassyC JIT crashes.
   Single-header library.  Include exactly once in the driver translation unit.

   Provides:
     classyc_stacktrace()           — walk + print trace (GDB-callable)
     classyc_install_crash_handlers — install SIGSEGV/SIGABRT/etc. handlers
     classyc_register_jit_funcs     — populate registry from MIR context

   Output format (Java / C#-style):

     === ClassyC Stack Trace ===
     Signal: Segmentation fault (SIGSEGV) at 0x0000000000000000

       at Point::sum(int x, int y)  [point.c:26]
       at main(int argc, char** argv)  [main.c:45]
       at <JIT entry>

     === End Stack Trace ===

   Copyright (C) 2025 ClassyC project.  */

#ifndef CLASSYC_STACKTRACE_H
#define CLASSYC_STACKTRACE_H

/* ------------------------------------------------------------------ */
/*  Platform gate                                                      */
/* ------------------------------------------------------------------ */
#if defined(__unix__) || defined(__APPLE__)

#include <signal.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>

/* We need ucontext for extracting frame/instruction pointers from the
   signal context.  On macOS this is in <sys/ucontext.h>. */
#if defined(__APPLE__)
#include <sys/ucontext.h>
#else
#include <ucontext.h>
#endif

/* mir.h gives us MIR_context_t, MIR_module_t, MIR_func_t, etc. */
#include "mir.h"
#if !MIR_NO_DBINFO
#include "mir-dbinfo.h"
#endif

/* ------------------------------------------------------------------ */
/*  Configuration                                                      */
/* ------------------------------------------------------------------ */
#define CSTKTR_MAX_FRAMES   64
#define CSTKTR_MAX_FUNCS    4096
#define CSTKTR_MAX_PARAMS   16
#define CSTKTR_NAME_BUF     512

/* ------------------------------------------------------------------ */
/*  Per-function registry entry                                        */
/* ------------------------------------------------------------------ */
typedef struct {
    const char *name;       /* parameter name (may be NULL) */
    const char *type_name;  /* type as string ("int", "Point*", …) */
} cstktr_param_t;

typedef struct {
    uintptr_t code_start;
    uintptr_t code_end;       /* exclusive */
    const char *display_name; /* "ClassName::method" or plain "funcname" */
    const char *mir_name;     /* raw mangled MIR name */
    const char *source_file;  /* source file path, NULL = unknown */
    uint32_t first_line;      /* first source line in function body */
    uint32_t num_params;
    cstktr_param_t params[CSTKTR_MAX_PARAMS];
} cstktr_func_t;

/* ------------------------------------------------------------------ */
/*  Global registry (populated before execution, read-only at runtime) */
/* ------------------------------------------------------------------ */
static cstktr_func_t *cstktr_table     = NULL;
static size_t          cstktr_count    = 0;
static size_t          cstktr_cap      = 0;

/* Stash the MIR context so the crash handler can re-scan for lazily
   generated functions that weren't present at registration time. */
static MIR_context_t   cstktr_mir_ctx  = NULL;

/* ------------------------------------------------------------------ */
/*  Async-signal-safe write helpers (write(2) to stderr only)          */
/* ------------------------------------------------------------------ */
/* Suppress warn_unused_result on write() inside a signal handler. */
static inline void cstktr_wr(const void *buf, size_t n) {
    ssize_t r = write(STDERR_FILENO, buf, n);
    (void)r;
}

static void cstktr_writes(const char *s) {
    if (s == NULL) return;
    size_t n = 0;
    while (s[n]) n++;
    cstktr_wr(s, n);
}

static void cstktr_writec(char c) { cstktr_wr(&c, 1); }

static void cstktr_write_uint(uint64_t v) {
    char buf[20];
    int i = 0;
    if (v == 0) { cstktr_writec('0'); return; }
    while (v) { buf[i++] = '0' + (char)(v % 10); v /= 10; }
    while (--i >= 0) cstktr_writec(buf[i]);
}

static void cstktr_write_hex(uint64_t v) {
    static const char hex[] = "0123456789abcdef";
    char buf[18];
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 15; i >= 0; i--) { buf[2 + (15 - i)] = hex[(v >> (i * 4)) & 0xf]; }
    cstktr_wr(buf, 18);
}

/* ------------------------------------------------------------------ */
/*  Display name demangling (mirrors dwarf_display_name in b2obj.c)    */
/* ------------------------------------------------------------------ */
static char cstktr_demangle_buf[CSTKTR_NAME_BUF];

static const char *cstktr_demangle(const char *mir_name) {
    if (mir_name == NULL) return "<unknown>";

    /* __ctor_ClassName__params → ClassName::ClassName */
    if (strncmp(mir_name, "__ctor_", 7) == 0) {
        const char *cls = mir_name + 7;
        const char *sep = strstr(cls, "__");
        size_t clen = sep ? (size_t)(sep - cls) : strlen(cls);
        if (clen > 0 && clen < sizeof(cstktr_demangle_buf) / 3) {
            snprintf(cstktr_demangle_buf, sizeof(cstktr_demangle_buf),
                     "%.*s::%.*s", (int)clen, cls, (int)clen, cls);
            return cstktr_demangle_buf;
        }
    }
    /* __dtor_ClassName__params → ClassName::~ClassName */
    if (strncmp(mir_name, "__dtor_", 7) == 0) {
        const char *cls = mir_name + 7;
        const char *sep = strstr(cls, "__");
        size_t clen = sep ? (size_t)(sep - cls) : strlen(cls);
        if (clen > 0 && clen < sizeof(cstktr_demangle_buf) / 3) {
            snprintf(cstktr_demangle_buf, sizeof(cstktr_demangle_buf),
                     "%.*s::~%.*s", (int)clen, cls, (int)clen, cls);
            return cstktr_demangle_buf;
        }
    }
    /* ClassName_method__params → ClassName::method
       The mangled name is: <class> '_' <method> '__' <param-types>.
       Use the FIRST underscore as the class/method separator, since
       class names are single identifiers while method names may
       contain underscores (e.g. "nationwide_audit"). */
    {
        const char *dbl = strstr(mir_name, "__");
        if (dbl != NULL && dbl != mir_name) {
            const char *first_under = strchr(mir_name, '_');
            if (first_under != NULL && first_under > mir_name && first_under < dbl) {
                size_t clen = (size_t)(first_under - mir_name);
                size_t mlen = (size_t)(dbl - first_under - 1);
                if (clen > 0 && mlen > 0
                    && clen + mlen + 4 < sizeof(cstktr_demangle_buf)) {
                    snprintf(cstktr_demangle_buf, sizeof(cstktr_demangle_buf),
                             "%.*s::%.*s", (int)clen, mir_name, (int)mlen, first_under + 1);
                    return cstktr_demangle_buf;
                }
            }
        }
    }
    return mir_name;
}

/* ------------------------------------------------------------------ */
/*  Registry management                                                */
/* ------------------------------------------------------------------ */
static void cstktr_ensure_cap(size_t need) {
    if (cstktr_count + need <= cstktr_cap) return;
    size_t nc = cstktr_cap ? cstktr_cap * 2 : 256;
    while (nc < cstktr_count + need) nc *= 2;
    cstktr_func_t *nb = (cstktr_func_t *)realloc(cstktr_table, nc * sizeof(cstktr_func_t));
    if (nb == NULL) return; /* best effort */
    cstktr_table = nb;
    cstktr_cap = nc;
}

/* Compare for qsort by code_start ascending. */
static int cstktr_cmp(const void *a, const void *b) {
    const cstktr_func_t *fa = (const cstktr_func_t *)a;
    const cstktr_func_t *fb = (const cstktr_func_t *)b;
    if (fa->code_start < fb->code_start) return -1;
    if (fa->code_start > fb->code_start) return 1;
    return 0;
}

/* Lookup: find the entry whose [code_start, code_end) contains `pc`. */
static const cstktr_func_t *cstktr_lookup(uintptr_t pc) {
    if (cstktr_table == NULL || cstktr_count == 0) return NULL;
    /* Binary search for the last entry with code_start <= pc. */
    size_t lo = 0, hi = cstktr_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (cstktr_table[mid].code_start <= pc)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo == 0) return NULL;
    const cstktr_func_t *e = &cstktr_table[lo - 1];
    if (pc >= e->code_start && pc < e->code_end) return e;
    return NULL;
}

/* Quick linear scan of MIR modules for a function containing `pc`.
   Used as a fallback when the sorted table has no match (lazy gen). */
static const char *cstktr_lazy_lookup_name(uintptr_t pc) {
    if (cstktr_mir_ctx == NULL) return NULL;
    DLIST(MIR_module_t) *mlist = MIR_get_module_list(cstktr_mir_ctx);
    for (MIR_module_t mod = DLIST_HEAD(MIR_module_t, *mlist); mod != NULL;
         mod = DLIST_NEXT(MIR_module_t, mod)) {
        for (MIR_item_t item = DLIST_HEAD(MIR_item_t, mod->items); item != NULL;
             item = DLIST_NEXT(MIR_item_t, item)) {
            if (item->item_type != MIR_func_item) continue;
            MIR_func_t func = item->u.func;
            if (func->machine_code == NULL || func->machine_code_len == 0) continue;
            uintptr_t start = (uintptr_t)func->machine_code;
            uintptr_t end   = start + func->machine_code_len;
            if (pc >= start && pc < end) return func->name;
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Type-name helper for dbinfo types                                  */
/* ------------------------------------------------------------------ */
#if !MIR_NO_DBINFO
/* Small buffer pool so we can return a few type-name strings without
   dynamic allocation (we only need up to CSTKTR_MAX_PARAMS at a time). */
static char cstktr_type_bufs[CSTKTR_MAX_PARAMS][128];
static int cstktr_type_buf_idx = 0;

static const char *cstktr_type_name(MIR_module_t mod, MIR_dbtype_id_t id) {
    if (mod == NULL || mod->dbtypes == NULL || id == 0) return "?";
    if (id >= mod->dbtypes->num_types) return "?";
    MIR_dbtype_t *dt = &mod->dbtypes->types[id];

    /* Chase qualifiers / typedefs to get a presentable name */
    int depth = 0;
    int ptr_depth = 0;
    while (dt && depth++ < 8) {
        if (dt->kind == MIR_DBT_CONST || dt->kind == MIR_DBT_VOLATILE
            || dt->kind == MIR_DBT_TYPEDEF || dt->kind == MIR_DBT_RESTRICT) {
            MIR_dbtype_id_t tid = dt->u.ref.target_id;
            if (tid == 0 || tid >= mod->dbtypes->num_types) break;
            dt = &mod->dbtypes->types[tid];
            continue;
        }
        if (dt->kind == MIR_DBT_PTR) {
            ptr_depth++;
            MIR_dbtype_id_t tid = dt->u.ref.target_id;
            if (tid == 0 || tid >= mod->dbtypes->num_types) break;
            dt = &mod->dbtypes->types[tid];
            continue;
        }
        break;
    }

    const char *base = "?";
    if (dt != NULL) {
        if (dt->name != NULL)
            base = dt->name;
        else if (dt->kind == MIR_DBT_BASE && dt->name != NULL)
            base = dt->name;
        else if (dt->kind == MIR_DBT_VOID || (dt->kind == MIR_DBT_BASE && dt->byte_size == 0))
            base = "void";
    }

    char *buf = cstktr_type_bufs[cstktr_type_buf_idx % CSTKTR_MAX_PARAMS];
    cstktr_type_buf_idx++;
    size_t off = 0;
    size_t blen = strlen(base);
    if (blen + (size_t)ptr_depth + 1 >= sizeof(cstktr_type_bufs[0]))
        return base; /* too long, just use base */
    memcpy(buf + off, base, blen); off += blen;
    for (int i = 0; i < ptr_depth; i++) buf[off++] = '*';
    buf[off] = '\0';
    return buf;
}
#endif /* !MIR_NO_DBINFO */

/* ------------------------------------------------------------------ */
/*  Populate registry from a MIR context (JIT path)                    */
/*                                                                     */
/*  Call this after MIR_link but before executing user code.            */
/* ------------------------------------------------------------------ */
static void classyc_register_jit_funcs(MIR_context_t ctx) {
    DLIST(MIR_module_t) *mlist = MIR_get_module_list(ctx);
    MIR_module_t mod;

    for (mod = DLIST_HEAD(MIR_module_t, *mlist); mod != NULL;
         mod = DLIST_NEXT(MIR_module_t, mod)) {
        MIR_item_t item;
        for (item = DLIST_HEAD(MIR_item_t, mod->items); item != NULL;
             item = DLIST_NEXT(MIR_item_t, item)) {
            if (item->item_type != MIR_func_item) continue;
            MIR_func_t func = item->u.func;
            if (func->machine_code == NULL || func->machine_code_len == 0) continue;

            cstktr_ensure_cap(1);
            cstktr_func_t *e = &cstktr_table[cstktr_count];
            memset(e, 0, sizeof(*e));

            e->code_start = (uintptr_t)func->machine_code;
            e->code_end   = (uintptr_t)func->machine_code + func->machine_code_len;
            e->mir_name   = func->name;
            e->display_name = cstktr_demangle(func->name);
            /* The demangle buffer is reused, so we need to intern the string. */
            if (e->display_name == cstktr_demangle_buf) {
                size_t len = strlen(cstktr_demangle_buf);
                char *copy = (char *)malloc(len + 1);
                if (copy) { memcpy(copy, cstktr_demangle_buf, len + 1); e->display_name = copy; }
                else e->display_name = func->name;
            }

            /* Source file + first line: scan instructions for the first one
               that has source location info. */
            e->source_file = NULL;
            e->first_line = 0;
            for (MIR_insn_t insn = DLIST_HEAD(MIR_insn_t, func->insns);
                 insn != NULL; insn = DLIST_NEXT(MIR_insn_t, insn)) {
                if (insn->source_line != 0) {
                    e->first_line = insn->source_line;
                    if (insn->source_file_id > 0
                        && insn->source_file_id <= mod->num_source_files
                        && mod->source_files != NULL) {
                        e->source_file = mod->source_files[insn->source_file_id];
                    }
                    break;
                }
            }

            /* Parameter info from dbinfo when available. */
            e->num_params = 0;
#if !MIR_NO_DBINFO
            cstktr_type_buf_idx = 0;
            if (func->dbinfo != NULL && func->dbinfo->num_vars > 0) {
                for (uint32_t vi = 0; vi < func->dbinfo->num_vars && e->num_params < CSTKTR_MAX_PARAMS; vi++) {
                    MIR_dbvar_t *dv = &func->dbinfo->vars[vi];
                    if (!dv->is_param) continue;
                    cstktr_param_t *pp = &e->params[e->num_params++];
                    pp->name = dv->source_name;
                    pp->type_name = cstktr_type_name(mod, dv->type_id);
                    /* Intern type_name since buffer is reused */
                    if (pp->type_name >= cstktr_type_bufs[0]
                        && pp->type_name < cstktr_type_bufs[CSTKTR_MAX_PARAMS] + 128) {
                        size_t len = strlen(pp->type_name);
                        char *copy = (char *)malloc(len + 1);
                        if (copy) { memcpy(copy, pp->type_name, len + 1); pp->type_name = copy; }
                        else pp->type_name = "?";
                    }
                }
            }
#endif
            cstktr_count++;
        }
    }

    /* Sort by code_start for binary search. */
    if (cstktr_count > 1)
        qsort(cstktr_table, cstktr_count, sizeof(cstktr_func_t), cstktr_cmp);
}

/* ------------------------------------------------------------------ */
/*  Print one stack frame                                              */
/* ------------------------------------------------------------------ */
static void cstktr_print_frame(int depth, uintptr_t pc) {
    cstktr_writes("  at ");

    const cstktr_func_t *e = cstktr_lookup(pc);
    if (e == NULL) {
        /* Fallback: try live scan for lazily-generated functions. */
        const char *lazy_name = cstktr_lazy_lookup_name(pc);
        if (lazy_name != NULL) {
            cstktr_writes(cstktr_demangle(lazy_name));
            cstktr_writes("()");
        } else {
            cstktr_writes("<native ");
            cstktr_write_hex(pc);
            cstktr_writes(">");
        }
    } else {
        /* Function name */
        cstktr_writes(e->display_name);
        /* Parameters */
        cstktr_writec('(');
        for (uint32_t i = 0; i < e->num_params; i++) {
            if (i > 0) cstktr_writes(", ");
            if (e->params[i].type_name) {
                cstktr_writes(e->params[i].type_name);
                cstktr_writec(' ');
            }
            cstktr_writes(e->params[i].name ? e->params[i].name : "?");
        }
        cstktr_writec(')');
        /* Source location */
        if (e->source_file != NULL || e->first_line != 0) {
            cstktr_writes("  [");
            if (e->source_file) {
                /* Print only the basename for brevity. */
                const char *base = e->source_file;
                const char *p = base;
                while (*p) { if (*p == '/') base = p + 1; p++; }
                cstktr_writes(base);
            } else {
                cstktr_writes("?");
            }
            if (e->first_line != 0) {
                cstktr_writec(':');
                cstktr_write_uint(e->first_line);
            }
            cstktr_writec(']');
        }
    }
    cstktr_writec('\n');
    (void)depth;
}

/* ------------------------------------------------------------------ */
/*  Signal name helper                                                 */
/* ------------------------------------------------------------------ */
static const char *cstktr_signame(int sig) {
    switch (sig) {
    case SIGSEGV: return "Segmentation fault (SIGSEGV)";
    case SIGABRT: return "Aborted (SIGABRT)";
    case SIGBUS:  return "Bus error (SIGBUS)";
    case SIGFPE:  return "Floating-point exception (SIGFPE)";
    case SIGILL:  return "Illegal instruction (SIGILL)";
    default:      return "Unknown signal";
    }
}

/* ------------------------------------------------------------------ */
/*  Frame-pointer walking (platform-specific)                          */
/* ------------------------------------------------------------------ */

/* Extract (frame_pointer, instruction_pointer) from a ucontext. */
static void cstktr_get_context_regs(void *uc_void, uintptr_t *out_fp, uintptr_t *out_ip) {
    *out_fp = 0; *out_ip = 0;
    if (uc_void == NULL) return;

#if defined(__APPLE__) && defined(__x86_64__)
    ucontext_t *uc = (ucontext_t *)uc_void;
    *out_ip = (uintptr_t)uc->uc_mcontext->__ss.__rip;
    *out_fp = (uintptr_t)uc->uc_mcontext->__ss.__rbp;
#elif defined(__APPLE__) && defined(__aarch64__)
    ucontext_t *uc = (ucontext_t *)uc_void;
    *out_ip = (uintptr_t)uc->uc_mcontext->__ss.__pc;
    *out_fp = (uintptr_t)uc->uc_mcontext->__ss.__fp;
#elif defined(__linux__) && defined(__x86_64__)
    ucontext_t *uc = (ucontext_t *)uc_void;
    /* REG_RIP=16, REG_RBP=10 — use numeric values to avoid _GNU_SOURCE dependency. */
    *out_ip = (uintptr_t)uc->uc_mcontext.gregs[16]; /* REG_RIP */
    *out_fp = (uintptr_t)uc->uc_mcontext.gregs[10]; /* REG_RBP */
#elif defined(__linux__) && defined(__aarch64__)
    ucontext_t *uc = (ucontext_t *)uc_void;
    *out_ip = (uintptr_t)uc->uc_mcontext.pc;
    *out_fp = (uintptr_t)uc->uc_mcontext.regs[29];   /* x29 = FP */
#else
    (void)uc_void;  /* unsupported architecture — no context extraction */
#endif
}

/* Check whether `addr` looks like it belongs to a JIT function. */
static int cstktr_is_jit_addr(uintptr_t addr) {
    if (cstktr_lookup(addr) != NULL) return 1;
    if (cstktr_lazy_lookup_name(addr) != NULL) return 1;
    return 0;
}

/* Scan the raw stack for return addresses that fall inside known JIT
   function ranges.  MIR-generated code does not maintain a frame-pointer
   chain, but every CALL instruction pushes a return address onto the
   stack.  We walk upward from RSP and collect words that look like
   return addresses into JIT code.

   This is a heuristic — it may include false positives (stale return
   addresses from completed calls), but in practice the signal fires at
   the point of the fault and the stack above it is intact. */
static int cstktr_scan_stack(uintptr_t sp, uintptr_t faulting_ip,
                              uintptr_t *out, int max_out) {
    int found = 0;
    /* We already printed the faulting IP; skip it in the scan. */
    uintptr_t skip = faulting_ip;
    /* Scan a reasonable range of the stack (512 slots ≈ 4 KB). */
    const int MAX_SCAN = 512;
    for (int i = 0; i < MAX_SCAN && found < max_out; i++) {
        uintptr_t *slot = (uintptr_t *)(sp + (uintptr_t)i * sizeof(uintptr_t));
        /* Cheap validity check: the address must be in a plausible code range. */
        uintptr_t val = *slot;
        if (val < 0x10000 || val == skip) continue;
        if (cstktr_is_jit_addr(val)) {
            /* Avoid duplicates from the same function (can happen with inlining). */
            int dup = 0;
            for (int j = 0; j < found; j++)
                if (out[j] == val) { dup = 1; break; }
            if (!dup) {
                out[found++] = val;
                skip = val; /* don't match the same word again */
            }
        }
    }
    return found;
}

/* Get the stack pointer from a ucontext. */
static uintptr_t cstktr_get_sp(void *uc_void) {
    if (uc_void == NULL) return 0;
#if defined(__APPLE__) && defined(__x86_64__)
    return (uintptr_t)((ucontext_t *)uc_void)->uc_mcontext->__ss.__rsp;
#elif defined(__APPLE__) && defined(__aarch64__)
    return (uintptr_t)((ucontext_t *)uc_void)->uc_mcontext->__ss.__sp;
#elif defined(__linux__) && defined(__x86_64__)
    return (uintptr_t)((ucontext_t *)uc_void)->uc_mcontext.gregs[15]; /* REG_RSP */
#elif defined(__linux__) && defined(__aarch64__)
    return (uintptr_t)((ucontext_t *)uc_void)->uc_mcontext.sp;
#else
    return 0;
#endif
}

/* Walk the frame-pointer chain starting from (fp, ip).
   If fp/ip are 0, start from the current call frame.
   uc_raw is the raw ucontext pointer (for stack scanning). */
static void cstktr_walk_frames(uintptr_t start_fp, uintptr_t start_ip,
                                void *uc_raw) {
    uintptr_t fp, ip;

    if (start_fp != 0) {
        fp = start_fp;
        ip = start_ip;
    } else {
        /* Called from GDB or user code — start from here. */
#if defined(__GNUC__) || defined(__clang__)
        fp = (uintptr_t)__builtin_frame_address(0);
        ip = (uintptr_t)__builtin_return_address(0);
#else
        /* Self-hosted build: ClassyC does not implement
           __builtin_frame_address / __builtin_return_address.  Use the
           host-compiled runtime helpers (src/mir-aot-runtime.c) instead.
           They read the frame/return address of *this* caller. */
        extern void *cstktr_caller_frame_address(void);
        extern void *cstktr_caller_return_address(void);
        fp = (uintptr_t)cstktr_caller_frame_address();
        ip = (uintptr_t)cstktr_caller_return_address();
#endif
    }

    int nframes = 0;

    /* If we have an explicit faulting IP (from signal context), print it first. */
    if (start_ip != 0) {
        cstktr_print_frame(nframes++, ip);
    }

    /* Try 1: frame-pointer chain (works when code is compiled with
       -fno-omit-frame-pointer, or for native frames above JIT code). */
    for (int i = 0; i < CSTKTR_MAX_FRAMES && fp != 0; i++) {
        if (fp < 0x1000 || (fp & (sizeof(void *) - 1)) != 0) break;

        uintptr_t *frame = (uintptr_t *)fp;
        uintptr_t next_fp  = frame[0];
        uintptr_t ret_addr = frame[1];

        if (ret_addr == 0) break;
        cstktr_print_frame(nframes++, ret_addr);

        if (next_fp <= fp) break;
        fp = next_fp;
    }

    /* Try 2: if the frame-pointer walk found ≤1 frame (likely MIR JIT
       code without frame pointers), scan the raw stack for return
       addresses that fall within registered JIT functions. */
    if (nframes <= 1 && (cstktr_count > 0 || cstktr_mir_ctx != NULL)) {
        uintptr_t sp = uc_raw ? cstktr_get_sp(uc_raw) : 0;
        if (sp == 0 && start_fp != 0) sp = start_fp;  /* rough estimate */
        if (sp != 0) {
            uintptr_t addrs[CSTKTR_MAX_FRAMES];
            int nfound = cstktr_scan_stack(sp, start_ip, addrs, CSTKTR_MAX_FRAMES);
            if (nfound > 0) {
                if (nframes <= 1)
                    cstktr_writes("  --- call chain (recovered from stack) ---\n");
                for (int i = 0; i < nfound; i++)
                    cstktr_print_frame(nframes++, addrs[i]);
            }
        }
    }

    if (nframes == 0) {
        cstktr_writes("  <no frames recovered>\n");
    }
}

/* ------------------------------------------------------------------ */
/*  Public: print a decorated stack trace — callable from GDB          */
/*                                                                     */
/*  Usage in GDB:  call classyc_stacktrace()                           */
/* ------------------------------------------------------------------ */
__attribute__((used, visibility("default")))
void classyc_stacktrace(void) {
    cstktr_writes("\n\033[1;31m=== ClassyC Stack Trace ===\033[0m\n");
    cstktr_walk_frames(0, 0, NULL);
    cstktr_writes("\033[1;31m=== End Stack Trace ===\033[0m\n\n");
}

/* ------------------------------------------------------------------ */
/*  Signal handler                                                     */
/* ------------------------------------------------------------------ */
static volatile sig_atomic_t cstktr_handling = 0;

static void cstktr_crash_handler(int sig, siginfo_t *si, void *uc) {
    /* Prevent re-entrant crashes while we print. */
    if (cstktr_handling) _exit(128 + sig);
    cstktr_handling = 1;

    cstktr_writes("\n\033[1;31m=== ClassyC Stack Trace ===\033[0m\n");
    cstktr_writes("Signal: ");
    cstktr_writes(cstktr_signame(sig));
    if (si != NULL) {
        cstktr_writes(" at ");
        cstktr_write_hex((uintptr_t)si->si_addr);
    }
    cstktr_writec('\n');
    cstktr_writec('\n');

    uintptr_t fp = 0, ip = 0;
    cstktr_get_context_regs(uc, &fp, &ip);
    cstktr_walk_frames(fp, ip, uc);

    cstktr_writes("\033[1;31m=== End Stack Trace ===\033[0m\n\n");

    /* Re-raise with default handler so the process exits with the right
       status and core-dumps if enabled. */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_DFL;
    sigaction(sig, &sa, NULL);
    raise(sig);
}

/* ------------------------------------------------------------------ */
/*  Install crash handlers (call before executing JIT code)             */
/* ------------------------------------------------------------------ */
static void classyc_install_crash_handlers(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = cstktr_crash_handler;
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND; /* one-shot to allow core dump */
    sigemptyset(&sa.sa_mask);

    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGBUS,  &sa, NULL);
    sigaction(SIGFPE,  &sa, NULL);
    sigaction(SIGILL,  &sa, NULL);
}

#else /* _WIN32 or unsupported */

/* Stubs so the driver compiles on all platforms. */
static void classyc_register_jit_funcs(void *ctx) { (void)ctx; }
static void classyc_install_crash_handlers(void) {}
__attribute__((used))
void classyc_stacktrace(void) {}

#endif /* platform gate */

#endif /* CLASSYC_STACKTRACE_H */
