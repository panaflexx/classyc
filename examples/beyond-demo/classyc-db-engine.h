/* classyc-db-engine.h — shared query engine + indexed Collection for ClassyDB.
 *
 * Included by classyc-db-core.cy and classyc-db-server.cy.
 */

#ifndef CLASSYC_DB_ENGINE_H
#define CLASSYC_DB_ENGINE_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include "dict_types.h"
#include "map.h"
#include "list.h"
#include "set.h"
#include "file.h"

/* Runtime helpers for explicit dict lifetime management. */
dict dict_create_object();
dict dict_create_array();
dict dict_create_number(double n);
dict dict_create_int64(long n);
dict dict_create_string(char *s);
dict dict_value_copy(dict src);
void dict_destroy(dict d);
int  dict_array_append(dict array_val, dict new_val);

/* ═══════════════════════════════════════════════════════════════════════
   Document / value helpers
   ═══════════════════════════════════════════════════════════════════════ */

int IsNumber(dict v) {
    return v != 0 && (v.type() == DICT_NUMBER || v.type() == DICT_INT64);
}

double AsDouble(dict v) {
    if (v == 0) return 0.0;
    if (v.type() == DICT_NUMBER) return (double)v;
    if (v.type() == DICT_INT64)  return (double)(long)v;
    return 0.0;
}

int64_t AsInt64(dict v) {
    if (v == 0) return 0;
    if (v.type() == DICT_INT64) return (int64_t)(long)v;
    if (v.type() == DICT_NUMBER) return (int64_t)(double)v;
    return 0;
}

int ValueEq(dict a, dict b) {
    if (a == 0 || b == 0) return (a == 0 && b == 0) ? 1 : 0;
    DictType ta = a.type();
    DictType tb = b.type();
    if (ta != tb) {
        if (IsNumber(a) && IsNumber(b)) return AsDouble(a) == AsDouble(b);
        return 0;
    }
    switch (ta) {
        case DICT_NULL:   return 1;
        case DICT_BOOL:   return (int)a == (int)b;
        case DICT_NUMBER: return (double)a == (double)b;
        case DICT_INT64:  return (long)a == (long)b;
        case DICT_STRING: return strcmp((char*)a, (char*)b) == 0;
        default:          return 0;
    }
}

/* Type rank gives a total order across scalar types (number < string < bool)
 * so an index stays consistently sorted even on mixed-type fields.  Non-scalar
 * values (null / array / object) share the top rank and are not comparable. */
int ValueTypeRank(dict v) {
    if (v == 0) return 0;
    switch (v.type()) {
        case DICT_NUMBER:
        case DICT_INT64:  return 1;
        case DICT_STRING: return 2;
        case DICT_BOOL:   return 3;
        default:          return 4;
    }
}

/* Three-way compare for INDEX ORDERING: numbers by value, strings by content,
 * bools by value, non-scalars tie at the top rank.  Null operands and
 * non-scalars report "equal" (0).  Note 0 also means "incomparable" — query
 * operators must gate on ValueComparable so a missing field never matches. */
int ValueCompare(dict a, dict b) {
    if (a == 0 || b == 0) return 0;
    int ra = ValueTypeRank(a), rb = ValueTypeRank(b);
    if (ra != rb) return ra < rb ? -1 : 1;
    if (ra == 1) {
        double x = AsDouble(a), y = AsDouble(b);
        return x < y ? -1 : (x > y ? 1 : 0);
    }
    if (ra == 2) {
        int c = strcmp((char*)a, (char*)b);
        return c < 0 ? -1 : (c > 0 ? 1 : 0);
    }
    if (ra == 3) {
        int x = (int)a, y = (int)b;
        return (x > y) - (x < y);
    }
    return 0;
}

/* True when the pair orders meaningfully: same scalar type rank, both
 * non-null, and not one of the non-comparable (null/array/object) kinds. */
int ValueComparable(dict a, dict b) {
    if (a == 0 || b == 0) return 0;
    int ra = ValueTypeRank(a), rb = ValueTypeRank(b);
    return ra == rb && ra != 4;
}

