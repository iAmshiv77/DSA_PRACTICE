# Pattern: Merge Intervals

## Definition
Sort intervals by start time. Then greedily merge overlapping ones by comparing the current interval's start with the last merged interval's end.

## When to Use
- Merging/inserting overlapping time ranges
- Finding free slots in schedules
- Minimum number of resources (meeting rooms, CPU cores)

## Core Template — Merge Overlapping
```cpp
sort(intervals.begin(), intervals.end());
vector<pair<int,int>> merged;
for (auto& [s, e] : intervals) {
    if (merged.empty() || merged.back().second < s)
        merged.push_back({s, e});
    else
        merged.back().second = max(merged.back().second, e);
}
```

## Template — Insert Interval
```cpp
vector<vector<int>> result;
int i = 0, n = intervals.size();
// add all intervals before new one
while (i < n && intervals[i][1] < newInterval[0])
    result.push_back(intervals[i++]);
// merge overlapping
while (i < n && intervals[i][0] <= newInterval[1]) {
    newInterval[0] = min(newInterval[0], intervals[i][0]);
    newInterval[1] = max(newInterval[1], intervals[i][1]);
    i++;
}
result.push_back(newInterval);
// add remaining
while (i < n) result.push_back(intervals[i++]);
```

## Template — Minimum Meeting Rooms (Min Heap)
```cpp
sort(meetings.begin(), meetings.end());
priority_queue<int, vector<int>, greater<int>> endTimes; // min-heap
for (auto& [s, e] : meetings) {
    if (!endTimes.empty() && endTimes.top() <= s)
        endTimes.pop();  // reuse room
    endTimes.push(e);
}
return endTimes.size();
```

## Complexity
- Sort  : O(n log n)
- Merge : O(n)
- Total : O(n log n)

## Classic Problems in This Set

| # | Problem | Approach |
|---|---------|---------|
| 01 | Merge Intervals | sort + greedy merge |
| 02 | Insert Interval | three-phase scan (before, overlap, after) |
| 03 | Interval Intersections | two pointer on two lists |
| 04 | Overlapping Intervals | sort + check adjacent |
| 05 | Minimum Meeting Rooms | min-heap of end times |
| 06 | Maximum CPU Load | max active intervals at any time (heap) |
| 07 | Employee Free Time | merge all, find gaps |

## Edge Cases (Interview Critical)
1. **Single interval** — no merging needed, return as-is
2. **Non-overlapping** — result equals input (after sort)
3. **Fully contained** `[1,10]` and `[2,5]` — inner absorbed by outer
4. **Touch but not overlap** `[1,3]` and `[3,5]` — depends on problem: usually merge if `start <= prevEnd` (not strictly less)
5. **Negative times** — sort still works
6. **Insert at boundaries** — insert before first / after last

## Interview Questions on Edge Cases

**Q: [1,3] and [3,5] — should they merge?**
A: Depends on definition. LeetCode Merge Intervals uses `intervals[i][0] <= merged.back()[1]` (≤), so [1,3],[3,5] → [1,5]. Always clarify with interviewer.

**Q: Minimum meeting rooms — why min-heap on end times?**
A: We want to know: is there any room that finished before the next meeting starts? The min-heap's top gives the earliest-ending room. If it ends ≤ new start, reuse it; else add a new room.

**Q: How to find maximum concurrent intervals (CPU load)?**
A: Same as meeting rooms, but instead of count of heap elements, track max value = sum of loads of active jobs using events (+load at start, -load at end) sorted by time.
