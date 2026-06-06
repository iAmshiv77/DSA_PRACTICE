# C++ Memory Management — Heap, Leaks & Modern RAII

> Senior-level deep dive: how C++ memory actually works, why leaks happen, and how
> modern C++ (RAII + smart pointers + move semantics) makes leaks largely a
> *design* problem rather than a *bookkeeping* problem.

---

## 1. The Memory Model — Where Does Memory Live?

```
 High addresses
 ┌─────────────────────────────┐
 │           STACK             │  ← grows downward
 │  - function frames          │     automatic storage
 │  - local variables          │     freed when scope ends (LIFO)
 │  - return addresses         │     fast (just move SP), size-limited (~1–8 MB)
 ├──────────────┬──────────────┤
 │              ▼              │
 │           (unused)          │
 │              ▲              │
 ├──────────────┴──────────────┤
 │           HEAP              │  ← grows upward
 │  - new / malloc             │     dynamic storage
 │  - lives until you free it  │     large, but slower, fragmentable
 ├─────────────────────────────┤
 │   BSS  (uninit globals = 0) │  static storage
 │   DATA (init globals)       │  lives for whole program
 ├─────────────────────────────┤
 │   TEXT (code, read-only)    │
 └─────────────────────────────┘
 Low addresses
```

| Region | Allocated by | Freed by | Lifetime | Cost |
|--------|--------------|----------|----------|------|
| **Stack** | compiler (automatic) | scope exit | function call | ~free (1 instr) |
| **Heap** | `new` / `malloc` | `delete` / `free` (YOU) | until freed | syscall + bookkeeping |
| **Static/Global** | linker | program exit | whole program | n/a |
| **Thread-local** | `thread_local` | thread exit | per-thread | n/a |

**Key insight:** A *leak* is heap memory that is still allocated but no longer
reachable by any pointer. The OS reclaims everything on process exit, so a leak
matters because of **long-running processes** (servers, daemons) that exhaust
memory over time.

---

## 2. What a Heap Leak Actually Is

```cpp
void leak() {
    int* p = new int[1000];   // 4000 bytes on heap, p points to it
    // ... no delete[] ...
}                             // p (stack var) destroyed; heap block orphaned
// Those 4000 bytes are now unreachable → LEAK
```

A leak is created when **the last pointer to a heap block is lost before the
block is freed.** Common ways to lose it:

1. **Forgot `delete`** — the classic.
2. **Early return / exception** between `new` and `delete`.
3. **Overwriting the only pointer** — `p = new X; p = new Y;` (first X leaks).
4. **Wrong delete form** — `delete` on `new[]` (UB), or `free` on `new`.
5. **Owner ambiguity** — two code paths both think the other frees it, so neither does.
6. **Cyclic `shared_ptr`** — reference counts never reach zero (see §6).

```cpp
// Exception-driven leak — looks correct, still leaks:
void process() {
    Resource* r = new Resource();
    riskyOperation();   // throws → next line never runs
    delete r;           // ← skipped, r leaked
}
```

---

## 3. RAII — The Cornerstone Idea

**RAII = Resource Acquisition Is Initialization.** Bind a resource's lifetime to
an object's lifetime. Acquire in the constructor, release in the destructor. The
language *guarantees* the destructor runs on scope exit — **even when an
exception is thrown** (stack unwinding). This is what makes C++ resource handling
deterministic.

```cpp
// A hand-rolled RAII wrapper (smart pointers already do this for you)
class FileHandle {
    std::FILE* f_;
public:
    explicit FileHandle(const char* path) : f_(std::fopen(path, "r")) {
        if (!f_) throw std::runtime_error("open failed");
    }
    ~FileHandle() { if (f_) std::fclose(f_); }   // ALWAYS runs on scope exit

    // Rule of 5: a resource-owning type must define/delete copy & move
    FileHandle(const FileHandle&)            = delete;
    FileHandle& operator=(const FileHandle&) = delete;
    FileHandle(FileHandle&& o) noexcept : f_(o.f_) { o.f_ = nullptr; }
    FileHandle& operator=(FileHandle&& o) noexcept {
        if (this != &o) { if (f_) std::fclose(f_); f_ = o.f_; o.f_ = nullptr; }
        return *this;
    }
    std::FILE* get() const { return f_; }
};
```

