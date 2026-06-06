# Pattern: Fast & Slow Pointers (Floyd's Tortoise & Hare)

## Definition
Two pointers move through a sequence at different speeds (fast moves 2x or more). When a cycle exists, the fast pointer eventually laps the slow pointer and they meet.

## When to Use
- Detect a **cycle** in a linked list or array
- Find the **start** of a cycle
- Find the **middle** of a linked list
- Detect if a sequence eventually reaches a fixed point (Happy Number)
- Find a **duplicate** in an array of 1..n (Floyd's cycle trick)

## Core Templates

### Template 1 — Cycle Detection
```cpp
ListNode* slow = head, *fast = head;
while (fast && fast->next) {
    slow = slow->next;
    fast = fast->next->next;
    if (slow == fast) return true;  // cycle exists
}
return false;
```

### Template 2 — Find Cycle Start
```cpp
// After detection (slow == fast):
slow = head;
while (slow != fast) {
    slow = slow->next;
    fast = fast->next;  // fast moves at speed 1 now
}
return slow;  // cycle start
```

### Template 3 — Middle of Linked List
```cpp
ListNode* slow = head, *fast = head;
while (fast && fast->next) {
    slow = slow->next;
    fast = fast->next->next;
}
return slow;  // middle node
```

### Template 4 — Happy Number (number cycle)
```cpp
auto nextNum = [](int n) {
    int sum = 0;
    while (n) { int d = n % 10; sum += d*d; n /= 10; }
    return sum;
};
int slow = n, fast = nextNum(n);
while (fast != 1 && slow != fast) {
    slow = nextNum(slow);
    fast = nextNum(nextNum(fast));
}
return fast == 1;
```

## Why It Works (Intuition)
If there's a cycle of length `c`, after `k` steps the fast pointer is `k mod c` ahead. Eventually they meet at some node inside the cycle. After resetting slow to head and moving both at speed 1, they meet again at the cycle start — proven by the math of modular arithmetic.

## Complexity
- Time : O(n)
- Space: O(1)

## Classic Problems in This Set

| # | Problem | Key Idea |
|---|---------|---------|
| 01 | LinkedList Cycle | basic detection |
| 02 | Start of Cycle | reset one pointer to head |
| 03 | Happy Number | treat digit-square sum as "next pointer" |
| 04 | Find Duplicate Number | treat value as pointer (Floyd's on array) |
| 05 | Middle of LinkedList | when fast reaches end, slow is at middle |
| 06 | Palindrome LinkedList | find mid, reverse second half, compare |
| 07 | Reorder List | find mid, reverse, merge |
| 08 | Circular Array Loop | careful: same direction only, length > 1 |

## Common Pitfalls
- Always check `fast != null && fast->next != null` before advancing
- Circular Array Loop: reset visited nodes to 0 to avoid re-processing
