# 📚 Complete Study Material

A structured collection of my complete study notes and practice solutions — covering Data Structures & Algorithms, System Design, Machine Coding, and DevOps.

## 📂 Repository Structure

| Folder | Description |
|--------|-------------|
| [`DSA-Practice/`](./DSA-Practice) | Pattern-wise DSA problems with C++ solutions (Two Pointers, Sliding Window, Kadane, Prefix Sum, Merge Intervals, and more) |
| [`CPP-Advanced/`](./CPP-Advanced) | Senior C++ internals — heap/stack, memory leaks, RAII, smart pointers, move semantics, leak-detection tooling |
| [`System-Design/`](./System-Design) | High-Level Design (HLD) & Low-Level Design (LLD) case studies, design patterns, SOLID, architecture patterns |
| [`Machine-Coding/`](./Machine-Coding) | Machine coding round notes — Node/NestJS, React/Next, MERN, REST API design, DB queries |
| [`DevOps-Infrastructure/`](./DevOps-Infrastructure) | Docker, Kubernetes, CI/CD, AWS, Networking, Linux, Monitoring, Security, IaC |

## 🧩 DSA-Practice — Patterns

- `01-Two-Pointers`
- `02-Fast-Slow-Pointers`
- `03-Sliding-Window`
- `04-Kadane`
- `05-Prefix-Sum`
- `06-Merge-Intervals`
- ...and more

## 🏗️ System-Design

- **01-HLD** — URL Shortener, Twitter Feed, WhatsApp, YouTube, Uber, Instagram, Rate Limiter, and more
- **02-LLD** — Parking Lot, Library Management, Hotel Booking, ATM, Elevator, Chess, E-Commerce + [Design Patterns](./System-Design/02-LLD/DESIGN-PATTERNS) + [SOLID Principles](./System-Design/02-LLD/SOLID-PRINCIPLES)
- **03-[Architecture-Patterns](./System-Design/03-Architecture-Patterns)** — 15 distributed-systems patterns (cross-checked vs Azure Cloud Design Patterns & microservices.io): Idempotency, Messaging, Queues, Resilience (Circuit Breaker/Retry/Bulkhead), Saga, Outbox/CDC, CQRS, Event Sourcing, API Gateway/BFF, Rate Limiting, Cache-Aside, Sharding, Leader Election, Strangler Fig, Sidecar/Service Mesh

## 🧠 CPP-Advanced (Senior C++)

- **01-Memory-Management** — heap vs stack, what a leak really is, RAII, Rule of 0/3/5, `unique_ptr`/`shared_ptr`/`weak_ptr`, reference-cycle leaks, move semantics, ASan/Valgrind — with runnable `.cpp` examples

## 📖 Read as a Book

This repo doubles as a browsable **online book** (powered by [Docsify](https://docsify.js.org) — no build step). Sidebar navigation, full-text search, prev/next paging, syntax highlighting, and a dark cover page.

**Live site:** `https://iAmshiv77.github.io/DSA_PRACTICE/` *(after enabling GitHub Pages — see below)*

**Read locally (offline):**
```bash
# Option A — Docsify CLI (nicest)
npm i -g docsify-cli && docsify serve .

# Option B — any static server, then open http://localhost:3000
npx serve .            # or: python -m http.server 3000
```

**Deploy (one-time setup):** push to `main`, then in GitHub → **Settings → Pages → Source = "GitHub Actions"**. The included [`.github/workflows/pages.yml`](.github/workflows/pages.yml) publishes the book on every push.

## 🚀 How to Use

Each topic folder contains its own `notes.md` (and `README.md` where applicable). DSA problems are individual `.cpp` files named by pattern and problem.

```bash
# Compile and run a DSA solution
g++ DSA-Practice/01-Two-Pointers/03_remove_duplicates.cpp -o sol && ./sol
```

---
> Maintained by [@iAmshiv77](https://github.com/iAmshiv77) · Work in progress 🚧
