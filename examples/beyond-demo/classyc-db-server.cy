/* classyc-db-server.cy — ClassyDB HTTP server.
 *
 * Build + run (JIT):
 *   ./bin/classyc -I include \
 *       examples/http-serve.c examples/beyond-demo/classyc-db-server.cy -eg
 *
 * Build + run (AOT):
 *   ./classyc-aot.sh -I include \
 *       examples/http-serve.c examples/beyond-demo/classyc-db-server.cy -o classyc-db
 *   ./classyc-db
 *
 * Test with curl:
 *   curl -s http://127.0.0.1:7099/api/users
 *   curl -s -X POST -d '{"name":"Dave","age":30,"role":"user","active":1,"salary":90000}' \
 *        http://127.0.0.1:7099/api/users
 *   curl -s http://127.0.0.1:7099/api/users/doc-1
 *   curl -s -X POST -d '{"age":{"$gte":30}}' http://127.0.0.1:7099/api/users/query
 *   curl -s -X DELETE http://127.0.0.1:7099/api/users/doc-1
 *
 * Create an index:
 *   curl -s -X POST -d '{"field":"role"}' http://127.0.0.1:7099/api/users/index
 *
 * Run synthetic self-test without opening a socket:
 *   ./bin/classyc -I include examples/beyond-demo/classyc-db-server.cy -eg test
 */

#include "httpserve.h"
#include "classyc-db-engine.h"

Database* g_db;

/* Keep the cyreg_routes linker set non-empty for AOT links: with zero
   ROUTE() entries across all TUs, GNU ld provides no __start_/__stop_
   anchors and the link fails (this app routes manually in app_route). */
static Response* __cyreg_sentinel(Request* req) { return resp_not_found(req->path); }
ROUTE("GET", "/__cyreg_sentinel__", __cyreg_sentinel);

/* HTTP-level op counters (filled by the app_handle wrapper). */
OpStats g_http_get;
OpStats g_http_post;
OpStats g_http_put;
OpStats g_http_delete;
double g_start_ms = 0;

/* ═══════════════════════════════════════════════════════════════════════
   HTTP routing
   ═══════════════════════════════════════════════════════════════════════ */

Response* app_route(Request* req) {
    String path = req->path;

    /* Performance monitor: HTTP method stats + engine op stats + per-
     * collection docs/indexes. */
    if (req->IsGet() && strcmp((char*)path, "/perfmon") == 0) {
        dict out = dict_create_object();
        out["uptime_s"] = dict_create_number((NowMs() - g_start_ms) / 1000.0);
        dict http = dict_create_object();
        http["GET"] = g_http_get.ToJson();
        http["POST"] = g_http_post.ToJson();
        http["PUT"] = g_http_put.ToJson();
        http["DELETE"] = g_http_delete.ToJson();
        out["http"] = http;
        out["engine"] = g_perfmon.ToJson();
        out["collections"] = g_db->CollectionsJson();
        String s = json(out);
        dict_destroy(out);
        return resp_ok(s);
    }

    if (!path.starts_with("/api/")) return resp_not_found(path);

    String rest = path.substr(5, (int)path.length() - 5);
    int slash = (int)rest.find("/");
    String collection, id;
    if (slash < 0) {
        collection = rest;
        id = "";
    } else {
        collection = rest.substr(0, slash);
        id = rest.substr(slash + 1, (int)rest.length() - slash - 1);
    }

    Collection* coll = g_db->CollectionNamed(collection);

    if (req->IsGet() && strcmp((char*)id, "") == 0) {
        return resp_ok(coll->ToJsonArray());
    }

    /* GET /api/<coll>/index — list indexed fields (POST creates one). */
    if (req->IsGet() && strcmp((char*)id, "index") == 0) {
        return resp_ok(coll->IndexesJson());
    }

    if (req->IsGet()) {
        dict doc = coll->FindById(id);
        if (!doc) return resp_not_found(id);
        return resp_ok(json(doc));
    }

    if (req->IsPost() && strcmp((char*)id, "") == 0) {
        dict body = req->body;
        if (!body) return resp_bad_request("expected JSON body");
        String newId = coll->Insert(body);
        dict r = dict_create_object();
        r["_id"] = newId;
        String out = json(r);
        dict_destroy(r);
        return resp_created(out);
    }

    /* Bulk insert: body is a JSON array of documents, max 65536 rows.
     * One connection, one parse, N inserts.  Client-provided _id values
     * are honored; server-assigned ids are not returned (count only). */
    if (req->IsPost() && strcmp((char*)id, "bulk") == 0) {
        dict body = req->body;
        if (!body || body.type() != DICT_ARRAY)
            return resp_bad_request("expected JSON array body");
        if ((int)body.length() > 65536)
            return resp_bad_request("bulk limited to 65536 rows");
        int done = 0;
        for (auto i, doc in body) {
            if (doc && doc.type() == DICT_OBJECT) {
                coll->Insert(doc);
                done++;
            }
        }
        dict r = dict_create_object();
        r["inserted"] = dict_create_int64(done);
        String out = json(r);
        dict_destroy(r);
        return resp_created(out);
    }

    if (req->IsPost() && strcmp((char*)id, "query") == 0) {
        dict filter = req->body;
        if (!filter) filter = json("{}");
        auto ids = coll->FindIds(filter);
        defer delete ids;
        auto arr = new List<String>();
        ids->ForEach((String id) => {
            dict d = coll->FindById(id);
            if (d) arr->Add(json(d));
        });
        String joined = arr->join(",");
        String out = f"[{joined}]";
        delete arr;
        return resp_ok(out);
    }

    if (req->IsPost() && strcmp((char*)id, "count") == 0) {
        dict filter = req->body;
        if (!filter) filter = json("{}");
        auto ids = coll->FindIds(filter);
        defer delete ids;
        String out = f"{{\"count\":{ids->Count()}}}";
        return resp_ok(out);
    }

    if (req->IsPost() && strcmp((char*)id, "index") == 0) {
        dict body = req->body;
        if (!body) return resp_bad_request("expected JSON body");
        String field = (String)body[(char*)"field"];
        if (!field || strlen((char*)field) == 0)
            return resp_bad_request("expected 'field'");
        coll->CreateIndex(field);
        return resp_ok("{}");
    }

    if (req->IsPut()) {
        dict update = req->body;
        if (!update) return resp_bad_request("expected JSON body");
        if (!coll->FindById(id)) return resp_not_found(id);
        coll->Update(id, update);
        return resp_ok("{}");
    }

    if (req->IsDelete()) {
        if (!coll->Delete(id)) return resp_not_found(id);
        return resp_no_content();
    }

    return resp_not_found(req->path);
}

