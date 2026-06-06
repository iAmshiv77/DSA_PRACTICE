# REST API Design — Interview Deep Dive

## REST Principles (MUST KNOW)

```
1. Stateless:      Server stores no client session. Each request is self-contained.
2. Client-Server:  Separation of concerns. Client and server evolve independently.
3. Cacheable:      Responses must define cacheability (Cache-Control header).
4. Uniform Interface:
   a. Resource identification in requests (URI)
   b. Manipulation through representations (JSON/XML)
   c. Self-descriptive messages (Content-Type, status codes)
   d. HATEOAS (Hypermedia As The Engine Of Application State)
5. Layered System:  Client doesn't know if it's talking to CDN, LB, or origin.
6. Code on Demand (optional): Server can send executable code (JS).

REST ≠ HTTP. REST is an architectural style. HTTP is the protocol.
GraphQL and gRPC are alternatives to REST.
```

---

## URL Design

```
# Resource naming: NOUNS not VERBS
✅ GET    /posts
✅ GET    /posts/:id
✅ POST   /posts
✅ PUT    /posts/:id       (full replace)
✅ PATCH  /posts/:id       (partial update)
✅ DELETE /posts/:id

❌ GET /getPosts
❌ POST /createPost
❌ DELETE /deletePost/123

# Hierarchical resources (nested)
GET  /users/:userId/posts           → all posts by user
GET  /users/:userId/posts/:postId   → specific post by user
POST /users/:userId/posts           → create post for user

# Prefer flat over deep nesting (max 2 levels)
❌ /users/:id/posts/:postId/comments/:commentId/likes
✅ /comments/:commentId/likes       (use resource directly)

# Actions that don't fit CRUD: use verbs sparingly
POST /posts/:id/publish
POST /posts/:id/like
POST /auth/refresh
POST /payments/:id/refund

# Plural nouns (consistent)
✅ /users, /posts, /categories
❌ /user, /post

# Lowercase, hyphens for multi-word
✅ /blog-posts, /order-items
❌ /blogPosts, /order_items
```

---

## HTTP Methods & Status Codes

```
Method    Safe? Idempotent? Use
GET       ✅    ✅          Read resource
HEAD      ✅    ✅          Like GET but no body (check if resource exists)
OPTIONS   ✅    ✅          CORS preflight, list allowed methods
POST      ❌    ❌          Create resource / non-idempotent action
PUT       ❌    ✅          Full update / upsert (same result if repeated)
PATCH     ❌    ❌*         Partial update (*can be idempotent if designed well)
DELETE    ❌    ✅          Delete (repeating returns 204 or 404)

Status Codes:
  200 OK             — success
  201 Created        — POST succeeded, include Location header
  204 No Content     — DELETE/PUT succeeded, no body
  206 Partial Content — range request (file download chunks)

  301 Moved Permanently  — SEO-friendly redirect (cached by browser)
  302 Found              — temporary redirect (not cached)
  304 Not Modified       — client cache is valid (conditional GET)
  307 Temporary Redirect — like 302 but method preserved
  308 Permanent Redirect — like 301 but method preserved

  400 Bad Request        — malformed syntax, validation error
  401 Unauthorized       — missing/invalid auth (you are not logged in)
  403 Forbidden          — authenticated but not authorized
  404 Not Found          — resource doesn't exist
  405 Method Not Allowed — wrong HTTP method
  409 Conflict           — state conflict (e.g., duplicate email)
  410 Gone               — resource permanently deleted
  422 Unprocessable Entity — validation error (semantically invalid)
  429 Too Many Requests  — rate limited

  500 Internal Server Error — generic server error
  502 Bad Gateway           — upstream server error
  503 Service Unavailable   — server down / maintenance
  504 Gateway Timeout       — upstream timeout
```

---

## Request/Response Design

### Consistent Response Envelope
```json
// Success response
{
  "data": { ... },
  "message": "User created successfully",
  "timestamp": "2024-01-15T10:30:00Z"
}

// Paginated response
{
  "data": [...],
  "pagination": {
    "page": 1,
    "limit": 20,
    "total": 100,
    "lastPage": 5,
    "hasNext": true,
    "hasPrev": false
  }
}

// Error response
{
  "error": {
    "code": "VALIDATION_ERROR",
    "message": "Request validation failed",
    "details": [
      { "field": "email", "message": "Invalid email format" },
      { "field": "age", "message": "Must be at least 18" }
    ]
  },
  "timestamp": "2024-01-15T10:30:00Z",
  "path": "/api/users",
  "requestId": "req_abc123"
}
```

