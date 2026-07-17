/* spaceway3k.cy — SPACEWAY 3000
 *
 * ClassyC + NanoVG map of a live space war / trading galaxy.
 * Domain style matches classy-aurora-ops / classy-space-trader-2000:
 *
 *   Domain   · List<Planet>, List<Ship>, List<BattleFx> by value
 *            · GetMut / [] for in-place travel, trade, combat
 *            · enums + nameof · f-strings · quiet dtors
 *   Memory   · stack List/Map RAII — no owns()/new for the fleet
 *   LINQ     · Where / Sort / Find / GroupBy / Select on value shells
 *   Map      · presence board planet_id → ship count
 *   Graphics · NanoVG GL3 (libnanovg.bmir) · GLFW window · map HUD
 *
 * Build / run (from this directory):
 *   make            # JIT run interactive window
 *   make smoke      # headless N-frame smoke test
 *   make test       # compile-check + smoke
 *
 * From repo root:
 *   make -C example/spaceway3k
 *
 * Env:
 *   SPACEWAY3K_SMOKE=1     invisible window, run ~180 frames, exit 0
 *   SPACEWAY3K_PROFILE=1   uncapped headless, print per-section ms breakdown
 *   SPACEWAY3K_FRAMES=N    frame limit for smoke/profile (default 180/300)
 *   SPACEWAY3K_SEED=N      RNG seed (default: time)
 *   SPACEWAY3K_SPEED=N     sim steps per frame (default 1)
 *
 * Keys: ESC quit · SPACE pause · R reseed · [ ] speed · F faction filter
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <GLFW/glfw3.h>
#include "nanovg.h"
#define NANOVG_GL3_IMPLEMENTATION
#include "nanovg_gl.h"
#include "list.h"
#include "map.h"
#include "set.h"

/* ───────────────────────── Scale ───────────────────────── */

enum {
    N_PLANETS     = 18,
    N_SHIPS       = 1000,
    N_CARGO       = 8,
    WORLD_W       = 1000,
    WORLD_H       = 700,
    WIN_W         = 1280,
    WIN_H         = 800,
    MAX_FX        = 96,
    HUD_W         = 280,
    TARGET_FPS    = 30   /* interactive cap — keeps CPU calm */
};

/* ───────────────────────── Domain ───────────────────────── */

enum Faction { free_traders = 0, imperial = 1, corsair = 2, syndicate = 3 };
enum Goods   {
    ore = 0, spice = 1, tech = 2, weapons = 3,
    food = 4, fuel = 5, meds = 6, luxury = 7
};

/* Generic free fn — T inferred at call site (aurora / neon-grid style). */
T Max<T>(T a, T b) { return a > b ? a : b; }

class GalaxyConfig {
    String title;
    int    hot_danger;
    int    rich_credits;
};

class Planet {
    int     id;
    String  name;
    float   x, y;          /* world coords */
    int     wealth;        /* 1..100 */
    int     danger;        /* 1..100 */
    Faction faction;
    float   pulse;         /* animation phase */

    Planet(int id, String name, float x, float y,
           int wealth, int danger, Faction f) {
        this.id = id;
        this.name = name;
        this.x = x;
        this.y = y;
        this.wealth = wealth;
        this.danger = danger;
        this.faction = f;
        this.pulse = (float)(id * 0.37);
    }
    ~Planet() {}

    int Radius() {
        return 10 + wealth / 8;
    }
    int FactionKey() { return (int)faction; }
    int IsHot() { return danger >= 55; }
    int IsRich() { return wealth >= 70; }

    String ToString() {
        Faction f = faction;
        String fac = ((String)f.nameof()).upper();
        return f"#{id} {name} [{fac}] W{wealth} D{danger}";
    }
};

class Ship {
    int     id;
    String  name;
    Faction faction;
    int     planet_id;     /* docked / destination */
    int     from_id;       /* travel origin (-1 if docked) */
    float   travel;        /* 0..1 while jumping; <0 docked */
    float   x, y;          /* rendered world position */
    int     credits;
    int     hull;          /* 0..100 */
    int     cargo[N_CARGO];
    int     cargo_tons;
    int     kills;
    int     trades;
    int     alive;
	double  death_time;    // time of death
    float   angle;         /* heading for glyph */

    Ship(int id, String name, Faction f, int planet_id, int credits) {
        this.id = id;
        this.name = name;
        this.faction = f;
        this.planet_id = planet_id;
        this.from_id = -1;
        this.travel = -1.0f;
        this.x = 0;
        this.y = 0;
        this.credits = credits;
        this.hull = 100;
        this.cargo_tons = 0;
        this.kills = 0;
        this.trades = 0;
        this.alive = 1;
        this.angle = 0;
		this.death_time = 0;
        int i;
        for (i = 0; i < N_CARGO; i++) this.cargo[i] = 0;
    }
    ~Ship() {}

    int IsAlive()   { return alive && hull > 0; }
    int IsCorsair() { return faction == corsair; }
    int IsRich()    { return credits >= 14000; }
    int IsTraveling() { return travel >= 0.0f; }
    int FactionKey() { return (int)faction; }

    void Load(int good, int qty) {
        int g = good % N_CARGO;
        if (qty <= 0) return;
        cargo[g] += qty;
        cargo_tons += qty;
    }
    int Unload(int good, int qty) {
        int g = good % N_CARGO;
        if (qty <= 0 || cargo[g] < qty) return 0;
        cargo[g] -= qty;
        cargo_tons -= qty;
        return 1;
    }
    void Damage(double frame_start, int d) {
        hull -= d;
		//printf(f"Damage! {name} took {d} damage!");
        if (hull <= 0)
			 { hull = 0; alive = 0; death_time=frame_start; /*printf(" AND DIED!\n");*/ }
		//else
		//	printf("\n");
    }
    void Repair(int r) {
        hull += r;
        if (hull > 100) hull = 100;
        if (hull > 0) { /*printf(f"RESURRECTED {name}\n");*/ alive = 1; death_time = 0; }
    }

    String ToString() {
        Faction f = faction;
        String fac = ((String)f.nameof()).upper();
        return f"#{id} {name} [{fac}] cr={credits} hull={hull}";
    }
};

/* Transient combat / trade flash — by-value POD-ish DTO */
class BattleFx {
    float x0, y0, x1, y1;
    float life;            /* 1 → 0 */
    int   fatal;
    int   kind;            /* 0 combat laser, 1 trade spark */

    BattleFx(float x0, float y0, float x1, float y1,
             float life, int fatal, int kind) {
        this.x0 = x0; this.y0 = y0;
        this.x1 = x1; this.y1 = y1;
        this.life = life;
        this.fatal = fatal;
        this.kind = kind;
    }
    ~BattleFx() {}

    int Alive() { return life > 0.02f; }
};

/* ───────────────────────── Profiling ───────────────────────── */

enum {
    PROF_SIM = 0,
    PROF_PRESENCE,
    PROF_STARS,
    PROF_LANES,
    PROF_PLANETS,
    PROF_SHIPS,
    PROF_FX,
    PROF_SCORE,
    PROF_HUD,
    PROF_NVG_END,   /* nvgEndFrame + GPU submit */
    PROF_SWAP,      /* swap + poll */
    PROF_N
};

