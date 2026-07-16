/* val-046-select-stack.cy — method-generic Select on pure stack List receivers.
 *
 * Regression for dangling orig_name: expression-context `List<T>()` used to
 * store a stack buffer in generic_specs.orig_name, so Select could not resolve
 * on pure stack lists while `new List<T>()` (interned id) worked.
 *
 * Run: ./bin/classyc -g -I include cy-validate/val-046-select-stack.cy -eg
 */
#include <stdio.h>
#include <string.h>
#include "list.h"

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

int times2(int x) { return x * 2; }
int plus1(int x) { return x + 1; }

class LapSample {
    int lap;
    int ms;
    LapSample(int lap, int ms) { this.lap = lap; this.ms = ms; }
};

int lap_ms(LapSample s) { return s.ms; }

class Ship {
    int heat;
    String callsign;
    Ship(int heat, String callsign) {
        this.heat = heat;
        this.callsign = callsign;
    }
};

int main() {
    printf("=== val-046 Select on stack List (no prior List*) ===\n\n");

    /* ── 1. Pure stack List<int> — never use new List ───────────────────── */
    printf("-- 1. stack List<int> Select --\n");
    {
        auto xs = List<int>();
        xs.Add(1); xs.Add(2); xs.Add(3);
        auto d = xs.Select<int>(times2);
        check(d.Count() == 3 && d.Get(0) == 2 && d.Get(1) == 4 && d.Get(2) == 6,
              "1a Select<int> on stack List");
        auto e = xs.Select(plus1);
        check(e.Count() == 3 && e.Get(0) == 2 && e.Get(2) == 4,
              "1b Select inferred U on stack List");
    }

    /* ── 2. Stack List + value class element ────────────────────────────── */
    printf("\n-- 2. stack List<LapSample> Select --\n");
    {
        auto samples = List<LapSample>();
        samples.Add(LapSample(1, 100));
        samples.Add(LapSample(2, 200));
        auto times = samples.Select<int>(lap_ms);
        check(times.Count() == 2 && times.Get(0) == 100 && times.Get(1) == 200,
              "2a Select LapSample→int");
        /* Avoid naming the result `ms` — lambda body field `s.ms` would look
           like a free ref to the incomplete local (capture false positive). */
        auto times2 = samples.Select((LapSample s) => s.ms);
        check(times2.Count() == 2 && times2.Get(0) == 100, "2b Select with lambda");
    }

    /* ── 3. Stack List + Select after Where (value chain) ───────────────── */
    printf("\n-- 3. Where then Select chain --\n");
    {
        auto fleet = List<Ship>();
        fleet.Add(Ship(88, "AURORA"));
        fleet.Add(Ship(22, "GLITCH"));
        fleet.Add(Ship(91, "NEXUS"));
        auto hot = fleet.Where((Ship s) => s.heat >= 50);
        auto names = hot.Select<String>((Ship s) => s.callsign);
        check(names.Count() == 2, "3a Select String after Where");
        check(strcmp((char*)names.Get(0), "AURORA") == 0
              || strcmp((char*)names.Get(0), "NEXUS") == 0,
              "3b first name is a hot ship");
        auto heats = fleet.Select((Ship s) => s.heat);
        check(heats.Count() == 3 && heats.Get(0) == 88, "3c Select heat inferred");
    }

    /* ── 4. Heap path still works (val-031 regression) ──────────────────── */
    printf("\n-- 4. heap List* Select regression --\n");
    {
        List<int>* xs = new List<int>{1, 2};
        defer delete xs;
        auto d = xs->Select<int>(times2);
        check(d.Count() == 2 && d.Get(0) == 2 && d.Get(1) == 4,
              "4a heap Select still works");
    }

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
