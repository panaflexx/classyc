/* classy-http-app.c — a SQLite-backed ClassyC application served by http-serve.c.
 *
 * The "controller" half of the gunicorn-like split: it owns the data and the
 * routes; the base server (http-serve.c) owns the sockets.  They share the
 * Request / Response object model declared in include/httpserve.h.  Persistence
 * is real — every read and write goes through include/sqlite.h against an
 * in-memory SQLite database that lives for the process lifetime.
 *
 * Run (two TUs compiled + linked into one program; needs libsqlite3):
 *
 *     ./bin/classyc -I include -l sqlite3 \
 *                   examples/http-serve.c examples/classy-http-app.c -eg
 *
 * Then drive it with curl:
 *
 *     curl -s http://127.0.0.1:8080/api/users
 *     curl -s 'http://127.0.0.1:8080/api/users?role=editor&limit=5'
 *     curl -s http://127.0.0.1:8080/api/users/2
 *     curl -s -X POST -d '{"name":"Dave","email":"dave@x.com","role":"editor"}' \
 *          http://127.0.0.1:8080/api/users
 *     curl -s -X PUT  -d '{"role":"superadmin"}' http://127.0.0.1:8080/api/users/1
 *     curl -s -X DELETE http://127.0.0.1:8080/api/users/3
 *
 * Memory model / ergonomics: one-shot bound `db->execute(sql, fmt, ...)` /
 * `db->query(sql, fmt, ...)`, rows come back as `dict` (so `.json()` serialises
 * them for free), `try { ... } catch (SqliteError e) { ... }` maps DB failures
 * to a 500, and a `Transaction*` with `defer delete` rolls back unless
 * `commit()` runs.  It just works.
 */
#include "httpserve.h"
#include "include/sqlite.h"

/* ═══════════════════════════════════════════════════════════════════════
   UsersController — every route talks to SQLite via `this.db`
   ═══════════════════════════════════════════════════════════════════════ */
class UsersController {
    Sqlite* db;

    /* The controller owns its db handle.  Schema is created up-front and
       seeded with three rows so the demo has something to list. */
    UsersController() {
        this.db = Sqlite.open(":memory:");
        this.db->execute(
            "CREATE TABLE users ("
            "  id     INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  name   TEXT NOT NULL,"
            "  email  TEXT,"
            "  role   TEXT NOT NULL DEFAULT 'viewer',"
            "  active INTEGER NOT NULL DEFAULT 1"
            ")");

        this.db->execute("INSERT INTO users(name,email,role,active) VALUES (?,?,?,?)",
                         "sssi", "Alice Nguyen", "alice@example.com", "admin",  1);
        this.db->execute("INSERT INTO users(name,email,role,active) VALUES (?,?,?,?)",
                         "sssi", "Bob Okafor",   "bob@example.com",   "editor", 1);
        this.db->execute("INSERT INTO users(name,email,role,active) VALUES (?,?,?,?)",
                         "sssi", "Carol Lima",   "carol@example.com", "viewer", 0);
    }

    ~UsersController() { delete this.db; }

    /* ── GET /api/users  (optional ?role=X&limit=N) ────────────────── */
    Response* List(Request* req) {
        try {
            String role   = req->QueryParam("role");
            String limits = req->QueryParam("limit");
            int    limit  = limits != NULL ? atoi(limits) : 100;

            /* `owned`: auto-released at scope exit on every path — no defer. */
            owned auto rows = (role == NULL || strcmp(role, "") == 0)
                ? this.db->query(
                    "SELECT * FROM users ORDER BY id LIMIT ?", "i", limit)
                : this.db->query(
                    "SELECT * FROM users WHERE role=? ORDER BY id LIMIT ?",
                    "si", (char*)role, limit);

            /* Build the response as a real dict and let `.json()` serialise it.
               `rows->ToDict()` turns the List<dict> into a DICT_ARRAY; the dict
               literal deep-copies it under the "data" key. */
            owned auto out = { "total": rows->Count(), "data": rows->ToDict() };
            return resp_ok(out.json());
        } catch (SqliteError e) {
            return resp_500(e.msg);
        }
    }

    /* ─ GET /api/users/{id} ────────────────────────────────────── */
    Response* Get(Request* req, int id) {
        try {
            owned auto rows = this.db->query(
                "SELECT * FROM users WHERE id=?", "i", id);
            if (rows->Count() == 0) return resp_not_found(f"User {id}");
            return resp_ok(rows->Get(0).json());
        } catch (SqliteError e) {
            return resp_500(e.msg);
        }
    }

