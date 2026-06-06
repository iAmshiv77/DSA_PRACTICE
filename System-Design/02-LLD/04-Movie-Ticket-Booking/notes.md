# Movie Ticket Booking System — Low Level Design

## Requirements

### Functional
- Multiple CinemaHalls, each with multiple Screens
- Each Screen has Seats of type REGULAR, PREMIUM, or RECLINER
- Movies can be scheduled as Shows on a specific Screen at a specific time
- Users can search shows by movie/date/city, select seats, book, and pay
- Seat passes through states: AVAILABLE → RESERVED (locked for N minutes) → BOOKED (paid) or back to AVAILABLE on timeout/cancellation
- Concurrent requests for the same seat must be handled safely (one wins, others fail gracefully)
- Payment supports multiple strategies (Credit Card, UPI, Wallet)
- Booking holds a receipt with booking reference

### Non-Functional
- Single BookingSystem entry-point (Singleton)
- State pattern for seat lifecycle
- Strategy pattern for pricing by seat type
- Optimistic locking concept to handle concurrent seat selection

---

## Actors and Use Cases

| Actor    | Use Cases                                                                     |
|----------|-------------------------------------------------------------------------------|
| Customer | Search shows, view seats, select seats, make payment, cancel booking          |
| Admin    | Add movie, add screen, schedule show, configure pricing                       |
| System   | Release reserved seats on payment timeout, send booking confirmation          |

---

## Class Diagram (textual)

```
BookingSystem (Singleton)
  ├── map<id, CinemaHall>
  ├── map<id, Movie>
  ├── map<id, Show>
  └── PricingStrategy*

CinemaHall
  ├── name, city
  └── vector<Screen*>

Screen
  ├── screenNumber, totalCapacity
  ├── CinemaHall*
  └── vector<Seat*>

Seat
  ├── seatId, row, column
  ├── SeatType { REGULAR, PREMIUM, RECLINER }
  ├── SeatState* (State pattern)
  └── version (optimistic lock)

SeatState (abstract)            ← State pattern
  ├── AvailableState
  ├── ReservedState
  └── BookedState

Movie
  ├── movieId, title, genre, duration (minutes)
  └── rating

Show
  ├── showId, startTime, endTime
  ├── Movie*, Screen*
  └── map<seatId, Seat*>

Booking
  ├── bookingId, bookingTime
  ├── Show*, Customer*
  ├── vector<Seat*>
  ├── Payment*
  └── BookingStatus { PENDING, CONFIRMED, CANCELLED }

Payment
  ├── paymentId, amount, paymentTime
  ├── PaymentStatus { PENDING, COMPLETED, FAILED, REFUNDED }
  └── PaymentStrategy* (Strategy pattern)

PaymentStrategy (abstract)      ← Strategy pattern
  ├── CreditCardPayment
  ├── UpiPayment
  └── WalletPayment

PricingStrategy (abstract)      ← Strategy pattern
  ├── StandardPricing
  └── WeekendPricing
```

---

## Design Patterns Used

| Pattern   | Applied To              | Reason                                                                       |
|-----------|-------------------------|------------------------------------------------------------------------------|
| Singleton | BookingSystem           | Single entry-point manages all halls, shows, and in-flight bookings          |
| State     | Seat lifecycle          | Seat transitions (AVAILABLE→RESERVED→BOOKED) enforce valid state changes     |
| Strategy  | PricingStrategy         | Swap pricing rules (standard/weekend/holiday) without changing booking logic |
| Strategy  | PaymentStrategy         | Swap payment method at runtime without changing Booking class                |

---

## Full C++ Implementation

