/* classy-docsearch.cy — real manpage / Markdown / HTML search TUI
 *
 * A practical document search engine built on the MapReduce inverted-index
 * idea from classy-search-engine.cy, written in the aurora/neon house style:
 *
 *   Domain   · Doc* owning stack List  · Hit by-value List (POD rank DTO)
 *            · Term* inverted map ownsValues · enums + nameof
 *   Memory   · stack List/Map RAII · value-returning Take/Copy/Where
 *            · List<Doc*>.owns() · Map<String,Term*>.ownsValues()
 *            · unowned for malloc/popen buffers (C heap, not `new`)
 *   LINQ     · Where / Take / Sort / First  on ranked hits
 *   Map      · stack Map for O(1) term → Term*  · title-path scan fallback
 *   String   · equals / contains / starts_with · f-strings
 *   I/O      · zcat for .gz  · man -l to open pages  · strip HTML / light MD
 *   Persist  · SQLite inverted index  · --reindex rebuild  · instant reopen
 *   TUI      · raw termios  · type-to-search  · Tab into list  · Enter open
 *
 * What it indexes by default (Google mode — unlimited):
 *   /usr/share/man/**          all sections (man1…man8, man3type, …)
 *   /usr/share/doc/**          README*, *.md, *.html, text
 *   /usr/local/share/{man,doc} if present
 *   DOCSEARCH_MAX=0 (default) indexes every candidate; set a number to cap
 *
 * SQLite cache (default ~/.cache/classy-docsearch/index.db):
 *   tables  meta / docs / postings(term,doc_id,tf)
 *   first run builds + saves; later runs load unless --reindex
 *
 * Key handling:
 *   1. .gz man pages  — `zcat` (via popen) for index text; `man -l` for viewing
 *   2. Markdown       — light markup strip for index; pandoc|less when available
 *   3. HTML           — pure-C tag strip for index (legible); pandoc -t plain
 *                       for view when present, else stripped text in less
 *
 * Build / run (needs libsqlite3):
 *   ./bin/classyc -I include -l sqlite3 examples/classy-docsearch.cy -eg
 *   ./bin/classyc -I include -l sqlite3 examples/classy-docsearch.cy -eg -- --reindex
 *   DOCSEARCH_DB=/tmp/idx.db DOCSEARCH_MAX=500 ... -eg
 *   DOCSEARCH_BATCH=1        non-interactive sample queries + exit
 *
 * Keys in the TUI:
 *   type          search as you type (query focus)
 *   Tab / ↓       focus result list
 *   ↑ ↓  j k      move selection (list focus)
 *   Enter         open selected manpage / document in pager
 *   Esc           return to query  (or clear query if already typing)
 *   Ctrl-U        clear query
 *   q             quit (list focus only — so you can type "qemu")
 *   Ctrl-C/Q      quit always (restores terminal)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <termios.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include "list.h"
#include "map.h"
#include "term.h"
#include "file.h"
#include "sqlite.h"

/* ───────────────────────── config / limits ───────────────────────── */

#define MAX_PATH      1024
#define MAX_TITLE      160
#define MAX_INDEX_BODY (48 * 1024)    /* bytes of plain text kept for tokenising */
#define MAX_VIEW_BODY  (512 * 1024)   /* for open/preview via strip+less */
#define MAX_QUERY      200
#define MAX_SHOW       40             /* result rows in TUI */
/* 0 = unlimited (Google mode — index everything found).  Override with DOCSEARCH_MAX. */
#define DEFAULT_MAX_FILES 0

/* ───────────────────────── domain ───────────────────────── */

enum DocKind { kind_man = 0, kind_md = 1, kind_html = 2, kind_text = 3 };
enum Focus  { focus_query = 0, focus_list = 1 };

/* Corpus entry.  Body is the *normalised plain text* used for indexing
 * (truncated); path is absolute so we can reopen the original for viewing. */
class Doc {
    int     id;
    String  title;     /* e.g. "ls(1)" or "flatpak/README.md" */
    String  path;
    String  section;   /* "man1", "doc", … */
    DocKind kind;
    String  body;      /* index text (may be empty if only title is useful) */

    Doc(int id, String title, String path, String section, DocKind kind, String body) {
        this.id = id;
        this.title = title;
        this.path = path;
        this.section = section;
        this.kind = kind;
        this.body = body;
    }

    ~Doc() {}

    String KindLabel() {
        DocKind k = kind;
        return (String)k.nameof();
    }
};

/* MAP/REDUCE leaf: inverted-index entry.  docIds stores one entry per
 * occurrence so frequency = count of a given id in the list. */
class Term {
    String     word;
    List<int>* docIds;

    Term(String word) {
        this.word = word;
        this.docIds = new List<int>();
    }
    ~Term() { delete this.docIds; }
};

/* Ranked hit — by-value DTO for List<Hit> (no user dtor). */
class Hit {
    int docId;
    int score;

    Hit(int docId, int score) {
        this.docId = docId;
        this.score = score;
    }
};

int ByScoreDesc(Hit a, Hit b) { return b.score - a.score; }

/* ───────────────────────── globals (lambdas can't close over locals) ───────────────────────── */

int g_max_files = DEFAULT_MAX_FILES;
int g_files_seen = 0;
int g_files_indexed = 0;
long g_bytes_indexed = 0;
int g_batch = 0;                 /* 1 = non-interactive smoke */

struct termios g_orig_term;
int g_raw = 0;

/* ───────────────────────── terminal raw mode ───────────────────────── */

void term_leave_raw(void) {
    if (g_raw) {
        tcsetattr(0, TCSAFLUSH, &g_orig_term);
        g_raw = 0;
        /* show cursor */
        fputs("\033[?25h", stdout);
        fflush(stdout);
    }
}

void term_enter_raw(void) {
    if (g_raw) return;
    if (!isatty(0)) return;
    if (tcgetattr(0, &g_orig_term) != 0) return;
    struct termios raw = g_orig_term;
    raw.c_lflag &= ~(unsigned)(ICANON | ECHO | IEXTEN);
    raw.c_iflag &= ~(unsigned)(IXON | ICRNL);
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 1;   /* 100 ms poll */
    if (tcsetattr(0, TCSAFLUSH, &raw) == 0) {
        g_raw = 1;
        fputs("\033[?25l", stdout);   /* hide cursor — we draw our own */
        fflush(stdout);
    }
}

