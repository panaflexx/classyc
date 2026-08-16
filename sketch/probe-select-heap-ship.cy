#include <stdio.h>
#include "list.h"
class Ship {
    int heat;
    Ship(int heat) { this.heat = heat; }
};
int get_heat(Ship s) { return s.heat; }
int main() {
    List<Ship>* fleet = new List<Ship>();
    fleet.Add(Ship(10));
    fleet.Add(Ship(20));
    auto heats = fleet->Select<int>(get_heat);
    printf("heats=%d first=%d\n", heats.Count(), heats.Get(0));
    defer delete fleet;
    return 0;
}
