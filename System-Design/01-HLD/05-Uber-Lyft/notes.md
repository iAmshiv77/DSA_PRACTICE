# HLD: Uber / Lyft (Ride-Sharing Platform)

## Requirements

**Functional:**
- Rider requests a ride from location A to B
- System matches rider to nearby available driver
- Driver accepts / rejects ride request
- Real-time location tracking of driver during ride
- ETA calculation for driver arrival and trip end
- Surge pricing based on supply/demand
- Payment processing on trip completion
- Trip history for both rider and driver

**Non-Functional:**
- 10M trips/day globally
- Driver location updates every 4 seconds per driver
- 5M active drivers → 1.25M location updates/sec
- Ride matching latency < 1 second
- 99.99% availability
- GPS accuracy within 10 meters

---

## Capacity Estimation

```
Location Updates:
  - 5M active drivers × 1 update per 4 sec = 1.25M writes/sec
  - Each update: { driverId, lat, lng, timestamp, heading } = ~50 bytes
  - Write throughput: 1.25M × 50B = 62.5 MB/sec

Ride Requests:
  - 10M trips/day = ~115 trips/sec (peak: ~350/sec during rush hour)

Storage:
  - Trip records: 10M/day × 500 bytes = 5 GB/day
  - Location history: 1.25M/sec × 50B × 86400 = ~5.4 TB/day
    (only retain last 24h for operational use; archive to cold storage for analytics)

Redis Memory for Live Driver Locations:
  - 5M drivers × (geohash + metadata) ~200 bytes = 1 GB
  - Fits comfortably in a Redis cluster
```

---

## High-Level Architecture

```
                        ┌──────────────────────────────────────────────┐
                        │              API Gateway                      │
                        │      (auth, rate limit, SSL termination)      │
                        └──────┬──────────────┬───────────────┬─────────┘
                               │              │               │
              ┌────────────────▼──┐  ┌────────▼───────┐  ┌───▼──────────────┐
              │  Location Service  │  │  Ride Service  │  │  Driver Service  │
              │  (WebSocket ingest)│  │  (matching,    │  │  (profile, docs, │
              │                    │  │   trip state)  │  │   availability)  │
              └────────┬───────────┘  └────────┬───────┘  └──────────────────┘
                       │                       │
              ┌────────▼───────────┐  ┌────────▼──────────────────┐
              │  Redis Geo         │  │  PostgreSQL (trips, users) │
              │  (live driver locs)│  │  + Read replicas           │
              └────────────────────┘  └───────────────────────────┘
                                               │
                              ┌────────────────▼──────────────────┐
                              │  Kafka (trip events, payments,    │
                              │         notifications, analytics) │
                              └───────────────────────────────────┘
```

---

## Deep Dive 1: Driver Location Updates

```
Driver App → WebSocket connection to Location Service

Every 4 seconds:
  WebSocket frame: { driverId, lat: 37.7749, lng: -122.4194, heading: 270, speed: 35 }

Location Service:
  1. Validate driver is active and authenticated
  2. Convert (lat, lng) to geohash (precision 6 = ~1.2km × 0.6km cell)
  3. Write to Redis:
       GEOADD drivers:active {lng} {lat} {driverId}
       HSET driver:{driverId} lat {lat} lng {lng} geohash {gh} updated_at {ts}
  4. Publish to Kafka topic "driver.location.updates" (for analytics/history)

Why WebSocket over HTTP polling?
  - Polling at 4s interval: 5M × 15 req/min = 75M req/min overhead
  - WebSocket: persistent connection, minimal overhead per update
  - Bi-directional: server can push ride assignments, messages to driver

Why Redis GEOADD?
  - Redis GEO commands store coordinates as geohash internally
  - GEODIST, GEORADIUS, GEORADIUSBYMEMBER are O(N+log M) — very fast
  - GEORADIUS drivers:active {lng} {lat} 5 km ASC COUNT 10
    Returns nearest 10 drivers within 5km in distance order
```

---

## Deep Dive 2: Geohash for Location Indexing

