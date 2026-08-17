/* nmb — nm-like inspector for binary (.bmir) and textual (.mir) MIR.
 *
 * List modules, symbols, debug types, line coverage, and safety-trap sites
 * without going through b2obj / objdump.  Intended for checking what
 * classyc actually emitted.
 *
 *   nmb file.bmir [file.mir ...]
 *   nmb -s -t -l examples/http_crud/http_crud.bmir
 */
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mir.h"
#if !MIR_NO_DBINFO
#include "mir-dbinfo.h"
#endif

static const char *prog = "nmb";

/* ── flags ──────────────────────────────────────────────────────────────── */
static int opt_undef;      /* -u: imports only */
static int opt_globals;    /* -g: exported / named objects only */
static int opt_protos;     /* -p: include prototypes */
static int opt_all;        /* -a: include unnamed / compiler-internal */
static int opt_debug_ty;   /* -T: dump module debug types */
static int opt_debug_var;  /* -d: dump per-func debug vars */
static int opt_lines;      /* -l: line-coverage per function */
static int opt_traps;      /* -t: list _safety_trap call sites */
static int opt_stats;      /* -s: per-file / per-module counts */
static int opt_nosort;     /* -v: file order (default is sort by name) */
static int opt_posix;      /* -P: compact "type name extra" */
static int opt_quiet;      /* -q: no module banners */

/* ── MIR error ──────────────────────────────────────────────────────────── */
static void MIR_NO_RETURN nmb_error (MIR_error_type_t type, const char *fmt, ...) {
  va_list ap;
  fprintf (stderr, "%s: MIR error %d: ", prog, (int) type);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  exit (2);
}

/* ── load .bmir / .mir ──────────────────────────────────────────────────── */
static int looks_like_text (FILE *f) {
  int c;
  long pos = ftell (f);
  if (pos < 0) return 0;
  while ((c = fgetc (f)) != EOF && isspace ((unsigned char) c))
    ;
  if (c != EOF) ungetc (c, f);
  {
    char buf[16];
    size_t n = fread (buf, 1, sizeof buf - 1, f);
    buf[n] = 0;
    fseek (f, pos, SEEK_SET);
    if (n == 0) return 0;
    /* textual MIR starts with `<name>:\tmodule` or a `#` comment */
    if (buf[0] == '#' || buf[0] == ';') return 1;
    for (size_t i = 0; i < n; i++) {
      if (buf[i] == ':' || buf[i] == '\t' || buf[i] == ' ') return 1;
      if (!isprint ((unsigned char) buf[i]) && buf[i] != '\n' && buf[i] != '\r') return 0;
    }
    return isalpha ((unsigned char) buf[0]) || buf[0] == '_';
  }
}

static char *slurp (FILE *f, size_t *len_out) {
  char *buf = NULL;
  size_t cap = 0, n = 0;
  int c;
  while ((c = fgetc (f)) != EOF) {
    if (n + 1 >= cap) {
      cap = cap ? cap * 2 : 1 << 16;
      buf = realloc (buf, cap);
      if (buf == NULL) {
        fprintf (stderr, "%s: out of memory\n", prog);
        exit (2);
      }
    }
    buf[n++] = (char) c;
  }
  if (buf == NULL) {
    buf = malloc (1);
    if (buf == NULL) exit (2);
  }
  buf[n] = 0;
  if (len_out) *len_out = n;
  return buf;
}

static int load_file (MIR_context_t ctx, const char *path) {
  FILE *f = fopen (path, "rb");
  int text;
  if (f == NULL) {
    perror (path);
    return 0;
  }
  {
    const char *dot = strrchr (path, '.');
    if (dot != NULL && strcmp (dot, ".mir") == 0)
      text = 1;
    else if (dot != NULL && strcmp (dot, ".bmir") == 0)
      text = 0;
    else
      text = looks_like_text (f);
  }
  if (text) {
    char *s = slurp (f, NULL);
    fclose (f);
#if !MIR_NO_SCAN
    MIR_scan_string (ctx, s);
#else
    fprintf (stderr, "%s: this MIR build has no scanner; cannot read %s\n", prog, path);
    free (s);
    return 0;
#endif
    free (s);
  } else {
    MIR_read (ctx, f);
    fclose (f);
  }
  return 1;
}

