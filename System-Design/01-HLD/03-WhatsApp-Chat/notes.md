# HLD: WhatsApp / Chat System

## Requirements

**Functional:**
- 1:1 real-time messaging
- Group chats (up to 256 members)
- Message status: sent ✓, delivered ✓✓, read ✓✓ (blue)
- Media sharing (images, video, audio, documents)
- Online presence (last seen)
- Push notifications for offline users

**Non-Functional:**
- 2B users, 100B messages/day
- Message delivery latency < 100ms (online users)
- High availability: 99.99%
- End-to-end encryption (Signal Protocol)
- Messages delivered at-least-once, deduped at receiver

---

## Capacity Estimation

```
Messages:   100B/day = 1.16M/sec (peak ~3.5M/sec)
Users:      2B total, 500M DAU
Media:      30% messages have media
            100B × 30% × 100KB avg = 3 PB/day (use CDN + dedup)

Storage per message: ~100 bytes (text) → 100B × 100B = 10 TB/day
Connections: 500M concurrent WebSocket connections → ~500M WebSocket servers needed at 1K conn/server → 500K servers. Use smarter multiplexing.
```

---

## High-Level Architecture

```
┌──────────┐    WebSocket    ┌──────────────────┐
│ Client A │◄──────────────►│  Chat Server 1    │
└──────────┘                │  (connection mgmt) │
                             └──────────┬────────┘
                                        │
┌──────────┐    WebSocket    ┌──────────▼────────┐   ┌─────────────┐
│ Client B │◄──────────────►│  Chat Server 2    │──►│ Message      │
└──────────┘                └──────────┬────────┘   │ Queue (Kafka)│
                                        │            └──────┬──────┘
                             ┌──────────▼────────┐          │
                             │  Presence Service  │   ┌──────▼──────┐
                             │  (online/offline)  │   │ Message DB  │
                             └───────────────────┘   │ (Cassandra) │
                                                      └─────────────┘

Offline path:
Message Queue → Push Notification Service → FCM/APNs → Device
```

---

## Core Problem: Real-Time Message Delivery

### Why WebSocket?
```
HTTP polling: Client asks "any new messages?" every N seconds → high latency, waste
Long polling: Client waits up to 30s → better but still not real-time
WebSocket: Persistent bidirectional TCP connection → true real-time, < 100ms

WebSocket handshake: HTTP → Upgrade → TCP keeps alive
Server can push at any time
```

### Connection Management
```
500M concurrent users × 1 WebSocket connection each

Load: 1 Chat Server handles 50K concurrent WebSocket connections
      500M / 50K = 10,000 chat servers needed

ZooKeeper: Maintains mapping of { user_id → chat_server_id }
Client reconnect: query ZooKeeper → connect to assigned server

If chat server fails:
  1. ZooKeeper detects via heartbeat
  2. Routes user to new server
  3. Client reconnects (< 1 sec)
```

---

## Message Flow: 1:1 Chat

### Both Users Online
```
1. A sends message → WebSocket → Chat Server 1
2. Chat Server 1:
   a. Writes message to Kafka (message_id, from, to, content, timestamp)
   b. Looks up "which server is B connected to?" (via service registry)
   c. Routes to Chat Server 2 via internal TCP/message bus
   d. Returns ACK to A (message sent ✓)
3. Chat Server 2 pushes message to B via WebSocket
4. B receives → sends delivery receipt → B's Chat Server → Kafka → A's Chat Server → A sees ✓✓
5. B reads → read receipt → A sees blue ✓✓
6. Message persisted to Cassandra asynchronously
```

### User B is Offline
```
1–2. Same: A sends, Chat Server 1 processes, Kafka receives
3. Chat Server checks presence: B is OFFLINE
4. Route message to Push Notification Service
5. Push Notification Service → FCM (Android) / APNs (iOS)
6. Device receives push → app wakes → fetches message from server
7. After delivery: send delivery receipt
```

---

## Message Storage (Cassandra Design)

### Why Cassandra?
- High write throughput (1M+ writes/sec per cluster)
- Time-ordered storage (sort by message timestamp within conversation)
- Linear horizontal scaling
- Multi-datacenter replication

