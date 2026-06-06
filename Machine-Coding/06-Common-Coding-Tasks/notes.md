# Common Coding Tasks — JavaScript / TypeScript

## Async Patterns

### Promise Combinators
```typescript
// Promise.all — all resolve or first reject
const [user, posts, followers] = await Promise.all([
  getUser(id),
  getPosts(id),
  getFollowers(id),
]);
// If ANY rejects → entire .all() rejects

// Promise.allSettled — wait for all, never rejects
const results = await Promise.allSettled([getUser(id), getPosts(id)]);
results.forEach(result => {
  if (result.status === 'fulfilled') console.log(result.value);
  else console.error(result.reason);
});

// Promise.race — first to settle (resolve OR reject) wins
const data = await Promise.race([
  fetchFromPrimary(),
  fetchFromFallback(),
]);

// Promise.any — first to RESOLVE wins (rejects only if ALL reject)
const data = await Promise.any([server1.fetch(), server2.fetch(), server3.fetch()]);
// Throws AggregateError if all reject

// Timeout pattern
function withTimeout<T>(promise: Promise<T>, ms: number): Promise<T> {
  const timeout = new Promise<never>((_, reject) =>
    setTimeout(() => reject(new Error(`Timeout after ${ms}ms`)), ms)
  );
  return Promise.race([promise, timeout]);
}

await withTimeout(fetchData(), 5000);
```

### Retry with Exponential Backoff
```typescript
async function retry<T>(
  fn: () => Promise<T>,
  options: { attempts?: number; delay?: number; factor?: number } = {},
): Promise<T> {
  const { attempts = 3, delay = 1000, factor = 2 } = options;

  for (let i = 0; i < attempts; i++) {
    try {
      return await fn();
    } catch (err) {
      if (i === attempts - 1) throw err;  // last attempt, re-throw
      const wait = delay * Math.pow(factor, i);  // 1s, 2s, 4s...
      await new Promise(resolve => setTimeout(resolve, wait));
    }
  }
  throw new Error('Should not reach here');
}

// Usage
const data = await retry(() => fetchFromApi(), { attempts: 3, delay: 500 });
```

### Async Queue (Concurrency Limiter)
```typescript
// Limit concurrent async operations (e.g., max 5 HTTP requests at once)
async function asyncPool<T, R>(
  concurrency: number,
  items: T[],
  fn: (item: T) => Promise<R>,
): Promise<R[]> {
  const results: R[] = [];
  const inFlight = new Set<Promise<void>>();

  for (const item of items) {
    const p = fn(item).then(result => {
      results.push(result);
      inFlight.delete(p);
    });
    inFlight.add(p);

    if (inFlight.size >= concurrency) {
      await Promise.race(inFlight);
    }
  }

  await Promise.all(inFlight);
  return results;
}

// Process 100 URLs with max 5 concurrent requests
const results = await asyncPool(5, urls, url => fetch(url).then(r => r.json()));
```

---

## Utility Functions

### Debounce
```typescript
function debounce<T extends (...args: any[]) => any>(
  fn: T,
  delay: number,
): (...args: Parameters<T>) => void {
  let timer: ReturnType<typeof setTimeout>;

  return function (...args: Parameters<T>) {
    clearTimeout(timer);
    timer = setTimeout(() => fn(...args), delay);
  };
}

// Usage: search input
const onSearch = debounce((query: string) => {
  fetch(`/api/search?q=${query}`);
}, 300);
```

### Throttle
```typescript
function throttle<T extends (...args: any[]) => any>(
  fn: T,
  limit: number,
): (...args: Parameters<T>) => void {
  let lastRun = 0;

  return function (...args: Parameters<T>) {
    const now = Date.now();
    if (now - lastRun >= limit) {
      lastRun = now;
      fn(...args);
    }
  };
}

// Usage: scroll event — at most once per 100ms
window.addEventListener('scroll', throttle(handleScroll, 100));
```

