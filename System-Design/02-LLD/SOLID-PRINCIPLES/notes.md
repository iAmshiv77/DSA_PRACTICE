# SOLID Principles

Five object-oriented design principles (Robert C. Martin) that make software
**easier to extend, test, and maintain**. They're the "why" behind most design
patterns — patterns are concrete recipes; SOLID is the underlying intent.

```
S — Single Responsibility   : one class, one reason to change
O — Open/Closed             : open for extension, closed for modification
L — Liskov Substitution     : subtypes must be substitutable for base types
I — Interface Segregation   : many small interfaces > one fat interface
D — Dependency Inversion    : depend on abstractions, not concretions
```

All examples are C++ (matches this repo). The same reasoning maps 1:1 to
TypeScript/Java/Python.

---

# S — Single Responsibility Principle (SRP)

> **A class should have only one reason to change.**

A "responsibility" = an axis of change driven by one actor/concern. If a class
handles business logic *and* persistence *and* formatting, three different teams'
changes all collide in one file.

### ❌ Violation — one class, three reasons to change
```cpp
class Invoice {
    std::vector<Item> items_;
public:
    double calculateTotal() const { /* business rule */ }
    void   saveToDatabase() const { /* persistence — changes when DB changes */ }
    std::string toPDF()     const { /* formatting — changes when layout changes */ }
};
```
A schema change, a tax-rule change, and a layout change *all* edit `Invoice`.

### ✅ Fix — split by reason-to-change
```cpp
class Invoice {                 // 1. business data + rules
    std::vector<Item> items_;
public:
    double calculateTotal() const { /* ... */ }
    const std::vector<Item>& items() const { return items_; }
};

class InvoiceRepository {       // 2. persistence
public:
    void save(const Invoice&) { /* DB-specific */ }
};

class InvoicePrinter {          // 3. presentation
public:
    std::string toPDF(const Invoice&) { /* layout */ }
};
```

**Smell detector:** "this class does X **and** Y", giant classes ("god objects"),
a method list that spans unrelated concerns. **Caution:** don't over-split into
anemic one-method classes — responsibility is about *reasons to change*, not line count.

**🌍 Real-world uses**
- **Spring/NestJS layering:** Controller (HTTP) → Service (business) → Repository
  (persistence) — each layer is one reason to change.
- **React:** a presentational component (render) vs a custom hook (data/state) vs
  an API client module.
- **Microservices** themselves are SRP at the service boundary — one service, one
  bounded context (orders ≠ payments ≠ notifications).
- **Logging frameworks** separate the logger, the formatter, and the appender/sink.

---

# O — Open/Closed Principle (OCP)

> **Software entities should be open for extension but closed for modification.**

You should add new behavior by *adding* code, not by *editing* tested, working
code. The enabler is **polymorphism**: depend on an abstraction, plug in new
implementations.

### ❌ Violation — every new shape edits this function
```cpp
double area(const Shape& s) {
    if (s.type == CIRCLE)    return 3.14159 * s.r * s.r;
    else if (s.type == RECT) return s.w * s.h;
    // adding Triangle → edit this function again (and retest it)
}
```

### ✅ Fix — extend by adding a subclass, no existing code touched
```cpp
class Shape {
public:
    virtual ~Shape() = default;
    virtual double area() const = 0;     // the stable abstraction
};
class Circle : public Shape {
    double r_;
public: explicit Circle(double r): r_(r) {}
    double area() const override { return 3.14159 * r_ * r_; }
};
class Rectangle : public Shape {
    double w_, h_;
public: Rectangle(double w,double h): w_(w),h_(h) {}
    double area() const override { return w_ * h_; }
};
// New shape? Add a new class. area() callers never change.
class Triangle : public Shape { /* ... */ };
```

**Realization:** strategy pattern, plugin systems, and visitor are all OCP in
action. The `switch`-on-type that grows forever is the canonical violation.

**🌍 Real-world uses**
- **Payment gateways:** a `PaymentMethod` interface with `CreditCard`, `UPI`,
  `PayPal`, `ApplePay` impls — adding a new method never edits checkout logic.
- **Notification systems:** `INotificationChannel` → Email / SMS / Push / Slack;
  add WhatsApp without touching the dispatcher.
- **Compilers / linters:** new rules register as plugins; the engine is closed.
- **VS Code / browser extensions, Jenkins plugins, Kafka Connect connectors** —
  the host is closed, behavior extends via add-ons.

---

# L — Liskov Substitution Principle (LSP)

> **Objects of a superclass should be replaceable with objects of a subclass
> without breaking the program.**

A subtype must honor the base type's **contract**: same (or weaker)
preconditions, same (or stronger) postconditions, no surprising exceptions, no
strengthened invariants. If client code that works with `Base` breaks when handed
a `Derived`, LSP is violated.

