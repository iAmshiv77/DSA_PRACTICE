# Machine Coding: React / Next.js

## React Hooks — Deep Dive

### useState
```typescript
// Lazy initialization — use when initial state is expensive to compute
const [state, setState] = useState(() => computeExpensiveInitialState());

// Functional update — use when new state depends on old state
setState(prev => ({ ...prev, count: prev.count + 1 }));

// WRONG — stale closure bug:
const [count, setCount] = useState(0);
const handleClick = () => {
  setCount(count + 1); // 'count' captured at closure time
  setCount(count + 1); // STILL count + 1, not count + 2!
};
// RIGHT:
const handleClick = () => {
  setCount(prev => prev + 1);
  setCount(prev => prev + 1); // count + 2
};
```

### useEffect
```typescript
// Dependency array rules:
useEffect(() => { ... });              // runs after EVERY render
useEffect(() => { ... }, []);          // runs ONCE on mount
useEffect(() => { ... }, [dep]);       // runs when dep changes

// Cleanup:
useEffect(() => {
  const sub = eventEmitter.on('event', handler);
  return () => sub.off('event', handler); // cleanup on unmount or re-run
}, []);

// Async in useEffect:
useEffect(() => {
  let cancelled = false;

  async function fetchData() {
    const data = await api.getUser(userId);
    if (!cancelled) setUser(data);  // guard against race condition
  }

  fetchData();
  return () => { cancelled = true; };  // cancel on unmount/re-run
}, [userId]);

// Common mistake: missing dependency
useEffect(() => {
  fetchUser(userId); // userId not in deps → stale closure
}, []); // ESLint will warn: react-hooks/exhaustive-deps
```

### useCallback & useMemo
```typescript
// useMemo: memoize COMPUTED VALUE
const sortedList = useMemo(
  () => [...items].sort((a, b) => a.name.localeCompare(b.name)),
  [items],
);

// useCallback: memoize FUNCTION REFERENCE (for stable prop passing)
const handleDelete = useCallback(
  (id: string) => {
    dispatch(deleteItem(id));
  },
  [dispatch],
);

// When to use:
// useMemo:     expensive computation, referentially stable object for deps
// useCallback: function passed to child component wrapped in React.memo
// Don't overuse: every memo has cost. Profile first.

// React.memo: skip re-render if props haven't changed
const ItemRow = React.memo(({ item, onDelete }: Props) => {
  return <div onClick={() => onDelete(item.id)}>{item.name}</div>;
});
```

### useRef
```typescript
// 1. DOM access
const inputRef = useRef<HTMLInputElement>(null);
const focusInput = () => inputRef.current?.focus();
<input ref={inputRef} />

// 2. Store mutable value without triggering re-render
const intervalRef = useRef<ReturnType<typeof setInterval>>();
useEffect(() => {
  intervalRef.current = setInterval(() => tick(), 1000);
  return () => clearInterval(intervalRef.current);
}, []);

// 3. Previous value pattern
function usePrevious<T>(value: T): T | undefined {
  const ref = useRef<T>();
  useEffect(() => { ref.current = value; });
  return ref.current;
}
```

### useContext
```typescript
// 1. Create context with default value
interface AuthContextType {
  user: User | null;
  login: (credentials: Credentials) => Promise<void>;
  logout: () => void;
}
const AuthContext = createContext<AuthContextType | null>(null);

// 2. Custom hook with null guard
function useAuth(): AuthContextType {
  const ctx = useContext(AuthContext);
  if (!ctx) throw new Error('useAuth must be used within AuthProvider');
  return ctx;
}

// 3. Provider
export function AuthProvider({ children }: { children: ReactNode }) {
  const [user, setUser] = useState<User | null>(null);
  const login = async (credentials: Credentials) => { ... };
  const logout = () => setUser(null);
  return (
    <AuthContext.Provider value={{ user, login, logout }}>
      {children}
    </AuthContext.Provider>
  );
}

// Performance: context re-renders ALL consumers when value changes.
// Split contexts or use useMemo for value:
const value = useMemo(() => ({ user, login, logout }), [user]);
```

