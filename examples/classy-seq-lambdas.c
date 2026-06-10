/* classy-seq-lambdas.c — filter/map/reduce/count lambda methods.
 *
 * Sequences:
 *   - plain C11 arrays:      int nums[] = {1,2,3,4,5,6};
 *   - slices (filter/map results — stack-allocated, bound with `auto`)
 *   - classes with the Count()/Get(int) protocol (e.g. List<T>)
 *
 * Lambdas:
 *   n => expr                 single untyped param (type inferred)
 *   (acc, n) => expr          untyped param list (types inferred)
 *   (int x) => expr           typed lambda (parse-time, also works)
 *   named_function            any function with the right signature
 *
 * Results:
 *   filter -> slice of the element type;  map -> slice of the return type;
 *   reduce -> type of the initial value;  count() -> element count.
 *   Slices are indexable, for-in iterable, and chainable.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int passed;
int failed;

void check(int cond, char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

/* Named predicate/transform used as callbacks */
int is_positive(int x) { return x > 0; }
int triple(int x) { return x * 3; }

/* Generic dynamic-array class with the Count()/Get(int) protocol */
class List<T> {
    T*  data;
    int length;
    int capacity;

    List() {
        this.length   = 0;
        this.capacity = 4;
        this.data     = (T*) malloc(sizeof(T) * this.capacity);
    }
    ~List() { if (this.data) free((void*) this.data); }

    int Count() { return this.length; }
    T Get(int index) { return this.data[index]; }

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
};

int main() {
    printf("=== sequence lambda methods test suite ===\n\n");
    passed = 0; failed = 0;

    /* ---- the headline example ---- */
    printf("-- int array: filter / map / reduce --\n");

    int nums[] = {1, 2, 3, 4, 5, 6};

    auto evens   = nums.filter(n => n % 2 == 0);
    auto doubled = nums.map(n => n * 2);
    int  sum     = nums.reduce(0, (acc, n) => acc + n);

    check(evens.count() == 3,            "1a  evens.count() == 3");
    check(evens[0] == 2,                 "1b  evens[0] == 2");
    check(evens[1] == 4,                 "1c  evens[1] == 4");
    check(evens[2] == 6,                 "1d  evens[2] == 6");
    check(doubled.count() == 6,          "1e  doubled.count() == 6");
    check(doubled[0] == 2,               "1f  doubled[0] == 2");
    check(doubled[5] == 12,              "1g  doubled[5] == 12");
    check(sum == 21,                     "1h  reduce sum == 21");

    /* ---- count() on a plain array ---- */
    check(nums.count() == 6,             "2a  nums.count() == 6");

    /* ---- chaining ---- */
    printf("\n-- chaining --\n");

    auto big = nums.filter(n => n > 2).map(n => n * 10);
    check(big.count() == 4,              "3a  chained count == 4");
    check(big[0] == 30 && big[3] == 60,  "3b  chained values");

    int chained_sum = nums.filter(n => n % 2 == 1).reduce(0, (a, b) => a + b);
    check(chained_sum == 9,              "3c  filter+reduce == 1+3+5");

    /* ---- for-in over a slice ---- */
    printf("\n-- for-in over slices --\n");

    int fsum = 0;
    for (auto x in evens) fsum += x;
    check(fsum == 12,                    "4a  for-in over filter result");

    int isum = 0, icount = 0;
    for (auto i, x in doubled) { isum += i; icount = icount + (x > 0); }
    check(isum == 15 && icount == 6,     "4b  two-var for-in over map result");

    /* ---- typed lambdas and named functions as callbacks ---- */
    printf("\n-- typed lambdas / named functions --\n");

    int mixed[] = {-2, 5, -7, 9, 1};
    auto pos1 = mixed.filter((int x) => x > 0);  /* typed lambda */
    auto pos2 = mixed.filter(is_positive);       /* named function */
    auto trip = mixed.map(triple);

    check(pos1.count() == 3,             "5a  typed lambda filter");
    check(pos2.count() == 3,             "5b  named function filter");
    check(pos2[0] == 5 && pos2[2] == 1,  "5c  named function filter values");
    check(trip[0] == -6 && trip[3] == 27,"5d  named function map");

    /* ---- map can change the element type ---- */
    printf("\n-- map with type change --\n");

    auto halves = nums.map(n => n / 2.0);    /* int -> double */
    check(halves[0] > 0.49 && halves[0] < 0.51, "6a  int -> double map");
    check(halves[5] > 2.99 && halves[5] < 3.01, "6b  int -> double map");

    double weights[] = {1.5, 2.5, 3.0};
    double total = weights.reduce(0.0, (acc, w) => acc + w);
    check(total > 6.99 && total < 7.01,  "6c  double reduce");

    auto rounded = weights.map(w => (int) (w + 0.5));  /* double -> int */
    check(rounded[0] == 2 && rounded[2] == 3, "6d  double -> int map");

    /* ---- String arrays (char* elements) ---- */
    printf("\n-- String arrays --\n");

    String words[] = {"hi", "hello", "hey", "salutations"};

	for(auto w in words)
		printf(f"{w}\n");

    auto longish = words.filter(w => strlen(w) > 2);
    check(longish.count() == 3,          "7a  string filter count");
    check(strcmp(longish[0], "hello") == 0, "7b  string filter [0]");
    check(strcmp(longish[2], "salutations") == 0, "7c  string filter [2]");

    auto lens = words.map(w => (int) strlen(w));
    check(lens[0] == 2 && lens[3] == 11, "7d  string -> length map");

    int total_len = words.reduce(0, (acc, w) => acc + (int) strlen(w));
    check(total_len == 21,               "7e  string length reduce");

	printf(f"words[1]={words[1]}\n");

    /* ---- List<T>: Count()/Get(int) protocol ---- */
    printf("\n-- List<int> --\n");

    List<int>* lst = new List<int>();
    defer delete lst;
    for (int i = 1; i <= 8; i++) lst->Add(i);

    auto big_items = lst->filter(n => n > 5);
    auto squares   = lst->map(n => n * n);
    int  lsum      = lst->reduce(0, (acc, n) => acc + n);

    check(lst->count() == 8,             "8a  list count()");
    check(big_items.count() == 3,        "8b  list filter count");
    check(big_items[0] == 6 && big_items[2] == 8, "8c  list filter values");
    check(squares.count() == 8,          "8d  list map count");
    check(squares[7] == 64,              "8e  list map values");
    check(lsum == 36,                    "8f  list reduce sum");

    /* slice from a List chains like any other slice */
    int lsum2 = lst->filter(n => n % 2 == 0).reduce(0, (a, b) => a + b);
    check(lsum2 == 20,                   "8g  list filter+reduce == 2+4+6+8");

    /* ---- slices are writable ---- */
    printf("\n-- slice element assignment --\n");
    evens[0] = 42;
    check(evens[0] == 42,                "9a  slice element write");

    printf("\n=== results: %d passed, %d failed ===\n", passed, failed);
    return failed;
}
