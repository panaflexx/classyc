/* classy-customers-rest.cy  —  REST controller backed by SQLite
 *
 * A small Flask-style REST API for a customer directory.  Nothing is wired
 * to a network — requests are constructed in code and dispatched through a
 * router — but the persistence layer is real: every read and write goes
 * through `include/sqlite.h` against an in-memory SQLite database.
 *
 *   · HTTP request/response as first-class objects
 *   · Persistence in SQLite via `Sqlite* db` member
 *   · One-shot bound `db->execute(sql, fmt, ...)` / `db->query(sql, fmt, ...)`
 *   · Rows come back as `dict` and `(Customer) r` bind-casts them
 *   · `try { ... } catch (SqliteError e) { ... }` -> 500 on db failure
 *   · `Transaction* tx` + `defer delete tx` for multi-step updates
 *   · `db->lastInsertRowId()` powers `POST` -> `201 Created` with the new id
 *
 * Routes
 *   GET    /api/customers             – list (optional ?state=XX&limit=N)
 *   GET    /api/customers/{id}        – get one
 *   POST   /api/customers             – create (JSON body)
 *   PUT    /api/customers/{id}        – update (JSON body, partial)
 *   DELETE /api/customers/{id}        – remove
 *
 * Build & run:
 *   ./bin/classyc -I include -l sqlite3 examples/classy-customers-rest.cy -eg
 */

#include "include/sqlite.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ═══════════════════════════════════════════════════════════════════════
   Schema row — used as a bind-cast target on SELECTs
   ═══════════════════════════════════════════════════════════════════════ */
class Customer {
    int    id;
    String firstName;
    String lastName;
    String email;
    String phone;
    String state;
};

/* ═══════════════════════════════════════════════════════════════════════
   String utilities (copied from classy-controller-like.cy, lightly trimmed)
   ═══════════════════════════════════════════════════════════════════════ */

/* Extract a query-string parameter value, or NULL if absent. */
String qparam(String qs, String key) {
    if (qs == NULL || key == NULL) return NULL;
    String needle = key + "=";
    int p = (int) qs.find(needle);
    if (p < 0) return NULL;
    String rest = qs.substr(p + (int)needle.length(),
                            (int)qs.length() - p - (int)needle.length());
    int amp = (int) rest.find("&");
    if (amp >= 0) return rest.substr(0, amp);
    return rest;
}

/* Path segment after a known prefix, stripping a leading slash. */
String path_after(String path, String prefix) {
    if (!path.starts_with(prefix)) return NULL;
    int plen = (int) prefix.length();
    if (plen >= (int) path.length()) return "";
    String rest = path.substr(plen, (int)path.length() - plen);
    if (rest.starts_with("/"))
        return rest.substr(1, (int)rest.length() - 1);
    return rest;
}

/* ═══════════════════════════════════════════════════════════════════════
   Request / Response (same shape as classy-controller-like.cy)
   ═══════════════════════════════════════════════════════════════════════ */
class Request {
    String method;
    String path;
    String query;
    dict   body;

    Request(String method, String path, String query, String bodyJson) {
        this.method = method.trim().upper().detach();
        this.path   = path.trim().lower().detach();
        if ((char*)query != NULL) this.query = query; else this.query = "";
        this.body   = 0;
        if ((char*)bodyJson != NULL) this.body = json(bodyJson);
    }

    int IsGet()    { return strcmp(this.method, "GET")    == 0; }
    int IsPost()   { return strcmp(this.method, "POST")   == 0; }
    int IsPut()    { return strcmp(this.method, "PUT")    == 0; }
    int IsDelete() { return strcmp(this.method, "DELETE") == 0; }

    String QueryParam(String key) { return qparam(this.query, key); }
};

class Response {
    int    status;
    String statusText;
    String body;

    Response(int status, String statusText, String body) {
        this.status     = status;
        this.statusText = statusText;
        this.body       = body.detach();
    }
};

