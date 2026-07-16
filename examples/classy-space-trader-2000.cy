// @expect: skip
/* classy-space-trader-2000.cy — SPACE TRADER 2000
 *
 * Long-running galaxy sim for GEN-OPT / midopt benchmarking.
 * Stresses: by-value List/Map, dense for-in, Where/Find/Sort, Map[k],
 * GetMut, String f-strings, stack RAII shells.
 *
 * Default wall time: 60 seconds of simulation ticks.
 * Override:  ST2000_SECONDS=10 ./bin/classyc -I include examples/classy-space-trader-2000.cy -eg
 * Quick smoke: ST2000_SECONDS=2 …
 *
 * Bench midopt (from repo root):
 *   /usr/bin/time -p ./bin/classyc -I include -fno-exceptions -O2 \
 *       examples/classy-space-trader-2000.cy -eg
 *   ST2000_SECONDS=60 /usr/bin/time -p ./bin/classyc -I include -fno-exceptions -O2 \
 *       -fno-midopt examples/classy-space-trader-2000.cy -eg
 *
 * (Marked @expect: skip so examples/run-examples.sh does not wait a full minute.)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "list.h"
#include "map.h"
#include "set.h"

/* ───────────────────────── World scale ───────────────────────── */

enum { N_PLANETS = 48, N_OUTPOSTS = 72, N_SHIPS = 256, N_CARGO_KINDS = 12 };

/* ───────────────────────── Domain ───────────────────────── */

enum Faction { free_traders = 0, imperial = 1, corsair = 2, syndicate = 3 };
enum Goods   { ore = 0, spice = 1, tech = 2, weapons = 3, food = 4, luxury = 5,
               fuel = 6, meds = 7, scrap = 8, datachips = 9, artifacts = 10, water = 11 };

class Planet {
    int    id;
    String name;
    int    x, y;           /* sector coords */
    int    wealth;         /* 1..100 */
    int    danger;         /* 1..100 — pirate density */
    Faction faction;

    Planet(int id, String name, int x, int y, int wealth, int danger, Faction f) {
        this.id = id;
        this.name = name;
        this.x = x;
        this.y = y;
        this.wealth = wealth;
        this.danger = danger;
        this.faction = f;
    }
    ~Planet() {}

    int PriceBias(int good) {
        /* Wealthy worlds pay more for luxury; poor for food/ore. */
        int g = good % N_CARGO_KINDS;
        if (g == luxury || g == artifacts) return wealth / 4;
        if (g == food || g == ore || g == water) return (100 - wealth) / 5;
        return danger / 8;
    }
};

class Outpost {
    int    id;
    String callsign;
    int    planet_id;
    int    inventory[N_CARGO_KINDS];
    int    stock;

    Outpost(int id, String callsign, int planet_id) {
        this.id = id;
        this.callsign = callsign;
        this.planet_id = planet_id;
        this.stock = 0;
        int i;
        for (i = 0; i < N_CARGO_KINDS; i++) {
            this.inventory[i] = 10 + (id * 3 + i * 7) % 40;
            this.stock += this.inventory[i];
        }
    }
    ~Outpost() {}

    int Buy(int good, int qty) {
        int g = good % N_CARGO_KINDS;
        if (qty <= 0 || inventory[g] < qty) return 0;
        inventory[g] -= qty;
        stock -= qty;
        return 1;
    }
    void Sell(int good, int qty) {
        int g = good % N_CARGO_KINDS;
        if (qty <= 0) return;
        inventory[g] += qty;
        stock += qty;
    }
};

class Trader {
    int     id;
    String  name;
    Faction faction;
    int     planet_id;
    int     credits;
    int     hull;          /* 0..100 */
    int     cargo[N_CARGO_KINDS];
    int     cargo_tons;
    int     kills;
    int     trades;
    int     alive;

    Trader(int id, String name, Faction f, int planet_id, int credits) {
        this.id = id;
        this.name = name;
        this.faction = f;
        this.planet_id = planet_id;
        this.credits = credits;
        this.hull = 100;
        this.cargo_tons = 0;
        this.kills = 0;
        this.trades = 0;
        this.alive = 1;
        int i;
        for (i = 0; i < N_CARGO_KINDS; i++) this.cargo[i] = 0;
    }
    ~Trader() {}

