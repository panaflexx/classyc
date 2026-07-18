#!/usr/bin/env python3
"""
Stress-test client for ClassyDB.

Usage:
    # start server manually, then run client
    ./bin/classyc -I include examples/http-serve.c examples/beyond-demo/classyc-db-server.cy -eg
    python3 examples/beyond-demo/stress_client.py

    # or let the client start the server itself
    python3 examples/beyond-demo/stress_client.py --start-server

The client uses asyncio + raw TCP (no external deps) to hammer the
single-worker HTTP server with concurrent keepalive-less connections.
"""

import argparse
import asyncio
import json
import random
import statistics
import subprocess
import sys
import time
from dataclasses import dataclass, field
from typing import List

from httpconn import HttpConn

HOST = "127.0.0.1"
PORT = 7099


@dataclass
class Metrics:
    ok: int = 0
    errors: int = 0
    latencies_ms: List[float] = field(default_factory=list)

    def add(self, latency_s: float, status_ok: bool):
        self.latencies_ms.append(latency_s * 1000)
        if status_ok:
            self.ok += 1
        else:
            self.errors += 1

    def merge(self, other: "Metrics"):
        self.ok += other.ok
        self.errors += other.errors
        self.latencies_ms.extend(other.latencies_ms)

    def report(self, title: str, wall: float):
        total = self.ok + self.errors
        if not total:
            print(f"{title}: no samples")
            return
        lat = sorted(self.latencies_ms)
        p50 = lat[int(len(lat) * 0.50)]
        p95 = lat[int(len(lat) * 0.95)]
        p99 = lat[int(len(lat) * 0.99)]
        avg = statistics.mean(lat)
        serial_sec = sum(lat) / 1000.0
        print(
            f"{title}\n"
            f"  requests:   {total} ({self.ok} ok, {self.errors} errors)\n"
            f"  mean:       {avg:.2f} ms\n"
            f"  p50:        {p50:.2f} ms\n"
            f"  p95:        {p95:.2f} ms\n"
            f"  p99:        {p99:.2f} ms\n"
            f"  min/max:    {lat[0]:.2f} / {lat[-1]:.2f} ms\n"
            f"  serial tpt: {total / serial_sec:.1f} req/sec (if run one-at-a-time)\n"
            f"  actual tpt: {self.ok / wall:.1f} req/sec (wall: {wall:.2f}s)"
        )


async def http_request(method: str, path: str, body: bytes = b"") -> tuple[int, bytes]:
    """One-shot helper (used by seed/index phases): opens its own connection
    and closes it.  Hot loops use per-worker HttpConn instead (keep-alive)."""
    conn = HttpConn(HOST, PORT)
    try:
        return await conn.request(method, path, body)
    finally:
        await conn.close()


async def create_indexes(fields: List[str]):
    """Create secondary indexes for the seeded collection."""
    for field in fields:
        status, body = await http_request(
            "POST", "/api/users/index", json.dumps({"field": field}).encode()
        )
        if status != 200:
            print(f"index create error for {field}: {status} {body[:200]}", file=sys.stderr)


async def seed_collection(count: int) -> List[str]:
    """Insert `count` documents and return their ids."""
    ids: List[str] = []
    roles = ["user", "admin", "guest"]
    for i in range(count):
        doc = {
            "name": f"user-{i}",
            "age": random.randint(18, 80),
            "role": random.choice(roles),
            "active": 1,
            "salary": random.randint(30000, 250000),
        }
        status, body = await http_request(
            "POST", "/api/users", json.dumps(doc).encode()
        )
        if status == 201:
            resp = json.loads(body)
            ids.append(resp["_id"])
        else:
            print(f"seed error: {status} {body[:200]}", file=sys.stderr)
    return ids


async def worker_inserts(n: int, metrics: Metrics):
    roles = ["user", "admin", "guest"]
    conn = HttpConn(HOST, PORT)
    try:
        for i in range(n):
            doc = {
                "name": f"load-{i}-{id(metrics)}",
                "age": random.randint(18, 80),
                "role": random.choice(roles),
                "active": 1,
                "salary": random.randint(30000, 250000),
            }
            t0 = time.perf_counter()
            status, _ = await conn.request(
                "POST", "/api/users", json.dumps(doc).encode()
            )
            metrics.add(time.perf_counter() - t0, status == 201)
    finally:
        await conn.close()


async def worker_queries(n: int, metrics: Metrics):
    filters = [
        {"role": "admin"},
        {"age": {"$gte": 30}},
        {"$and": [{"active": 1}, {"salary": {"$gte": 100000}}]},
        {"age": {"$in": [25, 35, 45, 55, 65]}},
    ]
    conn = HttpConn(HOST, PORT)
    try:
        for _ in range(n):
            filt = random.choice(filters)
            t0 = time.perf_counter()
            status, _ = await conn.request(
                "POST", "/api/users/query", json.dumps(filt).encode()
            )
            metrics.add(time.perf_counter() - t0, status == 200)
    finally:
        await conn.close()


async def worker_counts(n: int, metrics: Metrics):
    filters = [
        {"role": "admin"},
        {"age": {"$gte": 30}},
        {"$and": [{"active": 1}, {"salary": {"$gte": 100000}}]},
        {"age": {"$in": [25, 35, 45, 55, 65]}},
    ]
    conn = HttpConn(HOST, PORT)
    try:
        for _ in range(n):
            filt = random.choice(filters)
            t0 = time.perf_counter()
            status, _ = await conn.request(
                "POST", "/api/users/count", json.dumps(filt).encode()
            )
            metrics.add(time.perf_counter() - t0, status == 200)
    finally:
        await conn.close()


