/*
 * Problem  : Hollow Pyramid
 * Difficulty: Easy
 * Pattern  : Star Patterns (nested loops)
 *
 * Problem Statement:
 *   Print a centered pyramid with stars only on the edges and base.
 *
 * Example (n = 5):
 *       *
 *      * *
 *     *   *
 *    *     *
 *   *********
 *
 * Approach / Intuition:
 *   - Within the (2*i - 1) star slots of row i, print a star only when it
 *     is the first slot, the last slot, or we are on the final (base) row.
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
