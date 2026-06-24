/* test-sqlite-classyc.cy — end-to-end smoke test for sketch/sqlite-classyc.h.
 *
 * Exercises the wrapper from open to close:
 *
 *   1. Sqlite.open(":memory:")  + `defer delete db;`
 *   2. execute()  — CREATE TABLE, INSERTs
 *   3. query()    — full table scan, row -> dict
 *   4. query()    — filtered SELECT (literal in SQL)
 *   5. (Customer) bind-cast over dict rows
 *   6. Transaction with commit()
 *   7. Transaction rolled back automatically by ~Transaction()
 *   8. Prepared Statement — bound INSERTs re-using one stmt
 *   9. Prepared Statement — bound SELECT (parameter binding)
 *  10. One-shot bound execute(sql, fmt, ...) / query(sql, fmt, ...)
 *  11. lastInsertRowId()
 *  12. NULL preserved as JSON null in dict rows
 *  13. SqliteError exceptions (bad SQL, bad prepare)
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

    /* ---------- 8. prepared statement: bound INSERTs ---------- */
    printf("\n-- prepared INSERT (re-used with bind) --\n");
    {
        Statement* ins = db->prepare(
            "INSERT INTO customers VALUES (?, ?, ?, ?, ?, ?)");
        check(ins != 0, "8a  prepare INSERT");
        if (ins) {
            defer delete ins;

            /* row 1 */
            ins->bind(1, 10);
            ins->bind(2, "Linus");
            ins->bind(3, "Torvalds");
            ins->bind(4, "linus@kernel.org");
            ins->bind(5, "555-1010");
            ins->bind(6, "OR");
            int n1 = ins->execute();
            check(n1 == 1, "8b  bound INSERT row 1 -> 1 row affected");

            /* row 2 — same stmt, new binds */
            ins->bind(1, 11);
            ins->bind(2, "Brian");
            ins->bind(3, "Kernighan");
            ins->bind(4, "bwk@cs.princeton.edu");
            ins->bind(5, "555-1111");
            ins->bind(6, "NJ");
            int n2 = ins->execute();
            check(n2 == 1, "8c  bound INSERT row 2 -> 1 row affected");

            /* row 3 — mixed types incl. bindNull */
            ins->bind(1, 12);
            ins->bind(2, "Dennis");
            ins->bind(3, "Ritchie");
            ins->bindNull(4);                /* no email */
            ins->bind(5, "555-1212");
            ins->bind(6, "NJ");
            int n3 = ins->execute();
            check(n3 == 1, "8d  bound INSERT row 3 (with NULL) -> 1 row");
        }
    }
    List<dict>* after_prep = db->query("SELECT * FROM customers");
    if (after_prep) {
        defer delete after_prep;
        check(after_prep->Count() == 8, "8e  8 rows after bound INSERTs");
    }

    /* ---------- 9. prepared statement: bound SELECT ---------- */
    printf("\n-- prepared SELECT (bound parameter) --\n");
    {
        Statement* sel = db->prepare(
            "SELECT id, firstName, lastName, state FROM customers "
            "WHERE state = ? ORDER BY id");
        check(sel != 0, "9a  prepare SELECT");
        if (sel) {
            defer delete sel;

            /* ?1 = "NJ" -> expect Kernighan + Ritchie */
            sel->bind(1, "NJ");
            List<dict>* nj = sel->query();
            if (nj) {
                defer delete nj;
                check(nj->Count() == 2, "9b  state='NJ' -> 2 rows");
                for (auto r in nj) {
                    printf("  NJ: #%d  %s %s\n",
                           (int)(long)r.id,
                           (char*)r.firstName,
                           (char*)r.lastName);
                }
            }

            /* Re-use the same Statement with a new bind */
            sel->bind(1, "CA");
            List<dict>* ca2 = sel->query();
            if (ca2) {
                defer delete ca2;
                check(ca2->Count() == 2, "9c  state='CA' -> 2 rows (re-used stmt)");
                for (auto r in ca2) {
                    printf("  CA: #%d  %s %s\n",
                           (int)(long)r.id,
                           (char*)r.firstName,
                           (char*)r.lastName);
                }
            }
        }
    }

    /* ---------- 10. one-shot bound execute / query ---------- */
    printf("\n-- one-shot bound execute / query --\n");
    {
        int n = db->execute(
            "INSERT INTO customers VALUES (?, ?, ?, ?, ?, ?)",
            "isssss", 20, "Donald", "Knuth", "dek@cs.stanford.edu",
            "555-2020", "CA");
        check(n == 1, "10a one-shot execute -> 1 row inserted");

        /* Mixed types in one call: int + string */
        List<dict>* hits = db->query(
            "SELECT id, firstName, lastName FROM customers "
            "WHERE id >= ? AND state = ? ORDER BY id",
            "is", 10, "CA");
        if (hits) {
            defer delete hits;
            check(hits->Count() == 1, "10b one-shot query (id>=10 AND state=CA) -> 1 row");
            for (auto r in hits) {
                printf("  hit: #%d %s %s\n",
                       (int)(long)r.id,
                       (char*)r.firstName,
                       (char*)r.lastName);
            }
        }

        /* NULL bind via 'n' */
        int n2 = db->execute(
            "INSERT INTO customers VALUES (?, ?, ?, ?, ?, ?)",
            "isssss", 21, "Bjarne", "Stroustrup",
            "bjarne@example.com", "555-2121", "NJ");
        check(n2 == 1, "10c second one-shot insert");

        /* The 'n' format char binds NULL without consuming a vararg. */
        int n3 = db->execute(
            "INSERT INTO customers VALUES (?, ?, ?, ?, ?, ?)",
            "issnss", 22, "Edsger", "Dijkstra",
            "555-2222", "NL");
        check(n3 == 1, "10d one-shot insert with explicit NULL bind");
    }

    /* ---------- 11. lastInsertRowId ---------- */
    printf("\n-- lastInsertRowId --\n");
    {
        /* Use a table with auto-incrementing rowid */
        db->execute("CREATE TABLE notes (id INTEGER PRIMARY KEY AUTOINCREMENT, body TEXT)");
        db->execute("INSERT INTO notes(body) VALUES ('first')", "");
        long id1 = db->lastInsertRowId();
        check(id1 == 1, "11a lastInsertRowId() == 1 after first INSERT");
        db->execute("INSERT INTO notes(body) VALUES (?)", "s", "second");
        long id2 = db->lastInsertRowId();
        check(id2 == 2, "11b lastInsertRowId() == 2 after second INSERT");
    }

    /* ---------- 12. NULL preserved as JSON null ---------- */
    printf("\n-- NULL preservation --\n");
    {
        /* Dijkstra (id=22) had email bound as NULL. */
        List<dict>* dnk = db->query(
            "SELECT id, firstName, email FROM customers WHERE id=?",
            "i", 22);
        if (dnk) {
            defer delete dnk;
            check(dnk->Count() == 1, "12a found Dijkstra");
            dict d = dnk->Get(0);
            /* JSON null serializes as the literal string "null". */
            char* j = d.json;
            printf("  row: %s\n", j);
            /* Crude but effective: ensure the JSON contains `"email":null`. */
            check(strstr(j, "\"email\":null") != 0,
                  "12b email column preserved as JSON null");
        }
    }

    /* ---------- 13. SqliteError exceptions ---------- */
    printf("\n-- SqliteError exceptions --\n");
    {
        int caught = 0;
        try {
            db->execute("INSRT INTO customers VALUES (1)");   // typo
        } catch (SqliteError e) {
            caught = 1;
            printf("  caught SqliteError: %s\n", e.msg);
        }
        check(caught == 1, "13a SqliteError on malformed SQL (execute)");

        caught = 0;
        try {
            List<dict>* bad = db->query("SELECT * FROM no_such_table");
            if (bad) delete bad;
        } catch (SqliteError e) {
            caught = 1;
            printf("  caught SqliteError: %s\n", e.msg);
        }
        check(caught == 1, "13b SqliteError on missing table (query)");

        caught = 0;
        try {
            db->execute("INSERT INTO customers VALUES (1, 'dup', 'dup', '', '', 'XX')",
                        "");
            /* row id=1 already exists -> PRIMARY KEY conflict */
        } catch (SqliteError e) {
            caught = 1;
            printf("  caught SqliteError: %s\n", e.msg);
        }
        check(caught == 1, "13c SqliteError on PK conflict (bound execute)");
    }

    /* ---------- 14. final dump ---------- */
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
