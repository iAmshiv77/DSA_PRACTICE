# Library Management System — Low Level Design

## Requirements

### Functional
- Catalog of Books; each Book may have multiple physical BookItems (copies)
- Members can search books by title, author, ISBN, subject
- Members can borrow up to N books simultaneously (configurable)
- Members can reserve a book that is currently checked out
- When a reserved book is returned, the reserving member is notified (Observer)
- Librarian can add/remove books, manage members, process check-in/check-out
- System tracks due dates and calculates overdue Fines
- Each Member has a LibraryCard with a unique barcode

### Non-Functional
- Single Library instance (Singleton)
- Pluggable book creation via Factory
- Thread-safe for concurrent borrow/return operations

---

## Actors and Use Cases

| Actor     | Use Cases                                                              |
|-----------|------------------------------------------------------------------------|
| Member    | Search catalog, borrow item, return item, reserve item, pay fine       |
| Librarian | Add/remove book, register member, process check-out/in, waive fine     |
| System    | Notify member when reserved book becomes available, compute overdue fee |

---

## Class Diagram (textual)

```
Library (Singleton)
  ├── BookCatalog
  │     └── map<isbn, Book>
  ├── vector<Member>
  ├── vector<Librarian>
  └── NotificationService  ← Observer hub

Book
  ├── isbn, title, author, subject, publicationDate
  └── vector<BookItem*>

BookItem  (physical copy)
  ├── barcode, rackLocation
  ├── BookStatus { AVAILABLE, RESERVED, LOANED, LOST }
  └── Book*  (back-ref)

Person (abstract)
  ├── Member
  │     ├── LibraryCard
  │     ├── vector<BookLending*>  (active loans)
  │     └── vector<BookReservation*>
  └── Librarian

LibraryCard
  ├── cardNumber, issueDate, expiryDate
  └── bool isActive

BookLending
  ├── BookItem*, Member*
  ├── lendingDate, dueDate, returnDate
  └── static checkout() / returnBook()

BookReservation
  ├── BookItem*, Member*
  ├── reservationDate, status
  └── ReservationStatus { WAITING, PENDING, COMPLETED, CANCELLED }

Fine
  ├── BookLending*
  ├── amount, isPaid
  └── static calculate(lending)

BookFactory  ← Factory
  └── static create(type, ...) → unique_ptr<Book>
```

---

## Design Patterns Used

| Pattern   | Applied To                  | Reason                                                                  |
|-----------|-----------------------------|-------------------------------------------------------------------------|
| Singleton | Library                     | One library instance coordinates all state globally                     |
| Factory   | BookFactory                 | Decouple creation of book types (TextBook, Novel, Journal, etc.)        |
| Observer  | NotificationService         | Notify waiting members automatically when a reserved book is returned   |

---

## Full C++ Implementation

