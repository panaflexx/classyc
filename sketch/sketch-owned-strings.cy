/* sketch-owned-strings.cy — value-semantic String fields (Option D).
 *
 * A `String` stored in a class field is owned by the object: assignment copies
 * it into a private heap buffer, and the destructor frees it.  No GC, static,
 * low runtime — strings live and die with their object, C#/Java-style.
 *
 * Run:  ./bin/classyc -g -I include sketch/sketch-owned-strings.cy -eg
 * AOT:  bash classyc-aot.sh -g -I include sketch/sketch-owned-strings.cy -o /tmp/sos
 *       valgrind --leak-check=full /tmp/sos
 */
#include <stdio.h>
#include <string.h>
#include "list.h"

int passed = 0, failed = 0;
void check(int cond, const char* label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

int user_dtors = 0;

class User {
    int    id;
    String name;     /* owned by the object: copied in, freed in ~User */
    String role;
    User(int id, String name, String role) {
        this.id = id;
        this.name = name;       /* takes a private copy */
        this.role = role;
    }
    ~User() { user_dtors++; }   /* compiler also frees name/role here */
};

/* Build a user whose name is a freshly-allocated (arena) String. */
User* makeUser(int id, String first, String last) {
    return new User(id, first + " " + last, id == 1 ? "admin" : "viewer");
}

int main() {
    printf("=== value-semantic String fields ===\n\n");

    /* (1) literal-sourced String field, read back after construction */
    {
        owned auto u = new User(1, "Alice", "admin");
        check(strcmp(u->name, "Alice") == 0, "(1) literal name stored");
        check(strcmp(u->role, "admin") == 0, "(1) literal role stored");
    }
    check(user_dtors == 1, "(1) user freed once");

    /* (2) heap (concatenated) String field survives the producing scope */
    {
        owned auto u = makeUser(2, "Bob", "Okafor");   // name = "Bob Okafor" (heap)
        check(strcmp(u->name, "Bob Okafor") == 0, "(2) concatenated name owned by object");
        check(strcmp(u->role, "viewer") == 0, "(2) role correct");
    }
    check(user_dtors == 2, "(2) user freed once (owned String reclaimed)");

    /* (3) reassigning a String field frees the old buffer (no leak) */
    {
        owned auto u = new User(3, "temp", "viewer");
        u->name = "renamed";                 // old "temp" copy freed, new copy owned
        u->name = u->name + "-again";        // self-derived reassign: old freed, new owned
        check(strcmp(u->name, "renamed-again") == 0, "(3) reassigned field reads correctly");
    }
    check(user_dtors == 3, "(3) user freed once after reassignments");

    /* (4) a list of owned users, each carrying owned Strings, all auto-freed */
    user_dtors = 0;
    {
        owned auto users = new List<User*>().owns();
        for (int i = 1; i <= 50; i++)
            users->Add(makeUser(i, "User", (String)"#" + i));   // 50 heap-named users
        check(users->Count() == 50, "(4) list holds 50 users");
        check(strcmp(users->Get(0)->name, "User #1") == 0, "(4) element name intact");
        check(strcmp(users->Get(49)->name, "User #50") == 0, "(4) last element name intact");
    }   /* list auto-deleted -> deletes 50 users -> frees 100 owned Strings */
    check(user_dtors == 50, "(4) all 50 users freed");

    printf("\n  user_dtors=%d\n", user_dtors);
    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
