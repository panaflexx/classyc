#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <alloca.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <elf.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <time.h>
#include "mir-alloc-default.c"
#include "mir-gen.h"  // mir.h gets included as well
#include "dwarf-gen.h"

/* Debug tracing: enabled when B2OBJ_DEBUG is set in the environment. */
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
        fprintf (stderr, "[b2obj +%7.3fs] ", b2obj_now () - b2obj_t0); \
        fprintf (stderr, __VA_ARGS__);                           \
        fputc ('\n', stderr);                                    \
        fflush (stderr);                                         \
    }                                                            \
} while (0)

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

struct lib {
  char *name;
  void *handler;
};
typedef struct lib lib_t;

/* stdlibs according to c2mir */
#if defined(__unix__)
#if UINTPTR_MAX == 0xffffffff
static lib_t std_libs[]
  = {{"/lib/libc.so.6", NULL},   {"/lib32/libc.so.6", NULL},     {"/lib/libm.so.6", NULL},
     {"/lib32/libm.so.6", NULL}, {"/lib/libpthread.so.0", NULL}, {"/lib32/libpthread.so.0", NULL}};
static const char *std_lib_dirs[] = {"/lib", "/lib32"};
#elif UINTPTR_MAX == 0xffffffffffffffff
#if defined(__x86_64__)
static lib_t std_libs[] = {{"/lib64/libc.so.6", NULL},
                           {"/lib/x86_64-linux-gnu/libc.so.6", NULL},
                           {"/lib64/libm.so.6", NULL},
                           {"/lib/x86_64-linux-gnu/libm.so.6", NULL},
                           {"/usr/lib64/libpthread.so.0", NULL},
                           {"/lib/x86_64-linux-gnu/libpthread.so.0", NULL},
                           {"/usr/lib/libc.so", NULL}};
static const char *std_lib_dirs[] = {"/lib64", "/lib/x86_64-linux-gnu"};
#elif (__aarch64__)
static lib_t std_libs[]
  = {{"/lib64/libc.so.6", NULL},       {"/lib/aarch64-linux-gnu/libc.so.6", NULL},
     {"/lib64/libm.so.6", NULL},       {"/lib/aarch64-linux-gnu/libm.so.6", NULL},
     {"/lib64/libpthread.so.0", NULL}, {"/lib/aarch64-linux-gnu/libpthread.so.0", NULL}};
static const char *std_lib_dirs[] = {"/lib64", "/lib/aarch64-linux-gnu"};
#elif (__PPC64__)
static lib_t std_libs[] = {
  {"/lib64/libc.so.6", NULL},
  {"/lib64/libm.so.6", NULL},
  {"/lib64/libpthread.so.0", NULL},
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  {"/lib/powerpc64le-linux-gnu/libc.so.6", NULL},
  {"/lib/powerpc64le-linux-gnu/libm.so.6", NULL},
  {"/lib/powerpc64le-linux-gnu/libpthread.so.0", NULL},
#else
  {"/lib/powerpc64-linux-gnu/libc.so.6", NULL},
  {"/lib/powerpc64-linux-gnu/libm.so.6", NULL},
  {"/lib/powerpc64-linux-gnu/libpthread.so.0", NULL},
#endif
};
static const char *std_lib_dirs[] = {
  "/lib64",
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  "/lib/powerpc64le-linux-gnu",
#else
  "/lib/powerpc64-linux-gnu",
#endif
};
#elif (__s390x__)
static lib_t std_libs[]
  = {{"/lib64/libc.so.6", NULL},       {"/lib/s390x-linux-gnu/libc.so.6", NULL},
     {"/lib64/libm.so.6", NULL},       {"/lib/s390x-linux-gnu/libm.so.6", NULL},
     {"/lib64/libpthread.so.0", NULL}, {"/lib/s390x-linux-gnu/libpthread.so.0", NULL}};
static const char *std_lib_dirs[] = {"/lib64", "/lib/s390x-linux-gnu"};
#elif (__riscv)
static lib_t std_libs[]
  = {{"/lib64/libc.so.6", NULL},       {"/lib/riscv64-linux-gnu/libc.so.6", NULL},
     {"/lib64/libm.so.6", NULL},       {"/lib/riscv64-linux-gnu/libm.so.6", NULL},
     {"/lib64/libpthread.so.0", NULL}, {"/lib/riscv64-linux-gnu/libpthread.so.0", NULL}};
static const char *std_lib_dirs[] = {"/lib64", "/lib/riscv64-linux-gnu"};
#else
#error cannot recognize 32- or 64-bit target
#endif
#endif
static const char *lib_suffix = ".so";
#endif

#ifdef _WIN32
static const int slash = '\\';
#else
static const int slash = '/';
#endif

#if defined(__APPLE__)
static lib_t std_libs[] = {{"/usr/lib/libc.dylib", NULL}, {"/usr/lib/libm.dylib", NULL}};
static const char *std_lib_dirs[] = {"/usr/lib"};
static const char *lib_suffix = ".dylib";
#endif

#ifdef _WIN32
static lib_t std_libs[] = {{"C:\\Windows\\System32\\msvcrt.dll", NULL},
                           {"C:\\Windows\\System32\\kernel32.dll", NULL},
                           {"C:\\Windows\\System32\\ucrtbase.dll", NULL}};
static const char *std_lib_dirs[] = {"C:\\Windows\\System32"};
static const char *lib_suffix = ".dll";
#define dlopen(n, f) LoadLibrary (n)
#define dlclose(h) FreeLibrary (h)
#define dlsym(h, s) GetProcAddress (h, s)
#endif

static void close_std_libs (void) {
  for (int i = 0; i < sizeof (std_libs) / sizeof (lib_t); i++)
    if (std_libs[i].handler != NULL) dlclose (std_libs[i].handler);
}

