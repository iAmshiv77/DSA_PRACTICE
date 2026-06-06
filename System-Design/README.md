# System Design — Interview Preparation

## Structure
```
System-Design/
├── 01-HLD/   High Level Design  — scalability, architecture, distributed systems
└── 02-LLD/   Low Level Design   — OOP, design patterns, class diagrams, code
```

## When Each is Asked

| Round | What They Test |
|-------|---------------|
| HLD (45 min) | How you think at scale. 10M users, 100M requests/day. Trade-offs. |
| LLD (45 min) | Clean code, OOP, design patterns, extensibility, thread safety. |

## HLD vs LLD — Key Difference

| HLD | LLD |
|-----|-----|
| Components & data flow | Classes & relationships |
| Databases, caches, queues | Interfaces, inheritance, composition |
| Scale, availability, consistency | Design patterns, SOLID, extensibility |
| "How does the system work?" | "How do you write the code for it?" |

## Universal Interview Framework

### HLD (45 min breakdown)
```
0–5 min   Clarify requirements (functional + non-functional)
5–10 min  Capacity estimation (QPS, storage, bandwidth)
10–20 min High-level design (boxes and arrows)
20–35 min Deep dive into hardest components
35–45 min Bottlenecks, failure handling, monitoring
```

### LLD (45 min breakdown)
```
0–5 min   Clarify use cases and constraints
5–15 min  Identify entities and relationships
15–25 min Design classes, interfaces, enums
25–40 min Write code for core flows
40–45 min Edge cases, thread safety, extensibility
```