```cpp
#include <iostream>
#include <vector>
#include <map>
#include <string>
#include <memory>
#include <mutex>
#include <algorithm>
#include <ctime>
#include <stdexcept>
#include <functional>

// ─────────────────────────────────────────────
// Enums
// ─────────────────────────────────────────────

enum class BookStatus       { AVAILABLE, RESERVED, LOANED, LOST };
enum class ReservationStatus{ WAITING, PENDING, COMPLETED, CANCELLED };
enum class BookType         { NOVEL, TEXTBOOK, JOURNAL, MAGAZINE };

// ─────────────────────────────────────────────
// Observer — Notification
// ─────────────────────────────────────────────

// Observer interface — any entity that wants to be notified
class BookAvailabilityObserver {
public:
    virtual ~BookAvailabilityObserver() = default;
    virtual void onBookAvailable(const std::string& isbn,
                                 const std::string& title) = 0;
};

// Subject — holds a list of observers and notifies them
class NotificationService {
    // isbn → list of observers waiting for that book
    std::map<std::string, std::vector<BookAvailabilityObserver*>> waitList_;
    std::mutex mutex_;
public:
    void subscribe(const std::string& isbn, BookAvailabilityObserver* observer) {
        std::lock_guard<std::mutex> lock(mutex_);
        waitList_[isbn].push_back(observer);
    }

    void unsubscribe(const std::string& isbn, BookAvailabilityObserver* observer) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& list = waitList_[isbn];
        list.erase(std::remove(list.begin(), list.end(), observer), list.end());
    }

    // Called by Library when a BookItem is returned
    void notifyAvailable(const std::string& isbn, const std::string& title) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = waitList_.find(isbn);
        if (it == waitList_.end() || it->second.empty()) return;
        // Notify only the first observer in the wait list (FIFO)
        it->second.front()->onBookAvailable(isbn, title);
        it->second.erase(it->second.begin());
    }
};

// ─────────────────────────────────────────────
// LibraryCard
// ─────────────────────────────────────────────

class LibraryCard {
    std::string cardNumber_;
    bool        isActive_;
    time_t      issueDate_;
public:
    explicit LibraryCard(std::string num)
        : cardNumber_(std::move(num)), isActive_(true), issueDate_(std::time(nullptr)) {}

    const std::string& getCardNumber() const { return cardNumber_; }
    bool isActive() const { return isActive_; }
    void deactivate() { isActive_ = false; }
    void activate()   { isActive_ = true;  }
};

// ─────────────────────────────────────────────
// Forward declarations
// ─────────────────────────────────────────────

class BookItem;
class Member;

// ─────────────────────────────────────────────
// Fine
// ─────────────────────────────────────────────

class Fine {
    double amount_;
    bool   isPaid_;
public:
    Fine() : amount_(0.0), isPaid_(false) {}

    // $1 per day overdue
    static double calculate(time_t dueDate, time_t returnDate) {
        if (returnDate <= dueDate) return 0.0;
        double diffSeconds = difftime(returnDate, dueDate);
        double days        = diffSeconds / 86400.0;
        return days * 1.0;
    }

    void setAmount(double amt) { amount_ = amt; }
    double getAmount()   const { return amount_; }
    bool   getIsPaid()   const { return isPaid_; }
    void   pay()               { isPaid_ = true;  }
};

// ─────────────────────────────────────────────
// BookLending
// ─────────────────────────────────────────────

class BookLending {
    BookItem* item_;
    Member*   member_;
    time_t    lendingDate_;
    time_t    dueDate_;        // lendingDate + 14 days
    time_t    returnDate_;
    bool      returned_;
    Fine      fine_;

public:
    BookLending(BookItem* item, Member* member)
        : item_(item), member_(member),
          lendingDate_(std::time(nullptr)),
          returnDate_(0), returned_(false)
    {
        dueDate_ = lendingDate_ + 14 * 86400; // 14-day loan period
    }

    BookItem* getItem()        const { return item_;        }
    Member*   getMember()      const { return member_;      }
    time_t    getDueDate()     const { return dueDate_;     }
    bool      isReturned()     const { return returned_;    }
    const Fine& getFine()      const { return fine_;        }
    Fine&       getFine()            { return fine_;        }

    // Returns overdue fine amount (0 if on time)
    double markReturned() {
        returnDate_ = std::time(nullptr);
        returned_   = true;
        double amt  = Fine::calculate(dueDate_, returnDate_);
        fine_.setAmount(amt);
        return amt;
    }
};

// ─────────────────────────────────────────────
// BookReservation
// ─────────────────────────────────────────────

class BookReservation {
    std::string          isbn_;
    Member*              member_;
    time_t               reservationDate_;
    ReservationStatus    status_;
public:
    BookReservation(std::string isbn, Member* member)
        : isbn_(std::move(isbn)), member_(member),
          reservationDate_(std::time(nullptr)),
          status_(ReservationStatus::WAITING) {}

    const std::string& getIsbn()   const { return isbn_;   }
    Member*            getMember() const { return member_;  }
    ReservationStatus  getStatus() const { return status_;  }

    void complete() { status_ = ReservationStatus::COMPLETED; }
    void cancel()   { status_ = ReservationStatus::CANCELLED; }
};

// ─────────────────────────────────────────────
// Book
// ─────────────────────────────────────────────

class BookItem; // forward

class Book {
protected:
    std::string isbn_;
    std::string title_;
    std::string author_;
    std::string subject_;
    BookType    type_;
    std::vector<std::unique_ptr<BookItem>> copies_;
public:
    Book(std::string isbn, std::string title,
         std::string author, std::string subject, BookType type)
        : isbn_(std::move(isbn)), title_(std::move(title)),
          author_(std::move(author)), subject_(std::move(subject)),
          type_(type) {}

    virtual ~Book() = default;

    const std::string& getIsbn()    const { return isbn_;    }
    const std::string& getTitle()   const { return title_;   }
    const std::string& getAuthor()  const { return author_;  }
    const std::string& getSubject() const { return subject_; }
    BookType getType() const { return type_; }

    void addCopy(std::unique_ptr<BookItem> item) {
        copies_.push_back(std::move(item));
    }

    // Returns first available copy, nullptr if all loaned/reserved
    BookItem* findAvailableCopy() const;

    const std::vector<std::unique_ptr<BookItem>>& getCopies() const {
        return copies_;
    }
};

// ─────────────────────────────────────────────
// BookItem — physical copy
// ─────────────────────────────────────────────

class BookItem {
    std::string barcode_;
    std::string rackLocation_;
    BookStatus  status_;
    Book*       book_;   // back-ref to parent book
public:
    BookItem(std::string barcode, std::string rack, Book* book)
        : barcode_(std::move(barcode)), rackLocation_(std::move(rack)),
          status_(BookStatus::AVAILABLE), book_(book) {}

    const std::string& getBarcode()      const { return barcode_;      }
    const std::string& getRackLocation() const { return rackLocation_; }
    BookStatus         getStatus()       const { return status_;        }
    Book*              getBook()         const { return book_;          }

    void setStatus(BookStatus s) { status_ = s; }
    bool isAvailable()  const { return status_ == BookStatus::AVAILABLE; }
};

// Now define findAvailableCopy after BookItem is complete
BookItem* Book::findAvailableCopy() const {
    for (const auto& item : copies_) {
        if (item->isAvailable()) return item.get();
    }
    return nullptr;
}

// ─────────────────────────────────────────────
// Factory — creates Book subtypes
// ─────────────────────────────────────────────

class Novel    : public Book {
public:
    Novel(std::string isbn, std::string title, std::string author)
        : Book(std::move(isbn), std::move(title), std::move(author), "Fiction", BookType::NOVEL) {}
};

class TextBook : public Book {
    std::string edition_;
public:
    TextBook(std::string isbn, std::string title,
             std::string author, std::string edition)
        : Book(std::move(isbn), std::move(title), std::move(author), "Academic", BookType::TEXTBOOK),
          edition_(std::move(edition)) {}
    const std::string& getEdition() const { return edition_; }
};

class BookFactory {
public:
    static std::unique_ptr<Book> create(BookType type,
                                        const std::string& isbn,
                                        const std::string& title,
                                        const std::string& author,
                                        const std::string& extra = "") {
        switch (type) {
            case BookType::NOVEL:
                return std::make_unique<Novel>(isbn, title, author);
            case BookType::TEXTBOOK:
                return std::make_unique<TextBook>(isbn, title, author, extra);
            default:
                return std::make_unique<Book>(isbn, title, author, extra, type);
        }
    }
};

// ─────────────────────────────────────────────
// Person hierarchy
// ─────────────────────────────────────────────

class Person {
protected:
    std::string name_;
    std::string email_;
    std::string phone_;
public:
    Person(std::string name, std::string email, std::string phone)
        : name_(std::move(name)), email_(std::move(email)), phone_(std::move(phone)) {}
    virtual ~Person() = default;
    const std::string& getName()  const { return name_;  }
    const std::string& getEmail() const { return email_; }
};

// Member implements Observer so it receives book-available notifications
class Member : public Person, public BookAvailabilityObserver {
    LibraryCard                              card_;
    std::vector<std::unique_ptr<BookLending>>     activeLoans_;
    std::vector<std::unique_ptr<BookReservation>> reservations_;
    double                                    totalFine_;

    static constexpr int MAX_BOOKS = 5; // max simultaneous loans

public:
    Member(std::string name, std::string email, std::string phone, std::string cardNum)
        : Person(std::move(name), std::move(email), std::move(phone)),
          card_(std::move(cardNum)), totalFine_(0.0) {}

    const LibraryCard& getCard() const { return card_; }
    int  activeLoanCount()       const { return static_cast<int>(activeLoans_.size()); }
    bool canBorrow()             const { return activeLoanCount() < MAX_BOOKS; }
    double getTotalFine()        const { return totalFine_; }

    BookLending* addLoan(BookItem* item) {
        activeLoans_.push_back(std::make_unique<BookLending>(item, this));
        return activeLoans_.back().get();
    }

    void recordFine(double amt) { totalFine_ += amt; }

    void payFine(double amt) {
        totalFine_ -= amt;
        if (totalFine_ < 0.0) totalFine_ = 0.0;
    }

    void addReservation(std::unique_ptr<BookReservation> res) {
        reservations_.push_back(std::move(res));
    }

    // BookAvailabilityObserver — called by NotificationService
    void onBookAvailable(const std::string& isbn,
                         const std::string& title) override {
        std::cout << "[NOTIFY] Dear " << name_
                  << ": Book \"" << title
                  << "\" (ISBN: " << isbn << ") is now available!\n";
        // In a real system: send email/SMS, update reservation status
    }
};

class Librarian : public Person {
public:
    Librarian(std::string name, std::string email, std::string phone)
        : Person(std::move(name), std::move(email), std::move(phone)) {}
};

// ─────────────────────────────────────────────
// BookCatalog
// ─────────────────────────────────────────────

class BookCatalog {
    std::map<std::string, std::unique_ptr<Book>> books_; // isbn → Book
public:
    void addBook(std::unique_ptr<Book> book) {
        books_[book->getIsbn()] = std::move(book);
    }

    Book* findByIsbn(const std::string& isbn) const {
        auto it = books_.find(isbn);
        return it != books_.end() ? it->second.get() : nullptr;
    }

    std::vector<Book*> searchByTitle(const std::string& keyword) const {
        std::vector<Book*> results;
        for (const auto& [isbn, book] : books_) {
            if (book->getTitle().find(keyword) != std::string::npos) {
                results.push_back(book.get());
            }
        }
        return results;
    }

    std::vector<Book*> searchByAuthor(const std::string& author) const {
        std::vector<Book*> results;
        for (const auto& [isbn, book] : books_) {
            if (book->getAuthor().find(author) != std::string::npos) {
                results.push_back(book.get());
            }
        }
        return results;
    }
};

// ─────────────────────────────────────────────
// Library — Singleton
// ─────────────────────────────────────────────

class Library {
    static Library*   instance_;
    static std::mutex mutex_;

    BookCatalog                           catalog_;
    NotificationService                   notifier_;
    std::vector<std::unique_ptr<Member>>      members_;
    std::vector<std::unique_ptr<Librarian>>   librarians_;
    std::string                           name_;

    Library() : name_("Super Schools Library") {}

public:
    Library(const Library&)            = delete;
    Library& operator=(const Library&) = delete;

    static Library* getInstance() {
        if (!instance_) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!instance_) {
                instance_ = new Library();
            }
        }
        return instance_;
    }

    // ── Admin operations ──────────────────────
    void addBook(std::unique_ptr<Book> book) {
        catalog_.addBook(std::move(book));
    }

    Member* registerMember(std::string name, std::string email,
                           std::string phone, std::string cardNum) {
        members_.push_back(std::make_unique<Member>(
            std::move(name), std::move(email), std::move(phone), std::move(cardNum)));
        return members_.back().get();
    }

    // ── Borrowing ─────────────────────────────
    BookLending* checkOut(const std::string& isbn, Member* member) {
        if (!member->canBorrow())
            throw std::runtime_error("Borrow limit reached");

        Book* book = catalog_.findByIsbn(isbn);
        if (!book) throw std::runtime_error("Book not found: " + isbn);

        BookItem* copy = book->findAvailableCopy();
        if (!copy) throw std::runtime_error("No available copy for: " + isbn);

        copy->setStatus(BookStatus::LOANED);
        return member->addLoan(copy);
    }

    // ── Returning ─────────────────────────────
    double returnBook(BookLending* lending) {
        double fineAmt = lending->markReturned();
        lending->getItem()->setStatus(BookStatus::AVAILABLE);

        if (fineAmt > 0.0) {
            lending->getMember()->recordFine(fineAmt);
            std::cout << "Overdue fine: $" << fineAmt
                      << " added to member " << lending->getMember()->getName() << "\n";
        }

        // Notify any waiting members via Observer
        Book* book = lending->getItem()->getBook();
        notifier_.notifyAvailable(book->getIsbn(), book->getTitle());

        return fineAmt;
    }

    // ── Reservation ───────────────────────────
    void reserveBook(const std::string& isbn, Member* member) {
        Book* book = catalog_.findByIsbn(isbn);
        if (!book) throw std::runtime_error("Book not found: " + isbn);

        // Register member as observer for this ISBN
        notifier_.subscribe(isbn, member);

        auto res = std::make_unique<BookReservation>(isbn, member);
        member->addReservation(std::move(res));
        std::cout << member->getName() << " reserved \"" << book->getTitle() << "\"\n";
    }

    // ── Search ────────────────────────────────
    Book* findByIsbn(const std::string& isbn) const {
        return catalog_.findByIsbn(isbn);
    }

    std::vector<Book*> searchByTitle(const std::string& kw) const {
        return catalog_.searchByTitle(kw);
    }
};

Library*    Library::instance_ = nullptr;
std::mutex  Library::mutex_;

// ─────────────────────────────────────────────
// main — usage demonstration
// ─────────────────────────────────────────────

int main() {
    Library* lib = Library::getInstance();

    // Add books via Factory
    auto novel = BookFactory::create(BookType::NOVEL, "978-0-06-112008-4",
                                     "To Kill a Mockingbird", "Harper Lee");
    auto copy1 = std::make_unique<BookItem>("ITEM-001", "A-12", novel.get());
    auto copy2 = std::make_unique<BookItem>("ITEM-002", "A-13", novel.get());
    novel->addCopy(std::move(copy1));
    novel->addCopy(std::move(copy2));
    lib->addBook(std::move(novel));

    // Register members
    Member* alice = lib->registerMember("Alice", "alice@example.com", "555-0001", "CARD-001");
    Member* bob   = lib->registerMember("Bob",   "bob@example.com",   "555-0002", "CARD-002");

    // Alice borrows copy 1
    BookLending* loan1 = lib->checkOut("978-0-06-112008-4", alice);
    std::cout << "Alice checked out a copy\n";

    // Alice borrows copy 2
    BookLending* loan2 = lib->checkOut("978-0-06-112008-4", alice);
    std::cout << "Alice checked out second copy\n";

    // Bob tries to borrow — no copies left, so reserves instead
    try {
        lib->checkOut("978-0-06-112008-4", bob);
    } catch (const std::exception& e) {
        std::cout << "Checkout failed: " << e.what() << "\n";
        lib->reserveBook("978-0-06-112008-4", bob);   // bob subscribes as Observer
    }

    // Alice returns first copy → Bob is notified via Observer
    std::cout << "\nAlice returns copy 1:\n";
    lib->returnBook(loan1);

    return 0;
}
```

