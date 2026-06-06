# ATM System — Low Level Design

## Requirements

### Functional
- Accept card and validate PIN
- Support: balance inquiry, cash withdrawal, deposit, mini-statement
- Dispense cash using available denominations
- Print receipt
- Lock card after 3 consecutive wrong PIN attempts
- Return card on cancel or after transaction

### Non-Functional
- One transaction at a time per ATM (state machine prevents concurrent operations on same machine)
- Concurrent withdrawals from the same bank account must be safe (optimistic locking / serialized at bank)
- Audit trail for every transaction

---

## State Machine

```
          insertCard()          verifyPin()           selectTransaction()
  IDLE ──────────────→ CARD_INSERTED ──────────────→ PIN_VERIFIED ──────────────→ TRANSACTION_SELECTED
   ↑                        │                              │                              │
   │                   cancel()/eject                 wrongPin×3                    dispense / print
   │                        │                         lockCard()                         │
   └────────────────────────┴──────────────────────────────┴──────────────────────────────┘
                                        returnCard() → IDLE
```

States: `IDLE`, `CARD_INSERTED`, `PIN_VERIFIED`, `TRANSACTION_SELECTED`, `CASH_DISPENSED`

---

## Key Classes

| Class              | Responsibility                                               |
|--------------------|--------------------------------------------------------------|
| ATM                | Context in State pattern; owns hardware components          |
| ATMState (abstract)| State interface: `insertCard`, `authenticatePin`, etc.      |
| IdleState          | Only insertCard is valid                                     |
| CardInsertedState  | Only authenticatePin is valid                               |
| PinVerifiedState   | Only selectTransaction is valid                             |
| TransactionState   | Execute and finalize transaction                            |
| Card               | Holds card number, associated account                       |
| Account            | Balance, transaction history                                |
| Transaction        | Amount, type, timestamp, result                             |
| CashDispenser      | Chain of Responsibility for denomination selection          |
| CardReader         | Hardware abstraction — reads card data                      |
| Display            | Shows messages to user                                      |
| Keypad             | Accepts PIN / amount input                                  |
| Printer            | Prints receipt                                              |

---

## Design Patterns Used

| Pattern                   | Applied To                             | Reason                                                              |
|---------------------------|----------------------------------------|---------------------------------------------------------------------|
| State                     | ATM states                             | Each state allows different operations; invalid ops throw errors    |
| Chain of Responsibility   | CashDispenser denominations            | Each denomination handler tries to fill request, passes remainder   |
| Singleton                 | ATM (one physical machine)             | Single ATM instance per process                                     |

---

## Full C++ Implementation

