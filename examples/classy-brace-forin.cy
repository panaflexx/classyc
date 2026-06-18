/* classy-brace-forin.c — brace-init + Count/Get for-in protocol tests
 *
 * ① Brace-init:  new T{e1, e2, ...}
 *    Sugar for: zero-arg constructor + one obj->Add(e) call per element.
 *    Duck-typed: any class with an Add method taking one argument.
 *
 * ② Iteration protocol:  for (auto x in coll)  /  for (auto i, x in coll)
 *    Any class with Count() and Get(int) methods is iterable.
 *
 * Usage:  ./bin/classyc examples/classy-brace-forin.c -eg
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ─── Generic dynamic-array class ────────────────────────────────────────── */

class List<T> {
    T*  data;
    int length;
    int capacity;

    List() {
        this.length   = 0;
        this.capacity = 4;
        this.data     = (T*) malloc(sizeof(T) * this.capacity);
    }

    ~List() {
        if (this.data) free((void*) this.data);
    }

    int Count() { return this.length; }

    void Add(T item) {
        if (this.length >= this.capacity) {
            int newCap = this.capacity * 2;
            T*  newData = (T*) malloc(sizeof(T) * newCap);
            for (int i = 0; i < this.length; i++) newData[i] = this.data[i];
            free((void*) this.data);
            this.data     = newData;
            this.capacity = newCap;
        }
        this.data[this.length] = item;
        this.length++;
    }

    T Get(int index) { return this.data[index]; }

    void Set(int index, T item) { this.data[index] = item; }
};

/* ─── Non-generic class with the same protocols ──────────────────────────── */

class IntBag {
    int items[16];
    int n;

    IntBag() { this.n = 0; }

    void Add(int v)  { this.items[this.n] = v; this.n++; }
    int  Count()     { return this.n; }
    int  Get(int i)  { return this.items[i]; }
};

/* ─── Test harness ────────────────────────────────────────────────────────── */

int passed;
int failed;

void check(int cond, char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else       { printf("  FAIL  %s\n", label); failed++; }
}

/* ─── main ────────────────────────────────────────────────────────────────── */

int main() {
    printf("=== brace-init + for-in protocol tests ===\n\n");
    passed = 0; failed = 0;

    /* ── ① Brace-init: List<String> ── */
    printf("-- brace-init List<String> --\n");
    List<String>* words = new List<String>{"hello", "world", "classyc"};
    check(words->Count() == 3,                   "1a  Count() == 3");
    check(strcmp(words->Get(0), "hello")   == 0, "1b  Get(0) == 'hello'");
    check(strcmp(words->Get(1), "world")   == 0, "1c  Get(1) == 'world'");
    check(strcmp(words->Get(2), "classyc") == 0, "1d  Get(2) == 'classyc'");

    /* ── ② auto + brace-init ── */
    printf("\n-- auto + brace-init --\n");
    auto nums = new List<int>{10, 20, 30, 40, 50};
    check(nums->Count() == 5,  "2a  Count() == 5");
    check(nums->Get(0) == 10,  "2b  Get(0) == 10");
    check(nums->Get(4) == 50,  "2c  Get(4) == 50");

    /* growth past initial capacity (4) via brace-init */
    auto grown = new List<int>{1, 2, 3, 4, 5, 6, 7, 8, 9};
    check(grown->Count() == 9, "2d  growth: Count() == 9");
    check(grown->Get(8) == 9,  "2e  growth: Get(8) == 9");
    delete grown;

    /* empty brace-init */
    auto empty = new List<int>{};
    check(empty->Count() == 0, "2f  empty {}: Count() == 0");
    delete empty;

    /* trailing comma */
    auto trail = new List<int>{7, 8, 9,};
    check(trail->Count() == 3 && trail->Get(2) == 9, "2g  trailing comma");
    delete trail;

    /* ── ③ for-in via Count/Get protocol ── */
    printf("\n-- for (auto x in list) --\n");
    int sum = 0;
    for (auto v in nums) sum += v;
    check(sum == 150, "3a  sum over List<int> == 150");

    int wlen = 0;
    for (auto w in words) wlen += strlen(w);
    check(wlen == 5 + 5 + 7, "3b  total strlen over List<String>");

    List<double>* ds = new List<double>{1.5, 2.5, 3.0};
    double dsum = 0.0;
    for (auto d in ds) dsum += d;
    check(dsum > 6.9 && dsum < 7.1, "3c  sum over List<double> ≈ 7.0");
    delete ds;

    /* ── ④ two-var indexed form ── */
    printf("\n-- for (auto i, x in list) --\n");
    int idx_sum = 0, weighted = 0;
    for (auto i, v in nums) {
        idx_sum  += i;
        weighted += i * v;
    }
    check(idx_sum == 0+1+2+3+4,                       "4a  index sum");
    check(weighted == 0*10 + 1*20 + 2*30 + 3*40 + 4*50, "4b  weighted sum");

    /* ── ⑤ break / continue inside for-in ── */
    printf("\n-- break/continue --\n");
    int until = 0;
    for (auto v in nums) {
        if (v == 40) break;
        until += v;
    }
    check(until == 10 + 20 + 30, "5a  break stops at 40");

    int odd_idx = 0;
    for (auto i, v in nums) {
        if (i % 2 == 0) continue;
        odd_idx += v;
    }
    check(odd_idx == 20 + 40, "5b  continue skips even indices");

    /* ── ⑥ non-generic class with the same protocols ── */
    printf("\n-- non-generic IntBag --\n");
    IntBag* bag = new IntBag{3, 1, 4, 1, 5};
    check(bag->Count() == 5, "6a  brace-init on plain class");
    int bsum = 0;
    for (auto v in bag) bsum += v;
    check(bsum == 14, "6b  for-in on plain class");
    delete bag;

    /* ── ⑦ nested for-in ── */
    printf("\n-- nested for-in --\n");
    int pairs = 0;
    for (auto a in nums)
        for (auto b in nums)
            if (a < b) pairs++;
    check(pairs == 10, "7a  nested loops: 10 ordered pairs");

    delete words;
    delete nums;

    printf("\n=== results: %d passed, %d failed ===\n", passed, failed);
    return failed;
}