static void open_std_libs (void) {
  for (int i = 0; i < sizeof (std_libs) / sizeof (struct lib); i++)
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
  if (last_slash == NULL || last_slash[1] != '\0') VARR_PUSH (char, temp_string, slash);
#ifndef _WIN32
  VARR_PUSH_ARR (char, temp_string, "lib", 3);
#endif
  VARR_PUSH_ARR (char, temp_string, name, strlen (name));
  VARR_PUSH_ARR (char, temp_string, lib_suffix, strlen (lib_suffix));
  VARR_PUSH (char, temp_string, 0);
  if ((res = dlopen (VARR_ADDR (char, temp_string), RTLD_LAZY)) == NULL) {
#ifndef _WIN32
    if ((f = fopen (VARR_ADDR (char, temp_string), "rb")) != NULL) {
      fclose (f);
      fprintf (stderr, "loading %s:%s\n", VARR_ADDR (char, temp_string), dlerror ());
    }
#endif
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
  union {
    uint32_t i;
    float f;
  } u = {0x7fc00000};
  return u.f;
}
#endif

static void *import_resolver (const char *name) {
  void *handler, *sym = NULL;

  for (int i = 0; i < sizeof (std_libs) / sizeof (struct lib); i++)
    if ((handler = std_libs[i].handler) != NULL && (sym = dlsym (handler, name)) != NULL) break;
  if (sym == NULL)
    for (int i = 0; i < VARR_LENGTH (lib_t, extra_libs); i++)
      if ((handler = VARR_GET (lib_t, extra_libs, i).handler) != NULL
          && (sym = dlsym (handler, name)) != NULL)
        break;
  if (sym == NULL) {
#ifdef _WIN32
    if (strcmp (name, "LoadLibrary") == 0) return LoadLibrary;
    if (strcmp (name, "FreeLibrary") == 0) return FreeLibrary;
    if (strcmp (name, "GetProcAddress") == 0) return GetProcAddress;
#else
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
#endif
    /* Not found in any shared library.  For ahead-of-time object generation
       this is normal: the symbol is likely defined in another object file
       (or in a library) that the final linker will resolve.  Return NULL so
       the caller can substitute a placeholder address. */
    return NULL;
  }
  return sym;
}

void lib_dirs_from_env_var (const char *env_var) {
  const char *var_value = getenv (env_var);
  if (var_value == NULL || var_value[0] == '\0') return;

  // copy to an allocated buffer
  int value_len = strlen (var_value);
  char *value = (char *) malloc (value_len + 1);
  strcpy (value, var_value);

  // colon separated list
  char *value_ptr = value;
  char *colon = NULL;
  while ((colon = strchr (value_ptr, ':')) != NULL) {
    colon[0] = '\0';
    VARR_PUSH (char_ptr_t, lib_dirs, value_ptr);
    // goto next
    value_ptr = colon + 1;
  }
  // final part of string
  // colon == NULL
  VARR_PUSH (char_ptr_t, lib_dirs, value_ptr);
}

int get_mir_type (void) {
  const char *type_value = getenv (MIR_ENV_VAR_TYPE);
  if (type_value == NULL || type_value[0] == '\0') return MIR_TYPE_DEFAULT;

  if (strcmp (type_value, MIR_TYPE_INTERP_NAME) == 0) return MIR_TYPE_INTERP;

  if (strcmp (type_value, MIR_TYPE_GEN_NAME) == 0) return MIR_TYPE_GEN;

  if (strcmp (type_value, MIR_TYPE_LAZY_NAME) == 0) return MIR_TYPE_LAZY;

  fprintf (stderr, "warning: unknown MIR_TYPE '%s', using default one\n", type_value);
  return MIR_TYPE_DEFAULT;
}

void open_extra_libs (void) {
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

// Structure to hold relocation information (assumed to be provided by MIR)
typedef struct {
    size_t offset;      // Offset in the machine code
    const char *symbol; // External symbol name
    int type;           // Relocation type (e.g., R_X86_64_PC32)
} reloc_t;

// Structure to return code and relocations from generation
typedef struct {
    void *code;
    size_t code_size;
    reloc_t *relocs;    // Array of relocations
    size_t n_relocs;    // Number of relocations
} code_data_t;

// Structure to store relocation info (adjust as needed)
typedef struct {
    const char *symbol;  // Symbol name
    void *addr;          // Resolved address
} symbol_entry_t;

// Global or context-passed list to record symbols
typedef struct {
    symbol_entry_t *entries;
    size_t n_entries;
    size_t capacity;
} symbol_list_t;

static symbol_list_t symbols = {0};

/* Placeholder address handed to MIR_link for symbols that are not present in
   any loaded shared library.  Such symbols are emitted as undefined in the
   object file and resolved by the final linker; the concrete address used here
   is never relied upon, because relocations are emitted by symbol name. */
static char aot_undef_placeholder;

void *hybrid_import_resolver(const char *name) {
    // Call the original resolver to get the real address (NULL if not found)
    void *addr = import_resolver(name);
    if (addr == NULL) addr = &aot_undef_placeholder; /* keep MIR_link happy */

    // Record the symbol and its address
    if (symbols.n_entries >= symbols.capacity) {
        symbols.capacity = symbols.capacity ? symbols.capacity * 2 : 16;
        symbols.entries = realloc(symbols.entries, symbols.capacity * sizeof(symbol_entry_t));
    }
    symbols.entries[symbols.n_entries].symbol = strdup(name);  // Duplicate to manage memory
    symbols.entries[symbols.n_entries].addr = addr;
    symbols.n_entries++;

    return addr;  // Return address for MIR_link (real or placeholder)
}

/* ================================================================== */
/*  Collected item types for ELF generation                           */
/* ================================================================== */

typedef struct {
    const char *name;       /* symbol name (may be NULL for anonymous data) */
    void       *code;       /* copy of machine code */
    size_t      code_len;   /* length in bytes */
    size_t      text_offset;/* offset within the concatenated .text section */
    MIR_item_t  item;       /* original MIR item (for relocs VARR) */
} func_entry_t;

typedef struct {
    const char *name;       /* symbol name (may be NULL) */
    uint8_t    *bytes;      /* raw data bytes */
    size_t      size;       /* data size in bytes */
    size_t      data_offset;/* offset within .data section */
    int         is_ref_data;/* 1 if this came from MIR_ref_data_item */
    MIR_item_t  ref_item;   /* for ref_data: the referenced item */
    int64_t     ref_disp;   /* for ref_data: displacement */
    void       *item_addr;  /* MIR item->addr at load time (for reloc scanning) */
} data_entry_t;

typedef struct {
    const char *name;
    size_t      len;
    size_t      bss_offset; /* offset within .bss section */
    void       *item_addr;
} bss_entry_t;

/* TLS image pieces for ELF .tdata / .tbss (N2 local-exec) */
typedef struct {
    const char *name;       /* may be NULL for padding */
    uint8_t    *bytes;      /* tdata only; NULL for tbss */
    size_t      size;
    size_t      tls_offset; /* offset within combined TLS image */
    int         is_bss;     /* 1 = .tbss */
} tls_entry_t;

/* A single relocation to emit */
typedef struct {
    size_t      offset;     /* offset within the target section */
    const char *symbol;     /* symbol name */
    int         type;       /* ELF reloc type, e.g. R_X86_64_64 */
    int64_t     addend;
    int         sect;       /* 0=.text, 1=.data, 2=.mir.addrpool */
} elf_reloc_t;

/* ================================================================== */
/*  Helper: add a string to a dynamically-growing string table         */
/* ================================================================== */
static size_t strtab_add(char **buf, size_t *bufsize, size_t *bufcap, const char *str) {
    size_t len = strlen(str) + 1;
    while (*bufsize + len > *bufcap) {
        *bufcap = *bufcap ? *bufcap * 2 : 256;
        *buf = realloc(*buf, *bufcap);
    }
    size_t off = *bufsize;
    memcpy(*buf + off, str, len);
    *bufsize += len;
    return off;
}

/* ================================================================== */
/*  Helper: 8-byte-align a value                                       */
/* ================================================================== */
static size_t align8(size_t v) { return (v + 7) & ~(size_t)7; }

/* ================================================================== */
/*  Helper: write padding bytes                                        */
/* ================================================================== */
static void write_padding(int fd, size_t nbytes) {
    static const char zeros[16] = {0};
    while (nbytes > 0) {
        size_t n = nbytes < sizeof(zeros) ? nbytes : sizeof(zeros);
        write(fd, zeros, n);
        nbytes -= n;
    }
}

/* ================================================================== */
/*  Helper: find or add a name in a simple dedup list, return index    */
/* ================================================================== */
typedef struct { char **names; size_t n; size_t cap; } name_set_t;
static size_t name_set_find_or_add(name_set_t *s, const char *name) {
    for (size_t i = 0; i < s->n; i++)
        if (strcmp(s->names[i], name) == 0) return i;
    if (s->n >= s->cap) {
        s->cap = s->cap ? s->cap * 2 : 32;
        s->names = realloc(s->names, s->cap * sizeof(char *));
    }
    s->names[s->n] = strdup(name);
    return s->n++;
}
static int name_set_find(name_set_t *s, const char *name, size_t *idx) {
    for (size_t i = 0; i < s->n; i++)
        if (strcmp(s->names[i], name) == 0) { *idx = i; return 1; }
    return 0;
}

/* ================================================================== */
/*  Map internal MIR builtin names to real, linkable symbols           */
/* ================================================================== */
/*
 * The code generator emits calls to internal "mir.*" builtin functions
 * (e.g. for passing aggregates by value or for va_arg).  At JIT time these
 * resolve to in-process addresses, but in an object file they must reference
 * the real backing function so the system linker can resolve them.
 */
static const char *map_symbol(const char *name) {
    if (name == NULL) return name;
    static const struct { const char *from; const char *to; } map[] = {
        /* aggregate-by-value copy: backed by libc memcpy */
        { "mir.arg_memcpy",   "memcpy" },
        /* varargs helpers: exported from the MIR core library (mir.o) */
        { "mir.va_arg",       "va_arg_builtin" },
        { "mir.va_block_arg", "va_block_arg_builtin" },
        /* conversion helpers: provided by mir-aot-runtime.c */
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
        /* Emulated TLS (N1); real ELF LE is N2 — see TLS-IMPLEMENTATION.md */
        { "mir.tls_addr",          "mir_tls_addr" },
        { "mir.tls_base",          "mir_tls_base" },
    };
    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++)
        if (strcmp(name, map[i].from) == 0) return map[i].to;
    return name;
}

/* ================================================================== */
/* ================================================================== */
/*  DWARF helper: produce a GDB-friendly display name from mangled MIR name. */
/*  Class methods:  "ClassName_method__params" -> "ClassName::method"         */
/*  Constructors:   "__ctor_ClassName__params" -> "ClassName::ClassName"      */
/*  Destructors:    "__dtor_ClassName__params" -> "ClassName::~ClassName"     */
/*  Plain functions: returned as-is.                                         */
/* ================================================================== */
static char dwarf_name_buf[512];
static const char *dwarf_display_name(const char *mir_name) {
    if (strncmp(mir_name, "__ctor_", 7) == 0) {
        const char *cls = mir_name + 7;
        const char *sep = strstr(cls, "__");
        size_t clen = sep ? (size_t)(sep - cls) : strlen(cls);
        snprintf(dwarf_name_buf, sizeof(dwarf_name_buf), "%.*s::%.*s",
                 (int)clen, cls, (int)clen, cls);
        return dwarf_name_buf;
    }
    if (strncmp(mir_name, "__dtor_", 7) == 0) {
        const char *cls = mir_name + 7;
        const char *sep = strstr(cls, "__");
        size_t clen = sep ? (size_t)(sep - cls) : strlen(cls);
        snprintf(dwarf_name_buf, sizeof(dwarf_name_buf), "%.*s::~%.*s",
                 (int)clen, cls, (int)clen, cls);
        return dwarf_name_buf;
    }
    /* ClassName_method__params: find __ suffix, then last _ before it */
    const char *dbl = strstr(mir_name, "__");
    if (dbl != NULL && dbl != mir_name) {
        const char *under = NULL;
        for (const char *p = mir_name; p < dbl; p++)
            if (*p == '_') under = p;
        if (under != NULL && under > mir_name && under < dbl - 1) {
            size_t clen = (size_t)(under - mir_name);
            size_t mlen = (size_t)(dbl - under - 1);
            snprintf(dwarf_name_buf, sizeof(dwarf_name_buf), "%.*s::%.*s",
                     (int)clen, mir_name, (int)mlen, under + 1);
            return dwarf_name_buf;
        }
    }
    return mir_name;
}

/* ================================================================== */
/*  create_object_file_from_module                                     */
/*  Walks all items in all modules, generates code, collects data/bss, */
/*  builds ELF sections, and writes a valid ELF64 relocatable object.  */
/* ================================================================== */
static void create_object_file_from_module(MIR_context_t ctx, const char *output_file) {
    /* ----- Phase 0: arrays for collected items ----- */
    func_entry_t *funcs = NULL;  size_t n_funcs = 0, cap_funcs = 0;
    data_entry_t *datas = NULL;  size_t n_datas = 0, cap_datas = 0;
    bss_entry_t  *bsses = NULL;  size_t n_bsses = 0, cap_bsses = 0;
    tls_entry_t  *tlss = NULL;   size_t n_tlss = 0, cap_tlss = 0;
    size_t tdata_size = 0, tbss_size = 0;
    elf_reloc_t  *relocs = NULL; size_t n_relocs = 0, cap_relocs = 0;
    name_set_t exports = {0};
    name_set_t imports = {0};

    /* ----- Phase 1a: Generate machine code for all functions ----- */
    DBG("phase 1a: generating machine code for all functions");
    size_t gen_count = 0;
    for (MIR_module_t module = DLIST_HEAD(MIR_module_t, *MIR_get_module_list(ctx));
         module != NULL;
         module = DLIST_NEXT(MIR_module_t, module)) {
        for (MIR_item_t item = DLIST_HEAD(MIR_item_t, module->items);
             item != NULL;
             item = DLIST_NEXT(MIR_item_t, item)) {
            if (item->item_type == MIR_func_item) {
                MIR_gen(ctx, item);
                if ((++gen_count % 200) == 0) DBG("  phase 1a: %zu functions generated", gen_count);
            }
        }
    }
    DBG("phase 1a done: %zu functions", gen_count);

    /* ----- Phase 1b: Collect all items (including those created by MIR_gen) ----- */
    DBG("phase 1b: collecting items");
    for (MIR_module_t module = DLIST_HEAD(MIR_module_t, *MIR_get_module_list(ctx));
         module != NULL;
         module = DLIST_NEXT(MIR_module_t, module)) {
        for (MIR_item_t item = DLIST_HEAD(MIR_item_t, module->items);
             item != NULL;
             item = DLIST_NEXT(MIR_item_t, item)) {

            switch (item->item_type) {

            case MIR_export_item:
                name_set_find_or_add(&exports, item->u.export_id);
                break;

            case MIR_import_item:
                name_set_find_or_add(&imports, map_symbol(item->u.import_id));
                break;

            case MIR_func_item: {
                MIR_func_t f = item->u.func;
                if (!f->machine_code || f->machine_code_len == 0) {
                    fprintf(stderr, "warning: function '%s' produced no code\n", f->name);
                    break;
                }
                if (n_funcs >= cap_funcs) {
                    cap_funcs = cap_funcs ? cap_funcs * 2 : 16;
                    funcs = realloc(funcs, cap_funcs * sizeof(func_entry_t));
                }
                func_entry_t *fe = &funcs[n_funcs++];
                fe->name = f->name;
                fe->code_len = f->machine_code_len;
                fe->code = malloc(fe->code_len);
                memcpy(fe->code, f->machine_code, fe->code_len);
                fe->text_offset = 0;
                fe->item = item;
                DBG("  func: %s  code_len=%zu", f->name, fe->code_len);
                break;
            }

            /*
             * Data / bss / ref_data form *objects* the same way MIR's
             * load_bss_data_section does: a named item starts an object, then
             * any number of *anonymous* data/bss/ref items continue it, packed
             * contiguously (no inter-item padding).  Classyc uses anonymous
             * MIR_bss for struct-member padding inside global initializers
             * (e.g. 4 zero bytes between an i32 and the next pointer).  Those
             * zeros must stay in .data at the right offset — if we shunt them
             * into .bss the later pointer fields slide left and function
             * pointers in tables like oggenc's `formats[]` become garbage.
             *
             * Pure-bss objects (named bss + optional anon bss) still go to
             * .bss.  Mixed objects go entirely to .data (bss pieces as zeros).
             */
            case MIR_tls_data_item:
            case MIR_tls_bss_item: {
                /* N2 LE: symbol metadata; full template image emitted once per module. */
                int is_bss = (item->item_type == MIR_tls_bss_item);
                size_t sz = is_bss ? (size_t) item->u.tls_bss->len
                                   : item->u.tls_data->nel
                                         * _MIR_type_size (ctx, item->u.tls_data->el_type);
                const char *nm = is_bss ? item->u.tls_bss->name : item->u.tls_data->name;
                if (nm == NULL) break; /* anonymous padding — image still holds zeros */
                if (n_tlss >= cap_tlss) {
                    cap_tlss = cap_tlss ? cap_tlss * 2 : 16;
                    tlss = realloc (tlss, cap_tlss * sizeof (tls_entry_t));
                }
                tls_entry_t *te = &tlss[n_tlss++];
                te->name = nm;
                te->size = sz;
                te->tls_offset = MIR_tls_item_offset (item);
                te->is_bss = is_bss;
                te->bytes = NULL;
                DBG ("  tls sym: %s off=%u size=%zu", nm, (unsigned) te->tls_offset, sz);
                break;
            }

            case MIR_data_item:
            case MIR_ref_data_item:
            case MIR_bss_item:
            case MIR_lref_data_item:
            case MIR_expr_data_item: {
                /* Only start an object on a named item (or a lone anonymous
                   that MIR would still load as a section head).  Anonymous
                   continuations are consumed by the object that started
                   earlier; if we see a stray anonymous here it is a section
                   head with no name (rare) — still process it as an object. */
                MIR_item_t head = item;
                MIR_item_t curr;
                int has_non_bss = 0;

                /* Walk the contiguous object and decide pure-bss vs mixed. */
                for (curr = head; curr != NULL; curr = DLIST_NEXT(MIR_item_t, curr)) {
                    if (curr != head) {
                        /* Continuation must be anonymous dataish. */
                        const char *n = NULL;
                        if (curr->item_type == MIR_data_item) n = curr->u.data->name;
                        else if (curr->item_type == MIR_ref_data_item) n = curr->u.ref_data->name;
                        else if (curr->item_type == MIR_bss_item) n = curr->u.bss->name;
                        else if (curr->item_type == MIR_lref_data_item) n = curr->u.lref_data->name;
                        else if (curr->item_type == MIR_expr_data_item) n = curr->u.expr_data->name;
                        else break;
                        if (n != NULL) break;
                    } else {
                        /* Head must be dataish (we are in those cases). */
                    }
                    if (curr->item_type == MIR_data_item
                        || curr->item_type == MIR_ref_data_item
                        || curr->item_type == MIR_lref_data_item
                        || curr->item_type == MIR_expr_data_item)
                        has_non_bss = 1;
                    else if (curr->item_type != MIR_bss_item)
                        break;
                }

                /* Emit each piece of the object.  `curr` is the first item
                   *not* in this object (or NULL).  Advance the outer loop by
                   walking pieces and setting item to the last one emitted. */
                MIR_item_t last = head;
                for (MIR_item_t p = head; p != curr; p = DLIST_NEXT(MIR_item_t, p)) {
                    last = p;
                    if (p->item_type == MIR_bss_item) {
                        MIR_bss_t b = p->u.bss;
                        if (!has_non_bss) {
                            /* Pure bss object → .bss section */
                            if (n_bsses >= cap_bsses) {
                                cap_bsses = cap_bsses ? cap_bsses * 2 : 16;
                                bsses = realloc(bsses, cap_bsses * sizeof(bss_entry_t));
                            }
                            bss_entry_t *be = &bsses[n_bsses++];
                            be->name = b->name;
                            be->len = b->len;
                            be->bss_offset = 0;
                            be->item_addr = p->addr;
                            if (b->name)
                                DBG("  bss: %s  len=%lu  addr=%p", b->name,
                                    (unsigned long)b->len, p->addr);
                        } else {
                            /* Mixed object: emit zero-filled .data padding */
                            if (b->len == 0 && b->name == NULL) continue;
                            if (n_datas >= cap_datas) {
                                cap_datas = cap_datas ? cap_datas * 2 : 32;
                                datas = realloc(datas, cap_datas * sizeof(data_entry_t));
                            }
                            data_entry_t *de = &datas[n_datas++];
                            de->name = b->name;
                            de->size = b->len;
                            de->bytes = b->len ? calloc(1, b->len) : NULL;
                            de->data_offset = 0;
                            de->is_ref_data = 0;
                            de->ref_item = NULL;
                            de->ref_disp = 0;
                            de->item_addr = p->addr;
                            DBG("  data(bss-pad): %s  size=%lu",
                                b->name ? b->name : "(anon)", (unsigned long)b->len);
                        }
                    } else if (p->item_type == MIR_data_item) {
                        MIR_data_t d = p->u.data;
                        size_t sz = d->nel * _MIR_type_size(ctx, d->el_type);
                        if (sz == 0 && d->name == NULL) continue;
                        if (n_datas >= cap_datas) {
                            cap_datas = cap_datas ? cap_datas * 2 : 32;
                            datas = realloc(datas, cap_datas * sizeof(data_entry_t));
                        }
                        data_entry_t *de = &datas[n_datas++];
                        de->name = d->name;
                        de->size = sz;
                        de->bytes = sz ? malloc(sz) : NULL;
                        if (sz) memcpy(de->bytes, d->u.els, sz);
                        de->data_offset = 0;
                        de->is_ref_data = 0;
                        de->ref_item = NULL;
                        de->ref_disp = 0;
                        de->item_addr = p->addr;
                        DBG("  data: %s  size=%zu  addr=%p",
                            d->name ? d->name : "(anon)", sz, p->addr);
                    } else if (p->item_type == MIR_ref_data_item) {
                        MIR_ref_data_t rd = p->u.ref_data;
                        if (n_datas >= cap_datas) {
                            cap_datas = cap_datas ? cap_datas * 2 : 32;
                            datas = realloc(datas, cap_datas * sizeof(data_entry_t));
                        }
                        data_entry_t *de = &datas[n_datas++];
                        de->name = rd->name;
                        de->size = 8;
                        de->bytes = calloc(1, 8);
                        de->data_offset = 0;
                        de->is_ref_data = 1;
                        de->ref_item = rd->ref_item;
                        de->ref_disp = rd->disp;
                        de->item_addr = p->addr;
                        DBG("  ref_data: %s -> %s + %ld",
                            rd->name ? rd->name : "(anon)",
                            MIR_item_name(ctx, rd->ref_item), (long)rd->disp);
                    } else if (p->item_type == MIR_lref_data_item
                               || p->item_type == MIR_expr_data_item) {
                        /* Not yet supported for AOT object emission. */
                        fprintf(stderr,
                                "warning: b2obj: skipping unsupported %s item in data object\n",
                                p->item_type == MIR_lref_data_item ? "lref_data" : "expr_data");
                    }
                }
                /* Outer for-loop will DLIST_NEXT from item; point item at last
                   so the next iteration starts at curr. */
                item = last;
                break;
            }

            case MIR_forward_item:
            case MIR_proto_item:
                break;

            default:
                break;
            }
        }
    }

    /* ----- [[registry("NAME")]] linker sets -----
       classyc lowered each tagged record to a ref_data symbol
       `__cyreg_<NAME>__*` pointing at the record.  Pull them OUT of .data into
       one PROGBITS section `cyreg_<NAME>` per registry (holding the 8-byte
       pointers, relocated to the records), so the system linker synthesises
       `__start_cyreg_<NAME>` / `__stop_cyreg_<NAME>` bounding the set — the
       AOT analogue of the JIT driver's module scan. */
    typedef struct {
        char name[128];
        uint8_t *buf; size_t size, cap;
        struct { size_t off; const char *sym; int64_t add; } *rel;
        size_t nrel, caprel;
        Elf64_Rela *rbuf;                /* resolved relocations (indices) */
        size_t sec_idx, rela_sec_idx;    /* ELF section indices (assigned later) */
        size_t nm, rela_nm;              /* .shstrtab name offsets */
        size_t file_off, rela_file_off;  /* file offsets */
    } cyreg_sec_t;
    cyreg_sec_t *cyr = NULL; size_t n_cyr = 0, cap_cyr = 0;
    struct { const char *name; size_t sec; size_t off; } *cyr_syms = NULL;
    size_t n_cyr_syms = 0, cap_cyr_syms = 0;
    {
        size_t w = 0;
        for (size_t i = 0; i < n_datas; i++) {
            const char *nm = datas[i].name;
            /* Prefer is_ref_data; also accept 8-byte __cyreg_* left as ordinary
               .data (some MIR→bmir paths drop the ref_data tag). */
            if (nm != NULL && strncmp(nm, "__cyreg_", 8) == 0
                && (datas[i].is_ref_data || datas[i].size == 8)) {
                const char *p = nm + 8;
                const char *e = strstr(p, "__");
                size_t klen = e ? (size_t)(e - p) : strlen(p);
                char key[128];
                if (klen >= sizeof key) klen = sizeof key - 1;
                memcpy(key, p, klen); key[klen] = 0;
                cyreg_sec_t *s = NULL;
                for (size_t k = 0; k < n_cyr; k++)
                    if (strcmp(cyr[k].name, key) == 0) { s = &cyr[k]; break; }
                if (s == NULL) {
                    if (n_cyr >= cap_cyr) { cap_cyr = cap_cyr ? cap_cyr*2 : 4;
                        cyr = realloc(cyr, cap_cyr * sizeof *cyr); }
                    s = &cyr[n_cyr++]; memset(s, 0, sizeof *s);
                    snprintf(s->name, sizeof s->name, "%s", key);
                }
                size_t off = s->size;             /* entries are 8 bytes, naturally aligned */
                if (s->size + 8 > s->cap) { s->cap = s->cap ? s->cap*2 : 64;
                    s->buf = realloc(s->buf, s->cap); }
                memset(s->buf + off, 0, 8); s->size += 8;
                const char *tgt = datas[i].ref_item ? map_symbol(MIR_item_name(ctx, datas[i].ref_item)) : NULL;
                if (tgt != NULL) {
                    if (s->nrel >= s->caprel) { s->caprel = s->caprel ? s->caprel*2 : 8;
                        s->rel = realloc(s->rel, s->caprel * sizeof *s->rel); }
                    s->rel[s->nrel].off = off; s->rel[s->nrel].sym = tgt;
                    s->rel[s->nrel].add = datas[i].ref_disp; s->nrel++;
                }
                if (n_cyr_syms >= cap_cyr_syms) { cap_cyr_syms = cap_cyr_syms ? cap_cyr_syms*2 : 16;
                    cyr_syms = realloc(cyr_syms, cap_cyr_syms * sizeof *cyr_syms); }
                cyr_syms[n_cyr_syms].name = nm;
                cyr_syms[n_cyr_syms].sec  = (size_t)(s - cyr);
                cyr_syms[n_cyr_syms].off  = off;
                n_cyr_syms++;
                free(datas[i].bytes);             /* drop from .data */
            } else {
                datas[w++] = datas[i];
            }
        }
        n_datas = w;
    }

    /* ----- Phase 1c: N2 native TLS image (.tdata) -----
       Concatenate each module's TLS template into one .tdata buffer.
       Named TLS symbols keep their per-module offsets (load_module_tls layout). */
    uint8_t *tdata_buf = NULL;
    {
      for (MIR_module_t module = DLIST_HEAD (MIR_module_t, *MIR_get_module_list (ctx));
           module != NULL; module = DLIST_NEXT (MIR_module_t, module)) {
        if (module->tls_module_id == 0 || module->tls_template == NULL || module->tls_size == 0)
          continue;
        size_t old = tdata_size;
        /* One module image for v1 AOT (typical single TU). */
        tdata_size = module->tls_size;
        tdata_buf = realloc (tdata_buf, tdata_size ? tdata_size : 1);
        memcpy (tdata_buf, module->tls_template, module->tls_size);
        (void) old;
        DBG ("  .tdata image: %zu bytes (mod_id=%u)", tdata_size,
             (unsigned) module->tls_module_id);
      }
    }

    /* ----- Phase 2: assign offsets within sections ----- */
    DBG("phase 1b done: %zu funcs, %zu datas, %zu bsses", n_funcs, n_datas, n_bsses);
    DBG("phase 2: assigning section offsets and building buffers");

    /* .text: concatenate all function codes (16-byte aligned) */
    size_t text_size = 0;
    for (size_t i = 0; i < n_funcs; i++) {
        if (i > 0) text_size = (text_size + 15) & ~(size_t)15;
        funcs[i].text_offset = text_size;
        text_size += funcs[i].code_len;
    }

    /* .data: concatenate all data items.
       A MIR data "object" is one *named* item followed by any number of
       *anonymous* (name == NULL) continuation items, which MIR lays out
       contiguously with no inter-item padding (see load_bss_data_section in
       mir.c: it does `addr += len` per item and only rounds the *whole*
       object up to 8).  A new named item begins a new object, which MIR
       allocates separately (8-aligned).  So align only at a new named
       boundary and pack continuations tightly.  Aligning *every* item to 8
       (the old behaviour) shifted sub-8-byte members (e.g. the i32 fields of
       a struct), corrupting the layout and the offsets of later pointer
       members / ref relocations (24-byte stride for a 16-byte struct). */
    size_t data_size = 0;
    for (size_t i = 0; i < n_datas; i++) {
        if (i > 0 && datas[i].name != NULL) data_size = align8(data_size);
        datas[i].data_offset = data_size;
        data_size += datas[i].size;
    }

    /* .bss: same object model as .data (named item starts a new, 8-aligned
       object; anonymous items are packed continuations). */
    size_t bss_size = 0;
    for (size_t i = 0; i < n_bsses; i++) {
        if (i > 0 && bsses[i].name != NULL) bss_size = align8(bss_size);
        bsses[i].bss_offset = bss_size;
        bss_size += bsses[i].len;
    }

    /* Build .text data buffer */
    uint8_t *text_buf = calloc(1, text_size ? text_size : 1);
    for (size_t i = 0; i < n_funcs; i++)
        memcpy(text_buf + funcs[i].text_offset, funcs[i].code, funcs[i].code_len);

    /* Build .data data buffer */
    uint8_t *data_buf = calloc(1, data_size ? data_size : 1);
    for (size_t i = 0; i < n_datas; i++)
        if (datas[i].size) memcpy(data_buf + datas[i].data_offset, datas[i].bytes, datas[i].size);

    DBG("phase 2b: collecting relocations from generator");
    /* ----- Phase 2b: collect .text relocations from the code generator ----- */

    /*
     * The MIR code generator (run with MIR_gen_set_save_relocs) records, for
     * each function, the exact code offsets that reference external symbols
     * along with the symbol name.  These are far more reliable than scanning
     * the machine code for embedded addresses, because some references (e.g.
     * string literals with reserved ".lc" names) do not embed the symbol's
     * loaded address at all.
     */
    for (size_t fi = 0; fi < n_funcs; fi++) {
        MIR_func_t f = funcs[fi].item->u.func;
        if (f->relocs == NULL) continue;
        size_t nr = VARR_LENGTH(MIR_code_reloc_t, f->relocs);
        for (size_t ri = 0; ri < nr; ri++) {
            MIR_code_reloc_t cr = VARR_GET(MIR_code_reloc_t, f->relocs, ri);
            if (cr.symbol == NULL) continue;
            if (n_relocs >= cap_relocs) {
                cap_relocs = cap_relocs ? cap_relocs * 2 : 64;
                relocs = realloc(relocs, cap_relocs * sizeof(elf_reloc_t));
            }
            elf_reloc_t *er = &relocs[n_relocs++];
            er->offset = funcs[fi].text_offset + cr.offset;
            er->symbol = map_symbol(cr.symbol);
            er->type   = cr.type;
            er->addend = cr.addend;
            er->sect = 0;
        }
    }

    /* Module-level .mir.addrpool (PIC GOT-shaped slots) from the generator. */
    const uint8_t *addrpool_bytes = NULL;
    size_t addrpool_len = 0;
    const MIR_code_reloc_t *ap_relocs = NULL;
    size_t n_ap_relocs = 0;
    MIR_gen_get_addrpool (ctx, &addrpool_bytes, &addrpool_len, &ap_relocs, &n_ap_relocs);
    uint8_t *addrpool_buf = NULL;
    if (addrpool_len > 0) {
        addrpool_buf = calloc (1, addrpool_len);
        if (addrpool_bytes) memcpy (addrpool_buf, addrpool_bytes, addrpool_len);
    }
    for (size_t i = 0; i < n_ap_relocs; i++) {
        if (ap_relocs[i].symbol == NULL) continue;
        if (n_relocs >= cap_relocs) {
            cap_relocs = cap_relocs ? cap_relocs * 2 : 64;
            relocs = realloc (relocs, cap_relocs * sizeof (elf_reloc_t));
        }
        elf_reloc_t *er = &relocs[n_relocs++];
        er->offset = ap_relocs[i].offset;
        er->symbol = map_symbol (ap_relocs[i].symbol);
        er->type = ap_relocs[i].type;
        er->addend = ap_relocs[i].addend;
        er->sect = 2; /* .mir.addrpool */
    }

    /* .data relocations from ref_data items */
    for (size_t i = 0; i < n_datas; i++) {
        if (!datas[i].is_ref_data) continue;
        const char *target_name = map_symbol(MIR_item_name(ctx, datas[i].ref_item));
        if (!target_name) continue;
        if (n_relocs >= cap_relocs) {
            cap_relocs = cap_relocs ? cap_relocs * 2 : 64;
            relocs = realloc(relocs, cap_relocs * sizeof(elf_reloc_t));
        }
        elf_reloc_t *er = &relocs[n_relocs++];
        er->offset = datas[i].data_offset;
        er->symbol = target_name;
#if defined(__aarch64__)
        er->type = R_AARCH64_ABS64;
#else
        er->type = R_X86_64_64;
#endif
        er->addend = datas[i].ref_disp;
        er->sect = 1;
        /* The 8 bytes in data_buf are already zero (calloc) */
    }

    DBG("phase 2b done: %zu relocations (addrpool=%zu bytes, %zu pool relocs)",
        n_relocs, addrpool_len, n_ap_relocs);
    /* ----- Phase 3: build string tables and symbol table ----- */

    /*
     * Section indices:
     *   0  = null
     *   1  = .text
     *   2  = .data
     *   3  = .bss
     *   4  = .rela.text
     *   5  = .rela.data
     *   6  = .symtab
     *   7  = .strtab
     *   8  = .shstrtab
     *   9  = .note.GNU-stack
     */
    enum {
        SEC_NULL = 0,
        SEC_TEXT,        /* 1 */
        SEC_DATA,        /* 2 */
        SEC_BSS,         /* 3 */
        SEC_ADDRPOOL,    /* 4 — .mir.addrpool (PIC address slots) */
        SEC_RELA_TEXT,   /* 5 */
        SEC_RELA_DATA,   /* 6 */
        SEC_RELA_ADDRPOOL, /* 7 */
        SEC_SYMTAB,      /* 8 */
        SEC_STRTAB,      /* 9 */
        SEC_SHSTRTAB,    /* 10 */
        SEC_NOTE_STACK,  /* 11 */
        SEC_DEBUG_INFO,  /* 12 — .debug_info */
        SEC_DEBUG_ABBREV,/* 13 — .debug_abbrev */
        SEC_DEBUG_LINE,  /* 14 — .debug_line */
        SEC_DEBUG_STR,   /* 15 — .debug_str */
        SEC_DEBUG_FRAME, /* 16 — .debug_frame (CFI for backtraces) */
        SEC_RELA_DEBUG_INFO,  /* 17 — .rela.debug_info */
        SEC_RELA_DEBUG_LINE,  /* 18 — .rela.debug_line */
        SEC_RELA_DEBUG_FRAME, /* 19 — .rela.debug_frame */
        NUM_SECTIONS     /* 20 */
    };

    /* Build .shstrtab */
    char  *shstrtab = NULL;
    size_t shstrtab_size = 0, shstrtab_cap = 0;
    strtab_add(&shstrtab, &shstrtab_size, &shstrtab_cap, ""); /* index 0 = null */
    size_t nm_text       = strtab_add(&shstrtab, &shstrtab_size, &shstrtab_cap, ".text");
    size_t nm_data       = strtab_add(&shstrtab, &shstrtab_size, &shstrtab_cap, ".data");
    size_t nm_bss        = strtab_add(&shstrtab, &shstrtab_size, &shstrtab_cap, ".bss");
    size_t nm_addrpool   = strtab_add(&shstrtab, &shstrtab_size, &shstrtab_cap, MIR_AOT_ADDRPOOL_NAME);
    size_t nm_tdata      = strtab_add(&shstrtab, &shstrtab_size, &shstrtab_cap, ".tdata");
    size_t nm_rela_text  = strtab_add(&shstrtab, &shstrtab_size, &shstrtab_cap, ".rela.text");
    size_t nm_rela_data  = strtab_add(&shstrtab, &shstrtab_size, &shstrtab_cap, ".rela.data");
    size_t nm_rela_addrpool = strtab_add(&shstrtab, &shstrtab_size, &shstrtab_cap, ".rela.mir.addrpool");
    size_t nm_symtab     = strtab_add(&shstrtab, &shstrtab_size, &shstrtab_cap, ".symtab");
    size_t nm_strtab     = strtab_add(&shstrtab, &shstrtab_size, &shstrtab_cap, ".strtab");
    size_t nm_shstrtab   = strtab_add(&shstrtab, &shstrtab_size, &shstrtab_cap, ".shstrtab");
    size_t nm_note_stack = strtab_add(&shstrtab, &shstrtab_size, &shstrtab_cap, ".note.GNU-stack");
    size_t nm_debug_info   = strtab_add(&shstrtab, &shstrtab_size, &shstrtab_cap, ".debug_info");
    size_t nm_debug_abbrev = strtab_add(&shstrtab, &shstrtab_size, &shstrtab_cap, ".debug_abbrev");
    size_t nm_debug_line   = strtab_add(&shstrtab, &shstrtab_size, &shstrtab_cap, ".debug_line");
    size_t nm_debug_str    = strtab_add(&shstrtab, &shstrtab_size, &shstrtab_cap, ".debug_str");
    size_t nm_debug_frame  = strtab_add(&shstrtab, &shstrtab_size, &shstrtab_cap, ".debug_frame");
    size_t nm_rela_debug_info = strtab_add(&shstrtab, &shstrtab_size, &shstrtab_cap, ".rela.debug_info");
    size_t nm_rela_debug_line = strtab_add(&shstrtab, &shstrtab_size, &shstrtab_cap, ".rela.debug_line");
    size_t nm_rela_debug_frame = strtab_add(&shstrtab, &shstrtab_size, &shstrtab_cap, ".rela.debug_frame");

    /* Dynamic sections: optional .tdata then [[registry]] cyreg_* pairs. */
    size_t sec_tdata = 0;
    size_t n_extra = 2 * n_cyr;
    if (tdata_size > 0 || n_tlss > 0) {
        sec_tdata = NUM_SECTIONS;
        n_extra += 1;
    }
    size_t total_sections = NUM_SECTIONS + n_extra;
    size_t cyr_base = NUM_SECTIONS + (sec_tdata ? 1 : 0);
    for (size_t k = 0; k < n_cyr; k++) {
        char snm[160], rnm[168];
        snprintf(snm, sizeof snm, "cyreg_%s", cyr[k].name);
        snprintf(rnm, sizeof rnm, ".rela.cyreg_%s", cyr[k].name);
        cyr[k].sec_idx      = cyr_base + 2 * k;
        cyr[k].rela_sec_idx = cyr_base + 2 * k + 1;
        cyr[k].nm      = strtab_add(&shstrtab, &shstrtab_size, &shstrtab_cap, snm);
        cyr[k].rela_nm = strtab_add(&shstrtab, &shstrtab_size, &shstrtab_cap, rnm);
    }

    /* Build .strtab + symtab entries */
    char  *strtab = NULL;
    size_t strtab_size = 0, strtab_cap = 0;
    strtab_add(&strtab, &strtab_size, &strtab_cap, ""); /* index 0 = null */

    /* We need to build the symtab in two passes: locals first, then globals.
     * Local symbols: section symbols for .text, .data, .bss
     * Global symbols: exported funcs/data/bss, then imported/undefined
     */

    /* Collect all unique symbol names that appear in relocations or items */
    /* Map: symbol_name -> symtab_index  (built during emission) */

    /* We'll build the symtab dynamically */
    Elf64_Sym *symtab = NULL;
    size_t n_syms = 0, cap_syms = 0;

    #define SYMTAB_PUSH(s) do { \
        if (n_syms >= cap_syms) { \
            cap_syms = cap_syms ? cap_syms * 2 : 64; \
            symtab = realloc(symtab, cap_syms * sizeof(Elf64_Sym)); \
        } \
        symtab[n_syms++] = (s); \
    } while(0)

    /* Symbol 0: null */
    { Elf64_Sym s = {0}; SYMTAB_PUSH(s); }

    /* Section symbols (local) for .text, .data, .bss, .mir.addrpool */
    size_t addrpool_sec_sym_idx = 0;
    {
        Elf64_Sym s = {0};
        s.st_info = ELF64_ST_INFO(STB_LOCAL, STT_SECTION);

        s.st_shndx = SEC_TEXT;
        SYMTAB_PUSH(s);

        s.st_shndx = SEC_DATA;
        SYMTAB_PUSH(s);

        s.st_shndx = SEC_BSS;
        SYMTAB_PUSH(s);

        s.st_shndx = SEC_ADDRPOOL;
        addrpool_sec_sym_idx = n_syms;
        SYMTAB_PUSH(s);
    }

    size_t first_global = n_syms; /* for sh_info */

    /* We need a mapping from symbol name -> symtab index for relocation emission.
     * Build a simple name->index table. */
    typedef struct { const char *name; size_t idx; } sym_map_entry_t;
    sym_map_entry_t *sym_map = NULL;
    size_t n_sym_map = 0, cap_sym_map = 0;

    /* Open-addressing index over sym_map so a name -> entry lookup is O(1).
       Without it every relocation scanned the whole symbol table with strcmp,
       making object emission O(n_relocs * n_symbols): on a large module that
       quadratic term dominated the entire b2obj run (~20% of total time on
       oggenc, far worse on bigger inputs).  Slots hold sym_map index + 1, so 0
       means empty.  Kept ~50% loaded and rebuilt on growth. */
    size_t *sym_idx_tab = NULL;
    size_t sym_idx_cap = 0;

    #define SYM_HASH(nm, h) do { \
        const unsigned char *_s = (const unsigned char *) (nm); \
        (h) = (size_t) 1469598103934665603ULL; \
        for (; *_s != '\0'; _s++) { (h) ^= (size_t) *_s; (h) *= (size_t) 1099511628211ULL; } \
    } while(0)

    /* Insert entry `ix` of sym_map into the index (table must have room). */
    /* Keep only the FIRST occurrence of a name, so a lookup returns the lowest
       sym_map index -- exactly what the original linear strcmp scan returned.
       sym_map can legitimately contain duplicate names, so this matters. */
    #define SYM_IDX_INSERT(ix) do { \
        size_t _h, _m = sym_idx_cap - 1, _p, _e; int _dup = 0; \
        SYM_HASH(sym_map[(ix)].name, _h); \
        for (_p = _h & _m; (_e = sym_idx_tab[_p]) != 0; _p = (_p + 1) & _m) \
            if (strcmp(sym_map[_e - 1].name, sym_map[(ix)].name) == 0) { _dup = 1; break; } \
        if (!_dup) sym_idx_tab[_p] = (ix) + 1; \
    } while(0)

    #define SYM_IDX_REBUILD() do { \
        size_t _newcap = sym_idx_cap ? sym_idx_cap * 2 : 256; \
        while (_newcap < n_sym_map * 2) _newcap *= 2; \
        free(sym_idx_tab); \
        sym_idx_tab = calloc(_newcap, sizeof(size_t)); \
        sym_idx_cap = _newcap; \
        for (size_t _i = 0; _i < n_sym_map; _i++) SYM_IDX_INSERT(_i); \
    } while(0)

    /* Look up `nm`; sets (found) and (out) to the sym_map slot. */
    #define SYM_MAP_FIND(nm, found, out) do { \
        (found) = 0; (out) = 0; \
        if (sym_idx_cap != 0) { \
            size_t _h, _m = sym_idx_cap - 1, _p, _e; \
            SYM_HASH((nm), _h); \
            for (_p = _h & _m; (_e = sym_idx_tab[_p]) != 0; _p = (_p + 1) & _m) \
                if (strcmp(sym_map[_e - 1].name, (nm)) == 0) { (found) = 1; (out) = _e - 1; break; } \
        } \
    } while(0)

    #define SYM_MAP_ADD(nm, ix) do { \
        if (n_sym_map >= cap_sym_map) { \
            cap_sym_map = cap_sym_map ? cap_sym_map * 2 : 64; \
            sym_map = realloc(sym_map, cap_sym_map * sizeof(sym_map_entry_t)); \
        } \
        sym_map[n_sym_map].name = (nm); \
        sym_map[n_sym_map].idx = (ix); \
        n_sym_map++; \
        if (n_sym_map * 2 >= sym_idx_cap) SYM_IDX_REBUILD(); \
        else SYM_IDX_INSERT(n_sym_map - 1); \
    } while(0)

    /* PC32 text→pool targets this section symbol (index remapped after sort). */
    SYM_MAP_ADD (MIR_AOT_ADDRPOOL_NAME, addrpool_sec_sym_idx);

    /* Global symbols: functions */
    for (size_t i = 0; i < n_funcs; i++) {
        const char *fname = funcs[i].name;
        /* Determine binding: global if exported, otherwise local */
        size_t dummy;
        int is_exported = name_set_find(&exports, fname, &dummy);
        int binding = is_exported ? STB_GLOBAL : STB_LOCAL;
        /* ClassyC class methods/ctors/dtors are lowered to free functions with
           mangled names of the form `Class_method__<sig>` (see
           mangle_func_def_mir_name in classyc.c).  When a class lives in a
           header included by several translation units, every such unit emits
           an identical copy of those methods, so the system linker would abort
           with "multiple definition of `Class_method__...`".  Emit them as WEAK
           instead so the linker folds the identical copies (C++-inline / ODR
           semantics).  This is the AOT mirror of the JIT's MIR func-redef
           permission.  The marker is the `__` the mangler always inserts before
           the signature; plain free functions (no `__`) stay strong GLOBAL so a
           genuine duplicate is still reported.  (Header-level free helpers that
           are legitimately shared should be declared `static`.) */
        if (is_exported && strstr(fname, "__") != NULL)
            binding = STB_WEAK;
        Elf64_Sym s = {0};
        s.st_name = strtab_add(&strtab, &strtab_size, &strtab_cap, fname);
        s.st_info = ELF64_ST_INFO(binding, STT_FUNC);
        s.st_shndx = SEC_TEXT;
        s.st_value = funcs[i].text_offset;
        s.st_size = funcs[i].code_len;
        SYM_MAP_ADD(fname, n_syms);
        SYMTAB_PUSH(s);
    }

    /* Global symbols: named data items */
    for (size_t i = 0; i < n_datas; i++) {
        if (!datas[i].name) continue;
        /* Check if already in sym_map (shouldn't be, but guard) */
        int found = 0;
        { size_t _sm; SYM_MAP_FIND(datas[i].name, found, _sm); (void) _sm; }
        if (found) continue;
        size_t dummy;
        int is_exported = name_set_find(&exports, datas[i].name, &dummy);
        Elf64_Sym s = {0};
        s.st_name = strtab_add(&strtab, &strtab_size, &strtab_cap, datas[i].name);
        s.st_info = ELF64_ST_INFO(is_exported ? STB_GLOBAL : STB_LOCAL, STT_OBJECT);
        s.st_shndx = SEC_DATA;
        s.st_value = datas[i].data_offset;
        s.st_size = datas[i].size;
        SYM_MAP_ADD(datas[i].name, n_syms);
        SYMTAB_PUSH(s);
    }

    /* Global symbols: named BSS items */
    for (size_t i = 0; i < n_bsses; i++) {
        if (!bsses[i].name) continue;
        int found = 0;
        { size_t _sm; SYM_MAP_FIND(bsses[i].name, found, _sm); (void) _sm; }
        if (found) continue;
        size_t dummy;
        int is_exported = name_set_find(&exports, bsses[i].name, &dummy);
        Elf64_Sym s = {0};
        s.st_name = strtab_add(&strtab, &strtab_size, &strtab_cap, bsses[i].name);
        s.st_info = ELF64_ST_INFO(is_exported ? STB_GLOBAL : STB_LOCAL, STT_OBJECT);
        s.st_shndx = SEC_BSS;
        s.st_value = bsses[i].bss_offset;
        s.st_size = bsses[i].len;
        SYM_MAP_ADD(bsses[i].name, n_syms);
        SYMTAB_PUSH(s);
    }

    /* TLS symbols (STT_TLS) in .tdata */
    if (sec_tdata) {
        for (size_t i = 0; i < n_tlss; i++) {
            if (!tlss[i].name) continue;
            int found = 0;
            { size_t _sm; SYM_MAP_FIND(tlss[i].name, found, _sm); (void) _sm; }
            if (found) continue;
            size_t dummy;
            int is_exported = name_set_find (&exports, tlss[i].name, &dummy);
            Elf64_Sym s = {0};
            s.st_name = strtab_add (&strtab, &strtab_size, &strtab_cap, tlss[i].name);
            s.st_info = ELF64_ST_INFO (is_exported ? STB_GLOBAL : STB_LOCAL, STT_TLS);
            s.st_shndx = (Elf64_Section) sec_tdata;
            s.st_value = tlss[i].tls_offset;
            s.st_size = tlss[i].size;
            SYM_MAP_ADD (tlss[i].name, n_syms);
            SYMTAB_PUSH (s);
            DBG ("  TLS sym %s st_value=%zu", tlss[i].name, tlss[i].tls_offset);
        }
    }

    /* [[registry]] entry symbols: LOCAL STT_OBJECT pointers living in their
       cyreg_<NAME> section.  Local binding lets several objects each define
       their own entries without duplicate-symbol clashes; the linker still
       merges the sections and provides the __start_/__stop_ anchors. */
    for (size_t i = 0; i < n_cyr_syms; i++) {
        Elf64_Sym s = {0};
        s.st_name = strtab_add(&strtab, &strtab_size, &strtab_cap, cyr_syms[i].name);
        s.st_info = ELF64_ST_INFO(STB_LOCAL, STT_OBJECT);
        s.st_shndx = (uint16_t) cyr[cyr_syms[i].sec].sec_idx;
        s.st_value = cyr_syms[i].off;
        s.st_size = 8;
        SYMTAB_PUSH(s);
    }

    /* Imported / external undefined symbols.
     * Also add any reloc symbol not yet in sym_map. */
    for (size_t i = 0; i < imports.n; i++) {
        int found = 0;
        { size_t _sm; SYM_MAP_FIND(imports.names[i], found, _sm); (void) _sm; }
        if (found) continue;
        Elf64_Sym s = {0};
        s.st_name = strtab_add(&strtab, &strtab_size, &strtab_cap, imports.names[i]);
        s.st_info = ELF64_ST_INFO(STB_GLOBAL, STT_NOTYPE);
        s.st_shndx = SHN_UNDEF;
        SYM_MAP_ADD(imports.names[i], n_syms);
        SYMTAB_PUSH(s);
    }

    /* Also ensure every reloc symbol that isn't yet in sym_map gets added as UNDEF */
    for (size_t i = 0; i < n_relocs; i++) {
        const char *rname = relocs[i].symbol;
        int found = 0;
        { size_t _sm; SYM_MAP_FIND(rname, found, _sm); (void) _sm; }
        if (found) continue;
        Elf64_Sym s = {0};
        s.st_name = strtab_add(&strtab, &strtab_size, &strtab_cap, rname);
        s.st_info = ELF64_ST_INFO(STB_GLOBAL, STT_NOTYPE);
        s.st_shndx = SHN_UNDEF;
        SYM_MAP_ADD(rname, n_syms);
        SYMTAB_PUSH(s);
    }

    /* Re-sort symtab so all locals come before globals (ELF requirement).
     * We built it that way, but the func/data symbols that are not exported
     * are LOCAL and were placed after the section symbols but before we knew
     * about all globals.  Let's just do a stable partition. */
    /* Actually, let's recount first_global properly. */
    {
        /* Partition: move all locals to the front, globals after */
        Elf64_Sym *sorted = calloc(n_syms, sizeof(Elf64_Sym));
        sym_map_entry_t *sorted_map = calloc(n_sym_map, sizeof(sym_map_entry_t));
        size_t *old_to_new = calloc(n_syms, sizeof(size_t));
        size_t out = 0;
        /* Pass 1: locals */
        for (size_t i = 0; i < n_syms; i++) {
            if (ELF64_ST_BIND(symtab[i].st_info) == STB_LOCAL) {
                old_to_new[i] = out;
                sorted[out++] = symtab[i];
            }
        }
        first_global = out;
        /* Pass 2: globals */
        for (size_t i = 0; i < n_syms; i++) {
            if (ELF64_ST_BIND(symtab[i].st_info) != STB_LOCAL) {
                old_to_new[i] = out;
                sorted[out++] = symtab[i];
            }
        }
        /* Update sym_map indices */
        for (size_t i = 0; i < n_sym_map; i++) {
            sym_map[i].idx = old_to_new[sym_map[i].idx];
        }
        memcpy(symtab, sorted, n_syms * sizeof(Elf64_Sym));
        free(sorted);
        free(sorted_map);
        free(old_to_new);
    }

    /* ----- Build .rela.text, .rela.data, .rela.mir.addrpool ----- */
    size_t n_rela_text = 0, n_rela_data = 0, n_rela_pool = 0;
    for (size_t i = 0; i < n_relocs; i++) {
        if (relocs[i].sect == 1) n_rela_data++;
        else if (relocs[i].sect == 2) n_rela_pool++;
        else n_rela_text++;
    }

    Elf64_Rela *rela_text = calloc(n_rela_text ? n_rela_text : 1, sizeof(Elf64_Rela));
    Elf64_Rela *rela_data = calloc(n_rela_data ? n_rela_data : 1, sizeof(Elf64_Rela));
    Elf64_Rela *rela_pool = calloc(n_rela_pool ? n_rela_pool : 1, sizeof(Elf64_Rela));
    size_t rt_idx = 0, rd_idx = 0, rp_idx = 0;

    for (size_t i = 0; i < n_relocs; i++) {
        /* Find symbol index */
        size_t sym_idx = 0;
        { int _f; size_t _sm; SYM_MAP_FIND(relocs[i].symbol, _f, _sm); \
          if (_f) sym_idx = sym_map[_sm].idx; }
        Elf64_Rela r = {0};
        r.r_offset = relocs[i].offset;
        r.r_info = ELF64_R_INFO(sym_idx, relocs[i].type);
        r.r_addend = relocs[i].addend;
        if (relocs[i].sect == 1)
            rela_data[rd_idx++] = r;
        else if (relocs[i].sect == 2)
            rela_pool[rp_idx++] = r;
        else
            rela_text[rt_idx++] = r;
    }

    /* Resolve [[registry]] section relocations (symtab indices are final). */
    for (size_t k = 0; k < n_cyr; k++) {
        cyr[k].rbuf = cyr[k].nrel ? calloc(cyr[k].nrel, sizeof(Elf64_Rela)) : NULL;
        for (size_t r = 0; r < cyr[k].nrel; r++) {
            size_t sym_idx = 0;
            for (size_t j = 0; j < n_sym_map; j++)
                if (strcmp(sym_map[j].name, cyr[k].rel[r].sym) == 0) { sym_idx = sym_map[j].idx; break; }
            cyr[k].rbuf[r].r_offset = cyr[k].rel[r].off;
#if defined(__aarch64__)
            cyr[k].rbuf[r].r_info   = ELF64_R_INFO(sym_idx, R_AARCH64_ABS64);
#else
            cyr[k].rbuf[r].r_info   = ELF64_R_INFO(sym_idx, R_X86_64_64);
#endif
            cyr[k].rbuf[r].r_addend = cyr[k].rel[r].add;
        }
    }

    DBG("phase 3 done: %zu symbols", n_syms);

    /* ----- Phase 3b: Build DWARF debug sections (if debug info present) ----- */
    dwbuf_t dw_abbrev, dw_info, dw_line, dw_str, dw_frame;
    dwbuf_init(&dw_abbrev);
    dwbuf_init(&dw_info);
    dwbuf_init(&dw_line);
    dwbuf_init(&dw_str);
    dwbuf_init(&dw_frame);
    int has_dwarf = 0;
    dwarf_frame_reloc_t *frame_relocs = NULL;
    size_t n_frame_relocs = 0;

    /* DWARF relocation tracking: records (offset_in_dwarf_section, addend_in_text, is_line) */
    typedef struct { size_t offset; int64_t addend; int in_line; /* 0=.debug_info, 1=.debug_line */ } dwarf_reloc_t;
    dwarf_reloc_t *dw_relocs = NULL;
    size_t n_dw_relocs = 0, cap_dw_relocs = 0;
    #define DW_RELOC_PUSH(off, add, sect) do { \
        if (n_dw_relocs >= cap_dw_relocs) { \
            cap_dw_relocs = cap_dw_relocs ? cap_dw_relocs * 2 : 32; \
            dw_relocs = realloc(dw_relocs, cap_dw_relocs * sizeof(dwarf_reloc_t)); \
        } \
        dw_relocs[n_dw_relocs++] = (dwarf_reloc_t){(off), (add), (sect)}; \
    } while(0)