```cpp
#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <mutex>
#include <chrono>
#include <stdexcept>
#include <map>

// ─────────────────────────────────────────────
// Forward declarations
// ─────────────────────────────────────────────

class ATM;

// ─────────────────────────────────────────────
// Enums
// ─────────────────────────────────────────────

enum class TransactionType { WITHDRAWAL, DEPOSIT, BALANCE_INQUIRY, MINI_STATEMENT };
enum class TransactionStatus { PENDING, SUCCESS, FAILED, CANCELLED };

// ─────────────────────────────────────────────
// Transaction
// ─────────────────────────────────────────────

class Transaction {
    std::string         id_;
    TransactionType     type_;
    double              amount_;
    TransactionStatus   status_ = TransactionStatus::PENDING;
    std::string         timestamp_;
public:
    Transaction(std::string id, TransactionType type, double amount)
        : id_(std::move(id)), type_(type), amount_(amount) {
        // In production: use chrono + format
        timestamp_ = "2026-03-24T10:00:00";
    }

    void setStatus(TransactionStatus s) { status_ = s; }

    TransactionStatus getStatus() const { return status_; }
    double            getAmount() const { return amount_; }
    TransactionType   getType()   const { return type_;   }
    const std::string& getId()    const { return id_;     }

    void print() const {
        static const std::map<TransactionType, std::string> typeNames = {
            {TransactionType::WITHDRAWAL,      "Withdrawal"},
            {TransactionType::DEPOSIT,         "Deposit"},
            {TransactionType::BALANCE_INQUIRY, "Balance Inquiry"},
            {TransactionType::MINI_STATEMENT,  "Mini Statement"},
        };
        static const std::map<TransactionStatus, std::string> statusNames = {
            {TransactionStatus::PENDING,   "PENDING"},
            {TransactionStatus::SUCCESS,   "SUCCESS"},
            {TransactionStatus::FAILED,    "FAILED"},
            {TransactionStatus::CANCELLED, "CANCELLED"},
        };
        std::cout << "[" << timestamp_ << "] " << typeNames.at(type_)
                  << " $" << amount_ << " → " << statusNames.at(status_) << "\n";
    }
};

// ─────────────────────────────────────────────
// Account
// ─────────────────────────────────────────────

class Account {
    std::string accountNumber_;
    double      balance_;
    bool        isLocked_ = false;
    std::vector<Transaction> history_;
    mutable std::mutex accountMutex_; // protects balance and isLocked

public:
    Account(std::string number, double initialBalance)
        : accountNumber_(std::move(number)), balance_(initialBalance) {}

    const std::string& getAccountNumber() const { return accountNumber_; }

    // Returns false if account locked or insufficient funds
    bool withdraw(double amount, Transaction& txn) {
        std::lock_guard<std::mutex> lock(accountMutex_);
        if (isLocked_) {
            txn.setStatus(TransactionStatus::FAILED);
            std::cout << "Account is locked.\n";
            return false;
        }
        if (balance_ < amount) {
            txn.setStatus(TransactionStatus::FAILED);
            std::cout << "Insufficient funds.\n";
            return false;
        }
        balance_ -= amount;
        txn.setStatus(TransactionStatus::SUCCESS);
        history_.push_back(txn);
        return true;
    }

    bool deposit(double amount, Transaction& txn) {
        std::lock_guard<std::mutex> lock(accountMutex_);
        balance_ += amount;
        txn.setStatus(TransactionStatus::SUCCESS);
        history_.push_back(txn);
        return true;
    }

    double getBalance() const {
        std::lock_guard<std::mutex> lock(accountMutex_);
        return balance_;
    }

    void lock() {
        std::lock_guard<std::mutex> lock(accountMutex_);
        isLocked_ = true;
        std::cout << "Account " << accountNumber_ << " locked after too many PIN failures.\n";
    }

    void printMiniStatement() const {
        std::lock_guard<std::mutex> lock(accountMutex_);
        int count = 0;
        for (auto it = history_.rbegin(); it != history_.rend() && count < 5; ++it, ++count) {
            it->print();
        }
    }
};

// ─────────────────────────────────────────────
// Card
// ─────────────────────────────────────────────

class Card {
    std::string cardNumber_;
    std::string pin_;          // hashed in production
    Account*    account_;
    int         failedAttempts_ = 0;
    bool        isLocked_       = false;

public:
    Card(std::string number, std::string pin, Account* account)
        : cardNumber_(std::move(number)), pin_(std::move(pin)), account_(account) {}

    bool validatePin(const std::string& inputPin) {
        if (inputPin == pin_) {
            failedAttempts_ = 0;
            return true;
        }
        ++failedAttempts_;
        if (failedAttempts_ >= 3) {
            isLocked_ = true;
            account_->lock();
        }
        return false;
    }

    bool        isLocked()              const { return isLocked_;    }
    Account*    getAccount()            const { return account_;     }
    const std::string& getCardNumber()  const { return cardNumber_;  }
};

// ─────────────────────────────────────────────
// Chain of Responsibility — Cash Dispenser
// ─────────────────────────────────────────────

class DenominationHandler {
protected:
    DenominationHandler* next_ = nullptr;
    int denomination_;
    int count_;
public:
    DenominationHandler(int denom, int count)
        : denomination_(denom), count_(count) {}
    virtual ~DenominationHandler() = default;

    void setNext(DenominationHandler* next) { next_ = next; }

    // Returns remaining amount after dispensing from this handler
    virtual int handle(int amount) {
        int notesToDispense = std::min(amount / denomination_, count_);
        int dispensed       = notesToDispense * denomination_;
        count_             -= notesToDispense;

        if (notesToDispense > 0) {
            std::cout << "  Dispensing " << notesToDispense
                      << " x $" << denomination_ << "\n";
        }

        int remaining = amount - dispensed;
        if (remaining > 0 && next_) {
            return next_->handle(remaining);
        }
        return remaining; // 0 means fully dispensed
    }

    int getAvailable() const { return denomination_ * count_; }
};

class CashDispenser {
    // Handlers from largest to smallest denomination
    DenominationHandler h100_{100, 50};
    DenominationHandler h50_ {50,  100};
    DenominationHandler h20_ {20,  200};
    DenominationHandler h10_ {10,  500};

public:
    CashDispenser() {
        h100_.setNext(&h50_);
        h50_.setNext(&h20_);
        h20_.setNext(&h10_);
    }

    bool canDispense(int amount) const {
        return (h100_.getAvailable() + h50_.getAvailable() +
                h20_.getAvailable() + h10_.getAvailable()) >= amount;
    }

    // Returns true if fully dispensed
    bool dispense(int amount) {
        std::cout << "Dispensing $" << amount << ":\n";
        int remaining = h100_.handle(amount);
        if (remaining > 0) {
            std::cout << "  Cannot dispense exact amount. Remaining: $" << remaining << "\n";
            return false;
        }
        return true;
    }
};

// ─────────────────────────────────────────────
// Hardware Components
// ─────────────────────────────────────────────

class CardReader {
public:
    void ejectCard() const {
        std::cout << "[CardReader] Card ejected.\n";
    }
    void retainCard() const {
        std::cout << "[CardReader] Card retained (locked).\n";
    }
};

class Display {
public:
    void show(const std::string& message) const {
        std::cout << "[Display] " << message << "\n";
    }
};

class Keypad {
public:
    std::string getInput() const { return ""; } // abstraction; real impl reads from hardware
};

class Printer {
public:
    void printReceipt(const Transaction& txn) const {
        std::cout << "[Printer] Receipt:\n";
        txn.print();
    }
};

// ─────────────────────────────────────────────
// State Pattern — ATM States
// ─────────────────────────────────────────────

class ATMState {
public:
    virtual ~ATMState() = default;
    virtual void insertCard(ATM& atm, Card* card)                        = 0;
    virtual void authenticatePin(ATM& atm, const std::string& pin)       = 0;
    virtual void selectTransaction(ATM& atm, TransactionType type,
                                   double amount)                         = 0;
    virtual void cancel(ATM& atm)                                        = 0;
    virtual const char* name() const                                     = 0;
};

// ─────────────────────────────────────────────
// ATM — Context
// ─────────────────────────────────────────────

class ATM {
    std::unique_ptr<ATMState> state_;
    Card*           insertedCard_ = nullptr;
    CashDispenser   cashDispenser_;
    CardReader      cardReader_;
    Display         display_;
    Printer         printer_;
    int             txnCounter_ = 0;

public:
    ATM();  // defined after state classes

    void setState(std::unique_ptr<ATMState> state) {
        state_ = std::move(state);
        display_.show(std::string("State: ") + state_->name());
    }

    void insertCard(Card* card)                               { state_->insertCard(*this, card); }
    void authenticatePin(const std::string& pin)              { state_->authenticatePin(*this, pin); }
    void selectTransaction(TransactionType type, double amt)  { state_->selectTransaction(*this, type, amt); }
    void cancel()                                             { state_->cancel(*this); }

    // Accessors used by state objects
    Card*          getInsertedCard()  const { return insertedCard_;   }
    void           setInsertedCard(Card* c) { insertedCard_ = c;      }
    CashDispenser& getCashDispenser()       { return cashDispenser_;  }
    CardReader&    getCardReader()          { return cardReader_;      }
    Display&       getDisplay()             { return display_;         }
    Printer&       getPrinter()             { return printer_;         }

    std::string generateTxnId() {
        return "TXN-" + std::to_string(++txnCounter_);
    }
};

// ─────────────────────────────────────────────
// Concrete States
// ─────────────────────────────────────────────

class IdleState : public ATMState {
public:
    void insertCard(ATM& atm, Card* card) override {
        if (card->isLocked()) {
            atm.getDisplay().show("Card is locked. Please contact your bank.");
            atm.getCardReader().retainCard();
            return;
        }
        atm.setInsertedCard(card);
        atm.getDisplay().show("Card accepted. Please enter PIN.");
        atm.setState(std::make_unique<class CardInsertedState>());
    }
    void authenticatePin(ATM&, const std::string&) override {
        std::cout << "No card inserted.\n";
    }
    void selectTransaction(ATM&, TransactionType, double) override {
        std::cout << "No card inserted.\n";
    }
    void cancel(ATM&) override {
        std::cout << "Nothing to cancel.\n";
    }
    const char* name() const override { return "IDLE"; }
};

class CardInsertedState : public ATMState {
public:
    void insertCard(ATM&, Card*) override {
        std::cout << "Card already inserted.\n";
    }
    void authenticatePin(ATM& atm, const std::string& pin) override {
        Card* card = atm.getInsertedCard();
        if (card->validatePin(pin)) {
            atm.getDisplay().show("PIN correct. Select transaction.");
            atm.setState(std::make_unique<class PinVerifiedState>());
        } else if (card->isLocked()) {
            atm.getDisplay().show("Too many wrong attempts. Card retained.");
            atm.getCardReader().retainCard();
            atm.setInsertedCard(nullptr);
            atm.setState(std::make_unique<IdleState>());
        } else {
            atm.getDisplay().show("Wrong PIN. Try again.");
        }
    }
    void selectTransaction(ATM&, TransactionType, double) override {
        std::cout << "PIN not verified yet.\n";
    }
    void cancel(ATM& atm) override {
        atm.getDisplay().show("Cancelled. Card ejected.");
        atm.getCardReader().ejectCard();
        atm.setInsertedCard(nullptr);
        atm.setState(std::make_unique<IdleState>());
    }
    const char* name() const override { return "CARD_INSERTED"; }
};

class PinVerifiedState : public ATMState {
public:
    void insertCard(ATM&, Card*) override { std::cout << "Card already in use.\n"; }
    void authenticatePin(ATM&, const std::string&) override { std::cout << "Already authenticated.\n"; }

    void selectTransaction(ATM& atm, TransactionType type, double amount) override {
        Card*    card    = atm.getInsertedCard();
        Account* account = card->getAccount();
        Transaction txn(atm.generateTxnId(), type, amount);

        switch (type) {
            case TransactionType::WITHDRAWAL: {
                if (!atm.getCashDispenser().canDispense(static_cast<int>(amount))) {
                    atm.getDisplay().show("ATM cannot dispense requested amount.");
                    txn.setStatus(TransactionStatus::FAILED);
                    break;
                }
                if (account->withdraw(amount, txn)) {
                    atm.getCashDispenser().dispense(static_cast<int>(amount));
                    atm.getDisplay().show("Please collect your cash.");
                    atm.getPrinter().printReceipt(txn);
                }
                break;
            }
            case TransactionType::DEPOSIT: {
                account->deposit(amount, txn);
                atm.getDisplay().show("Deposit successful.");
                atm.getPrinter().printReceipt(txn);
                break;
            }
            case TransactionType::BALANCE_INQUIRY: {
                double bal = account->getBalance();
                atm.getDisplay().show("Balance: $" + std::to_string(bal));
                txn.setStatus(TransactionStatus::SUCCESS);
                break;
            }
            case TransactionType::MINI_STATEMENT: {
                account->printMiniStatement();
                txn.setStatus(TransactionStatus::SUCCESS);
                break;
            }
        }

        // After any transaction, eject card and return to idle
        atm.getDisplay().show("Transaction complete. Card ejected.");
        atm.getCardReader().ejectCard();
        atm.setInsertedCard(nullptr);
        atm.setState(std::make_unique<IdleState>());
    }

    void cancel(ATM& atm) override {
        atm.getDisplay().show("Cancelled. Card ejected.");
        atm.getCardReader().ejectCard();
        atm.setInsertedCard(nullptr);
        atm.setState(std::make_unique<IdleState>());
    }
    const char* name() const override { return "PIN_VERIFIED"; }
};

// ─────────────────────────────────────────────
// ATM constructor — defined after state classes
// ─────────────────────────────────────────────

ATM::ATM() : state_(std::make_unique<IdleState>()) {
    display_.show("ATM ready.");
}

// ─────────────────────────────────────────────
// Demo
// ─────────────────────────────────────────────

int main() {
    Account acc("ACC-001", 5000.0);
    Card    card("4111-1111-1111-1111", "1234", &acc);

    ATM atm;

    // Normal flow
    atm.insertCard(&card);
    atm.authenticatePin("1234");
    atm.selectTransaction(TransactionType::BALANCE_INQUIRY, 0.0);

    atm.insertCard(&card);
    atm.authenticatePin("1234");
    atm.selectTransaction(TransactionType::WITHDRAWAL, 500.0);

    // Wrong PIN flow
    Card card2("4222-2222-2222-2222", "9999", &acc);
    atm.insertCard(&card2);
    atm.authenticatePin("0000"); // wrong
    atm.authenticatePin("0001"); // wrong
    atm.authenticatePin("0002"); // wrong — card retained

    return 0;
}
```

