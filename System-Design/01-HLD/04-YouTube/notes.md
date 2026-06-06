# HLD: YouTube (Video Upload & Streaming Platform)

## Requirements

**Functional:**
- Upload video (any format, up to 50 GB)
- Stream video at multiple resolutions (360p, 480p, 720p, 1080p, 4K)
- Search videos by title, description, tags
- Like / dislike / comment on videos
- Subscribe to channels
- View count, recommendations

**Non-Functional:**
- 500 hours of video uploaded per minute
- 1B daily active users, 1B hours watched per day
- Upload latency acceptable (async transcoding)
- Streaming latency < 200ms to first byte
- High availability (99.99%)
- Global CDN delivery

---

## Capacity Estimation

```
Uploads:
  - 500 hours/min = 8.3 hours/sec of raw video
  - Each 1 hour raw video ≈ 10 GB (uncompressed)
  - 8.3 × 10 GB = 83 GB/sec ingested raw (before transcoding)
  - After transcoding to 5 resolutions ≈ 5× storage
  - Storage: 500h/min × 60min × 24h × 365 × 10GB × 5 variants
            ≈ 1.3 PB/day raw + transcoded

Reads (Streaming):
  - 1B hours/day = 11.5M hours/sec
  - 1M concurrent viewers at ~2 Mbps avg = 2 Tbps egress bandwidth
  - Peak: 2× = 4 Tbps (handled by CDN, not origin)

Views per video:
  - Average video: 10 minutes = 600 seconds
  - 1B hours/day = 6T seconds of watch time
  - If 800M videos exist → avg ~7500 sec/video/day viewed

Comments:
  - 100M comments/day = ~1200/sec
  - Each comment: ~500 bytes → 50 GB/day
```

---

## High-Level Architecture

```
                         ┌───────────────────────────────────────────────────┐
                         │                  API Gateway                       │
                         │         (auth, rate limit, routing)                │
                         └──────┬───────────────┬──────────────┬─────────────┘
                                │               │              │
                    ┌───────────▼───┐   ┌───────▼──────┐  ┌───▼──────────────┐
                    │  Upload       │   │  Streaming   │  │  Metadata /       │
                    │  Service      │   │  Service     │  │  Search Service   │
                    └───────┬───────┘   └──────┬───────┘  └──────┬────────────┘
                            │                  │                  │
             ┌──────────────▼──┐     ┌─────────▼──────┐  ┌───────▼───────────┐
             │  Object Store   │     │  CDN Network   │  │  MySQL (metadata) │
             │  (raw videos)   │     │  (video chunks)│  │  Cassandra (cmts) │
             │  e.g. S3        │     │  e.g. Akamai   │  │  Elasticsearch    │
             └──────────────┬──┘     └────────────────┘  └───────────────────┘
                            │
              ┌─────────────▼──────────────┐
              │  Transcoding Pipeline       │
              │  (Kafka → Worker Fleet)     │
              │  FFmpeg / cloud encoders    │
              └────────────────────────────┘
```

---

## Deep Dive 1: Video Upload Flow

```
Step 1: Client requests upload URL
  Client → Upload Service → generates pre-signed S3 URL
  Response: { uploadId, uploadUrl, chunkSize: 5MB }

Step 2: Chunked multipart upload (client-side)
  Client splits file into 5MB chunks
  For each chunk:
    PUT {uploadUrl}/part/{chunkNumber}
    Response: { ETag }
  Final call: CompleteMultipartUpload({ uploadId, parts: [{ETag}] })

Step 3: Upload service notifies transcoding pipeline
  S3 triggers event → SNS → Kafka topic "video.uploaded"
  Message: { videoId, rawS3Key, userId, title, duration }

Step 4: Transcoding workers consume from Kafka
  Each worker picks a job, runs FFmpeg:
    ffmpeg -i input.mp4 -vf scale=1280:720 -c:v h264 -crf 23 output_720p.mp4
  Produces: 360p, 480p, 720p, 1080p, 4K (if source allows)
  Also: thumbnail extraction at t=5s, t=30%, t=50%

Step 5: Transcoded chunks pushed to CDN origin store
  Each resolution stored as HLS segments (*.ts files + *.m3u8 playlist)
  S3 path: /videos/{videoId}/720p/segment_{n}.ts

Step 6: Metadata updated
  MySQL: UPDATE videos SET status='READY', available_resolutions='360,480,720,1080' WHERE id={videoId}
  Elasticsearch: index video document for search
```