#if !MIR_NO_DBINFO
    {
    /* Check if any module has debug info */
    MIR_module_t mod = DLIST_HEAD(MIR_module_t, *MIR_get_module_list(ctx));
    if (mod != NULL && mod->num_source_files > 0) {
        has_dwarf = 1;
        DBG("phase 3b: generating DWARF debug sections");

        /* --- .debug_str: collect all strings --- */
        /* offset 0 = empty string */
        dwbuf_u8(&dw_str, 0);
        /* We'll use inline strings (DW_FORM_string) for simplicity,
           but populate .debug_str for the producer and comp_dir. */
        size_t str_producer_off = dw_str.len;
        dwbuf_str(&dw_str, "classyc (MIR)");
        size_t str_compdir_off = dw_str.len;
        {
            char cwd[1024];
            if (getcwd(cwd, sizeof(cwd)) == NULL) strcpy(cwd, ".");
            dwbuf_str(&dw_str, cwd);
        }

        /* --- .debug_abbrev --- */
        /* Abbreviation 1: DW_TAG_compile_unit (has children) */
        dwbuf_uleb(&dw_abbrev, 1); /* abbrev code */
        dwbuf_uleb(&dw_abbrev, DW_TAG_compile_unit);
        dwbuf_u8(&dw_abbrev, DW_CHILDREN_yes);
        dwbuf_uleb(&dw_abbrev, DW_AT_producer);  dwbuf_uleb(&dw_abbrev, DW_FORM_string);
        dwbuf_uleb(&dw_abbrev, DW_AT_language);   dwbuf_uleb(&dw_abbrev, DW_FORM_data2);
        dwbuf_uleb(&dw_abbrev, DW_AT_name);       dwbuf_uleb(&dw_abbrev, DW_FORM_string);
        dwbuf_uleb(&dw_abbrev, DW_AT_comp_dir);   dwbuf_uleb(&dw_abbrev, DW_FORM_string);
        dwbuf_uleb(&dw_abbrev, DW_AT_low_pc);     dwbuf_uleb(&dw_abbrev, DW_FORM_addr);
        dwbuf_uleb(&dw_abbrev, DW_AT_high_pc);    dwbuf_uleb(&dw_abbrev, DW_FORM_data8);
        dwbuf_uleb(&dw_abbrev, DW_AT_stmt_list);  dwbuf_uleb(&dw_abbrev, DW_FORM_sec_offset);
        dwbuf_uleb(&dw_abbrev, 0); dwbuf_uleb(&dw_abbrev, 0); /* end attrs */

        /* Abbreviation 2: DW_TAG_subprogram (has children — for params/vars) */
        dwbuf_uleb(&dw_abbrev, 2);
        dwbuf_uleb(&dw_abbrev, DW_TAG_subprogram);
        dwbuf_u8(&dw_abbrev, DW_CHILDREN_yes);
        dwbuf_uleb(&dw_abbrev, DW_AT_name);        dwbuf_uleb(&dw_abbrev, DW_FORM_string);
        dwbuf_uleb(&dw_abbrev, DW_AT_low_pc);      dwbuf_uleb(&dw_abbrev, DW_FORM_addr);
        dwbuf_uleb(&dw_abbrev, DW_AT_high_pc);     dwbuf_uleb(&dw_abbrev, DW_FORM_data8);
        dwbuf_uleb(&dw_abbrev, DW_AT_frame_base);  dwbuf_uleb(&dw_abbrev, DW_FORM_exprloc);
        dwbuf_uleb(&dw_abbrev, DW_AT_decl_file);   dwbuf_uleb(&dw_abbrev, DW_FORM_udata);
        dwbuf_uleb(&dw_abbrev, DW_AT_decl_line);   dwbuf_uleb(&dw_abbrev, DW_FORM_udata);
        dwbuf_uleb(&dw_abbrev, DW_AT_external);    dwbuf_uleb(&dw_abbrev, DW_FORM_flag_present);
        dwbuf_uleb(&dw_abbrev, 0); dwbuf_uleb(&dw_abbrev, 0);

        /* Abbreviation 3: DW_TAG_formal_parameter (no children) */
        dwbuf_uleb(&dw_abbrev, 3);
        dwbuf_uleb(&dw_abbrev, DW_TAG_formal_parameter);
        dwbuf_u8(&dw_abbrev, DW_CHILDREN_no);
        dwbuf_uleb(&dw_abbrev, DW_AT_name);      dwbuf_uleb(&dw_abbrev, DW_FORM_string);
        dwbuf_uleb(&dw_abbrev, DW_AT_decl_line); dwbuf_uleb(&dw_abbrev, DW_FORM_udata);
        dwbuf_uleb(&dw_abbrev, DW_AT_type);      dwbuf_uleb(&dw_abbrev, DW_FORM_ref4);
        dwbuf_uleb(&dw_abbrev, DW_AT_location);  dwbuf_uleb(&dw_abbrev, DW_FORM_exprloc);
        dwbuf_uleb(&dw_abbrev, 0); dwbuf_uleb(&dw_abbrev, 0);

        /* Abbreviation 4: DW_TAG_variable (no children) */
        dwbuf_uleb(&dw_abbrev, 4);
        dwbuf_uleb(&dw_abbrev, DW_TAG_variable);
        dwbuf_u8(&dw_abbrev, DW_CHILDREN_no);
        dwbuf_uleb(&dw_abbrev, DW_AT_name);      dwbuf_uleb(&dw_abbrev, DW_FORM_string);
        dwbuf_uleb(&dw_abbrev, DW_AT_decl_line); dwbuf_uleb(&dw_abbrev, DW_FORM_udata);
        dwbuf_uleb(&dw_abbrev, DW_AT_type);      dwbuf_uleb(&dw_abbrev, DW_FORM_ref4);
        dwbuf_uleb(&dw_abbrev, DW_AT_location);  dwbuf_uleb(&dw_abbrev, DW_FORM_exprloc);
        dwbuf_uleb(&dw_abbrev, 0); dwbuf_uleb(&dw_abbrev, 0);

        /* Abbreviation 5: DW_TAG_base_type (no children) */
        dwbuf_uleb(&dw_abbrev, 5);
        dwbuf_uleb(&dw_abbrev, DW_TAG_base_type);
        dwbuf_u8(&dw_abbrev, DW_CHILDREN_no);
        dwbuf_uleb(&dw_abbrev, DW_AT_name);      dwbuf_uleb(&dw_abbrev, DW_FORM_string);
        dwbuf_uleb(&dw_abbrev, DW_AT_byte_size); dwbuf_uleb(&dw_abbrev, DW_FORM_udata);
        dwbuf_uleb(&dw_abbrev, DW_AT_encoding);  dwbuf_uleb(&dw_abbrev, DW_FORM_data1);
        dwbuf_uleb(&dw_abbrev, 0); dwbuf_uleb(&dw_abbrev, 0);

        /* ---- Type-DIE abbreviations (consumed by emit_type_die below) ---- */
        /* 6: pointer_type with pointee type */
        dwbuf_uleb(&dw_abbrev, 6);
        dwbuf_uleb(&dw_abbrev, DW_TAG_pointer_type);
        dwbuf_u8(&dw_abbrev, DW_CHILDREN_no);
        dwbuf_uleb(&dw_abbrev, DW_AT_byte_size); dwbuf_uleb(&dw_abbrev, DW_FORM_udata);
        dwbuf_uleb(&dw_abbrev, DW_AT_type);      dwbuf_uleb(&dw_abbrev, DW_FORM_ref4);
        dwbuf_uleb(&dw_abbrev, 0); dwbuf_uleb(&dw_abbrev, 0);

        /* 7: pointer_type to void (no DW_AT_type) */
        dwbuf_uleb(&dw_abbrev, 7);
        dwbuf_uleb(&dw_abbrev, DW_TAG_pointer_type);
        dwbuf_u8(&dw_abbrev, DW_CHILDREN_no);
        dwbuf_uleb(&dw_abbrev, DW_AT_byte_size); dwbuf_uleb(&dw_abbrev, DW_FORM_udata);
        dwbuf_uleb(&dw_abbrev, 0); dwbuf_uleb(&dw_abbrev, 0);

        /* 8: typedef */
        dwbuf_uleb(&dw_abbrev, 8);
        dwbuf_uleb(&dw_abbrev, DW_TAG_typedef);
        dwbuf_u8(&dw_abbrev, DW_CHILDREN_no);
        dwbuf_uleb(&dw_abbrev, DW_AT_name); dwbuf_uleb(&dw_abbrev, DW_FORM_string);
        dwbuf_uleb(&dw_abbrev, DW_AT_type); dwbuf_uleb(&dw_abbrev, DW_FORM_ref4);
        dwbuf_uleb(&dw_abbrev, 0); dwbuf_uleb(&dw_abbrev, 0);

        /* 9/10/11: const / volatile / restrict qualified types */
        dwbuf_uleb(&dw_abbrev, 9);
        dwbuf_uleb(&dw_abbrev, DW_TAG_const_type);
        dwbuf_u8(&dw_abbrev, DW_CHILDREN_no);
        dwbuf_uleb(&dw_abbrev, DW_AT_type); dwbuf_uleb(&dw_abbrev, DW_FORM_ref4);
        dwbuf_uleb(&dw_abbrev, 0); dwbuf_uleb(&dw_abbrev, 0);
        dwbuf_uleb(&dw_abbrev, 10);
        dwbuf_uleb(&dw_abbrev, DW_TAG_volatile_type);
        dwbuf_u8(&dw_abbrev, DW_CHILDREN_no);
        dwbuf_uleb(&dw_abbrev, DW_AT_type); dwbuf_uleb(&dw_abbrev, DW_FORM_ref4);
        dwbuf_uleb(&dw_abbrev, 0); dwbuf_uleb(&dw_abbrev, 0);
        dwbuf_uleb(&dw_abbrev, 11);
        dwbuf_uleb(&dw_abbrev, DW_TAG_restrict_type);
        dwbuf_u8(&dw_abbrev, DW_CHILDREN_no);
        dwbuf_uleb(&dw_abbrev, DW_AT_type); dwbuf_uleb(&dw_abbrev, DW_FORM_ref4);
        dwbuf_uleb(&dw_abbrev, 0); dwbuf_uleb(&dw_abbrev, 0);

        /* 12/13: structure / union type (have children: members) */
        dwbuf_uleb(&dw_abbrev, 12);
        dwbuf_uleb(&dw_abbrev, DW_TAG_structure_type);
        dwbuf_u8(&dw_abbrev, DW_CHILDREN_yes);
        dwbuf_uleb(&dw_abbrev, DW_AT_name);      dwbuf_uleb(&dw_abbrev, DW_FORM_string);
        dwbuf_uleb(&dw_abbrev, DW_AT_byte_size); dwbuf_uleb(&dw_abbrev, DW_FORM_udata);
        dwbuf_uleb(&dw_abbrev, 0); dwbuf_uleb(&dw_abbrev, 0);
        dwbuf_uleb(&dw_abbrev, 13);
        dwbuf_uleb(&dw_abbrev, DW_TAG_union_type);
        dwbuf_u8(&dw_abbrev, DW_CHILDREN_yes);
        dwbuf_uleb(&dw_abbrev, DW_AT_name);      dwbuf_uleb(&dw_abbrev, DW_FORM_string);
        dwbuf_uleb(&dw_abbrev, DW_AT_byte_size); dwbuf_uleb(&dw_abbrev, DW_FORM_udata);
        dwbuf_uleb(&dw_abbrev, 0); dwbuf_uleb(&dw_abbrev, 0);

        /* 14: member */
        dwbuf_uleb(&dw_abbrev, 14);
        dwbuf_uleb(&dw_abbrev, DW_TAG_member);
        dwbuf_u8(&dw_abbrev, DW_CHILDREN_no);
        dwbuf_uleb(&dw_abbrev, DW_AT_name);                 dwbuf_uleb(&dw_abbrev, DW_FORM_string);
        dwbuf_uleb(&dw_abbrev, DW_AT_type);                 dwbuf_uleb(&dw_abbrev, DW_FORM_ref4);
        dwbuf_uleb(&dw_abbrev, DW_AT_data_member_location); dwbuf_uleb(&dw_abbrev, DW_FORM_udata);
        dwbuf_uleb(&dw_abbrev, 0); dwbuf_uleb(&dw_abbrev, 0);

        /* 15: enumeration_type (children: enumerators) */
        dwbuf_uleb(&dw_abbrev, 15);
        dwbuf_uleb(&dw_abbrev, DW_TAG_enumeration_type);
        dwbuf_u8(&dw_abbrev, DW_CHILDREN_yes);
        dwbuf_uleb(&dw_abbrev, DW_AT_name);      dwbuf_uleb(&dw_abbrev, DW_FORM_string);
        dwbuf_uleb(&dw_abbrev, DW_AT_byte_size); dwbuf_uleb(&dw_abbrev, DW_FORM_udata);
        dwbuf_uleb(&dw_abbrev, DW_AT_type);      dwbuf_uleb(&dw_abbrev, DW_FORM_ref4);
        dwbuf_uleb(&dw_abbrev, 0); dwbuf_uleb(&dw_abbrev, 0);

        /* 16: enumerator */
        dwbuf_uleb(&dw_abbrev, 16);
        dwbuf_uleb(&dw_abbrev, DW_TAG_enumerator);
        dwbuf_u8(&dw_abbrev, DW_CHILDREN_no);
        dwbuf_uleb(&dw_abbrev, DW_AT_name);        dwbuf_uleb(&dw_abbrev, DW_FORM_string);
        dwbuf_uleb(&dw_abbrev, DW_AT_const_value); dwbuf_uleb(&dw_abbrev, DW_FORM_sdata);
        dwbuf_uleb(&dw_abbrev, 0); dwbuf_uleb(&dw_abbrev, 0);

        /* 17: array_type (children: subrange) */
        dwbuf_uleb(&dw_abbrev, 17);
        dwbuf_uleb(&dw_abbrev, DW_TAG_array_type);
        dwbuf_u8(&dw_abbrev, DW_CHILDREN_yes);
        dwbuf_uleb(&dw_abbrev, DW_AT_type); dwbuf_uleb(&dw_abbrev, DW_FORM_ref4);
        dwbuf_uleb(&dw_abbrev, 0); dwbuf_uleb(&dw_abbrev, 0);

        /* 18: subrange_type with known upper bound */
        dwbuf_uleb(&dw_abbrev, 18);
        dwbuf_uleb(&dw_abbrev, DW_TAG_subrange_type);
        dwbuf_u8(&dw_abbrev, DW_CHILDREN_no);
        dwbuf_uleb(&dw_abbrev, DW_AT_upper_bound); dwbuf_uleb(&dw_abbrev, DW_FORM_udata);
        dwbuf_uleb(&dw_abbrev, 0); dwbuf_uleb(&dw_abbrev, 0);

        /* 19: subrange_type with unknown bound (no attrs) */
        dwbuf_uleb(&dw_abbrev, 19);
        dwbuf_uleb(&dw_abbrev, DW_TAG_subrange_type);
        dwbuf_u8(&dw_abbrev, DW_CHILDREN_no);
        dwbuf_uleb(&dw_abbrev, 0); dwbuf_uleb(&dw_abbrev, 0);

        /* 20: unspecified_type (used for void references) */
        dwbuf_uleb(&dw_abbrev, 20);
        dwbuf_uleb(&dw_abbrev, DW_TAG_unspecified_type);
        dwbuf_u8(&dw_abbrev, DW_CHILDREN_no);
        dwbuf_uleb(&dw_abbrev, 0); dwbuf_uleb(&dw_abbrev, 0);

        /* Null terminator for abbreviation table */
        dwbuf_uleb(&dw_abbrev, 0);

        /* --- .debug_info: compilation unit header + DIEs --- */
        size_t cu_start = dw_info.len;
        dwbuf_u32(&dw_info, 0); /* unit_length placeholder */
        dwbuf_u16(&dw_info, 4); /* DWARF version 4 */
        dwbuf_u32(&dw_info, 0); /* debug_abbrev_offset */
        dwbuf_u8(&dw_info, 8);  /* address_size = 8 (x86_64) */

        /* DIE: compile_unit (abbrev 1) */
        dwbuf_uleb(&dw_info, 1); /* abbrev code 1 */
        dwbuf_str(&dw_info, "classyc (MIR)"); /* DW_AT_producer */
        dwbuf_u16(&dw_info, DW_LANG_C11); /* DW_AT_language */
        /* DW_AT_name: use first real source file */
        const char *cu_name = mod->num_source_files >= 1 ? mod->source_files[1] : "<unknown>";
        /* Skip "<environment>" if there are more files */
        for (uint32_t fi = 1; fi <= mod->num_source_files; fi++) {
            if (mod->source_files[fi] != NULL && mod->source_files[fi][0] != '<') {
                cu_name = mod->source_files[fi];
                break;
            }
        }
        dwbuf_str(&dw_info, cu_name);
        { /* DW_AT_comp_dir */
            char cwd[1024];
            if (getcwd(cwd, sizeof(cwd)) == NULL) strcpy(cwd, ".");
            dwbuf_str(&dw_info, cwd);
        }
        /* DW_AT_low_pc = 0 (relocatable — needs R_X86_64_64 reloc to .text) */
        DW_RELOC_PUSH(dw_info.len, 0, 0); /* reloc at current offset, addend=0, in .debug_info */
        dwbuf_u64(&dw_info, 0);
        /* DW_AT_high_pc = text_size (length form — not an address, no reloc needed) */
        dwbuf_u64(&dw_info, text_size);
        /* DW_AT_stmt_list = 0 (offset into .debug_line) */
        dwbuf_u32(&dw_info, 0);

        /* ---- Type DIEs ----
           Every module debug-type id gets a DIE here (as a child of the
           compile unit).  Variables, parameters and struct members reference
           them through DW_AT_type (DW_FORM_ref4, a CU-relative offset).  Those
           offsets are recorded as fixups and patched once every type DIE has
           been placed (forward references are therefore fine). */
        MIR_dbtype_table_t *dbtypes = mod->dbtypes;
        uint32_t num_types = dbtypes != NULL ? dbtypes->num_types : 0;
        size_t *type_off = NULL; /* CU-relative offset of each type's DIE */
        struct dwtype_fixup { size_t pos; uint32_t id; } *tfix = NULL;
        size_t n_tfix = 0, cap_tfix = 0;
#define TYPE_REF(TID)                                                              \
        do {                                                                       \
            if (n_tfix == cap_tfix) {                                              \
                cap_tfix = cap_tfix ? cap_tfix * 2 : 64;                           \
                tfix = realloc(tfix, cap_tfix * sizeof(*tfix));                    \
            }                                                                      \
            tfix[n_tfix].pos = dw_info.len; tfix[n_tfix].id = (uint32_t)(TID);     \
            n_tfix++;                                                              \
            dwbuf_u32(&dw_info, 0);                                                \
        } while (0)

        /* unspecified_type DIE: target for void and any unresolved reference */
        size_t void_off = dw_info.len - cu_start;
        dwbuf_uleb(&dw_info, 20);
        if (num_types > 0) {
            type_off = malloc(num_types * sizeof(size_t));
            for (uint32_t i = 0; i < num_types; i++) type_off[i] = void_off;
            for (uint32_t id = 1; id < num_types; id++) {
                MIR_dbtype_t *t = &dbtypes->types[id];
                type_off[id] = dw_info.len - cu_start;
                switch (t->kind) {
                case MIR_DBT_BASE:
                    dwbuf_uleb(&dw_info, 5);
                    dwbuf_str(&dw_info, t->name ? t->name : "");
                    dwbuf_uleb(&dw_info, t->byte_size);
                    dwbuf_u8(&dw_info, (uint8_t)(t->u.base.encoding ? t->u.base.encoding
                                                                    : DW_ATE_signed));
                    break;
                case MIR_DBT_PTR:
                    if (t->u.ref.target_id == 0) {
                        dwbuf_uleb(&dw_info, 7); /* pointer to void */
                        dwbuf_uleb(&dw_info, t->byte_size ? t->byte_size : 8);
                    } else {
                        dwbuf_uleb(&dw_info, 6);
                        dwbuf_uleb(&dw_info, t->byte_size ? t->byte_size : 8);
                        TYPE_REF(t->u.ref.target_id);
                    }
                    break;
                case MIR_DBT_TYPEDEF:
                    dwbuf_uleb(&dw_info, 8);
                    dwbuf_str(&dw_info, t->name ? t->name : "");
                    TYPE_REF(t->u.ref.target_id);
                    break;
                case MIR_DBT_CONST:
                    dwbuf_uleb(&dw_info, 9); TYPE_REF(t->u.ref.target_id); break;
                case MIR_DBT_VOLATILE:
                    dwbuf_uleb(&dw_info, 10); TYPE_REF(t->u.ref.target_id); break;
                case MIR_DBT_RESTRICT:
                    dwbuf_uleb(&dw_info, 11); TYPE_REF(t->u.ref.target_id); break;
                case MIR_DBT_STRUCT:
                case MIR_DBT_UNION:
                    dwbuf_uleb(&dw_info, t->kind == MIR_DBT_STRUCT ? 12 : 13);
                    dwbuf_str(&dw_info, t->name ? t->name : "");
                    dwbuf_uleb(&dw_info, t->byte_size);
                    for (uint32_t mi = 0; mi < t->u.aggregate.num_members; mi++) {
                        MIR_dbmember_t *mb = &t->u.aggregate.members[mi];
                        dwbuf_uleb(&dw_info, 14);
                        dwbuf_str(&dw_info, mb->name ? mb->name : "");
                        TYPE_REF(mb->type_id);
                        dwbuf_uleb(&dw_info, mb->byte_offset);
                    }
                    dwbuf_u8(&dw_info, 0); /* end of members */
                    break;
                case MIR_DBT_ENUM:
                    dwbuf_uleb(&dw_info, 15);
                    dwbuf_str(&dw_info, t->name ? t->name : "");
                    dwbuf_uleb(&dw_info, t->byte_size ? t->byte_size : 4);
                    TYPE_REF(t->u.enumeration.underlying_id);
                    for (uint32_t ei = 0; ei < t->u.enumeration.num_enumerators; ei++) {
                        MIR_dbenumerator_t *en = &t->u.enumeration.enumerators[ei];
                        dwbuf_uleb(&dw_info, 16);
                        dwbuf_str(&dw_info, en->name ? en->name : "");
                        dwbuf_sleb(&dw_info, en->value);
                    }
                    dwbuf_u8(&dw_info, 0); /* end of enumerators */
                    break;
                case MIR_DBT_ARRAY:
                    dwbuf_uleb(&dw_info, 17);
                    TYPE_REF(t->u.array.element_id);
                    if (t->u.array.count >= 0) { /* upper_bound = count - 1 */
                        dwbuf_uleb(&dw_info, 18);
                        dwbuf_uleb(&dw_info,
                                   (uint64_t)(t->u.array.count > 0 ? t->u.array.count - 1 : 0));
                    } else {
                        dwbuf_uleb(&dw_info, 19); /* unknown bound */
                    }
                    dwbuf_u8(&dw_info, 0); /* end of subranges */
                    break;
                default:
                    /* MIR_DBT_FUNC / unknown: fall back to void (unspecified). */
                    type_off[id] = void_off;
                    break;
                }
            }
        }

        /* Emit a DW_TAG_subprogram for each function */
        for (size_t fi = 0; fi < n_funcs; fi++) {
            func_entry_t *fe = &funcs[fi];
            MIR_func_t func = fe->item->u.func;

            dwbuf_uleb(&dw_info, 2); /* abbrev 2: subprogram */
            dwbuf_str(&dw_info, dwarf_display_name(fe->name)); /* DW_AT_name */
            DW_RELOC_PUSH(dw_info.len, (int64_t)fe->text_offset, 0); /* reloc to .text + offset */
            dwbuf_u64(&dw_info, 0); /* DW_AT_low_pc (relocated) */
            dwbuf_u64(&dw_info, fe->code_len); /* DW_AT_high_pc (length, not relocated) */
            /* DW_AT_frame_base: DW_OP_call_frame_cfa (1 byte expr) */
            dwbuf_uleb(&dw_info, 1); /* exprloc length */
            dwbuf_u8(&dw_info, DW_OP_call_frame_cfa);
            /* DW_AT_decl_file */
            dwbuf_uleb(&dw_info, 1); /* file index 1 */
            /* DW_AT_decl_line — find first source line from insns */
            {
                uint32_t first_line = 0;
                for (MIR_insn_t insn = DLIST_HEAD(MIR_insn_t, func->insns);
                     insn != NULL; insn = DLIST_NEXT(MIR_insn_t, insn)) {
                    if (insn->source_line != 0) { first_line = insn->source_line; break; }
                }
                dwbuf_uleb(&dw_info, first_line);
            }
            /* DW_AT_external = flag_present (implicit true) */

            /* Emit params and vars from dbinfo */
            if (func->dbinfo != NULL) {
                for (uint32_t vi = 0; vi < func->dbinfo->num_vars; vi++) {
                    MIR_dbvar_t *v = &func->dbinfo->vars[vi];
                    if (v->source_name == NULL) continue;
                    /* Use abbrev 3 for params, abbrev 4 for vars */
                    dwbuf_uleb(&dw_info, v->is_param ? 3 : 4);
                    dwbuf_str(&dw_info, v->source_name); /* DW_AT_name */
                    dwbuf_uleb(&dw_info, v->decl_line); /* DW_AT_decl_line */
                    TYPE_REF(v->type_id); /* DW_AT_type (patched below) */
                    /* DW_AT_location.
                       Prefer the machine location resolved by the code
                       generator (mir-gen, after register allocation); fall
                       back to the front-end's MIR-level intent. */
                    dwbuf_t loc; dwbuf_init(&loc);
                    if (v->mach_kind == MIR_DBMACH_MEM) {
                        /* [base reg + offset]: DW_OP_bregN <sleb offset> */
                        if (v->mach_reg < 32) {
                            dwbuf_u8(&loc, (uint8_t)(DW_OP_breg0 + v->mach_reg));
                        } else {
                            dwbuf_u8(&loc, DW_OP_bregx);
                            dwbuf_uleb(&loc, v->mach_reg);
                        }
                        dwbuf_sleb(&loc, v->mach_offset);
                        if (v->mach_deref) {
                            /* Aggregate reached through a spilled frame pointer:
                               [[base+offset]] + offset2. */
                            dwbuf_u8(&loc, DW_OP_deref);
                            if (v->mach_offset2 != 0) {
                                dwbuf_u8(&loc, DW_OP_plus_uconst);
                                dwbuf_uleb(&loc, (uint64_t)(uint32_t) v->mach_offset2);
                            }
                        }
                    } else if (v->mach_kind == MIR_DBMACH_REG) {
                        /* Whole value in a register: DW_OP_regN */
                        if (v->mach_reg < 32) {
                            dwbuf_u8(&loc, (uint8_t)(DW_OP_reg0 + v->mach_reg));
                        } else {
                            dwbuf_u8(&loc, DW_OP_regx);
                            dwbuf_uleb(&loc, v->mach_reg);
                        }
                    } else if (v->loc_kind == MIR_DBLOC_FRAME) {
                        /* DW_OP_fbreg <offset> */
                        dwbuf_u8(&loc, DW_OP_fbreg);
                        dwbuf_sleb(&loc, v->loc.frame_offset);
                    }
                    dwbuf_uleb(&dw_info, loc.len); /* exprloc length (0 = unavailable) */
                    if (loc.len > 0) dwbuf_bytes(&dw_info, loc.data, loc.len);
                    dwbuf_free(&loc);
                }
            }

            dwbuf_u8(&dw_info, 0); /* end of subprogram children */
        }

        dwbuf_u8(&dw_info, 0); /* end of compile_unit children */

        /* Resolve every DW_AT_type reference now that all type DIE offsets are
           known.  Out-of-range ids degrade to the void/unspecified DIE. */
        for (size_t i = 0; i < n_tfix; i++) {
            uint32_t off = (type_off != NULL && tfix[i].id < num_types)
                             ? (uint32_t) type_off[tfix[i].id]
                             : (uint32_t) void_off;
            dwbuf_patch_u32(&dw_info, tfix[i].pos, off);
        }
        free(tfix);
        free(type_off);
#undef TYPE_REF

        /* Patch CU length (excludes the 4-byte length field itself) */
        uint32_t cu_len = (uint32_t)(dw_info.len - cu_start - 4);
        dwbuf_patch_u32(&dw_info, cu_start, cu_len);

        /* --- .debug_line: DWARF4 line number program --- */
        /* Build a minimal line table from instruction source locations.
           For each function, walk insns and emit set_address + advance_line + copy. */
        size_t line_start = dw_line.len;
        dwbuf_u32(&dw_line, 0); /* total_length placeholder */
        dwbuf_u16(&dw_line, 4); /* DWARF version 4 */
        size_t header_length_off = dw_line.len;
        dwbuf_u32(&dw_line, 0); /* header_length placeholder */
        size_t after_header_len = dw_line.len;
        dwbuf_u8(&dw_line, 1);  /* minimum_instruction_length */
        dwbuf_u8(&dw_line, 1);  /* maximum_operations_per_instruction */
        dwbuf_u8(&dw_line, 1);  /* default_is_stmt */
        dwbuf_u8(&dw_line, (uint8_t)(int8_t)-5); /* line_base */
        dwbuf_u8(&dw_line, 14); /* line_range */
        dwbuf_u8(&dw_line, 13); /* opcode_base */
        /* standard_opcode_lengths: opcodes 1..12 */
        dwbuf_u8(&dw_line, 0); /* copy */
        dwbuf_u8(&dw_line, 1); /* advance_pc */
        dwbuf_u8(&dw_line, 1); /* advance_line */
        dwbuf_u8(&dw_line, 1); /* set_file */
        dwbuf_u8(&dw_line, 1); /* set_column */
        dwbuf_u8(&dw_line, 0); /* negate_stmt */
        dwbuf_u8(&dw_line, 0); /* set_basic_block */
        dwbuf_u8(&dw_line, 0); /* const_add_pc */
        dwbuf_u8(&dw_line, 1); /* fixed_advance_pc */
        dwbuf_u8(&dw_line, 0); /* set_prologue_end */
        dwbuf_u8(&dw_line, 0); /* set_epilogue_begin */
        dwbuf_u8(&dw_line, 1); /* set_isa */

        /* include_directories: just null terminator (no include dirs) */
        dwbuf_u8(&dw_line, 0);

        /* file_names table */
        for (uint32_t fi = 1; fi <= mod->num_source_files; fi++) {
            const char *fn = mod->source_files[fi];
            if (fn == NULL) fn = "?";
            dwbuf_str(&dw_line, fn);  /* file name */
            dwbuf_uleb(&dw_line, 0); /* directory index 0 */
            dwbuf_uleb(&dw_line, 0); /* last modification time */
            dwbuf_uleb(&dw_line, 0); /* file size */
        }
        dwbuf_u8(&dw_line, 0); /* end of file_names */

        /* Patch header_length */
        uint32_t hdr_len = (uint32_t)(dw_line.len - after_header_len);
        dwbuf_patch_u32(&dw_line, header_length_off, hdr_len);

        /* Line number program: for each function, use exact PC offsets from the
           line map (captured during target_translate) when available. */
        for (size_t fi = 0; fi < n_funcs; fi++) {
            func_entry_t *fe = &funcs[fi];
            MIR_func_t func = fe->item->u.func;
            MIR_line_map_t *lm = (func->dbinfo != NULL) ? func->dbinfo->line_map : NULL;

            /* Need at least some source info to emit a line sequence */
            uint32_t first_line = 0, first_file = 0;
            if (lm != NULL && lm->num_entries > 0) {
                first_line = lm->entries[0].source_line;
                first_file = lm->entries[0].source_file_id;
            } else {
                for (MIR_insn_t insn = DLIST_HEAD(MIR_insn_t, func->insns);
                     insn != NULL; insn = DLIST_NEXT(MIR_insn_t, insn)) {
                    if (insn->source_line != 0) {
                        first_line = insn->source_line;
                        first_file = insn->source_file_id;
                        break;
                    }
                }
            }
            if (first_line == 0) continue;

            /* DW_LNE_set_address to function start (needs relocation) */
            dwbuf_u8(&dw_line, 0); /* extended opcode escape */
            dwbuf_uleb(&dw_line, 9); /* 1 + 8 bytes */
            dwbuf_u8(&dw_line, DW_LNE_set_address);
            DW_RELOC_PUSH(dw_line.len, (int64_t)fe->text_offset, 1);
            dwbuf_u64(&dw_line, 0); /* address placeholder (relocated) */

            /* Set initial file */
            if (first_file != 0) {
                dwbuf_u8(&dw_line, DW_LNS_set_file);
                dwbuf_uleb(&dw_line, first_file);
            }

            if (lm != NULL && lm->num_entries > 0) {
                /* ---- Exact PC offsets from line map ---- */
                uint32_t prev_pc = 0;
                uint32_t cur_line = 1; /* DWARF line state machine starts at line 1 */
                uint16_t cur_file = first_file;

                for (uint32_t li = 0; li < lm->num_entries; li++) {
                    MIR_line_map_entry_t *e = &lm->entries[li];
                    /* Skip duplicate line entries at same PC */
                    if (li > 0 && e->source_line == lm->entries[li-1].source_line
                               && e->pc_offset == lm->entries[li-1].pc_offset)
                        continue;
                    /* Skip entries with same line as current (unless different file) */
                    if (e->source_line == cur_line && e->source_file_id == cur_file && li > 0)
                        continue;

                    /* Advance PC */
                    if (e->pc_offset > prev_pc) {
                        dwbuf_u8(&dw_line, DW_LNS_advance_pc);
                        dwbuf_uleb(&dw_line, e->pc_offset - prev_pc);
                        prev_pc = e->pc_offset;
                    }

                    /* Set file if changed */
                    if (e->source_file_id != cur_file) {
                        dwbuf_u8(&dw_line, DW_LNS_set_file);
                        dwbuf_uleb(&dw_line, e->source_file_id);
                        cur_file = e->source_file_id;
                    }

                    /* Set column if non-zero */
                    if (e->source_col != 0) {
                        dwbuf_u8(&dw_line, DW_LNS_set_column);
                        dwbuf_uleb(&dw_line, e->source_col);
                    }

                    /* Advance line */
                    int32_t line_delta = (int32_t)e->source_line - (int32_t)cur_line;
                    if (line_delta != 0) {
                        dwbuf_u8(&dw_line, DW_LNS_advance_line);
                        dwbuf_sleb(&dw_line, line_delta);
                    }
                    cur_line = e->source_line;

                    /* Emit row */
                    dwbuf_u8(&dw_line, DW_LNS_copy);
                }

                /* Advance to end of function */
                if (fe->code_len > prev_pc) {
                    dwbuf_u8(&dw_line, DW_LNS_advance_pc);
                    dwbuf_uleb(&dw_line, fe->code_len - prev_pc);
                }
            } else {
                /* ---- Fallback: single line entry at function start ---- */
                dwbuf_u8(&dw_line, DW_LNS_advance_line);
                dwbuf_sleb(&dw_line, (int64_t)first_line - 1);
                dwbuf_u8(&dw_line, DW_LNS_copy);

                dwbuf_u8(&dw_line, DW_LNS_advance_pc);
                dwbuf_uleb(&dw_line, fe->code_len);
            }

            /* End sequence */
            dwbuf_u8(&dw_line, 0); /* extended escape */
            dwbuf_uleb(&dw_line, 1);
            dwbuf_u8(&dw_line, DW_LNE_end_sequence);
        }

        /* Patch total_length */
        uint32_t line_total = (uint32_t)(dw_line.len - line_start - 4);
        dwbuf_patch_u32(&dw_line, line_start, line_total);

        /* --- .debug_frame: CFI so DW_OP_call_frame_cfa / backtraces work --- */
        {
            dwarf_frame_func_t *ff = calloc (n_funcs ? n_funcs : 1, sizeof (dwarf_frame_func_t));
            for (size_t fi = 0; fi < n_funcs; fi++) {
                ff[fi].code = (const uint8_t *) funcs[fi].code;
                ff[fi].code_len = funcs[fi].code_len;
                ff[fi].text_offset = funcs[fi].text_offset;
            }
            dwarf_emit_debug_frame (ff, n_funcs, &dw_frame, &frame_relocs, &n_frame_relocs);
            free (ff);
        }

        DBG("phase 3b done: .debug_info=%zu .debug_abbrev=%zu .debug_line=%zu .debug_str=%zu"
            " .debug_frame=%zu relocs=%zu frame_relocs=%zu",
            dw_info.len, dw_abbrev.len, dw_line.len, dw_str.len, dw_frame.len, n_dw_relocs,
            n_frame_relocs);
    }
    }
