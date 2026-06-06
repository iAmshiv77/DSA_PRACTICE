# HLD: Twitter / News Feed

## Requirements

**Functional:**
- Post a tweet (text, images, videos, links)
- Follow / unfollow users
- Home timeline: see tweets from people you follow
- User timeline: see tweets by a specific user
- Like, retweet, reply
- Search tweets

**Non-Functional:**
- 300M DAU, 600K timeline reads/sec (read-heavy)
- Post tweet latency < 200ms
- Timeline load < 500ms
- Eventually consistent timeline (OK to see tweet after a few seconds)
- High availability (99.99%)

---

## Capacity Estimation

```
Writes:
  - 100M tweets/day = 1200 writes/sec (peak ~3600/sec)
  - Each tweet: 300 bytes avg (text) + optional media

Reads:
  - 300M DAU × 20 timeline loads/day = 6B timeline reads/day
  - 6B / 86400 = ~70K reads/sec (peak ~200K/sec)

Storage:
  - Tweets: 1200/sec × 86400 × 365 × 5 × 300B ≈ 5.7 TB/year
  - Media: 1200/sec × 20% have media × 200KB = 48 GB/day
  - User relationships (follows): 300M users × 200 follows × 8B = 480 GB
```

---

## High-Level Architecture

```
                    ┌─────────────────────────────────────┐
                    │         API Gateway                  │
                    │   (auth, rate limit, routing)        │
                    └──────┬───────────┬──────────────────┘
                           │           │
              ┌────────────▼──┐    ┌───▼─────────────┐
              │  Tweet Service │    │  Timeline Service │
              │  (write path)  │    │  (read path)      │
              └────────┬──────┘    └────────┬──────────┘
                       │                    │
          ┌────────────▼───────┐    ┌───────▼──────────┐
          │  Fan-out Service   │    │   Redis Cache      │
          │  (async, Kafka)    │    │  (timeline cache)  │
          └─────────┬──────────┘    └──────────────────┘
                    │
    ┌───────────────┼───────────────┐
    ▼               ▼               ▼
┌───────┐    ┌──────────┐    ┌──────────┐
│Tweet  │    │User Graph│    │Timeline  │
│Store  │    │(follows) │    │Store     │
│(Cass) │    │(MySQL)   │    │(Redis/   │
└───────┘    └──────────┘    │ Cassand) │
                              └──────────┘
```

---

## Core Problem: Timeline Generation

### The Fan-Out Problem
When a user with 10M followers posts a tweet, how do you show it in all 10M followers' timelines?

### Strategy: Fan-Out on Write (Push Model)
```
User posts tweet
    → Write to tweet store
    → Publish tweet_id to Kafka topic "new_tweets"
    → Fan-out workers consume → fetch follower list → write tweet_id to each follower's timeline cache

Read timeline:
    → Fetch timeline cache (pre-built list of tweet_ids)
    → Fetch tweet details in batch

Pros: Fast reads (O(1) cache lookup)
Cons: High write amplification (1 tweet × 1M followers = 1M writes)
```

### Strategy: Fan-Out on Read (Pull Model)
```
User posts tweet → write to own tweet store only

Read timeline:
    → Fetch list of who user follows
    → Query each person's tweet store (last 20 tweets each)
    → Merge and sort

Pros: No write amplification
Cons: Slow reads (O(N) merge where N = number following)
```

### Twitter's Hybrid Approach (Industry Standard)

```
Normal users (< 1000 followers) → Fan-out on WRITE
                                   Push tweet_id to all follower timelines

Celebrities (> 1M followers)    → Fan-out on READ
                                   Store tweet in celebrity post store only

Timeline read:
    1. Fetch pre-built timeline cache (contains non-celebrity tweet_ids)
    2. Identify followed celebrities
    3. Fetch recent celebrity tweets
    4. Merge + sort all tweet_ids by timestamp
    5. Batch-fetch full tweet objects from tweet store
    6. Return to client
```

---

## Data Models

### Tweet Table (Cassandra — write-optimized)
```
tweet_id       BIGINT PRIMARY KEY  (Snowflake ID — contains timestamp)
user_id        BIGINT
content        TEXT
media_urls     LIST<TEXT>
created_at     TIMESTAMP
likes_count    COUNTER
retweet_count  COUNTER
reply_count    COUNTER
is_deleted     BOOLEAN
```

