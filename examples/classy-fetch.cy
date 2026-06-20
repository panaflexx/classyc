/* classy-fetch.cy — fetch a public JSON API with include/httpclient.h
 *
 * Talks to the PokéAPI (https://pokeapi.co) over HTTPS and shows the classy
 * HTTP client end to end:
 *
 *   · Http.get(url)                 one-line fetch, returns an HttpResponse*
 *   · resp->ok() / status / error   status handling
 *   · resp->header("name")          response headers exposed as a dict
 *   · resp->headerNames()           header names as a List<String>
 *   · resp->asDict()                JSON body parsed into a dict
 *   · Http.get(url, headers)        custom request headers via List<String>
 *   · a List<String> of names       batch fetch with a for-in loop
 *   · a 404                         graceful error handling
 *
 * TLS is handled by OpenSSL, which the header loads on demand — nothing to
 * link or configure.
 *
 * Usage:  classyc examples/classy-fetch.cy -eg
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "include/httpclient.h"

String POKE_API="https://pokeapi.co/api/v2/pokemon/";

/* Pretty-print the interesting fields of a parsed Pokémon dict. */
void print_pokemon(dict d) {
    printf("  #%-4d %-12s  height %-3d  weight %-4d  base-exp %-3d  (species: %s)\n",
           (int)d.id, (char *)d.name,
           (int)d.height, (int)d.weight, (int)d.base_experience,
           (char *)d.species.name);
}

/* Fetch one Pokémon by name and print a one-line summary. */
void show_pokemon(String name) {
    String url = POKE_API + name;
    auto   resp = Http.get((char *)url);
    defer delete resp;

    if (!resp->ok()) {
        printf("  %-12s  -> HTTP %d %s\n", (char *)name, resp->status,
               resp->error != NULL ? (char *)resp->error : (char *)resp->statusText);
        return;
    }
    print_pokemon(resp->asDict());
}

int main() {
    printf("════════════════════════════════════════════════\n");
    printf("  classyc HTTP client — PokéAPI fetcher\n");
    printf("════════════════════════════════════════════════\n\n");

    /* ── 1. One detailed fetch: inspect the response envelope ──────────── */
    printf("-- 1. GET %sditto --\n", POKE_API);
    {
        auto resp = Http.get(POKE_API + "ditto");
        defer delete resp;

        printf("  status      : %d %s\n", resp->status, (char *)resp->statusText);
        printf("  ok()        : %d\n", resp->ok());
        printf("  content-type: %s\n", (char *)resp->header("content-type"));
        printf("  server      : %s\n", (char *)resp->header("server"));
        printf("  body length : %d bytes\n", resp->length());

        /* response headers are a dict; their names come back as a List<String> */
        auto names = resp->headerNames();
        defer delete names;
        printf("  headers     : %d returned\n", names->Count());

        if (resp->ok()) {
            printf("  parsed JSON :\n  ");
            print_pokemon(resp->asDict());
        }
    }

    /* ── 2. Custom request headers via List<String> ────────────────────── */
    printf("\n-- 2. GET with custom request headers --\n");
    {
        List<String> *headers = new List<String>();
        defer delete headers;
        headers->Add("Accept: application/json");
        headers->Add("X-Powered-By: ClassyC");

        auto resp = Http.get( POKE_API + "charizard", headers);
        defer delete resp;

        if (resp->ok()) {
            dict d = resp->asDict();
            printf("  sent 2 custom headers; got #%d %s back\n",
                   (int)d.id, (char *)d.name);
        } else {
            printf("  request failed: HTTP %d\n", resp->status);
        }
    }

    /* ── 3. Batch fetch over a List<String> of names ───────────────────── */
    printf("\n-- 3. Batch fetch (for-in over a List<String>) --\n");
    {
        List<String> *roster = new List<String>();
        defer delete roster;
        roster->Add("bulbasaur");
        roster->Add("pikachu");
        roster->Add("snorlax");
        roster->Add("mewtwo");
        roster->Add("eevee");

        for (auto name in roster) show_pokemon(name);
    }

    /* ── 4. Graceful handling of a 404 ─────────────────────────────────── */
    printf("\n-- 4. A request that 404s --\n");
    {
        auto resp = Http.get(POKE_API + "definitely-not-a-pokemon");
        defer delete resp;
        printf("  status %d %s -> ok()=%d\n",
               resp->status, (char *)resp->statusText, resp->ok());
    }

    printf("\nDone.\n");
    return 0;
}
