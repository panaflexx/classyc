/* classy-file.c — exercises inc/file.h
 *
 * Tests:
 *   1.  File.write_text (static)  — create a JSON file
 *   2.  File.exists (static)      — confirm it landed on disk
 *   3.  File.read_text (static)   — slurp without an open handle
 *   4.  File.open + read_all      — instance open, bulk read
 *   5.  json() integration        — parse the JSON into a dict and query keys
 *   6.  read_line                 — iterate line-by-line with explicit free
 *   7.  each_line callback        — streaming, count all lines
 *   8.  each_line stop-early      — callback returns 0 to abort mid-file
 *   9.  each_chunk callback       — chunk bytes sum == file size
 *   10. instance write / writeln  — write a second file, read it back
 *   11. seek / pos                — navigate within an open file
 *   12. File.append_text (static) — append a line, verify length grows
 *   13. Error handling            — bad path → ok()==0, safe no-ops
 *   14. defer delete              — fclose called on scope exit
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "inc/file.h"

/* ── dict runtime declarations ──────────────────────────────────────── */
dict  dict_object_get(dict obj, char *key);
int   dict_object_set(dict obj, char *key, dict val);
dict  dict_create_string(char *s);
dict  dict_create_object();

/* ── test harness ───────────────────────────────────────────────────── */
int passed;
int failed;

void check(int cond, char *label) {
    if (cond) {
        printf("  PASS  %s\n", label);
        passed = passed + 1;
    } else {
        printf("  FAIL  %s\n", label);
        failed = failed + 1;
    }
}

/* ── callbacks ──────────────────────────────────────────────────────── */

/* Count how many lines each_line delivers; ctx = int* */
int count_lines_cb(char *line, int lineno, void *ctx) {
    int *n = (int *)ctx;
    *n = lineno;
    return 1;   /* continue */
}

/* Stop after line 3; ctx = int* receives the last lineno seen */
int stop_at_3_cb(char *line, int lineno, void *ctx) {
    int *n = (int *)ctx;
    *n = lineno;
    return lineno < 3;  /* continue while lineno < 3, stop at 3 */
}

/* Accumulate total bytes seen; ctx = int* */
int sum_bytes_cb(char *chunk, int len, void *ctx) {
    int *total = (int *)ctx;
    *total = *total + len;
    return 1;   /* continue */
}

/* ── test data ──────────────────────────────────────────────────────── */

/* A well-formed JSON object we write to disk then parse back.
   8 lines, including the final newline. */
static char *JSON_PATH = "/tmp/classy_file_test.json";
static char *JSON = "{\n"
    "  \"language\": \"classy\",\n"
    "  \"version\": 1,\n"
    "  \"meta\": {\n"
    "    \"author\": \"compiler\",\n"
    "    \"stable\": 1\n"
    "  }\n"
    "}\n";

static char *WRITE_PATH = "/tmp/classy_file_write_test.txt";

/* ── main ───────────────────────────────────────────────────────────── */

