/* test-typed-forin.cy — typed for-in loop variables
 *
 *   for (String s in arr)        — element bound + coerced to String
 *   for (int n in arr)           — element bound + coerced to int
 *   for (T1 k, T2 v in d)        — two-var typed (key/index + value)
 *
 * Desugars to `for (auto __v in arr) { T s = __v; ... }`, reusing the existing
 * dict/array element coercion on assignment.
 */
#include <stdio.h>
#include <string.h>
#include "include/list.h"

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

int main() {
    printf("=== typed for-in ===\n\n");

    /* ── dict string array: `for (String s in ...)` ─────────────────────── */
    dict d = json("{\"tags\":[\"fever\",\"cough\",\"fatigue\"]}");
    int hits = 0, total_len = 0;
    for (String s in d.tags) {
        if (strcmp((char*)s, "cough") == 0) hits++;
        total_len += strlen((char*)s);
    }
    check(hits == 1,            "String s in dict array: value compares as string");
    check(total_len == 5+5+7,   "String s in dict array: all elements visited");

    /* ── dict int array: `for (int n in ...)` ───────────────────────────── */
    dict nums = json("{\"xs\":[10,20,30]}");
    int sum = 0;
    for (int n in nums.xs) sum += n;
    check(sum == 60,            "int n in dict array: elements unwrap to int");

    /* ── two-var typed: `for (int i, String s in ...)` ──────────────────── */
    int idx_sum = 0, slen = 0;
    for (int i, String s in d.tags) { idx_sum += i; slen += strlen((char*)s); }
    check(idx_sum == 3,         "two-var typed: indices 0+1+2");
    check(slen == 5+5+7,        "two-var typed: values bound as String");

    /* ── object two-var typed key: `for (String k, auto v in obj)` ──────── */
    dict obj = json("{\"a\":1,\"b\":2}");
    int keymask = 0;
    for (String k, auto v in obj) {
        if (strcmp((char*)k, "a") == 0) keymask |= 1;
        if (strcmp((char*)k, "b") == 0) keymask |= 2;
    }
    check(keymask == 3,         "object two-var: keys bound as String");

    /* ── plain C array still works with an explicit element type ────────── */
    int carr[] = {1, 2, 3, 4};
    int csum = 0;
    for (int v in carr) csum += v;
    check(csum == 10,           "int v in C array");

    /* ── List<T> (Count/Get protocol) with typed var ────────────────────── */
    List<int>* lst = new List<int>();
    lst->Add(5); lst->Add(7); lst->Add(9);
    int lsum = 0;
    for (int v in lst) lsum += v;
    check(lsum == 21,           "int v in List<int>");
    delete lst;

    /* ── normal for-loop is unaffected by the typed-for-in detection ────── */
    int n2 = 0;
    for (int i = 0; i < 5; i++) n2 += i;
    check(n2 == 10,             "regular for-loop still parses");

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
