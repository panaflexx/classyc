/* val-004-dict-arrays.cy — validates dict JSON ARRAY support (the user's
 * "does dict name.array[0].value work?" question).
 *
 * WHAT WORKS:
 *   · json("{...[...]...}") parses arrays
 *   · d.arr retrieves the array value; json(d.arr) -> "[...]"
 *   · integer subscript d.arr[i] returns the element
 *   · d.arr[i].field for STRING leaves (returns char*-like)
 *   · numeric leaf read as a scalar: (int)d.arr[i].field
 *
 * KNOWN LIMITATIONS (asserted so they're tracked — see SHORTCOMINGS.md C):
 *   · for (auto x in d.arr) iterates 0 times
 *   · treating a numeric leaf as a dict (json(d.arr[i].value)) crashes; must
 *     read it as a scalar instead
 *   · array literal assignment d.k = [..] is unimplemented
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

    /* array of objects: the exact 'name.array[0].value' form */
    dict r = json("{\"items\":[{\"value\":7,\"name\":\"a\"},{\"value\":9,\"name\":\"b\"}]}");
    check(strcmp((char*)r.items[0].name, "a") == 0, "r.items[0].name (string leaf) works");
    check(strcmp((char*)r.items[1].name, "b") == 0, "r.items[1].name (string leaf) works");
    /* numeric leaf: MUST be read as scalar (C2) */
    check((int)r.items[0].value == 7,               "r.items[0].value read as (int)");
    check((int)r.items[1].value == 9,               "r.items[1].value read as (int)");

    /* round-trip whole structure preserves the nested array */
    dict d2 = json((char*)r.json);
    check("items" in d2,                            "round-trip preserves array key");

    /* ---- documented limitations (asserted as current behavior) ---- */

    /* C3: for-in over a dict array value does NOT iterate */
    int n = 0;
    for (auto x in d.nums) n++;
    check(n == 0, "LIMITATION C3: for-in over dict array yields 0 (documented)");

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
