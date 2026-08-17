/* test-routes.cy — exercise attribute-routed controllers without a socket.
 * Compile with the controllers (which self-register); enumerate the routes
 * and drive route_dispatch() with synthetic requests. */

#include "httpserve.h"
#include <stdio.h>

static void show(Request* req) {
    Response* r = route_dispatch(req);
    printf("  %-4s %-18s -> %d  %s\n",
           (char*)req->method, (char*)req->path, r->status, (char*)r->body);
    delete r;
}

int main() {
    printf("registered routes:\n");
    for (auto r in route_list())
        printf("  %-4s %s\n", r.method, r.path);

    printf("\ndispatch tests:\n");
    { owned Request* q = new Request("GET", "/api/symptoms", "id=T001", ""); show(q); }
    { owned Request* q = new Request("POST", "/api/addpatient", "sessionId=S1",
        "{\"firstName\":\"Grace\",\"lastName\":\"Hopper\",\"emailAddress\":\"g@navy.mil\",\"mobileNumber\":\"555\"}");
      show(q); }
    { owned Request* q = new Request("GET", "/nope", "", ""); show(q); }
    return 0;
}
