# Queue Service (Task Queues & Background Job Processing)

> A queue service holds **units of work** so they can be processed **asynchronously**
> by a pool of workers — decoupling "accept the request" from "do the slow work."
> This is the single most common async pattern in backend systems.

While a [messaging service](../02-Messaging-Service) is about *communication between
services* (events), a **queue/task service** is about *offloading work* (jobs): send
email, resize image, generate report, charge card, run ETL.

---

## The Core Idea

```
                            enqueue (fast, returns 202)
  Client ──POST /report──► API ──────────────► [  QUEUE  ] ──┐
            ◄── 202 Accepted, jobId ──         (FIFO-ish)    │ dequeue
                                                             ▼
                                              ┌─────────────────────────────┐
                                              │  Worker pool (N processes)   │
                                              │  W1  W2  W3  ...  Wn         │  ← scale horizontally
                                              │  (competing consumers)       │
                                              └─────────────────────────────┘
                                                             │
  Client polls /report/{jobId}  ◄── status: PENDING→DONE ───┘  (or webhook/push on completion)
```

**Why:** the HTTP request returns in milliseconds; the 30-second report generation
happens out-of-band. The API stays responsive; workers absorb load; you scale
workers independently of web servers.

---

## When to Use a Queue

| Use a queue when the work is... | Examples |
|--------------------------------|----------|
| **Slow** | report generation, video transcoding, PDF export |
| **Spiky** | flash-sale orders, signup bursts — queue smooths the spike |
| **Failure-prone / retryable** | calling a flaky 3rd-party API, sending SMS |
| **Fire-and-forget** | emails, push notifications, audit logging |
| **Resource-heavy** | ML inference, image/thumbnail processing, ETL |
| **Schedulable / deferrable** | "send reminder in 24h", nightly batch jobs |

> If the caller needs the answer *right now*, don't queue it — do it inline.

---

## Anatomy of a Robust Queue System

```
1. PRODUCER      enqueue(job)  → returns jobId immediately
2. BROKER/STORE  durable queue (SQS / Redis / RabbitMQ / DB table)
3. WORKERS       pull jobs, process, ack on success
4. VISIBILITY    in-flight job hidden from others (visibility timeout / lease)
5. ACK / NACK    success → delete; failure → requeue (with backoff)
6. RETRY         exponential backoff + jitter, max attempts
7. DLQ           after max retries → dead letter queue + alert
8. RESULT STORE  status + result (DB / cache) for the client to poll/receive
```

### Visibility timeout / lease (prevents double-processing)
```
Worker pulls job  → job becomes INVISIBLE for `visibilityTimeout` (e.g. 30s)
Worker finishes   → ack → job DELETED
Worker crashes    → timeout expires → job becomes VISIBLE again → another worker retries
Worker slow       → extend the lease (heartbeat) so it isn't stolen mid-process
```

### Minimal worker loop (pseudo-code)
```python
while running:
    job = queue.receive(visibility_timeout=30)   # leased, hidden from peers
    if not job:
        continue
    try:
        if already_processed(job.id):            # idempotency guard (dedup)
            queue.ack(job); continue
        process(job)                             # the real work
        mark_processed(job.id)
        queue.ack(job)                           # success → remove
    except RetryableError:
        if job.attempts >= MAX_RETRIES:
            dlq.send(job)                         # poison pill → DLQ + alert
            queue.ack(job)
        else:
            queue.nack(job, delay=backoff(job.attempts))  # requeue with backoff
```

---

## Key Design Concerns

| Concern | Solution |
|---------|----------|
| **Duplicate processing** | At-least-once is normal → make workers **idempotent** (dedup on jobId). See [Idempotency](../01-Idempotency). |
| **Ordering** | Most queues don't guarantee strict order; use FIFO queues (SQS FIFO) or partition by key if order matters |
| **Priority** | Separate high/low queues, or a priority queue; workers drain high first |
| **Fairness / starvation** | Per-tenant queues or weighted draining so one big customer can't hog all workers |
| **Backpressure** | Cap queue depth / rate-limit producers; shed or 429 when overloaded |
| **Delayed jobs** | Delay queues / scheduled visibility (e.g. "process in 1h") |
| **Long jobs** | Heartbeat to extend the lease; or chunk into smaller jobs |
| **Observability** | Track queue depth, age of oldest message, processing time, DLQ size |

---

## Technology Choices