/* ── item helpers ───────────────────────────────────────────────────────── */
static const char *item_name (MIR_context_t ctx, MIR_item_t item) {
  const char *n = MIR_item_name (ctx, item);
  return n ? n : "";
}

static int is_internal_name (const char *n) {
  if (n == NULL || n[0] == 0) return 1;
  if (n[0] == '.') return 1;
  if (n[0] == '_' && n[1] == '_') return 1; /* __ctor, __proto, __thunk, … */
  return 0;
}

static char nm_letter (MIR_item_t item, int exported_p) {
  switch (item->item_type) {
  case MIR_func_item: return exported_p ? 'T' : 't';
  case MIR_proto_item: return 'P';
  case MIR_import_item: return 'U';
  case MIR_export_item: return 'E';
  case MIR_forward_item: return 'F';
  case MIR_data_item:
  case MIR_ref_data_item:
  case MIR_lref_data_item:
  case MIR_expr_data_item: return exported_p ? 'D' : 'd';
  case MIR_bss_item: return exported_p ? 'B' : 'b';
  case MIR_tls_data_item: return exported_p ? 'R' : 'r';
  case MIR_tls_bss_item: return exported_p ? 'S' : 's';
  default: return '?';
  }
}

static const char *item_kind (MIR_item_t item) {
  switch (item->item_type) {
  case MIR_func_item: return "func";
  case MIR_proto_item: return "proto";
  case MIR_import_item: return "import";
  case MIR_export_item: return "export";
  case MIR_forward_item: return "forward";
  case MIR_data_item: return "data";
  case MIR_ref_data_item: return "ref";
  case MIR_lref_data_item: return "lref";
  case MIR_expr_data_item: return "expr";
  case MIR_bss_item: return "bss";
  case MIR_tls_data_item: return "tls_data";
  case MIR_tls_bss_item: return "tls_bss";
  default: return "?";
  }
}

static size_t item_size (MIR_item_t item) {
  switch (item->item_type) {
  case MIR_data_item: return item->u.data->nel;
  case MIR_bss_item: return (size_t) item->u.bss->len;
  case MIR_tls_data_item: return item->u.tls_data->nel;
  case MIR_tls_bss_item: return (size_t) item->u.tls_bss->len;
  default: return 0;
  }
}

static int want_item (MIR_context_t ctx, MIR_item_t item, int exported_p) {
  if (opt_undef) return item->item_type == MIR_import_item;
  if (item->item_type == MIR_proto_item && !opt_protos && !opt_all) return 0;
  if (opt_globals) {
    if (item->item_type == MIR_import_item) return 0;
    if (item->item_type == MIR_proto_item) return 0;
    if (item->item_type == MIR_export_item) return 1;
    if (item->item_type == MIR_func_item) return exported_p;
    return item_name (ctx, item)[0] != 0 && !is_internal_name (item_name (ctx, item));
  }
  if (!opt_all && is_internal_name (item_name (ctx, item))
      && item->item_type != MIR_import_item && item->item_type != MIR_export_item
      && item->item_type != MIR_func_item)
    return 0;
  return 1;
}

/* ── function stats / line coverage / traps ─────────────────────────────── */
typedef struct {
  uint32_t ninsns, ncalls, ntraps, nlocated, nlabels;
  uint16_t first_file, last_file;
  uint32_t first_line, last_line;
} func_stat_t;

static int ref_is_named (MIR_context_t ctx, MIR_op_t op, const char *want) {
  const char *n;
  if (op.mode != MIR_OP_REF || op.u.ref == NULL) return 0;
  n = item_name (ctx, op.u.ref);
  return n != NULL && strcmp (n, want) == 0;
}

