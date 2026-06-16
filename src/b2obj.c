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

/* A single relocation to emit */
typedef struct {
    size_t      offset;     /* offset within the target section (.text or .data) */
    const char *symbol;     /* symbol name */
    int         type;       /* ELF reloc type, e.g. R_X86_64_64 */
    int64_t     addend;
    int         in_data;    /* 0 = .text reloc, 1 = .data reloc */
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

            case MIR_data_item: {
                MIR_data_t d = item->u.data;
                size_t sz = d->nel * _MIR_type_size(ctx, d->el_type);
                /* Drop only anonymous empty data; a *named* zero-length item
                   (e.g. an unused __func__ array) must still define a symbol so
                   that code referencing its address can be linked. */
                if (sz == 0 && d->name == NULL) break;
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
                de->item_addr = item->addr;
                DBG("  data: %s  size=%zu  addr=%p", d->name ? d->name : "(anon)", sz, item->addr);
                break;
            }

            case MIR_ref_data_item: {
                MIR_ref_data_t rd = item->u.ref_data;
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
                de->item_addr = item->addr;
                DBG("  ref_data: %s -> %s + %ld",
                    rd->name ? rd->name : "(anon)",
                    MIR_item_name(ctx, rd->ref_item), (long)rd->disp);
                break;
            }

            case MIR_bss_item: {
                MIR_bss_t b = item->u.bss;
                if (n_bsses >= cap_bsses) {
                    cap_bsses = cap_bsses ? cap_bsses * 2 : 16;
                    bsses = realloc(bsses, cap_bsses * sizeof(bss_entry_t));
                }
                bss_entry_t *be = &bsses[n_bsses++];
                be->name = b->name;
                be->len = b->len;
                be->bss_offset = 0;
                be->item_addr = item->addr;
                if (b->name)
                    DBG("  bss: %s  len=%lu  addr=%p", b->name, (unsigned long)b->len, item->addr);
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

    /* .data: concatenate all data items (8-byte aligned) */
    size_t data_size = 0;
    for (size_t i = 0; i < n_datas; i++) {
        if (i > 0) data_size = align8(data_size);
        datas[i].data_offset = data_size;
        data_size += datas[i].size;
    }

    /* .bss: just total size (8-byte aligned per item) */
    size_t bss_size = 0;
    for (size_t i = 0; i < n_bsses; i++) {
        if (i > 0) bss_size = align8(bss_size);
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
            er->in_data = 0;
        }
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
        er->type = R_X86_64_64;
        er->addend = datas[i].ref_disp;
        er->in_data = 1;
        /* The 8 bytes in data_buf are already zero (calloc) */
    }

    DBG("phase 2b done: %zu relocations", n_relocs);
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
        SEC_RELA_TEXT,   /* 4 */
        SEC_RELA_DATA,   /* 5 */
        SEC_SYMTAB,      /* 6 */
        SEC_STRTAB,      /* 7 */
        SEC_SHSTRTAB,    /* 8 */
        SEC_NOTE_STACK,  /* 9 */
        SEC_DEBUG_INFO,  /* 10 — .debug_info */
        SEC_DEBUG_ABBREV,/* 11 — .debug_abbrev */
        SEC_DEBUG_LINE,  /* 12 — .debug_line */
        SEC_DEBUG_STR,   /* 13 — .debug_str */
        SEC_RELA_DEBUG_INFO, /* 14 — .rela.debug_info */
        SEC_RELA_DEBUG_LINE, /* 15 — .rela.debug_line */
        NUM_SECTIONS     /* 16 */
    };

    /* Build .shstrtab */
    char  *shstrtab = NULL;
    size_t shstrtab_size = 0, shstrtab_cap = 0;
    strtab_add(&shstrtab, &shstrtab_size, &shstrtab_cap, ""); /* index 0 = null */
    size_t nm_text       = strtab_add(&shstrtab, &shstrtab_size, &shstrtab_cap, ".text");
    size_t nm_data       = strtab_add(&shstrtab, &shstrtab_size, &shstrtab_cap, ".data");
    size_t nm_bss        = strtab_add(&shstrtab, &shstrtab_size, &shstrtab_cap, ".bss");
    size_t nm_rela_text  = strtab_add(&shstrtab, &shstrtab_size, &shstrtab_cap, ".rela.text");
    size_t nm_rela_data  = strtab_add(&shstrtab, &shstrtab_size, &shstrtab_cap, ".rela.data");
    size_t nm_symtab     = strtab_add(&shstrtab, &shstrtab_size, &shstrtab_cap, ".symtab");
    size_t nm_strtab     = strtab_add(&shstrtab, &shstrtab_size, &shstrtab_cap, ".strtab");
    size_t nm_shstrtab   = strtab_add(&shstrtab, &shstrtab_size, &shstrtab_cap, ".shstrtab");
    size_t nm_note_stack = strtab_add(&shstrtab, &shstrtab_size, &shstrtab_cap, ".note.GNU-stack");
    size_t nm_debug_info   = strtab_add(&shstrtab, &shstrtab_size, &shstrtab_cap, ".debug_info");
    size_t nm_debug_abbrev = strtab_add(&shstrtab, &shstrtab_size, &shstrtab_cap, ".debug_abbrev");
    size_t nm_debug_line   = strtab_add(&shstrtab, &shstrtab_size, &shstrtab_cap, ".debug_line");
    size_t nm_debug_str    = strtab_add(&shstrtab, &shstrtab_size, &shstrtab_cap, ".debug_str");
    size_t nm_rela_debug_info = strtab_add(&shstrtab, &shstrtab_size, &shstrtab_cap, ".rela.debug_info");
    size_t nm_rela_debug_line = strtab_add(&shstrtab, &shstrtab_size, &shstrtab_cap, ".rela.debug_line");

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

    /* Section symbols (local) for .text, .data, .bss */
    {
        Elf64_Sym s = {0};
        s.st_info = ELF64_ST_INFO(STB_LOCAL, STT_SECTION);

        s.st_shndx = SEC_TEXT;
        SYMTAB_PUSH(s);

        s.st_shndx = SEC_DATA;
        SYMTAB_PUSH(s);

        s.st_shndx = SEC_BSS;
        SYMTAB_PUSH(s);
    }

    size_t first_global = n_syms; /* for sh_info */

    /* We need a mapping from symbol name -> symtab index for relocation emission.
     * Build a simple name->index table. */
    typedef struct { const char *name; size_t idx; } sym_map_entry_t;
    sym_map_entry_t *sym_map = NULL;
    size_t n_sym_map = 0, cap_sym_map = 0;

    #define SYM_MAP_ADD(nm, ix) do { \
        if (n_sym_map >= cap_sym_map) { \
            cap_sym_map = cap_sym_map ? cap_sym_map * 2 : 64; \
            sym_map = realloc(sym_map, cap_sym_map * sizeof(sym_map_entry_t)); \
        } \
        sym_map[n_sym_map].name = (nm); \
        sym_map[n_sym_map].idx = (ix); \
        n_sym_map++; \
    } while(0)

    /* Global symbols: functions */
    for (size_t i = 0; i < n_funcs; i++) {
        const char *fname = funcs[i].name;
        /* Determine binding: global if exported, otherwise local */
        size_t dummy;
        int is_exported = name_set_find(&exports, fname, &dummy);
        Elf64_Sym s = {0};
        s.st_name = strtab_add(&strtab, &strtab_size, &strtab_cap, fname);
        s.st_info = ELF64_ST_INFO(is_exported ? STB_GLOBAL : STB_LOCAL, STT_FUNC);
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
        for (size_t j = 0; j < n_sym_map; j++)
            if (strcmp(sym_map[j].name, datas[i].name) == 0) { found = 1; break; }
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
        for (size_t j = 0; j < n_sym_map; j++)
            if (strcmp(sym_map[j].name, bsses[i].name) == 0) { found = 1; break; }
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

    /* Imported / external undefined symbols.
     * Also add any reloc symbol not yet in sym_map. */
    for (size_t i = 0; i < imports.n; i++) {
        int found = 0;
        for (size_t j = 0; j < n_sym_map; j++)
            if (strcmp(sym_map[j].name, imports.names[i]) == 0) { found = 1; break; }
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
        for (size_t j = 0; j < n_sym_map; j++)
            if (strcmp(sym_map[j].name, rname) == 0) { found = 1; break; }
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

    /* ----- Build .rela.text and .rela.data ----- */
    size_t n_rela_text = 0, n_rela_data = 0;
    for (size_t i = 0; i < n_relocs; i++) {
        if (relocs[i].in_data) n_rela_data++; else n_rela_text++;
    }

    Elf64_Rela *rela_text = calloc(n_rela_text ? n_rela_text : 1, sizeof(Elf64_Rela));
    Elf64_Rela *rela_data = calloc(n_rela_data ? n_rela_data : 1, sizeof(Elf64_Rela));
    size_t rt_idx = 0, rd_idx = 0;

    for (size_t i = 0; i < n_relocs; i++) {
        /* Find symbol index */
        size_t sym_idx = 0;
        for (size_t j = 0; j < n_sym_map; j++) {
            if (strcmp(sym_map[j].name, relocs[i].symbol) == 0) {
                sym_idx = sym_map[j].idx;
                break;
            }
        }
        Elf64_Rela r = {0};
        r.r_offset = relocs[i].offset;
        r.r_info = ELF64_R_INFO(sym_idx, relocs[i].type);
        r.r_addend = relocs[i].addend;
        if (relocs[i].in_data)
            rela_data[rd_idx++] = r;
        else
            rela_text[rt_idx++] = r;
    }

    DBG("phase 3 done: %zu symbols", n_syms);

    /* ----- Phase 3b: Build DWARF debug sections (if debug info present) ----- */
    dwbuf_t dw_abbrev, dw_info, dw_line, dw_str;
    dwbuf_init(&dw_abbrev);
    dwbuf_init(&dw_info);
    dwbuf_init(&dw_line);
    dwbuf_init(&dw_str);
    int has_dwarf = 0;

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

        DBG("phase 3b done: .debug_info=%zu .debug_abbrev=%zu .debug_line=%zu .debug_str=%zu relocs=%zu",
            dw_info.len, dw_abbrev.len, dw_line.len, dw_str.len, n_dw_relocs);
    }
    }
