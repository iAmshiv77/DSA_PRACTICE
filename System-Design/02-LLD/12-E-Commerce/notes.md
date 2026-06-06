# LLD: E-Commerce System (like Amazon)

## Requirements
- Product catalog with categories, variants
- Cart (guest + logged-in)
- Order placement with inventory check
- Payment (multiple methods)
- Order tracking, returns/refunds
- Search & filters

## Key Classes

```
ECommerceSystem (Facade)
├── ProductCatalog
│   ├── Product
│   │   ├── ProductVariant (size/color)
│   │   └── Category
│   └── Inventory
├── CartService
│   └── Cart
│       └── CartItem[]
├── OrderService
│   └── Order
│       ├── OrderItem[]
│       └── OrderStatus (state machine)
├── PaymentService (Strategy)
├── ShippingService
│   └── ShipmentTracker
└── SearchService
```

## Patterns Used
- **Strategy**: Payment (card/UPI/wallet/COD)
- **State**: Order status transitions
- **Observer**: Order status → notify customer/warehouse/delivery
- **Decorator**: Product pricing (base + discount + tax + coupon)
- **Builder**: Complex Order creation

## C++ Implementation

```cpp
#include <iostream>
#include <vector>
#include <unordered_map>
#include <memory>
#include <string>
#include <optional>
using namespace std;

// ── Enums ─────────────────────────────────────────────────────────────────────
enum class OrderStatus { PENDING, CONFIRMED, SHIPPED, DELIVERED, CANCELLED, RETURNED };

// ── Price Decorator ───────────────────────────────────────────────────────────
class PriceComponent {
public:
    virtual double getPrice() const = 0;
    virtual string getDescription() const = 0;
    virtual ~PriceComponent() = default;
};

class BasePrice : public PriceComponent {
    double price;
public:
    explicit BasePrice(double p) : price(p) {}
    double getPrice() const override { return price; }
    string getDescription() const override { return "Base"; }
};

class TaxDecorator : public PriceComponent {
    unique_ptr<PriceComponent> wrapped;
    double rate;  // 0.18 for 18% GST
public:
    TaxDecorator(unique_ptr<PriceComponent> c, double r)
        : wrapped(move(c)), rate(r) {}
    double getPrice() const override { return wrapped->getPrice() * (1 + rate); }
    string getDescription() const override {
        return wrapped->getDescription() + " + Tax(" + to_string((int)(rate*100)) + "%)";
    }
};

class DiscountDecorator : public PriceComponent {
    unique_ptr<PriceComponent> wrapped;
    double discountPct;
public:
    DiscountDecorator(unique_ptr<PriceComponent> c, double pct)
        : wrapped(move(c)), discountPct(pct) {}
    double getPrice() const override { return wrapped->getPrice() * (1 - discountPct); }
    string getDescription() const override {
        return wrapped->getDescription() + " - Discount(" + to_string((int)(discountPct*100)) + "%)";
    }
};

// ── Product Variant ───────────────────────────────────────────────────────────
struct ProductVariant {
    string variantId;
    string size;   // "S", "M", "L", "XL"
    string color;  // "Red", "Blue"
    int    stock;
    double extraPrice;  // delta from base price
};

// ── Product ───────────────────────────────────────────────────────────────────
class Product {
public:
    string id;
    string name;
    string category;
    double basePrice;
    string sellerId;
    vector<ProductVariant> variants;
    double discountPct = 0;
    double taxRate = 0.18;

    Product(const string& id, const string& name, const string& cat,
            double price, const string& seller)
        : id(id), name(name), category(cat), basePrice(price), sellerId(seller) {}

    double getFinalPrice(const string& variantId = "") const {
        double price = basePrice;
        for (const auto& v : variants) {
            if (v.variantId == variantId) { price += v.extraPrice; break; }
        }

        // Apply decorator chain
        auto pricing = make_unique<BasePrice>(price);
        if (discountPct > 0)
            pricing = make_unique<DiscountDecorator>(move(pricing), discountPct);
        pricing = make_unique<TaxDecorator>(move(pricing), taxRate);

        return pricing->getPrice();
    }

    bool isInStock(const string& variantId, int qty) const {
        for (const auto& v : variants) {
            if (v.variantId == variantId) return v.stock >= qty;
        }
        return false;
    }

    bool reduceStock(const string& variantId, int qty) {
        for (auto& v : variants) {
            if (v.variantId == variantId) {
                if (v.stock < qty) return false;
                v.stock -= qty;
                return true;
            }
        }
        return false;
    }
};

// ── Cart ──────────────────────────────────────────────────────────────────────
struct CartItem {
    string productId;
    string variantId;
    int    quantity;
    double priceAtAdd;
};

class Cart {
public:
    string customerId;
    vector<CartItem> items;
    string couponCode;

    void addItem(const string& productId, const string& variantId, int qty, double price) {
        for (auto& item : items) {
            if (item.productId == productId && item.variantId == variantId) {
                item.quantity += qty;
                return;
            }
        }
        items.push_back({productId, variantId, qty, price});
    }

    void removeItem(const string& productId, const string& variantId) {
        items.erase(
            remove_if(items.begin(), items.end(),
                [&](const CartItem& i) {
                    return i.productId == productId && i.variantId == variantId;
                }),
            items.end()
        );
    }

    double subtotal() const {
        double total = 0;
        for (const auto& item : items) total += item.priceAtAdd * item.quantity;
        return total;
    }

    void clear() { items.clear(); couponCode = ""; }
};

// ── Payment Strategy ──────────────────────────────────────────────────────────
class PaymentStrategy {
public:
    virtual bool pay(double amount, const string& orderId) = 0;
    virtual string method() const = 0;
    virtual ~PaymentStrategy() = default;
};

class UPIPayment : public PaymentStrategy {
    string upiId;
public:
    explicit UPIPayment(const string& id) : upiId(id) {}
    bool pay(double amount, const string& orderId) override {
        cout << "UPI: ₹" << amount << " paid for order " << orderId << "\n";
        return true;
    }
    string method() const override { return "UPI"; }
};

class CODPayment : public PaymentStrategy {
public:
    bool pay(double amount, const string& orderId) override {
        cout << "COD: ₹" << amount << " will be collected on delivery of " << orderId << "\n";
        return true;
    }
    string method() const override { return "COD"; }
};

// ── Order Observer ────────────────────────────────────────────────────────────
class OrderEventListener {
public:
    virtual void onOrderEvent(const string& orderId, OrderStatus status) = 0;
    virtual ~OrderEventListener() = default;
};

class CustomerEmailNotifier : public OrderEventListener {
    string email;
public:
    explicit CustomerEmailNotifier(const string& e) : email(e) {}
    void onOrderEvent(const string& orderId, OrderStatus status) override {
        static unordered_map<int, string> msgs = {
            {(int)OrderStatus::CONFIRMED, "Your order is confirmed!"},
            {(int)OrderStatus::SHIPPED,   "Your order has been shipped!"},
            {(int)OrderStatus::DELIVERED, "Your order has been delivered!"},
        };
        auto it = msgs.find((int)status);
        if (it != msgs.end())
            cout << "[Email to " << email << "] " << it->second << " (Order: " << orderId << ")\n";
    }
};

// ── Order (State Machine + Builder) ──────────────────────────────────────────
class Order {
    static int counter;
public:
    string id;
    string customerId;
    vector<CartItem> items;
    double totalAmount;
    string paymentMethod;
    string shippingAddress;
    OrderStatus status = OrderStatus::PENDING;
    vector<OrderEventListener*> listeners;

    static string generateId() { return "ORD-" + to_string(++counter); }

    Order(const string& cid, const vector<CartItem>& it, double total,
          const string& payment, const string& address)
        : id(generateId()), customerId(cid), items(it),
          totalAmount(total), paymentMethod(payment), shippingAddress(address) {}

    void addListener(OrderEventListener* l) { listeners.push_back(l); }

    bool updateStatus(OrderStatus next) {
        // Validate transition
        static unordered_map<int, vector<int>> valid = {
            {(int)OrderStatus::PENDING,   {(int)OrderStatus::CONFIRMED, (int)OrderStatus::CANCELLED}},
            {(int)OrderStatus::CONFIRMED, {(int)OrderStatus::SHIPPED,   (int)OrderStatus::CANCELLED}},
            {(int)OrderStatus::SHIPPED,   {(int)OrderStatus::DELIVERED}},
            {(int)OrderStatus::DELIVERED, {(int)OrderStatus::RETURNED}},
        };

        auto& allowed = valid[(int)status];
        if (find(allowed.begin(), allowed.end(), (int)next) == allowed.end()) {
            cout << "Invalid order status transition\n";
            return false;
        }
        status = next;
        for (auto* l : listeners) l->onOrderEvent(id, status);
        return true;
    }

    void print() const {
        cout << "\n=== Order " << id << " ===\n";
        cout << "Customer: " << customerId << "\n";
        cout << "Payment: " << paymentMethod << " | Total: ₹" << totalAmount << "\n";
        cout << "Address: " << shippingAddress << "\n";
        cout << "Items:\n";
        for (const auto& item : items) {
            cout << "  Product " << item.productId
                 << " (variant: " << item.variantId << ")"
                 << " x" << item.quantity
                 << " @ ₹" << item.priceAtAdd << "\n";
        }
    }
};
int Order::counter = 0;

// ── E-Commerce System ─────────────────────────────────────────────────────────
class ECommerceSystem {
    unordered_map<string, unique_ptr<Product>> products;
    unordered_map<string, Cart> carts;  // customerId → Cart
    unordered_map<string, unique_ptr<Order>> orders;

public:
    void addProduct(unique_ptr<Product> product) {
        products[product->id] = move(product);
    }

    Cart& getCart(const string& customerId) {
        return carts[customerId];  // creates if doesn't exist
    }

    optional<string> checkout(const string& customerId, const string& address,
                               PaymentStrategy& payment,
                               OrderEventListener* listener = nullptr) {
        auto& cart = carts[customerId];
        if (cart.items.empty()) {
            cout << "Cart is empty\n";
            return nullopt;
        }

        // Check stock and reserve
        for (const auto& item : cart.items) {
            auto it = products.find(item.productId);
            if (it == products.end()) {
                cout << "Product not found: " << item.productId << "\n";
                return nullopt;
            }
            if (!it->second->isInStock(item.variantId, item.quantity)) {
                cout << "Out of stock: " << it->second->name << "\n";
                return nullopt;
            }
        }

        double total = cart.subtotal();

        // Process payment
        string tempOrderId = "temp-" + customerId;
        if (!payment.pay(total, tempOrderId)) {
            cout << "Payment failed\n";
            return nullopt;
        }

        // Deduct stock
        for (const auto& item : cart.items) {
            products[item.productId]->reduceStock(item.variantId, item.quantity);
        }

        // Create order
        auto order = make_unique<Order>(customerId, cart.items, total,
                                        payment.method(), address);
        if (listener) order->addListener(listener);

        string orderId = order->id;
        order->updateStatus(OrderStatus::CONFIRMED);
        orders[orderId] = move(order);
        orders[orderId]->print();

        cart.clear();
        return orderId;
    }

    bool updateOrderStatus(const string& orderId, OrderStatus status) {
        auto it = orders.find(orderId);
        if (it == orders.end()) { cout << "Order not found\n"; return false; }
        return it->second->updateStatus(status);
    }
};

// ── Main ──────────────────────────────────────────────────────────────────────
int main() {
    ECommerceSystem sys;

    // Add products
    auto shirt = make_unique<Product>("P001", "Cotton Shirt", "Clothing", 499, "Nike");
    shirt->variants.push_back({"V001", "M", "Blue", 10, 0});
    shirt->variants.push_back({"V002", "L", "Blue", 5,  50});
    shirt->discountPct = 0.10;  // 10% off
    double price = shirt->getFinalPrice("V001");
    cout << "Shirt M/Blue final price: ₹" << price << "\n";

    sys.addProduct(move(shirt));

    // Customer adds to cart
    Cart& cart = sys.getCart("C001");
    cart.addItem("P001", "V001", 2, price);

    // Checkout
    CustomerEmailNotifier notifier("user@example.com");
    UPIPayment upi("user@upi");

    auto orderId = sys.checkout("C001", "123 MG Road, Mumbai", upi, &notifier);

    if (orderId) {
        sys.updateOrderStatus(*orderId, OrderStatus::SHIPPED);
        sys.updateOrderStatus(*orderId, OrderStatus::DELIVERED);
    }

    return 0;
}
```

## Interview Q&A

**Q: How do you handle flash sales with thousands of concurrent buyers?**
- Pre-warm inventory count in Redis: `SET inventory:{productId} 1000`
- On each purchase attempt: `DECR inventory:{productId}` — if result < 0, `INCR` back and reject
- This is atomic in Redis (single-threaded) — no race condition
- Use a queue (Kafka) to serialize actual order creation — dequeue and process orders up to inventory count
- Database update happens asynchronously after Redis confirms allocation

**Q: How do you design the search/filter for millions of products?**
Elasticsearch with denormalized product documents (include category name, brand name in the doc — don't JOIN at query time). Faceted search uses aggregation buckets (brand: Nike=1200, Adidas=800). Filter by `category.keyword`, range on `price`, terms on `size`. Product updates propagate to ES via Kafka event stream.
