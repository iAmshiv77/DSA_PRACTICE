# Event Sourcing

> Instead of storing the **current state** of an entity, store the **full sequence of
> events** that led to it. Current state is *derived* by replaying events. The event
> log is the source of truth.

---

## State-Oriented vs Event-Sourced

```
TRADITIONAL (state)                    EVENT SOURCING (events)
─────────────────────                  ─────────────────────────────────────────
account: { balance: 90 }               AccountOpened     {balance: 0}
                                       MoneyDeposited    {+100}
(you only see the CURRENT value;       MoneyWithdrawn    {-10}
 history is lost on each UPDATE)        ──────────────────────────────
                                       current state = fold(events) = 90
                                       (full history preserved, append-only)
```

The write store is an **append-only log of immutable events**. You never `UPDATE` or
`DELETE` — you only append. Current state = left-fold over the event stream.

```python
def current_balance(events):
    balance = 0
    for e in events:                       # replay
        if   e.type == "Deposited":  balance += e.amount
        elif e.type == "Withdrawn":  balance -= e.amount
    return balance
```

---

## Why Do This

| Benefit | Explanation |
|---------|-------------|
| **Complete audit log** | Every change is a recorded fact — *why* the state is what it is. Free, perfect history (huge for finance, healthcare, compliance). |
| **Temporal queries** | "What was the balance on March 1?" → replay events up to that timestamp. Time travel. |
| **Rebuild / fix** | Bug in a projection? Fix the code and **replay** all events to rebuild the read model. |
| **New read models for free** | Need a new view? Build a projection and replay history into it. |
| **Debugging** | Reproduce any past state exactly by replaying. |
| **Natural fit for events/messaging** | The events you store *are* the events you publish. |

---

## Snapshots (performance)

Replaying millions of events on every read is slow. Periodically save a **snapshot**
of state, then replay only events *after* it.

```
[ snapshot @ event 10000: balance=5000 ] + events 10001..10042  →  current state
                                                  (replay 42, not 10042)
```

---

## Projections (read side) — CQRS territory

You don't query the event log directly for reads. You build **projections**:
materialized read models updated as events arrive.

```
Event stream ──► Projection A (current balances table)
             ──► Projection B (monthly statement view)
             ──► Projection C (fraud-detection feature store)
```
This is exactly why Event Sourcing pairs with [CQRS](../07-CQRS): events = write model,
projections = read models.

---

## The Hard Parts (be honest in interviews)

- **Eventual consistency** — projections lag the event log.
- **Schema/versioning of events** — events are immutable and live forever; you must
  handle old event versions (upcasting). You can't just "migrate" them.
- **Querying** — you *cannot* ad-hoc query the log ("all accounts > $1000"); you need
  a projection for every query shape.
- **GDPR / "right to be forgotten"** — deleting data from an append-only log is hard;
  use crypto-shredding (encrypt per-user, throw away the key).
- **Complexity** — a big leap from CRUD. Use it where history/audit is a real
  requirement, not everywhere.

---

## 🌍 Real-World Uses

- **Banking & accounting ledgers** — the original event-sourced system: a ledger is
  literally an append-only log of transactions; balance is derived. Auditability is
  mandatory.
- **Version control (Git)** — Git is event sourcing for code: commits are immutable
  events, current tree is the fold. So is your database's own write-ahead log.
- **E-commerce order lifecycle** — order as a stream: `Created → PaymentReceived →
  Shipped → Delivered → Returned`; full history for support/disputes.
- **Trading / financial systems** — every order and fill stored as events for
  regulatory replay and reconstruction.
- **Collaborative editing / CRDTs** — operations as events replayed to converge state.
- **Tooling:** **EventStoreDB**, **Axon Framework**, **Kafka** (as an event log/store),
  **Marten** (Postgres-based) are purpose-built for event sourcing.

---

## Interview Questions

**Q: What is event sourcing and how does state work?**
A: You persist an append-only sequence of immutable domain events rather than current
state. Current state is derived by folding/replaying the events. The log is the
source of truth; nothing is updated or deleted.

**Q: What problem do snapshots solve?**
A: Replaying a long event stream on every load is slow. A snapshot stores state at a
point in time; you load the latest snapshot and replay only the events after it,
bounding replay cost.

**Q: Biggest challenges?**
A: Event schema evolution (immutable events live forever → upcasting old versions),
no ad-hoc querying (need projections per query), eventual consistency of projections,
GDPR deletion in an append-only store, and overall complexity vs CRUD.

**Q: How does it relate to CQRS?**
A: Event sourcing supplies the write model (event log); CQRS supplies read models as
projections of those events. They're independent but very commonly combined.

**Q: How do you delete a user's data under an append-only log (GDPR)?**
A: Crypto-shredding — encrypt each user's event data with a per-user key and delete
the key to render the events unreadable; or use tombstone/compaction strategies where
the store supports them.
