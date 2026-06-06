# Behavioral Design Patterns

Behavioral patterns are concerned with algorithms and the assignment of responsibilities between objects. They describe how objects communicate and collaborate.

---

# Chain of Responsibility

## Intent
Pass a request along a chain of handlers. Each handler decides to process the request or pass it to the next handler in the chain.

## When to Use
- More than one object may handle a request, and the handler is not known a priori
- You want to issue a request to one of several handlers without specifying the receiver explicitly
- The set of handlers should be specifiable dynamically

## C++ Implementation

```cpp
#include <iostream>
#include <string>
#include <memory>

// ── Handler interface ──────────────────────────────────────────────────────
enum class LogLevel { DEBUG = 1, INFO = 2, WARNING = 3, ERROR = 4 };

class Logger {
protected:
    LogLevel                  level_;
    std::unique_ptr<Logger>   next_;
public:
    explicit Logger(LogLevel level) : level_(level) {}
    virtual ~Logger() = default;

    Logger* setNext(std::unique_ptr<Logger> next) {
        next_ = std::move(next);
        return next_.get();
    }

    void handle(LogLevel msgLevel, const std::string& message) {
        if (msgLevel >= level_) {
            write(message);
        }
        if (next_) {
            next_->handle(msgLevel, message);
        }
    }

protected:
    virtual void write(const std::string& message) = 0;
};

// ── Concrete handlers ──────────────────────────────────────────────────────
class ConsoleLogger : public Logger {
public:
    explicit ConsoleLogger(LogLevel level) : Logger(level) {}
protected:
    void write(const std::string& msg) override {
        std::cout << "[Console] " << msg << "\n";
    }
};

class FileLogger : public Logger {
public:
    explicit FileLogger(LogLevel level) : Logger(level) {}
protected:
    void write(const std::string& msg) override {
        std::cout << "[File]    " << msg << "\n";
    }
};

class EmailLogger : public Logger {
public:
    explicit EmailLogger(LogLevel level) : Logger(level) {}
protected:
    void write(const std::string& msg) override {
        std::cout << "[Email]   ALERT: " << msg << "\n";
    }
};

int main() {
    // Build chain: Console → File → Email
    auto console = std::make_unique<ConsoleLogger>(LogLevel::DEBUG);
    auto file    = std::make_unique<FileLogger>(LogLevel::WARNING);
    auto email   = std::make_unique<EmailLogger>(LogLevel::ERROR);

    auto* filePtr  = console->setNext(std::move(file));
    filePtr->setNext(std::move(email));

    std::cout << "-- DEBUG message --\n";
    console->handle(LogLevel::DEBUG, "Variable x = 42");         // Console only

    std::cout << "-- WARNING message --\n";
    console->handle(LogLevel::WARNING, "Low memory");            // Console + File

    std::cout << "-- ERROR message --\n";
    console->handle(LogLevel::ERROR, "Database connection lost"); // Console + File + Email
}
```

## Real-World Use Cases
- **NestJS**: Middleware pipeline — each middleware calls `next()` or short-circuits. Guards, interceptors, and exception filters form a CoR chain around every request
- **Node.js**: `express` middleware chain (`app.use(fn)`) is a textbook Chain of Responsibility
- **Browser**: DOM event bubbling — event travels up the parent chain until a handler calls `stopPropagation()`

---

# Command

## Intent
Encapsulate a request as an object, allowing parameterization, queuing, logging, and undo/redo of requests.

## When to Use
- You want to parameterize objects with operations
- You want to queue operations, schedule their execution, or execute them remotely
- You need undoable operations

## C++ Implementation

