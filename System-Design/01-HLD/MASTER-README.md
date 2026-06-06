# High Level Design — Master Reference

## The HLD Framework (Memorize This)

```
Step 1: Clarify Requirements         (5 min)
Step 2: Capacity Estimation          (5 min)
Step 3: High-Level Architecture      (10 min)
Step 4: Deep Dive Key Components     (15 min)
Step 5: Scalability & Fault Tolerance(5 min)
Step 6: Monitoring & Alerting        (5 min)
```

---

## Step 1: Requirements Clarification Questions

Always ask BEFORE drawing anything:

**Functional:**
- Who are the users? What actions do they perform?
- Read-heavy or write-heavy?
- Does it need real-time updates?
- Mobile app, web app, or both?
- Do we need search functionality?

**Non-Functional:**
- How many DAU (daily active users)?
- What is the expected QPS for reads/writes?
- What is the acceptable latency (p99)?
- What is the availability target? (99.9% = 8.7h downtime/yr, 99.99% = 52 min)
- Is consistency more important than availability? (CAP theorem)
- Data retention period?
- Multi-region or single region?

---

## Step 2: Capacity Estimation

### Key Formulas
```
QPS           = DAU × requests_per_user_per_day ÷ 86400
Peak QPS      = QPS × 3  (rule of thumb)
Storage/day   = writes_per_day × avg_object_size
Storage/5yr   = Storage/day × 365 × 5 × replication_factor
Bandwidth     = QPS × avg_response_size
```

### Numbers to Memorize
```
1 million req/day = ~12 req/sec
1 billion req/day = ~12,000 req/sec (12K QPS)
1 KB = 10^3 bytes, 1 MB = 10^6, 1 GB = 10^9, 1 TB = 10^12

Memory latency:     ~100 ns
SSD latency:        ~100 µs  (1000x slower than memory)
HDD latency:        ~10 ms   (100x slower than SSD)
Network same DC:    ~500 µs
Network cross-DC:   ~150 ms

1 server handles:   ~10K–100K requests/sec (depends on work)
```

### Estimation Template
```
Twitter example:
- DAU = 300M
- Tweets/day: 300M × 2 = 600M tweets
- Write QPS: 600M / 86400 ≈ 7000/sec (peak ~21K)
- Read QPS: 300M × 100 timeline views = 30B reads/day ≈ 350K/sec
- Storage: 600M × 300B (text) = 180 GB/day = 65 TB/year
- Media: 600M × 10% × 200KB = 12 TB/day
```

---

## Step 3: Architecture Building Blocks

### Standard Components (Use These)

```
┌─────────────────────────────────────────────────────────┐
│  Client (Mobile/Web)                                    │
│        ↓                                               │
│  DNS  →  CDN (static assets, images)                   │
│        ↓                                               │
│  Load Balancer (Layer 4 / Layer 7)                     │
│        ↓                                               │
│  API Gateway (auth, rate limit, routing)               │
│        ↓                                               │
│  Service Layer (stateless microservices)               │
│        ↓                ↓              ↓               │
│  Cache (Redis)     Message Queue    DB (SQL/NoSQL)     │
│                   (Kafka/RabbitMQ)                     │
│                         ↓                             │
│                    Worker Services                     │
│                         ↓                             │
│                    Storage (S3/Blob)                   │
└─────────────────────────────────────────────────────────┘
```

### When to Use What

**SQL vs NoSQL:**
| Use SQL when | Use NoSQL when |
|-------------|---------------|
| Strong ACID needed | Horizontal scale required |
| Complex queries/joins | Simple key-value lookups |
| Structured data | Flexible/dynamic schema |
| Financial transactions | High write throughput |
| Example: payments, orders | Example: social feeds, sessions |

**Cache Strategy:**
| Strategy | When | Pros/Cons |
|----------|------|-----------|
| Cache-Aside (Lazy) | Read-heavy | App controls; risk of stale data |
| Write-Through | Write-heavy with reads | Always consistent; write latency |
| Write-Behind (Write-Back) | High write throughput | Fast writes; risk of data loss |
| Read-Through | Read-heavy | Cache manages load; cold start |
| Refresh-Ahead | Predictable access | Proactive; may cache unused data |

**Message Queue:**
| Use Case | Choice |
|----------|--------|
| High throughput event streaming | Kafka |
| Simple task queues, RPC | RabbitMQ |
| AWS ecosystem | SQS/SNS |
| Simple pub/sub | Redis Pub/Sub |

**Database Choices:**
| DB Type | Product | Best For |
|---------|---------|---------|
| Relational | PostgreSQL, MySQL | Transactions, complex queries |
| Document | MongoDB | Flexible schema, nested data |
| Key-Value | Redis, DynamoDB | Cache, sessions, leaderboards |
| Wide-Column | Cassandra, HBase | Time-series, write-heavy |
| Graph | Neo4j | Social graphs, recommendations |
| Search | Elasticsearch | Full-text search, analytics |
| Time-Series | InfluxDB, TimescaleDB | Metrics, IoT |

---

## Step 4: Scalability Patterns

### Horizontal vs Vertical Scaling
```
Vertical: Bigger machine. Simple, but has limits. Use first.
Horizontal: More machines. Complex, but unlimited. Use for stateless services.
```

