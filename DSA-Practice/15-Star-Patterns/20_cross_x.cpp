/*
 * Problem  : Cross / X Pattern
 * Difficulty: Easy
 * Pattern  : Star Patterns (nested loops)
 *
 * Problem Statement:
 *   Print an "X" in an n x n grid (use an odd n so the diagonals cross).
 *
 * Example (n = 5):
 *   *       *
 *     *   *
 *       *
 *     *   *
 *   *       *
 *
 * Approach / Intuition:
 *   - Main diagonal:  i == j.
 *   - Anti-diagonal:  j == n - 1 - i.
 *   - Print a star on either diagonal, blank elsewhere.
 *
 * Time Complexity : O(n^2)
 * Space Complexity: O(1)
 */

#include <bits/stdc++.h>
using namespace std;

class Solution {
public:
    void printPattern(int n) {
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) {
                if (i == j || j == n - 1 - i) cout << "* ";
                else cout << "  ";
            }
            cout << "\n";
        }
    }
};

/* ────────── Test ────────── */
int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    Solution sol;
    sol.printPattern(5);
    return 0;
}
