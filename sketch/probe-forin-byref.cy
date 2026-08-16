/* probe-forin-byref.cy — R2.1 by-ref for-in loop vars
 *
 * A: read-only body (fields + read-only methods) → by-ref bind (no copy)
 * B: mutation via the loop var's field      → copy fallback (snapshot)
 * C: mutation of the collection in body     → copy fallback
 * D: two-var form (index, element)          → by-ref for the element
 * E: method call on the var that writes     → copy fallback
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
    String Name() { return this.callsign; }
};

static void seed(List<Ship>* f) {
    f->Add(Ship(1, "AURORA", 88));
    f->Add(Ship(2, "VEILRUN", 61));
    f->Add(Ship(3, "RIMSPARK", 44));
}

int main() {
    auto fleet = List<Ship>();
    seed(&fleet);

    /* A: pure reads — byref */
    long sa = 0;
    int hot = 0;
    for (auto s in fleet) {
        sa += s.heat + s.Id();
        if (s.IsHot()) hot++;
    }
    printf("A sum=%ld hot=%d (expect %ld 2)\n", sa, (long)(88+1 + 61+2 + 44+3));

    /* B: write through the var — copy fallback; must NOT affect fleet */
    for (auto s in fleet) s.heat = 0;
    long sb = 0;
    for (auto s in fleet) sb += s.heat;
    printf("B fleet heat=%ld (expect %ld — snapshot preserved)\n", sb, (long)(88+61+44));

    /* C: mutate collection in body — copy fallback */
    int cnt = 0;
    for (auto s in fleet) {
        cnt += s.Id();
        if (s.id == 2) fleet.Add(Ship(9, "X", 1));   /* growth during iteration */
    }
    printf("C cnt=%d fleet=%d (expect %d 4)\n", cnt, fleet.Count(), 1+2+3+9);

    /* D: two-var form */
    long sd = 0;
    for (auto i, s in fleet) sd += i + s.heat;
    printf("D sum=%ld (expect %ld)\n", sd, (long)(0+1+2+3 + 88+61+44+1));

    /* E: mutating method on the var — copy fallback; fleet unchanged */
    for (auto s in fleet) s.Boost(100);
    long se = 0;
    for (auto s in fleet) se += s.heat;
    printf("E fleet heat=%ld (expect %ld — Boost lost on copy)\n", se, (long)(88+61+44+1));

    /* F: pointer receiver — same read-only body as A */
    owned auto pf = new List<Ship>();
    seed(pf);
    long sf = 0;
    int hf = 0;
    for (auto s in pf) {
        sf += s.heat + s.Id();
        if (s.IsHot()) hf++;
    }
    printf("F ptr sum=%ld hot=%d (expect %ld 2)\n", sf, hf, (long)(88+1 + 61+2 + 44+3));

    /* G: *p deref receiver */
    long sg = 0;
    for (auto s in *pf) sg += s.heat;
    printf("G deref sum=%ld (expect %ld)\n", sg, (long)(88+61+44));
    return 0;
}
