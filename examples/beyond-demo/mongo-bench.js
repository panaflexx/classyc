/* mongo-bench.js — MongoDB twin of classyc-db-stress.cy (run inside mongosh).
 *
 * Same phases and report format.  Documents are generated with a xorshift
 * PRNG (seed 42) mirroring the ClassyDB distributions, so result sizes are
 * statistically equivalent (±1%) rather than bit-identical.
 *
 * Run:  docker exec -i mongo-bench mongosh --quiet < mongo-bench.js
 */

const N = 10000, OPS = 10000;
const users = db.getSiblingDB("bench").users;
users.drop();

let rngState = 42;
function rnd() {                 /* xorshift32 */
    rngState ^= rngState << 13; rngState >>>= 0;
    rngState ^= rngState >> 17;
    rngState ^= rngState << 5;  rngState >>>= 0;
    return rngState;
}
const ROLES = ["user", "admin", "guest"];
function MakeDoc(i) {
    return {
        _id: "doc-" + (i + 1),
        name: "user-" + i,
        age: 18 + (rnd() % 63),
        role: ROLES[rnd() % 3],
        active: rnd() % 2,
        salary: 30000 + (rnd() % 220000)
    };
}

function Report(label, n, ms) {
    const s = Math.max(ms, 1) / 1000;
    print("  " + label.padEnd(16) + " " + String(n).padStart(6) +
          " ops in " + s.toFixed(3).padStart(7) + "s  -> " +
          (n / s).toFixed(1).padStart(10) + " ops/sec  (" +
          ((ms * 1000) / n).toFixed(3).padStart(8) + " us/op)");
}

print("MongoDB stress (mongosh, loopback)");
print("  seed docs: " + N);
print("  ops/phase: " + OPS + "\n");

/* ── INSERT (per-op insertOne: measures wire+server per-op cost) ── */
let t0 = Date.now();
for (let i = 0; i < N; i++) users.insertOne(MakeDoc(i));
Report("INSERT (insertOne)", N, Date.now() - t0);

/* ── INSERT bulk (one insertMany: amortized server-side per-doc cost) ── */
{
    const bulk = db.getSiblingDB("bench").users_bulk;
    bulk.drop();
    const docs = [];
    for (let i = 0; i < N; i++) docs.push(MakeDoc(i));
    t0 = Date.now();
    bulk.insertMany(docs, { ordered: false });
    Report("INSERT (insertMany)", N, Date.now() - t0);
    bulk.drop();
}

/* ── CREATE INDEXES ── */
t0 = Date.now();
users.createIndex({ role: 1 });
users.createIndex({ age: 1 });
users.createIndex({ active: 1 });
users.createIndex({ salary: 1 });
Report("INDEX", 4, Date.now() - t0);
print("");

/* ── QUERY by type (_id-only projection = FindIds id list) ──
 * batchSize(20000): a single fetch per query — otherwise the default batch
 * forces ~N/101 getMore round trips and we benchmark the wire, not the DB. */
print("-- FindIds breakdown (_id projection, toArray) --");
function RunQueryPhase(label, filter, ops) {
    let total = 0;
    const t0 = Date.now();
    for (let i = 0; i < ops; i++)
        total += users.find(filter, { _id: 1 }).batchSize(20000).toArray().length;
    Report(label, ops, Date.now() - t0);
    print("                     avg result size: " + (total / ops).toFixed(1));
}
RunQueryPhase("role=admin",   { role: "admin" },                          OPS / 4);
RunQueryPhase("age>=30",      { age: { $gte: 30 } },                      OPS / 4);
RunQueryPhase("active&&rich", { $and: [{ active: 1 }, { salary: { $gte: 100000 } }] }, OPS / 4);
RunQueryPhase("age $in",      { age: { $in: [25, 35, 45, 55, 65] } },     OPS / 4);
RunQueryPhase("age=30",       { age: 30 },                                OPS / 4);

/* ── mixed QUERY ── */
const filters = [
    { role: "admin" },
    { age: { $gte: 30 } },
    { $and: [{ active: 1 }, { salary: { $gte: 100000 } }] },
    { age: { $in: [25, 35, 45, 55, 65] } }
];
t0 = Date.now();
let totalFound = 0;
for (let i = 0; i < OPS; i++)
    totalFound += users.find(filters[i % 4], { _id: 1 }).batchSize(20000).toArray().length;
Report("QUERY mixed", OPS, Date.now() - t0);
print("  total documents matched: " + totalFound + "\n");

/* ── GET by id (full doc, like FindById) ── */
t0 = Date.now();
let found = 0;
for (let i = 0; i < OPS; i++)
    if (users.findOne({ _id: "doc-" + ((i % N) + 1) })) found++;
Report("GET", OPS, Date.now() - t0);
print("  found: " + found + "\n");

/* ── UPDATE ($inc salary by id) ── */
t0 = Date.now();
for (let i = 0; i < OPS; i++)
    users.updateOne({ _id: "doc-" + ((i % N) + 1) }, { $inc: { salary: 100 } });
Report("UPDATE", OPS, Date.now() - t0);