void on_signal(int sig) {
    term_leave_raw();
    fputs("\n", stdout);
    _exit(128 + sig);
}

int term_cols(void) {
    struct winsize ws;
    if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_col >= 40) return (int)ws.ws_col;
    return 80;
}

int term_rows(void) {
    struct winsize ws;
    if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_row >= 12) return (int)ws.ws_row;
    return 24;
}

/* ───────────────────────── path / kind helpers ───────────────────────── */

int ends_with_ci(const char* s, const char* suf) {
    if (!s || !suf) return 0;
    int n = (int)strlen(s);
    int m = (int)strlen(suf);
    if (m > n) return 0;
    for (int i = 0; i < m; i++) {
        unsigned char a = (unsigned char)s[n - m + i];
        unsigned char b = (unsigned char)suf[i];
        if (tolower(a) != tolower(b)) return 0;
    }
    return 1;
}

int is_gz_path(const char* path) { return ends_with_ci(path, ".gz"); }

/* Reject paths with shell metacharacters — we shell out for zcat / man / less. */
int path_safe(const char* p) {
    if (!p || !p[0]) return 0;
    for (const char* s = p; *s; s++) {
        char c = *s;
        if (c == '\'' || c == '"' || c == '`' || c == '$' || c == ';' ||
            c == '|' || c == '&' || c == '\n' || c == '\r' || c == '\\')
            return 0;
    }
    return 1;
}

/* basename pointer into path (not allocated). */
const char* path_base(const char* path) {
    const char* s = path ? strrchr(path, '/') : NULL;
    return s ? s + 1 : (path ? path : "");
}

DocKind classify_name(const char* name) {
    if (!name) return kind_text;
    if (ends_with_ci(name, ".html") || ends_with_ci(name, ".htm") ||
        ends_with_ci(name, ".html.gz") || ends_with_ci(name, ".htm.gz"))
        return kind_html;
    if (ends_with_ci(name, ".md") || ends_with_ci(name, ".md.gz") ||
        ends_with_ci(name, ".markdown") || ends_with_ci(name, ".markdown.gz"))
        return kind_md;
    /* man: foo.1  foo.1.gz  foo.3pm.gz  foo.1posix.gz */
    int n = (int)strlen(name);
    const char* base = name;
    int bn = n;
    if (ends_with_ci(name, ".gz") && n > 3) { bn = n - 3; }
    /* scan for .[1-9] near the end */
    for (int i = bn - 1; i >= 0; i--) {
        if (base[i] == '.') {
            if (i + 1 < bn) {
                char sec = base[i + 1];
                if (sec >= '1' && sec <= '9') return kind_man;
            }
            break;
        }
    }
    if (strncmp(name, "README", 6) == 0 || strncmp(name, "readme", 6) == 0 ||
        strncmp(name, "CHANGELOG", 9) == 0 || strncmp(name, "NEWS", 4) == 0 ||
        ends_with_ci(name, ".txt") || ends_with_ci(name, ".text"))
        return kind_text;
    return kind_text;
}

int looks_indexable(const char* name) {
    if (!name || !name[0] || name[0] == '.') return 0;
    /* Binary / archive clutter only — do NOT skip toolchain man pages or
     * man3 (strncmp lives there).  Google mode means everything text-ish. */
    if (ends_with_ci(name, ".png") || ends_with_ci(name, ".jpg") ||
        ends_with_ci(name, ".jpeg") || ends_with_ci(name, ".gif") ||
        ends_with_ci(name, ".svg") || ends_with_ci(name, ".pdf") ||
        ends_with_ci(name, ".so") || ends_with_ci(name, ".a") ||
        ends_with_ci(name, ".o") || ends_with_ci(name, ".pyc") ||
        ends_with_ci(name, ".tar.gz") || ends_with_ci(name, ".deb") ||
        ends_with_ci(name, ".woff") || ends_with_ci(name, ".woff2") ||
        ends_with_ci(name, ".ttf") || ends_with_ci(name, ".ico") ||
        ends_with_ci(name, ".zip") || ends_with_ci(name, ".xz") ||
        ends_with_ci(name, ".bz2") || ends_with_ci(name, ".zst"))
        return 0;
    DocKind k = classify_name(name);
    if (k == kind_man || k == kind_md || k == kind_html) return 1;
    if (strncmp(name, "README", 6) == 0 || strncmp(name, "readme", 6) == 0)
        return 1;
    if (strncmp(name, "CHANGELOG", 9) == 0 || strncmp(name, "changelog", 9) == 0 ||
        strncmp(name, "NEWS", 4) == 0 || strncmp(name, "TODO", 4) == 0 ||
        strncmp(name, "AUTHORS", 7) == 0 || strncmp(name, "COPYING", 7) == 0 ||
        strncmp(name, "LICENSE", 7) == 0 || strncmp(name, "HACKING", 7) == 0)
        return 1;
    if (ends_with_ci(name, ".txt") || ends_with_ci(name, ".text") ||
        ends_with_ci(name, ".txt.gz") || ends_with_ci(name, ".text.gz"))
        return 1;
    /* compressed plain text docs: allow README*.gz / name.1.gz already handled */
    if (ends_with_ci(name, ".gz")) {
        /* man-like .N.gz already kind_man; leftover .gz of unknown type: skip */
        if (k == kind_text) {
            /* still allow README*.gz etc via prefix checks above */
            return 0;
        }
    }
    return 0;
}

/* ───────────────────────── loaders (gz / plain) ───────────────────────── */

/* Read up to maxb bytes from popen cmd into a malloc'd NUL-term buffer. */
char* read_popen_capped(const char* cmd, int maxb) {
    FILE* f = popen(cmd, "r");
    if (!f) return NULL;
    char* buf = (char*)malloc((size_t)maxb + 1);
    if (!buf) { pclose(f); return NULL; }
    size_t n = fread(buf, 1, (size_t)maxb, f);
    pclose(f);
    if (n == 0) { free(buf); return NULL; }
    buf[n] = 0;
    return buf;
}

char* read_file_capped(const char* path, int maxb) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    char* buf = (char*)malloc((size_t)maxb + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t n = fread(buf, 1, (size_t)maxb, f);
    fclose(f);
    if (n == 0) { free(buf); return NULL; }
    /* reject obvious binary (NUL in first 256) */
    int check = n < 256 ? (int)n : 256;
    for (int i = 0; i < check; i++) {
        if (buf[i] == 0) { free(buf); return NULL; }
    }
    buf[n] = 0;
    return buf;
}

