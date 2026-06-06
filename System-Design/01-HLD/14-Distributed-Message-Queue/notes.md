# HLD: Distributed Message Queue (Kafka-like)

## What Is It?
A durable, distributed pub/sub system where producers write messages to topics, and consumers read them in order, at their own pace. Enables loose coupling between services.

---

## Requirements

**Functional:**
- Producers publish messages to named topics
- Consumers subscribe to topics (consumer groups)
- At-least-once delivery guarantee
- Message ordering within a partition
- Message retention (configurable, e.g., 7 days)
- Replay from any offset

**Non-Functional:**
- High throughput: 1M+ messages/sec
- Low latency: < 10ms end-to-end
- Durable: no message loss on broker failure
- Horizontal scalability
- Consumer can lag and catch up

---

## Core Concepts

```
Topic:      Named stream of messages (e.g., "user-events", "orders")
Partition:  Topic split into N ordered logs. Scale unit.
Offset:     Position of a message within a partition (monotonically increasing)
Producer:   Writes to a partition (key-based or round-robin)
Consumer:   Reads from partition(s) at its own offset
Consumer Group: Multiple consumers sharing a topic. Each partition → one consumer in group.
Broker:     Server storing partition data. Cluster of N brokers.
Leader:     One broker per partition handles reads/writes.
Follower:   Replicas of partition on other brokers.
```

---

## Architecture

```
Producer A  →┐
Producer B  →│    ┌───────────────────────────────────────┐
Producer C  →│    │           Kafka Cluster               │
             └───►│  ┌─────────────────────────────────┐  │
                  │  │  Topic: "orders"                 │  │
                  │  │  ┌──────────┐ ┌──────────┐      │  │
                  │  │  │ Part 0   │ │ Part 1   │      │  │
                  │  │  │ [0,1,2,3]│ │ [0,1,2] │      │  │
                  │  │  └──────────┘ └──────────┘      │  │
                  │  └─────────────────────────────────┘  │
                  └───────────────────────────────────────┘
                               │
               ┌───────────────┼───────────────┐
               ▼               ▼               ▼
         Consumer 1       Consumer 2       Consumer 3
         (reads Part 0)   (reads Part 1)   (reads Part 0 — group 2)
```

---

## How Kafka Achieves High Throughput

```
1. Sequential disk I/O (append-only log)
   - HDD sequential: 600 MB/s vs random: 150 KB/s
   - SSD sequential: 2+ GB/s
   - Kafka never modifies data, only appends → sequential writes

2. Zero-copy (sendfile syscall)
   - Without zero-copy: Disk → Kernel → User → Socket (4 copies)
   - With zero-copy:    Disk → Socket (via DMA, 0 copies in userspace)
   - 60% less CPU for reads

3. Batching
   - Producer batches N messages before sending (latency vs throughput tradeoff)
   - Broker batches writes to disk
   - Consumer fetches in batches

4. Compression
   - Compress batch (gzip/lz4/snappy) → less network bandwidth

5. Partitioning
   - Each partition is independent → linear write scale
   - 1000 partitions × 1 MB/s each = 1 GB/s throughput
```

---

## Replication & Durability

```
Each partition has 1 leader + N-1 followers (replicas)

Producer sends to leader → leader writes to log → replicas fetch asynchronously

Acknowledgment levels (acks):
  acks=0: fire and forget. Fastest, can lose messages.
  acks=1: leader confirms. Fast, loses data if leader fails before replication.
  acks=all: all ISR (in-sync replicas) confirm. Slowest, zero data loss.

ISR (In-Sync Replicas): replicas that are caught up within threshold
  If follower lags > 10s → removed from ISR
  If leader fails → one of ISR becomes new leader (Zookeeper/KRaft election)
```

---

## Consumer Groups & Partition Assignment

```
Topic: 4 partitions
Consumer Group A: 4 consumers → 1 partition each (optimal)
Consumer Group A: 2 consumers → 2 partitions each
Consumer Group A: 6 consumers → 4 consumers active, 2 idle (wasteful)
Consumer Group A: 1 consumer  → reads all 4 partitions

Rule: Max parallelism = number of partitions

Multiple groups reading same topic = each gets all messages
Use case: "orders" topic → billing group + inventory group both consume

Offset management:
  Consumers commit their offset to Kafka (or Zookeeper)
  On consumer restart: resume from committed offset
  At-most-once:  commit before processing (may lose if crash after commit)
  At-least-once: commit after processing (may reprocess if crash after process)
  Exactly-once:  transactional API (2PC with external system)
```

