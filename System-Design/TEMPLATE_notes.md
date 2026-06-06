# System: [Name]

## 1. Functional Requirements
-
-

## 2. Non-Functional Requirements
- Scale: ___ DAU, ___ QPS (read), ___ QPS (write)
- Latency: p99 < ___ ms
- Availability: 99.9% / 99.99%
- Consistency: strong / eventual

## 3. Capacity Estimation
- QPS = DAU × req/day ÷ 86400 = ___
- Storage = ___ records/day × ___ bytes × ___ years = ___
- Bandwidth = ___ QPS × ___ KB = ___

## 4. High-Level Design

```
Client → CDN → Load Balancer → API Servers
                                   ↓
                           Cache (Redis)   DB (SQL/NoSQL)
                                   ↓
                           Message Queue → Workers
```

## 5. Data Model / DB Schema

```sql
-- Table:
```

## 6. Key API Endpoints

```
POST /api/...
GET  /api/...
```

## 7. Deep Dives

### Caching
- Cache what:
- TTL:
- Invalidation strategy:

### Scaling
- DB sharding key:
- Replication: primary-replica / multi-master
- CDN for:

### Async Processing
- Queue:
- Worker does:

## 8. Bottlenecks & Trade-offs

| Trade-off | Choice | Reason |
|-----------|--------|--------|
| SQL vs NoSQL | | |
| Push vs Pull feed | | |
| Cache aside vs write-through | | |

## 9. Failure Scenarios
- DB goes down:
- Cache miss storm:
- Queue backup:
