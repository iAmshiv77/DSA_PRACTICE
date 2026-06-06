# Pattern: Sliding Window

## Definition
Maintain a "window" (contiguous subarray/substring) defined by two pointers (`start` and `end`). Expand the window by moving `end` forward; shrink it by moving `start` forward when a constraint is violated. Achieves O(n) instead of O(n²).

## When to Use
- Longest/shortest **subarray or substring** satisfying a constraint
- Maximum/minimum sum of subarray of **fixed size k**
- Count subarrays/substrings matching a condition
- Anagram / permutation detection inside a string

## Fixed vs Variable Window

| Type | How to Identify | What changes |
|------|----------------|--------------|
| Fixed size k | "subarray of size k" | only `end` grows, `start = end - k + 1` |
| Variable (max) | "longest … with condition" | shrink `start` when violated |
| Variable (min) | "shortest … with condition" | shrink `start` while condition holds |

## Core Templates

### Template 1 — Fixed Window (size k)
```cpp
int windowSum = 0, maxSum = INT_MIN;
for (int i = 0; i < k; i++) windowSum += arr[i];
maxSum = windowSum;
for (int i = k; i < n; i++) {
    windowSum += arr[i] - arr[i - k];
    maxSum = max(maxSum, windowSum);
}
```

### Template 2 — Variable Window (find LONGEST)
```cpp
unordered_map<char,int> freq;
int start = 0, maxLen = 0;
for (int end = 0; end < n; end++) {
    freq[s[end]]++;
    while (/* constraint violated */) {
        freq[s[start]]--;
        if (freq[s[start]] == 0) freq.erase(s[start]);
        start++;
    }
    maxLen = max(maxLen, end - start + 1);
}
```

### Template 3 — Variable Window (find SHORTEST)
```cpp
int start = 0, minLen = INT_MAX, windowSum = 0;
for (int end = 0; end < n; end++) {
    windowSum += arr[end];
    while (windowSum >= target) {
        minLen = min(minLen, end - start + 1);
        windowSum -= arr[start++];
    }
}
```

### Template 4 — Count windows with Exactly K (= AtMost(K) - AtMost(K-1))
```cpp
// exact count trick
int countAtMost(vector<int>& A, int k) { ... }
return countAtMost(A, k) - countAtMost(A, k - 1);
```

## Complexity
- Time : O(n) — each element enters and leaves window at most once
- Space: O(k) for the frequency map (k = distinct chars/values)

## Classic Problems in This Set

| # | Problem | Window Type | What to Track |
|---|---------|------------|---------------|
| 01 | Max Sum Subarray of Size K | Fixed k | running sum |
| 02 | Min Size Subarray Sum | Variable min | running sum |
| 03 | Longest K Distinct Chars | Variable max | char frequency map |
| 04 | Fruits into Baskets | Variable max | type frequency map (k=2) |
| 05 | Longest No-Repeat Substring | Variable max | last seen index map |
| 06 | Longest Repeating After Replace | Variable max | max freq char in window |
| 07 | Max Consecutive Ones III | Variable max | count of zeros flipped |
| 08 | Minimum Window Substring | Variable min | char counts + `formed` counter |
| 09 | Permutation in String | Fixed len(s1) | char frequency comparison |
| 10 | Find All Anagrams | Fixed len(p) | char frequency comparison |
| 11 | Words Concatenation | Fixed total len | word frequency map |
| 12 | Min Size Subarray Sum (variant) | Variable min | running sum |

## Edge Cases to Watch (Interview Critical)
1. **Empty string/array** — return 0 or -1 immediately
2. **All elements satisfy condition** — answer is entire array
3. **No valid window** — return 0 / "" / INT_MAX as appropriate
4. **k > n** — for fixed window, not possible; return -1
5. **Negative numbers** — sliding window for sum ONLY works with non-negatives; use prefix sum + deque for negatives
6. **Unicode / multi-byte chars** — use unordered_map not array of 26
7. **Repeating characters in target** — track `formed` counter correctly

## Interview Questions on Edge Cases

**Q: Why doesn't sliding window work for "minimum subarray sum" when array has negative numbers?**
A: Adding a negative element can decrease the sum, so shrinking from start might miss a valid longer window that includes the negative. Need deque-based approach (monotonic deque on prefix sums).

**Q: How do you handle "at most k distinct" vs "exactly k distinct"?**
A: Exactly k = atMost(k) - atMost(k-1). This converts the hard "exact" problem into two easy "at most" problems.

**Q: What if the window constraint involves ordering (e.g., must contain all words in any order)?**
A: Use a word-frequency map and a `formed` counter that tracks how many words are satisfied.
