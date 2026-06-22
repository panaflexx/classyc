/* Test 110: Complex for-in with dict, string, and custom iterables */
#include <stdio.h>

class Range {
    int start, end, step;
    Range(int s, int e, int st) { this.start = s; this.end = e; this.step = st; }

    int Count() { return (this.end - this.start + this.step - 1) / this.step; }
    int Get(int i) { return this.start + i * this.step; }
};

int main() {
    // Dict iteration - key only
    dict d = { "a": 1, "b": 2, "c": 3 };
    printf("dict keys: ");
    for (auto k in d) printf("%s ", k);
    printf("\n");

    // Dict iteration - key, value
    printf("dict pairs: ");
    for (auto k, v in d) printf("%s=%d ", k, v);
    printf("\n");

    // String iteration (chars)
    String s = "abc";
    printf("string chars: ");
    for (auto c in s) printf("%c ", c);
    printf("\n");

    // Custom iterable (Range)
    Range r(0, 10, 2);
    printf("range: ");
    for (auto n in r) printf("%d ", n);
    printf("\n");

    // Nested for-in
    dict nested = { "x": [1,2], "y": [3,4] };
    for (auto k, v in nested) {
        printf("%s: ", k);
        for (auto n in v) printf("%d ", n);
        printf("\n");
    }

    return 0;
}