int ValueGt(dict a, dict b)  { return ValueComparable(a, b) && ValueCompare(a, b) >  0; }
int ValueGte(dict a, dict b) { return ValueComparable(a, b) && ValueCompare(a, b) >= 0; }
int ValueLt(dict a, dict b)  { return ValueComparable(a, b) && ValueCompare(a, b) <  0; }
int ValueLte(dict a, dict b) { return ValueComparable(a, b) && ValueCompare(a, b) <= 0; }

/* True for scalars the index can answer exactly (number / string / bool).
 * Null, array, and object conditions fall back to a full DocMatches scan. */
int IsIndexableScalar(dict v) {
    if (v == 0) return 0;
    DictType t = v.type();
    return t == DICT_NUMBER || t == DICT_INT64 || t == DICT_STRING || t == DICT_BOOL;
}

int ValueIn(dict actual, dict arr) {
    if (arr == 0 || arr.type() != DICT_ARRAY) return 0;
    for (auto i, item in arr)
        if (ValueEq(actual, item)) return 1;
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════
   Query engine
   ═══════════════════════════════════════════════════════════════════════ */

int FieldMatches(dict doc, String key, dict condition);

int DocMatches(dict doc, dict filter) {
    if (!filter || filter.type() != DICT_OBJECT) return 1;
    for (auto k, v in filter) {
        if (k[0] == '$') {
            if (strcmp(k, "$and") == 0) {
                if (!v || v.type() != DICT_ARRAY) return 0;
                for (auto i, item in v)
                    if (!DocMatches(doc, item)) return 0;
            } else if (strcmp(k, "$or") == 0) {
                if (!v || v.type() != DICT_ARRAY) return 0;
                int any = 0;
                for (auto i, item in v)
                    if (DocMatches(doc, item)) { any = 1; break; }
                if (!any) return 0;
            } else {
                return 0;
            }
        } else {
            if (!FieldMatches(doc, k, v)) return 0;
        }
    }
    return 1;
}

int FieldMatches(dict doc, String key, dict condition) {
    dict actual = doc[(char*)key];
    if (condition == 0) return actual == 0;
    if (condition.type() != DICT_OBJECT) return ValueEq(actual, condition);
    for (auto op, opval in condition) {
        if (strcmp(op, "$eq")  == 0) { if (!ValueEq(actual, opval))  return 0; }
        else if (strcmp(op, "$gt")  == 0) { if (!ValueGt(actual, opval))  return 0; }
        else if (strcmp(op, "$gte") == 0) { if (!ValueGte(actual, opval)) return 0; }
        else if (strcmp(op, "$lt")  == 0) { if (!ValueLt(actual, opval))  return 0; }
        else if (strcmp(op, "$lte") == 0) { if (!ValueLte(actual, opval)) return 0; }
        else if (strcmp(op, "$in")  == 0) { if (!ValueIn(actual, opval))  return 0; }
        else { return 0; }
    }
    return 1;
}

/* ═══════════════════════════════════════════════════════════════════════
   ID generator
   ═══════════════════════════════════════════════════════════════════════ */

int db_next_id = 1;

String NewDocId() {
    return (String)"doc-" + db_next_id++;
}

/* ═══════════════════════════════════════════════════════════════════════
   PerfMon — lightweight op counters behind HTTP GET /perfmon
   ═══════════════════════════════════════════════════════════════════════ */

double NowMs() {
    return 1000.0 * (double)clock() / (double)CLOCKS_PER_SEC;
}

class OpStats {
    long count;
    double total_ms;
    OpStats() { this.count = 0; this.total_ms = 0.0; }
    void add(double ms) { this.count++; this.total_ms += ms; }
    double avg_ms() { return this.count > 0 ? this.total_ms / (double)this.count : 0.0; }
    dict ToJson() {
        dict o = dict_create_object();
        o["count"] = dict_create_int64(this.count);
        o["avg_ms"] = dict_create_number(this.avg_ms());
        return o;
    }
};

class PerfMon {
    OpStats inserts;
    OpStats queries_index;   /* FindIds answered by a secondary index      */
    OpStats queries_scan;    /* FindIds that fell back to a full doc scan  */
    OpStats gets;
    OpStats updates;
    OpStats deletes;
    OpStats index_builds;

    dict ToJson() {
        dict o = dict_create_object();
        o["insert"] = this.inserts.ToJson();
        o["query_index_hit"] = this.queries_index.ToJson();
        o["query_scan"] = this.queries_scan.ToJson();
        o["get_by_id"] = this.gets.ToJson();
        o["update"] = this.updates.ToJson();
        o["delete"] = this.deletes.ToJson();
        o["create_index"] = this.index_builds.ToJson();
        return o;
    }
};

PerfMon g_perfmon;

/* ═══════════════════════════════════════════════════════════════════════
   Index
   ═══════════════════════════════════════════════════════════════════════ */

class IndexEntry {
    dict value;   /* owned copy of the indexed scalar value */
    String id;    /* borrowed from docs map */

    IndexEntry(dict v, String i) {
        this.value = dict_value_copy(v);
        this.id = i;
    }
    ~IndexEntry() {
        dict_destroy(this.value);
    }
};

/* Index ordering uses the raw total order (type rank, then value) — NOT the
 * query-operator comparisons, which deliberately fail on incomparable pairs. */
int IndexValueCompare(dict a, dict b) {
    return ValueCompare(a, b);
}

/* Entry comparator for bulk-build Sort (List<IndexEntry*>::Sort signature). */
int IndexEntryCompare(IndexEntry* a, IndexEntry* b) {
    return IndexValueCompare(a->value, b->value);
}

class Index {
    String field;
    List<IndexEntry*> entries;

    Index(String field) {
        this.field = field;
        this.entries = List<IndexEntry*>();
        this.entries.owns();
    }
    ~Index() {}

    int _LowerBound(dict value) {
        int lo = 0, hi = this.entries.Count();
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            IndexEntry* e = this.entries.Get(mid);
            if (IndexValueCompare(e->value, value) < 0) lo = mid + 1;
            else hi = mid;
        }
        return lo;
    }

    int _UpperBound(dict value) {
        int lo = 0, hi = this.entries.Count();
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            IndexEntry* e = this.entries.Get(mid);
            if (IndexValueCompare(e->value, value) <= 0) lo = mid + 1;
            else hi = mid;
        }
        return lo;
    }

    int _FindInsertPos(dict value, String id) {
        int pos = this._LowerBound(value);
        int n = this.entries.Count();
        while (pos < n) {
            IndexEntry* e = this.entries.Get(pos);
            int cmp = IndexValueCompare(value, e->value);
            if (cmp < 0) break;
            if (cmp == 0 && strcmp((char*)id, (char*)e->id) < 0) break;
            pos++;
        }
        return pos;
    }

    void Insert(String id, dict doc) {
        dict val = doc[(char*)this.field];
        if (val == 0) return;
        int pos = this._LowerBound(val);
        IndexEntry* e = new IndexEntry(val, id);
        this.entries.Insert(pos, e);
    }

    /* Bulk build (used by CreateIndex): append unsorted, then one Sort —
     * O(N log N) instead of O(N^2) sorted-insert memmoves. */
    void AddUnsorted(String id, dict doc) {
        dict val = doc[(char*)this.field];
        if (val == 0) return;
        this.entries.Add(new IndexEntry(val, id));
    }
    void SortEntries() {
        this.entries.Sort(IndexEntryCompare);
    }

    void Remove(String id, dict value) {
        if (value == 0) return;
        int pos = this._LowerBound(value);
        int n = this.entries.Count();
        while (pos < n) {
            IndexEntry* e = this.entries.Get(pos);
            int cmp = IndexValueCompare(value, e->value);
            if (cmp < 0) break;
            if (cmp == 0 && strcmp((char*)id, (char*)e->id) == 0) {
                this.entries.RemoveAt(pos);
                return;
            }
            pos++;
        }
    }

    List<String>* FindEqual(dict value) {
        int lo = this._LowerBound(value);
        int hi = this._UpperBound(value);
        int n = hi - lo;
        auto result = new List<String>(n > 0 ? n : 1);
#ifdef CLASSYC_FAST_LIST
        {
            IndexEntry** src = this.entries.data + lo;
            String* dst = result->data;
            for (int i = 0; i < n; i++) {
                dst[i] = src[i]->id;
            }
            result->length = n;
        }
#else
        for (int i = lo; i < hi; i++) {
            IndexEntry* e = this.entries.Get(i);
            result->Add(e->id);
        }
#endif
        return result;
    }

    List<String>* FindRange(dict low, int lowInclusive, dict high, int highInclusive) {
        int n = this.entries.Count();
        int start = 0, end = n;
        if (low != 0) {
            start = this._LowerBound(low);
            if (!lowInclusive) {
                while (start < n) {
                    IndexEntry* e = this.entries.Get(start);
                    if (IndexValueCompare(e->value, low) > 0) break;
                    start++;
                }
            }
        }
        if (high != 0) {
            end = this._UpperBound(high);
            if (!highInclusive) {
                while (end > start) {
                    IndexEntry* e = this.entries.Get(end - 1);
                    if (IndexValueCompare(e->value, high) < 0) break;
                    end--;
                }
            }
        }
        /* Clamp to the type-rank region of the bound(s): entries sort by
         * (rank, value) — number < string < bool < non-scalar — so an
         * unbounded side would otherwise spill into the next scalar kind
         * (e.g. {"$gte":30} swallowing every string and null). */
        int rank = 0;
        if (low != 0)       rank = ValueTypeRank(low);
        else if (high != 0) rank = ValueTypeRank(high);
        if (rank != 0) {
            while (start < n && ValueTypeRank(this.entries.Get(start)->value) != rank) start++;
            while (end > start && ValueTypeRank(this.entries.Get(end - 1)->value) != rank) end--;
        }
        if (end < start) end = start;
        int count = end - start;
        auto result = new List<String>(count > 0 ? count : 1);
#ifdef CLASSYC_FAST_LIST
        {
            IndexEntry** src = this.entries.data + start;
            String* dst = result->data;
            for (int i = 0; i < count; i++) {
                dst[i] = src[i]->id;
            }
            result->length = count;
        }
#else
        for (int i = start; i < end; i++) {
            IndexEntry* e = this.entries.Get(i);
            result->Add(e->id);
        }
#endif
        return result;
    }
};

