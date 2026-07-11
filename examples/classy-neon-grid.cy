/* classy-neon-grid.cy — NEON GRID race telemetry command center
 *
 * Showcase weave of ClassyC features (see also cy-validate/, BY-VALUE.md):
 *
 *   Domain   · Pilot* owning stack List  · LapSample by-value List (POD DTO)
 *            · fluent Bump · named ctor args · enums + nameof
 *   Memory   · stack List/Map value form (RAII)
 *            · Where/Take/Skip/Copy return value shells — no owned on pipelines
 *            · List<Pilot*>.owns() for domain pointees · move-only containers
 *            · owned / new only for heap escape (split, GroupBy Map*)
 *   LINQ     · Where / Select / Any / All / Find / FindOr / GroupBy
 *            · First / Last / Take / Skip · OrderBy helpers · ForEach
 *            · Range · Plus · Slice · Distinct
 *   Map      · stack Map · subscript m[k] · TryAdd / GetOrAdd / GetOr / TryGet
 *            · SelectValues / Keys · for (auto k in map)
 *   Set      · content hashing for String (val-008)
 *   String   · equals / contains / starts_with / split / join
 *   Generics · free Max<T> inference (val-023)
 *   JSON     · dict { … } brace-init · (T) d / (T)? d bind (val-020)
 *   Style    · `.` auto-deref · `?.` / `??` · value LINQ · f-strings · dict DTO
 *
 * Usage (from project root):
 *   ./bin/classyc -I include examples/classy-neon-grid.cy -eg
 */

#include <stdio.h>
#include <string.h>
#include "map.h"
#include "list.h"
#include "set.h"

/* ───────────────────────── Domain ───────────────────────── */

enum Faction { nova = 0, ember = 1, voids = 2 };
enum EloTier { C = 0, B = 1, A = 2, S = 3 };

EloTier RankOf(int elo) {
    if (elo >= 2000) return S;
    if (elo >= 1800) return A;
    if (elo >= 1500) return B;
    return C;
}

/* Generic free fn — T inferred at call site (val-023). */
T Max<T>(T a, T b) { return a > b ? a : b; }

/* JSON-bindable ops config (val-020 typed binding). */
class GridConfig {
    String event_name;
    int    ace_threshold;
    int    pole_ms;
};

class Pilot {
    int      id;
    String   callsign;
    Faction  faction;
    int      elo;
    int      best_ms;

    Pilot(int id, String callsign, Faction faction, int elo, int best_ms) {
        this.id = id;
        this.callsign = callsign;
        this.faction = faction;
        this.elo = elo;
        this.best_ms = best_ms;
    }

    ~Pilot() {
        printf("      ~Pilot #%d %s decommissioned\n", id, callsign);
    }

    int IsAce()  { return elo >= 1800; }
    int IsFast() { return best_ms < 62000; }
    EloTier Tier() { return RankOf(elo); }
    int FactionKey() { return (int)faction; }

    /* Fluent mutator — returns this (val-009 chaining). */
    Pilot* Bump(int delta) {
        this.elo += delta;
        return this;
    }

    String LapLabel() {
        int sec = best_ms / 1000;
        int ms  = best_ms % 1000;
        if (ms < 10)  return f"{sec}.00{ms}s";
        if (ms < 100) return f"{sec}.0{ms}s";
        return f"{sec}.{ms}s";
    }

    String ToString() {
        Faction f = faction;
        EloTier t = Tier();
        String fac = ((String)f.nameof()).upper();
        String lap = LapLabel();
        return f"#{id} {callsign} [{fac}] elo={elo} tier={t.nameof()} best={lap}";
    }

    void Banner() { printf("   ▶  %s\n", ToString()); }
};

int ByEloDesc(Pilot* a, Pilot* b) { return b.elo - a.elo; }
int ByPace(Pilot* a, Pilot* b)    { return a.best_ms - b.best_ms; }
int ByIntAsc(int a, int b)        { return a - b; }

/* Order helpers return value Lists (RAII at the call site). */
List<Pilot*> OrderByElo(List<Pilot*>* src) {
    auto r = src.Copy();
    r.Sort(ByEloDesc);
    return move r;
}
List<Pilot*> OrderByPace(List<Pilot*>* src) {
    auto r = src.Copy();
    r.Sort(ByPace);
    return r;   /* implicit move of move-only collection local */
}

