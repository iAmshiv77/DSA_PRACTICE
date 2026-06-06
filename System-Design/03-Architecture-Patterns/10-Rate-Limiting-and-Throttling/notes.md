# Rate Limiting & Throttling

> Control how many requests a client/tenant/service can make in a time window — to
> protect resources from overload, abuse, and runaway costs, and to enforce fairness.

> See also the HLD case study: [`../../01-HLD/07-Rate-Limiter`](../../01-HLD/07-Rate-Limiter).
> This note focuses on the *algorithms* and where the pattern fits.

---

## Why

```
✗ A buggy client loops and sends 10k req/s  → DB melts → everyone's service degraded
✗ A scraper/DDoS hammers your API           → resource exhaustion
✗ One tenant on a shared platform hogs all capacity → noisy-neighbor unfairness
✗ Pay-per-use downstream (3rd-party API)    → uncontrolled cost blowout
```
Rate limiting = **self-preservation**. Throttling = the response when a limit is hit
(reject with `429 Too Many Requests`, queue, or degrade).

---

## The Four Classic Algorithms

### 1. Token Bucket — allows bursts, smooths average (most common)
```
Bucket holds up to N tokens; refills at R tokens/sec.
Each request removes 1 token. No token → reject (429).

  [🪙🪙🪙🪙🪙]  refill ──► steady R/s
   take 1 per request; burst up to N, then limited to R
```
- **Pros:** permits short bursts (good UX), simple, memory-cheap. **The default.**
- Used by AWS, Stripe, most API gateways.

### 2. Leaky Bucket — smooths to a constant output rate
```
Requests enter a queue (bucket); processed at a FIXED rate (the leak).
Bucket full → overflow → reject.
  in: bursty ──► [ queue ] ──► out: constant R/s
```
- **Pros:** perfectly smooth output (protects downstream that hates bursts).
- **Cons:** no burst allowance; adds queueing latency.

### 3. Fixed Window — count per calendar window
```
count requests in [12:00:00–12:00:59]; reset at the minute boundary.
```
- **Pros:** trivial. **Cons:** boundary spike — 100 at 12:00:59 + 100 at 12:01:00 =
  200 in 2 seconds, double the intended limit.

### 4. Sliding Window (log / counter) — fixes the boundary problem
```
Sliding log: store timestamps, count those within the last 60s (accurate, more memory).
Sliding counter: weighted blend of current + previous fixed window (approx, cheap).
```
- **Pros:** smooth, accurate. **Cons:** more memory/compute than fixed window.

| Algorithm | Bursts? | Smoothness | Cost | Use when |
|-----------|---------|------------|------|----------|
| Token Bucket | ✅ allowed | good | low | General API limiting (default) |
| Leaky Bucket | ❌ | perfect | low | Protect burst-sensitive downstream |
| Fixed Window | ✅ (at edges) | poor | lowest | Quick/rough limits |
| Sliding Window | ⚠️ controlled | best | higher | Accurate limits at scale |

---

## Distributed Rate Limiting

A single-node counter doesn't work across N API servers. Centralize the counter:
```
All API nodes ──► Redis (atomic INCR + EXPIRE, or a Lua token-bucket script)
                  └─ one shared count per key (user/IP/API-key)
```
- Use **Redis** with atomic ops / Lua for correctness under concurrency.
- Trade exactness for speed with local caching + periodic sync if needed.
- Key by what you're protecting: API key, user ID, IP, or tenant.

---

## Response & Headers (good API citizenship)

```
HTTP 429 Too Many Requests
Retry-After: 30
X-RateLimit-Limit: 100
X-RateLimit-Remaining: 0
X-RateLimit-Reset: 1717670400
```
Tell clients the limit, what's left, and when to retry — so well-behaved clients back
off instead of retry-storming.

---

## Throttling vs Rate Limiting vs Load Shedding

```
Rate Limiting : enforce a quota per client (fairness, abuse prevention)
Throttling    : the action taken at the limit (reject/queue/slow down)
Load Shedding : under global overload, drop low-priority traffic to protect the core
                (e.g. serve checkout, shed analytics) — Priority Queue pattern helps
```

---

## 🌍 Real-World Uses

- **Public APIs (GitHub, Stripe, Twitter/X, Google Maps)** — per-API-key quotas with
  `X-RateLimit-*` headers; GitHub's 5000 req/hr is the classic example.
- **AWS API Gateway / Cloudflare / Kong / NGINX** — built-in token/leaky bucket
  limiters at the edge to absorb DDoS and enforce plans.
- **Login/auth endpoints** — strict limits to thwart credential-stuffing and
  brute-force (e.g. 5 attempts / 15 min, then backoff).
- **Multi-tenant SaaS** — per-tenant limits to prevent noisy-neighbor starvation.
- **Stripe** — documents token-bucket limiting; returns 429 with retry guidance.
- **Internal microservices** — protect a shared DB/3rd-party dependency from being
  overwhelmed by an upstream spike (pairs with circuit breaker + queue-based load
  leveling).

---

## Interview Questions

**Q: Token bucket vs leaky bucket?**
A: Token bucket allows bursts up to the bucket size while capping the average rate —
good for user-facing APIs. Leaky bucket forces a constant output rate via a queue,
smoothing bursts entirely — good for protecting burst-sensitive downstream, at the
cost of queueing latency and no burst allowance.

**Q: What's wrong with fixed-window counting?**
A: The boundary spike: a client can send a full window's worth at the end of one
window and another full window's worth at the start of the next, doubling the
effective rate across the boundary. Sliding window fixes this.

**Q: How do you rate-limit across many API servers?**
A: Centralize the counter in a shared store like Redis using atomic operations
(INCR/EXPIRE or a Lua token-bucket script) keyed by user/IP/API-key, so all nodes
share one consistent count. Optionally cache locally and sync to trade exactness for
latency.

**Q: What should a rate-limited response look like?**
A: HTTP 429 with `Retry-After` and `X-RateLimit-Limit/Remaining/Reset` headers so
clients know the quota and when to retry, encouraging graceful backoff instead of
retry storms.

**Q: Rate limiting vs load shedding?**
A: Rate limiting enforces per-client quotas for fairness/abuse. Load shedding is a
global-overload response that drops lower-priority work to keep critical paths alive
regardless of which client sent it.