### Filtering, Sorting, Pagination
```
# Filtering
GET /products?status=active&category=electronics&minPrice=100&maxPrice=500

# Search
GET /users?search=john         (server decides which fields to search)
GET /users?name=john&email=... (explicit field search)

# Sorting
GET /posts?sort=createdAt      (ascending, default)
GET /posts?sort=-createdAt     (descending with -)
GET /posts?sort=-createdAt,title  (multiple: newest first, then A-Z title)
# Alternative: sort=createdAt&order=desc

# Pagination — two styles:
1. Offset-based: ?page=2&limit=20
   → Easy to implement, works with any DB
   → Problem: inconsistent when items inserted/deleted between pages ("cursor drift")

2. Cursor-based: ?cursor=eyJpZCI6MTIzfQ&limit=20
   → Stable pages (cursor points to specific item, not position)
   → Used for feeds (Twitter, Instagram) where items inserted frequently
   → Can't jump to page N directly
   → cursor = base64(JSON { id: lastSeenId, createdAt: lastSeenDate })

# Field selection
GET /users?fields=id,name,email   (only return these fields)
```

### Idempotency
```
Problem: POST is not idempotent. Network retry → duplicate payment!
Solution: Idempotency-Key header

Client sends: POST /payments
              Idempotency-Key: unique-uuid-per-operation

Server:
  1. Check if key exists in Redis/DB
  2. If yes → return cached response (same as first request)
  3. If no → process, store result with key (TTL: 24hr), return result

This makes POST idempotent from client perspective.
Stripe, PayPal all use this pattern.

Implementation:
```typescript
async function processPayment(idempotencyKey: string, dto: PaymentDto) {
  const cached = await redis.get(`idem:${idempotencyKey}`);
  if (cached) return JSON.parse(cached);

  const result = await chargeCard(dto);
  await redis.setex(`idem:${idempotencyKey}`, 86400, JSON.stringify(result));
  return result;
}
```
```

---

## API Versioning

```
1. URL versioning (most common, most explicit)
   /api/v1/users
   /api/v2/users
   Pros: easy to see version, can have different routes
   Cons: URL "pollution", clients must update URL

2. Header versioning
   Accept: application/vnd.myapi.v2+json
   API-Version: 2
   Pros: clean URLs
   Cons: harder to test (need to set headers), less visible

3. Query parameter
   /api/users?version=2
   Pros: easy to test in browser
   Cons: caching issues, unconventional

Best practice:
  - Version when you have BREAKING changes
  - Non-breaking (adding fields, new endpoints) → no new version needed
  - Support old version for 6-12 months → deprecation notice → sunset
  - Use Accept header for deprecation warning:
    Deprecation: Sun, 01 Jan 2025 00:00:00 GMT
    Sunset: Mon, 01 Jul 2025 00:00:00 GMT
```

---

## Authentication Patterns

```
1. API Key (server-to-server)
   X-API-Key: key_prod_abc123
   Simple, no expiry (unless rotated), sent with every request
   Store hashed in DB (like password)

2. Bearer Token (JWT)
   Authorization: Bearer eyJhbGc...
   Stateless, self-contained, expiry built in
   Revocation problem: need blacklist or short TTL

3. Basic Auth
   Authorization: Basic base64(username:password)
   Only for development/internal tools
   NEVER without HTTPS

4. OAuth 2.0 (third-party delegation)
   User grants app access to their Google/GitHub data
   App gets access_token, uses it on behalf of user

5. Cookie-based (web apps)
   Set-Cookie: session=xxx; HttpOnly; Secure; SameSite=Strict
   CSRF protection needed
```

---

## Rate Limiting Response

```http
HTTP/1.1 429 Too Many Requests
Retry-After: 60
X-RateLimit-Limit: 100
X-RateLimit-Remaining: 0
X-RateLimit-Reset: 1704067200

{
  "error": {
    "code": "RATE_LIMIT_EXCEEDED",
    "message": "Too many requests. Retry after 60 seconds."
  }
}
```

---

## Common Headers

```http
# Request headers
Content-Type: application/json
Accept: application/json
Authorization: Bearer token
X-Request-ID: unique-id        (for distributed tracing)
X-Forwarded-For: 203.0.113.1   (client IP through proxies)
If-None-Match: "etag-value"    (conditional GET)
If-Modified-Since: Wed, 01 Jan 2025 00:00:00 GMT

# Response headers
Content-Type: application/json; charset=utf-8
Cache-Control: max-age=300, public
ETag: "33a64df551..."
Last-Modified: Wed, 01 Jan 2025 10:00:00 GMT
Location: /api/users/123       (after 201 Created)
X-Request-ID: unique-id
Retry-After: 60                (after 429 or 503)
```