/* Entry point called by the server cores: routes the request and records
 * per-method HTTP op stats (app compute time — queue waits excluded). */
Response* app_handle(Request* req) {
    double t0 = NowMs();
    Response* r = app_route(req);
    double ms = NowMs() - t0;
    if (req->IsGet())         g_http_get.add(ms);
    else if (req->IsPost())   g_http_post.add(ms);
    else if (req->IsPut())    g_http_put.add(ms);
    else if (req->IsDelete()) g_http_delete.add(ms);
    return r;
}

/* ═══════════════════════════════════════════════════════════════════════
   Synthetic self-test (no socket needed)
   ═══════════════════════════════════════════════════════════════════════ */

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

void expect_status(Response* r, int status, const char *label) {
    check(r->status == status, label);
}

int RunSyntheticTests() {
    printf("════════════════════════════════════════════════════════════\n");
    printf("  ClassyDB server — synthetic HTTP tests\n");
    printf("════════════════════════════════════════════════════════════\n\n");

    g_db = new Database();
    defer delete g_db;
    g_start_ms = NowMs();

    /* Seed */
    {
        owned Request* req = new Request("POST", "/api/users", "",
            "{\"name\":\"Ada\",\"age\":36,\"role\":\"admin\",\"active\":1,\"salary\":180000}");
        owned Response* r = app_handle(req);
        expect_status(r, 201, "POST /api/users -> 201");
    }
    {
        owned Request* req = new Request("POST", "/api/users", "",
            "{\"name\":\"Grace\",\"age\":41,\"role\":\"admin\",\"active\":1,\"salary\":220000}");
        owned Response* r = app_handle(req);
        expect_status(r, 201, "POST second user -> 201");
    }

    /* Create indexes */
    {
        owned Request* req = new Request("POST", "/api/users/index", "",
            "{\"field\":\"role\"}");
        owned Response* r = app_handle(req);
        expect_status(r, 200, "POST index -> 200");
    }
    {
        owned Request* req = new Request("POST", "/api/users/index", "",
            "{\"field\":\"age\"}");
        owned Response* r = app_handle(req);
        expect_status(r, 200, "POST index -> 200");
    }

    /* List */
    {
        owned Request* req = new Request("GET", "/api/users", "", "");
        owned Response* r = app_handle(req);
        expect_status(r, 200, "GET /api/users -> 200");
        check(r->body.contains("Ada") && r->body.contains("Grace"), "list contains seeded users");
    }

    /* Get by id */
    {
        owned Request* req = new Request("GET", "/api/users/doc-1", "", "");
        owned Response* r = app_handle(req);
        expect_status(r, 200, "GET /api/users/doc-1 -> 200");
        check(r->body.contains("Ada"), "doc-1 is Ada");
    }

    /* Query */
    {
        owned Request* req = new Request("POST", "/api/users/query", "",
            "{\"age\":{\"$gte\":30}}");
        owned Response* r = app_handle(req);
        expect_status(r, 200, "POST query -> 200");
        check(r->body.contains("Ada") && r->body.contains("Grace"), "query returns adults");
    }

    /* Query with $and using index */
    {
        owned Request* req = new Request("POST", "/api/users/query", "",
            "{\"$and\":[{\"role\":\"admin\"},{\"salary\":{\"$gte\":200000}}]}");
        owned Response* r = app_handle(req);
        expect_status(r, 200, "POST $and query -> 200");
        check(r->body.contains("Grace"), "$and query returns Grace");
    }

    /* Count */
    {
        owned Request* req = new Request("POST", "/api/users/count", "",
            "{\"role\":\"admin\"}");
        owned Response* r = app_handle(req);
        expect_status(r, 200, "POST count -> 200");
        check(r->body.contains("\"count\":2"), "count returns 2 admins");
    }

    /* Update */
    {
        owned Request* req = new Request("PUT", "/api/users/doc-1", "",
            "{\"$set\":{\"role\":\"superadmin\"}}");
        owned Response* r = app_handle(req);
        expect_status(r, 200, "PUT update -> 200");
    }
    {
        owned Request* req = new Request("GET", "/api/users/doc-1", "", "");
        owned Response* r = app_handle(req);
        check(r->body.contains("superadmin"), "update persisted");
    }

    /* Delete */
    {
        owned Request* req = new Request("DELETE", "/api/users/doc-2", "", "");
        owned Response* r = app_handle(req);
        expect_status(r, 204, "DELETE -> 204");
    }
    {
        owned Request* req = new Request("GET", "/api/users/doc-2", "", "");
        owned Response* r = app_handle(req);
        expect_status(r, 404, "deleted doc -> 404");
    }

    /* PUT on a missing doc -> 404 (no silent no-op) */
    {
        owned Request* req = new Request("PUT", "/api/users/doc-999", "",
            "{\"$set\":{\"role\":\"ghost\"}}");
        owned Response* r = app_handle(req);
        expect_status(r, 404, "PUT missing doc -> 404");
    }

    /* Bulk insert + 65536-row cap */
    {
        owned Request* req = new Request("POST", "/api/users/bulk", "",
            "[{\"name\":\"Bulk1\",\"age\":20,\"role\":\"bulk\"},"
            " {\"name\":\"Bulk2\",\"age\":21,\"role\":\"bulk\"},"
            " {\"name\":\"Bulk3\",\"age\":22,\"role\":\"bulk\"}]");
        owned Response* r = app_handle(req);
        expect_status(r, 201, "POST bulk -> 201");
        check(r->body.contains("\"inserted\":3"), "bulk inserted 3");
    }
    {
        owned Request* req = new Request("POST", "/api/users/count", "",
            "{\"role\":\"bulk\"}");
        owned Response* r = app_handle(req);
        check(r->body.contains("\"count\":3"), "count sees bulk docs");
    }
    {
        /* 65537-row body, built in one raw buffer — a String-concat loop
           here would be O(n^2) arena allocations (GBs). */
        unowned char *big = (char*) malloc(65537 * 8 + 3);
        char *w = big;
        *w++ = '[';
        for (int i = 0; i < 65537; i++) {
            if (i > 0) *w++ = ',';
            memcpy(w, "{\"a\":1}", 7);
            w += 7;
        }
        *w++ = ']';
        *w = 0;
        owned Request* req = new Request("POST", "/api/users/bulk", "", (String)big);
        owned Response* r = app_handle(req);
        expect_status(r, 400, "bulk >65536 rows -> 400");
        free(big);
    }

    /* Index listing */
    {
        owned Request* req = new Request("GET", "/api/users/index", "", "");
        owned Response* r = app_handle(req);
        expect_status(r, 200, "GET index list -> 200");
        check(r->body.contains("role") && r->body.contains("age"),
              "index list has role+age");
    }

    /* Perfmon */
    {
        owned Request* req = new Request("GET", "/perfmon", "", "");
        owned Response* r = app_handle(req);
        expect_status(r, 200, "GET /perfmon -> 200");
        check(r->body.contains("insert") && r->body.contains("query_index_hit"),
              "perfmon shows engine stats");
    }

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}

/* ═══════════════════════════════════════════════════════════════════════
   Entry point
   ═══════════════════════════════════════════════════════════════════════ */

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "test") == 0) {
        return RunSyntheticTests();
    }

    g_db = new Database();
    g_start_ms = NowMs();
    printf("ClassyDB server starting...\n");
    if (argc > 1 && strcmp(argv[1], "serial") == 0)
        return serve(7099);        /* one connection at a time (http-serve.c) */
    return serve_fibers(7099);     /* default: one fiber per connection */
}
