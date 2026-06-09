/*
 * b2objmac.c - Convert a binary MIR (.bmir) file to a Mach-O 64-bit object file.
 *
 * This is the macOS counterpart of b2obj.c (which produces ELF objects).
 * It reads a .bmir file, generates x86-64 machine code via the MIR code
 * generator, and writes a Mach-O MH_OBJECT file that can be linked with
 * the macOS system linker (ld).
 *
 * Usage:  b2objmac <input.bmir> <output.o>
 *
 * Relocation mapping (ELF -> Mach-O x86-64):
 *   R_X86_64_PC32  -> X86_64_RELOC_SIGNED      (PC-relative 32-bit)
 *   R_X86_64_64    -> X86_64_RELOC_UNSIGNED     (absolute 64-bit)
 *
 * macOS 10.12 compatibility: no APFS-only APIs, no @available guards,
 * plain POSIX I/O, no Mach-O LC_BUILD_VERSION (uses LC_VERSION_MIN_MACOSX).
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
#include <mach-o/x86_64/reloc.h>

#include "mir-alloc-default.c"
#include "mir-gen.h" /* mir.h included transitively */

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
  size_t len = strlen (name);
  char *mangled = malloc (len + 2);
  mangled[0] = '_';
  memcpy (mangled + 1, name, len + 1);
  return mangled;
}

