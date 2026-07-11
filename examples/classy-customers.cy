/* classy-customers.cy — typed JSON ingestion + in-memory query demo.
 *
 * Reads examples/customers.json (an array of ~50 customer records), uses the
 * typed JSON binder `(Customer)? d` to materialize each record into a strongly
 * typed `Customer` class, indexes them by id into a `Map<int, Customer*>`, and
 * then runs a handful of "database-style" queries against the map.
 *
 * Highlights
 *   - File.read_text reads the whole file in one call (include/file.h).
 *   - json(...) parses the array into a tagged dict.
 *   - d.length()  / for-in over a dict array  (the C1/C3 features).
 *   - (Customer)? customer_dict   — typed bind cast: missing fields default to
 *     zero/NULL rather than throwing.  Lets us tolerate slightly irregular
 *     records without try/catch around every iteration.
 *   - Map<int, Customer*> with `.ownsValues()`: each record lives in a
 *     heap-allocated Customer and releasing the map frees them all in one shot.
 *     (The Map<K,V> for-in protocol requires V to be scalar or pointer, so a
 *     by-value Customer would not be iterable; the pointer storage trivially
 *     sidesteps that constraint and is what production code would use anyway.)
 *
 * Run:
 *   ./bin/classyc -g -I include examples/classy-customers.cy -eg
 *
 * Memory model (the C#-like "just works" path): because Customer/Address are
 * *classes*, the typed bind gives their `String` fields VALUE SEMANTICS — the
 * binder copies each string into a private heap buffer the object owns, and the
 * object's destructor frees them (recursing into the nested Address).  So the
 * parsing dict is just a transient: we `delete` it right after ingest and the
 * customers keep their own string copies.  The map binding is `owned`, so the
 * whole database (the Map's table *and* every Customer, with all their strings)
 * is released automatically at the end of main — no manual `free`, no leaks.
 */
#include <stdio.h>
#include <string.h>
#include "file.h"
#include "map.h"

/* ── Schema ─────────────────────────────────────────────────────────
   These mirror customers.json() one-for-one.  We use `class` (not plain struct)
   so the `String` fields get VALUE SEMANTICS: the typed bind copies each
   string into a buffer the object owns, and the auto-generated destructor
   frees them when the Customer is destroyed (recursing into the nested
   Address).  That is what lets us throw the parsing dict away after ingest. */
class Address {
    String street;
    String city;
    String state;
    String zipCode;
    String country;
};

class Customer {
    int     id;
    String  firstName;
    String  lastName;
    String  fullName;
    String  email;
    String  phone;
    Address address;          /* nested by-value class field (its strings are
                                 freed with the Customer, see destructor cascade) */
    String  dateOfBirth;
    String  sex;
    String  race;
    String  eyeColor;
    int     heightInches;
    int     weightLbs;
    String  occupation;
};

/* ── Ingest ────────────────────────────────────────────────────────────────
   Read the file, parse JSON, bind each element, insert into the map keyed by
   the customer's `id`.  Returns the populated map; the caller owns it (we use
   an `owned` binding in main).  Prints a progress line for visibility. */
Map<int, Customer*>* load_customers(char *path) {
    char *text = File.read_text(path);
    if (!text) {
        printf("FATAL: could not read %s\n", path);
        return 0;
    }

    /* Parse the whole file into a dict.  customers.json() is a bare JSON array,
       so the result has DICT_ARRAY at its root.  The dict is a transient: once
       we have bound every record (which copies the strings into the owning
       Customer objects) we delete it here — the customers keep their own
       copies, so nothing dangles. */
    dict d = json(text);
    free(text);
    defer delete d;

    /* `.ownsValues()` flips the value-ownership flag: releasing the map runs
       each Customer's destructor (freeing its owned String fields, including
       the nested Address) in addition to releasing the Map's own table. */
    Map<int, Customer*>* db = (new Map<int, Customer*>())->ownsValues();

    /* for-in over a dict array binds (index, element-as-dict). */
    int n = 0;
    for (auto i, rec in d) {
        /* Lenient cast: any field missing in a record gets the zero default
           rather than aborting the load.  Switch to `(Customer) rec` for a
           strict ingest that throws KeyException on the first missing key.

           Bind into a heap-allocated Customer so the map can store a pointer
           (which the for-in protocol accepts).  Binding into a class copies
           every String field into a buffer the Customer owns, so the parsing
           dict no longer has to outlive it. */
        Customer* c = new Customer();
        *c = (Customer)? rec;
        db->Set(c->id, c);
        n = n + 1;
    }
    printf("loaded %d customers from %s\n", n, path);
    return db;
}

/* ── Queries (the "database" face of the demo) ───────────────────────────── */

/* Q1 — point lookup by id.  Map<K,V>->Contains is the membership check; the
   subscript form `db[id]` gives the stored pointer. */