### Chunked Upload State Machine

```
PENDING_UPLOAD
      |
      | (all chunks received, CompleteMultipartUpload)
      v
RAW_STORED
      |
      | (Kafka event consumed by transcoder)
      v
TRANSCODING
      |
      | (all resolutions complete)
      v
PROCESSING_THUMBNAILS
      |
      v
READY        ←── visible to viewers
      |
      | (owner deletes)
      v
DELETED (soft delete, retain for 30 days)
```

---

## Deep Dive 2: Transcoding Pipeline

```
Architecture:
  Kafka topic "video.transcoding.jobs"
    - Partitioned by videoId (ensures ordering per video)

  Worker fleet (auto-scaling EC2 / k8s):
    - Pull job from Kafka
    - Download raw from S3
    - Run FFmpeg for ONE resolution (parallelism at job level)
    - Upload output chunks to S3
    - Publish completion event to "video.transcoding.done"
    - Coordinator service tracks which resolutions are complete
    - When all resolutions done → update metadata to READY

Why one resolution per worker?
  - Independent failure isolation (720p failure does not block 360p)
  - Faster parallelism (5 workers per video = 5× faster)
  - Easier retry (only retry failed resolution)

FFmpeg commands by resolution:
  360p:  -vf scale=640:360  -b:v 800k  -b:a 128k
  480p:  -vf scale=854:480  -b:v 1500k -b:a 128k
  720p:  -vf scale=1280:720 -b:v 3000k -b:a 192k
  1080p: -vf scale=1920:1080 -b:v 6000k -b:a 192k
  4K:    -vf scale=3840:2160 -b:v 20000k -b:a 256k

Codecs:
  H.264 (AVC)  → widest device compatibility
  H.265 (HEVC) → 40% better compression, same quality
  VP9          → royalty-free, good for web (Chrome/Firefox)
  AV1          → newest, best compression, slower encode
```

---

## Deep Dive 3: CDN Video Delivery

```
Delivery Protocol: HLS (HTTP Live Streaming)
  Master playlist:   /videos/{id}/master.m3u8
    #EXTM3U
    #EXT-X-STREAM-INF:BANDWIDTH=800000,RESOLUTION=640x360
    360p/playlist.m3u8
    #EXT-X-STREAM-INF:BANDWIDTH=3000000,RESOLUTION=1280x720
    720p/playlist.m3u8

  Segment playlist:  /videos/{id}/720p/playlist.m3u8
    #EXTM3U
    #EXT-X-TARGETDURATION:6
    #EXTINF:6.0,
    segment_000.ts
    segment_001.ts
    ...

CDN Request Flow:
  Browser → CDN Edge (nearest PoP)
    HIT:  serve segment directly (< 5ms)
    MISS: CDN → Origin S3 → cache at edge (TTL: 24h for static segments)

Adaptive Bitrate (ABR):
  Client player monitors download speed every 3 seconds
  If bandwidth drops → switch to lower resolution playlist
  If bandwidth improves → switch to higher resolution
  Decision made by client (HLS.js, ExoPlayer, AVPlayer)

CDN Topology (YouTube Open Connect style):
  Tier 1: Global POPs (50 nodes)  — cache popular content (Zipf distribution)
  Tier 2: ISP embedded nodes      — cache within ISP network (< 1ms)
  Cache ratio: top 20% videos = 80% of traffic → cache hit rate ~90%+
```

---

## Data Models

### Videos Table (MySQL — metadata)

```sql
CREATE TABLE videos (
    id               BIGINT PRIMARY KEY,       -- Snowflake ID
    user_id          BIGINT NOT NULL,
    title            VARCHAR(200) NOT NULL,
    description      TEXT,
    status           ENUM('PENDING','TRANSCODING','READY','DELETED') DEFAULT 'PENDING',
    duration_sec     INT,
    thumbnail_url    VARCHAR(500),
    raw_s3_key       VARCHAR(500),
    resolutions      VARCHAR(100),             -- '360,480,720,1080'
    category_id      INT,
    tags             JSON,
    view_count       BIGINT DEFAULT 0,
    like_count       BIGINT DEFAULT 0,
    dislike_count    BIGINT DEFAULT 0,
    is_public        BOOLEAN DEFAULT TRUE,
    created_at       TIMESTAMP DEFAULT NOW(),
    updated_at       TIMESTAMP,
    deleted_at       TIMESTAMP,
    INDEX idx_user_id (user_id),
    INDEX idx_status (status),
    INDEX idx_created_at (created_at)
);
```

