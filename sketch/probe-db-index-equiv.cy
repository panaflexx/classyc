/* probe-db-index-equiv.cy — differential test: indexed vs table-scan results.
 *
 * For every query below, FindIds is run BEFORE any CreateIndex (pure
 * DocMatches table scan) and AFTER (index-assisted path), and the two
 * result id lists must be equal as sets.  This pins the invariant the
 * engine's exact-index short-circuit relies on, across mixed-type fields,
 * null/bool/array conditions, $or, and single/multi-conjunct $and.
 *
 * Run from repo root:
 *   ./bin/classyc -I include -I examples/beyond-demo sketch/probe-db-index-equiv.cy -eg
 */

#include "classyc-db-engine.h"
#include <string.h>

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

int CmpStr(String a, String b) { return strcmp((char*)a, (char*)b); }

/* Sorted "id,id,..." key so two id lists compare as sets. */
String IdsKey(List<String>* ids) {
    ids->Sort(CmpStr);
    return ids->join(",");
}

void Equiv(Collection* c, dict q, const char* label, String want) {
    auto idx = c->FindIds(q);
    String got = IdsKey(idx);
    int ok = strcmp((char*)got, (char*)want) == 0;
    check(ok, label);
    if (!ok) printf("    scan=[%s]\n    idx =[%s]\n", (char*)want, (char*)got);
    delete idx;
}

int main() {
    printf("=== probe: index vs scan equivalence ===\n\n");

    Database db;
    Collection* c = db.CollectionNamed("t");

    /* Mixed-type fields on purpose:
     *   age:    int64, double, string, null-box, missing
     *   active: bool true/false and int64 1/0
     *   nested: array (non-scalar) on one doc
     */
    c->Insert(json("{\"name\":\"Ada\",\"age\":36,\"role\":\"admin\",\"active\":true}"));
    c->Insert(json("{\"name\":\"Grace\",\"age\":41.5,\"role\":\"admin\",\"active\":1}"));
    c->Insert(json("{\"name\":\"Alan\",\"age\":28,\"role\":\"user\",\"active\":false}"));
    c->Insert(json("{\"name\":\"Tim\",\"age\":\"thirty\",\"role\":\"user\",\"active\":0}"));
    c->Insert(json("{\"name\":\"Dennis\",\"age\":null,\"role\":\"user\",\"active\":true,\"nested\":[1,2]}"));
    c->Insert(json("{\"name\":\"NoAge\",\"role\":\"admin\"}"));

    /* Queries run through BOTH paths. */
    dict qs[12];
    qs[0]  = json("{\"role\":\"admin\"}");
    qs[1]  = json("{\"age\":{\"$gte\":30}}");
    qs[2]  = json("{\"role\":{\"$gt\":\"a\"}}");
    qs[3]  = json("{\"active\":true}");
    qs[4]  = json("{\"active\":1}");
    qs[5]  = json("{\"age\":null}");
    qs[6]  = json("{\"age\":{\"$in\":[28,41.5,\"thirty\"]}}");
    qs[7]  = json("{\"role\":\"user\",\"age\":{\"$lt\":40}}");
    qs[8]  = json("{\"$and\":[{\"role\":\"admin\"}]}");
    qs[9]  = json("{\"$and\":[{\"role\":\"admin\"},{\"age\":{\"$gte\":30}}]}");
    qs[10] = json("{\"$or\":[{\"role\":\"user\"},{\"age\":{\"$gte\":40}}]}");
    qs[11] = json("{\"nested\":[1,2]}");

    const char* labels[12];
    labels[0]  = "string eq (role=admin)";
    labels[1]  = "numeric range over mixed-type field";
    labels[2]  = "string range ($gt on role)";
    labels[3]  = "bool eq (active=true)";
    labels[4]  = "int64 eq vs bool docs (active=1)";
    labels[5]  = "null condition (age=null)";
    labels[6]  = "$in with mixed scalars";
    labels[7]  = "two-field filter (re-filter path)";
    labels[8]  = "$and single conjunct";
    labels[9]  = "$and two conjuncts";
    labels[10] = "$or (never indexed)";
    labels[11] = "array condition (non-scalar)";

    /* Pass 1: table scan (no indexes yet) — record reference results. */
    String scanKeys[12];
    for (int i = 0; i < 12; i++) {
        auto ids = c->FindIds(qs[i]);
        scanKeys[i] = detach IdsKey(ids);  /* keep past the loop-block arena release */
        delete ids;
    }

    /* Create indexes on every field used above. */
    c->CreateIndex("role");
    c->CreateIndex("age");
    c->CreateIndex("active");
    c->CreateIndex("nested");
    c->CreateIndex("name");

    /* Pass 2: index-assisted — must match scan exactly. */
    for (int i = 0; i < 12; i++)
        Equiv(c, qs[i], labels[i], scanKeys[i]);

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
