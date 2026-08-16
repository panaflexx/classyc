#include <stdio.h>
#include "list.h"
class Ship {
    int heat;
    String callsign;
    Ship(int heat, String callsign) { this.heat = heat; this.callsign = callsign; }
};
int main() {
    /* Force heap specialization of List<Ship> first */
    List<Ship>* warm = new List<Ship>();
    warm.Add(Ship(1, "x"));
    defer delete warm;

    auto fleet = List<Ship>();
    fleet.Add(Ship(88, "AURORA"));
    fleet.Add(Ship(22, "GLITCH"));
    auto heats = fleet.Select((Ship s) => s.heat);
    auto names = fleet.Select<String>((Ship s) => s.callsign);
    printf("heats=%d names=%d\n", heats.Count(), names.Count());
    return 0;
}
