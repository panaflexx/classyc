/* classy-controller-like.c  —  REST controller, classyc-idiomatic
 *
 * A "what-if" REST API for a tiny user directory.  Nothing is wired to a
 * network — requests are constructed in code and dispatched through a
 * router — but every line is written the way you would write real classyc:
 *
 *   · HTTP request/response as first-class objects
 *   · In-memory store as List<dict>   (each dict IS the row)
 *   · String methods for routing, parsing, and formatting
 *   · f-strings for log lines and human-readable messages
 *   · d.json now returns String, so it composes with + and f"{…}"
 *   · defer for response cleanup
 *
 * Routes handled
 *   GET    /api/users            – list (optional ?page=N&limit=N&role=X)
 *   GET    /api/users/{id}       – get one
 *   POST   /api/users            – create  (JSON body)
 *   PUT    /api/users/{id}       – update  (JSON body, partial)
 *   DELETE /api/users/{id}       – remove
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ═══════════════════════════════════════════════════════════════════════
   List<T>  (production class — see classy-generics.c for full docs)
   ═══════════════════════════════════════════════════════════════════════ */
class List<T> {
    T*  data;
    int length;
    int capacity;

    List() {
        this.length = 0; this.capacity = 8;
        this.data = (T*) malloc(sizeof(T) * 8);
    }
    List(int cap) {
        this.length = 0;
        this.capacity = cap > 0 ? cap : 8;
        this.data = (T*) malloc(sizeof(T) * this.capacity);
    }
    ~List() { if (this.data) free((void*) this.data); }

    int  Count()    { return this.length; }
    int  IsEmpty()  { return this.length == 0; }
    T    Get(int i) { return this.data[i]; }
    void Set(int i, T item) { this.data[i] = item; }

    void Add(T item) {
        if (this.length >= this.capacity) {
            int nc = this.capacity * 2;
            T* nd = (T*) malloc(sizeof(T) * nc);
            for (int i = 0; i < this.length; i++) nd[i] = this.data[i];
            free((void*) this.data);
            this.data = nd; this.capacity = nc;
        }
        this.data[this.length++] = item;
    }

    void RemoveAt(int idx) {
        if (idx < 0 || idx >= this.length) return;
        for (int i = idx; i < this.length - 1; i++) this.data[i] = this.data[i + 1];
        this.length--;
    }

    List<T>* Concat(List<T>* other) {
        for (auto item in other) this->Add(item);
        return this;
    }
};

/* ═══════════════════════════════════════════════════════════════════════
   String utilities
   ═══════════════════════════════════════════════════════════════════════ */

/* Extract a query-string parameter value, or NULL if absent.
   e.g.  qparam("page=2&limit=5", "page")  →  "2"              */
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

/* Extract the path segment after a known prefix, stripping a leading slash.
   e.g.  path_after("/api/users/42", "/api/users")  →  "42"        */
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
   Request
   ═══════════════════════════════════════════════════════════════════════ */
class Request {
    String method;   /* normalised to upper-case                          */
    String path;     /* normalised to lower-case, trimmed                 */
    String query;    /* raw query string, e.g. "page=1&limit=10"          */
    dict   body;     /* parsed from JSON body, or NULL                    */

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

    /* Convenience: pull one query param */
    String QueryParam(String key) { return qparam(this.query, key); }
};

/* ═══════════════════════════════════════════════════════════════════════
   Response
   ═══════════════════════════════════════════════════════════════════════ */
class Response {
    int    status;
    String statusText;
    String body;      /* raw JSON string ready to send                    */

    Response(int status, String statusText, String body) {
        this.status     = status;
        this.statusText = statusText;
        this.body       = body.detach();
    }

    void Print() {
        printf("HTTP/1.1 %d %s\n", this.status, this.statusText);
        printf("Content-Type: application/json\n");
        printf("Content-Length: %d\n\n", (int) this.body.length());
        printf("%s\n", this.body);
    }
};

/* ── Response factory helpers ───────────────────────────────────────── */

Response* resp_ok(String body) {
    return new Response(200, "OK", body);
}
Response* resp_created(String body) {
    return new Response(201, "Created", body);
}
Response* resp_no_content() {
    return new Response(204, "No Content", "{}");
}
Response* resp_bad_request(String msg) {
    return new Response(400, "Bad Request", f"{{\"error\":\"{msg}\"}}");
}
Response* resp_not_found(String resource) {
    return new Response(404, "Not Found", f"{{\"error\":\"{resource} not found\"}}");
}
Response* resp_conflict(String msg) {
    return new Response(409, "Conflict", f"{{\"error\":\"{msg}\"}}");
}

/* ═══════════════════════════════════════════════════════════════════════
   UsersController
   ═══════════════════════════════════════════════════════════════════════ */
class UsersController {
    List<dict>* db;
    int         next_id;

