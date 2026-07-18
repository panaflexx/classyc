/* classyc-db-stress.cy — in-process ClassyDB micro-benchmark.
 *
 * Bypasses the HTTP server to measure the raw DB engine.
 *
 * Run:
 *   ./bin/classyc -I include examples/beyond-demo/classyc-db-stress.cy -eg
 *   ./bin/classyc -I include examples/beyond-demo/classyc-db-stress.cy -eg 10000 10000
 */

#include "classyc-db-engine.h"
#include <time.h>
#include <stdlib.h>

/* Use clock() — single-threaded, so CPU time ≈ wall time. */
double NowSec() {
    return (double)clock() / (double)CLOCKS_PER_SEC;
}

void Report(const char* label, int n, double t) {
    if (t <= 0) t = 0.000001;
    printf("  %-16s %6d ops in %7.3fs  -> %10.1f ops/sec  (%8.3f us/op)\n",
           label, n, t, (double)n / t, (t * 1e6) / (double)n);
}

String RandomRole() {
    int r = rand() % 3;
    if (r == 0) return (String)"user";
    if (r == 1) return (String)"admin";
    return (String)"guest";
}

dict MakeDoc(int i) {
    dict d = dict_create_object();
    d["name"]   = dict_create_string((char*)((String)"user-" + i));
    d["age"]    = dict_create_int64(18 + (rand() % 63));
    d["role"]   = dict_create_string((char*)RandomRole());
    d["active"] = dict_create_int64(rand() % 2);
    d["salary"] = dict_create_int64(30000 + (rand() % 220000));
    return d;
}

dict QueryRoleAdmin()  { return json("{\"role\":\"admin\"}"); }
dict QueryAgeGte30()   { return json("{\"age\":{\"$gte\":30}}"); }
dict QueryActiveRich() { return json("{\"$and\":[{\"active\":1},{\"salary\":{\"$gte\":100000}}]}"); }
dict QueryAgeIn()      { return json("{\"age\":{\"$in\":[25,35,45,55,65]}}"); }
dict QueryAgeEq30()    { return json("{\"age\":30}"); }

void RunQueryPhase(Collection* users, const char* label, dict filter, int ops) {
    double t0 = NowSec();
    int total = 0;
    for (int i = 0; i < ops; i++) {
        auto ids = users->FindIds(filter);
        total += ids->Count();
        delete ids;
    }
    Report(label, ops, NowSec() - t0);
    printf("                     avg result size: %.1f\n", (double)total / (double)ops);
}

int main(int argc, char **argv) {
    srand(42);

    int seedCount = 10000;
    int ops       = 10000;
    if (argc > 1) seedCount = atoi(argv[1]);
    if (argc > 2) ops = atoi(argv[2]);

    printf("ClassyDB in-process stress\n");
    printf("  seed docs: %d\n", seedCount);
    printf("  ops/phase: %d\n\n", ops);

    Database db;
    Collection* users = db.CollectionNamed("users");

    /* ── INSERT ── */
    double t0 = NowSec();
    for (int i = 0; i < seedCount; i++) {
        dict d = MakeDoc(i);
        users->Insert(d);
        dict_destroy(d);
    }
    Report("INSERT", seedCount, NowSec() - t0);
    printf("  collection size: %d\n\n", users->Count());

    /* ── CREATE INDEXES ── */
    t0 = NowSec();
    users->CreateIndex("role");
    users->CreateIndex("age");
    users->CreateIndex("active");
    users->CreateIndex("salary");
    Report("INDEX", 4, NowSec() - t0);
    printf("\n");

    auto allIds = users->FindIds(json("{}"));
    int idCount = allIds->Count();

    /* ── QUERY by type ── */
    printf("-- FindIds breakdown (no serialization) --\n");
    RunQueryPhase(users, "role=admin",    QueryRoleAdmin(),  ops / 4);
    RunQueryPhase(users, "age>=30",       QueryAgeGte30(),   ops / 4);
    RunQueryPhase(users, "active&&rich",  QueryActiveRich(), ops / 4);
    RunQueryPhase(users, "age $in",       QueryAgeIn(),      ops / 4);
    RunQueryPhase(users, "age=30",        QueryAgeEq30(),    ops / 4);

    /* ── mixed QUERY ── */
    dict qRole  = QueryRoleAdmin();
    dict qAge   = QueryAgeGte30();
    dict qAnd   = QueryActiveRich();
    dict qIn    = QueryAgeIn();

    t0 = NowSec();
    int totalFound = 0;
    for (int i = 0; i < ops; i++) {
        dict f;
        switch (i % 4) {
            case 0: f = qRole; break;
            case 1: f = qAge;  break;
            case 2: f = qAnd;  break;
            default: f = qIn;  break;
        }
        auto ids = users->FindIds(f);
        totalFound += ids->Count();
        delete ids;
    }
    Report("QUERY mixed", ops, NowSec() - t0);
    printf("  total documents matched: %d\n\n", totalFound);

    /* ── GET ── */
    t0 = NowSec();
    int found = 0;
    for (int i = 0; i < ops; i++) {
        String id = allIds->Get(i % idCount);
        if (users->FindById(id)) found++;
    }
    Report("GET", ops, NowSec() - t0);
    printf("  found: %d\n\n", found);

    /* ── UPDATE ── */
    dict upd = json("{\"$inc\":{\"salary\":100}}");
    t0 = NowSec();
    for (int i = 0; i < ops; i++) {
        String id = allIds->Get(i % idCount);
        users->Update(id, upd);
    }
    Report("UPDATE", ops, NowSec() - t0);
    printf("\n");

    delete allIds;
    return 0;
}
