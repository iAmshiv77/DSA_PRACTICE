# HLD: Google Search Indexing

## Core Problem
Crawl the web (~50B pages), build an inverted index, serve search results in < 200ms.

## Components
```
Web Crawler → Raw Storage → Document Processor → Inverted Index Builder → Query Service

                                ┌─────────────────────────────────────────────┐
                                │  URL Frontier (priority queue + dedup)       │
                                │         ↓                                    │
                                │  Crawler Fleet (distributed)                │
                                │         ↓                                    │
                                │  Raw Content Store (HDFS / Blob)            │
                                │         ↓                                    │
                                │  Content Processor:                         │
                                │    - HTML parse                             │
                                │    - Link extraction                        │
                                │    - Text extraction + tokenization         │
                                │    - Language detection                     │
                                │         ↓                                   │
                                │  Index Builder (MapReduce / Spark)          │
                                │         ↓                                   │
                                │  Sharded Inverted Index (Index Servers)     │
                                │         ↓                                   │
                                │  Query Serving Layer                        │
                                └─────────────────────────────────────────────┘
```

## Inverted Index

```
Forward index:
  docId → list of words
  Doc 1: "the quick brown fox"
  Doc 2: "the quick red fox"

Inverted index:
  word → posting list [(docId, position, TF)]
  "quick" → [(1, 2, 0.3), (2, 2, 0.3)]
  "brown" → [(1, 3, 0.2)]
  "fox"   → [(1, 4, 0.4), (2, 4, 0.4)]

Compressed posting list:
  Delta encoding: [1, 3, 7, 12] → [1, 2, 4, 5]  (store gaps, not absolutes)
  VarInt encoding: store gaps as variable-length integers
  Result: 10x compression for common words
```

## URL Frontier (BFS with Prioritization)
```
Two queues:
  Priority Queue (what to crawl next):
    Score = PageRank(url) × freshness_factor × domain_authority
    High priority: news sites, .gov, high-PageRank pages
    Low priority: forums, user-generated content

  Politeness Queue (throttle per domain):
    One queue per domain
    Respect robots.txt crawl-delay
    Default: 1 request per domain per second

Deduplication:
  Bloom filter: O(1) membership test with false positives (no false negatives)
    - Space: 1 bit per URL (approximate)
    - When full: rotate to new filter, merge periodically
  URL normalization before dedup:
    - Lowercase scheme + host
    - Remove default port, trailing slash
    - Sort query params
    - Remove session IDs (?sessionid=xxx)
```

## PageRank Algorithm
```
PageRank(A) = (1 - d) + d × Σ(PageRank(B) / outLinks(B))
d = damping factor (0.85) — probability user keeps clicking vs starts fresh

Iterative computation until convergence:
  Initialize: all pages = 1/N
  Iterate: update each page's score based on inbound links
  Converge: when change < 0.0001

Computed via MapReduce:
  Map:   (page, rank) → (linked_page, rank / num_links)
  Reduce: (page, [incoming_ranks]) → (page, 0.15 + 0.85 × sum(incoming_ranks))
  Run 10-50 iterations
```

## Content Deduplication
```
Near-duplicate detection (SimHash):
  1. Extract features (n-grams) from document
  2. Compute weighted hash for each feature
  3. SimHash = sign of each bit position across all feature hashes
  4. Two docs similar if Hamming distance < threshold (e.g., 3 bits)
  5. Store SimHash fingerprints in hash table
  6. O(1) lookup for near-duplicate detection

Exact duplicates:
  MD5/SHA256 of normalized content
  Store content hash in Redis/DB
  Skip indexing if hash already seen
```

## Index Serving (Distributed)
```
Index sharding strategies:
  1. Document sharding: each shard has ALL terms for N docs
     Query: broadcast to all shards, merge results
     Simple but every query hits all shards

  2. Term sharding: shard by term (all postings for "apple" on shard 3)
     Query: route each term to its shard, merge at query layer
     Uneven — popular terms (stopwords) → hot shards

  3. Combined: document sharding + term-level caching for hot terms

Index server structure:
  IndexServer (RAM-resident hot index + disk-based cold index)
  ├── InMemory index: last 24hr crawl (recent content)
  └── Disk index: full crawled web (weeks/months old)

Query processing:
  1. Parse query → tokenize, stem, expand synonyms
  2. Look up posting lists for each term
  3. Intersect posting lists (AND), union (OR)
  4. Score each result: TF-IDF × PageRank × freshness × click-through-rate
  5. Return top K results
```

## TF-IDF Scoring
```
TF  (Term Frequency)  = occurrences of term in doc / total words in doc
IDF (Inverse Doc Freq) = log(total_docs / docs_containing_term)
Score = TF × IDF

IDF punishes common words ("the", "and") — they appear in every doc → IDF ≈ 0
IDF rewards rare terms ("photosynthesis") → high IDF → high score

Modern: BM25 (better than TF-IDF):
  BM25 = IDF × (TF × (k+1)) / (TF + k × (1 - b + b × (docLen/avgDocLen)))
  k=1.5, b=0.75 — standard parameters
  Handles document length normalization better
```

## Query Serving < 200ms
```
Optimization layers:
  1. Query cache (Redis): exact query → cached result (TTL 5min)
     Cache hit rate: ~40% for popular queries
  2. Posting list cache (in-memory on index server): hot terms kept in RAM
  3. Index servers serve from SSD (not spinning disk)
  4. Early termination: stop at top-K results, don't score all docs
  5. Tiered index: 3 tiers
     Tier 1: top 1M highest-quality docs (RAM, very fast)
     Tier 2: next 100M docs (SSD, fast)
     Tier 3: remaining (HDD, slow)
     Start query at Tier 1 — only go to Tier 2 if insufficient results

Parallelism:
  Query broadcast to all shards simultaneously (fan-out)
  Each shard returns top 10 candidates
  Query aggregator merges, re-ranks, returns final top 10
  Hedged requests: send to 2 replicas of each shard, use faster response
```

## Interview Q&A

**Q: How do you handle re-crawling (keeping index fresh)?**
```
Priority-based recrawl schedule:
  High-frequency content (news, stock prices): recrawl every 15min
  Normal sites: recrawl every few days to weeks
  Rarely changed (old blog posts): recrawl monthly

Signals for recrawl priority:
  - Last-Modified HTTP header
  - ETag (content hash from server)
  - Historical change frequency (how often did this page change before?)
  - Sitemap.xml with <lastmod> hints
  - Push notifications (PubSubHubbub / WebSub)

Incremental crawl: only re-index if content hash changed
```

**Q: How do you deal with JavaScript-heavy sites (React/Vue)?**
```
Headless browser rendering (like Puppeteer/Playwright in crawler fleet):
  1. Fetch HTML (fast — most content)
  2. If no meaningful text content detected → queue for JS rendering
  3. Headless Chrome renders page, executes JS, waits for network idle
  4. Extract rendered HTML → index

Cost: JS rendering is 10-100x more expensive
Solution: Render only for sites that need it (heuristic: < 200 words in raw HTML)
```

**Q: What's the difference between crawling and indexing?**
```
Crawling:  Discovering and fetching web pages (HTTP requests, following links)
Parsing:   Extracting text, links, metadata from HTML
Indexing:  Processing extracted text, computing scores, building inverted index
Serving:   Responding to user queries using the index

These are separate pipelines. Crawled content sits in raw storage for days
before indexing pipeline processes it. Indexing happens in large batch jobs.
```
