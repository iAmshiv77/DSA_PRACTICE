# Idempotency

> **An operation is idempotent if performing it once or many times produces the
> same result.** `GET`, `PUT`, `DELETE` are idempotent; a naive `POST /charge` is not.

In distributed systems, **retries are guaranteed** (timeouts, network blips,
at-least-once message delivery, user double-clicks). Without idempotency, a retry
charges the card twice, ships two orders, or sends two emails. Idempotency is how
you make "at-least-once" delivery *safe*.

---

## Why You Can't Avoid Duplicates

```
Client ──POST /charge──► Server ──(charges card OK)──► ✗ response lost (timeout)
Client (thinks it failed) ──POST /charge (retry)──► Server ──charges AGAIN──► 💸💸
```

The client **cannot tell** "request never arrived" from "response was lost." Its
only safe move is to retry. The server must make the retry harmless.

```
At-most-once  : may lose work, never duplicates   (rarely acceptable)
At-least-once : never loses, MAY duplicate         (the common default)
Exactly-once  : the goal — achieved as
                at-least-once delivery + idempotent processing
```
> "Exactly-once delivery" is largely a myth at the transport layer. You get
> **exactly-once *effects*** by combining at-least-once delivery with idempotent
> consumers. Idempotency is the load-bearing half.

---

## The Idempotency Key Pattern (the standard solution)

The client generates a unique **idempotency key** (UUID) per logical operation and
sends it with every retry of that operation. The server records the key + result;
on a repeat key it returns the stored result instead of re-executing.

```
POST /payments
Idempotency-Key: 9f1c...-uuid
{ "amount": 5000, "currency": "USD" }
```

```
                ┌──────────────────────────────────────────────┐
request +key →  │ 1. Look up key in idempotency store           │
                │                                               │
                │   FOUND + completed → return stored response  │ (no re-execute)
                │   FOUND + in-progress → 409 / wait            │ (concurrent retry)
                │   NOT FOUND → 2. insert key (in-progress)     │
                │                3. execute the real operation  │
                │                4. store result, mark complete │
                │                5. return result               │
                └──────────────────────────────────────────────┘
```

### Reference implementation (pseudo-code)
```python
def create_payment(key, payload):
    # Atomic claim: INSERT ... ON CONFLICT DO NOTHING
    claimed = db.try_insert(IdempotencyRecord(
        key=key, status="IN_PROGRESS", request_hash=hash(payload)))

    if not claimed:
        existing = db.get(key)
        if existing.request_hash != hash(payload):
            raise Conflict("key reused with a different body")   # guard against misuse
        if existing.status == "COMPLETED":
            return existing.response          # ← safe replay
        raise Conflict("original request still in progress")     # 409, client retries later

    # We own the key — do the real work exactly once
    result = charge_card(payload)             # the non-idempotent side effect
    db.update(key, status="COMPLETED", response=result)
    return result
```

**Critical details (the senior parts):**
- The key claim must be **atomic** — `INSERT ... ON CONFLICT` / `SETNX`, *not*
  `SELECT` then `INSERT` (that races: two retries both see "not found").
- **Bind the key to the request body** (`request_hash`). If the same key arrives
  with a different payload, reject it — that's a client bug, not a retry.
- **Set a TTL** on stored keys (e.g. 24h). They can't live forever.
- **Wrap key-write + side-effect in one transaction** where possible, or use the
  [outbox pattern](#) so the record and the effect can't diverge on a crash.

---

## Three Ways to Achieve Idempotency

| Strategy | How | Best for |
|----------|-----|----------|
| **Idempotency key** | Client-supplied UUID, server dedups | External APIs, payments, order creation (POST) |
| **Natural idempotency** | Design the op to be inherently repeatable | `SET status='paid'` (not `balance -= 10`), upserts, `PUT` |
| **Dedup on a unique constraint** | Unique index on a business key | `INSERT order WHERE order_id unique` → 2nd insert fails harmlessly |

```sql
-- Natural idempotency: absolute state, not relative delta
UPDATE accounts SET status = 'ACTIVE' WHERE id = 42;   -- ✅ idempotent (run twice = same)
UPDATE accounts SET balance = balance - 10 WHERE id=42; -- ❌ NOT idempotent (double-debit)

-- Dedup via unique constraint
CREATE UNIQUE INDEX ux_orders_idem ON orders(idempotency_key);
INSERT INTO orders(...) VALUES (...);   -- duplicate key → constraint error → ignore/return existing
```

---

## 🌍 Real-World Uses

- **Stripe** — the canonical example. Every mutating API call accepts an
  `Idempotency-Key` header; Stripe stores the result for 24h and replays it on retry.
  ([stripe.com/docs/api/idempotent_requests](https://stripe.com/docs/api/idempotent_requests))
- **PayPal / Razorpay / Adyen** — same header-based idempotency for charges & refunds.
- **AWS** — `ClientToken` / `ClientRequestToken` on `RunInstances`, `CreateVolume`,
  `TransactWriteItems` etc. so SDK retries don't create duplicate resources.
- **Kafka** — the *idempotent producer* (`enable.idempotence=true`) uses a
  producer ID + per-partition sequence number so broker retries don't write dupes.
- **Stripe/Shopify webhooks** — each event has a stable ID; **consumers must dedup
  on it** because providers deliver at-least-once (you WILL get the same webhook twice).
- **Payment double-click protection** — frontend generates one idempotency key per
  checkout session, reused if the user mashes "Pay".
- **HTTP semantics** — `PUT /users/42 {…}` and `DELETE /users/42` are idempotent by
  design; `POST /users` is not — which is exactly why POST needs idempotency keys.

---

## Edge Cases

| Edge case | Handling |
|-----------|----------|
| Two retries arrive simultaneously | Atomic claim (`ON CONFLICT`); loser gets 409 / waits |
| Crash *after* side-effect, *before* storing result | Outbox/transaction so they commit together; on replay, detect completed effect |
| Same key, different body | Reject (`422`/`409`) — protects against accidental key reuse |
| Key store grows unbounded | TTL + background cleanup |
| Non-idempotent downstream (3rd-party charge) | Forward *your* idempotency key to them too (chain it) |
| Multi-step workflow | Make each step idempotent + use a saga with idempotent compensations |

---

## Interview Questions

**Q: Difference between idempotency and exactly-once delivery?**
A: Exactly-once *delivery* is nearly impossible over a network. What's achievable
is exactly-once *effect*: at-least-once delivery + idempotent processing. Idempotency
is the practical mechanism; "exactly-once" is the outcome.

**Q: How does Stripe implement idempotency keys?**
A: Client sends an `Idempotency-Key` header. Stripe atomically records it, executes
the charge once, stores the response ~24h, and replays the stored response for any
retry with the same key — rejecting reuse with a different body.

**Q: Why must claiming the key be atomic?**
A: With `SELECT`-then-`INSERT`, two concurrent retries can both read "not found"
and both execute the side effect. An atomic `INSERT ... ON CONFLICT` / `SETNX`
ensures exactly one claimant.

**Q: How do you make a balance debit idempotent?**
A: Don't model it as a relative delta (`balance -= 10`). Use a unique transaction
record (insert a `ledger_entry` keyed by idempotency key — second insert fails) and
derive balance from the ledger; or guard the debit with the idempotency key so it
applies once.

**Q: You consume Stripe webhooks — how do you avoid double-processing?**
A: Webhooks are at-least-once. Store each processed event ID; before handling,
check/insert it atomically and skip if already seen. Make the handler itself
idempotent as defense in depth.
