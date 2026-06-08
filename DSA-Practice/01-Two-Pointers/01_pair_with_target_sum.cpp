

#include <bits/stdc++.h>
using namespace std;

// Sorted Array
class Solution {
   public:
    vector<int> solve(vector<int>& nums, int target) {
        int l = 0;
        int r = nums.size() - 1;
        while (l < r) {
            int sum = nums[l] + nums[r];
            if (sum == target) {
                return {l, r};
            } else if (sum > target) {
                r--;
            } else {
                l++;
            }
        }
        return {l, r};
    }
};

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);
    vector<int> num = {1, 3, 4, 5, 8, 9};
    Solution sol;
    auto result = sol.solve(num, 11);
    for (int x : result) {
        cout << x << " ";
    };
}

// Unsorted Array