```cpp
#include <iostream>
#include <string>
#include <memory>
#include <stack>
#include <vector>

// ── Receiver ───────────────────────────────────────────────────────────────
class TextEditor {
    std::string text_;
public:
    void insertText(const std::string& str, size_t pos) {
        text_.insert(pos, str);
    }
    void deleteText(size_t pos, size_t len) {
        text_.erase(pos, len);
    }
    const std::string& getText() const { return text_; }
};

// ── Command interface ──────────────────────────────────────────────────────
class Command {
public:
    virtual ~Command() = default;
    virtual void execute() = 0;
    virtual void undo()    = 0;
    virtual std::string description() const = 0;
};

// ── Concrete commands ──────────────────────────────────────────────────────
class InsertCommand : public Command {
    TextEditor&  editor_;
    std::string  text_;
    size_t       position_;
public:
    InsertCommand(TextEditor& e, std::string text, size_t pos)
        : editor_(e), text_(std::move(text)), position_(pos) {}

    void execute() override { editor_.insertText(text_, position_); }
    void undo()    override { editor_.deleteText(position_, text_.size()); }
    std::string description() const override {
        return "Insert '" + text_ + "' at " + std::to_string(position_);
    }
};

class DeleteCommand : public Command {
    TextEditor&  editor_;
    std::string  deletedText_; // saved for undo
    size_t       position_;
    size_t       length_;
public:
    DeleteCommand(TextEditor& e, size_t pos, size_t len)
        : editor_(e), position_(pos), length_(len) {}

    void execute() override {
        deletedText_ = editor_.getText().substr(position_, length_);
        editor_.deleteText(position_, length_);
    }
    void undo() override { editor_.insertText(deletedText_, position_); }
    std::string description() const override {
        return "Delete " + std::to_string(length_) + " chars at " + std::to_string(position_);
    }
};

// ── Invoker — manages undo/redo stacks ────────────────────────────────────
class CommandHistory {
    std::stack<std::unique_ptr<Command>> undoStack_;
    std::stack<std::unique_ptr<Command>> redoStack_;
public:
    void executeCommand(std::unique_ptr<Command> cmd) {
        std::cout << "Execute: " << cmd->description() << "\n";
        cmd->execute();
        undoStack_.push(std::move(cmd));
        while (!redoStack_.empty()) redoStack_.pop(); // new action clears redo
    }

    bool undo() {
        if (undoStack_.empty()) return false;
        auto& cmd = undoStack_.top();
        std::cout << "Undo: " << cmd->description() << "\n";
        cmd->undo();
        redoStack_.push(std::move(cmd));
        undoStack_.pop();
        return true;
    }

    bool redo() {
        if (redoStack_.empty()) return false;
        auto& cmd = redoStack_.top();
        std::cout << "Redo: " << cmd->description() << "\n";
        cmd->execute();
        undoStack_.push(std::move(cmd));
        redoStack_.pop();
        return true;
    }
};

int main() {
    TextEditor    editor;
    CommandHistory history;

    history.executeCommand(std::make_unique<InsertCommand>(editor, "Hello", 0));
    history.executeCommand(std::make_unique<InsertCommand>(editor, " World", 5));
    std::cout << "Text: \"" << editor.getText() << "\"\n\n";

    history.undo();
    std::cout << "Text: \"" << editor.getText() << "\"\n\n";

    history.redo();
    std::cout << "Text: \"" << editor.getText() << "\"\n\n";

    history.executeCommand(std::make_unique<DeleteCommand>(editor, 5, 6));
    std::cout << "Text: \"" << editor.getText() << "\"\n";
}
```

## Real-World Use Cases
- **NestJS + BullMQ**: Each queued job is a Command — it carries all data needed to execute asynchronously, retried on failure
- **Git**: Every commit is a Command object — `git revert` is undo, `git cherry-pick` is replaying a specific command
- **React**: `useReducer` dispatch is a Command pattern — actions are command objects processed by the reducer

---

# Iterator

## Intent
Provide a way to access the elements of an aggregate object sequentially without exposing its underlying representation.

## When to Use
- You want to traverse a collection without knowing its internal structure (array, list, tree, graph)
- You need multiple simultaneous traversals over the same collection
- You want a uniform interface for traversing different collection types

## C++ Implementation

```cpp
#include <iostream>
#include <vector>
#include <memory>
#include <stdexcept>

// ── Iterator interface ────────────────────────────────────────────────────
template<typename T>
class Iterator {
public:
    virtual ~Iterator() = default;
    virtual bool    hasNext() const = 0;
    virtual T&      next()          = 0;
    virtual void    reset()         = 0;
};

// ── Concrete collection: circular buffer ──────────────────────────────────
template<typename T>
class CircularBuffer {
    std::vector<T> data_;
    size_t         capacity_;
    size_t         head_ = 0;
    size_t         tail_ = 0;
    size_t         size_ = 0;
public:
    explicit CircularBuffer(size_t capacity)
        : data_(capacity), capacity_(capacity) {}

    void push(T value) {
        if (size_ == capacity_) throw std::overflow_error("Buffer full");
        data_[tail_] = std::move(value);
        tail_ = (tail_ + 1) % capacity_;
        ++size_;
    }

    size_t size()     const { return size_;     }
    size_t capacity() const { return capacity_; }

    T& at(size_t logicalIndex) {
        return data_[(head_ + logicalIndex) % capacity_];
    }

    // ── Concrete iterator ───────────────────────────────────────────────
    class ForwardIterator : public Iterator<T> {
        CircularBuffer& buffer_;
        size_t          index_;
    public:
        explicit ForwardIterator(CircularBuffer& buf)
            : buffer_(buf), index_(0) {}

        bool hasNext() const override { return index_ < buffer_.size(); }
        T&   next()    override {
            if (!hasNext()) throw std::out_of_range("No more elements");
            return buffer_.at(index_++);
        }
        void reset()   override { index_ = 0; }
    };

    std::unique_ptr<Iterator<T>> iterator() {
        return std::make_unique<ForwardIterator>(*this);
    }
};

int main() {
    CircularBuffer<int> buffer(5);
    buffer.push(10);
    buffer.push(20);
    buffer.push(30);
    buffer.push(40);

    auto it = buffer.iterator();
    std::cout << "Forward traverse: ";
    while (it->hasNext()) {
        std::cout << it->next() << " ";
    }
    std::cout << "\n";

    it->reset();
    std::cout << "After reset: ";
    while (it->hasNext()) {
        std::cout << it->next() << " ";
    }
    std::cout << "\n";
}
```

