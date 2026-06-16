

#include <bits/stdc++.h>
using namespace std;

class Solution {
   public:
    void test(int c) {
        for (int i = 0; i < c; i++) {
            for (int j = 0; j < i; j++) {
                cout << "*" << " ";
            }
            cout << endl;
        }
    }
};

/* ────────── Test ────────── */
int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    Solution sol;
    sol.test(20);
    return 0;
}
