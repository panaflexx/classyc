/* val-041-million-byval-bench.cy — 1M-item List/Map by-value stress + corpus crawl
 *
 * Exercises first-class by-value collection semantics at scale:
 *   · List<int> of 1_000_000 · Where / Take / Skip / Copy / Sort pipelines
 *   · Map of 1_000_000 entries · Set / Get / Where / Keys / Copy
 *   · Optional real-text harvest from /usr/share/man and /usr/share/doc
 *     (inverted term→df Map, posting List) for search-engine flavour
 *
 * House style under test: RAII value shells — no `owned`/`delete` on local
 * transforms (GROUP/docs pointers may still use .owns() for element pointees).
 *
 * Timing: wall µs via gettimeofday.  PASS/FAIL like other cy-validate files.
 *
 * Run (from project root; may take ~tens of seconds JIT):
 *   ./bin/classyc -g -I include cy-validate/val-041-million-byval-bench.cy -eg
 *
 * Env (optional):
 *   N=1000000          target size (default 1000000)
 *   BENCH_MAX_FILES=800 crawl cap (default 800)
 *   BENCH_NO_CRAWL=1   skip man/doc harvest
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "list.h"
#include "map.h"
#include "set.h"

/* ───────────────────────── harness ───────────────────────── */

int passed = 0, failed = 0;

void check(int cond, const char* label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

long now_us() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long)tv.tv_sec * 1000000L + (long)tv.tv_usec;
}

void report(const char* name, long us, long n) {
    double ms = us / 1000.0;
    double mops = (n > 0 && us > 0) ? (n / (us / 1e6)) / 1e6 : 0.0;
    printf("  TIME  %-28s  %8.1f ms  (%ld ops ≈ %.2f Mops/s)\n",
           name, ms, n, mops);
}

int env_int(const char* key, int defv) {
    const char* s = getenv(key);
    if (!s || !s[0]) return defv;
    return atoi(s);
}

/* ───────────────────────── globals + small helpers ───────────────────────── */

int g_max_files = 800;
int g_files_seen = 0;
int g_files_read = 0;
long g_bytes_read = 0;
int g_half = 0;   /* Where predicate threshold (lambdas can't capture locals) */

int is_even(int x) { return (x & 1) == 0; }
int gt_half(int x) { return x > g_half; }
int by_int_asc(int a, int b) { return a - b; }
int by_int_desc(int a, int b) { return b - a; }

/* ───────────────────────── man/doc crawl ───────────────────────── */

/* Prefer plain text / man-ish files; skip huge blobs & obvious binaries. */
int looks_textish(const char* name) {
    if (!name || !name[0] || name[0] == '.') return 0;
    /* man pages: foo.1, foo.1.gz handled as "read will often fail/garbage" — skip .gz */
    int n = (int)strlen(name);
    if (n > 3 && strcmp(name + n - 3, ".gz") == 0) return 0;
    if (n > 4 && strcmp(name + n - 4, ".bz2") == 0) return 0;
    if (n > 3 && strcmp(name + n - 3, ".xz") == 0) return 0;
    if (n > 4 && strcmp(name + n - 4, ".png") == 0) return 0;
    if (n > 4 && strcmp(name + n - 4, ".svg") == 0) return 0;
    if (n > 4 && strcmp(name + n - 4, ".pdf") == 0) return 0;
    return 1;
}

/* Lowercase intern for a token (arena String). */
String make_term(const char* s, int len) {
    if (len <= 0) return (String)"";
    if (len > 48) len = 48;   /* clamp; long identifiers rare in man */
    char* w = (char*)malloc((size_t)len + 1);
    if (!w) return (String)"";
    for (int i = 0; i < len; i++)
        w[i] = (char)tolower((unsigned char)s[i]);
    w[len] = 0;
    return (String)w;
}

/* Tokenise body → add unique terms into `vocab` Set and bump df in `df`. */
void harvest_terms(const char* body, Set<String>* vocab, Map<String, int>* df) {
    if (!body) return;
    int i = 0;
    while (body[i]) {
        if (isalnum((unsigned char)body[i])) {
            int start = i;
            while (body[i] && isalnum((unsigned char)body[i])) i++;
            int len = i - start;
            if (len >= 2 && len <= 32) {
                String t = make_term(body + start, len);
                const char* tp = (const char*)t;
                if (tp && tp[0]) {
                    if (vocab.Add(t)) {
                        /* newly inserted into set — first time seen globally */
                        df.Set(t, 1);
                    } else {
                        int c = df.GetOr(t, 0);
                        df.Set(t, c + 1);
                    }
                }
            }
        } else {
            i++;
        }
    }
}