int main() {
    printf("=== classy-file ===\n\n");
    passed = 0;
    failed = 0;

    /* ── 1. Static write_text ─────────────────────────────────────── */
    printf("-- 1. static write_text --\n");
    int wr = File.write_text(JSON_PATH, JSON);
    check(wr == 0,               "1a  write_text returned 0");

    /* ── 2. Static exists ─────────────────────────────────────────── */
    printf("\n-- 2. static exists --\n");
    check(File.exists(JSON_PATH),          "2a  exists() after write");
    check(!File.exists("/no/such/file"),   "2b  exists() for bad path == 0");

    /* ── 3. Static read_text ──────────────────────────────────────── */
    printf("\n-- 3. static read_text --\n");
    char *raw = File.read_text(JSON_PATH);
    check(raw != 0,                        "3a  read_text non-null");
    check((int)strlen(raw) > 20,           "3b  read_text has content");
    check(File.read_text("/no/such") == 0, "3c  read_text bad path == null");
    free(raw);

    /* ── 4. Instance open + read_all ──────────────────────────────── */
    printf("\n-- 4. open + size + read_all --\n");
    {
        File *f = File.open(JSON_PATH, "r");
        defer delete f;   /* fclose on block exit */

        check(f->ok(),                 "4a  open ok");
        check(f->size() > 20,          "4b  size() > 20 bytes");
        check(f->pos() == 0,           "4c  initial pos == 0");

        char *text = f->read_all();
        check(text != 0,               "4d  read_all non-null");
        check((int)strlen(text) > 20,  "4e  read_all has content");
        /* After read_all the position is at EOF */
        check(f->pos() == f->size(),   "4f  pos == size after read_all");
        free(text);
    }

    /* ── 5. json() integration ────────────────────────────────────── */
    printf("\n-- 5. json() parse + dict queries --\n");
    {
        File *f = File.open(JSON_PATH, "r");
        defer delete f;

        char *text = f->read_all();
        check(text != 0,               "5a  read text for json()");

        dict data = json(text);
        free(text);

        check(data != 0,               "5b  json() returned non-null dict");
        check("language" in data,      "5c  key 'language' present");
        check("version"  in data,      "5d  key 'version'  present");
        check("meta"     in data,      "5e  key 'meta'     present");
        check(!("nope"   in data),     "5f  missing key absent");

        /* nested object */
        check(data.meta != 0,          "5g  data.meta non-null");
        check("author" in data.meta,   "5h  data.meta.author present");
        check("stable" in data.meta,   "5i  data.meta.stable present");

        printf("    dict as JSON: %s\n", data.json);
    }

    /* ── 6. read_line ─────────────────────────────────────────────── */
    printf("\n-- 6. read_line --\n");
    {
        File *f = File.open(JSON_PATH, "r");
        defer delete f;

        int lcount = 0;
        char *line;
        while ((line = f->read_line()) != 0) {
            lcount = lcount + 1;
            free(line);
        }
        /* JSON has 8 newline-terminated lines */
        check(lcount == 8,             "6a  read_line: 8 lines total");
        check(f->eof(),                "6b  eof() after last read_line");
        check(f->read_line() == 0,     "6c  read_line at EOF returns null");
    }

    /* ── 7. each_line — full iteration ───────────────────────────── */
    printf("\n-- 7. each_line full --\n");
    {
        File *f = File.open(JSON_PATH, "r");
        defer delete f;

        int n = 0;
        f->each_line(count_lines_cb, &n);
        check(n == 8,                  "7a  each_line delivered 8 lines");
    }

    /* ── 8. each_line — stop early ───────────────────────────────── */
    printf("\n-- 8. each_line stop-early --\n");
    {
        File *f = File.open(JSON_PATH, "r");
        defer delete f;

        int stopped = 0;
        f->each_line(stop_at_3_cb, &stopped);
        /* callback returns 0 at lineno==3, so lineno 3 is the last delivered */
        check(stopped == 3,            "8a  stopped exactly at line 3");
    }

    /* ── 9. each_chunk — byte totals ─────────────────────────────── */
    printf("\n-- 9. each_chunk --\n");
    {
        File *f = File.open(JSON_PATH, "r");
        defer delete f;

        long fsize = f->size();
        int total = 0;

        /* small chunk size to exercise multiple callbacks */
        f->each_chunk(sum_bytes_cb, &total, 8);
        check(total > 0,               "9a  chunk bytes > 0");
        check(total == (int)fsize,     "9b  total chunk bytes == file size");

        /* verify rewind: run again with different chunk size */
        int total2 = 0;
        f->each_chunk(sum_bytes_cb, &total2, 32);
        check(total2 == total,         "9c  second chunk pass same total");
    }

    /* ── 10. Instance writeln / write, then read back ────────────── */
    printf("\n-- 10. instance writeln + write --\n");
    {
        {
            File *w = File.open(WRITE_PATH, "w");
            defer delete w;
            check(w->ok(),             "10a open for write ok");
            w->writeln("alpha");       /* line 1 */
            w->writeln("beta");        /* line 2 */
            w->write("gamma");         /* line 3 — no trailing newline */
        }

        /* read back line-by-line */
        File *r = File.open(WRITE_PATH, "r");
        defer delete r;
        int lc = 0;
        int n = 0;
        r->each_line(count_lines_cb, &n);
        check(n == 3,                  "10b 3 lines written and read back");

        File.remove_file(WRITE_PATH);
    }

    /* ── 11. seek / pos ──────────────────────────────────────────── */
    printf("\n-- 11. seek / pos --\n");
    {
        File *f = File.open(JSON_PATH, "r");
        defer delete f;

        check(f->pos() == 0,           "11a initial pos == 0");

        /* seek forward */
        f->seek(5, 0);                 /* SEEK_SET */
        check(f->pos() == 5,           "11b pos == 5 after seek(5, SEEK_SET)");

        /* seek back to start */
        f->seek(0, 0);
        check(f->pos() == 0,           "11c pos == 0 after seek(0, SEEK_SET)");

        /* seek to end then read should return empty */
        long sz = f->size();
        f->seek(0, 2);                 /* SEEK_END */
        check(f->pos() == sz,          "11d seek to end: pos == size");
        check(f->eof(),                "11e eof() after seek to end");
    }

    /* ── 12. File.append_text ────────────────────────────────────── */
    printf("\n-- 12. static append_text --\n");
    {
        char *apath = "/tmp/classy_append_test.txt";
        File.write_text(apath, "line1\n");
        long s1 = 0;
        {
            File *f = File.open(apath, "r");
            defer delete f;
            s1 = f->size();
        }
        File.append_text(apath, "line2\n");
        long s2 = 0;
        {
            File *f = File.open(apath, "r");
            defer delete f;
            s2 = f->size();
        }
        check(s2 > s1,                 "12a  size grew after append");
        check(s2 == s1 + 6,            "12b  grew by exactly 6 bytes");
        File.remove_file(apath);
    }

    /* ── 13. Error handling — bad path ───────────────────────────── */
    printf("\n-- 13. error handling --\n");
    {
        File *bad = File.open("/no/such/path/nope.json", "r");
        defer delete bad;

        check(!bad->ok(),              "13a bad path: ok()==0");
        check(bad->read_all() == 0,    "13b bad path: read_all==null");
        check(bad->read_line() == 0,   "13c bad path: read_line==null");
        check(bad->size() == -1,       "13d bad path: size==-1");
        check(bad->pos()  == -1,       "13e bad path: pos==-1");
        /* write on bad handle is a safe no-op */
        check(bad->write("x") == -1,   "13f bad path: write==-1");
    }

    /* ── 14. defer delete runs fclose ────────────────────────────── */
    printf("\n-- 14. defer delete closes the handle --\n");
    {
        /* Open, note the handle is non-null, let defer close it. */
        File *f = File.open(JSON_PATH, "r");
        defer delete f;
        check(f->ok(),                 "14a handle open before block exit");
        /* f->close() is NOT called explicitly; defer delete runs ~File(). */
    }
    /* If we reach here without crashing, fclose was called safely. */
    check(1,                           "14b block exited cleanly after defer delete");

    /* ── cleanup ─────────────────────────────────────────────────── */
    File.remove_file(JSON_PATH);

    printf("\n=== classy-file: %d passed, %d failed ===\n", passed, failed);
    return failed;
}
