# Parking Lot — Low Level Design

## Requirements

### Functional
- Support multiple floors, each with multiple spots
- Spot types: Motorcycle, Compact, Large, Handicapped
- Vehicle types: Motorcycle, Car, Truck
- Issue a ticket on entry, calculate fee on exit
- Display board shows available spots per floor per type
- Support multiple entry/exit gates
- A vehicle can only park in an appropriate spot type

### Non-Functional
- Thread-safe: concurrent entry/exit must not double-assign a spot
- Single ParkingLot instance across the system (Singleton)
- Pricing is configurable without code changes (Strategy)

---

## Actors and Use Cases

| Actor             | Use Cases                                              |
|-------------------|--------------------------------------------------------|
| Driver            | Enter lot, receive ticket, pay and exit                |
| Parking Attendant | Assign spot, process payment, open gate                |
| System Admin      | Configure pricing, view occupancy reports              |

---

## Class Diagram (textual)

```
ParkingLot (Singleton)
  ├── vector<ParkingFloor>
  ├── vector<EntrancePanel>
  ├── vector<ExitPanel>
  └── PricingStrategy*

ParkingFloor
  ├── map<SpotType, vector<ParkingSpot*>>
  └── DisplayBoard

ParkingSpot (abstract)
  ├── CompactSpot
  ├── LargeSpot
  ├── HandicappedSpot
  └── MotorcycleSpot

Vehicle (abstract)
  ├── Car
  ├── Truck
  └── Motorcycle

Ticket
  ├── Vehicle*
  ├── ParkingSpot*
  ├── entry_time
  └── exit_time

PricingStrategy (abstract)
  ├── HourlyPricingStrategy
  └── FlatRatePricingStrategy
```

---

## Design Patterns Used

| Pattern   | Applied To                   | Reason                                                     |
|-----------|------------------------------|------------------------------------------------------------|
| Singleton | ParkingLot                   | Only one lot instance must exist across the application    |
| Factory   | VehicleFactory, SpotFactory  | Decouple creation of concrete vehicle/spot types           |
| Strategy  | PricingStrategy              | Swap pricing rules without changing ticket/payment logic   |

---

## Full C++ Implementation

