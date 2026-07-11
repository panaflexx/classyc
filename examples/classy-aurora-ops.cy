/* classy-aurora-ops.cy — AURORA OPS constellation mission board
 *
 * Showcase of ClassyC's **first-class by-value collection idiom**:
 * everyday LINQ pipelines use stack/value List/Map shells with RAII —
 * no `owned auto` / `delete` on every intermediate.  `owned` is the
 * heap escape hatch, not the default for local transforms.
 *
 * Weave (see also classy-neon-grid.cy, cy-validate/, BY-VALUE.md):
 *
 *   Domain   · Ship* owning stack List  · Signal by-value List (POD DTO)
 *            · fluent Boost · named ctor args · enums + nameof
 *   Memory   · stack List/Map RAII · value-returning Where/Take/Skip/Copy
 *            · List<Ship*>.owns() for domain pointees · move-only containers
 *            · helpers return List by value (`return move r` / implicit move)
 *   LINQ     · Where / Select / Any / All / Find / FindOr
 *            · First / Last / Take / Skip · OrderBy helpers · ForEach
 *            · Range · Plus · Slice · Distinct — all by-value local shells
 *   Map      · stack Map · subscript m[k] · TryAdd / GetOr / Where
 *   String   · equals / contains / starts_with / split / join · f-strings
 *   Generics · free Max<T> inference · Select method generic
 *   JSON     · dict brace-init · (T) d / (T)? d bind
 *   Style    · `.` auto-deref · `?.` / `??` · value chains · for-in
 *
 * Usage (from project root):
 *   ./bin/classyc -I include examples/classy-aurora-ops.cy -eg
 */

#include <stdio.h>
#include <string.h>
#include "map.h"
#include "list.h"
#include "set.h"

/* ───────────────────────── Domain ───────────────────────── */

enum Sector { core = 0, rim = 1, veil = 2 };
enum Alert  { green = 0, amber = 1, red = 2 };

Alert AlertOf(int heat) {
    if (heat >= 80) return red;
    if (heat >= 50) return amber;
    return green;
}

T Max<T>(T a, T b) { return a > b ? a : b; }

class OpsConfig {
    String board_name;
    int    hot_threshold;
    int    max_wing;
};

class Ship {
    int     id;
    String  callsign;
    Sector  sector;
    int     heat;
    int     range_ly;

    Ship(int id, String callsign, Sector sector, int heat, int range_ly) {
        this.id = id;
        this.callsign = callsign;
        this.sector = sector;
        this.heat = heat;
        this.range_ly = range_ly;
    }

    ~Ship() {
        printf("      ~Ship #%d %s decommissioned\n", id, callsign);
    }

    int IsHot()     { return heat >= 50; }
    int IsDeep()    { return range_ly >= 120; }
    Alert Level()   { return AlertOf(heat); }
    int SectorKey() { return (int)sector; }

    Ship* Boost(int delta) {
        this.heat += delta;
        return this;
    }

    String RangeLabel() {
        if (range_ly < 10) return f"0{range_ly}ly";
        return f"{range_ly}ly";
    }

    String ToString() {
        Sector s = sector;
        Alert a = Level();
        String sec = ((String)s.nameof()).upper();
        String rng = RangeLabel();
        return f"#{id} {callsign} [{sec}] heat={heat} alert={a.nameof()} range={rng}";
    }

    void Banner() { printf("   ▶  %s\n", ToString()); }
};

int ByHeatDesc(Ship* a, Ship* b) { return b.heat - a.heat; }
int ByRange(Ship* a, Ship* b)    { return a.range_ly - b.range_ly; }
int ByIntAsc(int a, int b)       { return a - b; }

/* Helpers return List by value — no heap shell at the call site. */
List<Ship*> OrderByHeat(List<Ship*>* src) {
    auto r = src.Copy();
    r.Sort(ByHeatDesc);
    return move r;
}

List<Ship*> OrderByRange(List<Ship*>* src) {
    auto r = src.Copy();
    r.Sort(ByRange);
    return r;                          /* implicit move of local */
}

void print_banner(Ship* p) { p.Banner(); }

/* POD DTO — by-value in List (no user dtor). */
class Signal {
    int channel;
    int strength;
    int sector;                        /* Sector ordinal */

    Signal(int channel, int strength, int sector) {
        this.channel = channel;
        this.strength = strength;
        this.sector = sector;
    }

    int IsLoud() { return strength >= 70; }
    String ToString() {
        Sector s = (Sector)sector;
        String sec = ((String)s.nameof()).upper();
        return f"ch{channel} str={strength} [{sec}]";
    }
};

int ByStrength(Signal a, Signal b) { return a.strength - b.strength; }

