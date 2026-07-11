/* val-004-dict-arrays.cy — validates dict JSON ARRAY support.
 *
 * Covers the three SHORTCOMINGS.md C-section fixes:
 *   C1: `d.arr.length` / `d.arr.count` expose array length (and object size).
 *   C2: leaf dict access (`d.items[0].value`) stays a tagged DictValue*, so
 *       `json(leaf)` re-serializes the scalar instead of dereferencing it as a
 *       raw pointer.  Explicit casts unwrap the union payload as before.
 *   C3: `for (auto x in d.arr)` dispatches to the array iterator at runtime
 *       (tag check on the DictValue header), so the loop runs N iterations
 *       and `x` is the element (DictValue*).  The single-var form types `x` as
 *       char* (the object-key convention); for a typed element use the two-var
 *       form or the typed for-in `for (int x in d.arr)`.
 *
 * Array literals in dict brace-init:
 *   dict d = { "nums": [10, 20, 30], "empty": [], "rows": [ {"a":1} ] };
 * Unkeyed braces still work:  { "a": {1, 2, 3} }  → array value.
 * Empty nested `{}` is an empty *object*; empty `[]` is an empty *array*.
 *
 * Run:  ./bin/classyc -g -I include cy-validate/val-004-dict-arrays.cy -eg
 */
#include <stdio.h>
#include <string.h>

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

int main() {
    printf("=== val-004 dict JSON arrays ===\n\n");

    /* scalar array */
    dict d = json("{\"nums\":[10,20,30]}");
    check("nums" in d,                              "array key present");
    check(strcmp((char*)json(d.nums), "[10,20,30]") == 0, "json(array value) serializes");
    check((int)d.nums[0] == 10,                     "d.nums[0] integer subscript");
    check((int)d.nums[2] == 30,                     "d.nums[2] integer subscript");

    /* C1: array length via .length() / .count() (method-call form so they
       do not shadow real dict keys named `length` / `count`). */
    check((int)d.nums.length() == 3,                "C1: d.nums.length() == 3");
    check((int)d.nums.count()  == 3,                "C1: d.nums.count()  == 3");
    check((int)d.length()      == 1,                "C1: d.length() on object (one key)");

    /* array of objects: the exact 'name.array[0].value' form */
    dict r = json("{\"items\":[{\"value\":7,\"name\":\"a\"},{\"value\":9,\"name\":\"b\"}]}");
    check(strcmp((char*)r.items[0].name, "a") == 0, "r.items[0].name (string leaf) works");
    check(strcmp((char*)r.items[1].name, "b") == 0, "r.items[1].name (string leaf) works");
    /* numeric leaf via explicit cast (the supported, lossless form) */
    check((int)r.items[0].value == 7,               "r.items[0].value read as (int)");
    check((int)r.items[1].value == 9,               "r.items[1].value read as (int)");

    /* C2: numeric leaf stays a tagged dict-value; json() re-serializes the
       scalar payload instead of dereferencing it as a raw pointer. */
    dict v0 = r.items[0].value;
    check(strcmp((char*)json(v0), "7") == 0,        "C2: json(numeric-leaf) prints '7' (no SIGSEGV)");
    dict v1 = r.items[1].value;
    check(strcmp((char*)json(v1), "9") == 0,        "C2: json(numeric-leaf) prints '9'");
    /* string leaf round-trip too */
    dict s0 = r.items[0].name;
    check(strcmp((char*)json(s0), "\"a\"") == 0,    "C2: json(string-leaf) prints '\"a\"'");

    /* round-trip whole structure preserves the nested array */
    dict d2 = json((char*)r.json());
    check("items" in d2,                            "round-trip preserves array key");

    /* C3: for-in over a dict array iterates N times.
       The single-var form declares the loop variable as char* (per the
       existing dict for-in convention where single-var = key), so it serves
       primarily as a count guard; use the two-var form or a typed for-in when
       you need the element as a typed value. */
    int n = 0;
    for (auto x in d.nums) n++;
    check(n == 3,                                   "C3: for-in over dict array runs N iterations");

    /* C3b: typed single-var for-in binds the element coerced to the type. */
    int typed_sum = 0;
    for (int x in d.nums) typed_sum += x;
    check(typed_sum == 60,                          "C3: typed for-in (int x): elements unwrap (10+20+30)");

    /* C3: two-var form binds (index, element-as-dict).  Now `(int)x` triggers
       the dict-to-scalar unwrap and reads the int64 payload. */
    int idx_sum = 0, val_sum = 0;
    for (auto i, x in d.nums) { idx_sum += i; val_sum += (int)x; }
    check(idx_sum == 3,                             "C3: two-var for-in: indices 0+1+2 == 3");
    check(val_sum == 60,                            "C3: two-var for-in: values 10+20+30 == 60");

    /* Object for-in still works (regression guard for the dispatch). */
    int keys_seen = 0;
    for (auto k in r) { if (strcmp(k, "items") == 0) keys_seen++; }
    check(keys_seen == 1,                           "object for-in still yields keys");

    /* for-in over array of objects: use the two-var form to get the element
       as a dict (single-var form's type is `char*` per the object-iteration
       convention; the dispatch still runs the right number of iterations but
       chaining `x.name` needs a dict-typed binding). */
    int objs = 0, name_a_seen = 0;
    for (auto i, x in r.items) {
        objs++;
        if (strcmp((char*)x.name, "a") == 0) name_a_seen++;
    }
    check(objs == 2,                                "C3: for-in over array-of-objects runs 2x");
    check(name_a_seen == 1,                         "C3: chained x.name access on array element");

    /* C4: square-bracket array literals inside dict brace-init */
    dict lit = {
        "nums": [10, 20, 30],
        "tags": ["a", "b"],
        "empty": [],
        "obj": {},
        "rows": [ { "v": 1 }, { "v": 2 } ],
        "nested": [ [1, 2], [3] ]
    };
    check((int)lit.nums.length() == 3 && (int)lit.nums[1] == 20,
          "C4: [10,20,30] brace-init array");
    check(strcmp((char*)lit.tags[0], "a") == 0,     "C4: string array element");
    check((int)lit.empty.length() == 0,             "C4: empty [] is array len 0");
    check((int)lit.obj.length() == 0,               "C4: empty {} is object (0 keys)");
    check((int)lit.rows[1].v == 2,                  "C4: array of objects");
    check((int)lit.nested[0][1] == 2,               "C4: nested arrays");
    check(strcmp((char*)json(lit.nums), "[10,20,30]") == 0,
          "C4: json() of brace array literal");

    /* Assignment form */
    dict m = {};
    m = { "powers": [1, 2, 3, 4] };
    check((int)m.powers.length() == 4 && (int)m.powers[3] == 4,
          "C4: assign dict = { \"k\": [...] }");

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
