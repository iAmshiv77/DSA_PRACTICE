# Machine Coding: MERN Full-Stack

## Task 1: Blog Platform (MERN)

### MongoDB Schema (Mongoose)
```javascript
// models/Post.js
const postSchema = new mongoose.Schema({
  title:    { type: String, required: true, trim: true, maxlength: 200 },
  slug:     { type: String, unique: true, lowercase: true },
  content:  { type: String, required: true },
  excerpt:  { type: String, maxlength: 500 },
  author:   { type: mongoose.Schema.Types.ObjectId, ref: 'User', required: true },
  tags:     [{ type: String, lowercase: true }],
  status:   { type: String, enum: ['draft', 'published'], default: 'draft' },
  views:    { type: Number, default: 0 },
  likes:    [{ type: mongoose.Schema.Types.ObjectId, ref: 'User' }],
  comments: [{
    author:    { type: mongoose.Schema.Types.ObjectId, ref: 'User' },
    content:   String,
    createdAt: { type: Date, default: Date.now },
  }],
}, {
  timestamps: true,  // adds createdAt, updatedAt
});

// Auto-generate slug from title
postSchema.pre('save', function(next) {
  if (this.isModified('title')) {
    this.slug = this.title
      .toLowerCase()
      .replace(/[^a-z0-9]+/g, '-')
      .replace(/^-|-$/g, '');
  }
  next();
});

// Virtual: comment count
postSchema.virtual('commentCount').get(function() {
  return this.comments.length;
});

// Index for common queries
postSchema.index({ author: 1, status: 1 });
postSchema.index({ tags: 1 });
postSchema.index({ slug: 1 }, { unique: true });
postSchema.index({ title: 'text', content: 'text' }); // full-text search
```

### Express API
```javascript
// routes/posts.js
const router = express.Router();

// GET /api/posts?page=1&limit=10&tag=nodejs&author=userId&search=term
router.get('/', async (req, res) => {
  try {
    const { page = 1, limit = 10, tag, author, search, status = 'published' } = req.query;

    const filter = { status };
    if (tag)    filter.tags = tag;
    if (author) filter.author = author;
    if (search) filter.$text = { $search: search };

    const [posts, total] = await Promise.all([
      Post.find(filter)
        .populate('author', 'name avatar')   // only name and avatar, not password
        .select('-content')                   // exclude heavy content in list
        .sort({ createdAt: -1 })
        .skip((page - 1) * limit)
        .limit(Number(limit))
        .lean(),                              // plain JS object, faster
      Post.countDocuments(filter),
    ]);

    res.json({
      data: posts,
      pagination: {
        page: Number(page),
        limit: Number(limit),
        total,
        lastPage: Math.ceil(total / limit),
      },
    });
  } catch (err) {
    res.status(500).json({ message: err.message });
  }
});

// POST /api/posts/:id/like (toggle like)
router.post('/:id/like', authenticate, async (req, res) => {
  const post = await Post.findById(req.params.id);
  if (!post) return res.status(404).json({ message: 'Not found' });

  const userId = req.user._id;
  const likeIndex = post.likes.indexOf(userId);

  if (likeIndex === -1) {
    post.likes.push(userId);    // add like
  } else {
    post.likes.splice(likeIndex, 1); // remove like
  }

  await post.save();
  res.json({ likes: post.likes.length, liked: likeIndex === -1 });
});

// MongoDB aggregation — top authors by post count
router.get('/stats/top-authors', async (req, res) => {
  const stats = await Post.aggregate([
    { $match: { status: 'published' } },
    { $group: {
      _id: '$author',
      postCount: { $sum: 1 },
      totalViews: { $sum: '$views' },
      totalLikes: { $sum: { $size: '$likes' } },
    }},
    { $sort: { postCount: -1 } },
    { $limit: 10 },
    { $lookup: {
      from: 'users',
      localField: '_id',
      foreignField: '_id',
      as: 'author',
    }},
    { $unwind: '$author' },
    { $project: {
      'author.name': 1,
      'author.avatar': 1,
      postCount: 1,
      totalViews: 1,
      totalLikes: 1,
    }},
  ]);

  res.json(stats);
});
```

