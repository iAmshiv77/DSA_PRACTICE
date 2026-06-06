# Database Queries — SQL + MongoDB

## SQL — Core Patterns

### JOINs
```sql
-- INNER JOIN: rows present in BOTH tables
SELECT u.name, o.total
FROM users u
INNER JOIN orders o ON u.id = o.user_id;

-- LEFT JOIN: ALL rows from left + matching from right (NULL if no match)
SELECT u.name, COUNT(o.id) AS order_count
FROM users u
LEFT JOIN orders o ON u.id = o.user_id
GROUP BY u.id, u.name;

-- SELF JOIN: join table to itself
-- Find employees and their managers
SELECT e.name AS employee, m.name AS manager
FROM employees e
LEFT JOIN employees m ON e.manager_id = m.id;

-- Multiple JOINs
SELECT u.name, o.id AS order_id, p.name AS product, oi.quantity
FROM users u
JOIN orders o ON u.id = o.user_id
JOIN order_items oi ON o.id = oi.order_id
JOIN products p ON oi.product_id = p.id
WHERE o.status = 'completed';

-- CROSS JOIN: cartesian product (rarely used)
-- Useful: generate all date-user combinations for reports
SELECT d.date, u.id AS user_id
FROM dates d CROSS JOIN users u;
```

### Aggregations
```sql
-- GROUP BY with HAVING (filter after aggregation)
SELECT
  category,
  COUNT(*) AS product_count,
  AVG(price) AS avg_price,
  SUM(stock) AS total_stock,
  MAX(price) AS max_price,
  MIN(price) AS min_price
FROM products
WHERE is_active = true          -- WHERE filters BEFORE grouping
GROUP BY category
HAVING COUNT(*) > 5             -- HAVING filters AFTER grouping
ORDER BY avg_price DESC;

-- DISTINCT COUNT
SELECT COUNT(DISTINCT user_id) AS unique_buyers FROM orders;

-- Conditional aggregation (CASE in aggregate)
SELECT
  category,
  COUNT(*) AS total,
  COUNT(CASE WHEN price > 100 THEN 1 END) AS premium_count,
  SUM(CASE WHEN status = 'sold' THEN 1 ELSE 0 END) AS sold,
  AVG(CASE WHEN status = 'available' THEN price END) AS avg_available_price
FROM products
GROUP BY category;
```

### Window Functions
```sql
-- ROW_NUMBER, RANK, DENSE_RANK
SELECT
  name,
  salary,
  department,
  ROW_NUMBER() OVER (PARTITION BY department ORDER BY salary DESC) AS row_num,
  RANK()       OVER (PARTITION BY department ORDER BY salary DESC) AS rank,
  DENSE_RANK() OVER (PARTITION BY department ORDER BY salary DESC) AS dense_rank
  -- RANK skips numbers after ties (1,1,3), DENSE_RANK doesn't (1,1,2)
FROM employees;

-- Top N per group (e.g., top 3 salaries per department)
SELECT * FROM (
  SELECT
    name, salary, department,
    RANK() OVER (PARTITION BY department ORDER BY salary DESC) AS rk
  FROM employees
) ranked
WHERE rk <= 3;

-- Running total (cumulative sum)
SELECT
  date,
  revenue,
  SUM(revenue) OVER (ORDER BY date) AS running_total,
  SUM(revenue) OVER (
    ORDER BY date
    ROWS BETWEEN 6 PRECEDING AND CURRENT ROW
  ) AS rolling_7_day
FROM daily_revenue;

-- LAG / LEAD (access previous/next row)
SELECT
  date,
  revenue,
  LAG(revenue, 1, 0) OVER (ORDER BY date) AS prev_revenue,
  revenue - LAG(revenue, 1, 0) OVER (ORDER BY date) AS day_over_day_change,
  ROUND(
    100.0 * (revenue - LAG(revenue) OVER (ORDER BY date)) /
    NULLIF(LAG(revenue) OVER (ORDER BY date), 0), 2
  ) AS pct_change
FROM daily_revenue;

-- NTILE: divide into buckets
SELECT name, salary,
  NTILE(4) OVER (ORDER BY salary) AS quartile  -- 1=bottom 25%, 4=top 25%
FROM employees;
```

