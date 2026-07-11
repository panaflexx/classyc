/* classy-neon-grid.cy — NEON GRID race telemetry command center
 *
 * A self-contained showcase of everything List/Map landed for CLASSYC-CLEANUP:
 *
 *   List
 *     · brace-init, Range, Plus (non-mutating ++), Slice, Distinct, Sort
 *     · Where / Any / All / Find
 *     · Select<U> (generic method) · SelectString · Filter/Map
 *     · list->GroupBy(fn) via UFCS (free GroupBy lives in map.h)
 *     · owns() pointer ownership · ToJson / ToString
 *
 *   Map
 *     · subscript, TryAdd / GetOrAdd / AddOrUpdate / ContainsKey
 *     · Where / WhereKeys / SelectValues / SelectKeys
 *     · map->GroupBy  (nested Map<G, List<V>*> monomorphization)
 *     · Keys() / Values() nested generics
 *     · int-key + String-key ToJson via nameof
 *     · ownsValues() for Track* payloads
 *
 *   Extras: f-strings, nameof/typeof reflection, defer delete LIFO,
 *           throwing Get + GetOr / try-catch, for (auto k, v in map)
 *
 * Usage (from project root):
 *   ./bin/classyc -I include examples/classy-neon-grid.cy -eg
 */

#include <stdio.h>
#include <string.h>
#include "map.h"
#include "list.h"

/* ───────────────────────── Domain ───────────────────────── */

class Pilot {
    int    id;
    String callsign;
    String faction;   /* "nova" | "ember" | "void" */
    int    elo;
    int    best_ms;   /* best lap, milliseconds */

    Pilot(int id, String callsign, String faction, int elo, int best_ms) {
        this->id = id;
        this->callsign = callsign;
        this->faction = faction;
        this->elo = elo;
        this->best_ms = best_ms;
    }

    ~Pilot() {
        printf("      ~Pilot #%d %s decommissioned\n", id, callsign);
    }

    int Elo()     { return elo; }
    int BestMs()  { return best_ms; }
    int Id()      { return id; }

    void banner() {
        printf("   ▶  #%02d  %-12s  %-6s  elo=%4d  best=%d.%03ds\n",
               id, callsign, faction, elo, best_ms / 1000, best_ms % 1000);
    }
};

/* ───────────────── Projectors / predicates (fn ptrs for Select/GroupBy) ── */

String pilot_callsign(Pilot* p) { return p->callsign; }
int    pilot_elo(Pilot* p)      { return p->elo; }
int    pilot_id(Pilot* p)       { return p->id; }
String pilot_faction(Pilot* p)  { return p->faction; }

int pilot_is_ace(Pilot* p)   { return p->elo >= 1800; }
int pilot_is_fast(Pilot* p)  { return p->best_ms < 62000; }
int pilot_any_live(Pilot* p) { return p != NULL; }

/* Faction id → stable bucket key for MapGroupBy / list GroupBy. */
int faction_bucket(Pilot* p) {
    const char* f = (const char*)p->faction;
    if (f && f[0] == 'n') return 0; /* nova  */
    if (f && f[0] == 'e') return 1; /* ember */
    return 2;                        /* void  */
}

const char* faction_name(int bucket) {
    if (bucket == 0) return "NOVA";
    if (bucket == 1) return "EMBER";
    return "VOID";
}

/* Elo tiers for leaderboard grouping. */
int elo_tier(int elo) {
    if (elo >= 2000) return 3; /* S  */
    if (elo >= 1800) return 2; /* A  */
    if (elo >= 1500) return 1; /* B  */
    return 0;                  /* C  */
}

int score_tier(String callsign, int elo) {
    (void)callsign;
    return elo_tier(elo);
}

int score_is_ace(String k, int v) { (void)k; return v >= 1800; }
int score_double(String k, int v) { (void)k; return v * 2; }
int score_key_code(String k, int v) {
    (void)v;
    const char* s = (const char*)k;
    return (s && s[0]) ? (int)s[0] : 0;
}
int bump_elo(int v) { return v + 25; }

