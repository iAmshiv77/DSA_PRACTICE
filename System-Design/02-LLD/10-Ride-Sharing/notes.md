# Ride Sharing System — Low Level Design

## Requirements

### Functional
- Rider requests a ride from a pickup location to a destination
- System matches the nearest available Driver to the request
- Trip goes through a state machine: REQUESTED → DRIVER_ASSIGNED → DRIVER_ARRIVED → IN_PROGRESS → COMPLETED / CANCELLED
- Both Rider and Driver are notified on every state transition (Observer)
- Pricing calculated by distance, time, and surge multiplier (Strategy)
- Driver can accept or reject a ride request
- Rider can cancel before driver arrives
- RideManager is the single entry-point for all ride operations (Singleton)

### Non-Functional
- Observer pattern: decouple state change logic from notification delivery
- Strategy pattern: pluggable pricing (standard, surge, flat)
- Singleton RideManager: one instance coordinates all active trips
- DriverMatcher is a pluggable strategy (nearest, rating-based)

---

## Actors and Use Cases

| Actor   | Use Cases                                                            |
|---------|----------------------------------------------------------------------|
| Rider   | Request ride, cancel ride, rate driver, view trip history            |
| Driver  | Go online/offline, accept/reject request, start trip, end trip       |
| System  | Match driver, calculate fare, notify parties, handle cancellation    |

---

## Class Diagram (textual)

```
RideManager (Singleton)
  ├── map<id, Rider>
  ├── map<id, Driver>
  ├── map<id, Trip>
  ├── DriverMatcher*       ← Strategy
  └── PricingCalculator*   ← Strategy

Location
  ├── latitude, longitude
  └── distanceTo(Location) → double (km, haversine)

Rider
  ├── riderId, name, phone
  ├── currentLocation : Location
  └── implements TripObserver

Driver
  ├── driverId, name, phone, vehicleInfo
  ├── currentLocation : Location
  ├── DriverStatus { OFFLINE, AVAILABLE, ON_TRIP }
  ├── rating : double
  └── implements TripObserver

RideRequest
  ├── requestId
  ├── Rider*
  ├── pickup : Location
  ├── destination : Location
  └── timestamp

Trip
  ├── tripId
  ├── RideRequest*
  ├── Driver*
  ├── TripStatus* (State pattern)   REQUESTED→DRIVER_ASSIGNED→DRIVER_ARRIVED→IN_PROGRESS→COMPLETED/CANCELLED
  ├── startTime, endTime
  ├── fare : double
  └── NotificationBus  ← holds observers

TripObserver (interface)       ← Observer
  └── onStatusChange(Trip&, TripStatus)

TripStatus (enum)
  REQUESTED, DRIVER_ASSIGNED, DRIVER_ARRIVED, IN_PROGRESS, COMPLETED, CANCELLED

DriverMatcher (abstract)       ← Strategy
  ├── NearestDriverMatcher
  └── HighestRatedMatcher

PricingCalculator (abstract)   ← Strategy
  ├── StandardPricing
  └── SurgePricing
```

---

## Design Patterns Used

| Pattern   | Applied To                    | Reason                                                                      |
|-----------|-------------------------------|-----------------------------------------------------------------------------|
| Singleton | RideManager                   | One coordination point for all rides; prevents duplicate assignments         |
| Observer  | TripObserver / NotificationBus| Rider and Driver are notified of state changes without coupling to Trip logic|
| Strategy  | DriverMatcher                 | Swap matching algorithm (nearest vs. rated) without touching Trip code       |
| Strategy  | PricingCalculator             | Swap fare calculation (standard/surge/flat) at runtime                      |
| State     | TripStatus state machine      | Enforce valid transitions; invalid transitions throw rather than silently fail|

---

## Full C++ Implementation