### CTEs and Subqueries
```sql
-- CTE (Common Table Expression) — WITH clause
WITH monthly_revenue AS (
  SELECT
    DATE_TRUNC('month', created_at) AS month,
    SUM(amount) AS revenue
  FROM orders
  WHERE status = 'completed'
  GROUP BY 1
),
revenue_with_growth AS (
  SELECT
    month,
    revenue,
    LAG(revenue) OVER (ORDER BY month) AS prev_month,
    ROUND(100.0 * (revenue - LAG(revenue) OVER (ORDER BY month)) /
      NULLIF(LAG(revenue) OVER (ORDER BY month), 0), 2) AS growth_pct
  FROM monthly_revenue
)
SELECT * FROM revenue_with_growth
WHERE growth_pct > 10
ORDER BY month;

-- Recursive CTE (hierarchical data, org charts)
WITH RECURSIVE org_tree AS (
  -- Base case: top-level (no manager)
  SELECT id, name, manager_id, 1 AS level
  FROM employees
  WHERE manager_id IS NULL

  UNION ALL

  -- Recursive case: employees with managers
  SELECT e.id, e.name, e.manager_id, ot.level + 1
  FROM employees e
  JOIN org_tree ot ON e.manager_id = ot.id
)
SELECT level, name FROM org_tree ORDER BY level, name;

-- Subquery in WHERE
SELECT name FROM products
WHERE category_id IN (
  SELECT id FROM categories WHERE parent_id IS NULL  -- top-level categories only
);

-- Correlated subquery (runs once per outer row — can be slow!)
SELECT name, salary
FROM employees e
WHERE salary > (
  SELECT AVG(salary) FROM employees WHERE department = e.department
);
-- Better: use window function or JOIN with aggregated CTE
```

### Indexes & Performance
```sql
-- B-tree index (default): equality, range, ORDER BY, prefix search
CREATE INDEX idx_orders_user_id ON orders(user_id);
CREATE INDEX idx_orders_status_created ON orders(status, created_at DESC);
-- Composite index: leftmost prefix rule
-- (status, created_at) covers: WHERE status = ?  AND  WHERE status = ? AND created_at = ?
-- Does NOT cover: WHERE created_at = ?  (missing leftmost column)

-- Partial index: index subset of rows
CREATE INDEX idx_active_users ON users(email) WHERE is_active = true;

-- Unique index
CREATE UNIQUE INDEX idx_users_email ON users(email);

-- Check if index is used:
EXPLAIN ANALYZE SELECT * FROM orders WHERE user_id = 5;
-- Look for: "Index Scan" vs "Seq Scan"
-- Seq Scan on large table = missing index

-- Covering index: all needed columns in index (no table heap access)
CREATE INDEX idx_orders_covering ON orders(user_id, status, total, created_at);
-- SELECT user_id, status, total FROM orders WHERE user_id = 5
-- Can be satisfied entirely from index (faster)

-- When indexes DON'T help:
-- Low cardinality column (boolean, gender) with few distinct values
-- Function on indexed column: WHERE LOWER(email) = 'x'  (use functional index or store lowercase)
-- Leading wildcard: LIKE '%term' (use full-text search instead)
```

### N+1 Problem (SQL)
```sql
-- N+1: fetching users then querying orders for each user in app code
SELECT * FROM users; -- 1 query
-- For each user:
SELECT * FROM orders WHERE user_id = 1; -- N queries!

-- Solution: JOIN
SELECT u.*, o.id AS order_id, o.total
FROM users u
LEFT JOIN orders o ON u.id = o.user_id;

-- Solution: WHERE IN (batched)
SELECT * FROM orders WHERE user_id IN (1, 2, 3, 4, 5);

-- In ORM: use eager loading
// TypeORM: relations: { orders: true }
// Sequelize: include: [Order]
// Prisma: include: { orders: true }
```

---

## MongoDB — Query Patterns

