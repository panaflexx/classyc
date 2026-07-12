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
 *   LINQ     · capturing Where (local min_score) · Take / Sort on ranked hits
 *   Map      · stack Map for O(1) term → Term*  · title-path scan fallback
 *   String   · equals / contains / starts_with · f-strings
 *   I/O      · zcat for .gz  · man -l to open pages  · strip HTML / light MD
 *   Persist  · binary docindex.cdb (whole-index dump)  · --reindex rebuild
 *   Search   · 2-stage: inverted terms → refine "phrase" / a+b on title|path|body
 *   TUI      · raw termios  · type-to-search  · Tab into list  · Enter open
 *
 * Query syntax:
 *   words          bag-of-words via inverted index (broad recall)
 *   "exact phrase"  stage-2 filter: substring in title / path / body (if loaded)
 *   word1+word2    stage-2 filter: adjacent tokens (any non-alnum separator)
 *   mix            index OR of all words, then all "…" and + clauses must pass
 *
 * What it indexes by default (Google mode — unlimited):
 *   /usr/share/man/**          all sections (man1…man8, man3type, …)
 *   /usr/share/doc/**          README*, *.md, *.html, text
 *   /usr/local/share/{man,doc} if present
 *   DOCSEARCH_MAX=0 (default) indexes every candidate; set a number to cap
 *
 * Binary cache (default ~/.cache/classy-docsearch/docindex.cdb):
 *   compact little-endian dump of docs + inverted postings (search is in-RAM)
 *   first run builds + saves; later runs load unless --reindex
 *
 * Key handling:
 *   1. .gz man pages  — `zcat` (via popen) for index text; `man -l` for viewing
 *   2. Markdown       — light markup strip for index; pandoc|less when available
 *   3. HTML           — pure-C tag strip for index (legible); pandoc -t plain
 *                       for view when present, else stripped text in less
 *
 * Build / run:
 *   ./bin/classyc -I include examples/classy-docsearch.cy -eg
 *   ./bin/classyc -I include examples/classy-docsearch.cy -eg -- --reindex
 *   DOCSEARCH_CDB=/tmp/docindex.cdb DOCSEARCH_MAX=500 ... -eg
 *   DOCSEARCH_BATCH=1        non-interactive sample queries + exit
 *
 * Keys in the TUI:
 *   type          search as you type (query focus)
 *   "phrase"      exact substring (title/path/body)
 *   a+b           adjacent terms (token neighborhood)
 *   Tab / ↓       focus result list
 *   ↑ ↓  j k      move selection (list focus)
 *   Enter         open selected manpage / document in pager
 *   Esc           list → query;  query → quit
 *   Ctrl-U        clear query
 *   q             quit (list focus only — so you can type "qemu")
 *   Ctrl-C/Q      quit always (restores terminal)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <termios.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <stdint.h>
#include "list.h"
#include "map.h"
#include "term.h"
#include "file.h"

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

/* ───────────────────────── globals (UI / load state shared across crawl + TUI) ─────────────────────────
 * Note: capturing lambdas as HOF args (Where/Filter/…) can close over locals now
 * (Strategy A open-code; see LAMBDA-CAPTURE.md).  These g_* bindings are for
 * cross-function UI/load state, not a workaround for missing captures.
 */

int g_max_files = DEFAULT_MAX_FILES;
int g_files_seen = 0;
int g_files_indexed = 0;
long g_bytes_indexed = 0;
int g_batch = 0;                 /* 1 = non-interactive smoke */
int g_reindex = 0;               /* 1 = force binary index rebuild */
int g_from_cdb = 0;              /* 1 if corpus loaded from docindex.cdb */
int g_tui_mode = 0;              /* 1 = alt-screen TUI is up (loading or live) */
int g_index_ready = 0;           /* 0 while loading; search waits until ready */
int g_want_quit = 0;             /* Esc/Ctrl-C while loading */
char g_cdb_path[MAX_PATH];
char g_ui_status[240];           /* live status under the query bar */
Focus g_ui_focus = focus_query;  /* focus during load-time input */
int   g_ui_selected = 0;         /* list selection during load-time input */

/* Soft refs for progress redraw + live search while loading (set by main). */
List<Doc*>*         g_ui_corpus = NULL;
Map<String, Term*>* g_ui_inv = NULL;
List<Hit>*          g_ui_hits = NULL;
char*               g_ui_query = NULL;
int*                g_ui_qlen = NULL;
int                 g_ui_n_terms = 0;

/* Forward decls — crawl/load call these before draw_ui is defined. */
void ui_set_status(const char* fmt, ...);
void ui_clear_status(void);
void ui_poll_typeahead(void);
void ui_live_search(void);
List<Hit> search_docs(List<Doc*>* corpus, Map<String, Term*>* inv, const char* query);

struct termios g_orig_term;
int g_raw = 0;

/* ───────────────────────── terminal raw mode ───────────────────────── */