```
Geohash basics:
  World divided into rectangular cells at 12 precision levels
  Precision 6: cell ≈ 1.2km × 0.6km  (used for driver lookup)
  Precision 5: cell ≈ 4.9km × 4.9km  (used for surge zone detection)

  Example: 37.7749, -122.4194 → geohash "9q8yy"

Geohash search for nearby drivers:
  1. Compute geohash of rider's location (precision 6)
  2. Get 8 neighboring cells (prevents edge-of-cell misses)
  3. Query Redis: GEORADIUS drivers:active {lng} {lat} 3 km ASC COUNT 5
     (Redis GEO already handles proximity — geohash is used for partitioning)

Geohash partitioning for scale:
  Shard Redis by geohash prefix (first 2 chars = ~1300 km cells)
  Each shard handles one geographic region
  9q → North America West (San Francisco, LA, Seattle)
  u17 → Europe West (London, Paris)

Why geohash beats lat/lng range queries:
  lat BETWEEN 37.7 AND 37.8 AND lng BETWEEN -122.5 AND -122.3
  → requires 2D index, slow on large datasets
  Geohash converts 2D problem to 1D prefix problem — faster index scans
```

---

## Deep Dive 3: Ride Matching Algorithm

```
Rider requests ride:
  POST /rides { pickupLat, pickupLng, dropoffLat, dropoffLng, rideType }

Ride Service matching steps:
  1. Find candidate drivers:
       GEORADIUS drivers:available {pickupLng} {pickupLat} 5 km ASC COUNT 20
       Returns up to 20 available drivers within 5km, sorted by distance

  2. Filter candidates:
       - Driver status = AVAILABLE (not on another ride)
       - Vehicle type matches ride type (POOL, ECONOMY, BLACK)
       - Driver acceptance rate > threshold (quality filter)

  3. Score candidates:
       Score = w1 × (1/distance) + w2 × driver_rating + w3 × acceptance_rate
       Pick top 5 candidates

  4. Dispatch to top driver first:
       Send push notification to driver app (via APNs/FCM)
       Set 10-second timeout in Redis: SET offer:{rideId}:{driverId} "pending" EX 10

  5. Driver responds:
       ACCEPT → create trip, notify rider, start tracking
       REJECT or TIMEOUT → move to next candidate in list

  6. If all 5 candidates reject → expand search radius to 10km, retry

State stored in Redis during matching:
  ride:{rideId} = { status, riderId, candidateDrivers, offerSentAt, ... }
  TTL: 5 minutes (if not matched, expire and notify rider)
```

---

## Deep Dive 4: Trip State Machine

```
                ┌─────────────┐
                │  REQUESTED  │  ← Rider places request
                └──────┬──────┘
                       │ Driver accepts
                       ▼
                ┌─────────────┐
                │  ACCEPTED   │  ← Driver assigned, en route to pickup
                └──────┬──────┘
                       │ Driver reaches pickup location
                       ▼
                ┌─────────────┐
                │   ARRIVED   │  ← Driver at pickup, waiting for rider
                └──────┬──────┘
                       │ Rider boards, driver starts trip
                       ▼
                ┌──────────────┐
                │  IN_PROGRESS │  ← Trip underway, tracking active
                └──────┬───────┘
                       │ Driver marks arrived at destination
                       ▼
                ┌──────────────┐
                │  COMPLETED   │  ← Payment processed, rating prompted
                └──────────────┘

Alternative transitions:
  REQUESTED  → CANCELLED (by rider, before match)
  ACCEPTED   → CANCELLED (by driver or rider, penalty may apply)
  IN_PROGRESS → CANCELLED (rare, dispute flow)

State stored in:
  PostgreSQL trips table (source of truth)
  Redis ride:{rideId} (real-time state, synced to DB on each transition)

Each state transition:
  1. Update PostgreSQL trips.status
  2. Update Redis ride:{rideId}.status
  3. Publish event to Kafka "trip.state.changed"
     → Notification service sends push to rider/driver
     → Analytics pipeline processes event
     → Payment service listens for COMPLETED event
```

---

## Deep Dive 5: ETA Calculation

```
Two ETA values needed:
  1. Driver arrival ETA (ACCEPTED state): how long until driver reaches rider
  2. Trip ETA (IN_PROGRESS state): how long until trip end

Approach:
  Use routing engine (e.g., Google Maps API, Mapbox, or in-house OSRM)

  Input: { origin: driver_location, destination: pickup_location, departure_time: now }
  Output: { duration_seconds: 420, distance_meters: 2300, route_polyline }

  Real-time traffic:
    - Historical speed data per road segment by hour + day
    - Live incident data (accidents, closures)
    - Bayesian update: estimated_time = historical × (1 + traffic_factor)

  ETA update frequency:
    - Every 30 seconds push updated ETA to rider via WebSocket/SSE
    - If deviation > 2 minutes from original ETA → re-route

In-house vs third-party routing:
  Uber uses in-house routing (H3 hexagonal indexing)
  H3: hexagonal grid at multiple resolutions
    Resolution 9: hex ≈ 0.1 km² (street level)
  Hexagons tile better than squares (equal distance to all 6 neighbors)
  Store road speed per H3 hex → faster lookup than segment-level
```

