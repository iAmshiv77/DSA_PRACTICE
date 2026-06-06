# LLD: Logging Framework (like Log4j / SLF4J)

## Requirements
- Log at levels: TRACE, DEBUG, INFO, WARN, ERROR, FATAL
- Multiple appenders: Console, File, Database, Remote (TCP)
- Multiple formatters: plain text, JSON, XML
- Hierarchical loggers (com.app.service inherits from com.app)
- Async logging (don't block main thread)
- Log rotation (by size/date)
- Filter messages by level

---

## Class Design

```
Logger
├── name: string
├── level: LogLevel (min level to log)
├── appenders: vector<Appender*>
├── parent: Logger*
├── propagate: bool  (send to parent appenders too)
└── log(LogLevel, message, ...)

LogLevel (enum, ordered)
  TRACE < DEBUG < INFO < WARN < ERROR < FATAL

LogRecord
├── level, message, timestamp
├── loggerName, threadId
├── file, line, function (source location)

Appender (interface)
├── append(LogRecord*)
└── setFormatter(Formatter*)

ConcreteAppenders:
  ConsoleAppender, FileAppender, AsyncAppender, DatabaseAppender

Formatter (interface)
└── format(LogRecord*) → string

ConcreteFormatters:
  PlainTextFormatter, JSONFormatter, XMLFormatter

Filter (interface)
└── isLoggable(LogRecord*) → bool

LogManager (Singleton)
├── loggers: map<string, Logger*>
├── getLogger(name) → Logger*
└── rootLogger: Logger*
```

---

## Hierarchy Design

```
Root Logger (level=WARN, appenders=[ConsoleAppender])
    ↑ parent
com (inherits from root)
    ↑ parent
com.app (level=DEBUG overrides)
    ↑ parent
com.app.service (propagate=true → also goes to com.app appenders)

LogManager.getLogger("com.app.service.UserService")
  → creates logger if not exists
  → walks up hierarchy to find parent
```

---

## C++ Implementation

```cpp
#include <bits/stdc++.h>
#include <fstream>
#include <mutex>
#include <thread>
#include <queue>
#include <condition_variable>
using namespace std;

enum class LogLevel { TRACE=0, DEBUG=1, INFO=2, WARN=3, ERROR=4, FATAL=5 };

string levelToString(LogLevel l) {
    const char* names[] = {"TRACE","DEBUG","INFO","WARN","ERROR","FATAL"};
    return names[static_cast<int>(l)];
}

struct LogRecord {
    LogLevel level;
    string message;
    string loggerName;
    string timestamp;
    thread::id threadId;
    string file;
    int line;

    string getTimestamp() const { return timestamp; }
};

// ── Formatter ────────────────────────────────────────────
class Formatter {
public:
    virtual string format(const LogRecord& r) = 0;
    virtual ~Formatter() = default;
};

class PlainTextFormatter : public Formatter {
public:
    string format(const LogRecord& r) override {
        return "[" + r.timestamp + "] ["
            + levelToString(r.level) + "] ["
            + r.loggerName + "] "
            + r.message;
    }
};

class JSONFormatter : public Formatter {
public:
    string format(const LogRecord& r) override {
        return "{\"level\":\"" + levelToString(r.level)
            + "\",\"logger\":\"" + r.loggerName
            + "\",\"message\":\"" + r.message
            + "\",\"time\":\"" + r.timestamp + "\"}";
    }
};

// ── Appender ─────────────────────────────────────────────
class Appender {
protected:
    unique_ptr<Formatter> formatter;
    mutex mtx;
public:
    Appender() : formatter(make_unique<PlainTextFormatter>()) {}
    void setFormatter(unique_ptr<Formatter> f) { formatter = move(f); }
    virtual void append(const LogRecord& record) = 0;
    virtual ~Appender() = default;
};

class ConsoleAppender : public Appender {
public:
    void append(const LogRecord& r) override {
        lock_guard<mutex> lock(mtx);
        cout << formatter->format(r) << "\n";
    }
};

class FileAppender : public Appender {
    ofstream file;
    size_t maxSize;
    size_t currentSize = 0;
    string filename;
    int rotationIndex = 0;

    void rotate() {
        file.close();
        string rotated = filename + "." + to_string(rotationIndex++);
        rename(filename.c_str(), rotated.c_str());
        file.open(filename, ios::app);
        currentSize = 0;
    }
public:
    FileAppender(const string& fname, size_t maxBytes = 10*1024*1024)
        : filename(fname), maxSize(maxBytes) {
        file.open(fname, ios::app);
    }

    void append(const LogRecord& r) override {
        lock_guard<mutex> lock(mtx);
        string formatted = formatter->format(r) + "\n";
        if (currentSize + formatted.size() > maxSize) rotate();
        file << formatted;
        file.flush();
        currentSize += formatted.size();
    }
};

// Async Appender — wraps another appender with a queue
class AsyncAppender : public Appender {
    unique_ptr<Appender> delegate;
    queue<LogRecord> logQueue;
    mutex qMutex;
    condition_variable cv;
    thread worker;
    bool running = true;

    void processQueue() {
        while (running || !logQueue.empty()) {
            unique_lock<mutex> lock(qMutex);
            cv.wait(lock, [&]{ return !logQueue.empty() || !running; });
            while (!logQueue.empty()) {
                LogRecord r = logQueue.front(); logQueue.pop();
                lock.unlock();
                delegate->append(r);
                lock.lock();
            }
        }
    }
public:
    AsyncAppender(unique_ptr<Appender> d)
        : delegate(move(d)),
          worker(&AsyncAppender::processQueue, this) {}

    ~AsyncAppender() {
        { lock_guard<mutex> lock(qMutex); running = false; }
        cv.notify_all();
        worker.join();
    }

    void append(const LogRecord& r) override {
        lock_guard<mutex> lock(qMutex);
        logQueue.push(r);
        cv.notify_one();
    }
};

// ── Logger ────────────────────────────────────────────────
class Logger {
    string name;
    LogLevel level;
    vector<shared_ptr<Appender>> appenders;
    Logger* parent;
    bool propagate;

    string currentTimestamp() {
        auto now = chrono::system_clock::now();
        auto t = chrono::system_clock::to_time_t(now);
        char buf[20];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&t));
        return string(buf);
    }

public:
    Logger(string n, LogLevel lvl = LogLevel::INFO,
           Logger* p = nullptr, bool prop = true)
        : name(n), level(lvl), parent(p), propagate(prop) {}

    void addAppender(shared_ptr<Appender> a) { appenders.push_back(a); }
    void setLevel(LogLevel l) { level = l; }

    void log(LogLevel msgLevel, const string& msg,
             const string& file = "", int line = 0) {
        if (msgLevel < level) return;  // below threshold

        LogRecord r{msgLevel, msg, name, currentTimestamp(),
                    this_thread::get_id(), file, line};

        for (auto& a : appenders) a->append(r);
        if (propagate && parent) parent->log(msgLevel, msg, file, line);
    }

    void trace(const string& msg) { log(LogLevel::TRACE, msg); }
    void debug(const string& msg) { log(LogLevel::DEBUG, msg); }
    void info (const string& msg) { log(LogLevel::INFO,  msg); }
    void warn (const string& msg) { log(LogLevel::WARN,  msg); }
    void error(const string& msg) { log(LogLevel::ERROR, msg); }
    void fatal(const string& msg) { log(LogLevel::FATAL, msg); }
};

// ── LogManager (Singleton) ────────────────────────────────
class LogManager {
    map<string, unique_ptr<Logger>> loggers;
    unique_ptr<Logger> rootLogger;
    mutex mtx;

    LogManager() {
        rootLogger = make_unique<Logger>("root", LogLevel::WARN);
        rootLogger->addAppender(make_shared<ConsoleAppender>());
    }

    Logger* findParent(const string& name) {
        size_t pos = name.rfind('.');
        if (pos == string::npos) return rootLogger.get();
        string parentName = name.substr(0, pos);
        return getLogger(parentName);
    }

public:
    static LogManager& getInstance() {
        static LogManager instance;
        return instance;
    }

    Logger* getLogger(const string& name) {
        lock_guard<mutex> lock(mtx);
        if (!loggers.count(name)) {
            Logger* parent = findParent(name);
            loggers[name] = make_unique<Logger>(name, LogLevel::INFO, parent);
        }
        return loggers[name].get();
    }

    Logger* getRootLogger() { return rootLogger.get(); }
};

// ── Usage ─────────────────────────────────────────────────
int main() {
    auto& lm = LogManager::getInstance();

    // Configure
    auto fileApp = make_shared<FileAppender>("app.log");
    fileApp->setFormatter(make_unique<JSONFormatter>());
    lm.getRootLogger()->addAppender(fileApp);
    lm.getRootLogger()->setLevel(LogLevel::DEBUG);

    // Get logger
    Logger* logger = lm.getLogger("com.app.service.UserService");
    logger->setLevel(LogLevel::DEBUG);

    logger->info("User login successful");
    logger->debug("User ID: 12345");
    logger->warn("Password expiry in 3 days");
    logger->error("DB connection failed");

    return 0;
}
```

---

## Edge Cases

| Edge Case | Solution |
|-----------|---------|
| Circular parent hierarchy | LogManager prevents: root has no parent |
| File not writable | Fallback to ConsoleAppender; throw on init |
| Queue fills up in async | Bounded queue; drop oldest or block producer |
| Log format string injection | Sanitize message before writing |
| Thread safety | Mutex per appender; LogRecord is immutable |
| Level filter per appender | Add level check inside Appender.append() |

---

## Interview Questions

**Q: Why separate Formatter from Appender?**
A: Single Responsibility. FileAppender handles I/O. Formatter handles representation. You can mix: FileAppender + JSON vs FileAppender + XML without touching appender code.

**Q: How does the hierarchy help in practice?**
A: Set root to WARN. Set com.myapp to DEBUG for your own code. Third-party libraries (also using this logger) are automatically filtered at WARN. Fine-grained control without touching each logger.

**Q: Why async logging?**
A: File I/O is ~100µs, DB I/O is ~10ms. Synchronous logging adds this latency to every request. Async: log record pushed to queue (< 1µs), background thread writes to file. Trade-off: logs may be lost on sudden crash (bounded queue may be partially flushed).
