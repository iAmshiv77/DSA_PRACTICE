# HLD: URL Shortener (TinyURL / bit.ly)

## Requirements Clarification

**Functional:**
- Given a long URL, generate a short URL (e.g., tinyurl.com/abc123)
- Redirect short URL to original long URL
- Optional: custom alias, expiry date, analytics (click count)

**Non-Functional:**
- 100M URLs created/day (write)
- 10:1 read:write ratio → 1B redirects/day
- Low latency for redirects (< 10ms p99)
- High availability (99.99%)
- Short URLs must be unique and not guessable (no enumeration)

---

## Capacity Estimation

```
Write QPS:   100M / 86400 ≈ 1200/sec   (peak: ~3600/sec)
Read QPS:    1B / 86400 ≈ 12000/sec    (peak: ~36000/sec)

Storage:
  - 1 URL record ≈ 500 bytes
  - 100M/day × 365 × 5 years = 182.5B records
  - Storage = 182.5B × 500B ≈ 91 TB (with replication × 3 = 273 TB)

Bandwidth:
  - Write: 3600 × 500B = 1.8 MB/s
  - Read:  36000 × 500B = 18 MB/s (small, URL response)
```

---

## High-Level Architecture

```
┌──────────┐        ┌──────────────┐       ┌─────────────────┐
│  Client  │──POST─→│  API Gateway  │──────→│  URL Service    │
│          │←─────── (rate limit)   │←──────│  (shorten/read) │
└──────────┘        └──────────────┘       └────────┬────────┘
                                                    │
                     ┌──────────────────────────────┤
                     ↓                              ↓
             ┌──────────────┐            ┌──────────────────┐
             │  Redis Cache │            │  SQL / NoSQL DB   │
             │ (shortCode→  │            │  (url_mappings)   │
             │  longURL)    │            └──────────────────┘
             └──────────────┘

     Read path: Redis hit → return in < 1ms
     Cache miss: DB query → populate cache → return
```

---

## Core Design: Short URL Generation

### Option A: Hash-Based
```
shortCode = base62(MD5(longURL + salt))[0:7]
```
- Pro: deterministic (same URL → same code)
- Con: collision probability, hard to check uniqueness at DB scale

### Option B: Counter-Based (Preferred)
```
shortCode = base62(unique_ID)
unique_ID  = globally_unique_counter (from Redis INCR or Snowflake ID)
```
- Pro: no collisions, predictable length
- Con: guessable if sequential → **add offset or shuffle**

### Base62 Encoding
```
Characters: 0–9, a–z, A–Z (62 chars)
7 characters → 62^7 = 3.5 TRILLION unique codes
At 1200 writes/sec → lasts 3.5T / 1200 / 86400 / 365 ≈ 92 years
```

### Counter Distribution at Scale
```
Option 1: Redis INCR  — single atomic counter, bottleneck at extreme scale
Option 2: Snowflake   — distributed ID (41-bit timestamp + 10-bit machine + 12-bit seq)
Option 3: Range-based — each server gets a range (server A: 1–1M, server B: 1M–2M)
```

---

## Database Design

```sql
CREATE TABLE url_mappings (
    short_code    VARCHAR(10) PRIMARY KEY,
    long_url      TEXT NOT NULL,
    user_id       BIGINT,
    created_at    TIMESTAMP DEFAULT NOW(),
    expires_at    TIMESTAMP,
    click_count   BIGINT DEFAULT 0
);

CREATE INDEX idx_long_url ON url_mappings(MD5(long_url));  -- for dedup
```

**Why NoSQL (DynamoDB/Cassandra) is also great here:**
- Simple key-value lookup (short_code → long_url)
- Massive horizontal scale
- No complex queries needed

---

## API Design

```
POST /api/v1/shorten
Body: { "long_url": "...", "custom_alias": "mylink", "expires_in": 86400 }
Response: { "short_url": "tinyurl.com/abc123", "expires_at": "..." }

GET /{short_code}
Response: 301 (Permanent Redirect) or 302 (Temporary Redirect)
→ Location: https://original-long-url.com

GET /api/v1/stats/{short_code}
Response: { "clicks": 12345, "created_at": "...", "last_accessed": "..." }
```

**301 vs 302 Redirect:**
- 301 Permanent: browser caches redirect. Fewer server hits. Analytics suffer.
- 302 Temporary: browser always hits server. Better for analytics. More load.
- Use 302 for click analytics, 301 for static redirects.

---

## Deep Dive: Read Path (Redirect)

```
1. Client: GET tinyurl.com/abc123
2. DNS → Load Balancer → URL Service
3. Check Redis: GET url:abc123
   ├── HIT:  return 302 + Location header (< 1ms)
   └── MISS: query DB → populate Redis (TTL 24h) → return 302
4. Update click_count (async, via message queue)
```

**Cache Key:** `url:{short_code}`
**Cache Value:** `{long_url, expires_at}`
**TTL:** Set to URL expiry time. If no expiry, use 24h with refresh.

---

## Scaling the Write Path

```
1. Counter Service (dedicated microservice)
   - Uses Redis INCR or Snowflake for unique IDs
   - Returns next available ID

2. URL Service
   - Convert ID to base62
   - Write to DB (async OK — eventual consistency acceptable)
   - Return short URL immediately

3. DB Write
   - Primary handles all writes
   - Read replicas for analytics queries
   - Shard by short_code hash for future scale
```

---

## Edge Cases & How to Handle

| Edge Case | Solution |
|-----------|---------|
| Same long URL submitted twice | Check MD5 hash in DB, return existing short code |
| Custom alias already taken | Return 409 Conflict, suggest alternatives |
| Expired URL accessed | Delete from cache, return 404 or "expired" page |
| Malicious/phishing URL | URL validation service (Google Safe Browsing API) |
| Very long URL (> 2048 chars) | Validate and cap at server level |
| Short code collision (hash approach) | Append random suffix and retry |
| User deletes URL still cached | Set short TTL OR delete from cache on delete |
| Hot URL (viral, millions of hits) | Already cached; local in-process cache for top 1% |

---

## Interview Deep-Dive Questions

**Q: How do you prevent someone from enumerating all URLs?**
A: (1) Use random base62 codes not sequential counters, OR (2) Add a private salt to the counter before base62 encoding. Custom aliases can be searched but the system's auto-generated ones are not guessable.

**Q: How would you implement analytics (click tracking)?**
A: Don't update DB synchronously on every redirect (too slow). Instead: publish click event to Kafka → consumer aggregates into analytics DB (ClickHouse/Cassandra). Report click_count from analytics DB, not from redirect path.

**Q: What if Redis goes down?**
A: URL service falls back directly to DB. Redirect latency increases from 1ms to ~10ms. Redis is NOT the source of truth — DB is. Use Redis Sentinel or Redis Cluster for HA.

**Q: How would you implement custom short codes?**
A: Additional DB table `custom_aliases(alias, short_code, user_id)`. On create: check alias availability, write to both tables. Rate-limit custom alias creation to prevent squatting.

**Q: How do you handle 100M+ daily writes at scale?**
A: (1) Shard counter service across machines using range allocation, (2) Shard DB by hash(short_code), (3) Use a write-optimized storage (Cassandra) for primary writes, (4) Async replication to read replicas.
