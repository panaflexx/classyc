/* val-015-string-literal-and-replace.cy — validates three ergonomics fixes:
 *
 *   B1 — built-in String methods called directly on a string LITERAL receiver
 *        ("abc".lower(), "x".equals(y), ...) without a ((String)..) cast.
 *   A2 — search-and-replace via the 2-arg overload replace(needle, repl),
 *        alongside the original positional replace(pos, len, repl).
 *   B4 — List<T>.Map(fn) fluent transform (chains with Filter).
 *
 * Each is a self-checking assertion; exit code == number of failures.
 *
 * Run:  ./bin/classyc -g -I include cy-validate/val-015-string-literal-and-replace.cy -eg
 */
#include <stdio.h>
#include <string.h>
#include "list.h"

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

int dbl(int x) { return x * 2; }

int main() {
    printf("=== val-015 string-literal methods + replace + List.Map ===\n\n");

    /* ── B1: instance methods directly on string literals ──────────────── */
    check(strcmp((char*)"MiXeD".upper(), "MIXED") == 0, "literal.upper()");
    check(strcmp((char*)"MiXeD".lower(), "mixed") == 0, "literal.lower()");
    check("abc".equals("abc") == 1,                     "literal.equals() true");
    check("abc".equals("abd") == 0,                     "literal.equals() false");
    check("hello world".contains("wor") == 1,           "literal.contains()");
    check("file.pdf".starts_with("file") == 1,          "literal.starts_with()");
    check("file.pdf".ends_with(".pdf") == 1,            "literal.ends_with()");
    check("héllo".length() == 5,                        "literal.length() counts code points");
    check("abcdef".find("cd") == 2,                     "literal.find() index");
    check(strcmp((char*)"abcdef".substr(1, 3), "bcd") == 0, "literal.substr()");
    check(strcmp((char*)"  pad  ".trim(), "pad") == 0,  "literal.trim()");

    /* static String.copy(...) keyword receiver still works */
    check(strcmp((char*)String.copy("xyz", 3), "xyz") == 0, "String.copy() static still works");

    /* ── A2: 2-arg search-and-replace ──────────────────────────────────── */
    String path = "report.pdf";
    path = path.replace(".pdf", ".txt");
    check(strcmp((char*)path, "report.txt") == 0,       "replace(needle,repl) single match");
    check(strcmp((char*)"a-b-c-d".replace("-", "_"), "a_b_c_d") == 0,
          "replace(needle,repl) multiple matches");
    check(strcmp((char*)"x x x".replace("x", "[X]"), "[X] [X] [X]") == 0,
          "replace(needle,repl) replacement longer than needle");
    check(strcmp((char*)"a,b,c".replace(",", ""), "abc") == 0,
          "replace(needle,'') deletes");
    check(strcmp((char*)"abc".replace("z", "Q"), "abc") == 0,
          "replace(needle,repl) needle absent -> unchanged");

    /* positional 3-arg replace must still behave as before */
    String p = "abcdef";
    check(strcmp((char*)p.replace(0, 3, "XYZ"), "XYZdef") == 0,
          "positional replace(pos,len,repl) preserved");

    /* ── B4: List<T>.Map (chains with Filter; both return by value) ──── */
    List<int>* nums = new List<int>{ 1, 2, 3, 4 };
    defer delete nums;
    auto out = nums->Filter((int x) => x > 1).Map(dbl);
    check(out.Count() == 3, "List.Map after Filter: count");
    check(out.Get(0) == 4 && out.Get(1) == 6 && out.Get(2) == 8,
          "List.Map applies fn to each element");

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
