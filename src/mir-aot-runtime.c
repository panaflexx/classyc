#include <stdint.h>

#define C2M_STR_API
#include "cstring.h"
#define C2M_DICT_API
#include "dict.h"
#define C2M_EXC_API
#include "cyexc.h"

/* ------------------------------------------------------------------ */
/*  Stack-trace helpers for the self-hosted (ClassyC-compiled) build.   */
/*                                                                      */
/*  classyc-stacktrace.h normally bootstraps a manual stack walk with   */
/*  the GCC builtins __builtin_frame_address / __builtin_return_address. */
/*  ClassyC itself does not implement those builtins, so when the driver */
/*  is compiled by ClassyC it calls these host-compiled helpers instead. */
/*  Level 1 yields the frame / return address of the *caller*            */
/*  (cstktr_walk_frames), matching what the inline builtins produced.    */
/* ------------------------------------------------------------------ */
void *cstktr_caller_frame_address (void)  { return __builtin_frame_address (1); }
void *cstktr_caller_return_address (void) { return __builtin_return_address (1); }

float       mir_aot_ui2f  (uint64_t i)    { return (float) i; }
double      mir_aot_ui2d  (uint64_t i)    { return (double) i; }
long double mir_aot_ui2ld (uint64_t i)    { return (long double) i; }
int64_t     mir_aot_ld2i  (long double l) { return (int64_t) l; }

#ifdef __APPLE__
#include <stdio.h>

FILE *__mir_stderr_ptr(void) { return stderr; }
FILE *__mir_stdout_ptr(void) { return stdout; }
FILE *__mir_stdin_ptr(void) { return stdin; }
#endif

/* ------------------------------------------------------------------ */
/*  Minimal va_arg / va_block_arg builtins for AOT binaries.          */
/*                                                                      */
/*  b2obj rewrites the internal MIR symbols mir.va_arg / mir.va_block_arg */
/*  to these external names.  The implementations below are the only    */
/*  pieces of the MIR core that most AOT programs actually need.        */
/*                                                                      */
/*  They are declared weak so that:                                     */
/*    • when you link ONLY the AOT runtime (the common case) you get    */
/*      the tiny definitions and a small binary;                        */
/*    • when you ALSO link libmir.a / mir.o the strong definitions from */
/*      the MIR library silently override these and there is no         */
/*      duplicate-symbol error.                                         */
/* ------------------------------------------------------------------ */

#if !defined(_WIN32)

struct x86_64_va_list {
    uint32_t gp_offset, fp_offset;
    uint64_t *overflow_arg_area, *reg_save_area;
};

__attribute__((weak))
void *va_arg_builtin (void *p, uint64_t t) {
    struct x86_64_va_list *va = p;
    int type = (int)t;                 /* MIR_type_t is a small enum */
    int fp_p = (type == 8 /* MIR_T_F */ || type == 9 /* MIR_T_D */);
    void *a;

    if (fp_p && va->fp_offset <= 160) {
        a = (char *) va->reg_save_area + va->fp_offset;
        va->fp_offset += 16;
    } else if (!fp_p && type != 10 /* MIR_T_LD */ && va->gp_offset <= 40) {
        a = (char *) va->reg_save_area + va->gp_offset;
        va->gp_offset += 8;
    } else {
        a = va->overflow_arg_area;
        va->overflow_arg_area += (type == 10 /* MIR_T_LD */ ? 2 : 1);
    }
    return a;
}

__attribute__((weak))
void va_block_arg_builtin (void *res, void *p, size_t s, uint64_t ncase) {
    /* For the vast majority of AOT-generated vararg calls (printf etc.)
       the simple overflow path is sufficient.  We still do the correct
       size rounding so that va_arg continues to work after a block arg. */
    struct x86_64_va_list *va = p;
    size_t size = ((s + 7) / 8) * 8;
    void *a = va->overflow_arg_area;
    if (res != NULL) memcpy (res, a, s);
    va->overflow_arg_area += size / 8;
}

#else
/* Win32 / MinGW – provide the _WIN32 versions if you ever need AOT on Windows */
struct x86_64_va_list {
    uint64_t *arg_area;
};

__attribute__((weak))
void *va_arg_builtin (void *p, uint64_t t) {
    struct x86_64_va_list *va = p;
    void *a = va->arg_area;
    va->arg_area++;
    return a;
}

__attribute__((weak))
void va_block_arg_builtin (void *res, void *p, size_t s, uint64_t ncase MIR_UNUSED) {
    struct x86_64_va_list *va = p;
    void *a = s <= 8 ? va->arg_area : *(void **) va->arg_area;
    if (res != NULL) memcpy (res, a, s);
    va->arg_area++;
}
#endif
