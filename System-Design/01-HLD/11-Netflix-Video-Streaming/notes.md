# HLD: Netflix Video Streaming Platform

## Requirements

**Functional:**
- Stream movies and TV shows to 220M subscribers worldwide
- Support multiple devices: Smart TV, mobile, browser, game consoles
- Adaptive bitrate streaming (adjust quality based on network speed)
- Subtitle and multi-audio track support
- Resume playback across devices ("Continue Watching")
- Offline download for mobile
- Recommendation engine (personalized home screen)
- A/B testing for thumbnails (maximize click-through rate)
- DRM content protection

**Non-Functional:**
- 15M concurrent streams at peak
- < 200ms time-to-first-frame
- Rebuffering rate < 0.1%
- 99.99% availability
- ~75 Tbps peak egress bandwidth (handled by CDN)

---

## Capacity Estimation

```
Content library:
  36,000 hours of content
  1 hour × 5 resolutions × ~5 GB/resolution = 25 GB per hour
  36,000 × 25 GB = 900 TB of video storage

Streaming:
  15M concurrent streams × 5 Mbps avg = 75 Tbps peak egress
  CDN absorbs ~99% → origin serves ~750 Gbps

Metadata:
  15,000 titles × 10 KB metadata = 150 MB (trivial)
  Watch history: 220M users × 500 events/user × 100 bytes = 11 TB

Encoding storage:
  Each hour of content after encoding all resolutions + DRM: ~25 GB
  36,000 hours × 25 GB = 900 TB
  With replication × 3 = 2.7 PB
```

---

## High-Level Architecture

```
                         ┌────────────────────────────────────────────────┐
                         │                 Client Devices                  │
                         │    Smart TV, iOS, Android, Browser, PS5/Xbox   │
                         └──────────────────────┬─────────────────────────┘
                                                │
                  ┌─────────────────────────────┼──────────────────────────────┐
                  ▼                             ▼                              ▼
         ┌────────────────┐          ┌──────────────────┐           ┌─────────────────┐
         │  Netflix CDN   │          │   API Gateway     │           │  Open Connect   │
         │  (Open Connect │          │   (auth, routing) │           │  ISP Appliances │
         │   Global PoPs) │          └────────┬──────────┘           │  (embedded CDN) │
         └────────────────┘                   │                      └─────────────────┘
                  ▲                           │
    ┌─────────────┘       ┌───────────────────┼──────────────────┐
    │                     │                   │                  │
    │            ┌────────▼──────┐   ┌────────▼──────┐   ┌───────▼──────────┐
    │            │  Playback     │   │  Metadata     │   │  Recommendation  │
    │            │  Service      │   │  Service      │   │  Service         │
    │            │  (manifest,   │   │  (Cassandra)  │   │  (ML pipeline)   │
    │            │   license)    │   └───────────────┘   └──────────────────┘
    │            └────────┬──────┘
    │                     │
    │            ┌────────▼──────┐
    │            │  DRM / License│
    │            │  Service (KMS)│
    │            └───────────────┘
    │
    │   ┌──────────────────────────────────────────────────────────────┐
    └───│  Origin Storage (S3): encoded video segments + manifests     │
        └──────────────────────────────────────────────────────────────┘

Content Ingestion Pipeline (separate from streaming path):
  Raw Upload → S3 → Transcoding Workers → Packaging → CDN distribution
```

---

## Deep Dive 1: Video Encoding Pipeline

