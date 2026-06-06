# Structural Design Patterns

Structural patterns deal with how classes and objects are composed to form larger structures. They simplify the structure by identifying the relationship between entities.

---

# Adapter

## Intent
Convert the interface of a class into another interface that clients expect. Adapter lets classes work together that otherwise could not because of incompatible interfaces.

## When to Use
- You want to use an existing class but its interface does not match what you need
- You are integrating a third-party library whose API you cannot modify
- You need a reusable class that cooperates with unrelated or unforeseen classes

## C++ Implementation

```cpp
#include <iostream>
#include <string>
#include <memory>

// ── Target interface — what the client expects ────────────────────────────────
class JsonLogger {
public:
    virtual ~JsonLogger() = default;
    virtual void logJson(const std::string& json) = 0;
};

// ── Adaptee — existing class with incompatible interface ──────────────────────
// This is a third-party library we cannot modify
class LegacyFileLogger {
public:
    void writeToFile(const std::string& level,
                     const std::string& message,
                     const std::string& timestamp) {
        std::cout << "[FILE] " << timestamp << " [" << level << "] " << message << "\n";
    }
};

// ── Adapter — wraps LegacyFileLogger to satisfy JsonLogger interface ──────────
class FileLoggerAdapter : public JsonLogger {
    LegacyFileLogger legacy_;

    // Parse minimal JSON: { "level":"INFO", "msg":"hello", "ts":"2024-01-01" }
    std::string extract(const std::string& json, const std::string& key) const {
        std::string search = "\"" + key + "\":\"";
        auto start = json.find(search);
        if (start == std::string::npos) return "";
        start += search.size();
        auto end = json.find('"', start);
        return json.substr(start, end - start);
    }

public:
    void logJson(const std::string& json) override {
        std::string level = extract(json, "level");
        std::string msg   = extract(json, "msg");
        std::string ts    = extract(json, "ts");
        legacy_.writeToFile(level, msg, ts);  // delegate to adaptee
    }
};

// ── Client — only knows JsonLogger ───────────────────────────────────────────
void doLogging(JsonLogger& logger) {
    logger.logJson(R"({"level":"INFO","msg":"Server started","ts":"2024-01-01"})");
    logger.logJson(R"({"level":"ERROR","msg":"DB connection failed","ts":"2024-01-01"})");
}

int main() {
    FileLoggerAdapter adapter;
    doLogging(adapter);
}
```

## Real-World Use Cases
- **NestJS**: `TypeOrmLogger` adapts TypeORM's internal logger to NestJS `LoggerService` interface
- **Node.js**: `stream.Transform` adapts old-style callbacks to the streams API
- **React**: Wrapping a jQuery plugin component behind a React component interface
- **Payment integrations**: Wrapping Stripe, Razorpay, and PayPal behind a common `IPaymentGateway` adapter

---

# Bridge

## Intent
Decouple an abstraction from its implementation so that the two can vary independently. Instead of a monolithic hierarchy, maintain two separate hierarchies — one for abstractions and one for implementations — linked by a reference (the bridge).

## When to Use
- You want to avoid a permanent binding between abstraction and implementation
- Both abstraction and implementation should be extensible via subclassing
- Changes to the implementation should not impact client code
- You have a Cartesian-product class explosion: `[Shape × RenderEngine]` → `CircleOpenGL`, `CircleVulkan`, `RectOpenGL`...

## C++ Implementation