RAII applies to **every** resource, not just memory: file handles, sockets,
mutex locks (`std::lock_guard`), DB connections, GPU buffers. "Allocate raw,
free manually" is the anti-pattern; "wrap in an owner" is the pattern.

---

## 4. The Rule of 0 / 3 / 5

| Rule | Meaning |
|------|---------|
| **Rule of 0** | If your class has **no raw owning resource**, define *none* of the special members. Let members (smart pointers, containers) manage themselves. **Aim for this.** |
| **Rule of 3** | If you define a destructor, copy ctor, *or* copy assignment, you probably need **all three** (C++98 era). |
| **Rule of 5** | Modern: destructor, copy ctor, copy assign, **move ctor, move assign** — define all or delete consistently. |

```cpp
// Rule of 0 — the ideal. No manual memory, no leaks possible, fully copyable/movable.
class User {
    std::string            name_;     // manages its own heap buffer
    std::vector<int>       scores_;   // manages its own heap buffer
    std::unique_ptr<Addr>  address_;  // manages its own heap object
    // No destructor needed. No copy/move needed. Compiler does the right thing.
};
```

**Senior heuristic:** the moment you type `new` or `delete` in application code,
ask "why isn't this a `unique_ptr`/`vector`/`string`?" Raw owning pointers belong
inside library-level RAII wrappers, almost never in business logic.

---

## 5. Smart Pointers — The Modern Default

```
┌──────────────────┬───────────────────────────────────────────────────────┐
│ std::unique_ptr  │ Sole ownership. Zero overhead vs raw pointer.          │
│                  │ Move-only (can't copy). DEFAULT CHOICE.                │
├──────────────────┼───────────────────────────────────────────────────────┤
│ std::shared_ptr  │ Shared ownership via atomic reference count.           │
│                  │ Copyable. Frees when count hits 0. Has overhead.       │
├──────────────────┼───────────────────────────────────────────────────────┤
│ std::weak_ptr    │ Non-owning observer of a shared_ptr. Breaks cycles.    │
│                  │ .lock() to safely access (returns shared_ptr or null). │
└──────────────────┴───────────────────────────────────────────────────────┘
```

### `unique_ptr` — use this 90% of the time
```cpp
auto u = std::make_unique<Widget>(args);   // allocates, owns
u->doThing();                              // use like a pointer
// no delete — freed automatically when u goes out of scope
auto u2 = std::move(u);                     // ownership transferred; u is now null
```
- **Zero runtime cost** — same size and speed as a raw pointer, just safe.
- Move-only: passing it transfers ownership (encodes intent in the type system).
- Custom deleters for non-`new` resources: `unique_ptr<FILE, decltype(&fclose)>`.

### `shared_ptr` — only when ownership is genuinely shared
```cpp
auto s1 = std::make_shared<Widget>();  // control block: ref count = 1
auto s2 = s1;                          // ref count = 2 (atomic increment)
// freed only when BOTH s1 and s2 are gone (count → 0)
```
- Control block holds **strong count** + **weak count** + the object.
- `make_shared` does **one** allocation (object + control block together) — prefer it.
- Reference counting is **atomic** → thread-safe count, but **not** thread-safe
  for the pointed-to object. Copying is not free.

### `weak_ptr` — observe without owning
```cpp
std::weak_ptr<Widget> w = s1;          // does NOT raise ref count
if (auto locked = w.lock()) {          // try to promote to shared_ptr
    locked->use();                     // safe: object guaranteed alive in this block
} else {
    // object already destroyed
}
```

**Decision tree:**
```
Need a pointer to a heap object?
├─ Does anything else need to own it too?
│   ├─ No  → unique_ptr            (default)
│   └─ Yes → shared_ptr
│            └─ Is there a back-reference / cache / observer? → weak_ptr for that edge
└─ Just observing, never owning, lifetime guaranteed elsewhere?
    └─ raw pointer T*  or  reference T&   (non-owning is a legit use of raw pointers!)
```

> **Raw pointers are NOT banned** — they're fine as *non-owning* observers
> (function params, "I'm just looking"). What you avoid is raw pointers that
> *own* (where someone must remember to `delete`).

---