static const char *prof_names[PROF_N] = {
    "TickSim",
    "RebuildPresence",
    "DrawStars",
    "DrawLanes",
    "DrawPlanets",
    "DrawShips",
    "DrawFx",
    "EconomyScore",
    "DrawHud",
    "nvgEndFrame",
    "swap+poll"
};

static int    g_profile = 1;
static double g_prof_ms[PROF_N];
static int    g_prof_frames = 0;

static double prof_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

/* ───────────────────────── Globals / input ───────────────────────── */

static int g_paused = 0;
static int g_quit   = 0;
static int g_reseed = 0;
static int g_speed  = 1;
static int g_filter = -1;   /* -1 all, else Faction */

static void on_key(GLFWwindow *win, int key, int scancode, int action, int mods) {
    (void)scancode; (void)mods;
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;
    if (key == GLFW_KEY_ESCAPE) {
        g_quit = 1;
        glfwSetWindowShouldClose(win, GL_TRUE);
    } else if (key == GLFW_KEY_SPACE) {
        g_paused = !g_paused;
    } else if (key == GLFW_KEY_R) {
        g_reseed = 1;
    } else if (key == GLFW_KEY_LEFT_BRACKET) {
        if (g_speed > 1) g_speed--;
    } else if (key == GLFW_KEY_RIGHT_BRACKET) {
        if (g_speed < 8) g_speed++;
    } else if (key == GLFW_KEY_F) {
        g_filter = (g_filter + 2) % 5 - 1; /* -1,0,1,2,3 */
    }
}

/* ───────────────────────── RNG ───────────────────────── */

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
float rndf(float lo, float hi) {
    return lo + (hi - lo) * ((float)(rnd_u() & 0xFFFF) / 65535.0f);
}

/* ───────────────────────── Names ───────────────────────── */