/* ═══════════════════════════════════════════════════════════════════════
   Collection
   ═══════════════════════════════════════════════════════════════════════ */

class Collection {
    String name;
    Map<String, dict> docs;
    Map<String, Index*> indexes;

    Collection(String name) {
        this.name = name;
        this.docs = Map<String, dict>();
        this.indexes = Map<String, Index*>();
        this.indexes.ownsValues();
    }

    ~Collection() {
        for (auto k, v in this.docs) {
            if (v) dict_destroy(v);
        }
    }

    void CreateIndex(String field) {
        if (this.indexes.Contains(field)) return;
        double t0 = NowMs();
        Index* idx = new Index(field);
        this.indexes[field] = idx;
        for (auto k, v in this.docs)
            idx->AddUnsorted(k, v);
        idx->SortEntries();
        g_perfmon.index_builds.add(NowMs() - t0);
    }

    String Insert(dict doc) {
        if (!doc) return NULL;
        double t0 = NowMs();
        String id = (String)doc._id;
        if (id == 0 || strlen((char*)id) == 0) {
            id = NewDocId();
            doc._id = id;
        }
        dict owned = dict_value_copy(doc);
        dict old = this.docs.GetOr(id, NULL);
        if (old) {
            for (auto k, v in this.indexes)
                v->Remove(id, old);
            dict_destroy(old);
        }
        this.docs[id] = owned;
        for (auto k, v in this.indexes)
            v->Insert(id, owned);
        g_perfmon.inserts.add(NowMs() - t0);
        return id;
    }

