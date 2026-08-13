/*
 * b2objmac.c - Convert a binary MIR (.bmir) file to a Mach-O 64-bit object file.
 *
 * This is the macOS counterpart of b2obj.c (which produces ELF objects).
 * It reads a .bmir file, generates machine code via the MIR code generator,
 * and writes a Mach-O MH_OBJECT file that can be linked with the macOS
 * system linker (ld).
 *
 * Usage:  b2objmac <input.bmir> <output.o>
 *
 * Architectures (selected at compile time with #ifdef):
 *   __x86_64__  (incl. macOS 10.12): Intel Mach-O, X86_64 + TLV relocs
 *   __aarch64__ (Apple Silicon):     ARM64 Mach-O, ARM64 + TLVP relocs
 *
 * TLS (macOS AOT — N1 emulated, both x64 and arm64):
 *   Codegen rewrites TLS to mir_tls_addr() before RA (call-safe).
 *   Object emits template(s) + strong `__mir_tls_aot_regs` for bootstrap.
 *   (Native Mach-O TLV is deferred until pre-RA lowering exists.)
 *
 * PIE-safe addresses (x86_64):
 *   Local movabs → leaq sym(%rip)  (string literals / .lc*)
 *   External movabs → movq GOTPCREL
 *
 * PIE-safe addresses (arm64, via mir-gen-aarch64 object mode):
 *   Local REF → adrp+add (PAGE21/PAGEOFF12)
 *   Import REF → adrp+ldr GOT (GOT_LOAD_PAGE*)
 *   Direct bl → BRANCH26 (stubs only for BRANCH26 to externals)
 *
 * macOS 10.12 compatibility: no APFS-only APIs, LC_VERSION_MIN_MACOSX,
 * arm64-only blocks behind #if defined(__aarch64__).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <time.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <mach-o/reloc.h>
#if defined(__aarch64__)
/* Apple Silicon — never compiled on macOS 10.12 (Intel-only). */
#include <mach-o/arm64/reloc.h>
#ifndef CPU_TYPE_ARM64
#define CPU_TYPE_ARM64 ((cpu_type_t) 0x0100000c)
#endif
#ifndef CPU_SUBTYPE_ARM64_ALL
#define CPU_SUBTYPE_ARM64_ALL ((cpu_subtype_t) 0)
#endif
#else
/* Intel macOS (including 10.12): x86_64 Mach-O. */
#include <mach-o/x86_64/reloc.h>
#endif
/* Thread-local section types: present on 10.12+ SDKs; define if missing. */
#ifndef S_THREAD_LOCAL_REGULAR
#define S_THREAD_LOCAL_REGULAR 0x11
#define S_THREAD_LOCAL_ZEROFILL 0x12
#define S_THREAD_LOCAL_VARIABLES 0x13
#endif
/* Debug section attribute (DWARF); present on all modern Apple SDKs. */
#ifndef S_ATTR_DEBUG
#define S_ATTR_DEBUG 0x02000000
#endif

#include "mir-alloc-default.c"
#include "mir-gen.h" /* mir.h included transitively */
#include "dwarf-aot-emit.h"

/* ELF GOT-relative relocation type (mir.h only defines PC32 and 64).  We use it
 * as an internal marker meaning "this reference must go through the GOT"; on
 * Mach-O it is emitted as X86_64_RELOC_GOT_LOAD. */
#ifndef R_X86_64_GOTPCREL
#define R_X86_64_GOTPCREL 9
#endif

/* ================================================================== */
/*  Debug tracing                                                      */
/* ================================================================== */
static int b2obj_debug = -1;
static double b2obj_t0 = 0.0;

