# LLD: Snake & Ladder

## Requirements
- N players, 10×10 board (100 cells)
- Snakes: head → tail (go back)
- Ladders: bottom → top (go forward)
- Dice: 1-6
- Win: first to reach exactly cell 100 (must not overshoot)

## Patterns Used
- **Singleton**: Game board (one board per game)
- **Strategy**: Dice rolling (fair die vs loaded die vs custom)
- **Observer**: Notify players of turn result, winner event
- **Command**: (optional) record moves for replay

## C++ Implementation

```cpp
#include <iostream>
#include <vector>
#include <unordered_map>
#include <memory>
#include <random>
#include <string>
#include <stdexcept>
using namespace std;

// ── Dice Strategy ─────────────────────────────────────────────────────────────
class DiceStrategy {
public:
    virtual int roll() = 0;
    virtual ~DiceStrategy() = default;
};

class FairDice : public DiceStrategy {
    mt19937 rng{ random_device{}() };
    uniform_int_distribution<int> dist{ 1, 6 };
public:
    int roll() override { return dist(rng); }
};

class LoadedDice : public DiceStrategy {
    int fixedValue;
public:
    explicit LoadedDice(int v) : fixedValue(v) {}
    int roll() override { return fixedValue; }
};

// ── Player ────────────────────────────────────────────────────────────────────
class Player {
public:
    string name;
    int position = 0;  // 0 = before board, 1-100 = on board

    explicit Player(const string& n) : name(n) {}
    bool hasWon() const { return position == 100; }
};

// ── Board (Singleton) ─────────────────────────────────────────────────────────
class Board {
    unordered_map<int, int> snakes;   // head → tail
    unordered_map<int, int> ladders;  // bottom → top
    int size;

    Board(int sz) : size(sz) {}
    static Board* instance;

public:
    static Board* getInstance(int sz = 100) {
        if (!instance) instance = new Board(sz);
        return instance;
    }

    void addSnake(int head, int tail) {
        if (head <= tail) throw invalid_argument("Snake head must be > tail");
        snakes[head] = tail;
    }

    void addLadder(int bottom, int top) {
        if (bottom >= top) throw invalid_argument("Ladder bottom must be < top");
        ladders[bottom] = top;
    }

    // Returns final position after snakes/ladders
    int getDestination(int pos) const {
        if (snakes.count(pos))  {
            cout << "  🐍 Snake! " << pos << " → " << snakes.at(pos) << "\n";
            return snakes.at(pos);
        }
        if (ladders.count(pos)) {
            cout << "  🪜 Ladder! " << pos << " → " << ladders.at(pos) << "\n";
            return ladders.at(pos);
        }
        return pos;
    }

    int getSize() const { return size; }
};
Board* Board::instance = nullptr;

// ── Game Event Observer ───────────────────────────────────────────────────────
class GameObserver {
public:
    virtual void onMove(const Player& player, int rolled, int from, int to) = 0;
    virtual void onWin(const Player& player) = 0;
    virtual ~GameObserver() = default;
};

class ConsoleLogger : public GameObserver {
public:
    void onMove(const Player& p, int rolled, int from, int to) override {
        cout << p.name << " rolled " << rolled
             << " | moved " << from << " → " << to << "\n";
    }
    void onWin(const Player& p) override {
        cout << "🎉 " << p.name << " WINS!\n";
    }
};

// ── Game ──────────────────────────────────────────────────────────────────────
class SnakeLadderGame {
    vector<Player> players;
    Board* board;
    unique_ptr<DiceStrategy> dice;
    vector<unique_ptr<GameObserver>> observers;
    int currentPlayerIdx = 0;
    bool gameOver = false;

public:
    SnakeLadderGame(vector<string> names, unique_ptr<DiceStrategy> d)
        : board(Board::getInstance()), dice(move(d)) {
        for (const auto& name : names) players.emplace_back(name);
    }

    void addObserver(unique_ptr<GameObserver> obs) {
        observers.push_back(move(obs));
    }

    void playTurn() {
        if (gameOver) return;

        Player& player = players[currentPlayerIdx];
        int rolled = dice->roll();
        int from = player.position;
        int newPos = from + rolled;

        if (newPos > board->getSize()) {
            cout << player.name << " rolled " << rolled
                 << " (overshoot — stays at " << from << ")\n";
        } else {
            int finalPos = board->getDestination(newPos);
            player.position = finalPos;

            for (auto& obs : observers)
                obs->onMove(player, rolled, from, finalPos);

            if (player.hasWon()) {
                for (auto& obs : observers) obs->onWin(player);
                gameOver = true;
                return;
            }
        }

        currentPlayerIdx = (currentPlayerIdx + 1) % players.size();
    }

    void playFullGame() {
        while (!gameOver) playTurn();
    }

    bool isOver() const { return gameOver; }
};

// ── Main ──────────────────────────────────────────────────────────────────────
int main() {
    Board* board = Board::getInstance(100);

    // Setup snakes (head → tail)
    board->addSnake(99, 54);
    board->addSnake(70, 35);
    board->addSnake(52, 29);
    board->addSnake(25, 8);

    // Setup ladders (bottom → top)
    board->addLadder(4, 56);
    board->addLadder(13, 76);
    board->addLadder(33, 68);
    board->addLadder(57, 92);

    SnakeLadderGame game({"Alice", "Bob", "Charlie"}, make_unique<FairDice>());
    game.addObserver(make_unique<ConsoleLogger>());
    game.playFullGame();

    return 0;
}
```

## Edge Cases
- **Overshoot**: Player at 98 rolls 4 → stays (100 is exact win only)
- **Same-cell snake/ladder**: not allowed (validation in addSnake/addLadder)
- **Chain**: snake head = ladder bottom (should be disallowed in real game)
- **Single player**: game still works
- **Dice = 6 rule**: some variants let you roll again on 6 — extend via boolean flag

## Interview Q&A

**Q: How would you add a "roll again on 6" rule?**
In `playTurn()`, after processing the move, check if `rolled == 6 && !gameOver`. If true, call `playTurn()` recursively (or loop) without advancing `currentPlayerIdx`. Add a max chain limit (e.g., 3 sixes) to prevent infinite loops.

**Q: How would you implement a network multiplayer version?**
Each player action (`roll`) becomes a REST/WebSocket event. Game state moves to a server. Use event sourcing — store all moves, reconstruct state by replaying. Board state stored in Redis (fast reads for turn validation). WebSocket broadcasts state changes to all connected clients.
