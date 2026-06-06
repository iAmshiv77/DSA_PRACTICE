# Leader Election

> In a cluster of identical nodes, elect exactly **one leader** to coordinate actions
> that must not run concurrently (a singleton task, a write coordinator, a scheduler).

---

## The Problem

```
You run a task on a schedule (e.g. "send daily invoices") on a cluster of 5 nodes.
If ALL 5 run it → invoices sent 5×. If you hardcode "node 1 does it" → node 1 dies
→ never runs. You need exactly ONE node doing it, and automatic failover if it dies.
```

Leader election guarantees: **at most one leader at a time**, and **a new leader is
elected if the current one fails.**

```
   Node A ─┐
   Node B ─┼── elect ──► Node B = LEADER (does the singleton work)
   Node C ─┘             A, C = followers (standby)
                         B dies → re-elect → A becomes leader
```

---

## How It's Done (you rarely implement raw consensus yourself)

| Mechanism | How | Used by |
|-----------|-----|---------|
| **Distributed lock + lease (TTL)** | Whoever acquires a lock with an expiring lease is leader; renew via heartbeat; lease expires → re-elect | Redis (Redlock), DB row lock, **etcd/Consul** sessions |
| **Consensus algorithm** | Raft / Paxos elect a leader with quorum agreement | etcd, Consul, ZooKeeper, Kafka (KRaft), CockroachDB |
| **ZooKeeper ephemeral znodes** | Smallest sequential ephemeral node = leader; node vanishes on disconnect → next takes over | Kafka (pre-KRaft), HBase, Hadoop |
| **Kubernetes lease** | `coordination.k8s.io` Lease object; controllers use leader-election libs | K8s controllers/operators |

```python
# Lease-based leader election (conceptual)
while True:
    if lock.acquire("leader", ttl=15):     # atomic; only one wins
        i_am_leader = True
    if i_am_leader:
        lock.renew("leader", ttl=15)        # heartbeat to keep leadership
        do_singleton_work()
    sleep(5)
# If the leader crashes, it stops renewing → lease expires in 15s → another node acquires.
```

---

## Critical Pitfalls

- **Split-brain** — a network partition makes two nodes each think they're leader →
  two coordinators corrupt state. Quorum-based consensus (majority must agree) prevents
  this; lease-based needs **fencing tokens** (monotonic counter; storage rejects
  writes from an old leader).
- **Clock skew** — lease-based election relies on timeouts; badly skewed clocks cause
  premature or missed failovers. Consensus algorithms avoid wall-clock dependence.
- **Failover lag** — between leader death and re-election, the singleton work pauses.
  Tune lease TTL vs failover speed.
- **Don't roll your own consensus.** Use etcd/ZooKeeper/Consul/a K8s lease — naive
  implementations have subtle split-brain bugs.

---

## 🌍 Real-World Uses

- **Kafka** — one broker is the **controller** (elected); each partition has one
  leader replica handling reads/writes. Originally ZooKeeper, now KRaft (Raft).
- **Kubernetes** — controller-manager and scheduler run active-passive with leader
  election so only one instance acts, with automatic failover (HA control plane).
- **Databases (CockroachDB, etcd, Consul, MongoDB replica sets, Patroni for Postgres)**
  — elect a primary/leader for writes; auto-failover on leader loss.
- **Distributed cron / job schedulers** — ensure a scheduled job runs on exactly one
  node (Quartz clustering, Elastic's leader election, etc.).
- **ZooKeeper** — the classic coordination service; many systems delegate election to
  it via ephemeral sequential znodes.

---

## Interview Questions

**Q: When do you need leader election?**
A: When a task must run on exactly one node in a cluster — a singleton scheduled job,
a write coordinator, a primary database, a cluster controller — with automatic
failover if that node dies.

**Q: What is split-brain and how do you prevent it?**
A: A network partition causes two nodes to each believe they're the leader, so both
act and corrupt shared state. Prevent it with quorum-based consensus (a majority must
agree on the leader) and fencing tokens (a monotonically increasing leadership number
that downstream storage checks, rejecting writes from a stale leader).

**Q: How does lease-based election handle a crashed leader?**
A: The leader holds a lock with a TTL and must renew it via heartbeat. If it crashes,
it stops renewing, the lease expires, and another node acquires the lock and becomes
leader. The TTL bounds failover time.

**Q: Why not implement leader election yourself?**
A: Correct election requires solving distributed consensus (handling partitions,
clock skew, split-brain), which is notoriously subtle. Use battle-tested systems —
etcd, ZooKeeper, Consul, or Kubernetes leases — that implement Raft/Paxos correctly.

**Q: How does Raft elect a leader?**
A: Nodes are followers; if one times out without hearing from a leader, it becomes a
candidate, increments a term, and requests votes. A candidate that wins a majority
(quorum) becomes leader and sends heartbeats; the majority requirement prevents two
leaders in the same term.