const char *planet_names[] = {
    "Kepler-Prime", "New Ceres", "Rigel Station", "Helios IV", "Dusthaven",
    "Cryonia", "Vega Reach", "Orion's End", "Nova Mutual", "Ashfall",
    "Port Meridian", "Lumen Bay", "Sable Drift", "Echo Reef", "Ironwell",
    "Solstice", "Nimbus Gate", "Polaris Freehold"
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

/* ───────────────────────── Faction colors (NanoVG) ───────────────────────── */

NVGcolor FactionColor(Faction f, int alpha) {
    if (f == free_traders) return nvgRGBA( 90, 200, 255, alpha); /* cyan */
    if (f == imperial)     return nvgRGBA(255, 210,  70, alpha); /* gold */
    if (f == corsair)      return nvgRGBA(255,  80,  90, alpha); /* crimson */
    return nvgRGBA(180, 120, 255, alpha);                         /* violet */
}

NVGcolor FactionGlow(Faction f, int alpha) {
    if (f == free_traders) return nvgRGBA( 40, 120, 180, alpha);
    if (f == imperial)     return nvgRGBA(160, 120,  20, alpha);
    if (f == corsair)      return nvgRGBA(140,  30,  40, alpha);
    return nvgRGBA( 90,  50, 150, alpha);
}

/* ───────────────────────── Seeding ───────────────────────── */

void SeedPlanets(List<Planet> *world) {
    int i;
    /* Spread planets with a soft grid + jitter so the map stays readable */
    int cols = 6;
    int rows = 3;
    float margin_x = 80.0f;
    float margin_y = 80.0f;
    float cell_w = (WORLD_W - 2 * margin_x) / (float)(cols - 1);
    float cell_h = (WORLD_H - 2 * margin_y) / (float)(rows - 1);

    for (i = 0; i < N_PLANETS; i++) {
        int col = i % cols;
        int row = i / cols;
        float x = margin_x + col * cell_w + rndf(-28.0f, 28.0f);
        float y = margin_y + row * cell_h + rndf(-35.0f, 35.0f);
        if (x < 40) x = 40;
        if (y < 40) y = 40;
        if (x > WORLD_W - 40) x = WORLD_W - 40;
        if (y > WORLD_H - 40) y = WORLD_H - 40;
        Faction f = (Faction)(i % 4);
        int wealth = rnd(20, 95);
        int danger = rnd(8, 92);
        if (f == corsair) danger = Max(danger, 55);
        if (f == imperial) wealth = Max(wealth, 50);
        world.Add(Planet(i, planet_names[i % 18], x, y, wealth, danger, f));
    }
}

void SeedShips(List<Ship> *fleet, List<Planet> *world) {
    int i;
    int npl = world.Count();
    for (i = 0; i < N_SHIPS; i++) {
        Faction f = (Faction)(i % 4);
        int start = i % npl;
        int cash = 6000 + rnd(0, 14000);
        if (f == corsair) cash = 2500 + rnd(0, 5000);
        if (f == imperial) cash = 12000 + rnd(0, 10000);
        fleet.Add(Ship(i, MakeShipName(i), f, start, cash));
        Ship *s = fleet.GetMut(i);
        Planet p = world.Get(start);
        /* slight orbital offset so ships aren't stacked on the planet core */
        float a = rndf(0, 6.28318f);
        float r = 14.0f + rndf(0, 18.0f);
        s.x = p.x + cosf(a) * r;
        s.y = p.y + sinf(a) * r;
        s.angle = a;
    }
}

/* ───────────────────────── Economy / combat ───────────────────────── */

int BasePrice(int good) {
    int table[N_CARGO] = { 12, 28, 45, 60, 8, 15, 35, 90 };
    return table[good % N_CARGO];
}

int QuoteBuy(Planet p, int good) {
    int bias = (good == luxury) ? p.wealth / 5 : p.danger / 10;
    return BasePrice(good) + bias + rnd(0, 5);
}
int QuoteSell(Planet p, int good) {
    int bias = (good == food || good == ore) ? (100 - p.wealth) / 6 : p.wealth / 8;
    int q = BasePrice(good) + bias - rnd(0, 4);
    return q < 2 ? 2 : q;
}

void SnapShipToPlanet(Ship *s, List<Planet> *world) {
    Planet p = world.Get(s.planet_id % world.Count());
    float a = s.angle;
    float r = 14.0f + (float)(s.id % 7) * 2.5f;
    s.x = p.x + cosf(a) * r;
    s.y = p.y + sinf(a) * r;
}

void UpdateTravel(Ship *s, List<Planet> *world, float dt) {
    if (!s.IsAlive()) return;
    if (!s.IsTraveling()) {
        /* gentle orbit while docked */
        s.angle += dt * 0.35f;
        SnapShipToPlanet(s, world);
        return;
    }
    Planet from = world.Get(s.from_id % world.Count());
    Planet to   = world.Get(s.planet_id % world.Count());
    s.travel += dt * (0.22f + (float)(s.id % 5) * 0.02f);
    if (s.travel >= 1.0f) {
        s.travel = -1.0f;
        s.from_id = -1;
        s.angle = rndf(0, 6.28318f);
        SnapShipToPlanet(s, world);
        return;
    }
    float t = s.travel;
    /* ease-in-out */
    float e = t * t * (3.0f - 2.0f * t);
    s.x = from.x + (to.x - from.x) * e;
    s.y = from.y + (to.y - from.y) * e;
    s.angle = atan2f(to.y - from.y, to.x - from.x);
}

void DoTrade(List<Ship> *fleet, List<Planet> *world, List<BattleFx> *fx, int i) {
    if (i < 0 || i >= fleet.Count()) return;
    Ship *t = fleet.GetMut(i);
    if (!t.IsAlive() || t.IsTraveling()) return;

    Planet p = world.Get(t.planet_id % world.Count());
    int good = rnd(0, N_CARGO - 1);
    int qty  = rnd(1, 6);
    int buy  = QuoteBuy(p, good);
    int sell = QuoteSell(p, good);

    if (t.cargo[good] > 0 && rnd(0, 1) == 0) {
        int q = t.cargo[good] < qty ? t.cargo[good] : qty;
        if (t.Unload(good, q)) {
            t.credits += (sell + 3) * q;
            t.trades++;
            if (fx.Count() < MAX_FX)
                fx.Add(BattleFx(t.x, t.y, p.x, p.y, 1.0f, 0, 1));
        }
        return;
    }

    int cost = buy * qty;
    if (t.credits >= cost) {
        t.credits -= cost;
        t.Load(good, qty);
        t.trades++;
        if (fx.Count() < MAX_FX)
            fx.Add(BattleFx(p.x, p.y, t.x, t.y, 0.7f, 0, 1));
    } else if (t.credits < 400) {
        t.credits += rnd(40, 180);
    }
}

void DoTravel(List<Ship> *fleet, List<Planet> *world, int i) {
    if (i < 0 || i >= fleet.Count()) return;
    Ship *t = fleet.GetMut(i);
    if (!t.IsAlive() || t.IsTraveling()) return;
    /* Stay docked often enough for co-located battles to fire */
    if (rnd(0, 7) != 0) return;
    int dest = rnd(0, world.Count() - 1);
    if (dest == t.planet_id) return;
    t.from_id = t.planet_id;
    t.planet_id = dest;
    t.travel = 0.0f;
    int burn = rnd(3, 20);
    if (t.credits > burn) t.credits -= burn;
}

/* Find a living docked ship index, or -1. */
int PickDockedShip(List<Ship> *fleet) {
    int tries;
    for (tries = 0; tries < 16; tries++) {
        int i = rnd(0, fleet.Count() - 1);
        Ship s = fleet.Get(i);
        if (s.IsAlive() && !s.IsTraveling()) return i;
    }
    return -1;
}

/* Find a rival (or rare same-faction) target docked at the same world. */
int FindTargetAtPlanet(List<Ship> *fleet, int aggressor, int planet_id) {
    int n = fleet.Count();
    int rival = -1;
    int any   = -1;
    int start = rnd(0, n - 1);
    int k;
    for (k = 0; k < n; k++) {
        int i = (start + k) % n;
        if (i == aggressor) continue;
        Ship s = fleet.Get(i);
        if (!s.IsAlive() || s.IsTraveling()) continue;
        if (s.planet_id != planet_id) continue;
        Ship ag = fleet.Get(aggressor);
        if (s.faction != ag.faction) {
            rival = i;
            /* corsairs / high tension: take first rival quickly */
            if (ag.IsCorsair() || s.IsCorsair() || rnd(0, 2) == 0)
                return rival;
        } else if (any < 0) {
            any = i;
        }
    }
    if (rival >= 0) return rival;
    /* rare same-faction scuffle (piracy / mutiny) */
    if (any >= 0 && rnd(0, 24) == 0) return any;
    return -1;
}

void DoBattle(List<Ship> *fleet, List<Planet> *world, List<BattleFx> *fx,
              int *deaths, int *battles, double frame_start) {
    int a = PickDockedShip(fleet);
    if (a < 0) return;
    Ship *ta = fleet.GetMut(a);
    int b = FindTargetAtPlanet(fleet, a, ta.planet_id);
    if (b < 0) return;
    Ship *tb = fleet.GetMut(b);

    Planet p = world.Get(ta.planet_id % world.Count());

    /* Engagement chance — co-located rivals fight often, especially on hot worlds */
    int chance = 40 + p.danger / 2;          /* ~40–90 */
    if (ta.IsCorsair() || tb.IsCorsair()) chance += 20;
    if (ta.faction == tb.faction) chance = 8;
    if (p.IsHot()) chance += 15;
    if (chance > 95) chance = 95;
    if (rnd(0, 99) >= chance) return;

    /* Hard hits so kills land within a few exchanges */
    int da = rnd(18, 42);
    int db = rnd(18, 42);
    if (ta.faction == imperial) da += 8;
    if (tb.faction == imperial) db += 8;
    if (ta.IsCorsair()) da += 6;
    if (tb.IsCorsair()) db += 6;
    /* wounded ships hit softer */
    if (ta.hull < 40) da = da * 2 / 3;
    if (tb.hull < 40) db = db * 2 / 3;

    ta.Damage(frame_start, db);
    tb.Damage(frame_start, da);
    (*battles)++;

    int fatal = 0;
    if (!ta.IsAlive()) { fatal = 1; tb.kills++; (*deaths)++; }
    if (!tb.IsAlive()) { fatal = 1; ta.kills++; (*deaths)++; }

    /* Always paint the laser — longer life on kills */
    float life = fatal ? 1.6f : 1.1f;
    if (fx.Count() < MAX_FX)
        fx.Add(BattleFx(ta.x, ta.y, tb.x, tb.y, life, fatal, 0));
    /* Extra nova on kill so deaths read on the map */
    if (fatal && fx.Count() < MAX_FX) {
        float mx = (ta.x + tb.x) * 0.5f;
        float my = (ta.y + tb.y) * 0.5f;
        fx.Add(BattleFx(mx, my, mx + 1.0f, my + 1.0f, 1.8f, 1, 0));
    }
}

void TickSim(List<Ship> *fleet, List<Planet> *world, List<BattleFx> *fx,
             int *deaths, int *battles,double frame_start, float dt) {
    int n = fleet.Count();
    int base = rnd(0, n - 1);
    int k;
    for (k = 0; k < 20; k++) {
        int i = (base + k * 3) % n;
        DoTrade(fleet, world, fx, i);
        DoTravel(fleet, world, i);
    }
    /* Several engagement rolls per tick — targets are co-located rivals */
    for (k = 0; k < 8; k++)
        DoBattle(fleet, world, fx, deaths, battles, frame_start);

    /* Light repairs only — don't undo combat every tick */
    for (k = 0; k < 4; k++) {
        int i = rnd(0, n - 1);
        Ship *t = fleet.GetMut(i);
        if (!t.IsAlive()) {
            /* occasional salvage rebuild so the map stays populated */
            if (rnd(0, 180) == 0) {
				//printf(f"Salvage {t.name}\n");
                t.Repair(55);
                t.credits = 1500 + rnd(0, 2500);
                t.kills = 0;
            }
            continue;
        }
        /* only patch up when not critically damaged */
        if (t.hull < 85 && t.hull > 35 && !t.IsTraveling()) {
            Planet p = world.Get(t.planet_id % world.Count());
            if (p.wealth > 50 && rnd(0, 2) == 0) {
                t.Repair(rnd(1, 4));
                if (t.credits > 25) t.credits -= 15;
            }
        }
    }

    /* animate ships + planets + FX */
    for (k = 0; k < n; k++) {
        Ship *s = fleet.GetMut(k);
        UpdateTravel(s, world, dt);
    }
    for (k = 0; k < world.Count(); k++) {
        Planet *p = world.GetMut(k);
        p.pulse += dt * 1.6f;
    }
    /* decay FX — rebuild live list (by-value) */
    if (fx.Count() > 0) {
        auto live = List<BattleFx>();
        for (auto e in fx) {
            e.life -= dt * 0.85f;  /* linger so lasers/kills are readable */
            if (e.Alive()) live.Add(e);
        }
        fx.Clear();
        for (auto e in live) fx.Add(e);
    }
}

/* ───────────────────────── Queries ───────────────────────── */

int CountAlive(List<Ship> *fleet) {
    int n = 0;
    for (auto s in fleet)
        if (s.IsAlive()) n++;
    return n;
}

long long EconomyScore(List<Ship> *fleet) {
    long long score = 0;
    for (auto s in fleet) {
        if (!s.IsAlive()) continue;
        score += s.credits;
        score += s.cargo_tons * 12;
        score += s.kills * 150;
    }
    return score;
}

void RebuildPresence(Map<int, int> *presence, List<Ship> *fleet) {
    presence.Clear();
    for (auto s in fleet) {
        if (!s.IsAlive() || s.IsTraveling()) continue;
        int pid = s.planet_id;
        if (presence.Contains(pid))
            presence[pid] = presence[pid] + 1;
        else
            presence[pid] = 1;
    }
}

/* ───────────────────────── Drawing ───────────────────────── */

void WorldToScreen(float wx, float wy, float map_x, float map_y,
                   float map_w, float map_h, float *sx, float *sy) {
    *sx = map_x + (wx / (float)WORLD_W) * map_w;
    *sy = map_y + (wy / (float)WORLD_H) * map_h;
}

void DrawStars(NVGcontext *vg, float x, float y, float w, float h, float t) {
    nvgSave(vg);
    nvgScissor(vg, x, y, w, h);
    /* deep space */
    NVGpaint bg = nvgLinearGradient(vg, x, y, x, y + h,
                                    nvgRGBA(6, 10, 28, 255),
                                    nvgRGBA(12, 8, 32, 255));
    nvgBeginPath(vg);
    nvgRect(vg, x, y, w, h);
    nvgFillPaint(vg, bg);
    nvgFill(vg);

    int i;
    for (i = 0; i < 220; i++) {
        /* deterministic pseudo-random star field */
        unsigned u = (unsigned)(i * 2654435761u);
        float sx = x + (float)(u % 997) / 997.0f * w;
        float sy = y + (float)((u >> 10) % 991) / 991.0f * h;
        float tw = 0.55f + 0.45f * sinf(t * 1.7f + (float)i * 0.31f);
        int a = (int)(40 + 160 * tw);
        float r = 0.6f + (float)(u % 3) * 0.45f;
        nvgBeginPath(vg);
        //nvgCircle(vg, sx, sy, r);
        nvgRect(vg, sx, sy, 3,3);
        nvgFillColor(vg, nvgRGBA(220, 230, 255, a));
        nvgFill(vg);
    }

    /* faint sector grid */
    nvgStrokeColor(vg, nvgRGBA(40, 70, 120, 28));
    nvgStrokeWidth(vg, 1.0f);
    int g;
    for (g = 1; g < 8; g++) {
        float gx = x + w * (float)g / 8.0f;
        float gy = y + h * (float)g / 8.0f;
        nvgBeginPath(vg);
        nvgMoveTo(vg, gx, y);
        nvgLineTo(vg, gx, y + h);
        nvgStroke(vg);
        nvgBeginPath(vg);
        nvgMoveTo(vg, x, gy);
        nvgLineTo(vg, x + w, gy);
        nvgStroke(vg);
    }
    nvgRestore(vg);
}

void DrawPlanet(NVGcontext *vg, Planet p, float sx, float sy, float scale,
                int ship_count, int font) {
    float r = (float)p.Radius() * scale;
    float pulse = 1.0f + 0.06f * sinf(p.pulse);
    r *= pulse;

    /* outer danger ring */
    if (p.IsHot()) {
        nvgBeginPath(vg);
        nvgCircle(vg, sx, sy, r + 6.0f);
        nvgStrokeColor(vg, nvgRGBA(255, 90, 70, 70 + (int)(40 * sinf(p.pulse * 2))));
        nvgStrokeWidth(vg, 2.0f);
        nvgStroke(vg);
    }

    /* atmosphere glow */
    NVGpaint glow = nvgRadialGradient(vg, sx, sy, r * 0.2f, r * 2.2f,
                                      FactionGlow(p.faction, 110),
                                      nvgRGBA(0, 0, 0, 0));
    nvgBeginPath(vg);
    nvgCircle(vg, sx, sy, r * 2.2f);
    nvgFillPaint(vg, glow);
    nvgFill(vg);

    /* body */
    NVGpaint body = nvgRadialGradient(vg, sx - r * 0.3f, sy - r * 0.3f,
                                      r * 0.1f, r,
                                      nvgRGBA(240, 245, 255, 230),
                                      FactionColor(p.faction, 255));
    nvgBeginPath(vg);
    nvgCircle(vg, sx, sy, r);
    nvgFillPaint(vg, body);
    nvgFill(vg);

    /* faction rim */
    nvgBeginPath(vg);
    nvgCircle(vg, sx, sy, r);
    nvgStrokeColor(vg, FactionColor(p.faction, 200));
    nvgStrokeWidth(vg, 1.5f);
    nvgStroke(vg);

    /* label */
    nvgFontSize(vg, 11.0f);
    nvgFontFaceId(vg, font);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
    nvgFillColor(vg, nvgRGBA(210, 220, 240, 200));
    nvgText(vg, sx, sy + r + 3, (const char *)p.name, NULL);

    if (ship_count > 0) {
        nvgFontSize(vg, 10.0f);
        nvgFillColor(vg, nvgRGBA(160, 255, 200, 180));
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", ship_count);
        nvgText(vg, sx, sy + r + 15, buf, NULL);
    }
}

void DrawShip(NVGcontext *vg, Ship s, float sx, float sy, float scale, double frame_start) {
    float sz = 5.5f * scale;

	//printf(f"{s.name} {s.alive} calc { frame_start - s.death_time}\n");

    /* wreck — grey X so kills stay visible on the map */
    if (!s.IsAlive() && (frame_start-s.death_time < 5.0)) {
		//printf(f"DEAD {s.name} {s.alive} calc {frame_start} {s.death_time} - { frame_start - s.death_time}\n");
		int alpha= (int)( 255.0 * (5-frame_start-s.death_time / 5));
        nvgSave(vg);
        nvgTranslate(vg, sx, sy);
        nvgStrokeColor(vg, nvgRGBA(180, 90, 90, alpha/2));
        nvgStrokeWidth(vg, 1.8f * scale);
        nvgBeginPath(vg);
        nvgMoveTo(vg, -sz, -sz);
        nvgLineTo(vg,  sz,  sz);
        nvgMoveTo(vg,  sz, -sz);
        nvgLineTo(vg, -sz,  sz);
        nvgStroke(vg);
        nvgBeginPath(vg);
        nvgCircle(vg, 0, 0, sz * 0.9f);
        nvgStrokeColor(vg, nvgRGBA(120, 40, 40, alpha/3));
        nvgStrokeWidth(vg, 1.0f);
        nvgStroke(vg);
        nvgRestore(vg);
        return;
    }
    if (!s.IsAlive())
		return;

    if (s.IsTraveling()) sz *= 0.9f;

    nvgSave(vg);
    nvgTranslate(vg, sx, sy);
    nvgRotate(vg, s.angle);

    /* trail while traveling */
    if (s.IsTraveling()) {
        nvgBeginPath(vg);
        nvgMoveTo(vg, -sz * 2.8f, 0);
        nvgLineTo(vg, -sz * 0.5f, 0);
        nvgStrokeColor(vg, FactionColor(s.faction, 100));
        nvgStrokeWidth(vg, 1.5f);
        nvgStroke(vg);
    }

    /* chevron / wedge */
    nvgBeginPath(vg);
    nvgMoveTo(vg,  sz * 1.4f,  0);
    nvgLineTo(vg, -sz * 0.9f,  sz * 0.75f);
    nvgLineTo(vg, -sz * 0.4f,  0);
    nvgLineTo(vg, -sz * 0.9f, -sz * 0.75f);
    nvgClosePath(vg);
    nvgFillColor(vg, FactionColor(s.faction, s.IsTraveling() ? 200 : 240));
    nvgFill(vg);
    nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 80));
    nvgStrokeWidth(vg, 1.0f);
    nvgStroke(vg);

    /* hull damage tint — bright red when critically wounded */
    if (s.hull < 50) {
        int a = s.hull < 25 ? 220 : 140;
        nvgBeginPath(vg);
        nvgCircle(vg, 0, 0, sz * 0.4f);
        nvgFillColor(vg, nvgRGBA(255, 50, 40, a));
        nvgFill(vg);
    }
    nvgRestore(vg);
}

