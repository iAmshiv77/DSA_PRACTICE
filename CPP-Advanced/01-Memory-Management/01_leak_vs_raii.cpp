// 01 — Raw-pointer leak vs RAII fix
//
// Demonstrates the three classic leak sources and how RAII / smart pointers
// eliminate every one of them.
//
// Build with AddressSanitizer to SEE the leaks reported on exit:
//   g++ -std=c++20 -fsanitize=address -g 01_leak_vs_raii.cpp -o leak && ./leak
//
// ASan will report leaks from leakForgot(), leakOverwrite(), leakException()
// and report ZERO leaks from the noLeak* functions.

#include <iostream>
#include <memory>
#include <stdexcept>

struct Resource {
    int id;
    explicit Resource(int i) : id(i) { std::cout << "  + Resource " << id << " acquired\n"; }
    ~Resource()                       { std::cout << "  - Resource " << id << " released\n"; }
};

// ---------------------------------------------------------------- LEAKS ----

// 1. Forgot delete
void leakForgot() {
    Resource* r = new Resource(1);
    r->id = 42;
    // ... no delete → leak
}

// 2. Overwriting the only pointer
void leakOverwrite() {
    Resource* r = new Resource(2);
    r = new Resource(3);   // pointer to Resource 2 is lost → Resource 2 leaks
    delete r;              // only Resource 3 freed
}

// 3. Exception between new and delete
void leakException() {
    Resource* r = new Resource(4);
    throw std::runtime_error("boom");   // delete below never runs → leak
    delete r;                           // unreachable
}

// ------------------------------------------------------------- RAII FIX ----

// 1' unique_ptr — frees automatically, no delete to forget
void noLeakUnique() {
    auto r = std::make_unique<Resource>(10);
    r->id = 42;
}   // freed here, guaranteed

// 2' Reassigning a unique_ptr frees the old object automatically
void noLeakOverwrite() {
    auto r = std::make_unique<Resource>(11);
    r = std::make_unique<Resource>(12);   // Resource 11 freed on reassignment
}   // Resource 12 freed here

// 3' Exception-safe by construction — dtor runs during stack unwinding
void noLeakException() {
    auto r = std::make_unique<Resource>(13);
    try {
        throw std::runtime_error("boom");
    } catch (const std::exception& e) {
        std::cout << "  caught: " << e.what() << " (Resource still freed safely)\n";
    }
}   // Resource 13 freed here regardless of the throw

int main() {
    std::cout << "--- Leaky versions (ASan will flag these) ---\n";
    leakForgot();
    leakOverwrite();
    try { leakException(); } catch (...) { std::cout << "  caught boom from leakException\n"; }

    std::cout << "\n--- RAII versions (zero leaks) ---\n";
    noLeakUnique();
    noLeakOverwrite();
    noLeakException();
}
