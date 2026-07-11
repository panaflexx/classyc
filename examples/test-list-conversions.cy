/* test-list-conversions.cy — List<T> → array / dict convenience conversions
 *
 * Exercises the C#/LINQ-flavored converters added to list.h:
 *   · ToArray()        — bulk-copy to a heap T[]            (C# List.ToArray)
 *   · CopyTo(buf)      — bulk-copy into a caller buffer     (C# List.CopyTo)
 *   · IntsToJsonArray  — List<int>    → DICT_ARRAY of ints
 *   · StringsToJsonArray — List<String> → DICT_ARRAY of strings
 *   · ToJsonArrayBy(fn)  — generic projection to DICT_ARRAY (C# Select)
 *   · ToDictBy(k, v)     — List<T> → DICT_OBJECT            (C# ToDictionary)
 */

#include <stdio.h>
#include "include/list.h"

/* projection helpers for the generic converters */
dict        squareAsNum(int x) { return dict_create_int64((long)(x * x)); }
const char* keyOf(int x)       { static char b[16]; snprintf(b, sizeof(b), "n%d", x); return b; }
dict        valOf(int x)       { return dict_create_int64((long)x); }

int main() {
    /* ── ToArray(): independent heap copy ─────────────────────────────── */
    List<int>* nums = new List<int>();
    nums->Add(10); nums->Add(20); nums->Add(30);

    int n = nums->Count();
    int* arr = nums->ToArray();           /* caller owns -> free */
    arr[0] = 999;                          /* prove it's a copy, not a view */
    printf("ToArray: list[0]=%d  arr[0]=%d  n=%d\n", nums->Get(0), arr[0], n);
    free(arr);

    /* ── CopyTo(): into a fixed buffer ────────────────────────────────── */
    int buf[3];
    nums->CopyTo(buf);
    printf("CopyTo:  buf = [ %d %d %d ]\n", buf[0], buf[1], buf[2]);

    /* ── IntsToJsonArray() ────────────────────────────────────────────── */
    dict ints = { "items": nums->IntsToJsonArray() };
    printf("ints:    %s\n", ints.json());

    /* ── ToJsonArrayBy(): project each element (square it) ────────────── */
    dict squares = { "items": nums->ToJsonArrayBy(squareAsNum) };
    printf("squares: %s\n", squares.json());

    /* ── ToDictBy(): build an object keyed by element ─────────────────── */
    dict byKey = nums->ToDictBy(keyOf, valOf);
    printf("byKey:   %s\n", byKey.json());
    delete nums;

    /* ── StringsToJsonArray() (the SymptomController GET path) ─────────── */
    String csv = "fever,cough,fatigue";
    List<String>* parts = csv.split(",");
    dict out = { "symptomList": parts->StringsToJsonArray() };
    printf("strings: %s\n", out.json());

    /* ── ToJsonArray(): automagical, type-dispatched conversion ───────── */
    List<int>* ai = new List<int>{ 1, 2, 3 };
    List<double>* ad = new List<double>{ 1.5, 2.5, 3.0 };
    dict autos = {
        "ints":    ai->ToJsonArray(),
        "doubles": ad->ToJsonArray(),
        "strings": parts->ToJsonArray()
    };
    printf("auto:    %s\n", autos.json());
    delete ai; delete ad; delete parts;

    return 0;
}
