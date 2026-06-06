# 🏛️ Architecture Patterns

Cross-cutting backend / distributed-systems patterns expected of senior engineers —
the reliability, scalability, and decoupling techniques behind real production systems.
These sit above class-level [design patterns](../02-LLD/DESIGN-PATTERNS) and
[SOLID](../02-LLD/SOLID-PRINCIPLES), and complement the [HLD case studies](../01-HLD).

> Sourced & cross-checked against the [Azure Cloud Design Patterns](https://learn.microsoft.com/en-us/azure/architecture/patterns/)
> catalog and Chris Richardson's [microservices.io](https://microservices.io/patterns/)
> pattern language. Each folder below has a full `notes.md` (diagrams, code, real-world
> uses, interview Q&A). The reference tables further down list the *rest* of the
> canonical catalog so the picture is complete.

---

## 📂 Patterns with full write-ups

| # | Pattern | One-liner |
|---|---------|-----------|
| [01](./01-Idempotency) | **Idempotency** | Make retries safe → exactly-once *effects* on at-least-once delivery |
| [02](./02-Messaging-Service) | **Async Messaging / EDA** | Decouple services in time, space & rate via a broker (queue / pub-sub / stream) |
| [03](./03-Queue-Service) | **Task Queues / Background Jobs** | Offload slow, spiky, retryable work to a worker fleet |
| [04](./04-Circuit-Breaker-and-Resilience) | **Resilience** (Circuit Breaker, Retry, Timeout, Bulkhead, Fallback) | Stop cascading failures; degrade gracefully |
| [05](./05-Saga) | **Saga** | Distributed transactions via local steps + compensations |
| [06](./06-Transactional-Outbox) | **Transactional Outbox** (+ CDC) | Atomic DB write + event publish — solve the dual-write problem |
| [07](./07-CQRS) | **CQRS** | Separate read model from write model |
| [08](./08-Event-Sourcing) | **Event Sourcing** | Store events, derive state; full audit + replay |
| [09](./09-API-Gateway-and-BFF) | **API Gateway & BFF** | Single entry point; routing, aggregation, cross-cutting concerns |
| [10](./10-Rate-Limiting-and-Throttling) | **Rate Limiting & Throttling** | Protect resources; token/leaky/sliding-window algorithms |
| [11](./11-Cache-Aside) | **Cache-Aside** | Lazy cache loading + caching strategies; stampede/penetration |
| [12](./12-Sharding) | **Sharding** | Horizontal partitioning to scale writes & storage |
| [13](./13-Leader-Election) | **Leader Election** | Exactly-one coordinator with automatic failover |
| [14](./14-Strangler-Fig-and-Anti-Corruption-Layer) | **Strangler Fig & ACL** | Safe incremental legacy migration |
| [15](./15-Sidecar-and-Service-Mesh) | **Sidecar / Ambassador / Service Mesh** | Cross-cutting infra as a companion proxy |

---

## 🔗 How they connect

```
At-least-once delivery (messaging/queue) + Idempotency        = exactly-once effect
Saga + Compensating Transactions + Outbox                     = distributed consistency
CQRS + Event Sourcing                                          = scalable read/write split w/ audit
Retry + Circuit Breaker + Timeout + Bulkhead + Fallback        = graceful degradation
API Gateway + BFF + Rate Limiting                              = a robust edge
Cache-Aside + Sharding + Read Replicas                         = data-tier scale
Strangler Fig + ACL + API Gateway                              = safe monolith → microservices
Sidecar / Service Mesh                                         = many of the above, at infra layer
```

---

## 📚 Full catalog reference (the rest, for completeness)

The patterns above are the highest-leverage ones for senior interviews & design. For
exhaustiveness, here's the remainder of the canonical catalog grouped by concern —
many are variations/companions of the detailed patterns above.

### Reliability & Resilience
| Pattern | Summary |
|---------|---------|
| Compensating Transaction | Undo a multi-step eventually-consistent operation (→ basis of [Saga](./05-Saga)) |
| Health Endpoint Monitoring | Expose health-check endpoints external tools probe |
| Queue-Based Load Leveling | Buffer spikes with a queue between producer and service (→ [Queue Service](./03-Queue-Service)) |
| Throttling | Cap resource consumption per client/tenant (→ [Rate Limiting](./10-Rate-Limiting-and-Throttling)) |
| Scheduler Agent Supervisor | Coordinate + recover distributed actions with a supervisor |
| Geode | Geo-distributed nodes each serving any region |
| Deployment Stamps | Deploy independent copies (stamps) of the whole stack per scale-unit |

### Messaging
| Pattern | Summary |
|---------|---------|
| Publisher-Subscriber | Broadcast events to many consumers (→ [Messaging](./02-Messaging-Service)) |
| Competing Consumers | Multiple workers share one channel (→ [Queue Service](./03-Queue-Service)) |
| Priority Queue | Process higher-priority messages first |
| Claim Check | Store a large payload externally; pass a reference through the bus |
| Pipes and Filters | Decompose processing into reusable, chained stages |
| Sequential Convoy | Process related messages in order without blocking other groups |
| Messaging Bridge | Connect otherwise-incompatible messaging systems |
| Choreography | Services react to events with no central orchestrator (→ [Saga](./05-Saga)) |
| Idempotent Consumer | Safely handle duplicate messages (→ [Idempotency](./01-Idempotency)) |

### Data Management & Performance
| Pattern | Summary |
|---------|---------|
| Materialized View | Pre-computed, query-optimized view (→ [CQRS](./07-CQRS) read model) |
| Index Table | Secondary indexes over frequently-queried fields |
| Database per Service | Each microservice owns its data store |
| Static Content Hosting | Serve static assets from object storage/CDN |
| Valet Key | Hand clients a scoped token for direct resource access (e.g. presigned S3 URL) |

### API / Edge / Communication
| Pattern | Summary |
|---------|---------|
| Gateway Routing / Aggregation / Offloading | The three gateway sub-patterns (→ [API Gateway](./09-API-Gateway-and-BFF)) |
| Backends for Frontends | Per-client gateway (→ [API Gateway & BFF](./09-API-Gateway-and-BFF)) |
| Asynchronous Request-Reply | Async backend with a clear client response (polling/callback) |
| Gatekeeper | Dedicated host validates/sanitizes requests before private backends |
| Service Discovery / Registry | Locate service instances dynamically (client/server-side) |
| Federated Identity | Delegate auth to an external identity provider (OAuth/OIDC/SAML) |

### Deployment & Cross-cutting
| Pattern | Summary |
|---------|---------|
| Ambassador | Outbound-proxy sidecar (→ [Sidecar](./15-Sidecar-and-Service-Mesh)) |
| Anti-Corruption Layer | Translate between new and legacy models (→ [Strangler Fig & ACL](./14-Strangler-Fig-and-Anti-Corruption-Layer)) |
| External Configuration Store | Centralize config outside deployment artifacts |
| Compute Resource Consolidation | Pack multiple tasks into one compute unit for efficiency |
| Microservice Chassis | Framework handling cross-cutting concerns for new services |
| Distributed Tracing / Log Aggregation | Observability across services (correlation IDs, centralized logs) |

---

## Sources
- [Azure Cloud Design Patterns — Microsoft Learn](https://learn.microsoft.com/en-us/azure/architecture/patterns/)
- [Microservices.io Pattern Language — Chris Richardson](https://microservices.io/patterns/)
- [Microsoft — Design patterns for microservices](https://learn.microsoft.com/en-us/azure/architecture/microservices/design/patterns)