### CRUD with Filters
```javascript
// Find with complex filter
db.orders.find({
  status: { $in: ['pending', 'processing'] },
  total:  { $gte: 100, $lt: 1000 },
  tags:   { $all: ['priority', 'domestic'] },  // array contains ALL
  'address.city': 'Mumbai',                     // nested field
  createdAt: { $gte: new Date('2024-01-01') },
});

// Text search (requires text index)
db.posts.createIndex({ title: 'text', content: 'text' });
db.posts.find({ $text: { $search: 'nodejs typescript' } },
              { score: { $meta: 'textScore' } })
        .sort({ score: { $meta: 'textScore' } });

// Array queries
db.products.find({ tags: 'electronics' });           // tag exists in array
db.products.find({ tags: { $in: ['a', 'b'] } });     // any of these tags
db.products.find({ tags: { $all: ['a', 'b'] } });    // all tags present
db.products.find({ 'variants.size': 'XL' });         // nested array element

// Existence / type checks
db.users.find({ phone: { $exists: true, $ne: null } });
db.users.find({ age: { $type: 'number' } });

// Update operators
db.users.updateOne(
  { _id: userId },
  {
    $set:    { name: 'New Name', 'address.city': 'Delhi' },
    $unset:  { temporaryField: '' },
    $inc:    { loginCount: 1, credits: -10 },
    $push:   { tags: 'premium' },
    $addToSet: { roles: 'admin' },    // add only if not already in array
    $pull:   { tags: 'free' },
    $currentDate: { updatedAt: true },
  }
);

// Upsert (insert if not found)
db.userStats.updateOne(
  { userId: 'abc' },
  { $inc: { views: 1 } },
  { upsert: true }
);

// findOneAndUpdate — atomic get + update
const updated = await db.orders.findOneAndUpdate(
  { _id: orderId, status: 'pending' },
  { $set: { status: 'processing' } },
  { returnDocument: 'after', new: true }
);
```

### Aggregation Pipeline
```javascript
// Monthly sales report with category breakdown
db.orders.aggregate([
  // Filter completed orders in 2024
  { $match: {
    status: 'completed',
    createdAt: { $gte: new Date('2024-01-01'), $lt: new Date('2025-01-01') }
  }},

  // Unwind items array (one doc per item)
  { $unwind: '$items' },

  // Look up product details
  { $lookup: {
    from: 'products',
    localField: 'items.productId',
    foreignField: '_id',
    as: 'product',
    pipeline: [{ $project: { name: 1, category: 1 } }],
  }},
  { $unwind: '$product' },

  // Group by month and category
  { $group: {
    _id: {
      month:    { $month: '$createdAt' },
      year:     { $year:  '$createdAt' },
      category: '$product.category',
    },
    revenue:    { $sum: { $multiply: ['$items.price', '$items.quantity'] } },
    unitsSold:  { $sum: '$items.quantity' },
    orderCount: { $addToSet: '$_id' },  // unique order IDs
  }},

  // Compute order count from set
  { $addFields: { orderCount: { $size: '$orderCount' } } },

  // Sort by month then revenue
  { $sort: { '_id.year': 1, '_id.month': 1, revenue: -1 } },

  // Reshape output
  { $project: {
    _id: 0,
    year:       '$_id.year',
    month:      '$_id.month',
    category:   '$_id.category',
    revenue:    { $round: ['$revenue', 2] },
    unitsSold:  1,
    orderCount: 1,
  }},
]);
```

---

## Interview Query Problems

### Problem 1: Second Highest Salary
```sql
-- Option 1: Subquery
SELECT MAX(salary) AS second_highest
FROM employees
WHERE salary < (SELECT MAX(salary) FROM employees);

-- Option 2: LIMIT OFFSET (works in MySQL/PostgreSQL)
SELECT DISTINCT salary
FROM employees
ORDER BY salary DESC
LIMIT 1 OFFSET 1;

-- Option 3: Dense Rank (handles N-th highest)
SELECT salary FROM (
  SELECT salary, DENSE_RANK() OVER (ORDER BY salary DESC) AS rk
  FROM employees
) ranked
WHERE rk = 2;
```

### Problem 2: Find Duplicate Records
```sql
-- Find duplicate emails
SELECT email, COUNT(*) AS count
FROM users
GROUP BY email
HAVING COUNT(*) > 1;

-- Get all rows that are duplicates (including all occurrences)
SELECT * FROM users
WHERE email IN (
  SELECT email FROM users GROUP BY email HAVING COUNT(*) > 1
)
ORDER BY email;

-- Delete duplicates, keep lowest ID
DELETE FROM users
WHERE id NOT IN (
  SELECT MIN(id) FROM users GROUP BY email
);
```

