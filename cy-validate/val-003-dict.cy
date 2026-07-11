/* val-003-dict.cy — validates the heterogeneous `dict` (JSON-like) feature.
 *
 * Covers: object init, nested init, dot read/write, dynamic key creation,
 * [ ] subscript, "k" in d, for-in (keys and key,value), json() parse/serialize
 * round-trip, and the d.json() shorthand.
 *
 * NOTE (see SHORTCOMINGS.md A4): printing a dict value with %s needs json() or a
 * (char*) cast on a known-string leaf; numeric leaves must be read as scalars.
 *
 * Run:  ./bin/classyc -g -I include cy-validate/val-003-dict.cy -eg
 */
#include <stdio.h>
#include <string.h>

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

int main() {
    printf("=== val-003 dict ===\n\n");

    /* init + nested init */
    dict cfg = {
        "server": { "host": "localhost", "port": 8080 },
        "debug": 1,
        "timeout": 30.5
    };
    check(cfg != 0,              "object literal creates non-null dict");
    check(cfg.server != 0,       "nested object present");
    check("server" in cfg,       "\"server\" in cfg");
    check(!("ghost" in cfg),     "absent key not in cfg");

    /* dot read — string leaf as char* (workaround for A4) */
    check(strcmp((char*)cfg.server.host, "localhost") == 0, "chained dot read (string leaf)");
    /* numeric leaf must be read as scalar, NOT as dict (A4/C2) */
    check((int)cfg.server.port == 8080, "numeric leaf read as scalar");
    check((int)cfg.debug == 1,          "top-level int leaf");

    /* dot write + dynamic key creation */
    cfg.retries = 5;
    check((int)cfg.retries == 5,        "dynamic key creation via dot-assign");
    cfg.server.host = "example.com";
    check(strcmp((char*)cfg.server.host, "example.com") == 0, "nested dot-write");

    /* subscript read/write */
    cfg["motto"] = "carpe diem";
    check(strcmp((char*)cfg["motto"], "carpe diem") == 0, "subscript string round-trip");
    cfg["lucky"] = 7;
    check((int)cfg["lucky"] == 7,       "subscript int round-trip");

    /* for-in: keys only */
    int kcount = 0;
    for (auto k in cfg) kcount++;
    check(kcount >= 5, "for-in (keys) visits all top-level keys");

    /* for-in: key,value — stringify each value with json() */
    int seen_debug = 0;
    for (auto k, v in cfg) {
        if (strcmp(k, "debug") == 0 && strcmp((char*)json(v), "1") == 0) seen_debug = 1;
    }
    check(seen_debug == 1, "for-in (key,value) yields correct value via json()");

    /* json() parse + round-trip */
    dict parsed = json("{\"name\":\"bob\",\"age\":42}");
    check(parsed != 0 && "name" in parsed && "age" in parsed, "json(str) parses object");
    char *ser = json(parsed);
    dict reparsed = json(ser);
    check("name" in reparsed && "age" in reparsed, "json round-trip preserves keys");

    /* d.json() serialize method (not a key — keys use bare d.json) */
    dict tiny = { "a": 1, "b": 2 };
    char *j = tiny.json();
    check(j != 0 && strlen(j) > 5, "d.json() serializes");
    printf("  tiny.json() = %s\n", j);

    /* Bare d.json is a field named "json", not serialization */
    dict mixed = { "a": 1, "json": "payload" };
    check(strcmp((char*)mixed.json, "payload") == 0, "bare d.json is key access");
    check(strstr(mixed.json(), "payload") != 0, "d.json() still serializes whole object");

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