## Real-World Use Cases
- **C++ STL**: Every container has `begin()`/`end()` iterators — range-based `for` uses the Iterator pattern
- **Node.js**: `fs.createReadStream()` is an iterator over file chunks; generator functions (`function*`) are iterators
- **JavaScript**: The `Symbol.iterator` protocol — `for...of` works on any object implementing `[Symbol.iterator]()`
- **TypeORM**: `QueryBuilder` result streaming; cursor-based pagination

---

# Mediator

## Intent
Define an object that encapsulates how a set of objects interact. Mediator promotes loose coupling by preventing objects from referring to each other explicitly and lets you vary their interaction independently.

## When to Use
- Many objects communicate in complex, hard-to-follow ways
- Reusing objects is difficult because they refer to too many other objects
- Behavior distributed across many classes should be customizable without subclassing

## C++ Implementation

```cpp
#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>

// ── Mediator interface ─────────────────────────────────────────────────────
class Aircraft; // forward

class ATCMediator {
public:
    virtual ~ATCMediator() = default;
    virtual void requestLanding(Aircraft* aircraft)    = 0;
    virtual void requestTakeoff(Aircraft* aircraft)    = 0;
    virtual void notifyAll(Aircraft* sender, const std::string& event) = 0;
};

// ── Colleague ─────────────────────────────────────────────────────────────
class Aircraft {
protected:
    std::string  callSign_;
    ATCMediator* atc_;
public:
    Aircraft(std::string callSign, ATCMediator* atc)
        : callSign_(std::move(callSign)), atc_(atc) {}
    virtual ~Aircraft() = default;

    const std::string& getCallSign() const { return callSign_; }

    void requestLanding() { atc_->requestLanding(this); }
    void requestTakeoff() { atc_->requestTakeoff(this); }

    virtual void receive(const std::string& message) {
        std::cout << "[" << callSign_ << "] " << message << "\n";
    }
};

// ── Concrete Mediator ─────────────────────────────────────────────────────
class AirTrafficControl : public ATCMediator {
    std::vector<Aircraft*> aircraft_;
    bool                   runwayClear_       = true;
    Aircraft*              waitingToLand_     = nullptr;
public:
    void registerAircraft(Aircraft* a) { aircraft_.push_back(a); }

    void requestLanding(Aircraft* aircraft) override {
        if (runwayClear_) {
            runwayClear_ = false;
            aircraft->receive("Cleared to land on runway 27L");
        } else {
            waitingToLand_ = aircraft;
            aircraft->receive("Hold pattern — runway occupied");
        }
    }

    void requestTakeoff(Aircraft* aircraft) override {
        if (runwayClear_) {
            runwayClear_ = false;
            aircraft->receive("Cleared for takeoff on runway 27L");
        } else {
            aircraft->receive("Hold position — runway busy");
        }
    }

    void notifyAll(Aircraft* sender, const std::string& event) override {
        if (event == "RUNWAY_CLEAR") {
            runwayClear_ = true;
            if (waitingToLand_) {
                Aircraft* next = waitingToLand_;
                waitingToLand_ = nullptr;
                requestLanding(next);
            }
        }
        for (auto* a : aircraft_) {
            if (a != sender)
                a->receive("Traffic: " + sender->getCallSign() + " " + event);
        }
    }
};

int main() {
    AirTrafficControl atc;
    Aircraft flight1("AA101", &atc);
    Aircraft flight2("UA202", &atc);
    atc.registerAircraft(&flight1);
    atc.registerAircraft(&flight2);

    std::cout << "-- AA101 requests landing --\n";
    flight1.requestLanding();

    std::cout << "\n-- UA202 requests landing (busy) --\n";
    flight2.requestLanding();

    std::cout << "\n-- AA101 clears runway --\n";
    atc.notifyAll(&flight1, "RUNWAY_CLEAR");
}
```

