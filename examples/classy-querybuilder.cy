/* Exercises the QueryBuilder<T> feature merged into include/sqlite.h */

#include "sqlite.h"
#include <stdio.h>

class User {
    int    id;
    String name;
    int    age;
    int    active;
    User() {}
    void bindRow(dict r) {
        this->id     = (int)(long) r.id;
        this->name   = r.name;
        this->age    = (int)(long) r.age;
        this->active = (int)(long) r.active;
    }
};

class Product {
    int    id;
    String name;
    double price;
    Product() {}
    void bindRow(dict r) {
        this->id    = (int)(long) r.id;
        this->name  = r.name;
        this->price = (double) r.price;
    }
};

int main() {
    printf("=== QueryBuilder<T> (library) ===\n\n");

    Sqlite* db = Sqlite.open(":memory:");
    if (!db) { printf("open failed\n"); return 1; }
    defer delete db;

    db->execute("CREATE TABLE Users (id INTEGER PRIMARY KEY, name TEXT, age INT, active INT)");
    db->execute("CREATE TABLE Products (id INTEGER PRIMARY KEY, name TEXT, price REAL)");

    db->execute("INSERT INTO Users (name, age, active) VALUES (?,?,?)", "sii", "Alice", 25, 1);
    db->execute("INSERT INTO Users (name, age, active) VALUES (?,?,?)", "sii", "Bob", 17, 1);
    db->execute("INSERT INTO Users (name, age, active) VALUES (?,?,?)", "sii", "Charlie", 30, 0);
    db->execute("INSERT INTO Users (name, age, active) VALUES (?,?,?)", "sii", "Diana", 22, 1);
    db->execute("INSERT INTO Users (name, age, active) VALUES (?,?,?)", "sii", "Eve", 16, 0);

    db->execute("INSERT INTO Products (name, price) VALUES (?,?)", "sd", "Apple", 1.50);
    db->execute("INSERT INTO Products (name, price) VALUES (?,?)", "sd", "Banana", 0.75);
    db->execute("INSERT INTO Products (name, price) VALUES (?,?)", "sd", "Cherry", 3.00);

    printf("Adults (age > 18) ordered by name:\n");
    owned auto adults = new QueryBuilder<User>(db, "Users")
        .Where("age", ">", "18")
        .OrderBy("name")
        .ToList();
    defer delete adults;
    for (int i = 0; i < adults->Count(); i++) {
        User* u = adults->Get(i);
        printf("  - %s (age %d)\n", (char*)u->name, u->age);
    }

    printf("\nActive adults count: %d (expected 2)\n",
        new QueryBuilder<User>(db, "Users")
            .Where("age", ">", "18")
            .Where("active", "=", "1")
            .Count());

    printf("\nFirst adult by age:\n");
    User* first = new QueryBuilder<User>(db, "Users")
        .Where("age", ">", "18")
        .OrderBy("age")
        .FirstOrDefault();
    defer delete first;
    if (first) printf("  %s (age %d)\n", (char*)first->name, first->age);

    printf("\nAny minors? %d (expected 1)\n",
        new QueryBuilder<User>(db, "Users").Where("age", "<", "18").Any());

    printf("\nPagination (skip 1, take 2) by name:\n");
    owned auto page = new QueryBuilder<User>(db, "Users")
        .OrderBy("name").Skip(1).Take(2).ToList();
    defer delete page;
    for (int i = 0; i < page->Count(); i++)
        printf("  - %s\n", (char*)page->Get(i)->name);

    printf("\nCheap products (< $2.00) by price:\n");
    owned auto cheap = new QueryBuilder<Product>(db, "Products")
        .Where("price", "<", "2.00")
        .OrderBy("price")
        .ToList();
    defer delete cheap;
    for (int i = 0; i < cheap->Count(); i++) {
        Product* p = cheap->Get(i);
        printf("  - %s ($%.2f)\n", (char*)p->name, p->price);
    }

    printf("\n=== done ===\n");
    return 0;
}
