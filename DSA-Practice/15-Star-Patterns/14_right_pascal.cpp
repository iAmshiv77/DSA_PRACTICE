/*
 * Problem  : Right Pascal's Triangle (Arrow Pointing Left)
 * Difficulty: Easy
 * Pattern  : Star Patterns (nested loops)
 *
 * Problem Statement:
 *   Print a right-aligned arrow: a triangle that grows then shrinks.
 *
 * Example (n = 5):
 *           *
 *         * *
 *       * * *
 *     * * * *
 *   * * * * *
 *     * * * *
 *       * * *
 *         * *
 *           *
 *
 * Approach / Intuition:
 *   - Upper half: rows 1..n with (n - i) leading gaps then i stars.
 *   - Lower half: rows n-1..1 with the same formula.
 *
 * Time Complexity : O(n^2)
 * Space Complexity: O(1)
 */

#include <bits/stdc++.h>
using namespace std;

class Solution {
public:
    void printRow(int i, int n) {
        for (int j = 0; j < n - i; j++) cout << "  ";
        for (int j = 1; j <= i; j++) cout << "* ";
        cout << "\n";
    }

    void printPattern(int n) {
        for (int i = 1; i <= n; i++) printRow(i, n);
        for (int i = n - 1; i >= 1; i--) printRow(i, n);
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
