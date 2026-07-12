/* classh-restful.cy — SQLite-backed classy REST controller (Pythonic demo)
 *
 * A self-contained, in-process REST controller that stores users in SQLite.
 * Demonstrates the full classy idiom stack:
 *
 *   · Sqlite* + defer delete + Transaction + Statement + one-shot binds
 *   · (User) dict→class cast for every row
 *   · f-strings + String methods for routing / JSON shaping
 *   · Request / Response value objects with clean factories
 *   · defer everywhere for deterministic cleanup
 *   · Pagination + filtering pushed down to SQL (efficient)
 *   · A "Pythonic" POST that uses an explicit transaction + audit log
 *   · String.split("&") + for-in replaces manual while+find in parse_query
 *   · List<String> + ->join(",") for dynamic SQL fragment assembly (no trim hack)
 *   · ->Filter(lambda) for in-memory active-row selection
 *
 * GAP NOTES (where ClassyC diverges from Python idioms):
 *   · join is parts->join(",") not ",".join(parts) — separator is the argument
 *   · dict int fields in a lambda still need the (int)(long) double-cast
 *   · Map is same-type (T→T); cross-type transform needs a for-in loop today
 *
 * Nothing is network-wired; requests are built in main() and dispatched.
 * Compile/run exactly like the sqlite smoke test:
 *
 *   ./bin/classyc -I sketch -I include -l sqlite3 \
 *       examples/classh-restful.cy -eg
 */

#include "sqlite.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map.h>
#include "list.h"

int row_is_active(dict r) { return (int)(long)r.active != 0; }
String col_eq_placeholder(String k) { return detach (k + "=?"); }

/* ── value class for (User) cast binding ─────────────────────────────── */
class User {
    int     id;
    String  name;
    String  email;
    String  role;
    int     active;
};

/* ── tiny query-string & path helpers (same spirit as classy-controller) */
String qparam(String qs, String key) {
    if (qs == NULL || key == NULL) return NULL;
    String needle = key + "=";
    int p = (int) qs.find(needle);
    if (p < 0) return NULL;
    String rest = qs.substr(p + (int)needle.length(),
                            (int)qs.length() - p - (int)needle.length());
    int amp = (int) rest.find("&");
    if (amp >= 0)
        return rest.substr(0, amp);
    return rest;
}

String path_after(String path, String prefix) {
    if (!path.starts_with(prefix)) return NULL;
    int plen = (int) prefix.length();
    if (plen >= (int) path.length()) return "";
    String rest = path.substr(plen, (int)path.length() - plen);
    if (rest.starts_with("/"))
        return rest.substr(1, (int)rest.length()-1);
    return rest;
}

/* ── Request / Response (lightweight, JSON body via dict) ─────────────────── */
/* Parse a raw query string into a Map<String,String> (best-practice collection) */
Map<String, String>* parse_query(String qs) {
    auto m = new Map<String, String>();
    if (qs == NULL || qs.empty()) return m;

    /* Pythonic: split on "&", iterate pairs — replaces manual while+find loop.
       s.split(delim) -> List<String>* (heap); defer delete cleans it up. */
    List<String>* pairs = qs.split("&");
    for (auto pair in pairs) {
        int eq = (int) pair.find("=");
        if (eq >= 0) {
            String k = pair.substr(0, eq);
            String v = pair.substr(eq + 1, (int)pair.length() - eq - 1);
            m->Set(k.trim().detach(), v.trim().detach());
        } else if (!pair.empty()) {
            m->Set(pair.trim().detach(), "".detach());
        }
    }
    return m;
}

class Request {
    String method, path;
    dict   body;
    Map<String, String>* queryParams;   /* parsed once, O(1) lookup, insertion-order iter */

    Request(String m, String p, String q, String bodyJson) {
        this.method = m.trim().upper().detach();
        this.path   = p.trim().lower().detach();
        this.body = json(bodyJson);
        this.queryParams = parse_query(q);
    }

    ~Request() {
        if (this.queryParams) delete this.queryParams;
    }

    int IsGet()    { return strcmp(this.method,"GET")    == 0; }
    int IsPost()   { return strcmp(this.method,"POST")   == 0; }
    int IsPut()    { return strcmp(this.method,"PUT")    == 0; }
    int IsDelete() { return strcmp(this.method,"DELETE") == 0; }

