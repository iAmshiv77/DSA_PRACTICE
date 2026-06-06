# LLD: Vending Machine

## Requirements
- Display products and prices
- Accept coins/notes (INR/USD denominations)
- Dispense product when sufficient amount inserted
- Return change
- Admin can restock products and collect money
- Handle sold-out products

---

## State Machine (Core of This Problem)

```
IDLE
  → insertCoin() → HAS_MONEY
  → selectProduct() (no money) → stays IDLE, shows "insert money"

HAS_MONEY
  → insertCoin() → HAS_MONEY (accumulate)
  → selectProduct() →
      IF product available AND money sufficient → DISPENSING
      IF insufficient money → stays HAS_MONEY, shows "add more"
      IF sold out → stays HAS_MONEY, shows "sold out"
  → cancel() → return money → IDLE

DISPENSING
  → dispenseProduct() →
      IF change needed → RETURNING_CHANGE
      ELSE → IDLE

RETURNING_CHANGE
  → returnChange() → IDLE
```

---

## Class Design

```
VendingMachine (Context - holds state)
├── state: VendingMachineState
├── inventory: map<Product, int>      // product → quantity
├── coinSlot: CoinSlot
├── productDispenser: ProductDispenser
├── currentAmount: double
└── selectedProduct: Product*

VendingMachineState (interface)
├── insertCoin(VendingMachine*, Coin*)
├── selectProduct(VendingMachine*, string productCode)
├── cancel(VendingMachine*)
└── dispense(VendingMachine*)

Concrete States:
  IdleState, HasMoneyState, DispensingState, ReturningChangeState

Coin (enum or class)
├── PENNY(0.01), NICKEL(0.05), DIME(0.10), QUARTER(0.25)
├── ONE(1.00), FIVE(5.00), TEN(10.00)

Product
├── code: string, name: string, price: double

CoinSlot
├── insertCoin(Coin) → bool  (validate coin)
└── returnCoins(double amount) → vector<Coin>

ProductDispenser
└── dispense(Product*) → bool
```

---

## C++ Code

