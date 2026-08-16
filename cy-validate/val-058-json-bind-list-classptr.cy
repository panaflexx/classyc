/* val-058-json-bind-list-classptr.cy — typed JSON binding into List<ClassPtr>*.
 *
 * Previously (Phase 2/C4 in SHORTCOMINGS.md): a pointer-to-class element type
 * for a bound collection (e.g. `List<User*> *users` filled from a JSON array
 * of objects) matched `scalar_type_p()` (true for every TM_PTR) before ever
 * reaching the dedicated "pointer-to-class" branch, so each element was
 * built by unwrapping the nested-object DictValue*'s union payload as if it
 * held a raw scalar — Add() was still called the right number of times (so
 * Count() looked correct) but every element pointer was garbage, crashing on
 * first use.
 *
 * Fix: gen_dict_bind_collection_field now special-cases TM_PTR-to-TM_CLASS
 * element types *before* the generic scalar/pointer branch: it allocates and
 * zero-fills the pointee, recurses gen_dict_bind_into to fill its fields from
 * the nested dict object, and passes the real pointer to Add(T).
 *
 * Run: ./bin/classyc -g -I include cy-validate/val-058-json-bind-list-classptr.cy -eg
 */
#include <stdio.h>
#include "list.h"

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

class User {
    String name;
    int age;
};

class Config {
    List<User*> *users;
};

/* Two-level nesting: a class holding a List<ClassPtr>* whose element class
   itself has a nested-by-value member, to exercise the recursive bind. */
class Address {
    String city;
    int zip;
};
class Employee {
    String name;
    Address addr;
};
class Company {
    List<Employee*> *staff;
};

int main() {
    printf("=== val-058 JSON bind into List<ClassPtr>* ===\n\n");

    {
        dict d = json("{\"users\":[{\"name\":\"alice\",\"age\":30},{\"name\":\"bob\",\"age\":25}]}");
        Config c = (Config) d;
        check(c.users != 0, "bind produced a non-NULL List<User*>*");
        check(c.users->Count() == 2, "Count() == 2");
        User *u0 = c.users->Get(0);
        User *u1 = c.users->Get(1);
        check(u0 != 0 && u1 != 0, "elements are non-NULL pointers");
        check(u0->name.equals("alice") && u0->age == 30, "element 0 fields correct");
        check(u1->name.equals("bob") && u1->age == 25, "element 1 fields correct");
        {
            int i = 0, name_ok = 1, age_sum = 0;
            for (auto u in c.users) {
                if (i == 0 && !u->name.equals("alice")) name_ok = 0;
                if (i == 1 && !u->name.equals("bob")) name_ok = 0;
                age_sum += u->age;
                i++;
            }
            check(name_ok && age_sum == 55, "for-in over List<User*>* sees real objects");
        }
        delete c.users;   /* must not crash */
        check(1, "delete c.users did not crash");
    }

    /* Lenient (T)? with a missing key: field stays NULL, no throw. */
    {
        dict e = json("{}");
        Config c2 = (Config)? e;
        check(c2.users == 0, "lenient bind with missing key leaves List<User*>* NULL");
    }

    /* Strict (T) with a missing key: throws KeyException, does not corrupt. */
    {
        dict e = json("{}");
        int caught = 0;
        try {
            Config c3 = (Config) e;
        } catch (KeyException ex) {
            caught = 1;
        }
        check(caught, "strict bind with missing key throws KeyException");
    }

    /* Two levels of nesting: List<Employee*>* where Employee has a
       by-value nested Address. */
    {
        dict d = json("{\"staff\":[{\"name\":\"cy\",\"addr\":{\"city\":\"nyc\",\"zip\":10001}}]}");
        Company co = (Company) d;
        check(co.staff != 0 && co.staff->Count() == 1, "nested List<Employee*>* bound, Count() == 1");
        Employee *e0 = co.staff->Get(0);
        check(e0 != 0 && e0->name.equals("cy"), "Employee element name correct");
        check(e0->addr.city.equals("nyc") && e0->addr.zip == 10001,
              "Employee's by-value nested Address bound correctly through the pointer element");
        delete co.staff;
    }

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