    int IsAlive() { return alive && hull > 0; }
    int IsCorsair() { return faction == corsair; }
    int IsRich() { return credits >= 12000; }

    void Load(int good, int qty) {
        int g = good % N_CARGO_KINDS;
        if (qty <= 0) return;
        cargo[g] += qty;
        cargo_tons += qty;
    }
    int Unload(int good, int qty) {
        int g = good % N_CARGO_KINDS;
        if (qty <= 0 || cargo[g] < qty) return 0;
        cargo[g] -= qty;
        cargo_tons -= qty;
        return 1;
    }

    void Damage(int d) {
        hull -= d;
        if (hull <= 0) { hull = 0; alive = 0; }
    }
    void Repair(int r) {
        hull += r;
        if (hull > 100) hull = 100;
        if (hull > 0) alive = 1;
    }
};

/* POD combat event — by-value in List */
class Skirmish {
    int tick;
    int a_id, b_id;
    int dmg_a, dmg_b;
    int fatal;

    Skirmish(int tick, int a, int b, int da, int db, int fatal) {
        this.tick = tick;
        this.a_id = a;
        this.b_id = b;
        this.dmg_a = da;
        this.dmg_b = db;
        this.fatal = fatal;
    }
};

/* ───────────────────────── RNG (xorshift) ───────────────────────── */

unsigned g_rng = 0xC0FFEE42u;

unsigned rnd_u() {
    unsigned x = g_rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g_rng = x;
    return x;
}
int rnd(int lo, int hi) {
    if (hi <= lo) return lo;
    return lo + (int)(rnd_u() % (unsigned)(hi - lo + 1));
}

/* ───────────────────────── Name tables ───────────────────────── */

const char *planet_names[] = {
    "Kepler-Prime", "New Ceres", "Rigel Station", "Helios IV", "Dusthaven",
    "Cryonia", "Vega Reach", "Orion's End", "Nova Mutual", "Ashfall",
    "Port Meridian", "Lumen Bay", "Sable Drift", "Echo Reef", "Tarn's World",
    "Ironwell", "Solstice", "Parallax", "Nimbus Gate", "Cobalt Reach",
    "Farside", "Amber Orbit", "Zenith Dock", "Redline", "Polaris Freehold",
    "Marrow Deep", "Quiet Sun", "Shard Belt", "Gossamer", "Undertow",
    "High Anchor", "Low Orbit", "Silk Road", "Brass Moon", "Tin Sky",
    "Copper Fall", "Glass Sea", "Bone Dry", "Soft Landing", "Hard Light",
    "Second Chance", "Last Call", "First Contact", "Zero Point", "Nine Tails",
    "Lucky Strike", "Dead Reckon", "True North"
};

const char *ship_prefixes[] = {
    "SS", "MV", "ISV", "CSV", "RSV", "HMS", "UNS", "FTL"
};
const char *ship_cores[] = {
    "Wanderer", "Prospector", "Fortune", "Hawk", "Mule", "Comet", "Raven",
    "Drifter", "Courier", "Phantom", "Titan", "Sparrow", "Nomad", "Corsair",
    "Merchant", "Vagabond", "Pioneer", "Ranger", "Clipper", "Barge"
};

String MakeShipName(int id) {
    const char *p = ship_prefixes[id % 8];
    const char *c = ship_cores[(id * 7) % 20];
    int n = 100 + (id * 13) % 900;
    return f"{p} {c}-{n}";
}

/* ───────────────────────── Seeding ───────────────────────── */

void SeedPlanets(List<Planet> *world) {
    int i;
    for (i = 0; i < N_PLANETS; i++) {
        Faction f = (Faction)(i % 4);
        world.Add(Planet(i, planet_names[i % 48], rnd(0, 200), rnd(0, 200),
                         rnd(15, 95), rnd(5, 90), f));
    }
}

void SeedOutposts(List<Outpost> *posts, List<Planet> *world) {
    int i;
    for (i = 0; i < N_OUTPOSTS; i++) {
        int pid = i % world.Count();
        String cs = f"OP-{i}";
        posts.Add(Outpost(i, cs, pid));
    }
}

