/*
 * Problem  : Right-Aligned Triangle (Mirrored)
 * Difficulty: Easy
 * Pattern  : Star Patterns (nested loops)
 *
 * Problem Statement:
 *   Print a right triangle aligned to the right using leading spaces.
 *
 * Example (n = 5):
 *           *
 *         * *
 *       * * *
 *     * * * *
 *   * * * * *
 *
 * Approach / Intuition:
 *   - Row i has (n - i) leading "gaps" then i stars.
 *   - Each star occupies 2 chars ("* "), so each gap is 2 spaces to align.
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
            for (int j = 0; j < n - i; j++) cout << "  ";
            for (int j = 1; j <= i; j++) cout << "* ";
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
