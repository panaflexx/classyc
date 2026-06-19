/* classy-collections-class.cy — List<T> and Set<T> over a custom class
 *
 * A small music-library manager that stores a user-defined `Track` class in
 * both a generic List<T> (ordered playlist) and generic Set<T>s (reference
 * sets: favourites, recently played), then does real work with them:
 *
 *   List<Track*>   ordered library; for-in, Sort (typed lambdas), Filter
 *   Set<Track*>    identity sets; Add/Contains + Union/Intersect/Difference
 *
 * Note on `Track*` vs `Track`: in ClassyC a `class` is a reference type — you
 * create instances with `new` and hand the collections the resulting pointer.
 * So the element type is `Track*`, and the Set deduplicates / intersects by
 * object identity (exactly what "favourites" and "recently played" want).
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
    ~Track() {}

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
    printf("=== ClassyC music library: List<Track*> + Set<Track*> ===\n\n");

    /* Create the tracks once; keep handles so we can put the *same* objects
     * into several collections. */
    Track* t_queen   = new Track(1, "Bohemian Rhapsody", "Queen",        354);
    Track* t_beatles = new Track(2, "Blackbird",         "The Beatles",  138);
    Track* t_floyd   = new Track(3, "Time",              "Pink Floyd",   413);
    Track* t_bowie   = new Track(4, "Heroes",            "David Bowie",  371);
    Track* t_zep     = new Track(5, "Kashmir",           "Led Zeppelin", 508);
    Track* t_dylan   = new Track(6, "Hurricane",         "Bob Dylan",    211);

    /* ── List<Track*> : the ordered library ─────────────────────────────── */
    auto library = new List<Track*>();
    defer delete library;
    library->Add(t_queen);
    library->Add(t_beatles);
    library->Add(t_floyd);
    library->Add(t_bowie);
    library->Add(t_zep);
    library->Add(t_dylan);

    print_list("Library (insertion order)", library);

    /* Sort the library with typed lambdas over the custom class pointer. */
    library->Sort((Track* a, Track* b) => strcmp(a->title, b->title));
    print_list("\nSorted by title", library);

    library->Sort((Track* a, Track* b) => a->duration() - b->duration());
    printf("\nShortest track: ");  library->First()->show();
    printf("Longest track:  ");    library->Last()->show();

    /* Filter: everything longer than six minutes (returns a new heap list). */
    auto epics = library->Filter((Track* t) => t->duration() > 360);
    defer delete epics;
    print_list("\nEpics (> 6:00)", epics);

    /* ── Set<Track*> : identity sets ────────────────────────────────────── */
    auto favorites = new Set<Track*>();
    auto recent    = new Set<Track*>();
    defer delete favorites;
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

    /* Set algebra over object references. */
    auto loved_and_fresh = favorites->Intersect(recent);   /* favourite AND recent */
    auto neglected       = favorites->Difference(recent);  /* favourite, not recent */
    auto active          = favorites->Union(recent);       /* either                */
    defer delete loved_and_fresh;
    defer delete neglected;
    defer delete active;

    printf("\n");
    print_set("Loved & freshly played (favorites ∩ recent)", loved_and_fresh);
    print_set("Neglected favorites (favorites − recent)",    neglected);
    printf("Active rotation (favorites ∪ recent): %d tracks\n", active->Count());

    /* ── Cleanup: free the Track objects, then defers free the containers ── */
    for (auto t in library) delete t;

    printf("\nDone.\n");
    return 0;
}
