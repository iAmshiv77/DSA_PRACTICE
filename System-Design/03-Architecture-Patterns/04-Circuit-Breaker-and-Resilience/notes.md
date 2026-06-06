# Resilience Patterns — Circuit Breaker, Retry, Timeout, Bulkhead, Fallback

> In a distributed system, **dependencies will fail**. Resilience patterns stop one
> slow/failing dependency from cascading into a total outage. They're how you build
> systems that **degrade gracefully** instead of falling over.

These five patterns are almost always used *together* (Netflix Hystrix / resilience4j
/ Polly bundle them).

---

## The Problem: Cascading Failure

```
Service A ──► Service B ──► Service C (slow: 30s timeouts)
                              ▲
   B's threads all block waiting on C  →  B exhausts its thread pool
   →  A's calls to B now hang  →  A exhausts its threads  →  WHOLE SYSTEM DOWN
   One slow leaf takes down the entire tree.
```

The fix is to **fail fast** and **isolate** failures so they can't propagate.

---

## 1. Circuit Breaker — fail fast when a dependency is sick

A state machine that "trips" after repeated failures, so callers stop hammering a
dead dependency and get an immediate error/fallback instead of waiting on timeouts.

```
        failures ≥ threshold
   ┌───────────────────────────────┐
   ▼                               │
CLOSED ──(too many failures)──► OPEN ──(after cooldown)──► HALF-OPEN
  │  (normal: calls pass through)  │ (reject instantly,    │ (let ONE trial
  ▲                                │  return fast error/    │  call through)
  │                                │  fallback)             │
  └────(trial call succeeds)───────┴────────────────────────┘
         back to CLOSED                  fail → back to OPEN
```

| State | Behavior |
|-------|----------|
| **CLOSED** | Requests flow normally; count failures. Threshold exceeded → OPEN. |
| **OPEN** | Reject immediately (no call made). After a cooldown timer → HALF-OPEN. |
| **HALF-OPEN** | Allow a few trial calls. Success → CLOSED; failure → OPEN again. |

```python
class CircuitBreaker:
    def __init__(self, threshold=5, cooldown=30):
        self.fail_count = 0; self.state = "CLOSED"; self.opened_at = None
        self.threshold = threshold; self.cooldown = cooldown

    def call(self, fn):
        if self.state == "OPEN":
            if now() - self.opened_at > self.cooldown:
                self.state = "HALF_OPEN"      # time to test recovery
            else:
                raise CircuitOpenError()       # fail fast, no downstream call
        try:
            result = fn()
            self.fail_count = 0; self.state = "CLOSED"   # success resets
            return result
        except Exception:
            self.fail_count += 1
            if self.fail_count >= self.threshold:
                self.state = "OPEN"; self.opened_at = now()
            raise
```

---

## 2. Retry (with exponential backoff + jitter) — for *transient* faults

```
attempt 1 → fail → wait 1s  → attempt 2 → fail → wait 2s
          → attempt 3 → fail → wait 4s → attempt 4 → fail → give up (→ DLQ/fallback)

base * 2^n  (exponential)   +   random jitter   ← jitter prevents a "retry storm"
                                                   where all clients retry in lockstep
```
- Only retry **idempotent** operations (or use an [idempotency key](../01-Idempotency)) —
  retrying a non-idempotent charge double-charges.
- Only retry **transient** errors (timeout, 503, connection reset). Don't retry a
  `400 Bad Request` — it'll fail every time.
- **Pair Retry with Circuit Breaker:** retry transient blips, but once the breaker
  opens, stop retrying a persistently-down service.

---

## 3. Timeout — never wait forever

```
Always bound every network call. No timeout = a hung dependency holds your thread
forever → thread-pool exhaustion → cascade.

  connect timeout:  time to establish connection      (e.g. 1s)
  read/request timeout: time to get a response          (e.g. 3s)
  Total budget:  caller's deadline propagates to callee (deadline propagation)
```