## Real-World Use Cases
- **NestJS**: `EventEmitter2` / CQRS `EventBus` acts as a mediator — modules emit events without knowing subscribers
- **React**: Redux store is a Mediator — components dispatch actions and subscribe to state without knowing each other
- **Browser**: DOM `EventTarget` mediates between event producers and consumers

---

# Memento

## Intent
Without violating encapsulation, capture and externalize an object's internal state so that it can be restored to that state later.

## When to Use
- You need to implement undo/redo and cannot expose object internals
- A direct interface to obtaining the state would expose implementation details
- You want to save and restore snapshots of object state

## C++ Implementation

```cpp
#include <iostream>
#include <string>
#include <vector>
#include <memory>

// ── Memento — opaque snapshot ─────────────────────────────────────────────
class EditorMemento {
    std::string text_;
    int         cursorPos_;

    friend class TextEditorOriginator;

    EditorMemento(std::string text, int cursor)
        : text_(std::move(text)), cursorPos_(cursor) {}
public:
    std::string getLabel() const {
        return "Snapshot[cursor=" + std::to_string(cursorPos_) +
               " text='" + text_.substr(0, 15) + "']";
    }
};

// ── Originator ────────────────────────────────────────────────────────────
class TextEditorOriginator {
    std::string text_;
    int         cursorPos_ = 0;
public:
    void type(const std::string& chars) {
        text_.insert(cursorPos_, chars);
        cursorPos_ += static_cast<int>(chars.size());
    }

    void print() const {
        std::cout << "Text: \"" << text_ << "\" cursor=" << cursorPos_ << "\n";
    }

    std::unique_ptr<EditorMemento> save() const {
        return std::unique_ptr<EditorMemento>(new EditorMemento(text_, cursorPos_));
    }

    void restore(const EditorMemento& m) {
        text_      = m.text_;
        cursorPos_ = m.cursorPos_;
    }
};

// ── Caretaker ─────────────────────────────────────────────────────────────
class History {
    std::vector<std::unique_ptr<EditorMemento>> snapshots_;
public:
    void push(std::unique_ptr<EditorMemento> m) {
        std::cout << "Saved: " << m->getLabel() << "\n";
        snapshots_.push_back(std::move(m));
    }

    std::unique_ptr<EditorMemento> pop() {
        if (snapshots_.empty()) return nullptr;
        auto m = std::move(snapshots_.back());
        snapshots_.pop_back();
        return m;
    }
};

int main() {
    TextEditorOriginator editor;
    History              history;

    editor.type("Hello");
    history.push(editor.save());

    editor.type(", World");
    history.push(editor.save());

    editor.type("!!!");
    editor.print();

    std::cout << "\n-- Undo --\n";
    if (auto snap = history.pop()) { editor.restore(*snap); editor.print(); }

    std::cout << "\n-- Undo again --\n";
    if (auto snap = history.pop()) { editor.restore(*snap); editor.print(); }
}
```

## Real-World Use Cases
- **NestJS**: Database transaction savepoints — roll back to a specific point without exposing internal transaction state
- **Redux DevTools**: Time-travel debugging stores a Memento of Redux state after each action
- **Git**: Each commit is a Memento of the working directory state

---

# Observer

## Intent
Define a one-to-many dependency between objects so that when one object changes state, all its dependents are notified and updated automatically.

## When to Use
- When one object changes state, others need to be updated automatically
- An object should be able to notify other objects without assumptions about who they are
- You want to support broadcast communication

## C++ Implementation

