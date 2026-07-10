/* val-030-nameof-typeof.cy — nameof/typeof reflection
 *
 *  - nameof<T>() / typeof<T>() type-level strings (typeof keeps pointer stars)
 *  - expr.nameof() / expr.typeof() method forms
 *  - Enum constants: apple.nameof() == "apple"
 *  - Enum variables: reverse-map runtime value to enumerator spelling
 *  - Free nameof(id) for identifiers
 *  - Generics: nameof inside specialized List/Map still works (existing path)
 *
 * Run:  ./bin/classyc -g -I include cy-validate/val-030-nameof-typeof.cy -eg
 */
#include <stdio.h>
#include <string.h>
#include "list.h"

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

enum Fruit { apple = 1, banana = 2, cherry = 3 };

class Box {
    int v;
    Box(int v) { this->v = v; }
};

/* Generic body uses nameof<T> — same intrinsic List.ToJsonArray relies on. */
class Named<T> {
    static const char* TypeName() { return nameof<T>(); }
    static const char* TypeOf()  { return typeof<T>(); }
};

int main() {
    printf("=== val-030 nameof / typeof ===\n\n");

    printf("-- type-level --\n");
    check(strcmp(nameof<int>(), "int") == 0,       "1a  nameof<int>()");
    check(strcmp(nameof<int*>(), "int") == 0,      "1b  nameof<int*>() strips *");
    check(strcmp(typeof<int*>(), "int*") == 0,     "1c  typeof<int*>() keeps *");
    check(strcmp(nameof<String>(), "String") == 0, "1d  nameof<String>()");
    check(strcmp(nameof<Fruit>(), "Fruit") == 0,   "1e  nameof<Fruit>()");
    check(strcmp(typeof<Box>(), "Box") == 0,       "1f  typeof<Box>()");
    check(strcmp(typeof<Box*>(), "Box*") == 0,     "1g  typeof<Box*>()");

    printf("\n-- enum constant methods --\n");
    check(strcmp(apple.nameof(), "apple") == 0,    "2a  apple.nameof()");
    check(strcmp(banana.nameof(), "banana") == 0,  "2b  banana.nameof()");
    check(strcmp(apple.typeof(), "Fruit") == 0,    "2c  apple.typeof() is Fruit");
    check(strcmp(nameof(cherry), "cherry") == 0,   "2d  nameof(cherry)");

    printf("\n-- enum variable reverse map --\n");
    enum Fruit f = cherry;
    check(strcmp(f.nameof(), "cherry") == 0,       "3a  f.nameof() -> cherry");
    check(strcmp(f.typeof(), "Fruit") == 0,        "3b  f.typeof() -> Fruit");
    f = apple;
    check(strcmp(f.nameof(), "apple") == 0,        "3c  reassigned f.nameof()");

    printf("\n-- scalar id nameof --\n");
    int x = 42;
    check(strcmp(x.nameof(), "x") == 0,            "4a  x.nameof() is spelling");
    check(strcmp(x.typeof(), "int") == 0,          "4b  x.typeof() is int");

    printf("\n-- generic specialize nameof/typeof --\n");
    check(strcmp(Named<int>.TypeName(), "int") == 0,      "5a  Named<int>.TypeName");
    check(strcmp(Named<String>.TypeName(), "String") == 0,"5b  Named<String>.TypeName");
    check(strcmp(Named<int*>.TypeOf(), "int*") == 0,      "5c  Named<int*>.TypeOf keeps *");

    printf("\n-- List still works with nameof (ToJson) --\n");
    List<int>* xs = new List<int>{1, 2, 3};
    defer delete xs;
    String j = xs->ToJson();
    check(strcmp((char*)j, "[1,2,3]") == 0,        "6a  List.ToJson via nameof");

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