/* Load raw bytes — decompress .gz via zcat when needed. */
char* load_raw(const char* path, int maxb) {
    if (!path_safe(path)) return NULL;
    if (is_gz_path(path)) {
        char cmd[MAX_PATH + 64];
        snprintf(cmd, sizeof(cmd), "zcat -- '%s' 2>/dev/null", path);
        return read_popen_capped(cmd, maxb);
    }
    return read_file_capped(path, maxb);
}

/* ───────────────────────── normalisers (man / md / html → plain) ───────────────────────── */

/* Decode a few common HTML entities in-place-ish into out. */
int emit_entity(const char* p, char* out, int* oi, int out_max) {
    if (strncmp(p, "&amp;", 5) == 0)  { if (*oi < out_max) out[(*oi)++] = '&';  return 5; }
    if (strncmp(p, "&lt;", 4) == 0)   { if (*oi < out_max) out[(*oi)++] = '<';  return 4; }
    if (strncmp(p, "&gt;", 4) == 0)   { if (*oi < out_max) out[(*oi)++] = '>';  return 4; }
    if (strncmp(p, "&quot;", 6) == 0) { if (*oi < out_max) out[(*oi)++] = '"';  return 6; }
    if (strncmp(p, "&nbsp;", 6) == 0) { if (*oi < out_max) out[(*oi)++] = ' ';  return 6; }
    if (strncmp(p, "&#39;", 5) == 0)  { if (*oi < out_max) out[(*oi)++] = '\''; return 5; }
    return 0;
}

/* Pure-C HTML strip — good enough to make package HTML legible + indexable.
 * (Also: `pandoc -t plain`, `w3m -dump`, `lynx -dump`, `html2text` if installed.) */
char* strip_html(const char* in, int out_max) {
    if (!in) return NULL;
    char* out = (char*)malloc((size_t)out_max + 1);
    if (!out) return NULL;
    int oi = 0;
    int i = 0;
    int in_tag = 0;
    int in_script = 0;
    while (in[i] && oi < out_max) {
        if (!in_tag && in[i] == '<') {
            /* script / style skip */
            if (strncmp(in + i, "<script", 7) == 0 || strncmp(in + i, "<SCRIPT", 7) == 0)
                in_script = 1;
            if (strncmp(in + i, "</script", 8) == 0 || strncmp(in + i, "</SCRIPT", 8) == 0)
                in_script = 0;
            if (strncmp(in + i, "<style", 6) == 0 || strncmp(in + i, "<STYLE", 6) == 0)
                in_script = 1;
            if (strncmp(in + i, "</style", 7) == 0 || strncmp(in + i, "</STYLE", 7) == 0)
                in_script = 0;
            /* block-ish tags → newline for readability */
            if (strncmp(in + i, "<br", 3) == 0 || strncmp(in + i, "<BR", 3) == 0 ||
                strncmp(in + i, "<p", 2) == 0 || strncmp(in + i, "<P", 2) == 0 ||
                strncmp(in + i, "<div", 4) == 0 || strncmp(in + i, "<tr", 3) == 0 ||
                strncmp(in + i, "<li", 3) == 0 || strncmp(in + i, "<h", 2) == 0) {
                if (oi > 0 && out[oi - 1] != '\n') out[oi++] = '\n';
            }
            in_tag = 1;
            i++;
            continue;
        }
        if (in_tag) {
            if (in[i] == '>') in_tag = 0;
            i++;
            continue;
        }
        if (in_script) { i++; continue; }
        if (in[i] == '&') {
            int used = emit_entity(in + i, out, &oi, out_max);
            if (used > 0) { i += used; continue; }
        }
        unsigned char c = (unsigned char)in[i++];
        if (c == '\r') continue;
        if (c == '\t') c = ' ';
        /* collapse runs of spaces but keep newlines */
        if (c == ' ' && oi > 0 && out[oi - 1] == ' ') continue;
        if (c == '\n' && oi > 0 && out[oi - 1] == '\n') {
            /* allow blank lines but not torrents of them */
            if (oi > 1 && out[oi - 2] == '\n') continue;
        }
        out[oi++] = (char)c;
    }
    out[oi] = 0;
    return out;
}

/* Light Markdown → plain: drop fences/markers, keep words. */
char* strip_md(const char* in, int out_max) {
    if (!in) return NULL;
    char* out = (char*)malloc((size_t)out_max + 1);
    if (!out) return NULL;
    int oi = 0;
    int i = 0;
    int at_line = 1;
    while (in[i] && oi < out_max) {
        if (at_line) {
            /* headings / list markers */
            while (in[i] == '#' || in[i] == '>' || in[i] == ' ') i++;
            if (in[i] == '-' || in[i] == '*' || in[i] == '+') {
                if (in[i + 1] == ' ') i += 2;
            }
            at_line = 0;
        }
        char c = in[i++];
        if (c == '`') continue;                 /* inline/code fence ticks */
        if (c == '*' || c == '_') continue;     /* emphasis */
        if (c == '[') continue;
        if (c == ']') continue;
        if (c == '\r') continue;
        out[oi++] = c;
        if (c == '\n') at_line = 1;
    }
    out[oi] = 0;
    return out;
}

/* nroff/man source → rough plain text for indexing (not pretty enough to view). */
char* strip_roff(const char* in, int out_max) {
    if (!in) return NULL;
    char* out = (char*)malloc((size_t)out_max + 1);
    if (!out) return NULL;
    int oi = 0;
    int i = 0;
    int at_line = 1;
    while (in[i] && oi < out_max) {
        if (at_line && in[i] == '.') {
            /* skip macro line */
            while (in[i] && in[i] != '\n') i++;
            if (in[i] == '\n') {
                if (oi > 0 && out[oi - 1] != '\n') out[oi++] = '\n';
                i++;
            }
            at_line = 1;
            continue;
        }
        char c = in[i++];
        if (c == '\\') {
            /* \- \(xx \fX escapes — drop one char or two after ( */
            if (in[i] == '(') { i++; if (in[i]) i++; if (in[i]) i++; continue; }
            if (in[i] == 'f' || in[i] == '*') { i++; if (in[i]) i++; continue; }
            if (in[i]) { /* keep next literal-ish */
                char n = in[i++];
                if (n == '-' || n == 'e' || n == '&') {
                    if (n == '-') { if (oi < out_max) out[oi++] = '-'; }
                    continue;
                }
                if (oi < out_max) out[oi++] = n;
            }
            continue;
        }
        if (c == '\r') continue;
        if (oi < out_max) out[oi++] = c;
        at_line = (c == '\n');
    }
    out[oi] = 0;
    return out;
}

