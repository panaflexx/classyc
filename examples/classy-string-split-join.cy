/* classy-string-split-join.cy — String.equals / String.split / List<String>.join
 *
 * Exercises the String <-> List<String> bridge added to the compiler:
 *
 *   String s   = "bar";
 *   s.equals("bar")                  -> int (0/1), content comparison
 *
 *   String path = "/a/b/c";
 *   List<String>* parts = path.split("/");   // heap List<String>*
 *   parts->Count(); parts->Get(i);           // standard collection protocol
 *   delete parts;
 *
 *   List<String>* names = new List<String>{"x","y"};  // brace-init
 *   String csv = names->join(", ");          // collapse back to a String
 *   delete names;
 *
 * split() returns a real List<String> (the include/list.h class), so every
 * List method works on the result and `delete` reclaims it.  join() is the
 * inverse, available on any List<String>.
 */

#include <stdio.h>
#include "include/list.h"

/* ---- test harness ---- */

int passed;
int failed;

void check(int cond, char *label) {
    if (cond) {
        printf("  PASS  %s\n", label);
        passed = passed + 1;
    } else {
        printf("  FAIL  %s\n", label);
        failed = failed + 1;
    }
}

int main() {
    printf("=== String split/join/equals suite ===\n\n");
    passed = 0;
    failed = 0;

    /* ---- equals: content comparison ---- */
    String s = "bar";
    check(s.equals("bar") == 1, "equals: identical content");
    check(s.equals("baz") == 0, "equals: different content");
    check(s.equals("ba")  == 0, "equals: prefix is not equal");
    check(("" + "").equals("") == 1, "equals: empty == empty");

    /* ---- split: String -> List<String>* ---- */
    String path = "/a/b/c";
    List<String>* parts = path.split("/");
    check(parts->Count() == 4, "split: '/a/b/c' on '/' yields 4 fields");
    check(parts->Get(0).equals(""),  "split: leading delim -> empty field 0");
    check(parts->Get(1).equals("a"), "split: field 1 == 'a'");
    check(parts->Get(3).equals("c"), "split: field 3 == 'c'");

    /* split is a normal List<String>, so List methods compose */
    String rejoined = parts->join("/");
    check(rejoined.equals("/a/b/c"), "split then join round-trips");
    delete parts;

    /* no delimiter found -> single element holding the whole string */
    List<String>* whole = path.split("ZZ");
    check(whole->Count() == 1, "split: missing delim -> 1 field");
    check(whole->Get(0).equals("/a/b/c"), "split: that field is the whole input");
    delete whole;

    /* ---- join over a brace-initialized List<String> ---- */
    List<String>* names = new List<String>{"x", "y", "z"};
    check(names->Count() == 3, "brace-init: new List<String>{...} has 3 items");
    String csv = names->join(", ");
    check(csv.equals("x, y, z"), "join: 'x, y, z'");
    String tight = names->join("");
    check(tight.equals("xyz"), "join: empty separator concatenates");
    delete names;

    /* ---- CSV-style round trip ---- */
    String row = "alice,30,nyc";
    List<String>* cols = row.split(",");
    check(cols->Count() == 3, "csv: 3 columns");
    check(cols->Get(0).equals("alice"), "csv: column 0");
    check(cols->join(" | ").equals("alice | 30 | nyc"), "csv: re-join with ' | '");
    delete cols;

    printf("\n%s\n", "============================================================");
    printf("  results: %d passed, %d failed\n", passed, failed);
    printf("%s\n", "============================================================");
    return failed;
}