/* Response factory helpers */
Response* resp_ok(String body)            { return new Response(200, "OK", body); }
Response* resp_created(String body)       { return new Response(201, "Created", body); }
Response* resp_no_content()               { return new Response(204, "No Content", "{}"); }
Response* resp_bad_request(String msg)    { return new Response(400, "Bad Request",
                                            f"{{\"error\":\"{msg}\"}}"); }
Response* resp_not_found(String resource) { return new Response(404, "Not Found",
                                            f"{{\"error\":\"{resource} not found\"}}"); }
Response* resp_conflict(String msg)       { return new Response(409, "Conflict",
                                            f"{{\"error\":\"{msg}\"}}"); }
Response* resp_500(String msg)            { return new Response(500, "Internal Server Error",
                                            f"{{\"error\":\"{msg}\"}}"); }

/* ═══════════════════════════════════════════════════════════════════════
   CustomersController  —  every method talks to SQLite via `this.db`
   ═══════════════════════════════════════════════════════════════════════ */
class CustomersController {
    Sqlite* db;

    /* The controller owns its db handle.  Schema is created up-front and
       seeded with three rows so the demo has something to list. */
    CustomersController() {
        this.db = Sqlite.open(":memory:");
        this.db->execute(
            "CREATE TABLE customers ("
            "  id        INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  firstName TEXT NOT NULL,"
            "  lastName  TEXT NOT NULL,"
            "  email     TEXT,"
            "  phone     TEXT,"
            "  state     TEXT"
            ")");

        /* Seed */
        this.db->execute(
            "INSERT INTO customers(firstName,lastName,email,phone,state) "
            "VALUES (?, ?, ?, ?, ?)", "sssss",
            "Ada", "Lovelace", "ada@analytical.engine", "555-0001", "CA");
        this.db->execute(
            "INSERT INTO customers(firstName,lastName,email,phone,state) "
            "VALUES (?, ?, ?, ?, ?)", "sssss",
            "Alan", "Turing", "alan@bletchley.uk", "555-0002", "NY");
        this.db->execute(
            "INSERT INTO customers(firstName,lastName,email,phone,state) "
            "VALUES (?, ?, ?, ?, ?)", "sssss",
            "Grace", "Hopper", "grace@cobol.dev", "555-0003", "CA");
    }

    ~CustomersController() {
        delete this.db;
    }

    /* ── small helper: render List<dict>* as a JSON array String ── */
    String RowsToJson(List<dict>* rows) {
        String out = "[";
        int first = 1;
        for (auto r in rows) {
            if (!first) out = out + ",";
            out = out + r.json;
            first = 0;
        }
        return out + "]";
    }

    /* ── GET /api/customers ─────────────────────────────────────────── */
    Response* List(Request* req) {
        try {
            String state  = req->QueryParam("state");
            String limits = req->QueryParam("limit");
            int    limit  = limits != NULL ? atoi(limits) : 100;

            List<dict>* rows;
            if (state == NULL) {
                rows = this.db->query(
                    "SELECT * FROM customers ORDER BY id LIMIT ?", "i", limit);
            } else {
                rows = this.db->query(
                    "SELECT * FROM customers WHERE state=? "
                    "ORDER BY id LIMIT ?", "si", (char*)state, limit);
            }
            defer delete rows;

            String body = f"{{\"total\":{rows->Count()},\"data\":{this->RowsToJson(rows)}}}";
            return resp_ok(body);
        } catch (SqliteError e) {
            return resp_500(e.msg);
        }
    }

    /* ── GET /api/customers/{id} ────────────────────────────────────── */
    Response* Get(Request* req, int id) {
        try {
            List<dict>* rows = this.db->query(
                "SELECT * FROM customers WHERE id=?", "i", id);
            defer delete rows;
            if (rows->Count() == 0) return resp_not_found(f"Customer {id}");
            String body = f"{rows->Get(0).json}";
            return resp_ok(body);
        } catch (SqliteError e) {
            return resp_500(e.msg);
        }
    }

