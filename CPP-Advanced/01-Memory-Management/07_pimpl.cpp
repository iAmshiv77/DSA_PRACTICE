// 07 — Pimpl idiom (pointer to implementation) in one file
//
//   g++ -std=c++20 -g 07_pimpl.cpp -o pimpl && ./pimpl
//
// Normally split across widget.h / widget.cpp; shown together for runnability.
// The header part (above the line) exposes NO implementation details — clients
// recompile only when the public interface changes, not the private impl.

#include <iostream>
#include <memory>
#include <string>

// ===================== widget.h (public interface) =========================
class Widget {
    struct Impl;                      // opaque: incomplete type in the "header"
    std::unique_ptr<Impl> pImpl;      // the compilation firewall
public:
    explicit Widget(std::string name);
    ~Widget();                        // MUST be out-of-line (defined where Impl is complete)
    Widget(Widget&&) noexcept;        // move ops also need Impl complete → declare here,
    Widget& operator=(Widget&&) noexcept;   // define in .cpp
    void greet() const;
    void bump();
};

// ===================== widget.cpp (hidden implementation) ===================
struct Widget::Impl {
    std::string name;                 // heavy/private members hidden from clients
    int counter = 0;
};

Widget::Widget(std::string name)
    : pImpl(std::make_unique<Impl>(Impl{std::move(name), 0})) {}

Widget::~Widget() = default;                       // Impl complete here → deleter OK
Widget::Widget(Widget&&) noexcept = default;
Widget& Widget::operator=(Widget&&) noexcept = default;

void Widget::greet() const {
    std::cout << "  Widget '" << pImpl->name << "' counter=" << pImpl->counter << "\n";
}
void Widget::bump() { pImpl->counter++; }

// ===================== client code =========================================
int main() {
    Widget w("alpha");
    w.greet();
    w.bump(); w.bump();
    w.greet();

    Widget moved = std::move(w);      // move works because move ops are defined
    moved.greet();
    std::cout << "Pimpl: clients never saw Impl's fields; changing them = no client recompile.\n";
}
