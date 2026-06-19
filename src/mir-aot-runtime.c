#include <stdint.h>

#define C2M_STR_API
#include "cstring.h"
#define C2M_DICT_API
#include "dict.h"
#define C2M_EXC_API
#include "cyexc.h"

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
