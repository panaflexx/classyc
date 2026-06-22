/* Test 128: Complex type inference with auto and generics */
#include <stdio.h>
#include "list.h"
#include "map.h"

class Box<T> {
    T value;
    Box(T v) { this.value = v; }
    T get() { return this.value; }
};

auto makeBox(int x) { return new Box<int>(x); }
auto makeBoxStr(String s) { return new Box<String>(s); }

int main() {
    // Auto with generic class
    auto b1 = makeBox(42);
    auto b2 = makeBoxStr("hello");
    printf("auto box int: %d\n", b1->get());
    printf("auto box str: %s\n", b2->get());

    // Auto with List
    auto list = new List<int>({1, 2, 3});
    auto filtered = list->Filter((int x) => x > 1);
    auto mapped = filtered->Map((int x) => x * 10);
    printf("auto list chain: ");
    for (auto x in mapped) printf("%d ", x);
    printf("\n");

    // Auto with Map
    auto map = new Map<String, int>();
    map["one"] = 1;
    map["two"] = 2;
    auto keys = map->Keys();
    auto values = map->Values();
    printf("auto map keys: ");
    for (auto k in keys) printf("%s ", k);
    printf("\n");
    printf("auto map values: ");
    for (auto v in values) printf("%d ", v);
    printf("\n");

    // Nested auto
    auto nested = new List<List<int>*>({
        new List<int>({1, 2}),
        new List<int>({3, 4})
    });
    for (auto inner in nested) {
        for (auto x in inner) printf("%d ", x);
        printf("\n");
    }

    // Auto with lambda
    auto adder = (int x) => (int y) => x + y;
    auto add5 = adder(5);
    printf("auto lambda: %d\n", add5(10));

    return 0;
}