```
Step 1: Raw upload
  Content team uploads raw ProRes/RAW file (50–200 GB per movie)
  Chunked multipart upload directly to S3 (bypass API servers)
  S3 → SQS event → Video Processing Service

Step 2: Pre-processing
  Validate file integrity (checksums)
  Scene detection (split into ~10-second independent encoding units)
  Audio track separation (main audio, dubbed tracks, audio description)
  Subtitle extraction/conversion to WebVTT

Step 3: Transcoding (parallel workers per resolution + chunk)
  Netflix runs "per-title encoding" — each title gets custom encoding parameters
  instead of fixed presets. Example:
    Simple animation (low complexity): -crf 28 achieves same quality as -crf 23 on live action
    Action movie (high motion): needs higher bitrate to avoid blocking artifacts

  Resolutions and target bitrates:
    240p:  0.35 Mbps  (H.264)
    360p:  0.58 Mbps  (H.264)
    480p:  1.05 Mbps  (H.264)
    720p:  3.00 Mbps  (H.264 / H.265)
    1080p: 6.00 Mbps  (H.264 / H.265 / AV1)
    4K:    16.0 Mbps  (H.265 / AV1)
    HDR:   +50% bitrate over SDR equivalent

  Codec strategy:
    H.264 (AVC)  → widest device compatibility (every device from 2008+)
    H.265 (HEVC) → 40% smaller at same quality; requires hardware decode support
    AV1          → 50% smaller than H.264; royalty-free; best compression; encode is slow
    VP9          → older Google codec; used for Chrome web playback before AV1

Step 4: HLS/DASH packaging
  Each resolution encoded as fMP4 segments (fragmented MP4, 4-6 seconds each)
  HLS:  .m3u8 playlist per resolution + master playlist
  DASH: .mpd manifest + .m4s segments
  Both formats generated to support all device types

Step 5: DRM encryption
  Widevine (Google) → Android, Chrome, Chromecast
  FairPlay (Apple)  → iOS, macOS, Apple TV, Safari
  PlayReady (Microsoft) → Xbox, Windows, Edge
  Each segment encrypted with content encryption key (CEK)
  CEK stored in KMS (Key Management Service), never in video file

Step 6: Upload to origin S3 + CDN distribution
  S3 path: /titles/{titleId}/{resolution}/seg_{n}.m4s
  Manifest: /titles/{titleId}/master.mpd or master.m3u8
  CDN pre-warm: pushed to OCAs before release
```

---

## Deep Dive 2: Adaptive Bitrate Streaming (ABR)

```
Master playlist (HLS m3u8):
  #EXTM3U
  #EXT-X-STREAM-INF:BANDWIDTH=350000,RESOLUTION=426x240,CODECS="avc1.42e00a"
  240p/playlist.m3u8
  #EXT-X-STREAM-INF:BANDWIDTH=1050000,RESOLUTION=854x480,CODECS="avc1.4d400d"
  480p/playlist.m3u8
  #EXT-X-STREAM-INF:BANDWIDTH=3000000,RESOLUTION=1280x720,CODECS="avc1.64001f"
  720p/playlist.m3u8
  #EXT-X-STREAM-INF:BANDWIDTH=6000000,RESOLUTION=1920x1080,CODECS="hvc1.1.6"
  1080p-h265/playlist.m3u8

Client ABR algorithm:
  State: { currentResolution, bufferSeconds, measuredBandwidth }

  Every segment download:
    measuredBandwidth = segmentSize / downloadTime
    bufferSeconds += segmentDuration

  Decision logic:
    if bufferSeconds > 30 and measuredBandwidth > currentResolution.bitrate × 1.3:
      upgrade resolution (one step at a time)
    elif bufferSeconds < 10 or measuredBandwidth < currentResolution.bitrate × 0.8:
      downgrade resolution immediately
    else:
      maintain current resolution

  Buffer thresholds:
    > 30s: aggressive — upgrade quality
    10-30s: stable — maintain
    < 10s: defensive — downgrade to preserve playback continuity
    < 3s:  critical — pause and show buffering spinner

  Startup heuristic:
    Begin at lowest bitrate (safe start)
    After 3 segments: if bandwidth is clearly higher, jump up 2 levels
    "Aggressive startup" mode: reduces time to HD but risks rebuffer if overestimated

DASH (Dynamic Adaptive Streaming over HTTP):
  Similar to HLS but uses .mpd manifest (XML-based)
  Better supported on Android/Smart TVs; HLS preferred on Apple devices
  Netflix generates both from same source segments
```