#endif /* !MIR_NO_DBINFO */

    /* Even without full DWARF DIEs, emit .debug_frame when we have code so
       linked AOT binaries can unwind MIR frames (CFI does not need -g). */
    if (dw_frame.len == 0 && n_funcs > 0) {
        dwarf_frame_func_t *ff = calloc (n_funcs ? n_funcs : 1, sizeof (dwarf_frame_func_t));
        for (size_t fi = 0; fi < n_funcs; fi++) {
            ff[fi].code = (const uint8_t *) funcs[fi].code;
            ff[fi].code_len = funcs[fi].code_len;
            ff[fi].text_offset = funcs[fi].text_offset;
        }
        dwarf_emit_debug_frame (ff, n_funcs, &dw_frame, &frame_relocs, &n_frame_relocs);
        free (ff);
        DBG ("phase 3b: .debug_frame only (no -g DIE info): %zu bytes, %zu relocs",
             dw_frame.len, n_frame_relocs);
    }

    /* Build .rela.debug_info and .rela.debug_line from collected DWARF relocs.
       All relocations point at the .text section symbol (index 1 in symtab)
       with R_X86_64_64 type and the function's text_offset as addend. */
    size_t n_rela_dbinfo = 0, n_rela_dbline = 0;
    for (size_t i = 0; i < n_dw_relocs; i++) {
        if (dw_relocs[i].in_line) n_rela_dbline++; else n_rela_dbinfo++;
    }
    Elf64_Rela *rela_dbinfo = calloc(n_rela_dbinfo ? n_rela_dbinfo : 1, sizeof(Elf64_Rela));
    Elf64_Rela *rela_dbline = calloc(n_rela_dbline ? n_rela_dbline : 1, sizeof(Elf64_Rela));
    Elf64_Rela *rela_dbframe = calloc (n_frame_relocs ? n_frame_relocs : 1, sizeof (Elf64_Rela));
    {
        size_t di_idx = 0, dl_idx = 0;
        /* .text section symbol is at index 1 in symtab (SEC_TEXT section sym) */
        size_t text_sym_idx = 1;
        for (size_t i = 0; i < n_dw_relocs; i++) {
            Elf64_Rela r = {0};
            r.r_offset = dw_relocs[i].offset;
#if defined(__aarch64__)
            r.r_info = ELF64_R_INFO(text_sym_idx, R_AARCH64_ABS64);
#else
            r.r_info = ELF64_R_INFO(text_sym_idx, R_X86_64_64);
#endif
            r.r_addend = dw_relocs[i].addend;
            if (dw_relocs[i].in_line)
                rela_dbline[dl_idx++] = r;
            else
                rela_dbinfo[di_idx++] = r;
        }
        for (size_t i = 0; i < n_frame_relocs; i++) {
            Elf64_Rela r = {0};
            r.r_offset = frame_relocs[i].offset;
#if defined(__aarch64__)
            r.r_info = ELF64_R_INFO (text_sym_idx, R_AARCH64_ABS64);
#else
            r.r_info = ELF64_R_INFO (text_sym_idx, R_X86_64_64);
#endif
            r.r_addend = frame_relocs[i].addend;
            rela_dbframe[i] = r;
        }
    }

    /* ----- Phase 4: compute file layout and write ELF ----- */

    /* File layout:
     *   ELF header
     *   .text   (aligned to 16)
     *   .data   (aligned to 8)
     *   .rela.text (aligned to 8)
     *   .rela.data (aligned to 8)
     *   .symtab (aligned to 8)
     *   .strtab (aligned to 1)
     *   .shstrtab (aligned to 1)
     *   section headers (aligned to 8)
     */
    size_t off = sizeof(Elf64_Ehdr);

    /* .text */
    off = (off + 15) & ~(size_t)15;
    size_t text_off = off;
    off += text_size;

    /* .data */
    off = align8(off);
    size_t data_off = off;
    off += data_size;

    /* .mir.addrpool (PIC address slots) */
    off = align8 (off);
    size_t addrpool_off = off;
    off += addrpool_len;

    /* .tdata (TLS init image) */
    size_t tdata_off = 0;
    if (sec_tdata && tdata_size > 0) {
        off = align8 (off);
        tdata_off = off;
        off += tdata_size;
    }

    /* .rela.text */
    off = align8(off);
    size_t rela_text_off = off;
    size_t rela_text_size = n_rela_text * sizeof(Elf64_Rela);
    off += rela_text_size;

    /* .rela.data */
    off = align8(off);
    size_t rela_data_off = off;
    size_t rela_data_size = n_rela_data * sizeof(Elf64_Rela);
    off += rela_data_size;

    /* .rela.mir.addrpool */
    off = align8 (off);
    size_t rela_pool_off = off;
    size_t rela_pool_size = n_rela_pool * sizeof (Elf64_Rela);
    off += rela_pool_size;

    /* .symtab */
    off = align8(off);
    size_t symtab_off = off;
    size_t symtab_size = n_syms * sizeof(Elf64_Sym);
    off += symtab_size;

    /* .strtab */
    size_t strtab_off = off;
    off += strtab_size;

    /* .shstrtab */
    size_t shstrtab_off = off;
    off += shstrtab_size;

    /* DWARF debug sections (only if present) */
    off = align8(off);
    size_t debug_info_off = off;
    size_t debug_info_size = dw_info.len;
    off += debug_info_size;

    off = align8(off);
    size_t debug_abbrev_off = off;
    size_t debug_abbrev_size = dw_abbrev.len;
    off += debug_abbrev_size;

    off = align8(off);
    size_t debug_line_off = off;
    size_t debug_line_size = dw_line.len;
    off += debug_line_size;

    off = align8(off);
    size_t debug_str_off = off;
    size_t debug_str_size = dw_str.len;
    off += debug_str_size;

    off = align8(off);
    size_t debug_frame_off = off;
    size_t debug_frame_size = dw_frame.len;
    off += debug_frame_size;

    /* .rela.debug_info */
    off = align8(off);
    size_t rela_dbinfo_off = off;
    size_t rela_dbinfo_size = n_rela_dbinfo * sizeof(Elf64_Rela);
    off += rela_dbinfo_size;

    /* .rela.debug_line */
    off = align8(off);
    size_t rela_dbline_off = off;
    size_t rela_dbline_size = n_rela_dbline * sizeof(Elf64_Rela);
    off += rela_dbline_size;

    /* .rela.debug_frame */
    off = align8 (off);
    size_t rela_dbframe_off = off;
    size_t rela_dbframe_size = n_frame_relocs * sizeof (Elf64_Rela);
    off += rela_dbframe_size;

    /* [[registry]] cyreg_<NAME> PROGBITS + .rela.cyreg_<NAME> sections */
    for (size_t k = 0; k < n_cyr; k++) {
        off = align8(off);
        cyr[k].file_off = off;
        off += cyr[k].size;
        off = align8(off);
        cyr[k].rela_file_off = off;
        off += cyr[k].nrel * sizeof(Elf64_Rela);
    }

    /* section headers */
    off = align8(off);
    size_t sh_off = off;

    /* ----- Build section headers ----- */
    Elf64_Shdr *shdrs = calloc(total_sections, sizeof(Elf64_Shdr));

    /* 0: null */
    /* already zeroed */

    /* 1: .text */
    shdrs[SEC_TEXT].sh_name      = nm_text;
    shdrs[SEC_TEXT].sh_type      = SHT_PROGBITS;
    shdrs[SEC_TEXT].sh_flags     = SHF_ALLOC | SHF_EXECINSTR;
    shdrs[SEC_TEXT].sh_offset    = text_off;
    shdrs[SEC_TEXT].sh_size      = text_size;
    shdrs[SEC_TEXT].sh_addralign = 16;

    /* 2: .data */
    shdrs[SEC_DATA].sh_name      = nm_data;
    shdrs[SEC_DATA].sh_type      = SHT_PROGBITS;
    shdrs[SEC_DATA].sh_flags     = SHF_ALLOC | SHF_WRITE;
    shdrs[SEC_DATA].sh_offset    = data_off;
    shdrs[SEC_DATA].sh_size      = data_size;
    shdrs[SEC_DATA].sh_addralign = 8;

    /* 3: .bss */
    shdrs[SEC_BSS].sh_name      = nm_bss;
    shdrs[SEC_BSS].sh_type      = SHT_NOBITS;
    shdrs[SEC_BSS].sh_flags     = SHF_ALLOC | SHF_WRITE;
    shdrs[SEC_BSS].sh_offset    = data_off + data_size; /* no file space */
    shdrs[SEC_BSS].sh_size      = bss_size;
    shdrs[SEC_BSS].sh_addralign = 8;

    /* 4: .mir.addrpool */
    shdrs[SEC_ADDRPOOL].sh_name      = nm_addrpool;
    shdrs[SEC_ADDRPOOL].sh_type      = SHT_PROGBITS;
    shdrs[SEC_ADDRPOOL].sh_flags     = SHF_ALLOC | SHF_WRITE;
    shdrs[SEC_ADDRPOOL].sh_offset    = addrpool_off;
    shdrs[SEC_ADDRPOOL].sh_size      = addrpool_len;
    shdrs[SEC_ADDRPOOL].sh_addralign = 16;

    /* optional: .tdata (SHF_TLS) at sec_tdata */
    if (sec_tdata) {
        shdrs[sec_tdata].sh_name      = nm_tdata;
        shdrs[sec_tdata].sh_type      = SHT_PROGBITS;
        shdrs[sec_tdata].sh_flags     = SHF_ALLOC | SHF_WRITE | SHF_TLS;
        shdrs[sec_tdata].sh_offset    = tdata_off;
        shdrs[sec_tdata].sh_size      = tdata_size;
        shdrs[sec_tdata].sh_addralign = 8;
    }

    /* 5: .rela.text */
    shdrs[SEC_RELA_TEXT].sh_name      = nm_rela_text;
    shdrs[SEC_RELA_TEXT].sh_type      = SHT_RELA;
    shdrs[SEC_RELA_TEXT].sh_offset    = rela_text_off;
    shdrs[SEC_RELA_TEXT].sh_size      = rela_text_size;
    shdrs[SEC_RELA_TEXT].sh_link      = SEC_SYMTAB;
    shdrs[SEC_RELA_TEXT].sh_info      = SEC_TEXT;
    shdrs[SEC_RELA_TEXT].sh_addralign = 8;
    shdrs[SEC_RELA_TEXT].sh_entsize   = sizeof(Elf64_Rela);

    /* 6: .rela.data */
    shdrs[SEC_RELA_DATA].sh_name      = nm_rela_data;
    shdrs[SEC_RELA_DATA].sh_type      = SHT_RELA;
    shdrs[SEC_RELA_DATA].sh_offset    = rela_data_off;
    shdrs[SEC_RELA_DATA].sh_size      = rela_data_size;
    shdrs[SEC_RELA_DATA].sh_link      = SEC_SYMTAB;
    shdrs[SEC_RELA_DATA].sh_info      = SEC_DATA;
    shdrs[SEC_RELA_DATA].sh_addralign = 8;
    shdrs[SEC_RELA_DATA].sh_entsize   = sizeof(Elf64_Rela);

    /* 7: .rela.mir.addrpool */
    shdrs[SEC_RELA_ADDRPOOL].sh_name      = nm_rela_addrpool;
    shdrs[SEC_RELA_ADDRPOOL].sh_type      = SHT_RELA;
    shdrs[SEC_RELA_ADDRPOOL].sh_offset    = rela_pool_off;
    shdrs[SEC_RELA_ADDRPOOL].sh_size      = rela_pool_size;
    shdrs[SEC_RELA_ADDRPOOL].sh_link      = SEC_SYMTAB;
    shdrs[SEC_RELA_ADDRPOOL].sh_info      = SEC_ADDRPOOL;
    shdrs[SEC_RELA_ADDRPOOL].sh_addralign = 8;
    shdrs[SEC_RELA_ADDRPOOL].sh_entsize   = sizeof (Elf64_Rela);

    /* 8: .symtab */
    shdrs[SEC_SYMTAB].sh_name      = nm_symtab;
    shdrs[SEC_SYMTAB].sh_type      = SHT_SYMTAB;
    shdrs[SEC_SYMTAB].sh_offset    = symtab_off;
    shdrs[SEC_SYMTAB].sh_size      = symtab_size;
    shdrs[SEC_SYMTAB].sh_link      = SEC_STRTAB;
    shdrs[SEC_SYMTAB].sh_info      = first_global;
    shdrs[SEC_SYMTAB].sh_addralign = 8;
    shdrs[SEC_SYMTAB].sh_entsize   = sizeof(Elf64_Sym);

    /* 7: .strtab */
    shdrs[SEC_STRTAB].sh_name      = nm_strtab;
    shdrs[SEC_STRTAB].sh_type      = SHT_STRTAB;
    shdrs[SEC_STRTAB].sh_offset    = strtab_off;
    shdrs[SEC_STRTAB].sh_size      = strtab_size;
    shdrs[SEC_STRTAB].sh_addralign = 1;

    /* 8: .shstrtab */
    shdrs[SEC_SHSTRTAB].sh_name      = nm_shstrtab;
    shdrs[SEC_SHSTRTAB].sh_type      = SHT_STRTAB;
    shdrs[SEC_SHSTRTAB].sh_offset    = shstrtab_off;
    shdrs[SEC_SHSTRTAB].sh_size      = shstrtab_size;
    shdrs[SEC_SHSTRTAB].sh_addralign = 1;

    /* 9: .note.GNU-stack (empty, non-executable) */
    shdrs[SEC_NOTE_STACK].sh_name      = nm_note_stack;
    shdrs[SEC_NOTE_STACK].sh_type      = SHT_PROGBITS;
    shdrs[SEC_NOTE_STACK].sh_flags     = 0;  /* no SHF_EXECINSTR */
    shdrs[SEC_NOTE_STACK].sh_offset    = 0;
    shdrs[SEC_NOTE_STACK].sh_size      = 0;
    shdrs[SEC_NOTE_STACK].sh_addralign = 1;

    /* 10: .debug_info */
    shdrs[SEC_DEBUG_INFO].sh_name      = nm_debug_info;
    shdrs[SEC_DEBUG_INFO].sh_type      = SHT_PROGBITS;
    shdrs[SEC_DEBUG_INFO].sh_flags     = 0;
    shdrs[SEC_DEBUG_INFO].sh_offset    = debug_info_off;
    shdrs[SEC_DEBUG_INFO].sh_size      = debug_info_size;
    shdrs[SEC_DEBUG_INFO].sh_addralign = 1;

    /* 11: .debug_abbrev */
    shdrs[SEC_DEBUG_ABBREV].sh_name      = nm_debug_abbrev;
    shdrs[SEC_DEBUG_ABBREV].sh_type      = SHT_PROGBITS;
    shdrs[SEC_DEBUG_ABBREV].sh_flags     = 0;
    shdrs[SEC_DEBUG_ABBREV].sh_offset    = debug_abbrev_off;
    shdrs[SEC_DEBUG_ABBREV].sh_size      = debug_abbrev_size;
    shdrs[SEC_DEBUG_ABBREV].sh_addralign = 1;

    /* 12: .debug_line */
    shdrs[SEC_DEBUG_LINE].sh_name      = nm_debug_line;
    shdrs[SEC_DEBUG_LINE].sh_type      = SHT_PROGBITS;
    shdrs[SEC_DEBUG_LINE].sh_flags     = 0;
    shdrs[SEC_DEBUG_LINE].sh_offset    = debug_line_off;
    shdrs[SEC_DEBUG_LINE].sh_size      = debug_line_size;
    shdrs[SEC_DEBUG_LINE].sh_addralign = 1;

    /* 13: .debug_str */
    shdrs[SEC_DEBUG_STR].sh_name      = nm_debug_str;
    shdrs[SEC_DEBUG_STR].sh_type      = SHT_PROGBITS;
    shdrs[SEC_DEBUG_STR].sh_flags     = SHF_MERGE | SHF_STRINGS;
    shdrs[SEC_DEBUG_STR].sh_offset    = debug_str_off;
    shdrs[SEC_DEBUG_STR].sh_size      = debug_str_size;
    shdrs[SEC_DEBUG_STR].sh_addralign = 1;
    shdrs[SEC_DEBUG_STR].sh_entsize   = 1;

    /* 14: .debug_frame */
    shdrs[SEC_DEBUG_FRAME].sh_name      = nm_debug_frame;
    shdrs[SEC_DEBUG_FRAME].sh_type      = SHT_PROGBITS;
    shdrs[SEC_DEBUG_FRAME].sh_flags     = 0;
    shdrs[SEC_DEBUG_FRAME].sh_offset    = debug_frame_off;
    shdrs[SEC_DEBUG_FRAME].sh_size      = debug_frame_size;
    shdrs[SEC_DEBUG_FRAME].sh_addralign = 8;

    /* 15: .rela.debug_info */
    shdrs[SEC_RELA_DEBUG_INFO].sh_name      = nm_rela_debug_info;
    shdrs[SEC_RELA_DEBUG_INFO].sh_type      = SHT_RELA;
    shdrs[SEC_RELA_DEBUG_INFO].sh_offset    = rela_dbinfo_off;
    shdrs[SEC_RELA_DEBUG_INFO].sh_size      = rela_dbinfo_size;
    shdrs[SEC_RELA_DEBUG_INFO].sh_link      = SEC_SYMTAB;
    shdrs[SEC_RELA_DEBUG_INFO].sh_info      = SEC_DEBUG_INFO;
    shdrs[SEC_RELA_DEBUG_INFO].sh_addralign = 8;
    shdrs[SEC_RELA_DEBUG_INFO].sh_entsize   = sizeof(Elf64_Rela);

    /* 16: .rela.debug_line */
    shdrs[SEC_RELA_DEBUG_LINE].sh_name      = nm_rela_debug_line;
    shdrs[SEC_RELA_DEBUG_LINE].sh_type      = SHT_RELA;
    shdrs[SEC_RELA_DEBUG_LINE].sh_offset    = rela_dbline_off;
    shdrs[SEC_RELA_DEBUG_LINE].sh_size      = rela_dbline_size;
    shdrs[SEC_RELA_DEBUG_LINE].sh_link      = SEC_SYMTAB;
    shdrs[SEC_RELA_DEBUG_LINE].sh_info      = SEC_DEBUG_LINE;
    shdrs[SEC_RELA_DEBUG_LINE].sh_addralign = 8;
    shdrs[SEC_RELA_DEBUG_LINE].sh_entsize   = sizeof(Elf64_Rela);

    /* 17: .rela.debug_frame */
    shdrs[SEC_RELA_DEBUG_FRAME].sh_name      = nm_rela_debug_frame;
    shdrs[SEC_RELA_DEBUG_FRAME].sh_type      = SHT_RELA;
    shdrs[SEC_RELA_DEBUG_FRAME].sh_offset    = rela_dbframe_off;
    shdrs[SEC_RELA_DEBUG_FRAME].sh_size      = rela_dbframe_size;
    shdrs[SEC_RELA_DEBUG_FRAME].sh_link      = SEC_SYMTAB;
    shdrs[SEC_RELA_DEBUG_FRAME].sh_info      = SEC_DEBUG_FRAME;
    shdrs[SEC_RELA_DEBUG_FRAME].sh_addralign = 8;
    shdrs[SEC_RELA_DEBUG_FRAME].sh_entsize   = sizeof (Elf64_Rela);

    /* [[registry]] cyreg_<NAME> (SHF_ALLOC|SHF_WRITE PROGBITS) + its .rela */
    for (size_t k = 0; k < n_cyr; k++) {
        Elf64_Shdr *ps = &shdrs[cyr[k].sec_idx];
        ps->sh_name      = cyr[k].nm;
        ps->sh_type      = SHT_PROGBITS;
        ps->sh_flags     = SHF_ALLOC | SHF_WRITE;
        ps->sh_offset    = cyr[k].file_off;
        ps->sh_size      = cyr[k].size;
        ps->sh_addralign = 8;
        Elf64_Shdr *rs = &shdrs[cyr[k].rela_sec_idx];
        rs->sh_name      = cyr[k].rela_nm;
        rs->sh_type      = SHT_RELA;
        rs->sh_offset    = cyr[k].rela_file_off;
        rs->sh_size      = cyr[k].nrel * sizeof(Elf64_Rela);
        rs->sh_link      = SEC_SYMTAB;
        rs->sh_info      = cyr[k].sec_idx;
        rs->sh_addralign = 8;
        rs->sh_entsize   = sizeof(Elf64_Rela);
    }

    /* ----- ELF header ----- */
    Elf64_Ehdr ehdr;
    memset(&ehdr, 0, sizeof(ehdr));
    memcpy(ehdr.e_ident, ELFMAG, SELFMAG);
    ehdr.e_ident[EI_CLASS]   = ELFCLASS64;
    ehdr.e_ident[EI_DATA]    = ELFDATA2LSB;
    ehdr.e_ident[EI_VERSION] = EV_CURRENT;
    ehdr.e_ident[EI_OSABI]   = ELFOSABI_NONE;
    ehdr.e_type      = ET_REL;
