/* Test 109: Array to List conversion with complex types */
#include <stdio.h>
#include "list.h"

class Item {
    String name;
    int value;
    Item(String n, int v) { this.name = n; this.value = v; }
    String toString() { return f"{this.name}:{this.value}"; }
};

int main() {
    // Array of structs to List
    Item items[] = {
        Item("a", 1),
        Item("b", 2),
        Item("c", 3)
    };

    auto list = items.ToList();
    printf("array to list: ");
    for (auto item in list) printf("%s ", item.toString());
    printf("\n");

    // Array of pointers to List
    Item* ptrs[] = { new Item* = { new Item("x", 10), new Item("y", 20) };
    auto ptrList = ptrs.ToList();
    printf("ptr array to list: ");
    for (auto p in ptrList) printf("%s ", p->toString());
    printf("\n");

    // Slice from array
    int nums[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    auto slice = nums[2:7];  // elements 2-6
    auto sliceList = slice.ToList();
    printf("slice to list: ");
    for (auto n in sliceList) printf("%d ", n);
    printf("\n");

    // String array
    String names[] = {"alice", "bob", "carol"};
    auto nameList = names.ToList();
    printf("string array: ");
    for (auto n in nameList) printf("%s ", n);
    printf("\n");

    return 0;
}