static void analyze_func (MIR_context_t ctx, MIR_func_t func, func_stat_t *st) {
  MIR_insn_t insn;
  memset (st, 0, sizeof *st);
  for (insn = DLIST_HEAD (MIR_insn_t, func->insns); insn != NULL;
       insn = DLIST_NEXT (MIR_insn_t, insn)) {
    if (insn->code == MIR_LABEL) {
      st->nlabels++;
      continue;
    }
    st->ninsns++;
    if (MIR_call_code_p (insn->code)) {
      st->ncalls++;
      {
        size_t nops = MIR_insn_nops (ctx, insn);
        for (size_t i = 0; i < nops; i++) {
          if (ref_is_named (ctx, insn->ops[i], "_safety_trap")) {
            st->ntraps++;
            break;
          }
        }
      }
    }
    if (insn->source_line != 0 && insn->source_file_id != 0) {
      st->nlocated++;
      if (st->first_line == 0) {
        st->first_file = insn->source_file_id;
        st->first_line = insn->source_line;
      }
      st->last_file = insn->source_file_id;
      st->last_line = insn->source_line;
    }
  }
}

static const char *file_name (MIR_module_t mod, uint16_t id) {
  if (mod == NULL || mod->source_files == NULL || id == 0 || id > mod->num_source_files)
    return NULL;
  return mod->source_files[id];
}

static const char *base_name (const char *p) {
  const char *s;
  if (p == NULL) return NULL;
  s = strrchr (p, '/');
  return s ? s + 1 : p;
}

static const char *trap_reason (int64_t r) {
  switch (r) {
  case 1: return "oob";
  case 2: return "null";
  case 3: return "arith";
  case 4: return "uaf";
  case 5: return "shift";
  default: return "unknown";
  }
}

static void print_traps (MIR_context_t ctx, MIR_module_t mod, MIR_func_t func) {
  MIR_insn_t insn;
  unsigned idx = 0;
  for (insn = DLIST_HEAD (MIR_insn_t, func->insns); insn != NULL;
       insn = DLIST_NEXT (MIR_insn_t, insn)) {
    size_t nops, i;
    int64_t reason = -1, line = 0;
    if (!MIR_call_code_p (insn->code)) continue;
    nops = MIR_insn_nops (ctx, insn);
    {
      int hit = 0;
      for (i = 0; i < nops; i++)
        if (ref_is_named (ctx, insn->ops[i], "_safety_trap")) {
          hit = 1;
          break;
        }
      if (!hit) continue;
    }
    /* void call: proto, item, reason, file_id, line */
    if (nops >= 5) {
      if (insn->ops[2].mode == MIR_OP_INT) reason = insn->ops[2].u.i;
      else if (insn->ops[2].mode == MIR_OP_UINT) reason = (int64_t) insn->ops[2].u.u;
      if (insn->ops[4].mode == MIR_OP_INT) line = insn->ops[4].u.i;
      else if (insn->ops[4].mode == MIR_OP_UINT) line = (int64_t) insn->ops[4].u.u;
    }
    printf ("    trap %-6s line=%-5ld", trap_reason (reason), (long) line);
    if (insn->source_line != 0) {
      const char *fn = file_name (mod, insn->source_file_id);
      printf ("  @ %s:%u", fn ? fn : "?", insn->source_line);
      if (insn->source_col) printf (":%u", insn->source_col);
    } else {
      printf ("  @ (no insn loc)");
    }
    putchar ('\n');
    idx++;
    (void) idx;
  }
}

#if !MIR_NO_DBINFO
static const char *dbt_kind (MIR_dbtype_kind_t k) {
  static const char *names[]
    = {"void",     "base",     "ptr",      "array",    "struct", "union",
       "enum",     "func",     "typedef",  "const",    "volatile", "restrict"};
  if ((unsigned) k < sizeof names / sizeof names[0]) return names[k];
  return "?";
}

