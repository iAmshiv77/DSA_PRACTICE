# Creational Design Patterns

Creational patterns deal with object creation mechanisms. They abstract the instantiation process, making systems independent of how objects are created, composed, and represented.

---

# Singleton

## Intent
Ensure a class has exactly one instance and provide a global access point to it.

## When to Use
- Exactly one object is needed to coordinate actions across the system (logger, config, connection pool, thread pool)
- You need controlled access to a shared resource
- You want lazy initialization — create the object only when first needed

## C++ Implementation

```cpp
#include <mutex>
#include <iostream>

class Logger {
    static Logger*   instance_;
    static std::mutex mutex_;

    // Private constructor prevents direct instantiation
    Logger() { std::cout << "Logger created\n"; }

public:
    Logger(const Logger&)            = delete;
    Logger& operator=(const Logger&) = delete;

    // Thread-safe double-checked locking (C++11 memory model guarantees)
    static Logger* getInstance() {
        if (!instance_) {                         // first check (no lock)
            std::lock_guard<std::mutex> lock(mutex_);
            if (!instance_) {                     // second check (with lock)
                instance_ = new Logger();
            }
        }
        return instance_;
    }

    void log(const std::string& message) const {
        std::cout << "[LOG] " << message << "\n";
    }
};

Logger*    Logger::instance_ = nullptr;
std::mutex Logger::mutex_;

int main() {
    Logger* a = Logger::getInstance();
    Logger* b = Logger::getInstance();
    a->log("Hello");
    std::cout << "Same instance? " << (a == b ? "Yes" : "No") << "\n";
}
```

**C++11 alternative — Meyers Singleton (simpler, also thread-safe):**

```cpp
class Config {
    Config() = default;
public:
    static Config& getInstance() {
        static Config instance;  // initialized once, thread-safe per C++11 standard
        return instance;
    }
    Config(const Config&)            = delete;
    Config& operator=(const Config&) = delete;
};
```

## Real-World Use Cases
- `std::cout` / logging frameworks (spdlog, log4cpp)
- Database connection pool — one pool object manages N connections
- Game engine: `ResourceManager`, `AudioManager`
- OS: file system, print spooler

## Pros / Cons