#if defined(__aarch64__)
    ehdr.e_machine   = EM_AARCH64;
#else
    ehdr.e_machine   = EM_X86_64;
#endif
    ehdr.e_version   = EV_CURRENT;
    ehdr.e_ehsize    = sizeof(Elf64_Ehdr);
    ehdr.e_shentsize = sizeof(Elf64_Shdr);
    ehdr.e_shnum     = (uint16_t) total_sections;
    ehdr.e_shoff     = sh_off;
    ehdr.e_shstrndx  = SEC_SHSTRTAB;

    /* ----- Write to file ----- */
    DBG("phase 4: writing ELF file");
    int fd = open(output_file, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        perror("Failed to open output file");
        exit(EXIT_FAILURE);
    }

    write(fd, &ehdr, sizeof(ehdr));

    /* padding to text_off */
    write_padding(fd, text_off - sizeof(Elf64_Ehdr));

    /* .text */
    if (text_size) write(fd, text_buf, text_size);

    /* padding to data_off */
    { size_t cur = text_off + text_size;
      if (data_off > cur) write_padding(fd, data_off - cur); }

    /* .data */
    if (data_size) write(fd, data_buf, data_size);

    /* .mir.addrpool */
    { size_t cur = data_off + data_size;
      if (addrpool_off > cur) write_padding (fd, addrpool_off - cur); }
    if (addrpool_len && addrpool_buf) write (fd, addrpool_buf, addrpool_len);

    /* .tdata */
    if (sec_tdata && tdata_size) {
        size_t cur = addrpool_off + addrpool_len;
        if (tdata_off > cur) write_padding (fd, tdata_off - cur);
        write (fd, tdata_buf, tdata_size);
    }

    /* padding to rela_text_off */
    { size_t cur = (sec_tdata && tdata_size) ? (tdata_off + tdata_size)
                                            : (addrpool_off + addrpool_len);
      if (rela_text_off > cur) write_padding(fd, rela_text_off - cur); }

    /* .rela.text */
    if (rela_text_size) write(fd, rela_text, rela_text_size);

    /* padding to rela_data_off */
    { size_t cur = rela_text_off + rela_text_size;
      if (rela_data_off > cur) write_padding(fd, rela_data_off - cur); }

    /* .rela.data */
    if (rela_data_size) write(fd, rela_data, rela_data_size);

    /* .rela.mir.addrpool */
    { size_t cur = rela_data_off + rela_data_size;
      if (rela_pool_off > cur) write_padding (fd, rela_pool_off - cur); }
    if (rela_pool_size) write (fd, rela_pool, rela_pool_size);

    /* padding to symtab_off */
    { size_t cur = rela_pool_off + rela_pool_size;
      if (symtab_off > cur) write_padding(fd, symtab_off - cur); }

    /* .symtab */
    write(fd, symtab, symtab_size);

    /* .strtab */
    write(fd, strtab, strtab_size);

    /* .shstrtab */
    write(fd, shstrtab, shstrtab_size);

    /* DWARF debug sections */
    if (debug_info_size > 0) {
        { size_t cur = shstrtab_off + shstrtab_size;
          if (debug_info_off > cur) write_padding(fd, debug_info_off - cur); }
        write(fd, dw_info.data, debug_info_size);

        { size_t cur = debug_info_off + debug_info_size;
          if (debug_abbrev_off > cur) write_padding(fd, debug_abbrev_off - cur); }
        write(fd, dw_abbrev.data, debug_abbrev_size);

        { size_t cur = debug_abbrev_off + debug_abbrev_size;
          if (debug_line_off > cur) write_padding(fd, debug_line_off - cur); }
        write(fd, dw_line.data, debug_line_size);

        { size_t cur = debug_line_off + debug_line_size;
          if (debug_str_off > cur) write_padding(fd, debug_str_off - cur); }
        write(fd, dw_str.data, debug_str_size);
    }

    if (debug_frame_size > 0) {
        size_t cur = debug_str_size > 0 ? debug_str_off + debug_str_size
                   : debug_info_size > 0 ? debug_line_off + debug_line_size
                   : shstrtab_off + shstrtab_size;
        if (debug_frame_off > cur) write_padding (fd, debug_frame_off - cur);
        write (fd, dw_frame.data, debug_frame_size);
    }

    /* .rela.debug_info */
    if (rela_dbinfo_size > 0) {
        { size_t cur = (debug_frame_size > 0 ? debug_frame_off + debug_frame_size
                       : debug_str_size > 0 ? debug_str_off + debug_str_size
                                           : shstrtab_off + shstrtab_size);
          if (rela_dbinfo_off > cur) write_padding(fd, rela_dbinfo_off - cur); }
        write(fd, rela_dbinfo, rela_dbinfo_size);
    }

    /* .rela.debug_line */
    if (rela_dbline_size > 0) {
        { size_t cur = rela_dbinfo_off + rela_dbinfo_size;
          if (rela_dbline_off > cur) write_padding(fd, rela_dbline_off - cur); }
        write(fd, rela_dbline, rela_dbline_size);
    }

    /* .rela.debug_frame */
    if (rela_dbframe_size > 0) {
        size_t cur = rela_dbline_size > 0 ? rela_dbline_off + rela_dbline_size
                   : rela_dbinfo_size > 0 ? rela_dbinfo_off + rela_dbinfo_size
                   : debug_frame_size > 0 ? debug_frame_off + debug_frame_size
                   : shstrtab_off + shstrtab_size;
        if (rela_dbframe_off > cur) write_padding (fd, rela_dbframe_off - cur);
        write (fd, rela_dbframe, rela_dbframe_size);
    }

    /* [[registry]] cyreg sections: PROGBITS pointer array + its relocations */
    { size_t cur = (rela_dbframe_size > 0 ? rela_dbframe_off + rela_dbframe_size
                  : rela_dbline_size > 0 ? rela_dbline_off + rela_dbline_size
                  : rela_dbinfo_size > 0 ? rela_dbinfo_off + rela_dbinfo_size
                  : debug_frame_size > 0 ? debug_frame_off + debug_frame_size
                  : debug_str_size > 0   ? debug_str_off + debug_str_size
                  : shstrtab_off + shstrtab_size);
      for (size_t k = 0; k < n_cyr; k++) {
          if (cyr[k].file_off > cur) write_padding(fd, cyr[k].file_off - cur);
          if (cyr[k].size) write(fd, cyr[k].buf, cyr[k].size);
          cur = cyr[k].file_off + cyr[k].size;
          if (cyr[k].rela_file_off > cur) write_padding(fd, cyr[k].rela_file_off - cur);
          if (cyr[k].nrel) write(fd, cyr[k].rbuf, cyr[k].nrel * sizeof(Elf64_Rela));
          cur = cyr[k].rela_file_off + cyr[k].nrel * sizeof(Elf64_Rela);
      }
      /* padding to sh_off */
      if (sh_off > cur) write_padding(fd, sh_off - cur); }

    /* section headers */
    write(fd, shdrs, total_sections * sizeof(Elf64_Shdr));

    close(fd);

    DBG("wrote ELF object: %s", output_file);
    DBG("  .text:  %zu bytes, %zu functions", text_size, n_funcs);
    DBG("  .data:  %zu bytes, %zu items", data_size, n_datas);
    DBG("  .bss:   %zu bytes, %zu items", bss_size, n_bsses);
    DBG("  .rela.text: %zu entries", n_rela_text);
    DBG("  .rela.data: %zu entries", n_rela_data);
    DBG("  symtab: %zu symbols (first_global=%zu)", n_syms, first_global);

    /* ----- Cleanup ----- */
    free(text_buf);
    free(data_buf);
    free(rela_text);
    free(rela_data);
    free (rela_pool);
    free (addrpool_buf);
    free(symtab);
    free(strtab);
    free(shstrtab);
    free(shdrs);
    for (size_t k = 0; k < n_cyr; k++) { free(cyr[k].buf); free(cyr[k].rel); free(cyr[k].rbuf); }
    free(cyr);
    free(cyr_syms);
    free(sym_map);
    free(sym_idx_tab);
    for (size_t i = 0; i < n_funcs; i++) free(funcs[i].code);
    free(funcs);
    for (size_t i = 0; i < n_datas; i++) free(datas[i].bytes);
    free(datas);
    free(bsses);
    free(tlss);
    free(tdata_buf);
    free(relocs);
    for (size_t i = 0; i < exports.n; i++) free(exports.names[i]);
    free(exports.names);
    for (size_t i = 0; i < imports.n; i++) free(imports.names[i]);
    free(imports.names);
    dwbuf_free(&dw_abbrev);
    dwbuf_free(&dw_info);
    dwbuf_free(&dw_line);
    dwbuf_free(&dw_str);
    dwbuf_free (&dw_frame);
    free(dw_relocs);
    free (frame_relocs);
    free(rela_dbinfo);
    free(rela_dbline);
    free (rela_dbframe);

    #undef SYMTAB_PUSH
    #undef SYM_MAP_ADD
    #undef DW_RELOC_PUSH
}

