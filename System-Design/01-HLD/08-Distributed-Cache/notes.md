# HLD: Distributed Cache (Redis / Memcached)

## What Is It?
A distributed in-memory key-value store that sits between the application and the database to serve frequently accessed data at sub-millisecond latency.

---

## Requirements

**Functional:**
- GET key, SET key value (with optional TTL)
- DELETE key
- Support data structures: string, list, set, sorted set, hash
- Pub/Sub messaging

**Non-Functional:**
- < 1ms read latency (p99)
- High availability (99.99%)
- Horizontal scalability
- Persistence options (for restart recovery)
- Support 1M+ ops/sec

---

## Core Concepts

### Why Cache?
```
Without cache: App → DB (10ms query) → 10ms latency, 1000 QPS max
With cache:    App → Redis (0.1ms) → 10x latency improvement, 100K+ QPS

Database is usually the bottleneck. Cache removes it.
```

### What to Cache
```
✅ Read-heavy, write-light data: user profiles, product catalog
✅ Computed/aggregated results: leaderboards, recommendation lists
✅ Session data: auth tokens, shopping carts
✅ Static content metadata: image URLs, CDN paths

❌ Don't cache: financial balances, real-time inventory (strong consistency needed)
❌ Don't cache: unique per-user one-time data (wastes memory)
```

---

## Architecture: Single Node → Cluster

### Single Node (Small Scale)
```
App → Redis Single Node
```

### Redis Sentinel (High Availability)
```
┌──────────┐     ┌──────────┐     ┌──────────┐
│Sentinel 1│     │Sentinel 2│     │Sentinel 3│
└────┬─────┘     └────┬─────┘     └────┬─────┘
     └────────────────┼─────────────────┘
                      │ monitor
          ┌───────────▼────────────┐
          │  Primary (read/write)  │
          └──────────┬─────────────┘
                     │ replicate
          ┌──────────▼─────────────┐
          │  Replica(s) (read only) │
          └────────────────────────┘

Sentinel: monitors primary, elects new primary on failure
Client: asks sentinel for current primary address
```

### Redis Cluster (Horizontal Scale)
```
┌──────────┐   ┌──────────┐   ┌──────────┐
│ Shard 1  │   │ Shard 2  │   │ Shard 3  │
│ Primary  │   │ Primary  │   │ Primary  │
│ + Replica│   │ + Replica│   │ + Replica│
└──────────┘   └──────────┘   └──────────┘
     0–5460         5461–10922     10923–16383

16384 hash slots distributed across shards
key → CRC16(key) % 16384 → slot → shard
```

---

## Consistent Hashing (Key Distribution)

### Problem with Naive Hashing
```
3 nodes: slot = hash(key) % 3
Add 4th node: slot = hash(key) % 4
→ Almost every key remaps → massive cache invalidation
```

### Consistent Hashing Solution
```
Virtual ring (0 to 2^32)
Each node gets N virtual positions on ring
Key maps to position on ring → first node clockwise

Adding node: only keys between new node and its predecessor remap
Removing node: only keys of that node remap

Virtual nodes: each physical node = 150 virtual nodes
  → Better load distribution, smaller impact on ring changes
```

---

## Eviction Policies

| Policy | Algorithm | Use When |
|--------|-----------|---------|
| **LRU** (Least Recently Used) | Evict oldest accessed key | General purpose (default choice) |
| **LFU** (Least Frequently Used) | Evict least accessed key | Long-tail access patterns |
| **TTL** (Time-To-Live) | Evict expired keys | Session data, rate limiting |
| **allkeys-random** | Random eviction | When all keys equally important |
| **volatile-lru** | LRU among keys with TTL | Protect permanent keys |
| **noeviction** | Return error when full | Strict cache — never lose data |

### Redis LRU Implementation
```
Not true LRU (too memory expensive).
Redis uses approximated LRU: sample N random keys, evict least recently used among sample.
maxmemory-samples 10 → better accuracy, slightly slower
```

---

## Cache Patterns: Deep Dive

### 1. Cache-Aside (Lazy Loading)
```
Read:
  hit = cache.get(key)
  if hit: return hit
  val = db.query(key)
  cache.set(key, val, TTL=300)
  return val

Write:
  db.update(key, val)
  cache.delete(key)   # Invalidate, not update (avoids stale write race)

Pros: Only cache what's needed, resilient (cache miss → DB fallback)
Cons: Cache miss on first request (cold start), stale data possible
```

### 2. Write-Through
```
Write:
  db.update(key, val)
  cache.set(key, val)   # synchronous

Read: always hits cache (no misses for written data)

Pros: Cache always fresh for written data
Cons: Write latency doubles; unused data cached (wastes memory)
```