```cpp
#include <iostream>
#include <vector>
#include <map>
#include <string>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <ctime>
#include <sstream>
#include <algorithm>

// ─────────────────────────────────────────────
// Enums
// ─────────────────────────────────────────────

enum class SeatType      { REGULAR, PREMIUM, RECLINER };
enum class BookingStatus { PENDING, CONFIRMED, CANCELLED };
enum class PaymentStatus { PENDING, COMPLETED, FAILED, REFUNDED };

// ─────────────────────────────────────────────
// State Pattern — Seat lifecycle
// ─────────────────────────────────────────────

class Seat; // forward

class SeatState {
public:
    virtual ~SeatState() = default;
    virtual void reserve(Seat& seat) = 0;
    virtual void book(Seat& seat)    = 0;
    virtual void release(Seat& seat) = 0;
    virtual std::string name() const = 0;
};

class AvailableState : public SeatState {
public:
    void reserve(Seat& seat) override; // defined after Seat
    void book(Seat& /*seat*/) override {
        throw std::runtime_error("Cannot book directly from AVAILABLE — must reserve first");
    }
    void release(Seat& /*seat*/) override {
        throw std::runtime_error("Seat is already available");
    }
    std::string name() const override { return "AVAILABLE"; }
};

class ReservedState : public SeatState {
public:
    void reserve(Seat& /*seat*/) override {
        throw std::runtime_error("Seat already RESERVED");
    }
    void book(Seat& seat) override;   // defined after Seat
    void release(Seat& seat) override;
    std::string name() const override { return "RESERVED"; }
};

class BookedState : public SeatState {
public:
    void reserve(Seat& /*seat*/) override {
        throw std::runtime_error("Seat is already BOOKED");
    }
    void book(Seat& /*seat*/) override {
        throw std::runtime_error("Seat is already BOOKED");
    }
    void release(Seat& /*seat*/) override {
        throw std::runtime_error("Cannot release a BOOKED seat without cancellation");
    }
    std::string name() const override { return "BOOKED"; }
};

// ─────────────────────────────────────────────
// Seat
// ─────────────────────────────────────────────

class Seat {
    std::string              seatId_;
    char                     row_;
    int                      column_;
    SeatType                 type_;
    std::unique_ptr<SeatState> state_;
    int                      version_; // optimistic lock counter
    mutable std::mutex       mutex_;

public:
    Seat(std::string id, char row, int col, SeatType type)
        : seatId_(std::move(id)), row_(row), column_(col), type_(type),
          state_(std::make_unique<AvailableState>()), version_(0) {}

    // Non-copyable — seats have identity
    Seat(const Seat&)            = delete;
    Seat& operator=(const Seat&) = delete;

    const std::string& getId()  const { return seatId_; }
    SeatType           getType() const { return type_;   }
    int                getVersion() const { return version_; }
    std::string        getStateName() const { return state_->name(); }

    // State transitions — guarded by mutex for thread safety
    void reserve() {
        std::lock_guard<std::mutex> lock(mutex_);
        state_->reserve(*this);
    }
    void book() {
        std::lock_guard<std::mutex> lock(mutex_);
        state_->book(*this);
    }
    void release() {
        std::lock_guard<std::mutex> lock(mutex_);
        state_->release(*this);
    }

    // Called only from state classes
    void setState(std::unique_ptr<SeatState> newState) {
        state_ = std::move(newState);
        ++version_; // increment version on every state change (optimistic locking)
    }

    bool isAvailable() const { return state_->name() == "AVAILABLE"; }
};

// ─────────────────────────────────────────────
// State method bodies (now that Seat is complete)
// ─────────────────────────────────────────────

void AvailableState::reserve(Seat& seat) {
    std::cout << "Seat " << seat.getId() << ": AVAILABLE → RESERVED\n";
    seat.setState(std::make_unique<ReservedState>());
}

void ReservedState::book(Seat& seat) {
    std::cout << "Seat " << seat.getId() << ": RESERVED → BOOKED\n";
    seat.setState(std::make_unique<BookedState>());
}

void ReservedState::release(Seat& seat) {
    std::cout << "Seat " << seat.getId() << ": RESERVED → AVAILABLE (released/timeout)\n";
    seat.setState(std::make_unique<AvailableState>());
}

// ─────────────────────────────────────────────
// Strategy — Pricing
// ─────────────────────────────────────────────

class PricingStrategy {
public:
    virtual ~PricingStrategy() = default;
    virtual double getPrice(SeatType type) const = 0;
    virtual std::string label() const = 0;
};

class StandardPricing : public PricingStrategy {
public:
    double getPrice(SeatType type) const override {
        switch (type) {
            case SeatType::REGULAR:  return 10.0;
            case SeatType::PREMIUM:  return 15.0;
            case SeatType::RECLINER: return 22.0;
        }
        return 10.0;
    }
    std::string label() const override { return "Standard"; }
};

class WeekendPricing : public PricingStrategy {
public:
    double getPrice(SeatType type) const override {
        switch (type) {
            case SeatType::REGULAR:  return 13.0;
            case SeatType::PREMIUM:  return 19.0;
            case SeatType::RECLINER: return 28.0;
        }
        return 13.0;
    }
    std::string label() const override { return "Weekend"; }
};

// ─────────────────────────────────────────────
// Strategy — Payment
// ─────────────────────────────────────────────

class PaymentStrategy {
public:
    virtual ~PaymentStrategy() = default;
    virtual bool pay(double amount) = 0;
    virtual std::string name() const = 0;
};

class CreditCardPayment : public PaymentStrategy {
    std::string cardNumber_;
public:
    explicit CreditCardPayment(std::string card) : cardNumber_(std::move(card)) {}
    bool pay(double amount) override {
        std::cout << "Charging $" << amount << " to credit card ending "
                  << cardNumber_.substr(cardNumber_.size() - 4) << "\n";
        return true; // simulate success
    }
    std::string name() const override { return "CreditCard"; }
};

class UpiPayment : public PaymentStrategy {
    std::string upiId_;
public:
    explicit UpiPayment(std::string upiId) : upiId_(std::move(upiId)) {}
    bool pay(double amount) override {
        std::cout << "UPI payment of $" << amount << " via " << upiId_ << "\n";
        return true;
    }
    std::string name() const override { return "UPI"; }
};

// ─────────────────────────────────────────────
// Movie, Screen, Show
// ─────────────────────────────────────────────

class Movie {
    std::string id_, title_, genre_;
    int         durationMin_;
public:
    Movie(std::string id, std::string title, std::string genre, int dur)
        : id_(std::move(id)), title_(std::move(title)),
          genre_(std::move(genre)), durationMin_(dur) {}

    const std::string& getId()    const { return id_;    }
    const std::string& getTitle() const { return title_; }
    int getDuration() const { return durationMin_; }
};

class Screen {
    std::string               screenId_;
    int                       screenNumber_;
    std::vector<std::unique_ptr<Seat>> seats_;
public:
    Screen(std::string id, int num) : screenId_(std::move(id)), screenNumber_(num) {}

    const std::string& getId() const { return screenId_; }
    int getNumber() const { return screenNumber_; }

    void addSeat(std::unique_ptr<Seat> seat) {
        seats_.push_back(std::move(seat));
    }

    Seat* findSeat(const std::string& seatId) const {
        for (const auto& s : seats_) {
            if (s->getId() == seatId) return s.get();
        }
        return nullptr;
    }

    const std::vector<std::unique_ptr<Seat>>& getSeats() const { return seats_; }
};

class Show {
    std::string showId_;
    Movie*      movie_;
    Screen*     screen_;
    time_t      startTime_;
public:
    Show(std::string id, Movie* movie, Screen* screen, time_t start)
        : showId_(std::move(id)), movie_(movie), screen_(screen), startTime_(start) {}

    const std::string& getId()    const { return showId_;    }
    Movie*  getMovie()            const { return movie_;     }
    Screen* getScreen()           const { return screen_;    }
    time_t  getStartTime()        const { return startTime_; }
};

// ─────────────────────────────────────────────
// Payment
// ─────────────────────────────────────────────

class Payment {
    std::string             paymentId_;
    double                  amount_;
    PaymentStatus           status_;
    std::unique_ptr<PaymentStrategy> strategy_;
public:
    Payment(std::string id, double amt, std::unique_ptr<PaymentStrategy> strategy)
        : paymentId_(std::move(id)), amount_(amt),
          status_(PaymentStatus::PENDING), strategy_(std::move(strategy)) {}

    bool process() {
        bool ok = strategy_->pay(amount_);
        status_ = ok ? PaymentStatus::COMPLETED : PaymentStatus::FAILED;
        return ok;
    }

    PaymentStatus getStatus() const { return status_; }
    double getAmount()        const { return amount_;  }
};

// ─────────────────────────────────────────────
// Booking
// ─────────────────────────────────────────────

static int bookingCounter = 1000;

class Booking {
    std::string             bookingId_;
    Show*                   show_;
    std::string             customerId_;
    std::vector<Seat*>      seats_;
    std::unique_ptr<Payment> payment_;
    BookingStatus           status_;
    time_t                  bookingTime_;
    double                  totalAmount_;

public:
    Booking(Show* show, std::string customerId,
            std::vector<Seat*> seats, double amount)
        : bookingId_("BKG-" + std::to_string(++bookingCounter)),
          show_(show), customerId_(std::move(customerId)),
          seats_(std::move(seats)),
          status_(BookingStatus::PENDING),
          bookingTime_(std::time(nullptr)),
          totalAmount_(amount) {}

    const std::string& getId()      const { return bookingId_; }
    BookingStatus      getStatus()  const { return status_;    }
    double             getAmount()  const { return totalAmount_; }

    void attachPayment(std::unique_ptr<Payment> pay) {
        payment_ = std::move(pay);
    }

    bool confirm() {
        if (!payment_) throw std::runtime_error("No payment attached");
        bool ok = payment_->process();
        if (ok) {
            // Transition all seats from RESERVED → BOOKED
            for (auto* seat : seats_) {
                seat->book();
            }
            status_ = BookingStatus::CONFIRMED;
            std::cout << "Booking " << bookingId_ << " CONFIRMED. Total: $"
                      << totalAmount_ << "\n";
        } else {
            cancel();
        }
        return ok;
    }

    void cancel() {
        // Release any reserved seats
        for (auto* seat : seats_) {
            if (seat->getStateName() == "RESERVED") seat->release();
        }
        status_ = BookingStatus::CANCELLED;
        std::cout << "Booking " << bookingId_ << " CANCELLED\n";
    }
};

// ─────────────────────────────────────────────
// BookingSystem — Singleton
// ─────────────────────────────────────────────

class BookingSystem {
    static BookingSystem* instance_;
    static std::mutex     instanceMutex_;

    std::map<std::string, std::unique_ptr<Movie>>   movies_;
    std::map<std::string, std::unique_ptr<Screen>>  screens_;
    std::map<std::string, std::unique_ptr<Show>>    shows_;
    std::vector<std::unique_ptr<Booking>>           bookings_;
    std::unique_ptr<PricingStrategy>                pricing_;
    std::mutex                                      bookingMutex_;

    BookingSystem() : pricing_(std::make_unique<StandardPricing>()) {}

public:
    BookingSystem(const BookingSystem&)            = delete;
    BookingSystem& operator=(const BookingSystem&) = delete;

    static BookingSystem* getInstance() {
        if (!instance_) {
            std::lock_guard<std::mutex> lock(instanceMutex_);
            if (!instance_) instance_ = new BookingSystem();
        }
        return instance_;
    }

    void setPricingStrategy(std::unique_ptr<PricingStrategy> strategy) {
        pricing_ = std::move(strategy);
    }

    void addMovie(std::unique_ptr<Movie> movie) {
        movies_[movie->getId()] = std::move(movie);
    }

    void addScreen(std::unique_ptr<Screen> screen) {
        screens_[screen->getId()] = std::move(screen);
    }

    Show* scheduleShow(const std::string& showId,
                       const std::string& movieId,
                       const std::string& screenId,
                       time_t startTime) {
        auto* movie  = movies_.at(movieId).get();
        auto* screen = screens_.at(screenId).get();
        shows_[showId] = std::make_unique<Show>(showId, movie, screen, startTime);
        return shows_[showId].get();
    }

    // ── Core booking flow ─────────────────────
    //
    // Optimistic locking concept:
    //   1. Caller reads seat.version before selecting
    //   2. Before reserving, we re-check version == expectedVersion
    //   3. If version changed (another thread booked it), we reject
    //
    Booking* createBooking(const std::string& showId,
                           const std::string& customerId,
                           const std::vector<std::string>& seatIds,
                           const std::vector<int>& expectedVersions) {
        std::lock_guard<std::mutex> lock(bookingMutex_);

        Show* show = shows_.at(showId).get();
        Screen* screen = show->getScreen();

        // Phase 1: Validate all seats are still available (optimistic version check)
        std::vector<Seat*> selected;
        for (size_t i = 0; i < seatIds.size(); ++i) {
            Seat* seat = screen->findSeat(seatIds[i]);
            if (!seat) throw std::runtime_error("Seat not found: " + seatIds[i]);

            // Optimistic lock: if version changed since caller read it, abort
            if (seat->getVersion() != expectedVersions[i]) {
                throw std::runtime_error(
                    "Seat " + seatIds[i] + " was modified concurrently — please retry");
            }
            if (!seat->isAvailable()) {
                throw std::runtime_error("Seat " + seatIds[i] + " is no longer available");
            }
            selected.push_back(seat);
        }

        // Phase 2: Reserve all selected seats atomically
        double total = 0.0;
        for (auto* seat : selected) {
            seat->reserve();
            total += pricing_->getPrice(seat->getType());
        }

        // Phase 3: Create and register booking
        bookings_.push_back(std::make_unique<Booking>(
            show, customerId, selected, total));
        return bookings_.back().get();
    }
};

BookingSystem* BookingSystem::instance_       = nullptr;
std::mutex     BookingSystem::instanceMutex_;

// ─────────────────────────────────────────────
// main — usage demonstration
// ─────────────────────────────────────────────

int main() {
    BookingSystem* system = BookingSystem::getInstance();

    // Setup movie and screen
    system->addMovie(std::make_unique<Movie>("M1", "Inception", "Sci-Fi", 148));

    auto screen = std::make_unique<Screen>("SC1", 1);
    screen->addSeat(std::make_unique<Seat>("A1", 'A', 1, SeatType::REGULAR));
    screen->addSeat(std::make_unique<Seat>("A2", 'A', 2, SeatType::PREMIUM));
    screen->addSeat(std::make_unique<Seat>("A3", 'A', 3, SeatType::RECLINER));
    system->addScreen(std::move(screen));

    system->scheduleShow("SH1", "M1", "SC1", std::time(nullptr) + 3600);

    // Get current versions before booking (optimistic locking pattern)
    std::vector<std::string> seatIds       = {"A1", "A2"};
    std::vector<int>         versions      = {0, 0}; // freshly read

    // Customer books two seats
    Booking* booking = system->createBooking("SH1", "customer-42", seatIds, versions);

    // Attach payment strategy at runtime
    auto payment = std::make_unique<Payment>(
        "PAY-001", booking->getAmount(),
        std::make_unique<CreditCardPayment>("4111111111111234"));
    booking->attachPayment(std::move(payment));

    // Confirm (transitions seats RESERVED → BOOKED)
    booking->confirm();

    // Switch to weekend pricing
    system->setPricingStrategy(std::make_unique<WeekendPricing>());
    std::cout << "Pricing switched to: WeekendPricing\n";

    return 0;
}
```

