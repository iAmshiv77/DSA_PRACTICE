# Networking, DNS & CDN — Interview Deep Dive

## OSI Model (7 Layers)

```
Layer 7 — Application:  HTTP, HTTPS, WebSocket, gRPC, DNS, FTP, SMTP
Layer 6 — Presentation: SSL/TLS, encoding, compression
Layer 5 — Session:      Session management (rarely discussed separately)
Layer 4 — Transport:    TCP, UDP — end-to-end communication
Layer 3 — Network:      IP — routing between networks
Layer 2 — Data Link:    Ethernet, MAC addresses — local network
Layer 1 — Physical:     Cables, radio waves, bits

Interview shortcut: "All People Seem To Need Data Processing"
```

---

## TCP vs UDP

### TCP (Transmission Control Protocol)
```
Connection-oriented: 3-way handshake before data
  SYN → SYN-ACK → ACK (client → server → client)

Guarantees:
  ✅ Delivery (retransmit on loss)
  ✅ Order (sequence numbers)
  ✅ No duplicates
  ✅ Error checking

Flow control: receiver advertises window size (how much it can buffer)
Congestion control: slow start, congestion avoidance, fast retransmit

Use: HTTP, HTTPS, WebSocket, database connections, file transfer

Connection close: 4-way handshake
  FIN → ACK → FIN → ACK (graceful)
  RST → (immediate, no acknowledgment)
```

### UDP (User Datagram Protocol)
```
Connectionless: no handshake, no state
Guarantees: NONE (no delivery, no order, no duplicate protection)

Why use it?
  ✅ Lower latency (no handshake overhead)
  ✅ No head-of-line blocking
  ✅ Works great for real-time: video calls, gaming, DNS

DNS: UDP port 53 (small queries), TCP for large responses
Video streaming: UDP (can tolerate lost frames)
QUIC (HTTP/3): UDP-based, adds reliability in user-space
```

### TCP Handshake Deep Dive

```
TIME_WAIT state: after connection close, TCP waits 2×MSL (60–240 sec)
  Why: ensure delayed packets don't corrupt new connections
  Problem: server runs out of ports (65535 limit)
  Fix: SO_REUSEADDR, increase ephemeral port range

SYN flood attack: attacker sends SYNs, never completes handshake
  → Server allocates state for each half-open connection
  → Exhausts connection table
  Fix: SYN cookies (don't allocate state until handshake complete)

TCP keep-alive: sends probe packets on idle connections
  Detects dead connections (e.g., NAT timeout, crashed peer)
  Settings: tcp_keepalive_time (7200s default → too long for servers)
  Recommendation: 60s for server applications
```

---

## HTTP Deep Dive

### HTTP/1.1 vs HTTP/2 vs HTTP/3

```
HTTP/1.1:
  - Text-based headers
  - One request per connection (with pipelining, but head-of-line blocking)
  - Keep-alive: reuse connection for multiple requests (still sequential)
  - Each request has full headers (no compression)

HTTP/2:
  - Binary protocol (faster parsing)
  - Multiplexing: multiple concurrent streams over ONE connection
  - Header compression (HPACK: 50–80% reduction)
  - Server push (server sends resources before client asks)
  - Still TCP → head-of-line blocking at transport layer
  - Default for HTTPS today

HTTP/3 (QUIC):
  - UDP-based (not TCP!)
  - No head-of-line blocking (stream-level loss recovery)
  - 0-RTT connection establishment (faster resumption)
  - Built-in encryption (always TLS 1.3)
  - Better on mobile (connection migration by connection ID)
```

### HTTP Status Codes
```
1xx — Informational:  100 Continue, 101 Switching Protocols (WebSocket)
2xx — Success:        200 OK, 201 Created, 204 No Content, 206 Partial Content
3xx — Redirect:       301 Moved Permanently, 302 Found, 304 Not Modified
4xx — Client Error:   400 Bad Request, 401 Unauthorized, 403 Forbidden,
                      404 Not Found, 409 Conflict, 429 Too Many Requests
5xx — Server Error:   500 Internal Server Error, 502 Bad Gateway,
                      503 Service Unavailable, 504 Gateway Timeout

Common interview trap: 401 vs 403
  401: Not authenticated (no/invalid credentials) — "who are you?"
  403: Authenticated but not authorized (no permission) — "you can't do this"
```