```cpp
#include <iostream>
#include <string>
#include <memory>

// ── Implementation interface ────────────────────────────────────────────────
class Renderer {
public:
    virtual ~Renderer() = default;
    virtual void drawCircle(double x, double y, double radius) const = 0;
    virtual void drawRect(double x, double y, double w, double h) const = 0;
    virtual std::string name() const = 0;
};

// ── Concrete Implementations ─────────────────────────────────────────────────
class OpenGLRenderer : public Renderer {
public:
    void drawCircle(double x, double y, double r) const override {
        std::cout << "[OpenGL] Circle at (" << x << "," << y << ") r=" << r << "\n";
    }
    void drawRect(double x, double y, double w, double h) const override {
        std::cout << "[OpenGL] Rect at (" << x << "," << y << ") " << w << "x" << h << "\n";
    }
    std::string name() const override { return "OpenGL"; }
};

class VulkanRenderer : public Renderer {
public:
    void drawCircle(double x, double y, double r) const override {
        std::cout << "[Vulkan] Circle at (" << x << "," << y << ") r=" << r << "\n";
    }
    void drawRect(double x, double y, double w, double h) const override {
        std::cout << "[Vulkan] Rect at (" << x << "," << y << ") " << w << "x" << h << "\n";
    }
    std::string name() const override { return "Vulkan"; }
};

// ── Abstraction hierarchy ────────────────────────────────────────────────────
class Shape {
protected:
    Renderer& renderer_;  // ← the bridge
public:
    explicit Shape(Renderer& r) : renderer_(r) {}
    virtual ~Shape() = default;
    virtual void draw() const = 0;
    virtual void resize(double factor) = 0;
};

class Circle : public Shape {
    double x_, y_, radius_;
public:
    Circle(Renderer& r, double x, double y, double radius)
        : Shape(r), x_(x), y_(y), radius_(radius) {}

    void draw() const override {
        renderer_.drawCircle(x_, y_, radius_);
    }
    void resize(double factor) override { radius_ *= factor; }
};

class Rectangle : public Shape {
    double x_, y_, width_, height_;
public:
    Rectangle(Renderer& r, double x, double y, double w, double h)
        : Shape(r), x_(x), y_(y), width_(w), height_(h) {}

    void draw() const override {
        renderer_.drawRect(x_, y_, width_, height_);
    }
    void resize(double factor) override { width_ *= factor; height_ *= factor; }
};

int main() {
    OpenGLRenderer opengl;
    VulkanRenderer vulkan;

    Circle    c1(opengl, 5, 5, 10);
    Circle    c2(vulkan,  3, 3,  7);
    Rectangle r1(opengl, 0, 0, 20, 15);

    c1.draw();  // OpenGL renders circle
    c2.draw();  // Vulkan renders circle
    r1.draw();  // OpenGL renders rect

    // Switch renderer at runtime — no new subclasses needed
    Circle c3(vulkan, 1, 1, 3);
    c3.draw();
}
```

## Real-World Use Cases
- **Node.js**: `fs` module abstracts file operations (abstraction) over OS-level syscalls (implementation)
- **NestJS**: `LoggerService` abstraction bridged to `ConsoleLogger`, `WinstonLogger`, `PinoLogger` implementations
- **React**: Component abstraction bridged to `ReactDOM` (web) vs `ReactNative` (mobile) renderers — same JSX, different platform output

---

# Composite

## Intent
Compose objects into tree structures to represent part-whole hierarchies. Composite lets clients treat individual objects and compositions of objects uniformly.

## When to Use
- You need to represent hierarchies of objects (file system, UI component trees, org charts)
- You want clients to ignore the difference between single objects and compositions
- Operations should be applicable recursively to the entire tree

## C++ Implementation

