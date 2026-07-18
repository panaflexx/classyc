/* classyc-db-core.cy — ClassyDB: a MongoDB-flavored document database in ClassyC.
 *
 * Demonstrates:
 *   · dict as the native document format
 *   · Map<String, dict> collections with explicit ownership
 *   · MongoDB-style query filters ($eq, $gt, $gte, $lt, $lte, $in, $and, $or)
 *   · update operators ($set, $inc, $unset)
 *   · secondary indexes (equality + range)
 *   · JSON import/export
 *   · a small insertion + query benchmark
 *
 * Run:
 *   ./bin/classyc -I include examples/beyond-demo/classyc-db-core.cy -eg
 */

#include "classyc-db-engine.h"

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

int main() {
    printf("════════════════════════════════════════════════════════════\n");
    printf("  ClassyDB — in-memory document database demo\n");
    printf("════════════════════════════════════════════════════════════\n\n");

    Database db;

    /* ── 1. Insert ── */
    printf("-- 1. Insert --\n");
    Collection* users = db.CollectionNamed("users");

    String id1 = users->Insert(json("{\"name\":\"Ada\",\"age\":36,\"role\":\"admin\",\"active\":1,\"salary\":180000}"));
    String id2 = users->Insert(json("{\"name\":\"Grace\",\"age\":41,\"role\":\"admin\",\"active\":1,\"salary\":220000}"));
    String id3 = users->Insert(json("{\"name\":\"Alan\",\"age\":28,\"role\":\"user\",\"active\":0,\"salary\":95000}"));
    String id4 = users->Insert(json("{\"name\":\"Tim\",\"age\":22,\"role\":\"user\",\"active\":1,\"salary\":72000}"));
    String id5 = users->Insert(json("{\"name\":\"Dennis\",\"age\":55,\"role\":\"user\",\"active\":1,\"salary\":210000}"));

    check(users->Count() == 5, "inserted 5 users");
    printf("  ids: %s, %s, %s, %s, %s\n", (char*)id1, (char*)id2, (char*)id3, (char*)id4, (char*)id5);

    /* ── 2. FindById ── */
    printf("\n-- 2. FindById --\n");
    dict found = users->FindById(id1);
    check(found != 0, "FindById returns doc");
    if (found) printf("  %s\n", (char*)json(found));

    /* ── 3. Queries (table scan) ── */
    printf("\n-- 3. Queries (before indexes) --\n");

    dict q_admins = json("{\"role\":\"admin\"}");
    auto admin_ids = users->FindIds(q_admins);
    defer delete admin_ids;
    check(admin_ids->Count() == 2, "role=admin matches 2");

    dict q_adults = json("{\"age\":{\"$gte\":30}}");
    auto adult_ids = users->FindIds(q_adults);
    defer delete adult_ids;
    check(adult_ids->Count() == 3, "age >= 30 matches 3");

    dict q_rich_active = json("{\"$and\":[{\"active\":1},{\"salary\":{\"$gte\":150000}}]}");
    auto rich_ids = users->FindIds(q_rich_active);
    defer delete rich_ids;
    check(rich_ids->Count() == 3, "active && salary>=150k matches 3");

    dict q_in = json("{\"age\":{\"$in\":[22,28,55]}}");
    auto in_ids = users->FindIds(q_in);
    defer delete in_ids;
    check(in_ids->Count() == 3, "age in [22,28,55] matches 3");

    /* ── 4. Create indexes and re-run queries ── */
    printf("\n-- 4. Queries (with indexes) --\n");
    users->CreateIndex("role");
    users->CreateIndex("age");
    users->CreateIndex("active");
    users->CreateIndex("salary");

    auto admin_ids2 = users->FindIds(q_admins);
    defer delete admin_ids2;
    check(admin_ids2->Count() == 2, "indexed role=admin matches 2");

    auto adult_ids2 = users->FindIds(q_adults);
    defer delete adult_ids2;
    check(adult_ids2->Count() == 3, "indexed age>=30 matches 3");

    auto rich_ids2 = users->FindIds(q_rich_active);
    defer delete rich_ids2;
    check(rich_ids2->Count() == 3, "indexed $and matches 3");

    auto in_ids2 = users->FindIds(q_in);
    defer delete in_ids2;
    check(in_ids2->Count() == 3, "indexed age $in matches 3");

    /* ── 5. Update ── */
    printf("\n-- 5. Update --\n");
    users->Update(id1, json("{\"$set\":{\"role\":\"superadmin\"},\"$inc\":{\"salary\":5000}}"));
    dict updated = users->FindById(id1);
    check(strcmp((char*)updated.role, "superadmin") == 0, "$set updated role");
    check((int)AsDouble(updated.salary) == 185000, "$inc updated salary");
    printf("  updated: %s\n", (char*)json(updated));

    /* Index should reflect the update. */
    dict q_super = json("{\"role\":\"superadmin\"}");
    auto super_ids = users->FindIds(q_super);
    defer delete super_ids;
    check(super_ids->Count() == 1, "index sees updated role");

    /* ── 6. Delete ── */
    printf("\n-- 6. Delete --\n");
    users->Delete(id3);
    check(users->Count() == 4, "after delete count is 4");
    check(users->FindById(id3) == 0, "deleted doc not found");

    /* ── 7. Dump collection as JSON array ── */
    printf("\n-- 7. Collection dump --\n");
    printf("  %s\n", (char*)users->ToJsonArray());

    /* ── 7b. Freeze / thaw (JSONL persistence) ── */
    printf("\n-- 7b. Freeze / thaw --\n");
    File.remove_file("classyc-db-core.tmp.jsonl");
    check(db.Freeze("classyc-db-core.tmp.jsonl") == 0, "freeze ok");
    check(File.exists("classyc-db-core.tmp.jsonl") == 1, "freeze file exists");

    Database db2;
    int thawed = db2.Thaw("classyc-db-core.tmp.jsonl");
    check(thawed == 4, "thaw loads 4 docs");
    Collection* users2 = db2.CollectionNamed("users");
    auto admin3 = users2->FindIds(json("{\"role\":\"admin\"}"));
    defer delete admin3;
    check(admin3->Count() == 1, "thawed index answers role=admin");
    dict g2 = users2->FindById(id2);
    check(g2 != 0 && strcmp((char*)g2.name, "Grace") == 0, "thawed doc-2 is Grace");
    String id6 = users2->Insert(json("{\"name\":\"New\",\"age\":1}"));
    check(id6.starts_with("doc-") && strcmp((char*)id6, "doc-5") != 0,
          "new id after thaw does not collide");
    File.remove_file("classyc-db-core.tmp.jsonl");

    /* ── 8. Small benchmark ── */
    printf("\n-- 8. Benchmark: 20,000 docs --\n");
    Collection* events = db.CollectionNamed("events");
    for (int i = 0; i < 20000; i++) {
        dict evt = dict_create_object();
        evt["type"] = dict_create_string((i % 3 == 0) ? "click" : ((i % 3 == 1) ? "view" : "purchase"));
        evt["user_id"] = dict_create_int64(i);
        evt["value"] = dict_create_number((double)i * 1.5);
        evt["ts"] = dict_create_int64(i);
        events->Insert(evt);
        dict_destroy(evt);
    }
    printf("  inserted %d events\n", events->Count());

    events->CreateIndex("type");
    events->CreateIndex("value");

    dict q_purchase = json("{\"type\":\"purchase\"}");
    auto purchase_ids = events->FindIds(q_purchase);
    defer delete purchase_ids;
    printf("  purchase events found: %d\n", purchase_ids->Count());
    check(purchase_ids->Count() == 6666, "~6666 purchase events");

    dict q_highvalue = json("{\"$and\":[{\"type\":\"purchase\"},{\"value\":{\"$gte\":20000.0}}]}");
    auto hv_ids = events->FindIds(q_highvalue);
    defer delete hv_ids;
    printf("  high-value purchases: %d\n", hv_ids->Count());
    check(hv_ids->Count() > 0, "some high-value purchases found");

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