```cpp
#include <iostream>
#include <vector>
#include <map>
#include <string>
#include <memory>
#include <mutex>
#include <chrono>
#include <stdexcept>
#include <algorithm>

// ─────────────────────────────────────────────
// Enums
// ─────────────────────────────────────────────

enum class SpotType   { MOTORCYCLE, COMPACT, LARGE, HANDICAPPED };
enum class VehicleType{ MOTORCYCLE, CAR, TRUCK };

// ─────────────────────────────────────────────
// Strategy Pattern — Pricing
// ─────────────────────────────────────────────

class PricingStrategy {
public:
    virtual ~PricingStrategy() = default;
    virtual double calculateFee(VehicleType type, double hours) const = 0;
};

class HourlyPricingStrategy : public PricingStrategy {
    // Rates per vehicle type per hour (USD)
    static constexpr double MOTORCYCLE_RATE =  1.0;
    static constexpr double CAR_RATE        =  2.5;
    static constexpr double TRUCK_RATE      =  4.0;
public:
    double calculateFee(VehicleType type, double hours) const override {
        double rate = 0.0;
        switch (type) {
            case VehicleType::MOTORCYCLE: rate = MOTORCYCLE_RATE; break;
            case VehicleType::CAR:        rate = CAR_RATE;        break;
            case VehicleType::TRUCK:      rate = TRUCK_RATE;      break;
        }
        // Minimum charge: 1 hour
        return rate * std::max(hours, 1.0);
    }
};

class FlatRatePricingStrategy : public PricingStrategy {
public:
    double calculateFee(VehicleType /*type*/, double /*hours*/) const override {
        return 10.0; // flat $10 regardless
    }
};

// ─────────────────────────────────────────────
// Vehicle hierarchy
// ─────────────────────────────────────────────

class Vehicle {
protected:
    std::string licensePlate_;
    VehicleType type_;
public:
    Vehicle(std::string plate, VehicleType type)
        : licensePlate_(std::move(plate)), type_(type) {}
    virtual ~Vehicle() = default;

    const std::string& getLicensePlate() const { return licensePlate_; }
    VehicleType getType() const { return type_; }

    // Each vehicle knows which spot type it can use
    virtual SpotType requiredSpotType() const = 0;
};

class Car : public Vehicle {
public:
    explicit Car(std::string plate) : Vehicle(std::move(plate), VehicleType::CAR) {}
    SpotType requiredSpotType() const override { return SpotType::COMPACT; }
};

class Truck : public Vehicle {
public:
    explicit Truck(std::string plate) : Vehicle(std::move(plate), VehicleType::TRUCK) {}
    SpotType requiredSpotType() const override { return SpotType::LARGE; }
};

class Motorcycle : public Vehicle {
public:
    explicit Motorcycle(std::string plate) : Vehicle(std::move(plate), VehicleType::MOTORCYCLE) {}
    SpotType requiredSpotType() const override { return SpotType::MOTORCYCLE; }
};

// ─────────────────────────────────────────────
// Vehicle Factory
// ─────────────────────────────────────────────

class VehicleFactory {
public:
    static std::unique_ptr<Vehicle> create(VehicleType type, const std::string& plate) {
        switch (type) {
            case VehicleType::CAR:        return std::make_unique<Car>(plate);
            case VehicleType::TRUCK:      return std::make_unique<Truck>(plate);
            case VehicleType::MOTORCYCLE: return std::make_unique<Motorcycle>(plate);
        }
        throw std::invalid_argument("Unknown vehicle type");
    }
};

// ─────────────────────────────────────────────
// ParkingSpot hierarchy
// ─────────────────────────────────────────────

class ParkingSpot {
protected:
    std::string spotId_;
    SpotType    spotType_;
    bool        isOccupied_ = false;
public:
    ParkingSpot(std::string id, SpotType type)
        : spotId_(std::move(id)), spotType_(type) {}
    virtual ~ParkingSpot() = default;

    const std::string& getId()   const { return spotId_;    }
    SpotType  getType()          const { return spotType_;  }
    bool      isOccupied()       const { return isOccupied_; }

    void occupy()  { isOccupied_ = true;  }
    void vacate()  { isOccupied_ = false; }

    virtual bool canFitVehicle(const Vehicle& v) const {
        return v.requiredSpotType() == spotType_;
    }
};

class CompactSpot     : public ParkingSpot {
public:
    explicit CompactSpot(std::string id) : ParkingSpot(std::move(id), SpotType::COMPACT) {}
};

class LargeSpot       : public ParkingSpot {
public:
    explicit LargeSpot(std::string id) : ParkingSpot(std::move(id), SpotType::LARGE) {}
};

class HandicappedSpot : public ParkingSpot {
public:
    explicit HandicappedSpot(std::string id) : ParkingSpot(std::move(id), SpotType::HANDICAPPED) {}
    // Handicapped spots can fit any vehicle
    bool canFitVehicle(const Vehicle& /*v*/) const override { return true; }
};

class MotorcycleSpot  : public ParkingSpot {
public:
    explicit MotorcycleSpot(std::string id) : ParkingSpot(std::move(id), SpotType::MOTORCYCLE) {}
};

// ─────────────────────────────────────────────
// Spot Factory
// ─────────────────────────────────────────────

class SpotFactory {
public:
    static std::unique_ptr<ParkingSpot> create(SpotType type, const std::string& id) {
        switch (type) {
            case SpotType::COMPACT:     return std::make_unique<CompactSpot>(id);
            case SpotType::LARGE:       return std::make_unique<LargeSpot>(id);
            case SpotType::HANDICAPPED: return std::make_unique<HandicappedSpot>(id);
            case SpotType::MOTORCYCLE:  return std::make_unique<MotorcycleSpot>(id);
        }
        throw std::invalid_argument("Unknown spot type");
    }
};

// ─────────────────────────────────────────────
// Ticket
// ─────────────────────────────────────────────

using Clock     = std::chrono::steady_clock;
using TimePoint = std::chrono::time_point<Clock>;

class Ticket {
    std::string  ticketId_;
    Vehicle*     vehicle_;
    ParkingSpot* spot_;
    TimePoint    entryTime_;
    TimePoint    exitTime_;
    bool         isSettled_ = false;
public:
    Ticket(std::string id, Vehicle* vehicle, ParkingSpot* spot)
        : ticketId_(std::move(id)), vehicle_(vehicle),
          spot_(spot), entryTime_(Clock::now()) {}

    void settle() {
        exitTime_  = Clock::now();
        isSettled_ = true;
    }

    double getParkingHours() const {
        auto end = isSettled_ ? exitTime_ : Clock::now();
        auto dur = std::chrono::duration_cast<std::chrono::minutes>(end - entryTime_);
        return dur.count() / 60.0;
    }

    const std::string& getId()     const { return ticketId_; }
    Vehicle*           getVehicle()const { return vehicle_;  }
    ParkingSpot*       getSpot()   const { return spot_;     }
    bool               isSettled() const { return isSettled_;}
};

// ─────────────────────────────────────────────
// DisplayBoard
// ─────────────────────────────────────────────

class DisplayBoard {
    int floorNumber_;
    std::map<SpotType, int> availableSpots_;
public:
    explicit DisplayBoard(int floor) : floorNumber_(floor) {}

    void update(SpotType type, int available) {
        availableSpots_[type] = available;
    }

    void show() const {
        std::cout << "=== Floor " << floorNumber_ << " Available Spots ===\n";
        auto label = [](SpotType t) -> std::string {
            switch(t) {
                case SpotType::COMPACT:     return "Compact";
                case SpotType::LARGE:       return "Large";
                case SpotType::HANDICAPPED: return "Handicapped";
                case SpotType::MOTORCYCLE:  return "Motorcycle";
            }
            return "Unknown";
        };
        for (auto& [type, count] : availableSpots_) {
            std::cout << "  " << label(type) << ": " << count << "\n";
        }
    }
};

// ─────────────────────────────────────────────
// ParkingFloor
// ─────────────────────────────────────────────

class ParkingFloor {
    int floorNumber_;
    std::map<SpotType, std::vector<std::unique_ptr<ParkingSpot>>> spots_;
    DisplayBoard displayBoard_;
    mutable std::mutex floorMutex_;
public:
    explicit ParkingFloor(int floor) : floorNumber_(floor), displayBoard_(floor) {}

    void addSpot(std::unique_ptr<ParkingSpot> spot) {
        auto type = spot->getType();
        spots_[type].push_back(std::move(spot));
        refreshDisplay(type);
    }

    // Returns nullptr if no spot available
    ParkingSpot* findAndOccupySpot(const Vehicle& vehicle) {
        std::lock_guard<std::mutex> lock(floorMutex_);
        auto required = vehicle.requiredSpotType();

        // Primary: exact type match
        if (spots_.count(required)) {
            for (auto& spot : spots_.at(required)) {
                if (!spot->isOccupied() && spot->canFitVehicle(vehicle)) {
                    spot->occupy();
                    refreshDisplay(required);
                    return spot.get();
                }
            }
        }

        // Fallback: handicapped spots fit any vehicle
        if (spots_.count(SpotType::HANDICAPPED)) {
            for (auto& spot : spots_.at(SpotType::HANDICAPPED)) {
                if (!spot->isOccupied()) {
                    spot->occupy();
                    refreshDisplay(SpotType::HANDICAPPED);
                    return spot.get();
                }
            }
        }
        return nullptr;
    }

    void vacateSpot(ParkingSpot* spot) {
        std::lock_guard<std::mutex> lock(floorMutex_);
        spot->vacate();
        refreshDisplay(spot->getType());
    }

    void showDisplay() const { displayBoard_.show(); }
    int  getFloorNumber() const { return floorNumber_; }

private:
    // Must be called while holding floorMutex_
    void refreshDisplay(SpotType type) {
        if (!spots_.count(type)) return;
        int available = 0;
        for (auto& spot : spots_.at(type)) {
            if (!spot->isOccupied()) ++available;
        }
        displayBoard_.update(type, available);
    }
};

// ─────────────────────────────────────────────
// ParkingAttendant
// ─────────────────────────────────────────────

class ParkingAttendant {
    std::string name_;
public:
    explicit ParkingAttendant(std::string name) : name_(std::move(name)) {}
    const std::string& getName() const { return name_; }

    void processEntry(const Ticket& ticket) const {
        std::cout << "Attendant " << name_
                  << " issued ticket " << ticket.getId() << "\n";
    }
};

// ─────────────────────────────────────────────
// ParkingLot — Singleton
// ─────────────────────────────────────────────

class ParkingLot {
    std::string name_;
    std::string address_;
    std::vector<std::unique_ptr<ParkingFloor>> floors_;
    std::unique_ptr<PricingStrategy>           pricing_;
    std::map<std::string, std::unique_ptr<Ticket>> activeTickets_; // ticketId → Ticket
    std::mutex lotMutex_;
    int  ticketCounter_ = 0;

    // Private constructor — Singleton
    ParkingLot(std::string name, std::string address)
        : name_(std::move(name)), address_(std::move(address)),
          pricing_(std::make_unique<HourlyPricingStrategy>()) {}

    static ParkingLot* instance_;
    static std::mutex  instanceMutex_;

public:
    // No copy / move
    ParkingLot(const ParkingLot&)            = delete;
    ParkingLot& operator=(const ParkingLot&) = delete;

    // Thread-safe double-checked locking
    static ParkingLot* getInstance(const std::string& name = "",
                                   const std::string& address = "") {
        if (!instance_) {
            std::lock_guard<std::mutex> lock(instanceMutex_);
            if (!instance_) {
                instance_ = new ParkingLot(name, address);
            }
        }
        return instance_;
    }

    void setPricingStrategy(std::unique_ptr<PricingStrategy> strategy) {
        pricing_ = std::move(strategy);
    }

    void addFloor(std::unique_ptr<ParkingFloor> floor) {
        floors_.push_back(std::move(floor));
    }

    // Entry gate: find a spot and issue ticket
    Ticket* vehicleEntry(Vehicle* vehicle) {
        for (auto& floor : floors_) {
            ParkingSpot* spot = floor->findAndOccupySpot(*vehicle);
            if (spot) {
                std::lock_guard<std::mutex> lock(lotMutex_);
                std::string ticketId = "TKT-" + std::to_string(++ticketCounter_);
                auto ticket = std::make_unique<Ticket>(ticketId, vehicle, spot);
                Ticket* rawPtr = ticket.get();
                activeTickets_[ticketId] = std::move(ticket);
                return rawPtr;
            }
        }
        std::cout << "No available spot for vehicle " << vehicle->getLicensePlate() << "\n";
        return nullptr;
    }

    // Exit gate: settle ticket, calculate fee, vacate spot
    double vehicleExit(const std::string& ticketId) {
        std::lock_guard<std::mutex> lock(lotMutex_);
        auto it = activeTickets_.find(ticketId);
        if (it == activeTickets_.end()) {
            throw std::runtime_error("Ticket not found: " + ticketId);
        }

        Ticket* ticket = it->second.get();
        ticket->settle();

        double hours = ticket->getParkingHours();
        double fee   = pricing_->calculateFee(ticket->getVehicle()->getType(), hours);

        // Vacate the spot — find the floor that owns it
        ParkingSpot* spot = ticket->getSpot();
        for (auto& floor : floors_) {
            // We release outside the floor's mutex by calling vacateSpot
            floor->vacateSpot(spot);
        }

        std::cout << "Ticket " << ticketId << " settled. Hours: " << hours
                  << " Fee: $" << fee << "\n";

        activeTickets_.erase(it);
        return fee;
    }

    void showAllDisplayBoards() const {
        for (auto& floor : floors_) floor->showDisplay();
    }
};

// Static member definitions
ParkingLot* ParkingLot::instance_      = nullptr;
std::mutex  ParkingLot::instanceMutex_;

// ─────────────────────────────────────────────
// Demo
// ─────────────────────────────────────────────

int main() {
    ParkingLot* lot = ParkingLot::getInstance("City Center Parking", "123 Main St");

    // Build floors
    auto floor0 = std::make_unique<ParkingFloor>(0);
    floor0->addSpot(SpotFactory::create(SpotType::COMPACT,    "F0-C1"));
    floor0->addSpot(SpotFactory::create(SpotType::COMPACT,    "F0-C2"));
    floor0->addSpot(SpotFactory::create(SpotType::LARGE,      "F0-L1"));
    floor0->addSpot(SpotFactory::create(SpotType::MOTORCYCLE, "F0-M1"));
    floor0->addSpot(SpotFactory::create(SpotType::HANDICAPPED,"F0-H1"));
    lot->addFloor(std::move(floor0));

    lot->showAllDisplayBoards();

    // Vehicles enter
    auto car1   = VehicleFactory::create(VehicleType::CAR,        "ABC-123");
    auto truck1 = VehicleFactory::create(VehicleType::TRUCK,       "TRK-001");
    auto moto1  = VehicleFactory::create(VehicleType::MOTORCYCLE,  "MOT-555");

    Ticket* t1 = lot->vehicleEntry(car1.get());
    Ticket* t2 = lot->vehicleEntry(truck1.get());
    Ticket* t3 = lot->vehicleEntry(moto1.get());

    lot->showAllDisplayBoards();

    // Vehicles exit
    if (t1) lot->vehicleExit(t1->getId());
    if (t2) lot->vehicleExit(t2->getId());
    if (t3) lot->vehicleExit(t3->getId());

    lot->showAllDisplayBoards();
    return 0;
}
```