    UsersController() {
        this.db      = new List<dict>();
        this.next_id = 1;

        /* Seed the in-memory store with a few users */
        dict alice = { "id": 1, "name": "Alice Nguyen",
                       "email": "alice@example.com", "role": "admin",  "active": 1 };
        dict bob   = { "id": 2, "name": "Bob Okafor",
                       "email": "bob@example.com",   "role": "editor", "active": 1 };
        dict carol = { "id": 3, "name": "Carol Lima",
                       "email": "carol@example.com", "role": "viewer", "active": 0 };
        this.db->Add(alice);
        this.db->Add(bob);
        this.db->Add(carol);
        this.next_id = 4;
    }

    ~UsersController() {
        delete this.db;
    }

    /* ── internal helpers ────────────────────────────────────────────── */

    /* Find the db index of user with the given id, or -1. */
    int FindIndex(int id) {
        for (int i = 0; i < this.db->Count(); i++)
            if ((int)this.db->Get(i).id == id) return i;
        return -1;
    }

    /* Render the store (or a slice) as a JSON array string. */
    String UsersToJson(int from, int to) {
        String out = "[";
        int first = 1;
        for (int i = from; i < to && i < this.db->Count(); i++) {
            if (!first) out = out + ",";
            out   = out + this.db->Get(i).json;
            first = 0;
        }
        return out + "]";
    }

    /* ── GET /api/users ──────────────────────────────────────────────── */
    Response* List(Request* req) {
        int total = this.db->Count();

        /* optional ?role=X filter — build a filtered snapshot */
        String role_filter = req->QueryParam("role");
        List<dict>* view = new List<dict>(total);
        defer delete view;

        for (int i = 0; i < total; i++) {
            dict u = this.db->Get(i);
            if (role_filter == NULL || strcmp(role_filter, "") == 0)
                view->Add(u);
            else if (u.role != 0 && strcmp((char*)u.role, role_filter) == 0)
                view->Add(u);
        }

        /* optional ?page=N&limit=N pagination */
        String page_s  = req->QueryParam("page");
        String limit_s = req->QueryParam("limit");
        int page  = page_s  != NULL ? atoi(page_s)  : 1;
        int limit = limit_s != NULL ? atoi(limit_s) : view->Count();
        if (page  < 1) page  = 1;
        if (limit < 1) limit = view->Count();

        int from  = (page - 1) * limit;
        int to    = from + limit;
        if (from > view->Count()) from = view->Count();
        if (to   > view->Count()) to   = view->Count();

        /* Build JSON array for the page */
        String arr = "[";
        int first = 1;
        for (int i = from; i < to; i++) {
            if (!first) arr = arr + ",";
            arr   = arr + view->Get(i).json;
            first = 0;
        }
        arr = arr + "]";

        /* Wrap in a pagination envelope */
        String body = f"{{\"total\":{view->Count()},\"page\":{page},\"limit\":{limit},\"data\":{arr}}}";
        return resp_ok(body);
    }

    /* ── GET /api/users/{id} ─────────────────────────────────────────── */
    Response* Get(Request* req, int id) {
        int idx = this->FindIndex(id);
        if (idx < 0)
            return resp_not_found(f"User {id}");
        return resp_ok(this.db->Get(idx).json);
    }

    /* ── POST /api/users ─────────────────────────────────────────────── */
    Response* Create(Request* req) {
        if (req->body == 0)
            return resp_bad_request("Request body is required");

        /* Validate required fields */
        if (req->body.name == 0 || req->body.email == 0)
            return resp_bad_request("name and email are required");

        /* Check for duplicate email */
        for (int i = 0; i < this.db->Count(); i++) {
            dict u = this.db->Get(i);
            if (u.email != 0 && strcmp((char*)u.email, (char*)req->body.email) == 0)
                return resp_conflict(f"Email {(char*)req->body.email} already registered");
        }

        /* Build the new user dict */
        int new_id = this.next_id++;
        dict user = {};
        user.id    = new_id;
        user.name  = req->body.name;
        user.email = req->body.email;
        user.role  = "viewer";
        if (req->body.role != 0) user.role = req->body.role;
        user.active = 1;
        this.db->Add(user);

        return resp_created(user.json);
    }

    /* ── PUT /api/users/{id} ─────────────────────────────────────────── */
    Response* Update(Request* req, int id) {
        int idx = this->FindIndex(id);
        if (idx < 0)
            return resp_not_found(f"User {id}");
        if (req->body == 0)
            return resp_bad_request("Request body is required");

        /* Partial update: copy existing, overlay provided fields */
        dict existing = this.db->Get(idx);
        if (req->body.name  != 0) existing.name  = req->body.name;
        if (req->body.email != 0) existing.email = req->body.email;
        if (req->body.role  != 0) existing.role  = req->body.role;
        /* active is numeric — check via value property */
        if (req->body.active != 0) existing.active = req->body.active;

        /* Write back */
        this.db->Set(idx, existing);

        return resp_ok(this.db->Get(idx).json);
    }