    dict FindById(String id) {
        double t0 = NowMs();
        dict d = this.docs.GetOr(id, NULL);
        g_perfmon.gets.add(NowMs() - t0);
        return d;
    }

    int Delete(String id) {
        dict old = this.docs.GetOr(id, NULL);
        if (!old) return 0;
        double t0 = NowMs();
        for (auto k, v in this.indexes)
            v->Remove(id, old);
        dict_destroy(old);
        int removed = this.docs.Remove(id);
        g_perfmon.deletes.add(NowMs() - t0);
        return removed;
    }

    void Update(String id, dict update) {
        dict doc = this.FindById(id);
        if (!doc || !update) return;
        double t0 = NowMs();

        /* Snapshot old indexed values so we can maintain indexes after mutation. */
        Map<String, dict> oldVals = Map<String, dict>();
        for (auto k, v in this.indexes)
            oldVals[k] = doc[(char*)k];

        for (auto op, operand in update) {
            if (strcmp(op, "$set") == 0) {
                for (auto k, v in operand) doc[(char*)k] = v;
            } else if (strcmp(op, "$inc") == 0) {
                for (auto k, v in operand) {
                    dict cur = doc[(char*)k];
                    double nv = AsDouble(cur) + AsDouble(v);
                    doc[(char*)k] = dict_create_number(nv);
                }
            } else if (strcmp(op, "$unset") == 0) {
                for (auto k, v in operand) doc[(char*)k] = NULL;
            }
        }

        for (auto k, v in this.indexes) {
            dict oldv = oldVals.GetOr(k, NULL);
            dict newv = doc[(char*)k];
            if (!ValueEq(oldv, newv)) {
                v->Remove(id, oldv);
                v->Insert(id, doc);
            }
        }
        g_perfmon.updates.add(NowMs() - t0);
    }

