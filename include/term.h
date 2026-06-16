/* =========================================================================
   include/term.h — terminal styling for ClassyC programs
   =========================================================================

   Auto-detects whether stdout is a real terminal.  When piped to a file or
   another program, all style functions return "" so output stays clean.

   ── Usage ──────────────────────────────────────────────────────────────

   #include "include/term.h"

   printf("%sHello%s world\n", term_bold(), term_reset());
   printf("%sOK%s\n", term_green(), term_reset());

   // f-string style (idiomatic ClassyC)
   printf("%s\n", f"{term_bold()}build{term_reset()} succeeded");

   // Semantic helpers for common patterns
   term_print_ok("all 42 tests passed");
   term_print_err("file not found: foo.c");
   term_print_warn("deprecated flag ignored");
   term_print_info("watching for changes...");

   // Box drawing
   term_box("jitrunner v0.1");

   // Horizontal rule
   term_hr();

   // Enable/disable manually (overrides auto-detection)
   term_force_color(1);   // always emit escapes
   term_force_color(0);   // never emit escapes

   ── Design ─────────────────────────────────────────────────────────────

   Every function returns a `char *` that is either an ANSI escape sequence
   or "" depending on whether colour is active.  The strings are static
   constants — no allocation, no cleanup needed.

   The header is intentionally minimal: no macros that emit code, no
   va_list wrappers, no format-string rewriting.  It composes naturally
   with printf, f-strings, and String concatenation.
   ========================================================================= */

#ifndef TERM_H
#define TERM_H

#include <stdio.h>
#include <string.h>

/* ── isatty detection ──────────────────────────────────────────────── */

extern int isatty(int fd);

static int _term_color_mode = -1;  /* -1 = auto, 0 = off, 1 = on */

static int term_has_color(void) {
    if (_term_color_mode >= 0) return _term_color_mode;
    return isatty(1);  /* fd 1 = stdout */
}

static void term_force_color(int on) {
    _term_color_mode = on ? 1 : 0;
}

/* ── Attributes ────────────────────────────────────────────────────── */

static char *term_reset(void)     { return term_has_color() ? "\033[0m"  : ""; }
static char *term_bold(void)      { return term_has_color() ? "\033[1m"  : ""; }
static char *term_dim(void)       { return term_has_color() ? "\033[2m"  : ""; }
static char *term_italic(void)    { return term_has_color() ? "\033[3m"  : ""; }
static char *term_underline(void) { return term_has_color() ? "\033[4m"  : ""; }

/* ── Foreground colours ────────────────────────────────────────────── */

static char *term_black(void)   { return term_has_color() ? "\033[30m" : ""; }
static char *term_red(void)     { return term_has_color() ? "\033[31m" : ""; }
static char *term_green(void)   { return term_has_color() ? "\033[32m" : ""; }
static char *term_yellow(void)  { return term_has_color() ? "\033[33m" : ""; }
static char *term_blue(void)    { return term_has_color() ? "\033[34m" : ""; }
static char *term_magenta(void) { return term_has_color() ? "\033[35m" : ""; }
static char *term_cyan(void)    { return term_has_color() ? "\033[36m" : ""; }
static char *term_white(void)   { return term_has_color() ? "\033[37m" : ""; }

/* ── Bright foreground colours ─────────────────────────────────────── */

static char *term_bright_red(void)     { return term_has_color() ? "\033[91m" : ""; }
static char *term_bright_green(void)   { return term_has_color() ? "\033[92m" : ""; }
static char *term_bright_yellow(void)  { return term_has_color() ? "\033[93m" : ""; }
static char *term_bright_blue(void)    { return term_has_color() ? "\033[94m" : ""; }
static char *term_bright_magenta(void) { return term_has_color() ? "\033[95m" : ""; }
static char *term_bright_cyan(void)    { return term_has_color() ? "\033[96m" : ""; }

/* ── Bold + colour combos (common patterns) ────────────────────────── */

static char *term_bold_red(void)    { return term_has_color() ? "\033[1;31m" : ""; }
static char *term_bold_green(void)  { return term_has_color() ? "\033[1;32m" : ""; }
static char *term_bold_yellow(void) { return term_has_color() ? "\033[1;33m" : ""; }
static char *term_bold_blue(void)   { return term_has_color() ? "\033[1;34m" : ""; }
static char *term_bold_cyan(void)   { return term_has_color() ? "\033[1;36m" : ""; }

/* ══════════════════════════════════════════════════════════════════════
   Semantic print helpers — prefixed, coloured one-liners
   ══════════════════════════════════════════════════════════════════════ */

static void term_print_ok(char *msg) {
    printf("%s  ✓ %s%s\n", term_bold_green(), msg, term_reset());
}

static void term_print_err(char *msg) {
    printf("%s  ✗ %s%s\n", term_bold_red(), msg, term_reset());
}

static void term_print_warn(char *msg) {
    printf("%s  ⚠ %s%s\n", term_bold_yellow(), msg, term_reset());
}

static void term_print_info(char *msg) {
    printf("%s  ℹ %s%s\n", term_bold_cyan(), msg, term_reset());
}

/* ══════════════════════════════════════════════════════════════════════
   Box drawing — prints a single-line title in a Unicode box
   ══════════════════════════════════════════════════════════════════════ */

static void term_box(char *title) {
    int len = (int)strlen(title);
    int pad = 3;
    int width = len + pad * 2;

    printf("%s", term_bold_cyan());

    /* top border */
    printf("╔");
    for (int i = 0; i < width; i++) printf("═");
    printf("╗\n");

    /* title line */
    printf("║");
    for (int i = 0; i < pad; i++) printf(" ");
    printf("%s", title);
    for (int i = 0; i < pad; i++) printf(" ");
    printf("║\n");

    /* bottom border */
    printf("╚");
    for (int i = 0; i < width; i++) printf("═");
    printf("╝\n");

    printf("%s", term_reset());
}

/* ── Horizontal rule ───────────────────────────────────────────────── */

static void term_hr(void) {
    printf("%s", term_dim());
    for (int i = 0; i < 44; i++) printf("─");
    printf("%s\n", term_reset());
}

/* ── Labelled horizontal rule (e.g. "── run #3 ──────────") ────────── */

static void term_hr_label(char *label) {
    printf("%s── %s ", term_bold_yellow(), label);
    int used = 4 + (int)strlen(label);
    for (int i = used; i < 44; i++) printf("─");
    printf("%s\n", term_reset());
}

#endif /* TERM_H */
