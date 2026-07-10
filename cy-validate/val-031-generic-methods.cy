/* val-031-generic-methods.cy — method type parameters (Select<U>).
 *
 *   List<U>* Select<U>(U(*fn)(T))
 *   xs->Select<String>(fn) / xs->Select(fn)  with U inferred from fn return type
 *
 * Run:  ./bin/classyc -g -I include cy-validate/val-031-generic-methods.cy -eg
 */
#include <stdio.h>
#include <string.h>
#include "list.h"

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

int times2(int x) { return x * 2; }
int plus1(int x) { return x + 1; }

class User {
    int id;
    String name;
    User(int id, String name) { this->id = id; this->name = name; }
    ~User() {}
    int getId() { return this->id; }
};

int user_id(User* u) { return u->getId(); }
String user_name(User* u) { return u->name; }

int main() {
    printf("=== val-031 generic methods (Select<U>) ===\n\n");

    List<int>* xs = new List<int>{1, 2, 3};
    defer delete xs;

    printf("-- explicit type arg --\n");
    List<int>* d = xs->Select<int>(times2);
    defer delete d;
    check(d->Count() == 3, "1a  Select<int> count");
    check(d->Get(0) == 2 && d->Get(1) == 4 && d->Get(2) == 6, "1b  Select<int> values");

    printf("\n-- inference from fn return type --\n");
    List<int>* e = xs->Select(plus1);
    defer delete e;
    check(e->Count() == 3 && e->Get(0) == 2 && e->Get(2) == 4, "2a  inferred U=int");

    printf("\n-- T* → scalar / String --\n");
    List<User*>* users = new List<User*>().owns();
    defer delete users;
    users->Add(new User(10, "Ada"));
    users->Add(new User(20, "Bob"));

    List<int>* ids = users->Select<int>(user_id);
    defer delete ids;
    check(ids->Count() == 2 && ids->Get(0) == 10 && ids->Get(1) == 20, "3a  Select user ids");

    List<String>* names = users->Select<String>(user_name);
    defer delete names;
    check(names->Count() == 2, "3b  Select<String> count");
    check(strcmp((char*)names->Get(0), "Ada") == 0, "3c  Select<String> Ada");
    check(strcmp((char*)names->Get(1), "Bob") == 0, "3d  Select<String> Bob");

    printf("\n-- SelectString compat --\n");
    List<String>* names2 = users->SelectString(user_name);
    defer delete names2;
    check(names2->Count() == 2 && strcmp((char*)names2->Get(0), "Ada") == 0,
          "4a  SelectString still works");

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