### useReducer
```typescript
// Use when: complex state logic, multiple sub-values, next state depends on previous
type Action =
  | { type: 'FETCH_START' }
  | { type: 'FETCH_SUCCESS'; payload: User[] }
  | { type: 'FETCH_ERROR'; error: string };

interface State {
  users: User[];
  loading: boolean;
  error: string | null;
}

function reducer(state: State, action: Action): State {
  switch (action.type) {
    case 'FETCH_START':   return { ...state, loading: true, error: null };
    case 'FETCH_SUCCESS': return { ...state, loading: false, users: action.payload };
    case 'FETCH_ERROR':   return { ...state, loading: false, error: action.error };
    default: return state;
  }
}

function useUsers() {
  const [state, dispatch] = useReducer(reducer, { users: [], loading: false, error: null });

  const fetchUsers = async () => {
    dispatch({ type: 'FETCH_START' });
    try {
      const users = await api.getUsers();
      dispatch({ type: 'FETCH_SUCCESS', payload: users });
    } catch (err) {
      dispatch({ type: 'FETCH_ERROR', error: err.message });
    }
  };

  return { ...state, fetchUsers };
}
```

---

## Custom Hooks — Patterns

### useDebounce
```typescript
function useDebounce<T>(value: T, delay: number): T {
  const [debouncedValue, setDebouncedValue] = useState(value);

  useEffect(() => {
    const timer = setTimeout(() => setDebouncedValue(value), delay);
    return () => clearTimeout(timer);
  }, [value, delay]);

  return debouncedValue;
}

// Usage:
const debouncedSearch = useDebounce(searchTerm, 300);
useEffect(() => {
  if (debouncedSearch) fetchResults(debouncedSearch);
}, [debouncedSearch]);
```

### useFetch
```typescript
interface FetchState<T> {
  data: T | null;
  loading: boolean;
  error: Error | null;
}

function useFetch<T>(url: string): FetchState<T> {
  const [state, setState] = useState<FetchState<T>>({
    data: null, loading: true, error: null,
  });

  useEffect(() => {
    const controller = new AbortController();

    setState(prev => ({ ...prev, loading: true }));

    fetch(url, { signal: controller.signal })
      .then(res => {
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
        return res.json();
      })
      .then(data => setState({ data, loading: false, error: null }))
      .catch(err => {
        if (err.name !== 'AbortError') {
          setState({ data: null, loading: false, error: err });
        }
      });

    return () => controller.abort();
  }, [url]);

  return state;
}
```

### useLocalStorage
```typescript
function useLocalStorage<T>(key: string, initialValue: T) {
  const [storedValue, setStoredValue] = useState<T>(() => {
    try {
      const item = window.localStorage.getItem(key);
      return item ? JSON.parse(item) : initialValue;
    } catch {
      return initialValue;
    }
  });

  const setValue = (value: T | ((val: T) => T)) => {
    try {
      const valueToStore = value instanceof Function ? value(storedValue) : value;
      setStoredValue(valueToStore);
      window.localStorage.setItem(key, JSON.stringify(valueToStore));
    } catch (error) {
      console.error(error);
    }
  };

  return [storedValue, setValue] as const;
}
```

### useIntersectionObserver (Infinite Scroll)
```typescript
function useIntersectionObserver(
  elementRef: RefObject<Element>,
  options?: IntersectionObserverInit,
): IntersectionObserverEntry | null {
  const [entry, setEntry] = useState<IntersectionObserverEntry | null>(null);

  useEffect(() => {
    const el = elementRef.current;
    if (!el) return;

    const observer = new IntersectionObserver(([entry]) => {
      setEntry(entry);
    }, options);

    observer.observe(el);
    return () => observer.disconnect();
  }, [elementRef, options]);

  return entry;
}

// Infinite Scroll implementation:
function InfiniteList() {
  const [items, setItems] = useState<Item[]>([]);
  const [page, setPage] = useState(1);
  const [hasMore, setHasMore] = useState(true);
  const loaderRef = useRef<HTMLDivElement>(null);

  const entry = useIntersectionObserver(loaderRef, { threshold: 0.1 });

  useEffect(() => {
    if (entry?.isIntersecting && hasMore) {
      setPage(p => p + 1);
    }
  }, [entry?.isIntersecting]);

  useEffect(() => {
    fetchItems(page).then(newItems => {
      setItems(prev => [...prev, ...newItems]);
      setHasMore(newItems.length > 0);
    });
  }, [page]);

  return (
    <div>
      {items.map(item => <ItemCard key={item.id} item={item} />)}
      <div ref={loaderRef}>{hasMore ? 'Loading...' : 'No more items'}</div>
    </div>
  );
}
```

