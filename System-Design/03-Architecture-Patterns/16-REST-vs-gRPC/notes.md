# REST vs gRPC

> Two ways for services (or a client and a server) to **talk over the network**.
> REST sends human-readable JSON over HTTP/1.1; gRPC sends compact binary
> (Protobuf) over HTTP/2 using generated stubs that feel like calling a local
> function. Both move data between machines — they just trade readability for speed.

---

## First: What Problem Are They Both Solving?

A service on machine A needs to ask a service on machine B to *do something* or
*return data*. They need an agreed **contract** (what to send, what comes back) and
a **transport** (how the bytes travel). REST and gRPC are two different answers.

```
   Machine A  ── "give me user 42" ──►  Machine B
   (client)   ◄── { user data }    ──   (server)
                    ▲
            this conversation is what REST / gRPC standardize
```

---

## REST — REpresentational State Transfer

You model everything as a **resource** with a URL, and act on it with **HTTP verbs**.

```
GET    /users/42          → fetch user 42
POST   /users             → create a user   (body = JSON)
PUT    /users/42          → replace user 42
PATCH  /users/42          → update fields
DELETE /users/42          → delete user 42
```

- **Format:** JSON (text) — anyone can read it in a browser or `curl`.
- **Transport:** HTTP/1.1, status codes (200, 404, 500).
- **Contract:** loose. Documented in OpenAPI/Swagger, but not enforced by the wire.

```
Client                         Server
  │  GET /users/42  (text)        │
  ├──────────────────────────────►│
  │  200 OK                       │
  │  {"id":42,"name":"Asha"}      │
  ◄──────────────────────────────┤
```

## gRPC — Google Remote Procedure Call

You define **methods** in a `.proto` file. A code generator produces client + server
stubs, so calling a remote method *looks like calling a local function*.

```protobuf
// user.proto — the single source of truth (the contract)
syntax = "proto3";

service UserService {
  rpc GetUser (GetUserRequest) returns (User);
}

message GetUserRequest { int32 id = 1; }

message User {
  int32  id   = 1;
  string name = 2;
  string email = 3;
}
```

```
   protoc (code generator)
   user.proto ──► client stub  (you call userClient.GetUser({id:42}))
              └─► server skeleton (you implement GetUser)
```

- **Format:** Protobuf — compact **binary**, not human-readable.
- **Transport:** HTTP/2 — multiplexing, header compression, **streaming**.
- **Contract:** strict & enforced. The `.proto` *is* the contract; both sides
  generate from it, so mismatches fail at compile time, not in production.

```
Client                              Server
  userClient.GetUser({id:42})        │
  ├── binary frame over HTTP/2 ─────►│ implements GetUser()
  ◄── binary User{42,"Asha",...} ────┤
       (feels like a function call, runs over the network)
```

---

## How They Differ — Side by Side

| Dimension          | REST                          | gRPC                                   |
|--------------------|-------------------------------|----------------------------------------|
| **Data format**    | JSON / text (readable)        | Protobuf / binary (small, fast)        |
| **Transport**      | HTTP/1.1                      | HTTP/2 (multiplexed)                   |
| **Contract**       | Loose (OpenAPI, optional)     | Strict (`.proto`, enforced)            |
| **Payload size**   | Larger (field names repeated) | ~3–10× smaller                         |
| **Speed**          | Good                          | Faster (binary + HTTP/2)               |
| **Streaming**      | Awkward (SSE/polling/WebSocket)| Native — client/server/bi-directional |
| **Browser support**| Native (any client, curl)     | Needs gRPC-Web + proxy                 |
| **Codegen**        | Optional                      | Built-in (many languages)              |
| **Debuggability**  | Easy (read JSON by eye)       | Harder (need tooling to decode)        |
| **Best fit**       | Public APIs, web/mobile front | Internal microservice-to-microservice  |

---

## The Killer Feature: Streaming

REST is request → one response. gRPC (via HTTP/2) supports **4 call types**:

```
1. Unary           client ──req──►        server      (like REST)
                          ◄─resp──

2. Server stream   client ──req──►        server      e.g. live stock prices
                          ◄─msg──  ◄─msg── ◄─msg──     (many responses)

3. Client stream   client ─msg─► ─msg─► ─►server       e.g. upload chunks
                          ◄────── resp ──             (many requests, 1 reply)

4. Bi-directional  client ◄─msg─► ◄─msg─► server       e.g. live chat
                          (both talk at once)
```

This is why gRPC dominates real-time, internal, high-throughput systems.

---

## Concrete Example — Same "Get User" Endpoint

