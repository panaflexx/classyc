/* probe-hof-deref.cy — R2.2a capturing-lambda GetMut-deref substitution
 *
 * A: capturing Where over List<Ship> (class T) — param read-only
 * B: capturing CountWhere (zero copies after rewrite)
 * C: capturing Find
 * D: lambda mutates its param — must keep copy semantics (no buffer write)
 * E: nested move-only List<List<int>> Where — keeps Get (Copy) path
 */
#include <stdio.h>
#include "list.h"

class Ship {
    int id;
    String callsign;
    int heat;
    Ship(int id, String c, int h) { this.id = id; this.callsign = c; this.heat = h; }
    ~Ship() {}
    int IsHot() { return this.heat >= 50; }
    int Id() { return this.id; }
    void Boost(int d) { this.heat += d; }
};

int main() {
    auto fleet = List<Ship>();
    fleet.Add(Ship(1, "AURORA", 88));
    fleet.Add(Ship(2, "VEILRUN", 61));
    fleet.Add(Ship(3, "RIMSPARK", 44));
    fleet.Add(Ship(4, "KITE", 73));

    int floor = 60;
    /* A: capturing Where, read-only param */
    auto hot = fleet.Where((Ship s) => s.heat > floor);
    printf("A hot=%d (expect 3)\n", hot.Count());
    long sa = 0;
    for (auto s in hot) sa += s.heat;
    printf("A heats=%ld (expect %ld)\n", sa, (long)(88+61+73));

    /* B: capturing CountWhere */
    int want = 70;
    int n = fleet.CountWhere((Ship s) => s.heat > want);
    printf("B n=%d (expect 2)\n", n);

    /* C: capturing Find */
    int target = 2;
    Ship f = fleet.Find((Ship s) => s.id == target);
    printf("C find=%d %s (expect 2 VEILRUN)\n", f.id, f.id != 0 ? (char*)f.callsign : "?");

    /* D: mutating lambda — copy semantics must hold (fleet unchanged) */
    auto boosted = fleet.Where((Ship s) => { s.Boost(1000); return s.IsHot(); });
    long sd = 0;
    for (auto s in fleet) sd += s.heat;
    printf("D fleet=%ld (expect %ld — Boost lost) kept=%d\n", sd, (long)(88+61+44+73),
           boosted.Count());

    /* E: move-only nested — Get path preserved */
    auto outer = List<List<int>>();
    auto a = List<int>(); a.Add(1); a.Add(2);
    auto b = List<int>(); b.Add(9);
    outer.Add(move a);
    outer.Add(move b);
    int minlen = 2;
    auto big = outer.Where((List<int> x) => x.Count() >= minlen);
    printf("E big=%d first=%d (expect 1 1)\n", big.Count(), big.Get(0).Get(0));
    printf("E outer intact=%d (expect 2)\n", outer.Count());
    return 0;
}
