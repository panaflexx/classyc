/* val-043-list-getmut.cy — in-place mutation of by-value class elements.
 *
 * Get returns T by copy; GetMut / FirstMut return T* into the buffer so
 * methods like Boost can mutate list storage without Set.
 *
 * Run: ./bin/classyc -I include cy-validate/val-043-list-getmut.cy -eg
 */
#include <stdio.h>
#include "list.h"

int passed = 0, failed = 0;
void check(int cond, const char* label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

class Unit {
    int id;
    int heat;

    Unit(int id, int heat) {
        this.id = id;
        this.heat = heat;
    }

    Unit* Boost(int d) {
        this.heat += d;
        return this;
    }
};

int main() {
    printf("=== val-043 List.GetMut (by-value class) ===\n\n");

    auto fleet = List<Unit>();
    fleet.Add(Unit(1, 10));
    fleet.Add(Unit(2, 20));
    fleet.Add(Unit(3, 30));

    /* Get returns a copy — Boost must NOT stick. */
    Unit tmp = fleet.Get(0);
    tmp.Boost(100);
    check(fleet.Get(0).heat == 10, "Get copy Boost does not mutate list");

    /* GetMut returns pointer into buffer — Boost sticks. */
    fleet.GetMut(0).Boost(5).Boost(2);
    check(fleet.Get(0).heat == 17, "GetMut Boost chains into buffer");
    check(fleet.Get(0).id == 1,    "GetMut preserves id");

    /* [] sugar → GetMut lvalue: fleet[i].Method mutates buffer */
    fleet[1].Boost(3);
    check(fleet.Get(1).heat == 23, "fleet[i].Boost mutates buffer");

    fleet.FirstMut().Boost(1);
    check(fleet.First().heat == 18, "FirstMut mutates index 0");

    fleet.LastMut().Boost(7);
    check(fleet.Last().heat == 37, "LastMut mutates last");

    /* ints: GetMut and [] still work */
    auto nums = List<int>();
    nums.Add(1); nums.Add(2);
    *nums.GetMut(1) = 99;
    check(nums.Get(1) == 99, "GetMut int assign");
    nums[0] = 42;   /* Set protocol */
    check(nums.Get(0) == 42, "nums[i] = via Set");

    /* &fleet[i] → element pointer alias */
    Unit* alias = &fleet[2];
    alias.Boost(1);
    check(fleet.Get(2).heat == 38, "&fleet[i] aliases buffer");

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed ? 1 : 0;
}