void DrawFx(NVGcontext *vg, BattleFx e, float map_x, float map_y,
            float map_w, float map_h) {
    float sx0, sy0, sx1, sy1;
    WorldToScreen(e.x0, e.y0, map_x, map_y, map_w, map_h, &sx0, &sy0);
    WorldToScreen(e.x1, e.y1, map_x, map_y, map_w, map_h, &sx1, &sy1);
    int a = (int)(220 * e.life);
    if (a < 0) a = 0;

    if (e.kind == 0) {
        /* combat laser — thick red on kill, orange skirmish otherwise */
        nvgBeginPath(vg);
        nvgMoveTo(vg, sx0, sy0);
        nvgLineTo(vg, sx1, sy1);
        nvgStrokeColor(vg, e.fatal ? nvgRGBA(255, 50, 40, a)
                                   : nvgRGBA(255, 200, 70, a));
        nvgStrokeWidth(vg, e.fatal ? 3.4f : 2.0f);
        nvgStroke(vg);

        /* secondary glow pass */
        nvgBeginPath(vg);
        nvgMoveTo(vg, sx0, sy0);
        nvgLineTo(vg, sx1, sy1);
        nvgStrokeColor(vg, e.fatal ? nvgRGBA(255, 120, 80, a / 2)
                                   : nvgRGBA(255, 240, 160, a / 3));
        nvgStrokeWidth(vg, e.fatal ? 7.0f : 4.0f);
        nvgStroke(vg);

        float mx = (sx0 + sx1) * 0.5f;
        float my = (sy0 + sy1) * 0.5f;
        float burst = (e.fatal ? 12.0f : 6.0f) + 22.0f * (1.0f - e.life);
        nvgBeginPath(vg);
        nvgCircle(vg, mx, my, burst);
        nvgStrokeColor(vg, nvgRGBA(255, 140, 40, a / 2));
        nvgStrokeWidth(vg, 2.0f);
        nvgStroke(vg);
        if (e.fatal) {
            nvgBeginPath(vg);
            nvgCircle(vg, mx, my, burst * 0.45f);
            nvgFillColor(vg, nvgRGBA(255, 200, 80, a / 3));
            nvgFill(vg);
        }
    } else {
        /* trade spark */
        nvgBeginPath(vg);
        nvgCircle(vg, sx0, sy0, 3.0f + 4.0f * (1.0f - e.life));
        nvgFillColor(vg, nvgRGBA(120, 255, 180, a));
        nvgFill(vg);
    }
}