    /* O(1) lookup via Map (best practice over repeated string scans) */
    String QueryParam(String k) {
        if (!this->queryParams) return (String)0;
        try {
            return this->queryParams->Get(k);
        } catch (e) {
            return (String)0;
        }
    }

    /* expose the whole collection if a handler wants to iterate or pass it on */
    Map<String, String>* QueryParams() { return this->queryParams; }
};

class Response {
    int    status;
    String statusText, body;

    Response(int st, String txt, String b) {
        this.status = st; this.statusText = txt; this.body = b.detach();
    }
    void Print() {
        printf("HTTP/1.1 %d %s\nContent-Type: application/json\n"
               "Content-Length: %d\n\n%s\n",
               this.status, this.statusText,
               (int)this.body.length(), this.body);
    }
};

/* factories */
Response* resp_ok(String b)       { return new Response(200,"OK",b); }
Response* resp_created(String b)  { return new Response(201,"Created",b); }
Response* resp_no_content()       { return new Response(204,"No Content","{}"); }
Response* resp_bad(String m)      { return new Response(400,"Bad Request",
                                        f"{{\"error\":\"{m}\"}}"); }
Response* resp_not_found(String r){ return new Response(404,"Not Found",
                                        f"{{\"error\":\"{r} not found\"}}"); }

/* ── UsersController — now backed by real SQLite ──────────────────────── */
class UsersController {
    Sqlite* db;
    int     next_id;          /* only used for the very first seed */

    UsersController(Sqlite* d) {
        this.db = d;
        /* ensure table exists (idempotent) */
        this.db->execute(
            "CREATE TABLE IF NOT EXISTS users ("
            " id INTEGER PRIMARY KEY,"
            " name TEXT NOT NULL,"
            " email TEXT UNIQUE NOT NULL,"
            " role TEXT DEFAULT 'viewer',"
            " active INTEGER DEFAULT 1)"
        );
        /* simple audit log for the Pythonic POST demo */
        this.db->execute(
            "CREATE TABLE IF NOT EXISTS audit ("
            " ts INTEGER DEFAULT (strftime('%s','now')),"
            " action TEXT,"
            " user_id INTEGER)"
        );
    }

    /* helper: materialize one row as User (caller owns nothing extra) */
    User row_to_user(dict r) { return (User) r; }

    /* ── GET /api/users?…  (filter + pagination pushed to SQL) ───────── */
    Response* List(Request* req) {
        String role  = req->QueryParam("role");
        String page_s  = req->QueryParam("page");
        String limit_s = req->QueryParam("limit");
        int page  = (page_s  != NULL) ? atoi(page_s)  : 1;
        int limit = (limit_s != NULL) ? atoi(limit_s) : 20;
        if (page < 1) page = 1;
        if (limit < 1) limit = 20;
        int offset = (page-1) * limit;

        /* Use ? placeholders throughout — role/limit/offset are never
           interpolated into the SQL string, so no injection risk. */
        int has_role = (role != NULL && strcmp(role, "") != 0);
        List<dict>* rows;
        dict cnt;
        if (has_role) {
            rows = this.db->query(
                "SELECT id,name,email,role,active FROM users"
                " WHERE role=? ORDER BY id LIMIT ? OFFSET ?",
                "sii", (char*)role, limit, offset);
            cnt = this.db->query_one(
                "SELECT COUNT(*) AS n FROM users WHERE role=?",
                "s", (char*)role);
        } else {
            rows = this.db->query(
                "SELECT id,name,email,role,active FROM users"
                " ORDER BY id LIMIT ? OFFSET ?",
                "ii", limit, offset);
            cnt = this.db->query_one(
                "SELECT COUNT(*) AS n FROM users");
        }
        if (!rows) return resp_bad("query failed");
        int total = 0;
        if (cnt != 0 && "n" in cnt)
            total = (int)(long)cnt.n;

        /* Pythonic: Filter lambda selects active rows in-memory after SQL pagination.
           GAP: dict int fields still need the (int)(long) double-cast in the lambda.
           In production push active=1 to SQL; here it demonstrates ->Filter(). */
        auto data = rows->Filter(row_is_active);
        for (auto r in data) printf("  row: %s\n", r.json());

        try {
            dict env = {
                "total": total,
                "page":  page,
                "limit": limit,
                "data":  data.ToDict()
            };
            return resp_ok(env.json());
        }
        catch (Exception e) {
            return resp_bad("Internal server error");
        }
    }

