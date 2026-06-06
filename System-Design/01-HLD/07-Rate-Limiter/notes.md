# HLD: Rate Limiter

## What Is It?
A rate limiter controls the rate of requests a client/user can make. Prevents abuse, DoS attacks, and protects downstream services.

---

## Requirements

**Functional:**
- Limit requests per user/IP/API key
- Multiple rules: e.g., 100 req/min, 1000 req/day
- Return HTTP 429 Too Many Requests when exceeded
- Optional: headers showing remaining quota (X-RateLimit-Remaining)

**Non-Functional:**
- Ultra-low overhead (< 1ms overhead per request)
- Accurate (or close to accurate)
- Works across multiple servers (distributed)
- Fault tolerant: if rate limiter fails, don't block all traffic

---

## Where to Place the Rate Limiter

```
Option 1: Client-side    — unreliable (client can bypass)
Option 2: Server-side    — most common
Option 3: Middleware     — API Gateway (Kong, Nginx, AWS API Gateway)
Option 4: Service Mesh   — Istio, Linkerd (for microservice-to-microservice)

Best: API Gateway — single choke point, language-agnostic
```

---

## Rate Limiting Algorithms

### 1. Token Bucket
```
Concept: Bucket holds max N tokens. Tokens added at rate r/sec.
         Each request consumes 1 token. If empty → reject.

Pros: Allows burst (up to bucket capacity)
Cons: Race condition on distributed systems (need atomic ops)

Use for: APIs where some bursting is OK (most common choice)

Redis implementation:
  KEYS: token_bucket:{user_id} → { tokens: N, last_refill: timestamp }

  Algorithm:
    1. Get current tokens and last_refill time
    2. tokens += (now - last_refill) * refill_rate
    3. tokens = min(tokens, bucket_capacity)
    4. If tokens >= 1: tokens -= 1, allow. Update Redis atomically.
    5. Else: reject 429
```

### 2. Leaky Bucket
```
Concept: Requests enter queue (bucket). Processed at fixed rate.
         If queue full → reject.

Pros: Smooth output rate, no burst
Cons: Queued requests may be stale; latency added

Use for: Payment processing, rate-sensitive downstream services
```

### 3. Fixed Window Counter
```
Concept: Count requests per time window (e.g., per minute).
         Reset count at window start.

Redis:   INCR rate:{user_id}:{minute}
         EXPIRE rate:{user_id}:{minute} 60

Pros: Simple, low memory
Cons: Boundary problem — user can make 2N requests in 2*windowSize
      spanning two windows (N at end of window, N at start of next)
```

### 4. Sliding Window Log
```
Concept: Store timestamp of each request. Count timestamps in last minute.

Redis:   ZADD log:{user_id} {timestamp} {timestamp}
         ZREMRANGEBYSCORE log:{user_id} 0 (now - 60sec)
         count = ZCARD log:{user_id}

Pros: Exact accuracy
Cons: High memory (stores all timestamps)
```

### 5. Sliding Window Counter (Best for Production)
```
Concept: Weighted average between current and previous window.
         rate = prev_count × (1 - elapsed/window) + curr_count

Redis:   Two counters: curr_window, prev_window
         Memory: O(1) per user

Pros: Memory-efficient, close to sliding window accuracy
Cons: Slightly approximate (< 0.003% error)

This is what Cloudflare, Stripe use in production.
```

---

## Distributed Rate Limiting

### Problem
Multiple servers → each has its own in-memory counter. User can exceed limit by hitting different servers.

### Solution: Centralized Store (Redis)

```
Architecture:
  Client → LB → [Server 1, Server 2, Server 3] → Redis Cluster

Every server checks Redis before processing request.
Atomic Redis operations prevent race conditions.
```

### Redis Lua Script (Atomic Token Bucket)
```lua
-- KEYS[1] = rate limit key
-- ARGV[1] = max tokens, ARGV[2] = refill rate/sec, ARGV[3] = current time

local key = KEYS[1]
local max_tokens = tonumber(ARGV[1])
local refill_rate = tonumber(ARGV[2])
local now = tonumber(ARGV[3])

local data = redis.call('HMGET', key, 'tokens', 'last_refill')
local tokens = tonumber(data[1]) or max_tokens
local last_refill = tonumber(data[2]) or now

-- Calculate new tokens
local elapsed = now - last_refill
tokens = math.min(max_tokens, tokens + elapsed * refill_rate)

if tokens >= 1 then
    tokens = tokens - 1
    redis.call('HMSET', key, 'tokens', tokens, 'last_refill', now)
    redis.call('EXPIRE', key, 3600)
    return 1  -- allowed
else
    redis.call('HMSET', key, 'tokens', tokens, 'last_refill', now)
    return 0  -- rejected
end
```

---

## Response Headers (Best Practice)

```
HTTP/1.1 200 OK
X-RateLimit-Limit:      100
X-RateLimit-Remaining:  87
X-RateLimit-Reset:      1700000060   (unix timestamp when quota resets)
Retry-After:            30           (only on 429)
```

---

## Rule Configuration

```yaml
rules:
  - type: user
    endpoint: /api/v1/messages
    limit: 50
    window: 60s

  - type: ip
    endpoint: /api/v1/login
    limit: 5
    window: 300s   # 5 attempts per 5 min (brute force protection)

  - type: api_key
    endpoint: /api/v1/*
    limit: 10000
    window: 86400s  # daily quota
```

Rules stored in config service (etcd/Consul) or DB, cached locally on each server. Refresh every 30 seconds.

---

## Edge Cases

| Edge Case | Solution |
|-----------|---------|
| Rate limiter Redis is down | Fail open (allow traffic) to prevent full outage; alert ops |
| Race condition on decrement | Use atomic Lua script or Redis MULTI/EXEC |
| Multiple rules for same key | Apply the most restrictive rule that matches |
| Distributed clock skew | Use Redis server time (TIME command), not client time |
| User with VIP plan | Rule engine checks plan tier → higher limits |
| Shared IP (NAT / corporate network) | Rate limit by user_id, not IP alone |
| Warm-up period for new API key | Start with lower limits, increase over time |

---

## Interview Questions

**Q: Token bucket vs leaky bucket — when to use each?**
A: Token bucket allows bursting — good for user-facing APIs where occasional spikes are fine. Leaky bucket enforces constant rate — good for protecting downstream services that can't handle bursts (payment processors, email delivery).

**Q: How does sliding window counter work mathematically?**
A: Approximate count = prev_window_count × (1 - position_in_current_window/window_size) + current_window_count. If window is 60s and you're 30s in: count = prev × 0.5 + curr. Error rate is negligible (< 0.003% for typical traffic).

**Q: What if the rate limiting layer itself becomes a bottleneck?**
A: (1) Redis Cluster for horizontal scale, (2) Local in-process cache (token bucket) with async sync to Redis every 100ms (trade some accuracy for performance), (3) Use consistent hashing to pin each user to specific rate limiter node.

**Q: How would you rate limit at different granularities simultaneously (per minute AND per day)?**
A: Maintain separate counters per time window. Check both before allowing. Return the most restrictive limit in response headers.
