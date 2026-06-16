/*
 * Problem  : Inverted Pyramid
 * Difficulty: Easy
 * Pattern  : Star Patterns (nested loops)
 *
 * Problem Statement:
 *   Print a centered pyramid pointing downward.
 *
 * Example (n = 5):
 *   *********
 *    *******
 *     *****
 *      ***
 *       *
 *
 * Approach / Intuition:
 *   - Row i (counting down from n to 1) has (n - i) leading spaces
 *     and (2*i - 1) stars.
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
