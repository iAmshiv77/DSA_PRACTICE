# HLD: Payment Gateway / Payment System

## Requirements

**Functional:**
- Initiate payment (card, UPI, wallet, net banking)
- Validate and process payment with bank/PSP
- Handle payment success/failure/pending
- Refunds
- Transaction history
- Webhook notifications to merchants

**Non-Functional:**
- Exactly-once processing (no double charges)
- High availability (99.999% — five nines)
- Fraud detection
- PCI DSS compliance
- Audit trail for all transactions
- < 2 second end-to-end payment latency

---

## Capacity Estimation

```
Transactions: 1M/day (e-commerce) to 100M/day (PayTM/PhonePe scale)
Peak:         Festival sales → 10x normal = 10M TPS burst
Read:         Transaction history queries ~ 10x writes

Storage per transaction: ~1 KB → 100M/day × 1KB = 100 GB/day
Retention: 7 years (regulatory) → 100 GB × 365 × 7 = 255 TB
```

---

## High-Level Architecture

```
┌──────────┐   API    ┌─────────────────┐    ┌─────────────────────┐
│  Merchant │─────────►│  API Gateway     │───►│  Payment Service    │
│  Website │          │  (auth, TLS,     │    │  (orchestrator)     │
└──────────┘          │   rate limit)    │    └──────────┬──────────┘
                      └─────────────────┘               │
                                         ┌───────────────┼───────────────┐
                                         ▼               ▼               ▼
                                  ┌────────────┐  ┌──────────┐  ┌──────────────┐
                                  │  Fraud     │  │  Ledger  │  │  Bank/PSP    │
                                  │  Detection │  │  Service │  │  Connector   │
                                  └────────────┘  └──────────┘  └──────┬───────┘
                                         ▲               ▲             │
                                         └───────────────┤             │
                                                         │    ┌────────▼────────┐
                                                         │    │  Bank API /     │
                                                   ┌─────┴──┐ │  Visa/MC/UPI    │
                                                   │  DB    │ └─────────────────┘
                                                   │  (SQL) │
                                                   └────────┘
```

---

## Core Design: Idempotency (Most Critical)

### Why It Matters
```
Scenario: User clicks "Pay" → request sent → network timeout → user clicks again
          → Two charges to user's card? NO. This must NOT happen.

Solution: Idempotency key
  - Client generates unique key per payment attempt (UUID)
  - Header: X-Idempotency-Key: uuid-v4
  - Server: check if this key was seen before
    - YES → return cached response (no re-processing)
    - NO  → process, store result with key (TTL 24h)
```

### Idempotency Implementation
```sql
CREATE TABLE idempotency_keys (
    key         VARCHAR(64) PRIMARY KEY,
    status      ENUM('PROCESSING', 'DONE'),
    response    JSON,
    created_at  TIMESTAMP,
    expires_at  TIMESTAMP    -- TTL 24h
);
```

```python
def process_payment(idempotency_key, payment_request):
    # 1. Check for existing result
    existing = db.get_idempotency_key(idempotency_key)
    if existing and existing.status == 'DONE':
        return existing.response

    # 2. Lock: set status to PROCESSING (atomic)
    if not db.set_processing(idempotency_key):
        raise ConflictError("Request in progress")  # 409

    # 3. Process payment
    result = charge_bank(payment_request)

    # 4. Store result and mark DONE
    db.complete_idempotency_key(idempotency_key, result)
    return result
```

---

## Payment States (State Machine)

```
INITIATED → FRAUD_CHECK → AUTHORIZED → CAPTURED → SETTLED
                  ↓              ↓
              DECLINED       CANCELLED
                              (before capture)
                  ↓
               REFUNDED  ←── SETTLED

Separate steps:
  Authorization: Bank verifies card has funds, holds amount
  Capture:       Merchant claims the authorized amount (within 7 days)
  Settlement:    Actual money movement between banks (T+1 or T+2)
```

---

## Database Design (PostgreSQL)

```sql
-- Payments table (source of truth)
CREATE TABLE payments (
    payment_id      UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    merchant_id     BIGINT NOT NULL,
    customer_id     BIGINT,
    amount          DECIMAL(19, 4) NOT NULL,
    currency        CHAR(3) NOT NULL,        -- ISO 4217 (INR, USD)
    status          payment_status NOT NULL DEFAULT 'INITIATED',
    payment_method  TEXT NOT NULL,           -- card, upi, wallet
    idempotency_key VARCHAR(64) UNIQUE,
    metadata        JSONB,
    created_at      TIMESTAMPTZ DEFAULT NOW(),
    updated_at      TIMESTAMPTZ DEFAULT NOW()
);

-- Ledger (double-entry bookkeeping — immutable)
CREATE TABLE ledger_entries (
    entry_id        BIGSERIAL PRIMARY KEY,
    payment_id      UUID REFERENCES payments(payment_id),
    account_from    BIGINT NOT NULL,
    account_to      BIGINT NOT NULL,
    amount          DECIMAL(19, 4) NOT NULL,
    entry_type      TEXT NOT NULL,           -- debit/credit
    created_at      TIMESTAMPTZ DEFAULT NOW()
    -- NEVER UPDATE, only INSERT
);

-- Audit log (append-only)
CREATE TABLE payment_events (
    event_id        BIGSERIAL PRIMARY KEY,
    payment_id      UUID NOT NULL,
    event_type      TEXT NOT NULL,           -- status_changed, fraud_flagged
    old_status      TEXT,
    new_status      TEXT,
    actor           TEXT,                    -- system/user/admin
    metadata        JSONB,
    created_at      TIMESTAMPTZ DEFAULT NOW()
);
```

