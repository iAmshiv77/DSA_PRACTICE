// 04 — RAII for NON-memory resources + custom deleters
//
//   g++ -std=c++20 -g 04_raii_custom_deleter.cpp -o raii && ./raii
//
// Smart pointers manage ANY resource, not just `new`-ed memory, via a custom
// deleter. Shown: a C FILE* and a faux C-API handle, both leak-proof.

#include <iostream>
#include <memory>
#include <cstdio>

// --- Pattern A: unique_ptr with a custom deleter for a C resource -----------
void demoFile() {
    std::cout << "[FILE* via unique_ptr custom deleter]\n";
    // Deleter type is part of the unique_ptr type when using a function pointer.
    std::unique_ptr<std::FILE, decltype(&std::fclose)> fp(
        std::fopen("temp_demo.txt", "w"), &std::fclose);

    if (!fp) { std::cout << "  open failed\n"; return; }
    std::fputs("RAII closes this file automatically\n", fp.get());
    std::cout << "  wrote to file; fclose runs on scope exit\n";
}   // fclose(fp) called here — no manual cleanup, exception-safe

// --- Pattern B: a faux C API (acquire/release pair) -------------------------
using Handle = int;
Handle acquireHandle() { std::cout << "  + handle acquired\n"; return 7; }
void   releaseHandle(Handle h) { std::cout << "  - handle " << h << " released\n"; }

void demoHandle() {
    std::cout << "\n[opaque C handle via shared_ptr custom deleter]\n";
    // A lambda deleter; shared_ptr<void> can hold any resource with a deleter.
    Handle h = acquireHandle();
    std::shared_ptr<void> guard(nullptr, [h](void*) { releaseHandle(h); });
    std::cout << "  doing work with handle " << h << "\n";
}   // lambda deleter runs → releaseHandle(7)

// --- Pattern C: scope guard (run arbitrary cleanup on exit) -----------------
template <class F>
struct ScopeGuard {
    F fn; bool active = true;
    explicit ScopeGuard(F f) : fn(std::move(f)) {}
    ~ScopeGuard() { if (active) fn(); }
    void dismiss() { active = false; }
};

void demoScopeGuard() {
    std::cout << "\n[generic scope guard]\n";
    int* buf = new int[4];
    ScopeGuard cleanup([&] { delete[] buf; std::cout << "  buffer freed by guard\n"; });
    buf[0] = 1;
    std::cout << "  using raw buffer safely; guard frees it on every exit path\n";
}   // cleanup runs delete[]

int main() {
    demoFile();
    demoHandle();
    demoScopeGuard();
    std::remove("temp_demo.txt");
}
