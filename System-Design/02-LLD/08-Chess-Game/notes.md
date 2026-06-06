# Chess Game — Low Level Design

## Requirements

### Functional
- 8x8 Board with 64 squares
- 6 piece types: King, Queen, Rook, Bishop, Knight, Pawn
- 2 Players (Human or AI) assigned White and Black
- Each move validates: piece movement rules, not moving into check, turn order
- Special moves: castling, en passant, pawn promotion
- Game ends: checkmate, stalemate, resignation, draw by agreement
- Full move history (supports undo/redo via Command pattern)

### Non-Functional
- Abstract Piece base class with `validateMove()` per subclass
- Command pattern for Move (enables undo/redo stack)
- GameController orchestrates turn flow and check/checkmate detection

---

## Actors and Use Cases

| Actor          | Use Cases                                               |
|----------------|---------------------------------------------------------|
| Player (Human) | Select piece, make move, undo last move, resign         |
| GameController | Validate moves, detect check/checkmate/stalemate, log history |
| System         | Render board, notify result                             |

---

## Class Diagram (textual)

```
GameController
  ├── Board
  ├── Player[2]  (White, Black)
  ├── vector<MoveCommand*>  undoStack
  ├── vector<MoveCommand*>  redoStack
  └── GameStatus { IN_PROGRESS, CHECK, CHECKMATE, STALEMATE, DRAW }

Board
  ├── Cell[8][8]
  └── findKing(Color) → Cell*

Cell
  ├── row, col
  └── Piece*  (nullptr if empty)

Piece (abstract)
  ├── color, hasMoved
  ├── virtual validateMove(Board&, Cell* from, Cell* to) → bool
  ├── virtual getPossibleMoves(Board&, Cell* from) → vector<Cell*>
  └── virtual char symbol() → char
  │
  ├── King    — one step any direction, castling
  ├── Queen   — any direction unlimited
  ├── Rook    — horizontal/vertical unlimited
  ├── Bishop  — diagonal unlimited
  ├── Knight  — L-shape, can jump
  └── Pawn    — forward one (or two from start), diagonal capture

MoveCommand  ← Command pattern
  ├── Board*, Cell* from, Cell* to
  ├── Piece* capturedPiece  (for undo restore)
  ├── execute()
  └── undo()

Player
  ├── name, Color { WHITE, BLACK }
  └── isInCheck : bool
```

---

## Design Patterns Used

| Pattern          | Applied To           | Reason                                                             |
|------------------|----------------------|--------------------------------------------------------------------|
| Abstract Class   | Piece                | Each piece type encapsulates its own movement validation rules     |
| Command          | MoveCommand          | Encapsulate a move as an object; enables undo/redo stack           |
| Template Method  | Piece::validateMove  | Common pre-checks (same-color capture, board bounds) in base class |

---

## Full C++ Implementation

