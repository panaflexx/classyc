/* httpserve.h — shared contract for the ClassyC "gunicorn-like" HTTP server.
 *
 * A base server (http-serve.c) and an application (e.g. classy-http-app.c) are
 * compiled together as two translation units and linked into one program:
 *
 *     ./bin/classyc -I include -l crypto -l sqlite \
 *                   examples/http-serve.c examples/classy-http-app.c -eg
 *
 * This header is included by BOTH units.  The `Request` / `Response` classes
 * (and the small routing/response helpers) are therefore emitted in each unit;
 * the driver enables MIR func-redef permission so the identical definitions
 * link cleanly (C++-inline / ODR semantics) — that is what lets the two sides
 * share rich, method-carrying objects across the TU boundary.
 *
 * The boundary itself is two `extern` functions:
 *   · app_handle(req) — the APPLICATION implements it; the server calls it once
 *                       per request and takes ownership of the returned Response.
 *   · serve(port)     — the SERVER implements it; the application's main() calls
 *                       it to start the accept loop.
 */
#ifndef CLASSYC_HTTPSERVE_H
#define CLASSYC_HTTPSERVE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ═══════════════════════════════════════════════════════════════════════
   Small query-string / path helpers (shared by app routing)
   ═══════════════════════════════════════════════════════════════════════ */

/* Extract a query-string parameter value, or NULL if absent.
   e.g.  qparam("page=2&limit=5", "page")  →  "2"
   `static`: this helper is defined in a header included by several TUs; making
   it internal-linkage gives each unit its own copy and avoids a duplicate
   symbol when AOT-linking the objects together.                            */
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

/* Extract the path segment after a known prefix, stripping a leading slash.
   e.g.  path_after("/api/users/42", "/api/users")  →  "42"   (static, see above) */
static String path_after(String path, String prefix) {
    if (!path.starts_with(prefix)) return NULL;
    int plen = (int) prefix.length();
    if (plen >= (int) path.length()) return "";
    String rest = path.substr(plen, (int)path.length() - plen);
    if (rest.starts_with("/"))
        return rest.substr(1, (int)rest.length() - 1);
    return rest;
}

/* ═══════════════════════════════════════════════════════════════════════
   Request — one parsed HTTP request, as a first-class object
   ═══════════════════════════════════════════════════════════════════════ */
class Request {
    String method;   /* normalised to upper-case                          */
    String path;     /* trimmed; case preserved (ids are case-sensitive)  */
    String query;    /* raw query string, e.g. "page=1&limit=10"          */
    dict   body;     /* parsed from a JSON body, or 0                      */

    Request(String method, String path, String query, String bodyJson) {
        /* Value-semantic String fields: these copies are owned by the Request
           and freed with it — no manual cleanup. */
        this.method = method.trim().upper();
        this.path   = path.trim();
        if ((char*)query != NULL) this.query = query; else this.query = "";
        this.body   = 0;
        if ((char*)bodyJson != NULL && strlen((char*)bodyJson) > 0)
            this.body = json(bodyJson);
    }

    /* The Request owns the dict it parsed; free it with the request.
       `delete` is null-safe, so the no-body case is fine. */
    ~Request() { delete this.body; }

    int IsGet()    { return strcmp(this.method, "GET")    == 0; }
    int IsPost()   { return strcmp(this.method, "POST")   == 0; }
    int IsPut()    { return strcmp(this.method, "PUT")    == 0; }
    int IsDelete() { return strcmp(this.method, "DELETE") == 0; }

    String QueryParam(String key) { return qparam(this.query, key); }
};

/* ═══════════════════════════════════════════════════════════════════════
   Response — status + JSON body, knows how to serialise itself to the wire
   ═══════════════════════════════════════════════════════════════════════ */
class Response {
    int    status;
    String statusText;
    String body;      /* JSON string ready to send                        */
    int    keep_alive; /* 1 => "Connection: keep-alive" (server core sets) */

    Response(int status, String statusText, String body) {
        this.status     = status;
        this.statusText = statusText;
        /* `body` is a transient arena String; this copies it into a buffer the
           Response owns and frees on destruction. */
        this.body       = body;
        this.keep_alive = 0;
    }

