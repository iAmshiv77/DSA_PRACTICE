# HLD: Notification System

## Requirements

**Functional:**
- Send notifications across multiple channels: push (iOS/Android), SMS, email, in-app
- Support notification types: transactional (OTP, payment), marketing (campaigns), system alerts
- Fan-out: send to millions of users for a single broadcast event
- Template-based rendering with personalization and localization
- Delivery tracking: sent, delivered, read
- User preferences: opt-out per channel or notification type
- Retry on delivery failure with exponential backoff
- Do-not-disturb scheduling (no marketing notifications outside user's allowed hours)

**Non-Functional:**
- 1B notifications/day across all channels
- Transactional notifications (OTP, payment): < 5 second delivery
- Marketing notifications: deliver within 1 hour of send time
- At-least-once delivery guarantee
- Deduplication: never send the same notification twice
- Rate limiting: respect per-provider limits and per-user limits

---

## Capacity Estimation

```
Total volume:
  1B notifications/day = ~11,600/sec average
  Peak (morning campaigns): 10× = 116,000/sec

Channel breakdown (estimated):
  Push (FCM/APNs): 600M/day  = 6,900/sec
  Email:           300M/day  = 3,500/sec
  SMS:              80M/day  =   925/sec
  In-app:           20M/day  =   230/sec

Storage:
  Notification log: each record ~300 bytes
  1B × 300B = 300 GB/day (retain 90 days via Cassandra TTL = 27 TB)

Third-party rate limits (to plan against):
  FCM:            ~1000 msgs/sec per project key (use multiple keys)
  Twilio SMS:     ~1000 msg/sec per account
  SendGrid email: ~10M/day per account
  APNs:           connection-based; scale via HTTP/2 connection pool
```

---

## High-Level Architecture

```
                   ┌───────────────────────────────────────────────────┐
                   │             Notification API                       │
                   │  POST /notify  (internal — called by other svcs)  │
                   └─────────────────────┬─────────────────────────────┘
                                         │
                          ┌──────────────▼──────────────┐
                          │     Notification Service     │
                          │  - validate & enrich request │
                          │  - check user preferences    │
                          │  - deduplicate (Redis)       │
                          │  - route to correct queue    │
                          └──────────────┬───────────────┘
                                         │
          ┌──────────────────────────────┼──────────────────────────────┐
          ▼                              ▼                              ▼
┌─────────────────┐           ┌──────────────────┐           ┌──────────────────┐
│ Kafka:           │           │ Kafka:           │           │ Kafka:           │
│ notifs.critical  │           │ notifs.standard  │           │ notifs.marketing │
│ (OTP, payment)   │           │ (order, social)  │           │ (campaigns)      │
│ 50 consumers     │           │ 20 consumers     │           │ 5 consumers      │
└─────────┬────────┘           └────────┬─────────┘           └─────────┬────────┘
          │                             │                               │
          └─────────────────────────────┴───────────────────────────────┘
                                         │
                          ┌──────────────▼──────────────┐
                          │     Channel Dispatcher       │
                          │   (routes to channel worker) │
                          └──────┬──────────────────┬────┘
                                 │                  │
          ┌──────────────────────┼──────────┬───────┘
          ▼                      ▼          ▼                       ▼
  ┌──────────────┐    ┌──────────────┐  ┌──────────────┐   ┌──────────────┐
  │ Push Worker  │    │ Email Worker │  │  SMS Worker  │   │ In-App       │
  │ (FCM/APNs)   │    │ (SendGrid/   │  │ (Twilio/     │   │ Worker       │
  └──────┬───────┘    │  AWS SES)    │  │  MSG91)      │   │ (WebSocket + │
         │            └──────┬───────┘  └──────┬───────┘   │  Cassandra)  │
         │                   │                  │           └──────────────┘
         └───────────────────┴──────────────────┘
                              │
               ┌──────────────▼──────────────┐
               │  Delivery Tracker            │
               │  (Cassandra notification log)│
               └─────────────────────────────┘
```

---

## Deep Dive 1: Notification Types and Priority

```
Priority CRITICAL (Kafka consumers: 50, SLA: < 5s):
  - OTP / verification codes
  - Payment confirmation / failure
  - Account security alerts (new login, password change)

Priority HIGH (Kafka consumers: 20, SLA: < 30s):
  - Order shipped / delivered
  - Ride status changes (driver arrived, trip started)
  - Live event alerts

Priority NORMAL (Kafka consumers: 10, SLA: < 5 min):
  - Social interactions (likes, follows, comments)
  - System maintenance notices

Priority LOW (Kafka consumers: 5, SLA: < 1 hour):
  - Marketing campaigns
  - Weekly digest emails
  - Promotional push notifications

Why separate Kafka topics (not one topic with priority field)?
  A single topic with priority field does NOT work:
  Kafka consumers read in offset order — you cannot skip low-priority messages.
  Separate topics → separate consumer groups → CRITICAL never waits behind LOW.
```

---

## Deep Dive 2: Fan-Out Architecture for Campaigns

```
Problem: send a marketing push notification to 100M users in < 1 hour.

Step 1: Campaign Service creates campaign record
  campaigns table: { campaignId, templateId, segmentId, scheduledAt, status }

Step 2: Segment materializer (offline batch job, runs 30 min before scheduled send)
  SELECT user_id FROM users WHERE <segment_criteria>
  Write batches of 1000 user_ids to Kafka topic "campaign.fanout"
  100M users → 100,000 Kafka messages (batches)

Step 3: Fan-out workers (200 parallel workers)
  Each worker per batch (1000 users):
    1. Fetch preferences for 1000 users in ONE DB query (IN clause)
    2. Filter opted-out users → reduces batch size
    3. Render personalized template for each remaining user
    4. Enqueue individual send jobs to channel queue (Kafka notifs.marketing)

Step 4: Channel workers send
  FCM batch API: up to 500 notifications per HTTP call
  200 SMS workers × 5 concurrent Twilio calls = 1000 SMS/sec
  SendGrid: up to 1000 recipients per /mail/send call

Throughput math:
  100M users ÷ (200 workers × 1000 users/batch) = 500 batches per worker
  Each batch: ~100ms processing → 50 seconds of fan-out
  Channel delivery adds 10-30 min (provider throughput)
  Total: well within 1-hour SLA

Campaign fan-out for social events (user with 10M followers posts):
  Producer publishes 1 event → fan-out service reads follower list in batches of 1000
  For online users: direct WebSocket push (< 1s)
  For offline users: write to notification_inbox in Cassandra
  Total fan-out to 10M followers: ~2 minutes
```

---

## Deep Dive 3: Template Rendering

```
Template storage (MySQL):
  templates: id, name, channel, subject, body_template, variables_schema, locale, version

Template body (Handlebars / Mustache):
  title:    "Hey {{first_name}}, your order is ready!"
  body:     "Order #{{order_id}} from {{store_name}} is ready for pickup."
  deep_link: "app://orders/{{order_id}}"

Variables schema (for validation before render):
  { "first_name": "string", "order_id": "string", "store_name": "string" }

Rendering at fan-out time:
  1. Fetch template by (templateId + user.preferred_locale)
  2. Compile template (Handlebars.compile) — cache compiled form in memory
  3. Merge with user + event data
  4. For email: run compiled HTML through CSS inliner (required for email clients)

Localization:
  templates_i18n: { template_id, locale, subject, body_template }
  Fallback chain: user locale → base language → 'en'
  Stored as MJML for email (transpiles to responsive HTML at render time)

A/B testing subject lines:
  template has variants array: [{ variantId, subject, weight }]
  Assign variant deterministically: hash(userId + campaignId) % 100 < weight
  Track open rate per variant → auto-select winner after 1000 samples
```

---

## Deep Dive 4: Deduplication

```
Problem: retries, fan-out bugs, and network failures can cause duplicate sends.

Idempotency key generation:
  key = SHA256(userId + notificationType + entityId + windowBucket)
  windowBucket = floor(unixTimestamp / windowSeconds)

Window seconds by type:
  OTP:              30s  (matches OTP cooldown)
  ORDER_SHIPPED:    3600s (1 notification per order per hour max)
  MARKETING:        86400s (one marketing push per day per campaign)

Redis check (atomic):
  SET dedup:{key} "1" NX EX {windowSeconds*2}

  NX = only set if key does not exist
  If SET returns nil → duplicate → drop silently and log
  If SET returns OK → first time → proceed with send

This prevents:
  - Same OTP SMS sent twice in quick succession
  - Double email if worker crashes after send but before ack
  - Duplicate campaign push if Kafka message re-delivered

Defense in depth:
  Also check at channel worker level using provider's message ID
  FCM: store messageId in Redis SET, reject duplicates
```

---

## Deep Dive 5: Rate Limiting Per User

```
Per-user rate limits (prevent spamming):
  Push:  max 10/hour, 50/day
  SMS:   max 3/hour, 10/day
  Email: max 5/hour, 20/day

Implementation (sliding window in Redis):
  Key:  rate:{userId}:{channel}:{hourBucket}
  Type: Integer (INCR + EXPIRE)

  Before enqueueing:
    current = INCR rate:{userId}:PUSH:{hourBucket}
    if current == 1: EXPIRE rate:{userId}:PUSH:{hourBucket} 3600
    if current > 10: drop with reason "USER_RATE_LIMITED"

Do-Not-Disturb:
  User stores DND preference: { start: "22:00", end: "08:00", timezone: "Asia/Kolkata" }
  Before sending marketing notification:
    localHour = currentUtcHour + timezoneOffset
    if localHour >= 22 or localHour < 8:
      ZADD scheduled_notifs {deliverAt_unix} {notifPayloadJson}
      (separate consumer processes scheduled notifications at the right time)

Provider-level rate limits:
  Global FCM limiter: Redis token bucket shared across all push workers
    replenish 100 tokens/sec, bucket max = 1000
    each FCM batch call (500 msgs) consumes 1 token
    if no token: BullMQ/Kafka backpressure (message stays in queue)
```

---

## Deep Dive 6: Delivery Tracking and Retry

```
Notification states:
  PENDING     → enqueued in Kafka
  DISPATCHED  → sent to third-party provider
  DELIVERED   → provider confirmed delivery (via webhook/callback)
  READ        → user opened / tapped notification
  FAILED      → all retry attempts exhausted

Retry strategy (exponential backoff):
  Attempt 1: immediate
  Attempt 2: 30 seconds
  Attempt 3: 5 minutes
  Attempt 4: 30 minutes
  Attempt 5: 2 hours
  After 5 → mark FAILED; publish to dead letter topic; alert ops if CRITICAL

Delivery confirmation webhooks:
  FCM:      returns messageId synchronously; delivery receipt via FCM Data message
  Twilio:   POST /webhooks/sms/delivery  { MessageSid, MessageStatus, To }
  SendGrid: POST /webhooks/email/events  [{ event: "delivered", sg_message_id }]
  APNs:     connection-level error response per device token

Webhook handler:
  POST /internal/webhooks/delivery
  Body: { provider, providerMessageId, status, timestamp }
  → UPDATE notifications SET status=?, delivered_at=? WHERE provider_message_id=?

FCM-specific error handling:
  INVALID_REGISTRATION → remove device token from DB
  NOT_REGISTERED       → user uninstalled app; soft-delete token
  SENDER_ID_MISMATCH   → app config error; alert dev team; do not retry
  INTERNAL_ERROR       → retry with backoff
```

---

## Data Models

### Notifications Log (Cassandra)

```cql
CREATE TABLE notifications (
    user_id         BIGINT,
    notification_id TIMEUUID,
    type            TEXT,
    channel         TEXT,
    title           TEXT,
    body            TEXT,
    deep_link       TEXT,
    status          TEXT,
    template_id     BIGINT,
    idempotency_key TEXT,
    metadata        TEXT,
    is_read         BOOLEAN,
    sent_at         TIMESTAMP,
    delivered_at    TIMESTAMP,
    read_at         TIMESTAMP,
    PRIMARY KEY (user_id, notification_id)
) WITH CLUSTERING ORDER BY (notification_id DESC)
  AND default_time_to_live = 7776000;  -- 90 days
```

### Device Tokens (PostgreSQL)

```sql
CREATE TABLE device_tokens (
    id          BIGSERIAL PRIMARY KEY,
    user_id     BIGINT NOT NULL,
    token       TEXT NOT NULL,
    platform    VARCHAR(10),      -- ios / android / web
    app_version TEXT,
    is_active   BOOLEAN DEFAULT TRUE,
    last_seen   TIMESTAMP,
    created_at  TIMESTAMP DEFAULT NOW()
);
CREATE INDEX idx_device_tokens_user ON device_tokens(user_id) WHERE is_active = TRUE;
CREATE UNIQUE INDEX idx_device_tokens_token ON device_tokens(token);
```

### User Preferences (PostgreSQL)

```sql
CREATE TABLE notification_preferences (
    user_id           BIGINT,
    channel           VARCHAR(20),
    notification_type VARCHAR(50),
    is_enabled        BOOLEAN DEFAULT TRUE,
    dnd_start         TIME,
    dnd_end           TIME,
    timezone          VARCHAR(50),
    updated_at        TIMESTAMP DEFAULT NOW(),
    PRIMARY KEY (user_id, channel, notification_type)
);
```

### Campaigns (MySQL)

```sql
CREATE TABLE campaigns (
    id              BIGINT PRIMARY KEY,
    name            VARCHAR(200),
    template_id     BIGINT,
    segment_query   TEXT,
    channel         VARCHAR(20),
    scheduled_at    TIMESTAMP,
    status          VARCHAR(20),    -- DRAFT, SCHEDULED, RUNNING, COMPLETED, FAILED
    target_count    BIGINT,
    sent_count      BIGINT DEFAULT 0,
    delivered_count BIGINT DEFAULT 0,
    opened_count    BIGINT DEFAULT 0,
    created_at      TIMESTAMP DEFAULT NOW()
);
```

---

## Third-Party Integration

```
FCM (Android/Web Push):
  POST https://fcm.googleapis.com/v1/projects/{id}/messages:send
  Batch: POST .../messages:batchSend  (up to 500 per call)
  Auth: OAuth2 Bearer token (rotate every 60 min, cache in Redis)

APNs (iOS Push):
  HTTP/2 to api.push.apple.com (maintain persistent connection pool)
  Headers: apns-priority: 10 (critical) / 5 (normal)
           apns-expiration: 0 (no store-and-forward) or unix timestamp
  Send per-device token; no native batch endpoint

Twilio (SMS):
  POST https://api.twilio.com/2010-04-01/Accounts/{SID}/Messages.json
  No batch — parallelize with 50 concurrent HTTP connections per worker

SendGrid (Email):
  POST https://api.sendgrid.com/v3/mail/send
  Up to 1000 personalizations (recipients) per call

Provider failover:
  Primary SMS: MSG91 → Fallback: Twilio
  Primary Email: SendGrid → Fallback: AWS SES
  Trigger: 3 consecutive 5xx responses → open circuit breaker for 60s
  Health check every 10s; resume after first successful probe
```

---

## Edge Cases

| Edge Case | Solution |
|-----------|---------|
| Device token expired | FCM returns InvalidRegistration; remove token; if user has other devices, try those |
| User uninstalls app | FCM returns Unregistered; soft-delete device_token; SMS fallback if critical |
| Campaign accidentally targets all users | Manual approval gate for campaigns > 1M users; dry-run mode shows estimated count before execute |
| High SMS bounce rate | E.164 normalization on ingestion; carrier lookup to detect invalid numbers; suppress after 3 failures |
| Provider outage (FCM down 10 min) | Messages queue in Kafka; circuit breaker opens; retry resumes when provider recovers |
| User opts out mid-campaign | Preference checked at fan-out AND at worker level (defense in depth) |
| Notification storm (system event triggers 10M alerts) | Burst protection at Notification Service: max 500k enqueues/min; excess delayed 1 min |
| Scheduled notifications for user in timezone DST transition | Convert and re-compute delivery time using IANA timezone library; never store raw UTC offset |

---

## Interview Deep-Dive Questions

**Q: How do you ensure an OTP SMS is delivered in under 5 seconds?**
A: OTP uses the CRITICAL Kafka topic with 50 dedicated consumers that have nothing else to compete with. Workers maintain a pre-warmed persistent HTTP connection pool to Twilio. The path from API call to Twilio API response is under 100ms. Twilio's P99 delivery to the carrier is 2-3 seconds. If Twilio returns an error, MSG91 is the hot standby — no cold start. Total P99 end-to-end is consistently under 5 seconds.

**Q: How would you prevent a bug from sending 100M duplicate emails in a campaign?**
A: Multiple layers: (1) idempotency keys with 24-hour dedup window block re-sends at the service level, (2) campaign status is a state machine — COMPLETED cannot transition back to RUNNING, (3) per-user daily email rate limit (20/day) caps damage even if dedup fails, (4) fan-out workers checkpoint progress to a campaign_batches table — restarts resume from last completed batch, not from zero, (5) manual approval gate for campaigns over 1M recipients requiring human sign-off.

**Q: How does the in-app notification inbox work at scale?**
A: Notifications are written to Cassandra partitioned by user_id with TIMEUUID clustering — a single-partition read fetches the 50 most recent in < 10ms. Unread count is a Redis counter (INCR on write, DECR on read). When user opens the app, a single GET /notifications?limit=50 fetches from Cassandra. Real-time delivery for online users goes through a WebSocket connection maintained by the in-app worker. The WebSocket server is horizontally scaled with sticky routing (user always connects to the same node, based on consistent hash of userId).

**Q: How do you handle a third-party provider (FCM) being down?**
A: Circuit breaker pattern: after 3 consecutive failures, the circuit opens for 60 seconds. During that window, messages accumulate in Kafka (they are not dropped — Kafka retains them for 7 days). After 60 seconds, the circuit goes half-open: one probe request is sent. Success closes the circuit and normal processing resumes. FCM messages have a time_to_live field — for marketing notifications this can be 4 hours, so messages delivered 10 minutes late are still valid.

**Q: How do you measure notification system health?**
A: Per-channel metrics: delivery rate (delivered/sent), open rate (opened/delivered), bounce rate, P50/P95/P99 latency. Alert thresholds: CRITICAL channel delivery rate below 95% triggers immediate page; queue depth for CRITICAL topic above 500 triggers scale-out. All events (sent, delivered, opened) flow through Kafka into ClickHouse for analytics queries (delivery funnel by campaign, by template, by channel).