void SeedTraders(List<Trader> *fleet) {
    int i;
    for (i = 0; i < N_SHIPS; i++) {
        Faction f = (Faction)(i % 4);
        int start = i % N_PLANETS;
        int cash = 8000 + rnd(0, 12000);
        if (f == corsair) cash = 3000 + rnd(0, 4000);
        if (f == imperial) cash = 15000 + rnd(0, 10000);
        fleet.Add(Trader(i, MakeShipName(i), f, start, cash));
    }
}

/* ───────────────────────── Economy / combat ───────────────────────── */

int BasePrice(int good) {
    int table[N_CARGO_KINDS] = {
        12, 28, 45, 60, 8, 90, 15, 35, 5, 55, 120, 6
    };
    return table[good % N_CARGO_KINDS];
}

int QuoteBuy(Planet p, int good) {
    return BasePrice(good) + p.PriceBias(good) + rnd(0, 6);
}
int QuoteSell(Planet p, int good) {
    int q = BasePrice(good) + p.PriceBias(good) / 2 - rnd(0, 4);
    return q < 2 ? 2 : q;
}

/* One trade attempt for ship i at its current planet / a random outpost. */
void DoTrade(List<Trader> *fleet, List<Planet> *world, List<Outpost> *posts, int i) {
    if (i < 0 || i >= fleet.Count()) return;
    Trader *t = fleet.GetMut(i);
    if (!t.IsAlive()) return;

    Planet p = world.Get(t.planet_id % world.Count());
    int post_ix = rnd(0, posts.Count() - 1);
    Outpost *op = posts.GetMut(post_ix);
    /* Prefer outposts orbiting this planet when possible */
    int tries = 0;
    while (op.planet_id != t.planet_id && tries < 6) {
        post_ix = rnd(0, posts.Count() - 1);
        op = posts.GetMut(post_ix);
        tries++;
    }

    int good = rnd(0, N_CARGO_KINDS - 1);
    int qty = rnd(1, 8);
    int buy = QuoteBuy(p, good);
    int sell = QuoteSell(p, good);

    /* Sell inventory first if any (mark-up so credits accumulate over a minute) */
    if (t.cargo[good] > 0 && rnd(0, 1) == 0) {
        int q = t.cargo[good] < qty ? t.cargo[good] : qty;
        if (t.Unload(good, q)) {
            t.credits += (sell + 4) * q;
            op.Sell(good, q);
            t.trades++;
        }
        return;
    }

    /* Buy if affordable and outpost has stock */
    int cost = buy * qty;
    if (t.credits >= cost && op.Buy(good, qty)) {
        t.credits -= cost;
        t.Load(good, qty);
        t.trades++;
    } else if (t.credits < 500) {
        /* Hard-luck stipend so broke traders re-enter the market */
        t.credits += rnd(50, 200);
    }
}

void DoTravel(List<Trader> *fleet, List<Planet> *world, int i) {
    if (i < 0 || i >= fleet.Count()) return;
    Trader *t = fleet.GetMut(i);
    if (!t.IsAlive()) return;
    if (rnd(0, 2) != 0) return; /* 1/3 chance to jump */
    int dest = rnd(0, world.Count() - 1);
    t.planet_id = dest;
    /* light fuel burn */
    int burn = rnd(2, 18);
    if (t.credits > burn) t.credits -= burn;
}

void DoBattle(List<Trader> *fleet, List<Planet> *world, List<Skirmish> *log,
              int tick, int *deaths) {
    int a = rnd(0, fleet.Count() - 1);
    int b = rnd(0, fleet.Count() - 1);
    if (a == b) return;
    Trader *ta = fleet.GetMut(a);
    Trader *tb = fleet.GetMut(b);
    if (!ta.IsAlive() || !tb.IsAlive()) return;
    if (ta.planet_id != tb.planet_id) return;

    Planet p = world.Get(ta.planet_id % world.Count());
    /* Keep combat rare so a 60s run stays populated (bench still does huge ticks). */
    int roll = rnd(0, 999);
    int threshold = 12 + p.danger / 20;
    if (ta.IsCorsair() || tb.IsCorsair()) threshold += 18;
    if (roll > threshold) return;

    int da = rnd(2, 10);
    int db = rnd(2, 10);
    if (ta.faction == imperial) da += 2;
    if (tb.faction == imperial) db += 2;

    ta.Damage(db);
    tb.Damage(da);
    int fatal = 0;
    if (!ta.IsAlive()) { fatal = 1; tb.kills++; (*deaths)++; }
    if (!tb.IsAlive()) { fatal = 1; ta.kills++; (*deaths)++; }

    /* Cap battle log so we don't grow forever */
    if (log.Count() < 8000)
        log.Add(Skirmish(tick, ta.id, tb.id, da, db, fatal));
}