### Comments Table (Cassandra — high write volume)

```
CREATE TABLE comments (
    video_id    BIGINT,
    comment_id  TIMEUUID,     -- contains timestamp, enables time ordering
    user_id     BIGINT,
    content     TEXT,
    like_count  COUNTER,
    PRIMARY KEY (video_id, comment_id)
) WITH CLUSTERING ORDER BY (comment_id DESC);

-- Fetch latest 20 comments for a video:
SELECT * FROM comments WHERE video_id = ? LIMIT 20;
```

### Subscriptions (MySQL)

```sql
CREATE TABLE subscriptions (
    subscriber_id  BIGINT,
    channel_id     BIGINT,
    created_at     TIMESTAMP DEFAULT NOW(),
    PRIMARY KEY (subscriber_id, channel_id),
    INDEX (channel_id)
);
```

---

## View Count: Redis HyperLogLog

```
Problem: 1 video gets 10M views/day — incrementing a counter 10M times is fine,
         but "unique viewers" count requires deduplication.

HyperLogLog approach:
  On each view:
    PFADD views:unique:{videoId}:{date} {userId}   -- O(1), ~12KB memory regardless of cardinality
    INCR  views:total:{videoId}                    -- raw view count

  Every 5 minutes, async job:
    unique_count = PFCOUNT views:unique:{videoId}:{date}
    UPDATE videos SET view_count = view_count + (delta) WHERE id = {videoId}

HyperLogLog error rate: 0.81% — acceptable for "1.2M views" display

Why not exact count?
  Exact unique count requires storing every userId that viewed → terabytes of sets
  HyperLogLog uses ~12KB per video per day regardless of viewer count
```

---

## Like / Dislike System

```
Data model (MySQL):
  CREATE TABLE video_reactions (
      user_id   BIGINT,
      video_id  BIGINT,
      reaction  ENUM('LIKE','DISLIKE'),
      created_at TIMESTAMP DEFAULT NOW(),
      PRIMARY KEY (user_id, video_id)
  );

Like flow:
  1. POST /videos/{id}/like
  2. UPSERT into video_reactions (user_id, video_id, 'LIKE')
     - If previously DISLIKE → flip: decrement dislike_count, increment like_count
     - If previously LIKE → toggle off: decrement like_count, delete row
  3. Async update counts on videos table via Kafka event

Count display:
  - Cache in Redis: like_count:{videoId} with TTL 5min
  - On cache miss: SELECT COUNT from video_reactions
  - YouTube hides exact dislike count since Nov 2021 (by design choice)
```

---

## Search (Elasticsearch)

```
Index document (indexed on video READY):
{
  "videoId":     "123456",
  "title":       "How to bake sourdough bread",
  "description": "...",
  "tags":        ["bread", "baking", "sourdough"],
  "channelName": "BakingWithJohn",
  "viewCount":   14200,
  "likeCount":   980,
  "createdAt":   "2024-01-15T10:00:00Z",
  "language":    "en",
  "duration":    3640
}

Search query strategy:
  - Multi-match on title (boost: 3), tags (boost: 2), description (boost: 1)
  - Score = BM25 relevance × recency decay × viewCount weight
  - Filter: language, duration, upload date, category

Typeahead (Search-as-you-type):
  - Use Elasticsearch edge_ngram tokenizer on title field
  - Return top 5 suggestions ranked by view count
  - Cache in Redis with 10min TTL
```

---

## Recommendation System

```
Signals collected per user:
  - Watched videos (with watch percentage)
  - Liked / disliked / saved videos
  - Search history
  - Click-through on suggested videos
  - Channel subscriptions

Collaborative Filtering (Conceptual):
  Matrix: users × videos, value = implicit rating (watch% × recency)
  Find users similar to you (cosine similarity on watch history vectors)
  Recommend videos those similar users watched that you have not

In practice (YouTube's actual approach — DNN Two-Tower):
  Candidate Generation:
    - Embedding-based model: user embedding × video embedding → top 100 candidates
    - Trained on billions of (user, video, watched) triplets
    - Offline batch job every few hours

  Ranking:
    - Ranking model scores each of 100 candidates on predicted watch time
    - Features: video age, CTR on thumbnail, user-video similarity score
    - Real-time inference (< 50ms) on serving layer

  Serving:
    - Pre-computed recommendations stored in Redis per user
    - TTL: 1 hour (refresh in background)
    - On cache miss: fallback to trending + subscribed channels
```