    /* ── DELETE /api/users/{id} ──────────────────────────────────────── */
    Response* Delete(Request* req, int id) {
        int idx = this->FindIndex(id);
        if (idx < 0)
            return resp_not_found(f"User {id}");
        this.db->RemoveAt(idx);
        return resp_no_content();
    }
};

/* ═══════════════════════════════════════════════════════════════════════
   Router
   ═══════════════════════════════════════════════════════════════════════ */

Response* dispatch(UsersController* ctrl, Request* req) {
    String base = "/api/users";

    /* ── collection routes (exact base path) ── */
    if (strcmp(req->path, base) == 0) {
        if (req->IsGet())    return ctrl->List(req);
        if (req->IsPost())   return ctrl->Create(req);
    }

    /* ── resource routes (base + /id) ── */
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

/* ── request logging ─────────────────────────────────────────────────── */
void log_request(Request* req) {
    String has_body = req->body != 0 ? " (+body)" : "";
    printf("→ %s %s%s%s\n",
           req->method, req->path,
           req->query.empty() ? "" : "?",
           req->query);
}

void log_response(Response* res) {
    printf("← %d %s  %s\n\n", res->status, res->statusText, res->body);
}

/* ═══════════════════════════════════════════════════════════════════════
   main — simulate a series of REST calls
   ═══════════════════════════════════════════════════════════════════════ */

int main() {
    auto ctrl = new UsersController();
    defer delete ctrl;

    printf("════════════════════════════════════════\n");
    printf("  classyc REST controller (in-process)  \n");
    printf("════════════════════════════════════════\n\n");

    /* ── 1. List all users ─────────────────────────────────────────── */
    {
        auto req = new Request("GET", "/api/users", "", NULL);
        defer delete req;
        auto res = dispatch(ctrl, req);
        defer delete res;
        log_request(req);
        log_response(res);
    }

    /* ── 2. Paginated + role-filtered listing ──────────────────────── */
    {
        auto req = new Request("GET", "/api/users", "role=editor&limit=5&page=1", NULL);
        defer delete req;
        auto res = dispatch(ctrl, req);
        defer delete res;
        log_request(req);
        log_response(res);
    }

    /* ── 3. Get by id ──────────────────────────────────────────────── */
    {
        auto req = new Request("GET", "/api/users/2", "", NULL);
        defer delete req;
        auto res = dispatch(ctrl, req);
        defer delete res;
        log_request(req);
        log_response(res);
    }

    /* ── 4. Get non-existent id ────────────────────────────────────── */
    {
        auto req = new Request("GET", "/api/users/99", "", NULL);
        defer delete req;
        auto res = dispatch(ctrl, req);
        defer delete res;
        log_request(req);
        log_response(res);
    }

    /* ── 5. Create a new user ──────────────────────────────────────── */
    {
        auto req = new Request("POST", "/api/users", "",
            "{\"name\":\"Dave Singh\",\"email\":\"dave@example.com\",\"role\":\"editor\"}");
        defer delete req;
        auto res = dispatch(ctrl, req);
        defer delete res;
        log_request(req);
        log_response(res);
    }

    /* ── 6. Create duplicate email → 409 ──────────────────────────── */
    {
        auto req = new Request("POST", "/api/users", "",
            "{\"name\":\"Alice Clone\",\"email\":\"alice@example.com\"}");
        defer delete req;
        auto res = dispatch(ctrl, req);
        defer delete res;
        log_request(req);
        log_response(res);
    }

    /* ── 7. Create with missing fields → 400 ──────────────────────── */
    {
        auto req = new Request("POST", "/api/users", "",
            "{\"name\":\"No Email\"}");
        defer delete req;
        auto res = dispatch(ctrl, req);
        defer delete res;
        log_request(req);
        log_response(res);
    }

    /* ── 8. Partial update ─────────────────────────────────────────── */
    {
        auto req = new Request("PUT", "/api/users/1", "",
            "{\"role\":\"superadmin\"}");
        defer delete req;
        auto res = dispatch(ctrl, req);
        defer delete res;
        log_request(req);
        log_response(res);
    }

    /* ── 9. Delete ─────────────────────────────────────────────────── */
    {
        auto req = new Request("DELETE", "/api/users/3", "", NULL);
        defer delete req;
        auto res = dispatch(ctrl, req);
        defer delete res;
        log_request(req);
        log_response(res);
    }

    /* ── 10. List after mutations: f-string shows final state ──────── */
    {
        auto req = new Request("GET", "/api/users", "", NULL);
        defer delete req;
        auto res = dispatch(ctrl, req);
        defer delete res;
        printf("── Final state ─────────────────────────\n");
        log_request(req);
        log_response(res);
    }

    return 0;
}