/* Dispatch normaliser by kind.  Returns malloc'd plain text (caller frees). */
char* to_plain(const char* raw, DocKind kind, int out_max) {
    if (!raw) return NULL;
    if (kind == kind_html) return strip_html(raw, out_max);
    if (kind == kind_md)   return strip_md(raw, out_max);
    if (kind == kind_man)  return strip_roff(raw, out_max);
    /* plain text — copy capped */
    int n = (int)strlen(raw);
    if (n > out_max) n = out_max;
    char* out = (char*)malloc((size_t)n + 1);
    if (!out) return NULL;
    memcpy(out, raw, (size_t)n);
    out[n] = 0;
    return out;
}

/* Pretty man title from basename: "ls.1.gz" → "ls(1)". */
String man_title_from_base(const char* base) {
    char buf[MAX_TITLE];
    int n = (int)strlen(base);
    if (ends_with_ci(base, ".gz") && n > 3) n -= 3;
    /* find last .section */
    int dot = -1;
    for (int i = n - 1; i >= 0; i--) {
        if (base[i] == '.') { dot = i; break; }
    }
    if (dot > 0 && dot + 1 < n) {
        int name_len = dot;
        if (name_len > 80) name_len = 80;
        int sec_len = n - dot - 1;
        if (sec_len > 16) sec_len = 16;
        snprintf(buf, sizeof(buf), "%.*s(%.*s)", name_len, base, sec_len, base + dot + 1);
    } else {
        snprintf(buf, sizeof(buf), "%.*s", n > 100 ? 100 : n, base);
    }
    char* s = (char*)malloc(strlen(buf) + 1);
    if (!s) return (String)"";
    strcpy(s, buf);
    return (String)s;
}

String str_dup_c(const char* p) {
    if (!p) p = "";
    char* s = (char*)malloc(strlen(p) + 1);
    if (!s) return (String)"";
    strcpy(s, p);
    return (String)s;
}

/* ───────────────────────── tokenisation / invert ───────────────────────── */

String intern_word(const char* s, int len) {
    if (len <= 0) return (String)"";
    if (len > 40) len = 40;
    char* w = (char*)malloc((size_t)len + 1);
    if (!w) return (String)"";
    for (int i = 0; i < len; i++) w[i] = (char)tolower((unsigned char)s[i]);
    w[len] = 0;
    return (String)w;
}

/* Index every alphanumeric token in plain body into inv (owns Term*). */
void map_doc_into(Doc* d, Map<String, Term*>* inv) {
    const char* p = d.body;
    if (!p) return;
    int i = 0;
    while (p[i]) {
        if (isalnum((unsigned char)p[i])) {
            int start = i;
            while (p[i] && isalnum((unsigned char)p[i])) i++;
            int len = i - start;
            if (len >= 2 && len <= 32) {
                String w = intern_word(p + start, len);
                if (w && ((const char*)w)[0]) {
                    Term* t = NULL;
                    if (inv.TryGet(w, &t) && t != NULL) {
                        t.docIds.Add(d.id);
                    } else {
                        t = new Term(w);
                        t.docIds.Add(d.id);
                        inv.Set(w, t);
                    }
                }
            }
        } else {
            i++;
        }
    }
    /* also index significant tokens from title */
    const char* t = d.title;
    if (!t) return;
    i = 0;
    while (t[i]) {
        if (isalnum((unsigned char)t[i])) {
            int start = i;
            while (t[i] && isalnum((unsigned char)t[i])) i++;
            int len = i - start;
            if (len >= 2 && len <= 32) {
                String w = intern_word(t + start, len);
                if (w && ((const char*)w)[0]) {
                    Term* term = NULL;
                    if (inv.TryGet(w, &term) && term != NULL) {
                        term.docIds.Add(d.id);
                    } else {
                        term = new Term(w);
                        term.docIds.Add(d.id);
                        inv.Set(w, term);
                    }
                }
            }
        } else {
            i++;
        }
    }
}

/* ───────────────────────── crawl ───────────────────────── */

String section_for(const char* path, DocKind kind) {
    if (kind == kind_man) {
        /* …/man1/foo.1.gz → man1  (scan all /manN components) */
        for (const char* p = path; p && *p; p++) {
            if (p[0] == '/' && p[1] == 'm' && p[2] == 'a' && p[3] == 'n' &&
                p[4] >= '1' && p[4] <= '9' &&
                (p[5] == '/' || p[5] == 0 || p[5] == '.')) {
                char buf[8];
                snprintf(buf, sizeof(buf), "man%c", p[4]);
                return str_dup_c(buf);
            }
        }
        return str_dup_c("man");
    }
    return str_dup_c("doc");
}

String title_for(const char* path, DocKind kind) {
    const char* base = path_base(path);
    if (kind == kind_man) return man_title_from_base(base);
    /* doc: parent/base for readability */
    const char* slash = strrchr(path, '/');
    if (slash && slash != path) {
        const char* prev = slash - 1;
        while (prev > path && *prev != '/') prev--;
        if (*prev == '/') prev++;
        char buf[MAX_TITLE];
        snprintf(buf, sizeof(buf), "%.*s/%s",
                 (int)(slash - prev) > 40 ? 40 : (int)(slash - prev), prev, base);
        return str_dup_c(buf);
    }
    return str_dup_c(base);
}

