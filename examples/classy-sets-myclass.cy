/* classy-sets-myclass.cy — text analytics with a custom class over Set<T>
 *
 * A `WordBag` is a user-defined class that wraps a Set<String> of the unique
 * words found in a text file.  Around it we build genuinely useful, command
 * line-flavoured tools, all expressed as set operations:
 *
 *   sort -u          unique vocabulary, alphabetically  (Set -> List -> Sort)
 *   grep (set form)  which keywords occur in each file   (Set.Contains)
 *   stop-word strip  vocabulary minus a stop list        (Set.Difference)
 *   doc similarity   Jaccard = |A ∩ B| / |A ∪ B|         (Intersect / Union)
 *   diff             words shared / unique to each file  (Intersect/Difference)
 *
 * The files are created in /tmp first, then re-read from disk and processed,
 * so this is real file-in/analysis-out word crunching.
 *
 * Usage:  classyc examples/classy-sets-myclass.cy -eg
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "include/set.h"
#include "include/list.h"
#include "include/file.h"

/* ───────────────────────────── Helpers ───────────────────────────── */

/* Allocate a lowercased, NUL-terminated copy of [s, s+len). */
String intern_word(const char* s, int len) {
    char* w = (char*) malloc(len + 1);
    for (int i = 0; i < len; i++) w[i] = (char) tolower((unsigned char) s[i]);
    w[len] = 0;
    return (String) w;
}

/* Snapshot a Set<String> into a freshly sorted List<String>. Caller deletes. */
List<String>* to_sorted_list(Set<String>* s) {
    List<String>* lst = new List<String>();
    for (int i = 0; i < s->Count(); i++) lst->Add(s->Get(i));
    lst->Sort((String a, String b) => strcmp(a, b));
    return lst;
}

/* Print up to `maxShow` words from a list on one line. */
void print_words(String label, List<String>* words, int maxShow) {
    printf("   %-22s [%2d] ", label, words->Count());
    int n = 0;
    for (auto w in words) {
        if (n >= maxShow) { printf("…"); break; }
        printf("%s ", w);
        n++;
    }
    printf("\n");
}

/* ───────────────────────── The custom class ───────────────────────── */

/* WordBag — the bag of unique words for one document, plus the analytics we
 * can answer with set algebra. */
class WordBag {
    String       name;
    Set<String>* words;   /* unique words (vocabulary)        */
    int          total;   /* total tokens seen (with repeats) */

    WordBag(String name) {
        this->name  = name;
        this->words = new Set<String>();
        this->total = 0;
    }
    ~WordBag() { delete this->words; }

    /* Tokenise a blob of text and fold it into the bag. */
    void Ingest(const char* text) {
        int i = 0;
        while (text[i] != 0) {
            if (isalnum((unsigned char) text[i])) {
                int start = i;
                while (text[i] != 0 && isalnum((unsigned char) text[i])) i++;
                this->total = this->total + 1;
                this->words->Add(intern_word(text + start, i - start));
            } else {
                i++;
            }
        }
    }

    String       Name()   { return this->name; }
    Set<String>* Words()  { return this->words; }
    int          Unique() { return this->words->Count(); }
    int          Total()  { return this->total; }
    int          Has(String w) { return this->words->Contains(w); }

    /* Vocabulary richness: unique words as a percentage of total tokens. */
    int RichnessPct() {
        return this->total == 0 ? 0 : (this->words->Count() * 100) / this->total;
    }

    /* Jaccard similarity vs another bag, as a percentage.
     * `defer delete` co-locates cleanup with allocation, so the temporary sets
     * are freed on every exit path (including any future early return/throw). */
    int SimilarityPct(WordBag* other) {
        auto inter = this->words->Intersect(other->Words());
        auto uni = this->words->Union(other->Words());
        int i = inter.Count();
        int u = uni.Count();
        return u == 0 ? 0 : (i * 100) / u;
    }
};

/* Load a file from disk into a WordBag. */
WordBag* load_file(String name, char* path) {
    WordBag* bag = new WordBag(name);
    char* text = File.read_text(path);
    if (text != NULL) {
        bag->Ingest(text);
        free(text);
    } else {
        printf("   (warning: could not read %s)\n", path);
    }
    return bag;
}