void DrawHud(NVGcontext *vg, int font, int bold,
             List<Planet> *world, List<Ship> *fleet, List<BattleFx> *fx,
             Map<int, int> *presence, int tick, int deaths, int battles,
             int paused, int speed, int filter, float fps, long long score) {
    float hx = (float)(WIN_W - HUD_W);
    float hy = 0;
    float hw = (float)HUD_W;
    float hh = (float)WIN_H;

    nvgBeginPath(vg);
    nvgRect(vg, hx, hy, hw, hh);
    nvgFillColor(vg, nvgRGBA(10, 14, 28, 230));
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgMoveTo(vg, hx, hy);
    nvgLineTo(vg, hx, hy + hh);
    nvgStrokeColor(vg, nvgRGBA(60, 100, 180, 120));
    nvgStrokeWidth(vg, 1.5f);
    nvgStroke(vg);

    float y = 28;
    nvgFontFaceId(vg, bold);
    nvgFontSize(vg, 20.0f);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgFillColor(vg, nvgRGBA(120, 210, 255, 255));
    nvgText(vg, hx + 18, y, "SPACEWAY 3000", NULL);
    y += 26;
    nvgFontFaceId(vg, font);
    nvgFontSize(vg, 12.0f);
    nvgFillColor(vg, nvgRGBA(140, 160, 190, 220));
    nvgText(vg, hx + 18, y, "war · trade · classy map", NULL);
    y += 28;

    /* CountWhere: non-allocating scans (no intermediate List shells per frame). */
    int alive  = fleet.CountWhere((Ship s) => s.IsAlive());
    int cors   = fleet.CountWhere((Ship s) => s.IsAlive() && s.IsCorsair());
    int rich   = fleet.CountWhere((Ship s) => s.IsAlive() && s.IsRich());
    int travel = fleet.CountWhere((Ship s) => s.IsAlive() && s.IsTraveling());

    char line[128];
    nvgFontSize(vg, 13.0f);
    nvgFillColor(vg, nvgRGBA(200, 220, 240, 240));

#define HUD_LINE(fmt, ...) do { \
        snprintf(line, sizeof(line), fmt, __VA_ARGS__); \
        nvgText(vg, hx + 18, y, line, NULL); y += 18; \
    } while (0)

    HUD_LINE("tick        %d", tick);
    HUD_LINE("fps         %.0f", fps);
    HUD_LINE("speed       %dx %s", speed, paused ? "[PAUSED]" : "");
    y += 6;
    nvgFillColor(vg, nvgRGBA(100, 220, 160, 240));
    HUD_LINE("ships alive %d / %d", alive, fleet.Count());
    HUD_LINE("corsairs    %d", cors);
    HUD_LINE("rich        %d", rich);
    HUD_LINE("in transit  %d", travel);
    nvgFillColor(vg, nvgRGBA(255, 120, 100, 240));
    HUD_LINE("battles     %d", battles);
    HUD_LINE("deaths      %d", deaths);
    nvgFillColor(vg, nvgRGBA(100, 220, 160, 240));
    HUD_LINE("economy     %lld", score);
    HUD_LINE("fx active   %d", fx.Count());
    y += 10;

    nvgFillColor(vg, nvgRGBA(160, 180, 220, 220));
    nvgFontFaceId(vg, bold);
    nvgFontSize(vg, 13.0f);
    nvgText(vg, hx + 18, y, "FACTIONS", NULL);
    y += 20;
    nvgFontFaceId(vg, font);
    nvgFontSize(vg, 12.0f);

    int fi;
    for (fi = 0; fi < 4; fi++) {
        Faction f = (Faction)fi;
        int wing_n = fleet.CountWhere((Ship s) => s.IsAlive() && s.FactionKey() == fi);
        NVGcolor c = FactionColor(f, 255);
        nvgBeginPath(vg);
        nvgCircle(vg, hx + 26, y + 6, 5);
        nvgFillColor(vg, c);
        nvgFill(vg);
        nvgFillColor(vg, filter == fi ? nvgRGBA(255, 255, 200, 255)
                                      : nvgRGBA(190, 200, 220, 230));
        String fac = ((String)f.nameof()).upper();
        snprintf(line, sizeof(line), "%-14s %d", (const char *)fac, wing_n);
        nvgText(vg, hx + 40, y, line, NULL);
        y += 18;
    }

    y += 12;
    nvgFontFaceId(vg, bold);
    nvgFillColor(vg, nvgRGBA(160, 180, 220, 220));
    nvgText(vg, hx + 18, y, "HOTTEST WORLDS", NULL);
    y += 20;
    nvgFontFaceId(vg, font);

    /* top presence planets */
    auto ranked = List<Planet>();
    for (auto p in world) ranked.Add(p);
    /* Sort by presence then danger via manual selection of top 5 */
    int shown = 0;
    int guard = 0;
    while (shown < 5 && guard < 40) {
        guard++;
        int best_i = -1;
        int best_n = -1;
        int i;
        for (i = 0; i < ranked.Count(); i++) {
            Planet p = ranked.Get(i);
            int n = presence.Contains(p.id) ? presence[p.id] : 0;
            int score_p = n * 100 + p.danger;
            if (score_p > best_n) { best_n = score_p; best_i = i; }
        }
        if (best_i < 0) break;
        Planet hp = ranked.Get(best_i);
        int n = presence.Contains(hp.id) ? presence[hp.id] : 0;
        nvgFillColor(vg, FactionColor(hp.faction, 230));
        snprintf(line, sizeof(line), "%-14s %2d ships",
                 (const char *)hp.name, n);
        nvgText(vg, hx + 18, y, line, NULL);
        y += 16;
        /* zero-out so next pass picks another */
        ranked.GetMut(best_i).danger = -1;
        if (presence.Contains(hp.id)) presence[hp.id] = -1;
        shown++;
    }
    /* restore presence after ranking display */
    RebuildPresence(presence, fleet);

    y += 16;
    nvgFontFaceId(vg, bold);
    nvgFillColor(vg, nvgRGBA(160, 180, 220, 220));
    nvgText(vg, hx + 18, y, "TOP TRADERS", NULL);
    y += 20;
    nvgFontFaceId(vg, font);

    auto living = fleet.Where((Ship s) => s.IsAlive());
    living.Sort((Ship a, Ship b) => b.credits - a.credits);
    int topn = living.Count() < 6 ? living.Count() : 6;
    int ti;
    for (ti = 0; ti < topn; ti++) {
        Ship s = living.Get(ti);
        nvgFillColor(vg, FactionColor(s.faction, 230));
        snprintf(line, sizeof(line), "%-16s %6d",
                 (const char *)s.name, s.credits);
        /* clip long names */
        nvgText(vg, hx + 18, y, line, NULL);
        y += 16;
    }

    y = (float)WIN_H - 110;
    nvgFontFaceId(vg, font);
    nvgFontSize(vg, 11.0f);
    nvgFillColor(vg, nvgRGBA(120, 140, 170, 200));
    nvgText(vg, hx + 18, y, "ESC quit · SPACE pause", NULL); y += 15;
    nvgText(vg, hx + 18, y, "R reseed · [ ] speed", NULL); y += 15;
    nvgText(vg, hx + 18, y, "F faction filter", NULL); y += 18;
    nvgFillColor(vg, nvgRGBA(90, 110, 140, 180));
    nvgText(vg, hx + 18, y, "ClassyC · NanoVG · MIR", NULL);

