/* probe-map-forin-byref.cy — Map for-in by-ref value binding
 *
 * A: read-only body over Map<String, Ship> → by-ref value var (no copy)
 * B: write through the value var        → copy fallback (snapshot)
 * C: mutate the map in body (m[k] = v)  → copy fallback
 * D: single-var form (keys only)        → no byref, keys only
 */
#include <stdio.h>
#include "map.h"
#include "list.h"

class Ship {
    int id;
    String callsign;
    int heat;
    Ship(int id, String c, int h) { this.id = id; this.callsign = c; this.heat = h; }
    ~Ship() {}
    int IsHot() { return this.heat >= 50; }
    String Name() { return this.callsign; }
    void Boost(int d) { this.heat += d; }
};

int main() {
    auto wing = Map<String, Ship>();
    wing["AURORA"] = Ship(1, "AURORA", 88);
    wing["VEILRUN"] = Ship(2, "VEILRUN", 61);
    wing["RIMSPARK"] = Ship(3, "RIMSPARK", 44);

    /* A: pure reads — byref */
    long sa = 0;
    int nkeys = 0;
    for (auto sign, s in wing) {
        sa += s.heat + s.id;
        if (s.IsHot()) nkeys++;
        printf("  A [%s] %s heat=%d\n", (char*)sign, (char*)s.Name(), s.heat);
    }
    printf("A sum=%ld hot=%d (expect %ld 2)\n", sa, (long)(88+1 + 61+2 + 44+3));

    /* B: write through value var — copy fallback; wing unchanged */
    for (auto k, s in wing) s.heat = 0;
    long sb = 0;
    for (auto k, s in wing) sb += s.heat;
    printf("B wing heat=%ld (expect %ld — snapshot preserved)\n", sb, (long)(88+61+44));

    /* C: mutate map in body — copy fallback */
    int cnt = 0;
    for (auto k, s in wing) {
        cnt += s.id;
        if (s.id == 2) wing["KITE"] = Ship(4, "KITE", 73);
    }
    printf("C cnt=%d wing=%d (expect 6 4)\n", cnt, wing.Count());

    /* D: single-var (keys) form */
    int nk = 0;
    for (auto k in wing) nk++;
    printf("D keys=%d (expect 4)\n", nk);
    return 0;
}