void ingest_file(const char* path, List<Doc*>* corpus, Map<String, Term*>* inv) {
    /* g_max_files == 0 → unlimited */
    if (g_max_files > 0 && g_files_indexed >= g_max_files) return;
    if (!path_safe(path)) return;

    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) return;
    if (st.st_size <= 0) return;
    /* skip huge docs for indexing */
    if (st.st_size > 2 * 1024 * 1024) return;

    const char* base = path_base(path);
    if (!looks_indexable(base)) return;

    DocKind kind = classify_name(base);
    unowned char* raw = load_raw(path, MAX_INDEX_BODY * 2);
    if (!raw) return;

    unowned char* plain = to_plain(raw, kind, MAX_INDEX_BODY);
    free(raw);
    if (!plain || !plain[0]) {
        if (plain) free(plain);
        return;
    }

    int id = corpus.Count();
    String title = title_for(path, kind);
    String pstr = str_dup_c(path);
    String sec = section_for(path, kind);
    String body = (String)plain;   /* hand off malloc buffer as String */

    Doc* d = new Doc(id, title, pstr, sec, kind, body);
    corpus.Add(d);
    map_doc_into(d, inv);

    g_files_indexed++;
    g_bytes_indexed += (long)strlen(plain);
    if ((g_files_indexed % 100) == 0) {
        fprintf(stderr, "\r  indexed %d files (%ld KB plain)…",
                g_files_indexed, g_bytes_indexed / 1024);
        fflush(stderr);
    }
}

/* Collect every candidate path (no scan ceiling — Google mode). */
void collect_paths(const char* path, int depth, List<String>* out) {
    if (depth > 8) return;

    DIR* d = opendir(path);
    if (!d) return;

    struct dirent* e;
    char child[MAX_PATH];
    while ((e = readdir(d)) != NULL) {
        const char* name = e->d_name;
        if (!name || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        /* Skip translation trees — same content in other languages. */
        if (strcmp(name, "locale") == 0 || strcmp(name, "i18n") == 0 ||
            strcmp(name, "icons") == 0 || strcmp(name, "pixmaps") == 0 ||
            strcmp(name, "js") == 0 || strcmp(name, "_static") == 0)
            continue;

        int plen = (int)strlen(path);
        int nlen = (int)strlen(name);
        if (plen + 1 + nlen + 1 >= MAX_PATH) continue;
        memcpy(child, path, (size_t)plen);
        child[plen] = '/';
        memcpy(child + plen + 1, name, (size_t)nlen + 1);

        struct stat st;
        if (stat(child, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            /* man locale dirs like man/fr/ man/de/ — still skip pure i18n piles */
            if (nlen == 2 && name[0] >= 'a' && name[0] <= 'z' &&
                name[1] >= 'a' && name[1] <= 'z' &&
                strstr(path, "/man") != NULL && strstr(path, "/man/man") == NULL) {
                /* /usr/share/man/fr/… translated pages — skip unless you want them */
                continue;
            }
            collect_paths(child, depth + 1, out);
        } else if (S_ISREG(st.st_mode) || S_ISLNK(st.st_mode)) {
            g_files_seen++;
            if (looks_indexable(name) && path_safe(child))
                out.Add(str_dup_c(child));
            if ((g_files_seen % 2000) == 0) {
                fprintf(stderr, "\r  scanned %d files, %d candidates…",
                        g_files_seen, out.Count());
                fflush(stderr);
            }
        }
    }
    closedir(d);
}

int section_id(const char* path) {
    if (!path) return 0;
    for (const char* p = path; *p; p++) {
        if (p[0] == '/' && p[1] == 'm' && p[2] == 'a' && p[3] == 'n' &&
            p[4] >= '1' && p[4] <= '9' &&
            (p[5] == '/' || p[5] == 0 || p[5] == '.'))
            return (int)(p[4] - '0');
    }
    return 0;
}

/* Topic name length: "strncmp.3.gz" → 7  (not full basename). */
int man_topic_len(const char* base) {
    if (!base) return 0;
    int n = (int)strlen(base);
    if (n > 3 && ends_with_ci(base, ".gz")) n -= 3;
    for (int i = n - 1; i >= 0; i--) {
        if (base[i] == '.') {
            if (i + 1 < n && base[i + 1] >= '1' && base[i + 1] <= '9')
                return i;
            break;
        }
    }
    return n;
}

/* Light ranking only used when DOCSEARCH_MAX caps the corpus.
 * Short topic names win; man1/man3 mildly preferred as ties — never starve man3. */
int path_priority(const char* path) {
    if (!path) path = "";
    const char* base = path_base(path);
    int tlen = man_topic_len(base);
    int hy = 0;
    for (int i = 0; i < tlen; i++) if (base[i] == '-') hy++;
    int score = tlen * 10 + hy * 30;
    int sec = section_id(path);
    if (sec == 1) score -= 6;
    else if (sec == 3) score -= 5;   /* libc: strncmp, malloc, … */
    else if (sec == 2) score -= 4;
    else if (sec == 8) score -= 3;
    else if (sec == 5 || sec == 7) score -= 2;
    if (strncmp(base, "README", 6) == 0) score -= 8;
    return score;
}

int by_priority(String a, String b) {
    const char* xa = a;
    const char* xb = b;
    if (!xa) xa = "";
    if (!xb) xb = "";
    int pa = path_priority(xa);
    int pb = path_priority(xb);
    if (pa != pb) return pa - pb;
    return strcmp(xa, xb);
}

/* ───────────────────────── query / rank ───────────────────────── */

void lower_copy(const char* in, char* out, int out_max) {
    int i = 0;
    if (!in) { out[0] = 0; return; }
    while (in[i] && i < out_max - 1) {
        out[i] = (char)tolower((unsigned char)in[i]);
        i++;
    }
    out[i] = 0;
}

/* Rank docs for free-text query.  Returns value List<Hit> (RAII). */
List<Hit> search_docs(List<Doc*>* corpus, Map<String, Term*>* inv, const char* query) {
    auto hits = List<Hit>();
    int nDocs = corpus.Count();
    if (nDocs == 0) return move hits;

    char qbuf[MAX_QUERY];
    lower_copy(query, qbuf, MAX_QUERY);
    /* trim */
    char* q = qbuf;
    while (*q == ' ') q++;
    int qlen = (int)strlen(q);
    while (qlen > 0 && q[qlen - 1] == ' ') { q[--qlen] = 0; }

    if (qlen == 0) {
        /* empty query → first MAX_SHOW docs as score=0 browse list */
        int n = nDocs < MAX_SHOW ? nDocs : MAX_SHOW;
        for (int i = 0; i < n; i++) hits.Add(Hit(i, 0));
        return move hits;
    }

    int* tf = (int*)calloc((size_t)nDocs, sizeof(int));
    int* matched = (int*)calloc((size_t)nDocs, sizeof(int));
    int* title_hit = (int*)calloc((size_t)nDocs, sizeof(int));
    if (!tf || !matched || !title_hit) {
        free(tf); free(matched); free(title_hit);
        return move hits;
    }

    /* Title / path boosts (partial words while typing — as-you-type UX). */
    for (int d = 0; d < nDocs; d++) {
        Doc* doc = corpus.Get(d);
        char tbuf[MAX_TITLE];
        char pbuf[MAX_PATH];
        lower_copy(doc.title, tbuf, MAX_TITLE);
        lower_copy(doc.path, pbuf, MAX_PATH);

        /* name before '(' — "gzip(1)" → "gzip" */
        char name[MAX_TITLE];
        int ni = 0;
        while (tbuf[ni] && tbuf[ni] != '(' && ni < MAX_TITLE - 1) {
            name[ni] = tbuf[ni];
            ni++;
        }
        name[ni] = 0;

        if (strcmp(name, q) == 0) {
            title_hit[d] = 100;        /* exact man name */
        } else if (strncmp(name, q, qlen) == 0 && qlen >= 2) {
            title_hit[d] = 40;         /* prefix of name (typing "gz" → gzip) */
        } else if (strstr(tbuf, q) != NULL) {
            title_hit[d] = 8;          /* substring in full title */
        } else if (strstr(pbuf, q) != NULL) {
            title_hit[d] = 3;          /* path only */
        }
    }

    /* Token pass → inverted index (body + title tokens already mapped). */
    int qi = 0;
    int n_terms = 0;
    while (q[qi]) {
        if (isalnum((unsigned char)q[qi])) {
            int start = qi;
            while (q[qi] && isalnum((unsigned char)q[qi])) qi++;
            int len = qi - start;
            if (len >= 1 && len <= 32) {
                String w = intern_word(q + start, len);
                n_terms++;
                Term* t = NULL;
                if (inv.TryGet(w, &t) && t != NULL) {
                    int last = -1;
                    for (auto docId in t.docIds) {
                        if (docId < 0 || docId >= nDocs) continue;
                        tf[docId] = tf[docId] + 1;
                        if (docId != last) {
                            matched[docId] = matched[docId] + 1;
                            last = docId;
                        }
                    }
                }
            }
        } else {
            qi++;
        }
    }

    for (int d = 0; d < nDocs; d++) {
        int score = 0;
        if (tf[d] > 0)
            score = matched[d] * 1000 + tf[d];
        if (title_hit[d] > 0)
            score += title_hit[d] * 1000;
        if (score > 0) hits.Add(Hit(d, score));
    }

    free(tf);
    free(matched);
    free(title_hit);

    hits.Sort(ByScoreDesc);
    if (hits.Count() > MAX_SHOW) {
        auto top = hits.Take(MAX_SHOW);
        return move top;
    }
    return move hits;
}

/* ───────────────────────── open / view ───────────────────────── */

int have_cmd(const char* name) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "command -v %s >/dev/null 2>&1", name);
    return system(cmd) == 0;
}