#undef HUD_LINE
}

void DrawTitle(NVGcontext *vg, int bold, float t) {
    nvgFontFaceId(vg, bold);
    nvgFontSize(vg, 16.0f);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    float pulse = 0.75f + 0.25f * sinf(t * 2.0f);
    nvgFillColor(vg, nvgRGBA(100, 200, 255, (int)(200 * pulse)));
    nvgText(vg, 18, 14, "GALAXY MAP", NULL);
    nvgFontSize(vg, 12.0f);
    nvgFillColor(vg, nvgRGBA(140, 160, 190, 180));
    nvgText(vg, 18, 34, "live sector traffic · battles · trade routes", NULL);
}

/* ───────────────────────── World lifecycle ───────────────────────── */

void BuildWorld(List<Planet> *world, List<Ship> *fleet, List<BattleFx> *fx) {
    world.Clear();
    fleet.Clear();
    fx.Clear();
    SeedPlanets(world);
    SeedShips(fleet, world);
}

/* ───────────────────────── main ───────────────────────── */

int main(void) {
    int smoke = 0;
    int frame_limit = 0;
    char *env = getenv("SPACEWAY3K_SMOKE");
    if (env && env[0] == '1') smoke = 1;

    env = getenv("SPACEWAY3K_PROFILE");
    if (env && env[0] == '1') {
        g_profile = 1;
        smoke = 1; /* headless + uncapped for clean timings */
    }

    env = getenv("SPACEWAY3K_FRAMES");
    if (env && env[0]) {
        int v = atoi(env);
        if (v > 0 && v < 100000) frame_limit = v;
    }
    if (frame_limit == 0)
        frame_limit = g_profile ? 300 : (smoke ? 180 : 0);

    env = getenv("SPACEWAY3K_SEED");
    if (env && env[0]) g_rng = (unsigned)atoi(env);
    else g_rng = (unsigned)time(NULL) ^ 0xA5A5A5A5u;

    env = getenv("SPACEWAY3K_SPEED");
    if (env && env[0]) {
        int v = atoi(env);
        if (v >= 1 && v <= 8) g_speed = v;
    }

    dict cfg_json = {
        "title": "SPACEWAY 3000",
        "hot_danger": 55,
        "rich_credits": 14000
    };
    GalaxyConfig cfg = (GalaxyConfig) cfg_json;

    printf("\n");
    printf("  ╔══════════════════════════════════════════════════════════╗\n");
    printf("  ║              S P A C E W A Y   3 0 0 0                   ║\n");
    printf("  ║     ClassyC galaxy map · NanoVG · war & trade sim        ║\n");
    printf("  ╚══════════════════════════════════════════════════════════╝\n");
    printf("  %s · planets=%d ships=%d · types: %s %s\n",
           cfg.title, N_PLANETS, N_SHIPS,
           nameof<Faction>(), typeof<Ship>());
    if (g_profile) printf("  PROFILE mode: uncapped, %d frames, section timers\n", frame_limit);
    else if (smoke) printf("  SMOKE mode: headless frames then exit\n");
    printf("  keys: ESC SPACE R [ ] F\n\n");
    fflush(stdout);

    if (!glfwInit()) {
        fprintf(stderr, "Failed to init GLFW\n");
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    if (smoke) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    GLFWwindow *window = glfwCreateWindow(WIN_W, WIN_H, "SPACEWAY 3000", NULL, NULL);
    if (!window) {
        fprintf(stderr, "Failed to create window\n");
        glfwTerminate();
        return 1;
    }
    glfwSetKeyCallback(window, on_key);
    glfwMakeContextCurrent(window);
    /* Manual 30fps pacing below — vsync-off so we actually sleep and free the CPU. */
    glfwSwapInterval(0);

    NVGcontext *vg = nvgCreateGL3(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
    if (!vg) {
        fprintf(stderr, "Could not init NanoVG\n");
        return 1;
    }

    /* Prefer Roboto from nanovg examples; fall back to DejaVu */
    int font = nvgCreateFont(vg, "sans",
        "../../PROJ/nanogui2/ext/nanovg/example/Roboto-Regular.ttf");
    if (font < 0)
        font = nvgCreateFont(vg, "sans",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
    int bold = nvgCreateFont(vg, "sans-bold",
        "../../PROJ/nanogui2/ext/nanovg/example/Roboto-Bold.ttf");
    if (bold < 0)
        bold = nvgCreateFont(vg, "sans-bold",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf");
    if (font < 0) font = bold;
    if (bold < 0) bold = font;
    if (font < 0) {
        fprintf(stderr, "No usable font found\n");
        return 1;
    }

    auto world = List<Planet>();
    auto fleet = List<Ship>();
    auto fx = List<BattleFx>();
    auto presence = Map<int, int>();
    BuildWorld(&world, &fleet, &fx);

    int tick = 0;
    int deaths = 0;
    int battles = 0;
    int frames = 0;
    const double frame_budget = 1.0 / (double)TARGET_FPS;
    const float  sim_dt       = (float)frame_budget; /* fixed step @ 30Hz */
    double prev = glfwGetTime();
    double fps = (double)TARGET_FPS;
    double fps_accum = 0;
    int fps_count = 0;
    long long score = 0;

    if (!smoke)
        printf("  frame cap: %d fps (sleep pacing, vsync off)\n\n", TARGET_FPS);
    fflush(stdout);

    double wall0 = prof_now();

    while (!glfwWindowShouldClose(window) && !g_quit) {
        double frame_start = glfwGetTime();
        double t0, t1;
        float dt = sim_dt;
        if (smoke) {
            /* uncapped: keep smoke/profile tests fast */
            double now = frame_start;
            dt = (float)(now - prev);
            if (dt < 0.0001f) dt = 0.0001f;
            if (dt > 0.05f) dt = 0.05f;
            /* profile: fixed sim step so work is comparable frame-to-frame */
            if (g_profile) dt = sim_dt;
        }
        prev = frame_start;

        fps_accum += (smoke ? (double)dt : frame_budget);
        fps_count++;
        if (fps_accum >= 0.5) {
            fps = (double)fps_count / fps_accum;
            fps_accum = 0;
            fps_count = 0;
        }

        if (g_reseed) {
            g_reseed = 0;
            g_rng ^= (unsigned)(frame_start * 1e6) ^ 0x9E3779B9u;
            deaths = 0;
            battles = 0;
            tick = 0;
            BuildWorld(&world, &fleet, &fx);
            printf("  reseeded galaxy rng=%u\n", g_rng);
            fflush(stdout);
        }

        t0 = prof_now();
        if (!g_paused) {
            int s;
            for (s = 0; s < g_speed; s++) {
                TickSim(&fleet, &world, &fx, &deaths, &battles, frame_start, dt);
                tick++;
            }
        } else {
            /* still animate docked orbits slowly while paused */
            int i;
            for (i = 0; i < fleet.Count(); i++) {
                Ship *sh = fleet.GetMut(i);
                if (sh.IsAlive() && !sh.IsTraveling())
                    UpdateTravel(sh, &world, dt * 0.25f);
            }
            for (i = 0; i < world.Count(); i++)
                world.GetMut(i).pulse += dt * 0.8f;
        }
        t1 = prof_now();
        if (g_profile) g_prof_ms[PROF_SIM] += t1 - t0;

        t0 = prof_now();
        if ((tick & 15) == 0)
            RebuildPresence(&presence, &fleet);
        t1 = prof_now();
        if (g_profile) g_prof_ms[PROF_PRESENCE] += t1 - t0;

        int win_w, win_h, fb_w, fb_h;
        glfwGetWindowSize(window, &win_w, &win_h);
        glfwGetFramebufferSize(window, &fb_w, &fb_h);
        float px = (win_w > 0) ? (float)fb_w / (float)win_w : 1.0f;

        glViewport(0, 0, fb_w, fb_h);
        glClearColor(0.02f, 0.03f, 0.07f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        nvgBeginFrame(vg, (float)win_w, (float)win_h, px);

        float map_x = 0;
        float map_y = 0;
        float map_w = (float)win_w - (float)HUD_W;
        float map_h = (float)win_h;
        float scale = map_w / (float)WORLD_W;
        if (map_h / (float)WORLD_H < scale) scale = map_h / (float)WORLD_H;

        t0 = prof_now();
        DrawStars(vg, map_x, map_y, map_w, map_h, (float)frame_start);
        t1 = prof_now();
        if (g_profile) g_prof_ms[PROF_STARS] += t1 - t0;

        /* travel lanes under ships */
        t0 = prof_now();
        int i;
        for (i = 0; i < fleet.Count(); i++) {
            Ship s = fleet.Get(i);
            if (!s.IsAlive() || !s.IsTraveling()) continue;
            if (g_filter >= 0 && s.FactionKey() != g_filter) continue;
            Planet from = world.Get(s.from_id % world.Count());
            Planet to   = world.Get(s.planet_id % world.Count());
            float sx0, sy0, sx1, sy1;
            WorldToScreen(from.x, from.y, map_x, map_y, map_w, map_h, &sx0, &sy0);
            WorldToScreen(to.x, to.y, map_x, map_y, map_w, map_h, &sx1, &sy1);
            nvgBeginPath(vg);
            nvgMoveTo(vg, sx0, sy0);
            nvgLineTo(vg, sx1, sy1);
            nvgStrokeColor(vg, FactionColor(s.faction, 35));
            nvgStrokeWidth(vg, 1.0f);
            nvgStroke(vg);
        }
        t1 = prof_now();
        if (g_profile) g_prof_ms[PROF_LANES] += t1 - t0;

        /* planets */
        t0 = prof_now();
        for (i = 0; i < world.Count(); i++) {
            Planet p = world.Get(i);
            float sx, sy;
            WorldToScreen(p.x, p.y, map_x, map_y, map_w, map_h, &sx, &sy);
            int sc = presence.Contains(p.id) ? presence[p.id] : 0;
            if (g_filter >= 0 && p.FactionKey() != g_filter)
                continue;
            DrawPlanet(vg, p, sx, sy, scale * 0.95f, sc, font);
        }
        t1 = prof_now();
        if (g_profile) g_prof_ms[PROF_PLANETS] += t1 - t0;

        /* ships + wrecks */
        t0 = prof_now();
        for (i = 0; i < fleet.Count(); i++) {
            Ship s = fleet.Get(i);
            if (g_filter >= 0 && s.FactionKey() != g_filter) continue;
            float sx, sy;
            WorldToScreen(s.x, s.y, map_x, map_y, map_w, map_h, &sx, &sy);
            DrawShip(vg, s, sx, sy, scale * 1.1f, frame_start);
        }
        t1 = prof_now();
        if (g_profile) g_prof_ms[PROF_SHIPS] += t1 - t0;

        /* combat / trade FX */
        t0 = prof_now();
        for (auto e in fx)
            DrawFx(vg, e, map_x, map_y, map_w, map_h);
        t1 = prof_now();
        if (g_profile) g_prof_ms[PROF_FX] += t1 - t0;

        DrawTitle(vg, bold, (float)frame_start);

        t0 = prof_now();
        score = EconomyScore(&fleet);
        t1 = prof_now();
        if (g_profile) g_prof_ms[PROF_SCORE] += t1 - t0;

        t0 = prof_now();
        DrawHud(vg, font, bold, &world, &fleet, &fx, &presence,
                tick, deaths, battles, g_paused, g_speed, g_filter,
                (float)fps, score);
        t1 = prof_now();
        if (g_profile) g_prof_ms[PROF_HUD] += t1 - t0;

        t0 = prof_now();
        nvgEndFrame(vg);
        t1 = prof_now();
        if (g_profile) g_prof_ms[PROF_NVG_END] += t1 - t0;

        t0 = prof_now();
        glfwSwapBuffers(window);
        glfwPollEvents();
        t1 = prof_now();
        if (g_profile) g_prof_ms[PROF_SWAP] += t1 - t0;

        /* ── frame pacing: sleep out the rest of the 1/30s budget ── */
        if (!smoke) {
            double elapsed = glfwGetTime() - frame_start;
            double spare = frame_budget - elapsed;
            if (spare > 0.0005) {
                /* nanosleep is more precise than usleep for ~33ms budgets */
                struct timespec ts;
                ts.tv_sec = (time_t)spare;
                ts.tv_nsec = (long)((spare - (double)ts.tv_sec) * 1e9);
                if (ts.tv_nsec < 0) ts.tv_nsec = 0;
                if (ts.tv_nsec > 999999999L) ts.tv_nsec = 999999999L;
                nanosleep(&ts, NULL);
            }
        }

        frames++;
        if (g_profile) g_prof_frames++;
        if (smoke && frames >= frame_limit) {
            printf("  smoke: frames=%d tick=%d alive=%d battles=%d deaths=%d score=%lld fx=%d\n",
                   frames, tick, CountAlive(&fleet), battles, deaths, score,
                   fx.Count());
            fflush(stdout);
            break;
        }
    }

    if (g_profile && g_prof_frames > 0) {
        double wall = prof_now() - wall0;
        double sum = 0;
        int pi;
        for (pi = 0; pi < PROF_N; pi++) sum += g_prof_ms[pi];
        printf("\n  ── PROFILE  (%d frames, wall %.1f ms, %.1f fps uncapped) ──\n",
               g_prof_frames, wall, 1000.0 * (double)g_prof_frames / wall);
        printf("  %-18s  %10s  %8s  %7s\n", "section", "total_ms", "ms/frame", "%");
        printf("  ────────────────────────────────────────────────────\n");
        for (pi = 0; pi < PROF_N; pi++) {
            double avg = g_prof_ms[pi] / (double)g_prof_frames;
            double pct = sum > 0 ? 100.0 * g_prof_ms[pi] / sum : 0;
            printf("  %-18s  %10.1f  %8.3f  %6.1f%%\n",
                   prof_names[pi], g_prof_ms[pi], avg, pct);
        }
        printf("  ────────────────────────────────────────────────────\n");
        printf("  %-18s  %10.1f  %8.3f  %6.1f%%\n",
               "SUM sections", sum, sum / (double)g_prof_frames, 100.0);
        printf("  %-18s  %10.1f  %8.3f\n",
               "wall (incl other)", wall, wall / (double)g_prof_frames);
        printf("  30fps budget is 33.3 ms/frame — overage => dropped sleep headroom\n");
        fflush(stdout);
    }

    /* final LINQ-style report (aurora style) */
    auto living = fleet.Where((Ship s) => s.IsAlive());
    living.Sort((Ship a, Ship b) => b.credits - a.credits);
    auto by_fac = living.GroupBy((Ship s) => s.FactionKey());
    auto killers = fleet.Where((Ship s) => s.kills > 0);
    killers.Sort((Ship a, Ship b) => b.kills - a.kills);

    printf("\n  ── final report ──\n");
    printf("  frames=%d tick=%d alive=%d battles=%d deaths=%d\n",
           frames, tick, living.Count(), battles, deaths);
    printf("  factions alive (%s):\n", nameof<Faction>());
    for (auto bucket, group in by_fac) {
        Faction f = (Faction)bucket;
        printf("    %-14s %d\n",
               ((String)f.nameof()).upper(), group.Count());
    }
    int top = living.Count() < 5 ? living.Count() : 5;
    printf("  top credits:\n");
    int i;
    for (i = 0; i < top; i++) {
        Ship s = living.Get(i);
        printf("    %s\n", s.ToString());
    }
    int ktop = killers.Count() < 5 ? killers.Count() : 5;
    if (ktop > 0) {
        printf("  top killers:\n");
        for (i = 0; i < ktop; i++) {
            Ship s = killers.Get(i);
            printf("    %s  kills=%d\n", s.ToString(), s.kills);
        }
    }
    printf("  SPACEWAY 3000 — clear skies.\n\n");
    fflush(stdout);

    nvgDeleteGL3(vg);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
