/* Test 096: Generic class with multiple type parameters */
#include <stdio.h>

class Pair<T, U> {
    T first;
    U second;

    Pair(T f, U s) { this.first = f; this.second = s; }

    void print() {
        printf("Pair: %s, %s\n", this.first, this.second);
    }
};

int main() {
    Pair<String, int> p1 = new Pair<String, int>("hello", 42);
    Pair<int, String> p2 = new Pair<int, String>(100, "world");
    p1.print();
    p2.print();
    return 0;
}