static void print_debug_types (MIR_module_t mod) {
  uint32_t i;
  if (mod->dbtypes == NULL || mod->dbtypes->num_types <= 1) {
    printf ("  (no debug types)\n");
    return;
  }
  printf ("  debug types (%u):\n", mod->dbtypes->num_types - 1);
  for (i = 1; i < mod->dbtypes->num_types; i++) {
    MIR_dbtype_t *t = &mod->dbtypes->types[i];
    printf ("    [%u] %-8s", i, dbt_kind (t->kind));
    if (t->name) printf (" \"%s\"", t->name);
    if (t->byte_size) printf (" size=%u", t->byte_size);
    switch (t->kind) {
    case MIR_DBT_BASE: printf (" enc=%u", t->u.base.encoding); break;
    case MIR_DBT_PTR:
    case MIR_DBT_CONST:
    case MIR_DBT_VOLATILE:
    case MIR_DBT_RESTRICT:
    case MIR_DBT_TYPEDEF: printf (" -> %u", t->u.ref.target_id); break;
    case MIR_DBT_ARRAY:
      printf (" el=%u count=%lld", t->u.array.element_id, (long long) t->u.array.count);
      break;
    case MIR_DBT_STRUCT:
    case MIR_DBT_UNION: printf (" members=%u", t->u.aggregate.num_members); break;
    case MIR_DBT_ENUM:
      printf (" enumerators=%u", t->u.enumeration.num_enumerators);
      break;
    case MIR_DBT_FUNC:
      printf (" ret=%u params=%u%s", t->u.func.return_id, t->u.func.num_params,
              t->u.func.variadic ? " ..." : "");
      break;
    default: break;
    }
    putchar ('\n');
  }
}

static void print_func_vars (MIR_func_t func) {
  uint32_t i;
  if (func->dbinfo == NULL || func->dbinfo->num_vars == 0) return;
  printf ("    debug vars (%u):\n", func->dbinfo->num_vars);
  for (i = 0; i < func->dbinfo->num_vars; i++) {
    MIR_dbvar_t *v = &func->dbinfo->vars[i];
    printf ("      %s %-16s type=%-4u", v->is_param ? "param" : "var  ",
            v->source_name ? v->source_name : "?", v->type_id);
    switch (v->loc_kind) {
    case MIR_DBLOC_REG: printf (" reg=%s", v->loc.reg_name ? v->loc.reg_name : "?"); break;
    case MIR_DBLOC_FRAME: printf (" frame=%+lld", (long long) v->loc.frame_offset); break;
    case MIR_DBLOC_STATIC: printf (" static=%s", v->loc.item_name ? v->loc.item_name : "?"); break;
    default: printf (" loc=?"); break;
    }
    if (v->decl_line) printf (" line=%u", v->decl_line);
    putchar ('\n');
  }
}
#endif

/* ── collected rows (for sorting) ───────────────────────────────────────── */
typedef struct {
  MIR_item_t item;
  int exported_p;
  const char *name;
} row_t;

static int row_cmp (const void *a, const void *b) {
  const row_t *x = a, *y = b;
  int c = strcmp (x->name ? x->name : "", y->name ? y->name : "");
  if (c) return c;
  return (int) x->item->item_type - (int) y->item->item_type;
}

static int export_covers (MIR_context_t ctx, MIR_module_t mod, const char *name) {
  MIR_item_t it;
  if (name == NULL || name[0] == 0) return 0;
  for (it = DLIST_HEAD (MIR_item_t, mod->items); it != NULL; it = DLIST_NEXT (MIR_item_t, it)) {
    if (it->item_type == MIR_export_item && strcmp (item_name (ctx, it), name) == 0) return 1;
  }
  return 0;
}

static void print_func_sig (MIR_context_t ctx, MIR_func_t func) {
  uint32_t i;
  fputc (' ', stdout);
  if (func->nres == 0)
    fputs ("void", stdout);
  else
    for (i = 0; i < func->nres; i++) {
      if (i) fputc (',', stdout);
      fputs (MIR_type_str (ctx, func->res_types[i]), stdout);
    }
  printf ("(%u)", func->nargs);
}

