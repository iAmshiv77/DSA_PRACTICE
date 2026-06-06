# Machine Coding: NestJS / Node.js Backend

## Common Interview Tasks

### Task 1: Build a Todo API (CRUD + Auth)
**Requirements:**
- Register/Login with JWT
- CRUD todos (user can only see their own)
- Filter by status (pending/completed)
- Pagination

**NestJS Structure:**
```
src/
├── auth/
│   ├── auth.controller.ts
│   ├── auth.service.ts
│   ├── auth.module.ts
│   ├── strategies/
│   │   └── jwt.strategy.ts
│   └── dto/
│       ├── register.dto.ts
│       └── login.dto.ts
├── todos/
│   ├── todos.controller.ts
│   ├── todos.service.ts
│   ├── todos.module.ts
│   ├── entities/
│   │   └── todo.entity.ts
│   └── dto/
│       ├── create-todo.dto.ts
│       └── update-todo.dto.ts
└── common/
    ├── filters/
    │   └── http-exception.filter.ts
    └── interceptors/
        └── transform.interceptor.ts
```

**Entity:**
```typescript
// todo.entity.ts
import { Entity, PrimaryGeneratedColumn, Column,
         CreateDateColumn, UpdateDateColumn, ManyToOne } from 'typeorm';
import { User } from '../../users/entities/user.entity';

export enum TodoStatus {
  PENDING = 'pending',
  IN_PROGRESS = 'in_progress',
  COMPLETED = 'completed',
}

@Entity('todos')
export class Todo {
  @PrimaryGeneratedColumn()
  id: number;

  @Column()
  title: string;

  @Column({ type: 'text', nullable: true })
  description: string;

  @Column({ type: 'enum', enum: TodoStatus, default: TodoStatus.PENDING })
  status: TodoStatus;

  @ManyToOne(() => User, user => user.todos, { onDelete: 'CASCADE' })
  user: User;

  @Column()
  userId: number;

  @CreateDateColumn()
  createdAt: Date;

  @UpdateDateColumn()
  updatedAt: Date;
}
```

**Service:**
```typescript
// todos.service.ts
@Injectable()
export class TodosService {
  constructor(
    @InjectRepository(Todo)
    private readonly todoRepo: Repository<Todo>,
  ) {}

  async findAll(
    userId: number,
    status?: TodoStatus,
    page = 1,
    limit = 10,
  ): Promise<{ data: Todo[]; total: number; page: number; lastPage: number }> {
    const query = this.todoRepo.createQueryBuilder('todo')
      .where('todo.userId = :userId', { userId });

    if (status) {
      query.andWhere('todo.status = :status', { status });
    }

    query.orderBy('todo.createdAt', 'DESC')
         .skip((page - 1) * limit)
         .take(limit);

    const [data, total] = await query.getManyAndCount();

    return {
      data,
      total,
      page,
      lastPage: Math.ceil(total / limit),
    };
  }

  async findOne(id: number, userId: number): Promise<Todo> {
    const todo = await this.todoRepo.findOne({ where: { id, userId } });
    if (!todo) throw new NotFoundException(`Todo #${id} not found`);
    return todo;
  }

  async create(userId: number, dto: CreateTodoDto): Promise<Todo> {
    const todo = this.todoRepo.create({ ...dto, userId });
    return this.todoRepo.save(todo);
  }

  async update(id: number, userId: number, dto: UpdateTodoDto): Promise<Todo> {
    const todo = await this.findOne(id, userId);
    Object.assign(todo, dto);
    return this.todoRepo.save(todo);
  }

  async remove(id: number, userId: number): Promise<void> {
    const todo = await this.findOne(id, userId);
    await this.todoRepo.remove(todo);
  }
}
```

**Controller:**
```typescript
// todos.controller.ts
@Controller('todos')
@UseGuards(AuthGuard('jwt'))
export class TodosController {
  constructor(private readonly todosService: TodosService) {}

  @Get()
  findAll(
    @CurrentUser() user: User,
    @Query('status') status?: TodoStatus,
    @Query('page', new DefaultValuePipe(1), ParseIntPipe) page?: number,
    @Query('limit', new DefaultValuePipe(10), ParseIntPipe) limit?: number,
  ) {
    return this.todosService.findAll(user.id, status, page, limit);
  }