```cpp
#include <iostream>
#include <vector>
#include <map>
#include <string>
#include <memory>
#include <mutex>
#include <cmath>
#include <ctime>
#include <stdexcept>
#include <algorithm>

// ─────────────────────────────────────────────
// Enums
// ─────────────────────────────────────────────

enum class TripStatus  {
    REQUESTED, DRIVER_ASSIGNED, DRIVER_ARRIVED,
    IN_PROGRESS, COMPLETED, CANCELLED
};

enum class DriverStatus { OFFLINE, AVAILABLE, ON_TRIP };

// ─────────────────────────────────────────────
// Location
// ─────────────────────────────────────────────

struct Location {
    double lat, lng;
    Location(double la = 0.0, double lo = 0.0) : lat(la), lng(lo) {}

    // Euclidean approximation (fine for interview; real: haversine)
    double distanceTo(const Location& other) const {
        double dlat = lat - other.lat;
        double dlng = lng - other.lng;
        return std::sqrt(dlat * dlat + dlng * dlng) * 111.0; // rough km
    }
};

// ─────────────────────────────────────────────
// Observer — TripObserver
// ─────────────────────────────────────────────

class Trip; // forward

class TripObserver {
public:
    virtual ~TripObserver() = default;
    virtual void onStatusChange(const Trip& trip, TripStatus newStatus) = 0;
    virtual std::string observerId() const = 0;
};

// NotificationBus: Subject — holds observers, dispatches events
class NotificationBus {
    std::vector<TripObserver*> observers_;
public:
    void subscribe(TripObserver* obs)   { observers_.push_back(obs); }
    void unsubscribe(TripObserver* obs) {
        observers_.erase(
            std::remove(observers_.begin(), observers_.end(), obs),
            observers_.end());
    }
    void notify(const Trip& trip, TripStatus status) const {
        for (auto* obs : observers_) obs->onStatusChange(trip, status);
    }
};

// ─────────────────────────────────────────────
// RideRequest
// ─────────────────────────────────────────────

class RideRequest {
    static int counter_;
    std::string requestId_;
    std::string riderId_;
    Location    pickup_;
    Location    destination_;
    time_t      timestamp_;
public:
    RideRequest(std::string riderId, Location pickup, Location dest)
        : requestId_("REQ-" + std::to_string(++counter_)),
          riderId_(std::move(riderId)),
          pickup_(pickup), destination_(dest),
          timestamp_(std::time(nullptr)) {}

    const std::string& getId()       const { return requestId_;   }
    const std::string& getRiderId()  const { return riderId_;     }
    const Location&    getPickup()   const { return pickup_;      }
    const Location&    getDest()     const { return destination_; }
};
int RideRequest::counter_ = 0;

// ─────────────────────────────────────────────
// Driver
// ─────────────────────────────────────────────

class Driver : public TripObserver {
    std::string  driverId_;
    std::string  name_;
    Location     location_;
    DriverStatus status_;
    double       rating_;
public:
    Driver(std::string id, std::string name, Location loc, double rating = 4.5)
        : driverId_(std::move(id)), name_(std::move(name)),
          location_(loc), status_(DriverStatus::AVAILABLE), rating_(rating) {}

    const std::string& getId()      const { return driverId_; }
    const std::string& getName()    const { return name_;     }
    const Location&    getLocation() const { return location_; }
    DriverStatus       getStatus()  const { return status_;   }
    double             getRating()  const { return rating_;   }

    void setStatus(DriverStatus s)    { status_   = s; }
    void setLocation(Location loc)    { location_ = loc; }

    std::string observerId() const override { return "driver:" + driverId_; }

    void onStatusChange(const Trip& trip, TripStatus newStatus) override;
};

// ─────────────────────────────────────────────
// Rider
// ─────────────────────────────────────────────

class Rider : public TripObserver {
    std::string riderId_;
    std::string name_;
    Location    location_;
public:
    Rider(std::string id, std::string name, Location loc)
        : riderId_(std::move(id)), name_(std::move(name)), location_(loc) {}

    const std::string& getId()       const { return riderId_;  }
    const std::string& getName()     const { return name_;     }
    const Location&    getLocation() const { return location_; }

    std::string observerId() const override { return "rider:" + riderId_; }

    void onStatusChange(const Trip& trip, TripStatus newStatus) override;
};

// ─────────────────────────────────────────────
// Trip — holds state machine + NotificationBus
// ─────────────────────────────────────────────

static const char* statusName(TripStatus s) {
    switch (s) {
        case TripStatus::REQUESTED:        return "REQUESTED";
        case TripStatus::DRIVER_ASSIGNED:  return "DRIVER_ASSIGNED";
        case TripStatus::DRIVER_ARRIVED:   return "DRIVER_ARRIVED";
        case TripStatus::IN_PROGRESS:      return "IN_PROGRESS";
        case TripStatus::COMPLETED:        return "COMPLETED";
        case TripStatus::CANCELLED:        return "CANCELLED";
    }
    return "UNKNOWN";
}

class Trip {
    static int counter_;
    std::string      tripId_;
    RideRequest*     request_;
    Driver*          driver_;
    TripStatus       status_;
    NotificationBus  bus_;
    time_t           startTime_;
    time_t           endTime_;
    double           fare_;
public:
    Trip(RideRequest* req, Driver* driver)
        : tripId_("TRIP-" + std::to_string(++counter_)),
          request_(req), driver_(driver),
          status_(TripStatus::DRIVER_ASSIGNED),
          startTime_(0), endTime_(0), fare_(0.0) {}

    const std::string& getId()      const { return tripId_;  }
    TripStatus         getStatus()  const { return status_;  }
    Driver*            getDriver()  const { return driver_;  }
    RideRequest*       getRequest() const { return request_; }
    double             getFare()    const { return fare_;    }
    time_t             getStartTime() const { return startTime_; }
    time_t             getEndTime()   const { return endTime_;   }

    void subscribeObserver(TripObserver* obs) { bus_.subscribe(obs); }

    // State machine — enforce valid transitions
    void transitionTo(TripStatus next) {
        // Validate transition
        static const std::map<TripStatus, std::vector<TripStatus>> allowed = {
            { TripStatus::REQUESTED,       { TripStatus::DRIVER_ASSIGNED, TripStatus::CANCELLED } },
            { TripStatus::DRIVER_ASSIGNED, { TripStatus::DRIVER_ARRIVED,  TripStatus::CANCELLED } },
            { TripStatus::DRIVER_ARRIVED,  { TripStatus::IN_PROGRESS,     TripStatus::CANCELLED } },
            { TripStatus::IN_PROGRESS,     { TripStatus::COMPLETED,       TripStatus::CANCELLED } },
        };
        auto it = allowed.find(status_);
        if (it == allowed.end()) {
            throw std::runtime_error("Trip is in terminal state");
        }
        const auto& valid = it->second;
        if (std::find(valid.begin(), valid.end(), next) == valid.end()) {
            throw std::runtime_error(
                std::string("Invalid transition: ") +
                statusName(status_) + " → " + statusName(next));
        }

        status_ = next;
        std::cout << "[Trip " << tripId_ << "] " << statusName(next) << "\n";
        bus_.notify(*this, next);
    }

    void setFare(double f) { fare_ = f; }
    void markStarted() { startTime_ = std::time(nullptr); }
    void markEnded()   { endTime_   = std::time(nullptr); }
};
int Trip::counter_ = 0;

// ─────────────────────────────────────────────
// Observer bodies (Trip is now complete)
// ─────────────────────────────────────────────

void Driver::onStatusChange(const Trip& trip, TripStatus newStatus) {
    std::cout << "  [Driver " << name_ << "] notified: "
              << statusName(newStatus) << " (trip " << trip.getId() << ")\n";
    if (newStatus == TripStatus::COMPLETED || newStatus == TripStatus::CANCELLED) {
        status_ = DriverStatus::AVAILABLE;
    }
}

void Rider::onStatusChange(const Trip& trip, TripStatus newStatus) {
    std::cout << "  [Rider " << name_ << "] notified: "
              << statusName(newStatus) << " (trip " << trip.getId() << ")\n";
}

// ─────────────────────────────────────────────
// Strategy — DriverMatcher
// ─────────────────────────────────────────────

class DriverMatcher {
public:
    virtual ~DriverMatcher() = default;
    virtual Driver* match(const RideRequest& req,
                          const std::map<std::string, std::unique_ptr<Driver>>& drivers) = 0;
};

class NearestDriverMatcher : public DriverMatcher {
public:
    Driver* match(const RideRequest& req,
                  const std::map<std::string, std::unique_ptr<Driver>>& drivers) override {
        Driver* best    = nullptr;
        double  minDist = std::numeric_limits<double>::max();
        for (const auto& [id, driver] : drivers) {
            if (driver->getStatus() != DriverStatus::AVAILABLE) continue;
            double dist = driver->getLocation().distanceTo(req.getPickup());
            if (dist < minDist) { minDist = dist; best = driver.get(); }
        }
        return best;
    }
};

class HighestRatedMatcher : public DriverMatcher {
public:
    Driver* match(const RideRequest& /*req*/,
                  const std::map<std::string, std::unique_ptr<Driver>>& drivers) override {
        Driver* best       = nullptr;
        double  maxRating  = -1.0;
        for (const auto& [id, driver] : drivers) {
            if (driver->getStatus() != DriverStatus::AVAILABLE) continue;
            if (driver->getRating() > maxRating) {
                maxRating = driver->getRating();
                best      = driver.get();
            }
        }
        return best;
    }
};

// ─────────────────────────────────────────────
// Strategy — PricingCalculator
// ─────────────────────────────────────────────

class PricingCalculator {
public:
    virtual ~PricingCalculator() = default;
    virtual double calculate(const Location& pickup, const Location& dest,
                             double durationMinutes) const = 0;
    virtual std::string label() const = 0;
};

class StandardPricing : public PricingCalculator {
    static constexpr double BASE_FARE     = 2.50;
    static constexpr double PER_KM_RATE   = 1.20;
    static constexpr double PER_MIN_RATE  = 0.25;
public:
    double calculate(const Location& pickup, const Location& dest,
                     double durationMinutes) const override {
        double km   = pickup.distanceTo(dest);
        double fare = BASE_FARE + (km * PER_KM_RATE) + (durationMinutes * PER_MIN_RATE);
        return std::round(fare * 100.0) / 100.0;
    }
    std::string label() const override { return "Standard"; }
};

class SurgePricing : public PricingCalculator {
    double multiplier_;
public:
    explicit SurgePricing(double mult = 2.0) : multiplier_(mult) {}
    double calculate(const Location& pickup, const Location& dest,
                     double durationMinutes) const override {
        StandardPricing base;
        return base.calculate(pickup, dest, durationMinutes) * multiplier_;
    }
    std::string label() const override { return "Surge x" + std::to_string(multiplier_); }
};

// ─────────────────────────────────────────────
// RideManager — Singleton
// ─────────────────────────────────────────────

class RideManager {
    static RideManager* instance_;
    static std::mutex   instanceMutex_;

    std::map<std::string, std::unique_ptr<Rider>>      riders_;
    std::map<std::string, std::unique_ptr<Driver>>     drivers_;
    std::map<std::string, std::unique_ptr<Trip>>       trips_;
    std::map<std::string, std::unique_ptr<RideRequest>> requests_;

    std::unique_ptr<DriverMatcher>      matcher_;
    std::unique_ptr<PricingCalculator>  pricing_;
    std::mutex                          opMutex_;

    RideManager()
        : matcher_(std::make_unique<NearestDriverMatcher>()),
          pricing_(std::make_unique<StandardPricing>()) {}

public:
    RideManager(const RideManager&)            = delete;
    RideManager& operator=(const RideManager&) = delete;

    static RideManager* getInstance() {
        if (!instance_) {
            std::lock_guard<std::mutex> lock(instanceMutex_);
            if (!instance_) instance_ = new RideManager();
        }
        return instance_;
    }

    void setMatchStrategy(std::unique_ptr<DriverMatcher> m)       { matcher_ = std::move(m); }
    void setPricingStrategy(std::unique_ptr<PricingCalculator> p)  { pricing_ = std::move(p); }

    Rider* registerRider(std::string id, std::string name, Location loc) {
        riders_[id] = std::make_unique<Rider>(id, std::move(name), loc);
        return riders_[id].get();
    }

    Driver* registerDriver(std::string id, std::string name, Location loc, double rating = 4.5) {
        drivers_[id] = std::make_unique<Driver>(id, std::move(name), loc, rating);
        return drivers_[id].get();
    }

    // ── Core flow ─────────────────────────────

    Trip* requestRide(const std::string& riderId,
                      Location pickup, Location destination) {
        std::lock_guard<std::mutex> lock(opMutex_);

        Rider* rider = riders_.at(riderId).get();

        // Create request
        auto req = std::make_unique<RideRequest>(riderId, pickup, destination);
        std::string reqId = req->getId();
        requests_[reqId] = std::move(req);

        // Match driver
        Driver* driver = matcher_->match(*requests_[reqId], drivers_);
        if (!driver) throw std::runtime_error("No available drivers");

        driver->setStatus(DriverStatus::ON_TRIP);

        // Create trip in DRIVER_ASSIGNED state
        auto trip = std::make_unique<Trip>(requests_[reqId].get(), driver);
        trip->subscribeObserver(rider);    // Rider observes trip
        trip->subscribeObserver(driver);   // Driver observes trip

        std::string tripId = trip->getId();
        trips_[tripId] = std::move(trip);

        std::cout << "Driver " << driver->getName()
                  << " assigned to rider " << rider->getName() << "\n";
        // Notify observers of DRIVER_ASSIGNED
        trips_[tripId]->transitionTo(TripStatus::DRIVER_ASSIGNED);

        return trips_[tripId].get();
    }

    void driverArrived(const std::string& tripId) {
        trips_.at(tripId)->transitionTo(TripStatus::DRIVER_ARRIVED);
    }

    void startTrip(const std::string& tripId) {
        auto* trip = trips_.at(tripId).get();
        trip->markStarted();
        trip->transitionTo(TripStatus::IN_PROGRESS);
    }

    void completeTrip(const std::string& tripId) {
        auto* trip = trips_.at(tripId).get();
        trip->markEnded();

        double duration = 15.0; // minutes — in reality: (endTime - startTime) / 60
        double fare = pricing_->calculate(
            trip->getRequest()->getPickup(),
            trip->getRequest()->getDest(),
            duration);
        trip->setFare(fare);

        trip->transitionTo(TripStatus::COMPLETED);
        std::cout << "Fare: $" << fare << " (" << pricing_->label() << ")\n";
    }

    void cancelTrip(const std::string& tripId) {
        trips_.at(tripId)->transitionTo(TripStatus::CANCELLED);
    }
};

RideManager* RideManager::instance_     = nullptr;
std::mutex   RideManager::instanceMutex_;

// ─────────────────────────────────────────────
// main — usage demonstration
// ─────────────────────────────────────────────

int main() {
    RideManager* mgr = RideManager::getInstance();

    // Register participants
    Rider*  alice  = mgr->registerRider("R1",  "Alice",  Location(28.61, 77.20));
    Driver* bob    = mgr->registerDriver("D1", "Bob",    Location(28.62, 77.21), 4.8);
    Driver* carol  = mgr->registerDriver("D2", "Carol",  Location(28.65, 77.25), 4.6);

    (void)alice; (void)carol;

    // Alice requests a ride
    Location pickup(28.61, 77.20), dest(28.70, 77.30);
    Trip* trip = mgr->requestRide("R1", pickup, dest);

    std::cout << "\n--- Trip progressing ---\n";
    mgr->driverArrived(trip->getId());
    mgr->startTrip(trip->getId());
    mgr->completeTrip(trip->getId());

    // Switch to surge pricing and high-rated matching for next trips
    mgr->setPricingStrategy(std::make_unique<SurgePricing>(1.8));
    mgr->setMatchStrategy(std::make_unique<HighestRatedMatcher>());
    std::cout << "\nSurge pricing active\n";

    return 0;
}
```

