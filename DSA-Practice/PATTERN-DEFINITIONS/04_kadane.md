# Pattern: Kadane's Algorithm

## Definition
Track the **maximum subarray sum ending at the current index**. At each step decide: extend the previous subarray, or start fresh. Classic DP recurrence solved in O(n) time O(1) space.

## Core Recurrence
```
maxEndingHere[i] = max(arr[i], maxEndingHere[i-1] + arr[i])
answer = max over all i of maxEndingHere[i]
```

## Core Templates

### Template 1 — Maximum Subarray Sum
```cpp
int maxSum = arr[0], curSum = arr[0];
for (int i = 1; i < n; i++) {
    curSum = max(arr[i], curSum + arr[i]);
    maxSum = max(maxSum, curSum);
}
```

### Template 2 — With Start/End Indices
```cpp
int maxSum = arr[0], curSum = arr[0];
int start = 0, end = 0, tempStart = 0;
for (int i = 1; i < n; i++) {
    if (arr[i] > curSum + arr[i]) {
        curSum = arr[i];
        tempStart = i;
    } else {
        curSum += arr[i];
    }
    if (curSum > maxSum) {
        maxSum = curSum;
        start = tempStart;
        end = i;
    }
}
```

### Template 3 — Maximum Product Subarray
```cpp
int maxProd = arr[0], minProd = arr[0], result = arr[0];
for (int i = 1; i < n; i++) {
    if (arr[i] < 0) swap(maxProd, minProd);
    maxProd = max(arr[i], maxProd * arr[i]);
    minProd = min(arr[i], minProd * arr[i]);
    result = max(result, maxProd);
}
```

### Template 4 — Circular Subarray Max Sum
```cpp
// max(normal Kadane, total_sum - min_subarray_sum)
// Edge case: if all negative, answer is just normal Kadane
int totalSum = accumulate(arr, arr+n, 0);
int maxNormal = kadane(arr, false);   // standard max
int minSub    = kadane(arr, true);    // kadane for min
int circular  = totalSum - minSub;
return (circular == 0) ? maxNormal : max(maxNormal, circular);
```

## Complexity
- Time : O(n)
- Space: O(1)

## Classic Problems in This Set

| # | Problem | Variant |
|---|---------|---------|
| 01 | Maximum Subarray | basic Kadane |
| 02 | Minimum Subarray | negate or track minCur |
| 03 | Maximum Product Subarray | track both max & min (negatives flip sign) |
| 04 | Max Sum With One Deletion | DP: prefix max + suffix max arrays |
| 05 | Maximum Absolute Sum | max(maxSubarraySum, abs(minSubarraySum)) |
| 06 | Max Sum Circular Subarray | normal vs circular (total - min subarray) |

## Edge Cases (Interview Critical)
1. **All negative numbers** — answer is the least-negative element (single element)
2. **Single element** — works, returns that element
3. **Array of zeros** — answer is 0
4. **Circular with all negative** — circular = 0 is wrong; fall back to normal Kadane
5. **Product with zero** — zero resets the product; treat zero as a separator

## Interview Questions on Edge Cases

**Q: What if all numbers are negative?**
A: Kadane still works — `curSum = max(arr[i], curSum + arr[i])` will keep picking the least-negative single element.

**Q: How does max product differ from max sum?**
A: A large negative × negative = large positive. So track BOTH current max and current min product at every step.

**Q: Why does circular subarray = total - min subarray?**
A: A circular subarray is everything EXCEPT some contiguous middle piece. That middle piece should be the minimum subarray sum to maximize what remains.

**Q: What about "max subarray sum with at most one deletion"?**
A: Build prefix max ending at i and suffix max starting at i. Answer = max(prefix[i-1] + suffix[i+1]) for all i, plus normal Kadane without deletion.