### ❌ Classic violation — Rectangle/Square
```cpp
class Rectangle {
protected: int w_, h_;
public:
    virtual void setWidth(int w)  { w_ = w; }
    virtual void setHeight(int h) { h_ = h; }
    int area() const { return w_ * h_; }
};
class Square : public Rectangle {   // "a square IS-A rectangle" — but not behaviorally
public:
    void setWidth(int w)  override { w_ = h_ = w; }   // side effect!
    void setHeight(int h) override { w_ = h_ = h; }
};

// Client assumes the Rectangle contract:
void test(Rectangle& r) {
    r.setWidth(5); r.setHeight(4);
    assert(r.area() == 20);   // FAILS for Square (area == 16) → LSP broken
}
```
The fix isn't more inheritance — it's recognizing they aren't substitutable.
Model them separately (or as immutable shapes with no setters).

### ❌ Another: subclass strengthens a precondition / throws unexpectedly
```cpp
class Bird            { public: virtual void fly() {} };
class Ostrich : public Bird {
public: void fly() override { throw std::logic_error("can't fly"); } // breaks callers
};
```
Fix: don't put `fly()` on `Bird`; split `FlyingBird` / `Bird` so the type system
reflects real capability.

**Rule:** inheritance must model true behavioral substitutability ("is-a" in
behavior), not just shared fields. When in doubt, prefer composition.

**🌍 Real-world uses**
- **Java Collections:** any code taking a `List` works with `ArrayList` or
  `LinkedList` — they honor the same contract. (Counter-example: `Arrays.asList()`
  returns a fixed-size list whose `add()` throws — a subtle LSP gotcha.)
- **`std::` containers / iterators:** an algorithm written against an iterator
  concept works for `vector`, `list`, `deque`.
- **Cloud storage SDKs:** swap `S3Storage` for `AzureBlobStorage` behind one
  `IBlobStore` and the app keeps working — only if both truly honor the contract.
- **The bug to avoid:** an `IReadOnlyRepository` subtype whose `save()` throws —
  callers expecting the base contract break.

---

# I — Interface Segregation Principle (ISP)

> **No client should be forced to depend on methods it does not use.**

Prefer several small, role-specific interfaces over one fat interface. A fat
interface forces implementers to stub out methods they don't support — and
couples clients to changes they don't care about.

### ❌ Violation — fat interface forces empty implementations
```cpp
class IMachine {
public:
    virtual void print(Doc) = 0;
    virtual void scan(Doc)  = 0;
    virtual void fax(Doc)   = 0;
};
class OldPrinter : public IMachine {
public:
    void print(Doc d) override { /* real */ }
    void scan(Doc)    override { throw std::logic_error("no scanner"); }  // forced stub
    void fax(Doc)     override { throw std::logic_error("no fax"); }      // forced stub
};
```

### ✅ Fix — segregated role interfaces
```cpp
class IPrinter { public: virtual void print(Doc) = 0; virtual ~IPrinter() = default; };
class IScanner { public: virtual void scan(Doc)  = 0; virtual ~IScanner() = default; };
class IFax     { public: virtual void fax(Doc)   = 0; virtual ~IFax()     = default; };

class OldPrinter : public IPrinter {                  // implements only what it does
public: void print(Doc d) override { /* real */ }
};
class AllInOne : public IPrinter, public IScanner, public IFax {
public:
    void print(Doc) override {} void scan(Doc) override {} void fax(Doc) override {}
};
```
Clients depend only on the capability they use (`void job(IScanner&)`), so they're
immune to changes in printing or faxing.

**🌍 Real-world uses**
- **Go's `io` package** is the gold standard: tiny `Reader`, `Writer`, `Closer`
  interfaces compose into `ReadWriteCloser` — implement only what you support.
- **AWS IAM policies:** grant `s3:GetObject` without granting `s3:DeleteObject` —
  segregated permissions, same idea applied to authorization.
- **REST API design:** narrow, purpose-built endpoints/DTOs instead of one
  god-endpoint that returns everything.
- **Repository interfaces** split into `IReadRepository` / `IWriteRepository`
  (also enables CQRS).

---

# D — Dependency Inversion Principle (DIP)

> **High-level modules should not depend on low-level modules. Both should depend
> on abstractions. Abstractions should not depend on details; details depend on
> abstractions.**

Business logic shouldn't hard-wire concrete infrastructure (a specific DB, mailer,
HTTP client). Invert the arrow: define an interface the business logic owns, and
inject the concrete implementation.