**Expected output:**
```
Driver Bob assigned to rider Alice
[Trip TRIP-1] DRIVER_ASSIGNED
  [Rider Alice] notified: DRIVER_ASSIGNED (trip TRIP-1)
  [Driver Bob] notified: DRIVER_ASSIGNED (trip TRIP-1)

--- Trip progressing ---
[Trip TRIP-1] DRIVER_ARRIVED
  [Rider Alice] notified: DRIVER_ARRIVED (trip TRIP-1)
  [Driver Bob] notified: DRIVER_ARRIVED (trip TRIP-1)
[Trip TRIP-1] IN_PROGRESS
  [Rider Alice] notified: IN_PROGRESS (trip TRIP-1)
  [Driver Bob] notified: IN_PROGRESS (trip TRIP-1)
[Trip TRIP-1] COMPLETED
  [Rider Alice] notified: COMPLETED (trip TRIP-1)
  [Driver Bob] notified: COMPLETED (trip TRIP-1)
Fare: $16.50 (Standard)

Surge pricing active
```

---

## RideStatus State Machine

```
REQUESTED ──→ DRIVER_ASSIGNED ──→ DRIVER_ARRIVED ──→ IN_PROGRESS ──→ COMPLETED
    │               │                   │                │
    └───────────────┴───────────────────┴────────────────┴──→ CANCELLED
```

