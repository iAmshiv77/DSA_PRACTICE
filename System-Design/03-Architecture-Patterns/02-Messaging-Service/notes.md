# Messaging Service (Async Communication & Event-Driven Architecture)

> A messaging service lets services talk **asynchronously** by passing messages
> through a broker, instead of calling each other directly (synchronously). This
> decouples producers from consumers in **time, space, and rate**.

---

## Sync vs Async — Why Messaging Exists

```
SYNCHRONOUS (request/response, e.g. REST/gRPC)
  Order ──HTTP──► Payment ──HTTP──► Inventory ──HTTP──► Email
  ✗ Order is BLOCKED until the whole chain returns
  ✗ Email service down → the whole order fails (tight coupling)
  ✗ Traffic spike → every service must scale together

ASYNCHRONOUS (messaging)
  Order ──"OrderPlaced" event──► [ Broker ] ──► Payment   (consumes when ready)
                                            ├──► Inventory
                                            └──► Email
  ✓ Order returns immediately after publishing
  ✓ Email down → message waits in queue, processed later (loose coupling)
  ✓ Each consumer scales independently; broker absorbs spikes (buffering)
```

| Dimension | Decoupled how |
|-----------|---------------|
| **Time** | Consumer needn't be up when producer sends (broker buffers) |
| **Space** | Producer doesn't know consumers' addresses (just the topic) |
| **Rate** | Broker absorbs bursts; consumers pull at their own pace (back-pressure) |

---

## Core Messaging Models

### 1. Point-to-Point (Queue) — one message, one consumer
```
Producer ──► [ Queue ] ──► Consumer A
                       ╲──► Consumer B    (A and B compete; each msg goes to ONE)
```
Work distribution / task queues. N workers share the load (competing consumers).
→ See [`../03-Queue-Service/`](../03-Queue-Service).

### 2. Publish/Subscribe (Topic) — one message, every subscriber
```
                       ┌──► Subscriber 1 (Email)
Publisher ──► [ Topic ]├──► Subscriber 2 (Analytics)
                       └──► Subscriber 3 (Audit)      (ALL get a copy)
```
Event broadcast / fan-out. Each subscriber gets every message independently.

### 3. Event Streaming (log) — durable, replayable, ordered
```
Producer ──► [ append-only log: 0 1 2 3 4 5 ... ] ──► Consumer group A (offset 5)
                                                  └──► Consumer group B (offset 2, replaying)
```
Kafka-style. Messages persist; consumers track an **offset** and can replay history.
→ See [`../../01-HLD/14-Distributed-Message-Queue/`](../../01-HLD/14-Distributed-Message-Queue).

---

## Message Anatomy

```json
{
  "messageId":   "uuid-123",          // for idempotency / dedup
  "type":        "OrderPlaced",       // routing & schema selection
  "timestamp":   "2026-06-06T10:00Z",
  "correlationId":"trace-abc",        // tie related messages across services
  "payload":     { "orderId": 42, "amount": 5000 },
  "headers":     { "retryCount": 0, "source": "order-svc" }
}
```

---

## Delivery Guarantees (the trade-off triangle)

| Guarantee | Mechanism | Risk |
|-----------|-----------|------|
| **At-most-once** | ack before processing | lost messages on crash |
| **At-least-once** | ack after processing | **duplicates** → consumer must be idempotent |
| **Exactly-once (effect)** | at-least-once + dedup/idempotency | complexity |

> At-least-once is the pragmatic default → **consumers must be idempotent.** This
> is why [Idempotency](../01-Idempotency) and messaging are taught together.

---

## Reliability Patterns

```
Dead Letter Queue (DLQ)
  msg fails N times → move to DLQ → alert → human/automated review
  (prevents a "poison pill" message from blocking the queue forever)

Retry with backoff
  fail → retry after 1s, 2s, 4s, 8s ... (exponential + jitter)
  avoids hammering a struggling downstream

Outbox Pattern (atomic DB write + publish)
  Problem: write order to DB AND publish "OrderPlaced" — if one succeeds and the
           other fails, state diverges (dual-write problem).
  Solution: in ONE DB transaction, write the order AND an "outbox" row.
            A separate relay reads the outbox and publishes to the broker.
            DB commit = single source of truth → no lost/ghost events.

Saga (distributed transaction across services)
  Order → Payment → Inventory → Shipping, each a local txn.
  Any step fails → run COMPENSATING actions in reverse (refund, restock).
  Replaces 2PC, which doesn't scale across services.
```

