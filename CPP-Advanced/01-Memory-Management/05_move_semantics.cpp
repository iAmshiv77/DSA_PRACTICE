// 05 — Move semantics & the Rule of 5
//
//   g++ -std=c++20 -g 05_move_semantics.cpp -o move && ./move
//
// A hand-written buffer owner showing copy (deep, O(n)) vs move (steal, O(1)),
// and why moves prevent double-free.

#include <iostream>
#include <algorithm>
#include <utility>

class Buffer {
    std::size_t size_ = 0;
    int*        data_ = nullptr;
public:
    // ctor
    explicit Buffer(std::size_t n) : size_(n), data_(new int[n]{}) {
        std::cout << "  ctor(" << size_ << ")\n";
    }
    // dtor
    ~Buffer() {
        std::cout << "  dtor(size=" << size_ << ", "
                  << (data_ ? "owns" : "empty") << ")\n";
        delete[] data_;
    }
    // copy ctor — DEEP copy (allocates + copies every element)
    Buffer(const Buffer& o) : size_(o.size_), data_(new int[o.size_]) {
        std::copy(o.data_, o.data_ + size_, data_);
        std::cout << "  COPY ctor (deep, O(n=" << size_ << "))\n";
    }
    // copy assign
    Buffer& operator=(const Buffer& o) {
        if (this != &o) {
            delete[] data_;
            size_ = o.size_;
            data_ = new int[size_];
            std::copy(o.data_, o.data_ + size_, data_);
            std::cout << "  COPY assign (deep)\n";
        }
        return *this;
    }
    // move ctor — STEAL the buffer (O(1)), leave source empty & safe
    Buffer(Buffer&& o) noexcept : size_(o.size_), data_(o.data_) {
        o.size_ = 0;
        o.data_ = nullptr;          // source no longer owns → no double free
        std::cout << "  MOVE ctor (steal, O(1))\n";
    }
    // move assign
    Buffer& operator=(Buffer&& o) noexcept {
        if (this != &o) {
            delete[] data_;
            size_ = o.size_; data_ = o.data_;
            o.size_ = 0; o.data_ = nullptr;
            std::cout << "  MOVE assign (steal)\n";
        }
        return *this;
    }
    std::size_t size() const { return size_; }
};

Buffer makeBuffer() {
    Buffer b(1'000'000);
    return b;            // moved out (or elided), not copied
}

int main() {
    std::cout << "1) construct\n";
    Buffer a(5);

    std::cout << "2) copy (deep)\n";
    Buffer b = a;                 // COPY ctor

    std::cout << "3) move (steal)\n";
    Buffer c = std::move(a);      // MOVE ctor; a is now empty (size 0)

    std::cout << "4) return-value move\n";
    Buffer d = makeBuffer();      // move/elision, no million-element copy

    std::cout << "a.size()=" << a.size() << " (moved-from)\n";
    std::cout << "end of main — destructors run:\n";
}