void hr(const char* title) {
    printf("\n════════════════════════════════════════════════════════════\n");
    printf("  %s\n", title);
    printf("════════════════════════════════════════════════════════════\n");
}

void SeedFleet(List<Ship*>* fleet) {
    fleet.Add(new Ship(1, "AURORA",  core, 88, 142));
    fleet.Add(new Ship(2, "VEILRUN", veil, 61,  98));
    fleet.Add(new Ship(3, "RIMSPARK", rim, 44, 165));
    fleet.Add(new Ship(id=4, callsign="KITE", sector=core, heat=73, range_ly=110));
    fleet.Add(new Ship(5, "NEXUS",   veil, 91,  76));
    fleet.Add(new Ship(6, "EMBER",   rim,  35, 188));
    fleet.Add(new Ship(7, "SOLACE",  core, 55, 130));
    fleet.Add(new Ship(8, "GLITCH",  veil, 22,  54));
}

void FillHeatMap(Map<String, int>* board, List<Ship*>* ships) {
    for (auto p in ships) board[p.callsign] = p.heat;
}

/* ───────────────────────── main ─────────────────────────── */

int main() {
    printf("\n");
    printf("    █████╗ ██╗   ██╗██████╗  ██████╗ ██████╗  █████╗ \n");
    printf("   ██╔══██╗██║   ██║██╔══██╗██╔═══██╗██╔══██╗██╔══██╗\n");
    printf("   ███████║██║   ██║██████╔╝██║   ██║██████╔╝███████║\n");
    printf("   ██╔══██║██║   ██║██╔══██╗██║   ██║██╔══██╗██╔══██║\n");
    printf("   ██║  ██║╚██████╔╝██║  ██║╚██████╔╝██║  ██║██║  ██║\n");
    printf("   ╚═╝  ╚═╝ ╚═════╝ ╚═╝  ╚═╝ ╚═════╝ ╚═╝  ╚═╝╚═╝  ╚═╝\n");
    printf("              constellation ops · by-value List/Map idiom\n");
    printf("   types: %s · %s · %s\n",
           nameof<Sector>(), nameof<Alert>(), typeof<Ship*>());

    dict cfg_json = {
        "board_name": "AURORA OPS",
        "hot_threshold": 50,
        "max_wing": 3,
        "sectors": [0, 1, 2]
    };
    OpsConfig cfg = (OpsConfig) cfg_json;
    OpsConfig soft = (OpsConfig)? { "board_name": "warmup" };
    printf("   config: %s  hot≥%d  wing≤%d  (lenient name=%s thr=%d)\n",
           cfg.board_name, cfg.hot_threshold, cfg.max_wing,
           soft.board_name, soft.hot_threshold);

    /* ═══ 1. Fleet — stack List of owning pointers ═══════════════════════════ */
    hr("1 · FLEET  (stack List.owns · Seed · ForEach · First/Last · Max)");

    auto fleet = List<Ship*>();
    fleet.owns();
    SeedFleet(&fleet);

    fleet.Get(0).Boost(5).Boost(2);       /* AURORA 88 → 95 */

    int peak = fleet.First().heat;
    for (auto p in fleet) peak = Max(peak, p.heat);
    printf("  %d ships · first=%s · last=%s · peak heat=%d (Max<T>)\n",
           fleet.Count(), fleet.First().callsign, fleet.Last().callsign, peak);
    fleet.ForEach(print_banner);

    auto roster = fleet.Copy();           /* by-value List shell */

    /* ═══ 2. Sortie board ════════════════════════════════════════════════════ */
    hr("2 · SORTIE  (Where · OrderBy · Select · Find · ?. ?? · no owned)");

    auto hot_raw = fleet.Where((Ship* p) => p.IsHot());
    auto hot     = OrderByHeat(&hot_raw);
    printf("  hot ranked: %d  (top %s · floor %s)\n",
           hot.Count(), hot.First().callsign, hot.Last().callsign);
    hot.ForEach(print_banner);

    auto hot_names = hot.Select<String>((Ship* p) => p.callsign);
    printf("  hot callsigns: %s\n", hot_names.ToJson());

    String sample = hot_names.First();
    printf("  String: equals(AURORA)=%s  starts_with(AU)=%s  contains(ORA)=%s\n",
           sample.equals("AURORA") ? "true" : "false",
           sample.starts_with("AU") ? "true" : "false",
           sample.contains("ORA") ? "true" : "false");

    String tag = "VEIL-RUN";
    owned auto bits = tag.split("-");     /* String.split still heap List* */
    printf("  split/join: %s → %s\n", tag, bits.join("/"));

    auto heats = roster.Select((Ship* p) => p.heat);
    heats.Sort(ByIntAsc);
    printf("  heat ladder: %s\n", heats.ToJson());

    printf("  Any(IsHot)=%s  All(live)=%s  Any(IsDeep)=%s\n",
           fleet.Any((Ship* p) => p.IsHot())  ? "true" : "false",
           fleet.All((Ship* p) => p != NULL)  ? "true" : "false",
           fleet.Any((Ship* p) => p.IsDeep()) ? "true" : "false");

    Ship* deep = fleet.Find((Ship* p) => p.IsDeep());
    Ship* ghost = fleet.FindOr(NULL, (Ship* p) => p.heat > 99999);

    String deep_name = deep?.callsign ?? (String)"?";
    String ghost_name = ghost?.callsign ?? (String)"(none)";
    printf("  deepest scout: %s\n",
           deep?.ToString() ?? (String)"(no deep-range ship)");
    if (deep) {
        Sector ds = deep.sector;
        Alert da = deep.Level();
        printf("  nameof: sector=%s alert=%s\n", ds.nameof(), da.nameof());
    }
    printf("  FindOr miss → %s · ?? name=%s\n", ghost_name, deep_name);

    /* ═══ 3. Windows — all value transforms ══════════════════════════════════ */
    hr("3 · WINDOWS  (Range · Plus · Slice · Distinct · Skip/Take chains)");

    auto wing_a = List<int>.Range(1, 5);
    auto wing_b = List<int>.Range(6, 5);
    auto full   = wing_a.Plus(&wing_b);
    auto mid    = full.Slice(3, 4);
    auto tail   = full.Skip(7).Take(3);   /* chain of values */

    auto noisy = List<int>();
    noisy.Add(1); noisy.Add(2); noisy.Add(2);
    noisy.Add(3); noisy.Add(3); noisy.Add(3); noisy.Add(4);
    auto clean = noisy.Distinct();

    printf("  wing A:  %s\n", wing_a.ToJson());
    printf("  wing B:  %s\n", wing_b.ToJson());
    printf("  full:    %s\n", full.ToJson());
    printf("  midfield:%s · late: %s\n", mid.ToJson(), tail.ToJson());
    printf("  Distinct:%s → %s\n", noisy.ToJson(), clean.ToJson());

    auto pulse = Map<int, String>();
    pulse[1] = "uplink";
    pulse[5] = "handoff";
    pulse[10] = "dock";
    printf("  pulse[5]=%s  keys:", pulse[5]);
    for (auto k in pulse) printf(" %d", k);
    printf("\n");

    /* ═══ 4. Sector briefing ═════════════════════════════════════════════════ */
    hr("4 · SECTOR BRIEFING  (GroupBy · Set watchlist · enum nameof)");

    owned auto by_sector = roster.GroupBy((Ship* p) => p.SectorKey());
    printf("  %d sectors (%s)\n", by_sector.Count(), nameof<Sector>());

    for (auto bucket, group in by_sector) {
        Sector sec = (Sector)bucket;
        group.Sort(ByHeatDesc);
        printf("\n  ▸ %s sector (%d)  leader=%s\n",
               ((String)sec.nameof()).upper(), group.Count(),
               group.First().callsign);
        group.ForEach(print_banner);
    }

    auto watch = Set<String>();
    watch.Add("AURORA");
    watch.Add("NEXUS");
    String rebuilt = (String)"AUR" + "ORA";
    int dup = watch.Add(rebuilt);
    printf("\n  watchlist Set: count=%d  Add(rebuilt AURORA)=%s  Contains=%s\n",
           watch.Count(),
           dup ? "inserted" : "deduped",
           watch.Contains("AURORA") ? "true" : "false");

    /* ═══ 5. Heat board — stack Map ══════════════════════════════════════════ */
    hr("5 · HEAT BOARD  (stack Map · subscript · Where value Map)");

    auto board = Map<String, int>();
    FillHeatMap(&board, &fleet);
    board["SCOUT"] = 18;
    board["SCOUT"] = board["SCOUT"] + 7;
    printf("  board[\"AURORA\"]=%d  board[\"SCOUT\"]=%d\n",
           board["AURORA"], board["SCOUT"]);
    printf("  TryAdd AURORA? %s  GetOr(NEVER,-1)=%d\n",
           board.TryAdd("AURORA", 9999) ? "inserted" : "blocked",
           board.GetOr("NEVER", -1));

    auto hot_board = board.Where((String k, int v) => {
        (void)k;
        return v >= 50;
    });
    printf("  hot board: %s\n", hot_board.ToJson());

    {
        int threw = 0;
        try {
            int miss = board.Get("NEVER");
            (void)miss;
        } catch (e) {
            threw = 1;
            printf("  Get(NEVER) → KeyException: %s\n", e.msg);
        }
        printf("  threw? %s\n", threw ? "yes" : "no");
    }

    /* ═══ 6. Map LINQ — value SelectValues / Keys ════════════════════════════ */
    hr("6 · MAP LINQ  (SelectValues · SelectKeys · Keys — value shells)");

    auto doubled = board.SelectValues<int>((String k, int v) => {
        (void)k;
        return v * 2;
    });
    printf("  SelectValues(*2):\n    %s\n", doubled.to_string());

    auto coded = board.SelectKeys<int>((String k, int v) => {
        (void)v;
        const char* s = (const char*)k;
        return (s && s[0]) ? (int)s[0] : 0;
    });
    printf("  SelectKeys count=%d:\n    %s\n", coded.Count(), coded.ToJson());

    auto keys = board.Keys();
    printf("  Keys=%d  IsEmpty=%s  for-in keys:", keys.Count(),
           board.IsEmpty() ? "true" : "false");
    int shown = 0;
    for (auto k in board) {
        if (shown < 4) printf(" %s", k);
        shown++;
    }
    printf(" … (%d total)\n", shown);

    /* ═══ 7. Signals — by-value DTO List ═════════════════════════════════════ */
    hr("7 · SIGNALS  (stack List<Signal> by-value · Where · Sort)");

    auto signals = List<Signal>();
    signals.Add(Signal(1, 82, (int)core));
    signals.Add(Signal(2, 41, (int)rim));
    signals.Add(Signal(3, 95, (int)veil));
    signals.Add(Signal(4, 67, (int)core));

    printf("  %d by-value signals · first=%s\n",
           signals.Count(), signals.First().ToString());
    for (auto s in signals)
        printf("   · %s  loud=%s\n", s.ToString(), s.IsLoud() ? "yes" : "no");

    auto loud = signals.Where((Signal s) => s.IsLoud());
    printf("  Where(IsLoud): %d\n", loud.Count());
    for (auto s in loud) printf("   · %s\n", s.ToString());

    auto loud_top = loud.Take(2);
    printf("  Take(2) of loud: %d  first=%s\n",
           loud_top.Count(), loud_top.First().ToString());

    signals.Sort(ByStrength);
    printf("  after Sort(ByStrength): first=%s  last=%s\n",
           signals.First().ToString(), signals.Last().ToString());

    /* ═══ 8. Wing / podium — pure value pipeline ═════════════════════════════ */
    hr("8 · WING  (OrderByRange · Take · Skip chain · stack Map)");

    auto by_range = OrderByRange(&fleet);
    auto top3     = by_range.Take(3);
    auto silver   = by_range.Skip(1).Take(1);

    printf("  silver: %s\n", silver.First().ToString());

    auto wing = Map<String, Ship*>();
    int place = 1;
    for (auto p in top3) {
        wing[p.callsign] = p;
        printf("  #%d  %s\n", place, p.ToString());
        place++;
    }
    printf("  for (auto k, v in wing):\n");
    for (auto sign, p in wing) {
        printf("    [%s] %s heat=%d range=%s\n",
               sign, p.sector.nameof(), p.heat, p.RangeLabel());
    }

    /* ═══ 9. Uplink ══════════════════════════════════════════════════════════ */
    hr("9 · UPLINK  (dict DTO · f-strings · config bind)");

    String scout = deep?.callsign ?? (String)"?";
    Ship* silver_p = silver.IsEmpty() ? NULL : silver.First();
    String silver_name = silver_p?.callsign ?? (String)"?";
    dict uplink = {
        "board": cfg.board_name,
        "ships": fleet.Count(),
        "hot": hot.Count(),
        "peak_heat": peak,
        "deep_scout": scout,
        "silver": silver_name,
        "wing": full.ToJson(),
        "hot_board": hot_board.ToJson(),
        "sectors": by_sector.Count(),
        "watchlist": watch.Count(),
        "signals": signals.Count(),
        "loud": loud.Count(),
        "domain": {
            "sector": nameof<Sector>(),
            "alert": nameof<Alert>()
        }
    };
    printf("  payload: %s\n", uplink.json());
    printf("  feed:    %s\n",
           f"{cfg.board_name} · {fleet.Count()} ships · {hot.Count()} hot · scout {scout}");
    printf("  nameof: %s · %s.nameof()=%s · Max sample=%d\n",
           nameof(core), nameof(red), red.nameof(), Max(3, 5));

    hr("SHUTDOWN  (~fleet owns every Ship · stack List/Map free buffers)");
    printf("  dropping scope…\n");
    return 0;
}
