/*
 * Problem  : Plus (+) Pattern
 * Difficulty: Easy
 * Pattern  : Star Patterns (nested loops)
 *
 * Problem Statement:
 *   Print a plus sign in an n x n grid (use an odd n so it is centered).
 *
 * Example (n = 5):
 *       *
 *       *
 *   * * * * *
 *       *
 *       *
 *
 * Approach / Intuition:
 *   - A cell is part of the plus when it is on the middle row OR the
 *     middle column (index n / 2); otherwise it is blank.
 *
 * Time Complexity : O(n^2)
 * Space Complexity: O(1)
 */

#include <bits/stdc++.h>
using namespace std;

class Solution {
public:
    void printPattern(int n) {
        int mid = n / 2;
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) {
                if (i == mid || j == mid) cout << "* ";
                else cout << "  ";
            }
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
