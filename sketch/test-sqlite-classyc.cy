/* test-sqlite-classyc.cy — end-to-end smoke test for sketch/sqlite-classyc.h.
 *
 * Exercises the wrapper from open to close:
 *
 *   1. Sqlite.open(":memory:")  + `defer delete db;`
 *   2. execute()  — CREATE TABLE, INSERTs
 *   3. query()    — full table scan, row -> dict
 *   4. query()    — filtered SELECT (literal in SQL; binders not landed yet)
 *   5. (Customer) bind-cast over dict rows
 *   6. Transaction with commit()
 *   7. Transaction rolled back automatically by ~Transaction()
 *
 * Run:
 *   ./bin/classyc -I sketch -I include -l sqlite3 \
 *       sketch/test-sqlite-classyc.cy -eg
 */

#include "sketch/sqlite-classyc.h"
#include <stdio.h>
#include <string.h>

class Customer {
    int     id;
    String  firstName;
    String  lastName;
    String  email;
    String  phone;
};

int passed;
int failed;

void check(int cond, const char* label) {
    if (cond) {
        printf("  PASS  %s\n", label);
        passed = passed + 1;
    } else {
        printf("  FAIL  %s\n", label);
        failed = failed + 1;
    }
}

int main() {
    printf("=== sqlite-classyc smoke test ===\n\n");
    passed = 0;
    failed = 0;

    /* ---------- 1. open ---------- */
    printf("-- open --\n");
    Sqlite* db = Sqlite.open(":memory:");
    check(db != 0, "1a  Sqlite.open(\":memory:\")");
    if (!db) {
        printf("cannot open db -- aborting\n");
        return 1;
    }
    defer delete db;

    /* ---------- 2. CREATE TABLE + INSERT ---------- */
    printf("\n-- DDL + INSERT --\n");
    int rc = db->execute(
        "CREATE TABLE customers ("
        "  id        INTEGER PRIMARY KEY,"
        "  firstName TEXT NOT NULL,"
        "  lastName  TEXT NOT NULL,"
        "  email     TEXT,"
        "  phone     TEXT,"
        "  state     TEXT"
        ")");
    check(rc >= 0, "2a  CREATE TABLE customers");

    int n = 0;
    n = n + db->execute(
        "INSERT INTO customers VALUES "
        "(1,'Ada','Lovelace','ada@analytical.engine','555-0001','CA')");
    n = n + db->execute(
        "INSERT INTO customers VALUES "
        "(2,'Alan','Turing','alan@bletchley.uk','555-0002','NY')");
    n = n + db->execute(
        "INSERT INTO customers VALUES "
        "(3,'Grace','Hopper','grace@cobol.dev','555-0003','CA')");
    check(n == 3, "2b  inserted 3 rows");

    /* ---------- 3. full SELECT ---------- */
    printf("\n-- SELECT * --\n");
    List<dict>* rows = db->query("SELECT * FROM customers ORDER BY id");
    check(rows != 0, "3a  query() returned non-null");
    if (rows) {
        defer delete rows;
        check(rows->Count() == 3, "3b  3 rows back");
        for (auto r in rows) {
            printf("  #%d  %s %s <%s> [%s]\n",
                   (int)(long)r.id,
                   (char*)r.firstName,
                   (char*)r.lastName,
                   (char*)r.email,
                   (char*)r.state);
        }
    }

    /* ---------- 4. filtered SELECT ---------- */
    printf("\n-- SELECT WHERE state='CA' --\n");
    List<dict>* ca = db->query(
        "SELECT * FROM customers WHERE state='CA' ORDER BY id");
    check(ca != 0, "4a  filtered query() returned non-null");
    if (ca) {
        defer delete ca;
        check(ca->Count() == 2, "4b  2 CA customers");
        for (auto r in ca) {
            printf("  %s %s\n", (char*)r.firstName, (char*)r.lastName);
        }
    }

    /* ---------- 5. (Customer) bind-cast ---------- */
    printf("\n-- (Customer) bind-cast --\n");
    List<dict>* raw = db->query(
        "SELECT id, firstName, lastName, email, phone "
        "FROM customers ORDER BY id");
    check(raw != 0, "5a  query() ok");
    if (raw) {
        defer delete raw;
        int cust_count = 0;
        for (auto r in raw) {
            Customer c = (Customer) r;
            cust_count = cust_count + 1;
            printf("  Customer #%d  %s %s  <%s>  %s\n",
                   c.id,
                   (char*)c.firstName,
                   (char*)c.lastName,
                   (char*)c.email,
                   (char*)c.phone);
        }
        check(cust_count == 3, "5b  3 Customers materialized via cast");
    }

    /* ---------- 6. transaction: commit ---------- */
    printf("\n-- transaction (commit) --\n");
    {
        Transaction* tx = db->begin();
        defer delete tx;            // belt: ~Transaction() rolls back if we
                                    // somehow fall off without commit().

        db->execute("INSERT INTO customers VALUES "
                    "(4,'Margaret','Hamilton','mh@apollo.nasa','555-0004','MA')");
        db->execute("INSERT INTO customers VALUES "
                    "(5,'Katherine','Johnson','kj@orbits.nasa','555-0005','VA')");

        tx->commit();
    }
    List<dict>* after_commit = db->query("SELECT * FROM customers");
    if (after_commit) {
        defer delete after_commit;
        check(after_commit->Count() == 5, "6a  5 rows after commit");
    }

    /* ---------- 7. transaction: rollback via defer ---------- */
    printf("\n-- transaction (rollback) --\n");
    {
        Transaction* tx = db->begin();
        defer delete tx;            // commit() is never called below, so the
                                    // destructor will issue ROLLBACK.

        db->execute("INSERT INTO customers VALUES "
                    "(99,'Will','Vanish','will@nope.example','555-9999','XX')");

        /* deliberately exit scope without tx->commit() */
    }
    List<dict>* after_rb = db->query("SELECT * FROM customers");
    if (after_rb) {
        defer delete after_rb;
        check(after_rb->Count() == 5,
              "7a  still 5 rows after rollback (insert undone)");
    }

    /* ---------- 8. final dump ---------- */
    printf("\n-- final table --\n");
    List<dict>* dump = db->query(
        "SELECT id, firstName, lastName, state FROM customers ORDER BY id");
    if (dump) {
        defer delete dump;
        for (auto r in dump) {
            printf("  %d  %-10s %-10s  [%s]\n",
                   (int)(long)r.id,
                   (char*)r.firstName,
                   (char*)r.lastName,
                   (char*)r.state);
        }
    }

    printf("\n=== results: %d passed, %d failed ===\n", passed, failed);
    return failed;
}
