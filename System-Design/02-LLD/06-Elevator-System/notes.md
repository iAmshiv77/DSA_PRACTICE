# Elevator System — Low Level Design

## Requirements

### Functional
- Multiple elevators in a building
- Passengers request elevator from any floor (hall call) with a direction (UP/DOWN)
- Passengers select destination floor inside elevator (car call)
- Optimal dispatch: minimize wait time and travel distance
- Display current floor and direction inside and outside each elevator
- Door open/close with safety sensor (re-open if obstruction)

### Non-Functional
- Thread-safe: multiple requests arrive concurrently
- Extensible dispatch algorithm (Strategy pattern)
- Each elevator state is independently managed

---

## State Machine — Per Elevator

```
     requestAbove / requestBelow           reachedDestination
  IDLE ──────────────────────────→ MOVING_UP / MOVING_DOWN
   ↑                                          │
   │                                    openDoors()
   │                                          ↓
   └──── allRequestsServiced ──── DOOR_OPEN ─── doorsFullyClosed()
```

States: `IDLE`, `MOVING_UP`, `MOVING_DOWN`, `DOOR_OPEN`

---

## Dispatch Algorithms

### SCAN (Elevator Algorithm — like disk scheduling)
- Elevator moves in one direction, picking up all requests along the way
- Reverses only when no more requests in current direction
- Analogy: a disk read head scans back and forth

### Nearest Car Algorithm
- When a new hall call arrives, assign the elevator with:
  1. Smallest distance to the requested floor
  2. Traveling toward the floor (in the right direction)
  3. Idle elevators as last resort
- Better for sparse traffic; SCAN better for heavy traffic

---

## Key Classes

| Class                  | Responsibility                                                  |
|------------------------|-----------------------------------------------------------------|
| ElevatorSystem         | Entry point; owns all elevators and the dispatcher             |
| Elevator               | State machine; processes car calls; notifies controller        |
| ElevatorController     | Observer; receives state updates; triggers dispatch            |
| DispatchStrategy       | Interface for dispatch algorithms                               |
| ScanDispatchStrategy   | SCAN algorithm implementation                                   |
| NearestCarStrategy     | Nearest-car algorithm implementation                            |
| Request                | Hall call (floor + direction) or car call (destination floor)  |
| Door                   | Open/close with safety sensor                                   |
| Display                | Shows floor number and direction                                |

---

## Design Patterns Used

| Pattern  | Applied To                          | Reason                                                                  |
|----------|-------------------------------------|-------------------------------------------------------------------------|
| Observer | Elevator → ElevatorController       | Elevator notifies controller on every floor arrival / state change      |
| Strategy | DispatchStrategy                    | Swap SCAN for nearest-car or any future algorithm without changing code |
| State    | Elevator internal state             | Enforce valid transitions; door-open state disables movement            |

---

## Full C++ Implementation

