/* httpserve.h — shared contract for the ClassyC "gunicorn-like" HTTP server.
 *
 * A base server (http-serve.c / http-serve-fibers.c) and an application are
 * compiled together as multiple translation units:
 *
 *     ./bin/classyc -I include -l sqlite3 \
 *         examples/http-serve.c \
 *         examples/http_crud/main.cy examples/http_crud/items.cy -eg
 *
 * Controllers register routes with [[HttpGet]] / ROUTE() → registry("routes").
 * Apps enumerate them with route_list() (a List<RouteReg>) and dispatch with
 * route_dispatch().  Path templates may include `{param}` segments.
 */
#ifndef CLASSYC_HTTPSERVE_H
#define CLASSYC_HTTPSERVE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "list.h"

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
    dict   body;       /* JSON body, or null (only parsed when it looks like JSON) */
    String params;     /* path captures as "id=42&slug=foo" (query-string shape) */
    /* Raw recv-buffer views. Optional: server cores that have the original
       bytes call req_attach_raw() after construction. Needed for Cookie /
       Content-Type lookup and for non-JSON bodies (multipart, urlencoded).
       Not owned — they alias the connection buffer for the request lifetime. */
    char *raw_body;
    long  body_len;
    char *header_buf;
    long  header_len;

    Request(String method, String path, String query, String bodyJson) {
        this.method = method.trim().upper();
        this.path   = path.trim();
        if ((char*)query != NULL) this.query = query;
        else this.query = "";
        this.params = "";
        this.body   = 0;
        this.raw_body = 0;
        this.body_len = 0;
        this.header_buf = 0;
        this.header_len = 0;
        /* Login/upload send urlencoded or multipart; json() on those is
           either a throw or garbage. Only auto-parse object/array JSON. */
        if ((char*)bodyJson != NULL && strlen((char*)bodyJson) > 0) {
            char *p = (char*)bodyJson;
            while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
            if (*p == '{' || *p == '[')
                this.body = json(bodyJson);
        }
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

    /* Case-insensitive, line-anchored lookup in the raw header block.
       Returns a fresh arena String, or NULL if the header is absent. */
    String header(String name) {
        if (this.header_buf == 0 || this.header_len <= 0 || (char*)name == 0)
            return 0;
        const char *buf = this.header_buf;
        long header_end = this.header_len;
        const char *n = (char*)name;
        int nlen = (int)strlen(n);
        for (long i = 0; i + nlen < header_end; i++) {
            if (i > 0 && buf[i - 1] != '\n') continue;
            int match = 1;
            for (int j = 0; j < nlen; j++) {
                char a = buf[i + j], b = n[j];
                if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
                if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
                if (a != b) { match = 0; break; }
            }
            if (!match) continue;
            long k = i + nlen;
            if (buf[k] != ':') continue;
            k++;
            while (k < header_end && buf[k] == ' ') k++;
            long vstart = k;
            while (k < header_end && buf[k] != '\r' && buf[k] != '\n') k++;
            long vlen = k - vstart;
            char valbuf[1024];
            if (vlen >= (long)sizeof(valbuf)) vlen = (long)sizeof(valbuf) - 1;
            memcpy(valbuf, buf + vstart, (size_t)vlen);
            valbuf[vlen] = 0;
            return f"{valbuf}";
        }
        return 0;
    }

    /* Cookie: name lookup in the Cookie header. NULL if missing. */
    String cookie(String name) {
        String cookies = this.header("Cookie");
        char *p = (char*)cookies;
        if (!p || (char*)name == 0) return 0;
        int nlen = (int)strlen((char*)name);
        while (*p) {
            while (*p == ' ' || *p == ';') p++;
            if (!*p) break;
            char *eq = strchr(p, '=');
            if (!eq) break;
            char *semi = strchr(eq, ';');
            int klen = (int)(eq - p);
            int vlen = semi ? (int)(semi - eq - 1) : (int)strlen(eq + 1);
            if (klen == nlen && strncmp(p, (char*)name, (size_t)nlen) == 0) {
                char valbuf[256];
                if (vlen >= (int)sizeof(valbuf)) vlen = (int)sizeof(valbuf) - 1;
                memcpy(valbuf, eq + 1, (size_t)vlen);
                valbuf[vlen] = 0;
                return f"{valbuf}";
            }
            p = semi ? semi + 1 : eq + 1 + vlen;
        }
        return 0;
    }

    /* Back-compat aliases */
    String QueryParam(String key) { return qparam(this.query, key); }
    String PathParam(String key)  { return qparam(this.params, key); }
    int    PathParamInt(String key) { return this.argInt(key); }
};

/* Stash recv-buffer views on a Request. Not owned; valid until the caller
   recycles the connection buffer. body may contain embedded NULs (multipart). */
static void req_attach_raw(Request *req, char *buf, long header_end,
                           char *body, long body_len) {
    if (req == 0) return;
    req->header_buf = buf;
    req->header_len = header_end;
    req->raw_body = body_len > 0 ? body : 0;
    req->body_len = body_len;
}

/* ── Response ────────────────────────────────────────────────────────── */

class Response {
    int    status;
    String statusText;
    String body;
    int    keep_alive;  /* set by the server core */
    String contentType; /* default application/json */
    String setCookie;   /* "" when absent; emitted as Set-Cookie */

    Response(int status, String statusText, String body) {
        this.status      = status;
        this.statusText  = statusText;
        this.body        = body;
        this.keep_alive  = 0;
        this.contentType = "application/json";
        this.setCookie   = "";
    }

    String wire() {
        String b = this.body;
        int n = (int) strlen((char*)b);
        String conn;
        if (this.keep_alive) conn = "keep-alive";
        else conn = "close";
        int st = this.status;
        String stxt = this.statusText;
        String ct = this.contentType;
        if ((char*)ct == NULL || ((char*)ct)[0] == 0) ct = "application/json";
        if ((char*)this.setCookie != NULL && ((char*)this.setCookie)[0] != 0) {
            String ck = this.setCookie;
            return f"HTTP/1.1 {st} {stxt}\r\nContent-Type: {ct}\r\nContent-Length: {n}\r\nConnection: {conn}\r\nSet-Cookie: {ck}\r\n\r\n{b}";
        }
        return f"HTTP/1.1 {st} {stxt}\r\nContent-Type: {ct}\r\nContent-Length: {n}\r\nConnection: {conn}\r\n\r\n{b}";
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
extern int serve_workers_cchan(int port, int nworkers); /* http-serve-cchan.c (pthreads, no fibers) */

/* Shared listen / client helpers (http-serve.c).
   http_listen binds+listens and returns the listen fd, or -1 on error.
   http_handle_client serves the connection (HTTP/1.1 keep-alive) then returns;
   the caller owns and closes cfd.  Apps just return a Response. */
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
 *
 *     for (auto r in route_list())
 *         printf("%s %s\n", r.method, r.path);
 */

typedef struct {
    const char* method;
    const char* path;   /* exact or template with {param} segments */
    Response* (*handler)(Request*);
} RouteReg;

/* Linker set filled by [[registry("routes")]].  Prefer route_list(). */
extern RouteReg* __start_cyreg_routes[];
extern RouteReg* __stop_cyreg_routes[];

/* Snapshot of every registered route (by-value copies of the POD records).
   Strings and handlers stay owned by the registry; the List is a RAII shell. */
static List<RouteReg> route_list(void) {
    auto rs = List<RouteReg>();
    for (RouteReg** p = __start_cyreg_routes; p < __stop_cyreg_routes; p++)
        if (*p) rs.Add(**p);
    return move rs;
}

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
