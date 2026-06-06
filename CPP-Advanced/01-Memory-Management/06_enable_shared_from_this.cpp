// 06 — enable_shared_from_this: getting a shared_ptr to `this` safely
//
//   g++ -std=c++20 -fsanitize=address -g 06_enable_shared_from_this.cpp -o esft && ./esft
//
// The WRONG way (shared_ptr<T>(this)) makes a 2nd control block → double free,
// which ASan reports. The RIGHT way shares the existing control block.

#include <iostream>
#include <memory>
#include <vector>

// A registry that keeps objects alive while "subscribed"
struct Registry {
    std::vector<std::shared_ptr<struct Session>> subs;
    void add(std::shared_ptr<Session> s) { subs.push_back(std::move(s)); }
} registry;

struct Session : std::enable_shared_from_this<Session> {
    int id;
    explicit Session(int i) : id(i) { std::cout << "  + Session " << id << "\n"; }
    ~Session()                       { std::cout << "  - Session " << id << "\n"; }

    void subscribe() {
        // ✅ shares the EXISTING control block — refcount goes up, single ownership graph
        registry.add(shared_from_this());
        std::cout << "  Session " << id << " subscribed; use_count="
                  << shared_from_this().use_count() << "\n";
    }

    // ❌ DON'T: this would make a separate control block → double free at program end
    // void bad() { registry.add(std::shared_ptr<Session>(this)); }
};

int main() {
    auto s = std::make_shared<Session>(1);   // object owned by a shared_ptr first
    s->subscribe();                          // safe self-reference
    std::cout << "main holds it; registry holds it. One control block, no double free.\n";
    // both `s` and registry.subs are destroyed cleanly — Session freed exactly once
}