---

## Deep Dive 6: Surge Pricing Algorithm

```
Goal: increase price when demand > supply in an area to:
  (1) incentivize more drivers to come online
  (2) reduce demand (price-sensitive riders wait)

Surge multiplier calculation (per geohash zone, precision 5):
  supply  = count of available drivers in zone
  demand  = count of open ride requests in zone
  ratio   = demand / max(supply, 1)

  Surge multiplier table:
    ratio < 1.5  → 1.0× (no surge)
    ratio 1.5-2  → 1.25×
    ratio 2-3    → 1.5×
    ratio 3-4    → 2.0×
    ratio > 4    → 2.5× (or higher, capped at policy limit)

  Smooth surge zones:
    Aggregate multiple precision-5 cells into a surge map
    Avoid hard boundaries: interpolate multiplier between adjacent zones

  Update frequency: every 1 minute
    Kafka stream → Surge Calculation Service → Redis hash
    HSET surge:zones {geohash5} {multiplier}

  Rider sees surge before confirming:
    GET /rides/estimate includes surge_multiplier and surge_reason
    Rider must explicitly confirm if multiplier > 1.5×
```

---

## Data Models

### Trips Table (PostgreSQL)

```sql
CREATE TABLE trips (
    id              BIGINT PRIMARY KEY,       -- Snowflake ID
    rider_id        BIGINT NOT NULL,
    driver_id       BIGINT,
    status          VARCHAR(20) NOT NULL,     -- REQUESTED, ACCEPTED, etc.
    pickup_lat      DECIMAL(9,6),
    pickup_lng      DECIMAL(9,6),
    dropoff_lat     DECIMAL(9,6),
    dropoff_lng     DECIMAL(9,6),
    pickup_address  TEXT,
    dropoff_address TEXT,
    distance_km     DECIMAL(6,2),
    duration_sec    INT,
    base_fare       DECIMAL(8,2),
    surge_multiplier DECIMAL(4,2) DEFAULT 1.0,
    final_fare      DECIMAL(8,2),
    payment_id      BIGINT,
    vehicle_type    VARCHAR(20),
    requested_at    TIMESTAMP DEFAULT NOW(),
    accepted_at     TIMESTAMP,
    arrived_at      TIMESTAMP,
    started_at      TIMESTAMP,
    completed_at    TIMESTAMP,
    cancelled_at    TIMESTAMP,
    cancel_reason   VARCHAR(100),
    INDEX idx_rider_id (rider_id),
    INDEX idx_driver_id (driver_id),
    INDEX idx_status (status),
    INDEX idx_requested_at (requested_at)
);
```

### Drivers Table (PostgreSQL)

```sql
CREATE TABLE drivers (
    id              BIGINT PRIMARY KEY,
    user_id         BIGINT NOT NULL UNIQUE,
    status          VARCHAR(20) DEFAULT 'OFFLINE',  -- OFFLINE, AVAILABLE, ON_TRIP
    vehicle_type    VARCHAR(20),
    vehicle_plate   VARCHAR(20),
    rating          DECIMAL(3,2),
    total_trips     INT DEFAULT 0,
    acceptance_rate DECIMAL(5,2),
    created_at      TIMESTAMP DEFAULT NOW()
);
```

### Redis Keys

```
driver:{driverId}           HASH    lat, lng, heading, speed, status, geohash, updated_at
drivers:available           GEO     all available driver positions (GEOADD)
ride:{rideId}               HASH    status, riderId, driverId, candidateDrivers
offer:{rideId}:{driverId}   STRING  "pending" — expires in 10s
surge:zones                 HASH    {geohash5} → {multiplier}
location_history:{driverId} LIST    last 100 location updates (for replay)
```

---

## Payment Flow

```
Trip completes:
  1. Driver marks trip complete in app
  2. Trip Service:
     a. Calculate fare: base_fare × surge_multiplier + per_km × distance + per_min × duration
     b. Apply promo codes / credits from user wallet
     c. Publish event to Kafka "trip.completed"

  3. Payment Service consumes event:
     a. Charge rider's stored payment method (Stripe, Braintree)
     b. On success: transfer driver's cut (typically 75-80%) to driver's account
     c. On failure: retry 3× with exponential backoff
     d. On repeated failure: mark trip as PAYMENT_FAILED, notify rider

  4. Receipt pushed to rider via push notification + email

Fare breakdown:
  final_fare = (base_fare + (per_km_rate × distance_km) + (per_min_rate × duration_min))
               × surge_multiplier
               × (1 - promo_discount)
  platform_fee = final_fare × 0.20
  driver_payout = final_fare × 0.80

Idempotency:
  Payment request includes idempotency_key = trip_id
  Stripe / payment processor deduplicates on this key
  Prevents double charging if retry occurs
```