**REST** (Express-style):
```js
app.get('/users/:id', (req, res) => {
  const user = db.find(req.params.id);          // { id, name, email }
  res.json(user);                                // text JSON on the wire
});
// Client:  fetch('/users/42').then(r => r.json())
```

**gRPC** (Node, generated from `user.proto`):
```js
function GetUser(call, callback) {
  const user = db.find(call.request.id);         // call.request is typed
  callback(null, user);                          // binary Protobuf on the wire
}
// Client:  userClient.GetUser({ id: 42 }, (err, user) => { ... })
```

Same intent. REST ships `{"id":42,"name":"Asha","email":"a@x.com"}` (43 bytes of
readable text). gRPC ships the same data as ~12 bytes of binary, with the field
names living only in the `.proto`, never repeated on the wire.

---

## How Good Is It? — When to Pick Which

```
                Public API / 3rd-party devs / browser  ─────►  REST
                Mobile/web front-end talking to backend ─────►  REST
                Service ↔ service inside your cluster   ─────►  gRPC
                Real-time streaming / low latency       ─────►  gRPC
                Polyglot microservices (Go↔Java↔Python) ─────►  gRPC (codegen)
                Need to debug by eyeballing traffic      ─────►  REST
```

**Rule of thumb:** **REST at the edge, gRPC in the core.** A typical system exposes
REST (or GraphQL) to the outside world through an API gateway, then uses gRPC for
the fast, chatty, internal service-to-service calls behind it.

---

## Watch Out For

- **gRPC + browsers** — browsers can't speak raw gRPC; you need **gRPC-Web** plus a
  proxy (Envoy). For a public web/mobile API, REST is usually less friction.
- **Debuggability** — binary Protobuf can't be read with the naked eye; you need
  `grpcurl` / reflection tooling. JSON you can just print.
- **Versioning** — Protobuf is great here: **never reuse or renumber field tags**;
  add new fields with new numbers and old clients keep working.
- **Loose REST contracts** — without OpenAPI discipline, REST APIs drift and break
  clients silently. gRPC catches this at compile time.
- **Overkill** — for a simple CRUD app or public API, gRPC's tooling/codegen is more
  ceremony than it's worth. Don't add it without a real need (scale, streaming).

---

## 🌍 Real-World Uses

- **gRPC internally:** **Google** (origin), **Netflix**, **Uber**, **Square**,
  **Dropbox** — microservices talk to each other over gRPC for speed + typed contracts.
- **REST at the edge:** **Stripe**, **Twilio**, **GitHub**, **Twitter** public APIs —
  REST/JSON because any developer with `curl` must be able to call them.
- **Kubernetes & etcd** — the control plane uses gRPC + Protobuf for component comms.
- **Service mesh (Envoy/Istio)** — carries and load-balances gRPC traffic natively
  over HTTP/2 (ties into [[15-Sidecar-and-Service-Mesh]]).
- **gRPC-Web** — lets Angular/React front-ends call gRPC services through an Envoy proxy.

---

## Interview Questions

**Q: What is the core difference between REST and gRPC?**
A: REST sends human-readable JSON over HTTP/1.1 and models resources via URLs +
verbs. gRPC sends compact binary Protobuf over HTTP/2 and models remote *procedures*
(method calls) defined in a strict `.proto` contract with generated stubs. gRPC is
faster and supports streaming; REST is more universal and easier to debug.

**Q: Why is gRPC faster than REST?**
A: Two reasons. (1) Protobuf is a compact binary format — no repeated field names,
much smaller payloads to serialize and send. (2) HTTP/2 multiplexes many calls over
one connection with header compression, vs HTTP/1.1's one-request-per-connection
overhead.

**Q: When would you NOT use gRPC?**
A: For public-facing APIs and browser/mobile clients. Browsers can't speak raw gRPC
(need gRPC-Web + a proxy), binary payloads are hard to debug, and external developers
expect REST/JSON they can hit with curl. Use REST at the edge.

**Q: What does the `.proto` file give you that REST lacks?**
A: A single, enforced, language-neutral contract. Both client and server generate
code from it, so mismatches fail at compile time, and you get typed stubs in many
languages for free. REST contracts (OpenAPI) are optional and not enforced on the wire.

**Q: What is the typical architecture combining both?**
A: REST (or GraphQL) at the edge through an API gateway for external clients, and
gRPC internally for service-to-service communication. "REST at the edge, gRPC in the
core" — outside gets compatibility, inside gets speed and streaming.

**Q: What kinds of streaming does gRPC support?**
A: Four call types — unary (1 req/1 resp, like REST), server streaming (1 req, many
responses, e.g. live prices), client streaming (many requests, 1 response, e.g. chunk
upload), and bi-directional streaming (both sides send simultaneously, e.g. chat).
