/*
 * Problem  : Left Pascal's Triangle (Arrow Pointing Right)
 * Difficulty: Easy
 * Pattern  : Star Patterns (nested loops)
 *
 * Problem Statement:
 *   Print a left-aligned arrow: a triangle that grows then shrinks.
 *
 * Example (n = 5):
 *   *
 *   * *
 *   * * *
 *   * * * *
 *   * * * * *
 *   * * * *
 *   * * *
 *   * *
 *   *
 *
 * Approach / Intuition:
 *   - Upper half: rows 1..n with i stars.
 *   - Lower half: rows n-1..1 with i stars.
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
            for (int j = 1; j <= i; j++) cout << "* ";
            cout << "\n";
        }
        for (int i = n - 1; i >= 1; i--) {
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
