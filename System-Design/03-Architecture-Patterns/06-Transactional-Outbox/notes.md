# Transactional Outbox (+ CDC / Log Tailing / Polling Publisher)

> Solves the **dual-write problem**: how to update your database **and** publish a
> message/event **atomically**, when they live in two different systems (DB + broker)
> that can't share a transaction.

---

## The Dual-Write Problem

```
def place_order():
    db.save(order)              # ✅ committed to DB
    broker.publish("OrderPlaced") # ✗ broker is down → event LOST
    # Now: order exists, but payment/shipping never hear about it. State diverges.

# Or the reverse:
    broker.publish("OrderPlaced") # ✅ published
    db.save(order)                # ✗ DB crashes → "ghost" event for an order that doesn't exist
```

You cannot make "write to DB" and "publish to broker" atomic with a normal
transaction — they're separate systems. **Outbox** makes them atomic by routing the
event *through* the database.

---

## The Solution: Write the Event to an Outbox Table — in the Same Transaction

```
┌──────── ONE local DB transaction (ACID) ────────┐
│  INSERT INTO orders   (...)                       │   both commit together,
│  INSERT INTO outbox   (event='OrderPlaced', ...)  │   or both roll back
└───────────────────────────────────────────────────┘
                       │  (DB commit = single source of truth)
                       ▼
            ┌─────────────────────┐
            │  Message Relay       │  reads unpublished outbox rows,
            │  (separate process)  │  publishes to broker, marks them sent
            └──────────┬──────────┘
                       ▼
                 [ Kafka / SQS / RabbitMQ ] ──► consumers
```

The business row and the event commit **atomically**. A relay then reliably forwards
outbox rows to the broker. If the relay crashes, unsent rows are still in the table —
nothing is lost. Delivery is **at-least-once** (relay may republish after a crash) →
consumers must be [idempotent](../01-Idempotency).

```sql
CREATE TABLE outbox (
    id           BIGSERIAL PRIMARY KEY,
    aggregate_id VARCHAR,            -- e.g. order id
    event_type   VARCHAR,           -- 'OrderPlaced'
    payload      JSONB,
    created_at   TIMESTAMP DEFAULT now(),
    published_at TIMESTAMP          -- NULL until relay sends it
);
```

---

## Three Ways the Relay Reads the Outbox

| Approach | How | Trade-off |
|----------|-----|-----------|
| **Polling Publisher** | Relay periodically `SELECT * FROM outbox WHERE published_at IS NULL` | Simple; adds DB load + latency (poll interval); use `SKIP LOCKED` for multiple relays |
| **Transaction Log Tailing / CDC** | Tail the DB's write-ahead log (binlog/WAL) and emit events for outbox inserts | Near-real-time, no polling load; needs CDC infra (**Debezium**) |
| **Listen/Notify** | DB triggers (`pg_notify`) wake the relay on insert | Low latency; Postgres-specific |

```sql
-- Polling publisher with SKIP LOCKED so multiple relay instances don't double-send
SELECT * FROM outbox
  WHERE published_at IS NULL
  ORDER BY id
  FOR UPDATE SKIP LOCKED
  LIMIT 100;
-- publish each to broker, then:
UPDATE outbox SET published_at = now() WHERE id = ANY(:ids);
```

> **Change Data Capture (CDC)** with **Debezium** is the production-grade approach:
> it tails MySQL binlog / Postgres WAL and streams row changes to Kafka, so you often
> don't even need an explicit outbox table — you capture the order-table change
> itself. The outbox table gives you control over the *event shape*.

---

## Related: Inbox Pattern (consumer side)

The mirror image — to make a consumer idempotent, record processed message IDs in an
**inbox** table in the same transaction as the side effect, so a redelivered message
is detected and skipped. Outbox (reliable send) + Inbox (dedup receive) = end-to-end
exactly-once *effects*.

---

## 🌍 Real-World Uses

- **Debezium + Kafka Connect** — the de-facto open-source CDC stack; thousands of
  companies stream DB changes to Kafka this way (the "outbox" event router is a
  built-in Debezium SMT).
- **Event-driven microservices** — any system that must update its DB and notify
  other services reliably (orders, payments, user signups) uses outbox to avoid lost
  events.
- **Eventuate Tram (Chris Richardson)** — framework built around transactional
  outbox + saga.
- **Shopify, large e-commerce** — order/inventory changes propagate via CDC/outbox to
  search indexes, caches, analytics, and downstream services.
- **Data replication / search indexing** — keep Elasticsearch or a cache in sync with
  the primary DB by streaming outbox/CDC events (avoids dual-write drift).

---

## Interview Questions

**Q: What is the dual-write problem?**
A: Performing two writes to two systems (DB + message broker) that can't share a
transaction. If one succeeds and the other fails, state diverges — a saved order with
no published event, or a published event for an order that was never saved.

**Q: How does the outbox pattern fix it?**
A: Write the business row and an outbox row in the **same DB transaction**, so they
commit atomically. A separate relay later reads the outbox and publishes to the
broker, marking rows sent. The DB commit is the single source of truth; no event is
lost even if the broker is temporarily down.

**Q: Polling vs CDC for the relay?**
A: Polling is simple but adds DB load and latency (bounded by poll interval). CDC /
log tailing (Debezium) reads the DB's WAL/binlog for near-real-time publishing with
no polling load, at the cost of running CDC infrastructure.

**Q: Does outbox give exactly-once delivery?**
A: No — it gives at-least-once (the relay can republish after a crash before marking
rows sent). Combine with idempotent consumers (or the inbox pattern) for exactly-once
*effects*.

**Q: Why not just publish to the broker first, then save to the DB?**
A: If the DB write then fails, you've emitted a "ghost" event for state that doesn't
exist, and consumers act on a non-existent order. Outbox makes the DB the source of
truth so events only exist for committed state.
