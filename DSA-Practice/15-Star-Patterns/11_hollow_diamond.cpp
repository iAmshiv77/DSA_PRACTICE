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
