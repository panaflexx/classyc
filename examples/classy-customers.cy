/* classy-customers.cy — typed JSON ingestion + in-memory query demo.
 *
 * Reads examples/customers.json (an array of ~50 customer records), uses the
 * typed JSON binder `(Customer)? d` to materialize each record into a strongly
 * typed `Customer` class, indexes them by id into a `Map<int, Customer>*`, and
 * then runs a handful of "database-style" queries against the map.
 *
 * Highlights
 *   - File.read_text reads the whole file in one call (include/file.h).
 *   - json(...) parses the array into a tagged dict.
 *   - d.length()  / for-in over a dict array  (the C1/C3 features).
 *   - (Customer)? customer_dict   — Phase 1 typed bind cast: missing fields
 *     default to zero/NULL rather than throwing.  Lets us tolerate slightly
 *     irregular records without try/catch around every iteration.
 *   - Map<int, Customer*> with `.ownsValues()`: each record lives in a
 *     heap-allocated Customer and `delete db` frees them all in one shot.
 *     (The Map<K,V> for-in protocol requires V to be scalar or pointer, so a
 *     by-value Customer would not be iterable; the pointer storage trivially
 *     sidesteps that constraint and is what production code would use anyway.)
 *
 * Run:
 *   ./bin/classyc -g -I include examples/classy-customers.cy -eg
 *
 * The dict that parsed the JSON is *intentionally* never deleted: the
 * Customer.String fields are `char*` pointers into the dict's own arena, so
 * the dict must outlive any Customer we keep around.  In a service you'd
 * either use String.copy() during ingest or keep the dict for the process
 * lifetime (as we do here).
 */
#include <stdio.h>
#include <string.h>
#include "file.h"
#include "map.h"

/* ── Schema ─────────────────────────────────────────────────────────
   These mirror customers.json one-for-one.  Plain `struct` is the right tool
   here — the records are pure data, no invariants, no methods — and the bind
   cast `(T)? d` accepts struct targets identically to class targets, walking
   the same member list and recursing into nested struct fields. */

/* typedef both structs to single-token names so they slot into a generic
   type-arg list (`Map<int, Customer*>`) without parser gymnastics. */
typedef struct {
    String street;
    String city;
    String state;
    String zipCode;
    String country;
} Address;

typedef struct {
    int     id;
    String  firstName;
    String  lastName;
    String  fullName;
    String  email;
    String  phone;
    Address address;          /* nested by-value struct field */
    String  dateOfBirth;
    String  sex;
    String  race;
    String  eyeColor;
    int     heightInches;
    int     weightLbs;
    String  occupation;
} Customer;

/* ── Ingest ────────────────────────────────────────────────────────────────
   Read the file, parse JSON, bind each element, insert into the map keyed by
   the customer's `id`.  Returns the populated map (caller owns; we `defer
   delete` it in main).  Prints a progress line for visibility. */
Map<int, Customer*>* load_customers(char *path, dict *out_source_dict) {
    char *text = File.read_text(path);
    if (!text) {
        printf("FATAL: could not read %s\n", path);
        return 0;
    }

    /* Parse the whole file into a dict.  customers.json is a bare JSON array,
       so the result has DICT_ARRAY at its root. */
    dict d = json(text);
    free(text);

    /* Keep the parsed dict alive — the Customer.String fields point into the
       dict's arena.  We hand it back to main so it can outlive the map. */
    *out_source_dict = d;

    /* `.ownsValues()` flips the value-ownership flag: `delete db` frees each
       heap struct in addition to releasing the Map's own table.  `delete` on
       a plain struct pointer just runs `free()` (no destructor to run). */
    Map<int, Customer*>* db = (new Map<int, Customer*>())->ownsValues();

    /* for-in over a dict array binds (index, element-as-dict). */
    int n = 0;
    for (auto i, rec in d) {
        /* Lenient cast: any field missing in a record gets the zero default
           rather than aborting the load.  Switch to `(struct Customer) rec`
           for a strict ingest that throws KeyException on the first missing
           key.

           Bind into a heap-allocated struct so the map can store a pointer
           (which the for-in protocol accepts) and so `delete db` reclaims
           every record in one call via .ownsValues().  A plain struct has
           no constructor, so plain `malloc` is the right way to allocate; we
           then overwrite every field via the bind cast in one statement. */
        Customer* c = (Customer*) malloc(sizeof(Customer));
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

    /* Hold the parsed dict in main so it outlives the map.  The map's
       Customer values reference strings that live inside this dict's arena. */
    dict source;
    Map<int, Customer*>* db = load_customers("examples/customers.json", &source);
    if (db == NULL) return 1;
    defer delete db;     /* frees the Map's table + each owned struct pointer */

    printf("\ndatabase: %d records, %d unique ids\n",
           (int)source.length(), (int)db->Count());

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
