/* classy-search-engine.cy — a tiny MapReduce search engine on List<T>
 *
 * A miniature "old Google": build an inverted index over a corpus of
 * documents using the classic map -> shuffle/sort -> reduce pipeline, then
 * answer ranked keyword queries.
 *
 *   MAP      every document emits one (term, docId) Posting per word.
 *   SHUFFLE  all postings are sorted by (term, docId)               -> List.Sort
 *   REDUCE   runs of equal terms collapse into inverted-index Terms.
 *   QUERY    query words look up their Terms; per-doc scores are ranked.
 *
 * Everything is built from generic List<T> specialised over *custom classes*
 * (List<Doc*>, List<Posting*>, List<Term*>, List<Hit*>) and sorted/filtered
 * with typed lambdas that dereference those class pointers.
 *
 * Usage:  classyc examples/classy-search-engine.cy -eg
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "include/list.h"

/* ───────────────────────────── Model classes ───────────────────────────── */

/* A corpus document.  `id` is its dense index into the corpus list. */
class Doc {
    int    id;
    String title;
    String body;
    Doc(int id, String title, String body) {
        this->id = id; this->title = title; this->body = body;
    }
    ~Doc() {}
};

/* MAP output: a single (term, docId) emission — one per word occurrence. */
class Posting {
    String term;
    int    docId;
    Posting(String term, int docId) { this->term = term; this->docId = docId; }
    ~Posting() {}
};

/* REDUCE output: an inverted-index entry.  `docIds` holds one element per
 * occurrence (sorted, with repeats), so the number of times a docId appears is
 * that term's frequency within the document. */
class Term {
    String      word;
    List<int>*  docIds;
    Term(String word) { this->word = word; this->docIds = new List<int>(); }
    ~Term() { delete this->docIds; }
};

/* A ranked search result. */
class Hit {
    int docId;
    int score;
    Hit(int docId, int score) { this->docId = docId; this->score = score; }
    ~Hit() {}
};

/* ───────────────────────────── Tokenisation ───────────────────────────── */

/* Allocate a lowercased, NUL-terminated copy of [s, s+len). */
String intern_word(const char* s, int len) {
    char* w = (char*) malloc(len + 1);
    for (int i = 0; i < len; i++) w[i] = (char) tolower((unsigned char) s[i]);
    w[len] = 0;
    return (String) w;
}

/* MAP: scan a document body and emit (term, docId) for every word. */
void map_doc(Doc* d, List<Posting*>* out) {
    const char* p = d->body;
    int i = 0;
    while (p[i] != 0) {
        if (isalnum((unsigned char) p[i])) {
            int start = i;
            while (p[i] != 0 && isalnum((unsigned char) p[i])) i++;
            String w = intern_word(p + start, i - start);
            out->Add(new Posting(w, d->id));
        } else {
            i++;
        }
    }
}

/* ───────────────────────────── Reduce phase ───────────────────────────── */

/* SHUFFLE + REDUCE: sort postings by (term, docId), then collapse equal-term
 * runs into one Term each.  Returns the inverted index, sorted by term. */
List<Term*>* reduce_postings(List<Posting*>* postings) {
    postings->Sort((Posting* a, Posting* b) => {
        int c = strcmp(a->term, b->term);
        return c != 0 ? c : a->docId - b->docId;
    });

    List<Term*>* index = new List<Term*>();
    Term* cur = NULL;
    for (auto post in postings) {
        if (cur == NULL || strcmp(cur->word, post->term) != 0) {
            cur = new Term(post->term);
            index->Add(cur);
        }
        cur->docIds->Add(post->docId);
    }
    return index;
}

/* ───────────────────────────── Query phase ────────────────────────────── */

/* Binary search the term-sorted index for `word`; NULL if absent. */
Term* lookup(List<Term*>* index, String word) {
    int lo = 0, hi = index->Count() - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int c = strcmp(index->Get(mid)->word, word);
        if (c == 0) return index->Get(mid);
        if (c < 0) lo = mid + 1; else hi = mid - 1;
    }
    return NULL;
}

/* Score every document against a free-text query and return ranked Hits.
 * Ranking key = (#distinct query terms matched)*1000 + (total term frequency),
 * so documents that satisfy more of the query win, frequency breaks ties. */