---

## API Design

```
POST /api/v1/videos/upload/initiate
Body:   { filename, fileSize, title, description, tags, categoryId }
Resp:   { videoId, uploadId, uploadUrl, chunkSize }

PUT  /api/v1/videos/upload/{uploadId}/chunk/{n}   (direct to S3 presigned URL)
POST /api/v1/videos/upload/{uploadId}/complete
Body:   { parts: [{ partNumber, etag }] }

GET  /api/v1/videos/{videoId}
Resp:   { videoId, title, description, streamUrl, thumbnailUrl, viewCount, likeCount }

GET  /api/v1/videos/{videoId}/stream
Resp:   302 → CDN master.m3u8 URL

POST /api/v1/videos/{videoId}/like
POST /api/v1/videos/{videoId}/dislike

GET  /api/v1/search?q={query}&page=1&limit=20&filter[duration]=short
GET  /api/v1/recommendations?userId={id}&limit=20

POST /api/v1/videos/{videoId}/comments
GET  /api/v1/videos/{videoId}/comments?after={commentId}&limit=20
```

---

## Edge Cases

| Edge Case | Solution |
|-----------|---------|
| Upload interrupted mid-chunk | Multipart upload resumable; client retries from last successful chunk using saved uploadId |
| Transcoding worker crashes | Kafka consumer group rebalances; job reprocessed by another worker; idempotent via videoId + resolution key |
| Video too large (> 50 GB) | Validate at initiate step; return 413 before any upload starts |
| Copyright violation | Content ID fingerprinting (audio + video hashing) runs post-transcoding; auto-block or monetize |
| CDN cache stale after video deletion | Soft delete → CDN cache invalidation API called immediately for master.m3u8 |
| Comment spam flood | Rate limit: 10 comments/min per user; async profanity filter job |
| View count manipulation (bots) | HyperLogLog per device fingerprint + IP; anomaly detection on view velocity |
| Live streaming | Separate pipeline: RTMP ingest → low-latency HLS (2-second segments) → same CDN delivery |

---

## Interview Deep-Dive Questions

**Q: Why use HLS segments instead of a single MP4 file?**
A: HLS splits video into small chunks (4-6 seconds each). This enables: (1) adaptive bitrate switching between chunks without re-downloading, (2) fast seek (jump to any timestamp without downloading everything before it), (3) CDN caching at segment granularity (popular segments stay hot), (4) partial failure resilience (missing one segment triggers retry for just that segment).

**Q: Why is Cassandra chosen for comments, not MySQL?**
A: Comments are write-heavy (millions per hour) and read by video_id in time order. Cassandra's partition key on video_id means all comments for a video live on the same nodes — fast range scans. MySQL would require sharding for this write volume. Comments never need multi-table joins. Cassandra's eventual consistency is acceptable for comments (seeing a comment 1-2 seconds after it was posted is fine).

**Q: How would you handle a sudden viral video getting 10M concurrent views?**
A: The video's segments are already cached at CDN edges globally. The CDN absorbs nearly all traffic. Origin (S3) only serves CDN cache misses, which is a tiny fraction. If CDN edges under a specific ISP are overwhelmed, the CDN routes to nearest alternate edge. The stateless nature of HLS delivery means linear scaling with CDN nodes.

**Q: How do you ensure uploaded video is not CSAM or dangerous content?**
A: Three layers: (1) pre-upload hash check against known hash databases (PhotoDNA), (2) post-transcoding ML classifier runs on extracted frames, (3) human review queue for borderline cases flagged by classifier. Upload stays in TRANSCODING state until content check passes.

**Q: How does the recommendation system handle new videos with no engagement signals?**
A: Cold-start problem. New videos get a small exploration budget: shown to a random sample (0.01%) of users in category. If early engagement (CTR, watch%) is above threshold, promoted to broader audience. This is the "explore-exploit" tradeoff used in multi-armed bandit approaches.

**Q: If you had to reduce storage costs by 50%, what would you do?**
A: (1) Only store 4K source if upload was 4K — don't upscale, (2) Delete raw upload after transcoding is verified complete, (3) Use H.265 or AV1 instead of H.264 (40-50% smaller at same quality), (4) Tiered storage: videos with < 100 views/month move to S3 Glacier (10× cheaper), (5) Remove 4K/1080p versions for old videos with < 1000 lifetime views.