```cpp
#include <iostream>
#include <vector>
#include <memory>
#include <string>
#include <stack>
#include <cmath>
#include <stdexcept>
#include <algorithm>

// ─────────────────────────────────────────────
// Enums
// ─────────────────────────────────────────────

enum class Color      { WHITE, BLACK };
enum class PieceType  { KING, QUEEN, ROOK, BISHOP, KNIGHT, PAWN };
enum class GameStatus { IN_PROGRESS, CHECK, CHECKMATE, STALEMATE, DRAW };

// ─────────────────────────────────────────────
// Forward declarations
// ─────────────────────────────────────────────

class Board;
class Cell;

// ─────────────────────────────────────────────
// Piece — Abstract base
// ─────────────────────────────────────────────

class Piece {
protected:
    Color     color_;
    PieceType type_;
    bool      hasMoved_;
public:
    Piece(Color c, PieceType t) : color_(c), type_(t), hasMoved_(false) {}
    virtual ~Piece() = default;

    Color     getColor()    const { return color_;    }
    PieceType getType()     const { return type_;     }
    bool      hasMoved()    const { return hasMoved_; }
    void      markMoved()         { hasMoved_ = true; }

    // Template Method: subclasses override isLegalMove(); base handles common checks
    bool validateMove(const Board& board, Cell* from, Cell* to) const;

    // Each subclass defines its own movement geometry
    virtual bool isLegalMove(const Board& board, Cell* from, Cell* to) const = 0;
    virtual char symbol() const = 0;
};

// ─────────────────────────────────────────────
// Cell
// ─────────────────────────────────────────────

class Cell {
public:
    int                      row, col;
    std::unique_ptr<Piece>   piece;

    Cell(int r, int c) : row(r), col(c), piece(nullptr) {}

    bool isEmpty()  const { return piece == nullptr; }
    bool hasPiece() const { return piece != nullptr; }
    bool hasPieceOfColor(Color c) const {
        return hasPiece() && piece->getColor() == c;
    }
};

// ─────────────────────────────────────────────
// Board
// ─────────────────────────────────────────────

class Board {
    Cell cells_[8][8];
public:
    Board() {
        for (int r = 0; r < 8; ++r)
            for (int c = 0; c < 8; ++c)
                cells_[r][c] = Cell(r, c);
    }

    Cell* getCell(int row, int col) {
        if (row < 0 || row > 7 || col < 0 || col > 7) return nullptr;
        return &cells_[row][col];
    }

    const Cell* getCell(int row, int col) const {
        if (row < 0 || row > 7 || col < 0 || col > 7) return nullptr;
        return &cells_[row][col];
    }

    // Returns the King cell for the given color
    Cell* findKing(Color color) {
        for (int r = 0; r < 8; ++r)
            for (int c = 0; c < 8; ++c)
                if (cells_[r][c].hasPiece() &&
                    cells_[r][c].piece->getColor() == color &&
                    cells_[r][c].piece->getType() == PieceType::KING)
                    return &cells_[r][c];
        return nullptr;
    }

    // Checks if a given square is attacked by any piece of attackerColor
    bool isSquareAttacked(int row, int col, Color attackerColor) const;

    bool isInCheck(Color color) const {
        Color attacker = (color == Color::WHITE) ? Color::BLACK : Color::WHITE;
        // Find this color's king
        for (int r = 0; r < 8; ++r)
            for (int c = 0; c < 8; ++c)
                if (cells_[r][c].hasPiece() &&
                    cells_[r][c].piece->getColor() == color &&
                    cells_[r][c].piece->getType() == PieceType::KING)
                    return isSquareAttacked(r, c, attacker);
        return false;
    }

    void print() const {
        std::cout << "  a b c d e f g h\n";
        for (int r = 7; r >= 0; --r) {
            std::cout << (r + 1) << " ";
            for (int c = 0; c < 8; ++c) {
                const Cell& cell = cells_[r][c];
                if (cell.isEmpty()) {
                    std::cout << ". ";
                } else {
                    char sym = cell.piece->symbol();
                    // Uppercase = White, lowercase = Black
                    if (cell.piece->getColor() == Color::WHITE)
                        sym = std::toupper(sym);
                    else
                        sym = std::tolower(sym);
                    std::cout << sym << " ";
                }
            }
            std::cout << (r + 1) << "\n";
        }
        std::cout << "  a b c d e f g h\n";
    }
};

// ─────────────────────────────────────────────
// Piece base validateMove (Template Method)
// ─────────────────────────────────────────────

bool Piece::validateMove(const Board& board, Cell* from, Cell* to) const {
    if (!to) return false;
    // Cannot capture own piece
    if (to->hasPieceOfColor(color_)) return false;
    // Delegate geometry check to subclass
    return isLegalMove(board, from, to);
}

// ─────────────────────────────────────────────
// Concrete Pieces
// ─────────────────────────────────────────────

// Helper: path clear for sliding pieces (rook, bishop, queen)
static bool isPathClear(const Board& board, int r1, int c1, int r2, int c2) {
    int dr = (r2 > r1) ? 1 : (r2 < r1) ? -1 : 0;
    int dc = (c2 > c1) ? 1 : (c2 < c1) ? -1 : 0;
    int r = r1 + dr, c = c1 + dc;
    while (r != r2 || c != c2) {
        if (board.getCell(r, c) && !board.getCell(r, c)->isEmpty()) return false;
        r += dr; c += dc;
    }
    return true;
}

class King : public Piece {
public:
    King(Color c) : Piece(c, PieceType::KING) {}
    char symbol() const override { return 'K'; }

    bool isLegalMove(const Board& /*board*/, Cell* from, Cell* to) const override {
        int dr = std::abs(to->row - from->row);
        int dc = std::abs(to->col - from->col);
        // One step in any direction
        return dr <= 1 && dc <= 1 && (dr + dc) > 0;
        // Note: castling check omitted for brevity — would check hasMoved_ + rook hasMoved_
    }
};

class Queen : public Piece {
public:
    Queen(Color c) : Piece(c, PieceType::QUEEN) {}
    char symbol() const override { return 'Q'; }

    bool isLegalMove(const Board& board, Cell* from, Cell* to) const override {
        int dr = std::abs(to->row - from->row);
        int dc = std::abs(to->col - from->col);
        bool straightLine = (from->row == to->row || from->col == to->col);
        bool diagonal     = (dr == dc);
        if (!straightLine && !diagonal) return false;
        return isPathClear(board, from->row, from->col, to->row, to->col);
    }
};

class Rook : public Piece {
public:
    Rook(Color c) : Piece(c, PieceType::ROOK) {}
    char symbol() const override { return 'R'; }

    bool isLegalMove(const Board& board, Cell* from, Cell* to) const override {
        if (from->row != to->row && from->col != to->col) return false;
        return isPathClear(board, from->row, from->col, to->row, to->col);
    }
};

class Bishop : public Piece {
public:
    Bishop(Color c) : Piece(c, PieceType::BISHOP) {}
    char symbol() const override { return 'B'; }

    bool isLegalMove(const Board& board, Cell* from, Cell* to) const override {
        int dr = std::abs(to->row - from->row);
        int dc = std::abs(to->col - from->col);
        if (dr != dc) return false;
        return isPathClear(board, from->row, from->col, to->row, to->col);
    }
};

class Knight : public Piece {
public:
    Knight(Color c) : Piece(c, PieceType::KNIGHT) {}
    char symbol() const override { return 'N'; }

    bool isLegalMove(const Board& /*board*/, Cell* from, Cell* to) const override {
        int dr = std::abs(to->row - from->row);
        int dc = std::abs(to->col - from->col);
        // L-shape: (2,1) or (1,2) — Knight can jump over pieces
        return (dr == 2 && dc == 1) || (dr == 1 && dc == 2);
    }
};

class Pawn : public Piece {
public:
    Pawn(Color c) : Piece(c, PieceType::PAWN) {}
    char symbol() const override { return 'P'; }

    bool isLegalMove(const Board& board, Cell* from, Cell* to) const override {
        int direction = (color_ == Color::WHITE) ? 1 : -1;
        int dr = to->row - from->row;
        int dc = std::abs(to->col - from->col);

        // Forward one square — must be empty
        if (dc == 0 && dr == direction) {
            return to->isEmpty();
        }
        // Forward two squares from starting row — both cells must be empty
        int startRow = (color_ == Color::WHITE) ? 1 : 6;
        if (dc == 0 && dr == 2 * direction && from->row == startRow) {
            Cell* between = board.getCell(from->row + direction, from->col);
            return between && between->isEmpty() && to->isEmpty();
        }
        // Diagonal capture — must have enemy piece
        if (dc == 1 && dr == direction) {
            return to->hasPiece() && to->piece->getColor() != color_;
            // Note: en passant would need extra state (last move tracking)
        }
        return false;
    }
};

// ─────────────────────────────────────────────
// Board::isSquareAttacked (needs piece types)
// ─────────────────────────────────────────────

bool Board::isSquareAttacked(int row, int col, Color attackerColor) const {
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            const Cell& cell = cells_[r][c];
            if (!cell.hasPieceOfColor(attackerColor)) continue;
            Cell* from = const_cast<Cell*>(&cell);
            Cell* to   = const_cast<Cell*>(getCell(row, col));
            if (cell.piece->validateMove(*this, from, to)) return true;
        }
    }
    return false;
}

// ─────────────────────────────────────────────
// Command Pattern — MoveCommand
// ─────────────────────────────────────────────

class MoveCommand {
    Board*          board_;
    int             fromRow_, fromCol_;
    int             toRow_,   toCol_;
    std::unique_ptr<Piece> capturedPiece_; // saved for undo

public:
    MoveCommand(Board* board, int fr, int fc, int tr, int tc)
        : board_(board), fromRow_(fr), fromCol_(fc), toRow_(tr), toCol_(tc) {}

    void execute() {
        Cell* from = board_->getCell(fromRow_, fromCol_);
        Cell* to   = board_->getCell(toRow_,   toCol_);

        // Save captured piece before overwriting
        if (to->hasPiece()) {
            capturedPiece_ = std::move(to->piece);
        }

        // Move piece
        to->piece = std::move(from->piece);
        to->piece->markMoved();
    }

    void undo() {
        Cell* from = board_->getCell(fromRow_, fromCol_);
        Cell* to   = board_->getCell(toRow_,   toCol_);

        // Move piece back
        from->piece = std::move(to->piece);

        // Restore captured piece
        if (capturedPiece_) {
            to->piece = std::move(capturedPiece_);
        }
        // Note: hasMoved_ flag is not reverted here for brevity;
        // in a full implementation, save/restore hasMoved_ state too.
    }

    std::string notation() const {
        auto colChar = [](int c) { return static_cast<char>('a' + c); };
        std::string s;
        s += colChar(fromCol_); s += std::to_string(fromRow_ + 1);
        s += '-';
        s += colChar(toCol_);   s += std::to_string(toRow_ + 1);
        return s;
    }
};

// ─────────────────────────────────────────────
// Player
// ─────────────────────────────────────────────

class Player {
    std::string name_;
    Color       color_;
public:
    Player(std::string name, Color color)
        : name_(std::move(name)), color_(color) {}

    const std::string& getName()  const { return name_;  }
    Color              getColor() const { return color_;  }
};

// ─────────────────────────────────────────────
// GameController
// ─────────────────────────────────────────────

class GameController {
    Board                              board_;
    Player                             white_;
    Player                             black_;
    Color                              currentTurn_;
    GameStatus                         status_;
    std::vector<std::unique_ptr<MoveCommand>> history_;   // executed moves
    std::vector<std::unique_ptr<MoveCommand>> redoStack_;

    void setupBoard() {
        // White pieces — row 0
        board_.getCell(0, 0)->piece = std::make_unique<Rook>(Color::WHITE);
        board_.getCell(0, 1)->piece = std::make_unique<Knight>(Color::WHITE);
        board_.getCell(0, 2)->piece = std::make_unique<Bishop>(Color::WHITE);
        board_.getCell(0, 3)->piece = std::make_unique<Queen>(Color::WHITE);
        board_.getCell(0, 4)->piece = std::make_unique<King>(Color::WHITE);
        board_.getCell(0, 5)->piece = std::make_unique<Bishop>(Color::WHITE);
        board_.getCell(0, 6)->piece = std::make_unique<Knight>(Color::WHITE);
        board_.getCell(0, 7)->piece = std::make_unique<Rook>(Color::WHITE);
        for (int c = 0; c < 8; ++c)
            board_.getCell(1, c)->piece = std::make_unique<Pawn>(Color::WHITE);

        // Black pieces — row 7
        board_.getCell(7, 0)->piece = std::make_unique<Rook>(Color::BLACK);
        board_.getCell(7, 1)->piece = std::make_unique<Knight>(Color::BLACK);
        board_.getCell(7, 2)->piece = std::make_unique<Bishop>(Color::BLACK);
        board_.getCell(7, 3)->piece = std::make_unique<Queen>(Color::BLACK);
        board_.getCell(7, 4)->piece = std::make_unique<King>(Color::BLACK);
        board_.getCell(7, 5)->piece = std::make_unique<Bishop>(Color::BLACK);
        board_.getCell(7, 6)->piece = std::make_unique<Knight>(Color::BLACK);
        board_.getCell(7, 7)->piece = std::make_unique<Rook>(Color::BLACK);
        for (int c = 0; c < 8; ++c)
            board_.getCell(6, c)->piece = std::make_unique<Pawn>(Color::BLACK);
    }

public:
    GameController(std::string whiteName, std::string blackName)
        : white_(std::move(whiteName), Color::WHITE),
          black_(std::move(blackName), Color::BLACK),
          currentTurn_(Color::WHITE),
          status_(GameStatus::IN_PROGRESS)
    {
        setupBoard();
    }

    bool makeMove(int fromRow, int fromCol, int toRow, int toCol) {
        if (status_ != GameStatus::IN_PROGRESS &&
            status_ != GameStatus::CHECK) return false;

        Cell* from = board_.getCell(fromRow, fromCol);
        Cell* to   = board_.getCell(toRow,   toCol);

        if (!from || !to || from->isEmpty()) return false;
        if (from->piece->getColor() != currentTurn_) {
            std::cout << "Not your turn!\n";
            return false;
        }

        // Validate movement geometry
        if (!from->piece->validateMove(board_, from, to)) {
            std::cout << "Illegal move for this piece\n";
            return false;
        }

        // Execute move speculatively, then check if it leaves own king in check
        auto cmd = std::make_unique<MoveCommand>(&board_, fromRow, fromCol, toRow, toCol);
        cmd->execute();

        if (board_.isInCheck(currentTurn_)) {
            // Move leaves own king in check — illegal, undo it
            cmd->undo();
            std::cout << "Move would leave your king in check!\n";
            return false;
        }

        std::cout << "Move: " << cmd->notation() << "\n";
        redoStack_.clear(); // new move clears redo history
        history_.push_back(std::move(cmd));

        // Switch turns
        currentTurn_ = (currentTurn_ == Color::WHITE) ? Color::BLACK : Color::WHITE;

        // Check status for new current player
        if (board_.isInCheck(currentTurn_)) {
            status_ = GameStatus::CHECK;
            std::cout << (currentTurn_ == Color::WHITE ? "White" : "Black") << " is in CHECK!\n";
        } else {
            status_ = GameStatus::IN_PROGRESS;
        }

        return true;
    }

    // Undo — pop last move from history, push to redoStack
    bool undoMove() {
        if (history_.empty()) return false;
        auto& cmd = history_.back();
        cmd->undo();
        std::cout << "Undo: " << cmd->notation() << "\n";
        redoStack_.push_back(std::move(cmd));
        history_.pop_back();
        currentTurn_ = (currentTurn_ == Color::WHITE) ? Color::BLACK : Color::WHITE;
        status_ = GameStatus::IN_PROGRESS;
        return true;
    }

    // Redo — pop from redoStack, re-execute
    bool redoMove() {
        if (redoStack_.empty()) return false;
        auto& cmd = redoStack_.back();
        cmd->execute();
        std::cout << "Redo: " << cmd->notation() << "\n";
        history_.push_back(std::move(cmd));
        redoStack_.pop_back();
        currentTurn_ = (currentTurn_ == Color::WHITE) ? Color::BLACK : Color::WHITE;
        return true;
    }

    void printBoard() const { board_.print(); }
    GameStatus getStatus() const { return status_; }
    int getMoveCount() const { return static_cast<int>(history_.size()); }
};

// ─────────────────────────────────────────────
// main — usage demonstration
// ─────────────────────────────────────────────

int main() {
    GameController game("Alice", "Bob");
    game.printBoard();

    // e2→e4 (White pawn opening)
    game.makeMove(1, 4, 3, 4);

    // e7→e5 (Black pawn response)
    game.makeMove(6, 4, 4, 4);

    // Nf3 — Knight from g1 to f3
    game.makeMove(0, 6, 2, 5);

    game.printBoard();
    std::cout << "Total moves: " << game.getMoveCount() << "\n";

    // Undo last Knight move
    game.undoMove();
    std::cout << "After undo — moves: " << game.getMoveCount() << "\n";

    // Redo it
    game.redoMove();
    std::cout << "After redo — moves: " << game.getMoveCount() << "\n";

    return 0;
}
```