static double b2obj_now (void) {
    struct timespec ts;
    clock_gettime (CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

#define DBG(...) do {                                            \
    if (b2obj_debug < 0) b2obj_debug = getenv ("B2OBJ_DEBUG") != NULL; \
    if (b2obj_debug) {                                           \
        if (b2obj_t0 == 0.0) b2obj_t0 = b2obj_now ();            \
        fprintf (stderr, "[b2objmac +%7.3fs] ", b2obj_now () - b2obj_t0); \
        fprintf (stderr, __VA_ARGS__);                           \
        fputc ('\n', stderr);                                    \
        fflush (stderr);                                         \
    }                                                            \
} while (0)

/* ================================================================== */
/*  MIR type / env constants (same as b2obj.c)                        */
/* ================================================================== */
#define MIR_TYPE_INTERP 1
#define MIR_TYPE_INTERP_NAME "interp"
#define MIR_TYPE_GEN 2
#define MIR_TYPE_GEN_NAME "gen"
#define MIR_TYPE_LAZY 3
#define MIR_TYPE_LAZY_NAME "lazy"

#define MIR_TYPE_DEFAULT MIR_TYPE_LAZY

#define MIR_ENV_VAR_LIB_DIRS "MIR_LIB_DIRS"
#define MIR_ENV_VAR_EXTRA_LIBS "MIR_LIBS"
#define MIR_ENV_VAR_TYPE "MIR_TYPE"

/* ================================================================== */
/*  Shared-library handling (macOS dylib paths)                        */
/* ================================================================== */
struct lib {
  char *name;
  void *handler;
};
typedef struct lib lib_t;

static lib_t std_libs[] = {{"/usr/lib/libc.dylib", NULL},
                           {"/usr/lib/libm.dylib", NULL}};
static const char *std_lib_dirs[] = {"/usr/lib", "/usr/local/lib"};
static const char *lib_suffix = ".dylib";
static const int slash = '/';

static void close_std_libs (void) {
  for (int i = 0; i < (int)(sizeof (std_libs) / sizeof (lib_t)); i++)
    if (std_libs[i].handler != NULL) dlclose (std_libs[i].handler);
}

static void open_std_libs (void) {
  for (int i = 0; i < (int)(sizeof (std_libs) / sizeof (struct lib)); i++)
    std_libs[i].handler = dlopen (std_libs[i].name, RTLD_LAZY);
}

DEF_VARR (lib_t);
static VARR (lib_t) * extra_libs;

typedef const char *char_ptr_t;
DEF_VARR (char_ptr_t);
static VARR (char_ptr_t) * lib_dirs;

DEF_VARR (char);
static VARR (char) * temp_string;

static void *open_lib (const char *dir, const char *name) {
  const char *last_slash = strrchr (dir, slash);
  void *res;
  FILE *f;

  VARR_TRUNC (char, temp_string, 0);
  VARR_PUSH_ARR (char, temp_string, dir, strlen (dir));
  if (last_slash == NULL || last_slash[1] != '\0')
    VARR_PUSH (char, temp_string, slash);
  VARR_PUSH_ARR (char, temp_string, "lib", 3);
  VARR_PUSH_ARR (char, temp_string, name, strlen (name));
  VARR_PUSH_ARR (char, temp_string, lib_suffix, strlen (lib_suffix));
  VARR_PUSH (char, temp_string, 0);
  if ((res = dlopen (VARR_ADDR (char, temp_string), RTLD_LAZY)) == NULL) {
    if ((f = fopen (VARR_ADDR (char, temp_string), "rb")) != NULL) {
      fclose (f);
      fprintf (stderr, "loading %s:%s\n", VARR_ADDR (char, temp_string), dlerror ());
    }
  }
  return res;
}

static void process_extra_lib (char *lib_name) {
  lib_t lib;
  lib.name = lib_name;
  for (size_t i = 0; i < VARR_LENGTH (char_ptr_t, lib_dirs); i++)
    if ((lib.handler = open_lib (VARR_GET (char_ptr_t, lib_dirs, i), lib_name)) != NULL) break;
  if (lib.handler == NULL) {
    fprintf (stderr, "cannot find library lib%s -- good bye\n", lib_name);
    exit (1);
  }
  VARR_PUSH (lib_t, extra_libs, lib);
}

static void close_extra_libs (void) {
  void *handler;
  for (size_t i = 0; i < VARR_LENGTH (lib_t, extra_libs); i++)
    if ((handler = VARR_GET (lib_t, extra_libs, i).handler) != NULL) dlclose (handler);
}

#if defined(__APPLE__) && defined(__aarch64__)
float __nan (void) {
  union { uint32_t i; float f; } u = {0x7fc00000};
  return u.f;
}
#endif

/* ================================================================== */
/*  Import resolver / symbol recording                                 */
/* ================================================================== */
typedef struct {
    const char *symbol;
    void       *addr;
} symbol_entry_t;

typedef struct {
    symbol_entry_t *entries;
    size_t n_entries;
    size_t capacity;
} symbol_list_t;

static symbol_list_t symbols = {0};
static char aot_undef_placeholder;

static void *import_resolver (const char *name) {
  void *handler, *sym = NULL;
  for (int i = 0; i < (int)(sizeof (std_libs) / sizeof (struct lib)); i++)
    if ((handler = std_libs[i].handler) != NULL && (sym = dlsym (handler, name)) != NULL) break;
  if (sym == NULL)
    for (size_t i = 0; i < VARR_LENGTH (lib_t, extra_libs); i++)
      if ((handler = VARR_GET (lib_t, extra_libs, i).handler) != NULL
          && (sym = dlsym (handler, name)) != NULL)
        break;
  if (sym == NULL) {
    if (strcmp (name, "dlopen") == 0) return dlopen;
    if (strcmp (name, "dlerror") == 0) return dlerror;
    if (strcmp (name, "dlclose") == 0) return dlclose;
    if (strcmp (name, "dlsym") == 0) return dlsym;
    if (strcmp (name, "stat") == 0) return stat;
    if (strcmp (name, "lstat") == 0) return lstat;
    if (strcmp (name, "fstat") == 0) return fstat;
#if defined(__APPLE__) && defined(__aarch64__)
    if (strcmp (name, "__nan") == 0) return __nan;
    if (strcmp (name, "_MIR_set_code") == 0) return _MIR_set_code;
#endif
    return NULL;
  }
  return sym;
}

void *hybrid_import_resolver (const char *name) {
  void *addr = import_resolver (name);
  if (addr == NULL) addr = &aot_undef_placeholder;
  if (symbols.n_entries >= symbols.capacity) {
    symbols.capacity = symbols.capacity ? symbols.capacity * 2 : 16;
    symbols.entries = realloc (symbols.entries, symbols.capacity * sizeof (symbol_entry_t));
  }
  symbols.entries[symbols.n_entries].symbol = strdup (name);
  symbols.entries[symbols.n_entries].addr = addr;
  symbols.n_entries++;
  return addr;
}

/* ================================================================== */
/*  Environment helpers                                                */
/* ================================================================== */
static void lib_dirs_from_env_var (const char *env_var) {
  const char *var_value = getenv (env_var);
  if (var_value == NULL || var_value[0] == '\0') return;
  int value_len = strlen (var_value);
  char *value = (char *) malloc (value_len + 1);
  strcpy (value, var_value);
  char *value_ptr = value;
  char *colon = NULL;
  while ((colon = strchr (value_ptr, ':')) != NULL) {
    colon[0] = '\0';
    VARR_PUSH (char_ptr_t, lib_dirs, value_ptr);
    value_ptr = colon + 1;
  }
  VARR_PUSH (char_ptr_t, lib_dirs, value_ptr);
}

static int get_mir_type (void) {
  const char *type_value = getenv (MIR_ENV_VAR_TYPE);
  if (type_value == NULL || type_value[0] == '\0') return MIR_TYPE_DEFAULT;
  if (strcmp (type_value, MIR_TYPE_INTERP_NAME) == 0) return MIR_TYPE_INTERP;
  if (strcmp (type_value, MIR_TYPE_GEN_NAME) == 0) return MIR_TYPE_GEN;
  if (strcmp (type_value, MIR_TYPE_LAZY_NAME) == 0) return MIR_TYPE_LAZY;
  fprintf (stderr, "warning: unknown MIR_TYPE '%s', using default one\n", type_value);
  return MIR_TYPE_DEFAULT;
}

static void open_extra_libs (void) {
  const char *var_value = getenv (MIR_ENV_VAR_EXTRA_LIBS);
  if (var_value == NULL || var_value[0] == '\0') return;
  int value_len = strlen (var_value);
  char *value = (char *) malloc (value_len + 1);
  strcpy (value, var_value);
  char *value_ptr = value;
  char *colon = NULL;
  while ((colon = strchr (value_ptr, ':')) != NULL) {
    colon[0] = '\0';
    process_extra_lib (value_ptr);
    value_ptr = colon + 1;
  }
  process_extra_lib (value_ptr);
}

/* ================================================================== */
/*  Collected item types (mirrors b2obj.c)                             */
/* ================================================================== */
typedef struct {
    const char *name;
    void       *code;
    size_t      code_len;
    size_t      text_offset;
    MIR_item_t  item;
} func_entry_t;

typedef struct {
    const char *name;
    uint8_t    *bytes;
    size_t      size;
    size_t      data_offset;
    int         is_ref_data;
    MIR_item_t  ref_item;
    int64_t     ref_disp;
    void       *item_addr;
} data_entry_t;

typedef struct {
    const char *name;
    size_t      len;
    size_t      bss_offset;
    void       *item_addr;
} bss_entry_t;

/* A single relocation to emit (Mach-O variant) */
typedef struct {
    size_t      offset;     /* offset within the target section (__text or __data) */
    const char *symbol;     /* symbol name */
    int         type;       /* original MIR reloc type (R_X86_64_PC32 or R_X86_64_64) */
    int64_t     addend;
    int         in_data;    /* 0 = __text reloc, 1 = __data reloc */
    int         is_movabs;  /* 1 = reloc sits on a `movabs reg,imm64' immediate */
    int         is_got;     /* 1 = rewritten into GOT-relative load (external) */
    int         is_lea;     /* 1 = rewritten into leaq sym(%rip) (local, PIE-safe) */
} mach_reloc_t;

/* ================================================================== */
/*  Helpers                                                            */
/* ================================================================== */
static size_t align_up (size_t v, size_t align) {
  return (v + align - 1) & ~(align - 1);
}

static void write_padding (int fd, size_t nbytes) {
  static const char zeros[16] = {0};
  while (nbytes > 0) {
    size_t n = nbytes < sizeof (zeros) ? nbytes : sizeof (zeros);
    write (fd, zeros, n);
    nbytes -= n;
  }
}

/* Simple name dedup set */
typedef struct { char **names; size_t n; size_t cap; } name_set_t;

static size_t name_set_find_or_add (name_set_t *s, const char *name) {
  for (size_t i = 0; i < s->n; i++)
    if (strcmp (s->names[i], name) == 0) return i;
  if (s->n >= s->cap) {
    s->cap = s->cap ? s->cap * 2 : 32;
    s->names = realloc (s->names, s->cap * sizeof (char *));
  }
  s->names[s->n] = strdup (name);
  return s->n++;
}

static int name_set_find (name_set_t *s, const char *name, size_t *idx) {
  for (size_t i = 0; i < s->n; i++)
    if (strcmp (s->names[i], name) == 0) { *idx = i; return 1; }
  return 0;
}

/* Map internal MIR builtin names to real, linkable symbols */
static const char *map_symbol (const char *name) {
  if (name == NULL) return name;
  static const struct { const char *from; const char *to; } map[] = {
    { "mir.arg_memcpy",   "memcpy" },
    { "mir.va_arg",       "va_arg_builtin" },
    { "mir.va_block_arg", "va_block_arg_builtin" },
    { "mir.ui2f",         "mir_aot_ui2f" },
    { "mir.ui2d",         "mir_aot_ui2d" },
    { "mir.ui2ld",        "mir_aot_ui2ld" },
    { "mir.ld2i",         "mir_aot_ld2i" },
    /* seq_cst atomic helpers (mir-gen-atomic.c builtins): mir-aot-runtime.c */
    { "mir.atomic_load",       "mir_aot_atomic_load" },
    { "mir.atomic_store",      "mir_aot_atomic_store" },
    { "mir.atomic_fence",      "mir_aot_atomic_fence" },
    { "mir.atomic_xchg",       "mir_aot_atomic_xchg" },
    { "mir.atomic_fetch_add",  "mir_aot_atomic_fetch_add" },
    { "mir.atomic_fetch_sub",  "mir_aot_atomic_fetch_sub" },
    { "mir.atomic_fetch_and",  "mir_aot_atomic_fetch_and" },
    { "mir.atomic_fetch_or",   "mir_aot_atomic_fetch_or" },
    { "mir.atomic_fetch_xor",  "mir_aot_atomic_fetch_xor" },
    { "mir.atomic_cas",        "mir_aot_atomic_cas" },
    { "mir.tls_addr",          "mir_tls_addr" },
    { "mir.tls_base",          "mir_tls_base" },
  };
  for (size_t i = 0; i < sizeof (map) / sizeof (map[0]); i++)
    if (strcmp (name, map[i].from) == 0) return map[i].to;
  return name;
}

/* ================================================================== */
/*  Mach-O x86-64 relocation type mapping                              */
/* ================================================================== */
/*
 * MIR code generator records relocations using ELF types:
 *   R_X86_64_PC32 (2) -> X86_64_RELOC_SIGNED      (PC-relative 32-bit disp)
 *   R_X86_64_64  (1) -> X86_64_RELOC_UNSIGNED     (absolute 64-bit)
 *
 * For PC-relative references to local (section) symbols, Mach-O uses
 * X86_64_RELOC_SIGNED with a section symbol and no external flag.
 * For external symbols, X86_64_RELOC_SIGNED with r_extern=1.
 * For absolute 64-bit, X86_64_RELOC_UNSIGNED with r_extern=1 for
 * external symbols, or r_extern=0 + section symbol for local.
 */

static const char *macho_mangle (const char *name) {
  if (name == NULL) return name;
  if (name[0] == '.') return name;
  /* [[registry]] anchors: the consumer references __start_cyreg_<NAME> /
     __stop_cyreg_<NAME>; Apple's linker synthesises section bounds under the
     magic names `section$start$<SEG>$<SECT>` / `section$end$...` (no leading
     underscore).  Rewrite so both the relocation and the undefined symbol use
     that spelling and ld resolves them to the merged cyreg_<NAME> section. */
  if (strncmp (name, "__start_cyreg_", 14) == 0) {
    const char *reg = name + 14;
    char *m = malloc (32 + strlen (reg));
    sprintf (m, "section$start$__DATA$cyreg_%s", reg);
    return m;
  }
  if (strncmp (name, "__stop_cyreg_", 13) == 0) {
    const char *reg = name + 13;
    char *m = malloc (32 + strlen (reg));
    sprintf (m, "section$end$__DATA$cyreg_%s", reg);
    return m;
  }
  size_t len = strlen (name);
  char *mangled = malloc (len + 2);
  mangled[0] = '_';
  memcpy (mangled + 1, name, len + 1);
  return mangled;
}

static int elf_reloc_to_macho (int elf_type) {
#if defined(__aarch64__)
  /* MIR stores arm64 Mach-O-oriented codes (see mir.h R_ARM64_*). */
  switch (elf_type) {
  case R_ARM64_UNSIGNED: return ARM64_RELOC_UNSIGNED;
  case R_ARM64_BRANCH26: return ARM64_RELOC_BRANCH26;
  case R_ARM64_PAGE21: return ARM64_RELOC_PAGE21;
  case R_ARM64_PAGEOFF12: return ARM64_RELOC_PAGEOFF12;
  case R_ARM64_GOT_LOAD_PAGE21: return ARM64_RELOC_GOT_LOAD_PAGE21;
  case R_ARM64_GOT_LOAD_PAGEOFF12: return ARM64_RELOC_GOT_LOAD_PAGEOFF12;
  case R_ARM64_TLVP_LOAD_PAGE21: return ARM64_RELOC_TLVP_LOAD_PAGE21;
  case R_ARM64_TLVP_LOAD_PAGEOFF12: return ARM64_RELOC_TLVP_LOAD_PAGEOFF12;
  case R_AARCH64_ABS64: return ARM64_RELOC_UNSIGNED;
  default:
    fprintf (stderr, "warning: unknown arm64 reloc type %d, treating as UNSIGNED\n", elf_type);
    return ARM64_RELOC_UNSIGNED;
  }
#else
  switch (elf_type) {
  case R_X86_64_PC32: return X86_64_RELOC_SIGNED;
  case R_X86_64_64:   return X86_64_RELOC_UNSIGNED;
  case R_X86_64_GOTPCREL: return X86_64_RELOC_GOT_LOAD; /* GOT-relative load */
  case R_X86_64_TLV: return X86_64_RELOC_TLV; /* thread-local variable */
  case 4: return 2; /* R_X86_64_PLT32 -> X86_64_RELOC_BRANCH */
  default:
    fprintf (stderr, "warning: unknown ELF reloc type %d, treating as SIGNED\n", elf_type);
    return X86_64_RELOC_SIGNED;
  }
#endif
}

/* ================================================================== */
/*  create_macho_object_file_from_module                               */
/*                                                                     */
/*  Walks all items in all modules, generates code, collects data/bss, */
/*  builds Mach-O sections, and writes a valid MH_OBJECT file.         */
/*                                                                     */
/*  Mach-O x86-64 object layout:                                       */
/*    mach_header_64                                                   */
/*    LC_SEGMENT_64   (pagezero - not needed for MH_OBJECT)             */
/*    LC_SEGMENT_64   (__TEXT segment)                                  */
/*    LC_SYMTAB                                                         */
/*    LC_DYSYMTAB                                                       */
/*    LC_VERSION_MIN_MACOSX  (10.12 compat)                             */
/*    __TEXT segment data:                                              */
/*      __text section                                                  */
/*    __DATA segment data:                                              */
/*      __data section                                                  */
/*      __bss section (S_NO_DATA)                                       */
/*    string table                                                      */
/*    symbol table (nlist_64)                                           */
/*    relocation entries                                                 */
/* ================================================================== */
static void create_macho_object_file_from_module (MIR_context_t ctx,
                                                   const char *output_file) {
  /* ----- Phase 0: arrays for collected items ----- */
  func_entry_t  *funcs  = NULL;  size_t n_funcs = 0, cap_funcs = 0;
  data_entry_t  *datas  = NULL;  size_t n_datas = 0, cap_datas = 0;
  bss_entry_t   *bsses  = NULL;  size_t n_bsses = 0, cap_bsses = 0;
  mach_reloc_t  *relocs = NULL;  size_t n_relocs = 0, cap_relocs = 0;
  name_set_t exports = {0};
  name_set_t imports = {0};

  /* ----- Phase 1a: Generate machine code for all functions ----- */
  DBG ("phase 1a: generating machine code for all functions");
  size_t gen_count = 0;
  for (MIR_module_t module = DLIST_HEAD (MIR_module_t, *MIR_get_module_list (ctx));
       module != NULL;
       module = DLIST_NEXT (MIR_module_t, module)) {
    for (MIR_item_t item = DLIST_HEAD (MIR_item_t, module->items);
         item != NULL;
         item = DLIST_NEXT (MIR_item_t, item)) {
      if (item->item_type == MIR_func_item) {
        MIR_gen (ctx, item);
        if ((++gen_count % 200) == 0)
          DBG ("  phase 1a: %zu functions generated", gen_count);
      }
    }
  }
  DBG ("phase 1a done: %zu functions", gen_count);

  /* ----- Phase 1b: Collect all items ----- */
  DBG ("phase 1b: collecting items");
  for (MIR_module_t module = DLIST_HEAD (MIR_module_t, *MIR_get_module_list (ctx));
       module != NULL;
       module = DLIST_NEXT (MIR_module_t, module)) {
    for (MIR_item_t item = DLIST_HEAD (MIR_item_t, module->items);
         item != NULL;
         item = DLIST_NEXT (MIR_item_t, item)) {
      switch (item->item_type) {
      case MIR_export_item:
        name_set_find_or_add (&exports, item->u.export_id);
        break;
      case MIR_import_item:
        name_set_find_or_add (&imports, map_symbol (item->u.import_id));
        break;
      case MIR_func_item: {
        MIR_func_t f = item->u.func;
        if (!f->machine_code || f->machine_code_len == 0) {
          fprintf (stderr, "warning: function '%s' produced no code\n", f->name);
          break;
        }
        if (n_funcs >= cap_funcs) {
          cap_funcs = cap_funcs ? cap_funcs * 2 : 16;
          funcs = realloc (funcs, cap_funcs * sizeof (func_entry_t));
        }
        func_entry_t *fe = &funcs[n_funcs++];
        fe->name = f->name;
        fe->code_len = f->machine_code_len;
        fe->code = malloc (fe->code_len);
        memcpy (fe->code, f->machine_code, fe->code_len);
        fe->text_offset = 0;
        fe->item = item;
        DBG ("  func: %s  code_len=%zu", f->name, fe->code_len);
        break;
      }
      case MIR_tls_data_item:
      case MIR_tls_bss_item:
        /* N2 Mach-O TLS: collected in phase 1c (__thread_data / __thread_vars). */
        DBG ("  tls item (native Mach-O TLV): %s", MIR_item_name (ctx, item));
        break;
      case MIR_data_item: {
        MIR_data_t d = item->u.data;
        size_t sz = d->nel * _MIR_type_size (ctx, d->el_type);
        if (sz == 0 && d->name == NULL) break;
        if (n_datas >= cap_datas) {
          cap_datas = cap_datas ? cap_datas * 2 : 32;
          datas = realloc (datas, cap_datas * sizeof (data_entry_t));
        }
        data_entry_t *de = &datas[n_datas++];
        de->name = d->name;
        de->size = sz;
        de->bytes = sz ? malloc (sz) : NULL;
        if (sz) memcpy (de->bytes, d->u.els, sz);
        de->data_offset = 0;
        de->is_ref_data = 0;
        de->ref_item = NULL;
        de->ref_disp = 0;
        de->item_addr = item->addr;
        DBG ("  data: %s  size=%zu  addr=%p",
             d->name ? d->name : "(anon)", sz, item->addr);
        break;
      }
      case MIR_ref_data_item: {
        MIR_ref_data_t rd = item->u.ref_data;
        if (n_datas >= cap_datas) {
          cap_datas = cap_datas ? cap_datas * 2 : 32;
          datas = realloc (datas, cap_datas * sizeof (data_entry_t));
        }
        data_entry_t *de = &datas[n_datas++];
        de->name = rd->name;
        de->size = 8;
        de->bytes = calloc (1, 8);
        de->data_offset = 0;
        de->is_ref_data = 1;
        de->ref_item = rd->ref_item;
        de->ref_disp = rd->disp;
        de->item_addr = item->addr;
        DBG ("  ref_data: %s -> %s + %ld",
             rd->name ? rd->name : "(anon)",
             MIR_item_name (ctx, rd->ref_item), (long)rd->disp);
        break;
      }
      case MIR_bss_item: {
        MIR_bss_t b = item->u.bss;
        if (n_bsses >= cap_bsses) {
          cap_bsses = cap_bsses ? cap_bsses * 2 : 16;
          bsses = realloc (bsses, cap_bsses * sizeof (bss_entry_t));
        }
        bss_entry_t *be = &bsses[n_bsses++];
        be->name = b->name;
        be->len = b->len;
        be->bss_offset = 0;
        be->item_addr = item->addr;
        if (b->name)
          DBG ("  bss: %s  len=%lu  addr=%p", b->name, (unsigned long)b->len, item->addr);
        break;
      }
      case MIR_forward_item:
      case MIR_proto_item:
      case MIR_lref_data_item:
      case MIR_expr_data_item:
        break;
      default:
        break;
      }
    }
  }

  DBG ("phase 1b done: %zu funcs, %zu datas, %zu bsses", n_funcs, n_datas, n_bsses);

  /* ----- Phase 1c: N1 emulated TLS for AOT -----
     Emit per-module template + strong `__mir_tls_aot_regs` so mir_tls_addr()
     bootstraps at first use (see mir-tls.c).  Code is lowered to mir_tls_addr
     before RA (safe for printf args).  No Mach-O TLV sections in this path. */
  typedef struct {
    uint32_t id;
    uint32_t size;
    char tmpl_name[64];
    uint8_t *tmpl;
  } tls_mod_t;
  tls_mod_t *tls_mods = NULL;
  size_t n_tls_mods = 0, cap_tls_mods = 0;
  /* Keep zero TLS section sizes so later layout code is a no-op. */
  typedef struct {
    const char *name;
    size_t size;
    size_t desc_offset;
    size_t init_offset;
    int is_bss;
    uint8_t *init_bytes;
  } tls_sym_t;
  tls_sym_t *tlss = NULL;
  size_t n_tlss = 0;
  uint8_t *tdata_buf = NULL;
  size_t tdata_size = 0, tbss_size = 0, tvars_size = 0;
  uint8_t *tvars_buf = NULL;
  {
    for (MIR_module_t module = DLIST_HEAD (MIR_module_t, *MIR_get_module_list (ctx));
         module != NULL; module = DLIST_NEXT (MIR_module_t, module)) {
      if (module->tls_module_id == 0 || module->tls_template == NULL || module->tls_size == 0)
        continue;
      if (n_tls_mods >= cap_tls_mods) {
        cap_tls_mods = cap_tls_mods ? cap_tls_mods * 2 : 4;
        tls_mods = realloc (tls_mods, cap_tls_mods * sizeof (tls_mod_t));
      }
      tls_mod_t *tm = &tls_mods[n_tls_mods++];
      tm->id = module->tls_module_id;
      tm->size = (uint32_t) module->tls_size;
      snprintf (tm->tmpl_name, sizeof (tm->tmpl_name), "mir_tls_tmpl_%u", tm->id);
      tm->tmpl = malloc (tm->size ? tm->size : 1);
      memcpy (tm->tmpl, module->tls_template, tm->size);
      DBG ("  tls module id=%u size=%u tmpl=%s", tm->id, tm->size, tm->tmpl_name);

      /* Append template as named __data */
      if (n_datas >= cap_datas) {
        cap_datas = cap_datas ? cap_datas * 2 : 32;
        datas = realloc (datas, cap_datas * sizeof (data_entry_t));
      }
      data_entry_t *de = &datas[n_datas++];
      de->name = strdup (tm->tmpl_name);
      de->size = tm->size;
      de->bytes = malloc (tm->size ? tm->size : 1);
      memcpy (de->bytes, tm->tmpl, tm->size);
      de->data_offset = 0;
      de->is_ref_data = 0;
      de->ref_item = NULL;
      de->ref_disp = 0;
      de->item_addr = NULL;
    }
    /* Build __mir_tls_aot_regs: {id, size, tmpl*}... {0,0,NULL} */
    if (n_tls_mods > 0) {
      size_t ent = 16; /* uint32 id, uint32 size, uint64 ptr */
      size_t tab_sz = (n_tls_mods + 1) * ent;
      uint8_t *tab = calloc (1, tab_sz);
      for (size_t i = 0; i < n_tls_mods; i++) {
        memcpy (tab + i * ent, &tls_mods[i].id, 4);
        memcpy (tab + i * ent + 4, &tls_mods[i].size, 4);
        /* pointer bytes left 0; reloc filled later via is_ref_data style */
      }
      if (n_datas >= cap_datas) {
        cap_datas = cap_datas ? cap_datas * 2 : 32;
        datas = realloc (datas, cap_datas * sizeof (data_entry_t));
      }
      data_entry_t *de = &datas[n_datas++];
      de->name = strdup ("__mir_tls_aot_regs");
      de->size = tab_sz;
      de->bytes = tab;
      de->data_offset = 0;
      de->is_ref_data = 0;
      de->ref_item = NULL;
      de->ref_disp = 0;
      de->item_addr = NULL;
      /* Stash module list for reloc emission after data offsets known */
      DBG ("phase 1c: __mir_tls_aot_regs %zu bytes, %zu modules", tab_sz, n_tls_mods);
    }
  }

  /* ----- Phase 2: assign offsets within sections ----- */
  DBG ("phase 2: assigning section offsets");

  /* __text: concatenate all function codes (16-byte aligned) */
  size_t text_size = 0;
  for (size_t i = 0; i < n_funcs; i++) {
    if (i > 0) text_size = align_up (text_size, 16);
    funcs[i].text_offset = text_size;
    text_size += funcs[i].code_len;
  }

  /* ----- [[registry("NAME")]] linker sets (Mach-O) -----
     Pull __cyreg_<NAME>__* ref_data out of __data into one
     (__DATA,cyreg_<NAME>) section per registry, holding the 8-byte pointers
     (relocated to the records).  The Apple linker then provides the magic
     `section$start$__DATA$cyreg_<NAME>` / `section$end$...` symbols; we rename
     the consumer's __start_/__stop_ references to those (see macho_mangle). */
  typedef struct {
    char name[128];
    uint8_t *buf; size_t size, cap;
    struct { size_t off; const char *sym; int64_t add; } *rel;
    size_t nrel, caprel;
    struct relocation_info *rbuf;   /* resolved relocation entries */
    size_t n_sect;        /* 1-based Mach-O section number */
    uint64_t addr;        /* vmaddr within the segment */
    size_t file_off, reloc_file_off;
  } cyreg_sec_t;
  cyreg_sec_t *cyr = NULL; size_t n_cyr = 0, cap_cyr = 0;
  struct { const char *name; size_t sec; size_t off; } *cyr_syms = NULL;
  size_t n_cyr_syms = 0, cap_cyr_syms = 0;
  {
    size_t w = 0;
    for (size_t i = 0; i < n_datas; i++) {
      const char *nm = datas[i].name;
      if (nm != NULL && datas[i].is_ref_data && strncmp (nm, "__cyreg_", 8) == 0) {
        const char *p = nm + 8;
        const char *e = strstr (p, "__");
        size_t klen = e ? (size_t)(e - p) : strlen (p);
        char key[128];
        if (klen >= sizeof key) klen = sizeof key - 1;
        memcpy (key, p, klen); key[klen] = 0;
        cyreg_sec_t *s = NULL;
        for (size_t k = 0; k < n_cyr; k++)
          if (strcmp (cyr[k].name, key) == 0) { s = &cyr[k]; break; }
        if (s == NULL) {
          if (n_cyr >= cap_cyr) { cap_cyr = cap_cyr ? cap_cyr*2 : 4;
            cyr = realloc (cyr, cap_cyr * sizeof *cyr); }
          s = &cyr[n_cyr++]; memset (s, 0, sizeof *s);
          snprintf (s->name, sizeof s->name, "%s", key);
        }
        size_t off = s->size;
        if (s->size + 8 > s->cap) { s->cap = s->cap ? s->cap*2 : 64;
          s->buf = realloc (s->buf, s->cap); }
        memset (s->buf + off, 0, 8); s->size += 8;
        const char *tgt = datas[i].ref_item
            ? macho_mangle (map_symbol (MIR_item_name (ctx, datas[i].ref_item))) : NULL;
        if (tgt != NULL) {
          if (s->nrel >= s->caprel) { s->caprel = s->caprel ? s->caprel*2 : 8;
            s->rel = realloc (s->rel, s->caprel * sizeof *s->rel); }
          s->rel[s->nrel].off = off; s->rel[s->nrel].sym = tgt;
          s->rel[s->nrel].add = datas[i].ref_disp; s->nrel++;
        }
        if (n_cyr_syms >= cap_cyr_syms) { cap_cyr_syms = cap_cyr_syms ? cap_cyr_syms*2 : 16;
          cyr_syms = realloc (cyr_syms, cap_cyr_syms * sizeof *cyr_syms); }
        cyr_syms[n_cyr_syms].name = nm;
        cyr_syms[n_cyr_syms].sec  = (size_t)(s - cyr);
        cyr_syms[n_cyr_syms].off  = off;
        n_cyr_syms++;
        free (datas[i].bytes);
      } else {
        datas[w++] = datas[i];
      }
    }
    n_datas = w;
  }

  /* __data: concatenate all data items (8-byte aligned) */
  size_t data_size = 0;
  for (size_t i = 0; i < n_datas; i++) {
    if (i > 0) data_size = align_up (data_size, 8);
    datas[i].data_offset = data_size;
    data_size += datas[i].size;
  }

  /* N1 AOT: relocate __mir_tls_aot_regs[i].tmpl → mir_tls_tmpl_<id> */
  for (size_t mi = 0; mi < n_tls_mods; mi++) {
    size_t regs_off = (size_t) -1;
    for (size_t i = 0; i < n_datas; i++) {
      if (datas[i].name && strcmp (datas[i].name, "__mir_tls_aot_regs") == 0) {
        regs_off = datas[i].data_offset;
        break;
      }
    }
    if (regs_off == (size_t) -1) break;
    char *tmpl_sym = (char *) macho_mangle (tls_mods[mi].tmpl_name);
    if (n_relocs >= cap_relocs) {
      cap_relocs = cap_relocs ? cap_relocs * 2 : 64;
      relocs = realloc (relocs, cap_relocs * sizeof (mach_reloc_t));
    }
    mach_reloc_t *mr = &relocs[n_relocs++];
    mr->offset = regs_off + mi * 16 + 8; /* ptr field */
    mr->symbol = tmpl_sym;
#if defined(__aarch64__)
    mr->type = R_ARM64_UNSIGNED;
#else
    mr->type = R_X86_64_64;
#endif
    mr->addend = 0;
    mr->in_data = 1;
    mr->is_movabs = 0;
    mr->is_got = 0;
    mr->is_lea = 0;
  }

  /* cyreg sections live in the segment right after __data (addresses ascend;
     __bss zerofill stays last).  Section numbers are known now; the vmaddrs
     depend on the FINAL text_size (Phase 2c may add branch stubs), so they are
     computed later (see "finalize cyreg/bss addresses").  n_cyr==0 leaves
     everything exactly as before. */
  /* Section numbering (1-based):
       1 __text
       2 __data
       3..2+n_cyr  cyreg_*
       then optional __thread_data, __thread_vars (Mach-O TLS, x64+arm64)
       then __bss last (zerofill)
  */
  size_t n_tls_sects = 0;
  if (tdata_size > 0) n_tls_sects++;
  if (tvars_size > 0) n_tls_sects++;
  if (tbss_size > 0) n_tls_sects++;
  for (size_t k = 0; k < n_cyr; k++) cyr[k].n_sect = 3 + k; /* __text=1,__data=2 */
  size_t sect_thread_data = 0, sect_thread_vars = 0, sect_thread_bss = 0;
  {
    size_t s = 3 + n_cyr;
    if (tdata_size > 0) sect_thread_data = s++;
    if (tvars_size > 0) sect_thread_vars = s++;
    if (tbss_size > 0) sect_thread_bss = s++;
  }
  size_t bss_sect = 3 + n_cyr + n_tls_sects; /* __bss is the last section */

  /* __bss: just total size (8-byte aligned per item) */
  size_t bss_size = 0;
  for (size_t i = 0; i < n_bsses; i++) {
    if (i > 0) bss_size = align_up (bss_size, 8);
    bsses[i].bss_offset = bss_size;
    bss_size += bsses[i].len;
  }
  size_t cyreg_total = 0;
  uint64_t bss_addr = 0;   /* both set after text_size is final */


  /* ----- Phase 2b: collect relocations from the code generator ----- */
  DBG ("phase 2b: collecting relocations from generator");

  for (size_t fi = 0; fi < n_funcs; fi++) {
    MIR_func_t f = funcs[fi].item->u.func;
    if (f->relocs == NULL) continue;
    size_t nr = VARR_LENGTH (MIR_code_reloc_t, f->relocs);
    for (size_t ri = 0; ri < nr; ri++) {
      MIR_code_reloc_t cr = VARR_GET (MIR_code_reloc_t, f->relocs, ri);
      if (cr.symbol == NULL) continue;
      if (n_relocs >= cap_relocs) {
        cap_relocs = cap_relocs ? cap_relocs * 2 : 64;
        relocs = realloc (relocs, cap_relocs * sizeof (mach_reloc_t));
      }
      mach_reloc_t *mr = &relocs[n_relocs++];
      mr->offset  = funcs[fi].text_offset + cr.offset;
      mr->symbol  = macho_mangle (map_symbol (cr.symbol));
      mr->type    = cr.type;
      mr->addend  = cr.addend;
      mr->in_data = 0;
      mr->is_got  = 0;
      mr->is_lea  = 0;
      /* Absolute 64-bit on a `movabs reg, imm64' is an address materialization.
       * On macOS __text must not hold absolute addresses (PIE): rewrite later to
       *   leaq  sym(%rip), reg     for local symbols  (e.g. .lc string literals)
       *   movq  sym@GOTPCREL(%rip) for external symbols
       */
      mr->is_movabs = 0;
#if !defined(__aarch64__)
      if (cr.type == R_X86_64_64 && cr.offset >= 2) {
        const uint8_t *code = (const uint8_t *) funcs[fi].code;
        uint8_t rex = code[cr.offset - 2];
        uint8_t op  = code[cr.offset - 1];
        if ((rex == 0x48 || rex == 0x49) && op >= 0xB8 && op <= 0xBF)
          mr->is_movabs = 1;
      }
#endif
    }
  }

  /* __data relocations from ref_data items */
  for (size_t i = 0; i < n_datas; i++) {
    if (!datas[i].is_ref_data) continue;
    const char *target_name = macho_mangle (map_symbol (MIR_item_name (ctx, datas[i].ref_item)));
    if (!target_name) continue;
    if (n_relocs >= cap_relocs) {
      cap_relocs = cap_relocs ? cap_relocs * 2 : 64;
      relocs = realloc (relocs, cap_relocs * sizeof (mach_reloc_t));
    }
    mach_reloc_t *mr = &relocs[n_relocs++];
    mr->offset  = datas[i].data_offset;
    mr->symbol  = target_name;
#if defined(__aarch64__)
    mr->type    = R_ARM64_UNSIGNED; /* absolute 64-bit pointer */
#else
    mr->type    = R_X86_64_64; /* absolute 64-bit */
#endif
    mr->addend  = datas[i].ref_disp;
    mr->in_data = 1;
    mr->is_movabs = 0;
    mr->is_got  = 0;
    mr->is_lea  = 0;
  }

  DBG ("phase 2b done: %zu relocations", n_relocs);


  /* ----- Phase 2c: generate stubs for external symbols to avoid text relocations ----- */
  typedef struct { const char *name; size_t stub_offset; } stub_t;
  stub_t *stubs = NULL;
  size_t n_stubs = 0, cap_stubs = 0;

  name_set_t defined_names = {0};
  for (size_t i = 0; i < n_funcs; i++) name_set_find_or_add (&defined_names, macho_mangle (funcs[i].name));
  for (size_t i = 0; i < n_datas; i++) if (datas[i].name) name_set_find_or_add (&defined_names, macho_mangle (datas[i].name));
  for (size_t i = 0; i < n_bsses; i++) if (bsses[i].name) name_set_find_or_add (&defined_names, macho_mangle (bsses[i].name));
  for (size_t i = 0; i < n_tlss; i++)
    if (tlss[i].name) name_set_find_or_add (&defined_names, macho_mangle (tlss[i].name));

  /*
   * Classify every __text absolute reference:
   *
   *   - movabs of a *local* symbol address (string .lc*, data, bss): rewrite to
   *     `leaq sym(%rip), reg' (PIE-safe).  Without this, printf formats are
   *     absolute addresses and print trash under modern macOS PIE.
   *
   *   - movabs of an *external* symbol (e.g. __stderrp): rewrite to
   *     `movq sym@GOTPCREL(%rip), reg'.
   *
   *   - Other external refs (calls via const pool): local branch stub (jmp/b).
   */
  size_t orig_n_relocs = n_relocs;
  for (size_t i = 0; i < orig_n_relocs; i++) {
    if (relocs[i].in_data) continue;
    size_t dummy;
    int local = name_set_find (&defined_names, relocs[i].symbol, &dummy);
#if !defined(__aarch64__)
    if (relocs[i].is_movabs) {
      if (local)
        relocs[i].is_lea = 1; /* leaq sym(%rip) */
      else
        relocs[i].is_got = 1; /* GOT load */
      continue;
    }
#endif
    if (!local) {
#if defined(__aarch64__)
      /* Only direct branches need PLT-style stubs.  PAGE/GOT/UNSIGNED address
         materialization is fixed up by the linker on the instruction itself. */
      if (relocs[i].type != R_ARM64_BRANCH26) continue;
#endif
      /* External call/jump target: synthesize a branch stub. */
      int found = 0;
      for (size_t j = 0; j < n_stubs; j++) {
        if (strcmp (stubs[j].name, relocs[i].symbol) == 0) { found = 1; break; }
      }
      if (!found) {
        if (n_stubs >= cap_stubs) {
          cap_stubs = cap_stubs ? cap_stubs * 2 : 16;
          stubs = realloc (stubs, cap_stubs * sizeof (stub_t));
        }
        stubs[n_stubs].name = relocs[i].symbol;
        stubs[n_stubs].stub_offset = text_size;
#if defined(__aarch64__)
        text_size += 4; /* b imm26 */
#else
        text_size += 5; /* jmp rel32 */
#endif
        n_stubs++;
      }
    }
  }

  /* Now update the original relocations to point to the stubs! */
  for (size_t i = 0; i < orig_n_relocs; i++) {
    if (relocs[i].in_data) continue;
    if (relocs[i].is_got) continue; /* handled via GOT, not a stub */
#if defined(__aarch64__)
    if (relocs[i].type != R_ARM64_BRANCH26) continue;
#endif
    size_t dummy;
    if (!name_set_find (&defined_names, relocs[i].symbol, &dummy)) {
      for (size_t j = 0; j < n_stubs; j++) {
        if (strcmp (stubs[j].name, relocs[i].symbol) == 0) {
          char stub_sym[256];
          snprintf (stub_sym, sizeof(stub_sym), "__mir_stub_%s", stubs[j].name);
          relocs[i].symbol = strdup (stub_sym);
          break;
        }
      }
    }
  }
  /* Build __text data buffer */
  uint8_t *text_buf = calloc (1, text_size ? text_size : 1);
  for (size_t i = 0; i < n_funcs; i++)
    memcpy (text_buf + funcs[i].text_offset, funcs[i].code, funcs[i].code_len);

#if !defined(__aarch64__)
  /* Rewrite movabs address loads (x86_64).  All stay 10 bytes (7 insn + 3 nops).
   *
   * External (is_got):
   *     48/4C 8B /r disp32     movq sym@GOTPCREL(%rip), reg
   * Local (is_lea) — string literals / local data (PIE-safe):
   *     48/4C 8D /r disp32     leaq sym(%rip), reg
   */
  for (size_t i = 0; i < orig_n_relocs; i++) {
    if (!relocs[i].is_got && !relocs[i].is_lea) continue;
    size_t imm = relocs[i].offset;   /* offset of the imm64 within __text */
    size_t istart = imm - 2;         /* REX prefix of the movabs */
    uint8_t rex = text_buf[istart];
    uint8_t op  = text_buf[istart + 1];
    int reg = (int) (((rex & 1) << 3) | (op - 0xB8)); /* dest register 0..15 */
    text_buf[istart]     = (uint8_t) (0x48 | ((reg >= 8) ? 0x04 : 0x00)); /* REX.W(+R) */
    text_buf[istart + 1] = relocs[i].is_lea ? 0x8D : 0x8B;              /* lea vs mov */
    text_buf[istart + 2] = (uint8_t) (((reg & 7) << 3) | 0x05);          /* mod=00 rm=RIP */
    memset (text_buf + istart + 3, 0, 4);                                /* disp32 (linker) */
    text_buf[istart + 7] = 0x90;
    text_buf[istart + 8] = 0x90;
    text_buf[istart + 9] = 0x90;
    relocs[i].offset = istart + 3;
    relocs[i].type   = relocs[i].is_lea ? R_X86_64_PC32 : R_X86_64_GOTPCREL;
    relocs[i].addend = 0;
  }
#endif

  /* Build __data data buffer */
  uint8_t *data_buf = calloc (1, data_size ? data_size : 1);
  for (size_t i = 0; i < n_datas; i++)
    if (datas[i].size) memcpy (data_buf + datas[i].data_offset, datas[i].bytes, datas[i].size);

  /* Write addends into section data for Mach-O (which uses REL, not RELA).
   * GOT-relative relocations keep their disp32 as zero (already written by the
   * rewrite above) - the linker fills it in. */
  for (size_t i = 0; i < orig_n_relocs; i++) {
    mach_reloc_t *mr = &relocs[i];
    uint8_t *buf = mr->in_data ? data_buf : text_buf;
#if defined(__aarch64__)
    if (mr->type == R_ARM64_UNSIGNED || mr->type == R_AARCH64_ABS64) {
      int64_t addend = mr->addend;
      memcpy (buf + mr->offset, &addend, 8);
    }
#else
    if (mr->type == R_X86_64_64) {
      int64_t addend = mr->addend;
      memcpy (buf + mr->offset, &addend, 8);
    } else if (mr->type == R_X86_64_PC32) {
      int32_t addend = (int32_t) mr->addend;
      memcpy (buf + mr->offset, &addend, 4);
    }
#endif
  }

  /* Write stubs and add branch relocations for them */
  for (size_t i = 0; i < n_stubs; i++) {
    size_t off = stubs[i].stub_offset;
#if defined(__aarch64__)
    uint32_t bimm = 0x14000000; /* b #0 */
    memcpy (text_buf + off, &bimm, 4);
#else
    text_buf[off] = 0xE9; /* jmp rel32 */
    memset (text_buf + off + 1, 0, 4);
#endif

    if (n_relocs >= cap_relocs) {
      cap_relocs = cap_relocs ? cap_relocs * 2 : 64;
      relocs = realloc (relocs, cap_relocs * sizeof (mach_reloc_t));
    }
    mach_reloc_t *mr = &relocs[n_relocs++];
#if defined(__aarch64__)
    mr->offset  = off;
    mr->symbol  = stubs[i].name;
    mr->type    = R_ARM64_BRANCH26;
#else
    mr->offset  = off + 1;
    mr->symbol  = stubs[i].name;
    mr->type    = 4; /* R_X86_64_PLT32 -> X86_64_RELOC_BRANCH */
#endif
    mr->addend  = 0;
    mr->in_data = 0;
    mr->is_movabs = 0;
    mr->is_got  = 0;
    mr->is_lea  = 0;
  }
  /* ----- Phase 3: build symbol table and string table ----- */
  DBG ("phase 3: building symbol table and string table");

  /*
   * Mach-O symbol table layout (nlist_64):
   *   - Local symbols first (N_STAB / N_SECT with non-external)
   *   - Then defined external symbols (N_SECT | N_EXT)
   *   - Then undefined external symbols (N_UNDF | N_EXT)
   *
   * The LC_DYSYMTAB load command indexes:
   *   ilocalsym   = 0
   *   nlocalsym   = count of local symbols
   *   iextdefsym  = nlocalsym
   *   nextdefsym  = count of defined external symbols
   *   iundefsym   = nlocalsym + nextdefsym
   *   nundefsym   = count of undefined external symbols
   */

  /* String table: build incrementally */
  char  *strtab = NULL;
  size_t strtab_size = 0, strtab_cap = 0;
  /* index 0 is always the empty string */
  strtab_size = 1;
  strtab_cap = 256;
  strtab = calloc (1, strtab_cap);

  /* Helper to add a string and return its offset */
  /* (we use 1-byte alignment for the string table) */
  #define STRTAB_ADD(s) ({                                          \
    size_t _len = strlen (s) + 1;                                  \
    while (strtab_size + _len > strtab_cap) {                       \
      strtab_cap *= 2;                                             \
      strtab = realloc (strtab, strtab_cap);                       \
    }                                                               \
    size_t _off = strtab_size;                                      \
    memcpy (strtab + _off, (s), _len);                             \
    strtab_size += _len;                                           \
    _off;                                                           \
  })

  /* Symbol table (nlist_64 array) */
  struct nlist_64 *symtab = NULL;
  size_t n_syms = 0, cap_syms = 0;

  #define SYMTAB_PUSH(s) do {                                       \
    if (n_syms >= cap_syms) {                                       \
      cap_syms = cap_syms ? cap_syms * 2 : 64;                     \
      symtab = realloc (symtab, cap_syms * sizeof (struct nlist_64)); \
    }                                                               \
    symtab[n_syms++] = (s);                                         \
  } while (0)

  /* We need a name -> symtab index map for relocation emission */
  typedef struct { const char *name; size_t idx; } sym_map_entry_t;
  sym_map_entry_t *sym_map = NULL;
  size_t n_sym_map = 0, cap_sym_map = 0;

  #define SYM_MAP_ADD(nm, ix) do {                                  \
    if (n_sym_map >= cap_sym_map) {                                 \
      cap_sym_map = cap_sym_map ? cap_sym_map * 2 : 64;            \
      sym_map = realloc (sym_map, cap_sym_map * sizeof (sym_map_entry_t)); \
    }                                                               \
    sym_map[n_sym_map].name = (nm);                                 \
    sym_map[n_sym_map].idx = (ix);                                  \
    n_sym_map++;                                                     \
  } while (0)

  /* Section indices for Mach-O:
   *   1 = __text  (section index 1 within __TEXT segment)
   *   2 = __data  (section index 1 within __DATA segment)
   *   3 = __bss   (section index 2 within __DATA segment)
   * Mach-O n_sect is 1-based.
   */
  enum {
    MACH_SECT_TEXT = 1,
    MACH_SECT_DATA = 2,
    MACH_SECT_BSS  = 3,
  };

  /* Finalize cyreg/TLS/bss addresses now that text_size is final (post-stubs). */
  uint64_t tdata_addr = 0, tvars_addr = 0, tbss_addr = 0;
  {
    uint64_t a = text_size + data_size;
    for (size_t k = 0; k < n_cyr; k++) {
      a = align_up (a, 8);
      cyr[k].addr = a;
      a += cyr[k].size;
    }
    cyreg_total = a - (text_size + data_size);
    if (tdata_size > 0) {
      a = align_up (a, 8);
      tdata_addr = a;
      a += tdata_size;
    }
    if (tvars_size > 0) {
      a = align_up (a, 8);
      tvars_addr = a;
      a += tvars_size;
    }
    if (tbss_size > 0) {
      a = align_up (a, 8);
      tbss_addr = a;
      a += tbss_size;
    }
    bss_addr = align_up (a, 8);
  }

  size_t n_local_syms = n_syms;

  for (size_t i = 0; i < n_funcs; i++) {
    const char *fname = funcs[i].name;
    size_t dummy;
    int is_exported = name_set_find (&exports, fname, &dummy);
    struct nlist_64 s = {0};
    const char *mname = macho_mangle (fname);
    s.n_un.n_strx = STRTAB_ADD (mname);
    /* ClassyC lowers class methods/ctors/dtors to free functions with mangled
       names containing `__`.  A class defined in a shared header is emitted by
       every TU that includes it, so the same symbol appears in several objects.
       Mark such definitions as `.weak_def_can_be_hidden` (N_WEAK_DEF + private
       extern): the linker coalesces the identical copies at LINK time (C++-
       inline / ODR) and demotes the survivor to a local symbol.  Crucially
       this avoids a runtime *weak bind* — a plain N_WEAK_DEF would make dyld try
       to bind the coalesced address into the function's __TEXT location, which
       is read-only ("BIND targets __TEXT which is not writable").  This is the
       Mach-O mirror of b2obj's STB_WEAK trick and the JIT's func-redef. */
    int weak_coal = (is_exported && strstr (fname, "__") != NULL);
    s.n_type = N_SECT | (is_exported ? N_EXT : 0) | (weak_coal ? N_PEXT : 0);
    s.n_sect = MACH_SECT_TEXT;
    s.n_desc = weak_coal ? N_WEAK_DEF : 0;
    s.n_value = funcs[i].text_offset;
    SYM_MAP_ADD (mname, n_syms);
    SYMTAB_PUSH (s);
  }

  /* --- Defined external symbols: named data items --- */
  for (size_t i = 0; i < n_datas; i++) {
    if (!datas[i].name) continue;
    int found = 0;
    for (size_t j = 0; j < n_sym_map; j++)
      if (strcmp (sym_map[j].name, datas[i].name) == 0) { found = 1; break; }
    if (found) continue;
    size_t dummy;
    int is_exported = name_set_find (&exports, datas[i].name, &dummy);
    /* Strong override of weak empty table in mir-tls.c (Mach-O: ___mir_tls_aot_regs). */
    if (strcmp (datas[i].name, "__mir_tls_aot_regs") == 0) is_exported = 1;
    struct nlist_64 s = {0};
    const char *mname = macho_mangle (datas[i].name);
    s.n_un.n_strx = STRTAB_ADD (mname);
    s.n_type = N_SECT | (is_exported ? N_EXT : 0);
    s.n_sect = MACH_SECT_DATA;
    s.n_desc = 0;
    s.n_value = text_size + datas[i].data_offset;
    SYM_MAP_ADD (mname, n_syms);
    SYMTAB_PUSH (s);
  }

  /* --- Defined external symbols: named BSS items --- */
  for (size_t i = 0; i < n_bsses; i++) {
    if (!bsses[i].name) continue;
    int found = 0;
    for (size_t j = 0; j < n_sym_map; j++)
      if (strcmp (sym_map[j].name, bsses[i].name) == 0) { found = 1; break; }
    if (found) continue;
    size_t dummy;
    int is_exported = name_set_find (&exports, bsses[i].name, &dummy);
    struct nlist_64 s = {0};
    const char *mname = macho_mangle (bsses[i].name);
    s.n_un.n_strx = STRTAB_ADD (mname);
    s.n_type = N_SECT | (is_exported ? N_EXT : 0);
    s.n_sect = (uint8_t) bss_sect;
    s.n_desc = 0;
    s.n_value = bss_addr + bsses[i].bss_offset;
    SYM_MAP_ADD (mname, n_syms);
    SYMTAB_PUSH (s);
  }

  /* --- TLS: variable symbol → descriptor in __thread_vars;
     name$tlv$init → storage in __thread_data / __thread_bss (clang layout). */
  for (size_t i = 0; i < n_tlss; i++) {
    if (!tlss[i].name || sect_thread_vars == 0) continue;
    size_t dummy;
    int is_exported = name_set_find (&exports, tlss[i].name, &dummy);
    struct nlist_64 s = {0};
    const char *mname = macho_mangle (tlss[i].name);
    s.n_un.n_strx = STRTAB_ADD (mname);
    s.n_type = N_SECT | (is_exported ? N_EXT : 0);
    s.n_sect = (uint8_t) sect_thread_vars;
    s.n_desc = 0;
    s.n_value = tvars_addr + tlss[i].desc_offset;
    SYM_MAP_ADD (mname, n_syms);
    SYMTAB_PUSH (s);

    char init_name[512];
    snprintf (init_name, sizeof (init_name), "%s$tlv$init", tlss[i].name);
    const char *minit = macho_mangle (init_name);
    struct nlist_64 si = {0};
    si.n_un.n_strx = STRTAB_ADD (minit);
    si.n_type = N_SECT; /* local, like clang */
    if (tlss[i].is_bss) {
      si.n_sect = (uint8_t) sect_thread_bss;
      si.n_value = tbss_addr + tlss[i].init_offset;
    } else {
      si.n_sect = (uint8_t) sect_thread_data;
      si.n_value = tdata_addr + tlss[i].init_offset;
    }
    si.n_desc = 0;
    SYM_MAP_ADD (minit, n_syms);
    SYMTAB_PUSH (si);
  }

  /* --- [[registry]] entry symbols: LOCAL pointers in their cyreg section --- */
  for (size_t i = 0; i < n_cyr_syms; i++) {
    struct nlist_64 s = {0};
    const char *mname = macho_mangle (cyr_syms[i].name);
    s.n_un.n_strx = STRTAB_ADD (mname);
    s.n_type = N_SECT;                 /* local (no N_EXT) */
    s.n_sect = (uint8_t) cyr[cyr_syms[i].sec].n_sect;
    s.n_desc = 0;
    s.n_value = cyr[cyr_syms[i].sec].addr + cyr_syms[i].off;
    SYMTAB_PUSH (s);
  }

  /* --- Defined local symbols: stubs --- */
  for (size_t i = 0; i < n_stubs; i++) {
    char stub_sym[256];
    snprintf (stub_sym, sizeof(stub_sym), "__mir_stub_%s", stubs[i].name);
    struct nlist_64 s = {0};
    s.n_un.n_strx = STRTAB_ADD (stub_sym);
    s.n_type = N_SECT; /* local symbol */
    s.n_sect = MACH_SECT_TEXT;
    s.n_desc = 0;
    s.n_value = stubs[i].stub_offset;
    SYM_MAP_ADD (strdup(stub_sym), n_syms);
    SYMTAB_PUSH (s);
  }
  /* --- Defined external symbols: functions --- */
  size_t n_extdef_syms = n_syms - n_local_syms;

  /* --- Undefined external symbols (imports) --- */
  for (size_t i = 0; i < imports.n; i++) {
    int found = 0;
    for (size_t j = 0; j < n_sym_map; j++)
      if (strcmp (sym_map[j].name, imports.names[i]) == 0) { found = 1; break; }
    if (found) continue;
    struct nlist_64 s = {0};
    s.n_un.n_strx = STRTAB_ADD (imports.names[i]);
    s.n_type = N_UNDF | N_EXT;
    s.n_sect = NO_SECT;
    s.n_desc = 0;
    s.n_value = 0;
    SYM_MAP_ADD (imports.names[i], n_syms);
    SYMTAB_PUSH (s);
  }

  /* Also ensure every reloc symbol that isn't yet in sym_map gets added as UNDEF */
  for (size_t i = 0; i < n_relocs; i++) {
    const char *rname = relocs[i].symbol;
    int found = 0;
    for (size_t j = 0; j < n_sym_map; j++)
      if (strcmp (sym_map[j].name, rname) == 0) { found = 1; break; }
    if (found) continue;
    struct nlist_64 s = {0};
    s.n_un.n_strx = STRTAB_ADD (rname);
    s.n_type = N_UNDF | N_EXT;
    s.n_sect = NO_SECT;
    s.n_desc = 0;
    s.n_value = 0;
    SYM_MAP_ADD (rname, n_syms);
    SYMTAB_PUSH (s);
  }

  size_t n_undef_syms = n_syms - n_local_syms - n_extdef_syms;

  /* --- Sort symtab: locals, then defined externals, then undefined --- */
  /* We built them in that order already, but non-exported func/data/bss
   * symbols are local (N_SECT without N_EXT) and were placed after the
   * section symbols. Let's partition properly. */
  {
    struct nlist_64 *sorted = calloc (n_syms, sizeof (struct nlist_64));
    size_t *old_to_new = calloc (n_syms, sizeof (size_t));
    size_t out = 0;

    /* Pass 1: locals (N_SECT without N_EXT, or any non-external) */
    for (size_t i = 0; i < n_syms; i++) {
      if ((symtab[i].n_type & N_EXT) == 0) {
        old_to_new[i] = out;
        sorted[out++] = symtab[i];
      }
    }
    n_local_syms = out;

    /* Pass 2: defined externals (N_SECT | N_EXT) */
    for (size_t i = 0; i < n_syms; i++) {
      if ((symtab[i].n_type & N_EXT) && (symtab[i].n_type & N_TYPE) == N_SECT) {
        old_to_new[i] = out;
        sorted[out++] = symtab[i];
      }
    }
    n_extdef_syms = out - n_local_syms;

    /* Pass 3: undefined (N_UNDF | N_EXT) */
    for (size_t i = 0; i < n_syms; i++) {
      if ((symtab[i].n_type & N_EXT) && (symtab[i].n_type & N_TYPE) == N_UNDF) {
        old_to_new[i] = out;
        sorted[out++] = symtab[i];
      }
    }
    n_undef_syms = out - n_local_syms - n_extdef_syms;

    /* Update sym_map indices */
    for (size_t i = 0; i < n_sym_map; i++)
      sym_map[i].idx = old_to_new[sym_map[i].idx];

    memcpy (symtab, sorted, n_syms * sizeof (struct nlist_64));
    free (sorted);
    free (old_to_new);
  }

  DBG ("phase 3 done: %zu symbols (local=%zu, extdef=%zu, undef=%zu)",
       n_syms, n_local_syms, n_extdef_syms, n_undef_syms);

  /* ----- Phase 4: build Mach-O relocation entries ----- */
  DBG ("phase 4: building Mach-O relocation entries");

  /* Separate text and data relocations */
  size_t n_reloc_text = 0, n_reloc_data = 0;
  for (size_t i = 0; i < n_relocs; i++) {
    if (relocs[i].in_data) n_reloc_data++; else n_reloc_text++;
  }

  struct relocation_info *reloc_text = calloc (n_reloc_text ? n_reloc_text : 1,
                                               sizeof (struct relocation_info));
  struct relocation_info *reloc_data = calloc (n_reloc_data ? n_reloc_data : 1,
                                               sizeof (struct relocation_info));
  size_t rt_idx = 0, rd_idx = 0;

  for (size_t i = 0; i < n_relocs; i++) {
    /* Find symbol index in sym_map */
    size_t sym_idx = 0;
    int found = 0;
    for (size_t j = 0; j < n_sym_map; j++) {
      if (strcmp (sym_map[j].name, relocs[i].symbol) == 0) {
        sym_idx = sym_map[j].idx;
        found = 1;
        break;
      }
    }

    int mach_type = elf_reloc_to_macho (relocs[i].type);

    /* Determine if the symbol is external (undefined or defined external) */
    int is_extern = 0;
    if (found && sym_idx < n_syms) {
      is_extern = (symtab[sym_idx].n_type & N_EXT) != 0;
    }

    /*
     * Mach-O scattered relocations are deprecated and problematic.
     * We always use external relocations for simplicity and correctness.
     * For local symbols, we still set r_extern=1 and let the linker
     * resolve via the symbol table. This is the approach used by Clang.
     */
    struct relocation_info ri;
    memset (&ri, 0, sizeof (ri));
    ri.r_address = (int32_t)relocs[i].offset;
    ri.r_symbolnum = sym_idx;
    ri.r_extern = 1;
    ri.r_type = mach_type;
#if defined(__aarch64__)
    /* arm64: most instruction relocs are 4-byte; UNSIGNED pointer is 8-byte. */
    ri.r_length = (mach_type == ARM64_RELOC_UNSIGNED) ? 3 : 2;
    ri.r_pcrel = (mach_type == ARM64_RELOC_BRANCH26
                  || mach_type == ARM64_RELOC_PAGE21
                  || mach_type == ARM64_RELOC_GOT_LOAD_PAGE21
                  || mach_type == ARM64_RELOC_TLVP_LOAD_PAGE21) ? 1 : 0;
#else
    ri.r_length = (mach_type == X86_64_RELOC_UNSIGNED) ? 3 : 2; /* 3=8byte, 2=4byte */
    /* PC-relative: SIGNED (incl. LEA from R_X86_64_PC32), BRANCH, GOT_LOAD, TLV */
    ri.r_pcrel = (mach_type == X86_64_RELOC_SIGNED
                  || mach_type == X86_64_RELOC_BRANCH
                  || mach_type == X86_64_RELOC_GOT_LOAD
                  || mach_type == X86_64_RELOC_TLV) ? 1 : 0;
#endif

    if (relocs[i].in_data)
      reloc_data[rd_idx++] = ri;
    else
      reloc_text[rt_idx++] = ri;
  }

  /* Resolve [[registry]] cyreg-section relocations (absolute 64-bit to each
     referenced record; r_extern=1 like the other data relocs). */
  for (size_t k = 0; k < n_cyr; k++) {
    cyr[k].rbuf = cyr[k].nrel ? calloc (cyr[k].nrel, sizeof (struct relocation_info)) : NULL;
    for (size_t r = 0; r < cyr[k].nrel; r++) {
      size_t sym_idx = 0;
      for (size_t j = 0; j < n_sym_map; j++)
        if (strcmp (sym_map[j].name, cyr[k].rel[r].sym) == 0) { sym_idx = sym_map[j].idx; break; }
      struct relocation_info ri; memset (&ri, 0, sizeof ri);
      ri.r_address = (int32_t) cyr[k].rel[r].off;
      ri.r_symbolnum = sym_idx;
      ri.r_extern = 1;
#if defined(__aarch64__)
      ri.r_type = ARM64_RELOC_UNSIGNED;
#else
      ri.r_type = X86_64_RELOC_UNSIGNED;
#endif
      ri.r_length = 3;
      ri.r_pcrel = 0;
      cyr[k].rbuf[r] = ri;
    }
  }

  DBG ("  __text relocs: %zu, __data relocs: %zu", n_reloc_text, n_reloc_data);

  /* ----- Phase 4b: DWARF + .debug_frame (x86_64 and aarch64) ----- */
  /* Always emit CFI so backtraces leave MIR frames; full DIE/line when -g.
     Shared with b2obj via dwarf-aot-emit.h (works on Intel 10.12 and arm64). */