### React Frontend
```tsx
// hooks/usePosts.ts
function usePosts(filters: PostFilters) {
  const [state, setState] = useState<{
    posts: Post[];
    pagination: Pagination | null;
    loading: boolean;
    error: string | null;
  }>({ posts: [], pagination: null, loading: true, error: null });

  const debouncedSearch = useDebounce(filters.search, 300);

  useEffect(() => {
    const controller = new AbortController();
    setState(prev => ({ ...prev, loading: true }));

    const params = new URLSearchParams({
      page: String(filters.page),
      limit: String(filters.limit),
      ...(debouncedSearch && { search: debouncedSearch }),
      ...(filters.tag && { tag: filters.tag }),
    });

    fetch(`/api/posts?${params}`, { signal: controller.signal })
      .then(r => r.json())
      .then(data => setState({ posts: data.data, pagination: data.pagination, loading: false, error: null }))
      .catch(err => {
        if (err.name !== 'AbortError') {
          setState(prev => ({ ...prev, loading: false, error: err.message }));
        }
      });

    return () => controller.abort();
  }, [filters.page, filters.limit, debouncedSearch, filters.tag]);

  return state;
}
```

---

## Task 2: Real-Time Notifications (Socket.io + Express + React)

### Server Setup
```javascript
// server.js
const app = express();
const httpServer = createServer(app);
const io = new Server(httpServer, {
  cors: { origin: process.env.CLIENT_URL, credentials: true },
});

// Auth middleware for socket
io.use(async (socket, next) => {
  try {
    const token = socket.handshake.auth.token;
    const payload = jwt.verify(token, process.env.JWT_SECRET);
    socket.userId = payload.userId;
    next();
  } catch {
    next(new Error('Unauthorized'));
  }
});

// User → socket mapping (in prod use Redis adapter for multi-server)
const userSocketMap = new Map<string, string>(); // userId → socketId

io.on('connection', (socket) => {
  userSocketMap.set(socket.userId, socket.id);
  socket.join(`user:${socket.userId}`); // personal room

  socket.on('disconnect', () => {
    userSocketMap.delete(socket.userId);
  });
});

// Send notification from anywhere:
function sendNotification(userId: string, notification: Notification) {
  io.to(`user:${userId}`).emit('notification', notification);
}

// POST /api/posts/:id/comment → notify post author
router.post('/:id/comment', authenticate, async (req, res) => {
  const post = await Post.findById(req.params.id);
  const comment = { author: req.user._id, content: req.body.content };
  post.comments.push(comment);
  await post.save();

  // Notify author (if not commenting on own post)
  if (post.author.toString() !== req.user._id.toString()) {
    const notification = await Notification.create({
      recipient: post.author,
      type: 'comment',
      message: `${req.user.name} commented on your post`,
      link: `/posts/${post.slug}`,
    });
    sendNotification(post.author.toString(), notification);
  }

  res.status(201).json(comment);
});
```

### React Socket.io Client
```tsx
// hooks/useSocket.ts
import { io, Socket } from 'socket.io-client';

let socket: Socket | null = null;

export function useSocket(token: string | null) {
  const [connected, setConnected] = useState(false);

  useEffect(() => {
    if (!token) return;

    socket = io(process.env.REACT_APP_SERVER_URL!, {
      auth: { token },
      reconnectionAttempts: 5,
      reconnectionDelay: 1000,
    });

    socket.on('connect', () => setConnected(true));
    socket.on('disconnect', () => setConnected(false));

    return () => { socket?.disconnect(); socket = null; };
  }, [token]);

  return { socket, connected };
}

// NotificationBell.tsx
function NotificationBell() {
  const { token } = useAuth();
  const { socket } = useSocket(token);
  const [notifications, setNotifications] = useState<Notification[]>([]);
  const [unreadCount, setUnreadCount] = useState(0);

  useEffect(() => {
    if (!socket) return;

    socket.on('notification', (notification: Notification) => {
      setNotifications(prev => [notification, ...prev]);
      setUnreadCount(c => c + 1);

      // Browser notification (if permission granted)
      if (Notification.permission === 'granted') {
        new Notification(notification.message);
      }
    });

    return () => { socket.off('notification'); };
  }, [socket]);

  const markAllRead = async () => {
    await api.markNotificationsRead();
    setUnreadCount(0);
    setNotifications(prev => prev.map(n => ({ ...n, read: true })));
  };

  return (
    <div className="relative">
      <button onClick={markAllRead}>
        🔔 {unreadCount > 0 && <span className="badge">{unreadCount}</span>}
      </button>
      <NotificationDropdown notifications={notifications} />
    </div>
  );
}
```

