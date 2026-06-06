# LLD: Food Ordering System (like Swiggy/Zomato)

## Requirements
- Browse restaurants by location
- View menu items with customizations
- Place order, track order status
- Multiple payment methods
- Delivery agent assignment
- Rating & review after delivery

## Key Classes

```
FoodOrderingSystem (Singleton / Facade)
├── RestaurantManager
│   └── Restaurant
│       ├── Menu
│       │   └── MenuItem (with Customization options)
│       └── OrderManager
├── CustomerManager
│   └── Customer
├── DeliveryManager
│   └── DeliveryAgent
├── OrderProcessor
│   └── Order (state machine)
│       └── OrderItem[]
└── PaymentProcessor (Strategy)
    ├── CreditCardPayment
    ├── UPIPayment
    └── WalletPayment
```

## Patterns Used
- **Singleton**: FoodOrderingSystem
- **State**: Order (PLACED → ACCEPTED → PREPARING → PICKED_UP → DELIVERED / CANCELLED)
- **Strategy**: PaymentProcessor
- **Observer**: Customer & DeliveryAgent notified on state change
- **Facade**: FoodOrderingSystem hides internal complexity

## C++ Implementation

```cpp
#include <iostream>
#include <vector>
#include <unordered_map>
#include <memory>
#include <string>
#include <stdexcept>
using namespace std;

// ── Enums ─────────────────────────────────────────────────────────────────────
enum class OrderStatus {
    PLACED, ACCEPTED, PREPARING, PICKED_UP, DELIVERED, CANCELLED
};

string statusStr(OrderStatus s) {
    switch (s) {
        case OrderStatus::PLACED:    return "PLACED";
        case OrderStatus::ACCEPTED:  return "ACCEPTED";
        case OrderStatus::PREPARING: return "PREPARING";
        case OrderStatus::PICKED_UP: return "PICKED_UP";
        case OrderStatus::DELIVERED: return "DELIVERED";
        case OrderStatus::CANCELLED: return "CANCELLED";
    }
    return "";
}

// ── Menu ──────────────────────────────────────────────────────────────────────
struct Customization {
    string name;   // "Extra cheese"
    double price;  // +20
};

struct MenuItem {
    int id;
    string name;
    double basePrice;
    string category;
    bool isAvailable = true;
    vector<Customization> customizations;
};

// ── Order Item ────────────────────────────────────────────────────────────────
struct OrderItem {
    MenuItem item;
    int quantity;
    vector<Customization> selectedCustomizations;

    double total() const {
        double price = item.basePrice;
        for (const auto& c : selectedCustomizations) price += c.price;
        return price * quantity;
    }
};

// ── Payment Strategy ──────────────────────────────────────────────────────────
class PaymentStrategy {
public:
    virtual bool pay(double amount) = 0;
    virtual string name() const = 0;
    virtual ~PaymentStrategy() = default;
};

class UPIPayment : public PaymentStrategy {
    string upiId;
public:
    explicit UPIPayment(const string& id) : upiId(id) {}
    bool pay(double amount) override {
        cout << "Processing UPI payment of ₹" << amount << " via " << upiId << "\n";
        return true; // simulate success
    }
    string name() const override { return "UPI"; }
};

class CreditCardPayment : public PaymentStrategy {
    string last4;
public:
    explicit CreditCardPayment(const string& l4) : last4(l4) {}
    bool pay(double amount) override {
        cout << "Processing card payment of ₹" << amount << " (****" << last4 << ")\n";
        return true;
    }
    string name() const override { return "CreditCard"; }
};

// ── Observer ──────────────────────────────────────────────────────────────────
class OrderObserver {
public:
    virtual void onStatusChange(int orderId, OrderStatus status) = 0;
    virtual ~OrderObserver() = default;
};

class CustomerNotifier : public OrderObserver {
    string customerName;
public:
    explicit CustomerNotifier(const string& name) : customerName(name) {}
    void onStatusChange(int orderId, OrderStatus status) override {
        cout << "[Customer " << customerName << "] Order #" << orderId
             << " is now: " << statusStr(status) << "\n";
    }
};

// ── Order (State Machine) ─────────────────────────────────────────────────────
class Order {
    static int counter;
public:
    int id;
    int customerId;
    int restaurantId;
    vector<OrderItem> items;
    OrderStatus status = OrderStatus::PLACED;
    double totalAmount = 0;
    vector<OrderObserver*> observers;

    Order(int custId, int restId, vector<OrderItem> its)
        : id(++counter), customerId(custId), restaurantId(restId), items(move(its)) {
        for (const auto& item : items) totalAmount += item.total();
    }

    void addObserver(OrderObserver* obs) { observers.push_back(obs); }

    bool transition(OrderStatus next) {
        // Validate transitions
        static unordered_map<int, vector<int>> valid = {
            {(int)OrderStatus::PLACED,    {(int)OrderStatus::ACCEPTED, (int)OrderStatus::CANCELLED}},
            {(int)OrderStatus::ACCEPTED,  {(int)OrderStatus::PREPARING, (int)OrderStatus::CANCELLED}},
            {(int)OrderStatus::PREPARING, {(int)OrderStatus::PICKED_UP}},
            {(int)OrderStatus::PICKED_UP, {(int)OrderStatus::DELIVERED}},
        };

        auto& allowed = valid[(int)status];
        if (find(allowed.begin(), allowed.end(), (int)next) == allowed.end()) {
            cout << "Invalid transition from " << statusStr(status) << " to " << statusStr(next) << "\n";
            return false;
        }

        status = next;
        for (auto* obs : observers) obs->onStatusChange(id, status);
        return true;
    }

    void printSummary() const {
        cout << "\n--- Order #" << id << " ---\n";
        for (const auto& oi : items) {
            cout << "  " << oi.item.name << " x" << oi.quantity
                 << " = ₹" << oi.total() << "\n";
        }
        cout << "  Total: ₹" << totalAmount << " | Status: " << statusStr(status) << "\n";
    }
};
int Order::counter = 0;

// ── Restaurant ────────────────────────────────────────────────────────────────
class Restaurant {
public:
    int id;
    string name;
    string location;
    unordered_map<int, MenuItem> menu;
    int menuItemCounter = 0;

    Restaurant(int i, const string& n, const string& loc)
        : id(i), name(n), location(loc) {}

    int addMenuItem(const string& name, double price, const string& category) {
        int itemId = ++menuItemCounter;
        menu[itemId] = { itemId, name, price, category };
        return itemId;
    }

    const MenuItem* getItem(int itemId) const {
        auto it = menu.find(itemId);
        return (it != menu.end()) ? &it->second : nullptr;
    }

    void printMenu() const {
        cout << "\n=== " << name << " Menu ===\n";
        for (const auto& [id, item] : menu) {
            cout << "  [" << id << "] " << item.name
                 << " - ₹" << item.basePrice
                 << " (" << item.category << ")\n";
        }
    }
};

// ── Food Ordering System (Facade + Singleton) ─────────────────────────────────
class FoodOrderingSystem {
    static FoodOrderingSystem* instance;
    unordered_map<int, unique_ptr<Restaurant>> restaurants;
    unordered_map<int, unique_ptr<Order>> orders;
    int restCounter = 0;

    FoodOrderingSystem() = default;
public:
    static FoodOrderingSystem* getInstance() {
        if (!instance) instance = new FoodOrderingSystem();
        return instance;
    }

    int addRestaurant(const string& name, const string& location) {
        int id = ++restCounter;
        restaurants[id] = make_unique<Restaurant>(id, name, location);
        return id;
    }

    Restaurant* getRestaurant(int id) {
        auto it = restaurants.find(id);
        return (it != restaurants.end()) ? it->second.get() : nullptr;
    }

    int placeOrder(int customerId, int restaurantId,
                   vector<pair<int, int>> itemQuantities,  // {itemId, qty}
                   PaymentStrategy& payment,
                   OrderObserver* observer = nullptr) {

        auto* rest = getRestaurant(restaurantId);
        if (!rest) throw runtime_error("Restaurant not found");

        vector<OrderItem> items;
        double total = 0;

        for (const auto& [itemId, qty] : itemQuantities) {
            const auto* menuItem = rest->getItem(itemId);
            if (!menuItem) throw runtime_error("Item not found: " + to_string(itemId));
            if (!menuItem->isAvailable) throw runtime_error(menuItem->name + " unavailable");
            items.push_back({*menuItem, qty, {}});
            total += menuItem->basePrice * qty;
        }

        // Process payment
        if (!payment.pay(total)) throw runtime_error("Payment failed");

        auto order = make_unique<Order>(customerId, restaurantId, items);
        if (observer) order->addObserver(observer);

        int orderId = order->id;
        orders[orderId] = move(order);
        cout << "✅ Order #" << orderId << " placed successfully!\n";
        return orderId;
    }

    bool updateOrderStatus(int orderId, OrderStatus status) {
        auto it = orders.find(orderId);
        if (it == orders.end()) { cout << "Order not found\n"; return false; }
        return it->second->transition(status);
    }

    void printOrder(int orderId) const {
        auto it = orders.find(orderId);
        if (it != orders.end()) it->second->printSummary();
    }
};
FoodOrderingSystem* FoodOrderingSystem::instance = nullptr;

// ── Main ──────────────────────────────────────────────────────────────────────
int main() {
    auto* sys = FoodOrderingSystem::getInstance();

    // Setup restaurant
    int dominos = sys->addRestaurant("Domino's Pizza", "Bandra West");
    auto* rest = sys->getRestaurant(dominos);
    int pizza = rest->addMenuItem("Margherita Pizza", 299, "Pizza");
    int garlic = rest->addMenuItem("Garlic Bread", 99, "Sides");
    int coke   = rest->addMenuItem("Coke 500ml", 60, "Beverages");
    rest->printMenu();

    // Customer places order
    CustomerNotifier notifier("Ravi");
    UPIPayment upi("ravi@upi");

    int orderId = sys->placeOrder(
        1001, dominos,
        {{pizza, 1}, {garlic, 2}, {coke, 2}},
        upi, &notifier
    );

    sys->printOrder(orderId);

    // Simulate order lifecycle
    sys->updateOrderStatus(orderId, OrderStatus::ACCEPTED);
    sys->updateOrderStatus(orderId, OrderStatus::PREPARING);
    sys->updateOrderStatus(orderId, OrderStatus::PICKED_UP);
    sys->updateOrderStatus(orderId, OrderStatus::DELIVERED);

    // Try invalid transition
    sys->updateOrderStatus(orderId, OrderStatus::CANCELLED);  // should fail

    return 0;
}
```

## Interview Q&A

**Q: How do you handle two customers ordering the last item simultaneously?**
Use optimistic locking on MenuItem's `stockCount`. On order placement, run:
```sql
UPDATE menu_items SET stock = stock - qty
WHERE id = ? AND stock >= qty
```
If 0 rows affected → item out of stock → rollback and notify user. In Cassandra use lightweight transactions (`IF stock >= qty`). In Redis use Lua script for atomic decrement.

**Q: How is the delivery agent assigned to an order?**
DeliveryManager receives a Kafka event `order.accepted`. It queries active agents within X km radius using geohash lookup (Redis GEOSEARCH). Sorts by: free agents first, then by proximity. Assigns the best match. Uses a distributed lock (Redis `SETNX`) on the agent to prevent double-assignment from concurrent events.
