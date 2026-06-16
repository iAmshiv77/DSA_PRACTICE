/*
 * Problem  : Hollow Right Triangle
 * Difficulty: Easy
 * Pattern  : Star Patterns (nested loops)
 *
 * Problem Statement:
 *   Print a left-aligned right triangle outline.
 *
 * Example (n = 5):
 *   *
 *   * *
 *   *   *
 *   *     *
 *   * * * * *
 *
 * Approach / Intuition:
 *   - In row i, print a star at the first column, the last column (j == i),
 *     or anywhere on the final base row; otherwise print a blank.
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
            for (int j = 1; j <= i; j++) {
                if (j == 1 || j == i || i == n) cout << "* ";
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
