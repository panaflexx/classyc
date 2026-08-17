/* webmain.cy — the ASP.NET-style web application entry point.
 *
 * @expect: skip   (multi-TU app: needs http-serve.c + controllers linked together;
 *                  cannot run alone via examples/run-examples.sh)
 *
 * Controllers self-register their routes via `[[registry("routes")]]` (see the
 * ROUTE() macro in httpserve.h); this file is the ONLY place with app_handle()
 * and main().  It never mentions any controller by name — routes are gathered
 * automatically across all linked translation units.
 *
 * Build + run (JIT):
 *   ./bin/classyc -I include -I examples -l sqlite3 \
 *       examples/http-serve.c examples/webmain.cy \
 *       examples/AddPatientController.cy examples/SymptomController.cy -eg
 *
 * Build + run (AOT):
 *   ./classyc-aot.sh -I include -I examples -l sqlite3 \
 *       examples/http-serve.c examples/webmain.cy \
 *       examples/AddPatientController.cy examples/SymptomController.cy -o webapp
 *
 * Then:
 *   curl -s 'http://127.0.0.1:8080/api/symptoms?id=T001'
 *   curl -s -X POST 'http://127.0.0.1:8080/api/addpatient?sessionId=S1' \
 *        -d '{"firstName":"Grace","lastName":"Hopper",
 *             "emailAddress":"grace@navy.mil","mobileNumber":"555-0002"}'
 */

#include "httpserve.h"
#include <stdio.h>

/* The server calls this once per request; delegate to the auto-discovered
   route table. */
Response* app_handle(Request* req) {
    return route_dispatch(req);
}

int main() {
    printf("════════════════════════════════════════════\n");
    printf("  classyc web app  (attribute-routed)\n");
    printf("  registered routes:\n");
    for (auto r in route_list())
        printf("    %-4s %s\n", r.method, r.path);
    printf("════════════════════════════════════════════\n");
    return serve(8080);
}
