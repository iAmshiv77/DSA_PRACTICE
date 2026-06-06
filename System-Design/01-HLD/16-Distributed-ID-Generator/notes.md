# HLD: Distributed Unique ID Generator

## Why This Matters
Single DB auto-increment fails at scale (single point of failure, sharded DBs need coordination). You need a system that generates globally unique, sortable IDs at millions per second.

---

## Requirements
- Globally unique (no collisions across servers)
- Numerically sortable by time (useful for pagination, ordering)
- 64-bit integer (fits in BIGINT)
- Generates 100K+ IDs/sec
- High availability
- IDs should NOT reveal business information (not sequential from 1)

---

## Approaches

### Option 1: UUID v4
```
Example: 550e8400-e29b-41d4-a716-446655440000
128-bit random number

Pros: Trivially distributed, no coordination
Cons: Not sortable, 128 bits (2x storage), not human-friendly
```

### Option 2: Twitter Snowflake (RECOMMENDED)
```
64-bit integer structured as:

 1 bit    | 41 bits          | 10 bits   | 12 bits
 (unused) | (timestamp ms)   | (machine) | (sequence)

Timestamp: ms since custom epoch (Jan 1, 2010)
           → 2^41 ms = 69 years before rollover (until 2079)

Machine ID: 10 bits = 1024 unique machines
           Split as: 5 bits datacenter + 5 bits machine = 32 DCs × 32 machines

Sequence:  12 bits = 4096 IDs per millisecond per machine
           Reset to 0 each ms

Total capacity: 1024 machines × 4096/ms = 4.19M IDs/ms = 4.19 BILLION/sec
```

### Option 3: ULID (Universally Unique Lexicographically Sortable ID)
```
128 bits = 48-bit timestamp + 80-bit randomness
Encoded as 26-char base32 string
Sortable by string comparison
Less collision risk than Snowflake (no machine coordination)
```

### Option 4: Segment's KSUID
```
32-bit timestamp (second precision) + 128-bit random = 160 bits
More human-readable
```

---

## Twitter Snowflake — Implementation

```cpp
class SnowflakeGenerator {
    const long long EPOCH = 1609459200000LL;  // Jan 1, 2021 in ms
    const int SEQUENCE_BITS   = 12;
    const int MACHINE_BITS    = 10;
    const int TIMESTAMP_BITS  = 41;

    long long machineId;
    long long sequence = 0;
    long long lastTimestamp = -1;
    mutex mtx;

    long long currentTimeMs() {
        return chrono::duration_cast<chrono::milliseconds>(
            chrono::system_clock::now().time_since_epoch()
        ).count();
    }

public:
    SnowflakeGenerator(long long machineId) : machineId(machineId) {}

    long long nextId() {
        lock_guard<mutex> lock(mtx);
        long long now = currentTimeMs() - EPOCH;

        if (now == lastTimestamp) {
            sequence = (sequence + 1) & 0xFFF;  // 12 bits = 4096 max
            if (sequence == 0) {
                // Sequence exhausted this ms, wait for next ms
                while (now <= lastTimestamp)
                    now = currentTimeMs() - EPOCH;
            }
        } else {
            sequence = 0;
        }
        lastTimestamp = now;

        return (now << (MACHINE_BITS + SEQUENCE_BITS))
             | (machineId << SEQUENCE_BITS)
             | sequence;
    }
};
```

---

## Clock Skew Problem

```
Problem: NTP synchronizes clocks but can go backward.
         If server clock goes back → same ms used again → duplicate IDs!

Solutions:
1. Detect backward clock: if now < lastTimestamp → wait or throw exception
2. Use logical clocks instead of wall clock
3. Use a dedicated time service (TrueTime — Google's approach in Spanner)
   TrueTime returns [earliest, latest] interval → wait until interval excludes past

In practice:
  - Alert if clock skew > 500ms
  - Never use an ID generator on a machine with NTP corrections active
```

---

## Deploying at Scale

```
Option A: Centralized ID Service
  - All services call dedicated ID generator cluster
  - Generators are stateless (identified by machineId)
  - Auto-scaling: add more generators (up to 1024)
  - Bottleneck: network hop per ID request

Option B: Embedded Library (zero network hop)
  - Each service instance is assigned a machineId on startup
  - ZooKeeper/etcd allocates machineId from pool
  - ID generation is local, nanosecond latency
  - Used in production by most companies

Option C: Batch Allocation
  - Generator requests range [1000 IDs] from central counter
  - Uses range locally, requests new range when exhausted
  - Handles coordinator failure gracefully
```

---

## Edge Cases

| Edge Case | Solution |
|-----------|---------|
| Clock goes backward | Detect + wait until clock catches up; throw error if > 1 sec |
| Machine ID conflicts (two servers same ID) | ZooKeeper-based ID allocation with leader election |
| > 1024 machines needed | Increase machine bits to 12 (reduce sequence bits to 10) |
| Sequence overflow within 1ms | Wait for next millisecond |
| Generator restart mid-sequence | Sequence resets to 0 on restart — safe, same ms unlikely |
| 69-year epoch overflow | By then, migrate to new epoch. Alert at 50 years. |

---

## Interview Questions

**Q: Why not use database auto-increment?**
A: Single DB = single point of failure + bottleneck. With sharding: different shards generate conflicting IDs. Need coordination. Snowflake is fully distributed and needs no coordination.

**Q: How do Snowflake IDs help with database performance?**
A: IDs are time-ordered → insertions are sequential → B-tree pages fill in order → no page splits. Random UUIDs cause random insertions → frequent B-tree page splits → write amplification → slower inserts.

**Q: What if two Snowflake generators get the same machineId?**
A: Duplicate IDs possible within same millisecond. Prevention: use ZooKeeper or etcd to atomically claim machineId. On service startup, acquire lock on machineId. Release on shutdown. If process crashes, TTL releases it.
