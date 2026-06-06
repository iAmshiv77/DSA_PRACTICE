# HLD: Distributed Web Crawler

## Requirements

**Functional:**
- Crawl billions of web pages across the internet
- Extract text content and all hyperlinks from each page
- Store raw HTML and extracted metadata
- Respect robots.txt and crawl delay rules (politeness)
- Handle duplicate URLs and duplicate content
- Recrawl pages periodically to detect changes
- Support JavaScript-rendered pages (SPAs)

**Non-Functional:**
- Crawl 1B pages per day (entire crawlable web in ~50 days)
- Deduplicate both URLs (same page) and content (mirror pages)
- Scale horizontally — 50-1000 crawler nodes
- Fault tolerant: single node failure should not lose crawl progress
- Respect server-imposed politeness constraints per domain

---

## Capacity Estimation

```
Crawl rate target:
  1B pages/day = 11,600 pages/sec

Page size:
  Average HTML page: 100 KB (text + markup, no media)
  11,600 pages/sec × 100 KB = 1.1 GB/sec raw HTML ingestion

Storage (raw HTML, compressed):
  1B pages × 100 KB = 100 TB/day uncompressed
  With gzip (avg 5:1 ratio on HTML): ~20 TB/day compressed
  Retain 90 days → 1.8 PB total (use tiered storage: S3 Standard → Glacier)

URL metadata:
  50B URLs in the full web × 200 bytes each = 10 TB
  Stored in Cassandra: partitioned by domain hash

URL dedup:
  Bloom filter for 50B URLs: 10 bits/element × 50B = 62.5 GB RAM
  Sharded across Bloom filter cluster

DNS lookups:
  Without caching: 11,600 pages/sec ≈ up to 11,600 DNS queries/sec (1 per new domain)
  In practice: ~100 unique domains/sec (many pages are on same domains)
  With caching: ~100 DNS lookups/sec (trivial)
```

---

## High-Level Architecture

```
                         ┌──────────────────────────────────────────┐
                         │              URL Frontier                 │
                         │   (Priority Queue + Politeness Scheduler)│
                         └──────────────────────┬───────────────────┘
                                                 │
                     ┌───────────────────────────┼──────────────────────────┐
                     ▼                           ▼                          ▼
              ┌─────────────┐           ┌─────────────┐           ┌─────────────┐
              │  Fetcher    │           │  Fetcher    │           │  Fetcher    │  ×50-1000
              │  Node 1     │           │  Node 2     │           │  Node N     │
              │  (async I/O)│           │  (async I/O)│           │  (async I/O)│
              └──────┬──────┘           └──────┬──────┘           └──────┬──────┘
                     └───────────────────────── ┼ ──────────────────────┘
                                                 │
                         ┌──────────────────────▼───────────────────────┐
                         │           Content Processor                   │
                         │  - robots.txt enforcement                     │
                         │  - HTML parsing (link extraction)             │
                         │  - Text extraction + language detection       │
                         │  - URL normalization                          │
                         │  - Content dedup (SimHash / MD5)              │
                         └──────────────────────┬───────────────────────┘
                                                 │
              ┌──────────────────────────────────┼───────────────────────────────┐
              ▼                                  ▼                               ▼
  ┌───────────────────────┐       ┌──────────────────────┐       ┌───────────────────────┐
  │  Raw Storage          │       │  URL Metadata         │       │  URL Frontier          │
  │  (S3 / HDFS)          │       │  (Cassandra)          │       │  (newly discovered     │
  │  compressed HTML +    │       │  crawl status,        │       │   URLs fed back in)    │
  │  metadata.json        │       │  last_crawled,        │       └───────────────────────┘
  └───────────────────────┘       │  content_hash         │
                                  └──────────────────────┘
```

---

## Deep Dive 1: URL Frontier (Priority Queue + Politeness)