#if !MIR_NO_DBINFO
  dwarf_aot_result_t dwarf;
  memset (&dwarf, 0, sizeof (dwarf));
  {
    dwarf_aot_func_t *df = calloc (n_funcs ? n_funcs : 1, sizeof (dwarf_aot_func_t));
    for (size_t i = 0; i < n_funcs; i++) {
      df[i].name = funcs[i].name;
      df[i].code = (const uint8_t *) funcs[i].code;
      df[i].code_len = funcs[i].code_len;
      df[i].text_offset = funcs[i].text_offset;
      df[i].func = funcs[i].item != NULL ? funcs[i].item->u.func : NULL;
    }
    dwarf_aot_emit (ctx, text_size, df, n_funcs, &dwarf);
    free (df);
    DBG ("phase 4b: DWARF info=%zu abbrev=%zu line=%zu str=%zu frame=%zu relocs=%zu",
         dwarf.info.len, dwarf.abbrev.len, dwarf.line.len, dwarf.str.len, dwarf.frame.len,
         dwarf.n_relocs);
  }
  int has_dwarf_dies = (dwarf.info.len > 0);
  int has_dwarf_frame = (dwarf.frame.len > 0);
#else
  int has_dwarf_dies = 0;
  int has_dwarf_frame = 0;
#endif

  /* ----- Phase 5: compute file layout and write Mach-O ----- */
  DBG ("phase 5: computing file layout and writing Mach-O");

  /*
   * Mach-O MH_OBJECT layout (matches clang on 10.12+):
   *
   *   [mach_header_64]
   *   [load commands: single LC_SEGMENT_64 (all sections), LC_SYMTAB,
   *                    LC_DYSYMTAB, LC_VERSION_MIN_MACOSX]
   *   [section data: __text / __data / cyreg / tls / __DWARF,__debug_*]
   *   [relocations]
   *   [symbol table (nlist_64)]
   *   [string table]
   *
   * ld rejects MH_OBJECT with more than one LC_SEGMENT_64.  DWARF sections
   * live in the same segment; each section_64 still names segname "__DWARF".
   * Dual-arch: x86_64 (10.12+) and aarch64; reloc types selected by #ifdef.
   */

  /* DWARF sections (always at least .debug_frame when we have code) */
  size_t n_dwarf_sects = 0;
  if (has_dwarf_dies) n_dwarf_sects += 4; /* info, abbrev, line, str */
  if (has_dwarf_frame) n_dwarf_sects += 1; /* frame */
  /* Sizes for layout (zero when no DWARF / MIR_NO_DBINFO). */
  size_t d_info_len = 0, d_abbrev_len = 0, d_line_len = 0, d_str_len = 0, d_frame_len = 0;
  size_t d_n_relocs = 0;
