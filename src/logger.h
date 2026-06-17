/* logger.h — ClassyC compiler output: colors + diagnostics plumbing.
   Single-header.  This is the one place that knows how compiler messages are
   colored, printed, and (optionally) forwarded as structured diagnostics.

   Two facilities:
     1. ANSI color helpers (log_color_enabled / log_c + LOG_* codes).
     2. A diagnostic sink: a host (e.g. the LSP server) may register a callback
        to receive every error/warning as structured data instead of only as
        printed text.  The sink is a single process-wide hook whose storage lives
        in the translation unit that defines LOGGER_IMPL (classyc.c).

   Colors are emitted only to interactive terminals and respect NO_COLOR
   (https://no-color.org), so piped/file/CI output stays plain and unchanged.

   Copyright (C) 2025-2026 ClassyC project.  */

#ifndef CLASSYC_LOGGER_H
#define CLASSYC_LOGGER_H

#include <stdio.h>
#include <stdlib.h>

#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#define LOG_HAVE_ISATTY 1
#else
#define LOG_HAVE_ISATTY 0
#endif

/* ── ANSI SGR escape sequences ─────────────────────────────────────────── */
#define LOG_RESET    "\033[0m"
#define LOG_BOLD     "\033[1m"
#define LOG_DIM      "\033[2m"
#define LOG_RED      "\033[31m"
#define LOG_GREEN    "\033[32m"
#define LOG_YELLOW   "\033[33m"
#define LOG_BLUE     "\033[34m"
#define LOG_MAGENTA  "\033[35m"
#define LOG_CYAN     "\033[36m"
#define LOG_GRAY     "\033[90m"
#define LOG_BRED     "\033[1;31m" /* bold red    — errors            */
#define LOG_BYELLOW  "\033[1;33m" /* bold yellow — warnings          */
#define LOG_BCYAN    "\033[1;36m" /* bold cyan   — AST node names    */
#define LOG_BGREEN   "\033[1;32m"
#define LOG_BMAGENTA "\033[1;35m"

/* TRUE when ANSI colors should be written to stream `f`: it is an interactive
   terminal and NO_COLOR is unset.  Result is suitable to cache per output. */
static inline int log_color_enabled (FILE *f) {
  if (f == NULL) return 0;
  if (getenv ("NO_COLOR") != NULL) return 0;
#if LOG_HAVE_ISATTY
  return isatty (fileno (f));
#else
  return 0;
#endif
}

/* Returns `code` when `on`, else "".  Lets a single fprintf carry optional color
   without branching, e.g.:
     fprintf (f, "%s%s%s", log_c (on, LOG_BRED), text, log_c (on, LOG_RESET)); */
static inline const char *log_c (int on, const char *code) { return on ? code : ""; }

/* ── Structured diagnostics ────────────────────────────────────────────── */

/* One compiler diagnostic.  Positions are 1-based (as produced by the parser);
   `line < 0` means "no position".  Strings are borrowed for the call only. */
typedef struct {
  const char *file;
  int line, col;
  int error_p; /* 1 = error, 0 = warning */
  const char *message;
} log_diag_t;

typedef void (*log_diag_fn) (void *data, const log_diag_t *d);

/* Register/replace the process-wide diagnostic sink (NULL disables it).
   Single-threaded, like the rest of the compile path. */
void log_set_diag_sink (log_diag_fn fn, void *data);

/* TRUE when a sink is registered (lets callers skip formatting work otherwise). */
int log_diag_active (void);

/* Forward a structured diagnostic to the registered sink (no-op if none). */
void log_emit_diag (const log_diag_t *d);

/* Print "file:line:col: <label> -- " to `f`, colorized when `f` is a terminal.
   The position is omitted when line < 0. */
void log_print_diag_prefix (FILE *f, const char *file, int line, int col, const char *label,
                            const char *label_color);

/* ── Debug log (file) ──────────────────────────────────────────────────── */

/* A simple append-only text log, separate from stdout/stderr.  Intended for
   stdio servers like classyc-lsp, whose stdout carries the LSP wire protocol
   and so must never be used for ad-hoc logging.  Until log_debug_open() is
   called the log is disabled and log_debug() is a no-op, so ordinary compiler
   runs write no file.

   `path` opens (append) that file; a previously opened log is closed first.
   Passing NULL closes the log and disables logging.  Returns 1 on success
   (or when disabling), 0 if the file could not be opened. */
int log_debug_open (const char *path);

/* TRUE when a debug log is currently open. */
int log_debug_active (void);

/* Append one timestamped, newline-terminated line to the debug log (printf
   style).  No-op when no log is open.  Each line is flushed immediately so the
   log stays useful even if the process later hangs or crashes. */
void log_debug (const char *fmt, ...)
#if defined(__GNUC__)
  __attribute__ ((format (printf, 1, 2)))
#endif
  ;

#ifdef LOGGER_IMPL
#include <stdarg.h>
#include <time.h>

static log_diag_fn log__diag_fn = NULL;
static void *log__diag_data = NULL;
static FILE *log__debug_file = NULL;

int log_debug_open (const char *path) {
  if (log__debug_file != NULL) {
    fclose (log__debug_file);
    log__debug_file = NULL;
  }
  if (path == NULL) return 1; /* request to disable logging */
  log__debug_file = fopen (path, "a");
  return log__debug_file != NULL;
}

int log_debug_active (void) { return log__debug_file != NULL; }

void log_debug (const char *fmt, ...) {
  va_list ap;
  time_t now;
  struct tm tmv;
  char tbuf[20];

  if (log__debug_file == NULL) return;

  now = time (NULL);
#if LOG_HAVE_ISATTY /* POSIX: localtime_r is reentrant */
  localtime_r (&now, &tmv);
#else
  tmv = *localtime (&now);
#endif
  if (strftime (tbuf, sizeof tbuf, "%Y-%m-%d %H:%M:%S", &tmv) > 0)
    fprintf (log__debug_file, "%s ", tbuf);

  va_start (ap, fmt);
  vfprintf (log__debug_file, fmt, ap);
  va_end (ap);
  fputc ('\n', log__debug_file);
  fflush (log__debug_file);
}

void log_set_diag_sink (log_diag_fn fn, void *data) {
  log__diag_fn = fn;
  log__diag_data = data;
}

int log_diag_active (void) { return log__diag_fn != NULL; }

void log_emit_diag (const log_diag_t *d) {
  if (log__diag_fn != NULL) log__diag_fn (log__diag_data, d);
}

void log_print_diag_prefix (FILE *f, const char *file, int line, int col, const char *label,
                            const char *label_color) {
  int color = log_color_enabled (f);

  fputs (log_c (color, LOG_BOLD), f);
  if (line >= 0 && file != NULL) fprintf (f, "%s:%d:%d: ", file, line, col);
  fprintf (f, "%s%s%s -- ", log_c (color, label_color), label, log_c (color, LOG_RESET));
}
#endif /* LOGGER_IMPL */

#endif /* CLASSYC_LOGGER_H */
