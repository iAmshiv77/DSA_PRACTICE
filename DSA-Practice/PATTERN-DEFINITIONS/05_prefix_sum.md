# Pattern: Prefix Sum

## Definition
Precompute `prefix[i] = arr[0] + arr[1] + ... + arr[i]`. Any subarray sum `arr[l..r]` = `prefix[r] - prefix[l-1]` in O(1). When combined with a HashMap of prefix sums seen so far, it unlocks O(n) solutions for count-of-subarrays problems.

## Core Recurrence
```
prefix[0] = 0
prefix[i] = prefix[i-1] + arr[i-1]   (1-indexed, size n+1)
sum(l, r)  = prefix[r] - prefix[l-1]
```

## Core Templates

### Template 1 — Build Prefix Array
```cpp
vector<int> prefix(n + 1, 0);
for (int i = 0; i < n; i++)
    prefix[i + 1] = prefix[i] + arr[i];
// sum from l to r (0-indexed) = prefix[r+1] - prefix[l]
```

### Template 2 — Count Subarrays with Sum = K (HashMap trick)
```cpp
unordered_map<int,int> count;
count[0] = 1;   // empty prefix
int prefSum = 0, result = 0;
for (int x : arr) {
    prefSum += x;
    result += count[prefSum - k];   // how many prefixes = prefSum - k
    count[prefSum]++;
}
```

### Template 3 — Subarray Sums Divisible by K (modulo)
```cpp
unordered_map<int,int> remCount;
remCount[0] = 1;
int prefSum = 0, result = 0;
for (int x : arr) {
    prefSum = ((prefSum + x) % k + k) % k;  // handle negative mod
    result += remCount[prefSum];
    remCount[prefSum]++;
}
```

### Template 4 — Contiguous Array (equal 0s and 1s)
```cpp
// Replace 0 with -1, find longest subarray with sum 0
unordered_map<int,int> firstSeen;
firstSeen[0] = -1;
int prefSum = 0, maxLen = 0;
for (int i = 0; i < n; i++) {
    prefSum += (arr[i] == 0) ? -1 : 1;
    if (firstSeen.count(prefSum))
        maxLen = max(maxLen, i - firstSeen[prefSum]);
    else
        firstSeen[prefSum] = i;
}
```

## Complexity
- Time : O(n)
- Space: O(n) for the HashMap

## Classic Problems in This Set

| # | Problem | Key Insight |
|---|---------|------------|
| 01 | Subarray Sum Equals K | hashmap: count[prefSum - k] |
| 02 | Find Pivot Index | leftSum == totalSum - leftSum - arr[i] |
| 03 | Subarray Sums Divisible by K | store prefix mod k in map |
| 04 | Contiguous Array | map 0→-1, find longest zero-sum subarray |
| 05 | Shortest Subarray Sum ≥ K | monotonic deque on prefix sums |
| 06 | Count Range Sum | merge sort / BIT on prefix sums |

## Edge Cases (Interview Critical)
1. **Negative numbers** — prefix sum still works; be careful with modulo (add k before mod)
2. **k = 0** — subarrays that sum to 0; empty prefix counts as 0 (put 0 in map initially)
3. **Overflow** — use `long long` for large arrays with large values
4. **Pivot index** — check if leftSum == rightSum, not leftSum == totalSum/2 (integer division issue)
5. **Contiguous Array** — first occurrence must be stored, not overwritten

## Interview Questions on Edge Cases

**Q: Why initialize `count[0] = 1` in the subarray sum = k template?**
A: It accounts for subarrays starting from index 0. If prefix[i] == k, there's one subarray arr[0..i] — the "empty prefix" sum of 0 captures this.

**Q: How to handle negative modulo in C++?**
A: `((sum % k) + k) % k` — C++ can return negative remainders for negative numbers.

**Q: Why use a deque for "shortest subarray with sum ≥ k" (with negatives)?**
A: Sliding window breaks with negatives (adding negative can decrease sum, making shrinking unsafe). Monotonic increasing deque on prefix sums finds the longest valid gap efficiently.

**Q: Pivot index — why check `leftSum * 2 + arr[i] == totalSum` instead of computing rightSum?**
A: Avoids a second loop. leftSum * 2 + arr[i] == totalSum ⟺ leftSum == (totalSum - arr[i]) / 2 == rightSum.