  @Get(':id')
  findOne(@Param('id', ParseIntPipe) id: number, @CurrentUser() user: User) {
    return this.todosService.findOne(id, user.id);
  }

  @Post()
  @HttpCode(201)
  create(@Body() dto: CreateTodoDto, @CurrentUser() user: User) {
    return this.todosService.create(user.id, dto);
  }

  @Patch(':id')
  update(
    @Param('id', ParseIntPipe) id: number,
    @Body() dto: UpdateTodoDto,
    @CurrentUser() user: User,
  ) {
    return this.todosService.update(id, user.id, dto);
  }

  @Delete(':id')
  @HttpCode(204)
  remove(@Param('id', ParseIntPipe) id: number, @CurrentUser() user: User) {
    return this.todosService.remove(id, user.id);
  }
}
```

---

### Task 2: Real-Time Chat with WebSocket
```typescript
// chat.gateway.ts
@WebSocketGateway({ cors: true })
export class ChatGateway implements OnGatewayConnection, OnGatewayDisconnect {
  @WebSocketServer() server: Server;
  private activeUsers = new Map<string, string>(); // socketId → userId

  async handleConnection(client: Socket) {
    const userId = client.handshake.auth.userId;
    this.activeUsers.set(client.id, userId);
    client.broadcast.emit('user:online', { userId });
  }

  handleDisconnect(client: Socket) {
    const userId = this.activeUsers.get(client.id);
    this.activeUsers.delete(client.id);
    this.server.emit('user:offline', { userId });
  }

  @SubscribeMessage('join:room')
  handleJoin(@ConnectedSocket() client: Socket, @MessageBody() roomId: string) {
    client.join(roomId);
    return { event: 'joined', data: roomId };
  }

  @SubscribeMessage('message:send')
  async handleMessage(
    @ConnectedSocket() client: Socket,
    @MessageBody() payload: { roomId: string; content: string },
  ) {
    const userId = this.activeUsers.get(client.id);
    const message = await this.chatService.saveMessage({
      userId,
      roomId: payload.roomId,
      content: payload.content,
    });
    // Broadcast to room (including sender)
    this.server.to(payload.roomId).emit('message:new', message);
  }
}
```

---

### Task 3: File Upload API (S3)
```typescript
// upload.service.ts
@Injectable()
export class UploadService {
  private s3: S3Client;

  constructor(private configService: ConfigService) {
    this.s3 = new S3Client({
      region: this.configService.get('AWS_REGION'),
      credentials: {
        accessKeyId: this.configService.get('AWS_ACCESS_KEY'),
        secretAccessKey: this.configService.get('AWS_SECRET_KEY'),
      },
    });
  }

  async uploadFile(
    file: Express.Multer.File,
    folder: string,
  ): Promise<string> {
    const ext = extname(file.originalname);
    const key = `${folder}/${uuidv4()}${ext}`;

    await this.s3.send(new PutObjectCommand({
      Bucket: this.configService.get('S3_BUCKET'),
      Key: key,
      Body: file.buffer,
      ContentType: file.mimetype,
      ACL: 'private',
    }));

    return `https://${this.configService.get('S3_BUCKET')}.s3.amazonaws.com/${key}`;
  }

  async getSignedUrl(key: string): Promise<string> {
    const command = new GetObjectCommand({
      Bucket: this.configService.get('S3_BUCKET'),
      Key: key,
    });
    return getSignedUrl(this.s3, command, { expiresIn: 3600 });
  }
}

// Controller
@Post('upload')
@UseInterceptors(FileInterceptor('file'))
async uploadFile(
  @UploadedFile(new ParseFilePipe({
    validators: [
      new MaxFileSizeValidator({ maxSize: 5 * 1024 * 1024 }), // 5MB
      new FileTypeValidator({ fileType: /image\/(jpeg|png|webp)/ }),
    ],
  })) file: Express.Multer.File,
) {
  const url = await this.uploadService.uploadFile(file, 'avatars');
  return { url };
}
```

---

### Task 4: Background Job with BullMQ
```typescript
// email.processor.ts
@Processor('email')
export class EmailProcessor extends WorkerHost {
  private readonly logger = new Logger(EmailProcessor.name);