---

## HATEOAS (Hypermedia)

```json
// Response includes links to related actions
// Allows client to navigate API without hardcoding URLs
{
  "data": {
    "id": "123",
    "status": "pending",
    "amount": 100
  },
  "_links": {
    "self":    { "href": "/api/orders/123" },
    "cancel":  { "href": "/api/orders/123/cancel", "method": "POST" },
    "pay":     { "href": "/api/orders/123/pay",    "method": "POST" },
    "items":   { "href": "/api/orders/123/items",  "method": "GET"  }
  }
}
// Few APIs implement full HATEOAS. Usually just the concept is discussed in interviews.
```

---

## REST vs GraphQL vs gRPC

```
REST:
  Pros: Simple, widely understood, HTTP caching works, browser-friendly
  Cons: Over-fetching (too many fields), under-fetching (need multiple calls),
        N+1 (nested resources), versioning complexity

GraphQL:
  Pros: Client specifies exact fields needed, single endpoint, no versioning,
        strongly typed schema, introspection
  Cons: Complex caching (per-query not per-URL), N+1 on server (use DataLoader),
        no browser caching (all POST), overkill for simple APIs
  Use: Complex apps with many frontend teams, mobile (bandwidth sensitive)

gRPC:
  Pros: Binary protocol (smaller, faster), streaming, strong contracts (proto),
        generated clients in any language, bi-directional streaming
  Cons: Browser can't use directly (needs grpc-web proxy), hard to debug,
        not human-readable
  Use: Internal microservices, real-time streaming, high-performance backend-to-backend

When to use:
  Public API: REST (most familiar, tooling, caching)
  Multiple clients with different data needs: GraphQL
  Internal microservices with strict contracts: gRPC
```

---

## API Design Interview Questions

**Q: How do you design a paginated API that handles live data (items being added/deleted)?**
```
Offset pagination fails with live data:
  - Page 1 returns items 1-10
  - New item inserted at top
  - Page 2 now returns items 11-20, but old item 10 is now at position 11
  - Item 10 is shown TWICE (duplicate) or skipped

Solution: Cursor-based pagination
  - After page 1, server returns cursor = encode({ createdAt: lastItem.createdAt, id: lastItem.id })
  - Page 2 query: WHERE (createdAt, id) < (cursor.createdAt, cursor.id) ORDER BY createdAt DESC LIMIT 20
  - Inserts/deletes before cursor don't affect results

Use keyset/cursor pagination for feeds, timelines, chat history.
Use offset pagination for reports, analytics where exact page navigation needed.
```

**Q: How do you design an API for bulk operations?**
```
Option 1: Separate bulk endpoint
  POST /users/bulk
  Body: { users: [{name: "A"}, {name: "B"}] }
  Response: { results: [{id: 1, status: "created"}, {id: null, status: "error", error: "..."}] }

Option 2: Transaction semantics
  all-or-nothing: fails entire batch if any item fails (use DB transaction)
  partial success: each item independently, return per-item status

Best practice:
  - Limit batch size (e.g., max 100 items)
  - Return per-item status array
  - Use 207 Multi-Status for mixed success/failure
  - Consider async processing (return jobId, poll or webhook)

POST /emails/bulk-send
→ 202 Accepted { jobId: "abc", statusUrl: "/jobs/abc" }
GET /jobs/abc
→ { status: "completed", results: [...] }
```

**Q: How do you handle breaking vs non-breaking API changes?**
```
Non-breaking (backward compatible — safe to deploy):
  ✅ Adding new optional fields to response
  ✅ Adding new optional request parameters
  ✅ Adding new endpoints
  ✅ Adding new enum values (careful — client may not handle)
  ✅ Relaxing validation (accepting more values)

Breaking (requires new version):
  ❌ Removing fields from response
  ❌ Renaming fields
  ❌ Changing field types
  ❌ Making optional fields required
  ❌ Changing endpoint behavior/semantics
  ❌ Removing endpoints
  ❌ Changing authentication method

Strategy:
  1. Add new field alongside old field (both exist)
  2. Deprecate old field (log warnings, Deprecation header)
  3. After N months, remove old field in v2
  4. Never remove from v1 until sunset date
```