```cpp
#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <numeric>

// ── Component — common interface ────────────────────────────────────────────
class FileSystemNode {
protected:
    std::string name_;
public:
    explicit FileSystemNode(std::string name) : name_(std::move(name)) {}
    virtual ~FileSystemNode() = default;

    const std::string& getName() const { return name_; }

    virtual long long  getSize()  const = 0;
    virtual void       print(int indent = 0) const = 0;
    virtual bool       isDirectory() const = 0;
};

// ── Leaf — File ──────────────────────────────────────────────────────────────
class File : public FileSystemNode {
    long long size_;
public:
    File(std::string name, long long size)
        : FileSystemNode(std::move(name)), size_(size) {}

    long long getSize()  const override { return size_; }
    bool isDirectory()   const override { return false; }

    void print(int indent = 0) const override {
        std::cout << std::string(indent * 2, ' ')
                  << "📄 " << name_ << " (" << size_ << " bytes)\n";
    }
};

// ── Composite — Directory ─────────────────────────────────────────────────────
class Directory : public FileSystemNode {
    std::vector<std::unique_ptr<FileSystemNode>> children_;
public:
    explicit Directory(std::string name) : FileSystemNode(std::move(name)) {}

    void add(std::unique_ptr<FileSystemNode> node) {
        children_.push_back(std::move(node));
    }

    bool isDirectory() const override { return true; }

    // Recursively sums size of all children
    long long getSize() const override {
        long long total = 0;
        for (const auto& child : children_) {
            total += child->getSize();
        }
        return total;
    }

    void print(int indent = 0) const override {
        std::cout << std::string(indent * 2, ' ')
                  << "📁 " << name_ << "/ (" << getSize() << " bytes)\n";
        for (const auto& child : children_) {
            child->print(indent + 1);
        }
    }
};

int main() {
    auto root = std::make_unique<Directory>("root");

    auto src = std::make_unique<Directory>("src");
    src->add(std::make_unique<File>("main.cpp",    2048));
    src->add(std::make_unique<File>("utils.cpp",   1024));
    src->add(std::make_unique<File>("utils.h",      512));

    auto test = std::make_unique<Directory>("test");
    test->add(std::make_unique<File>("main_test.cpp", 3072));

    root->add(std::move(src));
    root->add(std::move(test));
    root->add(std::make_unique<File>("README.md", 256));

    root->print();
    std::cout << "Total size: " << root->getSize() << " bytes\n";
}
```

## Real-World Use Cases
- **React**: The component tree is a Composite — `render()` works uniformly on leaf nodes (HTML elements) and composite nodes (custom components)
- **NestJS**: Module dependency tree; `RouterModule` recursively registers route prefixes
- **Node.js**: `EventEmitter` chains, middleware composition (`express.Router`)

---

# Decorator

## Intent
Attach additional responsibilities to an object dynamically. Decorators provide a flexible alternative to subclassing for extending functionality.

## When to Use
- You want to add behavior to individual objects without affecting others of the same class
- Extension by subclassing is impractical (causes a class explosion)
- Behaviors should be composable: logging + caching + retry all at once

## C++ Implementation