---

## Machine Coding Tasks

### Task 1: Search with Debounce + Autocomplete
```typescript
// components/SearchAutocomplete.tsx
interface SearchResult {
  id: string;
  title: string;
}

export function SearchAutocomplete() {
  const [query, setQuery] = useState('');
  const [results, setResults] = useState<SearchResult[]>([]);
  const [isOpen, setIsOpen] = useState(false);
  const [selected, setSelected] = useState(-1);  // keyboard nav
  const [loading, setLoading] = useState(false);

  const debouncedQuery = useDebounce(query, 300);

  useEffect(() => {
    if (!debouncedQuery.trim()) {
      setResults([]);
      setIsOpen(false);
      return;
    }

    setLoading(true);
    searchApi(debouncedQuery)
      .then(data => {
        setResults(data);
        setIsOpen(data.length > 0);
        setSelected(-1);
      })
      .finally(() => setLoading(false));
  }, [debouncedQuery]);

  const handleKeyDown = (e: React.KeyboardEvent) => {
    if (e.key === 'ArrowDown') setSelected(s => Math.min(s + 1, results.length - 1));
    if (e.key === 'ArrowUp')   setSelected(s => Math.max(s - 1, -1));
    if (e.key === 'Enter' && selected >= 0) handleSelect(results[selected]);
    if (e.key === 'Escape') setIsOpen(false);
  };

  const handleSelect = (result: SearchResult) => {
    setQuery(result.title);
    setIsOpen(false);
  };

  return (
    <div className="relative">
      <input
        value={query}
        onChange={e => setQuery(e.target.value)}
        onKeyDown={handleKeyDown}
        onFocus={() => results.length > 0 && setIsOpen(true)}
        onBlur={() => setTimeout(() => setIsOpen(false), 200)}
        placeholder="Search..."
        aria-autocomplete="list"
        aria-expanded={isOpen}
      />
      {loading && <Spinner />}
      {isOpen && (
        <ul role="listbox">
          {results.map((result, index) => (
            <li
              key={result.id}
              role="option"
              aria-selected={index === selected}
              className={index === selected ? 'highlighted' : ''}
              onMouseDown={() => handleSelect(result)}
            >
              {result.title}
            </li>
          ))}
        </ul>
      )}
    </div>
  );
}
```

### Task 2: Drag & Drop Kanban Board
```typescript
// Simple HTML5 Drag and Drop — no library
interface Task {
  id: string;
  title: string;
  column: 'todo' | 'in_progress' | 'done';
}

const COLUMNS = ['todo', 'in_progress', 'done'] as const;

export function KanbanBoard() {
  const [tasks, setTasks] = useState<Task[]>(initialTasks);
  const draggedId = useRef<string | null>(null);

  const handleDragStart = (id: string) => {
    draggedId.current = id;
  };

  const handleDrop = (column: Task['column']) => {
    if (!draggedId.current) return;

    setTasks(prev =>
      prev.map(task =>
        task.id === draggedId.current ? { ...task, column } : task,
      ),
    );
    draggedId.current = null;
  };

  return (
    <div className="kanban-board">
      {COLUMNS.map(column => (
        <div
          key={column}
          className="column"
          onDragOver={e => e.preventDefault()} // allow drop
          onDrop={() => handleDrop(column)}
        >
          <h3>{column.replace('_', ' ').toUpperCase()}</h3>
          {tasks
            .filter(t => t.column === column)
            .map(task => (
              <div
                key={task.id}
                draggable
                onDragStart={() => handleDragStart(task.id)}
                className="task-card"
              >
                {task.title}
              </div>
            ))}
        </div>
      ))}
    </div>
  );
}
```

