/*
 * Problem  : Solid Rhombus (Parallelogram)
 * Difficulty: Easy
 * Pattern  : Star Patterns (nested loops)
 *
 * Problem Statement:
 *   Print an n x n block of stars slanted to the right.
 *
 * Example (n = 5):
 *       * * * * *
 *      * * * * *
 *     * * * * *
 *    * * * * *
 *   * * * * *
 *
 * Approach / Intuition:
 *   - Row i has (n - i) leading spaces, then a full row of n stars.
 *   - The decreasing indent is what produces the slant.
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