| Pros | Cons |
|------|------|
| Guaranteed single instance | Global state — hard to test (can't inject a mock) |
| Lazy initialization possible | Hides dependencies (violates explicit dependency principle) |
| Easy global access | Difficult to subclass |
| Thread-safe with double-checked locking | Can cause initialization order issues across TUs |

---

# Factory Method

## Intent
Define an interface for creating an object, but let subclasses decide which class to instantiate. Factory Method lets a class defer instantiation to subclasses.

## When to Use
- A class cannot anticipate the type of objects it must create
- You want subclasses to specify the objects they create
- You need to encapsulate object creation logic
- When creating objects based on runtime configuration

## C++ Implementation

```cpp
#include <iostream>
#include <memory>
#include <string>

// Product interface
class Button {
public:
    virtual ~Button() = default;
    virtual void render() const = 0;
    virtual void onClick() const = 0;
};

// Concrete products
class WindowsButton : public Button {
public:
    void render()  const override { std::cout << "Render Windows button\n"; }
    void onClick() const override { std::cout << "Windows click handler\n"; }
};

class MacButton : public Button {
public:
    void render()  const override { std::cout << "Render Mac button\n"; }
    void onClick() const override { std::cout << "Mac click handler\n"; }
};

// Creator — declares factory method
class Dialog {
public:
    virtual ~Dialog() = default;
    // Factory method — subclasses override to return different buttons
    virtual std::unique_ptr<Button> createButton() const = 0;

    // Template method that uses the factory method
    void render() const {
        auto button = createButton();
        button->render();
    }
};

// Concrete creators
class WindowsDialog : public Dialog {
public:
    std::unique_ptr<Button> createButton() const override {
        return std::make_unique<WindowsButton>();
    }
};

class MacDialog : public Dialog {
public:
    std::unique_ptr<Button> createButton() const override {
        return std::make_unique<MacButton>();
    }
};

// Client code
void clientCode(const Dialog& dialog) {
    dialog.render();
}

int main() {
    WindowsDialog winDlg;
    MacDialog     macDlg;
    clientCode(winDlg);
    clientCode(macDlg);
}
```

## Real-World Use Cases
- GUI frameworks — each OS-specific subclass creates native widgets
- `std::make_unique` / `std::make_shared` (conceptually)
- Document editors — `Application::createDocument()` overridden per app type
- Plugin architectures — each plugin registers its factory method

## Pros / Cons

| Pros | Cons |
|------|------|
| Avoids tight coupling between creator and product | Requires a new subclass per product type |
| Single Responsibility: creation logic in one place | Can lead to large class hierarchy |
| Open/Closed: add new products without changing existing creators | |

---

# Abstract Factory

## Intent
Provide an interface for creating families of related or dependent objects without specifying their concrete classes.

## When to Use
- System must be independent of how products are created
- You need to ensure that a family of products is used together (e.g., all Windows or all Mac widgets — never mixed)
- You want to swap entire product families at runtime

## C++ Implementation

```cpp
#include <iostream>
#include <memory>

// Abstract products
class Button {
public:
    virtual ~Button() = default;
    virtual void render() const = 0;
};
class Checkbox {
public:
    virtual ~Checkbox() = default;
    virtual void render() const = 0;
};

// Windows family
class WindowsButton  : public Button   { public: void render() const override { std::cout << "Windows Button\n";   } };
class WindowsCheckbox: public Checkbox { public: void render() const override { std::cout << "Windows Checkbox\n"; } };

// Mac family
class MacButton  : public Button   { public: void render() const override { std::cout << "Mac Button\n";   } };
class MacCheckbox: public Checkbox { public: void render() const override { std::cout << "Mac Checkbox\n"; } };

// Abstract factory
class GUIFactory {
public:
    virtual ~GUIFactory() = default;
    virtual std::unique_ptr<Button>   createButton()   const = 0;
    virtual std::unique_ptr<Checkbox> createCheckbox() const = 0;
};

// Concrete factories — each creates one coherent family
class WindowsFactory : public GUIFactory {
public:
    std::unique_ptr<Button>   createButton()   const override { return std::make_unique<WindowsButton>();   }
    std::unique_ptr<Checkbox> createCheckbox() const override { return std::make_unique<WindowsCheckbox>(); }
};

class MacFactory : public GUIFactory {
public:
    std::unique_ptr<Button>   createButton()   const override { return std::make_unique<MacButton>();   }
    std::unique_ptr<Checkbox> createCheckbox() const override { return std::make_unique<MacCheckbox>(); }
};

// Client — depends only on abstract factory and product interfaces
class Application {
    std::unique_ptr<Button>   button_;
    std::unique_ptr<Checkbox> checkbox_;
public:
    explicit Application(const GUIFactory& factory)
        : button_(factory.createButton()), checkbox_(factory.createCheckbox()) {}

    void render() const {
        button_->render();
        checkbox_->render();
    }
};

int main() {
    WindowsFactory winFactory;
    Application    winApp(winFactory);
    winApp.render();

    MacFactory  macFactory;
    Application macApp(macFactory);
    macApp.render();
}
```

## Real-World Use Cases
- Cross-platform UI toolkits (Qt, wxWidgets) — one factory per OS
- Database abstraction — `PostgresFactory` vs `MySQLFactory`, each creates `Connection`, `Command`, `DataReader`
- Game engines — `DX12Factory` vs `VulkanFactory` for GPU resources
- Dependency injection containers — swap entire test/prod implementations

## Pros / Cons

| Pros | Cons |
|------|------|
| Product family consistency guaranteed | Hard to add new product types (requires changing every factory) |
| Isolates concrete classes from client | More classes — increased complexity |
| Easy to swap entire product families | |

---

# Builder

## Intent
Separate the construction of a complex object from its representation so that the same construction process can create different representations.

## When to Use
- Object construction requires many steps or configuration options
- You want to avoid "telescoping constructors" (constructors with many parameters)
- Same construction process should produce different products
- Fluent interface for readable object assembly

## C++ Implementation

```cpp
#include <iostream>
#include <string>
#include <vector>
#include <memory>

// Product
class QueryBuilder {
    std::string             table_;
    std::vector<std::string> columns_;
    std::string             whereClause_;
    std::string             orderBy_;
    int                     limit_ = 0;

    QueryBuilder() = default;
    friend class SQLQueryBuilder;  // only builder can construct

public:
    std::string build() const {
        std::string query = "SELECT ";
        if (columns_.empty()) {
            query += "*";
        } else {
            for (size_t i = 0; i < columns_.size(); ++i) {
                if (i > 0) query += ", ";
                query += columns_[i];
            }
        }
        query += " FROM " + table_;
        if (!whereClause_.empty()) query += " WHERE " + whereClause_;
        if (!orderBy_.empty())     query += " ORDER BY " + orderBy_;
        if (limit_ > 0)            query += " LIMIT " + std::to_string(limit_);
        return query;
    }
};

// Builder — fluent interface (method chaining)
class SQLQueryBuilder {
    QueryBuilder product_;
public:
    SQLQueryBuilder& from(const std::string& table) {
        product_.table_ = table;
        return *this;
    }
    SQLQueryBuilder& select(const std::string& column) {
        product_.columns_.push_back(column);
        return *this;
    }
    SQLQueryBuilder& where(const std::string& condition) {
        product_.whereClause_ = condition;
        return *this;
    }
    SQLQueryBuilder& orderBy(const std::string& column) {
        product_.orderBy_ = column;
        return *this;
    }
    SQLQueryBuilder& limit(int n) {
        product_.limit_ = n;
        return *this;
    }
    QueryBuilder build() { return std::move(product_); }
};

// Director (optional) — encodes a specific construction sequence
class ReportQueryDirector {
    SQLQueryBuilder& builder_;
public:
    explicit ReportQueryDirector(SQLQueryBuilder& b) : builder_(b) {}
    void constructMonthlyReport() {
        builder_.from("orders")
                .select("user_id")
                .select("SUM(amount)")
                .where("created_at >= '2026-03-01'")
                .orderBy("SUM(amount) DESC")
                .limit(100);
    }
};

int main() {
    // Direct use (no director)
    SQLQueryBuilder builder;
    auto query = builder.from("users")
                        .select("id")
                        .select("email")
                        .where("is_active = true")
                        .orderBy("created_at")
                        .limit(20)
                        .build();
    std::cout << query.build() << "\n";

    // Using director
    SQLQueryBuilder b2;
    ReportQueryDirector director(b2);
    director.constructMonthlyReport();
    std::cout << b2.build().build() << "\n";
}
```

## Real-World Use Cases
- SQL query builders (JOOQ, Knex.js, TypeORM QueryBuilder)
- HTTP request builders (`curl_easy_setopt`, `OkHttpClient.Builder`)
- Config objects with many optional fields (protobuf builders)
- Pizza / burger ordering system in OOP examples

## Pros / Cons

| Pros | Cons |
|------|------|
| Eliminates telescoping constructors | More code than simple constructor |
| Step-by-step construction; partial builds possible | Product must be mutable during construction |
| Reuse same construction logic for different representations | |

---

# Prototype

## Intent
Specify the kinds of objects to create using a prototypical instance, and create new objects by copying this prototype.

## When to Use
- Object creation is expensive (DB query, network call) and you need many similar objects
- You want to create objects that are copies of existing objects at runtime
- Class hierarchies of factories parallel class hierarchies of products (avoids this with `clone()`)
- When the number of classes is large and you don't want a factory for each

## C++ Implementation

```cpp
#include <iostream>
#include <string>
#include <memory>
#include <unordered_map>
#include <vector>

// Abstract prototype
class Shape {
protected:
    std::string color_;
    int         x_, y_;
public:
    Shape(std::string color, int x, int y) : color_(std::move(color)), x_(x), y_(y) {}
    virtual ~Shape() = default;

    // Each subclass implements clone()
    virtual std::unique_ptr<Shape> clone() const = 0;
    virtual void draw() const = 0;

    void setPosition(int x, int y) { x_ = x; y_ = y; }
};

// Concrete prototypes
class Circle : public Shape {
    int radius_;
public:
    Circle(std::string color, int x, int y, int radius)
        : Shape(std::move(color), x, y), radius_(radius) {}

    std::unique_ptr<Shape> clone() const override {
        return std::make_unique<Circle>(*this); // copy constructor
    }
    void draw() const override {
        std::cout << "Circle color=" << color_ << " r=" << radius_
                  << " at (" << x_ << "," << y_ << ")\n";
    }
};

class Rectangle : public Shape {
    int width_, height_;
public:
    Rectangle(std::string color, int x, int y, int w, int h)
        : Shape(std::move(color), x, y), width_(w), height_(h) {}

    std::unique_ptr<Shape> clone() const override {
        return std::make_unique<Rectangle>(*this);
    }
    void draw() const override {
        std::cout << "Rect color=" << color_ << " " << width_ << "x" << height_
                  << " at (" << x_ << "," << y_ << ")\n";
    }
};

// Prototype Registry — stores named prototypes for lookup
class ShapeRegistry {
    std::unordered_map<std::string, std::unique_ptr<Shape>> cache_;
public:
    void registerShape(const std::string& key, std::unique_ptr<Shape> shape) {
        cache_[key] = std::move(shape);
    }
    std::unique_ptr<Shape> getClone(const std::string& key) const {
        auto it = cache_.find(key);
        if (it == cache_.end()) return nullptr;
        return it->second->clone();
    }
};

int main() {
    ShapeRegistry registry;
    registry.registerShape("red_circle",   std::make_unique<Circle>("red",   0, 0, 10));
    registry.registerShape("blue_rect",    std::make_unique<Rectangle>("blue", 0, 0, 20, 10));

    // Cheap: clone from registry instead of re-creating
    auto c1 = registry.getClone("red_circle");
    auto c2 = registry.getClone("red_circle");
    c2->setPosition(50, 50);

    c1->draw();
    c2->draw(); // different position, same properties
}
```

## Real-World Use Cases
- Object pools with pre-configured instances
- Copy-on-write (COW) strings and containers
- Game objects — clone a "grunt enemy" template to spawn many enemies
- Document editors — duplicate a page or section
- JavaScript's prototype chain is literally this pattern

## Pros / Cons

| Pros | Cons |
|------|------|
| Avoids costly re-initialization | Deep copy vs shallow copy — must be implemented carefully |
| Hides complex object construction from client | Circular references make cloning tricky |
| Flexible: add/remove prototypes at runtime | Each class needs a `clone()` method |