  async process(job: Job<EmailJobData>): Promise<void> {
    this.logger.log(`Processing email job ${job.id}: ${job.name}`);

    try {
      switch (job.name) {
        case 'welcome':
          await this.sendWelcomeEmail(job.data);
          break;
        case 'password-reset':
          await this.sendPasswordResetEmail(job.data);
          break;
        default:
          throw new Error(`Unknown job type: ${job.name}`);
      }
    } catch (error) {
      this.logger.error(`Job ${job.id} failed: ${error.message}`);
      throw error; // BullMQ will retry
    }
  }

  private async sendWelcomeEmail(data: WelcomeEmailData) {
    // send via nodemailer/zeptomail/sendgrid
  }
}

// Queue Producer
@Injectable()
export class EmailService {
  constructor(@InjectQueue('email') private emailQueue: Queue) {}

  async queueWelcomeEmail(userId: string, email: string): Promise<void> {
    await this.emailQueue.add('welcome', { userId, email }, {
      attempts: 3,
      backoff: { type: 'exponential', delay: 5000 },
      removeOnComplete: 100,
      removeOnFail: 200,
    });
  }
}
```

---

### Task 5: Rate Limiting Middleware (NestJS)
```typescript
// rate-limit.guard.ts
@Injectable()
export class RateLimitGuard implements CanActivate {
  constructor(private redis: Redis) {}

  async canActivate(context: ExecutionContext): Promise<boolean> {
    const request = context.switchToHttp().getRequest();
    const key = `rate_limit:${request.ip}:${request.path}`;
    const limit = 100;
    const window = 60; // seconds

    const current = await this.redis.incr(key);
    if (current === 1) await this.redis.expire(key, window);

    if (current > limit) {
      const ttl = await this.redis.ttl(key);
      throw new HttpException(
        { message: 'Too many requests', retryAfter: ttl },
        HttpStatus.TOO_MANY_REQUESTS,
      );
    }

    const response = context.switchToHttp().getResponse();
    response.setHeader('X-RateLimit-Limit', limit);
    response.setHeader('X-RateLimit-Remaining', Math.max(0, limit - current));

    return true;
  }
}
```

---

## Common NestJS Interview Questions

**Q: What is the difference between Pipe, Guard, Interceptor, and Middleware in NestJS?**
```
Middleware:    Runs before route handler. No access to execution context. Use for logging, auth token extraction.
Guard:         Decides if request should proceed. Has execution context. Use for authentication + authorization.
Pipe:          Transforms/validates request data. Use for DTO validation, type conversion.
Interceptor:   Wraps handler. Can modify request/response. Use for logging, caching, response transformation.

Order: Middleware → Guard → Interceptor (before) → Pipe → Handler → Interceptor (after) → Filter (if error)
```

**Q: How do you handle circular dependencies in NestJS?**
```typescript
// Use forwardRef
@Injectable()
export class AuthService {
  constructor(
    @Inject(forwardRef(() => UsersService))
    private usersService: UsersService,
  ) {}
}
// Better: restructure to avoid circular dependency (usually indicates design issue)
```

**Q: How do you implement soft delete?**
```typescript
@Entity()
export class Post {
  @DeleteDateColumn()
  deletedAt: Date | null;
}

// TypeORM automatically adds WHERE deletedAt IS NULL to all queries
// Soft delete:
await this.postRepo.softDelete(id);
// Hard delete:
await this.postRepo.delete(id);
// Find including deleted:
await this.postRepo.find({ withDeleted: true });
```

**Q: How do you implement database transactions in NestJS + TypeORM?**
```typescript
async transferFunds(fromId: number, toId: number, amount: number) {
  return this.dataSource.transaction(async (manager) => {
    const from = await manager.findOne(Account, { where: { id: fromId }, lock: { mode: 'pessimistic_write' } });
    const to = await manager.findOne(Account, { where: { id: toId }, lock: { mode: 'pessimistic_write' } });

    if (from.balance < amount) throw new BadRequestException('Insufficient funds');

    from.balance -= amount;
    to.balance += amount;

    await manager.save(from);
    await manager.save(to);
    // If any error → automatic rollback
  });
}
```
