# Low Level Design — Master Reference

## OOP Pillars — Interview Definitions

### Encapsulation
```
Bundle data (fields) + behavior (methods) together. Hide internal state.
Expose only what is necessary via public interface.

Example: BankAccount
  private double balance;   // hidden
  public void deposit(double amount) { ... }  // controlled access

Interview Q: Why is encapsulation important?
A: Prevents invalid state (e.g., negative balance). Allows changing internal
   implementation without breaking callers. Enables invariant enforcement.
```

### Abstraction
```
Hide HOW something works, expose WHAT it does.
Use interfaces/abstract classes to define contracts.

Example: PaymentProcessor interface
  process(amount) → bool    // What: process payment
                            // How: hidden (CreditCard vs UPI vs Wallet)

Interview Q: Difference between abstraction and encapsulation?
A: Encapsulation = hiding data (using access modifiers).
   Abstraction = hiding complexity (using interfaces/abstract classes).
   Both reduce coupling, but at different levels.
```

### Inheritance
```
Child class inherits fields + methods of parent. "Is-a" relationship.

Example: class ElectricCar extends Car { ... }
         class SavingsAccount extends BankAccount { ... }

PITFALL: Deep inheritance chains → fragile base class problem.
Rule: Prefer composition over inheritance for code reuse.
      Use inheritance only for true "is-a" relationships.
```

### Polymorphism
```
Same interface, different implementations. Runtime dispatch.
Method overriding (runtime) vs overloading (compile-time).

Example:
  Animal animals[] = {new Dog(), new Cat(), new Bird()};
  for (auto a : animals) a->sound();  // each calls its own sound()

Interview Q: How does virtual dispatch work?
A: Each object has a vtable pointer. Virtual method call → vtable lookup → correct method.
   Cost: one extra pointer indirection. Negligible in practice.
```

---

## SOLID Principles — Deep Dive

### S — Single Responsibility Principle
```
Definition: A class should have exactly ONE reason to change.

Violation:
  class UserService {
    saveUser(User u) { ... }         // DB concern
    sendWelcomeEmail(User u) { ... } // Email concern
    generateUserReport() { ... }     // Reporting concern
  }

Fix: Split into UserRepository, EmailService, ReportService
     UserService orchestrates, doesn't implement

Interview Trap: "What counts as ONE reason?"
  Answer: One actor/stakeholder whose requirements drive changes.
  DB team changes → UserRepository changes.
  Email team changes → EmailService changes.
  They don't affect each other.
```

### O — Open/Closed Principle
```
Definition: Open for EXTENSION, closed for MODIFICATION.
            Add new behavior without changing existing code.

Violation:
  double calculateDiscount(User u) {
    if (u.type == SILVER) return 0.05;
    else if (u.type == GOLD) return 0.10;
    else if (u.type == PLATINUM) return 0.20;  // new type = modify code
  }

Fix: DiscountStrategy interface + SilverDiscount, GoldDiscount, PlatinumDiscount
     Adding new tier = add new class, don't touch existing code

Key Tool: Strategy pattern, Plugin architecture
```

### L — Liskov Substitution Principle
```
Definition: Subclass must be usable wherever parent is used.
            Subclass should NOT strengthen preconditions or weaken postconditions.

Classic Violation: Rectangle → Square
  Rectangle r = new Square(5, 5);
  r.setWidth(10);   // Square: changes height too → unexpected behavior!
  assert(r.getArea() == 50)  // FAILS: area = 100

Fix: Don't inherit Square from Rectangle.
     Both extend Shape. Different behaviors.

Interview Q: How do you identify LSP violations?
  Code that checks instanceof → likely LSP violation.
  if (shape instanceof Square) { ... } else { ... }  // wrong
```

### I — Interface Segregation Principle
```
Definition: Clients should NOT depend on interfaces they don't use.
            Prefer many small interfaces over one fat interface.

Violation:
  interface MultiFunctionDevice {
    print();
    scan();
    fax();
    copy();
  }
  // OldPrinter only prints but must implement all methods (throws NotImplemented)

Fix: interface Printer { print(); }
     interface Scanner { scan(); }
     class ModernPrinter implements Printer, Scanner { ... }
     class BasicPrinter implements Printer { ... }
```

### D — Dependency Inversion Principle
```
Definition: High-level modules should NOT depend on low-level modules.
            Both should depend on abstractions.

Violation:
  class OrderService {
    MySQLOrderRepository repo = new MySQLOrderRepository();  // concrete
  }
  // Can't swap for MongoDB or testing without changing OrderService

Fix:
  class OrderService {
    IOrderRepository repo;   // interface
    OrderService(IOrderRepository repo) { this.repo = repo; }  // inject
  }
  // Can inject MySQLRepo, MongoRepo, MockRepo — OrderService unchanged

Key Tool: Dependency Injection (DI) containers, constructor injection
```

---

## Design Patterns — When to Use What

### CREATIONAL PATTERNS

#### Singleton
```
When: Exactly one instance needed (Logger, Config, DB Connection Pool)
How:  Private constructor + static instance + thread-safe initialization

C++:
  class Config {
    static Config* instance;
    Config() {}
  public:
    static Config* getInstance() {
      static Config instance;  // C++11: thread-safe static init
      return &instance;
    }
  };

Pitfalls: Hard to test (can't mock), global state, multithreaded issues
Interview: "Explain double-checked locking."
Answer: if (!instance) { lock; if (!instance) { instance = new...} }
        First check avoids lock overhead after init.
        Second check prevents race condition.
        C++11 memory model: use std::call_once instead.
```

