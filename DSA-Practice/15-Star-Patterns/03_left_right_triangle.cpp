/*
 * Problem  : Left-Aligned Right Triangle
 * Difficulty: Easy
 * Pattern  : Star Patterns (nested loops)
 *
 * Problem Statement:
 *   Print a right triangle that grows row by row, aligned to the left.
 *
 * Example (n = 5):
 *   *
 *   * *
 *   * * *
 *   * * * *
 *   * * * * *
 *
 * Approach / Intuition:
 *   - Row i (1-indexed) contains i stars.
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
