/*
 * Problem  : Inverted Left-Aligned Triangle
 * Difficulty: Easy
 * Pattern  : Star Patterns (nested loops)
 *
 * Problem Statement:
 *   Print a left-aligned triangle that shrinks row by row.
 *
 * Example (n = 5):
 *   * * * * *
 *   * * * *
 *   * * *
 *   * *
 *   *
 *
 * Approach / Intuition:
 *   - Row i (1-indexed from the top) contains (n - i + 1) stars.
 *
 * Time Complexity : O(n^2)
 * Space Complexity: O(1)
 */

#include <bits/stdc++.h>
using namespace std;

class Solution {
public:
    void printPattern(int n) {
        // TODO: implement this pattern
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
