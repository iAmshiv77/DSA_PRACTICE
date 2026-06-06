# Saga Pattern (Distributed Transactions Across Services)

> A **saga** manages data consistency across multiple services without a distributed
> ACID transaction. It's a sequence of **local transactions**; if any step fails, the
> saga runs **compensating transactions** to undo the prior steps.

---

## The Problem: No Distributed ACID Transaction

In a microservices world, each service has its **own database** (Database-per-Service).
A single business operation — "place an order" — spans Order, Payment, Inventory, and
Shipping services. You can't wrap them in one `BEGIN...COMMIT`.

```
Order Service   (orders DB)
Payment Service (payments DB)     ← 4 separate databases
Inventory Svc   (inventory DB)
Shipping Svc    (shipping DB)

"Place order" must update all 4 atomically-ish. But:
  ✗ 2-Phase Commit (2PC) is slow, locks resources, and doesn't scale across services.
  ✗ A crash mid-way leaves inconsistent state (money taken, no stock reserved).
```

A saga trades **atomicity** for **eventual consistency**: each step commits locally,
and failures are *compensated*, not *rolled back*.

---

## Local Transactions + Compensations

```
Happy path:                     T1 → T2 → T3 → T4   (all commit locally)

Failure at T3 → undo in reverse:
   T1 → T2 → T3(FAIL)
              ↓
   C2 ← C1            (run compensating transactions backwards)

Ti = forward transaction   |   Ci = compensating transaction (semantic undo)
```

| Forward step (Ti) | Compensation (Ci) |
|-------------------|-------------------|
| Create order (PENDING) | Cancel order |
| Charge payment | Refund payment |
| Reserve inventory | Release inventory |
| Schedule shipping | Cancel shipping |

> Compensations are **semantic undos**, not rollbacks. You can't un-send an email —
> you send a "correction" email. You can't un-charge — you issue a refund.

---

## Two Coordination Styles

### A) Choreography — services react to each other's events (no central brain)
```
Order ──"OrderCreated"──► Payment ──"PaymentDone"──► Inventory ──"StockReserved"──► Shipping
                              │                          │
                       "PaymentFailed"            "OutOfStock"
                              ▼                          ▼
                       Order cancels            Payment refunds → Order cancels
```
- **Pros:** decentralized, no single point of failure, loose coupling.
- **Cons:** hard to track the overall flow; risk of cyclic event dependencies; the
  "where are we?" question has no single answer. Good for **few** steps.

### B) Orchestration — a central coordinator drives the saga
```
            ┌─────────────────── Saga Orchestrator ───────────────────┐
            │  step1: chargePayment → step2: reserveStock → step3: ship │
            └───────┬──────────────────┬───────────────────┬──────────┘
                    ▼                  ▼                   ▼
                 Payment           Inventory           Shipping
   On any failure, orchestrator issues compensations in reverse.
```
- **Pros:** centralized logic, easy to understand/monitor, explicit state machine,
  simpler for **complex** sagas (many steps, branching).
- **Cons:** the orchestrator is extra infrastructure and a potential bottleneck/SPOF.

| | Choreography | Orchestration |
|---|---|---|
| Control | Distributed (events) | Centralized (coordinator) |
| Coupling | Looser | Tighter to orchestrator |
| Visibility | Hard to trace | Easy (one state machine) |
| Best for | Simple, 2–4 steps | Complex, many steps/branches |

---

## Critical Design Requirements

- **Idempotency** — every step and compensation may be retried → must be idempotent
  (see [Idempotency](../01-Idempotency)). At-least-once messaging guarantees retries.
- **Commutative-ish ordering** — handle out-of-order/late events.
- **Semantic locking** — a `PENDING` status acts as a soft lock so other operations
  know the record is mid-saga (avoid the "isolation" anomalies sagas lack).
- **No isolation (ACID's I is gone)** — a parallel reader can see intermediate state.
  Use status flags, versioning, or re-reads to cope.
- **Compensations can fail too** — retry them; if truly stuck, escalate to a human
  (Scheduler-Agent-Supervisor pattern).
- Pairs naturally with the [Transactional Outbox](../06-Transactional-Outbox) to
  reliably publish each step's event.

---

## 🌍 Real-World Uses

- **E-commerce order placement** — the textbook saga: reserve stock → charge card →
  arrange shipping; on payment failure, release stock and cancel the order.
- **Travel booking (flight + hotel + car)** — book all three; if the car rental
  fails, cancel the flight and hotel. Each provider is a separate service/system.
- **Banking transfers across systems** — debit account A, credit account B; if the
  credit fails, reverse the debit. (Ledgers model this as compensating entries.)
- **Uber / ride platforms** — trip lifecycle (match → fare hold → ride → charge) is
  saga-like with compensations for cancellations.
- **AWS Step Functions** — managed orchestration service explicitly built to run
  sagas/workflows with built-in retry and catch/compensate branches.
- **Frameworks:** Axon, Eventuate Tram Sagas, Temporal/Cadence, Camunda/Zeebe,
  MassTransit — all provide saga orchestration.

---

## Interview Questions

**Q: Why not use 2-Phase Commit (2PC) across microservices?**
A: 2PC holds locks across all participants for the transaction's duration, doesn't
scale, hurts availability (a blocked coordinator stalls everyone), and many modern
data stores/brokers don't support it. Sagas give availability + eventual consistency
instead of distributed ACID.

**Q: Choreography vs orchestration — when each?**
A: Choreography (event-driven, decentralized) suits simple sagas with few steps and
loose coupling. Orchestration (central coordinator/state machine) suits complex
sagas with many steps and branching, where visibility and explicit control matter.

**Q: What's a compensating transaction? Give a non-reversible example.**
A: A semantic undo of a completed step. You can't truly roll back a committed local
transaction, so you apply a counter-action: refund for a charge, release for a
reservation. For non-reversible effects like a sent email, you send a corrective
follow-up rather than "un-sending."

**Q: Sagas lack isolation — what goes wrong and how do you mitigate?**
A: A concurrent reader can observe intermediate states (e.g. order PENDING with money
charged but stock not yet reserved). Mitigate with semantic locks (status flags),
versioning, re-reads, or by designing reads to tolerate pending states.

**Q: How do idempotency and sagas relate?**
A: Saga steps and compensations run over at-least-once messaging, so they get
retried and can be duplicated. Each must be idempotent (dedup on a saga/step ID), or
the saga produces double effects (double refund, double reserve).