    /* Index-assisted candidates for one field condition, or NULL when the
     * index cannot answer it (no index, or a non-scalar / unsupported
     * condition).  A non-NULL result is EXACT — it matches precisely the docs
     * DocMatches would accept for this field — so a caller whose whole filter
     * is this one field may skip the DocMatches re-check. */
    List<String>* _FindIdsIndexedField(String field, dict condition) {
        Index* idx = this.indexes.GetOr(field, NULL);
        if (!idx) return NULL;
        if (condition != 0 && condition.type() == DICT_OBJECT) {
            if ((int)condition.length() != 1) return NULL;
            for (auto op, opval in condition) {
                if (strcmp(op, "$in")  == 0) {
                    if (opval.type() != DICT_ARRAY) return NULL;
                    auto result = new List<String>();
                    for (auto i, val in opval) {
                        if (!IsIndexableScalar(val)) { delete result; return NULL; }
                        auto partial = idx->FindEqual(val);
                        if (partial) {
                            result->AddRange(partial);
                            delete partial;
                        }
                    }
                    return result;
                }
                if (!IsIndexableScalar(opval)) return NULL;
                if (strcmp(op, "$eq")  == 0) return idx->FindEqual(opval);
                if (strcmp(op, "$gt")  == 0) return idx->FindRange(opval, 0, NULL, 0);
                if (strcmp(op, "$gte") == 0) return idx->FindRange(opval, 1, NULL, 0);
                if (strcmp(op, "$lt")  == 0) return idx->FindRange(NULL, 0, opval, 0);
                if (strcmp(op, "$lte") == 0) return idx->FindRange(NULL, 0, opval, 1);
                return NULL;  /* indexable scalar but unsupported operator */
            }
            return NULL;
        }
        if (!IsIndexableScalar(condition)) return NULL;
        return idx->FindEqual(condition);
    }

    List<String>* _FilterCandidatesByDocMatches(List<String>* candidates, dict filter) {
        int n = candidates->Count();
        auto result = new List<String>(n > 0 ? n : 1);
#ifdef CLASSYC_FAST_LIST
        {
            String* dst = result->data;
            int out = 0;
            for (int i = 0; i < n; i++) {
                String cid = candidates->UnsafeGet(i);
                dict d = this.docs.GetOr(cid, NULL);
                if (d && DocMatches(d, filter)) dst[out++] = cid;
            }
            result->length = out;
        }
#else
        for (int i = 0; i < n; i++) {
            String cid = candidates->Get(i);
            dict d = this.docs.GetOr(cid, NULL);
            if (d && DocMatches(d, filter)) result->Add(cid);
        }
#endif
        return result;
    }

    List<String>* _FindIdsIndexedTopLevel(dict filter) {
        List<String>* best = NULL;
        int keys = 0;
        for (auto k, v in filter) {
            keys++;
            if (k[0] == '$') continue;
            List<String>* cand = this._FindIdsIndexedField(k, v);
            if (cand != NULL) {
                int candCount = cand->Count();
                int use;
                if (best == NULL) use = 1;
                else use = candCount < best->Count();
                if (use) {
                    if (best != NULL) delete best;
                    best = cand;
                } else {
                    delete cand;
                }
            }
        }
        if (!best) return NULL;
        /* The whole filter is a single field answered exactly by its index —
         * skip the per-candidate doc fetch + DocMatches re-check. */
        if (keys == 1) return best;
        auto result = this._FilterCandidatesByDocMatches(best, filter);
        delete best;
        return result;
    }

