/* examples/readme-taste.cy — the program from the top of README.md
 *
 *   ./bin/classyc -I include examples/readme-taste.cy -eg
 */
#include <stdio.h>
#include "list.h"
#include "map.h"

[[copyable_no_release]]
class Track {
    String title;
    String artist;
    int    seconds;

    Track (String title, String artist, int seconds) {
        this.title = title;
        this.artist = artist;
        this.seconds = seconds;
    }
    ~Track () {}

    int IsLong () { return seconds >= 360; }

    String Label () { return f"{artist} — {title}"; }
};

int main (void) {
    auto crate = List<Track> ();
    crate.Add (Track ("Kashmir", "Led Zeppelin", 508));
    crate.Add (Track ("Africa", "Toto", 295));
    crate.Add (Track ("Tom Sawyer", "Rush", 276));
    crate.Add (Track ("Echoes", "Pink Floyd", 1412));

    auto epics = crate.Where ((Track t) => t.IsLong ());
    printf ("%d tracks, %d of them stretch out\n", crate.Count (), epics.Count ());

    for (auto t in epics)
        printf ("  %s (%ds)\n", t.Label (), t.seconds);

    auto counts = Map<String, int> ();
    for (auto t in crate)
        counts[t.artist] = counts.GetOr (t.artist, 0) + 1;

    dict night = { "mood": "late", "volume": 7 };
    printf ("mood=%s  artists=%d\n", (char *) night.mood, counts.Count ());
    return 0;
}
