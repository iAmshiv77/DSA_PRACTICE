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
        for (int i = 1; i <= n; i++) {
            for (int j = 0; j < n - i; j++) cout << " ";
            int width = 2 * i - 1;
            for (int j = 1; j <= width; j++) {
                if (j == 1 || j == width || i == n) cout << "*";
                else cout << " ";
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