int cmp_elo_desc(Pilot* a, Pilot* b) { return b->elo - a->elo; }
int cmp_best_ms(Pilot* a, Pilot* b)  { return a->best_ms - b->best_ms; }
int cmp_int(int a, int b)            { return a - b; }

/* ───────────────────────── Pretty helpers ───────────────── */

void hr(const char* title) {
    printf("\n════════════════════════════════════════════════════════════\n");
    printf("  %s\n", title);
    printf("════════════════════════════════════════════════════════════\n");
}

void section(const char* s) {
    printf("\n── %s ──\n", s);
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
    printf("              live race telemetry · ClassyC List/Map showcase\n");
    printf("   reflection: list T = %s · map peeks at typeof(int*) = %s\n",
           nameof<int>(), typeof<int*>());

    /* ═══ 1. Own the grid — List<Pilot*>.owns() ═══════════════════════════ */
    hr("1 · ROSTER  (List<Pilot*>.owns — auto dtor on scope exit)");

    List<Pilot*>* grid = new List<Pilot*>().owns();
    defer delete grid;

    grid->Add(new Pilot(1,  "AURORA",   "nova",  2140, 59840));
    grid->Add(new Pilot(2,  "HEXFIRE",  "ember", 1912, 61220));
    grid->Add(new Pilot(3,  "NULLCAT",  "void",  1766, 63110));
    grid->Add(new Pilot(4,  "KITE",     "nova",  1630, 64005));
    grid->Add(new Pilot(5,  "RIVEN",    "ember", 2055, 60112));
    grid->Add(new Pilot(6,  "GLITCH",   "void",  1488, 67200));
    grid->Add(new Pilot(7,  "SOLACE",   "nova",  1880, 61550));
    grid->Add(new Pilot(8,  "EMBERX",   "ember", 1520, 65890));

    printf("  Pilots on the grid: %d\n", grid->Count());
    for (auto p in grid) p->banner();

    /* ═══ 2. Select / SelectString / Where / Any / All ═════════════════════ */
    hr("2 · PROJECTIONS  (Select<U> · Where · Any/All · Find)");

    List<String>* callsigns = grid->Select<String>(pilot_callsign);
    defer delete callsigns;
    printf("  callsigns via Select<String>: %s\n", callsigns->ToJson());

    List<int>* elos = grid->Select(pilot_elo);   /* U inferred from fn */
    defer delete elos;
    elos->Sort(cmp_int);
    printf("  elo ladder (sorted): %s\n", elos->ToJson());

    List<Pilot*>* aces = grid->Where(pilot_is_ace);  /* non-owning view */
    defer delete aces;
    printf("  aces (elo ≥ 1800): %d\n", aces->Count());
    for (auto p in aces) p->banner();

    printf("  any ace?  %s\n", grid->Any(pilot_is_ace)  ? "YES" : "no");
    printf("  all live? %s\n", grid->All(pilot_any_live) ? "YES" : "no");
    printf("  any sub-62s lap? %s\n",
           grid->Any(pilot_is_fast) ? "YES — someone is cooking" : "nope");

    Pilot* pole = grid->Find(pilot_is_fast);
    if (pole) {
        printf("  first sub-62s pilot found: %s (%d ms)\n",
               pole->callsign, pole->best_ms);
    }

    /* ═══ 3. Range · Plus · Slice · Distinct ══════════════════════════════ */
    hr("3 · LAP WINDOWS  (Range · Plus · Slice · Distinct)");

    List<int>* heat_a = List<int>.Range(1, 5);   /* laps 1..5  */
    defer delete heat_a;
    List<int>* heat_b = List<int>.Range(6, 5);   /* laps 6..10 */
    defer delete heat_b;

    /* Plus is non-mutating concat (operator+ stand-in). heat_a stays intact. */
    List<int>* full_race = heat_a->Plus(heat_b);
    defer delete full_race;
    printf("  heat A:     %s  (untouched by Plus)\n", heat_a->ToJson());
    printf("  heat B:     %s\n", heat_b->ToJson());
    printf("  full race:  %s  via heat_a->Plus(heat_b)\n", full_race->ToJson());

    List<int>* midfield = full_race->Slice(3, 4);  /* laps 4..7 */
    defer delete midfield;
    printf("  midfield window Slice(3,4): %s\n", midfield->ToJson());

    List<int>* noisy = new List<int>{1, 2, 2, 3, 3, 3, 4};
    defer delete noisy;
    List<int>* clean = noisy->Distinct();
    defer delete clean;
    printf("  Distinct sensor ticks: %s → %s\n",
           noisy->ToJson(), clean->ToJson());

    /* ═══ 4. UFCS list->GroupBy on List<Pilot*> (pointer T) ════════════════ */
    hr("4 · FACTION BRIEFING  (list->GroupBy via UFCS · T = Pilot*)");

    /* Free GroupBy lives in map.h; UFCS lets us call it as a method.  T is
     * Pilot* — free-fn inference must keep the mangled trailing 'P' so we get
     * GroupBy<Pilot*,int>, not GroupBy<Pilot,int> (that used to JIT-crash). */
    Map<int, List<Pilot*>*>* by_faction = grid->GroupBy(faction_bucket);
    by_faction->ownsValues();   /* buckets die with the map; pilots stay in grid */
    defer delete by_faction;

    printf("  factions online: %d\n", by_faction->Count());
    for (auto bucket, roster in by_faction) {
        printf("\n  ▸ %s division (%d pilots)\n",
               faction_name(bucket), roster->Count());
        roster->Sort(cmp_elo_desc);
        for (auto p in roster) p->banner();
    }

    /* Free form still works — same monomorphization. */
    Map<int, List<Pilot*>*>* again = GroupBy(grid, faction_bucket);
    again->ownsValues();
    defer delete again;
    printf("\n  free GroupBy(grid, …) agrees: %d factions\n", again->Count());

    /* Scalar side-channel: also GroupBy on List<int> (elo tiers). */
    Map<int, List<int>*>* elo_buckets = elos->GroupBy(elo_tier);
    elo_buckets->ownsValues();
    defer delete elo_buckets;
    printf("  elo ladder GroupBy tiers: %d\n", elo_buckets->Count());

    /* ═══ 5. Live scoreboard Map ═══════════════════════════════════════════ */
    hr("5 · SCOREBOARD  (Map GetOrAdd / TryAdd / AddOrUpdate / Where*)");

    Map<String, int>* board = new Map<String, int>();
    defer delete board;

    for (auto p in grid) board->Set(p->callsign, p->elo);

    /* Instant qualifying bump. */
    board->AddOrUpdate("AURORA", 0, bump_elo);
    board->AddOrUpdate("NEWBIE", 1200, bump_elo);   /* insert path */
    printf("  AURORA after bump: %d\n", board->Get("AURORA"));
    printf("  NEWBIE seeded at:  %d\n", board->Get("NEWBIE"));
    printf("  TryAdd AURORA again? %s\n",
           board->TryAdd("AURORA", 9999) ? "inserted" : "blocked (already present)");
    printf("  GetOrAdd fallback for GHOST: %d\n",
           board->GetOrAdd("GHOST", 1000));

    Map<String, int>* ace_board = board->Where(score_is_ace);
    defer delete ace_board;
    printf("  ace board (elo ≥ 1800): %s\n", ace_board->ToJson());
    printf("  ContainsKey(HEXFIRE)? %s · ContainsValue(1000)? %s\n",
           board->ContainsKey("HEXFIRE") ? "yes" : "no",
           board->ContainsValue(1000) ? "yes" : "no");

    /* Throwing Get + soft paths. */
    {
        int threw = 0;
        try {
            int ghost = board->Get("NEVER");
            (void)ghost;
        } catch (e) {
            threw = 1;
            printf("  Get(\"NEVER\") → KeyException: %s\n", e.msg);
        }
        printf("  safe: GetOr(\"NEVER\", -1) = %d · threw=%d\n",
               board->GetOr("NEVER", -1), threw);
    }

    /* ═══ 6. Map higher-order generics ═════════════════════════════════════ */
    hr("6 · MAP LINQ  (SelectValues · SelectKeys · GroupBy tiers)");

    Map<String, int>* doubled = board->SelectValues<int>(score_double);
    defer delete doubled;
    printf("  SelectValues(*2) sample JSON:\n    %s\n", doubled->to_string());

    Map<int, int>* coded = board->SelectKeys<int>(score_key_code);
    defer delete coded;
    printf("  SelectKeys(first-char code) count=%d (keys are ints → decimal JSON)\n",
           coded->Count());
    printf("    %s\n", coded->ToJson());

    Map<int, List<int>*>* tiers = board->GroupBy<int>(score_tier);
    tiers->ownsValues();
    defer delete tiers;

    printf("  elo tiers (0=C … 3=S):\n");
    for (auto tier, bucket in tiers) {
        const char* label = "C";
        if (tier == 1) label = "B";
        else if (tier == 2) label = "A";
        else if (tier == 3) label = "S";
        printf("    tier %s: %d pilots  scores=%s\n",
               label, bucket->Count(), bucket->ToJson());
    }

    /* Nested generics Keys()/Values(). */
    List<String>* names = board->Keys();
    defer delete names;
    List<int>* values = board->Values();
    defer delete values;
    printf("  Keys().Count=%d  Values().Count=%d\n",
           names->Count(), values->Count());

    /* ═══ 7. Fastest lap board + ownership demo ════════════════════════════ */
    hr("7 · TELEMETRY ARCHIVE  (Map<String,Pilot*>.ownsValues on a slice)");

    /* Snapshot of top-3 by lap time into an *owning* map of clones?
     * Instead: non-owning pointers into `grid` (one owner rule). */
    List<Pilot*>* by_pace = grid->Copy();  /* shallow copy of pointers */
    defer delete by_pace;
    by_pace->Sort(cmp_best_ms);

    Map<String, Pilot*>* podium = new Map<String, Pilot*>();
    defer delete podium;
    for (int i = 0; i < 3 && i < by_pace->Count(); i++) {
        Pilot* p = by_pace->Get(i);
        podium->Set(p->callsign, p);
        printf("  podium #%d  %s  %d.%03ds\n",
               i + 1, p->callsign, p->best_ms / 1000, p->best_ms % 1000);
    }
    printf("  podium for-in:\n");
    for (auto sign, p in podium) {
        printf("    [%s] faction=%s elo=%d\n", sign, p->faction, p->elo);
    }

    /* ═══ 8. JSON payload for the broadcast uplink ═════════════════════════ */
    hr("8 · UPLINK  (ToJson / f-strings / nameof)");

    String heat_json = full_race->ToJson();
    String board_json = ace_board->ToJson();
    printf("  heat laps json:  %s\n", heat_json);
    printf("  ace board json:  %s\n", board_json);
    printf("  f-string feed:   %s\n",
           f"NEON GRID · {grid->Count()} pilots · {aces->Count()} aces · polesitter {(pole ? pole->callsign : (String)\"?\")}");

    printf("\n  type intel:  nameof<Pilot>() would be open; concrete: List of %s, Map keys typeof sample int* = %s\n",
           "Pilot*", typeof<int*>());

    /* ═══ Curtain call — dtors fire via defer (grid owns all pilots) ═══════ */
    hr("SHUTDOWN  (defer LIFO · owning List reclaims every Pilot)");
    printf("  dropping scope… watch ~Pilot for each roster seat:\n");
    return 0;
}