```
The URL Frontier is the heart of the crawler — it decides what to crawl next and when.

Two-tier queue design:

TIER 1: Front Queues (by priority — what to crawl)
  Priority score = f(domain_authority, PageRank, freshness_signal, days_since_last_crawl)
  Queues:
    HIGH:   News sites (.bbc.com, reuters.com), government (.gov), high-PageRank domains
    MEDIUM: Popular sites with moderate update frequency
    LOW:    Long-tail domains, user-generated content, high spam-risk

  Biased selector picks from HIGH queue 70% of the time, MEDIUM 25%, LOW 5%.

TIER 2: Back Queues (by domain — politeness enforcement)
  One queue per active domain (e.g., "bbc.com", "stackoverflow.com")
  A min-heap over all domain queues, ordered by next_allowed_crawl_time

  next_allowed_crawl_time = last_fetched_at + crawl_delay
  crawl_delay = max(robots.txt Crawl-delay value, 1 second minimum)

Selector algorithm:
  1. Pop front queue (by priority) → get URL
  2. Look up domain's back queue; check next_allowed_crawl_time
  3. If time has not passed → put URL back in front queue; pick next domain
  4. If time passed → dispatch URL to fetcher; update next_allowed_crawl_time
  5. If domain has no back queue → create one; fetch robots.txt first

Why separate tiers?
  Tier 1 (priority) decides importance ordering
  Tier 2 (domain buckets) ensures politeness per host regardless of priority
  Without tier 2: crawler might send 100 requests/sec to bbc.com → ban/block

Storage for URL Frontier:
  Too large for memory alone (billions of URLs in-flight)
  Solution: disk-backed priority queue (Apache Kafka or RocksDB-based)
  Keep top ~10M URLs in memory (Redis sorted set: ZADD crawl_queue {score} {url})
  Remainder in Kafka topic (partitioned by domain hash)
```

---

## Deep Dive 2: URL Deduplication with Bloom Filter

```
Problem: With 50B URLs, storing a hash set to check "have we crawled this?" takes:
  Hash set: 50B × 40 bytes per entry = 2 TB — too expensive

Bloom Filter:
  Probabilistic structure: O(1) insert and lookup
  No false negatives: if URL is in filter, will never be missed
  Controllable false positive rate: at 10 bits/element, FPR = 0.8%
  Memory: 50B × 10 bits = 62.5 GB → fits in a distributed Redis cluster

  False positive consequence: a valid new URL is occasionally skipped
  Rate of 0.8% means 8M out of 1B new URLs skipped per crawl → acceptable

Bloom filter operations:
  Before adding URL to frontier: BLOOM.EXISTS url_bloom {normalized_url}
    → YES (probably seen): skip
    → NO (definitely not seen): add to frontier + BLOOM.ADD url_bloom {normalized_url}

Distributed Bloom Filter (Redis Bloom module):
  Shard by hash(url[:domain]) across 5 Redis nodes
  Each node holds portion of the bit array
  RedisBloom module: BF.ADD, BF.EXISTS commands

URL normalization (before Bloom filter check):
  1. Lowercase scheme and hostname
  2. Remove default ports (:80, :443)
  3. Resolve relative paths (/../foo → /foo)
  4. Remove URL fragments (#section → gone)
  5. Remove tracking parameters: utm_*, sessionid=*, PHPSESSID=*
  6. Sort query parameters alphabetically (a=1&b=2 ≡ b=2&a=1)
  7. Decode then re-encode percent-encoding (canonical form)

Example:
  HTTP://BBC.COM:80/News/Article?utm_source=twitter&id=123#comments
  → http://bbc.com/News/Article?id=123
```

---

## Deep Dive 3: Robots.txt Compliance and Politeness

```
robots.txt spec:
  Fetched from: {scheme}://{hostname}/robots.txt
  Cached in Redis: robots:{domain} with TTL = 24 hours (per spec, re-check daily)

  Sample robots.txt:
    User-agent: *
    Disallow: /private/
    Disallow: /api/
    Crawl-delay: 2

    User-agent: Googlebot
    Allow: /
    Crawl-delay: 0.5

Crawler compliance steps:
  1. On first visit to any domain:
     a. Fetch {domain}/robots.txt
     b. If 404 → no restrictions; proceed freely
     c. If 5xx → assume no restrictions (don't retry immediately)
     d. Parse rules matching our User-Agent (e.g., "MyCrawler")
     e. Cache parsed rules in Redis for 24h
  2. Before fetching any URL: check if path is Disallowed
  3. Apply Crawl-delay from robots.txt (minimum 1s if not specified)

Sitemap.xml:
  robots.txt may include: Sitemap: https://example.com/sitemap.xml
  Sitemaps provide: all pages the site wants crawled, with <lastmod>, <changefreq>
  High-value signal: site owners telling us explicitly what to crawl and how often

Our crawler identity:
  User-Agent: MyCrawler/2.0 (+https://example.com/crawler; crawler@example.com)
  Identify clearly to allow site owners to whitelist or contact us

Additional politeness:
  Max concurrent requests to same domain: 1 (serial, not parallel)
  Honor Crawl-delay even if it means crawling fewer pages per second
  Back off exponentially on 429 (Too Many Requests) responses
  Stop crawling domain if > 5 consecutive errors → re-queue after 6 hours
```

