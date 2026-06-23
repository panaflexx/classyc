/* classy-collections-class.cy — List<T> and Set<T> over a custom class
 *
 * A small music-library manager demonstrating ClassyC's ownership protocol for
 * pointer collections. The footgun this used to have — "remember to manually
 * loop and delete every element before deleting the container" — is gone:
 *
 *   OWNING collection   → new List<Track*>().owns()
 *       · the collection owns the pointed-to objects
 *       · `delete list` (or `defer delete`) runs ~Track() on each element,
 *         then frees them — no manual loop, no leaks ✅
 *
 *   NON-OWNING collection → new List<Track*>()   (the default)
 *       · stores shared references to objects owned elsewhere
 *       · `delete list` frees only the container, never the elements
 *       · perfect for views / sub-sets that alias an owning collection
 *
 * The rule of thumb: exactly ONE collection owns each object. Here `library`
 * owns every Track; favorites/recent/epics/etc. are non-owning views that share
 * library's pointers. Deleting them is always safe and never double-frees.
 *
 * Under the hood this is powered by the `is_pointer<T>` compiler intrinsic plus
 * an `_owns_ptrs` flag in the collection; see include/list.h / set.h / map.h.
 *
 * Usage:  classyc examples/classy-collections-class.cy -eg
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "include/list.h"
#include "include/set.h"

/* ───────────────────────────── The custom class ───────────────────────── */

class Track {
    int    id;
    String title;
    String artist;
    int    seconds;

    Track(int id, String title, String artist, int seconds) {
        this->id = id; this->title = title; this->artist = artist; this->seconds = seconds;
    }

    /* Destructor: prints so you can watch the owning collection reclaim each
     * Track automatically at scope exit. */
    ~Track() {
        printf("      ~Track(%d) freed\n", this->id);
    }

    int duration() { return this->seconds; }

    void show() {
        printf("   [%2d] %-26s %-16s %d:%02d\n",
               this->id, this->title, this->artist,
               this->seconds / 60, this->seconds % 60);
    }
};

/* ───────────────────────────── Pretty-printers ─────────────────────────── */

void print_list(String label, List<Track*>* lst) {
    printf("%s (%d):\n", label, lst->Count());
    for (auto t in lst) t->show();
}

void print_set(String label, Set<Track*>* s) {
    printf("%s (%d):\n", label, s->Count());
    for (auto t in s) t->show();
}

/* ───────────────────────────────── main ───────────────────────────────── */

int main() {
    printf("═══════════════════════════════════════════════════════════════════\n");
    printf("  ClassyC music library: ownership via .owns() — no manual cleanup\n");
    printf("═══════════════════════════════════════════════════════════════════\n\n");

    /* ── The OWNING collection ───────────────────────────────────────────
     * `library` owns every Track. `.owns()` flags the list so that deleting it
     * runs each Track's destructor and frees it. This is the ONLY collection
     * that owns these objects. */
    auto library = new List<Track*>().owns();
    defer delete library;   // ← deletes the list AND every Track in it ✅

    library->Add(new Track(1, "Bohemian Rhapsody", "Queen",        354));
    library->Add(new Track(2, "Blackbird",         "The Beatles",  138));
    library->Add(new Track(3, "Time",              "Pink Floyd",   413));
    library->Add(new Track(4, "Heroes",            "David Bowie",  371));
    library->Add(new Track(5, "Kashmir",           "Led Zeppelin", 508));
    library->Add(new Track(6, "Hurricane",         "Bob Dylan",    211));

    print_list("Library (insertion order)", library);

    /* Sort the library with typed lambdas over the custom class pointer. */
    library->Sort((Track* a, Track* b) => strcmp(a->title, b->title));
    print_list("\nSorted by title", library);

    library->Sort((Track* a, Track* b) => a->duration() - b->duration());
    printf("\nShortest track: ");  library->First()->show();
    printf("Longest track:  ");    library->Last()->show();

    /* Grab a few shared handles for building views below. */
    Track* t_queen   = library->Get(0);   /* (after sort, order differs — by id lookups would be nicer, */
    Track* t_floyd   = library->Get(1);   /*  but any handles work for demonstrating sharing) */
    Track* t_zep     = library->Get(2);
    Track* t_beatles = library->Get(3);

    /* ── NON-OWNING views: Filter returns a list that SHARES library's pointers.
     * It is non-owning by default, so deleting it frees only the container —
     * the Track objects keep living under `library`'s ownership. */
    auto epics = library->Filter((Track* t) => t->duration() > 360);
    defer delete epics;     // ← frees the view's container only (no Track dtors)
    print_list("\nEpics (> 6:00)", epics);

    /* ── Set<Track*> views: identity sets that also SHARE library's pointers ─ */
    auto favorites = new Set<Track*>();   // non-owning (default)
    auto recent    = new Set<Track*>();   // non-owning (default)
    defer delete favorites;               // ← container-only frees, safe
    defer delete recent;

    favorites->Add(t_queen);
    favorites->Add(t_floyd);
    favorites->Add(t_zep);
    favorites->Add(t_zep);      /* duplicate — the set keeps one */

    recent->Add(t_beatles);
    recent->Add(t_floyd);
    recent->Add(t_zep);

    printf("\nfavorites has %d unique tracks; contains 'Blackbird'? %s\n",
           favorites->Count(),
           favorites->Contains(t_beatles) ? "yes" : "no");

    /* Set algebra returns more non-owning views over the same shared pointers. */
    auto loved_and_fresh = favorites->Intersect(recent);   /* favourite AND recent */
    auto neglected       = favorites->Difference(recent);  /* favourite, not recent */
    auto active          = favorites->Union(recent);       /* either                */
    defer delete loved_and_fresh;   // ← all container-only frees
    defer delete neglected;
    defer delete active;

    printf("\n");
    print_set("Loved & freshly played (favorites ∩ recent)", loved_and_fresh);
    print_set("Neglected favorites (favorites − recent)",    neglected);
    printf("Active rotation (favorites ∪ recent): %d tracks\n", active->Count());

    /* ── No manual cleanup loop! ─────────────────────────────────────────
     * At scope exit the `defer delete`s run in reverse order: the non-owning
     * views (active, neglected, loved_and_fresh, recent, favorites, epics) each
     * free just their container, and finally `library` — the sole owner — runs
     * every ~Track() and frees the objects. Exactly one free per object. */
    printf("\n--- Scope exit: deferred deletes run now (watch ~Track only once each) ---\n\n");

    printf("Done.\n");
    printf("═══════════════════════════════════════════════════════════════════\n");
    printf("OWNERSHIP MODEL:\n");
    printf("  new List<Track*>().owns()  → owns elements; delete frees them ✅\n");
    printf("  new List<Track*>()         → non-owning view; delete frees container only\n");
    printf("  new List<Track>()          → by-value; __destroy runs ~Track automatically\n");
    printf("\n");
    printf("  Rule: exactly ONE owner per object. Views share, the owner frees.\n");
    printf("═══════════════════════════════════════════════════════════════════\n");
    return 0;
}
