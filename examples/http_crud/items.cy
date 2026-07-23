/* items.cy — inventory table with ASP.NET-style HTTP attributes.
 *
 *   [[HttpGet("/api/items/{id}")]]
 *   static Response* get_item(Request* req) {
 *       int id = req->argInt("id");
 *       ...
 *   }
 *
 * No central switch / ROUTE table.  The compiler synthesizes a
 * [[registry("routes")]] RouteReg for each [[HttpGet]] / [[HttpPost]] / …
 * (legacy ROUTE("GET", path, fn) still works).
 */
#include "httpserve.h"
#include "sqlite.h"

Sqlite* g_db = NULL;

void items_boot(void) {
    g_db = Sqlite.open(":memory:");
    g_db->execute(
        "CREATE TABLE items ("
        "  id   INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  name TEXT NOT NULL,"
        "  qty  INTEGER NOT NULL DEFAULT 0,"
        "  note TEXT"
        ")");
    g_db->execute("INSERT INTO items(name,qty,note) VALUES (?,?,?)",
                  "sis", "Widget", 10, "bin A");
    g_db->execute("INSERT INTO items(name,qty,note) VALUES (?,?,?)",
                  "sis", "Gadget", 3, "bin B");
}

/* ── handlers (self-register via [[HttpGet]] / [[HttpPost]] / …) ──────── */

[[HttpGet("/health")]]
static Response* health(Request* req) {
    (void)req;
    owned auto ok = { "ok": true, "service": "http_crud" };
    return resp_ok(ok.json());
}

[[HttpGet("/api/items")]]
static Response* list_items(Request* req) {
    try {
        String q = req->arg("q");
        int limit = req->argInt("limit");
        if (limit <= 0 || limit > 500) limit = 100;

        owned auto rows = ((char*)q == NULL || ((char*)q)[0] == 0)
            ? g_db->query("SELECT * FROM items ORDER BY id LIMIT ?", "i", limit)
            : g_db->query(
                  "SELECT * FROM items WHERE name LIKE '%'||?||'%' "
                  "ORDER BY id LIMIT ?",
                  "si", (char*)q, limit);

        owned auto out = { "total": rows->Count(), "items": rows->ToDict() };
        return resp_ok(out.json());
    } catch (SqliteError e) {
        return resp_500(e.msg);
    }
}

[[HttpGet("/api/items/{id}")]]
static Response* get_item(Request* req) {
    int id = req->argInt("id");
    if (id <= 0) return resp_bad_request("id required");
    try {
        owned auto rows = g_db->query("SELECT * FROM items WHERE id=?", "i", id);
        if (rows->Count() == 0) return resp_not_found(f"item {id}");
        return resp_ok(rows->Get(0).json());
    } catch (SqliteError e) {
        return resp_500(e.msg);
    }
}

[[HttpPost("/api/items")]]
static Response* create_item(Request* req) {
    if (req->body == 0) return resp_bad_request("JSON body required");
    if (req->body.name == 0) return resp_bad_request("name is required");

    char* name = (char*)req->body.name;
    int   qty  = req->body.qty != 0 ? (int)req->body.qty : 0;
    char* note = req->body.note != 0 ? (char*)req->body.note : "";

    try {
        g_db->execute("INSERT INTO items(name,qty,note) VALUES (?,?,?)",
                      "sis", name, qty, note);
        long nid = g_db->lastInsertRowId();
        owned auto row = g_db->query("SELECT * FROM items WHERE id=?", "l", nid);
        return resp_created(row->Get(0).json());
    } catch (SqliteError e) {
        return resp_500(e.msg);
    }
}

[[HttpPut("/api/items/{id}")]]
static Response* update_item(Request* req) {
    int id = req->argInt("id");
    if (id <= 0) return resp_bad_request("id required");
    if (req->body == 0) return resp_bad_request("JSON body required");

    try {
        owned auto hit = g_db->query("SELECT id FROM items WHERE id=?", "i", id);
        if (hit->Count() == 0) return resp_not_found(f"item {id}");

        /* Partial update: only fields present in the JSON body. */
        char* name = req->body.name != 0 ? (char*)req->body.name : 0;
        char* note = req->body.note != 0 ? (char*)req->body.note : 0;
        int has_qty = req->body.qty != 0;
        int qty = has_qty ? (int)req->body.qty : 0;

        owned auto st = g_db->prepare(
            "UPDATE items SET "
            "  name = COALESCE(?, name),"
            "  qty  = CASE WHEN ? THEN ? ELSE qty END,"
            "  note = COALESCE(?, note) "
            "WHERE id = ?");
        if (name) st->bind(1, name); else st->bindNull(1);
        st->bind(2, has_qty ? 1 : 0);
        st->bind(3, qty);
        if (note) st->bind(4, note); else st->bindNull(4);
        st->bind(5, id);
        st->execute();

        owned auto row = g_db->query("SELECT * FROM items WHERE id=?", "i", id);
        return resp_ok(row->Get(0).json());
    } catch (SqliteError e) {
        return resp_500(e.msg);
    }
}

[[HttpDelete("/api/items/{id}")]]
static Response* delete_item(Request* req) {
    int id = req->argInt("id");
    if (id <= 0) return resp_bad_request("id required");
    try {
        int n = g_db->execute("DELETE FROM items WHERE id=?", "i", id);
        if (n == 0) return resp_not_found(f"item {id}");
        return resp_no_content();
    } catch (SqliteError e) {
        return resp_500(e.msg);
    }
}