/* Always decompress → normalise → less.
 * Pandoc and less both choke on raw .md.gz / .html.gz (binary "gz trash"). */
void view_doc(Doc* d) {
    if (!d || !path_safe(d.path)) return;
    term_leave_raw();
    fputs("\033[2J\033[H", stdout);
    printf("%s▸ opening%s %s  %s(%s)%s\n",
           term_bold_cyan(), term_reset(),
           d.title, term_dim(), d.path, term_reset());
    if (is_gz_path(d.path))
        printf("%s  (decompressing .gz…)%s\n", term_dim(), term_reset());
    fflush(stdout);

    char cmd[MAX_PATH + 320];

    if (d.kind == kind_man) {
        /* man -l understands .gz man sources and runs the system pager */
        snprintf(cmd, sizeof(cmd),
                 "man -l '%s' 2>/dev/null || "
                 "(zcat -- '%s' 2>/dev/null | groff -man -Tutf8 2>/dev/null | col -bx | less -R)",
                 d.path, d.path);
        system(cmd);
        term_enter_raw();
        return;
    }

    /* Load always goes through load_raw → zcat for *.gz. */
    unowned char* raw = load_raw(d.path, MAX_VIEW_BODY);
    if (!raw || !raw[0]) {
        if (raw) free(raw);
        printf("(could not load document — missing or empty after decompress)\n");
        printf("press enter…");
        getchar();
        term_enter_raw();
        return;
    }

    /* Sanity: refuse to page if still looks like gzip magic (zcat failed silently). */
    if ((unsigned char)raw[0] == 0x1f && (unsigned char)raw[1] == 0x8b) {
        free(raw);
        printf("(still gzip-compressed after load — zcat failed)\n");
        printf("press enter…");
        getchar();
        term_enter_raw();
        return;
    }

    const char* src_tmp = "/tmp/classy-docsearch-src";
    const char* view_tmp = "/tmp/classy-docsearch-view.txt";
    int used_pandoc = 0;

    if (d.kind == kind_md && have_cmd("pandoc")) {
        File.write_text((char*)src_tmp, raw);
        free(raw);
        raw = NULL;
        snprintf(cmd, sizeof(cmd),
                 "pandoc -f markdown -t plain '%s' 2>/dev/null | less -R",
                 src_tmp);
        if (system(cmd) == 0) used_pandoc = 1;
    } else if (d.kind == kind_html && have_cmd("pandoc")) {
        File.write_text((char*)src_tmp, raw);
        free(raw);
        raw = NULL;
        snprintf(cmd, sizeof(cmd),
                 "pandoc -f html -t plain '%s' 2>/dev/null | less -R",
                 src_tmp);
        if (system(cmd) == 0) used_pandoc = 1;
    }

    if (!used_pandoc) {
        /* Fall back: our strippers (already know how to handle plain MD/HTML). */
        unowned char* plain = NULL;
        if (raw) {
            plain = to_plain(raw, d.kind, MAX_VIEW_BODY);
            free(raw);
            raw = NULL;
        } else {
            /* pandoc failed after free — reload */
            raw = load_raw(d.path, MAX_VIEW_BODY);
            plain = raw ? to_plain(raw, d.kind, MAX_VIEW_BODY) : NULL;
            if (raw) free(raw);
            raw = NULL;
        }
        if (!plain || !plain[0]) {
            if (plain) free(plain);
            printf("(no legible text after decompress/strip)\n");
            printf("press enter…");
            getchar();
            term_enter_raw();
            return;
        }
        File.write_text((char*)view_tmp, plain);
        free(plain);
        snprintf(cmd, sizeof(cmd), "less -R '%s'", view_tmp);
        system(cmd);
    }

    term_enter_raw();
}