| Tech | Type | Best for |
|------|------|----------|
| **AWS SQS** | Managed queue | Default cloud choice; standard (fast) or FIFO (ordered, dedup) |
| **Redis + BullMQ / Sidekiq / RQ** | In-memory queue | Low latency, simple, rich job features (retries, cron, priorities) |
| **RabbitMQ + Celery** | Broker + worker fw | Mature, flexible routing, ack semantics |
| **Kafka** | Log/stream | High-throughput pipelines; less ideal as a pure task queue (no per-message ack/delete) |
| **Google Cloud Tasks** | Managed task queue | HTTP-target tasks, scheduling on GCP |
| **DB table as a queue** | `SELECT ... FOR UPDATE SKIP LOCKED` | Small scale, transactional consistency with your data, no extra infra |

```sql
-- Postgres as a simple, correct work queue (SKIP LOCKED avoids worker contention)
BEGIN;
SELECT * FROM jobs
  WHERE status = 'pending'
  ORDER BY created_at
  FOR UPDATE SKIP LOCKED          -- each worker grabs a different row, no blocking
  LIMIT 1;
-- ... process ...
UPDATE jobs SET status = 'done' WHERE id = :id;
COMMIT;
```

---

## 🌍 Real-World Uses

- **Email/SMS/push** — virtually every app queues notifications (SendGrid, Twilio,
  FCM calls happen in workers, never inline on the request).
- **Instagram / image platforms** — uploaded photos are queued for thumbnail
  generation, filtering, and CDN distribution; the upload API returns instantly.
- **YouTube / video platforms** — uploads enqueue transcoding jobs (multiple
  resolutions/codecs) processed by a worker fleet over minutes/hours.
- **Shopify / e-commerce** — order webhooks, inventory sync, and report exports run
  as background jobs (Sidekiq processes billions of jobs/day at Shopify).
- **Stripe** — webhook delivery, retries, and async settlement use queues with
  exponential backoff and DLQs.
- **Airbnb / booking** — post-booking work (confirmation emails, host notification,
  calendar sync, fraud checks) is queued so the booking request stays fast.
- **CI/CD (GitHub Actions, GitLab, Jenkins)** — build/test jobs sit in a queue and
  are picked up by available runners/agents.
- **Flash sales / ticketing** — incoming orders queue up so the system processes at
  a sustainable rate instead of collapsing under the spike (load leveling).
- **Data pipelines / ETL** — ingestion and transformation steps run as queued batch
  jobs (Airflow tasks, AWS Batch).

---

## Queue vs Pub/Sub vs Stream (quick disambiguation)

```
QUEUE (this doc)   : a job is processed by exactly ONE worker, then deleted.
                     "Do this work." Competing consumers share the load.
PUB/SUB            : every subscriber gets a COPY. "This happened." Fan-out.
STREAM (Kafka)     : durable ordered log; consumers track offsets, can replay.
```
They overlap (SQS, RabbitMQ, Kafka can each be bent into the others' roles), but the
**intent** differs: queue = work distribution, pub/sub = event broadcast, stream =
replayable event log.

---

## Interview Questions

**Q: How do you design a system to send 10M emails without blocking the API?**
A: API enqueues one job per email (or batches) and returns 202 immediately. A
horizontally-scaled worker fleet pulls from the queue, calls the email provider with
retries + backoff, marks each done, and routes permanent failures to a DLQ. Workers
are idempotent (dedup on jobId) because delivery is at-least-once.

**Q: What's a visibility timeout and why does it matter?**
A: When a worker pulls a job, the queue hides it for a timeout instead of deleting
it. If the worker finishes, it acks/deletes; if it crashes, the timeout expires and
the job reappears for another worker — guaranteeing at-least-once processing without
losing work. Long jobs heartbeat to extend the lease.

**Q: How do you prevent a worker crash from losing or duplicating a job?**
A: Don't delete on receive — delete on **successful ack** (visibility timeout
handles crashes → redelivery, so no loss). Since that risks duplicates, make
processing idempotent (dedup on jobId / unique constraint).

**Q: A few jobs always fail and clog the queue — what do you do?**
A: Retry with exponential backoff up to a max attempt count, then move the job to a
Dead Letter Queue and alert. The DLQ isolates poison pills so healthy jobs keep
flowing, and preserves the bad job for debugging.

**Q: How do you handle job priority and prevent starvation?**
A: Use separate queues per priority (workers drain high before low) or a priority
queue. To prevent low-priority starvation, reserve a fraction of worker capacity or
use weighted draining; for multi-tenant fairness, shard queues per tenant.

**Q: Can you use a SQL database as a queue?**
A: Yes, at small/medium scale: a `jobs` table polled with `SELECT ... FOR UPDATE
SKIP LOCKED` lets workers grab distinct rows without blocking, and you get
transactional consistency with your business data for free. It doesn't scale as far
as SQS/Kafka, but avoids extra infra and is often the right pragmatic choice.
