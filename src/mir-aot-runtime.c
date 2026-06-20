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
