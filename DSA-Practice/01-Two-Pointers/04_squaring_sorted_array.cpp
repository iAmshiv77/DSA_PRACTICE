
#include <bits/stdc++.h>
using namespace std;

class Solution {
   public:
    vector<int> square(vector<int>& nums) {
        int l = 0;

        while (l < nums.size()) {
            nums[l] = nums[l] * nums[l];
            l++;
        }
        return nums;
    }
};
int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    Solution sol;
    vector<int> num = {1, 3, 5, 6, 7, 8, 9};
    auto result = sol.square(num);
    for (int x : result) {
        cout << x << " ";
    };
};