---

## Deep Dive 3: CDN — Netflix Open Connect

```
Architecture:
  Netflix owns and operates its own CDN called Open Connect.
  Hardware appliances (OCAs - Open Connect Appliances) placed:
    - Directly in ISP data centers (~1,000+ ISP partners globally)
    - Netflix's own PoPs (Points of Presence) in major cities

  Embedded OCA in ISP:
    - ISP receives Netflix hardware at no cost
    - Hardware: 250TB storage, 100Gbps uplink
    - Traffic stays within ISP network → no ISP transit cost → ISP benefits too
    - Client DNS resolves to OCA within their ISP → latency < 5ms

  OCA storage hierarchy:
    Tier 1 (ISP embedded): ~200 most popular titles for that region
    Tier 2 (Netflix PoP):  ~1,000 most popular titles globally
    Tier 3 (S3 origin):    Full library of 36,000 hours

Traffic flow:
  1. Client starts playback → requests master manifest
  2. Netflix Playback Service → returns manifest URL pointing to nearest OCA
  3. Client fetches segments from OCA
     OCA hit: < 5ms, no origin load
     OCA miss: OCA → Netflix PoP → Origin S3 → cache at OCA for next viewers

CDN pre-warming:
  Nightly batch job analyzes predicted next-day popularity per region:
    - New episode releases (scheduled)
    - Trending in region (watch history signals)
    - Time zone rollover patterns (10PM spike prediction)
  Pushes content to OCAs in advance of demand spike
  Pre-warm completes 1-2 hours before predicted peak

Why Netflix built its own CDN:
  Third-party CDNs charge per GB — at 75 Tbps Netflix's costs would be $B/year
  Own CDN + ISP partnerships eliminated most transit costs
  Control over cache hierarchy allows per-title pre-warming that third-party CDNs cannot do
```

---

## Deep Dive 4: Recommendation Engine

```
Two-stage pipeline (same pattern as YouTube):

Stage 1: Candidate Generation (offline, runs hourly)
  Model: Two-Tower Neural Network
    User tower: embeds userId + watch history + demographics → user_vector (256-dim)
    Item tower: embeds titleId + metadata + genre → item_vector (256-dim)
    Score = cosine_similarity(user_vector, item_vector)
  Output: top-500 candidate titles per user
  Storage: Cassandra recommendations table (user_id → [title_ids])
  Training: daily Spark job on full watch history + implicit signals

Stage 2: Ranking (real-time, < 50ms)
  Re-ranks top-500 candidates using more features:
    - Current session context (device type, time of day, day of week)
    - Recent watch history (last 5 titles watched)
    - Thumbnail CTR for this user's demographic
    - Title recency (new releases boosted)
  Online feature store (Redis): current session features
  Ranking model: LightGBM gradient boosting (fast inference < 10ms)

Signals collected:
  Strong signals: completed watch (watched > 70%), explicit rating, add to list
  Weak signals:   partial watch (< 30%), browse then skip, trailer view
  Negative:       thumbs down, removed from continue watching

Cold-start problem (new user, no history):
  Ask user to pick 3+ genres/titles they like during onboarding
  Use demographic defaults + regional popularity until 10+ watches
  Genre-based recommendations from day 1

A/B testing thumbnails:
  Each title has 5-10 thumbnail variants
  Assign variant by: hash(userId + titleId) % numVariants
  Track CTR per variant per demographic cohort
  Auto-promote winning thumbnail when P-value < 0.05 (statistical significance)
  Winning variant stored in title_thumbnails table with metadata
```

---

## Deep Dive 5: Playback Session and DRM

