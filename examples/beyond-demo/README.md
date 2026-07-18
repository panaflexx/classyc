# beyond-demo — taking ClassyC to the limits

This directory is for demos that are intentionally **too big for a validation file**: real systems that show ClassyC competing with languages people normally reach for only after they've added a garbage collector, a runtime, and a package ecosystem.

The goal: a "Show HN" demo where the reaction is *"wait, this compiles to native C?"*

---

## What's here now: ClassyDB

We picked the **MongoDB-compatible in-memory document database** idea and built a working prototype. It is a single ClassyC project with:

- A schemaless document store where `dict` is the native document format.
- Collections backed by `Map<String, dict>`.
- **Secondary indexes** on scalar fields with equality + range lookups (`$eq`, `$gt`, `$gte`, `$lt`, `$lte`, `$in`), with a total type order (number < string < bool) so mixed-type fields stay correctly sorted.  Indexes are **explicit-only** — created per field via `POST /api/<coll>/index` (`{"field":"role"}`) or `coll->CreateIndex(field)`; nothing is indexed by default.  `GET /api/<coll>/index` lists indexed fields.
- A MongoDB-flavored query engine supporting `$eq`, `$gt`, `$gte`, `$lt`, `$lte`, `$in`, `$and`, `$or`.  Indexed single-field filters and `$and` conjuncts are answered **exactly** by the indexes (Set-intersection of candidate id lists) — no per-candidate document fetch.
- Update operators `$set`, `$inc`, `$unset` that keep indexes consistent.
- **Freeze/thaw persistence**: `db.Freeze(path)` dumps every collection to JSONL (docs + index metadata); `db.Thaw(path)` replays it (upsert semantics, indexes rebuilt, `doc-N` counter preserved).
- **`GET /perfmon`**: live counters — per-HTTP-method count/avg ms, engine ops (insert, `query_index_hit` vs `query_scan`, get/update/delete, create_index), per-collection doc counts + index lists.
- **Concurrent fiber HTTP server** (`http-serve-fibers.c`): one minicoro fiber per connection with non-blocking sockets on a single OS thread — 50-way client concurrency with no locks and no data races.  **HTTP/1.1 keep-alive**: multiple requests per connection (pipelining-safe buffering); the serial core (`http-serve.c`) remains as a fallback (`serial` argv).
- **Bulk insert**: `POST /api/<coll>/bulk` with a JSON array body (max 65,536 rows) — one connection, one parse, N inserts.
- An HTTP/JSON API built on `examples/http-serve.c` + `include/httpserve.h`.
- A synthetic HTTP test suite that runs without opening a socket.

### Files

| File | Purpose |
|------|---------|
| `classyc-db-engine.h` | Shared query engine + indexed Collection + Database + freeze/thaw |
| `classyc-db-core.cy` | Standalone document DB tests + 20k-doc benchmark |
| `classyc-db-server.cy` | HTTP server wrapper + synthetic HTTP tests |
| `classyc-db-stress.cy` | In-process engine micro-benchmark (no sockets) |
| `sqlite-bench.c` | SQLite twin of the stress bench (gcc + libsqlite3) |
| `mongo-bench.js` | mongosh twin of the stress bench (docker) |
| `mongo_http_app.py` | FastAPI/uvicorn/motor wrapper with the same REST API |
| `stress_client.py` | Async stress/performance client (keep-alive via `httpconn.py`) |
| `stress_customers.py` | 1M-customer scale test (dealership-faker shape, bulk insert) |
| `httpconn.py` | Shared persistent-connection (keep-alive) HTTP helper |
| `../../examples/http-serve-fibers.c` | Concurrent fiber HTTP core (one fiber per connection) |
| `Makefile` | `core`, `server`, `test`, `stress`, `aot` targets |
| `README.md` | This file |

### Build / run

```sh
# From examples/beyond-demo/
make core        # standalone self-tests (incl. freeze/thaw round-trip)
make server-test # HTTP API synthetic tests (no socket)
make server      # start the FIBER HTTP server on port 7099
make aot         # emit native binary ./classyc-db via classyc-aot.sh
make aot-test    # build + run the synthetic suite on the AOT binary
```

The server mode builds both HTTP cores with the app:

```sh
# fiber server (default) — one minicoro fiber per connection
../../bin/classyc -I ../../include -I ../../ext/ccchan \
    ../../examples/http-serve.c ../../examples/http-serve-fibers.c \
    classyc-db-server.cy -eg

# serial fallback — one connection at a time
../../bin/classyc -I ../../include -I ../../ext/ccchan \
    ../../examples/http-serve.c ../../examples/http-serve-fibers.c \
    classyc-db-server.cy -eg serial

# AOT server (native binary — same fiber core, keep-alive, bulk, perfmon)
make aot
./classyc-db
```

Then test with curl:

```sh
# Insert
curl -s -X POST -d '{"name":"Ada","age":36,"role":"admin","active":1,"salary":180000}' \
     http://127.0.0.1:7099/api/users

# Bulk insert (JSON array, max 65536 rows per call)
curl -s -X POST -d '[{"name":"Ada","age":36},{"name":"Grace","age":41}]' \
     http://127.0.0.1:7099/api/users/bulk

# List
curl -s http://127.0.0.1:7099/api/users

# Get by id
curl -s http://127.0.0.1:7099/api/users/doc-1

# Query
curl -s -X POST -d '{"age":{"$gte":30}}' http://127.0.0.1:7099/api/users/query

# Update
curl -s -X PUT -d '{"$set":{"role":"superadmin"}}' http://127.0.0.1:7099/api/users/doc-1

# Create a secondary index (explicit — ClassyDB never indexes columns by default)
curl -s -X POST -d '{"field":"role"}' http://127.0.0.1:7099/api/users/index

# List indexed fields
curl -s http://127.0.0.1:7099/api/users/index

# Count matching documents (fast, returns only the count)
curl -s -X POST -d '{"role":"admin"}' http://127.0.0.1:7099/api/users/count

# Performance monitor: HTTP method stats + engine op stats (insert,
# query_index_hit vs query_scan, get/update/delete, create_index) + per-
# collection doc counts and index lists
curl -s http://127.0.0.1:7099/perfmon

# Delete
curl -s -X DELETE http://127.0.0.1:7099/api/users/doc-1
```

### Persistence (freeze / thaw)

Engine-level JSONL snapshots — one line per document, plus index-metadata
lines, replayed in order on load:

```c
Database db;
// ... insert docs, create indexes ...
db.Freeze("backup.jsonl");          // 0 on success

Database db2;
int n = db2.Thaw("backup.jsonl");   // upserts docs, rebuilds indexes,
                                    // and keeps doc-N ids from colliding
```

`Thaw` returns the total doc count (or -1 on open error).  See the round-trip
in `classyc-db-core.cy` §7b.

### Stress test

A zero-dependency asyncio client is included:

```sh
make stress
# or manually:
python3 stress_client.py --start-server --seed 1000 --ops 2000 --concurrency 50
```

It starts the fiber server, seeds documents, creates indexes, then runs INSERT / QUERY / COUNT / GET / UPDATE / MIXED phases and reports latency percentiles + throughput.  (The client sends the server log to DEVNULL — an undrained stdout PIPE fills after ~64KB and blocks the server mid-run.)

Measured on this machine (1,000 seeded docs, concurrency 50, fiber server, **keep-alive client**):

| Workload | Throughput (actual) | vs one-request-per-connection | Notes |
|----------|--------------------:|------------------------------:|:------|
| INSERT   | ~22,200 req/sec | 3.8× | index maintenance included |
| GET      | ~28,900 req/sec | 4.5× | direct map lookup |
| UPDATE   | ~25,400 req/sec | 4.2× | index maintenance included |
| COUNT    | ~10,800 req/sec | 2.4× | index-only, no doc serialization |
| QUERY    | ~130 req/sec | ~1× | serializes hundreds of docs per response |
| MIXED    | ~450 req/sec | ~1.1× | all phases interleaved, 0 errors |

Keep-alive (persistent connections, `httpconn.py`) removed the per-request TCP connect/close that had capped the client at ~7k req/sec; QUERY is unchanged because it is serialization-bound, not connection-bound.  The asyncio client itself is now the ceiling on point ops.

All phases complete with **0 errors** at concurrency 50.  Actual throughput far exceeds the serial figure because recv/send waits of the 50 clients overlap inside the fiber server; app-level DB work itself is serialized per request (see the concurrency note below).  The QUERY line stays the slow one: each response builds and streams a large JSON array, and that compute does not overlap.

