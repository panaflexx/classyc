/* sqlite-bench.c — SQLite twin of classyc-db-stress.cy.
 *
 * Same srand(42) document stream, same phases, same report format, so the
 * numbers line up 1:1 with the ClassyDB in-process benchmark:
 *
 *   INSERT N docs (autocommit, per-op) → CREATE INDEX ×4 →
 *   FindIds-style query phases (rowid projection, stepped through) →
 *   mixed query → GET by id → UPDATE $inc-style.
 *
 * Build:  gcc -O2 -o sqlite-bench sqlite-bench.c -lsqlite3
 * Run:    ./sqlite-bench [seedCount=10000] [ops=10000] [dbpath=:memory:]
 *         ./sqlite-bench 10000 10000 :memory:
 *         ./sqlite-bench 10000 10000 /tmp/sqlite-bench.db
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sqlite3.h>

static double NowSec(void) {
    return (double)clock() / (double)CLOCKS_PER_SEC;
}

static void Report(const char* label, long n, double t) {
    if (t <= 0) t = 0.000001;
    printf("  %-16s %6ld ops in %7.3fs  -> %10.1f ops/sec  (%8.3f us/op)\n",
           label, n, t, (double)n / t, (t * 1e6) / (double)n);
}

static const char* ROLES[3] = { "user", "admin", "guest" };

static void Exec(sqlite3* db, const char* sql) {
    char* err = 0;
    if (sqlite3_exec(db, sql, 0, 0, &err) != SQLITE_OK) {
        fprintf(stderr, "sqlite error: %s\n  sql: %s\n", err, sql);
        sqlite3_free(err);
        exit(1);
    }
}

/* One FindIds-equivalent query: SELECT rowid ... stepped to completion. */
static void RunQueryPhase(sqlite3* db, sqlite3_stmt* st, const char* label, int ops) {
    double t0 = NowSec();
    long total = 0;
    for (int i = 0; i < ops; i++) {
        sqlite3_reset(st);
        while (sqlite3_step(st) == SQLITE_ROW) total++;
    }
    Report(label, ops, NowSec() - t0);
    printf("                     avg result size: %.1f\n", (double)total / (double)ops);
}