#endif /* !MIR_NO_DBINFO */

    /* Build .rela.debug_info and .rela.debug_line from collected DWARF relocs.
       All relocations point at the .text section symbol (index 1 in symtab)
       with R_X86_64_64 type and the function's text_offset as addend. */
    size_t n_rela_dbinfo = 0, n_rela_dbline = 0;
    for (size_t i = 0; i < n_dw_relocs; i++) {
        if (dw_relocs[i].in_line) n_rela_dbline++; else n_rela_dbinfo++;
    }
    Elf64_Rela *rela_dbinfo = calloc(n_rela_dbinfo ? n_rela_dbinfo : 1, sizeof(Elf64_Rela));
    Elf64_Rela *rela_dbline = calloc(n_rela_dbline ? n_rela_dbline : 1, sizeof(Elf64_Rela));
    {
        size_t di_idx = 0, dl_idx = 0;
        /* .text section symbol is at index 1 in symtab (SEC_TEXT section sym) */
        size_t text_sym_idx = 1;
        for (size_t i = 0; i < n_dw_relocs; i++) {
            Elf64_Rela r = {0};
            r.r_offset = dw_relocs[i].offset;
            r.r_info = ELF64_R_INFO(text_sym_idx, R_X86_64_64);
            r.r_addend = dw_relocs[i].addend;
            if (dw_relocs[i].in_line)
                rela_dbline[dl_idx++] = r;
            else
                rela_dbinfo[di_idx++] = r;
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

    /* section headers */
    off = align8(off);
    size_t sh_off = off;

    /* ----- Build section headers ----- */
    Elf64_Shdr shdrs[NUM_SECTIONS];
    memset(shdrs, 0, sizeof(shdrs));

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

    /* 4: .rela.text */
    shdrs[SEC_RELA_TEXT].sh_name      = nm_rela_text;
    shdrs[SEC_RELA_TEXT].sh_type      = SHT_RELA;
    shdrs[SEC_RELA_TEXT].sh_offset    = rela_text_off;
    shdrs[SEC_RELA_TEXT].sh_size      = rela_text_size;
    shdrs[SEC_RELA_TEXT].sh_link      = SEC_SYMTAB;
    shdrs[SEC_RELA_TEXT].sh_info      = SEC_TEXT;
    shdrs[SEC_RELA_TEXT].sh_addralign = 8;
    shdrs[SEC_RELA_TEXT].sh_entsize   = sizeof(Elf64_Rela);

    /* 5: .rela.data */
    shdrs[SEC_RELA_DATA].sh_name      = nm_rela_data;
    shdrs[SEC_RELA_DATA].sh_type      = SHT_RELA;
    shdrs[SEC_RELA_DATA].sh_offset    = rela_data_off;
    shdrs[SEC_RELA_DATA].sh_size      = rela_data_size;
    shdrs[SEC_RELA_DATA].sh_link      = SEC_SYMTAB;
    shdrs[SEC_RELA_DATA].sh_info      = SEC_DATA;
    shdrs[SEC_RELA_DATA].sh_addralign = 8;
    shdrs[SEC_RELA_DATA].sh_entsize   = sizeof(Elf64_Rela);

    /* 6: .symtab */
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

    /* 14: .rela.debug_info */
    shdrs[SEC_RELA_DEBUG_INFO].sh_name      = nm_rela_debug_info;
    shdrs[SEC_RELA_DEBUG_INFO].sh_type      = SHT_RELA;
    shdrs[SEC_RELA_DEBUG_INFO].sh_offset    = rela_dbinfo_off;
    shdrs[SEC_RELA_DEBUG_INFO].sh_size      = rela_dbinfo_size;
    shdrs[SEC_RELA_DEBUG_INFO].sh_link      = SEC_SYMTAB;
    shdrs[SEC_RELA_DEBUG_INFO].sh_info      = SEC_DEBUG_INFO;
    shdrs[SEC_RELA_DEBUG_INFO].sh_addralign = 8;
    shdrs[SEC_RELA_DEBUG_INFO].sh_entsize   = sizeof(Elf64_Rela);

    /* 15: .rela.debug_line */
    shdrs[SEC_RELA_DEBUG_LINE].sh_name      = nm_rela_debug_line;
    shdrs[SEC_RELA_DEBUG_LINE].sh_type      = SHT_RELA;
    shdrs[SEC_RELA_DEBUG_LINE].sh_offset    = rela_dbline_off;
    shdrs[SEC_RELA_DEBUG_LINE].sh_size      = rela_dbline_size;
    shdrs[SEC_RELA_DEBUG_LINE].sh_link      = SEC_SYMTAB;
    shdrs[SEC_RELA_DEBUG_LINE].sh_info      = SEC_DEBUG_LINE;
    shdrs[SEC_RELA_DEBUG_LINE].sh_addralign = 8;
    shdrs[SEC_RELA_DEBUG_LINE].sh_entsize   = sizeof(Elf64_Rela);

    /* ----- ELF header ----- */
    Elf64_Ehdr ehdr;
    memset(&ehdr, 0, sizeof(ehdr));
    memcpy(ehdr.e_ident, ELFMAG, SELFMAG);
    ehdr.e_ident[EI_CLASS]   = ELFCLASS64;
    ehdr.e_ident[EI_DATA]    = ELFDATA2LSB;
    ehdr.e_ident[EI_VERSION] = EV_CURRENT;
    ehdr.e_ident[EI_OSABI]   = ELFOSABI_NONE;
    ehdr.e_type      = ET_REL;
    ehdr.e_machine   = EM_X86_64;
    ehdr.e_version   = EV_CURRENT;
    ehdr.e_ehsize    = sizeof(Elf64_Ehdr);
    ehdr.e_shentsize = sizeof(Elf64_Shdr);
    ehdr.e_shnum     = NUM_SECTIONS;
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

    /* padding to rela_text_off */
    { size_t cur = data_off + data_size;
      if (rela_text_off > cur) write_padding(fd, rela_text_off - cur); }

    /* .rela.text */
    if (rela_text_size) write(fd, rela_text, rela_text_size);

    /* padding to rela_data_off */
    { size_t cur = rela_text_off + rela_text_size;
      if (rela_data_off > cur) write_padding(fd, rela_data_off - cur); }

    /* .rela.data */
    if (rela_data_size) write(fd, rela_data, rela_data_size);

    /* padding to symtab_off */
    { size_t cur = rela_data_off + rela_data_size;
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

    /* .rela.debug_info */
    if (rela_dbinfo_size > 0) {
        { size_t cur = (debug_str_size > 0 ? debug_str_off + debug_str_size
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

    /* padding to sh_off */
    { size_t cur = (rela_dbline_size > 0 ? rela_dbline_off + rela_dbline_size
                  : rela_dbinfo_size > 0 ? rela_dbinfo_off + rela_dbinfo_size
                  : debug_str_size > 0   ? debug_str_off + debug_str_size
                  : shstrtab_off + shstrtab_size);
      if (sh_off > cur) write_padding(fd, sh_off - cur); }

    /* section headers */
    write(fd, shdrs, sizeof(shdrs));

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
    free(symtab);
    free(strtab);
    free(shstrtab);
    free(sym_map);
    for (size_t i = 0; i < n_funcs; i++) free(funcs[i].code);
    free(funcs);
    for (size_t i = 0; i < n_datas; i++) free(datas[i].bytes);
    free(datas);
    free(bsses);
    free(relocs);
    for (size_t i = 0; i < exports.n; i++) free(exports.names[i]);
    free(exports.names);
    for (size_t i = 0; i < imports.n; i++) free(imports.names[i]);
    free(imports.names);
    dwbuf_free(&dw_abbrev);
    dwbuf_free(&dw_info);
    dwbuf_free(&dw_line);
    dwbuf_free(&dw_str);
    free(dw_relocs);
    free(rela_dbinfo);
    free(rela_dbline);

    #undef SYMTAB_PUSH
    #undef SYM_MAP_ADD
    #undef DW_RELOC_PUSH
}

int main(int argc, char **argv) {
    MIR_alloc_t alloc = &default_alloc;

    if (argc != 3) {
        fprintf(stderr, "Usage: %s <mir_input> <object_file>\n", argv[0]);
        return EXIT_FAILURE;
    }

    VARR_CREATE(char, temp_string, alloc, 0);
    VARR_CREATE(lib_t, extra_libs, alloc, 16);
    VARR_CREATE(char_ptr_t, lib_dirs, alloc, 16);
    for (int i = 0; i < (int)(sizeof(std_lib_dirs) / sizeof(char_ptr_t)); i++)
        VARR_PUSH(char_ptr_t, lib_dirs, std_lib_dirs[i]);
    lib_dirs_from_env_var("LD_LIBRARY_PATH");
    lib_dirs_from_env_var(MIR_ENV_VAR_LIB_DIRS);

    const char *mir_input_file = argv[1];
    const char *output_file = argv[2];

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
    {
        /* Optimisation level for code generation.  The MIR generator's default
           is 2 (GVN/CCP), but that pass can be extremely slow on large inputs
           (e.g. self-compiling c2mir.c).  Level 1 (register allocation +
           combiner) is a good default for ahead-of-time builds: it optimises
           well and completes quickly.  Override with the B2OBJ_OPT env var. */
        const char *opt = getenv("B2OBJ_OPT");
        int level = opt != NULL ? atoi(opt) : 1;
        MIR_gen_set_optimize_level(ctx, (unsigned)level);
        DBG("optimize level = %d", level);
    }
    DBG("starting MIR_link (eager code generation of all functions)");
    MIR_link(ctx, MIR_set_gen_interface, hybrid_import_resolver);
    DBG("MIR_link done (all functions generated)");

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
