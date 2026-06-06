# 🧠 CPP-Advanced

Senior-level C++ topics that go beyond DSA syntax — the language internals and
idioms expected of a 10+ year engineer.

## 📂 Topics

| Folder | Covers |
|--------|--------|
| [`01-Memory-Management/`](./01-Memory-Management) | Heap vs stack, what a leak really is, RAII, Rule of 0/3/5, `unique_ptr`/`shared_ptr`/`weak_ptr`, reference-cycle leaks, move semantics, `enable_shared_from_this`, Pimpl, non-owning views (`string_view`/`span`), exception-safety guarantees, RVO/copy elision, leak-detection tooling (ASan/Valgrind), allocators |

Each folder has a `notes.md` (concept deep-dive) plus runnable `.cpp` examples.

## 🛠️ Build & Run

```bash
# Plain build
g++ -std=c++20 -g 01-Memory-Management/02_unique_shared_weak.cpp -o demo && ./demo

# With AddressSanitizer — see leaks & UB reported on exit (recommended)
g++ -std=c++20 -fsanitize=address -g 01-Memory-Management/01_leak_vs_raii.cpp -o demo && ./demo

# Deep leak analysis without recompiling (Linux/Mac)
valgrind --leak-check=full ./demo
```

> On Windows with MSVC: `cl /std:c++20 /EHsc /fsanitize=address file.cpp`
