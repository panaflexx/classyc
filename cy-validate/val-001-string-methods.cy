/* val-001-string-methods.cy — validates the String API surface advertised in
 * README.md against the actual compiler/runtime (include/cstring.h).
 *
 * REAL signatures (verified in src/classyc.c get_string_method):
 *   length()                 -> size_t (code points)
 *   empty()                  -> int 0/1
 *   substr(pos, len)         -> String   (2 args, code-point indexed)
 *   find(needle)             -> size_t index, or (size_t)-1 if not found
 *   replace(pos, len, repl)  -> String   (3 args, positional)
 *   replace(needle, repl)    -> String   (2 args, search-and-replace; see val-015)
 *   upper() / lower()        -> String
 *   trim()                   -> String
 *   starts_with/ends_with/contains(s) -> int 0/1
 *   split(delim)             -> List<String>*
 *   join(delim) on List      -> String
 *   equals(other)            -> int 0/1
 *
 * Run:  ./bin/classyc -g -I include cy-validate/val-001-string-methods.cy -eg
 */
#include <stdio.h>
#include <string.h>
#include "list.h"

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

int main() {
    printf("=== val-001 String methods ===\n\n");

    /* length / empty */
    String s = "héllo";                      /* 5 code points, 6 bytes */
    check(s.length() == 5,            "length() counts code points, not bytes");
    check(s.empty() == 0,             "empty() false for non-empty");
    String e = "";
    check(e.empty() == 1,             "empty() true for empty");

    /* trim / upper / lower (chained) */
    String pad = "  Hello  ";
    check(strcmp(pad.trim(), "Hello") == 0,        "trim() strips both ends");
    check(strcmp(pad.trim().upper(), "HELLO") == 0,"trim().upper() chained");
    String mixed = "MiXeD";
    check(strcmp(mixed.lower(), "mixed") == 0,     "lower()");

    /* find: returns index or (size_t)-1 */
    String hw = "hello world";
    check(hw.find("world") == 6,      "find() returns code-point index");
    check(hw.find("hello") == 0,      "find() returns 0 at start");
    check(hw.find("zzz") == (size_t)-1,"find() returns (size_t)-1 when absent");

    /* substr(pos, len) — 2 args, code-point indexed */
    check(strcmp(hw.substr(6, 5), "world") == 0,   "substr(pos,len)");
    check(strcmp(hw.substr(0, 5), "hello") == 0,   "substr at start");

    /* replace(pos, len, repl) — 3 args (NOT a find/replace!) */
    String fn = "report.pdf";
    /* replace 4 cps starting at index 6 (".pdf") with ".txt" */
    check(strcmp(fn.replace(6, 4, ".txt"), "report.txt") == 0, "replace(pos,len,repl)");

    /* starts_with / ends_with / contains */
    check(hw.starts_with("hello") == 1, "starts_with true");
    check(hw.ends_with("world") == 1,   "ends_with true");
    check(hw.contains("o w") == 1,      "contains true");
    check(hw.contains("xyz") == 0,      "contains false");

    /* equals */
    String abc = "abc";
    check(abc.equals("abc") == 1,     "equals true");
    check(abc.equals("abd") == 0,     "equals false");

    /* split -> List<String>*, join -> String */
    String csv = "a,b,c,d";
    List<String> *parts = csv.split(",");
    check(parts->Count() == 4,        "split() returns List<String> of 4");
    check(strcmp(parts->Get(0), "a") == 0, "split element 0");
    check(strcmp(parts->Get(3), "d") == 0, "split element 3");
    check(strcmp(parts->join("|"), "a|b|c|d") == 0, "List<String>.join()");

    /* concatenation + auto-cast (README 'auto-promotion') */
    int n = 42; bool b = 1; double pi = 3.5; char c = 'Q';
    String built = (String)"n=" + n + " b=" + b + " pi=" + pi + " c=" + c;
    check(strcmp(built, "n=42 b=true pi=3.5 c=Q") == 0, "auto-cast concat (int/bool/double/char)");

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
