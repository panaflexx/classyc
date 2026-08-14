/* This file is a part of MIR project.
   Copyright (C) 2020-2024 Vladimir Makarov <vmakarov.gcc@gmail.com>.
*/

static const char mirc[]
  = "#define __mirc__ 1\n"
    "#define __MIRC__ 1\n"
    "#define __STDC_HOSTED__ 1\n"
    "//#define __STDC_ISO_10646__ 201103L\n"
    "/* Atomics implemented via MIR (CLASSY-ATOMICS.md); do not set __STDC_NO_ATOMICS__. */\n"
    "#define __STDC_NO_COMPLEX__ 1\n"
    "/* Do NOT define __STDC_NO_THREADS__: _Thread_local is implemented\n"
    "   (TLS-IMPLEMENTATION.md).  Full <threads.h> thrd_* API is still absent;\n"
    "   libraries (e.g. minicoro) key off this macro to enable C11 TLS. */\n"
    "#define __STDC_NO_VLA__ 1\n"
    "#define __STDC_UTF_16__ 1\n"
    "#define __STDC_UTF_32__ 1\n"
    "#define __STDC_VERSION__ 201112L\n"
    "#define __STDC__ 1\n"
    "\n"
    "/* Some GCC alternative keywords used but not defined in standard headers:  */\n"
    "#define __const const\n"
    "#define __const__ const\n"
    "#define __inline__ inline\n"
    "#define __restrict__ restrict\n"
    "#define __signed signed\n"
    "#define __signed__ signed\n"
    "#define __volatile volatile\n"
    "#define __volatile__ volatile\n"
    "/* GNU no-op: `__extension__ long long` / `__extension__ ({...})` prefix. */\n"
    "#define __extension__\n"
    "\n"
    /* Darwin sys/cdefs.h: `#if !defined(__GNUC__) || __GNUC__ < 4` →
       `#warning "Unsupported compiler detected"`.  Claim GCC 4.2 (Apple's
       documented minimum) so the warning is silent and GCC-5+ / clang-only
       paths stay closed.  `__has_*` stay 0 so Availability / nullability
       annotations stay off.  Do not define `__clang__`: that unmasks
       `_Nonnull` type-nullability tokens the parser does not accept. */
    "#define __GNUC__ 4\n"
    "#define __GNUC_MINOR__ 2\n"
    "#define __GNUC_PATCHLEVEL__ 1\n"
    /* Darwin (and glibc) remap memcpy/memset/snprintf to __builtin___*_chk
       when _FORTIFY_SOURCE > 0 and __GNUC__ is set.  We do not implement
       those builtins; disable fortify so list.h / map.h JIT-link. */
    "#define _FORTIFY_SOURCE 0\n"
    "#undef __USE_FORTIFY_LEVEL\n"
    "#define __USE_FORTIFY_LEVEL 0\n"
#if defined(__APPLE__)
    "#define __MACH__ 1\n"
#endif
    "#define __has_attribute(x) 0\n"
    "#define __has_feature(x) 0\n"
    "#define __has_extension(x) 0\n"
    "#define __has_c_attribute(x) 0\n"
    "#define __has_declspec_attribute(x) 0\n"
    "\n"
    "/* Built-in null pointer literal (used for String/dict/pointer comparisons): */\n"
    "#define null ((void*)0)\n";

#include "mirc_iso646.h"
#include "mirc_stdalign.h"
#include "mirc_stdbool.h"
#include "mirc_stdnoreturn.h"
#include "mirc_stdatomic.h"

#define TARGET_STD_INCLUDES                                                               \
  {"iso646.h", iso646_str}, {"stdalign.h", stdalign_str}, {"stdbool.h", stdbool_str},     \
    {"stdnoreturn.h", stdnoreturn_str}, {"stdatomic.h", stdatomic_str},                   \
    {"float.h", float_str}, {"limits.h", limits_str}, {"stdarg.h", stdarg_str},           \
    {"stdint.h", stdint_str}, {                                                           \
    "stddef.h", stddef_str                                                                \
  }
