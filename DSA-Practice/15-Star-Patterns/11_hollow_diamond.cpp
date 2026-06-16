/*
 * Problem  : Hollow Diamond
 * Difficulty: Easy
 * Pattern  : Star Patterns (nested loops)
 *
 * Problem Statement:
 *   Print a diamond outline (stars only on the edges).
 *
 * Example (n = 5):
 *       *
 *      * *
 *     *   *
 *    *     *
 *   *       *
 *    *     *
 *     *   *
 *      * *
 *       *
 *
 * Approach / Intuition:
 *   - Same skeleton as a diamond, but inside the (2*i - 1) star slots
 *     print a star only on the first and last slot.
 *
 * Time Complexity : O(n^2)
 * Space Complexity: O(1)
 */

#include <bits/stdc++.h>
using namespace std;

class Solution {
public:
    void printRow(int i, int n) {
        for (int j = 0; j < n - i; j++) cout << " ";
        int width = 2 * i - 1;
        for (int j = 1; j <= width; j++) {
            if (j == 1 || j == width) cout << "*";
            else cout << " ";
        }
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
