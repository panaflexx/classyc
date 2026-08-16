#include <stdio.h>
#include "list.h"
#include "map.h"
#include "set.h"
class Ship {
    int id; String callsign; int heat; int range_ly;
    Ship(int id, String callsign, int heat, int range_ly) {
        this.id = id; this.callsign = callsign; this.heat = heat; this.range_ly = range_ly;
    }
    int IsHot() { return heat >= 50; }
    int Alive() { return id != 0; }
    int IsDeep() { return range_ly >= 120; }
    String ToString() { return callsign; }
};
void print_banner(Ship s) { printf("banner %s\n", s.callsign); }
int ByIntAsc(int a, int b) { return a - b; }
int main() {
    auto fleet = List<Ship>();
    fleet.Add(Ship(1, "AURORA", 95, 142));
    fleet.Add(Ship(5, "NEXUS", 91, 76));
    fleet.Add(Ship(8, "GLITCH", 22, 54));
    auto roster = fleet.Copy();
    auto hot_raw = fleet.Where((Ship s) => s.IsHot());
    auto hot = hot_raw.Copy();
    hot.Sort((Ship a, Ship b) => b.heat - a.heat);
    auto hot_names = hot.Select<String>((Ship s) => s.callsign);
    auto heats = roster.Select((Ship s) => s.heat);
    printf("names=%s heats=%s\n", hot_names.ToJson(), heats.ToJson());
    return 0;
}