### HTTPS & TLS

```
TLS Handshake (TLS 1.3):
1. Client Hello: supported cipher suites, TLS version, client random
2. Server Hello: chosen cipher, server random, certificate
3. Certificate verification: client checks cert against CA
4. Key exchange: ECDHE — generate shared secret without transmitting it
5. Both derive session keys from shared secret + randoms
6. Encrypted communication begins

TLS 1.3 improvements over 1.2:
  - 1-RTT (vs 2-RTT) for new connections
  - 0-RTT resumption (replay risk, use carefully)
  - Removed weak cipher suites (RC4, MD5, SHA1)
  - Forward secrecy mandatory (ephemeral keys)

Certificate chain:
  Server cert → Intermediate CA → Root CA (trusted by OS/browser)

HSTS: HTTP Strict Transport Security
  Server sends: Strict-Transport-Security: max-age=31536000
  Browser: never try HTTP again for this domain (even if user types http://)
```

---

## DNS (Domain Name System)

```
Hierarchy:
  Root (.) → TLD (.com, .in, .org) → Domain (google.com) → Subdomain (mail.google.com)

Resolution steps (uncached):
1. Client → Recursive Resolver (ISP or 8.8.8.8)
2. Resolver → Root server: "who handles .com?"
3. Root → Resolver: ".com TLD server at X.X.X.X"
4. Resolver → TLD server: "who handles google.com?"
5. TLD → Resolver: "authoritative NS at X.X.X.X"
6. Resolver → Authoritative NS: "what is google.com?"
7. Authoritative → Resolver: "172.217.x.x, TTL=300"
8. Resolver → Client (cached for TTL seconds)

TTL: Time to live. Low TTL (60s) = fast propagation for changes.
     High TTL (86400s) = less DNS load, faster for end users.
     During migration: lower TTL to 60s a day before, then change record.
```

### DNS Record Types
```
A:      Domain → IPv4 address  (example.com → 93.184.216.34)
AAAA:   Domain → IPv6 address
CNAME:  Alias → canonical name  (www.example.com → example.com)
MX:     Mail exchange server
NS:     Name server for domain
TXT:    Arbitrary text (SPF, DKIM for email validation)
SOA:    Start of Authority (zone info)
PTR:    Reverse DNS (IP → hostname)
SRV:    Service location (host + port for specific service)
```

### DNS Load Balancing
```
Round-robin DNS: multiple A records for same domain
  example.com → [1.2.3.4, 5.6.7.8, 9.10.11.12]
  Each DNS query returns different order

Limitations:
  - No health checking (returns dead servers too)
  - Client caches → sticks to one server until TTL
  - Uneven distribution (clients behind proxy = uneven)

Better: Use actual Load Balancer (AWS ALB, Nginx) not DNS round-robin
```

---

## Load Balancers

### Layer 4 vs Layer 7

```
Layer 4 (Transport):
  Routes based on IP + TCP port only
  Can't inspect HTTP headers, cookies, URL path
  Faster (less processing)
  Examples: AWS NLB, HAProxy in TCP mode

Layer 7 (Application):
  Routes based on HTTP headers, URL, cookies, body
  Can do: content-based routing, SSL termination, sticky sessions
  Slower (parses HTTP)
  Examples: AWS ALB, Nginx, HAProxy in HTTP mode, Traefik
```

### Load Balancing Algorithms
```
Round Robin:          Requests distributed evenly (1→2→3→1→2...)
Weighted Round Robin: Server A gets 3x requests of Server B (different capacities)
Least Connections:    Route to server with fewest active connections
IP Hash:              hash(client_ip) % N → sticky server per client
Random:               Random selection (simple, works well at scale)
Least Response Time:  Route to fastest server (monitors latency)
```

### Sticky Sessions
```
Problem: User's session stored on Server A. Request routed to Server B → session lost.

Solutions:
1. Cookie-based stickiness: LB sets cookie → routes that client to same server
2. IP-based stickiness: same IP → same server (breaks behind NAT)
3. Externalize session: store in Redis (best) → any server can handle any request

Recommendation: Always externalize session. Sticky sessions are a crutch.
```

---

## CDN (Content Delivery Network)