```
Playback start sequence:
  1. Client: GET /api/playback/{titleId}
  2. Playback Service:
     a. Verify subscription is active (entitlement check)
     b. Determine optimal OCA for this client (geolocation + OCA health)
     c. Select supported codecs and max resolution for device
     d. Generate manifest URL (signed, expires in 24h)
     e. Issue DRM license request token
     Response: { manifestUrl, licenseUrl, licenseToken }

  3. Client fetches manifest from CDN
  4. Client requests license: POST {licenseUrl} with DRM challenge + licenseToken
  5. License Service decrypts CEK, wraps in DRM license, returns to client
  6. Client's DRM module stores license in secure enclave
  7. Playback begins — segments decrypted in hardware (DRM L1)

License renewal:
  License valid for 24h for streaming; 30 days for offline downloads
  Client renews license silently in background before expiry
  If renewal fails (subscription cancelled) → playback stops at segment boundary

Offline download:
  User downloads encrypted segments to device storage
  License issued with offline TTL (7 days watch window, 30 days to start)
  After TTL: license expires, playback fails, user must re-download
  Segments on disk remain but are unplayable without valid license
```

---

## Data Models

### Video Metadata (Cassandra)

```
CREATE TABLE titles (
    title_id        BIGINT PRIMARY KEY,
    title           TEXT,
    type            TEXT,        -- MOVIE, SERIES, DOCUMENTARY
    description     TEXT,
    duration_sec    INT,
    genres          LIST<TEXT>,
    cast_members    LIST<TEXT>,
    directors       LIST<TEXT>,
    release_year    INT,
    rating          TEXT,        -- PG, PG-13, R, TV-MA
    available_res   LIST<TEXT>,  -- ['240p','480p','720p','1080p']
    available_audio LIST<TEXT>,  -- ['en','es','fr']
    subtitle_langs  LIST<TEXT>,
    thumbnail_url   TEXT,
    imdb_rating     FLOAT,
    created_at      TIMESTAMP
);
```

### Watch History (Cassandra)

```
CREATE TABLE watch_history (
    user_id          BIGINT,
    watched_at       TIMESTAMP,
    title_id         BIGINT,
    episode_id       BIGINT,
    progress_sec     INT,
    completed        BOOLEAN,
    device_type      TEXT,
    PRIMARY KEY (user_id, watched_at)
) WITH CLUSTERING ORDER BY (watched_at DESC)
  AND default_time_to_live = 31536000;  -- 1 year

-- Resume playback key (Redis, fast lookup):
Key: progress:{userId}:{titleId}
Value: { positionSec: 2400, episodeId: 3, updatedAt }
TTL: 365 days
```

### Billing (PostgreSQL)

```sql
CREATE TABLE subscriptions (
    id              BIGINT PRIMARY KEY,
    user_id         BIGINT NOT NULL UNIQUE,
    plan            VARCHAR(20),    -- BASIC, STANDARD, PREMIUM
    status          VARCHAR(20),    -- ACTIVE, CANCELLED, PAST_DUE
    current_period_start TIMESTAMP,
    current_period_end   TIMESTAMP,
    payment_method_id    VARCHAR(100),
    max_streams          INT,        -- 1, 2, or 4 based on plan
    created_at      TIMESTAMP DEFAULT NOW()
);
```

---

## Billing and Plan Enforcement

```
Plan limits:
  BASIC (1 screen, 480p max):   $6.99/month
  STANDARD (2 screens, 1080p):  $15.49/month
  PREMIUM (4 screens, 4K+HDR):  $22.99/month

Concurrent stream enforcement:
  On playback start:
    INCR streams:active:{userId}  → current count
    EXPIRE streams:active:{userId} 120 (heartbeat-based TTL)
    If count > plan.maxStreams → return 403 "Too many screens"

  Client sends heartbeat every 60s:
    POST /api/playback/heartbeat { sessionId }
    → EXPIRE streams:active:{userId} 120 (reset TTL)

  On playback end (or session abandoned):
    DECR streams:active:{userId}
    Heartbeat stops → key expires naturally in 120s

Monthly billing (Stripe):
  Cron job runs on subscription renewal date
  Stripe charges stored payment method
  On failure: retry 3× over 7 days → downgrade to CANCELLED → notify user
```