async def worker_gets(ids: List[str], n: int, metrics: Metrics):
    conn = HttpConn(HOST, PORT)
    try:
        for _ in range(n):
            doc_id = random.choice(ids)
            t0 = time.perf_counter()
            status, _ = await conn.request("GET", f"/api/users/{doc_id}")
            metrics.add(time.perf_counter() - t0, status == 200)
    finally:
        await conn.close()


async def worker_updates(ids: List[str], n: int, metrics: Metrics):
    conn = HttpConn(HOST, PORT)
    try:
        for _ in range(n):
            doc_id = random.choice(ids)
            update = {"$inc": {"salary": 100}}
            t0 = time.perf_counter()
            status, _ = await conn.request(
                "PUT", f"/api/users/{doc_id}", json.dumps(update).encode()
            )
            metrics.add(time.perf_counter() - t0, status == 200)
    finally:
        await conn.close()


async def run_phase(name: str, coro_factory, total: int, concurrency: int):
    """Run `total` operations split across `concurrency` asyncio tasks."""
    per_worker = total // concurrency
    metrics = Metrics()
    workers = []
    for _ in range(concurrency):
        m = Metrics()
        workers.append((asyncio.create_task(coro_factory(per_worker, m)), m))

    start = time.perf_counter()
    await asyncio.gather(*(t for t, _ in workers))
    wall = time.perf_counter() - start

    for _, m in workers:
        metrics.merge(m)

    metrics.report(name, wall)
    print()
    return metrics


async def main():
    global HOST, PORT

    parser = argparse.ArgumentParser(description="Stress-test ClassyDB")
    parser.add_argument(
        "--start-server",
        action="store_true",
        help="Start the ClassyDB server before testing",
    )
    parser.add_argument("--host", default=HOST)
    parser.add_argument("--port", type=int, default=PORT)
    parser.add_argument("--seed", type=int, default=1000, help="docs to seed")
    parser.add_argument("--concurrency", type=int, default=50)
    parser.add_argument(
        "--ops",
        type=int,
        default=5000,
        help="operations per workload phase",
    )
    args = parser.parse_args()

    HOST = args.host
    PORT = args.port

    server_proc = None
    if args.start_server:
        print("Starting ClassyDB server...")
        server_proc = subprocess.Popen(
            [
                "../../bin/classyc",
                "-I", "../../include",
                "-I", "../../ext/ccchan",
                "../../examples/http-serve.c",
                "../../examples/http-serve-fibers.c",
                "classyc-db-server.cy",
                "-eg",
            ],
            cwd="/home/rdavenpo/src/MIR/classyc/examples/beyond-demo",
            # DEVNULL is load-bearing: the server logs one line per request,
            # and an undrained PIPE fills after ~64KB, blocking the server on
            # write() until the listen backlog overflows and clients reset.
            stdout=subprocess.DEVNULL,
            stderr=subprocess.STDOUT,
        )
        # Wait for server to bind
        for _ in range(30):
            try:
                reader, writer = await asyncio.wait_for(
                    asyncio.open_connection(HOST, PORT), timeout=1.0
                )
                writer.close()
                await writer.wait_closed()
                break
            except Exception:
                await asyncio.sleep(0.1)
        else:
            print("Server failed to start", file=sys.stderr)
            if server_proc:
                server_proc.kill()
            return 1
        print("Server ready.\n")

    print(f"=== ClassyDB stress test ===")
    print(f"target: {HOST}:{PORT}")
    print(f"seed docs: {args.seed}")
    print(f"concurrency: {args.concurrency}")
    print(f"ops/phase: {args.ops}\n")

    # Seed
    print(f"Seeding {args.seed} documents...")
    t0 = time.perf_counter()
    ids = await seed_collection(args.seed)
    print(f"Seeded {len(ids)} docs in {time.perf_counter() - t0:.2f}s")

    # Build indexes for the fields the query workload touches
    print("Creating indexes on role, age, active, salary...")
    await create_indexes(["role", "age", "active", "salary"])
    print("Indexes ready.\n")

    if not ids:
        print("No ids seeded, aborting.")
        return 1

    # Workloads
    await run_phase(
        "1. INSERTS",
        lambda n, m: worker_inserts(n, m),
        args.ops,
        args.concurrency,
    )

    await run_phase(
        "2. QUERIES",
        lambda n, m: worker_queries(n, m),
        args.ops,
        args.concurrency,
    )

    await run_phase(
        "2b. COUNTS",
        lambda n, m: worker_counts(n, m),
        args.ops,
        args.concurrency,
    )

    await run_phase(
        "3. GETs",
        lambda n, m: worker_gets(ids, n, m),
        args.ops,
        args.concurrency,
    )

    await run_phase(
        "4. UPDATEs",
        lambda n, m: worker_updates(ids, n, m),
        args.ops,
        args.concurrency,
    )

    # Mixed
    print("5. MIXED (insert/query/get/update)...")
    mixed_metrics = Metrics()
    n_each = args.ops // 4
    mixed_start = time.perf_counter()
    await asyncio.gather(
        worker_inserts(n_each, mixed_metrics),
        worker_queries(n_each, mixed_metrics),
        worker_gets(ids, n_each, mixed_metrics),
        worker_updates(ids, n_each, mixed_metrics),
    )
    mixed_wall = time.perf_counter() - mixed_start
    mixed_metrics.report("5. MIXED", mixed_wall)

    if server_proc:
        print("\nStopping server...")
        server_proc.terminate()
        try:
            server_proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            server_proc.kill()

    return 0


if __name__ == "__main__":
    asyncio.run(main())