## 6. The Classic Trap: `shared_ptr` Cycles

```
Parent ──shared_ptr──► Child
   ▲                      │
   └──────shared_ptr──────┘     Both counts stuck at 1 forever → LEAK
```

```cpp
struct Node {
    std::shared_ptr<Node> next;   // owns next
    std::shared_ptr<Node> prev;   // ← cycle! prev should NOT own
};
// a->next = b; b->prev = a;  →  neither ever reaches refcount 0
```

**Fix:** exactly one direction owns (`shared_ptr`), the back-edge observes
(`weak_ptr`):
```cpp
struct Node {
    std::shared_ptr<Node> next;   // forward edge owns
    std::weak_ptr<Node>   prev;   // back edge observes — breaks the cycle
};
```
Reference counting **cannot** collect cycles (that's why GC languages use mark-sweep).
In C++ you break cycles by design: **own in one direction only.**

---

## 7. Move Semantics — Cheap Ownership Transfer

Before C++11, returning a big object copied its heap buffer. Move semantics let
you **steal** the buffer (pointer swap) instead of copying it — O(1) instead of O(n),
and it's what makes `unique_ptr` transferable.

```cpp
std::vector<int> make() {
    std::vector<int> v(1'000'000);
    return v;                 // moved (or elided), NOT copied — buffer pointer stolen
}

std::string a = "hello";
std::string b = std::move(a); // b steals a's buffer; a is now valid-but-empty
// std::move doesn't move anything — it's a cast to rvalue that ENABLES the move
```

- An **lvalue** has a name/address (`x`). An **rvalue** is a temporary (`x + 1`, `make()`).
- A move ctor takes `T&&` and transfers internals, leaving the source in a valid
  empty state — then both destructors run safely (no double-free).
- Mark move operations `noexcept` — containers like `vector` only move (instead of
  copy) on reallocation if the move is `noexcept`.

---

## 8. Detecting & Diagnosing Leaks

| Tool | Platform | What it catches |
|------|----------|-----------------|
| **AddressSanitizer (ASan)** | clang/gcc | use-after-free, heap-buffer-overflow, leaks (`-fsanitize=address`) |
| **LeakSanitizer (LSan)** | clang/gcc | leaks specifically (bundled with ASan) |
| **Valgrind (memcheck)** | Linux/Mac | leaks, uninit reads, invalid free (slow, no recompile) |
| **Visual Studio CRT / `_CrtDumpMemoryLeaks`** | Windows/MSVC | leaks in debug builds |
| **Dr. Memory** | Windows/Linux | leaks, UB |
| **heaptrack / massif** | Linux | heap profiling (who allocated what, peak usage) |

```bash
# AddressSanitizer — the modern first choice (fast, precise stack traces)
g++ -std=c++20 -fsanitize=address -g program.cpp -o prog && ./prog
# On exit, prints: "Direct leak of 4000 byte(s) ... #0 operator new[] ... #1 leak()"

# Valgrind — no recompile needed
valgrind --leak-check=full --show-leak-kinds=all ./prog
```

**Categories Valgrind reports:**
- *Definitely lost* — no pointer to the block at all → real leak, fix it.
- *Indirectly lost* — only reachable via a definitely-lost block (e.g. a leaked tree's nodes).
- *Still reachable* — pointer exists at exit (often globals/singletons) — usually benign.
- *Possibly lost* — interior pointer only — investigate.

---

## 9. Other Heap Bugs (cousins of leaks)

```cpp
// Dangling pointer — use after free
int* p = new int(5);
delete p;
*p = 10;                 // UB: writing freed memory

// Double free
delete p;
delete p;                // UB: corrupts the heap allocator

// Wrong delete form
int* arr = new int[10];
delete arr;              // UB: should be delete[] arr;

// Memory leak via exception (see §2)
// Buffer overflow — write past allocation
int* a = new int[3];
a[3] = 1;                // UB: heap corruption
```
**All of these vanish** when you use `unique_ptr`/`vector`/`string` instead of
raw `new`/`delete`: there is no manual `delete` to get wrong, no array/scalar
mismatch, and overflows are caught by `vector::at` / ASan.

---

## 10. Allocators & Performance (staff-level awareness)

- **`new`/`malloc` are not free** — they take a lock (or use per-thread arenas),
  search free lists, and can fragment. In hot paths, allocation cost dominates.
- **Object pools / arena (bump) allocators** — pre-allocate a block, hand out
  slices, free all at once. Great for many short-lived same-size objects
  (game entities, request-scoped allocations, parse trees).
- **Custom allocators** — `std::pmr` (polymorphic memory resources, C++17) lets
  you swap allocation strategy without changing container types:
  `std::pmr::vector<int> v{&monotonic_buffer_resource};`.
- **Small-buffer optimization (SBO)** — `std::string`/`std::function` store small
  payloads inline (no heap alloc at all). Knowing this avoids needless allocations.
- **Fragmentation** — long-running servers can have free memory that's unusable
  because it's split into small gaps. Pools and arenas mitigate this.

---

## 11. More Ownership Idioms You're Expected to Know

### `enable_shared_from_this` — get a `shared_ptr` from `this` safely
If an object is owned by a `shared_ptr` and a method needs to hand *itself* to
something (a callback, a registry), you **must not** write `shared_ptr<T>(this)` —
that creates a *second* control block, so the object gets double-freed.
```cpp
class Session : public std::enable_shared_from_this<Session> {
public:
    void start() {
        // shared_from_this() shares the EXISTING control block (refcount++), safe.
        registry.add(shared_from_this());
        // shared_ptr<Session>(this);  // ❌ second control block → double free
    }
};
auto s = std::make_shared<Session>();   // must already be owned by a shared_ptr
s->start();
```
Ubiquitous in async code (e.g. Boost.Asio) to keep an object alive for the duration
of an in-flight operation.

### Pimpl idiom — "pointer to implementation" (compilation firewall)
Hide a class's private members behind an opaque `unique_ptr`, so the header exposes
no implementation details. Benefits: faster builds (changing the impl doesn't
recompile clients), stable ABI, true encapsulation.
```cpp
// widget.h — no includes of impl details, no private members leaked
class Widget {
    struct Impl;                       // forward-declared, incomplete here
    std::unique_ptr<Impl> pImpl;       // the firewall
public:
    Widget();
    ~Widget();                         // MUST be defined in .cpp (Impl complete there)
    void doThing();
};
// widget.cpp
struct Widget::Impl { int heavy; /* big dependencies live here */ };
Widget::Widget()  : pImpl(std::make_unique<Impl>()) {}
Widget::~Widget() = default;           // defined where Impl is complete
void Widget::doThing() { pImpl->heavy++; }
```
> Gotcha: with `unique_ptr<Impl>`, the destructor (and move ops) must be **defined in
> the .cpp** where `Impl` is a complete type — otherwise the compiler can't generate
> the deleter. This trips up everyone once.

### Non-owning views — avoid copies without raw pointers
`std::string_view` (C++17) and `std::span` (C++20) reference existing memory without
owning or copying it — the modern, bounds-aware replacement for `(const char*, len)`
and `(T*, n)` pairs.
```cpp
void log(std::string_view msg);        // takes "literal", std::string, char* — zero copies
int sum(std::span<const int> xs);      // works for vector, array, C-array — no copy, knows size
```
> **Lifetime caveat:** a view is a borrow. Never return a `string_view`/`span` to a
> local, and never outlive the buffer it points at — that's a dangling view.

### `unique_ptr<T[]>` — owning a heap array
```cpp
auto arr = std::make_unique<int[]>(100);  // calls delete[] automatically
arr[7] = 42;
// But prefer std::vector<int>(100) — same safety, plus size(), growth, iterators.
```

### Exception-safety guarantees (vocabulary seniors use)
| Guarantee | Meaning |
|-----------|---------|
| **No-throw** | Operation never throws (destructors, `swap`, move ops should be `noexcept`) |
| **Strong** | If it throws, state is rolled back to before the call (commit-or-rollback) |
| **Basic** | If it throws, invariants hold and nothing leaks, but state may have changed |
| **None** | Throwing may corrupt/leak — avoid |
Copy-and-swap is the classic way to get the **strong** guarantee in assignment.

### Copy elision / RVO / NRVO
The compiler constructs a returned object directly in the caller's storage,
eliminating the copy/move entirely. (Guaranteed for returning a prvalue temporary
since C++17.) Don't pessimize it by writing `return std::move(local);` — that can
*disable* NRVO. Just `return local;`.

---

## 12. Senior Rules of Thumb

1. **Prefer the stack.** If an object fits and its lifetime is scoped, don't heap-allocate.
2. **Rule of 0.** Compose from `vector`/`string`/`unique_ptr`; write no destructors.
3. **`make_unique` / `make_shared`** over bare `new` (exception-safe, single alloc).
4. **`unique_ptr` by default; `shared_ptr` only when ownership is truly shared.**
5. **Express ownership in the type.** `unique_ptr` param = "I take ownership";
   `T*`/`T&` param = "I just look."
6. **Break `shared_ptr` cycles with `weak_ptr`.** Own in one direction only.
7. **Mark moves `noexcept`.**
8. **Run ASan in CI.** Leaks and UB caught automatically, every build.
9. **Never mix** `new`/`delete[]`, `malloc`/`delete`, or two owners for one block.
10. **A leak is a design smell** — unclear ownership. Fix the ownership model, not just the symptom.

---

## 13. Interview Questions

**Q: What is a memory leak and why does it matter if the OS frees everything on exit?**
A: A leak is reachable-no-more heap memory that's never freed. It matters for
long-running processes (servers) — memory grows until OOM. Short CLI tools that
exit quickly are less affected, but leaks still indicate broken ownership.

**Q: `unique_ptr` vs `shared_ptr` — cost and when to use each?**
A: `unique_ptr` is zero-overhead (same as a raw pointer), move-only, sole owner —
the default. `shared_ptr` carries a heap control block with an **atomic** ref
count; copies are not free and it's larger. Use `shared_ptr` only when lifetime
is genuinely shared across owners with no single clear owner.

**Q: How can `shared_ptr` still leak?**
A: Reference cycles — two objects holding `shared_ptr` to each other keep each
other's count at 1 forever. Ref counting can't collect cycles. Fix: make the
back-edge a `weak_ptr`.

**Q: Why `make_shared` over `shared_ptr<T>(new T)`?**
A: One allocation (object + control block together) instead of two → faster, better
cache locality; and it's exception-safe (no leak if a surrounding argument
evaluation throws).

**Q: What does `std::move` do?**
A: Nothing at runtime — it's a `static_cast` to an rvalue reference, signaling
"this object can be pillaged." It *enables* a move ctor/assignment to run instead
of a copy. The actual transfer happens in those operations.

**Q: What is RAII and why does it make C++ exception-safe?**
A: Tie resource lifetime to object lifetime — acquire in ctor, release in dtor.
During exception stack unwinding, destructors of fully-constructed objects run
automatically, so resources are released on every exit path, including throws.

**Q: How do you find a leak in production C++?**
A: ASan/LSan in CI for pre-prod; Valgrind for deep analysis; heaptrack/massif for
heap profiling and peak usage; on Windows, CRT debug heap / Dr. Memory. Then trace
the allocation site to the owning code path and fix the ownership model.

**Q: Why not `shared_ptr<T>(this)` inside a method? What's the right way?**
A: It creates a second, independent control block, so two owners each free the object →
double free. Inherit from `std::enable_shared_from_this<T>` and call
`shared_from_this()`, which shares the existing control block (the object must already
be owned by a `shared_ptr`).

**Q: What is the Pimpl idiom and what's the destructor gotcha?**
A: Hide private members behind an opaque `unique_ptr<Impl>` so the header leaks no
implementation details — speeding builds and stabilizing ABI. Gotcha: the destructor
(and move ops) must be *defined in the .cpp* where `Impl` is complete; otherwise the
compiler can't synthesize `unique_ptr`'s deleter for an incomplete type.

**Q: `string_view`/`span` vs passing the object — when and what's the risk?**
A: Use them to pass read-only sequences without copying or owning (a "borrow"). They
avoid allocations and accept many source types. Risk: they're non-owning, so they
dangle if the underlying buffer is destroyed or you return a view of a local.

**Q: Should you `return std::move(local);` to optimize a return?**
A: No — it can disable NRVO/copy elision, making things slower. Return the local
directly (`return local;`); the compiler elides the copy/move (guaranteed for prvalue
temporaries since C++17).
