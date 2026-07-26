/* val-015-collection-byval-dtor.cy — validates that by-value class elements
 * stored in Set<T> and Map<K,V> have their destructors run when the owning
 * collection is deleted (the __destroy loop in include/set.h / include/map.h).
 *
 * Mirrors the List<T> ownership guarantee already covered for List in
 * examples/test-list-byval.cy and GENERICSMEM.md ("owned storage dies with
 * the owner").
 *
 * Run:  ./bin/classyc -g -I include cy-validate/val-015-collection-byval-dtor.cy -eg
 */
#include <stdio.h>
#include "set.h"
#include "map.h"

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

int tag_dtors = 0;
[[copyable_no_release]] /* counting dtor only — no owned resource */
class Tag {
    int id;
    Tag(int id) { this->id = id; }
    ~Tag() { tag_dtors = tag_dtors + 1; }
    int getId() { return this->id; }
};

int item_dtors = 0;
[[copyable_no_release]]
class Item {
    int id;
    Item(int id) { this->id = id; }
    ~Item() { item_dtors = item_dtors + 1; }
    int getId() { return this->id; }
};

int key_dtors = 0;
[[copyable_no_release]]
class Key {
    int id;
    Key(int id) { this->id = id; }
    ~Key() { key_dtors = key_dtors + 1; }
    int getId() { return this->id; }
};

int main() {
    printf("=== val-015 collection by-value element dtors ===\n\n");

    /* Set<T> owns its by-value elements: delete runs each element dtor. */
    Set<Tag>* s = new Set<Tag>();
    Tag a = Tag(1);
    Tag b = Tag(2);
    Tag c = Tag(3);
    s->Add(a);
    s->Add(b);
    s->Add(c);
    check(s->Count() == 3, "Set<Tag> holds 3 by-value elements");

    int set_before = tag_dtors;
    delete s;
    check(tag_dtors - set_before == 3, "Set<Tag> runs 3 element dtors on delete");

    /* Map<K,V> owns its by-value values: delete runs each value dtor. */
    Map<int, Item>* m = new Map<int, Item>();
    Item x = Item(10);
    Item y = Item(20);
    Item z = Item(30);
    m->Set(1, x);
    m->Set(2, y);
    m->Set(3, z);
    check(m->Count() == 3, "Map<int,Item> holds 3 by-value values");

    int map_before = item_dtors;
    delete m;
    check(item_dtors - map_before == 3, "Map<int,Item> runs 3 value dtors on delete");

    /* Map<K,V> also owns its by-value KEYS: delete runs each key dtor. */
    Map<Key, int>* km = new Map<Key, int>();
    Key k1 = Key(1);
    Key k2 = Key(2);
    km->Set(k1, 100);
    km->Set(k2, 200);
    check(km->Count() == 2, "Map<Key,int> holds 2 by-value keys");

    int key_before = key_dtors;
    delete km;
    check(key_dtors - key_before == 2, "Map<Key,int> runs 2 key dtors on delete");

    /* NOTE: stack/value forms of generic collections exist now
     * (`auto m = Map<K,V>();`, `b = move a` — see val-038/val-039/val-040);
     * this file still exercises the heap `delete` path for element-dtor
     * accounting. */

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