```
How it works:
  Origin server: your server (e.g., S3 bucket, app server)
  CDN edge: 100s of Points of Presence (PoPs) globally
  User request → DNS resolves to nearest CDN PoP
  CDN has cached content → serves locally (< 10ms)
  If cache miss → CDN fetches from origin (pull-through cache)

Benefits:
  - Low latency (serve from nearby PoP vs origin 200ms away)
  - Reduce origin load (CDN serves 95% of traffic)
  - DDoS protection (CDN absorbs attack traffic)
  - Bandwidth savings

Push vs Pull CDN:
  Pull: CDN fetches from origin on first request per PoP
        Simple. May have cold start latency.
        Good for: mostly static content
  Push: You upload content to CDN proactively
        Control when content is available everywhere
        Good for: software releases, large files
```

### Cache-Control Headers
```
Cache-Control: public, max-age=31536000, immutable
  → CDN + browser can cache for 1 year. Content never changes (use hash in URL).

Cache-Control: no-cache
  → Must revalidate with server on every request (ETag/If-None-Match)

Cache-Control: no-store
  → Never cache (sensitive data: banking, medical)

Cache-Control: s-maxage=3600
  → CDN caches for 1 hour, browser uses default

ETag: "abc123"  + If-None-Match: "abc123"
  → Conditional GET: if content unchanged → 304 Not Modified (no body transfer)
```

---

## WebSocket

```
HTTP Upgrade to WebSocket:
  1. Client: GET /chat HTTP/1.1 + Upgrade: websocket + Sec-WebSocket-Key
  2. Server: HTTP 101 Switching Protocols + Sec-WebSocket-Accept
  3. TCP connection now speaks WebSocket protocol (bidirectional frames)

Use cases: chat, real-time notifications, live scores, collaborative editing
Not needed for: one-way server push → use SSE (Server-Sent Events) instead

Server-Sent Events (SSE):
  HTTP/1.1 + text/event-stream content type
  Server pushes, client can't push back
  Auto-reconnect built in
  Simpler than WebSocket for one-way streaming

WebSocket scale:
  Each connection = open socket = file descriptor
  Linux default FD limit: 1024. Increase to 1M+ with ulimit + sysctl
  1 server with 50K connections × 16 bytes/msg = manageable
```

---

## Networking Interview Questions

**Q: What happens when you type google.com in a browser?**
A: (1) Browser checks cache. (2) OS DNS lookup (local cache → /etc/hosts → resolver). (3) DNS resolution (recursive query). (4) TCP 3-way handshake to IP:443. (5) TLS handshake. (6) HTTP GET request. (7) Server processes, responds with HTML. (8) Browser parses HTML, makes additional requests for CSS/JS/images. (9) Page renders.

**Q: What is NAT and why does it matter for distributed systems?**
A: NAT (Network Address Translation) maps multiple private IPs to one public IP. Home router: many devices → one public IP. Problem for P2P: can't directly connect to a NATed device. Also: NAT keepalive timeouts can drop idle WebSocket connections.

**Q: Why is UDP preferred for gaming/video calls over TCP?**
A: In real-time applications, a dropped packet is better re-sent (we need current state not old state). TCP's retransmit causes head-of-line blocking — all subsequent packets wait for retransmit. For video, a dropped frame is just glitchy for a moment; waiting for TCP retransmit makes it worse.

**Q: What is a reverse proxy vs a forward proxy?**
A: Forward proxy: client-side proxy. Client routes traffic through it (corporate proxy, VPN). Server sees proxy IP. Reverse proxy: server-side. Client connects to it; it forwards to backend servers. Client sees reverse proxy. Examples: Nginx, CDN, API Gateway.

**Q: Explain the difference between TCP FIN and RST.**
A: FIN: graceful shutdown. "I'm done sending. Waiting for your FIN." 4-way handshake. RST: abrupt reset. "Connection is invalid, terminate immediately." No acknowledgment. Triggered by: port not listening, firewall, connection in wrong state.

**Q: What is CORS and how does it work?**
A: Cross-Origin Resource Sharing. Browser security: script on origin-A can't make requests to origin-B by default. Browser sends preflight OPTIONS request first. Server responds with Access-Control-Allow-Origin header. If allowed, browser sends actual request. Note: this is browser enforcement — non-browser clients (curl, Postman) are not affected.