---

## Key Design Decisions

**Why Singleton for ParkingLot?**
There is physically one lot. All entry/exit gates share the same state. A global accessor avoids passing the lot reference everywhere while guaranteeing one source of truth for spot availability.

**Why Strategy for pricing?**
Parking fees vary by time-of-day, weekends, vehicle type, or promotion. Wrapping the calculation behind an interface means adding a new scheme (e.g., `PeakHourPricingStrategy`) requires zero changes to `Ticket` or `ParkingLot`.

**Thread safety approach**
- `ParkingFloor` owns a per-floor mutex. Spot assignment and display refresh happen under the same lock, so the display count is always consistent with actual occupancy.
- `ParkingLot` owns a separate mutex for `activeTickets_` to serialize ticket creation/deletion.
- Singleton creation uses double-checked locking with a static mutex.

---

## Interview Q&A

**Q1: How would you handle a vehicle that needs multiple spots (e.g., a very large truck)?**

Add a `slotsRequired()` method to `Vehicle`. In `findAndOccupySpot`, scan for N consecutive free spots of the right type. Atomically mark all of them occupied within the floor mutex. The `Ticket` stores a `vector<ParkingSpot*>` instead of a single pointer, and `vehicleExit` vacates all of them.

**Q2: How does your design scale to thousands of concurrent entries?**

Replace the single `lotMutex_` with per-floor locks (already done) and shard the `activeTickets_` map using a striped lock (e.g., `ticketId % N` bucket index). For very high throughput, move spot assignment to a Redis-backed lock with Lua scripts so multiple application instances can share state.

**Q3: The pricing strategy is set at startup. How would you support dynamic pricing changes at runtime?**

Wrap `pricing_` in a `std::shared_ptr` and use a `std::atomic<std::shared_ptr<PricingStrategy>>` (C++20) or an `std::shared_mutex` reader-writer lock. Threads holding a local copy of the shared_ptr finish their calculation against the old strategy while a new one is swapped in. This avoids blocking reads during strategy updates.