```cpp
#include <iostream>
#include <string>
#include <memory>
#include <chrono>

// ── Component interface ────────────────────────────────────────────────────
class DataSource {
public:
    virtual ~DataSource() = default;
    virtual void     writeData(const std::string& data) = 0;
    virtual std::string readData() const = 0;
};

// ── Concrete component ─────────────────────────────────────────────────────
class FileDataSource : public DataSource {
    std::string filename_;
    std::string stored_; // simulate file storage
public:
    explicit FileDataSource(std::string filename)
        : filename_(std::move(filename)) {}

    void writeData(const std::string& data) override {
        stored_ = data;
        std::cout << "[File:" << filename_ << "] Written " << data.size() << " bytes\n";
    }
    std::string readData() const override {
        std::cout << "[File:" << filename_ << "] Read " << stored_.size() << " bytes\n";
        return stored_;
    }
};

// ── Base decorator ─────────────────────────────────────────────────────────
class DataSourceDecorator : public DataSource {
protected:
    std::unique_ptr<DataSource> wrapped_;
public:
    explicit DataSourceDecorator(std::unique_ptr<DataSource> source)
        : wrapped_(std::move(source)) {}

    void writeData(const std::string& data) override { wrapped_->writeData(data); }
    std::string readData() const override             { return wrapped_->readData(); }
};

// ── Concrete decorator 1: Encryption ─────────────────────────────────────
class EncryptionDecorator : public DataSourceDecorator {
    // XOR "encryption" for demonstration purposes only
    std::string xorTransform(const std::string& input) const {
        std::string out = input;
        for (char& c : out) c ^= 0x5A;
        return out;
    }
public:
    using DataSourceDecorator::DataSourceDecorator;

    void writeData(const std::string& data) override {
        std::cout << "[Encrypt] Encrypting before write\n";
        wrapped_->writeData(xorTransform(data));
    }
    std::string readData() const override {
        std::cout << "[Encrypt] Decrypting after read\n";
        return xorTransform(wrapped_->readData());
    }
};

// ── Concrete decorator 2: Compression ─────────────────────────────────────
class CompressionDecorator : public DataSourceDecorator {
public:
    using DataSourceDecorator::DataSourceDecorator;

    void writeData(const std::string& data) override {
        std::cout << "[Compress] Compressing " << data.size() << " bytes\n";
        wrapped_->writeData("COMPRESSED[" + data + "]");
    }
    std::string readData() const override {
        std::string raw = wrapped_->readData();
        std::cout << "[Compress] Decompressing\n";
        // Strip COMPRESSED[] wrapper
        if (raw.size() > 12) return raw.substr(11, raw.size() - 12);
        return raw;
    }
};

int main() {
    // Stack: File ← Encrypt ← Compress
    // Write path: compress first, then encrypt, then write to file
    // Read  path: read from file, decrypt, decompress
    auto source = std::make_unique<FileDataSource>("data.bin");
    auto enc    = std::make_unique<EncryptionDecorator>(std::move(source));
    auto comp   = std::make_unique<CompressionDecorator>(std::move(enc));

    comp->writeData("Hello, Decorator Pattern!");
    std::cout << "\n";
    std::string result = comp->readData();
    std::cout << "Final read: " << result << "\n";
}
```

## Real-World Use Cases
- **NestJS interceptors**: `LoggingInterceptor`, `CacheInterceptor`, `TransformInterceptor` wrap handlers — pure Decorator chain
- **Node.js streams**: `zlib.createGzip()` wraps a writable stream; `crypto.createCipheriv()` wraps that — stackable decorators
- **React**: Higher-Order Components (HOC) like `withAuth(withLogging(Component))` are decorators
- **HTTP middleware**: `express` / `fastify` middleware chains add auth, logging, rate-limiting around a route handler

---

# Facade

## Intent
Provide a simplified, unified interface to a complex subsystem. A Facade hides subsystem complexity and provides a higher-level interface that makes the subsystem easier to use.

## When to Use
- A complex subsystem has many interdependent classes that clients must coordinate
- You want to layer subsystems: a Facade defines an entry point to each layer
- You need to decouple clients from the internal implementation of a subsystem

## C++ Implementation

```cpp
#include <iostream>
#include <string>

// ── Complex subsystem classes ─────────────────────────────────────────────

class CPU {
public:
    void freeze()   { std::cout << "[CPU] Freeze\n"; }
    void jump(long addr) { std::cout << "[CPU] Jump to address " << addr << "\n"; }
    void execute()  { std::cout << "[CPU] Execute\n"; }
};

class Memory {
public:
    void load(long addr, const std::string& data) {
        std::cout << "[Memory] Load '" << data << "' at addr " << addr << "\n";
    }
};

class HardDrive {
public:
    std::string read(long sector, int size) {
        std::cout << "[HardDrive] Read " << size << " bytes from sector " << sector << "\n";
        return "BOOT_DATA";
    }
};

class VideoCard {
public:
    void initialize(int width, int height) {
        std::cout << "[VideoCard] Init " << width << "x" << height << "\n";
    }
};

class BIOSChip {
public:
    void powerOn() { std::cout << "[BIOS] Power-on self-test\n"; }
};

// ── Facade ─────────────────────────────────────────────────────────────────
// Client only interacts with ComputerFacade — never the subsystem directly
class ComputerFacade {
    CPU       cpu_;
    Memory    memory_;
    HardDrive hdd_;
    VideoCard video_;
    BIOSChip  bios_;

    static constexpr long BOOT_SECTOR  = 0;
    static constexpr int  SECTOR_SIZE  = 512;
    static constexpr long LOAD_ADDRESS = 0x7C00;

public:
    // One simple call replaces ~10 coordinated subsystem calls
    void start() {
        std::cout << "=== Starting Computer ===\n";
        bios_.powerOn();
        video_.initialize(1920, 1080);
        cpu_.freeze();
        std::string bootData = hdd_.read(BOOT_SECTOR, SECTOR_SIZE);
        memory_.load(LOAD_ADDRESS, bootData);
        cpu_.jump(LOAD_ADDRESS);
        cpu_.execute();
        std::cout << "=== Boot Complete ===\n";
    }

    void shutdown() {
        std::cout << "=== Shutting down ===\n";
        // Coordinate shutdown sequence
    }
};

int main() {
    ComputerFacade computer;
    computer.start();
    // Client never knew about BIOS, CPU freeze sequence, memory addresses
}
```

