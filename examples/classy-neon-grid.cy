/* classy-neon-grid.cy — NEON GRID race telemetry command center
 *
 * Showcase of List/Map ergonomics after CLASSYC-CLEANUP:
 *
 *   List  · Range / Plus / Slice / Distinct / Sort
 *         · Where / Any / All / Find
 *         · Select<U> · list->GroupBy (UFCS) · owns() · ToJson
 *   Map   · TryAdd / GetOrAdd / AddOrUpdate / ContainsKey
 *         · Where / SelectValues / SelectKeys / GroupBy
 *         · Keys() / Values() · ownsValues() · ToJson
 *   Style · Pilot methods + typed lambdas
 *         · named enums as fields + nameof/typeof reverse-map
 *         · owned auto cleanup instead of defer delete
 *         · f-strings, throwing Get + try-catch
 *
 * Usage (from project root):
 *   ./bin/classyc -I include examples/classy-neon-grid.cy -eg
 */

#include <stdio.h>
#include <string.h>
#include "map.h"
#include "list.h"

/* ───────────────────────── Domain ───────────────────────── */

/* Named enums register as bare type names (like classes): Faction field,
 * Faction param/return, (Faction)cast, and enum Faction locals all work. */
enum Faction { nova = 0, ember = 1, voids = 2 };
enum EloTier { C = 0, B = 1, A = 2, S = 3 };

EloTier RankOf(int elo) {
    if (elo >= 2000) return S;
    if (elo >= 1800) return A;
    if (elo >= 1500) return B;
    return C;
}

class Pilot {
    int      id;
    String   callsign;
    Faction  faction;
    int      elo;
    int      best_ms;   /* best lap, milliseconds */

    Pilot(int id, String callsign, Faction faction, int elo, int best_ms) {
        this->id = id;
        this->callsign = callsign;
        this->faction = faction;
        this->elo = elo;
        this->best_ms = best_ms;
    }

    ~Pilot() {
        printf("      ~Pilot #%d %s decommissioned\n", id, callsign);
    }

    int      Id()         { return id; }
    String   Callsign()   { return callsign; }
    Faction  FactionOf()  { return faction; }
    int      Elo()        { return elo; }
    int      BestMs()     { return best_ms; }

    int IsAce()  { return elo >= 1800; }
    int IsFast() { return best_ms < 62000; }
    EloTier Tier() { return RankOf(elo); }

    /* GroupBy key as int (Map monomorphization is int-friendly). */
    int FactionKey() { return (int)faction; }

    void Banner() {
        Faction f = faction;
        printf("   ▶  #%02d  %-12s  %-6s  elo=%4d  best=%d.%03ds\n",
               id, callsign, ((String)f.nameof()).upper(),
               elo, best_ms / 1000, best_ms % 1000);
    }
};