---

## Deep Dive 4: Content Deduplication with SimHash

```
Two types of duplicate content:

Type 1: Exact duplicates (same content, different URL)
  Example: www.example.com/page and m.example.com/page (desktop vs mobile)
  Detection: MD5 or SHA-256 of normalized HTML text content
  Store: content_hash in URL metadata table
  On crawl: if content_hash already exists in DB → mark as DUPLICATE, skip indexing

Type 2: Near-duplicates (mostly same content, minor differences)
  Example: paginated articles (/page/1, /page/2), syndicated news, scraped/copied content
  MD5 would differ even for 99% identical content

SimHash algorithm (invented at Google):
  1. Tokenize document into n-grams (word-level 3-grams work well)
  2. For each token: compute 64-bit hash
  3. For each of 64 bit positions:
     count = sum of (hash[i]==1 ? +1 : -1) × token_frequency
  4. Final SimHash: bit[i] = count[i] > 0 ? 1 : 0

  Result: a 64-bit fingerprint where similar documents have similar fingerprints

Hamming distance:
  Distance = number of differing bits between two 64-bit SimHashes
  Distance ≤ 3: documents are near-duplicates (> 97% similar content)
  Distance ≥ 10: documents are clearly different

At scale (50B pages), brute force comparison is impossible.
Hamming distance lookup trick:
  Split 64-bit SimHash into 4 × 16-bit chunks
  For each chunk: lookup all stored hashes that share that 16-bit prefix
  Near-duplicates will match on at least one chunk (pigeonhole principle)
  Store: 4 hash tables (one per 16-bit chunk) in Cassandra
  Query: 4 lookups per page → find candidates → compute exact Hamming distance
```

---

## Deep Dive 5: Distributed Crawler Architecture

```
Each fetcher node:
  Single-threaded asynchronous I/O (Python aiohttp, Node.js, or Go)
  1000+ concurrent connections per node (non-blocking)
  50 fetcher nodes × 1000 concurrent = 50,000 concurrent HTTP connections
  At avg 200ms per page: 50K / 0.2s = 250,000 pages/sec capacity (10× headroom)

Domain assignment (consistent hashing):
  Each domain is always assigned to the same fetcher node
  Consistent hash ring: hash(domain) → node
  Why: prevents multiple nodes hitting the same domain simultaneously
       (would violate politeness even if each node stays within Crawl-delay)
  On node failure: its domains redistribute to remaining nodes

Coordinator (ZooKeeper or etcd):
  Tracks: which node owns which domain range
  Monitors: node heartbeats (every 10s)
  On failure detection: reassign domain ranges; other nodes pick up frontier entries

Checkpointing:
  Fetcher nodes ACK each URL to the coordinator after successful crawl
  Coordinator marks URL as CRAWLED in URL metadata table
  On node crash: un-ACKed URLs automatically return to frontier (at-least-once crawl)

DNS caching:
  Each fetcher node maintains a local DNS cache (TTL-aware)
  Cache entry: { hostname → [ip1, ip2], expiresAt }
  Without caching: every URL requires a DNS lookup before TCP connect
  With caching: resolve once per domain per TTL (typically 300s)
  Benefit: reduces DNS resolver load from 50K lookups/sec to ~100/sec
  Also: spread connections across all returned IPs (round-robin per cached entry)

Connection pooling:
  Maintain HTTP keep-alive connections to frequently crawled domains
  Avoids TCP handshake overhead for domains we crawl repeatedly
  Pool size per domain: 1 connection (serializes requests, respects Crawl-delay)
```

---

## Deep Dive 6: JavaScript Page Rendering