### Deep Clone
```typescript
// Modern: structuredClone (built-in, handles Date, Map, Set, etc.)
const clone = structuredClone(original);

// Custom deep clone (interview version)
function deepClone<T>(value: T, seen = new Map()): T {
  if (value === null || typeof value !== 'object') return value;

  // Handle circular references
  if (seen.has(value)) return seen.get(value);

  if (value instanceof Date) return new Date(value.getTime()) as unknown as T;
  if (value instanceof RegExp) return new RegExp(value) as unknown as T;
  if (value instanceof Map) {
    const clone = new Map();
    seen.set(value, clone);
    value.forEach((v, k) => clone.set(deepClone(k, seen), deepClone(v, seen)));
    return clone as unknown as T;
  }
  if (value instanceof Set) {
    const clone = new Set();
    seen.set(value, clone);
    value.forEach(v => clone.add(deepClone(v, seen)));
    return clone as unknown as T;
  }
  if (Array.isArray(value)) {
    const clone: any[] = [];
    seen.set(value, clone);
    value.forEach(item => clone.push(deepClone(item, seen)));
    return clone as unknown as T;
  }

  const clone = Object.create(Object.getPrototypeOf(value));
  seen.set(value, clone);
  for (const key of Object.keys(value)) {
    clone[key] = deepClone((value as any)[key], seen);
  }
  return clone;
}
```

### Deep Equal
```typescript
function deepEqual(a: unknown, b: unknown): boolean {
  if (a === b) return true;
  if (a === null || b === null) return false;
  if (typeof a !== typeof b) return false;
  if (typeof a !== 'object') return false;

  if (Array.isArray(a) !== Array.isArray(b)) return false;

  const keysA = Object.keys(a as object);
  const keysB = Object.keys(b as object);
  if (keysA.length !== keysB.length) return false;

  return keysA.every(key =>
    deepEqual((a as any)[key], (b as any)[key])
  );
}
```

### Flatten Array
```typescript
// Built-in: arr.flat(Infinity)

function flatten<T>(arr: (T | T[])[], depth = Infinity): T[] {
  if (depth === 0) return arr as T[];

  return arr.reduce<T[]>((acc, item) => {
    if (Array.isArray(item)) {
      acc.push(...flatten(item, depth - 1));
    } else {
      acc.push(item);
    }
    return acc;
  }, []);
}

// Iterative version (avoids stack overflow for very deep arrays)
function flattenIterative<T>(arr: any[]): T[] {
  const stack = [...arr];
  const result: T[] = [];

  while (stack.length) {
    const item = stack.pop()!;
    if (Array.isArray(item)) {
      stack.push(...item);  // unshift into stack
    } else {
      result.push(item);
    }
  }

  return result.reverse();
}
```

### Group By
```typescript
function groupBy<T, K extends string | number>(
  arr: T[],
  key: (item: T) => K,
): Record<K, T[]> {
  return arr.reduce((acc, item) => {
    const group = key(item);
    if (!acc[group]) acc[group] = [];
    acc[group].push(item);
    return acc;
  }, {} as Record<K, T[]>);
}

// Usage
const grouped = groupBy(users, u => u.role);
// { admin: [User, User], user: [User, User, User] }

// ES2024: Object.groupBy(arr, fn)  — now native
const grouped2 = Object.groupBy(users, u => u.role);
```

### Memoize
```typescript
function memoize<T extends (...args: any[]) => any>(fn: T): T {
  const cache = new Map<string, ReturnType<T>>();

  return function (...args: Parameters<T>): ReturnType<T> {
    const key = JSON.stringify(args);
    if (cache.has(key)) return cache.get(key)!;
    const result = fn(...args);
    cache.set(key, result);
    return result;
  } as T;
}

// Fibonacci with memoize
const fib = memoize((n: number): number => {
  if (n <= 1) return n;
  return fib(n - 1) + fib(n - 2);
});

// WeakMap version (for object keys — allows GC)
function memoizeWeak<T extends (arg: object) => any>(fn: T): T {
  const cache = new WeakMap();
  return ((arg: object) => {
    if (!cache.has(arg)) cache.set(arg, fn(arg));
    return cache.get(arg);
  }) as T;
}
```

