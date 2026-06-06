# HLD Cheatsheet — Quick Reference

## Numbers Every Engineer Must Know

### Latency Numbers
```
L1 cache hit:          0.5 ns
L2 cache hit:          7 ns
RAM access:            100 ns
SSD random read:       100 µs      (100,000 ns)
HDD seek:              10 ms       (10,000,000 ns)
Send 1KB over network: 10 µs
Round trip same DC:    500 µs
Round trip CA→NL:      150 ms
```

### Scale Numbers
```
Requests per server:   10K–100K RPS
MySQL:                 ~1000 writes/sec, ~10K reads/sec (w/ indices)
Redis:                 ~100K–1M ops/sec
Cassandra:             ~100K writes/sec per node
Kafka:                 ~1M msgs/sec per partition (batched)

Storage:
  1 tweet:             300 bytes
  1 photo:             300 KB
  1 video (1 min):     10 MB (720p)
  1 audio (1 min):     1 MB (mp3)

Twitter:   300M DAU, 600K write QPS (timeline reads)
WhatsApp:  2B users,  100B messages/day
YouTube:   2B users,  500 hours uploaded/min
Instagram: 1B users,  95M photos/day
Uber:      100M users, 15M trips/day
```

---

## Decision Trees

### Database Choice
```
Need ACID transactions?
  YES → SQL (PostgreSQL, MySQL)
  NO  → Do you need full-text search?
          YES → Elasticsearch
          NO  → Do you need graph traversal?
                  YES → Neo4j
                  NO  → Key-value? → Redis / DynamoDB
                        Time-series? → InfluxDB
                        Wide column (high write) → Cassandra
                        Document → MongoDB
```

### Cache Placement
```
Read >> Write?
  YES → Cache-Aside (lazy loading)
Write >> Read?
  YES → Write-Through or Write-Behind
Need strong consistency?
  YES → Write-Through
Need highest write performance?
  YES → Write-Behind (async)
```

### Consistency Level
```
Financial / critical data → Strong consistency (SQL + synchronous replication)
Social feeds / analytics  → Eventual consistency (Cassandra, DynamoDB)
User sessions             → Read-Your-Writes (sticky sessions or Redis)
```

---

## Architecture Patterns Quick Lookup

### Fan-Out (Write vs Read)

| | Fan-out on Write | Fan-out on Read |
|-|-----------------|-----------------|
| What | Pre-compute timeline on write | Merge feeds on read |
| When | Normal users (< 1000 followers) | Celebrities (> 1M followers) |
| Read speed | O(1) lookup | O(N) merge |
| Write speed | O(N) fan-out | O(1) write |
| Used in | Twitter (hybrid) | Instagram (pull-based) |

### Timeline/Feed Hybrid
```
1. User posts → write to own post store
2. If user has < 1000 followers → fan-out to follower timelines (push)
3. If user has > 1M followers (celebrity) → don't fan-out
4. On timeline read:
   - Fetch pre-built timeline from cache
   - Merge in celebrity posts on the fly
```

### Message Delivery Guarantees
```
At-Most-Once:   Fire and forget. Messages may be lost. Fast.
At-Least-Once:  Retry until ack. Duplicates possible. Default.
Exactly-Once:   Idempotent consumer + transactional producer. Complex.
```

---

## Storage Patterns

### SQL Schema Design Tips
```
Always:
  - Use surrogate keys (auto-increment or UUID)
  - Index foreign keys
  - Add created_at, updated_at timestamps
  - Use soft deletes (is_deleted flag + deleted_at)

Avoid:
  - SELECT * in production queries
  - N+1 queries (use JOINs or batch fetch)
  - Long transactions (hold locks)
  - Storing JSON blobs in SQL (defeats the purpose)
```

### NoSQL Data Modeling (Cassandra/DynamoDB)
```
Design for access patterns, NOT for normalization.
1. Identify the queries you will run
2. Create tables shaped for those queries
3. Denormalize — store data in multiple tables for different access patterns
4. Partition key = what you query by (determines shard)
5. Sort key = ordering within partition
```

---

## API Design Patterns

### REST Conventions
```
GET    /users/{id}              → get user
POST   /users                   → create user
PUT    /users/{id}              → full update
PATCH  /users/{id}              → partial update
DELETE /users/{id}              → delete user

GET    /users/{id}/posts        → user's posts
POST   /users/{id}/posts        → create post for user

Status codes:
200 OK, 201 Created, 204 No Content
400 Bad Request, 401 Unauthorized, 403 Forbidden, 404 Not Found
429 Too Many Requests, 500 Internal Server Error, 503 Service Unavailable
```

### Pagination
```
Offset pagination:    /posts?page=2&limit=20   (simple, bad for large offsets)
Cursor pagination:    /posts?after=base64token  (consistent, good for real-time feeds)
Keyset pagination:    /posts?after_id=12345     (fast, uses index)
```

---

## Quick System Summaries

| System | Core Problem | Key Solution |
|--------|-------------|-------------|
| URL Shortener | Unique short code + redirect | Base62 + KV store |
| Twitter | 600K read QPS timeline | Fan-out + Redis sorted set |
| WhatsApp | Message delivery guarantee | Message queue + WebSocket |
| YouTube | Video storage + streaming | S3 + CDN + adaptive bitrate |
| Uber | Real-time driver matching | GeoHash + WebSocket |
| Rate Limiter | Count requests atomically | Redis + token bucket |
| Cache | Hot data, low latency | Redis + consistent hashing |
| Notification | Fan-out to millions | Kafka + FCM/APNs |
| Autocomplete | Top-K prefix matches | Trie + Redis sorted set |
| Netflix | Personalized video delivery | CDN + ML recommendations |
