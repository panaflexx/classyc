/*
 * mir-aot-runtime.c - runtime support for ahead-of-time compiled MIR objects.
 *
 * The MIR x86-64 code generator lowers a few operations into calls to internal
 * "mir.*" builtin functions whose implementations are static inside the MIR
 * library and therefore not linkable by name.  When MIR code is emitted as an
 * object file (via b2obj), those calls become relocations and the helpers must
 * be provided by a real, linkable object - this file.
 *
 * b2obj maps the internal builtin names to the symbols defined here, e.g.
 *     mir.ui2f  ->  mir_aot_ui2f
 *
 * The implementations mirror the ones in mir-gen-x86_64.c exactly.
 */
#include <stdint.h>

#define C2M_STR_API
#include "cstring.h"
#define C2M_DICT_API
#include "dict.h"

float       mir_aot_ui2f  (uint64_t i)    { return (float) i; }
double      mir_aot_ui2d  (uint64_t i)    { return (double) i; }
long double mir_aot_ui2ld (uint64_t i)    { return (long double) i; }
int64_t     mir_aot_ld2i  (long double l) { return (int64_t) l; }