void q_lookup(Map<int, Customer*>* db, int id) {
    printf("\n[Q] lookup id=%d\n", id);
    if (!db->Contains(id)) {
        printf("  (no such customer)\n");
        return;
    }
    Customer* c = db[id];
    printf("  #%d  %s, %s — %s, %s %s\n",
           c->id, (char*)c->lastName, (char*)c->firstName,
           (char*)c->address.city, (char*)c->address.state,
           (char*)c->address.zipCode);
    printf("       %s  |  %s  |  %s\n",
           (char*)c->occupation, (char*)c->email, (char*)c->phone);
}

/* Q2 — predicate filter: list every customer in a given state. */
void q_by_state(Map<int, Customer*>* db, char *state) {
    printf("\n[Q] customers in state=%s\n", state);
    int hits = 0;
    for (auto id, c in db) {
        if (c->address.state != NULL && strcmp((char*)c->address.state, state) == 0) {
            printf("  #%-3d %s %s  (%s)\n",
                   c->id, (char*)c->firstName, (char*)c->lastName,
                   (char*)c->address.city);
            hits = hits + 1;
        }
    }
    if (hits == 0) printf("  (none)\n");
}

/* Q3 — find by last name (linear scan; the API is "DB-like" not "DB-fast"). */
void q_by_last_name(Map<int, Customer*>* db, char *last) {
    printf("\n[Q] customers with lastName=%s\n", last);
    int hits = 0;
    for (auto id, c in db) {
        if (c->lastName != NULL && strcmp((char*)c->lastName, last) == 0) {
            printf("  #%-3d %s %s  — %s\n",
                   c->id, (char*)c->firstName, (char*)c->lastName,
                   (char*)c->occupation);
            hits = hits + 1;
        }
    }
    if (hits == 0) printf("  (none)\n");
}

/* Q4 — group-by occupation, count, print insertion-order.  Demonstrates
   composing two maps: Map<String, int> as a tally over the customers map. */
void q_count_by_occupation(Map<int, Customer*>* db) {
    printf("\n[Q] count by occupation (top of insertion order)\n");
    Map<String, int>* tally = new Map<String, int>();
    defer delete tally;

    for (auto id, c in db) {
        String key;
        if (c->occupation != NULL) key = c->occupation;
        else                       key = "(unknown)";
        if (tally->Contains(key))
            tally[key] = tally[key] + 1;
        else
            tally->Set(key, 1);
    }

    /* Iterate the tally in insertion order. */
    for (auto occ, n in tally) {
        printf("  %3d  %s\n", n, (char*)occ);
    }
}

/* Q5 — simple aggregate over scalar fields.  Skips any record with a missing
   value (which surfaces as zero from the lenient bind). */
void q_average_height(Map<int, Customer*>* db) {
    long sum = 0;
    int  used = 0;
    for (auto id, c in db) {
        if (c->heightInches > 0) {
            sum = sum + (long)c->heightInches;
            used = used + 1;
        }
    }
    if (used == 0) {
        printf("\n[Q] average height: (no data)\n");
        return;
    }
    /* Integer math is fine here — the population is tens of customers, not
       millions; one decimal place of precision via x10 trick. */
    long avg10 = (sum * 10) / used;
    printf("\n[Q] average height: %ld.%ld inches over %d customers\n",
           avg10 / 10, avg10 % 10, used);
}

/* Q6 — top-K heaviest customers, just to show we can use the data
   numerically.  No sort here (the Map iteration order is insertion); we do a
   tiny in-place selection of the top-3. */
void q_top3_weight(Map<int, Customer*>* db) {
    printf("\n[Q] top 3 heaviest customers\n");
    int top_id[3]   = {0, 0, 0};
    int top_wt[3]   = {-1, -1, -1};

    for (auto id, c in db) {
        int w = c->weightLbs;
        /* Insertion-sort into the size-3 leaderboard. */
        for (int slot = 0; slot < 3; slot++) {
            if (w > top_wt[slot]) {
                /* shift down */
                for (int s = 2; s > slot; s--) {
                    top_wt[s] = top_wt[s - 1];
                    top_id[s] = top_id[s - 1];
                }
                top_wt[slot] = w;
                top_id[slot] = c->id;
                break;
            }
        }
    }

    for (int i = 0; i < 3; i++) {
        if (top_id[i] == 0) continue;
        Customer* c = db[top_id[i]];
        printf("  %d lbs  — #%d %s %s (%s)\n",
               c->weightLbs, c->id, (char*)c->firstName, (char*)c->lastName,
               (char*)c->occupation);
    }
}

int main(void) {
    printf("=== ClassyC customers demo ===\n");

    /* `owned`: the map (its table plus every Customer it owns, plus all of
       their value-semantic String fields) is released automatically at the
       end of main — no `defer delete`, no manual free, no leaks. */
    owned auto db = load_customers("examples/customers.json");
    if (db == NULL) return 1;

    printf("\ndatabase: %d unique ids\n", (int)db->Count());

    /* Run the queries */
    q_lookup(db, 5);
    q_lookup(db, 999);             /* a deliberate miss */
    q_by_state(db, "CA");
    q_by_last_name(db, "Smith");
    q_count_by_occupation(db);
    q_average_height(db);
    q_top3_weight(db);

    printf("\ndone.\n");
    return 0;
}
