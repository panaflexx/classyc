#include <stdio.h>
#include "list.h"
class LapSample {
    int lap; int ms;
    LapSample(int lap, int ms) { this.lap = lap; this.ms = ms; }
};
void Seed(List<LapSample>* xs) {
    xs.Add(LapSample(1, 100));
    xs.Add(LapSample(2, 200));
}
int main() {
    auto samples = List<LapSample>();
    Seed(&samples);
    auto ms = samples.Select((LapSample s) => s.ms);
    printf("ms=%s\n", ms.ToJson());
    return 0;
}