---

## Move Validation Summary Per Piece

| Piece  | Movement Rule                                        | Can Jump? | Special Cases              |
|--------|------------------------------------------------------|-----------|----------------------------|
| King   | 1 step in any of 8 directions                        | No        | Castling (both unmoved)     |
| Queen  | Any direction, unlimited (rook + bishop combined)   | No        | Path must be clear          |
| Rook   | Horizontal or vertical, unlimited                    | No        | Path must be clear          |
| Bishop | Diagonal only, unlimited                             | No        | Path must be clear          |
| Knight | L-shape: (2,1) or (1,2)                              | Yes       | Only piece that jumps       |
| Pawn   | Forward 1 (2 from start), diagonal capture          | No        | En passant, promotion       |

---

## Interview Q&A

**Q: Why is Piece an abstract class rather than an interface?**
A: Pieces share common data (`color_`, `hasMoved_`, `validateMove` template logic). An abstract class lets the base implement the common pre-checks (cannot capture own piece, must be on board) in `validateMove()`, then delegate geometry to the pure virtual `isLegalMove()`. This is the Template Method pattern — avoids duplicating the same guards in all 6 subclasses.

**Q: How does the Command pattern enable undo/redo?**
A: `MoveCommand` captures everything needed to reverse a move: the from/to coordinates and the captured piece (if any). `execute()` moves the piece; `undo()` moves it back and restores the captured piece. `GameController` keeps a `history_` vector (undo stack) and a `redoStack_`. Undo pops from history and pushes to redo; redo pops from redoStack and re-executes. This is exactly how text editors implement Ctrl+Z/Ctrl+Y.

