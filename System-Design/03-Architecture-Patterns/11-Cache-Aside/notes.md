# Cache-Aside (Lazy Loading) & Caching Strategies

> Load data into a cache **on demand**: the application checks the cache first, and on
> a miss, reads from the database and populates the cache. The most common caching
> pattern.

---

## Cache-Aside Flow

```
READ:
  1. app checks cache for key
     ├─ HIT  → return cached value (fast)
     └─ MISS → 2. read from DB
                3. write value into cache (with TTL)
                4. return value

WRITE:
  1. write to DB
  2. INVALIDATE (delete) the cache key   ← not "update the cache"
```

```python
def get_user(uid):
    user = cache.get(uid)
    if user is not None:            # cache hit
        return user
    user = db.query(uid)            # cache miss → DB
    cache.set(uid, user, ttl=300)   # populate for next time
    return user

def update_user(uid, data):
    db.update(uid, data)
    cache.delete(uid)               # invalidate, let next read repopulate
```

> **Invalidate on write, don't update the cache in place.** Updating risks writing a
> stale/racy value; deleting forces the next read to reload fresh from the DB.

---

## Caching Strategies Compared

| Strategy | Read path | Write path | Notes |
|----------|-----------|------------|-------|
| **Cache-Aside** (lazy) | app manages: miss → load | write DB, invalidate cache | Most common; cache only holds requested data; resilient to cache outage |
| **Read-Through** | cache library loads on miss | — | Like cache-aside but the cache (not app) fetches; app code simpler |
| **Write-Through** | — | write cache **and** DB synchronously | Cache always fresh; slower writes |
| **Write-Behind (write-back)** | — | write cache now, DB **async** later | Fast writes; risk of data loss if cache dies before flush |
| **Write-Around** | — | write DB only, skip cache | Avoids caching write-once data that's rarely read |

---

## The Hard Problems

### 1. Stale data & consistency
Cache and DB can diverge. Controls: **TTL** (bounded staleness), **invalidate on
write**, or event-driven invalidation (CDC). There's always a consistency window —
accept it or use write-through for hotter keys.

### 2. Cache Stampede / Thundering Herd
```
A hot key expires → 10,000 concurrent requests all MISS → all hit the DB at once → DB melts
```
Fixes: **request coalescing / single-flight** (one request loads, others wait),
**probabilistic early expiration**, **locking** on the key, or **pre-warming**.

### 3. Cache Penetration (queries for non-existent keys)
Requests for keys that don't exist always miss and hit the DB. Fix: cache the
"not found" result (negative caching) or use a **Bloom filter** to short-circuit.

### 4. Eviction policy
When the cache is full: **LRU** (least recently used — default), **LFU** (least
frequently used), **FIFO**, **TTL**-based. Choose per access pattern.

### 5. Hot keys
One key (a celebrity's profile) overwhelms a single cache node. Fix: replicate the
key across nodes, or add a local in-process cache layer.

---

## 🌍 Real-World Uses

- **Redis / Memcached** in front of Postgres/MySQL — the canonical cache-aside setup
  in virtually every high-traffic web app.
- **Facebook (memcached at scale)** — famous engineering writeups on cache-aside,
  stampede control, and invalidation across data centers.
- **CDN edge caching (Cloudflare, CloudFront, Akamai)** — cache-aside/read-through for
  static assets and cacheable API responses near users.
- **Database query / object caches** — Hibernate 2nd-level cache, Django/Rails cache
  layers.
- **Product catalogs, user sessions, feed timelines** — read-heavy data fronted by a
  cache; sessions often write-through for freshness.
- **DNS resolution** — a real-world cache-aside with TTLs.

---

## Interview Questions

**Q: Walk through cache-aside read and write.**
A: Read: check cache → on hit return it → on miss read DB, populate cache with TTL,
return. Write: update the DB, then invalidate (delete) the cache key so the next read
reloads fresh.

**Q: Why invalidate the cache on write instead of updating it?**
A: Updating in place risks races (two writers, or a stale read interleaving) that
leave a wrong value cached. Deleting is simpler and safe — the next read repopulates
from the authoritative DB.

**Q: What is a cache stampede and how do you prevent it?**
A: A hot key expires and a flood of concurrent requests all miss and hammer the DB
simultaneously. Prevent with single-flight/request coalescing (one loader, others
wait), locking, probabilistic early recomputation, or pre-warming the key.

**Q: Write-through vs write-behind?**
A: Write-through writes cache and DB synchronously — always consistent but slower
writes. Write-behind writes the cache immediately and flushes to the DB
asynchronously — fast writes but risks data loss if the cache fails before the flush.

**Q: How do you handle queries for keys that don't exist (penetration)?**
A: Negative caching (store a "not found" marker with a short TTL) and/or a Bloom
filter in front of the cache to reject keys that definitely don't exist, sparing the
DB.