---

## Message Ordering Guarantees

```
Within a partition: GUARANTEED (append-only log, single writer)
Across partitions:  NOT GUARANTEED

To ensure order for related messages:
  → Use the same partition key: hash(order_id) % N → same partition
  → All events for one order go to same partition → ordered

Trade-off: hot partition if one key dominates (e.g., one popular user)
  Solution: add random suffix to key for even distribution
            (sacrifice per-key ordering, gain throughput)
```

---

## Dead Letter Queue (DLQ)

```
Consumer processing fails 3 times?
  → Don't block the partition forever
  → Move to DLQ topic: "orders-dlq"
  → Alert engineering team
  → Separate consumer processes DLQ (manual review or retry logic)

DLQ message includes:
  - Original message + offset
  - Error reason
  - Retry count
  - Timestamp of each failure
```

---

## Schema Registry

```
Problem: Producer sends message with new field. Old consumer breaks.

Solution: Schema Registry (Confluent / AWS Glue)
  - All producers register schema (Avro/Protobuf/JSON Schema)
  - Schema versions managed centrally
  - Consumer fetches schema to deserialize

Compatibility modes:
  BACKWARD: new schema can read old data (add optional fields)
  FORWARD:  old schema can read new data (remove optional fields)
  FULL:     both (add+remove optional fields only)
```

---

## When to Use Kafka vs Alternatives

| Use Case | Kafka | RabbitMQ | SQS | Redis Streams |
|----------|-------|----------|-----|---------------|
| High throughput streaming | ✅ Best | ⚠️ | ⚠️ | ✅ |
| Message replay | ✅ | ❌ | ❌ | ✅ |
| Complex routing | ❌ | ✅ (exchanges) | ⚠️ | ❌ |
| Task queues / RPC | ⚠️ | ✅ Best | ✅ | ⚠️ |
| Fully managed | ⚠️ (MSK) | ⚠️ | ✅ | ✅ |
| Ordering guarantee | ✅ (partition) | ✅ (queue) | ⚠️ (FIFO) | ✅ |

---

## Edge Cases

| Edge Case | Solution |
|-----------|---------|
| Broker failure | Leader election; consumer rerouted to new leader in < 30s |
| Consumer lag grows unboundedly | Alert on lag; add more consumer instances; increase partitions |
| Hot partition (one key too popular) | Salted keys; repartition with more partitions |
| Message too large (> 1MB default) | Store in S3; Kafka message contains reference URL |
| Consumer poison pill (always fails) | DLQ after N retries; circuit breaker |
| Zookeeper failure (old Kafka) | Zookeeper ensemble; KRaft (Kafka 2.8+) removes Zookeeper |
| Retention full | Increase retention; add more brokers; tiered storage (S3) |

---

## Interview Questions

**Q: Kafka vs database as a message queue — why not just poll the DB?**
A: DB polling creates massive load, slow with millions of rows, no ordering guarantees, no replay, no consumer groups. Kafka is optimized for sequential access (10x faster), supports millions of consumers, has built-in replay from any offset.

**Q: How does exactly-once semantics work in Kafka?**
A: Producer: idempotent producer (sequence numbers, no duplicates on retry). Transactional: wrap produce + DB write in a transaction. Consumer: Kafka Transactions API — consume + produce in same transaction. Very complex; most teams use at-least-once + idempotent consumers.

**Q: How do you handle schema evolution?**
A: Schema Registry + Avro/Protobuf. Rules: (1) Never remove required fields, (2) Always add optional fields with defaults, (3) Use compatibility mode FULL for maximum flexibility.

**Q: How many partitions should a topic have?**
A: Rule of thumb: N = target_throughput / throughput_per_partition. If you want 1 GB/s and each partition gives 10 MB/s → 100 partitions. More partitions = more parallelism but more overhead (file handles, replication). Start with 3–6, scale up as needed.