    /* ─ POST /api/users ────────────────────────────────────────── */
    Response* Create(Request* req) {
        if (req->body == 0)
            return resp_bad_request("Request body is required");
        if (req->body.name == 0 || req->body.email == 0)
            return resp_bad_request("name and email are required");

        try {
            /* Transaction so the duplicate-email check + INSERT are atomic.
               `owned`: auto-release (ROLLBACK) unless commit() runs. */
            owned auto tx = this.db->begin();

            owned auto dup = this.db->query(
                "SELECT id FROM users WHERE email=?", "s", (char*)req->body.email);
            if (dup->Count() > 0)
                return resp_conflict(f"Email {(char*)req->body.email} already registered");

            char* role = req->body.role != 0 ? (char*)req->body.role : "viewer";
            this.db->execute(
                "INSERT INTO users(name,email,role,active) VALUES (?,?,?,?)",
                "sssi", (char*)req->body.name, (char*)req->body.email, role, 1);

            long new_id = this.db->lastInsertRowId();
            tx->commit();

            owned auto created = this.db->query(
                "SELECT * FROM users WHERE id=?", "l", new_id);
            return resp_created(created->Get(0).json());
        } catch (SqliteError e) {
            return resp_500(e.msg);
        }
    }

    /* ─ PUT /api/users/{id}  (partial update via COALESCE) ──────── */
    Response* Update(Request* req, int id) {
        if (req->body == 0)
            return resp_bad_request("Request body is required");

        try {
            owned auto existing = this.db->query(
                "SELECT id FROM users WHERE id=?", "i", id);
            if (existing->Count() == 0) return resp_not_found(f"User {id}");

            char* nm = req->body.name  != 0 ? (char*)req->body.name  : 0;
            char* em = req->body.email != 0 ? (char*)req->body.email : 0;
            char* rl = req->body.role  != 0 ? (char*)req->body.role  : 0;

            owned auto upd = this.db->prepare(
                "UPDATE users SET "
                "  name  = COALESCE(?, name), "
                "  email = COALESCE(?, email), "
                "  role  = COALESCE(?, role)  "
                "WHERE id = ?");
            if (nm) upd->bind(1, nm); else upd->bindNull(1);
            if (em) upd->bind(2, em); else upd->bindNull(2);
            if (rl) upd->bind(3, rl); else upd->bindNull(3);
            upd->bind(4, id);
            upd->execute();

            owned auto fresh = this.db->query(
                "SELECT * FROM users WHERE id=?", "i", id);
            return resp_ok(fresh->Get(0).json());
        } catch (SqliteError e) {
            return resp_500(e.msg);
        }
    }

    /* ─ DELETE /api/users/{id} ───────────────────────────────────── */
    Response* Delete(Request* req, int id) {
        try {
            int n = this.db->execute("DELETE FROM users WHERE id=?", "i", id);
            if (n == 0) return resp_not_found(f"User {id}");
            return resp_no_content();
        } catch (SqliteError e) {
            return resp_500(e.msg);
        }
    }
};

/* ═══════════════════════════════════════════════════════════════════════
   Routing
   ═══════════════════════════════════════════════════════════════════════ */
Response* dispatch(UsersController* ctrl, Request* req) {
    String base = "/api/users";

    if (strcmp(req->path, base) == 0) {
        if (req->IsGet())  return ctrl->List(req);
        if (req->IsPost()) return ctrl->Create(req);
    }

    if (req->path.starts_with(base + "/")) {
        String id_str = path_after(req->path, base);
        if (id_str != NULL && !id_str.empty()) {
            int id = atoi(id_str);
            if (req->IsGet())    return ctrl->Get(req, id);
            if (req->IsPut())    return ctrl->Update(req, id);
            if (req->IsDelete()) return ctrl->Delete(req, id);
        }
    }

    return resp_not_found(req->path);
}

/* ═══════════════════════════════════════════════════════════════════════
   The server↔app contract: one controller instance for the process lifetime.
   ═══════════════════════════════════════════════════════════════════════ */
UsersController* g_ctrl = 0;

Response* app_handle(Request* req) {
    return dispatch(g_ctrl, req);
}

int main() {
    g_ctrl = new UsersController();
    printf("════════════════════════════════════════════\n");
    printf("  classyc users API  (SQLite-backed)\n");
    printf("  try: curl -s http://127.0.0.1:8080/api/users\n");
    printf("════════════════════════════════════════════\n");
    int rc = serve(8080);        /* blocks, serving requests via app_handle */
    delete g_ctrl;               /* (only reached if serve() bails out)     */
    return rc;
}