#### Factory Method
```
When: Create objects where subclass decides the type
How:  Abstract creator with createProduct() method

class VehicleFactory {
  virtual Vehicle* createVehicle() = 0;  // subclasses implement
}
class CarFactory : public VehicleFactory {
  Vehicle* createVehicle() override { return new Car(); }
}

vs Abstract Factory: factory of factories (create families of related objects)
```

#### Builder
```
When: Complex object with many optional fields / construction steps
How:  Fluent interface, step-by-step construction

Pizza p = Pizza::Builder()
  .crust("thin")
  .size(12)
  .topping("cheese")
  .topping("mushroom")
  .build();

vs Constructor: "telescoping constructors" are unreadable with 10 params
vs Setter: object may be in invalid intermediate state
```

### STRUCTURAL PATTERNS

#### Strategy
```
When: Multiple algorithms for same task, swap at runtime
How:  Interface + concrete implementations, injected into context

class Sorter {
  SortStrategy* strategy;
public:
  void sort(vector<int>& v) { strategy->sort(v); }
};

Use: payment methods, discount calculations, compression algorithms
```

#### Observer
```
When: One-to-many dependency; when X changes, notify Y, Z, W
How:  Subject maintains list of observers; notifyAll() on change

class EventSource {
  vector<Observer*> observers;
  void addObserver(Observer* o) { observers.push_back(o); }
  void notify() { for (auto o : observers) o->update(this); }
};

Use: UI events, event-driven systems, stock price updates
```

#### Decorator
```
When: Add behavior dynamically without subclassing
How:  Wrapper implements same interface, adds behavior, delegates

Coffee coffee = new Milk(new Sugar(new BasicCoffee()));
// Each wrapper adds its behavior + delegates to inner

Use: Java I/O streams, middleware chains, logging wrappers
```

#### Adapter
```
When: Make incompatible interfaces work together
How:  Wrapper translates one interface to another

class NewPaymentAdapter implements ModernPaymentInterface {
  LegacyPaymentSystem* legacy;
  bool pay(double amount) override {
    return legacy->processCharge(amount * 100);  // convert to cents
  }
};
```

#### Composite
```
When: Tree structure where leaves and composites treated uniformly
How:  Component interface, Leaf and Composite both implement it

class FileSystemItem {
  virtual int getSize() = 0;
}
class File : FileSystemItem { int getSize() { return fileSize; } }
class Directory : FileSystemItem {
  vector<FileSystemItem*> children;
  int getSize() {
    int total = 0;
    for (auto c : children) total += c->getSize();
    return total;
  }
}
```

### BEHAVIORAL PATTERNS

#### State
```
When: Object changes behavior based on internal state
How:  State interface, concrete states, context delegates to current state

class ATM {
  ATMState* state;
public:
  void insertCard() { state->insertCard(this); }
  void setState(ATMState* s) { delete state; state = s; }
};

Use: Order lifecycle, ATM, elevator, traffic light, vending machine
```

#### Command
```
When: Encapsulate requests as objects (undo/redo, logging, queuing)
How:  Command interface with execute() + undo()

class Command { virtual void execute() = 0; virtual void undo() = 0; }
class TransferCommand : Command {
  Account *from, *to; double amount;
  void execute() { from->debit(amount); to->credit(amount); }
  void undo()    { to->debit(amount); from->credit(amount); }
};

Use: Text editor undo/redo, macro recording, transaction rollback
```

#### Template Method
```
When: Algorithm skeleton with some steps overridable by subclasses
How:  Base class defines algorithm; abstract methods for variable steps

class Game {
  // Template method
  void play() { initialize(); startPlay(); endPlay(); }
  virtual void initialize() = 0;
  virtual void startPlay() = 0;
  virtual void endPlay() = 0;
};

Use: Data processing pipelines, report generation, game loops
```

---

## LLD Interview Framework (Step by Step)

```
Step 1: Ask about use cases (5 min)
  "Before I start, let me understand the requirements..."
  - Who uses it? What actions?
  - What constraints? (max group size, max book borrow, etc.)
  - Any concurrency requirements?

Step 2: Identify core entities (3 min)
  Nouns from requirements → candidate classes
  "I see: User, ParkingLot, Spot, Ticket, Vehicle, PricingStrategy..."

Step 3: Define relationships (3 min)
  "Has-a" → composition
  "Is-a" → inheritance (use sparingly)
  Draw rough class diagram

Step 4: Define interfaces first (5 min)
  "I'll start with the interfaces before implementation..."
  class PricingStrategy { virtual double calculate(Ticket*) = 0; }

Step 5: Implement core classes (15 min)
  Start with the most important flow
  "Let me trace the 'park a car' flow and code it..."

Step 6: Discuss design patterns (5 min)
  "I used Strategy pattern for pricing because..."
  "Singleton for ParkingLot because..."

Step 7: Edge cases + thread safety (5 min)
  "What if two cars go to the same spot simultaneously?"
  "I'd use a mutex around spot assignment..."
```

---

## Common Interview Mistakes

| Mistake | Fix |
|---------|-----|
| Jumping to code before design | Always draw class diagram first |
| Not using interfaces | Start with interfaces; code to abstraction |
| God class (one class does everything) | SRP: split by responsibility |
| Not handling concurrency | Ask "multi-threaded?" then add mutex/lock |
| Missing enums | Replace magic strings with enums |
| Anemic domain model | Business logic in domain objects, not services |
| Missing error handling | throw on invalid state; return Optional for not-found |
| Tight coupling (new everywhere) | Inject dependencies through constructor |
