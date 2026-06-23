#!/usr/bin/env python3
"""customers.py — Python counterpart to classy-customers.cy.

Loads examples/customers.json, binds each record into a typed dataclass,
indexes them by id, then runs the same six database-style queries the
ClassyC demo runs.  Designed to be read side-by-side with the .cy file so
the trade-offs are visible.

The "binding" here is the classmethod `Customer.from_dict` — Python's stdlib
doesn't auto-derive that the way ClassyC's `(Customer)? d` cast does, so we
write a tiny per-class shim.  In a real project you'd reach for `pydantic`
or `msgspec` and get the auto-bind for free (sketched in the docstring of
Customer below).
"""

from __future__ import annotations

import json
from collections import Counter
from dataclasses import dataclass, field, fields
from statistics import mean

# ── Schema ───────────────────────────────────────────────────────────────────
# Plain data records.  Defaults make every field optional, mirroring the
# ClassyC `(Customer)? d` lenient cast behaviour.


@dataclass
class Address:
    street: str = ""
    city: str = ""
    state: str = ""
    zipCode: str = ""
    country: str = ""

    @classmethod
    def from_dict(cls, d: dict | None) -> "Address":
        if not d:
            return cls()
        return cls(**{f.name: d.get(f.name, f.default) for f in fields(cls)})


@dataclass
class Customer:
    """One customer record.

    With `pydantic`, the whole `from_dict` ceremony would be replaced by
    `class Customer(BaseModel): ...` plus `Customer.model_validate(rec)` —
    the closest analogue to ClassyC's `(Customer)? rec` cast.
    """

    id: int = 0
    firstName: str = ""
    lastName: str = ""
    fullName: str = ""
    email: str = ""
    phone: str = ""
    address: Address = field(default_factory=Address)
    dateOfBirth: str = ""
    sex: str = ""
    race: str = ""
    eyeColor: str = ""
    heightInches: int = 0
    weightLbs: int = 0
    occupation: str = ""

    @classmethod
    def from_dict(cls, d: dict) -> "Customer":
        # Lenient: any missing field falls back to the dataclass default.
        kwargs = {
            f.name: d.get(f.name, f.default) for f in fields(cls) if f.name != "address"
        }
        kwargs["address"] = Address.from_dict(d.get("address"))
        return cls(**kwargs)


# ── Ingest ───────────────────────────────────────────────────────────────────


def load_customers(path: str) -> dict[int, Customer]:
    """Read the JSON file, bind each element, index by id.

    Python's dict is the natural counterpart to ClassyC's `Map<int, Customer*>`;
    we lean on insertion order (guaranteed since 3.7) the same way."""
    with open(path) as f:
        raw = json.load(f)
    customers = {c["id"]: Customer.from_dict(c) for c in raw}
    print(f"loaded {len(customers)} customers from {path}")
    return customers


# ── Queries ──────────────────────────────────────────────────────────────────


def q_lookup(db: dict[int, Customer], id: int) -> None:
    print(f"\n[Q] lookup id={id}")
    c = db.get(id)
    if c is None:
        print("  (no such customer)")
        return
    print(
        f"  #{c.id}  {c.lastName}, {c.firstName} — "
        f"{c.address.city}, {c.address.state} {c.address.zipCode}"
    )
    print(f"       {c.occupation}  |  {c.email}  |  {c.phone}")


def q_by_state(db: dict[int, Customer], state: str) -> None:
    print(f"\n[Q] customers in state={state}")
    hits = [c for c in db.values() if c.address.state == state]
    if not hits:
        print("  (none)")
        return
    for c in hits:
        print(f"  #{c.id:<3} {c.firstName} {c.lastName}  ({c.address.city})")


def q_by_last_name(db: dict[int, Customer], last: str) -> None:
    print(f"\n[Q] customers with lastName={last}")
    hits = [c for c in db.values() if c.lastName == last]
    if not hits:
        print("  (none)")
        return
    for c in hits:
        print(f"  #{c.id:<3} {c.firstName} {c.lastName}  — {c.occupation}")


def q_count_by_occupation(db: dict[int, Customer]) -> None:
    print("\n[Q] count by occupation (top of insertion order)")
    tally = Counter(c.occupation or "(unknown)" for c in db.values())
    for occ, n in tally.items():
        print(f"  {n:3d}  {occ}")


def q_average_height(db: dict[int, Customer]) -> None:
    heights = [c.heightInches for c in db.values() if c.heightInches > 0]
    if not heights:
        print("\n[Q] average height: (no data)")
        return
    avg = mean(heights)
    print(f"\n[Q] average height: {avg:.1f} inches over {len(heights)} customers")


def q_top3_weight(db: dict[int, Customer]) -> None:
    print("\n[Q] top 3 heaviest customers")
    top = sorted(db.values(), key=lambda c: c.weightLbs, reverse=True)[:3]
    for c in top:
        print(
            f"  {c.weightLbs} lbs  — #{c.id} {c.firstName} {c.lastName} ({c.occupation})"
        )


# ── Driver ───────────────────────────────────────────────────────────────────


def main() -> None:
    print("=== Python customers demo ===")
    db = load_customers("examples/customers.json")
    print(f"\ndatabase: {len(db)} records, {len(db)} unique ids")
    q_lookup(db, 5)
    q_lookup(db, 999)
    q_by_state(db, "CA")
    q_by_last_name(db, "Smith")
    q_count_by_occupation(db)
    q_average_height(db)
    q_top3_weight(db)
    print("\ndone.")


if __name__ == "__main__":
    main()
