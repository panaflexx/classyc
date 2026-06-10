/* classy-generics.c — generic class List<T> demonstration
 *
 * class List<T> is monomorphized at parse time for each distinct type argument:
 *   List<String>  →  __generic_List_String  (a concrete class)
 *   List<int>     →  __generic_List_int
 *
 * Usage:  ./bin/classyc examples/classy-generics.c -eg
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

/* ─── Test harness ────────────────────────────────────────────────────────── */

int passed;
int failed;

void check(int cond, char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else       { printf("  FAIL  %s\n", label); failed++; }
}

/* ─── main ────────────────────────────────────────────────────────────────── */

int main() {
    printf("=== List<T> generics test ===\n\n");
    passed = 0; failed = 0;

    /* ── List<String> ── */
    printf("-- List<String> --\n");
    List<String>* words = new List<String>();
    words->Add("hello");
    words->Add("world");
    words->Add("classyc");

    check(words->Count() == 3,                   "1a  Count() == 3");
    check(strcmp(words->Get(0), "hello")   == 0, "1b  Get(0) == 'hello'");
    check(strcmp(words->Get(1), "world")   == 0, "1c  Get(1) == 'world'");
    check(strcmp(words->Get(2), "classyc") == 0, "1d  Get(2) == 'classyc'");

    words->Set(1, "earth");
    check(strcmp(words->Get(1), "earth") == 0,   "1e  Set(1,'earth'), Get(1)");

    delete words;

    /* ── List<int> ── */
    printf("\n-- List<int> --\n");
    List<int>* nums = new List<int>();
    for (int i = 0; i < 10; i++) nums->Add(i * i);

    check(nums->Count() == 10,  "2a  Count() == 10");
    check(nums->Get(0) == 0,    "2b  Get(0) == 0");
    check(nums->Get(3) == 9,    "2c  Get(3) == 9");
    check(nums->Get(9) == 81,   "2d  Get(9) == 81");

    /* trigger capacity growth */
    for (int i = 10; i < 20; i++) nums->Add(i);
    check(nums->Count() == 20,  "2e  Count() == 20 after growth");
    check(nums->Get(15) == 15,  "2f  Get(15) after growth");

    int sum = 0;
    for (int i = 0; i < nums->Count(); i++) sum += nums->Get(i);
    /* 0+1+4+9+16+25+36+49+64+81 + 10..19 */
    check(sum == 285 + (10+11+12+13+14+15+16+17+18+19), "2g  sum correct");

    delete nums;

    /* ── List<double> ── */
    printf("\n-- List<double> --\n");
    List<double>* ds = new List<double>();
    ds->Add(1.5);
    ds->Add(2.5);
    ds->Add(3.0);
    check(ds->Count() == 3,                        "3a  Count() == 3");
    check(ds->Get(0) > 1.4 && ds->Get(0) < 1.6,   "3b  Get(0) ≈ 1.5");
    check(ds->Get(2) > 2.9 && ds->Get(2) < 3.1,   "3c  Get(2) ≈ 3.0");
    delete ds;

    /* ── Two distinct specializations co-exist ── */
    printf("\n-- Two specializations co-exist --\n");
    List<String>* s2 = new List<String>();
    List<int>*    n2 = new List<int>();
    s2->Add("alpha");
    n2->Add(99);
    s2->Add("beta");
    n2->Add(100);
    check(s2->Count() == 2 && n2->Count() == 2,  "4a  independent counts");
    check(strcmp(s2->Get(0), "alpha") == 0,       "4b  s2.Get(0)");
    check(n2->Get(1) == 100,                      "4c  n2.Get(1)");
    delete s2;
    delete n2;

    printf("\n=== results: %d passed, %d failed ===\n", passed, failed);
    return failed;
}