---

## Task 3: File Upload with Progress (React + Express + Multer)

### Express Multer Setup
```javascript
// middleware/upload.js
const multer = require('multer');
const path = require('path');

const storage = multer.diskStorage({
  destination: (req, file, cb) => {
    cb(null, 'uploads/');
  },
  filename: (req, file, cb) => {
    const unique = `${Date.now()}-${Math.random().toString(36).substr(2, 9)}`;
    cb(null, unique + path.extname(file.originalname));
  },
});

const fileFilter = (req, file, cb) => {
  const allowed = ['image/jpeg', 'image/png', 'image/webp', 'application/pdf'];
  if (allowed.includes(file.mimetype)) {
    cb(null, true);
  } else {
    cb(new Error(`File type ${file.mimetype} not allowed`), false);
  }
};

module.exports = multer({
  storage,
  fileFilter,
  limits: { fileSize: 10 * 1024 * 1024 }, // 10MB
});

// Route
router.post('/upload', authenticate, upload.single('file'), async (req, res) => {
  if (!req.file) return res.status(400).json({ message: 'No file uploaded' });

  const fileRecord = await File.create({
    originalName: req.file.originalname,
    filename:     req.file.filename,
    path:         req.file.path,
    mimetype:     req.file.mimetype,
    size:         req.file.size,
    uploadedBy:   req.user._id,
  });

  res.json({ url: `/uploads/${req.file.filename}`, id: fileRecord._id });
});

// Multer error handler
router.use((err, req, res, next) => {
  if (err instanceof multer.MulterError) {
    if (err.code === 'LIMIT_FILE_SIZE') {
      return res.status(400).json({ message: 'File too large (max 10MB)' });
    }
  }
  res.status(400).json({ message: err.message });
});
```

### React Upload with Progress
```tsx
function FileUploader() {
  const [files, setFiles] = useState<UploadFile[]>([]);
  const [dragOver, setDragOver] = useState(false);

  const uploadFile = async (file: File) => {
    const id = crypto.randomUUID();
    setFiles(prev => [...prev, { id, name: file.name, progress: 0, status: 'uploading' }]);

    const formData = new FormData();
    formData.append('file', file);

    try {
      await new Promise<void>((resolve, reject) => {
        const xhr = new XMLHttpRequest();

        xhr.upload.addEventListener('progress', (e) => {
          if (e.lengthComputable) {
            const progress = Math.round((e.loaded / e.total) * 100);
            setFiles(prev =>
              prev.map(f => f.id === id ? { ...f, progress } : f)
            );
          }
        });

        xhr.addEventListener('load', () => {
          if (xhr.status >= 200 && xhr.status < 300) {
            const { url } = JSON.parse(xhr.responseText);
            setFiles(prev =>
              prev.map(f => f.id === id ? { ...f, status: 'done', url, progress: 100 } : f)
            );
            resolve();
          } else {
            reject(new Error('Upload failed'));
          }
        });

        xhr.addEventListener('error', () => reject(new Error('Network error')));

        xhr.open('POST', '/api/upload');
        xhr.setRequestHeader('Authorization', `Bearer ${getToken()}`);
        xhr.send(formData);
      });
    } catch (err) {
      setFiles(prev =>
        prev.map(f => f.id === id ? { ...f, status: 'error', error: err.message } : f)
      );
    }
  };

  const handleDrop = (e: React.DragEvent) => {
    e.preventDefault();
    setDragOver(false);
    Array.from(e.dataTransfer.files).forEach(uploadFile);
  };

  return (
    <div
      className={`drop-zone ${dragOver ? 'drag-over' : ''}`}
      onDragOver={e => { e.preventDefault(); setDragOver(true); }}
      onDragLeave={() => setDragOver(false)}
      onDrop={handleDrop}
    >
      <input
        type="file"
        multiple
        onChange={e => Array.from(e.target.files ?? []).forEach(uploadFile)}
      />
      <p>Drag files here or click to upload</p>

      {files.map(file => (
        <div key={file.id} className="file-item">
          <span>{file.name}</span>
          {file.status === 'uploading' && (
            <progress value={file.progress} max={100} />
          )}
          {file.status === 'done' && <span>✓</span>}
          {file.status === 'error' && <span className="error">{file.error}</span>}
        </div>
      ))}
    </div>
  );
}
```