#if !MIR_NO_DBINFO
  d_info_len = dwarf.info.len;
  d_abbrev_len = dwarf.abbrev.len;
  d_line_len = dwarf.line.len;
  d_str_len = dwarf.str.len;
  d_frame_len = dwarf.frame.len;
  d_n_relocs = dwarf.n_relocs;
#endif

  /* Count load commands: one segment only (required for MH_OBJECT). */
  uint32_t ncmds = 4; /* LC_SEGMENT_64, LC_SYMTAB, LC_DYSYMTAB, LC_VERSION_MIN_MACOSX */
  size_t n_all_sects = 3 + n_cyr + n_tls_sects + n_dwarf_sects; /* + __DWARF,* */
  size_t sizeofcmds = sizeof (struct segment_command_64)
                     + sizeof (struct section_64) * n_all_sects
                     + sizeof (struct symtab_command)
                     + sizeof (struct dysymtab_command)
                     + sizeof (struct version_min_command);

  size_t header_size = sizeof (struct mach_header_64);
  size_t lc_end = header_size + sizeofcmds;

  /* Section data starts after the header + load commands, page-aligned */
  size_t text_file_off = align_up (lc_end, 16); /* 16-byte align for code */
  size_t data_file_off = text_file_off + text_size;
  if (data_size > 0) data_file_off = align_up (data_file_off, 8);

  /* cyreg section data follows __data (8-aligned each). */
  size_t cyreg_run = data_file_off + data_size;
  for (size_t k = 0; k < n_cyr; k++) {
    cyreg_run = align_up (cyreg_run, 8);
    cyr[k].file_off = cyreg_run;
    cyreg_run += cyr[k].size;
  }
  size_t cyreg_file_end = cyreg_run;

  /* Native TLS file content after cyreg (x64 + arm64) */
  size_t tdata_file_off = 0, tvars_file_off = 0;
  size_t tls_file_end = cyreg_file_end;
  if (tdata_size > 0) {
    tdata_file_off = align_up (tls_file_end, 8);
    tls_file_end = tdata_file_off + tdata_size;
  }
  if (tvars_size > 0) {
    tvars_file_off = align_up (tls_file_end, 8);
    tls_file_end = tvars_file_off + tvars_size;
  }

  /* DWARF section data follows TLS content (before relocations). */
  size_t dwarf_file_off = align_up (tls_file_end, 8);
  size_t dbg_info_off = 0, dbg_abbrev_off = 0, dbg_line_off = 0, dbg_str_off = 0, dbg_frame_off = 0;
  size_t dwarf_data_end = dwarf_file_off;
  if (n_dwarf_sects > 0) {
    size_t d = dwarf_file_off;
    if (has_dwarf_dies) {
      dbg_info_off = d; d += d_info_len;
      dbg_abbrev_off = d; d += d_abbrev_len;
      dbg_line_off = d; d += d_line_len;
      dbg_str_off = d; d += d_str_len;
    }
    if (has_dwarf_frame) {
      d = align_up (d, 8);
      dbg_frame_off = d; d += d_frame_len;
    }
    dwarf_data_end = d;
  }

  /* Relocation entries follow section data */
  size_t reloc_text_off = dwarf_data_end;
  size_t reloc_data_off = reloc_text_off + n_reloc_text * sizeof (struct relocation_info);

  /* cyreg relocations follow the __data relocations. */
  size_t cyreg_reloc_run = reloc_data_off + n_reloc_data * sizeof (struct relocation_info);
  for (size_t k = 0; k < n_cyr; k++) {
    cyr[k].reloc_file_off = cyreg_reloc_run;
    cyreg_reloc_run += cyr[k].nrel * sizeof (struct relocation_info);
  }

  /* __thread_vars: 2 UNSIGNED relocs per TLS symbol (bootstrap + $tlv$init). */
  size_t tvars_reloc_off = cyreg_reloc_run;
  size_t tvars_reloc_bytes = n_tlss * 2 * sizeof (struct relocation_info);
  size_t after_tvars_relocs = tvars_reloc_off + tvars_reloc_bytes;

  /* DWARF section relocs (UNSIGNED → __text section, r_extern=0). */
  size_t n_reloc_dinfo = 0, n_reloc_dline = 0, n_reloc_dframe = 0;
