# Pattern: Binary Search

## Definition
Repeatedly halve the search space by comparing the midpoint with the target. Works on any **monotonic** structure — not just sorted arrays, but also "search spaces" where a condition transitions from false to true.

## When to Use
- Sorted array: find value, first/last occurrence, count
- Find an answer in a range where "can we achieve X?" is monotonic
- Matrix with sorted rows/columns
- Minimize-the-maximum / Maximize-the-minimum (binary search on answer)

## Core Templates

### Template 1 — Basic (find exact value)
```cpp
int lo = 0, hi = n - 1;
while (lo <= hi) {
    int mid = lo + (hi - lo) / 2;
    if (arr[mid] == target) return mid;
    else if (arr[mid] < target) lo = mid + 1;
    else hi = mid - 1;
}
return -1;
```

### Template 2 — Lower Bound (first position ≥ target)
```cpp
int lo = 0, hi = n;   // hi = n (one past end)
while (lo < hi) {
    int mid = lo + (hi - lo) / 2;
    if (arr[mid] < target) lo = mid + 1;
    else hi = mid;
}
return lo;  // first index where arr[lo] >= target
```

### Template 3 — Upper Bound (first position > target)
```cpp
int lo = 0, hi = n;
while (lo < hi) {
    int mid = lo + (hi - lo) / 2;
    if (arr[mid] <= target) lo = mid + 1;
    else hi = mid;
}
return lo;  // first index where arr[lo] > target
// count occurrences = upperBound(target) - lowerBound(target)
```

### Template 4 — Binary Search on Answer (Minimize/Maximize)
```cpp
// "Can we achieve ans = mid?" → monotonic function
int lo = minPossible, hi = maxPossible;
while (lo < hi) {
    int mid = lo + (hi - lo) / 2;
    if (canAchieve(mid)) hi = mid;   // try smaller (minimize)
    else lo = mid + 1;
}
return lo;
```

### Template 5 — Rotated Sorted Array
```cpp
int lo = 0, hi = n - 1;
while (lo <= hi) {
    int mid = lo + (hi - lo) / 2;
    if (arr[mid] == target) return mid;
    if (arr[lo] <= arr[mid]) {  // left half sorted
        if (arr[lo] <= target && target < arr[mid]) hi = mid - 1;
        else lo = mid + 1;
    } else {                    // right half sorted
        if (arr[mid] < target && target <= arr[hi]) lo = mid + 1;
        else hi = mid - 1;
    }
}
```

## Complexity
- Time : O(log n) per search, O(log n × f(n)) for "binary search on answer" where f(n) = cost of canAchieve
- Space: O(1)

## Classic Problems in This Set

| # | Problem | Template |
|---|---------|---------|
| 01 | Binary Search | Template 1 |
| 02 | Ceiling / Upper Bound | Template 3 |
| 03 | First & Last Position | Template 2 + 3 |
| 04 | Count Occurrences | upper - lower bound |
| 05 | Peak in Mountain | find where arr[mid] > arr[mid+1] |
| 06 | Min in Rotated | find pivot |
| 07 | Search in Rotated | Template 5 |
| 08 | Koko Eating Bananas | Template 4 (minimize max speed) |
| 09 | Min Days for Bouquets | Template 4 |
| 10 | Aggressive Cows | Template 4 (maximize min dist) |
| 11 | Book Allocation | Template 4 (minimize max pages) |
| 12 | Split Array Largest Sum | Template 4 |
| 13 | Search 2D Matrix | treat as 1D array |
| 14 | Search 2D Matrix II | start from top-right corner |
| 15 | Kth Smallest Sorted Matrix | BS on value range |
| 16 | Median of Two Sorted Arrays | BS on partition |

## Edge Cases (Interview Critical)
1. **Integer overflow** — use `mid = lo + (hi - lo) / 2`, NOT `(lo + hi) / 2`
2. **Infinite loop** — with `lo < hi` + `hi = mid` (not mid-1), always verify it terminates
3. **Empty array** — check `n == 0` before entering loop
4. **All elements equal** — lower/upper bound both work correctly
5. **Rotated with duplicates** — `arr[lo] == arr[mid]` is ambiguous; do `lo++` as fallback O(n) worst case
6. **Binary search on answer boundary** — off-by-one: `lo = mid+1` vs `lo = mid`, `hi = mid` vs `hi = mid-1`

## "Binary Search on Answer" Decision Framework

```
1. Can the answer be stated as a number in a range [lo, hi]?
2. Is "canAchieve(x)" monotonic? (once false, always false OR once true, always true)
3. If yes → binary search on x
```

Examples: minimize max load, maximize min gap, find k-th element.

## Interview Questions on Edge Cases

**Q: When does `lo = mid` cause infinite loop?**
A: When `lo + 1 == hi`, `mid = lo`, and condition sends `lo = mid = lo` → no progress. Fix: use `lo = mid + 1` on the "increase" branch, or use `lo < hi - 1` + handle remaining 2 elements separately.

**Q: How is "Search in 2D Matrix II" different from "Search in 2D Matrix"?**
A: Matrix I: rows + columns both sorted AND row i's last < row i+1's first → treat as 1D sorted array. Matrix II: only rows and columns sorted independently → start at top-right, move left if too big, down if too small. O(m+n).

**Q: Binary search for kth smallest in sorted matrix — what's the search space?**
A: Values [matrix[0][0], matrix[n-1][n-1]]. For a given mid value, count how many elements ≤ mid by walking each row with upper_bound. Answer is the smallest value where count ≥ k.