int main(int argc, char **argv) {
    MIR_alloc_t alloc = &default_alloc;
    int opt_level = -1;  /* -1: not given on the command line */
    int argi = 1;

    /* Optional -O0..-O3 code-generation optimisation level.  Overrides the
       B2OBJ_OPT env var when given. */
    if (argi < argc && strncmp(argv[argi], "-O", 2) == 0) {
        const char *p = argv[argi] + 2;
        if (p[0] >= '0' && p[0] <= '3' && p[1] == '\0') {
            opt_level = p[0] - '0';
            argi++;
        } else {
            fprintf(stderr, "%s: invalid optimisation level '%s' (use -O0, -O1, -O2 or -O3)\n",
                    argv[0], argv[argi]);
            return EXIT_FAILURE;
        }
    }

    if (argc - argi != 2) {
        fprintf(stderr, "Usage: %s [-O0|-O1|-O2|-O3] <mir_input> <object_file>\n", argv[0]);
        return EXIT_FAILURE;
    }

    VARR_CREATE(char, temp_string, alloc, 0);
    VARR_CREATE(lib_t, extra_libs, alloc, 16);
    VARR_CREATE(char_ptr_t, lib_dirs, alloc, 16);
    for (int i = 0; i < (int)(sizeof(std_lib_dirs) / sizeof(char_ptr_t)); i++)
        VARR_PUSH(char_ptr_t, lib_dirs, std_lib_dirs[i]);
    lib_dirs_from_env_var("LD_LIBRARY_PATH");
    lib_dirs_from_env_var(MIR_ENV_VAR_LIB_DIRS);

    const char *mir_input_file = argv[argi];
    const char *output_file = argv[argi + 1];

    MIR_context_t ctx = MIR_init();

    FILE *fp = fopen(mir_input_file, "r");
    if (!fp) {
        perror("Failed to open MIR input file");
        return EXIT_FAILURE;
    }

    DBG("reading MIR from %s", mir_input_file);
    MIR_read(ctx, fp);
    fclose(fp);
    DBG("MIR_read done");

    /* N2: ELF local-exec TLS — keep TLS item refs for TPOFF codegen; do not
       rewrite to mir_tls_addr (emulated).  Must be set before MIR_load_module. */
    MIR_set_tls_native_aot (ctx, 1);

    /* Load all modules */
    size_t n_modules = 0, n_funcs_total = 0;
    for (MIR_module_t module = DLIST_HEAD(MIR_module_t, *MIR_get_module_list(ctx));
         module != NULL;
         module = DLIST_NEXT(MIR_module_t, module)) {
        n_modules++;
        for (MIR_item_t item = DLIST_HEAD(MIR_item_t, module->items);
             item != NULL;
             item = DLIST_NEXT(MIR_item_t, item)) {
            if (item->item_type == MIR_func_item)
                n_funcs_total++;
        }
        MIR_load_module(ctx, module);
    }
    DBG("loaded %zu module(s), %zu function(s) total", n_modules, n_funcs_total);

    open_std_libs();
    open_extra_libs();
    DBG("opened libraries");

    /* Initialize code generator and link */
    MIR_gen_init(ctx);
    MIR_gen_set_save_relocs(ctx, 1);
    /* B2OBJ_GEN_DEBUG=<level>: surface the MIR generator's own diagnostics on
       stderr.  Level 0 prints one line per function with its MIR insn count and
       generation time -- the quickest way to spot a function whose codegen cost
       is superlinear in its size.  Higher levels dump per-pass CFGs (very
       verbose).  Off unless the variable is set. */
    {
        const char *gdbg = getenv("B2OBJ_GEN_DEBUG");
        if (gdbg != NULL) {
            MIR_gen_set_debug_file(ctx, stderr);
            MIR_gen_set_debug_level(ctx, atoi(gdbg));
        }
    }
    {
        /* Optimisation level for code generation.  The MIR generator's default
           is 2 (GVN/CCP), but that pass can be extremely slow on large inputs
           (e.g. self-compiling c2mir.c).  Level 1 (register allocation +
           combiner) is a good default for ahead-of-time builds: it optimises
           well and completes quickly.  Override with the B2OBJ_OPT env var or
           the -O0..-O3 command-line option (which takes precedence). */
        const char *opt = getenv("B2OBJ_OPT");
        int level = opt_level >= 0 ? opt_level : (opt != NULL ? atoi(opt) : 1);
        MIR_gen_set_optimize_level(ctx, (unsigned)level);
        MIR_set_inline_level(level);
        DBG("optimize level = %d", level);
    }
    DBG("starting MIR_link (eager code generation of all functions)");
    MIR_link(ctx, MIR_set_gen_interface, hybrid_import_resolver);
    DBG("MIR_link done (all functions generated)");
    if (getenv("B2OBJ_DEBUG") != NULL) {
        const MIR_link_stats_t *st = MIR_get_link_stats();
        fprintf(stderr,
                "[b2obj] simplify=%.0fms inlines=%.0fms gen=%.0fms inlined=%lu insns %lu->%lu "
                "skip callee=%lu growth=%lu cap=%lu\n",
                st->simplify_ms, st->inline_ms, st->interface_ms, st->n_inlined,
                st->n_insns_before, st->n_insns_after, st->skipped_callee, st->skipped_growth,
                st->skipped_cap);
        if (st->max_func_name[0] != '\0')
            fprintf(stderr, "[b2obj] largest after inline: %s (%lu insns)\n", st->max_func_name,
                    st->max_func_insns);
        MIR_gen_dump_timing(stderr);
    }

    /* Generate code for all functions and write the ELF object */
    create_object_file_from_module(ctx, output_file);
    DBG("create_object_file_from_module done");

    MIR_gen_finish(ctx);

    printf("Object file '%s' created successfully.\n", output_file);

    close_extra_libs();
    close_std_libs();
    MIR_finish(ctx);

    return EXIT_SUCCESS;
}