### 3. Write-Behind (Write-Back)
```
Write:
  cache.set(key, val)   # immediate return
  async: queue.push(db_write)  # background worker writes to DB

Pros: Ultra-fast writes
Cons: Data loss if cache crashes before DB write; complex implementation
```

### 4. Read-Through
```
App always reads from cache.
On miss, cache itself queries DB and populates itself.

Pros: App code simple (no miss logic)
Cons: Cache library must support this (less control)
```

---

## Cache Invalidation Strategies

```
1. TTL (Time-based): simple, slight staleness acceptable
2. Event-based: on DB write → publish event → invalidate cache key
3. Write-through: update cache on every write
4. Version tag: cache key includes version number (increment on change)
5. Cache-aside + delete: update DB → delete cache → next read refills
```

---

## Common Cache Problems

### Cache Stampede (Thundering Herd)
```
Problem: Cache key expires → 10000 requests all hit DB simultaneously

Solutions:
1. Mutex lock: first thread queries DB, holds lock; others wait for result
2. Probabilistic early expiry: randomly refresh before actual expiry
3. Background refresh: async worker refreshes cache before TTL
4. Stale-while-revalidate: serve stale data, refresh async
```

### Hot Key Problem
```
Problem: 1 key (e.g., "viral tweet") gets 1M requests/sec → single Redis node bottleneck

Solutions:
1. Local in-process cache (L1): each app server caches top-K hot keys
2. Key replication: copy hot key to multiple Redis nodes (key + random suffix)
3. Read replicas: route reads to replicas
4. CDN: if cacheable at CDN layer
```

### Cache Penetration
```
Problem: Request for key that never exists → every request hits DB
         Attackers spam non-existent IDs

Solutions:
1. Cache null values (TTL 60s): "key:999" → null
2. Bloom filter: probabilistic set membership before DB query
   If bloom says NO → return 404 immediately without hitting DB
```

### Cache Avalanche
```
Problem: Many cache keys expire at same time → mass DB requests

Solutions:
1. Jitter: TTL = base_TTL + random(0, base_TTL * 0.2)
2. Tiered cache: L1 (short TTL) + L2 (long TTL) + DB
3. Circuit breaker: if DB overloaded, return stale data
```

---

## Redis vs Memcached

| Feature | Redis | Memcached |
|---------|-------|-----------|
| Data structures | Rich (list, set, zset, hash, stream) | String only |
| Persistence | RDB + AOF | None |
| Replication | Built-in | No |
| Pub/Sub | Yes | No |
| Lua scripting | Yes | No |
| Cluster mode | Yes | Yes (client-side) |
| Memory efficiency | Slightly higher overhead | Slightly more efficient for strings |
| Use Memcached when | Simple string KV, max throughput | — |
| Use Redis when | Data structures, persistence, pub/sub | — |

---

## Edge Cases

| Edge Case | Solution |
|-----------|---------|
| Node failure in cluster | Redis Cluster promotes replica automatically; client retries |
| Split-brain (network partition) | Sentinel requires majority quorum for failover |
| Memory full | Configure eviction policy; alert on > 80% memory |
| Key name collisions | Use namespacing: service:type:id (e.g., user:profile:123) |
| Large values (> 1MB) | Compress values; consider object store for large blobs |
| Long TTL accumulation | Monitor keyspace; use SCAN not KEYS (blocks event loop) |

---

## Interview Questions

**Q: Redis is single-threaded. How does it handle 1M ops/sec?**
A: I/O multiplexing (epoll) — one thread handles thousands of connections via event loop. No context switching. Commands are O(1) or O(log N). The bottleneck is network bandwidth, not CPU. Redis 6.0+ added I/O threads for networking while keeping command execution single-threaded.

**Q: Difference between RDB and AOF persistence?**
A: RDB (snapshot): periodically saves binary snapshot to disk. Fast restart, compact file. May lose last N seconds of data on crash. AOF (append-only file): logs every write command. Near-zero data loss. Slower, larger file. Hybrid: RDB for snapshots + AOF for recent writes. Best of both.

**Q: How do you handle cache invalidation in microservices?**
A: Event-driven invalidation: service B publishes "user_updated" event → cache service subscribes → deletes affected cache keys. Alternatively: each service manages its own cache, invalidates on writes. Never share cache between services directly (tight coupling).

**Q: What is the difference between strong consistency and eventual consistency in caching?**
A: Strong: cache always matches DB (use write-through). Eventual: cache may be stale by TTL seconds (use cache-aside + TTL). Strong is expensive (synchronous writes, higher latency). Eventual is acceptable for most product features. Financial data needs strong; social feeds are fine with eventual.