```cpp
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>

// ── Observer interface ─────────────────────────────────────────────────────
class StockObserver {
public:
    virtual ~StockObserver() = default;
    virtual void onPriceChange(const std::string& symbol,
                                double oldPrice, double newPrice) = 0;
};

// ── Subject ────────────────────────────────────────────────────────────────
class StockTicker {
    std::string              symbol_;
    double                   price_;
    std::vector<StockObserver*> observers_;
public:
    StockTicker(std::string symbol, double initialPrice)
        : symbol_(std::move(symbol)), price_(initialPrice) {}

    void subscribe(StockObserver* obs)   { observers_.push_back(obs); }
    void unsubscribe(StockObserver* obs) {
        observers_.erase(std::remove(observers_.begin(), observers_.end(), obs),
                         observers_.end());
    }

    void setPrice(double newPrice) {
        if (newPrice == price_) return;
        double old = price_;
        price_ = newPrice;
        for (auto* obs : observers_) obs->onPriceChange(symbol_, old, newPrice);
    }

    double getPrice()              const { return price_;  }
    const std::string& getSymbol() const { return symbol_; }
};

// ── Concrete observers ─────────────────────────────────────────────────────
class MobileAlertObserver : public StockObserver {
    std::string userId_;
    double      threshold_;
public:
    MobileAlertObserver(std::string userId, double threshold)
        : userId_(std::move(userId)), threshold_(threshold) {}

    void onPriceChange(const std::string& symbol,
                       double /*old*/, double newPrice) override {
        if (newPrice >= threshold_)
            std::cout << "[Mobile:" << userId_ << "] ALERT: "
                      << symbol << " hit $" << newPrice << "\n";
    }
};

class AuditLogObserver : public StockObserver {
public:
    void onPriceChange(const std::string& symbol,
                       double oldPrice, double newPrice) override {
        std::cout << "[AuditLog] " << symbol
                  << ": $" << oldPrice << " → $" << newPrice << "\n";
    }
};

class TradingBotObserver : public StockObserver {
    double buyBelow_, sellAbove_;
public:
    TradingBotObserver(double buy, double sell) : buyBelow_(buy), sellAbove_(sell) {}
    void onPriceChange(const std::string& symbol,
                       double /*old*/, double newPrice) override {
        if      (newPrice < buyBelow_)  std::cout << "[Bot] BUY  " << symbol << " @ $" << newPrice << "\n";
        else if (newPrice > sellAbove_) std::cout << "[Bot] SELL " << symbol << " @ $" << newPrice << "\n";
    }
};

int main() {
    StockTicker aapl("AAPL", 180.0);

    MobileAlertObserver alert("user-42", 200.0);
    AuditLogObserver    audit;
    TradingBotObserver  bot(175.0, 195.0);

    aapl.subscribe(&alert);
    aapl.subscribe(&audit);
    aapl.subscribe(&bot);

    aapl.setPrice(185.0);
    aapl.setPrice(196.0);
    aapl.setPrice(202.0);

    aapl.unsubscribe(&bot);
    aapl.setPrice(210.0);
}
```

## Real-World Use Cases
- **NestJS**: `EventEmitter2` / `@OnEvent()` decorator; WebSocket gateways subscribing to domain events
- **Node.js**: `EventEmitter` — every `stream`, `http.Server`, `process` uses Observer
- **React**: `useEffect` with state dependency array — component observes state changes
- **Redux**: `store.subscribe()` — UI components observe the central store

---

# State

## Intent
Allow an object to alter its behavior when its internal state changes. The object will appear to change its class.

## When to Use
- An object's behavior depends on its state and must change at runtime
- Operations have large, multipart conditional statements that depend on the object's state
- State transitions need to be first-class, validated, and centralized

## C++ Implementation

```cpp
#include <iostream>
#include <memory>
#include <string>

// ── Forward declaration ────────────────────────────────────────────────────
class TrafficLight;

// ── State interface ────────────────────────────────────────────────────────
class TrafficLightState {
public:
    virtual ~TrafficLightState() = default;
    virtual void handle(TrafficLight& light) = 0;
    virtual std::string color() const = 0;
};

// ── Context ────────────────────────────────────────────────────────────────
class TrafficLight {
    std::unique_ptr<TrafficLightState> state_;
public:
    explicit TrafficLight(std::unique_ptr<TrafficLightState> initial)
        : state_(std::move(initial)) {}

    void setState(std::unique_ptr<TrafficLightState> s) { state_ = std::move(s); }

    void tick() {
        std::cout << state_->color() << " → ";
        state_->handle(*this);
        std::cout << state_->color() << "\n";
    }
};

// ── Concrete states ────────────────────────────────────────────────────────
class RedState : public TrafficLightState {
public:
    void handle(TrafficLight& light) override;
    std::string color() const override { return "RED"; }
};

class GreenState : public TrafficLightState {
public:
    void handle(TrafficLight& light) override;
    std::string color() const override { return "GREEN"; }
};

class YellowState : public TrafficLightState {
public:
    void handle(TrafficLight& light) override;
    std::string color() const override { return "YELLOW"; }
};

void RedState::handle(TrafficLight& l)    { l.setState(std::make_unique<GreenState>());  }
void GreenState::handle(TrafficLight& l)  { l.setState(std::make_unique<YellowState>()); }
void YellowState::handle(TrafficLight& l) { l.setState(std::make_unique<RedState>());    }

int main() {
    TrafficLight light(std::make_unique<RedState>());
    for (int i = 0; i < 6; ++i) light.tick();
}
```