### Schema
```
-- Messages table
CREATE TABLE messages (
    conversation_id  UUID,          -- partition key (1:1 or group)
    message_id       TIMEUUID,      -- cluster key (time-ordered UUID)
    sender_id        BIGINT,
    content          TEXT,
    media_url        TEXT,
    message_type     TEXT,          -- 'text', 'image', 'video'
    status           TEXT,          -- 'sent', 'delivered', 'read'
    created_at       TIMESTAMP,
    PRIMARY KEY (conversation_id, message_id)
) WITH CLUSTERING ORDER BY (message_id DESC);

-- User conversations index
CREATE TABLE user_conversations (
    user_id          BIGINT,
    conversation_id  UUID,
    last_message     TEXT,
    unread_count     INT,
    updated_at       TIMESTAMP,
    PRIMARY KEY (user_id, updated_at)
) WITH CLUSTERING ORDER BY (updated_at DESC);
```

---

## Group Chat

### Small Groups (< 256 members)
```
1. A sends to group_id:1234
2. Group Service fetches member list (cached in Redis)
3. For each online member:
   → Find their chat server → push via internal message bus
4. For offline members:
   → Queue push notification
5. Write to Cassandra once (conversation_id = group_id)
```

### Fan-Out Problem for Large Groups
```
Group with 256 members:
  - 256 servers might need to be notified
  - One message → 256 internal messages

Optimization:
  - Use Kafka topic per group or pub/sub model
  - Chat servers subscribe to groups their users are in
  - One Kafka publish → all subscribing servers consume
```

---

## Presence Service (Online/Offline)

```
Architecture:
  User connects → WebSocket established → Presence Service sets:
    user:{id}:status = ONLINE (Redis, TTL = 60s)
    user:{id}:heartbeat = now

  Every 30s: client sends heartbeat → refreshes TTL
  Client disconnects: TTL expires → status = OFFLINE

  "Last seen": on disconnect, write timestamp to DB
    user:{id}:last_seen = timestamp (persistent)

Scale:
  Redis cluster with geo-sharding
  Read from nearest replica
  Fan-out presence updates to all followers: only for mutual contacts
```

---

## End-to-End Encryption (Signal Protocol)

```
WhatsApp uses Signal Protocol (Double Ratchet + X3DH):
1. Key exchange (X3DH): establish shared secret without server knowing
2. Double Ratchet: every message uses different key (forward secrecy)
3. Server NEVER sees message content — only ciphertext
4. Keys stored on device only

For HLD: mention that encryption is at device level, server stores only ciphertext.
```

---

## Media Storage

```
Media flow:
1. Client encrypts media
2. Upload to object store (S3/GCS) → get media_url
3. Send message with media_url (not the actual media)
4. Recipient downloads from object store directly
5. CDN caches popular media

Media deduplication:
  Hash(media_content) → if hash exists in DB → reuse URL
  Saves ~30% storage (people forward same images)

TTL: 90 days for media (WhatsApp policy). After that, re-download needed.
```

---

## Edge Cases

| Edge Case | Solution |
|-----------|---------|
| Message ordering in group chat | Use lamport timestamps / sequence numbers per conversation |
| Duplicate message delivery | Message ID + receiver-side dedup (Redis set, TTL 24h) |
| Large group (200 members) + all online | Pub/sub model; Kafka topic per group |
| Chat server crash during delivery | Message persisted in Kafka; re-delivered when user reconnects |
| User blocks another | Block list check before delivery; don't reveal block status |
| Message deletion (unsend) | Soft delete + push "message_deleted" event to all who received it |
| Very slow network (2G) | Message queue on device; retry with exponential backoff |
| Multiple devices per user | Deliver to all active sessions; sync "read" state across devices |

---

## Interview Questions

**Q: How does WhatsApp handle 100B messages/day on relatively few servers?**
A: (1) WebSocket over persistent TCP (no HTTP overhead per message), (2) Binary encoding (protobuf not JSON), (3) Cassandra handles millions of writes/sec, (4) Messages are tiny (100 bytes avg text), (5) Efficient server-side fan-out.

**Q: How do you implement "message delivered" vs "message read" status?**
A: Delivered: when chat server pushes to recipient's device, device sends ACK back. Read: when user opens and views the message, app sends read-receipt. These flow back through the chat infrastructure to the sender in < 100ms.

**Q: How would you scale to 10B messages/day?**
A: (1) Add more Cassandra nodes (linear write scale), (2) Shard chat servers by user_id range, (3) Regional deployment — users in India connect to India servers, (4) Edge PoPs for message routing to reduce latency.

**Q: What happens when both users are offline?**
A: Message stored in Cassandra (persistent). Push notification sent to both devices. When either comes online, WebSocket connects, fetches unread messages from Cassandra ordered by timestamp.
