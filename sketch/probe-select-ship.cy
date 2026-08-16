#include <stdio.h>
#include "list.h"
class Ship {
    int heat;
    String name;
    Ship(int h, String n) { this.heat = h; this.name = n; }
};
int main() {
    auto fleet = List<Ship>();
    fleet.Add(Ship(10, "A"));
    fleet.Add(Ship(20, "B"));
    auto heats = fleet.Select((Ship s) => s.heat);
    auto names = fleet.Select<String>((Ship s) => s.name);
    printf("heats=%d names=%d first_heat=%d\n", heats.Count(), names.Count(), heats.Get(0));
    return 0;
}