---

## API Design

```
POST /api/v1/rides/estimate
Body:   { pickupLat, pickupLng, dropoffLat, dropoffLng, vehicleType }
Resp:   { estimatedFare, surgeMuliplier, etaMinutes, availableDriversCount }

POST /api/v1/rides
Body:   { pickupLat, pickupLng, dropoffLat, dropoffLng, vehicleType, paymentMethodId }
Resp:   { rideId, status: "REQUESTED", estimatedPickupEta }

GET  /api/v1/rides/{rideId}
Resp:   { rideId, status, driver: { name, rating, vehicle, location }, eta }

DELETE /api/v1/rides/{rideId}            (cancel)

WebSocket: /ws/rides/{rideId}
  Server pushes: { event: "DRIVER_LOCATION", lat, lng }
                 { event: "STATUS_CHANGED", status: "ARRIVED" }
                 { event: "ETA_UPDATED", etaSeconds: 240 }

PATCH /api/v1/driver/status
Body:   { status: "AVAILABLE" | "OFFLINE" }

POST /api/v1/driver/location  (or via WebSocket)
Body:   { lat, lng, heading, speed }
```

---

## Edge Cases

| Edge Case | Solution |
|-----------|---------|
| Driver GPS signal lost during trip | Use last known location, flag as LOCATION_LOST, prompt driver to reconnect |
| Driver cancels after acceptance | Penalty applied (affects acceptance rate); auto-reassign if < 2 min of trip start |
| Both rider and driver cancel simultaneously | Optimistic lock on status; first cancel wins, second gets conflict response |
| Surge zone boundary dispute | Surge locked at time of ride request, not at time of payment |
| Payment fails after trip completes | Retry async; if 24h passes, flag account, restrict future bookings until resolved |
| Driver takes wrong route (detour) | Route deviation detection; alert if deviation > 20% from optimal route |
| Multiple ride requests from same rider | Limit 1 active request; return existing rideId on duplicate POST |
| Geohash cell boundary — driver just outside 5km | Search 8 neighboring geohash cells to cover boundary cases |
| Database failure during trip | Redis state allows service to continue; sync to DB on recovery via WAL replay |
| Driver app crash during IN_PROGRESS | Driver reconnects via WebSocket; server sends current state to restore UI |

---

## Interview Deep-Dive Questions

**Q: Why Redis for live driver locations instead of PostgreSQL?**
A: PostgreSQL geo queries on 5M rows updated 1.25M times/sec would require aggressive indexing and would still be slower. Redis GEO operations (GEOADD, GEORADIUS) are in-memory with O(log N) complexity. Redis handles 1M+ writes/sec easily. PostgreSQL is the source of truth for trip records — durable, transactional — while Redis is the operational index for real-time lookups.

**Q: How does the matching service avoid assigning the same driver to two riders simultaneously?**
A: When a driver is offered a ride, their status is set to OFFERING atomically in Redis using a Lua script (SETNX-style). No other matching request can select a driver with status OFFERING or ON_TRIP. The atomic compare-and-swap in Redis prevents race conditions. If the offer expires (10s timeout), status reverts to AVAILABLE.

**Q: How would you design the system to handle 10× the current load?**
A: (1) Shard Redis GEO by geographic region (separate instances per city), (2) Scale Location Service horizontally with sticky WebSocket routing (driver always connects to same node via consistent hashing on driverId), (3) Kafka partitioned by geohash so regional consumers process their local events, (4) Separate read and write PostgreSQL clusters per region, (5) Move to a microservice per city for full isolation.

**Q: How do you ensure ETA accuracy during heavy traffic?**
A: Historical traffic model provides baseline. Live traffic feeds (Google, HERE, in-house probe data from driver GPS) provide real-time correction. The routing engine uses a weighted blend: ETA = 0.6 × live_traffic_estimate + 0.4 × historical_average. Driver GPS probe data is the most accurate since Uber actually knows current travel speeds on every road from active drivers.

**Q: Walk me through what happens when a rider opens the app to request a ride.**
A: (1) App sends GET /rides/estimate with pickup and dropoff coordinates, (2) Ride service queries Redis GEORADIUS for nearby available drivers to compute supply count, (3) Surge service returns current multiplier for the pickup geohash zone, (4) Routing engine computes distance and ETA, (5) All assembled into fare estimate response in < 200ms. When rider confirms: POST /rides triggers matching algorithm as described above.