    /* ── POST /api/customers ────────────────────────────────────────── */
    Response* Create(Request* req) {
        if (req->body == 0)
            return resp_bad_request("Request body is required");
        if (req->body.firstName == 0 || req->body.lastName == 0)
            return resp_bad_request("firstName and lastName are required");

        try {
            /* Use a transaction so duplicate-email check + INSERT are atomic. */
            Transaction* tx = this.db->begin();
            defer delete tx;          /* dtor ROLLBACKs if commit() never runs */

            if (req->body.email != 0) {
                List<dict>* dup = this.db->query(
                    "SELECT id FROM customers WHERE email=?", "s",
                    (char*)req->body.email);
                defer delete dup;
                if (dup->Count() > 0) {
                    return resp_conflict(
                        f"Email {(char*)req->body.email} already registered");
                }
            }

            char* email = req->body.email != 0 ? (char*)req->body.email : "";
            char* phone = req->body.phone != 0 ? (char*)req->body.phone : "";
            char* state = req->body.state != 0 ? (char*)req->body.state : "";

            this.db->execute(
                "INSERT INTO customers(firstName,lastName,email,phone,state) "
                "VALUES (?, ?, ?, ?, ?)", "sssss",
                (char*)req->body.firstName,
                (char*)req->body.lastName,
                email, phone, state);

            long new_id = this.db->lastInsertRowId();
            tx->commit();

            /* Re-read the row so the response reflects what SQLite stored.
               We materialise the JSON into a fresh String via f"{...}" so it
               survives the defer-delete + try-scope cleanup before Response
               can capture it. */
            List<dict>* created = this.db->query(
                "SELECT * FROM customers WHERE id=?", "l", new_id);
            defer delete created;
            String body = f"{created->Get(0).json}";
            return resp_created(body);
        } catch (SqliteError e) {
            return resp_500(e.msg);
        }
    }

    /* ── PUT /api/customers/{id} ────────────────────────────────────── */
    Response* Update(Request* req, int id) {
        if (req->body == 0)
            return resp_bad_request("Request body is required");

        try {
            /* Make sure the row exists first so we can 404 cleanly. */
            List<dict>* existing = this.db->query(
                "SELECT * FROM customers WHERE id=?", "i", id);
            defer delete existing;
            if (existing->Count() == 0)
                return resp_not_found(f"Customer {id}");

            /* Partial update — COALESCE keeps the existing value when the
               bound parameter is NULL.  We bind NULL for any absent field. */
            char* fn = req->body.firstName != 0 ? (char*)req->body.firstName : 0;
            char* ln = req->body.lastName  != 0 ? (char*)req->body.lastName  : 0;
            char* em = req->body.email     != 0 ? (char*)req->body.email     : 0;
            char* ph = req->body.phone     != 0 ? (char*)req->body.phone     : 0;
            char* st = req->body.state     != 0 ? (char*)req->body.state     : 0;

            Statement* upd = this.db->prepare(
                "UPDATE customers SET "
                "  firstName = COALESCE(?, firstName), "
                "  lastName  = COALESCE(?, lastName), "
                "  email     = COALESCE(?, email), "
                "  phone     = COALESCE(?, phone), "
                "  state     = COALESCE(?, state)   "
                "WHERE id = ?");
            defer delete upd;
            if (fn) upd->bind(1, fn); else upd->bindNull(1);
            if (ln) upd->bind(2, ln); else upd->bindNull(2);
            if (em) upd->bind(3, em); else upd->bindNull(3);
            if (ph) upd->bind(4, ph); else upd->bindNull(4);
            if (st) upd->bind(5, st); else upd->bindNull(5);
            upd->bind(6, id);
            upd->execute();

            List<dict>* fresh = this.db->query(
                "SELECT * FROM customers WHERE id=?", "i", id);
            defer delete fresh;
            String body = f"{fresh->Get(0).json}";
            return resp_ok(body);
        } catch (SqliteError e) {
            return resp_500(e.msg);
        }
    }

    /* ── DELETE /api/customers/{id} ─────────────────────────────────── */
    Response* Delete(Request* req, int id) {
        try {
            int n = this.db->execute(
                "DELETE FROM customers WHERE id=?", "i", id);
            if (n == 0) return resp_not_found(f"Customer {id}");
            return resp_no_content();
        } catch (SqliteError e) {
            return resp_500(e.msg);
        }
    }
};