#if !MIR_NO_DBINFO
  for (size_t i = 0; i < dwarf.n_relocs; i++) {
    if (dwarf.relocs[i].sect == 0) n_reloc_dinfo++;
    else if (dwarf.relocs[i].sect == 1) n_reloc_dline++;
    else n_reloc_dframe++;
  }
#endif
  size_t dbg_reloc_info_off = after_tvars_relocs;
  size_t dbg_reloc_line_off = dbg_reloc_info_off + n_reloc_dinfo * sizeof (struct relocation_info);
  size_t dbg_reloc_frame_off = dbg_reloc_line_off + n_reloc_dline * sizeof (struct relocation_info);
  size_t after_dbg_relocs
    = dbg_reloc_frame_off + n_reloc_dframe * sizeof (struct relocation_info);

  /* Symbol table */
  size_t symtab_off = after_dbg_relocs;
  symtab_off = align_up (symtab_off, 8);

  /* String table */
  size_t strtab_off = symtab_off + n_syms * sizeof (struct nlist_64);

  /* Total file size */
  size_t file_size = strtab_off + strtab_size;

  /* ----- Build load commands ----- */

  /* DWARF section virtual addresses (after all non-debug content).
     Must not share an address with another section (even size-0 __bss) —
     old ld64 (10.12) asserts on atom counts when sections overlap. */
  size_t dwarf_vm_base = 0;
  {
    uint64_t vend = text_size + data_size;
    uint64_t ba = bss_addr ? bss_addr : ((text_size + data_size + 7) & ~(uint64_t) 7);
    uint64_t be = ba + bss_size;
    if (be > vend) vend = be;
    /* Empty bss still owns `ba`; start DWARF strictly after that point. */
    if (bss_size == 0 && ba >= vend) vend = ba + 8;
    if (tbss_size > 0 && tbss_addr + tbss_size > vend) vend = tbss_addr + tbss_size;
    if (tvars_size > 0 && tvars_addr + tvars_size > vend) vend = tvars_addr + tvars_size;
    if (tdata_size > 0 && tdata_addr + tdata_size > vend) vend = tdata_addr + tdata_size;
    dwarf_vm_base = (size_t) align_up ((size_t) vend, 8);
  }

  /* LC_SEGMENT_64: single segment for all sections (MH_OBJECT requirement). */
  struct segment_command_64 seg_cmd;
  memset (&seg_cmd, 0, sizeof (seg_cmd));
  seg_cmd.cmd = LC_SEGMENT_64;
  seg_cmd.cmdsize = (uint32_t) (sizeof (struct segment_command_64)
                                + n_all_sects * sizeof (struct section_64));
  /* Empty segname matches clang MH_OBJECT; sections carry their own names. */
  memset (seg_cmd.segname, 0, 16);
  seg_cmd.vmaddr = 0;
  /* vmsize must cover every section's addr+size (incl. zerofill + DWARF). */
  {
    uint64_t vend = dwarf_vm_base;
    if (n_dwarf_sects > 0) {
      size_t dsz = 0;
      if (has_dwarf_dies) dsz += d_info_len + d_abbrev_len + d_line_len + d_str_len;
      if (has_dwarf_frame) dsz += d_frame_len;
      vend += dsz;
    }
    seg_cmd.vmsize = vend;
  }
  seg_cmd.fileoff = text_file_off;
  /* filesize covers all non-zerofill section bytes (code/data/tls/DWARF). */
  seg_cmd.filesize = (n_dwarf_sects > 0 ? dwarf_data_end : tls_file_end) - text_file_off;
  seg_cmd.maxprot = 7;  /* rwx */
  seg_cmd.initprot = 7; /* rwx */
  seg_cmd.nsects = (uint32_t) n_all_sects;
  seg_cmd.flags = 0;

  /* Section 1: __TEXT,__text */
  struct section_64 sec_text;
  memset (&sec_text, 0, sizeof (sec_text));
  strncpy (sec_text.sectname, "__text", 16);
  strncpy (sec_text.segname, "__TEXT", 16);
  sec_text.addr = 0;
  sec_text.size = text_size;
  sec_text.offset = (uint32_t)text_file_off;
  sec_text.align = 4; /* 2^4 = 16-byte alignment */
  /* ld64 requires reloff==0 when nreloc==0. */
  sec_text.reloff = (uint32_t) (n_reloc_text ? reloc_text_off : 0);
  sec_text.nreloc = (uint32_t)n_reloc_text;
  sec_text.flags = S_REGULAR | S_ATTR_SOME_INSTRUCTIONS | S_ATTR_PURE_INSTRUCTIONS;
  sec_text.reserved1 = 0;
  sec_text.reserved2 = 0;
  sec_text.reserved3 = 0;

  /* Section 2: __DATA,__data */
  struct section_64 sec_data;
  memset (&sec_data, 0, sizeof (sec_data));
  strncpy (sec_data.sectname, "__data", 16);
  strncpy (sec_data.segname, "__DATA", 16);
  sec_data.addr = text_size;
  sec_data.size = data_size;
  sec_data.offset = (uint32_t)(data_size > 0 ? data_file_off : 0);
  sec_data.align = 3; /* 2^3 = 8-byte alignment */
  sec_data.reloff = (uint32_t)(n_reloc_data > 0 ? reloc_data_off : 0);
  sec_data.nreloc = (uint32_t)n_reloc_data;
  sec_data.flags = S_REGULAR;
  sec_data.reserved1 = 0;
  sec_data.reserved2 = 0;
  sec_data.reserved3 = 0;

  /* Section 3: __DATA,__bss */
  struct section_64 sec_bss;
  memset (&sec_bss, 0, sizeof (sec_bss));
  strncpy (sec_bss.sectname, "__bss", 16);
  strncpy (sec_bss.segname, "__DATA", 16);
  sec_bss.addr = bss_addr ? bss_addr : (text_size + data_size);
  sec_bss.size = bss_size;
  sec_bss.offset = 0; /* S_NO_DATA */
  sec_bss.align = 3;  /* 2^3 = 8-byte alignment */
  sec_bss.reloff = 0;
  sec_bss.nreloc = 0;
  sec_bss.flags = S_ZEROFILL;
  sec_bss.reserved1 = 0;
  sec_bss.reserved2 = 0;
  sec_bss.reserved3 = 0;

  /* __DWARF,* section headers (same LC_SEGMENT_64 as code/data). */
  struct section_64 sec_dinfo, sec_dabbrev, sec_dline, sec_dstr, sec_dframe;
  memset (&sec_dinfo, 0, sizeof (sec_dinfo));
  memset (&sec_dabbrev, 0, sizeof (sec_dabbrev));
  memset (&sec_dline, 0, sizeof (sec_dline));
  memset (&sec_dstr, 0, sizeof (sec_dstr));
  memset (&sec_dframe, 0, sizeof (sec_dframe));
  if (n_dwarf_sects > 0) {
    uint64_t daddr = dwarf_vm_base;
    if (has_dwarf_dies) {
      strncpy (sec_dinfo.sectname, "__debug_info", 16);
      strncpy (sec_dinfo.segname, "__DWARF", 16);
      sec_dinfo.addr = daddr;
      sec_dinfo.size = d_info_len;
      sec_dinfo.offset = (uint32_t) dbg_info_off;
      sec_dinfo.align = 0;
      sec_dinfo.reloff = (uint32_t) (n_reloc_dinfo ? dbg_reloc_info_off : 0);
      sec_dinfo.nreloc = (uint32_t) n_reloc_dinfo;
      sec_dinfo.flags = S_REGULAR | S_ATTR_DEBUG;
      daddr += d_info_len;
      strncpy (sec_dabbrev.sectname, "__debug_abbrev", 16);
      strncpy (sec_dabbrev.segname, "__DWARF", 16);
      sec_dabbrev.addr = daddr;
      sec_dabbrev.size = d_abbrev_len;
      sec_dabbrev.offset = (uint32_t) dbg_abbrev_off;
      sec_dabbrev.flags = S_REGULAR | S_ATTR_DEBUG;
      daddr += d_abbrev_len;
      strncpy (sec_dline.sectname, "__debug_line", 16);
      strncpy (sec_dline.segname, "__DWARF", 16);
      sec_dline.addr = daddr;
      sec_dline.size = d_line_len;
      sec_dline.offset = (uint32_t) dbg_line_off;
      sec_dline.reloff = (uint32_t) (n_reloc_dline ? dbg_reloc_line_off : 0);
      sec_dline.nreloc = (uint32_t) n_reloc_dline;
      sec_dline.flags = S_REGULAR | S_ATTR_DEBUG;
      daddr += d_line_len;
      strncpy (sec_dstr.sectname, "__debug_str", 16);
      strncpy (sec_dstr.segname, "__DWARF", 16);
      sec_dstr.addr = daddr;
      sec_dstr.size = d_str_len;
      sec_dstr.offset = (uint32_t) dbg_str_off;
      sec_dstr.flags = S_REGULAR | S_ATTR_DEBUG;
      daddr += d_str_len;
    }
    if (has_dwarf_frame) {
      strncpy (sec_dframe.sectname, "__debug_frame", 16);
      strncpy (sec_dframe.segname, "__DWARF", 16);
      sec_dframe.addr = daddr;
      sec_dframe.size = d_frame_len;
      sec_dframe.offset = (uint32_t) dbg_frame_off;
      sec_dframe.align = 3; /* 8-byte */
      sec_dframe.reloff = (uint32_t) (n_reloc_dframe ? dbg_reloc_frame_off : 0);
      sec_dframe.nreloc = (uint32_t) n_reloc_dframe;
      sec_dframe.flags = S_REGULAR | S_ATTR_DEBUG;
    }
  }

  /* LC_SYMTAB */
  struct symtab_command sym_cmd;
  memset (&sym_cmd, 0, sizeof (sym_cmd));
  sym_cmd.cmd = LC_SYMTAB;
  sym_cmd.cmdsize = sizeof (struct symtab_command);
  sym_cmd.symoff = (uint32_t)symtab_off;
  sym_cmd.nsyms = (uint32_t)n_syms;
  sym_cmd.stroff = (uint32_t)strtab_off;
  sym_cmd.strsize = (uint32_t)strtab_size;

  /* LC_DYSYMTAB */
  struct dysymtab_command dysym_cmd;
  memset (&dysym_cmd, 0, sizeof (dysym_cmd));
  dysym_cmd.cmd = LC_DYSYMTAB;
  dysym_cmd.cmdsize = sizeof (struct dysymtab_command);
  dysym_cmd.ilocalsym = 0;
  dysym_cmd.nlocalsym = (uint32_t)n_local_syms;
  dysym_cmd.iextdefsym = (uint32_t)n_local_syms;
  dysym_cmd.nextdefsym = (uint32_t)n_extdef_syms;
  dysym_cmd.iundefsym = (uint32_t)(n_local_syms + n_extdef_syms);
  dysym_cmd.nundefsym = (uint32_t)n_undef_syms;

  /* LC_VERSION_MIN_MACOSX (macOS 10.12 compatibility) */
  struct version_min_command ver_cmd;
  memset (&ver_cmd, 0, sizeof (ver_cmd));
  ver_cmd.cmd = LC_VERSION_MIN_MACOSX;
  ver_cmd.cmdsize = sizeof (struct version_min_command);
  ver_cmd.version = 0x000a0c00; /* 10.12.0 */
  ver_cmd.sdk = 0x000a0c00;     /* SDK 10.12 */

  /* Mach-O header */
  struct mach_header_64 mhdr;
  memset (&mhdr, 0, sizeof (mhdr));
  mhdr.magic = MH_MAGIC_64;
