/*
 * Problem  : Butterfly Pattern
 * Difficulty: Easy
 * Pattern  : Star Patterns (nested loops)
 *
 * Problem Statement:
 *   Print a butterfly: two triangles of stars facing each other, separated
 *   by a shrinking-then-growing gap.
 *
 * Example (n = 4):
 *   *      *
 *   **    **
 *   ***  ***
 *   ********
 *   ********
 *   ***  ***
 *   **    **
 *   *      *
 *
 * Approach / Intuition:
 *   - Upper half (i = 1..n): i stars, 2*(n - i) spaces, i stars.
 *   - Lower half (i = n..1): same formula, mirrored.
 *
 * Time Complexity : O(n^2)
 * Space Complexity: O(1)
 */

#include <bits/stdc++.h>
using namespace std;

class Solution {
public:
    void printRow(int i, int n) {
        for (int j = 0; j < i; j++) cout << "*";
        for (int j = 0; j < 2 * (n - i); j++) cout << " ";
        for (int j = 0; j < i; j++) cout << "*";
        cout << "\n";
    }

    void printPattern(int n) {
        for (int i = 1; i <= n; i++) printRow(i, n);
        for (int i = n; i >= 1; i--) printRow(i, n);
    }
};

/* ────────── Test ────────── */
int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    Solution sol;
    sol.printPattern(4);
    return 0;
}