    /* Render the full HTTP/1.1 response message (status line + headers + body).
       Content-Length is the BYTE length (strlen), which is what the wire needs
       even when the body contains multi-byte UTF-8. */
    String wire() {
        String b = this.body;
        int n = (int) strlen((char*)b);
        String conn = this.keep_alive ? "keep-alive" : "close";
        return f"HTTP/1.1 {this.status} {this.statusText}\r\nContent-Type: application/json\r\nContent-Length: {n}\r\nConnection: {conn}\r\n\r\n{b}";
    }
};

/* ── Response factory helpers ─────────────────────────────────────────────
   `static` for the same reason as the helpers above: header-defined free
   functions get internal linkage so several TUs can include this header and
   still AOT-link without duplicate-symbol errors. */

static Response* resp_ok(String body)          { return new Response(200, "OK", body); }
static Response* resp_created(String body)     { return new Response(201, "Created", body); }
static Response* resp_no_content()             { return new Response(204, "No Content", "{}"); }
static Response* resp_bad_request(String msg)  { return new Response(400, "Bad Request", f"{{\"error\":\"{msg}\"}}"); }
static Response* resp_not_found(String what)   { return new Response(404, "Not Found", f"{{\"error\":\"{what} not found\"}}"); }
static Response* resp_conflict(String msg)     { return new Response(409, "Conflict", f"{{\"error\":\"{msg}\"}}"); }
static Response* resp_500(String msg)          { return new Response(500, "Internal Server Error", f"{{\"error\":\"{msg}\"}}"); }

/* ═══════════════════════════════════════════════════════════════════════
   The server ↔ application contract
   ═══════════════════════════════════════════════════════════════════════ */

/* Implemented by the APPLICATION.  Called once per request; the returned
   Response* is owned by the server, which deletes it after sending. */
extern Response* app_handle(Request* req);

/* Implemented by the SERVER (http-serve.c).  Starts the accept loop on `port`
   and never returns under normal operation.  The application's main() calls it. */
extern int serve(int port);

/* Implemented by the FIBER SERVER (http-serve-fibers.c).  Like serve(), but
   every connection is handled by its own minicoro fiber with non-blocking
   sockets on a single OS thread — many concurrent clients, no pthreads. */
extern int serve_fibers(int port);

/* ═══════════════════════════════════════════════════════════════════════
   Attribute-based routing (ASP.NET-style auto-discovery)
   ═══════════════════════════════════════════════════════════════════════

   A controller registers a route by dropping a `RouteReg` into the "routes"
   registry with the C23 `[[registry("routes")]]` attribute — no central table,
   no manual wiring.  The compiler + linker gather every such record across all
   translation units (JIT: driver module scan; AOT: ELF/Mach-O linker set), and
   `route_dispatch` matches an incoming request against them.

   In a controller:

       static Response* addpatient_post(Request* req) { ... }
       ROUTE("POST", "/api/addpatient", addpatient_post);

   In the application entry point:

       Response* app_handle(Request* req) { return route_dispatch(req); }
       int main() { return serve(8080); }
*/
typedef struct {
    const char* method;   /* "GET" / "POST" / ...            */
    const char* path;     /* exact request path to match     */
    Response* (*handler)(Request*);
} RouteReg;

/* The linker set: an array of RouteReg* gathered from every module. */
extern RouteReg* __start_cyreg_routes[];
extern RouteReg* __stop_cyreg_routes[];

/* Register `fn` for (method, path).  `fn` must be a unique identifier token. */
#define ROUTE(method, path, fn) \
    [[registry("routes")]] static RouteReg __cy_route_##fn = { (method), (path), (fn) }

/* Match a request against every registered route; 404 if none match.
   `static` so each TU gets its own copy (only the app entry point uses it). */
static Response* route_dispatch(Request* req) {
    for (RouteReg** p = __start_cyreg_routes; p < __stop_cyreg_routes; p++) {
        RouteReg* r = *p;
        if (strcmp((char*)req->path, r->path) == 0
            && strcmp((char*)req->method, r->method) == 0)
            return r->handler(req);
    }
    return resp_not_found(req->path);
}

#endif /* CLASSYC_HTTPSERVE_H */
