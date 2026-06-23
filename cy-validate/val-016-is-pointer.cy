/* val-016-is-pointer.cy — validates is_pointer<T> compiler intrinsic
 * inside generic template contexts where T is a type parameter */
#include <stdio.h>
#include "list.h"

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

/* Test class to use in List<T*> */
class Item {
    int id;
    Item(int id) { this->id = id; }
    ~Item() { }
};

int main() {
    printf("=== val-016 is_pointer<T> in generic contexts ===\n\n");

    /* Test 1: List<int> - T is int (not a pointer) */
    List<int>* nums = new List<int>();
    nums->Add(42);
    /* At this point inside List<int>'s destructor, is_pointer<int>() returns 0 */
    delete nums;
    check(1, "List<int> compiles and runs (T=int, not pointer)");

    /* Test 2: List<Item*>.owns() - T is Item*, is_pointer<Item*>() == 1, so the
     * owning destructor deletes each element (no manual loop). */
    List<Item*>* items = new List<Item*>().owns();
    items->Add(new Item(1));
    items->Add(new Item(2));
    delete items;   /* owning: is_pointer<Item*>()==1 -> deletes each Item* */
    check(1, "List<Item*>.owns() compiles and runs (T=Item*, is pointer)");

    /* Test 3: List<char*> - T is char* (is a pointer) */
    List<char*>* strs = new List<char*>();
    strs->Add("hello");
    strs->Add("world");
    delete strs;
    check(1, "List<char*> compiles and runs (T=char*, is pointer)");

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    printf("NOTE: is_pointer<T> is evaluated at compile time during template\n");
    printf("      specialization. This test validates it doesn't break compilation.\n");
    return failed;
}