    /* $and: collect exact candidate lists from every indexable single-field
     * conjunct and INTERSECT them (smallest list as base, Set membership for
     * the others) — no per-candidate doc fetch.  When every conjunct was
     * index-answered the intersection is exact and skips DocMatches entirely;
     * otherwise the (already tiny) intersection is re-checked once. */
    List<String>* _FindIdsIndexedAnd(dict filter, dict conjuncts) {
        auto cands = List<List<String>*>();
        cands.owns();   /* ~cands releases every candidate/intermediate list */
        int indexable = 0;
        for (auto i, c in conjuncts) {
            if (c.type() != DICT_OBJECT || (int)c.length() != 1) continue;
            for (auto fk, fv in c) {
                if (fk[0] == '$') continue;
                List<String>* cand = this._FindIdsIndexedField(fk, fv);
                if (cand != NULL) {
                    cands.Add(cand);
                    indexable++;
                }
            }
        }
        if (indexable == 0) return NULL;

        /* Smallest candidate list is the intersection base. */
        int baseIdx = 0;
        for (int i = 1; i < indexable; i++)
            if (cands.Get(i)->Count() < cands.Get(baseIdx)->Count()) baseIdx = i;

        /* Successively filter the base through Set membership of each other
         * candidate list; intermediates are owned by cands too. */
        List<String>* cur = cands.Get(baseIdx);
        for (int i = 0; i < indexable; i++) {
            if (i == baseIdx) continue;
            List<String>* other = cands.Get(i);
            auto inOther = Set<String>(other->Count() * 2);
            for (int j = 0; j < other->Count(); j++) inOther.Add(other->Get(j));
            auto next = new List<String>(cur->Count() > 0 ? cur->Count() : 1);
            for (int j = 0; j < cur->Count(); j++) {
                String id = cur->Get(j);
                if (inOther.Contains(id)) next->Add(id);
            }
            cands.Add(next);
            cur = next;
        }

        /* Exact when every conjunct was index-answered: copy cur out before
         * ~cands releases the candidate lists. */
        if (indexable == (int)conjuncts.length()) {
            auto out = new List<String>(cur->Count() > 0 ? cur->Count() : 1);
            for (int j = 0; j < cur->Count(); j++) out->Add(cur->Get(j));
            return out;
        }
        return this._FilterCandidatesByDocMatches(cur, filter);
    }

    List<String>* _FindIdsIndexed(dict filter) {
        if (!filter || filter.type() != DICT_OBJECT) return NULL;
        if ((int)filter.length() == 0) return NULL;
        if ((int)filter.length() == 1 && filter[(char*)"$and"] != 0) {
            dict conjuncts = filter[(char*)"$and"];
            if (conjuncts.type() == DICT_ARRAY)
                return this._FindIdsIndexedAnd(filter, conjuncts);
        }
        return this._FindIdsIndexedTopLevel(filter);
    }

    List<String>* FindIds(dict filter) {
        double t0 = NowMs();
        auto ids = this._FindIdsIndexed(filter);
        if (ids) {
            g_perfmon.queries_index.add(NowMs() - t0);
            return ids;
        }
        ids = new List<String>();
        for (auto k, v in this.docs) {
            if (DocMatches(v, filter)) ids->Add(k);
        }
        g_perfmon.queries_scan.add(NowMs() - t0);
        return ids;
    }

    int Count() { return this.docs.Count(); }

    String ToJsonArray() {
        auto arr = new List<String>();
        for (auto k, v in this.docs)
            arr->Add(json(v));
        String joined = arr->join(",");
        String out = f"[{joined}]";
        delete arr;
        return out;
    }

    /* JSON array of indexed field names (GET /api/<coll>/index). */
    String IndexesJson() {
        dict arr = dict_create_array();
        for (auto f, ix in this.indexes)
            dict_array_append(arr, dict_create_string((char*)f));
        String s = json(arr);
        dict_destroy(arr);
        return s;
    }

    /* {"docs":N,"indexes":[field,...]} — used by Database::CollectionsJson. */
    dict _StatsJson() {
        dict o = dict_create_object();
        o["docs"] = dict_create_int64(this.Count());
        dict arr = dict_create_array();
        for (auto f, ix in this.indexes)
            dict_array_append(arr, dict_create_string((char*)f));
        o["indexes"] = arr;
        return o;
    }