void print_banner(Pilot* p) { p.Banner(); }

/* By-value DTO — inline in List<LapSample>, not List<LapSample*>.
 * Prefer no user dtor for list elements: Where/Filter/Sort pass T by value.
 * Use List<T*>.owns() when T needs a real destructor.  See BY-VALUE.md. */
class LapSample {
    int lap;
    int ms;
    int faction;   /* Faction ordinal stored as int */

    LapSample(int lap, int ms, int faction) {
        this.lap = lap;
        this.ms = ms;
        this.faction = faction;
    }
    /* no ~LapSample — trivially relocatable DTO for List by-value */

    int IsQuick() { return ms < 62000; }
    String ToString() {
        Faction f = (Faction)faction;
        int sec = ms / 1000;
        int rem = ms % 1000;
        String fac = ((String)f.nameof()).upper();
        if (rem < 10)  return f"L{lap} {sec}.00{rem}s [{fac}]";
        if (rem < 100) return f"L{lap} {sec}.0{rem}s [{fac}]";
        return f"L{lap} {sec}.{rem}s [{fac}]";
    }
};

int ByLapMs(LapSample a, LapSample b) { return a.ms - b.ms; }

void hr(const char* title) {
    printf("\n════════════════════════════════════════════════════════════\n");
    printf("  %s\n", title);
    printf("════════════════════════════════════════════════════════════\n");
}

/* Accepts stack or heap List* (call sites pass &stack or owned pointer). */
void SeedGrid(List<Pilot*>* grid) {
    grid.Add(new Pilot(1, "AURORA",  nova,  2140, 59840));
    grid.Add(new Pilot(2, "HEXFIRE", ember, 1912, 61220));
    grid.Add(new Pilot(3, "NULLCAT", voids, 1766, 63110));
    grid.Add(new Pilot(id=4, callsign="KITE", faction=nova, elo=1630, best_ms=64005));
    grid.Add(new Pilot(5, "RIVEN",   ember, 2055, 60112));
    grid.Add(new Pilot(6, "GLITCH",  voids, 1488, 67200));
    grid.Add(new Pilot(7, "SOLACE",  nova,  1880, 61550));
    grid.Add(new Pilot(8, "EMBERX",  ember, 1520, 65890));
}

/* Fills a stack scoreboard map from the grid. */
void FillScoreboard(Map<String, int>* board, List<Pilot*>* pilots) {
    for (auto p in pilots) board[p.callsign] = p.elo;
}

/* ───────────────────────── main ─────────────────────────── */