---

## Broker Comparison

| Broker | Model | Strength | Use when |
|--------|-------|----------|----------|
| **Kafka** | Log/stream | Huge throughput, replay, ordering per partition | Event streaming, analytics, event sourcing |
| **RabbitMQ** | Queue + pub/sub | Flexible routing (exchanges), mature | Task queues, complex routing, RPC |
| **AWS SQS** | Queue | Fully managed, simple, scales infinitely | Decoupling on AWS, work queues |
| **AWS SNS** | Pub/sub | Fan-out to SQS/Lambda/HTTP/email | Notifications, fan-out (often SNS→SQS) |
| **Google Pub/Sub** | Pub/sub | Managed, global, auto-scale | GCP event-driven systems |
| **Redis Streams** | Log (lightweight) | Low latency, simple | Lightweight streaming, already using Redis |
| **NATS** | Pub/sub | Ultra-low latency, tiny footprint | IoT, microservice messaging, edge |

---

## 🌍 Real-World Uses

- **Uber** — driver location, trip state, surge pricing all flow through Kafka
  (trillions of messages/day); services react to events instead of polling.
- **Netflix** — Kafka (Keystone pipeline) ingests billions of events/day for
  real-time monitoring, recommendations, and A/B analytics.
- **E-commerce checkout (Amazon-style)** — "OrderPlaced" event fans out to
  payment, inventory, shipping, email, loyalty — each consumes independently, so a
  slow email service never blocks checkout.
- **LinkedIn** — birthplace of Kafka; activity feeds, metrics, and the social graph
  pipeline are all message-driven.
- **AWS SNS → SQS fan-out** — the standard cloud pattern: publish once to SNS, fan
  out to many SQS queues each owned by a different microservice.
- **Slack / Discord** — message delivery, notifications, and presence updates ride
  pub/sub so millions of clients get real-time fan-out.
- **Ride/food delivery (DoorDash, Swiggy)** — order lifecycle events
  (placed → accepted → preparing → picked-up → delivered) propagate via the broker
  to customer app, restaurant app, and driver app simultaneously.
- **Bank/fintech ledgers** — transactions published as events; fraud detection,
  notifications, and reporting subscribe without coupling to the core ledger.

---

## When NOT to Use Messaging

- You need an **immediate synchronous answer** (e.g. "is this username taken?") →
  use REST/gRPC. Async adds latency and complexity.
- **Simple CRUD** with no fan-out, no spikes, no decoupling need → a direct call is simpler.
- The team can't operate a broker → managed (SQS/SNS/Pub/Sub) or nothing; a
  mis-run broker is a new single point of failure.

> Rule of thumb: **commands** (do this now, tell me the result) lean synchronous;
> **events** (this happened, react however you like) lean asynchronous.

---

## Interview Questions

**Q: Queue vs Pub/Sub vs Streaming — when each?**
A: Queue = one consumer per message, work distribution (task queues). Pub/Sub =
every subscriber gets a copy, event broadcast/fan-out. Streaming (Kafka) = durable,
ordered, replayable log with consumer offsets — for analytics, event sourcing, and
when you need history/replay.

**Q: What's the dual-write problem and how does the outbox pattern solve it?**
A: Writing to the DB and publishing to the broker as two separate operations can
partially fail (DB ok, publish lost — or vice versa), diverging state. Outbox writes
the business row and an outbox row in **one DB transaction**; a relay later publishes
from the outbox. The DB commit is the single source of truth.

**Q: How do you guarantee message ordering?**
A: Order is only guaranteed within a partition/queue with a single consumer. Route
related messages to the same partition via a key (e.g. `hash(orderId)`), so all
events for one order stay ordered. Global ordering across partitions isn't guaranteed.

**Q: How do you handle a poison-pill message that always fails?**
A: Retry with exponential backoff up to N times, then route it to a Dead Letter
Queue and alert. The DLQ keeps one bad message from blocking the whole queue and
preserves it for investigation.

**Q: Consumers get duplicate messages — why, and what do you do?**
A: At-least-once delivery means a consumer can crash after processing but before
ack'ing, so the broker redelivers. Make consumers idempotent — dedup on `messageId`
or design effects to be naturally repeatable.