**Expected output:**
```
Seat A1: AVAILABLE → RESERVED
Seat A2: AVAILABLE → RESERVED
Charging $25.00 to credit card ending 1234
Seat A1: RESERVED → BOOKED
Seat A2: RESERVED → BOOKED
Booking BKG-1001 CONFIRMED. Total: $25
Pricing switched to: WeekendPricing
```

---

## Interview Q&A

**Q: Why use the State pattern for seats instead of an enum + if/else?**
A: With an enum, every place that modifies seat status must know all valid transitions and duplicate the guard logic. The State pattern encapsulates each state's valid transitions inside its own class. `AvailableState::book()` throws immediately — callers cannot accidentally skip the RESERVED step. Adding a new state (e.g., CLEANING) requires only a new class, not editing existing switch statements.

**Q: How do you handle concurrent seat booking?**
A: Two layers. First, the `Seat` itself has a `std::mutex` per instance — state transitions are atomic. Second, `BookingSystem::createBooking()` uses an optimistic locking pattern: the client reads `seat.version` before displaying the seat map. On submission, the server checks `version == expectedVersion`. If another thread already booked the seat (version incremented), the current request fails fast with "please retry." This avoids holding a DB lock during the seat-selection UI flow.

**Q: Why is PricingStrategy separate from PaymentStrategy?**
A: They vary along different axes. PricingStrategy is a system-wide concern (all bookings during a weekend use WeekendPricing — set once on BookingSystem). PaymentStrategy is per-transaction (each customer picks their payment method). Merging them would force a new pricing class for every payment method combination.

