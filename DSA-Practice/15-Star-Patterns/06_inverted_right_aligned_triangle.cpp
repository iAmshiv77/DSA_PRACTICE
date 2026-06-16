/*
 * Problem  : Inverted Right-Aligned Triangle
 * Difficulty: Easy
 * Pattern  : Star Patterns (nested loops)
 *
 * Problem Statement:
 *   Print a right-aligned triangle that shrinks row by row.
 *
 * Example (n = 5):
 *   * * * * *
 *     * * * *
 *       * * *
 *         * *
 *           *
 *
 * Approach / Intuition:
 *   - Row i (1-indexed) has (i - 1) leading gaps then (n - i + 1) stars.
 *
 * Time Complexity : O(n^2)
 * Space Complexity: O(1)
 */

#include <bits/stdc++.h>
using namespace std;

class Solution {
public:
    void printPattern(int n) {
        for (int i = 1; i <= n; i++) {
            for (int j = 0; j < i - 1; j++) cout << "  ";
            for (int j = i; j <= n; j++) cout << "* ";
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