static int elf_reloc_to_macho (int elf_type) {
  switch (elf_type) {
  case R_X86_64_PC32: return X86_64_RELOC_SIGNED;
  case R_X86_64_64:   return X86_64_RELOC_UNSIGNED;
  case 4: return 2; /* R_X86_64_PLT32 -> X86_64_RELOC_BRANCH */
  default:
    fprintf (stderr, "warning: unknown ELF reloc type %d, treating as SIGNED\n", elf_type);
    return X86_64_RELOC_SIGNED;
  }
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

  /* ----- Phase 2: assign offsets within sections ----- */
  DBG ("phase 2: assigning section offsets");

  /* __text: concatenate all function codes (16-byte aligned) */
  size_t text_size = 0;
  for (size_t i = 0; i < n_funcs; i++) {
    if (i > 0) text_size = align_up (text_size, 16);
    funcs[i].text_offset = text_size;
    text_size += funcs[i].code_len;
  }

  /* __data: concatenate all data items (8-byte aligned) */
  size_t data_size = 0;
  for (size_t i = 0; i < n_datas; i++) {
    if (i > 0) data_size = align_up (data_size, 8);
    datas[i].data_offset = data_size;
    data_size += datas[i].size;
  }

  /* __bss: just total size (8-byte aligned per item) */
  size_t bss_size = 0;
  for (size_t i = 0; i < n_bsses; i++) {
    if (i > 0) bss_size = align_up (bss_size, 8);
    bsses[i].bss_offset = bss_size;
    bss_size += bsses[i].len;
  }


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
    mr->type    = R_X86_64_64; /* absolute 64-bit */
    mr->addend  = datas[i].ref_disp;
    mr->in_data = 1;
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

  size_t orig_n_relocs = n_relocs;
  for (size_t i = 0; i < orig_n_relocs; i++) {
    if (relocs[i].in_data) continue; /* data relocations are fine */
    size_t dummy;
    if (!name_set_find (&defined_names, relocs[i].symbol, &dummy)) {
      /* It's an external symbol referenced from __text. We need a stub. */
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
        text_size += 5; /* jmp rel32 */
        n_stubs++;
      }
    }
  }

  /* Now update the original relocations to point to the stubs! */
  for (size_t i = 0; i < orig_n_relocs; i++) {
    if (relocs[i].in_data) continue;
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

  /* Build __data data buffer */
  uint8_t *data_buf = calloc (1, data_size ? data_size : 1);
  for (size_t i = 0; i < n_datas; i++)
    if (datas[i].size) memcpy (data_buf + datas[i].data_offset, datas[i].bytes, datas[i].size);

  /* Write addends into section data for Mach-O (which uses REL, not RELA) */
  for (size_t i = 0; i < orig_n_relocs; i++) {
    mach_reloc_t *mr = &relocs[i];
    uint8_t *buf = mr->in_data ? data_buf : text_buf;
    if (mr->type == R_X86_64_64) {
      int64_t addend = mr->addend;
      memcpy (buf + mr->offset, &addend, 8);
    } else if (mr->type == R_X86_64_PC32) {
      int32_t addend = (int32_t) mr->addend;
      memcpy (buf + mr->offset, &addend, 4);
    }
  }

  /* Write stubs and add branch relocations for them */
  for (size_t i = 0; i < n_stubs; i++) {
    size_t off = stubs[i].stub_offset;
    text_buf[off] = 0xE9; /* jmp rel32 */
    memset (text_buf + off + 1, 0, 4);

    if (n_relocs >= cap_relocs) {
      cap_relocs = cap_relocs ? cap_relocs * 2 : 64;
      relocs = realloc (relocs, cap_relocs * sizeof (mach_reloc_t));
    }
    mach_reloc_t *mr = &relocs[n_relocs++];
    mr->offset  = off + 1;
    mr->symbol  = stubs[i].name;
    mr->type    = 4; /* R_X86_64_PLT32 -> X86_64_RELOC_BRANCH */
    mr->addend  = 0; 
    mr->in_data = 0;
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

  size_t n_local_syms = n_syms;

  for (size_t i = 0; i < n_funcs; i++) {
    const char *fname = funcs[i].name;
    size_t dummy;
    int is_exported = name_set_find (&exports, fname, &dummy);
    struct nlist_64 s = {0};
    const char *mname = macho_mangle (fname);
    s.n_un.n_strx = STRTAB_ADD (mname);
    s.n_type = N_SECT | (is_exported ? N_EXT : 0);
    s.n_sect = MACH_SECT_TEXT;
    s.n_desc = 0;
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
    s.n_sect = MACH_SECT_BSS;
    s.n_desc = 0;
    s.n_value = text_size + data_size + bsses[i].bss_offset;
    SYM_MAP_ADD (mname, n_syms);
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
    ri.r_length = (mach_type == X86_64_RELOC_UNSIGNED) ? 3 : 2; /* 3=8byte, 2=4byte */
    ri.r_pcrel = (mach_type == X86_64_RELOC_SIGNED || mach_type == 2) ? 1 : 0;

    if (relocs[i].in_data)
      reloc_data[rd_idx++] = ri;
    else
      reloc_text[rt_idx++] = ri;
  }

  DBG ("  __text relocs: %zu, __data relocs: %zu", n_reloc_text, n_reloc_data);

  /* ----- Phase 5: compute file layout and write Mach-O ----- */
  DBG ("phase 5: computing file layout and writing Mach-O");

  /*
   * Mach-O object file layout:
   *
   *   [mach_header_64]
   *   [load commands: LC_SEGMENT_64, LC_SYMTAB, LC_DYSYMTAB, LC_VERSION_MIN_MACOSX]
   *   [__TEXT,__text section data]
   *   [__DATA,__data section data]
   *   (__DATA,__bss has no file content)
   *   [__text relocation entries]
   *   [__data relocation entries]
   *   [symbol table (nlist_64)]
   *   [string table]
   */

  /* Count load commands */
  uint32_t ncmds = 4; /* LC_SEGMENT_64, LC_SYMTAB, LC_DYSYMTAB, LC_VERSION_MIN_MACOSX */
  size_t sizeofcmds = sizeof (struct segment_command_64)
                     + sizeof (struct section_64) * 3  /* __text, __data, __bss */
                     + sizeof (struct symtab_command)
                     + sizeof (struct dysymtab_command)
                     + sizeof (struct version_min_command);

  size_t header_size = sizeof (struct mach_header_64);
  size_t lc_end = header_size + sizeofcmds;

  /* Section data starts after the header + load commands, page-aligned */
  size_t text_file_off = align_up (lc_end, 16); /* 16-byte align for code */
  size_t data_file_off = text_file_off + text_size;
  if (data_size > 0) data_file_off = align_up (data_file_off, 8);

  /* Relocation entries follow section data */
  size_t reloc_text_off = data_file_off + data_size;
  size_t reloc_data_off = reloc_text_off + n_reloc_text * sizeof (struct relocation_info);

  /* Symbol table */
  size_t symtab_off = reloc_data_off + n_reloc_data * sizeof (struct relocation_info);
  symtab_off = align_up (symtab_off, 8);

  /* String table */
  size_t strtab_off = symtab_off + n_syms * sizeof (struct nlist_64);

  /* Total file size */
  size_t file_size = strtab_off + strtab_size;

  /* ----- Build load commands ----- */

  /* LC_SEGMENT_64 for the single segment encompassing all sections */
  struct segment_command_64 seg_cmd;
  memset (&seg_cmd, 0, sizeof (seg_cmd));
  seg_cmd.cmd = LC_SEGMENT_64;
  seg_cmd.cmdsize = sizeof (struct segment_command_64) + 3 * sizeof (struct section_64);
  strncpy (seg_cmd.segname, "__TEXT", 16);
  seg_cmd.vmaddr = 0;
  seg_cmd.vmsize = text_size + data_size + bss_size;
  seg_cmd.fileoff = text_file_off;
  seg_cmd.filesize = text_size + data_size;
  seg_cmd.maxprot = 7;  /* rwx */
  seg_cmd.initprot = 7; /* rwx */
  seg_cmd.nsects = 3;
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
  sec_text.reloff = (uint32_t)reloc_text_off;
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
  sec_bss.addr = text_size + data_size;
  sec_bss.size = bss_size;
  sec_bss.offset = 0; /* S_NO_DATA */
  sec_bss.align = 3;  /* 2^3 = 8-byte alignment */
  sec_bss.reloff = 0;
  sec_bss.nreloc = 0;
  sec_bss.flags = S_ZEROFILL;
  sec_bss.reserved1 = 0;
  sec_bss.reserved2 = 0;
  sec_bss.reserved3 = 0;

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
  mhdr.cputype = CPU_TYPE_X86_64;
  mhdr.cpusubtype = CPU_SUBTYPE_X86_64_ALL;
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

  /* Load commands */
  write (fd, &seg_cmd, sizeof (seg_cmd));
  write (fd, &sec_text, sizeof (sec_text));
  write (fd, &sec_data, sizeof (sec_data));
  write (fd, &sec_bss, sizeof (sec_bss));
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

  /* __text relocation entries */
  if (n_reloc_text)
    write (fd, reloc_text, n_reloc_text * sizeof (struct relocation_info));

  /* __data relocation entries */
  if (n_reloc_data)
    write (fd, reloc_data, n_reloc_data * sizeof (struct relocation_info));

  /* Padding to symbol table */
  {
    size_t cur = reloc_data_off + n_reloc_data * sizeof (struct relocation_info);
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
  for (size_t i = 0; i < n_funcs; i++) free (funcs[i].code);
  free (funcs);
  for (size_t i = 0; i < n_datas; i++) free (datas[i].bytes);
  free (datas);
  free (bsses);
  free (relocs);
  for (size_t i = 0; i < exports.n; i++) free (exports.names[i]);
  free (exports.names);
  for (size_t i = 0; i < imports.n; i++) free (imports.names[i]);
  free (imports.names);

  #undef SYMTAB_PUSH
  #undef SYM_MAP_ADD
  #undef STRTAB_ADD
}

/* ================================================================== */
/*  main                                                               */
/* ================================================================== */
int main (int argc, char **argv) {
  MIR_alloc_t alloc = &default_alloc;

  if (argc != 3) {
    fprintf (stderr, "Usage: %s <mir_input> <object_file>\n", argv[0]);
    return EXIT_FAILURE;
  }

  VARR_CREATE (char, temp_string, alloc, 0);
  VARR_CREATE (lib_t, extra_libs, alloc, 16);
  VARR_CREATE (char_ptr_t, lib_dirs, alloc, 16);
  for (int i = 0; i < (int)(sizeof (std_lib_dirs) / sizeof (char_ptr_t)); i++)
    VARR_PUSH (char_ptr_t, lib_dirs, std_lib_dirs[i]);
  lib_dirs_from_env_var ("DYLD_LIBRARY_PATH");
  lib_dirs_from_env_var (MIR_ENV_VAR_LIB_DIRS);

  const char *mir_input_file = argv[1];
  const char *output_file = argv[2];

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
    const char *opt = getenv ("B2OBJ_OPT");
    int level = opt != NULL ? atoi (opt) : 1;
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