## Real-World Use Cases
- **NestJS**: Order/Booking status machines; `BookingStatus` transitions (PENDING → CONFIRMED → SHIPPED → DELIVERED)
- **Node.js**: TCP socket states: `CLOSED → LISTEN → SYN_RECEIVED → ESTABLISHED → CLOSE_WAIT`
- **React**: `useReducer` with explicit state transitions — each case in the reducer is a state transition

---

# Strategy

## Intent
Define a family of algorithms, encapsulate each one, and make them interchangeable. Strategy lets the algorithm vary independently from clients that use it.

## When to Use
- Multiple classes differ only in behavior — strategy lets you configure the behavior
- You need different variants of an algorithm
- An algorithm uses data that clients should not know about
- A class defines many behaviors that appear as multiple conditional statements

## C++ Implementation

```cpp
#include <iostream>
#include <vector>
#include <memory>
#include <algorithm>
#include <string>

// ── Strategy interface ─────────────────────────────────────────────────────
class SortStrategy {
public:
    virtual ~SortStrategy() = default;
    virtual void sort(std::vector<int>& data) = 0;
    virtual std::string name() const = 0;
};

// ── Concrete strategies ────────────────────────────────────────────────────
class BubbleSort : public SortStrategy {
public:
    void sort(std::vector<int>& data) override {
        int n = static_cast<int>(data.size());
        for (int i = 0; i < n - 1; ++i)
            for (int j = 0; j < n - i - 1; ++j)
                if (data[j] > data[j + 1]) std::swap(data[j], data[j + 1]);
    }
    std::string name() const override { return "BubbleSort O(n^2)"; }
};

class QuickSort : public SortStrategy {
    void qs(std::vector<int>& d, int lo, int hi) {
        if (lo >= hi) return;
        int pivot = d[hi], i = lo - 1;
        for (int j = lo; j < hi; ++j)
            if (d[j] <= pivot) std::swap(d[++i], d[j]);
        std::swap(d[i + 1], d[hi]);
        int p = i + 1;
        qs(d, lo, p - 1); qs(d, p + 1, hi);
    }
public:
    void sort(std::vector<int>& data) override {
        if (!data.empty()) qs(data, 0, static_cast<int>(data.size()) - 1);
    }
    std::string name() const override { return "QuickSort O(n log n) avg"; }
};

class StdSort : public SortStrategy {
public:
    void sort(std::vector<int>& data) override { std::sort(data.begin(), data.end()); }
    std::string name() const override { return "std::sort (introsort)"; }
};

// ── Context ────────────────────────────────────────────────────────────────
class Sorter {
    std::unique_ptr<SortStrategy> strategy_;
public:
    explicit Sorter(std::unique_ptr<SortStrategy> s) : strategy_(std::move(s)) {}
    void setStrategy(std::unique_ptr<SortStrategy> s) { strategy_ = std::move(s); }
    void sort(std::vector<int>& data) {
        std::cout << "Sorting with: " << strategy_->name() << " → ";
        strategy_->sort(data);
        for (int x : data) std::cout << x << " ";
        std::cout << "\n";
    }
};

int main() {
    std::vector<int> data = {64, 25, 12, 22, 11};
    Sorter sorter(std::make_unique<BubbleSort>());
    sorter.sort(data);

    data = {64, 25, 12, 22, 11};
    sorter.setStrategy(std::make_unique<QuickSort>());
    sorter.sort(data);

    data = {64, 25, 12, 22, 11};
    sorter.setStrategy(std::make_unique<StdSort>());
    sorter.sort(data);
}
```

## Real-World Use Cases
- **NestJS**: `PricingStrategy` for billing (flat, usage-based, tiered); `PaymentStrategy` (Stripe, Razorpay, UPI)
- **Passport.js**: Each authentication method (`jwt`, `local`, `google`) is a Strategy
- **React**: Form validation strategies; rendering strategies in `react-table`

---

# Template Method

## Intent
Define the skeleton of an algorithm in a base class, deferring some steps to subclasses. Template Method lets subclasses redefine certain steps of an algorithm without changing its overall structure.

## When to Use
- Multiple classes share the same algorithm skeleton but differ in specific steps
- You want to control the points at which subclasses can extend the algorithm (hooks)
- You want to avoid code duplication in related classes

## C++ Implementation