/* Hot path: score every living ship each tick (dense for-in). */
long long ScoreFleet(List<Trader> *fleet) {
    long long score = 0;
    for (auto t in fleet) {
        if (!t.IsAlive()) continue;
        score += t.credits;
        score += t.cargo_tons * 15;
        score += t.kills * 200;
        score += t.hull;
    }
    return score;
}

int CountAlive(List<Trader> *fleet) {
    int n = 0;
    for (auto t in fleet)
        if (t.IsAlive()) n++;
    return n;
}

int CountCorsairsAlive(List<Trader> *fleet) {
    int n = 0;
    for (auto t in fleet)
        if (t.IsAlive() && t.IsCorsair()) n++;
    return n;
}

/* LINQ-style filter — midopt + capturing Where open-code */
int CountRich(List<Trader> *fleet) {
    auto rich = fleet.Where((Trader t) => t.IsAlive() && t.IsRich());
    return rich.Count();
}

/* Leaderboard: top credits among living (Sort + Take) */
void PrintLeaders(List<Trader> *fleet, int topn) {
    auto living = fleet.Where((Trader t) => t.IsAlive());
    living.Sort((Trader a, Trader b) => b.credits - a.credits);
    int n = living.Count() < topn ? living.Count() : topn;
    int i;
    printf("  ── Top %d traders ──\n", n);
    for (i = 0; i < n; i++) {
        Trader t = living.Get(i);
        printf("    #%d  %-22s  cr=%d  hull=%d  trades=%d  kills=%d\n",
               t.id, (char *)t.name, t.credits, t.hull, t.trades, t.kills);
    }
}

/* Map: planet_id → ship count this tick (dense rebuild) */
void RebuildPresence(Map<int, int> *presence, List<Trader> *fleet) {
    presence.Clear();
    for (auto t in fleet) {
        if (!t.IsAlive()) continue;
        int pid = t.planet_id;
        if (presence.Contains(pid))
            presence[pid] = presence[pid] + 1;
        else
            presence[pid] = 1;
    }
}

int HottestPlanet(Map<int, int> *presence) {
    int best_p = -1, best_n = -1;
    for (auto pid, n in presence) {
        if (n > best_n) { best_n = n; best_p = pid; }
    }
    return best_p;
}

/* ───────────────────────── main ───────────────────────── */

