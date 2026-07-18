#!/usr/bin/env python3
"""
stress_customers.py — ClassyDB 1,000,000-customer scale test.

Adapted from ~/src/GUI/nanogui2/dealership-faker.py: same fake customers
(same faker calls, same fields), but generated into ClassyDB over HTTP
(fiber server) instead of SQLite.  Starts the fiber server itself, loads
the customers with concurrent workers, builds secondary indexes, then
times COUNT / QUERY / GET / UPDATE phases at 1M scale.

Usage:
  python3 stress_customers.py [--customers 1000000] [--concurrency 50] [--port 7099]

Try a smaller run first:
  python3 stress_customers.py --customers 100000
"""

import argparse
import asyncio
import json
import random
import string
import subprocess
import sys
import time

from faker import Faker

from httpconn import HttpConn

HOST = "127.0.0.1"
PORT = 7099

fake = Faker()

# ── tiny one-shot helper (hot loops use per-worker HttpConn keep-alive) ─────


async def http_request(method: str, path: str, body: bytes = b""):
    conn = HttpConn(HOST, PORT)
    try:
        return await conn.request(method, path, body)
    finally:
        await conn.close()


# ── customer generation (dealership-faker.py shape, customers only) ─────────


def make_customer(i):
    first = fake.first_name()
    last = fake.last_name()
    mi = random.choice(string.ascii_uppercase) if random.random() > 0.3 else ""

    cust_num = f"C{100000 + i:07d}"
    return {
        "_id": cust_num,                 # ClassyDB primary key = customer_number
        "customer_number": cust_num,
        "first_name": first,
        "middle_initial": mi,
        "last_name": last,
        "address1": fake.street_address(),
        "address2": fake.secondary_address() if random.random() > 0.7 else "",
        "city": fake.city(),
        "state": fake.state_abbr(),
        "zip": fake.zipcode(),
        "phone": fake.phone_number(),
        "email": fake.email(),
        "created_at": fake.date_time_this_decade().isoformat(),
    }


# ── seed phase ───────────────────────────────────────────────────────────────

inserted = 0
insert_errors = 0

BULK_BATCH = 1000          # docs per POST /api/users/bulk (cap is 65536)


async def post_batch(conn, batch):
    global inserted, insert_errors
    status, body = await conn.request("POST", "/api/users/bulk", json.dumps(batch).encode())
    if status == 201:
        inserted += len(batch)
    else:
        insert_errors += len(batch)
        print(f"  bulk error: {status} {body[:160]}", file=sys.stderr)


async def worker_inserts(start: int, step: int, n: int):
    conn = HttpConn(HOST, PORT)
    try:
        batch = []
        for i in range(start, n, step):
            batch.append(make_customer(i))
            if len(batch) >= BULK_BATCH:
                await post_batch(conn, batch)
                batch = []
        if batch:
            await post_batch(conn, batch)
    finally:
        await conn.close()


async def seed_monitor(n: int, t0: float):
    global inserted
    last = 0
    while inserted < n:
        await asyncio.sleep(2)
        if inserted - last >= 5000 or inserted >= n:
            rate = inserted / max(time.perf_counter() - t0, 1e-9)
            print(f"  {inserted:,} customers inserted...  ({rate:,.0f}/s)")
            last = inserted


async def seed_customers(n: int, concurrency: int):
    global inserted, insert_errors
    inserted = insert_errors = 0
    print(f"Generating + inserting {n:,} customers "
          f"({concurrency} workers, dealership-faker shape)...")
    t0 = time.perf_counter()
    mon = asyncio.create_task(seed_monitor(n, t0))
    await asyncio.gather(*(worker_inserts(w, concurrency, n) for w in range(concurrency)))
    wall = time.perf_counter() - t0
    mon.cancel()
    print(f"Inserted {inserted:,} customers in {wall:.1f}s "
          f"({inserted / wall:,.0f}/s, {insert_errors} errors)\n")


# ── timed phases ─────────────────────────────────────────────────────────────


def report(label, wall, extra=""):
    print(f"  {label:<44} {wall:8.3f}s  {extra}")


async def phase_indexes(fields):
    print("Creating secondary indexes...")
    for f in fields:
        t0 = time.perf_counter()
        status, _ = await http_request("POST", "/api/users/index", json.dumps({"field": f}).encode())
        report(f"CreateIndex({f})  [status {status}]", time.perf_counter() - t0)
    print()


async def phase_count(label, filt):
    t0 = time.perf_counter()
    status, body = await http_request("POST", "/api/users/count", json.dumps(filt).encode())
    wall = time.perf_counter() - t0
    count = json.loads(body)["count"] if status == 200 else -1
    report(f"COUNT {label:<33} -> {count:>10,}", wall)
    return count