#if defined(__aarch64__)
  mhdr.cputype = CPU_TYPE_ARM64;
  mhdr.cpusubtype = CPU_SUBTYPE_ARM64_ALL;
#else
  mhdr.cputype = CPU_TYPE_X86_64;
  mhdr.cpusubtype = CPU_SUBTYPE_X86_64_ALL;
#endif
  mhdr.filetype = MH_OBJECT;
  mhdr.ncmds = ncmds;
  mhdr.sizeofcmds = (uint32_t)sizeofcmds;
  mhdr.flags = MH_SUBSECTIONS_VIA_SYMBOLS;

  /* ----- Write the file ----- */
  int fd = open (output_file, O_CREAT | O_WRONLY | O_TRUNC, 0644);
  if (fd < 0) {
    perror ("Failed to open output file");
    exit (EXIT_FAILURE);
  }

  /* Header */
  write (fd, &mhdr, sizeof (mhdr));

  /* Build cyreg section headers (between __data and __bss). */
  struct section_64 *cyreg_secs = n_cyr ? calloc (n_cyr, sizeof (struct section_64)) : NULL;
  for (size_t k = 0; k < n_cyr; k++) {
    struct section_64 *sc = &cyreg_secs[k];
    char sn[16]; memset (sn, 0, sizeof sn);
    snprintf (sn, sizeof sn, "cyreg_%s", cyr[k].name); /* Mach-O sectname: 16 bytes, not NUL-required */
    memcpy (sc->sectname, sn, 16 < strlen(sn) ? 16 : strlen(sn));
    strncpy (sc->segname, "__DATA", 16);
    sc->addr = cyr[k].addr;
    sc->size = cyr[k].size;
    sc->offset = (uint32_t) cyr[k].file_off;
    sc->align = 3; /* 8 bytes */
    sc->reloff = (uint32_t)(cyr[k].nrel ? cyr[k].reloc_file_off : 0);
    sc->nreloc = (uint32_t) cyr[k].nrel;
    sc->flags = S_REGULAR;
  }

  struct section_64 sec_tdata, sec_tvars;
  memset (&sec_tdata, 0, sizeof (sec_tdata));
  memset (&sec_tvars, 0, sizeof (sec_tvars));
  if (tdata_size > 0) {
    strncpy (sec_tdata.sectname, "__thread_data", 16);
    strncpy (sec_tdata.segname, "__DATA", 16);
    sec_tdata.addr = tdata_addr;
    sec_tdata.size = tdata_size;
    sec_tdata.offset = (uint32_t) tdata_file_off;
    sec_tdata.align = 3;
    sec_tdata.flags = S_THREAD_LOCAL_REGULAR;
  }
  if (tvars_size > 0) {
    strncpy (sec_tvars.sectname, "__thread_vars", 16);
    strncpy (sec_tvars.segname, "__DATA", 16);
    sec_tvars.addr = tvars_addr;
    sec_tvars.size = tvars_size;
    sec_tvars.offset = (uint32_t) tvars_file_off;
    sec_tvars.align = 3;
    sec_tvars.flags = S_THREAD_LOCAL_VARIABLES;
    sec_tvars.nreloc = (uint32_t) (n_tlss * 2);
    sec_tvars.reloff = (uint32_t) (n_tlss ? tvars_reloc_off : 0);
  }
  struct section_64 sec_tbss;
  memset (&sec_tbss, 0, sizeof (sec_tbss));
  if (tbss_size > 0) {
    strncpy (sec_tbss.sectname, "__thread_bss", 16);
    strncpy (sec_tbss.segname, "__DATA", 16);
    sec_tbss.addr = tbss_addr;
    sec_tbss.size = tbss_size;
    sec_tbss.offset = 0; /* zerofill */
    sec_tbss.align = 3;
    sec_tbss.flags = S_THREAD_LOCAL_ZEROFILL;
  }

  /* Load commands: one LC_SEGMENT_64 with all section headers, then symtab cmds. */
  write (fd, &seg_cmd, sizeof (seg_cmd));
  write (fd, &sec_text, sizeof (sec_text));
  write (fd, &sec_data, sizeof (sec_data));
  for (size_t k = 0; k < n_cyr; k++) write (fd, &cyreg_secs[k], sizeof (struct section_64));
  if (tdata_size > 0) write (fd, &sec_tdata, sizeof (sec_tdata));
  if (tvars_size > 0) write (fd, &sec_tvars, sizeof (sec_tvars));
  if (tbss_size > 0) write (fd, &sec_tbss, sizeof (sec_tbss));
  write (fd, &sec_bss, sizeof (sec_bss));
  if (n_dwarf_sects > 0) {
    if (has_dwarf_dies) {
      write (fd, &sec_dinfo, sizeof (sec_dinfo));
      write (fd, &sec_dabbrev, sizeof (sec_dabbrev));
      write (fd, &sec_dline, sizeof (sec_dline));
      write (fd, &sec_dstr, sizeof (sec_dstr));
    }
    if (has_dwarf_frame) write (fd, &sec_dframe, sizeof (sec_dframe));
  }
  write (fd, &sym_cmd, sizeof (sym_cmd));
  write (fd, &dysym_cmd, sizeof (dysym_cmd));
  write (fd, &ver_cmd, sizeof (ver_cmd));

  /* Padding to __text section */
  if (text_file_off > lc_end)
    write_padding (fd, text_file_off - lc_end);

  /* __text section data */
  if (text_size) write (fd, text_buf, text_size);

  /* Padding to __data section */
  if (data_size > 0) {
    size_t cur = text_file_off + text_size;
    if (data_file_off > cur) write_padding (fd, data_file_off - cur);
    write (fd, data_buf, data_size);
  }

  /* cyreg section data (pointer arrays) */
  {
    size_t cur = data_file_off + data_size;
    for (size_t k = 0; k < n_cyr; k++) {
      if (cyr[k].file_off > cur) write_padding (fd, cyr[k].file_off - cur);
      if (cyr[k].size) write (fd, cyr[k].buf, cyr[k].size);
      cur = cyr[k].file_off + cyr[k].size;
    }
    if (tdata_size > 0) {
      if (tdata_file_off > cur) write_padding (fd, tdata_file_off - cur);
      write (fd, tdata_buf, tdata_size);
      cur = tdata_file_off + tdata_size;
    }
    if (tvars_size > 0) {
      if (tvars_file_off > cur) write_padding (fd, tvars_file_off - cur);
      write (fd, tvars_buf, tvars_size);
      cur = tvars_file_off + tvars_size;
    }
    /* Patch DWARF absolute address slots with text addends, then write. */
#if !MIR_NO_DBINFO
    for (size_t i = 0; i < dwarf.n_relocs; i++) {
      uint64_t add = (uint64_t) dwarf.relocs[i].addend;
      size_t off = dwarf.relocs[i].offset;
      uint8_t *base = NULL;
      size_t blen = 0;
      if (dwarf.relocs[i].sect == 0) {
        base = dwarf.info.data; blen = dwarf.info.len;
      } else if (dwarf.relocs[i].sect == 1) {
        base = dwarf.line.data; blen = dwarf.line.len;
      } else {
        base = dwarf.frame.data; blen = dwarf.frame.len;
      }
      if (base != NULL && off + 8 <= blen) memcpy (base + off, &add, 8);
    }
#endif
#if !MIR_NO_DBINFO
    /* Keep `cur` in sync with the file position — a stale cur caused a second
       pad before __debug_frame (6 bytes when data ended unaligned), shifting
       frame bytes and all following reloc/symtab data so ld64 saw garbage. */
    if (n_dwarf_sects > 0 && dwarf_file_off > cur) {
      write_padding (fd, dwarf_file_off - cur);
      cur = dwarf_file_off;
    }
    if (has_dwarf_dies) {
      if (d_info_len) write (fd, dwarf.info.data, d_info_len);
      if (d_abbrev_len) write (fd, dwarf.abbrev.data, d_abbrev_len);
      if (d_line_len) write (fd, dwarf.line.data, d_line_len);
      if (d_str_len) write (fd, dwarf.str.data, d_str_len);
      cur = dbg_str_off + d_str_len;
    }
    if (has_dwarf_frame) {
      if (dbg_frame_off > cur) {
        write_padding (fd, dbg_frame_off - cur);
        cur = dbg_frame_off;
      }
      write (fd, dwarf.frame.data, d_frame_len);
      cur = dbg_frame_off + d_frame_len;
    }
#endif
    (void) cur;
  }

  /* __text relocation entries */
  if (n_reloc_text)
    write (fd, reloc_text, n_reloc_text * sizeof (struct relocation_info));

  /* __data relocation entries */
  if (n_reloc_data)
    write (fd, reloc_data, n_reloc_data * sizeof (struct relocation_info));

  /* cyreg section relocation entries */
  for (size_t k = 0; k < n_cyr; k++)
    if (cyr[k].nrel) write (fd, cyr[k].rbuf, cyr[k].nrel * sizeof (struct relocation_info));

  /* __thread_vars relocs (clang): per descriptor
       +0  UNSIGNED → __tlv_bootstrap
       +16 UNSIGNED → name$tlv$init */
  if (n_tlss > 0) {
    size_t boot_idx = 0;
    for (size_t j = 0; j < n_sym_map; j++) {
      if (strcmp (sym_map[j].name, "__tlv_bootstrap") == 0
          || strcmp (sym_map[j].name, "___tlv_bootstrap") == 0) {
        boot_idx = sym_map[j].idx;
        break;
      }
    }
#if defined(__aarch64__)
    const int abs_reloc = ARM64_RELOC_UNSIGNED;
#else
    const int abs_reloc = X86_64_RELOC_UNSIGNED;
#endif
    for (size_t i = 0; i < n_tlss; i++) {
      struct relocation_info ri;
      memset (&ri, 0, sizeof (ri));
      ri.r_address = (int32_t) tlss[i].desc_offset;
      ri.r_symbolnum = boot_idx;
      ri.r_extern = 1;
      ri.r_type = abs_reloc;
      ri.r_length = 3;
      ri.r_pcrel = 0;
      write (fd, &ri, sizeof (ri));

      char init_name[512];
      snprintf (init_name, sizeof (init_name), "%s$tlv$init", tlss[i].name);
      const char *minit = macho_mangle (init_name);
      size_t init_idx = 0;
      for (size_t j = 0; j < n_sym_map; j++) {
        if (strcmp (sym_map[j].name, minit) == 0) {
          init_idx = sym_map[j].idx;
          break;
        }
      }
      memset (&ri, 0, sizeof (ri));
      ri.r_address = (int32_t) (tlss[i].desc_offset + 16);
      ri.r_symbolnum = init_idx;
      ri.r_extern = 1;
      ri.r_type = abs_reloc;
      ri.r_length = 3;
      ri.r_pcrel = 0;
      write (fd, &ri, sizeof (ri));
    }
  }

  /* DWARF section relocations: section-relative UNSIGNED against __text (sect 1).
     Emit grouped by section (info, line, frame) so reloff/nreloc match headers.
     Works on both x86_64 (10.12+) and aarch64. */