```cpp
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>

// ── Abstract class with template method ───────────────────────────────────
class DataParser {
public:
    virtual ~DataParser() = default;

    // Template method — non-virtual; defines the algorithm skeleton
    void parse(const std::string& rawData) {
        std::cout << "=== Parsing with " << format() << " ===\n";
        std::string              clean    = preProcess(rawData);
        std::vector<std::string> records  = extractRecords(clean);
        std::vector<std::string> valid    = validateRecords(records); // hook
        postProcess(valid);
        std::cout << "Parsed " << valid.size() << " records\n";
    }

protected:
    virtual std::string              format()         const = 0;
    virtual std::string              preProcess(const std::string& data) = 0;
    virtual std::vector<std::string> extractRecords(const std::string& data) = 0;

    // Hook — default: accept all records
    virtual std::vector<std::string> validateRecords(const std::vector<std::string>& r) {
        return r;
    }

    virtual void postProcess(const std::vector<std::string>& records) {
        for (const auto& r : records) std::cout << "  Record: " << r << "\n";
    }
};

// ── CSV Parser ────────────────────────────────────────────────────────────
class CsvDataParser : public DataParser {
protected:
    std::string format() const override { return "CSV"; }

    std::string preProcess(const std::string& data) override {
        std::string clean = data;
        clean.erase(std::remove(clean.begin(), clean.end(), '\r'), clean.end());
        return clean;
    }

    std::vector<std::string> extractRecords(const std::string& data) override {
        std::vector<std::string> records;
        std::istringstream       stream(data);
        std::string              line;
        bool firstLine = true;
        while (std::getline(stream, line)) {
            if (firstLine) { firstLine = false; continue; } // skip header
            if (!line.empty()) records.push_back(line);
        }
        return records;
    }
};

// ── JSON Parser ───────────────────────────────────────────────────────────
class JsonDataParser : public DataParser {
protected:
    std::string format() const override { return "JSON"; }

    std::string preProcess(const std::string& data) override {
        auto start = data.find('[');
        auto end   = data.rfind(']');
        if (start == std::string::npos) return data;
        return data.substr(start + 1, end - start - 1);
    }

    std::vector<std::string> extractRecords(const std::string& data) override {
        std::vector<std::string> records;
        std::string trimmed = data;
        size_t pos = 0;
        while ((pos = trimmed.find("},{")) != std::string::npos) {
            records.push_back("{" + trimmed.substr(0, pos + 1) + "}");
            trimmed = trimmed.substr(pos + 2);
        }
        if (!trimmed.empty()) records.push_back(trimmed);
        return records;
    }

    // Override hook to skip empty objects
    std::vector<std::string> validateRecords(
            const std::vector<std::string>& records) override {
        std::vector<std::string> valid;
        for (const auto& r : records)
            if (r.size() > 2) valid.push_back(r);
        return valid;
    }
};

int main() {
    CsvDataParser csv;
    csv.parse("name,age\nAlice,30\nBob,25\nCarol,35");

    std::cout << "\n";

    JsonDataParser json;
    json.parse(R"([{"name":"Alice"},{"name":"Bob"},{"name":"Carol"}])");
}
```

## Real-World Use Cases
- **NestJS**: `BaseRepository` with `findById`, `findAll` template methods; concrete repos override `buildQuery()`
- **Node.js**: `stream.Transform` — `_transform()` and `_flush()` are the steps you implement; the stream lifecycle is the template
- **React**: Class component lifecycle (`render`, `componentDidMount`, `componentDidUpdate`) is a Template Method — React calls them in order; you fill in the steps

---

# Visitor

## Intent
Represent an operation to be performed on elements of an object structure. Visitor lets you define a new operation without changing the classes of the elements on which it operates.

## When to Use
- You need to perform many distinct and unrelated operations on an object structure without polluting their classes
- The object structure classes are stable but you frequently need to define new operations on them
- An operation spans several classes and you want it in one place

## C++ Implementation

