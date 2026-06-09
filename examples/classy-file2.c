/* classy-file2.c — inc/file.h in everyday use
 *
 * A small self-contained tool that:
 *   1. Writes a text file with File.write_text (static one-liner)
 *   2. Streams it with each_line to count lines, words and chars
 *   3. Searches for a keyword with an early-exit each_line callback
 *   4. Serialises the analysis to a JSON file, then loads it back
 *   5. Appends a footer and re-reads the whole file with read_all
 *
 * No test harness — just a clean walkthrough of the API.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "inc/file.h"

/* no dict helpers needed — bracket writes handle everything */

/* ── the text we'll analyse ────────────────────────────────────────── */
static char *NOTES_PATH  = "/tmp/notes.txt";
static char *REPORT_PATH = "/tmp/notes_report.json";

static char *NOTES =
    "The compiler spoke:\n"
    "  each class a gate,\n"
    "  each method a path —\n"
    "  defer closes them cleanly.\n"
    "Files open and shut\n"
    "  like sentences. The dict\n"
    "  holds everything we know.\n";

/* ── streaming context structs ─────────────────────────────────────── */

struct Stats {
    int lines;
    int words;
    int chars;
};

struct Search {
    char *needle;
    int   found_lineno;
    char  found_line[256];
};

/* Count lines / words / chars.  Always continues. */
int stats_cb(char *line, int lineno, void *ctx) {
    struct Stats *s = (struct Stats *)ctx;
    s->lines = lineno;
    s->chars = s->chars + (int)strlen(line);

    /* naive word count: space/tab → non-space transitions */
    int in_word = 0;
    char *p = line;
    while (*p) {
        if (*p == ' ' || *p == '\t') {
            in_word = 0;
        } else if (!in_word) {
            s->words = s->words + 1;
            in_word = 1;
        }
        p = p + 1;
    }
    return 1;
}

/* Find the first line containing needle.  Returns 0 to stop. */
int search_cb(char *line, int lineno, void *ctx) {
    struct Search *s = (struct Search *)ctx;
    if (strstr(line, s->needle)) {
        s->found_lineno = lineno;
        int n = (int)strlen(line);
        if (n > 255) n = 255;
        int i;
        for (i = 0; i < n; i++) s->found_line[i] = line[i];
        s->found_line[n] = '\0';
        return 0;   /* stop — found it */
    }
    return 1;
}

/* ── main ───────────────────────────────────────────────────────────── */

int main() {
    printf("=== classy-file showcase ===\n\n");

    /* ── 1. Write the notes file (static, no handle needed) ─────── */
    File.write_text(NOTES_PATH, NOTES);
    printf("Wrote  %s\n", NOTES_PATH);

    /* ── 2. Size and line/word/char stats ───────────────────────── */
    {
        File *f = File.open(NOTES_PATH, "r");
        defer delete f;     /* handle closes automatically here */

        printf("Size:  %ld bytes\n\n", f->size());

        struct Stats stats;
        stats.lines = 0;
        stats.words = 0;
        stats.chars = 0;
        f->each_line(stats_cb, &stats);

        printf("Analysis:\n");
        printf("  lines : %d\n", stats.lines);
        printf("  words : %d\n", stats.words);
        printf("  chars : %d\n\n", stats.chars);

        /* ── 3. Search for a keyword (stops on first match) ──────── */
        struct Search srch;
        srch.needle = "defer";
        srch.found_lineno = 0;
        srch.found_line[0] = '\0';
        f->each_line(search_cb, &srch);

        if (srch.found_lineno > 0) {
            printf("First occurrence of \"%s\":\n", srch.needle);
            printf("  line %d: \"%s\"\n\n", srch.found_lineno, srch.found_line);
        } else {
            printf("  \"%s\" not found.\n\n", srch.needle);
        }

        /* ── 4. Save the report as JSON ──────────────────────────── */
        dict report = {};
        report["lines"]      = stats.lines;
        report["words"]      = stats.words;
        report["chars"]      = stats.chars;
        report["keyword"]    = srch.needle;
        report["match_line"] = srch.found_lineno;

        File.write_text(REPORT_PATH, report.json);
        printf("Saved report → %s\n", REPORT_PATH);
    }   /* f closes here via defer delete */

    /* ── 5. Load the JSON report back and print it ─────────────── */
    {
        char *raw = File.read_text(REPORT_PATH);    /* static one-liner */
        dict  rep  = json(raw);
        free(raw);

        printf("\nLoaded report (JSON round-trip):\n");
        printf("  %s\n", rep.json);

        /* iterate keys to show for-in over a parsed dict */
        printf("\n  keys present:");
        for (auto k in rep)
            printf(" %s", k);
        printf("\n");
    }

    /* ── 6. Append a footer and re-read the whole file ───────────── */
    File.append_text(NOTES_PATH, "---\n(end of notes)\n");

    {
        File *f = File.open(NOTES_PATH, "r");
        defer delete f;

        char *full = f->read_all();
        printf("\nFull file after append (%ld bytes):\n", f->size());
        printf("─────────────────────────\n%s", full);
        printf("─────────────────────────\n");
        free(full);
    }

    /* ── housekeeping ────────────────────────────────────────────── */
    File.remove_file(NOTES_PATH);
    File.remove_file(REPORT_PATH);

    printf("\nDone.\n");
    return 0;
}
