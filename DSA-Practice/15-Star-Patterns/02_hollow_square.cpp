/*
 * Problem  : Hollow Square
 * Difficulty: Easy
 * Pattern  : Star Patterns (nested loops)
 *
 * Problem Statement:
 *   Print an n x n square with stars only on the border.
 *
 * Example (n = 5):
 *   * * * * *
 *   *       *
 *   *       *
 *   *       *
 *   * * * * *
 *
 * Approach / Intuition:
 *   - A cell is on the border when it is in the first/last row
 *     or the first/last column. Otherwise print blank.
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
                if (i == 0 || i == n - 1 || j == 0 || j == n - 1)
                    cout << "* ";
                else
                    cout << "  ";
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
