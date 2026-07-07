/* val-027-dict-string-ownership.cy — pins dict String-value ownership.
 *
 * Bug (regression guard): storing a `String` VALUE (variable / concat / substr
 * result) into a dict via `d[key] = s` or `d.key = s` used to route through the
 * integer path (dict_create_int64), storing the raw char* pointer as a number.
 * That produced garbage json ("name":<bignum>) and a dangling pointer once the
 * source String's arena scope was released — a use-of-freed-String bug.  The
 * fix routes builtin `String` RHS through dict_create_string, which COPIES the
 * bytes so the dict owns its own buffer (literals already did this).
 *
 * Run:  ./bin/classyc -I include cy-validate/val-027-dict-string-ownership.cy -eg
 */
#include <stdio.h>
#include <string.h>

dict dict_create_object();

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

/* Build a dict whose String fields are computed in an INNER scope, so the
   source Strings' arena is released before the caller reads the dict.  The
   dict must own copies. */
dict make_user(int id) {
    dict u = dict_create_object();
    {
        String name = (String)"user#" + id;
        u["name"] = name;
        u["email"] = name + "@example.com";
    }
    u["id"] = id;
    return u;
}

int main(void) {
    printf("=== val-027 dict String ownership ===\n\n");

    /* (1) String variable stored + read in the same scope: value must be the
       string, and json must serialize it as a quoted string (not a number). */
    dict d = dict_create_object();
    String s = (String)"world-" + 42;
    d["var"] = s;
    d["lit"] = "hello";
    check(strcmp((char*)d["var"], "world-42") == 0, "(1) String var reads back correctly");
    check(strstr(d.json, "\"var\":\"world-42\"") != 0, "(1) String var serializes as JSON string");
    check(strstr(d.json, "\"lit\":\"hello\"")    != 0, "(1) literal still serializes as JSON string");

    /* (2) Dict String fields set from an inner scope survive the scope's arena
       release (dict owns a copy — no dangling / garbage). */
    dict a = make_user(1);
    dict b = make_user(2);
    check(strcmp((char*)a["name"],  "user#1")             == 0, "(2) inner-scope String field intact");
    check(strcmp((char*)a["email"], "user#1@example.com") == 0, "(2) inner-scope concat field intact");
    check(strcmp((char*)b["name"],  "user#2")             == 0, "(2) second dict field intact (no aliasing)");
    check(strstr(a.json, "\"name\":\"user#1\"") != 0, "(2) json serializes owned String field");

    /* (3) substr temporaries stored into a dict are owned copies. */
    dict e = dict_create_object();
    {
        String base = (String)"alpha-beta-gamma";
        e["head"] = base.substr(0, 5);
        e["tail"] = base.substr(6, 4);
    }
    check(strcmp((char*)e["head"], "alpha") == 0, "(3) substr head owned + intact");
    check(strcmp((char*)e["tail"], "beta")  == 0, "(3) substr tail owned + intact");

    /* (4) dot-assignment form d.key = strvar also copies. */
    dict f = dict_create_object();
    String tag = (String)"v" + 7;
    f.label = tag;
    check(strcmp((char*)f["label"], "v7") == 0, "(4) dot-assign String field owned + intact");

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
