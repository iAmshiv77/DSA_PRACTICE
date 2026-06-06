// 03 — The shared_ptr reference-cycle leak, and the weak_ptr fix
//
// Build with leak detection to PROVE the cycle leaks and the fix doesn't:
//   g++ -std=c++20 -fsanitize=address -g 03_shared_ptr_cycle.cpp -o cycle && ./cycle
//
// With CYCLE defined, ASan reports a leak (destructors never print).
// Without it (weak_ptr back-edge), both nodes are freed cleanly.

#include <iostream>
#include <memory>

// Toggle this to compare. Default: the FIXED version.
// #define CYCLE   // <- uncomment to reproduce the leak

struct Node {
    int value;
    std::shared_ptr<Node> next;        // forward edge OWNS

#ifdef CYCLE
    std::shared_ptr<Node> prev;        // BUG: back-edge also owns → cycle → leak
#else
    std::weak_ptr<Node>   prev;        // FIX: back-edge observes → no cycle
#endif

    explicit Node(int v) : value(v) { std::cout << "  + Node(" << value << ")\n"; }
    ~Node()                          { std::cout << "  - ~Node(" << value << ")\n"; }
};

int main() {
    std::cout << "Building a <-> b doubly-linked pair\n";
    auto a = std::make_shared<Node>(1);
    auto b = std::make_shared<Node>(2);

    a->next = b;     // a owns b
    b->prev = a;     // back reference

    std::cout << "a.use_count = " << a.use_count() << "\n";
    std::cout << "b.use_count = " << b.use_count() << "\n";

    std::cout << "Leaving scope...\n";
    // On scope exit:
    //   CYCLE version  -> a and b keep each other alive, destructors NEVER run (leak)
    //   weak_ptr fix   -> counts reach 0, "- ~Node" prints for both (clean)
}