---

## Double-Entry Bookkeeping (Ledger)

```
Every money movement = two entries:
  Debit (money leaves account) + Credit (money enters account)

Example: Customer pays ₹1000 to merchant
  Debit:  Customer account  -₹1000
  Credit: Merchant account  +₹1000

SUM of all entries in ledger must always = 0
If SUM ≠ 0 → data corruption (immediate alert)

Never UPDATE ledger entries. Only INSERT.
Reconciliation: periodically verify sum = 0
```

---

## Fraud Detection

```
Signals:
  - Velocity: > 3 transactions in 5 minutes from same device
  - Location mismatch: card billing address ≠ shipping address
  - Device fingerprint mismatch
  - New device + high value transaction
  - Known fraudulent IP / card number (blacklists)
  - Unusual amount pattern (e.g., many small < $10 transactions)

Architecture:
  Transaction → Feature extraction → ML Model (real-time, < 200ms)
                                    → Risk score (0–100)

  score < 30:  auto-approve
  30–70:       additional verification (OTP / 3D Secure)
  score > 70:  decline or manual review

Tools: Rule engine (Drools) + ML model (XGBoost) + Redis for velocity checks
```

---

## Handling Bank API Failures

```
Problem: Bank API call times out after 3 seconds.
         Did the charge succeed? Unknown.

Solution: Two-phase approach
  1. Before calling bank: write PENDING record
  2. Call bank API (3 retries with backoff)
  3. On success: update to CAPTURED
  4. On failure: update to FAILED
  5. On timeout/unknown: status = PROCESSING
     → Background reconciliation job queries bank for status
     → Update when confirmed

Reconciliation Job:
  Every 5 minutes:
    SELECT * FROM payments WHERE status = 'PROCESSING' AND age > 5min
    → Query bank API for each → update status
```

---

## Webhook to Merchants

```
POST /merchant-webhook
{
  "payment_id": "...",
  "status": "captured",
  "amount": 1000,
  "signature": "HMAC-SHA256(payload, merchant_secret)"
}

Delivery:
  1. Payment status updates → Kafka topic "payment_events"
  2. Webhook service consumes → POST to merchant URL
  3. Retry: exponential backoff (1s, 2s, 4s, 8s... up to 24h)
  4. If merchant returns non-2xx → retry
  5. After 72h of failures → alert, mark as undelivered

Security:
  - Sign payload with HMAC
  - Merchant verifies signature before processing
  - Idempotency: merchant should handle duplicate webhooks
```

---

## Edge Cases

| Edge Case | Solution |
|-----------|---------|
| Double charge | Idempotency key per transaction |
| Payment stuck in PROCESSING | Reconciliation job every 5 min |
| DB failure during payment | Write to WAL first; replay on recovery |
| Bank returns different amount than authorized | Reject; alert fraud team |
| Refund for captured payment | Create refund transaction; reverse ledger entries |
| Currency conversion | Lock exchange rate at authorization time; store in payment record |
| Partial capture | Authorization for ₹1000, capture ₹800 (item out of stock) |
| Card expiry between auth and capture | Re-authorize with new card details |

---

## Interview Questions

**Q: How do you ensure exactly-once payment processing?**
A: (1) Idempotency key per payment attempt — client generates UUID, server deduplicates. (2) Database transactions (ACID) — status update is atomic. (3) Outbox pattern for events — DB write + event publish in same transaction.

**Q: What is the outbox pattern?**
A: Instead of writing to DB and publishing to Kafka separately (can fail partially), write event to `outbox` table in the SAME DB transaction. Separate CDC service reads outbox and publishes to Kafka. Guarantees exactly-once event emission.

**Q: How do you handle PCI DSS compliance?**
A: Never store raw card numbers. Tokenize: card number → random token. Actual card data stored in PCI-compliant vault (Vault by HashiCorp, Stripe's vault). Only the vault handles card data. Your system handles tokens only.

**Q: How would you scale to 10M transactions per day?**
A: (1) Shard payments table by merchant_id, (2) Read replicas for transaction history queries, (3) Redis for idempotency key lookups (faster than DB), (4) Kafka for async event processing (webhooks, ledger entries), (5) Separate read model (CQRS) for reporting.
