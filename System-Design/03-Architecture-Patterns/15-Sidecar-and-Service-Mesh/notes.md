# Sidecar, Ambassador & Service Mesh

> Deploy cross-cutting functionality (proxying, TLS, retries, metrics) as a **separate
> companion process/container** next to your app — instead of baking it into every
> service in every language.

---

## Sidecar Pattern

A helper container deployed in the same unit (Kubernetes **Pod**) as the main app,
sharing its lifecycle and local network — handling concerns the app shouldn't.

```
┌─────────────── Pod ───────────────┐
│  ┌──────────┐      ┌────────────┐  │
│  │  App      │◄────►│  Sidecar    │  │  same Pod: shared network (localhost)
│  │ container │ localhost │ proxy/agent │  │  + shared lifecycle + storage
│  └──────────┘      └─────┬──────┘  │
└──────────────────────────┼─────────┘
                           ▼  (TLS, retries, metrics applied here)
                      other services
```

**Why:** the app stays focused on business logic; the sidecar provides
language-agnostic, reusable infrastructure (no need to re-implement mTLS/retries in
Java *and* Go *and* Python). Deploy/upgrade the sidecar independently.

**Common sidecars:** Envoy proxy (Istio), log shipper (Fluentd), metrics agent,
config/secret refresher, service-mesh data plane.

---

## Ambassador Pattern (a specialized sidecar)

A sidecar that specifically **proxies outbound network calls** on the app's behalf —
the app talks to `localhost`, the ambassador handles the messy network concerns.

```
App ──localhost──► Ambassador ──► (retries, circuit breaking, TLS, service discovery,
                                    routing, monitoring) ──► remote service
```
Useful for adding resilience/observability to a **legacy app you can't modify**, or to
offload client-side networking from app code.

---

## Service Mesh (sidecars at scale)

When *every* service has a proxy sidecar, the fleet of proxies forms a **data plane**,
managed by a **control plane** — that's a service mesh (Istio, Linkerd, Consul Connect).

```
            ┌──────────── Control Plane (Istio istiod) ────────────┐
            │   config, policy, certificates, telemetry rollup      │
            └───┬───────────────┬───────────────┬──────────────────┘
                ▼               ▼               ▼      (push config to proxies)
        ┌───────────┐   ┌───────────┐   ┌───────────┐
        │ App + Envoy│  │ App + Envoy│  │ App + Envoy│   ← DATA PLANE
        └───────────┘   └───────────┘   └───────────┘     (sidecar proxies)
         all service-to-service traffic flows through the proxies
```

**The mesh handles (without app code changes):**
- **mTLS** — automatic encryption + identity between services
- **Traffic management** — routing, canary/blue-green, load balancing
- **Resilience** — retries, timeouts, circuit breaking (the
  [resilience patterns](../04-Circuit-Breaker-and-Resilience) at the infra layer)
- **Observability** — uniform metrics, distributed tracing, access logs
- **Policy** — authz, rate limiting, quotas

**Trade-offs:** extra latency per hop, resource overhead (a proxy per pod),
operational complexity. For a handful of services it's overkill; at hundreds of
polyglot services it's transformative.

---

## 🌍 Real-World Uses

- **Istio + Envoy / Linkerd / Consul Connect** — the dominant service meshes; Envoy
  (born at **Lyft**) is the most widely deployed sidecar proxy.
- **Kubernetes** — the sidecar pattern is native to Pods; log shippers (Fluentd),
  secret refreshers, and mesh proxies all run as sidecars.
- **Google / large microservice fleets** — meshes provide uniform mTLS, telemetry, and
  traffic policy across thousands of polyglot services without touching app code.
- **Ambassador/Emissary, Dapr** — Dapr is a sidecar "runtime" giving any app building
  blocks (pub/sub, state, secrets) over localhost, language-agnostic.
- **Legacy app modernization** — wrap an unmodifiable app with an ambassador to add
  TLS, retries, and metrics it never had.

---

## Interview Questions

**Q: What is the sidecar pattern and why use it?**
A: Deploy cross-cutting functionality (proxy, logging, metrics, secrets) as a separate
container alongside the app in the same unit (Pod), sharing its lifecycle and local
network. It keeps the app focused on business logic and provides language-agnostic,
independently-upgradable infrastructure instead of reimplementing it in every service.

**Q: Sidecar vs ambassador?**
A: Ambassador is a specialized sidecar that specifically proxies the app's *outbound*
network calls (the app talks to localhost; the ambassador handles retries, TLS,
discovery, circuit breaking). All ambassadors are sidecars; not all sidecars are
ambassadors.

**Q: What is a service mesh and what does it give you?**
A: A network of sidecar proxies (data plane) managed by a control plane, routing all
service-to-service traffic. It provides automatic mTLS, traffic management
(canary/routing), resilience (retries/timeouts/circuit breaking), and uniform
observability — all without changing application code.

**Q: What are the downsides of a service mesh?**
A: Added per-hop latency, resource overhead (a proxy per pod), and significant
operational complexity. For a small number of services it's overkill; its value shows
at scale with many polyglot services needing uniform security and observability.

**Q: How does a mesh implement resilience without app changes?**
A: The sidecar proxy intercepts all outbound calls and applies retries, timeouts, and
circuit breaking based on control-plane config — so the resilience patterns live in
infrastructure rather than in each service's code.