## Real-World Use Cases
- **NestJS**: `AuthService` is a facade over `JwtService`, `UserRepository`, `RedisService`, `OtpService` — callers just call `login(dto)`
- **Node.js**: `mongoose.connect()` is a facade over TCP socket management, connection pooling, retry logic
- **React**: `useAuth()` custom hook is a facade over `useContext`, `localStorage`, token refresh logic
- **AWS SDK**: `S3.upload()` is a facade over multipart uploads, retry logic, checksum validation

---

# Flyweight

## Intent
Use sharing to support a large number of fine-grained objects efficiently. The Flyweight stores the shared (intrinsic) state in a shared object and the unique (extrinsic) state outside it.

## When to Use
- An application uses a large number of objects that consume a lot of memory
- Most of the object state can be made extrinsic (moved outside the object)
- Groups of objects can be replaced by a few shared objects once extrinsic state is removed
- Classic example: character glyphs in a text editor (one `Glyph` object per character type, not per occurrence)

## C++ Implementation

```cpp
#include <iostream>
#include <map>
#include <string>
#include <memory>
#include <vector>

// ── Flyweight — shared glyph data (intrinsic state) ──────────────────────────
class CharGlyph {
    char        character_;   // intrinsic — shared
    std::string fontFamily_;  // intrinsic — shared
    int         fontSize_;    // intrinsic — shared
public:
    CharGlyph(char c, std::string font, int size)
        : character_(c), fontFamily_(std::move(font)), fontSize_(size) {}

    // Render takes extrinsic state (position in document) as parameter
    void render(int x, int y, const std::string& color) const {
        std::cout << "Render '" << character_
                  << "' [" << fontFamily_ << " " << fontSize_ << "pt"
                  << " color:" << color << "] at (" << x << "," << y << ")\n";
    }

    char getChar() const { return character_; }
};

// ── Flyweight Factory — ensures glyphs are shared ────────────────────────────
class GlyphFactory {
    // Key: (char, font, size) → shared glyph
    std::map<std::string, std::shared_ptr<CharGlyph>> glyphs_;

    std::string makeKey(char c, const std::string& font, int size) const {
        return std::string(1, c) + "|" + font + "|" + std::to_string(size);
    }

public:
    std::shared_ptr<CharGlyph> getGlyph(char c, const std::string& font, int size) {
        std::string key = makeKey(c, font, size);
        auto it = glyphs_.find(key);
        if (it != glyphs_.end()) return it->second;  // reuse existing

        auto glyph = std::make_shared<CharGlyph>(c, font, size);
        glyphs_[key] = glyph;
        std::cout << "[Factory] Created new glyph for '" << c << "'\n";
        return glyph;
    }

    int getGlyphCount() const { return static_cast<int>(glyphs_.size()); }
};

// ── Context — holds extrinsic state per character occurrence ─────────────────
struct CharContext {
    std::shared_ptr<CharGlyph> glyph;  // shared flyweight
    int         x, y;                  // extrinsic — unique per occurrence
    std::string color;                 // extrinsic
};

// ── Client — a document with many characters ─────────────────────────────────
class TextDocument {
    GlyphFactory             factory_;
    std::vector<CharContext> characters_;
public:
    void addChar(char c, const std::string& font, int size,
                 int x, int y, const std::string& color) {
        auto glyph = factory_.getGlyph(c, font, size);
        characters_.push_back({ glyph, x, y, color });
    }

    void render() const {
        for (const auto& ctx : characters_) {
            ctx.glyph->render(ctx.x, ctx.y, ctx.color);
        }
    }

    void printStats() const {
        std::cout << "Characters: " << characters_.size()
                  << ", Unique glyphs: " << factory_.getGlyphCount() << "\n";
    }
};

int main() {
    TextDocument doc;

    // "Hello World" — 11 characters, but only ~7 unique glyphs (H,e,l,o, ,W,r,d)
    std::string text   = "Hello World";
    std::string font   = "Arial";
    int         size   = 12;
    int         xPos   = 10;
    for (char c : text) {
        doc.addChar(c, font, size, xPos, 20, "black");
        xPos += 8;
    }

    // Change color for second word
    xPos = 10;
    for (char c : text) {
        doc.addChar(c, font, size, xPos, 40, "blue");
        xPos += 8;
    }

    doc.printStats();  // 22 characters, but only 8 unique glyphs
}
```