---

## Edge Cases

| Edge Case | Solution |
|-----------|---------|
| New release causes thundering herd (everyone streams at 12AM) | OCA pre-warmed 1h before; staggered DNS routing to spread across OCAs; rate limit license requests to 10K/sec |
| User starts on phone, switches to TV mid-episode | Progress stored in Redis (30s sync); TV fetches progress on start → "Continue from 41:22?" |
| OCA in ISP goes offline | DNS health check removes OCA from rotation; clients routed to Netflix PoP transparently |
| DRM license server outage | Cache licenses in client for 24h; queued re-requests on recovery; existing streams unaffected |
| Copyright region restriction (title unavailable in country) | Geo-IP check at Playback Service; manifest never issued; client shows "Not available in your region" |
| User shares account (concurrent streams from different regions) | Concurrent stream count enforced per plan; location anomaly detection for account security |
| Network speed drops from 20 Mbps to 0.5 Mbps mid-stream | ABR switches to 360p within 2 segment cycles (8-12 seconds); buffer prevents any visible interruption |
| AV1 device support detection | Client sends supported codecs in User-Agent or playback request; server selects best codec per device |

---

## Interview Deep-Dive Questions

**Q: How does Netflix prevent rebuffering during peak hours?**
A: Three mechanisms work together. (1) ABR client-side: the player maintains a 30-second buffer and starts downgrading resolution when buffer drops below 10 seconds, before rebuffering would occur. (2) CDN pre-warming: the most popular content is already on OCAs inside ISPs before demand peaks, so origin load is near zero. (3) Multi-CDN fallback: if one CDN PoP is slow, the player can switch CDN origin mid-stream by refreshing the manifest — segments are stateless and interchangeable.

**Q: Why does Netflix use Cassandra instead of MySQL for watch history?**
A: Watch history is append-only (write once, never update), write-heavy (every 30 seconds per active user = millions of writes/second at peak), and read by user_id in descending time order — a perfect fit for Cassandra's partition key on user_id and clustering on timestamp DESC. MySQL would need aggressive sharding at this scale, and a hot user row updating every 30 seconds creates write contention on InnoDB row locks. Cassandra's log-structured merge tree is optimized for exactly this pattern: high-throughput sequential writes per partition.

**Q: Explain how Netflix per-title encoding saves storage and bandwidth.**
A: Standard encoding uses fixed presets (e.g., CRF 23 for all content). Per-title encoding runs multiple encodes at different bitrates and selects the optimal CRF for each title's complexity. A simple animated show needs much lower bitrate than a fast-action thriller to achieve the same visual quality. Netflix reports 20% average bitrate reduction from per-title encoding. At 75 Tbps peak, 20% savings = 15 Tbps × $0.01/GB = millions of dollars saved monthly in CDN costs.

**Q: How does A/B testing for thumbnails work at scale?**
A: Each title has 5-10 thumbnail variants uploaded during content processing. Assignment is deterministic: variant = hash(userId + titleId) % numVariants. This ensures the same user always sees the same thumbnail (consistent experience) while distributing across variants globally. Click-through rate is tracked per variant per user demographic (age, gender, watch history cluster). When a variant achieves statistical significance (P < 0.05 with min 10K impressions), it is promoted as the winner. The entire pipeline runs on streaming data in Flink — no batch job delay.

**Q: How would you design the "Continue Watching" row to work across devices?**
A: Progress is written to Redis (key: progress:{userId}:{titleId}, updated every 30 seconds during playback) and asynchronously persisted to Cassandra watch_history. On app open, the home screen calls GET /api/users/me/continue-watching which reads from Redis for recent sessions (hot cache, < 5ms) and falls back to Cassandra for older progress. The Continue Watching row is sorted by last_watched_at descending. Cross-device works because both the phone and TV write to the same Redis key by userId + titleId — the last writer wins, which is exactly the desired behavior.