#if !MIR_NO_DBINFO
  if (n_reloc_dinfo + n_reloc_dline + n_reloc_dframe > 0) {
#if defined(__aarch64__)
    const int abs_r = ARM64_RELOC_UNSIGNED;
#else
    const int abs_r = X86_64_RELOC_UNSIGNED;
#endif
    for (int want = 0; want <= 2; want++) {
      for (size_t i = 0; i < dwarf.n_relocs; i++) {
        if (dwarf.relocs[i].sect != want) continue;
        struct relocation_info ri;
        memset (&ri, 0, sizeof (ri));
        ri.r_address = (int32_t) dwarf.relocs[i].offset;
        ri.r_symbolnum = 1; /* __text is first section (1-based) */
        ri.r_extern = 0;
        ri.r_type = abs_r;
        ri.r_length = 3; /* 8 bytes */
        ri.r_pcrel = 0;
        write (fd, &ri, sizeof (ri));
      }
    }
  }
#endif

  /* Padding to symbol table */
  {
    size_t cur = after_dbg_relocs;
    if (symtab_off > cur) write_padding (fd, symtab_off - cur);
  }

  /* Symbol table */
  write (fd, symtab, n_syms * sizeof (struct nlist_64));

  /* String table */
  write (fd, strtab, strtab_size);

  close (fd);

  DBG ("wrote Mach-O object: %s", output_file);
  DBG ("  __text:  %zu bytes, %zu functions", text_size, n_funcs);
  DBG ("  __data:  %zu bytes, %zu items", data_size, n_datas);
  DBG ("  __bss:   %zu bytes, %zu items", bss_size, n_bsses);
  DBG ("  __text relocs: %zu entries", n_reloc_text);
  DBG ("  __data relocs: %zu entries", n_reloc_data);
  DBG ("  DWARF: info=%zu frame=%zu relocs=%zu", d_info_len, d_frame_len, d_n_relocs);
  DBG ("  symtab: %zu symbols (local=%zu, extdef=%zu, undef=%zu)",
       n_syms, n_local_syms, n_extdef_syms, n_undef_syms);

  /* ----- Cleanup ----- */
  free (text_buf);
  free (data_buf);
  free (reloc_text);
  free (reloc_data);
  free (symtab);
  free (strtab);
  free (sym_map);
  for (size_t k = 0; k < n_cyr; k++) { free (cyr[k].buf); free (cyr[k].rel); free (cyr[k].rbuf); }
  free (cyr);
  free (cyr_syms);
  free (cyreg_secs);
  for (size_t i = 0; i < n_funcs; i++) free (funcs[i].code);
  free (funcs);
  for (size_t i = 0; i < n_datas; i++) free (datas[i].bytes);
  free (datas);
  free (bsses);
  free (relocs);
  free (tdata_buf);
  free (tvars_buf);
  for (size_t i = 0; i < n_tlss; i++) free (tlss[i].init_bytes);
  free (tlss);
  for (size_t i = 0; i < n_tls_mods; i++) free (tls_mods[i].tmpl);
  free (tls_mods);
  for (size_t i = 0; i < exports.n; i++) free (exports.names[i]);
  free (exports.names);
  for (size_t i = 0; i < imports.n; i++) free (imports.names[i]);
  free (imports.names);
