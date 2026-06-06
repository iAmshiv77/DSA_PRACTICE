# Pattern: Trees

## Definition
A tree is a connected acyclic graph. A **binary tree** has at most 2 children per node. A **BST** additionally satisfies: left subtree < node < right subtree for all nodes.

## Core Traversals

### DFS — Recursive
```cpp
void inorder(TreeNode* root) {
    if (!root) return;
    inorder(root->left);
    process(root->val);    // ← move this line for pre/post order
    inorder(root->right);
}
// Preorder:  process BEFORE recursive calls
// Postorder: process AFTER recursive calls
```

### BFS — Level Order
```cpp
queue<TreeNode*> q;
q.push(root);
while (!q.empty()) {
    int size = q.size();
    vector<int> level;
    for (int i = 0; i < size; i++) {
        auto node = q.front(); q.pop();
        level.push_back(node->val);
        if (node->left)  q.push(node->left);
        if (node->right) q.push(node->right);
    }
    result.push_back(level);
}
```

### DFS — Iterative (Inorder)
```cpp
stack<TreeNode*> st;
TreeNode* curr = root;
while (curr || !st.empty()) {
    while (curr) { st.push(curr); curr = curr->left; }
    curr = st.top(); st.pop();
    process(curr->val);
    curr = curr->right;
}
```

## Key Recursive Patterns

### Return Value Up the Tree (Post-order thinking)
```cpp
// Used for: height, diameter, path sum, balanced check
int dfs(TreeNode* node) {
    if (!node) return base_case;
    int left  = dfs(node->left);
    int right = dfs(node->right);
    // update global answer using left + right + node
    return something_to_parent;
}
```

### Pass Value Down the Tree (Pre-order thinking)
```cpp
// Used for: validate BST, path sum check
void dfs(TreeNode* node, long minVal, long maxVal) {
    if (!node) return;
    if (node->val <= minVal || node->val >= maxVal) { valid = false; return; }
    dfs(node->left,  minVal, node->val);
    dfs(node->right, node->val, maxVal);
}
```

## Classic Problems in This Set

| # | Problem | Technique |
|---|---------|----------|
| 01–03 | Inorder/Preorder/Postorder | Basic DFS |
| 04–05 | Level Order / Zigzag | BFS with queue |
| 06 | Invert Tree | Post-order swap |
| 07 | Symmetric Tree | Simultaneous DFS on left+right mirrors |
| 08 | Subtree of Another Tree | DFS + isSameTree |
| 09 | LCA Binary Tree | Post-order: if both sides found, current = LCA |
| 10 | Search BST | Compare val, go left or right |
| 11 | LCA BST | Use BST property: split point is LCA |
| 12 | Kth Smallest BST | Inorder k-th element |
| 13–14 | Min/Max Depth | BFS (min faster) / DFS |
| 15 | Balanced Tree | Post-order height + check |
| 16 | Diameter | Post-order: max(left + right) at each node |
| 17 | Validate BST | Pass min/max bounds top-down |
| 18–19 | Path Sum I & II | DFS with running sum |
| 20 | Max Path Sum | Post-order: allow negative bypass with max(0, sub) |
| 21 | Construct from Pre+Inorder | Inorder split + recursion |
| 22 | Sorted Array to BST | Binary midpoint as root |
| 23 | Serialize/Deserialize | Preorder DFS with null markers |

## Edge Cases (Interview Critical)
1. **Null root** — always handle `if (!root) return ...` first
2. **Single node** — leaf is both min and max depth, diameter = 0
3. **Skewed tree** (linked list shape) — recursive DFS may stackoverflow for n=10^5; use iterative
4. **BST with INT_MIN/INT_MAX** — use `long` for bounds in Validate BST
5. **LCA — one node is ancestor of other** — algorithm still works: first found node = LCA
6. **Max Path Sum with all negatives** — a single node can be the answer; don't force paths through root
7. **Diameter** — diameter doesn't have to go through root

## Interview Questions on Edge Cases

**Q: LCA — what if one of p or q doesn't exist in the tree?**
A: Standard LCA assumes both exist. If not guaranteed, first verify both exist (extra pass), or modify DFS to return a flag.

**Q: Why use `long` for BST validation bounds?**
A: `INT_MIN` and `INT_MAX` are valid node values. Passing them as bounds means a node with value `INT_MIN` would incorrectly fail `val > minBound` if minBound is also INT_MIN as int. Use LONG_MIN / LONG_MAX.

**Q: How to find diameter without a global variable?**
A: Return `{height, diameter}` pair from DFS. `diameter = max(childDiameter, leftHeight + rightHeight)`.

**Q: Serialize — why preorder and not inorder?**
A: Inorder traversal of a BST can be reconstructed, but general binary trees require knowing the root first (preorder) to correctly split children. Preorder + null markers fully encodes structure.