```cpp
#include <bits/stdc++.h>
using namespace std;

enum class Coin { PENNY=1, NICKEL=5, DIME=10, QUARTER=25, ONE_DOLLAR=100 };

double coinValue(Coin c) {
    switch(c) {
        case Coin::PENNY:       return 0.01;
        case Coin::NICKEL:      return 0.05;
        case Coin::DIME:        return 0.10;
        case Coin::QUARTER:     return 0.25;
        case Coin::ONE_DOLLAR:  return 1.00;
        default: return 0;
    }
}

struct Product {
    string code, name;
    double price;
};

class VendingMachine;

class VendingMachineState {
public:
    virtual void insertCoin(VendingMachine* vm, Coin coin) = 0;
    virtual void selectProduct(VendingMachine* vm, const string& code) = 0;
    virtual void cancel(VendingMachine* vm) = 0;
    virtual string getStateName() = 0;
    virtual ~VendingMachineState() = default;
};

class VendingMachine {
    VendingMachineState* state;
    map<string, Product> inventory;       // code → Product
    map<string, int> quantity;            // code → qty
    double currentAmount = 0.0;
    string selectedProductCode;

public:
    VendingMachine();

    void insertCoin(Coin coin)             { state->insertCoin(this, coin); }
    void selectProduct(const string& code) { state->selectProduct(this, code); }
    void cancel()                          { state->cancel(this); }

    void setState(VendingMachineState* s) {
        delete state; state = s;
    }

    void addAmount(double amount) { currentAmount += amount; }
    double getCurrentAmount() { return currentAmount; }

    bool hasProduct(const string& code) {
        return quantity.count(code) && quantity[code] > 0;
    }
    Product* getProduct(const string& code) {
        if (!inventory.count(code)) return nullptr;
        return &inventory[code];
    }

    void dispenseProduct(const string& code) {
        quantity[code]--;
        cout << "Dispensing: " << inventory[code].name << endl;
    }

    double returnChange() {
        double change = currentAmount;
        currentAmount = 0;
        cout << "Returning change: $" << change << endl;
        return change;
    }

    void setSelectedProduct(const string& code) { selectedProductCode = code; }
    string getSelectedProduct() { return selectedProductCode; }

    void addProduct(const Product& p, int qty) {
        inventory[p.code] = p;
        quantity[p.code] += qty;
    }
};

class IdleState : public VendingMachineState {
public:
    void insertCoin(VendingMachine* vm, Coin coin) override;
    void selectProduct(VendingMachine* vm, const string& code) override {
        cout << "Please insert money first." << endl;
    }
    void cancel(VendingMachine* vm) override {
        cout << "Nothing to cancel." << endl;
    }
    string getStateName() override { return "IDLE"; }
};

class HasMoneyState : public VendingMachineState {
public:
    void insertCoin(VendingMachine* vm, Coin coin) override {
        vm->addAmount(coinValue(coin));
        cout << "Total: $" << vm->getCurrentAmount() << endl;
    }
    void selectProduct(VendingMachine* vm, const string& code) override;
    void cancel(VendingMachine* vm) override {
        vm->returnChange();
        vm->setState(new IdleState());
    }
    string getStateName() override { return "HAS_MONEY"; }
};

class DispensingState : public VendingMachineState {
public:
    DispensingState(VendingMachine* vm) {
        // Immediately dispense on entering this state
        string code = vm->getSelectedProduct();
        Product* p = vm->getProduct(code);
        vm->dispenseProduct(code);
        double change = vm->getCurrentAmount() - p->price;
        // return change if needed
        if (change > 0.001) {
            cout << "Change: $" << change << endl;
        }
        vm->returnChange();
        vm->setState(new IdleState());
    }
    void insertCoin(VendingMachine* vm, Coin c) override { cout << "Dispensing..."; }
    void selectProduct(VendingMachine* vm, const string&) override { cout << "Dispensing..."; }
    void cancel(VendingMachine* vm) override { cout << "Cannot cancel now."; }
    string getStateName() override { return "DISPENSING"; }
};

// Implement deferred methods
void IdleState::insertCoin(VendingMachine* vm, Coin coin) {
    vm->addAmount(coinValue(coin));
    cout << "Coin inserted: $" << coinValue(coin) << ". Total: $" << vm->getCurrentAmount() << endl;
    vm->setState(new HasMoneyState());
}

void HasMoneyState::selectProduct(VendingMachine* vm, const string& code) {
    if (!vm->hasProduct(code)) {
        cout << "Product sold out!" << endl;
        return;
    }
    Product* p = vm->getProduct(code);
    if (vm->getCurrentAmount() < p->price) {
        cout << "Insufficient. Need $" << (p->price - vm->getCurrentAmount()) << " more." << endl;
        return;
    }
    vm->setSelectedProduct(code);
    vm->setState(new DispensingState(vm));
}

VendingMachine::VendingMachine() : state(new IdleState()) {}

int main() {
    VendingMachine vm;
    vm.addProduct({"A1", "Coke", 1.50}, 5);
    vm.addProduct({"A2", "Pepsi", 1.25}, 3);
    vm.addProduct({"B1", "Chips", 2.00}, 0);  // sold out

    vm.insertCoin(Coin::ONE_DOLLAR);
    vm.insertCoin(Coin::QUARTER);
    vm.insertCoin(Coin::QUARTER);
    vm.selectProduct("A1");  // Coke $1.50, inserted $1.50 → dispense, no change

    vm.insertCoin(Coin::ONE_DOLLAR);
    vm.selectProduct("B1");  // Sold out
    vm.cancel();

    return 0;
}
```

---

## Edge Cases

| Edge Case | Solution |
|-----------|---------|
| Exact change not possible | Smallest denomination algorithm; if impossible → cancel and return all |
| Product sold mid-selection | Recheck quantity atomically before dispense |
| Coin jam / dispenser failure | Return to HAS_MONEY state; alert maintenance |
| Power failure during dispense | State persisted to disk; resume on restart |
| Admin restocks during active session | Mutex on inventory access |

---

## Interview Questions

**Q: Why use State pattern instead of if-else?**
A: Each state is self-contained. Adding new state = new class. No modification of existing states (OCP). Illegal transitions throw or are ignored within the state class — easy to reason about.

**Q: How do you handle concurrent users at two separate vending machines sharing the same product inventory?**
A: Shared inventory DB with pessimistic locking (SELECT FOR UPDATE on product row). Decrement atomically. If quantity = 0, reject dispense and return money.

**Q: How would you extend this to accept credit cards?**
A: Add a PaymentMethod interface. CoinPayment and CardPayment implement it. VendingMachine takes PaymentMethod. No changes to existing coin code. (Open/Closed Principle)