### User Follow Graph (MySQL)
```sql
CREATE TABLE follows (
    follower_id BIGINT,
    followee_id BIGINT,
    created_at  TIMESTAMP,
    PRIMARY KEY (follower_id, followee_id),
    INDEX (followee_id)  -- for "who follows this person"
);
```

### Timeline Cache (Redis)
```
Key:   timeline:{user_id}
Type:  Sorted Set
Score: timestamp (unix ms)
Value: tweet_id

# Max 1000 tweet_ids in timeline cache
# Older than 7 days → evict from cache, paginate from DB
```

---

## Deep Dive: The Timeline Cache

### Data Structure Choice
```
Redis Sorted Set: ZADD timeline:123 1700000000 "tweet_456"
                  ZRANGE timeline:123 0 19 WITHSCORES  (latest 20)

Why sorted set? → ordered by score (timestamp), O(log N) insert/remove
```

### Timeline Cache Write (Fan-Out Worker)
```python
def fan_out(tweet_id, author_id, timestamp):
    follower_ids = get_followers(author_id)  # from DB, cached
    pipeline = redis.pipeline()
    for follower_id in follower_ids:
        key = f"timeline:{follower_id}"
        pipeline.zadd(key, {tweet_id: timestamp})
        pipeline.zremrangebyrank(key, 0, -1001)  # keep only 1000
    pipeline.execute()
```

### Cache Miss Handling
```
Timeline cache cold or evicted → rebuild from DB:
    1. Fetch follows from user_follows table
    2. Query tweet store for each followee's recent tweets
    3. Merge + populate Redis sorted set
    4. Serve from cache going forward
```

---

## Search (Bonus Deep Dive)

```
Tweet search → Elasticsearch
- Index tweets on write (async via Kafka)
- Index fields: content, hashtags, user_id, created_at, location
- Full-text search with BM25 ranking
- Filter by recency (last 7 days = hot index, older = cold index)

Trending hashtags → count-min sketch in Redis
- HINCRBY hashtag:{period} {hashtag} 1
- Get top-K: sorted set of (count, hashtag)
```

---

## Edge Cases

| Edge Case | Solution |
|-----------|---------|
| Celebrity with 100M followers | Fan-out on read, not write |
| User deletes tweet | Soft delete + async eviction from all timeline caches |
| User posts 100 tweets rapidly | Rate limiting at API gateway (10 tweets/min) |
| Timeline longer than 1000 items | Paginate: after tweet_id param, query DB for older |
| Follower count changes (new follow) | Backfill new followee's recent 20 tweets into timeline |
| User unfollows → stale tweets in timeline | Remove their tweet_ids from timeline on unfollow |
| Duplicate timeline entries | Sorted set auto-deduplicates by value (tweet_id) |
| Network partition during fan-out | Kafka provides at-least-once; workers are idempotent (ZADD is idempotent) |

---

## Interview Questions

**Q: Why Cassandra for tweet storage?**
A: Cassandra is a wide-column store optimized for high-throughput writes. Tweets are append-only (write once, read many). Cassandra's partitioning by user_id allows efficient user timeline queries (get all tweets by user in time order). No joins needed.

**Q: What happens if the fan-out service falls behind (Kafka lag)?**
A: Timeline shows slightly stale content — acceptable given eventual consistency requirement. Consumer group auto-scales (add more fan-out workers). Kafka retains messages for 7 days, so nothing is lost.

**Q: How do you handle the "new user" cold start problem?**
A: New user follows 0 people → empty timeline. Show trending content, suggested follows, topic-based feed until they follow enough people.

**Q: How would you implement "Trending Topics"?**
A: Sliding window count on hashtags: use Redis ZINCRBY on a sorted set per time window (1-min, 1-hour). Take top-K using ZREVRANGE. Aggregate across data centers with gossip protocol.

**Q: How does Snowflake ID help with timeline sorting?**
A: Snowflake ID embeds timestamp in high bits → sorting by tweet_id ≈ sorting by time. No separate timestamp index needed for chronological ordering.
