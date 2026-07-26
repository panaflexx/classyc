/* val-052-list-view.cy — generic forward decl + ListView + CountWhere
 *
 * Language:
 *   class Peer<T>; ... class Peer<T> { ... }   // incomplete generic, then body
 *   Mutual Host ↔ Peer method returns (pre-register + MIR forwards)
 *
 * Stdlib:
 *   List.View() → ListView span
 *   CountWhere on List and ListView (capturing + non-capturing)
 *   View.ToList / Slice / for-in
 *
 * Run:  ./bin/classyc -g -I include cy-validate/val-052-list-view.cy -eg
 */

#include <stdio.h>
#include <string.h>
#include "list.h"

/* ── language: mutual generics via forward decl ─────────────────────────── */

class Peer<T>;

class Host<T> {
    T v;
    Host(T v) { this.v = v; }
    Peer<T> make_peer() { return Peer<T>(v); }
    T get() { return v; }
};

class Peer<T> {
    T v;
    Peer(T v) { this.v = v; }
    T get() { return v; }
    Host<T> make_host() { return Host<T>(v); }
};

/* ── domain DTO for ListView ────────────────────────────────────────────── */

[[copyable_no_release]]
class Ship {
    int    id;
    int    credits;
    int    alive;
    String name;

    Ship(int id, String name, int credits, int alive) {
        this.id = id;
        this.name = name;
        this.credits = credits;
        this.alive = alive;
    }
    ~Ship() {}

    int IsAlive() { return alive != 0; }
    int IsRich()  { return credits >= 100; }
};

int is_even(int x) { return (x % 2) == 0; }

int main() {
    printf("=== val-052 generic forward + ListView ===\n");

    /* mutual Host ↔ Peer */
    auto h = Host<int>(42);
    auto p = h.make_peer();
    auto h2 = p.make_host();
    if (h.get() != 42 || p.get() != 42 || h2.get() != 42) {
        printf("FAIL mutual Host/Peer\n");
        return 1;
    }
    printf("  mutual Host/Peer ok\n");

    /* ints + CountWhere + View */
    auto nums = List<int>();
    nums.Add(1); nums.Add(2); nums.Add(3); nums.Add(4); nums.Add(5);

    int thr = 2;
    int above = nums.CountWhere((int x) => x > thr);
    int evens = nums.CountWhere(is_even);
    printf("  CountWhere capture (>%d)=%d  non-cap even=%d\n", thr, above, evens);
    if (above != 3 || evens != 2) {
        printf("FAIL CountWhere ints\n");
        return 1;
    }

    auto v = nums.View();
    printf("  View Count=%d First=%d Last=%d\n", v.Count(), v.First(), v.Last());
    if (v.Count() != 5 || v.First() != 1 || v.Last() != 5) {
        printf("FAIL View basics\n");
        return 1;
    }

    int v_above = v.CountWhere((int x) => x > thr);
    if (v_above != 3) {
        printf("FAIL View.CountWhere capture got %d\n", v_above);
        return 1;
    }

    auto mid = v.Slice(1, 3);
    if (mid.Count() != 3 || mid.Get(0) != 2 || mid.Get(2) != 4) {
        printf("FAIL Slice\n");
        return 1;
    }

    auto copy = List<int>.FromView(mid);
    if (copy.Count() != 3 || copy.Get(0) != 2 || copy.Get(1) != 3 || copy.Get(2) != 4) {
        printf("FAIL FromView count=%d\n", copy.Count());
        return 1;
    }

    int sum = 0;
    for (auto x in v) sum += x;
    if (sum != 15) {
        printf("FAIL for-in View sum=%d\n", sum);
        return 1;
    }

    if (!v.Any((int x) => x == 4) || !v.All((int x) => x > 0)) {
        printf("FAIL View Any/All\n");
        return 1;
    }
    int found = v.Find((int x) => x > 3);
    if (found != 4) {
        printf("FAIL View.Find got %d\n", found);
        return 1;
    }

    /* by-value class elements */
    auto fleet = List<Ship>();
    fleet.Add(Ship(1, "AURORA", 200, 1));
    fleet.Add(Ship(2, "HAWK", 50, 1));
    fleet.Add(Ship(3, "WRECK", 0, 0));
    fleet.Add(Ship(4, "MULE", 150, 1));

    int alive = fleet.CountWhere((Ship s) => s.IsAlive());
    int rich  = fleet.CountWhere((Ship s) => s.IsAlive() && s.IsRich());
    printf("  fleet CountWhere alive=%d rich=%d\n", alive, rich);
    if (alive != 3 || rich != 2) {
        printf("FAIL Ship CountWhere\n");
        return 1;
    }

    auto fv = fleet.View();
    int alive2 = fv.CountWhere((Ship s) => s.IsAlive());
    if (alive2 != 3) {
        printf("FAIL fleet.View CountWhere\n");
        return 1;
    }

    fleet.GetMut(1).credits = 999;
    int rich2 = fleet.View().CountWhere((Ship s) => s.IsAlive() && s.IsRich());
    if (rich2 != 3) {
        printf("FAIL View sees GetMut (rich2=%d)\n", rich2);
        return 1;
    }

    printf("=== val-052 OK ===\n");
    return 0;
}
