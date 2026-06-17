/*
 * Problem  : Pyramid (Centered Triangle)
 * Difficulty: Easy
 * Pattern  : Star Patterns (nested loops)
 *
 * Problem Statement:
 *   Print a centered, symmetric pyramid of stars.
 *
 * Example (n = 5):
 *       *
 *      ***
 *     *****
 *    *******
 *   *********
 *
 * Approach / Intuition:
 *   - Row i (1-indexed) has (n - i) leading spaces and (2*i - 1) stars.
 *   - Stars are printed without gaps so the pyramid stays solid.
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