async def phase_queries(n_docs):
    print("COUNT phases (index-only, no serialization)...")
    await phase_count("state = TX", {"state": "TX"})
    await phase_count("state $in [TX,CA,FL,NY]", {"state": {"$in": ["TX", "CA", "FL", "NY"]}})
    await phase_count("created_at >= 2023 (string range!)", {"created_at": {"$gte": "2023-01-01"}})
    print()

    print("QUERY phases (full documents)...")
    t0 = time.perf_counter()
    status, body = await http_request("POST", "/api/users/query", json.dumps({"state": "TX"}).encode())
    wall = time.perf_counter() - t0
    docs = json.loads(body) if status == 200 else []
    report(f"QUERY state = TX  -> {len(docs):,} docs, {len(body) / 1e6:.1f} MB", wall)

    exact = f"C{100000 + n_docs // 2:07d}"
    t0 = time.perf_counter()
    status, body = await http_request("POST", "/api/users/query",
                                      json.dumps({"customer_number": exact}).encode())
    wall = time.perf_counter() - t0
    docs = json.loads(body) if status == 200 else []
    report(f"QUERY customer_number = {exact} (exact) -> {len(docs)} doc", wall)
    print()


async def phase_gets(n_docs, ops):
    print(f"GET by id ({ops:,} random)...")
    t0 = time.perf_counter()
    got = await gather_gets(n_docs, ops)
    wall = time.perf_counter() - t0
    report(f"GET  -> {got:,} found, {ops / wall:,.0f}/s", wall)
    print()


get_found = 0


async def worker_gets(n_docs, n):
    global get_found
    conn = HttpConn(HOST, PORT)
    try:
        for _ in range(n):
            i = random.randrange(n_docs)
            status, _ = await conn.request("GET", f"/api/users/C{100000 + i:07d}")
            if status == 200:
                get_found += 1
    finally:
        await conn.close()


async def gather_gets(n_docs, ops):
    global get_found
    get_found = 0
    per = ops // 50
    await asyncio.gather(*(worker_gets(n_docs, per) for _ in range(50)))
    return get_found


upd_done = 0


async def worker_updates(n_docs, n):
    global upd_done
    conn = HttpConn(HOST, PORT)
    try:
        for _ in range(n):
            i = random.randrange(n_docs)
            new_phone = fake.phone_number()
            status, _ = await conn.request("PUT", f"/api/users/C{100000 + i:07d}",
                                           json.dumps({"$set": {"phone": new_phone}}).encode())
            if status == 200:
                upd_done += 1
    finally:
        await conn.close()


async def phase_updates(n_docs, ops):
    global upd_done
    upd_done = 0
    print(f"UPDATE $set phone ({ops:,} random)...")
    t0 = time.perf_counter()
    per = ops // 50
    await asyncio.gather(*(worker_updates(n_docs, per) for _ in range(50)))
    wall = time.perf_counter() - t0
    report(f"UPDATE -> {upd_done:,} ok, {ops / wall:,.0f}/s", wall)
    print()


# ── server lifecycle ─────────────────────────────────────────────────────────


def server_rss_mb(proc):
    try:
        with open(f"/proc/{proc.pid}/status") as f:
            for line in f:
                if line.startswith("VmRSS"):
                    return int(line.split()[1]) / 1024.0
    except Exception:
        pass
    return -1


async def wait_for_bind(proc, timeout=30):
    for _ in range(int(timeout * 10)):
        try:
            reader, writer = await asyncio.wait_for(
                asyncio.open_connection(HOST, PORT), timeout=1.0)
            writer.close()
            await writer.wait_closed()
            return True
        except Exception:
            if proc.poll() is not None:
                return False
            await asyncio.sleep(0.1)
    return False


# ── main ─────────────────────────────────────────────────────────────────────


async def main():
    global PORT
    parser = argparse.ArgumentParser(description="ClassyDB 1M-customer scale test")
    parser.add_argument("--customers", type=int, default=1_000_000)
    parser.add_argument("--concurrency", type=int, default=50)
    parser.add_argument("--port", type=int, default=PORT)
    args = parser.parse_args()
    PORT = args.port

    print("Starting ClassyDB fiber server...")
    proc = subprocess.Popen(
        ["../../bin/classyc", "-I", "../../include", "-I", "../../ext/ccchan",
         "../../examples/http-serve.c", "../../examples/http-serve-fibers.c",
         "classyc-db-server.cy", "-eg"],
        cwd="/home/rdavenpo/src/MIR/classyc/examples/beyond-demo",
        stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    if not await wait_for_bind(proc):
        print("Server failed to start", file=sys.stderr)
        proc.kill()
        return 1
    print(f"Server ready (pid {proc.pid}, RSS {server_rss_mb(proc):,.0f} MB).\n")

    t_start = time.perf_counter()
    await seed_customers(args.customers, args.concurrency)
    print(f"Server RSS after load: {server_rss_mb(proc):,.0f} MB\n")

    await phase_indexes(["state", "city"])
    print(f"Server RSS after indexes: {server_rss_mb(proc):,.0f} MB\n")

    await phase_queries(args.customers)
    await phase_gets(args.customers, 5000)
    await phase_updates(args.customers, 5000)

    print(f"Total wall time: {time.perf_counter() - t_start:.1f}s")
    print(f"Final server RSS: {server_rss_mb(proc):,.0f} MB")

    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
    return 0


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