### ❌ Violation — high-level policy nailed to a low-level detail
```cpp
class MySQLDatabase {
public: void save(const std::string& data) { /* MySQL-specific */ }
};
class OrderService {
    MySQLDatabase db_;        // hard dependency on a concrete DB
public:
    void placeOrder() { db_.save("order"); }   // can't swap DB, can't unit-test
};
```

### ✅ Fix — depend on an abstraction, inject the concretion
```cpp
// Abstraction OWNED by the high-level layer (its needs, its interface)
class IOrderStore {
public:
    virtual ~IOrderStore() = default;
    virtual void save(const std::string& data) = 0;
};

// Low-level detail depends on the abstraction (implements it)
class MySQLOrderStore : public IOrderStore {
public: void save(const std::string& d) override { /* MySQL */ }
};
class InMemoryOrderStore : public IOrderStore {     // trivial test double
public: void save(const std::string&) override { /* record in a vector */ }
};

// High-level policy depends only on the interface
class OrderService {
    IOrderStore& store_;                            // injected
public:
    explicit OrderService(IOrderStore& s) : store_(s) {}
    void placeOrder() { store_.save("order"); }
};

// Wiring (composition root)
int main() {
    MySQLOrderStore prod;
    OrderService    svc(prod);          // prod
    svc.placeOrder();
    // tests: OrderService svc(InMemoryOrderStore{...});  ← no DB needed
}
```

DIP is the backbone of **dependency injection** and testability: swap real
infrastructure for fakes by changing one line at the composition root.

**🌍 Real-world uses**
- **Spring `@Autowired`, NestJS providers, .NET `IServiceCollection`, Google
  Guice/Dagger** — entire DI frameworks exist to wire abstractions to concretions.
- **Database drivers behind ORMs:** Prisma/TypeORM/JPA depend on a driver
  interface; swap Postgres → MySQL by config, not code.
- **Unit testing:** inject an in-memory repo or a mock mailer so tests run with no
  DB/SMTP — the single biggest practical payoff of DIP.
- **Hexagonal / Clean Architecture:** the domain defines "ports" (interfaces);
  infrastructure provides "adapters" (implementations).

---

## How SOLID Hangs Together

```
SRP  → small, focused classes
ISP  → small, focused interfaces          } cohesion
OCP  → extend via new types, don't edit
DIP  → depend on those interfaces         } extensibility + testability
LSP  → subtypes keep the contract         } correctness of the above
```

- **OCP needs DIP**: you can only "extend without modifying" if callers depend on
  an abstraction.
- **DIP needs LSP**: swapping implementations is only safe if they're truly
  substitutable.
- **ISP supports LSP**: small interfaces are easier to implement honestly, so
  subtypes are less likely to violate the contract.

---

## SOLID vs Design Patterns

| Principle | Patterns that embody it |
|-----------|-------------------------|
| SRP | Facade, Mediator (split responsibilities) |
| OCP | Strategy, Decorator, Visitor, Template Method |
| LSP | (a constraint on *all* inheritance hierarchies) |
| ISP | Adapter, Role interfaces |
| DIP | Dependency Injection, Abstract Factory, Bridge |

> See [`../DESIGN-PATTERNS/`](../DESIGN-PATTERNS) for the concrete pattern catalog.

---

## Pragmatic Cautions (senior judgment)

- SOLID is a **guideline, not dogma.** Over-applying it produces a maze of tiny
  classes and interfaces (over-engineering) that's *harder* to read.
- Apply pressure **where change actually happens.** Don't add an interface "just
  in case" — add it when a second implementation appears or a test needs a seam (YAGNI).
- The goal is **manageable change**, not maximal abstraction. If a concrete class
  is simple and stable, leave it concrete.

---

## Interview Questions

**Q: Give a real SRP violation you've seen.**
A: A `UserService` that validated input, hashed passwords, talked to the DB, and
sent welcome emails. Email-template changes and DB-migration changes both edited it
and risked breaking auth. Split into `UserService` (policy), `UserRepository`,
`PasswordHasher`, and `EmailSender`.

**Q: How do OCP and DIP relate?**
A: OCP says "extend without modifying"; that's only possible if existing code
depends on an abstraction rather than the concrete thing you're replacing — which
is exactly DIP. DIP is the mechanism that makes OCP achievable.

**Q: Square-Rectangle — why does it violate LSP, and what's the fix?**
A: Clients rely on the Rectangle contract that width and height are independent;
Square couples them, so `setWidth(5); setHeight(4); area()==20` fails. The fix is
to stop forcing an inheritance relationship that isn't behaviorally substitutable —
model shapes independently (often immutable, no setters).

**Q: Isn't SOLID just more boilerplate?**
A: Applied everywhere blindly, yes — it becomes over-engineering. Applied at the
seams where requirements actually change and where tests need to inject fakes, it
pays for itself. Judgment about *where* is the senior skill.
