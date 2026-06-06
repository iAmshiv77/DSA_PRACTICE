// 02 — unique_ptr, shared_ptr, weak_ptr in action
//
//   g++ -std=c++20 -g 02_unique_shared_weak.cpp -o smart && ./smart
//
// Shows: ownership transfer (unique), reference counting (shared),
// and non-owning observation (weak).

#include <iostream>
#include <memory>

struct Widget {
    std::string name;
    explicit Widget(std::string n) : name(std::move(n)) {
        std::cout << "  + Widget(" << name << ")\n";
    }
    ~Widget() { std::cout << "  - ~Widget(" << name << ")\n"; }
    void use() const { std::cout << "  using " << name << "\n"; }
};

void demoUnique() {
    std::cout << "[unique_ptr] sole ownership, move-only\n";
    auto a = std::make_unique<Widget>("A");
    a->use();
    auto b = std::move(a);                 // ownership transferred to b
    std::cout << "  a is " << (a ? "non-null" : "null (moved-from)") << "\n";
    b->use();
}   // b frees Widget A here

void demoShared() {
    std::cout << "\n[shared_ptr] shared ownership via ref count\n";
    auto s1 = std::make_shared<Widget>("S");
    std::cout << "  use_count = " << s1.use_count() << "\n";   // 1
    {
        auto s2 = s1;                       // copy → count 2
        std::cout << "  use_count = " << s1.use_count() << "\n";
        s2->use();
    }                                       // s2 gone → count back to 1
    std::cout << "  use_count = " << s1.use_count() << "\n";   // 1
}   // s1 gone → count 0 → Widget S freed

void demoWeak() {
    std::cout << "\n[weak_ptr] non-owning observer\n";
    std::weak_ptr<Widget> w;
    {
        auto s = std::make_shared<Widget>("W");
        w = s;                              // weak does NOT raise count
        std::cout << "  use_count (strong) = " << s.use_count() << "\n";  // still 1
        if (auto locked = w.lock()) locked->use();   // promote & use safely
    }   // s gone → Widget W freed even though w still "points" at it
    std::cout << "  after scope: w.expired() = " << std::boolalpha << w.expired() << "\n";
    if (auto locked = w.lock()) locked->use();
    else std::cout << "  w.lock() returned null — object already gone (no dangling!)\n";
}

int main() {
    demoUnique();
    demoShared();
    demoWeak();
}