```cpp
#include <iostream>
#include <vector>
#include <set>
#include <queue>
#include <memory>
#include <mutex>
#include <algorithm>
#include <functional>
#include <string>

// ─────────────────────────────────────────────
// Enums
// ─────────────────────────────────────────────

enum class Direction    { UP, DOWN, NONE };
enum class ElevatorState{ IDLE, MOVING_UP, MOVING_DOWN, DOOR_OPEN };

inline std::string dirStr(Direction d) {
    switch(d) {
        case Direction::UP:   return "UP";
        case Direction::DOWN: return "DOWN";
        default:              return "NONE";
    }
}
inline std::string stateStr(ElevatorState s) {
    switch(s) {
        case ElevatorState::IDLE:        return "IDLE";
        case ElevatorState::MOVING_UP:   return "MOVING_UP";
        case ElevatorState::MOVING_DOWN: return "MOVING_DOWN";
        case ElevatorState::DOOR_OPEN:   return "DOOR_OPEN";
    }
    return "UNKNOWN";
}

// ─────────────────────────────────────────────
// Request
// ─────────────────────────────────────────────

struct Request {
    int       floor;
    Direction direction; // NONE for car calls (destination is just a floor)
    bool      isHallCall; // true = from hallway panel, false = from inside car

    Request(int f, Direction dir, bool hallCall)
        : floor(f), direction(dir), isHallCall(hallCall) {}
};

// ─────────────────────────────────────────────
// Door
// ─────────────────────────────────────────────

class Door {
    bool isOpen_ = false;
    bool obstructed_ = false;
public:
    void open() {
        isOpen_ = true;
        std::cout << "  [Door] Opened.\n";
    }
    void close() {
        if (obstructed_) {
            std::cout << "  [Door] Obstruction detected — reopening.\n";
            return;
        }
        isOpen_ = false;
        std::cout << "  [Door] Closed.\n";
    }
    void setObstructed(bool v) { obstructed_ = v; }
    bool isOpen() const { return isOpen_; }
};

// ─────────────────────────────────────────────
// Display
// ─────────────────────────────────────────────

class Display {
    int       elevatorId_;
    int       currentFloor_ = 0;
    Direction direction_    = Direction::NONE;
public:
    explicit Display(int id) : elevatorId_(id) {}
    void update(int floor, Direction dir) {
        currentFloor_ = floor;
        direction_    = dir;
        std::cout << "  [Display E" << elevatorId_ << "] Floor " << floor
                  << " " << dirStr(dir) << "\n";
    }
};

// ─────────────────────────────────────────────
// Observer interface
// ─────────────────────────────────────────────

class IElevatorObserver {
public:
    virtual ~IElevatorObserver() = default;
    virtual void onFloorArrival(int elevatorId, int floor, ElevatorState state) = 0;
};

// ─────────────────────────────────────────────
// Elevator
// ─────────────────────────────────────────────

class Elevator {
    int           id_;
    int           currentFloor_ = 0;
    ElevatorState state_        = ElevatorState::IDLE;
    Direction     direction_    = Direction::NONE;

    // SCAN: separate sets for up and down stops
    std::set<int>  upStops_;    // floors to visit while going up
    std::set<int>  downStops_;  // floors to visit while going down

    Door    door_;
    Display display_;

    std::vector<IElevatorObserver*> observers_;
    mutable std::mutex mutex_;

public:
    explicit Elevator(int id) : id_(id), display_(id) {}

    int           getId()           const { return id_;           }
    int           getCurrentFloor() const { return currentFloor_; }
    ElevatorState getState()        const { return state_;        }
    Direction     getDirection()    const { return direction_;     }

    void addObserver(IElevatorObserver* obs) {
        observers_.push_back(obs);
    }

    void addStop(int floor, Direction preferredDir) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (floor > currentFloor_ || preferredDir == Direction::UP) {
            upStops_.insert(floor);
        } else {
            downStops_.insert(floor);
        }
        // If idle, decide direction now
        if (state_ == ElevatorState::IDLE) {
            if (!upStops_.empty()) {
                state_     = ElevatorState::MOVING_UP;
                direction_ = Direction::UP;
            } else if (!downStops_.empty()) {
                state_     = ElevatorState::MOVING_DOWN;
                direction_ = Direction::DOWN;
            }
        }
    }

    // Simulate one "step" of movement — call repeatedly from scheduler
    void step() {
        std::lock_guard<std::mutex> lock(mutex_);

        if (state_ == ElevatorState::DOOR_OPEN) {
            door_.close();
            _updateState();
            return;
        }

        if (state_ == ElevatorState::IDLE) return;

        // Move one floor
        if (state_ == ElevatorState::MOVING_UP) {
            ++currentFloor_;
        } else if (state_ == ElevatorState::MOVING_DOWN) {
            --currentFloor_;
        }

        display_.update(currentFloor_, direction_);
        _notifyObservers();

        // Check if this floor is a stop
        bool shouldStop = false;
        if (state_ == ElevatorState::MOVING_UP && upStops_.count(currentFloor_)) {
            upStops_.erase(currentFloor_);
            shouldStop = true;
        } else if (state_ == ElevatorState::MOVING_DOWN && downStops_.count(currentFloor_)) {
            downStops_.erase(currentFloor_);
            shouldStop = true;
        }

        if (shouldStop) {
            state_ = ElevatorState::DOOR_OPEN;
            door_.open();
            std::cout << "  [Elevator " << id_ << "] Stopped at floor "
                      << currentFloor_ << "\n";
        } else {
            _updateState();
        }
    }

    bool isIdle() const {
        return state_ == ElevatorState::IDLE;
    }

    // Distance heuristic for nearest-car algorithm
    int distanceTo(int floor) const {
        return std::abs(currentFloor_ - floor);
    }

private:
    // Must be called while holding mutex_
    void _updateState() {
        if (state_ == ElevatorState::MOVING_UP) {
            if (upStops_.empty()) {
                // Reverse if there are down stops above current handled
                if (!downStops_.empty()) {
                    state_     = ElevatorState::MOVING_DOWN;
                    direction_ = Direction::DOWN;
                } else {
                    state_     = ElevatorState::IDLE;
                    direction_ = Direction::NONE;
                }
            }
        } else if (state_ == ElevatorState::MOVING_DOWN) {
            if (downStops_.empty()) {
                if (!upStops_.empty()) {
                    state_     = ElevatorState::MOVING_UP;
                    direction_ = Direction::UP;
                } else {
                    state_     = ElevatorState::IDLE;
                    direction_ = Direction::NONE;
                }
            }
        } else if (state_ == ElevatorState::DOOR_OPEN) {
            // After door closes, continue in previous direction or go idle
            if (!upStops_.empty() && !downStops_.empty()) {
                // Resume direction based on which was active
                // (simplified: prefer up)
                state_     = ElevatorState::MOVING_UP;
                direction_ = Direction::UP;
            } else if (!upStops_.empty()) {
                state_     = ElevatorState::MOVING_UP;
                direction_ = Direction::UP;
            } else if (!downStops_.empty()) {
                state_     = ElevatorState::MOVING_DOWN;
                direction_ = Direction::DOWN;
            } else {
                state_     = ElevatorState::IDLE;
                direction_ = Direction::NONE;
            }
        }
    }

    void _notifyObservers() {
        for (auto* obs : observers_) {
            obs->onFloorArrival(id_, currentFloor_, state_);
        }
    }
};

// ─────────────────────────────────────────────
// Strategy — Dispatch Algorithm
// ─────────────────────────────────────────────

class DispatchStrategy {
public:
    virtual ~DispatchStrategy() = default;
    // Returns the index into the elevators vector to assign the request to
    virtual int selectElevator(const std::vector<std::unique_ptr<Elevator>>& elevators,
                                const Request& request) const = 0;
};

class NearestCarStrategy : public DispatchStrategy {
public:
    int selectElevator(const std::vector<std::unique_ptr<Elevator>>& elevators,
                       const Request& request) const override {
        int bestIdx      = 0;
        int bestScore    = INT_MAX;

        for (int i = 0; i < static_cast<int>(elevators.size()); ++i) {
            const auto& elev = elevators[i];
            int dist  = elev->distanceTo(request.floor);
            int score = dist;

            // Prefer idle elevators (add 0 penalty) over moving (add small penalty)
            // Prefer elevator already moving in the right direction (subtract penalty)
            if (elev->isIdle()) {
                score -= 5; // bonus for idle
            } else if (elev->getDirection() == request.direction) {
                score -= 2; // bonus for same direction
            }

            if (score < bestScore) {
                bestScore = score;
                bestIdx   = i;
            }
        }
        return bestIdx;
    }
};

class ScanDispatchStrategy : public DispatchStrategy {
public:
    int selectElevator(const std::vector<std::unique_ptr<Elevator>>& elevators,
                       const Request& request) const override {
        // Find elevator moving toward request in the same direction
        // that will pass through the requested floor
        for (int i = 0; i < static_cast<int>(elevators.size()); ++i) {
            const auto& elev = elevators[i];
            if (elev->getDirection() == request.direction) {
                bool inPath = (request.direction == Direction::UP &&
                               elev->getCurrentFloor() <= request.floor) ||
                              (request.direction == Direction::DOWN &&
                               elev->getCurrentFloor() >= request.floor);
                if (inPath) return i;
            }
        }
        // Fallback: nearest idle
        int bestIdx  = 0;
        int bestDist = INT_MAX;
        for (int i = 0; i < static_cast<int>(elevators.size()); ++i) {
            if (elevators[i]->isIdle()) {
                int d = elevators[i]->distanceTo(request.floor);
                if (d < bestDist) { bestDist = d; bestIdx = i; }
            }
        }
        return bestIdx;
    }
};

// ─────────────────────────────────────────────
// ElevatorController — Observer
// ─────────────────────────────────────────────

class ElevatorController : public IElevatorObserver {
public:
    void onFloorArrival(int elevatorId, int floor, ElevatorState state) override {
        std::cout << "[Controller] Elevator " << elevatorId
                  << " arrived at floor " << floor
                  << " state=" << stateStr(state) << "\n";
        // In a real system: update a central state map,
        // re-evaluate pending hall calls, update building displays
    }
};

// ─────────────────────────────────────────────
// ElevatorSystem
// ─────────────────────────────────────────────

class ElevatorSystem {
    std::vector<std::unique_ptr<Elevator>> elevators_;
    ElevatorController                     controller_;
    std::unique_ptr<DispatchStrategy>      dispatchStrategy_;

public:
    explicit ElevatorSystem(int numElevators,
                            std::unique_ptr<DispatchStrategy> strategy)
        : dispatchStrategy_(std::move(strategy)) {
        for (int i = 0; i < numElevators; ++i) {
            auto elev = std::make_unique<Elevator>(i);
            elev->addObserver(&controller_);
            elevators_.push_back(std::move(elev));
        }
    }

    void setDispatchStrategy(std::unique_ptr<DispatchStrategy> s) {
        dispatchStrategy_ = std::move(s);
    }

    // Hall call: someone on floor F presses UP or DOWN
    void hallCall(int floor, Direction direction) {
        int idx = dispatchStrategy_->selectElevator(elevators_,
                      Request(floor, direction, true));
        std::cout << "[System] Hall call floor=" << floor
                  << " dir=" << dirStr(direction)
                  << " → assigned to Elevator " << idx << "\n";
        elevators_[idx]->addStop(floor, direction);
    }

    // Car call: passenger inside elevator presses destination floor
    void carCall(int elevatorId, int destinationFloor) {
        if (elevatorId < 0 || elevatorId >= static_cast<int>(elevators_.size())) return;
        std::cout << "[System] Car call Elevator " << elevatorId
                  << " → floor " << destinationFloor << "\n";
        elevators_[elevatorId]->addStop(destinationFloor, Direction::NONE);
    }

    // Advance simulation by N steps
    void simulate(int steps) {
        for (int s = 0; s < steps; ++s) {
            std::cout << "-- Step " << s + 1 << " --\n";
            for (auto& elev : elevators_) {
                elev->step();
            }
        }
    }

    void printStatus() const {
        for (auto& elev : elevators_) {
            std::cout << "Elevator " << elev->getId()
                      << " floor=" << elev->getCurrentFloor()
                      << " state=" << stateStr(elev->getState()) << "\n";
        }
    }
};

// ─────────────────────────────────────────────
// Demo
// ─────────────────────────────────────────────

int main() {
    ElevatorSystem system(2, std::make_unique<NearestCarStrategy>());

    system.printStatus();

    // Person on floor 3 wants to go up
    system.hallCall(3, Direction::UP);
    // Person on floor 7 wants to go down
    system.hallCall(7, Direction::DOWN);

    system.simulate(4);

    // Passengers inside press their destinations
    system.carCall(0, 8);
    system.carCall(1, 2);

    system.simulate(6);
    system.printStatus();
    return 0;
}
```

