# Pattern: Heap (Priority Queue)

## Definition
A heap is a complete binary tree satisfying the heap property. A **min-heap** always has the smallest element at the root; a **max-heap** the largest. In C++: `priority_queue<int>` = max-heap; `priority_queue<int, vector<int>, greater<int>>` = min-heap.

Operations: insert O(log n), extract-top O(log n), peek O(1).

## Sub-Patterns

### 1. Top-K Problems
Keep a min-heap of size K. For each element: push it, then pop if size > K. At the end the heap contains the K largest elements.
```cpp
priority_queue<int, vector<int>, greater<int>> minHeap; // size K
for (int x : arr) {
    minHeap.push(x);
    if ((int)minHeap.size() > k) minHeap.pop();
}
// minHeap now has k largest elements
```

### 2. K-Way Merge
Push the first element of each list into a min-heap with metadata `{value, list_index, element_index}`.
```cpp
using T = tuple<int,int,int>;
priority_queue<T, vector<T>, greater<T>> pq;
for (int i = 0; i < k; i++)
    if (!lists[i].empty())
        pq.push({lists[i][0], i, 0});
while (!pq.empty()) {
    auto [val, li, ei] = pq.top(); pq.pop();
    result.push_back(val);
    if (ei + 1 < (int)lists[li].size())
        pq.push({lists[li][ei+1], li, ei+1});
}
```

### 3. Greedy + Max-Heap
Pick the highest-value option available at each step.
```cpp
// e.g., Task Scheduler: always schedule most frequent remaining task
priority_queue<int> maxHeap;
for (auto [task, freq] : freqMap) maxHeap.push(freq);
```

### 4. Two Heaps (Running Median)
- Max-heap `low` stores the smaller half
- Min-heap `high` stores the larger half
- Invariant: `low.size() == high.size()` or `low.size() == high.size() + 1`
```cpp
priority_queue<int> low;                         // max-heap
priority_queue<int, vector<int>, greater<int>> high;  // min-heap

void addNum(int num) {
    low.push(num);
    high.push(low.top()); low.pop();
    if (low.size() < high.size()) {
        low.push(high.top()); high.pop();
    }
}
double findMedian() {
    return low.size() > high.size()
        ? low.top()
        : (low.top() + high.top()) / 2.0;
}
```

## Complexity

| Operation | Min/Max Heap |
|-----------|-------------|
| Push | O(log n) |
| Pop (top) | O(log n) |
| Peek (top) | O(1) |
| Build heap | O(n) |
| Top-K | O(n log K) |
| K-Way Merge | O(N log K) where N = total elements |

## Classic Problems in This Set

| # | Problem | Sub-Pattern |
|---|---------|------------|
| 01 | Kth Smallest | min-heap or max-heap size K |
| 02 | Kth Largest | min-heap size K |
| 03 | Top K Frequent Elements | min-heap size K on (freq, val) |
| 04 | Top K Frequent Words | min-heap with custom comparator |
| 05 | K Closest Points | min-heap on distance |
| 06 | Find K Closest Elements | sliding window or min-heap |
| 07 | K Weakest Rows | min-heap on (soldiers, index) |
| 08 | Merge K Sorted Arrays | K-way merge |
| 09 | Last Stone Weight | max-heap greedy |
| 10 | Task Scheduler | max-heap + cooldown simulation |
| 11 | Reorganize String | max-heap + alternating placement |
| 12 | Min Refueling Stops | max-heap greedy (take best reachable) |
| 13 | IPO | two heaps: available projects + locked by capital |
| 14 | Course Schedule III | max-heap on duration (greedy swap) |
| 15 | Find Median from Data Stream | Two Heaps |
| 16 | Sliding Window Median | Two ordered sets / Two heaps with lazy deletion |

## Edge Cases (Interview Critical)
1. **K > n** — return all elements sorted
2. **All elements equal** — heap still works; comparator must be strict `<` not `<=`
3. **Custom objects** — define comparator or use `pair<int,int>` with index as tiebreaker
4. **Sliding Window Median** — heap elements become stale; use lazy deletion with a `toRemove` map
5. **Top-K Frequent Words** — same frequency: alphabetical order → use pair `{freq, word}` with min-heap, negate freq or custom comparator
6. **Task Scheduler** — don't forget the idle cycles when no valid task exists

## Interview Questions on Edge Cases

**Q: Why use a min-heap of size K for "K largest" instead of a max-heap?**
A: Max-heap would need to store all n elements then pop k times: O(n + k log n). Min-heap of size K: O(n log K). For large n and small K, min-heap is far better.

**Q: How does lazy deletion work for Sliding Window Median?**
A: When an element leaves the window, mark it in a `toRemove` map. When it surfaces as heap top, skip it (pop without using). This avoids O(n) search-and-delete in heap.

**Q: IPO — why two heaps?**
A: One max-heap for available project profits (capital ≤ current). One min-heap for all projects sorted by capital. Move projects from capital-heap to profit-heap as capital grows, then greedily pick max profit.