    /* ── Freeze/thaw (JSONL dump) ──
     * Line formats, replayed in file order by Database.Thaw:
     *   {"collection": name, "doc": {...}}    — one per stored document
     *   {"collection": name, "index": field}  — one per index definition */
    void _FreezeDocs(File* f, String name) {
        for (auto k, v in this.docs) {
            dict w = dict_create_object();
            w["collection"] = dict_create_string((char*)name);
            w["doc"] = dict_value_copy(v);
            String line = json(w);
            f->writeln((char*)line);
            dict_destroy(w);
        }
    }
    void _FreezeIndexes(File* f, String name) {
        for (auto field, idx in this.indexes) {
            dict w = dict_create_object();
            w["collection"] = dict_create_string((char*)name);
            w["index"] = dict_create_string((char*)field);
            String line = json(w);
            f->writeln((char*)line);
            dict_destroy(w);
        }
    }
};

/* ═══════════════════════════════════════════════════════════════════════
   Database
   ═══════════════════════════════════════════════════════════════════════ */

/* Replay callback for Thaw — defined after class Database. */
int _DbThawLine(char* line, int lineno, void* ctx);

class Database {
    Map<String, Collection*> collections;

    Database() {
        this.collections = Map<String, Collection*>();
    }

    ~Database() {
        for (auto k, v in this.collections)
            delete v;
    }

    Collection* CollectionNamed(String name) {
        Collection* c = this.collections.GetOr(name, NULL);
        if (!c) {
            c = new Collection(name);
            this.collections[name] = c;
        }
        return c;
    }

    /* Create an index on one field of one collection (convenience wrapper —
     * ClassyDB never indexes columns by default). */
    void CreateIndex(String collection, String field) {
        this.CollectionNamed(collection)->CreateIndex(field);
    }

    /* {collectionName: {"docs":N,"indexes":[...]}, ...} for /perfmon. */
    dict CollectionsJson() {
        dict o = dict_create_object();
        for (auto name, coll in this.collections)
            o[(char*)name] = coll->_StatsJson();
        return o;
    }

    /* Dump every collection to one JSONL file (docs, then index metadata,
     * per collection).  Returns 0 on success, -1 on open error. */
    int Freeze(String path) {
        File* f = File.open((char*)path, (char*)"w");
        if (!f->ok()) { delete f; return -1; }
        for (auto name, coll in this.collections) {
            coll->_FreezeDocs(f, name);
            coll->_FreezeIndexes(f, name);
        }
        delete f;
        return 0;
    }

    /* Replay a Freeze file into this Database (upsert semantics: existing
     * ids are replaced).  Indexes are rebuilt from the metadata lines.
     * Returns the total doc count after the load, or -1 on open error. */
    int Thaw(String path) {
        File* f = File.open((char*)path, (char*)"r");
        if (!f->ok()) { delete f; return -1; }
        f->each_line(_DbThawLine, this);
        int n = 0;
        for (auto name, coll in this.collections) n += coll->Count();
        delete f;
        return n;
    }
};

int _DbThawLine(char* line, int lineno, void* ctx) {
    Database* db = (Database*)ctx;
    if (strlen(line) == 0) return 1;
    dict w = json((String)line);
    if (w == 0 || w.type() != DICT_OBJECT) {
        if (w) dict_destroy(w);
        return 1;   /* skip malformed lines */
    }
    String cname = (String)w.collection;
    if (cname != 0) {
        Collection* c = db->CollectionNamed(cname);
        dict doc = w.doc;
        String ifield = (String)w.index;
        if (doc != 0 && doc.type() == DICT_OBJECT) {
            String id = c->Insert(doc);
            /* Keep the id counter ahead of any doc-N seen on disk. */
            if (id != 0 && id.starts_with("doc-")) {
                int n = atoi((char*)id + 4);
                if (n >= db_next_id) db_next_id = n + 1;
            }
        } else if (ifield != 0 && strlen((char*)ifield) > 0) {
            c->CreateIndex(ifield);
        }
    }
    dict_destroy(w);
    return 1;
}

#endif /* CLASSYC_DB_ENGINE_H */