### Task 3: Multi-Step Form with Validation
```typescript
// Without library (React Hook Form pattern manually)
interface FormData {
  step1: { name: string; email: string };
  step2: { phone: string; address: string };
  step3: { plan: 'basic' | 'pro' | 'enterprise' };
}

type Errors = Partial<Record<string, string>>;

function validateStep1(data: FormData['step1']): Errors {
  const errors: Errors = {};
  if (!data.name.trim()) errors.name = 'Name is required';
  if (!/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(data.email)) errors.email = 'Invalid email';
  return errors;
}

export function MultiStepForm() {
  const [step, setStep] = useState(1);
  const [formData, setFormData] = useState<FormData>({
    step1: { name: '', email: '' },
    step2: { phone: '', address: '' },
    step3: { plan: 'basic' },
  });
  const [errors, setErrors] = useState<Errors>({});

  const updateField = (stepKey: keyof FormData, field: string, value: string) => {
    setFormData(prev => ({
      ...prev,
      [stepKey]: { ...prev[stepKey], [field]: value },
    }));
    // Clear error on change
    if (errors[field]) setErrors(prev => ({ ...prev, [field]: undefined }));
  };

  const handleNext = () => {
    const newErrors = step === 1 ? validateStep1(formData.step1) : {};
    if (Object.keys(newErrors).length > 0) {
      setErrors(newErrors);
      return;
    }
    setStep(s => s + 1);
  };

  const handleSubmit = async () => {
    await submitForm(formData);
  };

  return (
    <form onSubmit={e => e.preventDefault()}>
      {/* Progress */}
      <div className="steps">
        {[1, 2, 3].map(n => (
          <div key={n} className={`step ${n <= step ? 'active' : ''}`}>{n}</div>
        ))}
      </div>

      {step === 1 && (
        <>
          <Input
            label="Name"
            value={formData.step1.name}
            error={errors.name}
            onChange={v => updateField('step1', 'name', v)}
          />
          <Input
            label="Email"
            value={formData.step1.email}
            error={errors.email}
            onChange={v => updateField('step1', 'email', v)}
          />
        </>
      )}

      {/* ... step 2, step 3 ... */}

      <div className="actions">
        {step > 1 && <button type="button" onClick={() => setStep(s => s - 1)}>Back</button>}
        {step < 3 && <button type="button" onClick={handleNext}>Next</button>}
        {step === 3 && <button type="button" onClick={handleSubmit}>Submit</button>}
      </div>
    </form>
  );
}
```

---

## Next.js — Deep Dive

### Rendering Strategies
```
CSR (Client-Side Rendering):
  - HTML shell sent, JS fetches data in browser
  - No SEO, slow initial paint
  - Use: dashboards, user-specific pages

SSR (Server-Side Rendering):
  - HTML generated on EACH request on server
  - Fresh data, good SEO, slower TTFB vs static
  - Use: news feeds, product pages with live inventory

SSG (Static Site Generation):
  - HTML generated at BUILD TIME
  - Fastest delivery (CDN), great SEO
  - Use: blog posts, docs, marketing pages

ISR (Incremental Static Regeneration):
  - SSG + revalidate interval
  - Stale-while-revalidate: serve cached, regenerate in background
  - Use: e-commerce (price updates every 60s), dashboards

Streaming (Next.js 13+):
  - Send HTML progressively, show loading UI instantly
  - React Suspense + Server Components
```

### App Router (Next.js 13+)
```
app/
├── layout.tsx          ← root layout (persistent across routes)
├── page.tsx            ← / route
├── loading.tsx         ← Suspense fallback for this segment
├── error.tsx           ← Error boundary for this segment
├── not-found.tsx       ← 404 for this segment
├── (auth)/             ← Route group (no URL segment)
│   ├── login/page.tsx
│   └── register/page.tsx
├── dashboard/
│   ├── layout.tsx      ← nested layout
│   └── page.tsx
├── api/
│   └── users/route.ts  ← Route Handler (replaces pages/api)
└── blog/
    └── [slug]/page.tsx ← dynamic route

Dynamic routes:
  [slug]     → /blog/my-post
  [...slug]  → /blog/a/b/c (catch-all)
  [[...slug]] → /blog AND /blog/a/b/c (optional catch-all)
```

