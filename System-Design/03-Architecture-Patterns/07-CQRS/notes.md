# CQRS — Command Query Responsibility Segregation

> Separate the **write model** (commands that change state) from the **read model**
> (queries that return data), using different models — and often different data
> stores — for each.

---

## The Idea

In a traditional CRUD app, one model serves both reads and writes:
```
        ┌──────────────┐
Client ►│  One Model   │◄─ reads AND writes hit the same tables/objects
        │  (CRUD)      │
        └──────────────┘
```
This couples two very different needs. **Writes** want normalization, validation, and
business rules. **Reads** want denormalized, query-optimized shapes — often many
different shapes for different screens. CQRS splits them:

```
            COMMANDS (write)                         QUERIES (read)
Client ──► Command Handler ──► Write DB        Client ──► Query Handler ──► Read DB
           (validate, apply    (normalized,                (denormalized,
            business rules)      source of truth)           optimized views)
                   │                                              ▲
                   └──── events / sync ──► project into ──────────┘
                         (read model is updated from writes)
```

- **Command** = intent to change state (`PlaceOrder`, `CancelSubscription`). Returns
  success/failure, *not* data. Goes through business rules.
- **Query** = request for data (`GetOrderHistory`). Side-effect free. Hits read-
  optimized views.
- The read store is kept eventually consistent with the write store via events /
  [outbox](../06-Transactional-Outbox) / CDC — this is where [Materialized View](#)
  comes in.

---

## Levels of CQRS (don't over-commit)

```
Level 1: Same DB, separate read/write MODELS in code        ← lightweight, common
Level 2: Same DB, separate read tables (materialized views)
Level 3: Separate read & write DATABASES, synced via events ← full CQRS, complex
         e.g. writes → Postgres (normalized);
              reads  → Elasticsearch / Redis / denormalized replica
```
Start at level 1. Only move to separate stores when read and write scaling/shapes
genuinely diverge — full CQRS adds eventual consistency and sync complexity.

---

## Why Use It

| Benefit | Explanation |
|---------|-------------|
| **Independent scaling** | Reads usually vastly outnumber writes — scale read replicas separately (10 read nodes, 2 write nodes). |
| **Optimized models** | Read side denormalized for fast queries; write side normalized for integrity. |
| **Multiple read views** | Same data projected into many shapes (search index, dashboard, mobile) without bloating the write model. |
| **Pairs with Event Sourcing** | The event log is the write side; read models are projections rebuilt from events. |
| **Security/clarity** | Reads and writes have separate, smaller, purpose-built interfaces (ISP at the architecture level). |

---

## The Cost

- **Eventual consistency** — the read model lags the write model by the sync delay.
  A user may not immediately see their own write ("read-your-writes" needs care:
  read from the write model or wait for projection).
- **More moving parts** — two models, sync pipeline, projection code.
- **Overkill for simple CRUD** — most apps don't need it. Apply per *bounded context*,
  not globally.

---

## CQRS + Event Sourcing (natural pair)

```
Command ──► validate ──► append EVENTS to event store (write side = source of truth)
                              │
                              └──► project events into read models (read side)
                                   (one projection per query shape)
```
Event Sourcing provides the write model (an append-only event log); CQRS provides the
read models (projections of those events). They're independent but frequently
combined. → See [Event Sourcing](../08-Event-Sourcing).

---

## 🌍 Real-World Uses

- **High-read systems (e-commerce product catalogs, social feeds)** — writes to a
  normalized store, reads served from denormalized caches/search indexes
  (Elasticsearch) kept in sync via events.
- **Banking / trading** — commands (transfers, trades) go through strict validation;
  separate read models power dashboards, statements, and analytics.
- **Order management** — write side enforces order rules; read side has pre-joined
  "order summary" views per UI (customer app, ops dashboard, warehouse).
- **Microsoft / Azure reference architectures** and frameworks like **Axon** (Java),
  **EventStoreDB**, **MediatR** (.NET — popularized command/query handlers) implement
  CQRS directly.
- **Collaborative apps** — separate write (intent) from read (projected document
  state) to scale viewers independently of editors.

---

## Interview Questions

**Q: What does CQRS actually separate?**
A: The model and path for **commands** (state changes, business rules, write-
optimized) from the model and path for **queries** (read-optimized, side-effect-free
views) — optionally backed by different data stores synced via events.

**Q: Biggest downside of full CQRS?**
A: Eventual consistency between write and read stores (the read model lags), plus the
operational complexity of a sync pipeline and duplicate models. It's overkill for
simple CRUD.

**Q: How does the read model stay in sync with the write model?**
A: The write side emits events (often via outbox/CDC); projections consume them and
update read models. Sync is asynchronous, so reads are eventually consistent.

**Q: How is CQRS related to Event Sourcing?**
A: They're separate but complementary. Event Sourcing makes the write model an
append-only event log; CQRS builds read models as projections of those events. You
can use either without the other.

**Q: How do you handle "read your own write" under CQRS?**
A: Either read that specific record from the write model, block until the projection
catches up, or return the just-written value optimistically from the command result —
because the async read model may not reflect the write yet.