int main() {
    printf("\n");
    printf("   ███╗   ██╗███████╗ ██████╗ ███╗   ██╗     ██████╗ ██████╗ ██╗██████╗ \n");
    printf("   ████╗  ██║██╔════╝██╔═══██╗████╗  ██║    ██╔════╝ ██╔══██╗██║██╔══██╗\n");
    printf("   ██╔██╗ ██║█████╗  ██║   ██║██╔██╗ ██║    ██║  ███╗██████╔╝██║██║  ██║\n");
    printf("   ██║╚██╗██║██╔══╝  ██║   ██║██║╚██╗██║    ██║   ██║██╔══██╗██║██║  ██║\n");
    printf("   ██║ ╚████║███████╗╚██████╔╝██║ ╚████║    ╚██████╔╝██║  ██║██║██████╔╝\n");
    printf("   ╚═╝  ╚═══╝╚══════╝ ╚═════╝ ╚═╝  ╚═══╝     ╚═════╝ ╚═╝  ╚═╝╚═╝╚═════╝ \n");
    printf("              live race telemetry · ClassyC feature weave\n");
    printf("   types: %s · %s · %s\n",
           nameof<Faction>(), nameof<EloTier>(), typeof<Pilot*>());

    /* ── Ops config (dict brace-init + typed bind, val-020) ─────────────── */
    dict cfg_json = {
        "event_name": "NEON GRID",
        "ace_threshold": 1800,
        "pole_ms": 62000,
        "powers": [1, 2, 3, 4]
    };
    GridConfig cfg = (GridConfig) cfg_json;
    GridConfig soft = (GridConfig)? { "event_name": "warmup" };
    printf("   config: %s  ace≥%d  pole<%dms  powers[0]=%d  (lenient name=%s thr=%d)\n",
           cfg.event_name, cfg.ace_threshold, cfg.pole_ms,
           (int)cfg_json.powers[0],
           soft.event_name, soft.ace_threshold);

    /* ═══ 1. Roster — stack List of owning pointers ════════════════════════ */
    hr("1 · ROSTER  (stack List.owns · Seed · ForEach · First/Last · Max)");

    auto grid = List<Pilot*>();
    grid.owns();                         /* delete Pilots in ~List */
    SeedGrid(&grid);

    grid.Get(0).Bump(10).Bump(15);       /* AURORA 2140 → 2165 */

    int peak = grid.First().elo;
    for (auto p in grid) peak = Max(peak, p.elo);
    printf("  %d pilots · first=%s · last=%s · peak elo=%d (Max<T>)\n",
           grid.Count(), grid.First().callsign, grid.Last().callsign, peak);
    grid.ForEach(print_banner);

    auto roster = grid.Copy();           /* value List shell — RAII */

    /* ═══ 2. Qualifying ════════════════════════════════════════════════════ */
    hr("2 · QUALIFYING  (Where · OrderBy · Select · Find · ?. ?? · value List)");

    auto aces_raw = grid.Where((Pilot* p) => p.IsAce());
    auto aces = OrderByElo(&aces_raw);
    printf("  aces ranked: %d  (top %s · floor %s)\n",
           aces.Count(), aces.First().callsign, aces.Last().callsign);
    aces.ForEach(print_banner);

    auto ace_names = aces.Select<String>((Pilot* p) => p.callsign);
    printf("  ace callsigns: %s\n", ace_names.ToJson());

    String sample = ace_names.First();
    printf("  String: equals(AURORA)=%s  starts_with(AU)=%s  contains(ORA)=%s\n",
           sample.equals("AURORA") ? "true" : "false",
           sample.starts_with("AU") ? "true" : "false",
           sample.contains("ORA") ? "true" : "false");
    printf("  ace_names.Contains(\"AURORA\")=%s  Contains(\"NOPE\")=%s\n",
           ace_names.Contains("AURORA") ? "true" : "false",
           ace_names.Contains("NOPE")   ? "true" : "false");

    String tag = "HEX-FIRE";
    owned auto bits = tag.split("-");
    printf("  split/join: %s → %s\n", tag, bits.join("/"));

    auto elos = roster.Select((Pilot* p) => p.elo);
    elos.Sort(ByIntAsc);
    printf("  elo ladder: %s\n", elos.ToJson());

    printf("  Any(IsAce)=%s  All(live)=%s  Any(IsFast)=%s\n",
           grid.Any((Pilot* p) => p.IsAce())  ? "true" : "false",
           grid.All((Pilot* p) => p != NULL)  ? "true" : "false",
           grid.Any((Pilot* p) => p.IsFast()) ? "true" : "false");

    Pilot* pole = grid.Find((Pilot* p) => p.IsFast());
    Pilot* ghost = grid.FindOr(NULL, (Pilot* p) => p.elo > 99999);

    String pole_name = pole?.callsign ?? (String)"?";
    String ghost_name = ghost?.callsign ?? (String)"(none)";
    printf("  polesitter: %s\n",
           pole?.ToString() ?? (String)"(no sub-62s pilot)");
    if (pole) {
        Faction pf = pole.faction;
        EloTier pt = pole.Tier();
        printf("  nameof: faction=%s tier=%s\n", pf.nameof(), pt.nameof());
    }
    printf("  FindOr miss → %s · ?? name=%s\n", ghost_name, pole_name);

    /* ═══ 3. Heat windows — all value shells ═══════════════════════════════ */
    hr("3 · LAP WINDOWS  (Range · Plus · Slice · Distinct · Skip/Take chains)");

    auto heat_a = List<int>.Range(1, 5);
    auto heat_b = List<int>.Range(6, 5);
    auto full_race = heat_a.Plus(&heat_b);
    auto midfield  = full_race.Slice(3, 4);
    auto tail      = full_race.Skip(7).Take(3);

    auto noisy = List<int>();
    noisy.Add(1); noisy.Add(2); noisy.Add(2);
    noisy.Add(3); noisy.Add(3); noisy.Add(3); noisy.Add(4);
    auto clean = noisy.Distinct();

    printf("  heat A:    %s\n", heat_a.ToJson());
    printf("  heat B:    %s\n", heat_b.ToJson());
    printf("  full race: %s\n", full_race.ToJson());
    printf("  midfield:  %s · late: %s\n", midfield.ToJson(), tail.ToJson());
    printf("  Distinct:  %s → %s\n", noisy.ToJson(), clean.ToJson());

    /* int-keyed lap log — stack Map + subscript */
    auto lap_log = Map<int, String>();
    lap_log[1] = "green";
    lap_log[5] = "pit-in";
    lap_log[10] = "checkered";
    printf("  lap_log[5]=%s  keys:", lap_log[5]);
    for (auto k in lap_log) printf(" %d", k);
    printf("\n");

    /* ═══ 4. Faction briefing ══════════════════════════════════════════════ */
    hr("4 · FACTION BRIEFING  (GroupBy · Set watchlist · enum nameof)");

    owned auto by_faction = roster.GroupBy((Pilot* p) => p.FactionKey());  /* Map* + ownsValues buckets */
    printf("  %d factions (%s)\n", by_faction.Count(), nameof<Faction>());

    for (auto bucket, group in by_faction) {
        Faction fac = (Faction)bucket;
        group.Sort(ByEloDesc);
        printf("\n  ▸ %s division (%d)  leader=%s\n",
               ((String)fac.nameof()).upper(), group.Count(),
               group.First().callsign);
        group.ForEach(print_banner);
    }

    auto watch = Set<String>();
    watch.Add("AURORA");
    watch.Add("RIVEN");
    String rebuilt = (String)"AUR" + "ORA";
    int dup = watch.Add(rebuilt);
    printf("\n  watchlist Set: count=%d  Add(rebuilt AURORA)=%s  Contains=%s\n",
           watch.Count(),
           dup ? "inserted" : "deduped",
           watch.Contains("AURORA") ? "true" : "false");

    owned auto elo_buckets = elos.GroupBy((int e) => (int)RankOf(e));  /* GroupBy → heap Map* */
    printf("  elo histogram (%s):", nameof<EloTier>());
    for (auto tier, scores in elo_buckets) {
        EloTier t = (EloTier)tier;
        printf("  %s=%d", t.nameof(), scores.Count());
    }
    printf("\n");

    /* ═══ 5. Scoreboard — stack Map ════════════════════════════════════════ */
    hr("5 · SCOREBOARD  (stack Map · subscript · GetOr · TryGet · throw)");

    auto board = Map<String, int>();
    FillScoreboard(&board, &grid);
    board["NEWBIE"] = 1200;
    board["NEWBIE"] = board["NEWBIE"] + 25;
    printf("  board[\"AURORA\"]=%d  board[\"NEWBIE\"]=%d\n",
           board["AURORA"], board["NEWBIE"]);
    printf("  TryAdd AURORA? %s  GetOrAdd(GHOST)=%d  GetOr(NEVER,-1)=%d\n",
           board.TryAdd("AURORA", 9999) ? "inserted" : "blocked",
           board.GetOrAdd("GHOST", 1000), board.GetOr("NEVER", -1));

    int peek = 0;
    printf("  TryGet(HEXFIRE)=%s val=%d\n",
           board.TryGet("HEXFIRE", &peek) ? "true" : "false", peek);

    auto ace_board = board.Where((String k, int v) => {
        (void)k;
        return v >= 1800;
    });
    printf("  ace board: %s\n", ace_board.ToJson());

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

    /* ═══ 6. Map LINQ (value Select* / Keys; GroupBy still heap Map*) ══════ */
    hr("6 · MAP LINQ  (SelectValues · SelectKeys · GroupBy · Keys)");

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

    owned auto tiers = board.GroupBy((String k, int v) => {
        (void)k;
        return (int)RankOf(v);
    });
    printf("  GroupBy %s:\n", nameof<EloTier>());
    for (auto tier, scores in tiers) {
        EloTier t = (EloTier)tier;
        printf("    %s: %d  %s\n", t.nameof(), scores.Count(), scores.ToJson());
    }
    auto keys = board.Keys();
    printf("  Keys=%d  IsEmpty=%s  for-in keys:", keys.Count(),
           board.IsEmpty() ? "true" : "false");
    int shown = 0;
    for (auto k in board) {
        if (shown < 4) printf(" %s", k);
        shown++;
    }
    printf(" … (%d total)\n", shown);

    /* ═══ 7. By-value LapSample — stack List ═══════════════════════════════ */
    hr("7 · LAP SAMPLES  (stack List<LapSample> by-value · no new per element)");

    auto samples = List<LapSample>();
    samples.Add(LapSample(1, 59840, (int)nova));
    samples.Add(LapSample(2, 60112, (int)ember));
    samples.Add(LapSample(3, 64005, (int)voids));
    samples.Add(LapSample(4, 61550, (int)nova));

    printf("  %d by-value samples · first=%s\n",
           samples.Count(), samples.First().ToString());
    for (auto s in samples)
        printf("   · %s  quick=%s\n", s.ToString(), s.IsQuick() ? "yes" : "no");

    auto quick_laps = samples.Where((LapSample s) => s.IsQuick());
    printf("  Where(IsQuick): %d\n", quick_laps.Count());
    for (auto s in quick_laps) printf("   · %s\n", s.ToString());

    /* Open-code Select: generic Select monomorphization on stack List+value-T
       is still a rough edge; Pilot* path uses Select fine. */
    auto sample_ms = List<int>();
    for (auto s in samples) sample_ms.Add(s.ms);
    printf("  Select(ms): %s\n", sample_ms.ToJson());

    samples.Sort(ByLapMs);
    printf("  after Sort(ByLapMs): first=%s  last=%s\n",
           samples.First().ToString(), samples.Last().ToString());

    LapSample heavy = samples.Find((LapSample s) => s.ms > 63000);
    printf("  Find(ms>63000): %s\n", heavy.ToString());

    /* ═══ 8. Podium ════════════════════════════════════════════════════════ */
    hr("8 · PODIUM  (OrderByPace · Take · Skip chain · stack Map — no owned)");

    auto by_pace = OrderByPace(&grid);
    auto top3    = by_pace.Take(3);
    auto silver  = by_pace.Skip(1).Take(1);

    printf("  silver: %s\n", silver.First().ToString());

    auto podium = Map<String, Pilot*>();
    int place = 1;
    for (auto p in top3) {
        podium[p.callsign] = p;
        printf("  #%d  %s\n", place, p.ToString());
        place++;
    }
    printf("  for (auto k, v in podium):\n");
    for (auto sign, p in podium) {
        printf("    [%s] %s elo=%d best=%s\n",
               sign, p.faction.nameof(), p.elo, p.LapLabel());
    }

    /* ═══ 9. Uplink ════════════════════════════════════════════════════════ */
    hr("9 · UPLINK  (dict DTO · f-strings · config bind)");

    String polesitter = pole?.callsign ?? (String)"?";
    Pilot* silver_p = silver.IsEmpty() ? NULL : silver.First();
    String silver_name = silver_p?.callsign ?? (String)"?";
    dict uplink = {
        "event": cfg.event_name,
        "pilots": grid.Count(),
        "aces": aces.Count(),
        "peak_elo": peak,
        "polesitter": polesitter,
        "silver": silver_name,
        "heat": full_race.ToJson(),
        "ace_board": ace_board.ToJson(),
        "factions": by_faction.Count(),
        "watchlist": watch.Count(),
        "samples": samples.Count(),
        "quick_laps": quick_laps.Count(),
        "domain": {
            "faction": nameof<Faction>(),
            "tier": nameof<EloTier>()
        }
    };
    printf("  payload: %s\n", uplink.json());
    printf("  feed:    %s\n",
           f"{cfg.event_name} · {grid.Count()} pilots · {aces.Count()} aces · pole {polesitter}");
    printf("  nameof: %s · %s.nameof()=%s · Max sample=%d\n",
           nameof(ember), nameof(S), S.nameof(), Max(3, 5));

    hr("SHUTDOWN  (~grid owns every Pilot · stack Maps/Lists free buffers)");
    printf("  dropping scope…\n");
    return 0;
}