```
Problem:
  ~60% of modern pages require JavaScript execution to render content
  (React, Vue, Angular SPAs return mostly empty HTML without JS)
  Raw HTML fetch returns: <div id="root"></div> — useless for indexing

Two-pass crawl strategy:

Pass 1 (fast — every page):
  Fetch raw HTML with standard HTTP GET
  Extract text content (strip tags)
  If text content > 500 words: process normally (skip JS rendering)
  If text content < 100 words AND page has heavy JS scripts: mark for Pass 2

Pass 2 (slow — JS-rendered pages only):
  Headless Chromium via Playwright/Puppeteer
  Launch browser instance (pooled — expensive to start)
  Navigate to URL, wait for condition:
    networkidle: no network requests for 500ms
    OR domcontentloaded + 3s timeout
    Max total timeout: 10s
  Extract rendered HTML → process as normal

Cost comparison:
  Pass 1 (raw fetch):       ~50ms per page, 10 MB RAM per worker
  Pass 2 (headless Chrome): ~3-5 seconds per page, 300 MB RAM per worker
  → Only ~30% of pages need Pass 2
  → Maintain separate fleet for JS rendering (smaller, GPU-optimized instances)

Heuristics to decide if Pass 2 needed:
  1. Domain known to be SPA (whitelist maintained manually + ML classifier)
  2. URL pattern matches SPA routes (/app/, /#/, /dashboard/)
  3. Raw HTML contained <script src="bundle.js"> but < 50 words of text
  4. Page returned HTTP 200 but content-length < 2KB (suspiciously small)

Optimization:
  Block unnecessary resources in headless browser:
    navigator.serviceWorker.register → disable
    Image loads → block (not needed for text extraction)
    Video/audio → block
    Third-party analytics scripts → block
  Reduces render time from 5s to 1-2s per page
```

---

## Data Models

### URL Metadata (Cassandra)

```
CREATE TABLE url_metadata (
    url_hash        TEXT PRIMARY KEY,      -- SHA-256 of normalized URL
    url             TEXT,
    domain          TEXT,
    last_crawled    TIMESTAMP,
    next_crawl_at   TIMESTAMP,
    crawl_status    TEXT,                  -- SUCCESS, ROBOTS_BLOCKED, ERROR, DUPLICATE
    http_status     INT,
    content_hash    TEXT,                  -- for change detection
    simhash         BIGINT,               -- 64-bit near-dedup fingerprint
    page_rank       FLOAT,
    crawl_depth     INT,
    discovered_from TEXT,                  -- parent URL hash
    content_lang    TEXT
);

-- Index for domain-level queries (recrawl scheduling):
CREATE TABLE domain_crawl_schedule (
    domain          TEXT PRIMARY KEY,
    next_crawl_at   TIMESTAMP,
    robots_cached   TIMESTAMP,
    crawl_delay_sec INT,
    is_banned       BOOLEAN
);
```

### Crawl Schedule (Redis Sorted Set)

```
Key:    crawl_schedule
Type:   Sorted Set
Score:  next_crawl_at as Unix timestamp
Member: url_hash

Pick next 10,000 URLs due for crawl:
  ZRANGEBYSCORE crawl_schedule 0 {now} LIMIT 0 10000

After processing each URL:
  ZADD crawl_schedule {next_crawl_at} {url_hash}   (update score for recrawl)
  or ZREM crawl_schedule {url_hash}                (if permanent remove)
```

### Raw Content Storage (S3)

```
Layout:
  s3://crawler-raw/{year}/{month}/{day}/{domain_hash}/{url_hash}/
    raw.html.gz        ← compressed raw HTML
    metadata.json      ← { url, fetched_at, http_status, content_type, headers }
    extracted.txt.gz   ← extracted clean text (for indexing pipeline)
    links.json         ← extracted outbound links (normalized)

Lifecycle policy:
  Standard tier: 90 days (operational recrawl reference)
  Glacier tier:  90–365 days (archival, low-cost)
  Delete after:  365 days
```

---

## Recrawl Scheduling

```
Not all pages need to be recrawled at the same frequency:

  News articles (bbc.com, reuters.com): recrawl every 15 minutes
  E-commerce product pages:             recrawl every 24 hours
  Wikipedia articles:                   recrawl every 7 days
  Static documentation pages:           recrawl every 30 days

Freshness signals to determine recrawl frequency:
  1. Sitemap.xml <changefreq>: "hourly", "daily", "weekly", "monthly", "never"
  2. HTTP response headers: Last-Modified, ETag (use If-Modified-Since on recrawl)
  3. Historical change rate: track how often content_hash changes across crawls
     If page has not changed in last 5 crawls → double the interval (exponential backoff)
     If page changes every crawl → decrease interval

HTTP conditional GET (reduces bandwidth):
  On recrawl: include If-None-Match: {stored_etag} or If-Modified-Since: {last_crawled}
  Server returns 304 Not Modified → skip storage and processing (save 100 KB + compute)
  Only ~30% of recrawled pages actually have new content → 70% bandwidth savings

Adaptive scheduling formula:
  new_interval = current_interval × (1 + unchanged_streak × 0.5)
  unchanged_streak: consecutive crawls with no content change
  Max interval: 30 days (even static pages recrawled monthly for availability check)
```