### Database Scaling Strategies
```
1. Indexing           — first step, free performance gain
2. Read Replicas      — scale reads; primary handles writes
3. Caching            — Redis in front of DB, 100x read improvement
4. Sharding (partitioning):
   - Horizontal: split by row (userId % N → shard)
   - Vertical: split by column (user_profile vs user_activity)
   - Range-based: userId 1–1M → shard 1
   - Hash-based: hash(userId) % N → shard
   - Directory-based: lookup table for shard
5. Denormalization    — precompute joins, trade storage for speed
6. Archiving          — move old data to cold storage
```

### CAP Theorem
```
C — Consistency:  every read gets latest write (or error)
A — Availability: every request gets a response (no error)
P — Partition Tolerance: system works despite network splits

Rule: In a distributed system, you MUST tolerate partitions.
      So choose: CP (consistent) or AP (available).

CP systems: MongoDB, HBase, Zookeeper
AP systems: Cassandra, CouchDB, DynamoDB
```

### PACELC (Extension of CAP)
```
Even without partition: choose Latency vs Consistency
P → C or A
E (else) → L or C

DynamoDB: PA/EL (available, low latency)
MySQL: PC/EC (consistent even without partition)
```

---

## Step 5: Fault Tolerance Patterns

### High Availability Patterns
```
Active-Active:    Multiple nodes handle traffic. Load balanced.
Active-Passive:   One primary, one standby. Failover on detection.
Multi-Region:     Geographically distributed. Disaster recovery.
```

### Failure Handling
```
Retry with exponential backoff + jitter
Circuit Breaker: stop calling failing service
Bulkhead: isolate failures (thread pool per service)
Timeout: never wait forever
Fallback: serve stale data / default response
```

### Data Consistency
```
Strong Consistency: read always returns latest write
Eventual Consistency: reads may return stale data, will converge
Causal Consistency: operations that are causally related are seen in order
Read-Your-Writes: user always sees their own writes
Monotonic Reads: once you read X, you won't read older than X
```

---

## Step 6: Non-Functional Topics

### API Design
```
REST:    stateless, HTTP verbs (GET/POST/PUT/DELETE), resource-based URLs
GraphQL: flexible queries, single endpoint, client specifies fields
gRPC:    binary protocol, streaming, low latency (microservices)
WebSocket: bidirectional, persistent connection (chat, real-time)
```

### Idempotency
```
An operation is idempotent if calling it N times = calling it once.
GET, PUT, DELETE are idempotent. POST is NOT.
Use idempotency keys for payments: X-Idempotency-Key header
Store: hash(idempotencyKey) → result in Redis (TTL 24h)
```

### Rate Limiting Algorithms
```
Token Bucket:    tokens added at rate r, burst allowed. Most popular.
Leaky Bucket:    requests processed at fixed rate. No burst.
Fixed Window:    count per time window. Boundary problem.
Sliding Window Log: exact, but memory expensive.
Sliding Window Counter: approximate, memory efficient.
```

---

## Common Interview Traps & Edge Cases

| Trap | Answer |
|------|--------|
| "Just use a bigger database" | Ask about scale first; SQL has limits around 1TB without sharding |
| Single point of failure | Every component needs redundancy |
| Cache invalidation | Choose a strategy: TTL, event-driven, write-through |
| Hot partition / hot key | Add random suffix to key, shard differently, use local cache |
| Clock skew in distributed system | Use logical clocks / vector clocks / NTP |
| Data loss on crash | Write-ahead log (WAL), replication, snapshots |
| N+1 query problem | Eager loading, denormalization, batching |
| Long tail latency | Timeout + hedge requests, async processing |

---

## Distributed Systems Concepts — Interview Q&A

**Q: What is consistent hashing and why do we use it?**
A: A ring-based mapping of keys to nodes. When a node is added/removed, only K/N keys need remapping (K=keys, N=nodes). Traditional hashing remaps all keys. Used in: Redis cluster, Cassandra, CDN routing.

**Q: What is a leader election and how does it work?**
A: In a distributed system, one node must be the authoritative source for writes. Leader election algorithms: Paxos, Raft (used in etcd/Consul). The leader gets a lease; followers wait for heartbeat. If heartbeat fails, new election.

**Q: How do you handle the thundering herd problem?**
A: When cache expires, thousands of requests hit the DB simultaneously. Solutions:
1. Mutex/lock: only first request queries DB, rest wait
2. Probabilistic early expiry: randomly refresh before TTL
3. Request coalescing (singleflight): deduplicate concurrent identical requests

**Q: What is the difference between latency and throughput?**
A: Latency = time for one request. Throughput = requests per second. They're inversely related under load. Optimizing one often hurts the other. Little's Law: L = λW (avg requests in system = arrival rate × avg time in system).

**Q: Explain two-phase commit (2PC) and its problems.**
A: Phase 1: Coordinator asks all participants "ready to commit?" Phase 2: if all yes → commit. Problem: coordinator failure after phase 1 leaves participants blocked (blocking protocol). Solution: 3PC or distributed sagas.

**Q: What is a saga pattern?**
A: For distributed transactions without 2PC. Each service has a local transaction + compensating transaction. Choreography: events trigger next step. Orchestration: central coordinator. Used in microservices.

**Q: What is backpressure?**
A: When a downstream service is slower than upstream, backpressure signals the upstream to slow down. Prevents cascade failure. Implemented via: queue depth limits, reactive streams (RxJava), TCP flow control.