---

## Concurrency Considerations

**Problem:** Two users initiate withdrawals of $500 from the same account (balance $600) at the same time on different ATMs.

**Solution layers:**

1. **Account-level mutex** — `accountMutex_` in `Account::withdraw` serializes balance reads and writes. The second thread to acquire the lock will see the updated balance and fail with "Insufficient funds."

2. **Database-level optimistic locking** — In a real system, the account row has a `version` column. The UPDATE is `WHERE version = X AND balance >= amount`. If two concurrent transactions both read version 1, only one UPDATE will match; the other gets 0 rows affected and retries or fails.

3. **ATM state machine** — Each physical ATM can only process one card at a time (enforced by the state machine). Two ATMs sharing the same account still serialize at the `Account` object or database.

---

## Interview Q&A

**Q1: Why use the State pattern instead of if/else chains in ATM?**

With if/else, every method would need to check the current state and handle all state-specific behavior in one class. Adding a new state (e.g., `MAINTENANCE`) would require modifying every method. The State pattern localizes each state's behavior in its own class, making it easy to add, remove, or modify states independently without touching other states (Open/Closed Principle).

**Q2: How does the Chain of Responsibility work for cash dispensing, and what happens if the ATM cannot make exact change?**

Each `DenominationHandler` computes how many notes of its denomination it can contribute to the requested amount, deducts from its count, then passes the remainder to the next handler. If the final handler still has a non-zero remainder, `dispense` returns `false`. The `PinVerifiedState` checks `canDispense` before attempting, so the account is never debited unless the ATM confirms it can dispense the full amount.

**Q3: How would you add a transfer-to-another-account feature?**

Add `TransactionType::TRANSFER` and a `targetAccountNumber` parameter to `selectTransaction`. In `PinVerifiedState`, resolve the target account (from a bank service injected into ATM), debit the source account and credit the target within a single database transaction (use a unit-of-work / saga pattern for cross-account consistency). If credit fails, roll back the debit. The state machine flow is unchanged.
