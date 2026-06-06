# Pattern: Two Pointers

## Definition
Use two index variables (`left` and `right`) that move toward each other or in the same direction to solve array/string problems — replacing the O(n²) brute-force with O(n).

## When to Use
- Sorted array / sorted linked list
- Finding pairs, triplets, or quadruplets with a target sum
- Removing/partitioning elements in-place
- Comparing two sequences simultaneously
- Squeezing a window from both ends

## Core Templates

### Template 1 — Opposite Ends (converging)
```cpp
int left = 0, right = n - 1;
while (left < right) {
    int sum = arr[left] + arr[right];
    if (sum == target)      { /* found */ left++; right--; }
    else if (sum < target)  left++;
    else                    right--;
}
```

### Template 2 — Same Direction (fast/slow)
```cpp
int slow = 0;
for (int fast = 0; fast < n; fast++) {
    if (valid(arr[fast])) {
        arr[slow++] = arr[fast];
    }
}
// slow = new length
```

### Template 3 — Three Pointers (Dutch Flag / 3Sum)
```cpp
int lo = 0, mid = 0, hi = n - 1;
while (mid <= hi) {
    if (arr[mid] == 0)      swap(arr[lo++], arr[mid++]);
    else if (arr[mid] == 1) mid++;
    else                    swap(arr[mid], arr[hi--]);
}
```

## Complexity
- Time : O(n) — each pointer moves at most n steps
- Space: O(1) — no extra memory

## Classic Problems in This Set

| # | Problem | Key Idea |
|---|---------|---------|
| 01 | Pair with Target Sum | converging pointers on sorted array |
| 02 | Rearrange 0 and 1 | slow pointer marks insertion spot |
| 03 | Remove Duplicates | slow pointer = write head |
| 04 | Squares of Sorted Array | fill result from back, move largest |
| 05 | 3Sum | fix one, two-pointer on rest |
| 06 | 3Sum Closest | track min abs diff while searching |
| 07 | Triplets with Smaller Sum | count valid pairs for each fixed left |
| 08 | Subarray Product < K | sliding window variant |
| 09 | Dutch National Flag | 3-way partition |
| 10 | 4Sum | two nested loops + two pointers |
| 11 | Backspace String Compare | two pointers from end |
| 12 | Shortest Unsorted Subarray | find boundaries from both ends |

## Common Pitfalls
- Forgetting to skip duplicates in 3Sum/4Sum → use `while` to advance past equal values
- Off-by-one when `left < right` vs `left <= right`
- Not handling empty array or single element
