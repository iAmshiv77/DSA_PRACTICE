# DSA Interview Practice — C++

## Structure

```
DSA-Practice/
├── 01-Two-Pointers/          12 problems
├── 02-Fast-Slow-Pointers/     8 problems
├── 03-Sliding-Window/        12 problems
├── 04-Kadane/                 6 problems
├── 05-Prefix-Sum/             6 problems
├── 06-Merge-Intervals/        7 problems
├── 07-Cyclic-Sort/            8 problems
├── 08-Reversal-LinkedList/    6 problems
├── 09-Stack/                  9 problems
├── 10-Hash-Maps/              4 problems
├── 11-Binary-Search/         20 problems
├── 12-Heap/                  16 problems
├── 13-Recursion-Backtracking/ 10 problems
├── 14-Trees/                 23 problems
└── 15-Star-Patterns/         20 problems
                         ─────────────
                        TOTAL: 167 problems
```

## How to Use Each File

Each `.cpp` file has this layout — fill it in as you solve:

```
/*
 * Problem  : name
 * Difficulty: Easy/Medium/Hard
 * Link     : leetcode/gfg url
 * Pattern  : pattern name
 *
 * Problem Statement:  ← read once
 * Examples:           ← trace by hand first
 * Approach:           ← write before coding
 * Time/Space:         ← analyse after solving
 */

class Solution { ... }
int main()        { ... }  ← add your own test cases
```

## Daily Practice Strategy

1. Pick **1 problem** per day from the current pattern
2. **Read** the problem, **trace examples** by hand (5 min)
3. **Write the approach** in comments before coding
4. **Code** the solution in the class
5. **Test** with edge cases in `main()`
6. **Review** time/space complexity

## Pattern Cheat Sheet

| Pattern | When to use |
|---------|-------------|
| Two Pointers | Sorted array, pair/triplet sum, partitioning |
| Fast & Slow | Cycle detection, middle of list |
| Sliding Window | Subarray/substring with constraint |
| Kadane | Max/min subarray sum/product |
| Prefix Sum | Range sum queries, count subarrays by sum |
| Merge Intervals | Overlapping ranges, scheduling |
| Cyclic Sort | Array with numbers 1..n, find missing/duplicate |
| Reversal LinkedList | Reverse in-place, k-group, rotate |
| Stack | Monotonic, matching brackets, next greater |
| Hash Maps | Frequency count, lookup in O(1) |
| Binary Search | Sorted array, search space reduction, minimize/maximize |
| Heap (K problems) | Top-K, K closest, streaming median |
| Backtracking | All permutations/combinations/subsets |
| Trees | DFS/BFS, path sum, LCA, BST properties |
| Star Patterns | Nested-loop drills: rows × columns, indent + shape |

## Compile & Run

```bash
g++ -std=c++17 -O2 -o sol filename.cpp && ./sol
```