**Expected output:**
```
Alice checked out a copy
Alice checked out second copy
Checkout failed: No available copy for: 978-0-06-112008-4
Bob reserved "To Kill a Mockingbird"

Alice returns copy 1:
[NOTIFY] Dear Bob: Book "To Kill a Mockingbird" (ISBN: 978-0-06-112008-4) is now available!
```

---

## Interview Q&A

**Q: Why is Library a Singleton?**
A: A library system has a single shared catalog, member registry, and notification hub. Instantiating multiple Library objects would create inconsistent state — two instances could each hand out the same book copy simultaneously. Singleton ensures all requests route through one authority.

**Q: How does the Observer pattern work here?**
A: `Member` implements `BookAvailabilityObserver`. When a member reserves a book, they are registered with `NotificationService` under that book's ISBN. When `returnBook()` is called, it calls `notifier_.notifyAvailable(isbn, title)`, which locates the first waiting observer and calls `onBookAvailable()` on them. This decouples the return flow from notification logic — the return code does not need to know about members or email/SMS delivery.

**Q: Why use a Factory for Book creation?**
A: Different book types (Novel, TextBook, Journal) have different fields and validation logic. The Factory centralizes creation so callers pass a `BookType` enum and data — they never deal with `new Novel(...)` directly. This makes adding a new book type a single change inside `BookFactory::create()` without touching any call site.

