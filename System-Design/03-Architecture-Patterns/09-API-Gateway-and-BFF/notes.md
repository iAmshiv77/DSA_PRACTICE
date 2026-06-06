# API Gateway & Backend-for-Frontend (BFF)

> A single entry point that sits between clients and a fleet of backend services,
> handling routing, aggregation, and cross-cutting concerns (auth, rate limiting,
> TLS) so each microservice doesn't have to.

---

## The Problem Without a Gateway

```
        ┌──────► Orders Service      Client must:
Mobile ─┼──────► Payments Service    ✗ know every service's address
        ├──────► Users Service       ✗ make many round-trips (chatty, slow on mobile)
        └──────► Inventory Service   ✗ re-implement auth, TLS, retries per call
                                     ✗ break when services are refactored/split
```

Direct client-to-microservice calls leak internal topology, multiply network
round-trips, and duplicate cross-cutting logic.

## With an API Gateway

```
                    ┌───────────────── API Gateway ─────────────────┐
                    │  • Routing      • AuthN/AuthZ   • Rate limiting │
Mobile / Web / 3P ─►│  • Aggregation  • TLS term.     • Caching       │─► Orders
                    │  • Req/resp xform • Logging/trace • Throttling    │─► Payments
                    └────────────────────────────────────────────────┘─► Users ...
                       ONE entry point; internal topology hidden
```

---

## What a Gateway Does (Azure splits these into sub-patterns)

| Responsibility | Pattern name | What it does |
|----------------|--------------|--------------|
| **Routing** | Gateway Routing | One endpoint → route to the right service by path/host |
| **Aggregation** | Gateway Aggregation | Fan out one client request to N services, combine responses (cuts mobile round-trips) |
| **Offloading** | Gateway Offloading | Centralize cross-cutting work: TLS termination, auth, rate limiting, caching, compression |

```
Gateway Aggregation example:
  GET /home  ──► gateway ──┬──► profile-svc
                           ├──► orders-svc      ──► merge into one JSON ──► client
                           └──► recommendations         (1 client call, not 3)
```

---

## Backend-for-Frontend (BFF)

One gateway per client type, because a mobile app and a web app need different data
shapes and payload sizes.

```
Mobile App  ──► Mobile BFF  ──┐
Web App     ──► Web BFF     ──┼──► shared microservices
3rd-party   ──► Public API  ──┘     (Orders, Users, Payments...)

Mobile BFF: small payloads, fewer fields, battery/bandwidth aware
Web BFF:    richer payloads, more data per call
Public API: stable, versioned, rate-limited contract for external devs
```
Avoids a single "god gateway" trying to serve every client optimally (and the team
friction of everyone editing one gateway).

---

## Watch Out For

- **Single point of failure** — the gateway must be HA (multiple instances, health
  checks). It's on the critical path for everything.
- **Latency hop** — one extra network hop; keep gateway logic lean.
- **Becoming a monolith** — don't put business logic in the gateway; it does routing
  + cross-cutting only. Logic belongs in services.
- **Bottleneck / scaling** — must scale with total traffic.

---

## 🌍 Real-World Uses

- **Netflix** — pioneered the BFF pattern (originally Zuul gateway) so each device
  type (TV, mobile, web) gets a tailored API; hundreds of device types, each with
  different needs.
- **Managed gateways:** **AWS API Gateway**, **Kong**, **NGINX**, **Apigee** (Google),
  **Azure API Management**, **Spring Cloud Gateway**, **Envoy** — all implement
  routing + auth + rate limiting + transformation.
- **Service mesh (Istio/Linkerd)** — pushes some gateway concerns (mTLS, retries,
  routing) into sidecars at the infra layer.
- **GraphQL gateways (Apollo Federation)** — a form of gateway aggregation: one
  GraphQL endpoint stitches many backend services.
- **Stripe / Twilio public APIs** — a hardened public-API gateway providing
  versioning, auth keys, and rate limiting to external developers.

---

## Interview Questions

**Q: Why put an API gateway in front of microservices?**
A: It gives clients one stable entry point, hides internal topology, and centralizes
cross-cutting concerns (auth, TLS, rate limiting, caching, logging) so each service
doesn't reimplement them. It can also aggregate calls to reduce client round-trips.

**Q: API Gateway vs BFF?**
A: An API gateway is a general single entry point for all clients. BFF is a variant:
a *separate* gateway per client type (mobile, web, public), each returning data
shaped/optimized for that client, avoiding a one-size-fits-all API.

**Q: What's the risk of an API gateway and how do you mitigate it?**
A: It's a single point of failure and a potential bottleneck on the critical path.
Mitigate with multiple HA instances behind a load balancer, health checks, autoscaling,
and by keeping it thin (no business logic).

**Q: What is gateway aggregation and why does it matter for mobile?**
A: The gateway fans one client request out to multiple services and merges the
responses into one. Mobile networks have high latency, so collapsing N round-trips
into one significantly improves perceived performance and saves battery/bandwidth.

**Q: Should business logic live in the gateway?**
A: No — only routing, aggregation, and cross-cutting concerns. Business logic in the
gateway recreates a monolith, couples services to it, and makes it a deployment
bottleneck.