/* ───────────────────────── TUI draw ───────────────────────── */

void draw_pad(int n) { for (int i = 0; i < n; i++) putchar(' '); }

void draw_ui(List<Doc*>* corpus, List<Hit>* hits, const char* query,
             Focus focus, int selected, int n_terms) {
    int cols = term_cols();
    int rows = term_rows();
    int list_rows = rows - 10;
    if (list_rows < 5) list_rows = 5;
    if (list_rows > MAX_SHOW) list_rows = MAX_SHOW;

    fputs("\033[2J\033[H", stdout);

    /* banner */
    printf("%s", term_bold_cyan());
    printf("╔");
    for (int i = 0; i < cols - 2 && i < 70; i++) printf("═");
    printf("╗\n");
    printf("║  CLASSY DOCSEARCH  ·  man + md + html  ·  ClassyC inverted index");
    int pad = cols - 62;
    if (pad > 0 && pad < 20) draw_pad(pad);
    printf("║\n");
    printf("╚");
    for (int i = 0; i < cols - 2 && i < 70; i++) printf("═");
    printf("╝%s\n", term_reset());

    /* stats */
    printf("  %s%d%s docs  ·  %s%d%s terms  ·  %s%ld%s KB plain\n",
           term_bright_green(), corpus.Count(), term_reset(),
           term_bright_green(), n_terms, term_reset(),
           term_bright_green(), g_bytes_indexed / 1024, term_reset());

    /* query line */
    int qfocus = (focus == focus_query);
    printf("\n  %sQuery%s ", qfocus ? term_bold_yellow() : term_dim(), term_reset());
    printf("%s", qfocus ? term_bold() : "");
    printf("[%s", query);
    if (qfocus) printf("▌");
    printf("]%s\n", term_reset());

    /* mode + hit count */
    printf("  %s%d match(es)%s   focus: %s%s%s\n\n",
           term_cyan(), hits.Count(), term_reset(),
           term_bold(),
           focus == focus_query ? "SEARCH  (Tab → list)" : "LIST  (Tab → search)",
           term_reset());

    /* results */
    int n = hits.Count();
    if (n == 0) {
        printf("  %s(no documents matched)%s\n", term_dim(), term_reset());
    } else {
        int start = 0;
        if (selected >= list_rows) start = selected - list_rows + 1;
        if (start < 0) start = 0;
        int end = start + list_rows;
        if (end > n) end = n;

        for (int i = start; i < end; i++) {
            Hit h = hits.Get(i);
            Doc* d = corpus.Get(h.docId);
            int sel = (i == selected && focus == focus_list);
            const char* title = d.title;
            const char* section = d.section;
            const char* path = d.path;
            if (!title) title = "?";
            if (!section) section = "";
            if (!path) path = "";
            if (sel) printf("%s%s", term_bold(), term_bright_cyan());
            printf("  %s ", sel ? "▶" : " ");
            printf("%-28.28s", title);
            printf("  %-6.6s", section);
            printf("  %s", d.KindLabel());
            if (h.score > 0) {
                printf("  %sscore=%d%s",
                       term_dim(), h.score,
                       sel ? term_bold() : term_reset());
            }
            if (sel) printf("%s", term_reset());
            printf("\n");
            if (sel) printf("    %s%s%s\n", term_dim(), path, term_reset());
        }
    }

    /* footer help */
    printf("\n  %s", term_dim());
    printf("type · Tab list · ↑↓/jk move · Enter open · Ctrl-U clear · q/Ctrl-C quit");
    printf("%s\n", term_reset());
    fflush(stdout);
}

/* ───────────────────────── main ───────────────────────── */

int env_int(const char* key, int defv) {
    const char* s = getenv(key);
    if (!s || !s[0]) return defv;
    return atoi(s);
}

int main(int argc, char** argv) {
    g_max_files = env_int("DOCSEARCH_MAX", DEFAULT_MAX_FILES);
    g_batch = env_int("DOCSEARCH_BATCH", 0);

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    printf("\n");
    printf("%s", term_bold_cyan());
    printf("   ██████╗  ██████╗  ██████╗███████╗███████╗ █████╗ ██████╗  ██████╗██╗  ██╗\n");
    printf("   ██╔══██╗██╔═══██╗██╔════╝██╔════╝██╔════╝██╔══██╗██╔══██╗██╔════╝██║  ██║\n");
    printf("   ██║  ██║██║   ██║██║     ███████╗█████╗  ███████║██████╔╝██║     ███████║\n");
    printf("   ██║  ██║██║   ██║██║     ╚════██║██╔══╝  ██╔══██║██╔══██╗██║     ██╔══██║\n");
    printf("   ██████╔╝╚██████╔╝╚██████╗███████║███████╗██║  ██║██║  ██║╚██████╗██║  ██║\n");
    printf("   ╚═════╝  ╚═════╝  ╚═════╝╚══════╝╚══════╝╚═╝  ╚═╝╚═╝  ╚═╝ ╚═════╝╚═╝  ╚═╝\n");
    printf("%s", term_reset());
    printf("              man · markdown · html  ·  type-to-search TUI\n");
    printf("   types: %s · %s · %s\n\n",
           nameof<DocKind>(), nameof<Hit>(), typeof<Doc*>());

    auto corpus = List<Doc*>();
    corpus.owns();

    auto inv = Map<String, Term*>();
    inv.ownsValues();   /* ~Map deletes each Term* */

    /* Roots: argv paths after the program name, or defaults. */
    auto roots = List<String>();
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') continue;   /* skip flags if any slip through */
        roots.Add((String)argv[i]);
    }
    if (roots.Count() == 0) {
        /* Full man tree (all sections: man1…man8, man3type, …) + package docs. */
        const char* defs[] = {
            "/usr/share/man",
            "/usr/share/doc",
            "/usr/local/share/man",
            "/usr/local/share/doc",
            NULL
        };
        for (int i = 0; defs[i]; i++) {
            struct stat st;
            if (stat(defs[i], &st) == 0 && S_ISDIR(st.st_mode))
                roots.Add((String)defs[i]);
        }
    }

    if (g_max_files == 0)
        printf("  max files: unlimited  (DOCSEARCH_MAX=0 · Google mode)\n");
    else
        printf("  max files: %d  (DOCSEARCH_MAX)\n", g_max_files);
    printf("  crawling:\n");
    for (auto r in roots) printf("    · %s\n", r);
    printf("\n");

    /* Phase 1: discover everything, then index (all of it unless capped). */
    auto paths = List<String>();
    for (auto r in roots) collect_paths(r, 0, &paths);
    fprintf(stderr, "\r  scanned %d files, %d candidates.                \n",
            g_files_seen, paths.Count());

    /* Sort only matters when budget is capped — short names / man1+man3 first. */
    if (g_max_files > 0) paths.Sort(by_priority);

    int sec_used[9];
    for (int i = 0; i < 9; i++) sec_used[i] = 0;

    for (auto p in paths) {
        if (g_max_files > 0 && g_files_indexed >= g_max_files) break;
        int before = g_files_indexed;
        ingest_file(p, &corpus, &inv);
        if (g_files_indexed > before) {
            int s = section_id(p);
            if (s < 0 || s > 8) s = 0;
            sec_used[s]++;
        }
    }
    fprintf(stderr, "\r  indexed %d files (%ld KB plain).                      \n",
            g_files_indexed, g_bytes_indexed / 1024);
    printf("  section mix:");
    for (int i = 0; i < 9; i++) {
        if (sec_used[i] <= 0) continue;
        if (i == 0) printf(" doc=%d", sec_used[i]);
        else printf(" man%d=%d", i, sec_used[i]);
    }
    printf("\n");
    printf("  inverted index: %d unique terms\n\n", inv.Count());

    if (corpus.Count() == 0) {
        term_print_err("no documents indexed — check paths / permissions");
        return 1;
    }

    /* ── batch smoke: run a few canned queries and exit ── */
    if (g_batch || !isatty(0)) {
        const char* samples[] = {
            "ls", "gzip", "strncmp", "malloc", "printf", "memory", "README", NULL
        };
        for (int i = 0; samples[i]; i++) {
            auto hits = search_docs(&corpus, &inv, samples[i]);
            printf("Query: \"%s\"  →  %d match(es)\n", samples[i], hits.Count());
            int show = hits.Count() < 5 ? hits.Count() : 5;
            for (int h = 0; h < show; h++) {
                Hit hit = hits.Get(h);
                Doc* d = corpus.Get(hit.docId);
                printf("   %d. %-28s  [%s] score=%d\n",
                       h + 1, d.title, d.section, hit.score);
            }
            if (hits.Count() == 0) printf("   (none)\n");
            printf("\n");
        }
        term_print_ok("batch docsearch complete");
        return 0;
    }

    /* ── interactive TUI ── */
    char query[MAX_QUERY];
    query[0] = 0;
    int qlen = 0;
    Focus focus = focus_query;
    int selected = 0;

    auto hits = search_docs(&corpus, &inv, query);
    term_enter_raw();
    draw_ui(&corpus, &hits, query, focus, selected, inv.Count());

    for (;;) {
        char c = 0;
        int n = (int)read(0, &c, 1);
        if (n <= 0) continue;

        int redraw = 0;
        int research = 0;

        if (c == 3 || c == 17) {               /* Ctrl-C / Ctrl-Q */
            break;
        } else if (c == 'q' && focus == focus_list) {
            break;                             /* q quits from list focus only */
        } else if (c == 27) {                  /* Esc or arrow prefix */
            char seq[2] = {0, 0};
            if (read(0, &seq[0], 1) == 1 && seq[0] == '[') {
                if (read(0, &seq[1], 1) == 1) {
                    if (seq[1] == 'A') {       /* up */
                        if (focus == focus_list && selected > 0) {
                            selected--;
                            redraw = 1;
                        } else if (focus == focus_query && hits.Count() > 0) {
                            focus = focus_list;
                            selected = 0;
                            redraw = 1;
                        }
                    } else if (seq[1] == 'B') { /* down */
                        if (focus == focus_list) {
                            if (selected + 1 < hits.Count()) selected++;
                            redraw = 1;
                        } else {
                            focus = focus_list;
                            selected = 0;
                            redraw = 1;
                        }
                    }
                }
            } else {
                /* bare Esc */
                if (focus == focus_list) {
                    focus = focus_query;
                    redraw = 1;
                } else if (qlen > 0) {
                    qlen = 0;
                    query[0] = 0;
                    research = 1;
                    redraw = 1;
                }
            }
        } else if (c == '\t') {
            if (focus == focus_query) {
                focus = focus_list;
                if (selected >= hits.Count()) selected = 0;
            } else {
                focus = focus_query;
            }
            redraw = 1;
        } else if (c == '\n' || c == '\r') {
            if (hits.Count() > 0) {
                if (focus == focus_query) focus = focus_list;
                if (selected < 0 || selected >= hits.Count()) selected = 0;
                Hit h = hits.Get(selected);
                Doc* d = corpus.Get(h.docId);
                view_doc(d);
                redraw = 1;
            }
        } else if (c == 21) {                  /* Ctrl-U clear */
            qlen = 0;
            query[0] = 0;
            focus = focus_query;
            selected = 0;
            research = 1;
            redraw = 1;
        } else if (c == 127 || c == 8) {       /* backspace */
            if (focus != focus_query) {
                focus = focus_query;
                redraw = 1;
            }
            if (qlen > 0) {
                query[--qlen] = 0;
                research = 1;
                redraw = 1;
            }
        } else if (c == 'j' && focus == focus_list) {
            if (selected + 1 < hits.Count()) selected++;
            redraw = 1;
        } else if (c == 'k' && focus == focus_list) {
            if (selected > 0) selected--;
            redraw = 1;
        } else if (c >= 32 && c < 127) {
            /* printable → always type into query */
            if (focus != focus_query) {
                focus = focus_query;
            }
            if (qlen < MAX_QUERY - 1) {
                query[qlen++] = c;
                query[qlen] = 0;
                research = 1;
                redraw = 1;
            }
        }

        if (research) {
            auto next = search_docs(&corpus, &inv, query);
            hits = move next;
            selected = 0;
        }
        if (redraw) {
            draw_ui(&corpus, &hits, query, focus, selected, inv.Count());
        }
    }

    term_leave_raw();
    fputs("\033[2J\033[H", stdout);
    term_print_ok("docsearch closed — corpus reclaimed by RAII");
    printf("  %d docs · %d terms\n\n", corpus.Count(), inv.Count());
    return 0;
}
