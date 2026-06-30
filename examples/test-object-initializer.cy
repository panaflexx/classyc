/* test-object-initializer.cy — C/C++23-style object initializers on `new`.
 *
 *   new T(ctor-args?) { .field = value, .field = value }
 *
 * Each `.name = expr` designator runs as a post-construction store on the
 * freshly-allocated object, reusing normal field-assignment semantics:
 * value-semantic String ownership, scalar coercion, and by-value aggregate
 * copy.  The leading `.` distinguishes this from the collection brace-init
 * `new List<int>{1,2,3}` (which calls Add per element).
 *
 * Run:  ./bin/classyc examples/test-object-initializer.cy -eg
 */
#include <stdio.h>
#include <string.h>

int passed = 0, failed = 0;
void check(int c, const char* l) {
    if (c) { printf("  PASS  %s\n", l); passed++; }
    else   { printf("  FAIL  %s\n", l); failed++; }
}

int dtors = 0;

class Point { int x; int y; };

class Person {
    int     id;
    String  first;
    String  last;
    int      age;
    double   score;
    Point    home;        /* by-value aggregate member */

    Person()          { this->id = -1; this->age = -1; }
    Person(int start) { this->id = start; this->age = -1; }
    ~Person()         { dtors = dtors + 1; }
};

int main() {
    printf("=== object initializers ===\n\n");

    /* No ctor args: zero-arg ctor runs, then designators fill fields. */
    Person* a = new Person() {
        .first = "Ada",
        .last  = "Lovelace",
        .age   = 36,
        .score = 9.5,
    };
    check(a->id == -1, "zero-arg ctor still runs (id == -1)");
    check(strcmp((char*)a->first, "Ada") == 0, "String field .first");
    check(strcmp((char*)a->last, "Lovelace") == 0, "String field .last");
    check(a->age == 36, "int field .age");
    check(a->score > 9.49 && a->score < 9.51, "double field .score");

    /* Ctor args + initializer: ctor sets id, designators set the rest. */
    Person* b = new Person(7) {
        .first = "Alan",
        .age   = 41,
        .home  = (Point){ .x = 3, .y = 4 },
    };
    check(b->id == 7, "ctor arg sets id == 7");
    check(strcmp((char*)b->first, "Alan") == 0, "field after ctor arg");
    check(b->age == 41, "int field with ctor arg");
    check(b->home.x == 3 && b->home.y == 4, "by-value aggregate field .home");

    /* Owned Strings are freed by delete (no leak / double-free). */
    int before = dtors;
    delete a;
    delete b;
    check(dtors - before == 2, "destructors run on delete");

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
