/* probe-getmut-deref.cy — validate the `T* p = GetMut(i); pred(*p); Add(*p)`
 * pattern for by-value class T (String member, user dtor) and move-only
 * nested T (List<List<int>>) — the planned list.h HOF rewrite. */
#include <stdio.h>
#include "list.h"

class Ship {
    int id;
    String callsign;
    int heat;
    Ship(int id, String callsign, int heat) {
        this.id = id; this.callsign = callsign; this.heat = heat;
    }
    ~Ship() {}
    int IsHot() { return this.heat >= 50; }
};

int is_hot_ptr_side(Ship s) { return s.IsHot(); }

int main() {
    /* class T */
    auto fleet = List<Ship>();
    fleet.Add(Ship(1, "AURORA", 88));
    fleet.Add(Ship(2, "VEILRUN", 61));
    fleet.Add(Ship(3, "RIMSPARK", 44));

    auto result = List<Ship>();
    int cnt = 0;
    for (int i = 0; i < fleet.Count(); i++) {
        Ship* p = fleet.GetMut(i);
        if (is_hot_ptr_side(*p)) { result.Add(*p); cnt++; }
    }
    printf("hot=%d result=%d (expect 2 2)\n", cnt, result.Count());
    long sum = 0;
    for (int i = 0; i < result.Count(); i++) sum += result.Get(i).heat;
    printf("heat sum=%ld (expect %ld)\n", sum, (long)(88+61));
    long fsum = 0;
    for (int i = 0; i < fleet.Count(); i++) fsum += fleet.Get(i).heat;
    printf("fleet intact=%ld (expect %ld)\n", fsum, (long)(88+61+44));

    /* move-only nested T */
    auto outer = List<List<int>>();
    auto a = List<int>(); a.Add(1); a.Add(2);
    auto b = List<int>(); b.Add(9);
    outer.Add(move a);
    outer.Add(move b);
    auto kept = List<List<int>>();
    for (int i = 0; i < outer.Count(); i++) {
        List<int>* p = outer.GetMut(i);
        if (p->Count() >= 2) kept.Add(*p);   /* Copy rewrite from lvalue */
    }
    printf("kept=%d first0=%d (expect 1 1)\n", kept.Count(), kept.Get(0).Get(0));
    printf("outer intact=%d (expect 2)\n", outer.Count());
    return 0;
}