---

## Edge Cases

| Edge Case | Solution |
|-----------|---------|
| Spider trap (infinite URL space — calendar, session IDs) | URL depth limit (max 10 path segments); path cycle detection; per-domain URL count cap (> 50K → throttle) |
| Redirect loop (A → B → A) | Track redirect chain per crawl session; abort after 5 redirects |
| Very large HTML page (100 MB) | Size limit: skip body after 10 MB; store truncation flag in metadata |
| Malicious domain serving malware | Crawl inside sandboxed environment (no JS execution in raw crawler); scan extracted content with threat intel API |
| DNS poisoning / SSRF | Validate resolved IP is not RFC-1918 private range before connecting; block 127.0.0.1, 10.x.x.x, 192.168.x.x |
| Domain rotating fast (spammy SEO domains) | Per-domain PageRank threshold; low-authority domains in LOW priority queue |
| Crawler IP banned by target | Rotate across IP pool (residential proxy or distributed IP ranges per region) |
| Robots.txt fetch fails (5xx) | Treat as no restrictions for one crawl cycle; re-fetch robots.txt on next cycle |
| Near-duplicate farm (1000 pages with same content, different ads) | SimHash deduplication; index only canonical page (rel=canonical header) |

---

## Interview Deep-Dive Questions

**Q: How do you handle a site that generates infinite URLs (spider trap)?**
A: Multiple defenses at different levels. First, URL depth limit: URLs with more than 10 path segments are rejected before even entering the frontier. Second, path cycle detection: if a URL path contains a repeated segment pattern like /a/b/a/b/, it is flagged and blocked. Third, per-domain URL count cap: if a single domain generates more than 50K unique URLs during a crawl run, we throttle it to 1 URL/minute and flag for human review. Fourth, query parameter explosion: URLs that differ only in auto-generated session or tracking parameters get normalized to the same canonical form.

**Q: Why use Bloom filter instead of a database table for URL deduplication?**
A: A database lookup for every URL at 11,600 pages/sec (with 50B total URLs) would require a massive database cluster even with indexing. A Bloom filter provides O(1) lookup with ~62 GB of RAM for 50B URLs at 0.8% false positive rate. The 0.8% false positive means we occasionally skip a valid new URL — the tradeoff is worth the 30× memory saving. A database is still used as the canonical store for URL metadata (crawl status, timestamps), but the Bloom filter acts as the fast-path deduplication gate before any DB write.

**Q: How do you prioritize which pages to crawl when you can only crawl 1B/day but the web has 50B pages?**
A: Priority is determined by a composite score: domain authority (well-established domains ranked higher), estimated PageRank (pages with more inbound links are more important), freshness signals (news domains recrawled hourly, static pages monthly), and time since last crawl (pages overdue for recrawl get higher priority). In practice: the crawlable web follows a power law — the top 10M domains account for 80%+ of important content. We ensure those are crawled first and more frequently.

**Q: How do you handle JavaScript-heavy sites without making your crawler too slow?**
A: Two-pass strategy. Pass 1 is a fast raw HTTP fetch for all pages. If the extracted text content is substantive (> 500 words), we process it immediately and skip JS rendering. Only pages that come back nearly empty trigger Pass 2 (headless Chromium). We also block unnecessary resources in the headless browser (images, videos, third-party analytics scripts) which reduces render time from 5 seconds to 1-2 seconds. A separate, smaller fleet handles JS rendering so it never blocks the main fast-path crawlers.

**Q: Walk me through what happens from the moment a brand new URL is discovered.**
A: (1) Link extractor finds href in a parsed page, (2) URL is normalized (lowercase, remove tracking params, canonical form), (3) Bloom filter check: if already seen, discard, (4) If new: added to URL metadata table with status PENDING, added to Bloom filter, (5) Priority score computed: domain authority + PageRank estimate, (6) URL enqueued into the appropriate front queue (HIGH/MEDIUM/LOW) in the URL Frontier, (7) Frontier scheduler checks domain's next_allowed_crawl_time, (8) When time arrives, URL dispatched to the fetcher node responsible for that domain (consistent hash), (9) Fetcher makes HTTP GET respecting robots.txt, (10) Response processed: HTML parsed, content stored to S3, new links extracted and the cycle repeats.
