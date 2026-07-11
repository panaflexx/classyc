/* val-035-safe-nav-coalesce.cy — `?.` safe navigation and `??` null coalescing
 *
 *  ?? (null coalescing):
 *   - a ?? b yields a when a is non-zero/non-null, else b
 *   - the LEFT side is evaluated exactly once; the RIGHT side is evaluated
 *     only when the left is zero/null (side-effect counters prove both)
 *   - right-associative chains: a ?? b ?? c
 *   - works over int, char*, String, double, and class pointers
 *   - binds tighter than ?: and looser than ||
 *   - `cond ? .5 : x` still lexes as a ternary with a float literal (no
 *     `?.` misfire), and trigraphs no longer eat `x??(y)` outside -fpedantic
 *
 *  ?. (safe navigation):
 *   - p?.field yields p->field when p != NULL, else 0/NULL
 *   - p?.Method(args) calls only when p != NULL (void methods allowed)
 *   - the whole remaining postfix chain is guarded: a?.b?.c and
 *     junk?.engine?.Power() short-circuit at the first NULL
 *   - composes with ??:  p?.name ?? "default"
 *   NOTE: the receiver expression is duplicated into the null guard, so it
 *   is evaluated twice — keep receivers simple (a variable or field).
 *
 * Run:  ./bin/classyc -g -I include cy-validate/val-035-safe-nav-coalesce.cy -eg
 */
#include <stdio.h>
#include <string.h>
#include "list.h"

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

/* side-effect counters for evaluation-order guarantees */
int lhs_calls = 0, rhs_calls = 0;
const char *lhs_str(const char *v) { lhs_calls++; return v; }
const char *rhs_str(void)          { rhs_calls++; return "rhs"; }
int rhs_int(void)                  { rhs_calls++; return 77; }

struct Inner { int val; };
struct Node { int x; const char *tag; struct Inner *inner; };

class Engine {
    int hp;
    Engine(int h) { hp = h; }
    int Power() { return hp; }
    void Rev() { printf("        (vroom %d)\n", hp); }
};

class Car {
    String name;
    Engine* engine;
    Car(String n) { name = n; engine = NULL; }
};

int main() {
    printf("=== val-035 ?. safe navigation / ?? null coalescing ===\n\n");

    printf("-- 1. ?? basics --\n");
    int zero = 0, seven = 7;
    check((zero ?? 5) == 5,                          "1a  0 ?? 5 == 5");
    check((seven ?? 5) == 7,                         "1b  7 ?? 5 == 7");
    const char *ns = NULL;
    check(strcmp(ns ?? "fallback", "fallback") == 0, "1c  NULL ?? \"fallback\"");
    const char *rs = "real";
    check(strcmp(rs ?? "fallback", "real") == 0,     "1d  \"real\" ?? \"fallback\"");
    check((zero ?? 0 ?? 9) == 9,                     "1e  right-assoc chain 0 ?? 0 ?? 9");
    double dz = 0.0;
    check((dz ?? 2.5) == 2.5,                        "1f  0.0 ?? 2.5 (double)");

    printf("\n-- 2. ?? evaluation-order guarantees --\n");
    lhs_calls = rhs_calls = 0;
    const char *r1 = lhs_str("kept") ?? rhs_str();
    check(strcmp(r1, "kept") == 0,                   "2a  non-null lhs wins");
    check(lhs_calls == 1,                            "2b  lhs evaluated exactly once");
    check(rhs_calls == 0,                            "2c  rhs skipped when lhs non-null");
    lhs_calls = rhs_calls = 0;
    const char *r2 = lhs_str(NULL) ?? rhs_str();
    check(strcmp(r2, "rhs") == 0,                    "2d  null lhs takes rhs");
    check(lhs_calls == 1 && rhs_calls == 1,          "2e  each side evaluated once");
    int iz = 0;
    rhs_calls = 0;
    check((iz ?? rhs_int()) == 77 && rhs_calls == 1, "2f  int rhs call on zero lhs");

    printf("\n-- 3. ?? precedence & lexing --\n");
    check((zero ?? 3 ? 100 : 200) == 100,            "3a  ?? binds tighter than ?:");
    check((1 ? .5 : 2.0) == 0.5,                     "3b  `? .5 :` float literal intact");
    check((zero??(9)) == 9,                          "3c  x??(y) not a trigraph");
    check((seven || zero ?? 4) == 1,                 "3d  || binds tighter than ??");

    printf("\n-- 4. ?. on plain C structs --\n");
    struct Inner in = {13};
    struct Node n = {42, "neo", &in};
    struct Node *p = &n, *q = NULL;
    check(p?.x == 42,                                "4a  p?.x reads field");
    check(q?.x == 0,                                 "4b  NULL?.x yields 0");
    check(strcmp(p?.tag ?? "none", "neo") == 0,      "4c  p?.tag ?? default (present)");
    check(strcmp(q?.tag ?? "none", "none") == 0,     "4d  q?.tag ?? default (null)");
    check(p?.inner?.val == 13,                       "4e  two-level chain, all present");
    check(q?.inner?.val == 0,                        "4f  chain short-circuits at first NULL");
    p->inner = NULL;
    check(p?.inner?.val == 0,                        "4g  chain short-circuits mid-way");

    printf("\n-- 5. ?. on classes (methods, String, void) --\n");
    Engine* e = new Engine(300);
    defer delete e;
    Engine* none = NULL;
    check(e?.Power() == 300,                         "5a  e?.Power() calls method");
    check(none?.Power() == 0,                        "5b  NULL?.Power() yields 0, no call");
    none?.Rev();  /* void method on NULL: must be a silent no-op */
    check(1,                                         "5c  NULL?.VoidMethod() is a no-op");
    e?.Rev();     /* prints (vroom 300) */
    check(1,                                         "5d  e?.VoidMethod() executes");
    Car* kitt = new Car("KITT");
    defer delete kitt;
    Car* junk = NULL;
    String nm = junk?.name ?? "scrap";
    check(strcmp((char*)nm, "scrap") == 0,           "5e  junk?.name ?? \"scrap\"");
    String nm2 = kitt?.name ?? "scrap";
    check(strcmp((char*)nm2, "KITT") == 0,           "5f  kitt?.name ?? default (present)");
    check(junk?.engine?.Power() == 0,                "5g  null?.member?.Method() chain");
    check(kitt?.engine?.Power() == 0,                "5h  chain stops at NULL member");
    kitt->engine = new Engine(120);
    defer delete kitt->engine;
    check(kitt?.engine?.Power() == 120,              "5i  full chain when all present");

    printf("\n-- 6. ?. with collections --\n");
    List<int>* xs = new List<int>{1, 2, 3};
    defer delete xs;
    List<int>* nil = NULL;
    check(xs?.Count() == 3,                          "6a  xs?.Count()");
    check(nil?.Count() == 0,                         "6b  NULL list ?.Count() yields 0");
    /* ?. yields 0 for a NULL receiver, and ?? treats 0 as null-ish (there is
       no int? in ClassyC) — so the default is taken.  Documented behavior. */
    check((nil?.Count() ?? -1) == -1,                "6c  ?. null result (0) triggers ?? default");
    check((xs?.Count() ?? -1) == 3,                  "6d  ?. present result passes through ??");

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