/* ═══════════════════════════════════════════════════════════════════════
   Router
   ═══════════════════════════════════════════════════════════════════════ */
Response* dispatch(CustomersController* ctrl, Request* req) {
    String base = "/api/customers";

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
   Logging
   ═══════════════════════════════════════════════════════════════════════ */
void log_request(Request* req) {
    printf("→ %s %s%s%s\n",
           req->method, req->path,
           req->query.empty() ? "" : "?", req->query);
    if (req->body != 0) printf("  body: %s\n", req->body.json);
}

void log_response(Response* res) {
    printf("← %d %s  %s\n\n", res->status, res->statusText, res->body);
}

/* ═══════════════════════════════════════════════════════════════════════
   main — simulate a series of REST calls
   ═══════════════════════════════════════════════════════════════════════ */
int main() {
    auto ctrl = new CustomersController();
    defer delete ctrl;

    printf("══════════════════════════════════════════════\n");
    printf("  classyc REST controller (SQLite-backed)     \n");
    printf("══════════════════════════════════════════════\n\n");

    /* ── 1. List all customers ─────────────────────────────────────── */
    {
        auto req = new Request("GET", "/api/customers", "", NULL);
        defer delete req;
        auto res = dispatch(ctrl, req);
        defer delete res;
        log_request(req); log_response(res);
    }

    /* ── 2. List filtered by ?state=CA ─────────────────────────────── */
    {
        auto req = new Request("GET", "/api/customers", "state=CA&limit=10", NULL);
        defer delete req;
        auto res = dispatch(ctrl, req);
        defer delete res;
        log_request(req); log_response(res);
    }

    /* ── 3. Get one by id ──────────────────────────────────────────── */
    {
        auto req = new Request("GET", "/api/customers/2", "", NULL);
        defer delete req;
        auto res = dispatch(ctrl, req);
        defer delete res;
        log_request(req); log_response(res);
    }

    /* ── 4. Get a non-existent id ──────────────────────────────────── */
    {
        auto req = new Request("GET", "/api/customers/99", "", NULL);
        defer delete req;
        auto res = dispatch(ctrl, req);
        defer delete res;
        log_request(req); log_response(res);
    }

    /* ── 5. Create — POST ──────────────────────────────────────────── */
    {
        String body = "{\"firstName\":\"Donald\",\"lastName\":\"Knuth\","
                      "\"email\":\"dek@cs.stanford.edu\","
                      "\"phone\":\"555-2020\",\"state\":\"CA\"}";
        auto req = new Request("POST", "/api/customers", "", body);
        defer delete req;
        auto res = dispatch(ctrl, req);
        defer delete res;
        log_request(req); log_response(res);
    }

    /* ── 6. Duplicate-email POST -> 409 Conflict ───────────────────── */
    {
        String body = "{\"firstName\":\"Imposter\",\"lastName\":\"Knuth\","
                      "\"email\":\"dek@cs.stanford.edu\"}";
        auto req = new Request("POST", "/api/customers", "", body);
        defer delete req;
        auto res = dispatch(ctrl, req);
        defer delete res;
        log_request(req); log_response(res);
    }

    /* ── 7. Partial update — PUT ───────────────────────────────────── */
    {
        String body = "{\"state\":\"WA\",\"phone\":\"555-9999\"}";
        auto req = new Request("PUT", "/api/customers/1", "", body);
        defer delete req;
        auto res = dispatch(ctrl, req);
        defer delete res;
        log_request(req); log_response(res);
    }

    /* ── 8. DELETE ─────────────────────────────────────────────────── */
    {
        auto req = new Request("DELETE", "/api/customers/3", "", NULL);
        defer delete req;
        auto res = dispatch(ctrl, req);
        defer delete res;
        log_request(req); log_response(res);
    }

    /* ── 9. Final state — list everyone ────────────────────────────── */
    {
        auto req = new Request("GET", "/api/customers", "", NULL);
        defer delete req;
        auto res = dispatch(ctrl, req);
        defer delete res;
        log_request(req); log_response(res);
    }

    return 0;
}
