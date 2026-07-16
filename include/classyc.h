/* This file is a part of MIR project.
   Copyright (C) 2020-2024 Vladimir Makarov <vmakarov.gcc@gmail.com>.
*/

#ifndef C2MIR_H

#define C2MIR_H

#include "mir.h"

#define COMMAND_LINE_SOURCE_NAME "<command-line>"
#define STDIN_SOURCE_NAME "<stdin>"

struct c2mir_macro_command {
  int def_p;              /* #define or #undef */
  const char *name, *def; /* def is used only when def_p is true */
};

struct c2mir_options {
  FILE *message_file;
  int debug_p, verbose_p, ignore_warnings_p, no_prepro_p, prepro_only_p;
  int syntax_only_p, pedantic_p, asm_p, object_p;
  int no_gen_p;     /* run preprocess+parse+check but skip MIR generation (LSP/analysis) */
  int debug_info_p; /* -g: emit source locations and debug info into MIR/bmir */
  int keep_syms_p;  /* keep symbol table + node positions after no_gen_p analysis (for LSP go-to-def etc.) */
  int exceptions_p; /* try/catch/throw + JIT safety guards (default on; -fno-exceptions disables) */
  int auto_release_p; /* -fauto-release: synthesize defer free(p) for owned heap locals that would leak */
  int ownership_report_p; /* -fownership-report: dump per-function alloc/release map after analysis */
  int check_whole_allocs_p; /* -fcheck-whole-allocs: link-time-style whole-program ownership analysis */
  int no_ownership_p; /* -fno-ownership: skip the ownership analysis pass entirely (no leak/UAF/double-free diagnostics, no -fauto-release synthesis) */
  int object_guards_p; /* -fobject-guards: side-table + quarantine runtime use-after-free / double-free guards on `new` class objects.  Ownership-directed: only derefs the ownership pass classifies as CHECK (MaybeOwned) are instrumented.  Off by default (opt-in). */
  int no_midopt_p; /* -fno-midopt: skip mid-level optimizer (check→gen). Default off = midopt runs. */
  int dump_mir_stats_p; /* -fdump-mir-stats: print per-module MIR func/insn/call counts after gen */
  size_t module_num;
  FILE *prepro_output_file; /* non-null for prepro_only_p */
  const char *output_file_name;
  size_t macro_commands_num, include_dirs_num;
  struct c2mir_macro_command *macro_commands;
  const char **include_dirs;
};

void c2mir_init (MIR_context_t ctx);
void c2mir_finish (MIR_context_t ctx);
int c2mir_compile (MIR_context_t ctx, struct c2mir_options *ops, int (*getc_func) (void *),
                   void *getc_data, const char *source_name, FILE *output_file);

typedef struct c2mir_pos {
  const char *fname;
  int lno, ln_pos;
} c2mir_pos_t;

/* 
 * Retained-analysis API for LSP / tools (use only with keep_syms_p + no_gen_p):
 * After a successful compile with keep_syms_p, the symbol table + node positions remain.
 * Call c2mir_release_analysis when switching documents or before next kept compile.
 */
void c2mir_release_analysis (MIR_context_t ctx);

/* Get the retained translation unit root node (N_MODULE) if available, NULL otherwise. */
struct node *c2mir_get_analysis_root (MIR_context_t ctx);

/* 
 * Find the definition location of a top-level identifier (e.g. function, class, global var).
 * Walks symbol table. Coordinates are 1-based (compiler convention).
 * Returns non-zero and fills out if found; out->fname will be the original source_name.
 */
int c2mir_find_definition (MIR_context_t ctx, const char *ident, c2mir_pos_t *out);

/*
 * Find the definition location for obj.member (or obj->member) using type of receiver.
 * Returns 1 and fills out (1-based) on success; 0 otherwise.
 */
int c2mir_find_member_definition (MIR_context_t ctx,
               const char *receiver,
               const char *member,
               c2mir_pos_t *out);

/*
 * Find all references to an identifier in the current analysis.
 * Walks the AST and collects positions of N_ID nodes whose name matches `ident`.
 * Coordinates are 1-based (compiler convention).
 * Returns the number of references found. On success, *out_refs is set to a
 * malloc'd array of c2mir_pos_t; caller must free with c2mir_free_references().
 * Returns 0 if the analysis is not available or `ident` is empty.
 */
int c2mir_find_references (MIR_context_t ctx, const char *ident, c2mir_pos_t **out_refs);

/* Free the array returned by c2mir_find_references. Safe with NULL. */
void c2mir_free_references (c2mir_pos_t *refs);

#endif
