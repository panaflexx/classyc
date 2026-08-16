#include <stdio.h>
#include "list.h"
class Ship {
    int heat;
    String callsign;
    Ship(int heat, String callsign) { this.heat = heat; this.callsign = callsign; }
    int IsHot() { return heat >= 50; }
};
int main() {
    auto fleet = List<Ship>();
    fleet.Add(Ship(88, "AURORA"));
    fleet.Add(Ship(22, "GLITCH"));
    auto hot = fleet.Where((Ship s) => s.IsHot());
    auto hot_names = hot.Select<String>((Ship s) => s.callsign);
    auto heats = fleet.Select((Ship s) => s.heat);
    printf("hot=%d names=%d heats=%d first_heat=%d\n",
           hot.Count(), hot_names.Count(), heats.Count(), heats.Get(0));
    return 0;
}
