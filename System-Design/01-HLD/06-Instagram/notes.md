# HLD: Instagram

## Core Features (Scope)
- Upload photos/videos
- Follow/unfollow users
- News feed (posts from followed users)
- Like/comment
- Search users and hashtags
- Stories (24hr TTL)

## Scale Estimates
```
Users:          1B registered, 500M DAU
Posts/day:      100M new photos, 10M videos
Feed reads:     5B/day (10x writes) → read-heavy
Storage:        100M photos × 200KB avg = 20TB/day
                Retain 5 years = 36PB total
```

## High-Level Architecture

```
Client → CDN (static assets, images)
       → API Gateway → Load Balancer
                     ├── User Service
                     ├── Post Service
                     ├── Feed Service
                     ├── Search Service
                     └── Notification Service
                           ↓
              PostgreSQL (users, follows)
              Cassandra   (posts, timelines)
              Redis       (feed cache, sessions, likes counter)
              S3/Blob     (photos, videos)
              Elasticsearch (search)
              Kafka       (async fan-out, notifications)
```

## Data Model

### User Service (PostgreSQL)
```sql
users (
  id          BIGSERIAL PRIMARY KEY,
  username    VARCHAR(30) UNIQUE NOT NULL,
  email       VARCHAR(255) UNIQUE,
  bio         TEXT,
  avatar_url  TEXT,
  is_private  BOOLEAN DEFAULT false,
  follower_count  INT DEFAULT 0,  -- denormalized counter
  following_count INT DEFAULT 0,
  created_at  TIMESTAMP
);

follows (
  follower_id BIGINT REFERENCES users(id),
  followee_id BIGINT REFERENCES users(id),
  created_at  TIMESTAMP DEFAULT NOW(),
  PRIMARY KEY (follower_id, followee_id)
);
CREATE INDEX ON follows(followee_id);  -- "who follows me"
```

### Post Service (Cassandra — write-heavy, time-series)
```cql
-- User's own posts (profile page)
CREATE TABLE posts_by_user (
  user_id    BIGINT,
  post_id    TIMEUUID,   -- time-ordered UUID
  image_url  TEXT,
  caption    TEXT,
  like_count COUNTER,
  PRIMARY KEY (user_id, post_id)
) WITH CLUSTERING ORDER BY (post_id DESC);

-- Global hashtag posts
CREATE TABLE posts_by_hashtag (
  hashtag    TEXT,
  post_id    TIMEUUID,
  user_id    BIGINT,
  image_url  TEXT,
  PRIMARY KEY (hashtag, post_id)
) WITH CLUSTERING ORDER BY (post_id DESC);
```

### Stories (Redis with TTL)
```
Key: story:{userId}
Type: Sorted Set (score = expiry timestamp)
Members: {storyId}:{imageUrl}

On upload: ZADD story:{userId} {now+86400} {storyId}
On read:   ZRANGEBYSCORE story:{userId} {now} +inf
Cleanup:   ZREMRANGEBYSCORE story:{userId} 0 {now}  (background job)
```

## Feed Architecture: Fan-Out on Write (Push Model)

```
User A (50 followers) uploads photo
         ↓
Post saved to Cassandra
         ↓ (Kafka event: new_post)
Feed Worker consumes event
         ↓
For each of A's 50 followers:
  LPUSH timeline:{followerId} {postId}
  LTRIM timeline:{followerId} 0 999  (keep last 1000)

Feed read: LRANGE timeline:{userId} 0 49  → 50 post IDs
           Batch fetch post details from Cassandra
```

### Handling Celebrities (Hybrid Fan-Out)
```
Problem: Celebrity with 10M followers — fan-out takes 10M Redis writes!

Solution: Hybrid approach
  - Regular users (< 1M followers): fan-out on write
  - Celebrities (> 1M followers): fan-out on read

Feed Read for user X:
  1. Get pre-computed feed from Redis (from people X follows with < 1M followers)
  2. For each celebrity X follows: fetch their latest posts from Cassandra
  3. Merge + sort by time
  4. Return top 50
```

## Photo Upload Flow

```
1. Client → POST /upload → Pre-signed S3 URL generated
2. Client uploads directly to S3 (bypasses our server)
3. S3 triggers Lambda on upload complete
4. Lambda → sends message to Kafka "photo_uploaded"
5. Image Processing Service consumes:
   - Generate thumbnails (4 sizes: 150x150, 320x320, 640x640, 1080x1080)
   - Strip EXIF data (privacy — GPS coordinates)
   - Run NSFW detection (ML model)
   - Store all versions in S3
6. CDN invalidation for new content
7. Post record created in Cassandra with status: PUBLISHED
8. Fan-out to followers' feeds (Kafka consumer)
```

## Like System (High Write Throughput)
```
Naive: UPDATE posts SET like_count = like_count + 1 WHERE id = X
       → hot row problem for viral posts

Solution:
  1. Write to Redis: INCR likes:{postId}
     Store who liked: SADD likers:{postId} {userId}  (for "did I like this")
  2. Async batch flush to DB every 30s via background job
  3. Read from Redis (fast) for display
  4. Cap Redis set at 1000 users; use approximate HyperLogLog for total count
```

## Search

```
Elasticsearch indices:
  users:    username, display_name, bio (analyzed)
  hashtags: name, post_count (numeric)
  posts:    caption, hashtags[] (for content search)

Search query:
  GET /search?q=sunset&type=user,hashtag
  → Elasticsearch multi-index search
  → Results ranked by: exact match > prefix match > fuzzy
  → Users ranked by: follower_count × relevance_score
```

## CDN Strategy
```
Image delivery:
  Photo URL: https://cdn.instagram.com/{size}/{postId}.jpg
  CDN origin = S3 bucket
  Cache-Control: max-age=31536000 (1 year — immutable content, content-addressed)

Profile pictures:
  Cache-Control: max-age=86400 (1 day — can change)
  Bust cache by appending version: ?v={updatedAt}
```

## Interview Q&A

**Q: How do you ensure a user can't like a post twice?**
Use a Redis Set: `SADD likers:{postId} {userId}` returns 0 if already exists (idempotent).
Also store in a `likes` table (postId, userId, PRIMARY KEY) for persistence — duplicate insert fails with unique constraint.

**Q: How does the feed stay consistent when a user follows/unfollows someone?**
On **follow**: backfill the timeline by fetching the new followee's recent posts (last 20) and adding to follower's Redis list.
On **unfollow**: don't delete from timeline (too expensive). Posts already in feed stay, new posts won't be added (fan-out service checks follow status before writing). Over time the unfollowed person's posts naturally age out of the 1000-item limit.

**Q: How would you implement Instagram Stories with view tracking?**
```
Story data: Redis Sorted Set (story:{userId}) with TTL on the key itself
View tracking: Redis Set — viewed:{storyId}:{viewerId} → TTL 24hr
              HyperLogLog for approximate view count: PFADD views:{storyId} {userId}
Who viewed: Store in Cassandra story_views(story_id, viewer_id, viewed_at)
            Only return to story owner (privacy)
```

**Q: How would you handle the thundering herd on a viral post?**
- **CDN**: images served from CDN, not origin
- **Read-through cache**: post metadata cached in Redis (TTL 5min)
- **Mutex on cache miss**: only one process refetches, others wait (Redis `SET nx`)
- **Counter approximation**: don't update DB on every like — batch flush
- **Database read replicas**: route read queries to replicas