---

## Key Design Decisions

**SCAN vs Nearest-Car**

SCAN provides better throughput under heavy load because it amortizes the cost of direction changes — every floor in the sweep benefits. Nearest-Car gives lower average wait times in sparse traffic because an idle elevator races to the caller instead of completing its current sweep. The Strategy pattern lets the system switch algorithms based on time-of-day or occupancy without touching the Elevator class.

**Per-elevator mutex vs single system mutex**

Each `Elevator` owns its own mutex so multiple elevators can step concurrently. The `ElevatorSystem::hallCall` reads from elevators under their individual locks during `distanceTo` and `getDirection`, which is safe because those are read-only snapshots. Assignment (`addStop`) takes the lock on the selected elevator only.

**Why Observer for controller notification?**

The elevator does not need to know about the controller's internal logic (e.g., updating a building display matrix, logging, or rescheduling pending calls). Keeping this a callback relationship means adding a new observer (e.g., a fire-alarm system that overrides normal dispatch) requires zero changes to the Elevator class.

---

## Interview Q&A

**Q1: How would you handle an emergency (fire alarm) that requires all elevators to go to ground floor?**

Add an `EmergencyMode` flag in `ElevatorSystem`. When triggered: clear all stops in every elevator, add stop 0 to every elevator's `upStops_`/`downStops_` appropriately, disable new hall/car calls, and ignore the normal dispatch strategy. This is a direct control override; the State machine still handles door safety. After all elevators report `IDLE` at floor 0, the flag can be cleared.

**Q2: Your SCAN strategy assigns a request to one elevator, but what if that elevator breaks down mid-trip?**

Each Elevator should publish a `FAULT` state change through the Observer interface. The Controller detects the fault, reads the elevator's pending stops, and re-issues them as new hall calls through the dispatch algorithm. The reassignment is transparent to passengers waiting. To minimize re-dispatch storms, use exponential backoff before marking an elevator faulted (brief door-open delays should not trigger re-dispatch).

**Q3: How would you extend this to support VIP floors that only certain elevators can service?**

Add a `Set<int> allowedFloors` to `Elevator`. Modify `DispatchStrategy::selectElevator` to skip elevators whose `allowedFloors` does not include the requested floor. If no elevator can serve the floor, queue the request and wait for one to become available. This is an instance of the Service Pool pattern — restricting which workers can handle which job types.