---

## TypeScript Utilities

### Custom Type Utilities
```typescript
// Deep Partial (make all nested properties optional)
type DeepPartial<T> = {
  [P in keyof T]?: T[P] extends object ? DeepPartial<T[P]> : T[P];
};

// Deep Readonly
type DeepReadonly<T> = {
  readonly [P in keyof T]: T[P] extends object ? DeepReadonly<T[P]> : T[P];
};

// Nullable (all properties can be null)
type Nullable<T> = { [P in keyof T]: T[P] | null };

// Pick deep nested property
type PickNested<T, K1 extends keyof T, K2 extends keyof T[K1]> = T[K1][K2];

// Remove null/undefined from type
type NonNullable<T> = T extends null | undefined ? never : T;

// Extract return type of async function
type Awaited<T> = T extends Promise<infer U> ? U : T;

// Function overload union to intersection
type UnionToIntersection<U> =
  (U extends any ? (k: U) => void : never) extends (k: infer I) => void ? I : never;
```

### Runtime Validators
```typescript
// Type guard
function isUser(obj: unknown): obj is User {
  return (
    typeof obj === 'object' &&
    obj !== null &&
    'id' in obj &&
    'email' in obj &&
    typeof (obj as any).id === 'string' &&
    typeof (obj as any).email === 'string'
  );
}

// API response validator
function validateApiResponse<T>(data: unknown, validator: (d: unknown) => d is T): T {
  if (!validator(data)) {
    throw new Error('Invalid API response shape');
  }
  return data;
}
```

---

## Event Emitter (Interview Classic)
```typescript
type EventMap = Record<string, any>;
type EventCallback<T> = (data: T) => void;

class EventEmitter<Events extends EventMap = EventMap> {
  private listeners = new Map<keyof Events, Set<EventCallback<any>>>();

  on<K extends keyof Events>(event: K, callback: EventCallback<Events[K]>): this {
    if (!this.listeners.has(event)) {
      this.listeners.set(event, new Set());
    }
    this.listeners.get(event)!.add(callback);
    return this;
  }

  off<K extends keyof Events>(event: K, callback: EventCallback<Events[K]>): this {
    this.listeners.get(event)?.delete(callback);
    return this;
  }

  once<K extends keyof Events>(event: K, callback: EventCallback<Events[K]>): this {
    const wrapper = (data: Events[K]) => {
      callback(data);
      this.off(event, wrapper);
    };
    return this.on(event, wrapper);
  }

  emit<K extends keyof Events>(event: K, data: Events[K]): boolean {
    const cbs = this.listeners.get(event);
    if (!cbs || cbs.size === 0) return false;
    cbs.forEach(cb => cb(data));
    return true;
  }

  removeAllListeners(event?: keyof Events): this {
    if (event) this.listeners.delete(event);
    else this.listeners.clear();
    return this;
  }
}

// Usage with typed events
interface AppEvents {
  'user:login': { userId: string; timestamp: Date };
  'message:new': { roomId: string; content: string };
}
const emitter = new EventEmitter<AppEvents>();
emitter.on('user:login', ({ userId }) => console.log(userId));
```

---

## Observable / Reactive Pattern
```typescript
// Minimal observable (like RxJS Observable)
class Observable<T> {
  constructor(private subscriber: (observer: Observer<T>) => (() => void) | void) {}

  subscribe(observer: Partial<Observer<T>>): Subscription {
    const cleanup = this.subscriber({
      next: observer.next ?? (() => {}),
      error: observer.error ?? console.error,
      complete: observer.complete ?? (() => {}),
    });
    return { unsubscribe: cleanup ?? (() => {}) };
  }

  pipe<R>(...operators: Array<(obs: Observable<any>) => Observable<any>>): Observable<R> {
    return operators.reduce((obs, op) => op(obs), this as Observable<any>) as Observable<R>;
  }
}

interface Observer<T> {
  next: (value: T) => void;
  error: (err: unknown) => void;
  complete: () => void;
}
interface Subscription { unsubscribe: () => void; }

// Operator
function map<T, R>(transform: (value: T) => R) {
  return (source: Observable<T>): Observable<R> =>
    new Observable(observer =>
      source.subscribe({
        next: value => observer.next(transform(value)),
        error: err  => observer.error(err),
        complete: () => observer.complete(),
      }).unsubscribe
    );
}
```

