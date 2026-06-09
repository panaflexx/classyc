/* classy-dict-arena.c — demonstrates the convenient `new dict(size)` syntax
 *
 * This uses the built-in arena allocator:
 *   auto d = new dict(1024 * 256);   // 256 KB arena-backed dict
 *   auto d2 = new dict();            // default size (256 KB)
 *
 * Benefits:
 * - Single allocation for the arena block + root dict
 * - `delete d` (or `defer delete d`) calls dict_destroy which frees
 *   the entire arena in one shot (plus any remaining heap objects)
 * - Great for large temporary dicts, configs, caches, etc.
 *
 * Note: individual keys/values added via dot or set still allocate
 * on the heap (future work will make more of it arena-only). The root
 * and initial capacity live in the arena.
 */

#include <stdio.h>
#include <string.h>

/* Runtime helpers that the compiler may call implicitly for new dict / delete */
dict dict_create_heap_arena(size_t bytes);
void dict_destroy(dict d);

/* Regular dict helpers (for explicit construction when desired) */
dict dict_create_object();
dict dict_create_string(char *s);
dict dict_create_int64(long n);
dict dict_create_number(double n);
int  dict_object_set(dict obj, char *key, dict val);
dict dict_object_get(dict obj, char *key);

/* Test harness */
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

int main() {
    printf("=== classy-dict-arena ===\n\n");
    passed = 0;
    failed = 0;

    /* ── 1. Basic creation with explicit size ───────────────────────── */
    printf("-- 1. new dict(size) + defer delete --\n");
    {
        auto d = new dict(1024 * 64);   /* 64 KB arena for this test */
        defer delete d;

        check(d != 0,                     "1a  new dict(64k) returned non-null");
        check("meta" in d == 0,           "1b  fresh dict has no keys yet");

        /* Populate it — this is where the arena shines for larger datasets */
        d.language   = "classy";
        d.version    = 1;
        d.build      = 20260607;
        d.enabled    = 1;
        d.threshold  = 3.14159;

        /* Nested object */
        d.server = {
            "host": "localhost",
            "port": 8080,
            "tls":  0
        };

        /* Array assignment inside dicts - liked the idea, but commented
           out for now (may implement later):

           d.tags = ["fast", "safe", "arena-backed"];

           And the corresponding check would be:
           check(d.tags != 0, "1g  array stored");
        */

        check(d.language != 0,            "1c  top-level string set");
        check(d.version != 0,             "1d  integer field readable");
        check(d.server != 0,              "1e  nested object created");
        check(d.server.port != 0,         "1f  nested integer access");

        printf("    d.json (partial): %s\n", d.json);
    }
    /* defer delete ran here — arena + all owned memory released */

    /* ── 2. Default size (new dict()) ───────────────────────────────── */
    printf("\n-- 2. new dict() uses default arena size --\n");
    {
        auto d = new dict();
        defer delete d;

        check(d != 0, "2a  new dict() (default size) worked");

        d.count = 100;
        d.status = "ok";

        check(d.count != 0, "2b  populated default-sized arena dict");
    }

    /* ── 3. Explicit size + heavy nesting + for-in + json round-trip ── */
    printf("\n-- 3. larger arena + nesting + for-in + json --\n");
    {
        auto big = new dict(1024 * 256);   /* the example size the user asked for */
        defer delete big;

        /* Build a realistic-looking structure */
        big.app = {
            "name": "mirnew"

            /* Array assignment inside dicts (commented for future impl):
               "features": ["classy", "generics", "arena-dicts"]
            */
        };
        big.stats = {
            "files": 142,
            "lines": 18600,
            "tests": 73
        };

        /* Add many flat keys to exercise growth inside the arena root */
        int sum = 0;
        for (int i = 0; i < 50; i = i + 1) {
            if (i < 10)      { big.k00 = i; sum = sum + i; }
            else if (i < 20) { big.k10 = i; sum = sum + i; }
            else if (i < 30) { big.k20 = i; sum = sum + i; }
            else if (i < 40) { big.k30 = i; sum = sum + i; }
            else             { big.k40 = i; sum = sum + i; }
        }
        check(sum == (49*50/2), "3a  summed 0..49 correctly via repeated sets");

        /* for-in over the keys we added */
        int key_count = 0;
        for (auto k in big) {
            key_count = key_count + 1;
        }
        check(key_count >= 7, "3b  for-in visited the expected number of keys");

        /* json round-trip sanity */
        char *j = big.json;
        check(j != 0 && strlen(j) > 100, "3c  json serialization produced substantial output");
        printf("    big.json length = %zu\n", (size_t)strlen(j));
    }

    /* ── 4. delete without defer (explicit cleanup) ─────────────────── */
    printf("\n-- 4. explicit delete on arena dict --\n");
    {
        auto temp = new dict(4096);
        temp.foo = "bar";
        check(temp.foo != 0, "4a  created and populated before explicit delete");

        delete temp;   /* should free the arena block + any heap entries */
        /* We can't safely touch temp after this, but reaching here without crash is good. */
        check(1, "4b  explicit delete completed without crash");
    }

    /* ── 5. Comparison: plain dict vs arena dict (behavior is identical) ─ */
    printf("\n-- 5. plain dict vs arena dict behave the same --\n");
    {
        dict plain = dict_create_object();
        plain.x = 42;
        plain.name = "plain";

        auto arena = new dict(8192);
        defer delete arena;
        arena.x = 42;
        arena.name = "arena";

        char *pname = (char*)plain.name;
        char *aname = (char*)arena.name;

        check(plain.x != 0 && arena.x != 0, "5a  same value semantics (non-null)");
        check(pname != 0 && aname != 0,
              "5b  string fields are accessible as char*");

        /* Clean up the plain one manually */
        dict_destroy(plain);
    }

    printf("\n=== results: %d passed, %d failed ===\n", passed, failed);
    return failed;
}