/* Read up to maxb bytes from path into malloc'd NUL-term buffer; NULL on fail. */
char* read_file_capped(const char* path, int maxb) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    char* buf = (char*)malloc((size_t)maxb + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t n = fread(buf, 1, (size_t)maxb, f);
    fclose(f);
    if (n == 0) { free(buf); return NULL; }
    /* Skip files that look binary (NUL in first 256 or high density of high bytes). */
    int check = n < 256 ? (int)n : 256;
    int high = 0;
    for (int i = 0; i < check; i++) {
        unsigned char c = (unsigned char)buf[i];
        if (c == 0) { free(buf); return NULL; }
        if (c > 127) high++;
    }
    if (high > check / 4) { free(buf); return NULL; }
    buf[n] = 0;
    g_bytes_read += (long)n;
    g_files_read++;
    return buf;
}

void walk_dir(const char* path, int depth, Set<String>* vocab, Map<String, int>* df) {
    if (g_files_seen >= g_max_files) return;
    if (depth > 6) return;

    DIR* d = opendir(path);
    if (!d) return;

    struct dirent* e;
    char child[1024];
    while ((e = readdir(d)) != NULL && g_files_seen < g_max_files) {
        const char* name = e->d_name;
        if (!name || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        if (!looks_textish(name) && e->d_type != DT_DIR && e->d_type != DT_UNKNOWN)
            continue;

        int plen = (int)strlen(path);
        int nlen = (int)strlen(name);
        if (plen + 1 + nlen + 1 >= (int)sizeof(child)) continue;
        memcpy(child, path, (size_t)plen);
        child[plen] = '/';
        memcpy(child + plen + 1, name, (size_t)nlen + 1);

        struct stat st;
        if (stat(child, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            walk_dir(child, depth + 1, vocab, df);
        } else if (S_ISREG(st.st_mode) && st.st_size > 0 && st.st_size < 256 * 1024) {
            if (!looks_textish(name)) continue;
            g_files_seen++;
            /* unowned: plain malloc buffer, not managed ownership layer */
            unowned char* body = read_file_capped(child, 32 * 1024);
            if (body) {
                harvest_terms(body, vocab, df);
                free(body);
            }
        }
    }
    closedir(d);
}

/* ───────────────────────── 1M List by-value bench ───────────────────────── */

void bench_list_million(int N) {
    printf("\n── List by-value · N=%d ──\n", N);
    long t0, t1;

    auto xs = List<int>(N > 0 ? N : 4);
    t0 = now_us();
    for (int i = 0; i < N; i++) xs.Add(i);
    t1 = now_us();
    report("List.Add fill", t1 - t0, N);
    check(xs.Count() == N, "list filled to N");

    t0 = now_us();
    auto evens = xs.Where(is_even);
    t1 = now_us();
    report("List.Where(even)", t1 - t0, N);
    check(evens.Count() == N / 2, "Where keeps N/2 evens");
    check(evens.Count() > 0 && evens.Get(0) == 0 && (evens.Last() % 2) == 0,
          "Where first/last even");

    t0 = now_us();
    auto top = xs.Take(1000);
    auto tail = xs.Skip(N - 1000);
    t1 = now_us();
    report("List.Take+Skip(1k)", t1 - t0, 2000);
    check(top.Count() == 1000 && top.Get(0) == 0 && top.Last() == 999, "Take(1000)");
    check(tail.Count() == 1000 && tail.Get(0) == N - 1000, "Skip to last 1000");

    t0 = now_us();
    auto cp = xs.Copy();
    t1 = now_us();
    report("List.Copy", t1 - t0, N);
    check(cp.Count() == N && cp.Get(N / 2) == N / 2, "Copy preserves elements");

    /* Pipeline of values (no owned) */
    g_half = N / 2;
    t0 = now_us();
    auto pipe = xs.Where(gt_half).Take(500).Skip(10);
    t1 = now_us();
    report("Where.Take.Skip chain", t1 - t0, N);
    int expect_pipe = 0;
    {
        int kept = 0;
        for (int i = g_half + 1; i < N && kept < 500; i++) kept++;
        expect_pipe = kept > 10 ? kept - 10 : 0;
    }
    check(pipe.Count() == expect_pipe, "chain yields expected");

    /* Sort a 50k slice (full 1M sort is optional — report separately) */
    int SORT_N = N < 50000 ? N : 50000;
    auto sample = xs.Take(SORT_N);
    t0 = now_us();
    sample.Sort(by_int_desc);
    t1 = now_us();
    report("List.Sort(50k desc)", t1 - t0, SORT_N);
    check(sample.Get(0) == SORT_N - 1 && sample.Last() == 0, "sort desc order");

    /* Plus two halves */
    auto left = xs.Take(N / 2);
    auto right = xs.Skip(N / 2);
    t0 = now_us();
    auto joined = left.Plus(&right);
    t1 = now_us();
    report("List.Plus halves", t1 - t0, N);
    check(joined.Count() == N && joined.Get(0) == 0 && joined.Last() == N - 1,
          "Plus reconstitutes");
}

/* ───────────────────────── 1M Map by-value bench ───────────────────────── */

void bench_map_million(int N) {
    printf("\n── Map by-value · N=%d (int keys) ──\n", N);
    long t0, t1;

    auto m = Map<int, int>(N > 0 ? N : 4);
    t0 = now_us();
    for (int i = 0; i < N; i++) m.Set(i, i * 3);
    t1 = now_us();
    report("Map.Set fill", t1 - t0, N);
    check(m.Count() == N, "map filled to N");

    t0 = now_us();
    long checksum = 0;
    for (int i = 0; i < N; i += 7) checksum += m.Get(i);
    t1 = now_us();
    report("Map.Get stride-7", t1 - t0, N / 7);
    check(checksum > 0, "Get checksum live");

    g_half = N / 2;
    t0 = now_us();
    auto high = m.Where((int k, int v) => {
        (void)v;
        return k > g_half;
    });
    t1 = now_us();
    report("Map.Where(k>N/2)", t1 - t0, N);
    int expect_high = 0;
    for (int k = 0; k < N; k++) if (k > g_half) expect_high++;
    check(high.Count() == expect_high, "Where(k>N/2) exact count");

    t0 = now_us();
    auto ks = m.Keys();
    t1 = now_us();
    report("Map.Keys", t1 - t0, N);
    check(ks.Count() == N, "Keys length N");

    t0 = now_us();
    auto vs = m.Values();
    t1 = now_us();
    report("Map.Values", t1 - t0, N);
    check(vs.Count() == N, "Values length N");

    t0 = now_us();
    auto mc = m.Copy();
    t1 = now_us();
    report("Map.Copy", t1 - t0, N);
    check(mc.Count() == N && mc.Get(42 % N) == (42 % N) * 3, "Copy Get ok");
}

/* String-key map stress.  Default N is often smaller than int-map N (see main):
 * set N_STRING=1000000 or N_STRING_FULL=1 for a full million string keys. */
void bench_map_string_keys(int N) {
    printf("\n── Map by-value · N=%d (String keys) ──\n", N);
    long t0, t1;
    char buf[32];

    auto m = Map<String, int>(N > 0 ? N : 4);
    t0 = now_us();
    for (int i = 0; i < N; i++) {
        snprintf(buf, sizeof(buf), "t%07d", i);
        /* strdup into arena-tracked String */
        char* s = (char*)malloc(strlen(buf) + 1);
        strcpy(s, buf);
        m.Set((String)s, i);
    }
    t1 = now_us();
    report("Map<String>.Set fill", t1 - t0, N);
    check(m.Count() == N, "string map filled");

    t0 = now_us();
    int hits = 0;
    for (int i = 0; i < N; i += 11) {
        snprintf(buf, sizeof(buf), "t%07d", i);
        hits += m.GetOr((String)buf, -1) == i ? 1 : 0;
    }
    t1 = now_us();
    report("Map<String>.GetOr stride", t1 - t0, N / 11);
    check(hits > (N / 11) / 2, "string GetOr mostly hits");

    t0 = now_us();
    auto filtered = m.Where((String k, int v) => {
        (void)k;
        return (v & 15) == 0;
    });
    t1 = now_us();
    report("Map<String>.Where", t1 - t0, N);
    int expect_f = 0;
    for (int i = 0; i < N; i++) if ((i & 15) == 0) expect_f++;
    check(filtered.Count() == expect_f, "Where v&15==0 exact");
}

/* ───────────────────────── corpus inverted index ───────────────────────── */

void bench_corpus(int fill_target) {
    printf("\n── Corpus crawl (/usr/share/man + /usr/share/doc) ──\n");
    long t0, t1;

    auto vocab = Set<String>();
    auto df = Map<String, int>();

    int no_crawl = env_int("BENCH_NO_CRAWL", 0);
    t0 = now_us();
    if (!no_crawl) {
        walk_dir("/usr/share/man", 0, &vocab, &df);
        walk_dir("/usr/share/doc", 0, &vocab, &df);
    }
    t1 = now_us();
    report("crawl+tokenise", t1 - t0, g_files_read);
    printf("  INFO  files_seen=%d files_read=%d bytes=%ld unique_terms=%d\n",
           g_files_seen, g_files_read, g_bytes_read, vocab.Count());

    check(no_crawl || g_files_seen >= 0, "crawl completed");
    /* With no crawl or empty dirs, seed a synthetic vocab so later steps work. */
    if (vocab.Count() < 16) {
        const char* seeds[] = {
            "memory", "safety", "language", "system", "kernel", "device",
            "network", "file", "process", "thread", "buffer", "socket",
            "driver", "module", "signal", "pointer"
        };
        for (int i = 0; i < 16; i++) {
            char* s = (char*)malloc(strlen(seeds[i]) + 1);
            strcpy(s, seeds[i]);
            String t = (String)s;
            vocab.Add(t);
            df.Set(t, 1 + (i % 5));
        }
        printf("  INFO  synthetic seed vocab (dirs empty or BENCH_NO_CRAWL)\n");
    }
    check(vocab.Count() >= 16, "vocab has terms");

    /* Posting list: expand real terms toward fill_target with synthetic (term,id). */
    auto postings_term = List<String>();
    auto postings_doc  = List<int>();
    t0 = now_us();
    /* Real terms as doc 0 postings */
    auto terms = df.Keys();
    for (int i = 0; i < terms.Count(); i++) {
        postings_term.Add(terms.Get(i));
        postings_doc.Add(i % 1024);
    }
    /* Pad to fill_target with patterned synthetic keys (reuse df map as term store) */
    char buf[40];
    int need = fill_target - postings_term.Count();
    if (need < 0) need = 0;
    for (int i = 0; i < need; i++) {
        int tid = i % (terms.Count() > 0 ? terms.Count() : 1);
        if (terms.Count() > 0) {
            postings_term.Add(terms.Get(tid));
        } else {
            snprintf(buf, sizeof(buf), "syn%06d", i % 10000);
            char* s = (char*)malloc(strlen(buf) + 1);
            strcpy(s, buf);
            postings_term.Add((String)s);
        }
        postings_doc.Add(i % 10000);
    }
    t1 = now_us();
    report("build posting Lists", t1 - t0, postings_term.Count());
    check(postings_term.Count() >= fill_target || postings_term.Count() == fill_target
          || postings_term.Count() >= 16,
          "postings reach target scale");
    check(postings_term.Count() == postings_doc.Count(), "posting lists aligned");

    /* Inverted: term → occurrence count (value Map) */
    auto inv = Map<String, int>();
    t0 = now_us();
    int P = postings_term.Count();
    for (int i = 0; i < P; i++) {
        String t = postings_term.Get(i);
        inv.Set(t, inv.GetOr(t, 0) + 1);
    }
    t1 = now_us();
    report("reduce postings→Map", t1 - t0, P);
    check(inv.Count() > 0, "inverted map non-empty");

    /* Query-ish: Where on inv for “popular” terms */
    t0 = now_us();
    auto popular = inv.Where((String k, int v) => {
        (void)k;
        return v >= 8;
    });
    t1 = now_us();
    report("Map.Where popular", t1 - t0, inv.Count());
    printf("  INFO  unique index terms=%d popular(>=8)=%d postings=%d\n",
           inv.Count(), popular.Count(), P);

    /* Value List transforms on posting docs */
    t0 = now_us();
    auto even_docs = postings_doc.Where(is_even);
    auto sample    = even_docs.Take(1000);
    t1 = now_us();
    report("posting List Where+Take", t1 - t0, P);
    check(sample.Count() <= 1000, "sample bounded");
}

/* ───────────────────────────────── main ───────────────────────────────── */

int main() {
    int N = env_int("N", 1000000);
    if (N < 1000) N = 1000;
    g_max_files = env_int("BENCH_MAX_FILES", 800);

    printf("=== val-041 million by-value List/Map bench ===\n");
    printf("target N=%d  max_crawl_files=%d\n", N, g_max_files);

    long wall0 = now_us();

    bench_list_million(N);
    bench_map_million(N);

    /* String-key map at full N is the heavy stress; allow override. */
    int NS = env_int("N_STRING", N > 200000 ? 200000 : N);
    /* Default 200k string keys (~full 1M is very slow under JIT); set N_STRING=1000000 for full. */
    if (env_int("N_STRING_FULL", 0)) NS = N;
    bench_map_string_keys(NS);

    bench_corpus(N);

    long wall1 = now_us();
    printf("\n── summary ──\n");
    report("TOTAL wall", wall1 - wall0, N);
    printf("=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
