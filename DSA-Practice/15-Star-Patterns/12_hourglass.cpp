/*
 * Problem  : Hourglass (Sandglass)
 * Difficulty: Easy
 * Pattern  : Star Patterns (nested loops)
 *
 * Problem Statement:
 *   Print an inverted pyramid stacked on top of an upright pyramid.
 *
 * Example (n = 5):
 *   *********
 *    *******
 *     *****
 *      ***
 *       *
 *      ***
 *     *****
 *    *******
 *   *********
 *
 * Approach / Intuition:
 *   - Top half: inverted pyramid, rows n..1.
 *   - Bottom half: upright pyramid, rows 2..n (skip 1 to avoid repeating
 *     the single-star middle row).
 *
 * Time Complexity : O(n^2)
 * Space Complexity: O(1)
 */

#include <bits/stdc++.h>
using namespace std;

class Solution {
public:
    void printPattern(int n) {
        for (int i = n; i >= 1; i--) {
            for (int j = 0; j < n - i; j++) cout << " ";
            for (int j = 0; j < 2 * i - 1; j++) cout << "*";
            cout << "\n";
        }
        for (int i = 2; i <= n; i++) {
            for (int j = 0; j < n - i; j++) cout << " ";
            for (int j = 0; j < 2 * i - 1; j++) cout << "*";
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