### Server Components vs Client Components
```typescript
// SERVER COMPONENT (default in App Router)
// Runs on server — can use async/await, access DB directly
// Cannot: useState, useEffect, onClick, browser APIs
async function UserProfile({ userId }: { userId: string }) {
  const user = await db.users.findById(userId); // direct DB access!
  return <div>{user.name}</div>;
}

// CLIENT COMPONENT — 'use client' directive
'use client';
import { useState } from 'react';

function Counter() {
  const [count, setCount] = useState(0);
  return <button onClick={() => setCount(c => c + 1)}>{count}</button>;
}

// PATTERN: Server component fetches, passes to client component
// Server:
async function ProductPage({ id }) {
  const product = await getProduct(id);
  return <AddToCartButton product={product} />; // client component
}

// Client:
'use client';
function AddToCartButton({ product }) {
  const addToCart = () => { /* ... */ };
  return <button onClick={addToCart}>Add to Cart</button>;
}
```

### Data Fetching in App Router
```typescript
// 1. Server Component — async fetch
async function ProductList() {
  const products = await fetch('https://api.example.com/products', {
    next: { revalidate: 60 }, // ISR: revalidate every 60s
    // cache: 'no-store'      // SSR: always fresh
    // cache: 'force-cache'   // SSG: cache indefinitely
  }).then(r => r.json());

  return <ul>{products.map(p => <li key={p.id}>{p.name}</li>)}</ul>;
}

// 2. Parallel data fetching (don't await sequentially!)
async function Dashboard() {
  // WRONG (waterfall):
  // const user = await getUser();
  // const stats = await getStats();

  // RIGHT (parallel):
  const [user, stats] = await Promise.all([getUser(), getStats()]);
  return <DashboardUI user={user} stats={stats} />;
}

// 3. Route Handlers (API routes)
// app/api/users/[id]/route.ts
import { NextRequest, NextResponse } from 'next/server';

export async function GET(
  request: NextRequest,
  { params }: { params: { id: string } },
) {
  const user = await db.users.findById(params.id);
  if (!user) return NextResponse.json({ error: 'Not found' }, { status: 404 });
  return NextResponse.json(user);
}

export async function PATCH(
  request: NextRequest,
  { params }: { params: { id: string } },
) {
  const body = await request.json();
  const user = await db.users.update(params.id, body);
  return NextResponse.json(user);
}
```

### Middleware
```typescript
// middleware.ts (root level)
import { NextResponse } from 'next/server';
import type { NextRequest } from 'next/server';

export function middleware(request: NextRequest) {
  const token = request.cookies.get('token')?.value;

  // Auth protection
  if (!token && request.nextUrl.pathname.startsWith('/dashboard')) {
    return NextResponse.redirect(new URL('/login', request.url));
  }

  // A/B testing via cookie
  const variant = request.cookies.get('variant')?.value ??
    (Math.random() > 0.5 ? 'a' : 'b');
  const response = NextResponse.next();
  response.cookies.set('variant', variant);
  return response;
}

export const config = {
  matcher: ['/dashboard/:path*', '/api/protected/:path*'],
};
```

### generateStaticParams (SSG with dynamic routes)
```typescript
// app/blog/[slug]/page.tsx
export async function generateStaticParams() {
  const posts = await getPosts();
  return posts.map(post => ({ slug: post.slug }));
}

// generateMetadata for SEO
export async function generateMetadata({ params }: { params: { slug: string } }) {
  const post = await getPost(params.slug);
  return {
    title: post.title,
    description: post.excerpt,
    openGraph: {
      title: post.title,
      images: [post.coverImage],
    },
  };
}

export default async function BlogPost({ params }: { params: { slug: string } }) {
  const post = await getPost(params.slug);
  return <article dangerouslySetInnerHTML={{ __html: post.content }} />;
}
```

---

## State Management Comparison

```
Local state (useState):
  Simple component state. Forms, toggles, UI state.

Lifted state:
  Share state between siblings → lift to common parent.
  Props drilling > 2 levels → use context or state manager.

Context API:
  Global state with low update frequency.
  User auth, theme, locale.
  High-frequency updates → performance issues (all consumers re-render).

Zustand (recommended for most apps):
  Lightweight, no boilerplate, works outside React.
  Selectors prevent unnecessary re-renders.

Redux Toolkit:
  Large teams, complex state, need DevTools, time-travel debugging.
  Overkill for most apps.

React Query / SWR:
  SERVER STATE (fetching, caching, syncing).
  Handles: loading/error states, caching, background refresh, optimistic updates.
  Not for: client-only state (UI toggles, form state).
```

