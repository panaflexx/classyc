/* val-044-map-class-forin.cy — Map for-in with by-value class values.
 *
 * for (auto k, v in map) where V is a class must bind a value loop var
 * (not reject ValAt).  GetMut/ValMut mutate the dense buffer in place.
 *
 * Run: ./bin/classyc -I include cy-validate/val-044-map-class-forin.cy -eg
 */
#include <stdio.h>
#include <string.h>
#include "map.h"
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
    printf("=== val-044 Map class for-in / GetMut ===\n\n");

    auto wing = Map<String, Unit>();
    wing.Set("AURORA", Unit(1, 88));
    wing.Set("NEXUS", Unit(5, 91));
    wing.Set("GLITCH", Unit(8, 22));

    int n = 0;
    int heat_sum = 0;
    for (auto sign, u in wing) {
        n++;
        heat_sum += u.heat;
        check(sign != NULL && ((const char*)sign)[0] != 0, "for-in key non-empty");
        check(u.id > 0, "for-in value class id");
    }
    check(n == 3, "for-in visits 3 entries");
    check(heat_sum == 88 + 91 + 22, "for-in value heats sum");

    /* ValAt returns a copy */
    Unit copy = wing.ValAt(0);
    copy.Boost(100);
    check(wing.ValAt(0).heat != copy.heat || wing.ValAt(0).heat == 88
              || wing.ValAt(0).heat == 91 || wing.ValAt(0).heat == 22,
          "ValAt is by-value (copy independent)");

    /* GetMut mutates storage */
    Unit* p = wing.GetMut("AURORA");
    check(p != NULL, "GetMut hits AURORA");
    p.Boost(7);
    check(wing.Get("AURORA").heat == 95, "GetMut Boost sticks");

    try {
        (void)wing.GetMut("MISSING");
        check(0, "GetMut missing should throw");
    } catch (e) {
        check(1, "GetMut missing throws KeyException");
    }

    /* [] sugar → GetMut lvalue for map values too */
    wing["NEXUS"].Boost(4);
    check(wing.Get("NEXUS").heat == 95, "map[k].Boost mutates buffer");

    /* ValMut by dense index */
    wing.ValMut(1).Boost(1);
    int any_boosted = 0;
    for (auto k, u in wing) {
        (void)k;
        if (u.heat == 92 || u.heat == 23 || u.heat == 96) any_boosted = 1;
    }
    check(any_boosted, "ValMut changed some heat");

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed ? 1 : 0;
}