---

## MongoDB Aggregation Pipeline — Cheat Sheet

```javascript
// Common pipeline stages:
[
  // 1. Filter (like WHERE)
  { $match: { status: 'active', createdAt: { $gte: startDate } } },

  // 2. Join (like LEFT JOIN)
  { $lookup: {
    from: 'users',
    localField: 'userId',
    foreignField: '_id',
    as: 'user',
    pipeline: [{ $project: { name: 1, email: 1 } }], // sub-pipeline
  }},

  // 3. Flatten array from $lookup
  { $unwind: '$user' },  // { preserveNullAndEmptyArrays: true } for LEFT JOIN

  // 4. Group (like GROUP BY + aggregation)
  { $group: {
    _id: '$category',
    count: { $sum: 1 },
    totalRevenue: { $sum: '$amount' },
    avgAmount: { $avg: '$amount' },
    maxAmount: { $max: '$amount' },
    items: { $push: '$$ROOT' },  // collect all docs
    uniqueUsers: { $addToSet: '$userId' },
  }},

  // 5. Sort
  { $sort: { totalRevenue: -1 } },

  // 6. Limit + Skip (pagination)
  { $skip: (page - 1) * limit },
  { $limit: limit },

  // 7. Shape output
  { $project: {
    category: '$_id',
    count: 1,
    totalRevenue: 1,
    _id: 0,
  }},

  // 8. Add computed fields
  { $addFields: {
    fullName: { $concat: ['$firstName', ' ', '$lastName'] },
    isHighValue: { $gte: ['$amount', 1000] },
    year: { $year: '$createdAt' },
  }},

  // 9. Facet — multiple aggregations in one query
  { $facet: {
    'byCategory': [{ $group: { _id: '$category', count: { $sum: 1 } } }],
    'byMonth':    [{ $group: { _id: { $month: '$createdAt' }, count: { $sum: 1 } } }],
    'total':      [{ $count: 'count' }],
  }},
]
```

---

## Common MERN Interview Questions

**Q: How do you prevent N+1 queries in Mongoose?**
```javascript
// N+1 PROBLEM:
const posts = await Post.find();
for (const post of posts) {
  post.authorName = (await User.findById(post.author)).name; // N queries!
}

// FIX: populate
const posts = await Post.find().populate('author', 'name email');

// Or: aggregation with $lookup
// Or: dataloader pattern (batch + cache per request)
```

**Q: How does mongoose populate work internally?**
```
Populate does NOT use SQL JOIN. It runs two queries:
  1. SELECT * FROM posts WHERE ...
  2. SELECT * FROM users WHERE _id IN [authorId1, authorId2, ...]
Then merges in memory. That's why it's not atomic (possible inconsistency).
Use aggregation $lookup if you need a single atomic operation.
```

**Q: How do you handle transactions in MongoDB?**
```javascript
// Multi-document transactions (requires replica set)
const session = await mongoose.startSession();
session.startTransaction();

try {
  await Order.create([{ items, total }], { session });
  await Inventory.updateMany(
    { _id: { $in: itemIds } },
    { $inc: { stock: -1 } },
    { session }
  );
  await session.commitTransaction();
} catch (err) {
  await session.abortTransaction();
  throw err;
} finally {
  session.endSession();
}
```

**Q: What is the difference between .lean() and regular Mongoose queries?**
```
Regular query: Returns Mongoose Document (with methods: .save(), .toJSON(), virtual fields)
              Slower — instantiates full Mongoose object
.lean():       Returns plain JavaScript object
              2-10x faster, lower memory
              Use for read-only operations (API responses)
              Cannot call .save() on lean results
```

**Q: How do you implement optimistic concurrency in MongoDB?**
```javascript
// Add version field
const schema = new Schema({ ... }, { optimisticConcurrency: true });
// Mongoose auto-adds __v field
// On save: WHERE _id = X AND __v = currentVersion
// If another update incremented __v → VersionError thrown → retry

// Manual pattern:
const doc = await Item.findById(id);
doc.quantity -= orderQuantity;
try {
  await Item.updateOne(
    { _id: id, version: doc.version },  // optimistic lock
    { $inc: { quantity: -orderQuantity, version: 1 } }
  );
} catch {
  // Someone else updated — retry logic
}
```
