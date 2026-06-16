/* test-generic-ptr-args.c — test pointer type arguments in generics
 *
 * Exercises:
 *   1. List<char*> — list of C string pointers
 *   2. Pointer type arg mangling produces distinct specializations
 *
 * Usage:  ./bin/classyc examples/test-generic-ptr-args.c -eg
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ─── Minimal generic List for testing ─────────────────────────────────── */

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

    int Count()    { return this.length; }
    T Get(int index) { return this.data[index]; }

    void Set(int index, T item) {
        if (index >= 0 && index < this.length) this.data[index] = item;
    }

    void EnsureCapacity(int min) {
        if (min <= this.capacity) return;
        int newCap = this.capacity;
        while (newCap < min) newCap = newCap * 2;
        T* newData = (T*) malloc(sizeof(T) * newCap);
        for (int i = 0; i < this.length; i++) newData[i] = this.data[i];
        free((void*) this.data);
        this.data     = newData;
        this.capacity = newCap;
    }

    void Add(T item) {
        this->EnsureCapacity(this.length + 1);
        this.data[this.length] = item;
        this.length++;
    }
};

/* ─── A simple class for testing pointer storage ───────────────────────── */

class Point {
    int x, y;

    Point(int x, int y) { this.x = x; this.y = y; }
    ~Point() {}

    int sum() { return this.x + this.y; }
};

/* ─── Test harness ──────────────────────────────────────────────────────── */

int passed;
int failed;

void check(int cond, char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else       { printf("  FAIL  %s\n", label); failed++; }
}

int main() {
    printf("=== Pointer type argument test suite ===\n\n");
    passed = 0; failed = 0;

    /* ── 1. List<char*> — list of C string pointers ─────────────────────── */
    printf("-- 1. List<char*> --\n");

    List<char*>* strs = new List<char*>();
    char *a = "hello";
    char *b = "world";
    char *c = "foo";
    strs->Add(a);
    strs->Add(b);
    strs->Add(c);

    check(strs->Count() == 3, "1a  Count == 3");
    check(strcmp(strs[0], "hello") == 0, "1b  strs[0] == 'hello'");
    check(strcmp(strs[1], "world") == 0, "1c  strs[1] == 'world'");
    check(strcmp(strs[2], "foo")   == 0, "1d  strs[2] == 'foo'");

    strs[1] = "bar";
    check(strcmp(strs[1], "bar") == 0, "1e  strs[1] = 'bar' (bracket write)");

    delete strs;

    /* ── 2. Distinct specializations ────────────────────────────────────── */
    printf("\n-- 2. Distinct specializations --\n");

    /* List<int> and List<char*> are different types */
    List<int>* ints = new List<int>();
    ints->Add(42);
    check(ints[0] == 42, "2a  List<int>[0] == 42");

    List<char*>* ptrs = new List<char*>();
    ptrs->Add("test");
    check(strcmp(ptrs[0], "test") == 0, "2b  List<char*>[0] == 'test'");

    delete ints;
    delete ptrs;

    /* ── Summary ────────────────────────────────────────────────────────── */
    printf("\n=== results: %d passed, %d failed ===\n", passed, failed);
    return failed;
}