void term_leave_raw(void) {
    if (g_raw) {
        /* leave alt screen, show cursor, restore tty */
        fputs("\033[?25h\033[?1049l", stdout);
        fflush(stdout);
        tcsetattr(0, TCSAFLUSH, &g_orig_term);
        g_raw = 0;
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
        /* alt screen: redraws can't scroll the original terminal */
        fputs("\033[?1049h\033[2J\033[H\033[?25l", stdout);
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
    if ((g_files_indexed % 50) == 0) {
        ui_set_status("indexing… %d files · %ld KB plain",
                      g_files_indexed, g_bytes_indexed / 1024);
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

#define MAX_Q_TERMS   32
#define MAX_Q_PHRASES  8
#define MAX_Q_PLUS     8
#define MAX_PLUS_WORDS 8
#define DOC_TEXT_MAX   (MAX_TITLE + MAX_PATH + 512)

void lower_copy(const char* in, char* out, int out_max) {
    int i = 0;
    if (!in) { out[0] = 0; return; }
    while (in[i] && i < out_max - 1) {
        out[i] = (char)tolower((unsigned char)in[i]);
        i++;
    }
    out[i] = 0;
}

/* Stage-2 haystack: title + path + body (body often empty after cdb load). */
void doc_search_text(Doc* doc, char* out, int out_max) {
    if (!out || out_max <= 0) return;
    out[0] = 0;
    if (!doc) return;
    char t[MAX_TITLE], p[MAX_PATH], b[512];
    lower_copy(doc.title, t, MAX_TITLE);
    lower_copy(doc.path, p, MAX_PATH);
    lower_copy(doc.body, b, (int)sizeof(b));
    snprintf(out, (size_t)out_max, "%s\n%s\n%s", t, p, b);
}

/* True if `needle` occurs as a literal substring of lowered haystack. */
int text_has_phrase(const char* hay, const char* phrase) {
    if (!hay || !phrase || !phrase[0]) return 0;
    return strstr(hay, phrase) != NULL;
}

/*
 * True if words[0..nwords) appear as consecutive alphanumeric tokens in hay
 * (any non-alnum separators).  Used for a+b+c adjacency filters.
 */
int text_has_adjacent(const char* hay, char words[][40], int nwords) {
    if (!hay || nwords <= 0) return 0;
    if (nwords == 1) {
        /* single token: whole-word preferred, substring fallback */
        const char* p = hay;
        int wlen = (int)strlen(words[0]);
        if (wlen <= 0) return 0;
        while (*p) {
            while (*p && !isalnum((unsigned char)*p)) p++;
            if (!*p) break;
            const char* s = p;
            while (*p && isalnum((unsigned char)*p)) p++;
            int len = (int)(p - s);
            if (len == wlen && strncmp(s, words[0], (size_t)wlen) == 0) return 1;
        }
        return strstr(hay, words[0]) != NULL;
    }

    /* Scan tokens; try to match the full chain starting at each position. */
    const char* p = hay;
    while (*p) {
        while (*p && !isalnum((unsigned char)*p)) p++;
        if (!*p) break;
        const char* save = p;
        int wi = 0;
        const char* q = p;
        while (wi < nwords) {
            while (*q && !isalnum((unsigned char)*q)) q++;
            if (!*q) break;
            const char* s = q;
            while (*q && isalnum((unsigned char)*q)) q++;
            int len = (int)(q - s);
            int wlen = (int)strlen(words[wi]);
            if (len != wlen || strncmp(s, words[wi], (size_t)wlen) != 0) break;
            wi++;
        }
        if (wi == nwords) return 1;
        /* advance one token from save */
        p = save;
        while (*p && isalnum((unsigned char)*p)) p++;
    }
    return 0;
}

/* Stage-1: add inverted-index postings for one word into tf/matched. */
void inv_add_word(Map<String, Term*>* inv, const char* word, int nDocs,
                  int* tf, int* matched) {
    if (!word || !word[0] || !inv || !tf || !matched) return;
    int len = (int)strlen(word);
    if (len < 1 || len > 32) return;
    String w = intern_word(word, len);
    Term* t = NULL;
    if (!(inv.TryGet(w, &t) && t != NULL)) return;
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

/*
 * Two-stage ranker:
 *   Stage 1 — inverted terms (free words + words inside "…" / a+b) for recall
 *   Stage 2 — "phrase" substring + a+b adjacency on title|path|body (unindexed)
 *
 * Operators refine candidates; they are not bag-of-words.  Body is only present
 * when the doc was crawled in this process (cdb loads leave body empty).
 */
List<Hit> search_docs(List<Doc*>* corpus, Map<String, Term*>* inv, const char* query) {
    auto hits = List<Hit>();
    int nDocs = corpus.Count();
    if (nDocs == 0) return move hits;

    char qbuf[MAX_QUERY];
    lower_copy(query, qbuf, MAX_QUERY);
    char* q = qbuf;
    while (*q == ' ') q++;
    int qlen = (int)strlen(q);
    while (qlen > 0 && q[qlen - 1] == ' ') { q[--qlen] = 0; }

    if (qlen == 0) {
        int n = nDocs < MAX_SHOW ? nDocs : MAX_SHOW;
        for (int i = 0; i < n; i++) hits.Add(Hit(i, 0));
        return move hits;
    }

    /* ── parse: free terms, "phrases", plus-chains ─────────────────────── */
    char terms[MAX_Q_TERMS][40];
    int n_terms = 0;
    char phrases[MAX_Q_PHRASES][MAX_QUERY];
    int n_phrases = 0;
    char plus_words[MAX_Q_PLUS][MAX_PLUS_WORDS][40];
    int plus_len[MAX_Q_PLUS];
    int n_plus = 0;

    int qi = 0;
    while (q[qi]) {
        if (q[qi] == '"') {
            qi++;
            int start = qi;
            while (q[qi] && q[qi] != '"') qi++;
            int len = qi - start;
            if (len > 0 && n_phrases < MAX_Q_PHRASES) {
                if (len >= MAX_QUERY) len = MAX_QUERY - 1;
                memcpy(phrases[n_phrases], q + start, (size_t)len);
                phrases[n_phrases][len] = 0;
                /* strip outer spaces inside the quotes */
                char* ps = phrases[n_phrases];
                while (*ps == ' ') ps++;
                int pl = (int)strlen(ps);
                while (pl > 0 && ps[pl - 1] == ' ') ps[--pl] = 0;
                if (ps != phrases[n_phrases]) memmove(phrases[n_phrases], ps, (size_t)pl + 1);
                if (phrases[n_phrases][0]) n_phrases++;
            }
            if (q[qi] == '"') qi++;
            continue;
        }
        if (isalnum((unsigned char)q[qi])) {
            /* word or word+word+… chain */
            char chain[MAX_PLUS_WORDS][40];
            int nc = 0;
            for (;;) {
                int start = qi;
                while (q[qi] && isalnum((unsigned char)q[qi])) qi++;
                int len = qi - start;
                if (len >= 1 && len <= 32 && nc < MAX_PLUS_WORDS) {
                    memcpy(chain[nc], q + start, (size_t)len);
                    chain[nc][len] = 0;
                    nc++;
                } else if (len > 32) {
                    /* skip oversized token */
                }
                if (q[qi] == '+') {
                    qi++;
                    continue;   /* next word of chain */
                }
                break;
            }
            if (nc == 1) {
                if (n_terms < MAX_Q_TERMS) {
                    memcpy(terms[n_terms], chain[0], 40);
                    n_terms++;
                }
            } else if (nc > 1 && n_plus < MAX_Q_PLUS) {
                plus_len[n_plus] = nc;
                for (int k = 0; k < nc; k++)
                    memcpy(plus_words[n_plus][k], chain[k], 40);
                n_plus++;
            }
            continue;
        }
        qi++;
    }

    /* Words inside phrases also feed stage-1 recall. */
    for (int pi = 0; pi < n_phrases; pi++) {
        const char* s = phrases[pi];
        int j = 0;
        while (s[j]) {
            if (isalnum((unsigned char)s[j])) {
                int start = j;
                while (s[j] && isalnum((unsigned char)s[j])) j++;
                int len = j - start;
                if (len >= 1 && len <= 32 && n_terms < MAX_Q_TERMS) {
                    memcpy(terms[n_terms], s + start, (size_t)len);
                    terms[n_terms][len] = 0;
                    n_terms++;
                }
            } else {
                j++;
            }
        }
    }
    for (int pi = 0; pi < n_plus; pi++) {
        for (int k = 0; k < plus_len[pi]; k++) {
            if (n_terms < MAX_Q_TERMS) {
                memcpy(terms[n_terms], plus_words[pi][k], 40);
                n_terms++;
            }
        }
    }

    int* tf = (int*)calloc((size_t)nDocs, sizeof(int));
    int* matched = (int*)calloc((size_t)nDocs, sizeof(int));
    int* title_hit = (int*)calloc((size_t)nDocs, sizeof(int));
    int* stage2_ok = (int*)calloc((size_t)nDocs, sizeof(int));
    if (!tf || !matched || !title_hit || !stage2_ok) {
        free(tf); free(matched); free(title_hit); free(stage2_ok);
        return move hits;
    }

    /* As-you-type title/path boosts on the raw query strip (ignore quotes/+). */
    char bare[MAX_QUERY];
    {
        int bi = 0;
        for (int i = 0; q[i] && bi < MAX_QUERY - 1; i++) {
            char c = q[i];
            if (c == '"' || c == '+') continue;
            bare[bi++] = c;
        }
        bare[bi] = 0;
        /* collapse double spaces */
        int w = 0;
        int sp = 1;
        for (int r = 0; bare[r]; r++) {
            if (bare[r] == ' ') {
                if (!sp) { bare[w++] = ' '; sp = 1; }
            } else {
                bare[w++] = bare[r];
                sp = 0;
            }
        }
        while (w > 0 && bare[w - 1] == ' ') w--;
        bare[w] = 0;
    }
    int bare_len = (int)strlen(bare);

    for (int d = 0; d < nDocs; d++) {
        Doc* doc = corpus.Get(d);
        char tbuf[MAX_TITLE];
        char pbuf[MAX_PATH];
        lower_copy(doc.title, tbuf, MAX_TITLE);
        lower_copy(doc.path, pbuf, MAX_PATH);

        char name[MAX_TITLE];
        int ni = 0;
        while (tbuf[ni] && tbuf[ni] != '(' && ni < MAX_TITLE - 1) {
            name[ni] = tbuf[ni];
            ni++;
        }
        name[ni] = 0;

        if (bare_len > 0) {
            if (strcmp(name, bare) == 0) {
                title_hit[d] = 100;
            } else if (strncmp(name, bare, bare_len) == 0 && bare_len >= 2) {
                title_hit[d] = 40;
            } else if (strstr(tbuf, bare) != NULL) {
                title_hit[d] = 8;
            } else if (strstr(pbuf, bare) != NULL) {
                title_hit[d] = 3;
            }
        }
        /* Phrase hits on title alone get a strong title boost too. */
        for (int pi = 0; pi < n_phrases; pi++) {
            if (strstr(tbuf, phrases[pi]) != NULL) {
                if (title_hit[d] < 80) title_hit[d] = 80;
            } else if (strstr(pbuf, phrases[pi]) != NULL) {
                if (title_hit[d] < 20) title_hit[d] = 20;
            }
        }
    }

    /* ── Stage 1: inverted index (recall) ──────────────────────────────── */
    for (int ti = 0; ti < n_terms; ti++)
        inv_add_word(inv, terms[ti], nDocs, tf, matched);

    int need_stage2 = (n_phrases > 0 || n_plus > 0);

    /* Mark every doc that stage-1 or title touched as a stage-2 candidate.
     * If the query is only operators and stage-1 found nothing, fall back to
     * scanning all docs for title/path (small enough corpora; still O(n)). */
    int any_cand = 0;
    for (int d = 0; d < nDocs; d++) {
        if (tf[d] > 0 || matched[d] > 0 || title_hit[d] > 0) {
            stage2_ok[d] = 1;
            any_cand = 1;
        }
    }
    if (need_stage2 && !any_cand) {
        for (int d = 0; d < nDocs; d++) stage2_ok[d] = 1;
        any_cand = 1;
    }

    /* ── Stage 2: unindexed phrase / adjacency refine ─────────────────── */
    if (need_stage2) {
        char hay[DOC_TEXT_MAX];
        for (int d = 0; d < nDocs; d++) {
            if (!stage2_ok[d]) continue;
            Doc* doc = corpus.Get(d);
            doc_search_text(doc, hay, DOC_TEXT_MAX);

            int ok = 1;
            for (int pi = 0; pi < n_phrases && ok; pi++) {
                if (!text_has_phrase(hay, phrases[pi])) ok = 0;
            }
            for (int pi = 0; pi < n_plus && ok; pi++) {
                if (!text_has_adjacent(hay, plus_words[pi], plus_len[pi])) ok = 0;
            }
            stage2_ok[d] = ok;
            if (ok) {
                /* Refine bonus: each satisfied operator pays out. */
                if (n_phrases > 0) title_hit[d] += 50 * n_phrases;
                if (n_plus > 0) title_hit[d] += 30 * n_plus;
            }
        }
    } else {
        for (int d = 0; d < nDocs; d++) stage2_ok[d] = 1;
    }

    for (int d = 0; d < nDocs; d++) {
        if (!stage2_ok[d]) continue;
        int score = 0;
        if (tf[d] > 0)
            score = matched[d] * 1000 + tf[d];
        if (title_hit[d] > 0)
            score += title_hit[d] * 1000;
        /* Operator-only queries: stage-2 pass with no index tf still counts. */
        if (need_stage2 && score == 0 && stage2_ok[d])
            score = 500;
        if (score > 0) hits.Add(Hit(d, score));
    }

    free(tf);
    free(matched);
    free(title_hit);
    free(stage2_ok);

    hits.Sort(ByScoreDesc);

    /* Capturing Where (Strategy A): free local min_score lives in this frame. */
    if (hits.Count() > MAX_SHOW && qlen >= 3) {
        int min_score = 1000;
        auto strong = hits.Where((Hit h) => h.score >= min_score);
        if (strong.Count() > 0) {
            if (strong.Count() > MAX_SHOW) {
                auto top = strong.Take(MAX_SHOW);
                return move top;
            }
            return move strong;
        }
    }
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

/* Print at most `width` printable chars (best-effort for ASCII UI), then clear EOL. */
void draw_trunc(const char* s, int width) {
    int n = 0;
    if (s) {
        while (s[n] && n < width) {
            unsigned char ch = (unsigned char)s[n];
            if (ch < 32) break;
            putchar((char)ch);
            n++;
        }
    }
    fputs("\033[K", stdout);   /* erase to end of line */
}

/* One screen line that ends with CR+LF only if more lines remain; track budget. */
int draw_line(int* used, int rows, const char* fmt, ...) {
    if (*used >= rows - 1) return 0;   /* leave last row for footer / no scroll */
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fputs("\033[K\n", stdout);
    (*used)++;
    return 1;
}

void draw_ui(List<Doc*>* corpus, List<Hit>* hits, const char* query,
             Focus focus, int selected, int n_terms) {
    int cols = term_cols();
    int rows = term_rows();
    if (cols < 48) cols = 48;
    if (rows < 10) rows = 10;

    /* Full repaint in alt screen — stay inside rows so the terminal never scrolls. */
    fputs("\033[H\033[J", stdout);

    int used = 0;
    int box_w = cols - 2;
    if (box_w > 72) box_w = 72;
    if (box_w < 40) box_w = 40;

    /* banner (3 lines) */
    printf("%s╔", term_bold_cyan());
    for (int i = 0; i < box_w; i++) printf("═");
    printf("╗\033[K\n");
    used++;
    printf("║   CLASSY DOCSEARCH · man+md+html · inverted index");
    {
        int inner = 50;
        int pad = box_w - inner;
        if (pad < 0) pad = 0;
        for (int i = 0; i < pad; i++) putchar(' ');
    }
    printf("║\033[K\n");
    used++;
    printf("╚");
    for (int i = 0; i < box_w; i++) printf("═");
    printf("╝%s\033[K\n", term_reset());
    used++;

    /* stats */
    printf("  %s%d%s docs · %s%d%s terms · %s%ld%s KB",
           term_bright_green(), corpus ? corpus.Count() : 0, term_reset(),
           term_bright_green(), n_terms, term_reset(),
           term_bright_green(), g_bytes_indexed / 1024, term_reset());
    if (g_from_cdb) printf(" %s[cdb]%s", term_dim(), term_reset());
    if (!g_index_ready) printf("  %sloading…%s", term_bold_yellow(), term_reset());
    fputs("\033[K\n", stdout);
    used++;

    /* query — always first-class, even while the index loads */
    int qfocus = (focus == focus_query);
    printf("  %sQuery%s %s[",
           qfocus ? term_bold_yellow() : term_dim(), term_reset(),
           qfocus ? term_bold() : "");
    {
        int qmax = cols - 14;
        if (qmax < 8) qmax = 8;
        draw_trunc(query ? query : "", qmax);
    }
    if (qfocus) printf("▌");
    printf("]%s\033[K\n", term_reset());
    used++;

    /* live status (load progress) or match summary */
    if (g_ui_status[0]) {
        printf("  %s", term_bright_cyan());
        draw_trunc(g_ui_status, cols - 4);
        printf("%s\033[K\n", term_reset());
    } else {
        int hc = hits ? hits.Count() : 0;
        printf("  %s%d match(es)%s  %s%s%s\033[K\n",
               term_cyan(), hc, term_reset(),
               term_dim(),
               focus == focus_query ? "SEARCH (Tab→list · Esc quit)"
                                    : "LIST (Tab→search · Esc back)",
               term_reset());
    }
    used++;

    /* blank separator — force default SGR so stats greens never tint the list */
    printf("%s\033[K\n", term_reset());
    used++;

    /* results: one line each — never extra path row (that blew past rows) */
    int list_budget = rows - used - 1;   /* footer occupies last row */
    if (list_budget < 1) list_budget = 1;
    if (list_budget > MAX_SHOW) list_budget = MAX_SHOW;

    /* Live partial search while the index is still loading. */
    int n = hits ? hits.Count() : 0;
    if (!g_index_ready && n == 0) {
        draw_line(&used, rows, "  %s⏳  loading index — type to search the corpus as it fills%s",
                  term_dim(), term_reset());
        draw_line(&used, rows, "  %s    %s%s",
                  term_dim(), g_cdb_path, term_reset());
    } else if (n == 0) {
        draw_line(&used, rows, "  %s(no documents matched%s)%s",
                  term_dim(),
                  g_index_ready ? "" : " yet — still loading",
                  term_reset());
    } else if (corpus) {
        if (!g_index_ready) {
            draw_line(&used, rows, "  %s▸ live results (index still loading…)%s",
                      term_bold_yellow(), term_reset());
        }
        int start = 0;
        if (selected >= list_budget) start = selected - list_budget + 1;
        if (start < 0) start = 0;
        int end = start + list_budget;
        if (end > n) end = n;

        for (int i = start; i < end; i++) {
            if (used >= rows - 1) break;
            Hit h = hits.Get(i);
            Doc* d = corpus.Get(h.docId);
            int sel = (i == selected && focus == focus_list);
            const char* title = d.title;
            const char* section = d.section;
            if (!title) title = "?";
            if (!section) section = "";

            /* Always open/close each row with reset so SGR never leaks to the
             * next line (score used to leave dim on; selection left cyan on). */
            printf("%s", term_reset());
            if (sel)
                printf("%s%s", term_bold(), term_bright_cyan());

            printf("  %s ", sel ? "▶" : " ");
            int twidth = cols - 28;
            if (twidth < 12) twidth = 12;
            if (twidth > 36) twidth = 36;
            printf("%-*.*s", twidth, twidth, title);
            printf(" %-5.5s", section);
            printf(" %-8.8s", (const char*)d.KindLabel());
            if (h.score > 0) {
                /* Dim score only: re-assert selection color after the dim
                 * segment so we do not rely on partial SGR stacking. */
                printf(" %s%d", term_dim(), h.score);
                if (sel)
                    printf("%s%s%s", term_reset(), term_bold(), term_bright_cyan());
                else
                    printf("%s", term_reset());
            }
            printf("%s\033[K\n", term_reset());
            used++;
        }
    }

    /* Footer pinned to last screen row — no trailing newline (avoids scroll). */
    printf("\033[%d;1H%s  type · \"phrase\" · a+b · Tab · ↑↓/jk · Enter · Ctrl-U · Esc%s\033[K",
           rows, term_dim(), term_reset());
    fflush(stdout);
}

/* Re-rank against whatever is in corpus/inv *right now* (partial OK). */
void ui_live_search(void) {
    if (!g_ui_corpus || !g_ui_inv || !g_ui_hits) return;
    if (g_ui_corpus->Count() == 0) {
        auto empty = List<Hit>();
        *g_ui_hits = move empty;
        g_ui_selected = 0;
        return;
    }
    const char* q = g_ui_query ? g_ui_query : "";
    auto next = search_docs(g_ui_corpus, g_ui_inv, q);
    *g_ui_hits = move next;
    if (g_ui_selected >= g_ui_hits->Count())
        g_ui_selected = g_ui_hits->Count() > 0 ? g_ui_hits->Count() - 1 : 0;
    if (g_ui_selected < 0) g_ui_selected = 0;
}

/* Open the selected hit (works mid-load once docs exist). */
void ui_open_selected(void) {
    if (!g_ui_hits || !g_ui_corpus) return;
    if (g_ui_hits->Count() <= 0) return;
    if (g_ui_selected < 0 || g_ui_selected >= g_ui_hits->Count())
        g_ui_selected = 0;
    Hit h = g_ui_hits->Get(g_ui_selected);
    if (h.docId < 0 || h.docId >= g_ui_corpus->Count()) return;
    Doc* d = g_ui_corpus->Get(h.docId);
    view_doc(d);
}

/* Drain keystrokes while loading: typeahead + arrows/Enter (not quit on down-arrow). */
void ui_poll_typeahead(void) {
    if (!g_tui_mode || !g_ui_query || !g_ui_qlen) return;
    int query_changed = 0;
    for (;;) {
        char c = 0;
        int n = (int)read(0, &c, 1);
        if (n <= 0) break;

        if (c == 3 || c == 17) {               /* Ctrl-C / Ctrl-Q */
            g_want_quit = 1;
            break;
        }

        if (c == 27) {                         /* Esc or CSI (arrows) */
            char seq0 = 0;
            int n0 = (int)read(0, &seq0, 1);
            if (n0 == 1 && seq0 == '[') {
                char seq1 = 0;
                if (read(0, &seq1, 1) == 1) {
                    if (seq1 == 'A') {          /* up */
                        if (g_ui_focus == focus_list && g_ui_selected > 0)
                            g_ui_selected--;
                        else if (g_ui_hits && g_ui_hits->Count() > 0) {
                            g_ui_focus = focus_list;
                            g_ui_selected = 0;
                        }
                    } else if (seq1 == 'B') {   /* down — must NOT quit */
                        if (g_ui_focus == focus_list) {
                            if (g_ui_hits && g_ui_selected + 1 < g_ui_hits->Count())
                                g_ui_selected++;
                        } else if (g_ui_hits && g_ui_hits->Count() > 0) {
                            g_ui_focus = focus_list;
                            g_ui_selected = 0;
                        }
                    }
                }
            } else {
                /* bare Esc: list->query, query->quit */
                if (g_ui_focus == focus_list) {
                    g_ui_focus = focus_query;
                } else {
                    g_want_quit = 1;
                    break;
                }
            }
            continue;
        }

        if (c == 9) {                          /* Tab */
            if (g_ui_focus == focus_query) {
                g_ui_focus = focus_list;
                if (g_ui_hits && g_ui_selected >= g_ui_hits->Count())
                    g_ui_selected = 0;
            } else {
                g_ui_focus = focus_query;
            }
            continue;
        }

        if (c == 10 || c == 13) {               /* Enter / CR — open hit */
            ui_open_selected();
            continue;
        }

        if (c == 'j' && g_ui_focus == focus_list) {
            if (g_ui_hits && g_ui_selected + 1 < g_ui_hits->Count())
                g_ui_selected++;
            continue;
        }
        if (c == 'k' && g_ui_focus == focus_list) {
            if (g_ui_selected > 0) g_ui_selected--;
            continue;
        }

        if (c == 21) {                          /* Ctrl-U clear query */
            *g_ui_qlen = 0;
            g_ui_query[0] = 0;
            g_ui_focus = focus_query;
            query_changed = 1;
            continue;
        }
        if (c == 127 || c == 8) {               /* backspace */
            g_ui_focus = focus_query;
            if (*g_ui_qlen > 0) {
                (*g_ui_qlen)--;
                g_ui_query[*g_ui_qlen] = 0;
                query_changed = 1;
            }
            continue;
        }
        if (c >= 32 && c < 127 && *g_ui_qlen < MAX_QUERY - 1) {
            g_ui_focus = focus_query;
            g_ui_query[*g_ui_qlen] = c;
            (*g_ui_qlen)++;
            g_ui_query[*g_ui_qlen] = 0;
            query_changed = 1;
        }
    }
    if (query_changed) ui_live_search();
}

/* Status under the query bar; live-searches + redraws TUI if it's up. */
void ui_set_status(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_ui_status, sizeof(g_ui_status), fmt, ap);
    va_end(ap);
    ui_poll_typeahead();
    if (g_want_quit) return;
    /* Re-search on every progress tick — empty query browses first docs. */
    if (g_tui_mode)
        ui_live_search();
    if (g_tui_mode) {
        auto empty = List<Hit>();
        List<Hit>* hp = g_ui_hits ? g_ui_hits : &empty;
        List<Doc*>* cp = g_ui_corpus;
        const char* q = g_ui_query ? g_ui_query : "";
        draw_ui(cp, hp, q, g_ui_focus, g_ui_selected, g_ui_n_terms);
    } else if (!g_batch) {
        fprintf(stderr, "\r  %s                    ", g_ui_status);
        fflush(stderr);
    }
}

void ui_clear_status(void) {
    g_ui_status[0] = 0;
}

/* ───────────────────────── binary index (docindex.cdb) ─────────────────────
 *
 * Classy Docsearch Binary v1 — whole-index dump for RAM search (not on-disk FTS).
 * Little-endian fixed layout:
 *
 *   header (64 bytes)
 *     magic[4] = 'C','D','B','1'
 *     u32 version = 1
 *     u32 n_docs, n_terms, n_posts
 *     i64 bytes_indexed
 *     i32 max_files
 *     i64 indexed_at
 *     u8  reserved[24]
 *   docs[n_docs]:
 *     i32 id · u32 kind · str title · str path · str section
 *       (str = u32 len + len bytes, no NUL)
 *   terms[n_terms]:
 *     str term · u32 n_pairs · {u32 doc_id, u32 tf} × n_pairs
 */

#define CDB_MAGIC "CDB1"
#define CDB_VERSION 1u
#define CDB_HEADER_SIZE 64
#define DEFAULT_CDB_NAME "docindex.cdb"

int env_int(const char* key, int defv) {
    const char* s = getenv(key);
    if (!s || !s[0]) return defv;
    return atoi(s);
}

void ensure_parent_dir(const char* file_path) {
    char dir[MAX_PATH];
    int n = (int)strlen(file_path);
    if (n <= 0 || n >= MAX_PATH) return;
    memcpy(dir, file_path, (size_t)n + 1);
    char* slash = strrchr(dir, '/');
    if (!slash || slash == dir) return;
    *slash = 0;
    if (!path_safe(dir)) return;
    char cmd[MAX_PATH + 32];
    snprintf(cmd, sizeof(cmd), "mkdir -p -- '%s' 2>/dev/null", dir);
    system(cmd);
}

/* Resolve path: --db / DOCSEARCH_CDB|DOCSEARCH_DB / ~/.cache/.../docindex.cdb */
void resolve_cdb_path(const char* override_path) {
    if (override_path && override_path[0]) {
        snprintf(g_cdb_path, sizeof(g_cdb_path), "%s", override_path);
        return;
    }
    const char* env = getenv("DOCSEARCH_CDB");
    if (!env || !env[0]) env = getenv("DOCSEARCH_DB");
    if (env && env[0]) {
        snprintf(g_cdb_path, sizeof(g_cdb_path), "%s", env);
        return;
    }
    const char* home = getenv("HOME");
    if (home && home[0]) {
        snprintf(g_cdb_path, sizeof(g_cdb_path),
                 "%s/.cache/classy-docsearch/%s", home, DEFAULT_CDB_NAME);
        return;
    }
    snprintf(g_cdb_path, sizeof(g_cdb_path), "./%s", DEFAULT_CDB_NAME);
}

int cdb_write_u32(FILE* f, uint32_t v) {
    unsigned char b[4];
    b[0] = (unsigned char)(v);
    b[1] = (unsigned char)(v >> 8);
    b[2] = (unsigned char)(v >> 16);
    b[3] = (unsigned char)(v >> 24);
    return fwrite(b, 1, 4, f) == 4;
}

int cdb_write_i32(FILE* f, int32_t v) {
    return cdb_write_u32(f, (uint32_t)v);
}

int cdb_write_i64(FILE* f, int64_t v) {
    uint64_t u = (uint64_t)v;
    unsigned char b[8];
    for (int i = 0; i < 8; i++) b[i] = (unsigned char)(u >> (8 * i));
    return fwrite(b, 1, 8, f) == 8;
}

int cdb_write_bytes(FILE* f, const void* p, uint32_t n) {
    if (n == 0) return 1;
    return fwrite(p, 1, n, f) == n;
}

int cdb_write_str(FILE* f, const char* s) {
    if (!s) s = "";
    uint32_t n = (uint32_t)strlen(s);
    if (!cdb_write_u32(f, n)) return 0;
    return cdb_write_bytes(f, s, n);
}

int cdb_read_u32(FILE* f, uint32_t* out) {
    unsigned char b[4];
    if (fread(b, 1, 4, f) != 4) return 0;
    *out = (uint32_t)b[0]
         | ((uint32_t)b[1] << 8)
         | ((uint32_t)b[2] << 16)
         | ((uint32_t)b[3] << 24);
    return 1;
}

int cdb_read_i32(FILE* f, int32_t* out) {
    uint32_t u = 0;
    if (!cdb_read_u32(f, &u)) return 0;
    *out = (int32_t)u;
    return 1;
}

int cdb_read_i64(FILE* f, int64_t* out) {
    unsigned char b[8];
    if (fread(b, 1, 8, f) != 8) return 0;
    uint64_t u = 0;
    for (int i = 0; i < 8; i++) u |= ((uint64_t)b[i]) << (8 * i);
    *out = (int64_t)u;
    return 1;
}

/* Read length-prefixed string into malloc'd NUL-terminated buffer (caller frees). */
char* cdb_read_str_raw(FILE* f) {
    uint32_t n = 0;
    if (!cdb_read_u32(f, &n)) return NULL;
    if (n > 8 * 1024 * 1024u) return NULL;   /* hard cap against corrupt files */
    char* s = (char*)malloc((size_t)n + 1);
    if (!s) return NULL;
    if (n > 0 && fread(s, 1, n, f) != n) { free(s); return NULL; }
    s[n] = 0;
    return s;
}

int cdb_probe(const char* path) {
    if (!path || !path[0]) return 0;
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    unsigned char hdr[16];
    size_t got = fread(hdr, 1, 16, f);
    fclose(f);
    if (got < 16) return 0;
    if (hdr[0] != 'C' || hdr[1] != 'D' || hdr[2] != 'B' || hdr[3] != '1') return 0;
    uint32_t ver = (uint32_t)hdr[4] | ((uint32_t)hdr[5] << 8)
                 | ((uint32_t)hdr[6] << 16) | ((uint32_t)hdr[7] << 24);
    uint32_t n_docs = (uint32_t)hdr[8] | ((uint32_t)hdr[9] << 8)
                    | ((uint32_t)hdr[10] << 16) | ((uint32_t)hdr[11] << 24);
    return ver == CDB_VERSION && n_docs > 0;
}

/* Atomic-ish write: stream to path.tmp then rename over destination. */
int save_index(List<Doc*>* corpus, Map<String, Term*>* inv) {
    ui_set_status("writing cdb → %s …", g_cdb_path);
    if (!g_tui_mode) {
        printf("  writing binary index → %s\n", g_cdb_path);
        fflush(stdout);
    }

    ensure_parent_dir(g_cdb_path);

    char tmp[MAX_PATH + 8];
    if (strlen(g_cdb_path) + 5 >= sizeof(tmp)) {
        if (g_tui_mode) ui_set_status("cdb path too long");
        else printf("  %scdb path too long%s\n", term_bold_red(), term_reset());
        return 0;
    }
    snprintf(tmp, sizeof(tmp), "%s.tmp", g_cdb_path);

    FILE* f = fopen(tmp, "wb");
    if (!f) {
        if (g_tui_mode) ui_set_status("cannot create %s", tmp);
        else printf("  %scannot create%s %s\n", term_bold_red(), term_reset(), tmp);
        return 0;
    }

    /* Count only terms we will actually emit (non-empty word + postings). */
    uint32_t n_terms_w = 0;
    uint32_t n_posts = 0;
    for (auto term, t in inv) {
        if (!t || !t.docIds) continue;
        const char* w = term;
        if (!w || !w[0] || t.docIds.Count() == 0) continue;
        auto seen = Map<int, int>();
        for (auto docId in t.docIds) {
            int c = seen.GetOr(docId, 0);
            if (c == 0) n_posts++;
            seen.Set(docId, c + 1);
        }
        if (seen.Count() > 0) n_terms_w++;
    }

    /* Header */
    if (fwrite(CDB_MAGIC, 1, 4, f) != 4
        || !cdb_write_u32(f, CDB_VERSION)
        || !cdb_write_u32(f, (uint32_t)corpus.Count())
        || !cdb_write_u32(f, n_terms_w)
        || !cdb_write_u32(f, n_posts)
        || !cdb_write_i64(f, (int64_t)g_bytes_indexed)
        || !cdb_write_i32(f, (int32_t)g_max_files)
        || !cdb_write_i64(f, (int64_t)time(NULL))) {
        fclose(f); unlink(tmp);
        if (g_tui_mode) ui_set_status("cdb header write failed");
        else printf("  %scdb header write failed%s\n", term_bold_red(), term_reset());
        return 0;
    }
    /* Fixed header is 64 bytes: 40 used above + 24 reserved. */
    unsigned char pad[24];
    memset(pad, 0, sizeof(pad));
    if (fwrite(pad, 1, 24, f) != 24) {
        fclose(f); unlink(tmp);
        return 0;
    }

    for (int di = 0; di < corpus.Count(); di++) {
        Doc* d = corpus.Get(di);
        if (!d) continue;
        /* Cast through String → char* explicitly (same idiom as map_doc_into). */
        const char* title = (const char*)d.title;
        const char* path = (const char*)d.path;
        const char* section = (const char*)d.section;
        if (!title) title = "";
        if (!path) path = "";
        if (!section) section = "";
        int wok = 1;
        if (!cdb_write_i32(f, (int32_t)d.id)) wok = 0;
        if (wok && !cdb_write_u32(f, (uint32_t)d.kind)) wok = 0;
        if (wok && !cdb_write_str(f, title)) wok = 0;
        if (wok && !cdb_write_str(f, path)) wok = 0;
        if (wok && !cdb_write_str(f, section)) wok = 0;
        if (!wok) {
            fclose(f); unlink(tmp);
            if (g_tui_mode) ui_set_status("cdb doc write failed");
            else printf("  %scdb doc write failed%s\n", term_bold_red(), term_reset());
            return 0;
        }
    }

    int written_terms = 0;
    int written_posts = 0;
    for (auto term, t in inv) {
        if (!t || !t.docIds) continue;
        const char* w = term;
        if (!w || !w[0] || t.docIds.Count() == 0) continue;

        auto tf_map = Map<int, int>();
        for (auto docId in t.docIds) {
            int c = tf_map.GetOr(docId, 0);
            tf_map.Set(docId, c + 1);
        }
        uint32_t np = (uint32_t)tf_map.Count();
        if (np == 0) continue;
        if (!cdb_write_str(f, w) || !cdb_write_u32(f, np)) {
            fclose(f); unlink(tmp);
            if (g_tui_mode) ui_set_status("cdb term write failed");
            else printf("  %scdb term write failed%s\n", term_bold_red(), term_reset());
            return 0;
        }
        written_terms++;
        for (auto docId, tf in tf_map) {
            if (tf <= 0) continue;
            if (!cdb_write_u32(f, (uint32_t)docId) || !cdb_write_u32(f, (uint32_t)tf)) {
                fclose(f); unlink(tmp);
                if (g_tui_mode) ui_set_status("cdb posting write failed");
                else printf("  %scdb posting write failed%s\n", term_bold_red(), term_reset());
                return 0;
            }
            written_posts++;
            if ((written_posts % 25000) == 0)
                ui_set_status("saving postings… %d rows", written_posts);
        }
    }

    if ((uint32_t)written_terms != n_terms_w || (uint32_t)written_posts != n_posts) {
        fclose(f); unlink(tmp);
        if (g_tui_mode) ui_set_status("cdb count mismatch");
        else printf("  %scdb count mismatch%s (terms %d/%u posts %d/%u)\n",
                    term_bold_red(), term_reset(),
                    written_terms, n_terms_w, written_posts, n_posts);
        return 0;
    }

    if (fflush(f) != 0) {
        fclose(f); unlink(tmp);
        return 0;
    }
    fclose(f);

    if (rename(tmp, g_cdb_path) != 0) {
        unlink(tmp);
        if (g_tui_mode) ui_set_status("cdb rename failed: %s", strerror(errno));
        else printf("  %scdb rename failed:%s %s\n",
                    term_bold_red(), term_reset(), strerror(errno));
        return 0;
    }

    ui_set_status("saved %d docs · %d terms · %d postings",
                  corpus.Count(), written_terms, written_posts);
    if (!g_tui_mode)
        fprintf(stderr, "\r  saved %d docs · %d terms · %d postings                 \n",
                corpus.Count(), written_terms, written_posts);
    return 1;
}

int load_index(List<Doc*>* corpus, Map<String, Term*>* inv) {
    ui_set_status("loading cdb ← %s …", g_cdb_path);
    if (!g_tui_mode) {
        printf("  loading binary index ← %s\n", g_cdb_path);
        fflush(stdout);
    }

    FILE* f = fopen(g_cdb_path, "rb");
    if (!f) {
        if (g_tui_mode) ui_set_status("cannot open %s", g_cdb_path);
        else printf("  %scannot open%s %s\n", term_bold_red(), term_reset(), g_cdb_path);
        return 0;
    }

    unsigned char magic[4];
    if (fread(magic, 1, 4, f) != 4
        || magic[0] != 'C' || magic[1] != 'D' || magic[2] != 'B' || magic[3] != '1') {
        fclose(f);
        if (g_tui_mode) ui_set_status("bad cdb magic");
        else printf("  %sbad cdb magic%s\n", term_bold_red(), term_reset());
        return 0;
    }

    uint32_t version = 0, n_docs = 0, n_terms = 0, n_posts = 0;
    int64_t bytes_indexed = 0, indexed_at = 0;
    int32_t max_files = 0;
    if (!cdb_read_u32(f, &version) || version != CDB_VERSION
        || !cdb_read_u32(f, &n_docs) || !cdb_read_u32(f, &n_terms)
        || !cdb_read_u32(f, &n_posts)
        || !cdb_read_i64(f, &bytes_indexed)
        || !cdb_read_i32(f, &max_files)
        || !cdb_read_i64(f, &indexed_at)) {
        fclose(f);
        if (g_tui_mode) ui_set_status("cdb header corrupt");
        else printf("  %scdb header corrupt%s\n", term_bold_red(), term_reset());
        return 0;
    }
    if (fseek(f, CDB_HEADER_SIZE, SEEK_SET) != 0) {
        fclose(f);
        return 0;
    }

    g_bytes_indexed = (long)bytes_indexed;

    for (uint32_t i = 0; i < n_docs; i++) {
        if (g_want_quit) { fclose(f); return 0; }
        int32_t id = 0;
        uint32_t kind = 0;
        if (!cdb_read_i32(f, &id) || !cdb_read_u32(f, &kind)) {
            fclose(f);
            if (g_tui_mode) ui_set_status("cdb docs truncated");
            else printf("  %scdb docs truncated%s\n", term_bold_red(), term_reset());
            return 0;
        }
        unowned char* title = cdb_read_str_raw(f);
        unowned char* path = cdb_read_str_raw(f);
        unowned char* section = cdb_read_str_raw(f);
        if (!title || !path || !section) {
            free(title); free(path); free(section);
            fclose(f);
            if (g_tui_mode) ui_set_status("cdb doc strings truncated");
            else printf("  %scdb doc strings truncated%s\n", term_bold_red(), term_reset());
            return 0;
        }
        String st = str_dup_c(title);
        String sp = str_dup_c(path);
        String ss = str_dup_c(section);
        free(title); free(path); free(section);
        String body = str_dup_c("");
        Doc* d = new Doc((int)id, st, sp, ss, (DocKind)kind, body);
        corpus.Add(d);
        if (((int)i % 2000) == 0)
            ui_set_status("loading docs… %u / %u", i + 1, n_docs);
    }

    g_ui_n_terms = 0;
    ui_set_status("loading postings… (%u docs · %u terms)", n_docs, n_terms);

    uint32_t loaded_posts = 0;
    for (uint32_t ti = 0; ti < n_terms; ti++) {
        if (g_want_quit) { fclose(f); return 0; }
        unowned char* term = cdb_read_str_raw(f);
        uint32_t np = 0;
        if (!term || !cdb_read_u32(f, &np)) {
            free(term);
            fclose(f);
            if (g_tui_mode) ui_set_status("cdb terms truncated");
            else printf("  %scdb terms truncated%s\n", term_bold_red(), term_reset());
            return 0;
        }
        String w = str_dup_c(term);
        free(term);
        Term* t = new Term(w);
        for (uint32_t k = 0; k < np; k++) {
            uint32_t doc_id = 0, tf = 0;
            if (!cdb_read_u32(f, &doc_id) || !cdb_read_u32(f, &tf)) {
                delete t;
                fclose(f);
                if (g_tui_mode) ui_set_status("cdb postings truncated");
                else printf("  %scdb postings truncated%s\n", term_bold_red(), term_reset());
                return 0;
            }
            if (tf == 0) continue;
            /* Expand tf into multiset of doc ids (matches map_doc_into / search_docs). */
            for (uint32_t r = 0; r < tf; r++) t.docIds.Add((int)doc_id);
            loaded_posts++;
        }
        inv.Set(w, t);
        if ((ti % 2000) == 0 || (loaded_posts % 50000) == 0) {
            g_ui_n_terms = inv.Count();
            ui_set_status("loading postings… %u terms · %u pairs",
                          ti + 1, loaded_posts);
        }
    }

    fclose(f);

    g_files_indexed = corpus.Count();
    g_ui_n_terms = inv.Count();
    g_from_cdb = 1;
    ui_set_status("ready — %d docs · %d terms · %u postings",
                  corpus.Count(), inv.Count(), loaded_posts);
    if (!g_tui_mode)
        fprintf(stderr, "\r  loaded %d docs · %d terms · %u posting pairs            \n",
                corpus.Count(), inv.Count(), loaded_posts);
    return corpus.Count() > 0;
}

void print_usage(void) {
    printf("Usage: classy-docsearch [options] [root-dirs…]\n");
    printf("  --reindex, -r     rebuild index (ignore existing docindex.cdb)\n");
    printf("  --db PATH         index file (default ~/.cache/classy-docsearch/docindex.cdb)\n");
    printf("  --help, -h        this help\n");
    printf("  root dirs         crawl roots (default: /usr/share/man + /usr/share/doc)\n");
    printf("Env: DOCSEARCH_CDB (or DOCSEARCH_DB), DOCSEARCH_MAX, DOCSEARCH_REINDEX=1, DOCSEARCH_BATCH=1\n");
    printf("Link: classyc -I include examples/classy-docsearch.cy -eg\n");
}

/* ───────────────────────── main ───────────────────────── */

int main(int argc, char** argv) {
    g_max_files = env_int("DOCSEARCH_MAX", DEFAULT_MAX_FILES);
    g_batch = env_int("DOCSEARCH_BATCH", 0);
    g_reindex = env_int("DOCSEARCH_REINDEX", 0);

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    const char* db_override = NULL;
    auto roots = List<String>();

    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];
        if (!a) continue;
        if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            print_usage();
            return 0;
        }
        if (strcmp(a, "--reindex") == 0 || strcmp(a, "-r") == 0) {
            g_reindex = 1;
            continue;
        }
        if (strcmp(a, "--db") == 0) {
            if (i + 1 < argc) db_override = argv[++i];
            continue;
        }
        if (strncmp(a, "--db=", 5) == 0) {
            db_override = a + 5;
            continue;
        }
        if (a[0] == '-') continue;   /* skip unknown flags */
        roots.Add((String)a);
    }

    resolve_cdb_path(db_override);
    ensure_parent_dir(g_cdb_path);

    auto corpus = List<Doc*>();
    corpus.owns();

    auto inv = Map<String, Term*>();
    inv.ownsValues();   /* ~Map deletes each Term* */

    if (roots.Count() == 0) {
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

    int interactive = (!g_batch && isatty(0) && isatty(1));

    /* ── Interactive: paint TUI + query bar *first*, load index under it ── */
    char query[MAX_QUERY];
    query[0] = 0;
    int qlen = 0;
    Focus focus = focus_query;
    int selected = 0;
    auto hits = List<Hit>();

    if (interactive) {
        g_tui_mode = 1;
        g_index_ready = 0;
        g_want_quit = 0;
        g_ui_corpus = &corpus;
        g_ui_inv = &inv;
        g_ui_hits = &hits;
        g_ui_query = query;
        g_ui_qlen = &qlen;
        g_ui_n_terms = 0;
        term_enter_raw();
        ui_set_status("opening %s …", g_cdb_path);
        if (g_want_quit) { term_leave_raw(); return 0; }
    } else {
        printf("\n");
        printf("%s", term_bold_cyan());
        printf("   CLASSY DOCSEARCH  ·  batch / non-interactive\n");
        printf("%s", term_reset());
        if (g_max_files == 0)
            printf("  max files: unlimited\n");
        else
            printf("  max files: %d\n", g_max_files);
        printf("  index cdb: %s\n", g_cdb_path);
        if (g_reindex) printf("  mode: REINDEX\n");
        printf("\n");
    }

    int loaded = 0;
    if (!g_reindex) {
        if (cdb_probe(g_cdb_path)) {
            loaded = load_index(&corpus, &inv);
            if (!loaded) {
                ui_set_status("cache unreadable — will reindex…");
                /* drop any partial corpus from a failed load */
                corpus = List<Doc*>();
                corpus.owns();
                inv = Map<String, Term*>();
                inv.ownsValues();
                g_ui_corpus = &corpus;
                g_ui_inv = &inv;
                g_from_cdb = 0;
            }
        } else {
            ui_set_status("no cache yet — will crawl & build index…");
        }
        if (g_want_quit) {
            term_leave_raw();
            g_tui_mode = 0;
            return 0;
        }
    } else {
        ui_set_status("REINDEX forced — crawling filesystem…");
    }

    if (!loaded) {
        g_files_indexed = 0;
        g_bytes_indexed = 0;
        g_files_seen = 0;
        g_from_cdb = 0;

        auto corpus2 = List<Doc*>();
        corpus2.owns();
        auto inv2 = Map<String, Term*>();
        inv2.ownsValues();

        /* Point live search at the in-progress collections. */
        g_ui_corpus = &corpus2;
        g_ui_inv = &inv2;

        ui_set_status("scanning roots…");
        auto paths = List<String>();
        for (auto r in roots) collect_paths(r, 0, &paths);
        ui_set_status("scanned %d files · %d candidates — indexing…",
                      g_files_seen, paths.Count());

        if (g_max_files > 0) paths.Sort(by_priority);

        int sec_used[9];
        for (int i = 0; i < 9; i++) sec_used[i] = 0;

        for (auto p in paths) {
            if (g_max_files > 0 && g_files_indexed >= g_max_files) break;
            int before = g_files_indexed;
            ingest_file(p, &corpus2, &inv2);
            if (g_files_indexed > before) {
                int s = section_id(p);
                if (s < 0 || s > 8) s = 0;
                sec_used[s]++;
            }
            g_ui_n_terms = inv2.Count();
            if (g_want_quit) break;
        }

        if (g_want_quit) {
            term_leave_raw();
            g_tui_mode = 0;
            return 0;
        }

        if (corpus2.Count() == 0) {
            if (interactive) {
                ui_set_status("no documents found — check paths / permissions");
                sleep(2);
                term_leave_raw();
            }
            term_print_err("no documents indexed — check paths / permissions");
            return 1;
        }

        g_ui_n_terms = inv2.Count();
        save_index(&corpus2, &inv2);
        corpus = move corpus2;
        inv = move inv2;
        g_ui_corpus = &corpus;
        g_ui_inv = &inv;
    }

    if (corpus.Count() == 0) {
        if (interactive) {
            ui_set_status("empty index");
            sleep(1);
            term_leave_raw();
        }
        term_print_err("no documents indexed — check paths / permissions");
        return 1;
    }

    /* Index is live */
    g_index_ready = 1;
    g_ui_n_terms = inv.Count();
    g_ui_corpus = &corpus;
    ui_clear_status();

    /* ── batch smoke: run a few canned queries and exit ── */
    if (!interactive) {
        const char* samples[] = {
            "ls", "gzip", "malloc", "memory",
            "\"ls\"", "share+man", "\"man1\"",
            NULL
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

    /* ── interactive TUI (already up; index now ready) ── */
    {
        auto next = search_docs(&corpus, &inv, query);
        hits = move next;
    }
    g_ui_hits = &hits;
    /* Keep selection/focus from any mid-load navigation. */
    focus = g_ui_focus;
    selected = g_ui_selected;
    if (selected >= hits.Count()) selected = hits.Count() > 0 ? hits.Count() - 1 : 0;
    if (selected < 0) selected = 0;
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
                /* bare Esc (no [ following within VTIME) */
                if (focus == focus_list) {
                    focus = focus_query;       /* list → search bar */
                    redraw = 1;
                } else {
                    break;                     /* query focus → quit */
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
            g_ui_hits = &hits;
            g_ui_selected = 0;
            g_ui_focus = focus;
        }
        if (redraw) {
            g_ui_selected = selected;
            g_ui_focus = focus;
            draw_ui(&corpus, &hits, query, focus, selected, inv.Count());
        }
    }

    term_leave_raw();
    g_tui_mode = 0;
    term_print_ok("docsearch closed — corpus reclaimed by RAII");
    printf("  %d docs · %d terms\n\n", corpus.Count(), inv.Count());
    return 0;
}