List<Hit*>* search(List<Term*>* index, int nDocs, String query) {
    int* tf      = (int*) calloc(nDocs, sizeof(int));   /* summed term freq    */
    int* matched = (int*) calloc(nDocs, sizeof(int));   /* distinct terms hit  */

    const char* p = query;
    int i = 0;
    while (p[i] != 0) {
        if (isalnum((unsigned char) p[i])) {
            int start = i;
            while (p[i] != 0 && isalnum((unsigned char) p[i])) i++;
            String qw = intern_word(p + start, i - start);
            Term* t = lookup(index, qw);
            if (t != NULL) {
                int last = -1;
                for (auto docId in t->docIds) {
                    tf[docId] = tf[docId] + 1;
                    if (docId != last) { matched[docId] = matched[docId] + 1; last = docId; }
                }
            }
        } else {
            i++;
        }
    }

    List<Hit*>* hits = new List<Hit*>();
    for (int d = 0; d < nDocs; d++)
        if (tf[d] > 0) hits->Add(new Hit(d, matched[d] * 1000 + tf[d]));
    free(tf);
    free(matched);

    hits->Sort((Hit* a, Hit* b) => b->score - a->score);
    return hits;
}

/* Run one query and print the ranked results. */
void run_query(List<Doc*>* corpus, List<Term*>* index, String query) {
    List<Hit*>* hits = search(index, corpus->Count(), query);

    printf("\nQuery: \"%s\"  ->  %d match(es)\n", query, hits->Count());
    int rank = 0;
    for (auto h in hits) {
        if (rank >= 5) break;
        Doc* d = corpus->Get(h->docId);
        int terms = h->score / 1000;
        int freq  = h->score % 1000;
        printf("   %d. %-9s  [%d term(s), tf=%d]\n", rank + 1, d->title, terms, freq);
        rank++;
    }
    if (hits->Count() == 0) printf("   (no documents matched)\n");

    for (auto h in hits) delete h;
    delete hits;
}

/* ───────────────────────────────── main ───────────────────────────────── */

int main() {
    printf("=== ClassyC MapReduce search engine ===\n");

    /* ── Corpus ─────────────────────────────────────────────────────────── */
    List<Doc*>* corpus = new List<Doc*>();
    corpus->Add(new Doc(0, "Rust",
        "Rust is a fast systems programming language focused on memory "
        "safety without a garbage collector."));
    corpus->Add(new Doc(1, "Go",
        "Go is a fast language with a garbage collector built for simple "
        "concurrent systems programming."));
    corpus->Add(new Doc(2, "Python",
        "Python is a simple high level language with automatic memory "
        "management and garbage collection."));
    corpus->Add(new Doc(3, "C",
        "C is a fast low level systems language with manual memory "
        "management and no safety guarantees."));
    corpus->Add(new Doc(4, "Haskell",
        "Haskell is a pure functional language with lazy evaluation and "
        "garbage collection."));

    printf("Indexed %d documents.\n", corpus->Count());

    /* ── MAP ────────────────────────────────────────────────────────────── */
    List<Posting*>* postings = new List<Posting*>();
    for (auto d in corpus) map_doc(d, postings);
    printf("MAP    emitted %d (term, doc) postings.\n", postings->Count());

    /* ── SHUFFLE + REDUCE ───────────────────────────────────────────────── */
    List<Term*>* index = reduce_postings(postings);
    printf("REDUCE built an inverted index of %d unique terms.\n", index->Count());

    /* Show the most widely-shared terms (highest document frequency). The
     * document frequency is the count of distinct docIds in a term's postings. */
    List<Term*>* common = index->Filter((Term* t) => {
        int df = 0, last = -1;
        for (int k = 0; k < t->docIds->Count(); k++) {
            int id = t->docIds->Get(k);
            if (id != last) { df++; last = id; }
        }
        return df >= 4;   /* appears in at least 4 of 5 documents */
    });
    printf("\nTerms appearing in >= 4 documents: ");
    for (auto t in common) printf("%s ", t->word);
    printf("\n");
    delete common;

    /* ── QUERY ──────────────────────────────────────────────────────────── */
    run_query(corpus, index, "fast systems language");
    run_query(corpus, index, "memory safety");
    run_query(corpus, index, "garbage collection");
    run_query(corpus, index, "javascript monad");   /* nothing matches */

    /* ── Cleanup ────────────────────────────────────────────────────────── */
    for (auto t in index)  delete t;
    delete index;
    for (auto p in postings) delete p;
    delete postings;
    for (auto d in corpus) delete d;
    delete corpus;

    printf("\nDone.\n");
    return 0;
}