/* ───────────────────────────────── main ───────────────────────────────── */

int main() {
    printf("=== ClassyC word-set analytics ===\n\n");

    char* path1 = "/tmp/classy_words_cats.txt";
    char* path2 = "/tmp/classy_words_dogs.txt";

    /* Create the corpus on disk (two short, overlapping documents). */
    File.write_text(path1,
        "The quick brown cat sat on the warm mat.\n"
        "The cat liked the warm sun and the soft mat.\n"
        "A cat is a small furry animal that many people keep as a pet.\n");
    File.write_text(path2,
        "The loyal brown dog ran across the park.\n"
        "The dog liked the park and the warm sun.\n"
        "A dog is a friendly furry animal that many people keep as a pet.\n");

    WordBag* cats = load_file("cats.txt", path1);
    WordBag* dogs = load_file("dogs.txt", path2);
    defer delete cats;
    defer delete dogs;

    /* ── Per-document stats ─────────────────────────────────────────────── */
    printf("-- corpus --\n");
    printf("   %-10s %3d tokens, %3d unique (%d%% richness)\n",
           cats->Name(), cats->Total(), cats->Unique(), cats->RichnessPct());
    printf("   %-10s %3d tokens, %3d unique (%d%% richness)\n",
           dogs->Name(), dogs->Total(), dogs->Unique(), dogs->RichnessPct());

    /* ── `sort -u`: unique vocabulary, alphabetised ─────────────────────── */
    printf("\n-- sort -u (unique words, sorted) --\n");
    List<String>* vocab1 = to_sorted_list(cats->Words());
    print_words(cats->Name(), vocab1, 100);
    delete vocab1;

    /* ── grep, set form: which keywords occur in each file ──────────────── */
    printf("\n-- grep (set membership) --\n");
    List<String>* keywords = new List<String>{ "cat", "dog", "park", "mat", "bird", "friendly" };
    printf("   %-12s %-8s %-8s\n", "keyword", cats->Name(), dogs->Name());
    for (auto kw in keywords)
        printf("   %-12s %-8s %-8s\n", kw,
               cats->Has(kw) ? "match" : "·",
               dogs->Has(kw) ? "match" : "·");
    delete keywords;

    /* ── stop-word stripping via set difference ─────────────────────────── */
    printf("\n-- stop-word removal (set difference) --\n");
    Set<String>* stop = new Set<String>{
        "the", "a", "an", "and", "as", "is", "on", "in", "of", "that", "to"
    };
    auto content = cats->Words()->Difference(stop);
    printf("   %s: %d unique -> %d content words after dropping %d stop words\n",
           cats->Name(), cats->Unique(), content.Count(), stop->Count());
    List<String>* contentSorted = to_sorted_list(&content);
    print_words("content words", contentSorted, 100);
    delete contentSorted;
    delete stop;

    /* ── document diff + similarity (set algebra) ───────────────────────── */
    printf("\n-- diff & similarity (set algebra) --\n");
    auto shared = cats->Words()->Intersect(dogs->Words());
    auto onlyCats = cats->Words()->Difference(dogs->Words());
    auto onlyDogs = dogs->Words()->Difference(cats->Words());
    auto allWords = cats->Words()->Union(dogs->Words());

    List<String>* sharedSorted = to_sorted_list(&shared);
    List<String>* onlyCatsSorted = to_sorted_list(&onlyCats);
    List<String>* onlyDogsSorted = to_sorted_list(&onlyDogs);
    print_words("shared",        sharedSorted,   100);
    print_words("only in cats",  onlyCatsSorted, 100);
    print_words("only in dogs",  onlyDogsSorted, 100);
    printf("   combined vocabulary: %d words\n", allWords.Count());
    printf("   Jaccard similarity : %d%%\n", cats->SimilarityPct(dogs));

    delete sharedSorted; delete onlyCatsSorted; delete onlyDogsSorted;

    /* ── tidy up the scratch files ──────────────────────────────────────── */
    File.remove_file(path1);
    File.remove_file(path2);

    printf("\nDone.\n");
    return 0;
}