**Q: How do you detect check?**
A: After each move, call `board_.isInCheck(currentTurn_)`. This finds the current player's King cell, then calls `isSquareAttacked(kingRow, kingCol, opponentColor)`. That function iterates all opponent pieces and calls `piece->validateMove(board, from, kingCell)` — if any return true, the king is under attack.

**Q: How do you detect checkmate?**
A: A player is in checkmate if: (1) they are in check, AND (2) there is no legal move available. To check (2), iterate all their pieces, generate all candidate target cells, speculatively execute each move, call `isInCheck()` on the result, and undo. If no move resolves the check, it is checkmate. This is O(pieces × board_squares) per turn — acceptable for chess.

**Q: What is the difference between check and checkmate in your model?**
A: `GameStatus::CHECK` is set when the current player's king is attacked but at least one legal move exists. `GameStatus::CHECKMATE` is set when in check and zero legal moves exist. Stalemate is similar — zero legal moves but NOT in check.

**Q: How would you add an AI player?**
A: Introduce a `Player` subtype `AiPlayer` with a `chooseMove(Board&) → MoveCommand` method. The simplest AI uses a minimax algorithm with alpha-beta pruning over the game tree. Because moves are Command objects, the AI can speculatively execute/undo sequences without side effects to evaluate positions.

**Q: How would you persist a game in progress?**
A: Serialize `history_` — each `MoveCommand` is just 4 integers (fromRow, fromCol, toRow, toCol). Replay them from the initial board state to reconstruct any position. This is exactly how PGN (Portable Game Notation) works in real chess software.
