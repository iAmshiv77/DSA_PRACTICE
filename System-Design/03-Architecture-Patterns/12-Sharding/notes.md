# Sharding (Horizontal Partitioning)

> Split one large dataset across **multiple databases/nodes (shards)**, each holding a
> subset of the data. The way you scale writes and storage beyond a single machine.

---

## Why Shard

```
Single DB hits a wall:
  ✗ storage: dataset > one disk
  ✗ writes:  > what one primary can handle (read replicas only scale READS)
  ✗ memory:  working set > one machine's RAM

Sharding: split rows across N nodes → each handles 1/N of data + traffic → linear scale
```

> Vertical partitioning (split *columns*/tables) ≠ sharding (split *rows* across
> nodes). Replication scales reads + HA; **sharding scales writes + storage.**

```
                    ┌── Shard 1 (users id 0–1M)
   Router/key ──────┼── Shard 2 (users id 1M–2M)
                    └── Shard 3 (users id 2M–3M)
```

---

## Sharding Strategies

| Strategy | How | Pros | Cons |
|----------|-----|------|------|
| **Hash-based** | `shard = hash(key) % N` | even distribution, no hot spots | range queries scatter; resharding moves most data |
| **Range-based** | by key ranges (A–M, N–Z; date ranges) | efficient range scans | hot spots (one range gets all traffic, e.g. latest dates) |
| **Directory/lookup** | a lookup table maps key → shard | flexible, easy rebalancing | the directory is a SPOF/extra hop |
| **Geo / entity** | by region or tenant | data locality, compliance | uneven sizes |

### Consistent Hashing (the resharding fix)
```
Plain `hash % N`: change N → almost EVERY key remaps → massive data movement.
Consistent hashing: keys & nodes on a ring → adding/removing a node remaps only
~1/N of keys → minimal movement. (Used by DynamoDB, Cassandra, Redis Cluster.)
```

---

## Choosing a Shard Key (the make-or-break decision)

A good shard key has **high cardinality**, **even access distribution**, and matches
your **query patterns**.

```
✓ Good: user_id for a per-user workload (queries scoped to one user → one shard)
✗ Bad:  country (few values, uneven → "India" shard overloaded = hot shard)
✗ Bad:  timestamp for writes (all new writes hit the newest shard = hot shard)
```
The shard key is hard to change later — pick carefully up front.

---

## The Hard Problems

- **Cross-shard queries / joins** — data on different shards can't be joined in the
  DB. Solutions: denormalize, scatter-gather (query all shards, merge in app), or keep
  related data co-located (same shard key).
- **Cross-shard transactions** — no single ACID transaction across shards → use
  [Saga](../05-Saga) / 2PC (avoid).
- **Rebalancing / resharding** — adding shards redistributes data; consistent hashing
  minimizes movement; do it online to avoid downtime.
- **Hot shards** — uneven key distribution overloads one shard; salt keys or repick
  the shard key.
- **Operational complexity** — backups, schema migrations, and monitoring now run ×N.

---

## 🌍 Real-World Uses

- **Instagram** — famously sharded Postgres by user ID with logical shards mapped to
  physical nodes (so they could rebalance without remapping IDs).
- **Discord** — sharded trillions of messages on **Cassandra/ScyllaDB** keyed by
  channel + time bucket.
- **DynamoDB / Cassandra / MongoDB / Vitess (YouTube's MySQL sharding)** — built-in or
  framework sharding with consistent hashing under the hood.
- **Slack / Notion / multi-tenant SaaS** — shard by workspace/tenant for isolation and
  locality.
- **Payment & ledger systems** — shard by account/customer ID so each customer's
  activity stays on one shard (enabling single-shard transactions).
- **Redis Cluster** — hash-slot sharding (16384 slots) across nodes.

---

## Interview Questions

**Q: Sharding vs replication?**
A: Replication copies the *same* data to multiple nodes — it scales reads and gives
HA/failover, but every node still holds the full dataset and writes funnel to a
primary. Sharding splits *different* data across nodes — it scales writes and storage
because each node owns only a subset.

**Q: How do you pick a shard key?**
A: High cardinality, even access distribution, and alignment with query patterns so
common queries hit a single shard. Avoid low-cardinality or monotonic keys (country,
timestamp) that create hot shards. It's hard to change later, so choose deliberately.

**Q: Why consistent hashing over `hash % N`?**
A: With `hash % N`, changing the node count remaps almost all keys, forcing huge data
movement. Consistent hashing places keys and nodes on a ring so adding/removing a node
only remaps ~1/N of keys, enabling smooth rebalancing.

**Q: How do you handle a query that spans shards?**
A: Scatter-gather (query each shard, merge results in the app), denormalize so the
data lives together, or design the shard key so related data co-locates. Cross-shard
joins in the DB aren't possible.

**Q: How do cross-shard transactions work?**
A: There's no native ACID transaction across shards. Use a saga with compensating
transactions for eventual consistency, or 2PC (rarely, due to its scaling/locking
costs). Better: co-locate transactional data on one shard.
