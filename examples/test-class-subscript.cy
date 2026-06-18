/* test-class-subscript.c — test [] operator overloading on generic classes
 *
 * Exercises:
 *   1. list[i] read via Get(int) protocol
 *   2. list[i] = val write via Set(int, T) protocol
 *   3. Bracket subscript on List<String>
 *   4. Bracket subscript on List<double>
 *   5. Mixed bracket and method calls
 *
 * Usage:  ./bin/classyc examples/test-class-subscript.c -eg
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ─── Generic dynamic-array class (same as classy-generics.c) ──────────── */

class List<T> {
    T*  data;
    int length;
    int capacity;

    List() {
        this.length   = 0;
        this.capacity = 4;
        this.data     = (T*) malloc(sizeof(T) * this.capacity);
    }

    List(int initialCapacity) {
        this.length   = 0;
        this.capacity = initialCapacity > 0 ? initialCapacity : 4;
        this.data     = (T*) malloc(sizeof(T) * this.capacity);
    }

    ~List() {
        if (this.data) free((void*) this.data);
    }

    int Count()    { return this.length; }
    int Capacity() { return this.capacity; }
    int IsEmpty()  { return this.length == 0; }

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

    T First() { return this.data[0]; }
    T Last()  { return this.data[this.length - 1]; }
};

/* ─── Test harness ──────────────────────────────────────────────────────── */

int passed;
int failed;

void check(int cond, char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else       { printf("  FAIL  %s\n", label); failed++; }
}

/* ─── main ──────────────────────────────────────────────────────────────── */

int main() {
    printf("=== [] operator overloading test suite ===\n\n");
    passed = 0; failed = 0;

    /* ── 1. List<int> bracket read ──────────────────────────────────────── */
    printf("-- 1. List<int> bracket read --\n");

    List<int>* nums = new List<int>();
    nums->Add(10);
    nums->Add(20);
    nums->Add(30);
    nums->Add(40);
    nums->Add(50);

    check(nums[0] == 10, "1a  nums[0] == 10");
    check(nums[1] == 20, "1b  nums[1] == 20");
    check(nums[4] == 50, "1c  nums[4] == 50");

    /* Index via variable */
    int idx = 2;
    check(nums[idx] == 30, "1d  nums[idx] == 30");

    /* ── 2. List<int> bracket write ─────────────────────────────────────── */
    printf("\n-- 2. List<int> bracket write --\n");

    nums[0] = 100;
    check(nums[0] == 100, "2a  nums[0] = 100");

    nums[2] = 999;
    check(nums[2] == 999, "2b  nums[2] = 999");
    check(nums[1] == 20,  "2c  nums[1] still 20 (unaffected)");
    check(nums[3] == 40,  "2d  nums[3] still 40 (unaffected)");

    /* Write via variable index */
    int wi = 4;
    nums[wi] = 500;
    check(nums[wi] == 500, "2e  nums[wi] = 500");

    delete nums;

    /* ── 3. List<String> bracket subscript ──────────────────────────────── */
    printf("\n-- 3. List<String> bracket subscript --\n");

    List<String>* words = new List<String>();
    words->Add("hello");
    words->Add("world");
    words->Add("foo");

    check(strcmp(words[0], "hello") == 0, "3a  words[0] == 'hello'");
    check(strcmp(words[1], "world") == 0, "3b  words[1] == 'world'");
    check(strcmp(words[2], "foo")   == 0, "3c  words[2] == 'foo'");

    words[1] = "planet";
    check(strcmp(words[1], "planet") == 0, "3d  words[1] = 'planet'");

    delete words;

    /* ── 4. List<double> bracket subscript ──────────────────────────────── */
    printf("\n-- 4. List<double> bracket subscript --\n");

    List<double>* vals = new List<double>();
    vals->Add(1.5);
    vals->Add(2.7);
    vals->Add(3.14);

    check(vals[0] > 1.4 && vals[0] < 1.6, "4a  vals[0] ≈ 1.5");
    check(vals[2] > 3.13 && vals[2] < 3.15, "4b  vals[2] ≈ 3.14");

    vals[1] = 99.9;
    check(vals[1] > 99.8 && vals[1] < 100.0, "4c  vals[1] = 99.9");

    delete vals;

    /* ── 5. Mixed bracket and method calls ──────────────────────────────── */
    printf("\n-- 5. Mixed bracket + method calls --\n");

    List<int>* mix = new List<int>{ 5, 10, 15, 20, 25 };
    check(mix->Count() == 5, "5a  Count == 5");
    check(mix[0] == 5,       "5b  mix[0] == 5 (bracket read)");
    check(mix->Get(0) == 5,  "5c  mix->Get(0) == 5 (method read)");

    /* Bracket write, then method read */
    mix[0] = 99;
    check(mix->Get(0) == 99, "5d  bracket write, method read: Get(0) == 99");

    /* Method write, then bracket read */
    mix->Set(1, 88);
    check(mix[1] == 88,      "5e  method write, bracket read: mix[1] == 88");

    /* Bracket in expression context */
    int sum = mix[0] + mix[1] + mix[2];
    check(sum == 99 + 88 + 15, "5f  bracket in expression: sum == 202");

    /* Bracket in loop */
    int total = 0;
    for (int i = 0; i < mix->Count(); i++)
        total = total + mix[i];
    check(total == 99 + 88 + 15 + 20 + 25, "5g  bracket in loop: total == 247");

    delete mix;

    /* ── Summary ────────────────────────────────────────────────────────── */
    printf("\n=== results: %d passed, %d failed ===\n", passed, failed);
    return failed;
}