**Q: How would you prevent double-checkout of the same BookItem in a concurrent system?**
A: Wrap the `findAvailableCopy()` + `setStatus(LOANED)` block under a `std::mutex` (already using one in the Singleton). In a database-backed system, use optimistic locking (version column) or a `SELECT ... FOR UPDATE` row-level lock on the `book_items` row, then check status inside the transaction.

**Q: How do you model the "member can hold max 5 books" rule?**
A: `Member::canBorrow()` checks `activeLoanCount() < MAX_BOOKS` before checkout. The `checkOut()` method on Library calls this guard before assigning a copy. This keeps the business rule inside the domain object (Member), not scattered across service calls.

**Q: What happens when a member has an overdue fine — should they be blocked from borrowing?**
A: Add a `hasPendingFine()` check in `canBorrow()`: return false if `totalFine_ > 0.0`. The `checkOut()` method would then throw "Outstanding fine must be paid first." In a real system, a threshold (e.g., > $5.00) is more common than a zero-tolerance block.

**Q: How would you extend this for digital books (e-books)?**
A: Introduce an `EBook : public Book` via the Factory. `EBook` has no `BookItem` copies — it has a download URL and a concurrent-reader limit. Override `findAvailableCopy()` to always return a virtual `EBookItem` while the reader count is below the limit. No other class changes.

**Q: Difference between BookReservation and BookLending?**
A: `BookReservation` records intent — a member wants a book that is currently unavailable. It has a `ReservationStatus` state machine (WAITING → PENDING → COMPLETED). `BookLending` records an active loan of a specific physical `BookItem` with a due date and fine tracking. A reservation transitions to a lending when the member actually checks out the book.