static void print_row (MIR_context_t ctx, MIR_module_t mod, row_t *r) {
  char let = nm_letter (r->item, r->exported_p);
  const char *name = r->name && r->name[0] ? r->name : "(anon)";
  if (opt_posix) {
    printf ("%c %s\n", let, name);
    return;
  }
  printf ("  %c  %-40s %-8s", let, name, item_kind (r->item));
  if (r->item->item_type == MIR_func_item) {
    MIR_func_t fn = r->item->u.func;
    func_stat_t st;
    analyze_func (ctx, fn, &st);
    print_func_sig (ctx, fn);
    printf ("  insns=%u calls=%u", st.ninsns, st.ncalls);
    if (st.ntraps) printf (" traps=%u", st.ntraps);
    if (opt_lines) {
      printf (" loc=%u/%u", st.nlocated, st.ninsns);
      if (st.first_line) {
        const char *fnm = file_name (mod, st.first_file);
        printf ("  %s:%u", fnm ? base_name (fnm) : "?", st.first_line);
        if (st.last_line && (st.last_file != st.first_file || st.last_line != st.first_line))
          printf ("-%u", st.last_line);
      }
    }
#if !MIR_NO_DBINFO
    if (fn->dbinfo && fn->dbinfo->num_vars)
      printf (" vars=%u", fn->dbinfo->num_vars);
#endif
  } else {
    size_t sz = item_size (r->item);
    if (sz) printf ("  size=%zu", sz);
  }
  putchar ('\n');
  if (opt_debug_var && r->item->item_type == MIR_func_item) {
#if !MIR_NO_DBINFO
    print_func_vars (r->item->u.func);
#endif
  }
  if (opt_traps && r->item->item_type == MIR_func_item)
    print_traps (ctx, mod, r->item->u.func);
}

/* ── module dump ────────────────────────────────────────────────────────── */
typedef struct {
  unsigned nfunc, nproto, nimp, nexp, nfwd, ndata, nbss, ntls;
  unsigned ninsns, ncalls, ntraps, nlocated;
  unsigned nfiles, ntypes, nvars;
} mod_stat_t;

static void accumulate (MIR_context_t ctx, MIR_module_t mod, mod_stat_t *ms) {
  MIR_item_t it;
  memset (ms, 0, sizeof *ms);
  ms->nfiles = mod->num_source_files;
#if !MIR_NO_DBINFO
  if (mod->dbtypes && mod->dbtypes->num_types > 1) ms->ntypes = mod->dbtypes->num_types - 1;
#endif
  for (it = DLIST_HEAD (MIR_item_t, mod->items); it != NULL; it = DLIST_NEXT (MIR_item_t, it)) {
    switch (it->item_type) {
    case MIR_func_item: {
      func_stat_t st;
      ms->nfunc++;
      analyze_func (ctx, it->u.func, &st);
      ms->ninsns += st.ninsns;
      ms->ncalls += st.ncalls;
      ms->ntraps += st.ntraps;
      ms->nlocated += st.nlocated;
#if !MIR_NO_DBINFO
      if (it->u.func->dbinfo) ms->nvars += it->u.func->dbinfo->num_vars;
#endif
      break;
    }
    case MIR_proto_item: ms->nproto++; break;
    case MIR_import_item: ms->nimp++; break;
    case MIR_export_item: ms->nexp++; break;
    case MIR_forward_item: ms->nfwd++; break;
    case MIR_bss_item: ms->nbss++; break;
    case MIR_tls_data_item:
    case MIR_tls_bss_item: ms->ntls++; break;
    default: ms->ndata++; break;
    }
  }
}

static void dump_module (MIR_context_t ctx, MIR_module_t mod, const char *file) {
  MIR_item_t it;
  row_t *rows = NULL;
  size_t nrows = 0, rcap = 0;
  mod_stat_t ms;
  uint32_t i;

  accumulate (ctx, mod, &ms);

  if (!opt_quiet) {
    printf ("%s  module %s\n", file ? file : "-", mod->name ? mod->name : "?");
    if (opt_stats) {
      printf ("  files=%u types=%u funcs=%u protos=%u imports=%u exports=%u\n", ms.nfiles,
              ms.ntypes, ms.nfunc, ms.nproto, ms.nimp, ms.nexp);
      printf ("  data=%u bss=%u tls=%u  insns=%u calls=%u traps=%u loc=%u/%u vars=%u\n",
              ms.ndata, ms.nbss, ms.ntls, ms.ninsns, ms.ncalls, ms.ntraps, ms.nlocated,
              ms.ninsns, ms.nvars);
    }
    if (mod->num_source_files && (opt_stats || opt_lines || opt_debug_ty)) {
      printf ("  source files:\n");
      for (i = 1; i <= mod->num_source_files; i++)
        printf ("    [%u] %s\n", i, mod->source_files[i]);
    }
  }

#if !MIR_NO_DBINFO
  if (opt_debug_ty) print_debug_types (mod);
#endif

  for (it = DLIST_HEAD (MIR_item_t, mod->items); it != NULL; it = DLIST_NEXT (MIR_item_t, it)) {
    int exp = 0;
    const char *n = item_name (ctx, it);
    if (it->item_type == MIR_func_item || it->item_type == MIR_data_item
        || it->item_type == MIR_bss_item)
      exp = export_covers (ctx, mod, n);
    if (!want_item (ctx, it, exp)) continue;
    if (nrows == rcap) {
      rcap = rcap ? rcap * 2 : 64;
      rows = realloc (rows, rcap * sizeof *rows);
      if (rows == NULL) {
        fprintf (stderr, "%s: out of memory\n", prog);
        exit (2);
      }
    }
    rows[nrows].item = it;
    rows[nrows].exported_p = exp;
    rows[nrows].name = n;
    nrows++;
  }
  if (!opt_nosort && nrows > 1) qsort (rows, nrows, sizeof *rows, row_cmp);
  for (i = 0; i < nrows; i++) print_row (ctx, mod, &rows[i]);
  free (rows);
}