### Zustand Pattern
```typescript
import { create } from 'zustand';
import { devtools, persist } from 'zustand/middleware';

interface CartStore {
  items: CartItem[];
  addItem: (item: CartItem) => void;
  removeItem: (id: string) => void;
  clearCart: () => void;
  total: () => number;
}

const useCartStore = create<CartStore>()(
  devtools(
    persist(
      (set, get) => ({
        items: [],
        addItem: (item) =>
          set(state => ({ items: [...state.items, item] })),
        removeItem: (id) =>
          set(state => ({ items: state.items.filter(i => i.id !== id) })),
        clearCart: () => set({ items: [] }),
        total: () => get().items.reduce((sum, item) => sum + item.price, 0),
      }),
      { name: 'cart-storage' }, // localStorage key
    ),
  ),
);

// Selector — only re-render when items.length changes, not on any state change
const itemCount = useCartStore(state => state.items.length);
```

---

## Common Interview Questions

**Q: What is the difference between useMemo and useCallback?**
```
useMemo:     memoizes the RESULT of a function call. Returns a value.
             useMemo(() => expensiveCalc(a, b), [a, b])

useCallback: memoizes the FUNCTION ITSELF. Returns a function.
             useCallback(() => doSomething(a, b), [a, b])

useCallback(fn, deps) === useMemo(() => fn, deps)

Use useCallback when passing callbacks to child components wrapped in React.memo
so the child doesn't re-render when parent re-renders.
```

**Q: What is the React reconciliation algorithm?**
```
React's diffing algorithm (Fiber):
1. Different element types → unmount old tree, mount new tree
2. Same type → update existing DOM node (just change attributes)
3. Lists → use key prop to match elements across renders
   - Key tells React which item is which after reorder
   - Never use array index as key if list can be reordered (stale state bugs)
   - Use stable unique ID (item.id)

O(n) algorithm (not O(n³) naive diffing) via:
  - Only compare same-level nodes (no cross-level)
  - Key-based list matching
```

**Q: What are Server Components and why do they matter?**
```
Server Components (RSC):
  - Render on server, send HTML + JSON description to client
  - Never downloaded as JS by client → zero bundle size cost
  - Can access server resources (DB, file system, env vars)
  - Cannot: useState, useEffect, event handlers, browser APIs

Client Components:
  - Have interactivity (hooks, events)
  - 'use client' boundary — marks component and its imports as client

Why it matters:
  1. Smaller JS bundle (server components never ship to client)
  2. Faster initial load (less JS to parse and execute)
  3. Direct DB access without API layer for read-only data
  4. Streaming — server progressively sends rendered HTML

Mental model: Server components = new PHP/Rails, Client components = React you know.
```

**Q: How do you optimize Next.js performance?**
```
1. Image optimization: <Image> component → WebP conversion, lazy loading, blur placeholder
2. Font optimization: next/font → self-hosted, no layout shift
3. Code splitting: dynamic imports → next/dynamic (lazy load heavy components)
4. Route prefetching: <Link> prefetches on hover (automatic)
5. Static where possible: use ISR instead of SSR if data < N minutes stale
6. Avoid client components at root level (pushes JS to browser)
7. Partial prerendering (experimental): static shell + dynamic holes
8. Bundle analysis: @next/bundle-analyzer to find large packages

const HeavyChart = dynamic(() => import('./Chart'), {
  loading: () => <Skeleton />,
  ssr: false,  // client only
});
```

**Q: How do you handle authentication in Next.js App Router?**
```typescript
// 1. Middleware to protect routes (runs at edge, before page renders)
// middleware.ts
import { getToken } from 'next-auth/jwt';

export async function middleware(req: NextRequest) {
  const token = await getToken({ req, secret: process.env.NEXTAUTH_SECRET });
  if (!token) return NextResponse.redirect(new URL('/login', req.url));
}

// 2. Auth in Server Components
import { getServerSession } from 'next-auth';
async function ProtectedPage() {
  const session = await getServerSession(authOptions);
  if (!session) redirect('/login');
  return <Dashboard user={session.user} />;
}

// 3. Auth in Route Handlers
async function GET(req: NextRequest) {
  const session = await getServerSession(authOptions);
  if (!session) return NextResponse.json({ error: 'Unauthorized' }, { status: 401 });
  // ...
}
```
