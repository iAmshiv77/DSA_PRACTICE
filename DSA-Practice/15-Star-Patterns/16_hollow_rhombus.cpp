/*
 * Problem  : Hollow Rhombus
 * Difficulty: Easy
 * Pattern  : Star Patterns (nested loops)
 *
 * Problem Statement:
 *   Print a slanted parallelogram with stars only on the border.
 *
 * Example (n = 5):
 *       * * * * *
 *      *       *
 *     *       *
 *    *       *
 *   * * * * *
 *
 * Approach / Intuition:
 *   - Same indent as a solid rhombus.
 *   - Print a star only on the first/last row or the first/last column.
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
