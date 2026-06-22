/* Test 117: Dict/Set/List with custom class keys and values */
#include <stdio.h>
#include "map.h"
#include "set.h"

class Person {
    String name;
    int age;
    Person(String n, int a) { this.name = n; this.age = a; }
    String toString() { return f"{this.name}({this.age})"; }
};

int main() {
    // Map with String key, Person* value
    Map<String, Person*>* people = new Map<String, Person*>();
    people["alice"] = new Person("Alice", 30);
    people["bob"] = new Person("Bob", 25);
    people["carol"] = new Person("Carol", 35);

    printf("Map iteration:\n");
    for (auto name, person in people) {
        printf("  %s -> %s\n", name, person->toString());
    }

    // Set of Person* (by identity)
    Set<Person*>* personSet = new Set<Person*>();
    personSet->Add(people["alice"]);
    personSet->Add(people["bob"]);
    personSet->Add(people["carol"]);
    personSet->Add(people["alice"]);  // duplicate by identity

    printf("Set size (by identity): %d\n", personSet->Count());
    for (auto p in personSet) printf("  %s\n", p->toString());

    // List of Person*
    List<Person*>* personList = new List<Person*>();
    personList->Add(people["alice"]);
    personList->Add(people["bob"]);
    personList->Add(people["carol"]);

    auto adults = personList->Filter((Person* p) => p->age >= 30);
    printf("Adults:\n");
    for (auto p in adults) printf("  %s\n", p->toString());

    return 0;
}