#if !MIR_NO_DBINFO
  dwarf_aot_result_free (&dwarf);
#endif

  #undef SYMTAB_PUSH
  #undef SYM_MAP_ADD
  #undef STRTAB_ADD
}

/* ================================================================== */
/*  main                                                               */
/* ================================================================== */
int main (int argc, char **argv) {
  MIR_alloc_t alloc = &default_alloc;
  int opt_level = -1;  /* -1: not given on the command line */
  int argi = 1;

  /* Optional -O0..-O3 code-generation optimisation level.  Overrides the
     B2OBJ_OPT env var when given. */
  if (argi < argc && strncmp (argv[argi], "-O", 2) == 0) {
    const char *p = argv[argi] + 2;
    if (p[0] >= '0' && p[0] <= '3' && p[1] == '\0') {
      opt_level = p[0] - '0';
      argi++;
    } else {
      fprintf (stderr, "%s: invalid optimisation level '%s' (use -O0, -O1, -O2 or -O3)\n",
               argv[0], argv[argi]);
      return EXIT_FAILURE;
    }
  }

  if (argc - argi != 2) {
    fprintf (stderr, "Usage: %s [-O0|-O1|-O2|-O3] <mir_input> <object_file>\n", argv[0]);
    return EXIT_FAILURE;
  }

  VARR_CREATE (char, temp_string, alloc, 0);
  VARR_CREATE (lib_t, extra_libs, alloc, 16);
  VARR_CREATE (char_ptr_t, lib_dirs, alloc, 16);
  for (int i = 0; i < (int)(sizeof (std_lib_dirs) / sizeof (char_ptr_t)); i++)
    VARR_PUSH (char_ptr_t, lib_dirs, std_lib_dirs[i]);
  lib_dirs_from_env_var ("DYLD_LIBRARY_PATH");
  lib_dirs_from_env_var (MIR_ENV_VAR_LIB_DIRS);

  const char *mir_input_file = argv[argi];
  const char *output_file = argv[argi + 1];

  MIR_context_t ctx = MIR_init ();

  FILE *fp = fopen (mir_input_file, "r");
  if (!fp) {
    perror ("Failed to open MIR input file");
    return EXIT_FAILURE;
  }

  DBG ("reading MIR from %s", mir_input_file);
  MIR_read (ctx, fp);
  fclose (fp);
  DBG ("MIR_read done");

  /* macOS AOT uses N1 emulated TLS (mir_tls_addr) so RA sees the helper call
     and does not clobber live args (e.g. printf format in %rdi).  Native
     Mach-O TLV (post-RA call *thunk) is unsafe until lowered pre-RA.
     JIT also uses emulated TLS.  We emit __mir_tls_aot_regs + template below. */
  MIR_set_tls_native_aot (ctx, 0);

  /* Load all modules */
  size_t n_modules = 0, n_funcs_total = 0;
  for (MIR_module_t module = DLIST_HEAD (MIR_module_t, *MIR_get_module_list (ctx));
       module != NULL;
       module = DLIST_NEXT (MIR_module_t, module)) {
    n_modules++;
    for (MIR_item_t item = DLIST_HEAD (MIR_item_t, module->items);
         item != NULL;
         item = DLIST_NEXT (MIR_item_t, item)) {
      if (item->item_type == MIR_func_item)
        n_funcs_total++;
    }
    MIR_load_module (ctx, module);
  }
  DBG ("loaded %zu module(s), %zu function(s) total", n_modules, n_funcs_total);

  open_std_libs ();
  open_extra_libs ();
  DBG ("opened libraries");

  /* Initialize code generator and link */
  MIR_gen_init (ctx);
  MIR_gen_set_save_relocs (ctx, 1);
  {
    /* Optimisation level for code generation; default 1.  Override with the
       B2OBJ_OPT env var or the -O0..-O3 command-line option (which takes
       precedence). */
    const char *opt = getenv ("B2OBJ_OPT");
    int level = opt_level >= 0 ? opt_level : (opt != NULL ? atoi (opt) : 1);
    MIR_gen_set_optimize_level (ctx, (unsigned)level);
    DBG ("optimize level = %d", level);
  }
  DBG ("starting MIR_link (eager code generation of all functions)");
  MIR_link (ctx, MIR_set_gen_interface, hybrid_import_resolver);
  DBG ("MIR_link done (all functions generated)");

  /* Generate code for all functions and write the Mach-O object */
  create_macho_object_file_from_module (ctx, output_file);
  DBG ("create_macho_object_file_from_module done");

  MIR_gen_finish (ctx);

  printf ("Mach-O object file '%s' created successfully.\n", output_file);

  close_extra_libs ();
  close_std_libs ();
  MIR_finish (ctx);

  return EXIT_SUCCESS;
}
