#include <stdio.h>
#include "list.h"
class LapSample {
    int lap; int ms;
    LapSample(int lap, int ms) { this.lap = lap; this.ms = ms; }
};
int lap_ms(LapSample s) { return s.ms; }
int main() {
    auto samples = List<LapSample>();
    samples.Add(LapSample(1, 100));
    samples.Add(LapSample(2, 200));
    auto a = samples.Select<int>(lap_ms);
    printf("a count=%d first=%d\n", a.Count(), a.Get(0));
    return 0;
}