void hr(const char* title) {
    printf("\n════════════════════════════════════════════════════════════\n");
    printf("  %s\n", title);
    printf("════════════════════════════════════════════════════════════\n");
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
    printf("   reflection: nameof<%s>()=%s · typeof<%s>()=%s · %s.nameof()=%s · %s.typeof()=%s\n",
           "int", nameof<int>(), "Pilot*", typeof<Pilot*>(),
           nameof(S), S.nameof(), nameof(nova), nova.typeof());

    /* Enum variable reverse-map (val-030 / val-037). */
    enum Faction demo_fac = ember;
    EloTier demo_tier = A;
    printf("   enum vars:  %s.nameof()=%s  → reassigned ",
           nameof(demo_fac), demo_fac.nameof());
    demo_fac = voids;
    printf("%s · %s.nameof()=%s\n",
           demo_fac.nameof(), nameof(demo_tier), demo_tier.nameof());

    /* ═══ 1. Own the grid ══════════════════════════════════════════════════ */
    hr("1 · ROSTER  (List<Pilot*>.owns — reclaimed by owned auto)");

    owned auto grid = new List<Pilot*>().owns();

    grid->Add(new Pilot(1,  "AURORA",   nova,  2140, 59840));
    grid->Add(new Pilot(2,  "HEXFIRE",  ember, 1912, 61220));
    grid->Add(new Pilot(3,  "NULLCAT",  voids, 1766, 63110));
    grid->Add(new Pilot(4,  "KITE",     nova,  1630, 64005));
    grid->Add(new Pilot(5,  "RIVEN",    ember, 2055, 60112));
    grid->Add(new Pilot(6,  "GLITCH",   voids, 1488, 67200));
    grid->Add(new Pilot(7,  "SOLACE",   nova,  1880, 61550));
    grid->Add(new Pilot(8,  "EMBERX",   ember, 1520, 65890));

    printf("  Pilots on the grid: %d  · domain enums: %s / %s\n",
           grid->Count(), nameof<Faction>(), nameof<EloTier>());
    printf("  constants: %s.nameof()=%s  %s.nameof()=%s  free nameof(%s)=%s\n",
           nameof(nova), nova.nameof(),
           nameof(S), S.nameof(),
           nameof(ember), nameof(ember));
    for (auto p in grid) p->Banner();

    /* Shallow non-owning snapshot for Select/GroupBy. Keeps `grid` as the sole
     * Pilot owner so owned auto still reclaims every seat at scope exit. */
    owned auto roster = grid->Copy();

    /* ═══ 2. Select / Where / Any / All / Find ═════════════════════════════ */
    hr("2 · PROJECTIONS  (Select<U> · Where · Any/All · Find)");

    owned auto callsigns = roster->Select<String>((Pilot* p) => p->Callsign());
    printf("  callsigns via Select<String>: %s\n", callsigns->ToJson());

    owned auto elos = roster->Select((Pilot* p) => p->Elo());  /* U inferred */
    elos->Sort((int a, int b) => a - b);
    printf("  elo ladder (sorted): %s\n", elos->ToJson());

    owned auto aces = grid->Where((Pilot* p) => p->IsAce());  /* non-owning view */
    printf("  aces (elo ≥ 1800): %d\n", aces->Count());
    for (auto p in aces) p->Banner();

    printf("  any ace?  %s\n", grid->Any((Pilot* p) => p->IsAce()) ? "YES" : "no");
    printf("  all live? %s\n", grid->All((Pilot* p) => p != NULL)  ? "YES" : "no");
    printf("  any sub-62s lap? %s\n",
           grid->Any((Pilot* p) => p->IsFast()) ? "YES — someone is cooking" : "nope");

    Pilot* pole = grid->Find((Pilot* p) => p->IsFast());
    if (pole) {
        Faction pf = pole->FactionOf();
        EloTier pt = pole->Tier();
        printf("  first sub-62s pilot found: %s (%d ms) · faction=%s · tier=%s\n",
               pole->Callsign(), pole->BestMs(), pf.nameof(), pt.nameof());
    }

    /* ═══ 3. Range · Plus · Slice · Distinct ══════════════════════════════ */
    hr("3 · LAP WINDOWS  (Range · Plus · Slice · Distinct)");

    owned auto heat_a = List<int>.Range(1, 5);   /* laps 1..5  */
    owned auto heat_b = List<int>.Range(6, 5);   /* laps 6..10 */

    owned auto full_race = heat_a->Plus(heat_b);
    printf("  heat A:     %s  (untouched by Plus)\n", heat_a->ToJson());
    printf("  heat B:     %s\n", heat_b->ToJson());
    printf("  full race:  %s  via heat_a->Plus(heat_b)\n", full_race->ToJson());

    owned auto midfield = full_race->Slice(3, 4);  /* laps 4..7 */
    printf("  midfield window Slice(3,4): %s\n", midfield->ToJson());

    owned auto noisy = new List<int>{1, 2, 2, 3, 3, 3, 4};
    owned auto clean = noisy->Distinct();
    printf("  Distinct sensor ticks: %s → %s\n",
           noisy->ToJson(), clean->ToJson());

    /* ═══ 4. list->GroupBy (UFCS, T = Pilot*) ══════════════════════════════ */
    hr("4 · FACTION BRIEFING  (list->GroupBy · Faction enum · nameof)");

    owned auto by_faction = roster->GroupBy((Pilot* p) => p->FactionKey());

    printf("  factions online: %d  (bucket keys are %s values)\n",
           by_faction->Count(), nameof<Faction>());
    for (auto bucket, group in by_faction) {
        enum Faction fac = (Faction)bucket;
        printf("\n  ▸ %s division (%d pilots)\n",
               ((String)fac.nameof()).upper(), group->Count());
        group->Sort((Pilot* a, Pilot* b) => b->Elo() - a->Elo());
        for (auto p in group) p->Banner();
    }

    owned auto again = GroupBy(roster, (Pilot* p) => p->FactionKey());
    printf("\n  free GroupBy(roster, …) agrees: %d factions\n", again->Count());

    owned auto elo_buckets = elos->GroupBy((int e) => (int)RankOf(e));
    printf("  elo ladder GroupBy tiers (%s):", nameof<EloTier>());
    for (auto tier, scores in elo_buckets) {
        EloTier t = (EloTier)tier;
        printf("  %s=%d", t.nameof(), scores->Count());
    }
    printf("\n");

    /* ═══ 5. Live scoreboard Map ═══════════════════════════════════════════ */
    hr("5 · SCOREBOARD  (Map GetOrAdd / TryAdd / AddOrUpdate / Where*)");

    owned auto board = new Map<String, int>();
    for (auto p in grid) board->Set(p->Callsign(), p->Elo());

    board->AddOrUpdate("AURORA", 0, (int v) => v + 25);
    board->AddOrUpdate("NEWBIE", 1200, (int v) => v + 25);
    printf("  AURORA after bump: %d\n", board->Get("AURORA"));
    printf("  NEWBIE seeded at:  %d\n", board->Get("NEWBIE"));
    printf("  TryAdd AURORA again? %s\n",
           board->TryAdd("AURORA", 9999) ? "inserted" : "blocked (already present)");
    printf("  GetOrAdd fallback for GHOST: %d\n",
           board->GetOrAdd("GHOST", 1000));

    owned auto ace_board = board->Where((String k, int v) => {
        (void)k;
        return v >= 1800;
    });
    printf("  ace board (elo ≥ 1800): %s\n", ace_board->ToJson());
    printf("  ContainsKey(HEXFIRE)? %s · ContainsValue(1000)? %s\n",
           board->ContainsKey("HEXFIRE") ? "yes" : "no",
           board->ContainsValue(1000) ? "yes" : "no");

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
    hr("6 · MAP LINQ  (SelectValues · SelectKeys · GroupBy EloTier nameof)");

    owned auto doubled = board->SelectValues<int>((String k, int v) => {
        (void)k;
        return v * 2;
    });
    printf("  SelectValues(*2) sample JSON:\n    %s\n", doubled->to_string());

    owned auto coded = board->SelectKeys<int>((String k, int v) => {
        (void)v;
        const char* s = (const char*)k;
        return (s && s[0]) ? (int)s[0] : 0;
    });
    printf("  SelectKeys(first-char code) count=%d (keys are ints → decimal JSON)\n",
           coded->Count());
    printf("    %s\n", coded->ToJson());

    owned auto tiers = board->GroupBy((String k, int v) => {
        (void)k;
        return (int)RankOf(v);
    });

    printf("  elo tiers via %s nameof (C…S):\n", nameof<EloTier>());
    for (auto tier, scores in tiers) {
        EloTier t = (EloTier)tier;
        printf("    tier %s: %d pilots  scores=%s\n",
               t.nameof(), scores->Count(), scores->ToJson());
    }

    owned auto names = board->Keys();
    owned auto values = board->Values();
    printf("  Keys().Count=%d  Values().Count=%d\n",
           names->Count(), values->Count());

    /* ═══ 7. Fastest lap podium ════════════════════════════════════════════ */
    hr("7 · TELEMETRY ARCHIVE  (Map<String,Pilot*> podium — non-owning refs)");

    owned auto by_pace = grid->Copy();
    by_pace->Sort((Pilot* a, Pilot* b) => a->BestMs() - b->BestMs());

    owned auto podium = new Map<String, Pilot*>();
    for (int i = 0; i < 3 && i < by_pace->Count(); i++) {
        Pilot* p = by_pace->Get(i);
        podium->Set(p->Callsign(), p);
        Faction fac = p->FactionOf();
        EloTier tier = p->Tier();
        printf("  podium #%d  %s  %d.%03ds  [%s / tier %s]\n",
               i + 1, p->Callsign(), p->BestMs() / 1000, p->BestMs() % 1000,
               ((String)fac.nameof()).upper(), tier.nameof());
    }
    printf("  podium for-in:\n");
    for (auto sign, p in podium) {
        Faction fac = p->FactionOf();
        printf("    [%s] faction=%s elo=%d\n", sign, fac.nameof(), p->Elo());
    }

    /* ═══ 8. Broadcast uplink ══════════════════════════════════════════════ */
    hr("8 · UPLINK  (ToJson / f-strings / nameof)");

    String heat_json = full_race->ToJson();
    String board_json = ace_board->ToJson();
    printf("  heat laps json:  %s\n", heat_json);
    printf("  ace board json:  %s\n", board_json);
    printf("  f-string feed:   %s\n",
           f"NEON GRID · {grid->Count()} pilots · {aces->Count()} aces · polesitter {(pole ? pole->Callsign() : (String)\"?\")}");

    printf("\n  type intel:  nameof<%s>()=%s · typeof<%s>()=%s · %s.nameof()=%s · free nameof(%s)=%s\n",
           "Pilot", nameof<Pilot>(), "Pilot*", typeof<Pilot*>(),
           nameof(ember), ember.nameof(), nameof(voids), nameof(voids));

    hr("SHUTDOWN  (owned auto · owning List reclaims every Pilot)");
    printf("  dropping scope… watch ~Pilot for each roster seat:\n");
    return 0;
}
