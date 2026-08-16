/* probe-where-single-get.cy — validate that `T item = Get(i)` inside a
 * Where-style loop is correct for by-value class T with a String member
 * and a user dtor (the pattern list.h Filter/Where avoids via double-Get).
 *
 * If this prints correct results under -eg/-ei/-el/-eb, the single-Get
 * rewrite is safe to land in list.h.
 */

#include <stdio.h>
#include <string.h>
#include "list.h"

class Ship {
    int id;
    String callsign;
    int heat;

    Ship(int id, String callsign, int heat) {
        this.id = id;
        this.callsign = callsign;
        this.heat = heat;
    }
    ~Ship() {}   /* user dtor — the RAII-registration case the comment warns about */

    int IsHot() { return this.heat >= 50; }
    String Name() { return this.callsign; }
};

int is_hot(Ship s) { return s.IsHot(); }

int main() {
    auto fleet = List<Ship>();
    fleet.Add(Ship(1, "AURORA", 88));
    fleet.Add(Ship(2, "VEILRUN", 61));
    fleet.Add(Ship(3, "RIMSPARK", 44));
    fleet.Add(Ship(4, "KITE", 73));
    fleet.Add(Ship(5, "NEXUS", 91));
    fleet.Add(Ship(6, "EMBER", 35));

    /* single-Get pattern: named by-value local + pred + Add of the local */
    auto result = List<Ship>();
    for (int i = 0; i < fleet.Count(); i++) {
        Ship item = fleet.Get(i);
        if (is_hot(item)) result.Add(item);
    }

    printf("hot count=%d (expect 4)\n", result.Count());
    long sum = 0;
    for (int i = 0; i < result.Count(); i++) {
        Ship s = result.Get(i);
        printf("  %d %s %d\n", s.id, (char*)s.callsign, s.heat);
        sum += s.id * 1000 + s.heat;
    }
    printf("checksum=%ld (expect %ld)\n", sum,
           1*1000+88 + 2*1000+61 + 4*1000+73 + 5*1000+91);

    /* source list must be intact after the loop */
    long src = 0;
    for (int i = 0; i < fleet.Count(); i++) src += fleet.Get(i).heat;
    printf("fleet intact sum=%ld (expect %ld)\n", src, 88+61+44+73+91+35);
    return 0;
}