## Real-World Use Cases
- **Node.js**: `Buffer` pools reuse allocated memory rather than creating new buffers per request
- **NestJS**: Guards and interceptors are singletons shared across requests — per-request state passed as context parameter (extrinsic state)
- **React**: `React.memo` caches component renders — the memoized component is the flyweight; props are extrinsic state
- **Game dev**: Tree/grass sprites — one texture object (intrinsic), thousands of instances with unique positions (extrinsic)

---

# Proxy

## Intent
Provide a surrogate or placeholder for another object to control access to it. The Proxy has the same interface as the real subject and controls when and how the real subject is accessed.

## Proxy Types
- **Virtual Proxy**: defers expensive object creation until first use (lazy init)
- **Protection Proxy**: controls access based on permissions
- **Remote Proxy**: represents an object in a different address space
- **Caching Proxy**: caches results of expensive operations

## C++ Implementation

```cpp
#include <iostream>
#include <string>
#include <memory>
#include <map>
#include <stdexcept>

// ── Subject interface ──────────────────────────────────────────────────────
class DatabaseService {
public:
    virtual ~DatabaseService() = default;
    virtual std::string query(const std::string& sql) = 0;
};

// ── Real Subject ───────────────────────────────────────────────────────────
class RealDatabaseService : public DatabaseService {
public:
    RealDatabaseService() {
        std::cout << "[DB] Establishing connection (expensive)...\n";
    }
    std::string query(const std::string& sql) override {
        std::cout << "[DB] Executing: " << sql << "\n";
        return "RESULT_FOR[" + sql + "]";
    }
};

// ── Virtual Proxy — lazy initialization ────────────────────────────────────
class LazyDatabaseProxy : public DatabaseService {
    mutable std::unique_ptr<RealDatabaseService> real_; // created on first use
public:
    std::string query(const std::string& sql) override {
        if (!real_) {
            std::cout << "[VirtualProxy] First query — initializing real DB...\n";
            real_ = std::make_unique<RealDatabaseService>();
        }
        return real_->query(sql);
    }
};

// ── Protection Proxy — access control ──────────────────────────────────────
enum class AccessLevel { READ_ONLY, READ_WRITE, ADMIN };

class ProtectionDatabaseProxy : public DatabaseService {
    std::unique_ptr<DatabaseService> real_;
    AccessLevel                      level_;

    bool isWriteQuery(const std::string& sql) const {
        return sql.rfind("INSERT", 0) == 0 ||
               sql.rfind("UPDATE", 0) == 0 ||
               sql.rfind("DELETE", 0) == 0;
    }
public:
    ProtectionDatabaseProxy(std::unique_ptr<DatabaseService> real, AccessLevel level)
        : real_(std::move(real)), level_(level) {}

    std::string query(const std::string& sql) override {
        if (level_ == AccessLevel::READ_ONLY && isWriteQuery(sql)) {
            throw std::runtime_error("[ProtectionProxy] Write denied for READ_ONLY user");
        }
        std::cout << "[ProtectionProxy] Access granted\n";
        return real_->query(sql);
    }
};

// ── Caching Proxy ──────────────────────────────────────────────────────────
class CachingDatabaseProxy : public DatabaseService {
    std::unique_ptr<DatabaseService>  real_;
    std::map<std::string, std::string> cache_;
public:
    explicit CachingDatabaseProxy(std::unique_ptr<DatabaseService> real)
        : real_(std::move(real)) {}

    std::string query(const std::string& sql) override {
        auto it = cache_.find(sql);
        if (it != cache_.end()) {
            std::cout << "[CacheProxy] Cache HIT for: " << sql << "\n";
            return it->second;
        }
        std::cout << "[CacheProxy] Cache MISS — querying DB\n";
        std::string result = real_->query(sql);
        cache_[sql] = result;
        return result;
    }
};

int main() {
    std::cout << "=== Virtual Proxy ===\n";
    LazyDatabaseProxy lazy;
    std::cout << "Proxy created — DB not connected yet\n";
    lazy.query("SELECT * FROM users");   // DB initialized here
    lazy.query("SELECT * FROM orders");  // reuses existing connection

    std::cout << "\n=== Protection Proxy ===\n";
    auto realDb = std::make_unique<RealDatabaseService>();
    ProtectionDatabaseProxy readOnly(std::move(realDb), AccessLevel::READ_ONLY);
    readOnly.query("SELECT * FROM users");
    try {
        readOnly.query("DELETE FROM users WHERE id=1");
    } catch (const std::exception& e) {
        std::cout << e.what() << "\n";
    }

    std::cout << "\n=== Caching Proxy ===\n";
    auto db    = std::make_unique<RealDatabaseService>();
    CachingDatabaseProxy cache(std::move(db));
    cache.query("SELECT * FROM products");  // miss
    cache.query("SELECT * FROM products");  // hit
    cache.query("SELECT * FROM products");  // hit
}
```