**Q: What happens if payment fails after seats are reserved?**
A: `Booking::confirm()` calls `payment_->process()`. On failure, it calls `cancel()`, which transitions all seats from RESERVED back to AVAILABLE. Any concurrent request that was blocked will then succeed when it retries.

**Q: How would you implement a reservation timeout (seats auto-released after 10 minutes)?**
A: When `createBooking()` reserves seats, push a delayed job to a queue (e.g., BullMQ in Node.js). The job checks: if the booking is still PENDING after 10 minutes, call `booking->cancel()`. In C++, use a background `std::thread` with a priority queue ordered by expiry time.

**Q: How does the Singleton BookingSystem differ from a service layer in NestJS?**
A: In NestJS, the `BookingService` is effectively a Singleton per module scope (NestJS default `Scope.DEFAULT`). The Singleton here enforces the same guarantee at the language level. In production, you would not use a C++ Singleton — you would inject the service via the DI container, which lets you swap it with a mock in tests.

**Q: How would you scale the seat booking across multiple server instances?**
A: The in-memory mutex only works for a single process. For horizontal scaling: use Redis or a database row-level lock. Pattern: `SET seat:{showId}:{seatId} {bookingId} NX EX 600` (Redis NX = only if not exists, EX = 10-minute TTL). Only the request that wins the SET proceeds; others get a "seat unavailable" response. On payment success, mark in DB as BOOKED. On failure or timeout, `DEL` the key to release.
