/* val-005-dict-arena.cy — validates the arena-backed dict memory model.
 *
 *   auto d = new dict(bytes);   // arena-backed dict (single block + root)
 *   auto d = new dict();        // default arena size
 *   delete d / defer delete d;  // frees the whole arena in one shot
 *
 * The arena holds the root + initial capacity; values added by dot/set may
 * still heap-allocate (documented future work). delete frees everything.
 *
 * Run:  ./bin/classyc -g -I include cy-validate/val-005-dict-arena.cy -eg
 */
#include <stdio.h>
#include <string.h>

dict dict_create_object();
void dict_destroy(dict d);

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

int main() {
    printf("=== val-005 dict arena ===\n\n");

    /* 1. explicit-size arena + defer delete */
    {
        auto d = new dict(1024 * 64);
        defer delete d;
        check(d != 0, "1a new dict(64k) non-null");
        d.language = "classy";
        d.version  = 1;
        d.server   = { "host": "localhost", "port": 8080 };
        check(strcmp((char*)d.language, "classy") == 0, "1b string field set");
        check((int)d.version == 1,                      "1c int field set");
        check((int)d.server.port == 8080,               "1d nested field in arena dict");
    }   /* defer delete d runs here (arena freed) */
    check(1, "1e scope exited cleanly after defer delete");

    /* 2. default-size arena */
    {
        auto d = new dict();
        defer delete d;
        check(d != 0, "2a new dict() default size non-null");
        d.count = 100;
        check((int)d.count == 100, "2b populated default-size arena dict");
    }

    /* 3. explicit delete (no defer) */
    {
        auto t = new dict(4096);
        t.foo = "bar";
        check(strcmp((char*)t.foo, "bar") == 0, "3a populated before explicit delete");
        delete t;
        check(1, "3b explicit delete completed without crash");
    }

    /* 4. arena dict behaves like a plain dict */
    {
        dict plain = dict_create_object();
        plain.x = 42;
        auto arena = new dict(8192);
        defer delete arena;
        arena.x = 42;
        check((int)plain.x == (int)arena.x, "4a plain and arena dict same value semantics");
        dict_destroy(plain);
    }

    /* 5. stress: create+destroy many arenas (no growth / leak) */
    for (int i = 0; i < 2000; i++) {
        auto a = new dict(2048);
        a.i = i;
        a.tag = "row";
        delete a;
    }
    check(1, "5. 2000 arena create/delete cycles survived");

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
