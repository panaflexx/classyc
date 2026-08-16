#include <stdio.h>
#include "list.h"
class Ship {
    int heat; String callsign;
    Ship(int heat, String callsign) { this.heat = heat; this.callsign = callsign; }
};
void Seed(List<Ship>* fleet) {
    fleet.Add(Ship(88, "AURORA"));
    fleet.Add(Ship(22, "GLITCH"));
}
int main() {
    auto fleet = List<Ship>();
    Seed(&fleet);
    auto heats = fleet.Select((Ship s) => s.heat);
    auto names = fleet.Select<String>((Ship s) => s.callsign);
    printf("heats=%d names=%d first=%d\n", heats.Count(), names.Count(), heats.Get(0));
    return 0;
}