static void usage (void) {
  fprintf (stderr,
           "Usage: %s [options] file.bmir|file.mir ...\n"
           "\n"
           "List MIR items the way nm lists ELF symbols.\n"
           "\n"
           "  -a     include unnamed / compiler-internal items\n"
           "  -d     dump per-function debug variables\n"
           "  -g     globals only (exports and named objects)\n"
           "  -l     line-coverage (located insns / first–last src)\n"
           "  -p     include prototypes\n"
           "  -P     posix-ish: \"T name\" only\n"
           "  -q     no module banner\n"
           "  -s     module statistics\n"
           "  -t     list _safety_trap call sites\n"
           "  -T     dump the module debug-type table\n"
           "  -u     undefined (imports) only\n"
           "  -v     do not sort; file order\n"
           "  -h     this help\n",
           prog);
}

int main (int argc, char **argv) {
  int i, nfiles = 0;
  const char **files;
  MIR_context_t ctx;

  prog = argv[0] ? argv[0] : "nmb";
  files = calloc ((size_t) argc, sizeof *files);
  if (files == NULL) return 2;

  for (i = 1; i < argc; i++) {
    const char *a = argv[i];
    if (a[0] != '-' || strcmp (a, "-") == 0) {
      files[nfiles++] = a;
      continue;
    }
    if (strcmp (a, "-h") == 0 || strcmp (a, "--help") == 0) {
      usage ();
      return 0;
    }
    for (const char *p = a + 1; *p; p++) {
      switch (*p) {
      case 'a': opt_all = 1; break;
      case 'd': opt_debug_var = 1; break;
      case 'g': opt_globals = 1; break;
      case 'l': opt_lines = 1; break;
      case 'p': opt_protos = 1; break;
      case 'P': opt_posix = 1; break;
      case 'q': opt_quiet = 1; break;
      case 's': opt_stats = 1; break;
      case 't': opt_traps = 1; break;
      case 'T': opt_debug_ty = 1; break;
      case 'u': opt_undef = 1; break;
      case 'v': opt_nosort = 1; break;
      default:
        fprintf (stderr, "%s: unknown option -%c\n", prog, *p);
        usage ();
        return 1;
      }
    }
  }
  if (nfiles == 0) {
    usage ();
    return 1;
  }

  ctx = MIR_init ();
  MIR_set_error_func (ctx, nmb_error);

  for (i = 0; i < nfiles; i++) {
    MIR_module_t last = DLIST_TAIL (MIR_module_t, *MIR_get_module_list (ctx));
    MIR_module_t m;
    if (!load_file (ctx, files[i])) {
      MIR_finish (ctx);
      return 1;
    }
    m = last == NULL ? DLIST_HEAD (MIR_module_t, *MIR_get_module_list (ctx))
                     : DLIST_NEXT (MIR_module_t, last);
    if (m == NULL) {
      fprintf (stderr, "%s: %s: no MIR module\n", prog, files[i]);
      continue;
    }
    for (; m != NULL; m = DLIST_NEXT (MIR_module_t, m)) dump_module (ctx, m, files[i]);
  }

  MIR_finish (ctx);
  free (files);
  return 0;
}
