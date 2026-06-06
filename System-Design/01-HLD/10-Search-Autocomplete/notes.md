# HLD: Search Autocomplete (Typeahead / Search-as-you-type)

## Requirements

**Functional:**
- Return top-k (5-10) search suggestions as the user types each character
- Suggestions ranked by popularity (query frequency)
- Support prefix matching ("ap" → "apple", "apple watch", "app store")
- Personalization layer (user's own history ranked higher)
- Analytics pipeline to update suggestion frequencies in near real-time
- Blocklist support for offensive or harmful suggestions

**Non-Functional:**
- 10K QPS at peak
- P99 latency < 100ms end-to-end from keypress to suggestions rendered
- Suggestions should reflect trending searches within minutes (not days)
- System supports 1B unique queries total in the index
- High availability (99.99%), read-heavy workload

---

## Capacity Estimation

```
Query volume:
  10K QPS = 10,000 prefix queries per second
  Average query: 5 chars typed → 5 autocomplete requests per full query
  Full searches/sec = 10K / 5 = 2K complete searches/sec

Storage:
  1B unique queries × avg 30 chars = 30 GB raw query strings
  With frequency counts and metadata: ~100 GB total

  Redis sorted sets per prefix (up to 7 chars):
  Unique prefixes up to 7 chars ≈ 26^1 + 26^2 + ... + 26^7 ≈ ~8B possible
  But only ~500M have actual queries mapped to them
  500M keys × avg 10 members × (30 + 8) bytes = ~190 GB
  Sharded across Redis cluster: 10 nodes × 19 GB each

CDN caching benefit:
  Prefixes of length 1-4: 26 + 676 + 17K + 456K ≈ 474K unique keys
  Each cached result: ~500 bytes → 474K × 500B = 237 MB total at CDN
  Nearly all short-prefix traffic (covers ~50% of QPS) served from CDN cache
```

---

## High-Level Architecture

```
                     ┌──────────────────────────────────────────────────┐
                     │             Client (Browser / App)               │
                     │  User types → debounce 100ms → cancel in-flight  │
                     │  AbortController if new char typed               │
                     └──────────────────────┬───────────────────────────┘
                                            │  GET /autocomplete?q=iph&userId=42
                                            ▼
                     ┌──────────────────────────────────────────────────┐
                     │               CDN Edge Cache                     │
                     │  Caches global suggestions for prefixes 1-4 chars│
                     │  Cache-Control: public, max-age=60               │
                     │  Vary: Accept-Language (per locale cache)        │
                     └──────────────────────┬───────────────────────────┘
                                            │ CDN miss (prefix > 4 chars or personalized)
                                            ▼
                     ┌──────────────────────────────────────────────────┐
                     │          Autocomplete API Service                │
                     │  (stateless, 20 nodes, LRU in-process cache)     │
                     └──────────┬──────────────────────┬───────────────┘
                                │                      │
             ┌──────────────────▼──┐        ┌──────────▼────────────────┐
             │  Redis Cluster       │        │  Personalization Service   │
             │  suggestions:{prefix}│        │  user_suggestions:{uid}   │
             │  Sorted Set top-k   │        │  (Cassandra + Redis cache) │
             └─────────────────────┘        └───────────────────────────┘
                                                          │
                                             ┌────────────▼──────────────┐
                                             │  Elasticsearch            │
                                             │  (fallback for prefix     │
                                             │   > 7 chars, fuzzy match) │
                                             └───────────────────────────┘

Frequency update pipeline (async):
  User submits full search
       │
       ▼
  Kafka topic "search.queries"
       │
       ▼
  Flink Streaming (5-min tumbling windows)
  → count per query → weighted decay merge → update Redis + ES
```

---

## Core Data Structure Option A: Trie

```
What a Trie is:
  Prefix tree where each node = one character.
  All strings sharing a prefix share the same tree path from root.

  After inserting "apple", "app", "apply":
        root
         │
         a
         │
         p
         │
         p  ← "app" (freq: 5000)
        / \
       l    l
       |    |
       y    e  ← "apple" (freq: 12000)
       |
      "apply" (freq: 3400)

Top-k stored at each node (pre-computed during index build):
  Node "ap" stores: ["apple", "app store", "apple watch", "apply", "app"]
  Avoids DFS traversal on every query — O(1) lookup after traversing prefix path.

Query complexity: O(p) where p = prefix length (traverse each character)
Update complexity: O(p × k) — must update stored top-k along entire path on score change

Memory:
  1B queries × ~50 bytes per node = ~50 GB
  Too large for single machine → need distributed sharding or switch to Redis sorted sets

When Trie is better:
  - Read-only index rebuilt nightly from query log batch job
  - You want exact prefix match with no dependencies on Redis
  - Simpler deployment where data fits in memory (smaller dataset)
```

---

## Core Data Structure Option B: Redis Sorted Set (Preferred at Scale)

```
One sorted set per prefix (up to prefix length 7):
  Key:   suggestions:{prefix}
  Score: frequency / popularity score (higher = shown first)
  Value: the full query string

Example for prefix "ap":
  ZADD suggestions:ap 12000 "apple"
  ZADD suggestions:ap 9500  "app store"
  ZADD suggestions:ap 8200  "apple watch"
  ZADD suggestions:ap 7100  "apple music"
  ZADD suggestions:ap 3400  "app"

Query for top 5:
  ZREVRANGE suggestions:ap 0 4 WITHSCORES
  O(log N + k) — extremely fast in Redis

Update on frequency change:
  ZINCRBY suggestions:ap 1 "apple"        ← real-time increment
  ZADD suggestions:ap 12001 "apple"       ← batch set from Flink job
  ZREMRANGEBYRANK suggestions:ap 0 -11   ← keep only top 10 entries

Key count management:
  "apple" (5 chars) → 5 sorted sets: a, ap, app, appl, apple
  Stop at prefix length 7 — covers 95%+ of real query traffic
  Prefixes > 7 chars handled by Elasticsearch (long-tail, lower QPS)

Memory:
  ~500M prefix keys × 10 entries each × 40 bytes avg = ~200 GB
  Sharded across 10 Redis nodes by hash(prefix[0:3]) → 20 GB per node
```

---

## Deep Dive 1: Caching Layers

```
Layer 1: Client-side LRU cache (browser / mobile app memory)
  Store last 20 prefix responses in Map<prefix, suggestions>
  User types "i" → "ip" → "iph" → backspace → "ip"
  Instantly shows "ip" result from local cache (0ms)
  Invalidate if user clears history or cache TTL expires (5 min)

Layer 2: CDN edge cache (for 1-4 char prefixes — global)
  26 + 676 + 17K + 456K = ~474K unique short prefix keys
  Total data: 474K × ~500B response = 237 MB per CDN PoP
  CDN TTL: 60 seconds (suggestions update every 5 min; 1-min staleness OK)
  Header: Cache-Control: public, max-age=60, s-maxage=60
          Vary: Accept-Language (separate cache entry per locale)
  Cache key: /autocomplete?q={prefix}&locale={locale}
  Personalized requests must NOT be cached at CDN (add user_id → bypass CDN)

Layer 3: API server in-process LRU cache (for 5-7 char prefixes)
  Cache size: 100K entries × ~500 bytes = 50 MB per node
  TTL: 30 seconds
  Cache hit rate for top prefixes: ~80-85% (power law — few prefixes = most traffic)

Layer 4: Redis sorted set (on cache miss)
  ZREVRANGE suggestions:{prefix} 0 9
  P99 latency from API server: ~5ms

Layer 5: Elasticsearch (prefix > 7 chars, fuzzy fallback)
  Edge-ngram tokenizer handles "iphone 15 pro c" → "iphone 15 pro case"
  Lower traffic tier (<3% of QPS), P99: ~20ms
  Also handles typo tolerance (fuzziness: 1)
```

---

## Deep Dive 2: Frequency Tracking and Score Updates

```
Event emission:
  On every completed search (user presses Enter or taps result):
  Kafka topic "search.queries":
    { userId, query, timestamp, sessionId, clickedSuggestion, resultCount }

Flink streaming aggregation:
  Tumbling window: 5 minutes
  Count occurrences of each query in the window
  Emit: { query, windowCount, windowEnd }

Score formula (weighted moving average for recency decay):
  new_score = 0.7 × existing_score + 0.3 × window_count

  Example:
    "iPhone 15" old score = 100,000
    This 5-min window saw 500 searches
    new_score = 0.7 × 100,000 + 0.3 × 500 = 70,150

  If a term stops being searched, its score decays each window until it falls out of top-k.
  If a new term spikes, it gains score quickly.

Applying to Redis:
  For each (query, new_score) from Flink:
    For prefix_len in range(1, min(len(query)+1, 8)):
      prefix = query[:prefix_len]
      ZADD suggestions:{prefix} {new_score} {query}
      ZREMRANGEBYRANK suggestions:{prefix} 0 -11  ← trim to top 10

  Optimization: only update if score changed by > 5% (skip writes for stable terms)

Trending detection:
  Compare 15-min window count to baseline (30-day hourly average)
  If ratio > 5×: mark as TRENDING, apply 2× score boost
  Store trending flag in Redis: SADD trending:queries {query} EX 3600
  Trending badge shown in autocomplete results: { text: "...", isTrending: true }
```

---

## Deep Dive 3: Personalization Layer

```
User query history (Cassandra):
  CREATE TABLE user_query_history (
      user_id     BIGINT,
      prefix      TEXT,
      query       TEXT,
      last_used   TIMESTAMP,
      use_count   INT,
      PRIMARY KEY (user_id, prefix, query)
  ) WITH CLUSTERING ORDER BY (prefix ASC, last_used DESC)
    AND default_time_to_live = 7776000;  -- 90 days

Personal score formula:
  personal_score = use_count × recency_weight
  recency_weight:
    last_used within 7 days  → 1.0
    last_used within 30 days → 0.5
    older                    → 0.1

Merging personal + global suggestions:
  1. Fetch global top-10 from Redis for prefix (always available)
  2. Fetch personal top-5 from Redis user cache (key: user_sug:{uid}:{prefix}, TTL: 5min)
     On cache miss: query Cassandra user_query_history
  3. Merge algorithm:
     - Personal suggestions occupy first 3 result slots (if score qualifies)
     - Remaining slots filled by global results not already in personal list
  4. Return combined top-5 to 10 results

Cache for personal suggestions:
  Key: user_sug:{userId}:{prefix}
  Only cache for prefix length 3+ (1-2 char prefixes too variable)
  TTL: 5 minutes (user's recent searches update more frequently)

Privacy handling:
  Personal history stored only for authenticated, opted-in users
  Guest users receive global-only suggestions
  GDPR delete: DELETE FROM user_query_history WHERE user_id = ?
               Also DEL all Redis keys matching user_sug:{userId}:*
```

---

## Deep Dive 4: Handling 10K QPS

```
Traffic distribution breakdown:
  ~50% hit CDN cache (1-4 char prefixes)         → 5,000 QPS by CDN
  ~35% hit API server in-process LRU cache        → 3,500 QPS from memory
  ~12% hit Redis cluster                          → 1,200 QPS to Redis
  ~3%  hit Elasticsearch (long-tail / fuzzy)      → 300 QPS to ES

Redis throughput headroom:
  Single Redis node: ~100K simple commands/sec
  1,200 ZREVRANGE commands/sec → single node has 80× headroom
  With 10-node cluster: 100K × 10 = 1M QPS capacity

API server sizing:
  Each API request: ~2ms total (in-process cache check + Redis call + merge)
  20 API nodes × 500 concurrent = 10,000 concurrent requests
  At 2ms avg: each node handles ~500 RPS → 20 nodes = 10K RPS total

Client-side debouncing (critical for QPS reduction):
  Debounce 100-150ms after last keypress before sending
  Cancel in-flight requests with AbortController on each new character
  Result: actual server QPS is ~60% lower than naive keystroke count

Scale-out triggers:
  API CPU > 70% on 3+ nodes → add 5 more nodes (auto-scale group)
  Redis memory per shard > 80% → add shard, rebalance prefix key ranges
  CDN cache hit rate < 40% → investigate CDN TTL or key distribution
```

---

## Data Models

### Redis Sorted Sets

```
Key:    suggestions:{prefix}
Score:  frequency score (float)
Member: full query string

Prefix    | Top entries
----------|----------------------------
a         | amazon, apple, airbnb, ...
ap        | apple, app store, apple watch, ...
app       | app store, apple, applebee's, ...
appl      | apple, applebee's, apple music, ...
apple     | apple, apple watch, apple music, ...

Key count estimate:
  ~500M active prefix keys in the cluster
  Sharded by: hash(prefix[:3]) % num_shards
```

### Query Analytics (ClickHouse for frequency aggregation)

```sql
CREATE TABLE search_query_events (
    query        String,
    user_id      UInt64,
    session_id   String,
    timestamp    DateTime,
    result_count UInt32,
    clicked_suggestion String
) ENGINE = MergeTree()
PARTITION BY toYYYYMM(timestamp)
ORDER BY (query, timestamp);

-- Aggregation query run by Flink every 5 minutes:
SELECT query, count() AS cnt
FROM search_query_events
WHERE timestamp >= now() - INTERVAL 5 MINUTE
GROUP BY query
HAVING cnt > 10
ORDER BY cnt DESC;
```

### Blocklist (Redis Set)

```
Key:  blocklist:queries:{locale}
Type: Redis Set
Members: normalized blocked query strings

Check before returning:  SISMEMBER blocklist:queries:en "offensive term"
Add to blocklist:        SADD blocklist:queries:en "offensive term"

Also: blocklist:domains:{locale} for blocking entire domain-based suggestions
Refresh: blocklist is loaded into API server in-process Set every 60s
         for O(1) lookup without Redis round-trip
```

---

## API Design

```
GET /api/v1/autocomplete?q={prefix}&limit=10&locale=en&userId={id}

Response:
{
  "query": "iph",
  "suggestions": [
    { "text": "iphone 15",       "score": 45000, "source": "global"   },
    { "text": "iphone 15 pro",   "score": 38000, "source": "personal" },
    { "text": "iphone case",     "score": 29000, "source": "global"   },
    { "text": "iphone charger",  "score": 21000, "source": "global"   },
    { "text": "iphone se",       "score": 18000, "source": "global",  "isTrending": true }
  ],
  "latencyMs": 8
}

Response Headers:
  Cache-Control: public, max-age=60    (for non-personalized)
  Cache-Control: private, no-store     (for personalized — userId present)
  Vary: Accept-Language

POST /internal/analytics/search
Body: { query, userId, sessionId, clickedSuggestion, resultCount }
(fire-and-forget from search results page → Kafka producer)
```

---

## Edge Cases

| Edge Case | Solution |
|-----------|---------|
| Offensive / spam query in suggestions | Blocklist filter at write time (Flink) and read time (API); blocklist loaded in-process for O(1) |
| New viral trend not in index yet | Trending detector flags 5× spike within 5-min window; fast-tracks ZINCRBY updates |
| Prefix with no Redis entries | Fall through to Elasticsearch edge_ngram; if still empty return empty array |
| Multi-language queries | Separate Redis key namespace per locale: suggestions:{locale}:{prefix} |
| Typo correction ("iphne") | Separate spell-check service using Levenshtein; runs parallel to autocomplete; blended in ranking |
| User history grows unbounded | Cassandra TTL (90 days) + cap at 1000 entries per user (ZREMRANGEBYRANK) |
| Hot prefix ("a") with millions of possible queries | Cap sorted set at top 100 entries; ZREMRANGEBYRANK after every update |
| CDN cache invalidation after blocklist update | Surrogate key / cache tag purge API call for affected prefix keys; takes < 30s globally |
| Bot inflating score of specific query | Rate limit search events per IP/user in Kafka consumer; anomaly detection on score velocity |

---

## Interview Deep-Dive Questions

**Q: Why Redis sorted sets instead of a Trie in a production system at scale?**
A: A Trie is elegant but operationally complex to distribute. With 1B queries and continuous updates from a streaming pipeline, a distributed trie requires custom sharding logic and complex consistency guarantees. Redis sorted sets give you atomic ZINCRBY for score updates, ZREVRANGE for top-k in one command, and trivial sharding by prefix hash. Redis also comes with replication, persistence, and extensive observability tooling. The Trie is ideal as a read-only in-memory index built offline, but for a live system with real-time updates Redis wins on simplicity.

**Q: How do you prevent bots from manipulating the autocomplete suggestions?**
A: Three defenses: (1) the Flink aggregation pipeline counts events per (userId, query, sessionId) and deduplicates — the same user searching the same term 1000 times in 5 minutes counts as ~1 unique signal, (2) score velocity limits — if a query's score grows faster than the 99th percentile rate, it is flagged for human review and removed from sorted sets until cleared, (3) minimum absolute threshold — a query needs 1000+ unique searchers over 30 days to qualify for autocomplete index. Bots typically operate from limited IP ranges and cannot generate enough unique user signals.

**Q: How would you add fuzzy matching ("iphne" → "iphone")?**
A: Run two parallel lookups: (1) exact prefix match via Redis for high-confidence results, (2) spell-correction service using a prebuilt BK-tree or Elasticsearch with fuzziness:1 for queries with no Redis hits or very low scores. The response merges both: exact matches shown first, fuzzy corrections shown with a "Did you mean: iphone?" label. Fuzzy matching is only triggered when the exact prefix returns fewer than 3 suggestions, to avoid slowing down the happy path.

**Q: How do you make suggestions update within minutes of a trending event?**
A: The Flink tumbling window is 5 minutes — so any surge in searches appears in Redis within 5 minutes. For sub-minute updates, reduce the window to 1 minute at the cost of more Redis write load. For truly instantaneous (< 30 seconds) trending, run a parallel real-time counter: INCR trend:{query}:{10s_bucket} on every search event, check if current bucket count is 3× the previous bucket, and inject the query into Redis sorted sets immediately with a boosted score bypassing the Flink batch window.