### Concurrency model: why fibers, not pthreads/RW-locks

The ClassyC String arena is process-global with positional (LIFO) checkpoints — without per-thread storage (FIBERS.md Phase 1), OS-thread parallelism over request handlers would corrupt the arena, and an RW lock around the DB would not fix that.  `http-serve-fibers.c` therefore runs one minicoro fiber per connection on a **single OS thread**: fibers yield only in the raw recv/send loops (never while holding arena Strings — every Request/Response/json String is created and released inside one yield-free block per request), so the arena's positional releases stay correct and no locks are needed at all.  The result is genuine 50-way I/O concurrency with zero shared-state risk.  True multi-core request handling is future work gated on the TLS/string-registry items in FIBERS.md; at that point an RW lock around `Collection` ops (read for FindIds/Get, write for Insert/Update/Delete) is the right shape.

Two correctness fixes landed with the 1M-customer scale test (`stress_customers.py`): the fiber scheduler now reaps `MCO_DEAD` coroutines itself (a slot cleared early by the connection fiber leaked each connection's 64KB minicoro stack — 10k connections cost ~660MB RSS before the fix, ~20MB after); and `Request` no longer lower-cases the URL path, so case-sensitive ids like `C0100000` resolve (PUT also returns 404 instead of a silent no-op `{}` when the target doc is missing).

### Engine performance notes (classyc-db-stress.cy, 10k docs / 10k ops)

Head-to-head with SQLite and MongoDB on the same document stream
(`srand(42)` — result counts verified identical) and the same workload
shape.  Harnesses: `sqlite-bench.c` (libsqlite3, -O2), `mongo-bench.js`
(mongosh in docker), `classyc-db-stress.cy` (ClassyC JIT).

**In-process, per-op (10k docs):**

| Phase | ClassyDB (JIT) | SQLite `:memory:` | SQLite (file) | mongod 7 (server) |
|-------|---------------:|------------------:|--------------:|------------------:|
| INSERT | 2.3–3.1 µs | 1.7 µs | 8.5 µs | 12.8 µs (bulk amortized) |
| CREATE INDEX ×4 | 5.3 ms | 2.8 ms | 2.9 ms | 62 ms |
| role=admin (3,375 ids) | 27 µs | 245 µs | 286 µs | ~3,000 µs |
| age>=30 (8,129 ids) | 73 µs | 415 µs | 464 µs | ~10,000 µs |
| active&&rich ($and) | 515 µs | 892 µs | 1,037 µs | ~7,000 µs |
| age $in (817 ids) | 14 µs | 59 µs | 65 µs | ~2,400 µs |
| age=30 (159 ids) | 1.8 µs | 11.3 µs | 12.8 µs | ~1,000 µs |
| GET by id | 0.08 µs | 0.53 µs | 1.5 µs | wire-bound |
| UPDATE $inc | 3.1 µs | 2.5 µs | 15.1 µs | wire-bound |

- ClassyDB wins every indexed id-projection read — the sorted-array index
  copies a dense id range (~8 ns/id) instead of stepping rows through a
  VM (~50–70 ns/row for SQLite) or fetching BSON documents (~1 µs/doc for
  mongod — it does a full FETCH even for an `_id`-only projection).
- SQLite keeps the edge on INSERT / UPDATE / INDEX build (B-tree bulk ops).
- mongod server times are from `explain("executionStats")`; end-to-end via
  a shell/driver adds a ~1 ms/op wire floor (see mongo-bench.js).

**Over HTTP — same client (`stress_client.py`, seed 1000, 2000 ops/phase, concurrency 50):**

| Phase | ClassyDB fiber (1 proc) | mongo + FastAPI/uvicorn/motor (1 worker) | same (4 workers) |
|-------|------------------------:|-----------------------------------------:|-----------------:|
| INSERT | 5,818 req/s | 932 req/s | 1,994 req/s |
| QUERY (full docs) | 136 req/s | 49 req/s | 135 req/s |
| COUNT | 4,523 req/s | 1,060 req/s | 1,732 req/s |
| GET | 6,355 req/s | 1,248 req/s | 2,820 req/s |
| UPDATE | 6,103 req/s | 1,067 req/s | 2,670 req/s |
| MIXED | 396 req/s | 135 req/s | 139 req/s |

- Point ops: ClassyDB is ~2.3–2.9× the 4-worker MongoDB stack (the Mongo
  path pays HTTP→uvicorn→motor→mongod plus Python JSON re-serialization).
- QUERY (~180 KB responses) converges to a tie — both sides are dominated
  by building and shipping the payload, not by the query itself.
- 1→4 worker scaling (~2.5×) shows the single Python wrapper was the Mongo
  stack's ceiling, not mongod; the asyncio client itself caps at ~7k
  req/sec per 50 workers, so ClassyDB's GET/COUNT rows are client-limited.
- Reproduce the MongoDB side: `docker run -d -p 27017:27017 --name mongo-bench mongo:7`,
  then `.venv/bin/uvicorn mongo_http_app:app --port 8000 --workers 4`
  (venv with `fastapi uvicorn[standard] motor`), then
  `python3 stress_client.py --port 8000 --seed 1000 --ops 2000 --concurrency 50`.

### Scale: 1,000,000 customers (stress_customers.py)

Dealership-faker-shaped customers over HTTP (`make stress-customers`), one
fiber server process, bulk insert (1,000 rows/request) + keep-alive client:

| Measurement | Result |
|-------------|-------:|
| Load 1M docs | 578 s (**1,730/s**, 0 errors; faker-generation-bound, transport is negligible) |
| RSS after load | 1.87 GB (~1.9 KB/doc all-in) |
| CreateIndex(state), (city) | 2.9 s / 3.6 s for 1M-entry bulk builds |
| COUNT state=TX (17,107 hits) | 1 ms |
| COUNT state $in 4 states (67,714 hits) | 4 ms |
| COUNT created_at>=2023 (**541,389 hits**) | 278 ms (~0.5 µs/hit) |
| QUERY state=TX (17,107 docs, 5.4 MB) | 320 ms (~48 µs/doc serialized+streamed) |
| QUERY exact, **no index** (1M-doc scan) | 183 ms |
| GET by id (5k random) | **39,159/s** |
| UPDATE $set (5k random) | **19,768/s** |

Takeaways: memory is linear (~1.9 KB/doc), index-only counts stay usable
even at half-collection selectivity, full-doc queries are ~50 µs/doc
serialization+transport, and an unindexed field costs one 0.18 µs/doc scan —
which is why the primary-key GET (or a secondary index) matters at 1M.

Key invariants, pinned by `sketch/probe-db-index-equiv.cy` (index results ≡ table-scan results over mixed-type/null/bool/array cases):

- Index keys sort by a total order (type rank: number < string < bool < non-scalar, then value), so binary search is exact even on mixed-type fields.
- `$gt`/`$lt`-family never matches missing/incomparable fields (query operators gate on `ValueComparable`; the raw total order is index-internal).
- A single-field filter answered by its index is returned **exactly** — no per-candidate `docs.GetOr` + `DocMatches` re-check; `$and` conjuncts are intersected as id Sets.

### Runtime fix that made this possible

While building ClassyDB we hit a real gap: `Map<String, V>` stored the `String` key pointer directly, so keys allocated in a function's String arena became dangling as soon as that function returned. The map looked up keys by content, but the stored keys were garbage.

We fixed `include/map.h` so that `Map<String, V>` now **heap-copies String keys on insertion** and **frees them on removal / destruction**. This makes `Map<String, V>` safe for long-lived containers without manual `detach`/`strdup` hacks in user code.

The change is local to `map.h` (`Set` and `destroy_key_at`) and uses `nameof<K>()` to detect String specializations. All 54 `cy-validate` tests still pass.

---

## Future candidate demos

If you want to keep pushing, the next candidates are:

1. **ClassyEngine** — 2D game engine with `jitrunner --watch` hot-reload, building on `examples/spaceway3k/`.
2. **ClassyCRDT** — real-time collaborative backend with TCP/WebSocket sync.
3. **ClassyRay** — CPU raytracer / path tracer with a live viewport.
4. **ClassyMesh** — tiny Envoy-like HTTP proxy with dynamic routing.
5. **ClassyKernel** — toy microkernel (hardest, but the ultimate systems-programming flex).

See the git history of this file for the original full write-up of each idea.

---

*"The best demo is one that makes people forget they're looking at a C compiler."*