---

## 4. Bulkhead — isolate resources so one failure can't sink the ship

Named after a ship's watertight compartments. Partition resources (thread pools,
connection pools) per dependency so a flood in one can't drown the others.

```
            WITHOUT bulkhead                 WITH bulkhead
   ┌──────────────────────────┐    ┌────────────┬────────────┬───────────┐
   │   shared thread pool      │    │ pool: svcB │ pool: svcC │ pool: svcD│
   │   (all 100 threads)       │    │ (33)       │ (33)       │ (33)      │
   └──────────────────────────┘    └────────────┴────────────┴───────────┘
   svcC hangs → all 100 threads     svcC hangs → only its 33 threads block;
   stuck → A is fully down           B and D keep serving. Partial degradation.
```

---

## 5. Fallback / Graceful Degradation — a sensible answer when the call fails

```
recommendations service down?
  → return a cached / generic "popular items" list instead of an error page
payment fraud-check timeout?
  → queue for async review instead of blocking checkout
```
Return degraded-but-useful results: cached data, defaults, empty set, or queue for
later. **Better a slightly worse experience than a 500.**

---

## How They Compose

```
       ┌─────────── Bulkhead (isolated thread pool per dependency) ──────────┐
       │   ┌──────── Circuit Breaker (skip calls when OPEN) ───────────┐     │
       │   │   ┌──── Retry (transient faults, backoff + jitter) ───┐   │     │
       │   │   │   ┌── Timeout (bound each attempt) ──┐            │   │     │
  call─┼───┼───┼───┤      actual downstream call      │── fail ────┼── Fallback (degrade)
       │   │   │   └──────────────────────────────────┘            │   │     │
       │   │   └───────────────────────────────────────────────────┘   │     │
       │   └───────────────────────────────────────────────────────────┘     │
       └─────────────────────────────────────────────────────────────────────┘
```

---

## 🌍 Real-World Uses

- **Netflix Hystrix** — the pattern's famous popularizer; circuit breakers + bulkheads
  + fallbacks wrap every inter-service call so one failing microservice degrades
  gracefully (e.g. recommendations fall back to a static list). Now succeeded by
  **resilience4j**.
- **resilience4j (Java), Polly (.NET), Istio/Envoy (service mesh)** — production
  libraries/proxies that implement all five patterns; Envoy enforces circuit
  breaking + retries + timeouts at the infra layer without app code.
- **AWS SDKs** — built-in exponential backoff with jitter on throttled/5xx responses.
- **Stripe / payment systems** — timeouts + retries with idempotency keys, circuit
  breakers around third-party processors.
- **Amazon checkout** — if the recommendations or reviews service is down, the page
  still renders (fallback) rather than failing the whole purchase flow.

---

## Interview Questions

**Q: What problem does a circuit breaker solve that retries don't?**
A: Retries handle *transient* faults but make a *persistent* outage worse (retry
storms hammering a dead service). A circuit breaker detects sustained failure and
*stops* calling, failing fast and giving the dependency time to recover. Use both:
retry blips, trip the breaker on sustained failure.

**Q: Explain the three circuit breaker states.**
A: CLOSED (calls pass, failures counted), OPEN (calls rejected instantly after the
failure threshold, for a cooldown), HALF-OPEN (a few trial calls test recovery —
success closes it, failure reopens it).

**Q: Why add jitter to exponential backoff?**
A: Without jitter, many clients that failed at the same instant retry at the same
intervals, creating synchronized "retry storms" that re-overload the recovering
service. Random jitter spreads retries out.

**Q: What's the bulkhead pattern?**
A: Isolate resources (separate thread/connection pools) per dependency so that one
saturated dependency can't consume all resources and take down calls to healthy
dependencies — only that compartment floods.

**Q: When should you NOT retry?**
A: Non-idempotent operations without an idempotency key (double-charge risk), and
non-transient errors like 400/401/404 that will deterministically fail again.
