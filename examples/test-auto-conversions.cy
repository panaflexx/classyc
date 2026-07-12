/* test-auto-conversions.cy — automagical List/Map <-> dict/JSON converters */
#include <stdio.h>
#include <string.h>
#include "list.h"
#include "map.h"

int passed = 0, failed = 0;
void check(int c, const char* l) {
    if (c) { printf("  PASS  %s\n", l); passed++; }
    else   { printf("  FAIL  %s\n", l); failed++; }
}

int main() {
    printf("=== automagical conversions ===\n\n");

    /* ── List<T>.FromJson(dict array) — reverse of ToJsonArray ──────────── */
    dict d = json("{\"xs\":[10,20,30],\"tags\":[\"a\",\"bb\",\"ccc\"]}");

    auto xs = List<int>.FromJson(d.xs);
    check(xs->Count() == 3 && xs->Get(0) == 10 && xs->Get(2) == 30,
          "List<int>.FromJson(dict array)");

    auto tags = List<String>.FromJson(d.tags);
    check(tags->Count() == 3 && strcmp(tags->Get(1), "bb") == 0,
          "List<String>.FromJson(dict array)");

    /* round-trip: dict array -> List -> JSON */
    check(strcmp(xs->ToJson(), "[10,20,30]") == 0, "List<int>.ToJson()");
    check(strcmp(tags->ToJson(), "[\"a\",\"bb\",\"ccc\"]") == 0, "List<String>.ToJson()");

    auto ds = new List<double>{ 1.5, 2.5 };
    check(strcmp(ds->ToJson(), "[1.5,2.5]") == 0, "List<double>.ToJson()");

    /* ── Map<String,V> conversions ──────────────────────────────────────── */
    Map<String, int>* ages = new Map<String, int>();
    defer delete ages;
    ages->Set("ada", 36);
    ages->Set("alan", 41);

    dict ad = ages->ToDict();
    check(strcmp((char*)json(ad), "{\"ada\":36,\"alan\":41}") == 0, "Map<String,int>.ToDict()");
    check(strcmp(ages->ToJson(), "{\"ada\":36,\"alan\":41}") == 0, "Map<String,int>.ToJson()");

    Map<String, String>* env = new Map<String, String>();
    defer delete env;
    env->Set("role", "admin");
    env->Set("tier", "gold");
    check(strcmp(env->ToJson(), "{\"role\":\"admin\",\"tier\":\"gold\"}") == 0,
          "Map<String,String>.ToJson()");

    /* Keys() / Values() -> List<T> */
    auto ks = ages->Keys();
    auto vs = ages->Values();
    check(ks.Count() == 2 && strcmp(ks.Get(0), "ada") == 0, "Map.Keys() -> List<K>");
    check(vs.Count() == 2 && vs.Get(0) == 36, "Map.Values() -> List<V>");

    /* Values() feeds straight back into a List converter */
    check(strcmp(vs.ToJson(), "[36,41]") == 0, "Map.Values()->ToJson() chains");

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
