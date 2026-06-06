# LLD: Hotel Booking System (like Booking.com / OYO)

## Requirements
- Search hotels by location, dates, guests
- View room types with availability
- Book a room (reserve dates)
- Cancel booking (with refund policy)
- Multiple room types per hotel
- Reviews and ratings

## Key Classes

```
HotelBookingSystem (Singleton / Facade)
├── HotelManager
│   └── Hotel
│       ├── Room (RoomType enum)
│       └── RoomAvailability
├── BookingManager
│   └── Booking (state machine)
├── SearchService
├── PricingEngine (Strategy)
│   ├── StandardPricing
│   └── DynamicPricing (surge based on demand)
└── PaymentService (Strategy)
```

## Patterns Used
- **Singleton**: HotelBookingSystem
- **Strategy**: PricingEngine (fixed vs dynamic pricing)
- **State**: Booking (PENDING → CONFIRMED → CHECKED_IN → CHECKED_OUT / CANCELLED)
- **Observer**: Notify guest on booking status change

## C++ Implementation

```cpp
#include <iostream>
#include <vector>
#include <unordered_map>
#include <map>
#include <memory>
#include <string>
#include <chrono>
#include <stdexcept>
#include <optional>
#include <algorithm>
using namespace std;

// ── Date (simplified) ─────────────────────────────────────────────────────────
struct Date {
    int year, month, day;

    bool operator<(const Date& o) const {
        if (year != o.year)  return year < o.year;
        if (month != o.month) return month < o.month;
        return day < o.day;
    }
    bool operator<=(const Date& o) const { return !(o < *this); }
    bool operator==(const Date& o) const { return !(*this < o) && !(o < *this); }

    int daysUntil(const Date& o) const {
        // simplified: assume same month
        return (o.year - year) * 365 + (o.month - month) * 30 + (o.day - day);
    }

    string str() const {
        return to_string(year) + "-" + to_string(month) + "-" + to_string(day);
    }
};

// ── Enums ─────────────────────────────────────────────────────────────────────
enum class RoomType   { SINGLE, DOUBLE, SUITE, DELUXE };
enum class BookingStatus { PENDING, CONFIRMED, CHECKED_IN, CHECKED_OUT, CANCELLED };

string roomTypeStr(RoomType t) {
    switch(t) {
        case RoomType::SINGLE: return "Single";
        case RoomType::DOUBLE: return "Double";
        case RoomType::SUITE:  return "Suite";
        case RoomType::DELUXE: return "Deluxe";
    }
    return "";
}

// ── Pricing Strategy ──────────────────────────────────────────────────────────
class PricingStrategy {
public:
    virtual double calculatePrice(RoomType type, int nights, Date checkIn) const = 0;
    virtual ~PricingStrategy() = default;
};

class StandardPricing : public PricingStrategy {
    unordered_map<int, double> rates = {
        {(int)RoomType::SINGLE, 2000},
        {(int)RoomType::DOUBLE, 3500},
        {(int)RoomType::SUITE,  8000},
        {(int)RoomType::DELUXE, 5000},
    };
public:
    double calculatePrice(RoomType type, int nights, Date) const override {
        return rates.at((int)type) * nights;
    }
};

class DynamicPricing : public PricingStrategy {
    StandardPricing base;
public:
    double calculatePrice(RoomType type, int nights, Date checkIn) const override {
        double basePrice = base.calculatePrice(type, nights, checkIn);
        // Weekend surcharge
        bool isWeekend = (checkIn.day % 7 == 0 || checkIn.day % 7 == 6);
        double multiplier = isWeekend ? 1.3 : 1.0;
        return basePrice * multiplier;
    }
};

// ── Room ──────────────────────────────────────────────────────────────────────
class Room {
public:
    int roomNumber;
    RoomType type;
    int floor;
    bool hasAC;
    bool hasView;
    // Booked date ranges: sorted list of [checkIn, checkOut]
    vector<pair<Date, Date>> bookings;

    Room(int num, RoomType t, int flr, bool ac, bool view)
        : roomNumber(num), type(t), floor(flr), hasAC(ac), hasView(view) {}

    bool isAvailable(const Date& checkIn, const Date& checkOut) const {
        for (const auto& [bIn, bOut] : bookings) {
            // Overlap if not (checkOut <= bIn || checkIn >= bOut)
            if (!(checkOut <= bIn || checkIn >= bOut)) return false;
        }
        return true;
    }

    void book(const Date& checkIn, const Date& checkOut) {
        bookings.push_back({checkIn, checkOut});
        // Keep sorted for efficiency
        sort(bookings.begin(), bookings.end(),
             [](const auto& a, const auto& b) { return a.first < b.first; });
    }

    void cancelBooking(const Date& checkIn, const Date& checkOut) {
        bookings.erase(
            remove_if(bookings.begin(), bookings.end(),
                [&](const auto& b) { return b.first == checkIn && b.second == checkOut; }),
            bookings.end()
        );
    }
};

// ── Booking (State Machine) ───────────────────────────────────────────────────
class BookingObserver {
public:
    virtual void onStatusChange(int bookingId, BookingStatus status) = 0;
    virtual ~BookingObserver() = default;
};

class GuestNotifier : public BookingObserver {
    string guestName, email;
public:
    GuestNotifier(const string& name, const string& e) : guestName(name), email(e) {}
    void onStatusChange(int bookingId, BookingStatus status) override {
        static unordered_map<int, string> msgs = {
            {(int)BookingStatus::CONFIRMED,   "Your booking is confirmed!"},
            {(int)BookingStatus::CHECKED_IN,  "Welcome! Enjoy your stay."},
            {(int)BookingStatus::CHECKED_OUT, "Thanks for staying! Checkout complete."},
            {(int)BookingStatus::CANCELLED,   "Your booking has been cancelled."},
        };
        auto it = msgs.find((int)status);
        if (it != msgs.end())
            cout << "[Email to " << email << "] " << it->second
                 << " (Booking #" << bookingId << ")\n";
    }
};

struct Booking {
    static int counter;
    int id;
    string guestId;
    int hotelId;
    int roomNumber;
    Date checkIn, checkOut;
    int nights;
    double totalAmount;
    BookingStatus status = BookingStatus::PENDING;
    vector<BookingObserver*> observers;

    Booking(const string& gId, int hId, int room, Date ci, Date co, double amount)
        : id(++counter), guestId(gId), hotelId(hId), roomNumber(room),
          checkIn(ci), checkOut(co), nights(ci.daysUntil(co)), totalAmount(amount) {}

    void addObserver(BookingObserver* obs) { observers.push_back(obs); }

    bool transition(BookingStatus next) {
        static unordered_map<int, vector<int>> valid = {
            {(int)BookingStatus::PENDING,     {(int)BookingStatus::CONFIRMED, (int)BookingStatus::CANCELLED}},
            {(int)BookingStatus::CONFIRMED,   {(int)BookingStatus::CHECKED_IN, (int)BookingStatus::CANCELLED}},
            {(int)BookingStatus::CHECKED_IN,  {(int)BookingStatus::CHECKED_OUT}},
        };
        auto& allowed = valid[(int)status];
        if (find(allowed.begin(), allowed.end(), (int)next) == allowed.end()) {
            cout << "Invalid booking status transition\n";
            return false;
        }
        status = next;
        for (auto* obs : observers) obs->onStatusChange(id, status);
        return true;
    }

    double cancellationFee(Date today) const {
        int daysUntilCheckIn = today.daysUntil(checkIn);
        if (daysUntilCheckIn >= 7) return 0;              // free cancellation
        if (daysUntilCheckIn >= 3) return totalAmount * 0.25;  // 25% fee
        return totalAmount * 0.50;                              // 50% fee
    }
};
int Booking::counter = 0;

// ── Hotel ─────────────────────────────────────────────────────────────────────
class Hotel {
public:
    int id;
    string name;
    string city;
    double rating;
    vector<unique_ptr<Room>> rooms;

    Hotel(int i, const string& n, const string& c, double r)
        : id(i), name(n), city(c), rating(r) {}

    int addRoom(RoomType type, int floor, bool ac, bool view) {
        int num = rooms.size() + 101;  // start from 101
        rooms.push_back(make_unique<Room>(num, type, floor, ac, view));
        return num;
    }

    vector<Room*> searchAvailable(RoomType type, const Date& ci, const Date& co) const {
        vector<Room*> result;
        for (const auto& room : rooms) {
            if (room->type == type && room->isAvailable(ci, co))
                result.push_back(room.get());
        }
        return result;
    }

    Room* getRoom(int number) {
        for (auto& r : rooms)
            if (r->roomNumber == number) return r.get();
        return nullptr;
    }
};

// ── Hotel Booking System ──────────────────────────────────────────────────────
class HotelBookingSystem {
    static HotelBookingSystem* instance;
    unordered_map<int, unique_ptr<Hotel>>   hotels;
    unordered_map<int, unique_ptr<Booking>> bookings;
    unique_ptr<PricingStrategy> pricing;
    int hotelCounter = 0;

    HotelBookingSystem() : pricing(make_unique<DynamicPricing>()) {}
public:
    static HotelBookingSystem* getInstance() {
        if (!instance) instance = new HotelBookingSystem();
        return instance;
    }

    int addHotel(const string& name, const string& city, double rating) {
        int id = ++hotelCounter;
        hotels[id] = make_unique<Hotel>(id, name, city, rating);
        return id;
    }

    Hotel* getHotel(int id) {
        auto it = hotels.find(id);
        return (it != hotels.end()) ? it->second.get() : nullptr;
    }

    vector<pair<Hotel*, vector<Room*>>> searchHotels(
        const string& city, RoomType type,
        const Date& checkIn, const Date& checkOut) {

        vector<pair<Hotel*, vector<Room*>>> results;
        for (auto& [id, hotel] : hotels) {
            if (hotel->city != city) continue;
            auto available = hotel->searchAvailable(type, checkIn, checkOut);
            if (!available.empty())
                results.push_back({hotel.get(), available});
        }
        // Sort by rating descending
        sort(results.begin(), results.end(),
             [](const auto& a, const auto& b) { return a.first->rating > b.first->rating; });
        return results;
    }

    optional<int> bookRoom(const string& guestId, int hotelId, int roomNumber,
                            const Date& checkIn, const Date& checkOut,
                            BookingObserver* observer = nullptr) {
        auto* hotel = getHotel(hotelId);
        if (!hotel) { cout << "Hotel not found\n"; return nullopt; }

        auto* room = hotel->getRoom(roomNumber);
        if (!room) { cout << "Room not found\n"; return nullopt; }

        if (!room->isAvailable(checkIn, checkOut)) {
            cout << "Room " << roomNumber << " not available for requested dates\n";
            return nullopt;
        }

        int nights = checkIn.daysUntil(checkOut);
        double amount = pricing->calculatePrice(room->type, nights, checkIn);

        room->book(checkIn, checkOut);

        auto booking = make_unique<Booking>(guestId, hotelId, roomNumber,
                                             checkIn, checkOut, amount);
        if (observer) booking->addObserver(observer);
        booking->transition(BookingStatus::CONFIRMED);

        int bookingId = booking->id;
        bookings[bookingId] = move(booking);

        cout << "✅ Room " << roomNumber << " booked for " << nights << " nights"
             << " | Total: ₹" << amount << " | Booking #" << bookingId << "\n";
        return bookingId;
    }

    bool cancelBooking(int bookingId, const Date& today) {
        auto it = bookings.find(bookingId);
        if (it == bookings.end()) { cout << "Booking not found\n"; return false; }

        auto& booking = it->second;
        double fee = booking->cancellationFee(today);

        if (!booking->transition(BookingStatus::CANCELLED)) return false;

        // Release room
        auto* hotel = getHotel(booking->hotelId);
        if (hotel) {
            auto* room = hotel->getRoom(booking->roomNumber);
            if (room) room->cancelBooking(booking->checkIn, booking->checkOut);
        }

        if (fee > 0)
            cout << "Cancellation fee: ₹" << fee << " (Refund: ₹" << (booking->totalAmount - fee) << ")\n";
        else
            cout << "Full refund: ₹" << booking->totalAmount << "\n";

        return true;
    }

    bool updateStatus(int bookingId, BookingStatus status) {
        auto it = bookings.find(bookingId);
        if (it == bookings.end()) return false;
        return it->second->transition(status);
    }
};
HotelBookingSystem* HotelBookingSystem::instance = nullptr;

// ── Main ──────────────────────────────────────────────────────────────────────
int main() {
    auto* sys = HotelBookingSystem::getInstance();

    // Setup hotel
    int taj = sys->addHotel("Taj Mahal Palace", "Mumbai", 4.8);
    auto* hotel = sys->getHotel(taj);
    hotel->addRoom(RoomType::DOUBLE, 5, true, true);
    hotel->addRoom(RoomType::DOUBLE, 6, true, true);
    hotel->addRoom(RoomType::SUITE,  10, true, true);

    Date ci{2024, 3, 15};
    Date co{2024, 3, 18};  // 3 nights

    // Search available rooms
    auto results = sys->searchHotels("Mumbai", RoomType::DOUBLE, ci, co);
    cout << "\nAvailable hotels:\n";
    for (auto& [hotel, rooms] : results) {
        cout << "  " << hotel->name << " (★" << hotel->rating << ")\n";
        for (auto* room : rooms)
            cout << "    Room " << room->roomNumber << " - " << roomTypeStr(room->type) << "\n";
    }

    // Book room
    GuestNotifier notifier("Aarav Shah", "aarav@example.com");
    auto bookingId = sys->bookRoom("G001", taj, 101, ci, co, &notifier);

    if (bookingId) {
        // Check-in
        sys->updateStatus(*bookingId, BookingStatus::CHECKED_IN);
        // Check-out
        sys->updateStatus(*bookingId, BookingStatus::CHECKED_OUT);
    }

    // Try booking same room (should fail — already booked)
    auto b2 = sys->bookRoom("G002", taj, 101, ci, co);
    if (!b2) cout << "Correctly rejected double-booking\n";

    // Another booking and cancel
    auto b3 = sys->bookRoom("G003", taj, 102, ci, co);
    if (b3) {
        Date today{2024, 3, 10};  // 5 days before check-in (25% fee applies)
        sys->cancelBooking(*b3, today);
    }

    return 0;
}
```

## Interview Q&A

**Q: How do you prevent double booking in a distributed system?**
Optimistic locking: add a `version` field to the room availability record. On booking:
```sql
UPDATE room_availability
SET booked_dates = booked_dates || ?, version = version + 1
WHERE room_id = ? AND version = ? AND NOT overlaps(booked_dates, ?)
```
If 0 rows affected → someone else booked first → retry or show "no longer available". Alternatively, use a distributed lock (Redis `SETNX room:{id}:{date}`) with TTL for the duration of the booking transaction.

**Q: How would you design the pricing for holiday surge?**
DynamicPricingStrategy pulls multipliers from a configuration table: `pricing_rules(hotel_id, date_range, multiplier)`. Rules evaluated in priority order (specific date > day-of-week > season). Rules are cached in Redis with 1-hour TTL. Prices shown to users are pre-computed and stored with a "valid until" timestamp — if user takes too long, recalculate before finalizing booking.
