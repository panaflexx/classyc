/* httpserve.h — shared contract for the ClassyC "gunicorn-like" HTTP server.
 *
 * A base server (http-serve.c / http-serve-fibers.c) and an application are
 * compiled together as multiple translation units:
 *
 *     ./bin/classyc -I include -l sqlite3 \
 *         examples/http-serve.c \
 *         examples/http_crud/main.cy examples/http_crud/items.cy -eg
 *
 * Controllers register routes with ROUTE() → [[registry("routes")]]; the app
 * entry point only calls route_dispatch().  Path templates may include
 * `{param}` segments (Flask-style).
 */
#ifndef CLASSYC_HTTPSERVE_H
#define CLASSYC_HTTPSERVE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── tiny string helpers (static → each TU gets its own copy for AOT) ── */

/* qparam("page=2&limit=5", "page") → "2" */
static String qparam(String qs, String key) {
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

/* path_after("/api/users/42", "/api/users") → "42" */
static String path_after(String path, String prefix) {
    if (!path.starts_with(prefix)) return NULL;
    int plen = (int) prefix.length();
    if (plen >= (int) path.length()) return "";
    String rest = path.substr(plen, (int)path.length() - plen);
    if (rest.starts_with("/"))
        return rest.substr(1, (int)rest.length() - 1);
    return rest;
}

/* ── Request ─────────────────────────────────────────────────────────── */

class Request {
    String method;     /* upper-case */
    String path;       /* as received (trimmed) */
    String query;      /* raw ?a=1&b=2 */
    dict   body;       /* JSON body, or null */
    String params;     /* path captures as "id=42&slug=foo" (query-string shape) */

    Request(String method, String path, String query, String bodyJson) {
        this.method = method.trim().upper();
        this.path   = path.trim();
        if ((char*)query != NULL) this.query = query;
        else this.query = "";
        this.params = "";
        this.body   = 0;
        if ((char*)bodyJson != NULL && strlen((char*)bodyJson) > 0)
            this.body = json(bodyJson);
    }

    ~Request() { delete this.body; }

    int IsGet()    { return strcmp(this.method, "GET")    == 0; }
    int IsPost()   { return strcmp(this.method, "POST")   == 0; }
    int IsPut()    { return strcmp(this.method, "PUT")    == 0; }
    int IsDelete() { return strcmp(this.method, "DELETE") == 0; }

    /* Flask-style: req->arg("page") / req->arg("id") */
    String arg(String key) {
        String from_path = qparam(this.params, key);
        if ((char*)from_path != NULL) return from_path;
        return qparam(this.query, key);
    }

    int argInt(String key) {
        String s = this.arg(key);
        if ((char*)s == NULL || ((char*)s)[0] == 0) return 0;
        return atoi((char*)s);
    }

    /* Back-compat aliases */
    String QueryParam(String key) { return qparam(this.query, key); }
    String PathParam(String key)  { return qparam(this.params, key); }
    int    PathParamInt(String key) { return this.argInt(key); }
};

/* ── Response ────────────────────────────────────────────────────────── */

class Response {
    int    status;
    String statusText;
    String body;
    int    keep_alive; /* set by the server core */

    Response(int status, String statusText, String body) {
        this.status     = status;
        this.statusText = statusText;
        this.body       = body;
        this.keep_alive = 0;
    }

    String wire() {
        String b = this.body;
        int n = (int) strlen((char*)b);
        String conn;
        if (this.keep_alive) conn = "keep-alive";
        else conn = "close";
        int st = this.status;
        String stxt = this.statusText;
        return f"HTTP/1.1 {st} {stxt}\r\nContent-Type: application/json\r\nContent-Length: {n}\r\nConnection: {conn}\r\n\r\n{b}";
    }
};

static Response* resp_ok(String body)         { return new Response(200, "OK", body); }
static Response* resp_created(String body)    { return new Response(201, "Created", body); }
static Response* resp_no_content()            { return new Response(204, "No Content", "{}"); }
static Response* resp_bad_request(String msg) { return new Response(400, "Bad Request", f"{{\"error\":\"{msg}\"}}"); }
static Response* resp_not_found(String what)  { return new Response(404, "Not Found", f"{{\"error\":\"{what} not found\"}}"); }
static Response* resp_conflict(String msg)    { return new Response(409, "Conflict", f"{{\"error\":\"{msg}\"}}"); }
static Response* resp_500(String msg)         { return new Response(500, "Internal Server Error", f"{{\"error\":\"{msg}\"}}"); }

/* ── server ↔ app contract ───────────────────────────────────────────── */

extern Response* app_handle(Request* req);
extern int serve(int port);                      /* http-serve.c — blocking */
extern int serve_fibers(int port);               /* http-serve-fibers.c */
extern int serve_workers(int port, int nworkers); /* http-serve-workers.cy (-ffibers) */

/* Shared listen / one-shot client helpers (http-serve.c).
   http_listen binds+listens and returns the listen fd, or -1 on error.
   http_handle_client reads one request, calls app_handle, writes the response
   (does not close cfd — the caller owns the fd). */
extern int  http_listen(int port);
extern void http_handle_client(int cfd);

/* ── attribute routing ─────────────────────────────────────────────────
 *
 * Prefer C23 attributes on the handler (ASP.NET / Spring style).  The
 * compiler synthesizes a [[registry("routes")]] RouteReg for each one:
 *
 *     [[HttpGet("/api/items/{id}")]]
 *     static Response* items_get(Request* req) {
 *         int id = req->argInt("id");
 *         ...
 *     }
 *
 *     [[HttpPost("/api/items")]]
 *     static Response* items_create(Request* req) { ... }
 *
 * Supported attributes (one string path arg, except HttpRoute):
 *     [[HttpGet(path)]]  [[HttpPost(path)]]  [[HttpPut(path)]]
 *     [[HttpDelete(path)]]  [[HttpPatch(path)]]
 *     [[HttpRoute(method, path)]]   // e.g. [[HttpRoute("OPTIONS", "/")]]
 *
 * Legacy macro (still works — expands to the same registry static):
 *     ROUTE("GET", "/api/items/{id}", items_get);
 *
 *     Response* app_handle(Request* req) { return route_dispatch(req); }
 */

typedef struct {
    const char* method;
    const char* path;   /* exact or template with {param} segments */
    Response* (*handler)(Request*);
} RouteReg;

extern RouteReg* __start_cyreg_routes[];
extern RouteReg* __stop_cyreg_routes[];

/* Legacy: explicit ROUTE table entry (prefer [[HttpGet]] / etc. on the fn). */
#define ROUTE(method, path, fn) \
    [[registry("routes")]] static RouteReg __cy_route_##fn = { (method), (path), (fn) }

/* Match a Flask-style template against a path; build captures as "k=v&k2=v2".
   On match returns the capture String (return-by-value so the String arena
   keeps it — do NOT write through String*, which dangles after return).
   On mismatch returns NULL. */
static String route_match(const char* pattern, const char* path) {
    if (pattern == NULL || path == NULL) return NULL;
    String caps = "";
    int ncap = 0;
    const char* p = pattern;
    const char* s = path;

    while (*p != 0 || *s != 0) {
        if (*p == '{') {
            const char* pe = p + 1;
            while (*pe && *pe != '}') pe++;
            if (*pe != '}') return NULL;
            int klen = (int)(pe - (p + 1));
            if (klen <= 0) return NULL;

            const char* se = s;
            while (*se && *se != '/') se++;
            int vlen = (int)(se - s);
            if (vlen <= 0) return NULL;

            /* Append k=v (values are path segments — no '&'/'='). */
            char keybuf[64], valbuf[256];
            if (klen >= (int)sizeof(keybuf)) klen = (int)sizeof(keybuf) - 1;
            if (vlen >= (int)sizeof(valbuf)) vlen = (int)sizeof(valbuf) - 1;
            memcpy(keybuf, p + 1, klen); keybuf[klen] = 0;
            memcpy(valbuf, s, vlen);     valbuf[vlen] = 0;

            String piece = f"{keybuf}={valbuf}";
            if (ncap == 0) caps = piece;
            else           caps = caps + "&" + piece;
            ncap++;

            p = pe + 1;
            s = se;
            continue;
        }
        if (*p == 0 || *s == 0 || *p != *s) return NULL;
        p++;
        s++;
    }
    return caps;
}

static Response* route_dispatch(Request* req) {
    for (RouteReg** p = __start_cyreg_routes; p < __stop_cyreg_routes; p++) {
        RouteReg* r = *p;
        if (strcmp((char*)req->method, r->method) != 0) continue;

        if (strchr(r->path, '{') == NULL) {
            if (strcmp((char*)req->path, r->path) == 0)
                return r->handler(req);
            continue;
        }

        String caps = route_match(r->path, (char*)req->path);
        if ((char*)caps != NULL) {
            /* Class field: c2m_str_own copies into the Request's private heap. */
            req->params = caps;
            return r->handler(req);
        }
    }
    return resp_not_found(req->path);
}

#endif /* CLASSYC_HTTPSERVE_H */
