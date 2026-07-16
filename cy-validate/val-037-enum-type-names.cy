/* val-037-enum-type-names.cy — bare named-enum types as fields/locals/returns.
 *
 * ClassyC now registers named enums like classes:
 *   · Faction fac;            // class field (not just enum Faction fac)
 *   · Faction f = nova;       // local / parameter / return type
 *   · enum Faction f = x;     // classic form still works outside main
 *   · (Faction)bucket         // cast to named enum
 *   · f.nameof() / nameof<Faction>()
 *
 * Run:  ./bin/classyc -g -I include cy-validate/val-037-enum-type-names.cy -eg
 */
#include <stdio.h>
#include <string.h>
#include "list.h"
#include "map.h"

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

enum Faction { nova = 0, ember = 1, voids = 2 };
enum EloTier { C = 0, B = 1, A = 2, S = 3 };

class Pilot {
    int      id;
    String   callsign;
    Faction  faction;   /* bare named-enum field */
    int      elo;

    Pilot(int id, String callsign, Faction faction, int elo) {
        this->id = id;
        this->callsign = callsign;
        this->faction = faction;
        this->elo = elo;
    }
    ~Pilot() {}

    Faction GetFaction() { return this->faction; }
    EloTier Tier() {
        if (elo >= 2000) return S;
        if (elo >= 1800) return A;
        if (elo >= 1500) return B;
        return C;
    }
};

/* Free-fn local `enum Faction f` + bare cast + return type. */
const char* faction_label(int bucket) {
    enum Faction f = (Faction)bucket;
    return f.nameof();
}

Faction identity_faction(Faction f) { return f; }

int faction_key(Pilot* p) { return (int)p->GetFaction(); }

int main() {
    printf("=== val-037 named enum types ===\n\n");

    printf("-- type-level --\n");
    check(strcmp(nameof<Faction>(), "Faction") == 0, "1a  nameof<Faction>()");
    check(strcmp(nameof<EloTier>(), "EloTier") == 0, "1b  nameof<EloTier>()");
    check(strcmp(nova.nameof(), "nova") == 0,        "1c  nova.nameof()");
    check(strcmp(S.typeof(), "EloTier") == 0,        "1d  S.typeof()");

    printf("\n-- class field + accessors --\n");
    owned auto grid = new List<Pilot*>().owns();
    grid->Add(new Pilot(1, "AURORA", nova, 2140));
    grid->Add(new Pilot(2, "HEXFIRE", ember, 1912));
    grid->Add(new Pilot(3, "NULLCAT", voids, 1600));

    Pilot* p0 = grid->Get(0);
    Faction f0 = p0->GetFaction();
    EloTier t0 = p0->Tier();
    check(strcmp(f0.nameof(), "nova") == 0,          "2a  field accessor nameof");
    check(strcmp(t0.nameof(), "S") == 0,             "2b  method EloTier nameof");
    check((int)f0 == 0 && (int)p0->GetFaction() == 0,"2c  enum compares as int");

    printf("\n-- locals outside main (free fn) --\n");
    check(strcmp(faction_label(1), "ember") == 0,    "3a  free-fn enum local + cast");
    Faction id_f = identity_faction(voids);
    check(strcmp(id_f.nameof(), "voids") == 0,       "3b  bare Faction param/return");

    printf("\n-- GroupBy keys via named enum --\n");
    auto roster = grid->Copy();  /* Copy returns List by value */
    auto by = roster.GroupBy(faction_key);  /* value Map shell */
    check(by.Count() == 3,                          "4a  three faction buckets");
    {
        enum Faction nf = (Faction)0;
        check(strcmp(nf.nameof(), "nova") == 0,      "4b  classic enum local + cast");
        List<Pilot*>* nova_list = by.GetOr(0, NULL);
        check(nova_list != NULL && nova_list->Count() == 1, "4c  nova bucket");
    }

    printf("\n-- reassignment reverse-map --\n");
    enum Faction walk = ember;
    check(strcmp(walk.nameof(), "ember") == 0,       "5a  walk.nameof() ember");
    walk = voids;
    check(strcmp(walk.nameof(), "voids") == 0,       "5b  reassigned walk.nameof()");
    Faction bare = walk;
    check(strcmp(bare.nameof(), "voids") == 0,       "5c  bare Faction local");
    check(strcmp(bare.typeof(), "Faction") == 0,     "5d  bare.typeof()");

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