int main(void) {
    int seconds = 60;
    char *env = getenv("ST2000_SECONDS");
    if (env != NULL && env[0] != '\0') {
        int v = atoi(env);
        if (v > 0 && v < 3600) seconds = v;
    }

    g_rng = (unsigned)time(NULL) ^ 0xA5A5A5A5u;

    printf("\n");
    printf("  ╔══════════════════════════════════════════════════════════╗\n");
    printf("  ║           S P A C E   T R A D E R   2 0 0 0              ║\n");
    printf("  ║     galaxy sim · List/Map for-in · midopt stress test    ║\n");
    printf("  ╚══════════════════════════════════════════════════════════╝\n");
    printf("  worlds=%d  outposts=%d  ships=%d  duration=%ds\n",
           N_PLANETS, N_OUTPOSTS, N_SHIPS, seconds);
    printf("  set ST2000_SECONDS=N to change duration\n\n");
    fflush(stdout);

    auto planets = List<Planet>();
    auto outposts = List<Outpost>();
    auto fleet = List<Trader>();
    auto battles = List<Skirmish>();
    auto presence = Map<int, int>();

    SeedPlanets(&planets);
    SeedOutposts(&outposts, &planets);
    SeedTraders(&fleet);

    printf("  seeded: planets=%d outposts=%d ships=%d\n",
           planets.Count(), outposts.Count(), fleet.Count());
    fflush(stdout);

    clock_t t0 = clock();
    double limit = (double)seconds * (double)CLOCKS_PER_SEC;
    int tick = 0;
    int deaths = 0;
    int last_report = -1;
    long long peak_score = 0;

    while ((double)(clock() - t0) < limit) {
        tick++;

        /* Rotate work across the fleet each tick */
        int base = (tick * 17) % fleet.Count();
        int k;
        for (k = 0; k < 48; k++) {
            int i = (base + k * 5) % fleet.Count();
            DoTrade(&fleet, &planets, &outposts, i);
            DoTravel(&fleet, &planets, i);
        }

        /* A few skirmish rolls per tick (most fail the probability gate) */
        for (k = 0; k < 4; k++)
            DoBattle(&fleet, &planets, &battles, tick, &deaths);

        /* Frequent light repairs so traders persist through the full minute */
        if ((tick & 3) == 0) {
            for (k = 0; k < 24; k++) {
                int i = rnd(0, fleet.Count() - 1);
                Trader *t = fleet.GetMut(i);
                if (!t.IsAlive()) {
                    /* Rare salvage rebuild */
                    if (rnd(0, 200) == 0) {
                        t.Repair(30);
                        t.credits = 2000 + rnd(0, 3000);
                    }
                    continue;
                }
                Planet p = planets.Get(t.planet_id % planets.Count());
                if (p.wealth > 40 && t.hull < 95) {
                    t.Repair(rnd(1, 8));
                    if (t.credits > 30) t.credits -= 20;
                }
            }
        }

        long long score = ScoreFleet(&fleet);
        if (score > peak_score) peak_score = score;

        /* Progress every second of wall time */
        int sec_now = (int)((double)(clock() - t0) / (double)CLOCKS_PER_SEC);
        if (sec_now != last_report && sec_now > 0) {
            last_report = sec_now;
            RebuildPresence(&presence, &fleet);
            int hot = HottestPlanet(&presence);
            int alive = CountAlive(&fleet);
            int cors = CountCorsairsAlive(&fleet);
            int rich = CountRich(&fleet);
            String hot_name = "?";
            if (hot >= 0 && hot < planets.Count()) {
                Planet hp = planets.Get(hot);
                hot_name = hp.name;
            }
            printf("  t+%02ds  tick=%-7d  alive=%d  corsairs=%d  rich=%d  "
                   "deaths=%d  score=%lld  hot=%s  battles_logged=%d\n",
                   sec_now, tick, alive, cors, rich, deaths, score,
                   (char *)hot_name, battles.Count());
            fflush(stdout);
        }
    }

    double elapsed = (double)(clock() - t0) / (double)CLOCKS_PER_SEC;
    int alive = CountAlive(&fleet);
    int rich = CountRich(&fleet);

    printf("\n  ═══════════════ FINAL REPORT ═══════════════\n");
    printf("  wall_time=%.2fs  ticks=%d  ticks/sec=%.1f\n",
           elapsed, tick, elapsed > 0.01 ? (double)tick / elapsed : 0.0);
    printf("  (primary midopt metric: ticks/sec — wall is fixed by ST2000_SECONDS)\n");
    printf("  ships_alive=%d / %d   rich=%d   combat_deaths=%d\n",
           alive, fleet.Count(), rich, deaths);
    printf("  peak_economy_score=%lld  skirmishes_logged=%d\n",
           peak_score, battles.Count());

    /* Outpost stock remaining */
    long long stock = 0;
    for (auto op in outposts) stock += op.stock;
    printf("  outpost_stock_units=%lld\n", stock);

    PrintLeaders(&fleet, 8);

    /* Sample recent fatal skirmishes */
    int fatal_n = 0;
    for (auto s in battles)
        if (s.fatal) fatal_n++;
    printf("  fatal_skirmishes=%d / %d logged\n", fatal_n, battles.Count());

    printf("\n  SPACE TRADER 2000 — simulation complete.\n");
    printf("  Tip: compare midopt with\n");
    printf("    ST2000_SECONDS=%d ./bin/classyc -I include -fno-exceptions -O2 \\\n",
           seconds);
    printf("        examples/classy-space-trader-2000.cy -eg\n");
    printf("    ST2000_SECONDS=%d ./bin/classyc -I include -fno-exceptions -O2 -fno-midopt \\\n",
           seconds);
    printf("        examples/classy-space-trader-2000.cy -eg\n\n");
    fflush(stdout);
    return 0;
}