```cpp
#include <iostream>
#include <vector>
#include <memory>
#include <cmath>
#include <string>

// ── Forward declarations ────────────────────────────────────────────────────
class Circle;
class Rectangle;
class Triangle;

// ── Visitor interface — one visit() overload per element type ──────────────
class ShapeVisitor {
public:
    virtual ~ShapeVisitor() = default;
    virtual void visit(const Circle& c)    = 0;
    virtual void visit(const Rectangle& r) = 0;
    virtual void visit(const Triangle& t)  = 0;
};

// ── Element interface ──────────────────────────────────────────────────────
class Shape {
public:
    virtual ~Shape() = default;
    virtual void accept(ShapeVisitor& v) const = 0;
    virtual std::string name() const = 0;
};

// ── Concrete elements ──────────────────────────────────────────────────────
class Circle : public Shape {
public:
    double radius;
    explicit Circle(double r) : radius(r) {}
    void accept(ShapeVisitor& v) const override { v.visit(*this); }
    std::string name() const override { return "Circle(r=" + std::to_string(radius) + ")"; }
};

class Rectangle : public Shape {
public:
    double width, height;
    Rectangle(double w, double h) : width(w), height(h) {}
    void accept(ShapeVisitor& v) const override { v.visit(*this); }
    std::string name() const override {
        return "Rect(" + std::to_string(width) + "x" + std::to_string(height) + ")";
    }
};

class Triangle : public Shape {
public:
    double base, height;
    Triangle(double b, double h) : base(b), height(h) {}
    void accept(ShapeVisitor& v) const override { v.visit(*this); }
    std::string name() const override { return "Triangle(b=" + std::to_string(base) + ")"; }
};

// ── Concrete visitor 1: Area calculator ───────────────────────────────────
class AreaCalculator : public ShapeVisitor {
public:
    double totalArea = 0.0;
    void visit(const Circle& c) override {
        double a = M_PI * c.radius * c.radius;
        std::cout << c.name() << " area = " << a << "\n";
        totalArea += a;
    }
    void visit(const Rectangle& r) override {
        double a = r.width * r.height;
        std::cout << r.name() << " area = " << a << "\n";
        totalArea += a;
    }
    void visit(const Triangle& t) override {
        double a = 0.5 * t.base * t.height;
        std::cout << t.name() << " area = " << a << "\n";
        totalArea += a;
    }
};

// ── Concrete visitor 2: Perimeter calculator ──────────────────────────────
class PerimeterCalculator : public ShapeVisitor {
public:
    void visit(const Circle& c) override {
        std::cout << c.name() << " perimeter = " << 2 * M_PI * c.radius << "\n";
    }
    void visit(const Rectangle& r) override {
        std::cout << r.name() << " perimeter = " << 2 * (r.width + r.height) << "\n";
    }
    void visit(const Triangle& t) override {
        double side = std::sqrt(std::pow(t.base / 2, 2) + std::pow(t.height, 2));
        std::cout << t.name() << " perimeter ~= " << (t.base + 2 * side) << "\n";
    }
};

int main() {
    std::vector<std::unique_ptr<Shape>> shapes;
    shapes.push_back(std::make_unique<Circle>(5.0));
    shapes.push_back(std::make_unique<Rectangle>(4.0, 6.0));
    shapes.push_back(std::make_unique<Triangle>(3.0, 4.0));

    std::cout << "=== Area ===\n";
    AreaCalculator area;
    for (const auto& s : shapes) s->accept(area);
    std::cout << "Total: " << area.totalArea << "\n\n";

    std::cout << "=== Perimeter ===\n";
    PerimeterCalculator perim;
    for (const auto& s : shapes) s->accept(perim);
    // New visitor added — no changes to Circle, Rectangle, or Triangle
}
```

## Real-World Use Cases
- **TypeScript/NestJS**: `class-transformer` uses a visitor-like approach — `classToPlain()` visits all decorated properties without knowing the class structure in advance
- **Compilers/AST**: Every optimizer, linter, type-checker, and code-generator is a Visitor over the AST — ESLint rules are visitors traversing the AST node tree
- **React**: `React.Children.map` and `cloneElement` visit the component tree — each transformation is a visitor pass

---

## Quick Reference

| Pattern                  | Core Idea                                            | Key Signal in Interview Problem                             |
|--------------------------|------------------------------------------------------|-------------------------------------------------------------|
| Chain of Responsibility  | Pass request along a handler chain                   | "Multiple handlers; handler not known in advance"           |
| Command                  | Encapsulate request as an object                     | "Undo/redo; queue operations; log history"                  |
| Iterator                 | Sequential access without exposing representation    | "Traverse collection; multiple traversal types"             |
| Mediator                 | Centralize object interaction                        | "N objects talking to N objects; complex coupling"          |
| Memento                  | Capture state for later restoration                  | "Undo/redo; snapshots; without exposing internals"          |
| Observer                 | Notify dependents on state change                    | "1-to-many notification; event subscription"                |
| State                    | Behavior changes with internal state                 | "Large if/else on state; valid transition enforcement"       |
| Strategy                 | Interchangeable family of algorithms                 | "Multiple algorithms for same task; swap at runtime"        |
| Template Method          | Skeleton in base class; steps deferred to subclasses | "Same algorithm, different steps; avoid duplication"        |
| Visitor                  | New operations without changing element classes      | "Many operations on stable object structure"                |