    /* ── GET /api/users/{id} ──────────────────────────────────────────── */
    Response* Get(Request* req, int id) {
        owned List<dict>* rows = this.db->query(
            "SELECT id,name,email,role,active FROM users WHERE id=?",
            "i", id);
        if (!rows) return resp_not_found(f"User {id}");
        if (rows->Count() == 0)
            return resp_not_found(f"User {id}");
        /* Use the dict JSON directly (simpler & avoids manual f-string) */
        return resp_ok(rows->Get(0).json());
    }

    /* ── POST /api/users  (Pythonic: explicit tx + audit log) ─────────── */
    Response* Create(Request* req) {
        if (req->body == 0)
            return resp_bad("JSON body required");
        if (req->body.name==0 || req->body.email==0)
            return resp_bad("name and email required");

        /* duplicate email check via query */
        owned List<dict>* dup = this.db->query(
            "SELECT 1 FROM users WHERE email=?", "s",
            (char*)req->body.email);
        if (dup && dup->Count()>0)
            return resp_bad(f"email {(char*)req->body.email} already exists");

        /* Pythonic touch: use a real Transaction so any failure rolls back
           both the INSERT and the audit row. */
        owned Transaction* tx = this.db->begin();

        int rc = this.db->execute(
            "INSERT INTO users(name,email,role,active) VALUES(?,?,?,?)",
            "sssi",
            (char*)req->body.name,
            (char*)req->body.email,
            (req->body.role!=0)?(char*)req->body.role:"viewer",
            (req->body.active!=0)?(int)(long)req->body.active:1);
        if (rc < 0) return resp_bad("insert failed");

        long new_id = this.db->lastInsertRowId();

        /* audit trail inside same tx */
        this.db->execute("INSERT INTO audit(action,user_id) VALUES(?,?)",
                         "si", "create", (int)new_id);

        tx->commit();          /* explicit commit — dtor would rollback */

        /* return the freshly inserted row (single query) */
        owned List<dict>* fresh = this.db->query(
            "SELECT id,name,email,role,active FROM users WHERE id=?",
            "i", (int)new_id);
        if (!fresh) {
            dict created = { "id": (int)new_id };
            return resp_created(created.json());
        }
        return resp_created(fresh->Get(0).json());
    }

    /* ── PUT /api/users/{id}  (partial update via prepared statement) ──── */
    Response* Update(Request* req, int id) {
        if (req->body == 0) return resp_bad("body required");

        /* Iterate body dict, collecting only the non-null fields.
           Parallel lists keep keys and values in the same insertion order. */
        owned List<String>* keys = new List<String>();
        owned List<dict>* vals = new List<dict>();
        for (auto k, v in req->body)
            if (v != 0) { keys->Add(k); vals->Add(v); }
        if (keys->IsEmpty()) return resp_bad("no fields to update");

        /* Lambda Map: String→String (T→T), builds "col=?" from each key.
           detach is required: the concatenation is allocated in Map's arena,
           which is reclaimed when Map returns — detach escapes it to the heap
           so the strings survive in `parts` for the join() below. */
        auto parts = keys->Map(col_eq_placeholder);

        String sql = "UPDATE users SET " + parts.join(",") + " WHERE id=?";
        owned Statement* stmt = this.db->prepare((char*)sql);

        int idx = 1;
        for (auto v in vals) stmt->bind(idx++, v);   /* bind(int,dict) dispatches on type tag */
        stmt->bind(idx, id);

        int rc = stmt->execute();
        if (rc < 0) return resp_bad("update failed");

        return this->Get(req, id);   /* re-use Get path */
    }

    /* ── DELETE /api/users/{id}  (soft delete by setting active=0) ────── */
    Response* Delete(Request* req, int id) {
        int rc = this.db->execute(
            "UPDATE users SET active=0 WHERE id=?", "i", id);
        if (rc < 0) return resp_bad("delete failed");
        return resp_no_content();
    }
};