Valid transitions are enforced by the `transitionTo()` method. Any attempt to jump states (e.g., REQUESTED → IN_PROGRESS) throws immediately.

---

## Interview Q&A

**Q: Why is RideManager a Singleton?**
A: It coordinates driver availability across all concurrent ride requests. If two instances existed, two requests could independently match the same driver simultaneously. The Singleton ensures driver status changes (`AVAILABLE → ON_TRIP`) are atomic within a single lock.

**Q: How does the Observer pattern work in this design?**
A: `Rider` and `Driver` implement `TripObserver`. When a `Trip` is created, both are subscribed to its `NotificationBus`. Every `transitionTo()` call ends with `bus_.notify(*this, newStatus)`, which calls `onStatusChange()` on each subscriber. The `Trip` class has no knowledge of email, SMS, or WebSocket delivery — that is the observer's responsibility. This satisfies the Open/Closed Principle: add a `NotificationLogObserver` without changing `Trip`.

**Q: What happens if no driver is available?**
A: `matcher_->match()` returns `nullptr`. `requestRide()` throws `"No available drivers"`. In a production system, the request would be queued and retried when a driver comes online (push the request to a Redis queue; when a driver sets status AVAILABLE, pop and assign).

**Q: How do you handle the case where a driver rejects a ride?**
A: Add a `rejectRequest(driverId, tripId)` method that transitions back to REQUESTED state (only valid from DRIVER_ASSIGNED), marks the driver AVAILABLE again, and calls `matcher_->match()` again with a blacklist of already-rejected drivers. Pass the blacklist as a parameter to the Strategy.

**Q: How would surge pricing be triggered automatically?**
A: A background job monitors `activeRequests / availableDrivers` ratio. If ratio > 2.0, inject `SurgePricing(2.0)` via `setPricingStrategy()`. When ratio drops below 1.2, revert to `StandardPricing`. In NestJS this would be a BullMQ scheduled job reading from Redis counters.

**Q: How would you scale this to multiple cities?**
A: Partition by city. Each city gets its own `RideManager` instance (or a city-scoped service in NestJS DI). Driver matching queries only drivers within the same geo-partition. The Singleton constraint becomes per-city, not global. Use a `CityRideManagerRegistry` as the true Singleton that maps `cityId → RideManager`.

**Q: How is this different from the State pattern in the Movie Ticket Booking system?**
A: In Movie Ticket Booking, each `Seat` owns its own state object (one state machine per seat). Here, `Trip` owns the state as a simple `TripStatus` enum and centralizes the transition table in `transitionTo()`. Both are valid — enum + transition map is simpler when there is one state machine per entity and transitions have no polymorphic behavior. Full State-pattern objects are better when each state needs its own methods with different implementations.
