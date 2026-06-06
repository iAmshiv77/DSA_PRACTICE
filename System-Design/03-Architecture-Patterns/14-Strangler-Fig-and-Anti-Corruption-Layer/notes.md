# Strangler Fig & Anti-Corruption Layer (Legacy Migration)

> How to migrate a legacy monolith to a new system **incrementally and safely** —
> without a risky "big bang" rewrite that takes years and often fails.

---

## Strangler Fig Pattern

Named after the strangler fig vine that grows around a tree, gradually replacing it.
Put a **facade/proxy** in front of the legacy system; route slices of functionality to
new services one at a time until the old system is fully replaced ("strangled").

```
Phase 1: everything → Legacy
   Client ──► [ Facade / Router ] ──► Legacy Monolith

Phase 2: peel off one feature
   Client ──► [ Facade ] ──┬──► New "Payments" Service   (migrated)
                           └──► Legacy Monolith           (everything else)

Phase 3: keep going...
   Client ──► [ Facade ] ──┬──► New Payments Service
                           ├──► New Orders Service
                           └──► Legacy (shrinking)

Phase 4: legacy gone
   Client ──► [ Facade ] ──► New Services (legacy decommissioned)
```

**Why incremental beats big-bang:**
- Ship value continuously; each slice is small and reversible.
- Roll back a single feature, not the whole migration.
- The business keeps running throughout; no multi-year freeze.
- You learn the legacy behavior slice-by-slice instead of all at once.

**Key mechanics:**
- The **facade** (often an [API Gateway](../09-API-Gateway-and-BFF)) routes by
  feature/route; clients don't know what's migrated.
- **Data migration** is the hard part — dual-write or CDC to keep old & new stores in
  sync during transition; cut over per feature.
- Use **feature flags** to flip traffic and roll back instantly.

---

## Anti-Corruption Layer (ACL)

A translation layer between the new system and the legacy (or a third-party) system,
so the legacy's messy model **doesn't leak into and corrupt** your clean new domain
model.

```
   New Clean Domain  ◄──► [ Anti-Corruption Layer ] ◄──► Legacy / 3rd-party
   (your model)            translate / adapt / map        (their ugly model)
```

- The legacy system might have weird field names, different semantics, SOAP, or a
  bad data model. The ACL **translates** between the two models in both directions.
- It's an application of the **Adapter** pattern at the architecture/bounded-context
  boundary (DDD term).
- Protects your new code from being shaped by legacy constraints; you can delete the
  ACL once the legacy is gone.

```
Legacy returns: { "CUST_NM": "A", "STAT_CD": "1" }
        ACL maps to: { name: "A", status: "ACTIVE" }   ← clean domain never sees CUST_NM
```

---

## They Work Together

```
Client ──► [ Strangler Facade ] ──► New Service ──► [ ACL ] ──► Legacy DB/API
                                 └─► Legacy (not yet migrated)
```
During migration, new services often still need legacy data — the ACL keeps that
interaction clean while the strangler fig gradually removes the dependency.

---

## 🌍 Real-World Uses

- **Monolith → microservices migrations** — the standard playbook at virtually every
  company that decomposed a legacy monolith (Amazon, eBay, Shopify, etc.): route
  endpoints through a gateway and carve out services one by one.
- **Mainframe modernization** — banks/insurers front COBOL/mainframe systems with an
  ACL and strangle functionality into modern services over years, safely.
- **Replatforming legacy e-commerce/CMS** — migrate checkout, then catalog, then
  search behind a routing layer while the old site still serves the rest.
- **Martin Fowler / Azure Architecture Center** document both as canonical migration
  patterns; cloud migrations (on-prem → cloud) use strangler routing heavily.
- **API versioning / vendor replacement** — ACL isolates a third-party API so you can
  swap the vendor without touching domain code.

---

## Interview Questions

**Q: What is the strangler fig pattern and why prefer it over a rewrite?**
A: Incrementally replace a legacy system by routing functionality through a facade to
new services one slice at a time until the old system is gone. It's preferred over a
big-bang rewrite because each step is small, reversible, and ships value continuously,
while the business keeps running and risk stays low.

**Q: What's the hardest part of a strangler migration?**
A: Data — keeping the legacy and new data stores consistent during the transition
(dual-writes or CDC) and cutting over per feature without loss, plus routing/feature-
flagging traffic safely.

**Q: What is an anti-corruption layer?**
A: A translation layer between your new clean domain model and a legacy or
third-party system, mapping between their model and yours so their (often messy)
concepts don't leak into and corrupt your domain. It's the Adapter pattern at a
bounded-context boundary.

**Q: How do the two patterns relate?**
A: Strangler fig governs the overall incremental migration (facade + per-feature
cutover); the anti-corruption layer protects new services that must still talk to the
legacy during that migration, keeping the new domain clean until the legacy is
decommissioned.

**Q: How do you safely route and roll back during migration?**
A: A facade/gateway plus feature flags route a feature's traffic to the new service;
if something breaks, flip the flag to send traffic back to the legacy instantly —
because both still exist during the transition.