int main(int argc, char** argv) {
    srand(42);

    int seedCount = 10000;
    int ops       = 10000;
    const char* path = ":memory:";
    if (argc > 1) seedCount = atoi(argv[1]);
    if (argc > 2) ops = atoi(argv[2]);
    if (argc > 3) path = argv[3];

    printf("SQLite stress (%s)\n", path);
    printf("  seed docs: %d\n", seedCount);
    printf("  ops/phase: %d\n\n", ops);

    sqlite3* db = 0;
    if (sqlite3_open(path, &db) != SQLITE_OK) {
        fprintf(stderr, "cannot open %s\n", path);
        return 1;
    }
    /* WAL + NORMAL sync is the realistic production-ish setting; for
       :memory: these are no-ops. */
    Exec(db, "PRAGMA journal_mode = WAL;");
    Exec(db, "PRAGMA synchronous = NORMAL;");
    Exec(db, "CREATE TABLE users (name TEXT, age INTEGER, role TEXT, active INTEGER, salary INTEGER);");

    /* ── INSERT (prepared, autocommit — per-op like ClassyDB) ── */
    double t0 = NowSec();
    sqlite3_stmt* ins = 0;
    sqlite3_prepare_v2(db, "INSERT INTO users VALUES (?1,?2,?3,?4,?5);", -1, &ins, 0);
    for (int i = 0; i < seedCount; i++) {
        char name[32];
        snprintf(name, sizeof name, "user-%d", i);
        int age = 18 + (rand() % 63);
        const char* role = ROLES[rand() % 3];
        int active = rand() % 2;
        int salary = 30000 + (rand() % 220000);
        sqlite3_bind_text(ins, 1, name, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(ins, 2, age);
        sqlite3_bind_text(ins, 3, role, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(ins, 4, active);
        sqlite3_bind_int(ins, 5, salary);
        if (sqlite3_step(ins) != SQLITE_DONE) { fprintf(stderr, "insert failed\n"); return 1; }
        sqlite3_reset(ins);
    }
    sqlite3_finalize(ins);
    Report("INSERT", seedCount, NowSec() - t0);

    /* ── CREATE INDEXES ── */
    t0 = NowSec();
    Exec(db, "CREATE INDEX idx_role ON users(role);");
    Exec(db, "CREATE INDEX idx_age ON users(age);");
    Exec(db, "CREATE INDEX idx_active ON users(active);");
    Exec(db, "CREATE INDEX idx_salary ON users(salary);");
    Report("INDEX", 4, NowSec() - t0);
    printf("\n");

    /* ── QUERY by type (rowid projection = FindIds id list) ── */
    printf("-- FindIds breakdown (id projection, stepped) --\n");
    sqlite3_stmt *qRole, *qAge, *qAnd, *qIn, *qEq;
    sqlite3_prepare_v2(db, "SELECT rowid FROM users WHERE role = 'admin';", -1, &qRole, 0);
    sqlite3_prepare_v2(db, "SELECT rowid FROM users WHERE age >= 30;", -1, &qAge, 0);
    sqlite3_prepare_v2(db, "SELECT rowid FROM users WHERE active = 1 AND salary >= 100000;", -1, &qAnd, 0);
    sqlite3_prepare_v2(db, "SELECT rowid FROM users WHERE age IN (25,35,45,55,65);", -1, &qIn, 0);
    sqlite3_prepare_v2(db, "SELECT rowid FROM users WHERE age = 30;", -1, &qEq, 0);

    RunQueryPhase(db, qRole, "role=admin",   ops / 4);
    RunQueryPhase(db, qAge,  "age>=30",      ops / 4);
    RunQueryPhase(db, qAnd,  "active&&rich", ops / 4);
    RunQueryPhase(db, qIn,   "age $in",      ops / 4);
    RunQueryPhase(db, qEq,   "age=30",       ops / 4);

    /* ── mixed QUERY ── */
    t0 = NowSec();
    long totalFound = 0;
    for (int i = 0; i < ops; i++) {
        sqlite3_stmt* st;
        switch (i % 4) {
            case 0: st = qRole; break;
            case 1: st = qAge;  break;
            case 2: st = qAnd;  break;
            default: st = qIn;  break;
        }
        sqlite3_reset(st);
        while (sqlite3_step(st) == SQLITE_ROW) totalFound++;
    }
    Report("QUERY mixed", ops, NowSec() - t0);
    printf("  total documents matched: %ld\n\n", totalFound);

    /* ── GET by id (full row, like FindById) ── */
    sqlite3_stmt* get = 0;
    sqlite3_prepare_v2(db, "SELECT name, age, role, active, salary FROM users WHERE rowid = ?1;", -1, &get, 0);
    t0 = NowSec();
    long found = 0;
    for (int i = 0; i < ops; i++) {
        sqlite3_bind_int(get, 1, (i % seedCount) + 1);
        sqlite3_reset(get);
        if (sqlite3_step(get) == SQLITE_ROW) found++;
        sqlite3_reset(get);
    }
    sqlite3_finalize(get);
    Report("GET", ops, NowSec() - t0);
    printf("  found: %ld\n\n", found);

    /* ── UPDATE ($inc salary by id; indexes maintained) ── */
    sqlite3_stmt* upd = 0;
    sqlite3_prepare_v2(db, "UPDATE users SET salary = salary + 100 WHERE rowid = ?1;", -1, &upd, 0);
    t0 = NowSec();
    for (int i = 0; i < ops; i++) {
        sqlite3_bind_int(upd, 1, (i % seedCount) + 1);
        sqlite3_reset(upd);
        sqlite3_step(upd);
        sqlite3_reset(upd);
    }
    sqlite3_finalize(upd);
    Report("UPDATE", ops, NowSec() - t0);

    sqlite3_finalize(qRole); sqlite3_finalize(qAge); sqlite3_finalize(qAnd);
    sqlite3_finalize(qIn);   sqlite3_finalize(qEq);
    sqlite3_close(db);
    return 0;
}