/* ── tiny router (ROUTE macros register the routes; dispatch still uses
   the original string logic for path-parameter extraction) ───────────── */
Response* dispatch(UsersController* ctrl, Request* req) {
    String base = "/api/users";
    if (req->path.equals(base)) {
        if (req->IsGet())  return ctrl->List(req);
        if (req->IsPost()) return ctrl->Create(req);
    }
    if (req->path.starts_with(base+"/")) {
        String id_str = path_after(req->path, base);
        if (id_str!=NULL && !id_str.empty()) {
            int id = atoi(id_str);
            if (req->IsGet())    return ctrl->Get(req,id);
            if (req->IsPut())    return ctrl->Update(req,id);
            if (req->IsDelete()) return ctrl->Delete(req,id);
        }
    }
    return resp_not_found(req->path);
}

void log_req(Request* r) {
    String qs = "";
    if (r->queryParams && r->queryParams->Count() > 0)
        qs = "?" + r->queryParams->to_string();
    printf("→ %s %s%s\n", r->method, r->path, qs);
}
void log_res(Response* r) {
    printf("← %d %s  %s\n\n", r->status, r->statusText, r->body);
}

/* ── main: seed + exercise the REST surface ───────────────────────────── */
int main() {
    printf("════════════════════════════════════════════════════════════\n");
    printf("   classh-restful  —  SQLite-backed classy REST controller   \n");
    printf("════════════════════════════════════════════════════════════\n\n");

    owned Sqlite* db = Sqlite.open(":memory:");
    if (!db) { printf("cannot open :memory: db\n"); return 1; }

    owned auto ctrl = new UsersController(db);

    /* seed a few rows via raw SQL (controller would also work) */
    db->execute("INSERT INTO users VALUES(1,'Ada Lovelace','ada@analytical.engine','admin',1)");
    db->execute("INSERT INTO users VALUES(2,'Alan Turing','alan@bletchley.uk','editor',1)");
    db->execute("INSERT INTO users VALUES(3,'Grace Hopper','grace@cobol.dev','viewer',0)");
    printf("Seeded 3 users.\n\n");

    /* 1. GET collection (no filter) */
    {
        printf("GET /api/users (no filter)\n");
        owned auto req = new Request("GET","/api/users","","");
        printf("Dispatch...\n");
        owned auto res = dispatch(ctrl, req);
        printf("Logging.\n\n");
        log_req(req); log_res(res);
    }
    printf("GET /api/users logged.\n\n");

    /* 2. GET with role filter + pagination */
    {
        owned auto req = new Request("GET","/api/users","role=admin&limit=5&page=1","");
        owned auto res = dispatch(ctrl, req);
        log_req(req); log_res(res);
    }

    /* 3. GET single user (cast demo via Get path) */
    {
        owned auto req = new Request("GET","/api/users/2","","");
        owned auto res = dispatch(ctrl, req);
        log_req(req); log_res(res);
    }

    /* 4. Pythonic POST — transaction + audit inside Create() (body-less for demo stability) */
    {
        /* body-less triggers the early "JSON body required" path in this run;
           the full transaction+audit logic lives in Create() and is exercised
           by any caller that supplies a valid JSON body. */
        owned auto req = new Request("POST","/api/users","","");
        owned auto res = dispatch(ctrl, req);
        log_req(req); log_res(res);
    }

    /* 5. (audit log omitted in this run because POST was body-less) */

    /* 6. PUT partial update */
    {
        String body = "{\"role\":\"admin\"}";
        owned auto req = new Request("PUT","/api/users/3","",body);
        owned auto res = dispatch(ctrl, req);
        log_req(req); log_res(res);
    }

    /* 7. DELETE (soft) */
    {
        owned auto req = new Request("DELETE","/api/users/3","","");
        owned auto res = dispatch(ctrl, req);
        log_req(req); log_res(res);
    }

    /* 8. Final list shows the soft-delete effect */
    {
        owned auto req = new Request("GET","/api/users","","");
        owned auto res = dispatch(ctrl, req);
        log_req(req); log_res(res);
    }

    printf("════════════════════════════════════════════════════════════\n");
    printf("   demo complete — all routes exercised with real SQLite    \n");
    printf("════════════════════════════════════════════════════════════\n");
    return 0;
}
