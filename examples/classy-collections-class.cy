/* classy-collections-class.cy — List<T> and Set<T> over a custom class
 *
 * A small music-library manager demonstrating TWO memory management patterns:
 *
 *   PATTERN 1: List<Track> (by-value)
 *     · Track instances are COPIED into the list
 *     · List OWNS the copies via __destroy intrinsic
 *     · `delete list` automatically runs ~Track() on each element
 *     · NO manual cleanup loop needed ✅
 *
 *   PATTERN 2: List<Track*> (pointers)
 *     · Track pointers are stored; objects live elsewhere
 *     · List does NOT own the pointed-to objects
 *     · __destroy does nothing for pointer types
 *     · YOU must delete each Track* manually ⚠️
 *
 * When to use each:
 *   - By-value (List<Track>): when list exclusively owns the data
 *   - Pointers (List<Track*>): when sharing objects across collections,
 *     or when object identity/mutability through multiple references matters
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

    /* Destructor: shows when objects are freed.
     * For List<Track> (by-value), this runs automatically via __destroy.
     * For List<Track*> (pointers), you must call `delete t` manually. */
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

void print_list_byval(String label, List<Track>* lst) {
    printf("%s (%d):\n", label, lst->Count());
    for (auto t in lst) t.show();
}

void print_list_ptr(String label, List<Track*>* lst) {
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
    printf("  ClassyC collections: auto-cleanup vs manual pointer management\n");
    printf("═══════════════════════════════════════════════════════════════════\n\n");

    /* ═══════════════════════════════════════════════════════════════════════
     * PATTERN 1: List<Track> — by-value storage with AUTO cleanup
     * ═══════════════════════════════════════════════════════════════════════
     *
     * Each Track is COPIED into the list. The list OWNS these copies.
     * When you `delete playlist`, the ~List() destructor calls __destroy on
     * each element, which runs ~Track() automatically — no manual loop needed.
     */
    printf("──────────────────────────────────────────────────────────────────\n");
    printf("PATTERN 1: List<Track> (by-value) — auto cleanup via __destroy\n");
    printf("──────────────────────────────────────────────────────────────────\n\n");

    {
        List<Track>* playlist = new List<Track>();
        defer delete playlist;  // ← This is ALL you need! No manual loop.

        /* Add by-value: each Track is COPIED into the list's backing array. */
        playlist->Add(Track(1, "Bohemian Rhapsody", "Queen", 354));
        playlist->Add(Track(2, "Blackbird", "The Beatles", 138));
        playlist->Add(Track(3, "Time", "Pink Floyd", 413));

        print_list_byval("Playlist (by-value copies)", playlist);

        /* Filter creates a NEW list with COPIES of matching tracks.
         * Both playlist and filtered are heap lists you must delete,
         * but both auto-cleanup their Track elements via __destroy. */
        auto filtered = playlist->Filter((Track t) => t.duration() > 200);
        defer delete filtered;

        print_list_byval("\nFiltered (> 3:20)", filtered);

        printf("\n--- Scope exit: defer delete runs ~List(), which calls\n");
        printf("    __destroy on each Track element (runs ~Track automatically) ---\n\n");
    }
    /* ← Both playlist and filtered deleted here.
     *   Watch the ~Track() output above show automatic cleanup. */

    printf("\n");

    /* ═══════════════════════════════════════════════════════════════════════
     * PATTERN 2: List<Track*> — pointer storage with MANUAL cleanup
     * ═══════════════════════════════════════════════════════════════════════
     *
     * When you need to SHARE Track objects across multiple collections
     * (e.g., the same Track* in library, favorites, and recent), use pointers.
     * The collections store REFERENCES, not copies. Object identity is preserved.
     *
     * BUT: __destroy does NOTHING for pointer types (T* has no destructor).
     * You must manually delete each Track* yourself before deleting the list.
     */
    printf("──────────────────────────────────────────────────────────────────\n");
    printf("PATTERN 2: List<Track*> (pointers) — manual cleanup required\n");
    printf("──────────────────────────────────────────────────────────────────\n\n");

    /* Create Track objects once; we'll share pointers across collections. */
    Track* t_queen   = new Track(10, "Bohemian Rhapsody", "Queen",        354);
    Track* t_beatles = new Track(11, "Blackbird",         "The Beatles",  138);
    Track* t_floyd   = new Track(12, "Time",              "Pink Floyd",   413);
    Track* t_bowie   = new Track(13, "Heroes",            "David Bowie",  371);
    Track* t_zep     = new Track(14, "Kashmir",           "Led Zeppelin", 508);
    Track* t_dylan   = new Track(15, "Hurricane",         "Bob Dylan",    211);

    /* ── List<Track*> : ordered library storing POINTERS ─────────────────── */
    auto library = new List<Track*>();
    defer delete library;  // ← deletes the LIST, NOT the Track objects

    library->Add(t_queen);
    library->Add(t_beatles);
    library->Add(t_floyd);
    library->Add(t_bowie);
    library->Add(t_zep);
    library->Add(t_dylan);

    print_list_ptr("Library (insertion order, shared pointers)", library);

    /* Sort the library with typed lambdas over the custom class pointer. */
    library->Sort((Track* a, Track* b) => strcmp(a->title, b->title));
    print_list_ptr("\nSorted by title", library);

    library->Sort((Track* a, Track* b) => a->duration() - b->duration());
    printf("\nShortest track: ");  library->First()->show();
    printf("Longest track:  ");    library->Last()->show();

    /* Filter: returns a new list of POINTERS to the SAME Track objects.
     * The epics list shares ownership — we still only delete Track* once. */
    auto epics = library->Filter((Track* t) => t->duration() > 360);
    defer delete epics;  // ← deletes the list, NOT the Track objects
    print_list_ptr("\nEpics (> 6:00)", epics);

    /* ── Set<Track*> : identity sets sharing the same Track* objects ──────── */
    auto favorites = new Set<Track*>();
    auto recent    = new Set<Track*>();
    defer delete favorites;  // ← these delete the SETS, not the Tracks
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

    /* Set algebra over object POINTERS (identity-based). */
    auto loved_and_fresh = favorites->Intersect(recent);   /* favourite AND recent */
    auto neglected       = favorites->Difference(recent);  /* favourite, not recent */
    auto active          = favorites->Union(recent);       /* either                */
    defer delete loved_and_fresh;  // ← these delete the RESULT SETS
    defer delete neglected;
    defer delete active;

    printf("\n");
    print_set("Loved & freshly played (favorites ∩ recent)", loved_and_fresh);
    print_set("Neglected favorites (favorites − recent)",    neglected);
    printf("Active rotation (favorites ∪ recent): %d tracks\n", active->Count());

    /* ── MANUAL cleanup: YOU must delete each Track* object ──────────────── */
    printf("\n--- Manual cleanup: delete each Track* before collections die ---\n\n");

    /* Why loop through library? Because it holds ALL Track* objects.
     * The other collections (favorites, recent, epics, etc.) share these
     * same pointers — we delete each object ONCE. */
    for (auto t in library) delete t;

    /* Now the `defer delete` statements above clean up the LIST/SET containers,
     * but the Track objects are already freed. If we had used List<Track>
     * (by-value), this manual loop would be UNNECESSARY — __destroy would
     * handle it automatically. */

    printf("\nDone.\n");
    printf("═══════════════════════════════════════════════════════════════════\n");
    printf("SUMMARY:\n");
    printf("  List<Track>  → auto cleanup via __destroy (no manual loop) ✅\n");
    printf("  List<Track*> → manual cleanup required (you delete each T*) ⚠️\n");
    printf("═══════════════════════════════════════════════════════════════════\n");
    return 0;
}