### Problem 3: Users Who Have Never Ordered
```sql
-- Option 1: LEFT JOIN with NULL check
SELECT u.id, u.name
FROM users u
LEFT JOIN orders o ON u.id = o.user_id
WHERE o.id IS NULL;

-- Option 2: NOT EXISTS (stops at first match — can be faster)
SELECT id, name FROM users u
WHERE NOT EXISTS (
  SELECT 1 FROM orders o WHERE o.user_id = u.id
);

-- Option 3: NOT IN (careful with NULLs!)
SELECT id, name FROM users
WHERE id NOT IN (
  SELECT DISTINCT user_id FROM orders WHERE user_id IS NOT NULL
);
```

### Problem 4: Running Balance
```sql
-- Account transaction ledger
SELECT
  id,
  description,
  amount,
  SUM(amount) OVER (
    ORDER BY created_at
    ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW
  ) AS running_balance
FROM transactions
WHERE account_id = 123
ORDER BY created_at;
```

### Problem 5: Retention / Cohort Analysis
```sql
-- Month-over-month user retention
WITH first_order AS (
  SELECT user_id, DATE_TRUNC('month', MIN(created_at)) AS cohort_month
  FROM orders GROUP BY user_id
),
user_activity AS (
  SELECT DISTINCT user_id, DATE_TRUNC('month', created_at) AS activity_month
  FROM orders
)
SELECT
  f.cohort_month,
  EXTRACT(EPOCH FROM ua.activity_month - f.cohort_month) / 2592000 AS months_since_join,
  COUNT(DISTINCT ua.user_id) AS retained_users
FROM first_order f
JOIN user_activity ua ON f.user_id = ua.user_id
GROUP BY 1, 2
ORDER BY 1, 2;
```

### Problem 6: Pivot (crosstab)
```sql
-- Sales per product per month (columns = months)
SELECT
  product_id,
  SUM(CASE WHEN EXTRACT(MONTH FROM sale_date) = 1 THEN revenue END) AS jan,
  SUM(CASE WHEN EXTRACT(MONTH FROM sale_date) = 2 THEN revenue END) AS feb,
  SUM(CASE WHEN EXTRACT(MONTH FROM sale_date) = 3 THEN revenue END) AS mar
FROM sales
WHERE EXTRACT(YEAR FROM sale_date) = 2024
GROUP BY product_id;
```

---

## Database Design Interview Questions

**Q: When would you use NoSQL over SQL?**
```
SQL (PostgreSQL, MySQL):
  ✅ Complex relationships (JOINs)
  ✅ ACID transactions (financial data)
  ✅ Well-defined schema
  ✅ Complex queries, analytics
  ✅ Referential integrity (foreign keys)

MongoDB/NoSQL:
  ✅ Flexible/evolving schema
  ✅ Document-oriented data (user profiles, product catalogs)
  ✅ Horizontal scaling (sharding native)
  ✅ High write throughput (time series, logs)
  ✅ Data stored and accessed together (embed vs reference)

MongoDB embed when:
  - Child data always accessed with parent (post + comments)
  - One-to-few relationship (user → addresses, max 5-10)

MongoDB reference when:
  - Data accessed independently
  - Many-to-many
  - Unbounded arrays (post → millions of comments → reference)
```

**Q: Explain ACID properties.**
```
Atomicity:   All operations in a transaction succeed or ALL fail. No partial state.
Consistency: Transaction brings DB from one valid state to another. Constraints upheld.
Isolation:   Concurrent transactions behave as if sequential. No dirty reads.
Durability:  Committed transactions survive crashes (written to disk/WAL).

Isolation levels (weakest to strongest):
  Read Uncommitted: can read uncommitted changes (dirty reads) — almost never used
  Read Committed:   only read committed data — default in PostgreSQL
  Repeatable Read:  same row read twice = same result — default in MySQL
  Serializable:     fully isolated, as if sequential — use for financial operations

Tradeoff: stronger isolation = more locks = less concurrency = lower throughput
```

**Q: What is an index and when should you NOT use one?**
```
Index = data structure (usually B-tree) that allows O(log n) lookup vs O(n) full scan.

Don't add index when:
  - Small table (< 1000 rows): full scan is fine
  - Column with very low cardinality (boolean) — optimizer may ignore it
  - Column rarely used in WHERE/JOIN
  - Table has very high write rate (each write must update all indexes)
  - Already covered by another composite index

Signs you need an index:
  EXPLAIN shows Seq Scan on large table
  Slow queries on WHERE/JOIN columns
  ORDER BY/GROUP BY on non-indexed columns causing "sort" in query plan
```