## Real-World Use Cases
- **NestJS**: Guards act as Protection Proxies before route handlers
- **NestJS**: `CacheInterceptor` is a Caching Proxy for endpoint responses
- **Node.js ES6 Proxy**: `new Proxy(obj, handler)` is a language-native proxy for validation, logging, lazy loading
- **TypeORM**: Entity Manager is a Virtual Proxy — relations are not loaded until explicitly accessed (lazy relations)
- **React Query**: `useQuery` is a Caching Proxy — HTTP responses cached, stale-while-revalidate strategy

---

## Quick Reference

| Pattern   | Core Idea                                      | Key Signal in Interview Problem                           |
|-----------|------------------------------------------------|-----------------------------------------------------------|
| Adapter   | Translate interface A → interface B            | "We have a legacy class / third-party we cannot modify"   |
| Bridge    | Separate abstraction hierarchy from impl hierarchy | "Two dimensions of variation; avoid class explosion" |
| Composite | Treat leaf and container identically           | "Tree structure; recursive operations on hierarchy"       |
| Decorator | Wrap object to add behavior at runtime         | "Add features without subclassing; stackable behaviors"   |
| Facade    | One simple API over a complex subsystem        | "Client should not know about subsystem internals"        |
| Flyweight | Share common state across many objects         | "Millions of objects; memory is the bottleneck"           |
| Proxy     | Control access to another object               | "Lazy init / access control / caching / remote object"    |