---

## Common JS/TS Interview Questions

**Q: Explain the event loop.**
```
Call Stack:  Executes synchronous code. LIFO. One frame per function call.
Web APIs:    setTimeout, fetch, DOM events — run outside call stack.
Task Queue:  (Macrotasks) setTimeout callbacks, setInterval, I/O callbacks.
Microtask Queue: Promise .then, queueMicrotask, MutationObserver.

Event loop algorithm:
  1. Execute all code in call stack until empty.
  2. Process ALL microtasks (Promise .then/.catch) until queue empty.
  3. Render (browser only).
  4. Pick ONE macrotask from task queue, execute it.
  5. Go to step 2.

Order: sync → microtasks → macrotask → microtasks → macrotask → ...

console.log('1');           // sync
setTimeout(() => console.log('2'), 0); // macrotask
Promise.resolve().then(() => console.log('3')); // microtask
console.log('4');           // sync
// Output: 1, 4, 3, 2
```

**Q: What is the difference between null, undefined, and undeclared?**
```typescript
undeclared:  Variable never declared. Accessing throws ReferenceError.
undefined:   Variable declared but not assigned a value.
             typeof undefined === 'undefined'
             function with no return → undefined
null:        Intentional absence of value. Explicitly set.
             typeof null === 'object' (historical bug in JS)

null == undefined  // true (loose equality)
null === undefined // false (strict equality)

Use:
  undefined: framework/runtime uses it (optional params, missing returns)
  null:      you explicitly indicate "no value" (user has no avatar: null)
```

**Q: Explain closures with a practical example.**
```typescript
// Closure: function that retains access to its lexical scope after outer function returns

function createCounter(start = 0) {
  let count = start; // enclosed variable

  return {
    increment: () => ++count,
    decrement: () => --count,
    getCount:  () => count,
    reset:     () => { count = start; },
  };
}

const counter = createCounter(10);
counter.increment(); // 11
counter.increment(); // 12
counter.decrement(); // 11
counter.getCount();  // 11

// Classic closure bug:
for (var i = 0; i < 3; i++) {
  setTimeout(() => console.log(i), 0); // prints 3,3,3 (not 0,1,2)
}
// Fix: use let (block scope), or IIFE, or bind
for (let i = 0; i < 3; i++) {
  setTimeout(() => console.log(i), 0); // prints 0,1,2
}
```

**Q: What is prototype chain and how does it work?**
```typescript
// Every object has __proto__ pointing to its prototype
// Property lookup traverses the chain until null

function Animal(name: string) { this.name = name; }
Animal.prototype.speak = function() { return `${this.name} makes a sound`; };

function Dog(name: string) { Animal.call(this, name); }
Dog.prototype = Object.create(Animal.prototype);
Dog.prototype.constructor = Dog;
Dog.prototype.bark = function() { return 'Woof!'; };

const dog = new Dog('Rex');
dog.bark();         // found on Dog.prototype
dog.speak();        // found on Animal.prototype (chain traversal)
dog.toString();     // found on Object.prototype

// ES6 class is syntactic sugar over this prototype system
class Dog extends Animal {
  bark() { return 'Woof!'; }
}
```

**Q: Implement Function.prototype.bind from scratch.**
```typescript
Function.prototype.myBind = function(context: any, ...args: any[]) {
  const originalFn = this;

  return function (...callArgs: any[]) {
    // Handle 'new' keyword usage with bound function
    if (new.target) {
      return new (originalFn as any)(...args, ...callArgs);
    }
    return originalFn.apply(context, [...args, ...callArgs]);
  };
};
```
