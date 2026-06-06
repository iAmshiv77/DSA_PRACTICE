# AWS Cloud

## EC2 — Elastic Compute Cloud

### Instance Type Families

| Family | Use Case | Examples |
|---|---|---|
| General Purpose | Web servers, app servers, dev environments | t3, t4g, m6i, m7g |
| Compute Optimized | Batch processing, ML inference, game servers | c6i, c7g, c6a |
| Memory Optimized | In-memory databases, large caches, SAP HANA | r6i, r7g, x2iedn |
| Storage Optimized | Data warehousing, HDFS, high IOPS needs | i3en, d3en, h1 |
| Accelerated Computing | ML training, graphics rendering | p4d, g5, inf2 |

T-series are burstable — they earn CPU credits over time and spend them during spikes. Good for variable workloads, bad for sustained high CPU.

M and C series have dedicated CPU — predictable performance, required for production app servers under consistent load.

### Auto Scaling Groups (ASG)

An ASG manages a fleet of EC2 instances, replacing unhealthy ones and scaling based on demand.

Key concepts:
- **Launch Template**: defines the instance configuration (AMI, instance type, security groups, user data, IAM role)
- **Desired capacity**: current target number of instances
- **Min/Max capacity**: hard limits on scaling
- **Scaling policies**: rules that change desired capacity

```
                    ┌────────────────────────────────┐
                    │         Auto Scaling Group      │
                    │  Min: 2  Desired: 4  Max: 10   │
                    │                                  │
                    │  [EC2] [EC2] [EC2] [EC2]        │
                    └────────────┬───────────────────-┘
                                 │ registered targets
                    ┌────────────▼────────────┐
                    │    Application Load      │
                    │       Balancer           │
                    └─────────────────────────┘
```

Scaling policy types:
- **Target Tracking**: keep a metric at a target value (e.g., keep average CPU at 60%)
- **Step Scaling**: add/remove specific counts based on CloudWatch alarm thresholds
- **Scheduled Scaling**: scale up before known traffic spikes (e.g., school start time every morning)

```json
{
  "PolicyType": "TargetTrackingScaling",
  "TargetTrackingConfiguration": {
    "PredefinedMetricSpecification": {
      "PredefinedMetricType": "ASGAverageCPUUtilization"
    },
    "TargetValue": 60.0,
    "ScaleInCooldown": 300,
    "ScaleOutCooldown": 60
  }
}
```

Scale-out cooldown is short (respond fast to load spikes). Scale-in cooldown is long (avoid oscillation).

### EC2 Pricing Models

- **On-Demand**: pay per second, no commitment. Most expensive. Use for unpredictable workloads.
- **Reserved Instances (RI)**: 1 or 3 year commitment. Up to 72% discount. Use for stable baseline load.
- **Savings Plans**: same discount as RIs but flexible across instance types/regions. Preferred over RIs.
- **Spot Instances**: spare AWS capacity, up to 90% discount. Can be interrupted with 2-minute notice. Use for batch jobs, ML training, stateless workers.
- **Dedicated Hosts**: physical server dedicated to you. Required for BYOL (Bring Your Own License) software like Windows Server, Oracle DB.

Production pattern: 70% Reserved/Savings Plans for baseline + 30% On-Demand for peak + Spot for batch workers.

---

## S3 — Simple Storage Service

### Storage Classes

| Class | Use Case | Retrieval Time | Relative Cost |
|---|---|---|---|
| S3 Standard | Frequently accessed data | Milliseconds | $$$ |
| S3 Intelligent-Tiering | Unknown or changing access patterns | Milliseconds | $$$ + monitoring fee |
| S3 Standard-IA | Infrequent access, important data | Milliseconds | $$ |
| S3 One Zone-IA | Infrequent, non-critical data | Milliseconds | $ |
| S3 Glacier Instant Retrieval | Archives accessed once per quarter | Milliseconds | $ |
| S3 Glacier Flexible | Archives, compliance | Minutes to hours | cents |
| S3 Glacier Deep Archive | 7-10 year retention, rarely accessed | 12 hours | cheapest |

### Lifecycle Policies

Automatically transition objects between storage classes based on age:

```json
{
  "Rules": [
    {
      "ID": "archive-old-logs",
      "Status": "Enabled",
      "Filter": { "Prefix": "logs/" },
      "Transitions": [
        { "Days": 30,  "StorageClass": "STANDARD_IA" },
        { "Days": 90,  "StorageClass": "GLACIER_IR" },
        { "Days": 365, "StorageClass": "DEEP_ARCHIVE" }
      ],
      "Expiration": { "Days": 2555 }
    }
  ]
}
```

### Presigned URLs

Temporary URLs that grant access to private S3 objects without exposing AWS credentials. The server generates the URL and sends it to the client. The client uses it directly — no traffic through your server.

```typescript
// NestJS service
import { GetObjectCommand, PutObjectCommand, S3Client } from '@aws-sdk/client-s3';
import { getSignedUrl } from '@aws-sdk/s3-request-presigner';

@Injectable()
export class StorageService {
  private readonly logger = new Logger(StorageService.name);
  private readonly s3: S3Client;
  private readonly bucket: string;
  private readonly uploadUrlExpiry: number;

  constructor(private readonly config: ConfigService) {
    this.bucket = config.get<string>('AWS_S3_BUCKET');
    this.uploadUrlExpiry = config.get<number>('S3_UPLOAD_URL_EXPIRY_SEC') ?? 300;
    this.s3 = new S3Client({ region: config.get<string>('AWS_REGION') });
  }

  async getPresignedDownloadUrl(key: string): Promise<string> {
    try {
      const command = new GetObjectCommand({ Bucket: this.bucket, Key: key });
      return getSignedUrl(this.s3, command, { expiresIn: 3600 });
    } catch (error) {
      handleError(error, {
        logger: this.logger,
        contextMessage: `getPresignedDownloadUrl failed for key ${key}`,
      });
    }
  }

  async getPresignedUploadUrl(key: string, contentType: string): Promise<string> {
    try {
      const command = new PutObjectCommand({
        Bucket: this.bucket,
        Key: key,
        ContentType: contentType,
      });
      return getSignedUrl(this.s3, command, { expiresIn: this.uploadUrlExpiry });
    } catch (error) {
      handleError(error, {
        logger: this.logger,
        contextMessage: `getPresignedUploadUrl failed for key ${key}`,
      });
    }
  }
}
```

The client uploads directly to S3, not through your NestJS API — this keeps your API server from becoming a bottleneck for file uploads.

### S3 Key Design for Performance

S3 automatically partitions based on key prefix. For high-throughput buckets, randomize prefixes:

```
# Bad — all requests go to same partition
uploads/2024/01/01/file1.jpg
uploads/2024/01/01/file2.jpg

# Good — spread across partitions
uploads/a8f3/2024/01/01/file1.jpg   (a8f3 = hash prefix)
uploads/c2e1/2024/01/01/file2.jpg
```

---

## RDS — Relational Database Service

### Multi-AZ Deployment

Primary DB instance in one AZ, synchronous standby replica in a different AZ. On primary failure, RDS automatically fails over to standby. Failover takes 60-120 seconds (DNS update).

```
    AZ-a                        AZ-b
┌──────────────┐          ┌──────────────┐
│   Primary    │ ──sync── │   Standby    │
│  (read/write)│ repl.    │  (no reads)  │
└──────────────┘          └──────────────┘
        │ DNS: mydb.xyz.rds.amazonaws.com
        │ (points to primary, auto-updates on failover)
```

Multi-AZ is for high availability, NOT for read scaling. The standby is not accessible.

### Read Replicas

Asynchronous replicas that serve read traffic. Up to 5 read replicas per primary (15 for Aurora). Can be in different regions (cross-region read replica for geographic performance).

```
              ┌──────────────┐
              │   Primary    │
              │  (all writes)│
              └──────┬───────┘
                     │ async replication
        ┌────────────┼────────────┐
        ▼            ▼            ▼
  ┌──────────┐ ┌──────────┐ ┌──────────┐
  │ Replica 1│ │ Replica 2│ │ Replica 3│
  │  (reads) │ │  (reads) │ │  (reads) │
  └──────────┘ └──────────┘ └──────────┘
```

Read replicas have a connection endpoint separate from the primary. Your application must route reads vs writes explicitly (TypeORM supports this with `replication` option).

```typescript
// TypeORM replication config
TypeOrmModule.forRoot({
  type: 'postgres',
  replication: {
    master: { host: 'primary.rds.amazonaws.com', port: 5432 },
    slaves: [
      { host: 'replica1.rds.amazonaws.com', port: 5432 },
      { host: 'replica2.rds.amazonaws.com', port: 5432 },
    ],
  },
  database: 'appdb',
  username: process.env.DB_USER,
  password: process.env.DB_PASSWORD,
})
```

---

## ElastiCache

Managed in-memory caching for Redis and Memcached.

### Redis vs Memcached on ElastiCache

| Feature | Redis | Memcached |
|---|---|---|
| Data structures | Strings, hashes, lists, sets, sorted sets, streams | Strings only |
| Persistence | RDB snapshots + AOF | None |
| Replication | Yes (read replicas) | No |
| Cluster mode | Yes (sharding) | Yes |
| Pub/Sub | Yes | No |
| Lua scripting | Yes | No |

Use Redis for: sessions, OTP storage, rate limiting, pub/sub, leaderboards, distributed locks.

### ElastiCache Redis Cluster Mode

With cluster mode disabled: one primary + up to 5 read replicas. All data on single primary shard.

With cluster mode enabled: up to 500 nodes across multiple shards. Keys are distributed by hash slot (0-16383). Allows horizontal scaling of writes.

```
Cluster mode enabled (3 shards):

Shard 1: slots 0-5460      → Primary + 2 replicas
Shard 2: slots 5461-10922  → Primary + 2 replicas
Shard 3: slots 10923-16383 → Primary + 2 replicas
```

---

## SQS and SNS

### SQS — Simple Queue Service

Point-to-point asynchronous messaging. Producers send messages, consumers poll and delete them.

Two queue types:
- **Standard Queue**: at-least-once delivery, best-effort ordering, near-unlimited throughput
- **FIFO Queue**: exactly-once processing, strict ordering, up to 3000 msg/sec with batching

Key concepts:
- **Visibility Timeout**: after a consumer receives a message, it becomes invisible to other consumers for this duration. If not deleted in time, it reappears (allows retry). Default: 30s.
- **Dead Letter Queue (DLQ)**: messages that fail processing N times are sent here for inspection.
- **Long Polling**: consumer waits up to 20 seconds for messages. Reduces empty receive calls and cost.

```typescript
// BullMQ uses Redis, but for direct SQS usage:
import { SQSClient, SendMessageCommand, ReceiveMessageCommand, DeleteMessageCommand } from '@aws-sdk/client-sqs';

const sqs = new SQSClient({ region: 'ap-south-1' });

// Produce
await sqs.send(new SendMessageCommand({
  QueueUrl: process.env.SQS_QUEUE_URL,
  MessageBody: JSON.stringify({ userId: '123', event: 'user.registered' }),
  MessageGroupId: 'user-events',  // for FIFO queue
}));
```

### SNS — Simple Notification Service

One-to-many fan-out pub/sub. One message published to a topic is delivered to all subscribers.

Subscribers can be: SQS queues, Lambda functions, HTTP endpoints, email, SMS.

```
         ┌────────────────┐
         │   SNS Topic    │
         │  user.events   │
         └───────┬────────┘
                 │ fan-out
    ┌────────────┼────────────┐
    ▼            ▼            ▼
┌────────┐  ┌────────┐  ┌────────┐
│SQS:    │  │Lambda: │  │HTTP:   │
│email   │  │analytics│ │webhook │
│queue   │  │processor│ │service │
└────────┘  └────────┘  └────────┘
```

SNS → SQS fan-out pattern: publish once to SNS, multiple services each get their own SQS queue with the message.

---

## Lambda

Serverless compute. Pay per invocation (first 1M/month free, then $0.20 per 1M). Pay for execution duration in 1ms increments.

### Lambda Cold Start

When a Lambda is invoked after a period of inactivity, AWS must: provision a container, download your deployment package, initialize the runtime, run your initialization code. This takes 100ms-3s depending on runtime and package size.

Mitigation strategies:
- Provisioned Concurrency: keep N instances warm at all times (costs money)
- Lightweight runtimes: Node.js and Python cold start faster than JVM languages
- Keep deployment package small: tree-shake dependencies, use Lambda layers
- Keep initialization code outside the handler (runs once per container, not per invocation)

```javascript
// Good: DB connection outside handler (reused across invocations in same container)
const client = new pg.Client({ connectionString: process.env.DB_URL });
await client.connect();

exports.handler = async (event) => {
  // client is already connected
  const result = await client.query('SELECT * FROM users WHERE id = $1', [event.userId]);
  return result.rows[0];
};
```

### Lambda Concurrency

- **Unreserved concurrency**: default pool shared across all functions in account (default 1000 per region)
- **Reserved concurrency**: guarantee N concurrent executions for this function, prevents others from using those slots
- **Provisioned concurrency**: pre-initialized instances — eliminates cold starts

---

## API Gateway

Fully managed API gateway. Two types:
- **REST API**: full featured, per-request pricing, supports usage plans and API keys
- **HTTP API**: 71% cheaper, lower latency, supports JWT authorizers, no usage plans. Preferred for most new projects.

Key features:
- Request/response transformation
- Authentication: Lambda authorizer, Cognito, IAM
- Throttling: per-route rate limiting
- Caching: cache responses at the gateway level
- CORS configuration
- Stage variables (dev, staging, prod)

API Gateway throttling: default 10,000 req/sec per account, 5,000 burst. Configure per-method overrides for sensitive endpoints.

---

## CloudFront

Global CDN with 450+ PoPs (Points of Presence). Caches content at edge locations close to users.

### Origins
- S3 bucket (static assets, SPAs)
- ALB or EC2 (dynamic content)
- API Gateway
- Custom HTTP origins

### Cache Behavior
```
Request → CloudFront Edge → cache hit? → serve from edge (fast)
                                      → cache miss? → forward to origin → cache response → serve
```

Cache key components: URL, query string, headers, cookies. Be specific about what varies the cache — including unnecessary headers in the cache key reduces hit rate.

### Signed URLs vs Signed Cookies for private content

- **Signed URL**: per-file access. Use for individual file access control.
- **Signed Cookie**: access to multiple files. Use for subscribers who should access all files in a directory.

```typescript
import { getSignedUrl } from '@aws-sdk/cloudfront-signer';

const signedUrl = getSignedUrl({
  url: `https://d1234.cloudfront.net/private/document.pdf`,
  keyPairId: process.env.CF_KEY_PAIR_ID,
  privateKey: process.env.CF_PRIVATE_KEY,
  dateLessThan: new Date(Date.now() + 3600 * 1000).toISOString(),
});
```

---

## VPC — Virtual Private Cloud

Your isolated network in AWS. Every resource you create lives in a VPC (or the default VPC).

### Subnets

A subnet is a range of IP addresses in your VPC within a single AZ.

- **Public subnet**: has a route to an Internet Gateway. Resources in a public subnet can have public IPs.
- **Private subnet**: no route to Internet Gateway. Outbound internet access only through a NAT Gateway.

```
VPC: 10.0.0.0/16
│
├── AZ ap-south-1a
│   ├── Public Subnet:  10.0.1.0/24  → Route: 0.0.0.0/0 → IGW
│   └── Private Subnet: 10.0.2.0/24  → Route: 0.0.0.0/0 → NAT GW
│
└── AZ ap-south-1b
    ├── Public Subnet:  10.0.3.0/24  → Route: 0.0.0.0/0 → IGW
    └── Private Subnet: 10.0.4.0/24  → Route: 0.0.0.0/0 → NAT GW
```

Best practice:
- Load balancers in public subnets
- Application servers (EC2, EKS nodes) in private subnets
- RDS, ElastiCache in private subnets (no internet access at all)

### Security Groups vs NACLs

**Security Groups**: stateful, operate at instance level. If you allow inbound traffic, the response is automatically allowed outbound. Rules are allow-only (no deny rules).

**NACLs (Network ACLs)**: stateless, operate at subnet level. Must explicitly allow both inbound and outbound for bidirectional traffic. Support both allow and deny rules, evaluated in rule number order.

```
Inbound request flow:
Internet → NACL (subnet boundary) → Security Group (instance boundary) → EC2

Security Group example:
Inbound:  Allow TCP 3000 from ALB Security Group
Outbound: Allow all (default)

NACL example:
Inbound:  Rule 100: Allow TCP 3000 from 0.0.0.0/0
          Rule 200: Allow TCP 1024-65535 from 0.0.0.0/0 (ephemeral ports for return traffic)
Outbound: Rule 100: Allow TCP 443 to 0.0.0.0/0
          Rule 200: Allow TCP 3000 from 0.0.0.0/0
```

For most purposes, Security Groups are sufficient. NACLs add a subnet-level layer of defense.

### NAT Gateway

Allows instances in private subnets to initiate outbound connections to the internet (e.g., pull npm packages, call external APIs) without having a public IP.

- One NAT Gateway per AZ (for high availability)
- Managed by AWS — no maintenance
- Expensive: $0.045/hour + $0.045/GB processed

To avoid NAT Gateway data transfer costs for AWS service calls, use VPC Endpoints (PrivateLink) — they route traffic directly within the AWS network.

---

## IAM — Identity and Access Management

### Policies

JSON documents that define permissions. Two types:
- **Identity-based policies**: attached to users, groups, or roles
- **Resource-based policies**: attached to resources (S3 bucket policy, SQS queue policy)

```json
{
  "Version": "2012-10-17",
  "Statement": [
    {
      "Sid": "AllowSpecificBucketAccess",
      "Effect": "Allow",
      "Action": [
        "s3:GetObject",
        "s3:PutObject",
        "s3:DeleteObject"
      ],
      "Resource": "arn:aws:s3:::my-app-bucket/uploads/*",
      "Condition": {
        "StringEquals": {
          "s3:prefix": ["uploads/"]
        }
      }
    },
    {
      "Sid": "AllowListBucket",
      "Effect": "Allow",
      "Action": "s3:ListBucket",
      "Resource": "arn:aws:s3:::my-app-bucket"
    }
  ]
}
```

### Roles and Instance Profiles

An IAM Role is an identity with a trust policy that defines who can assume it. Roles issue temporary credentials (STS tokens) — no long-term access keys.

An Instance Profile is a container for an IAM Role that can be attached to an EC2 instance. The EC2 instance assumes the role automatically; the AWS SDK retrieves credentials from the EC2 metadata endpoint (`169.254.169.254`).

```
EC2 Instance
└── Instance Profile → IAM Role (S3ReadAccess policy)
    └── Application code uses AWS SDK
        └── SDK auto-fetches temp credentials from metadata endpoint
            └── Credentials auto-rotate every ~hour
```

This is the correct pattern for application credentials on EC2/ECS/Lambda — never use long-term access keys stored as environment variables on compute resources.

### Least Privilege

Grant only the permissions required for the specific function. Never use `"Action": "*"` or `"Resource": "*"` in production policies.

Common violations to avoid:
- Lambda with `AdministratorAccess` policy
- EC2 instance that can write to any S3 bucket when it only needs one prefix
- Developers with IAM admin rights when they only need EC2 access

Use IAM Access Analyzer to identify overly permissive policies and unused permissions.

---

## AWS Well-Architected Framework

### Pillar 1: Operational Excellence
Design to run and monitor systems, and to continuously improve operations.

Key practices:
- Infrastructure as code (no manual clicks in console for production)
- Annotated documentation (runbooks, playbooks)
- Make frequent, small, reversible changes
- Anticipate failure — game days, chaos engineering
- Learn from all operational events (blameless post-mortems)

### Pillar 2: Security
Protect data, systems, and assets. Defense in depth.

Key practices:
- Implement a strong identity foundation (no root access keys, MFA everywhere)
- Enable traceability: CloudTrail, VPC Flow Logs, GuardDuty
- Apply security at all layers (edge, VPC, subnet, instance, application, data)
- Automate security best practices
- Protect data in transit (TLS) and at rest (KMS encryption)
- Prepare for security events (incident response runbooks)

### Pillar 3: Reliability
Build to recover from failures and meet demand.

Key practices:
- Test recovery procedures (not just backups, but restoration)
- Automatically recover from failure (health checks, ASGs)
- Scale horizontally — prefer more small resources over fewer large ones
- Manage change via automation
- Multi-AZ for all stateful services

### Pillar 4: Performance Efficiency
Use resources efficiently as demand changes and technologies evolve.

Key practices:
- Select the right resource types (don't use general purpose when memory-optimized is needed)
- Review selection regularly — AWS releases new instance types constantly
- Democratize advanced technologies — use managed services instead of running your own
- Benchmark continuously

### Pillar 5: Cost Optimization
Avoid unnecessary costs.

Key practices:
- Implement cloud financial management (tagging, budgets, cost anomaly detection)
- Adopt consumption model — pay for what you use
- Measure overall efficiency (business output per dollar spent)
- Stop spending on heavy lifting — use managed services
- Analyze and attribute expenditure (per-team cost allocation tags)

---

## Common Architecture Patterns

### Three-Tier Architecture (Classic)
```
Internet → Route 53 → CloudFront
                    → ALB (public subnet)
                      → EC2 ASG (private subnet, app tier)
                        → RDS PostgreSQL (private subnet, data tier)
                        → ElastiCache Redis (private subnet, cache tier)
```

### Serverless Architecture
```
Client → API Gateway → Lambda
                     → DynamoDB (or RDS Proxy → RDS)
                     → S3 (static assets)
         CloudFront → S3 (SPA hosting)
```

### Microservices on EKS
```
Internet → Route 53 → ALB Ingress Controller
                    → K8s Services
                      → Auth Service (Pod)
                      → School Service (Pod)
                      → Notification Service (Pod)
                        → SQS → Lambda (email/SMS)
                    → Shared: RDS, ElastiCache, ECR
```

---

## Cost Optimization Tips

1. **Right-size instances**: use CloudWatch CPU/memory metrics over 2 weeks and downgrade if p99 CPU < 40%
2. **Reserved Instances / Savings Plans**: commit 1-3 years for baseline load. Covers ~70% of steady-state usage.
3. **Spot Instances for batch**: workers, ML training, integration test environments — 70-90% savings
4. **S3 lifecycle policies**: automate transition to cheaper storage classes after 30/90 days
5. **Delete unattached EBS volumes**: automated via Lambda + Config rule
6. **NAT Gateway costs**: use VPC endpoints for S3, DynamoDB, ECR. Significant savings for high-traffic workloads.
7. **Data transfer**: keep traffic within the same AZ where possible ($0 vs $0.01/GB cross-AZ)
8. **CloudFront for S3**: reduces S3 GET request costs and offloads bandwidth
9. **Use Graviton (ARM) instances**: 20-40% better price/performance than x86 equivalents (t4g vs t3, m7g vs m6i)
10. **Enable S3 Intelligent-Tiering** for buckets with unknown access patterns — automatically moves data to cheaper tiers

---

## Interview Q&A

**Q1: What is the difference between an RDS Multi-AZ and a Read Replica? Can you use a Multi-AZ standby for reading?**

Multi-AZ and Read Replicas solve different problems. Multi-AZ is for high availability — the standby is a synchronous replica that takes over automatically if the primary fails. You cannot read from the Multi-AZ standby; it is purely a failover target. Read Replicas are for read scaling — they use asynchronous replication and have their own connection endpoints that applications read from. They do not automatically promote on primary failure (though you can manually promote one). For a production system you typically want both: Multi-AZ for HA and read replicas for read throughput.

**Q2: A developer accidentally deleted all data from an S3 bucket. How do you recover?**

First, check if S3 Versioning was enabled. If so, deleted objects have a delete marker — removing the delete marker restores the object. If the bucket had versioning but the developer deleted specific versions, S3 Glacier vault lock or Object Lock could protect against permanent deletion if configured. If versioning was not enabled, the data is gone unless you have a backup elsewhere (cross-region replication target, or periodic backup to another bucket). Preventive measures: enable versioning and MFA Delete on critical buckets, use S3 Object Lock for compliance data, restrict `DeleteObject` permission to specific roles via bucket policy.

**Q3: Walk me through how you would design a secure, private NestJS API on AWS.**

The API should never be directly internet-accessible. I would place EC2 instances or EKS pods in private subnets. An Application Load Balancer in public subnets receives HTTPS traffic (TLS termination at the ALB using ACM certificates). The ALB security group allows port 443 from anywhere. The application security group allows port 3000 only from the ALB security group — not from the internet. The RDS security group allows port 5432 only from the application security group. ElastiCache security group allows port 6379 only from the application security group. No instance has a public IP. Outbound internet access for the app tier (to call external APIs, pull updates) goes through a NAT Gateway. AWS WAF on the ALB blocks SQL injection, XSS, and rate limits. VPC Flow Logs and CloudTrail are enabled for audit. Secrets are in AWS Secrets Manager and fetched via the instance's IAM role at startup.

**Q4: What is the difference between a security group and a NACL, and when would you use a NACL to block traffic?**

Security groups are stateful and operate at the instance level — return traffic is automatically allowed. NACLs are stateless and operate at the subnet boundary — you must explicitly allow both inbound and outbound for each flow, including ephemeral return ports. For most application security, security groups are sufficient and easier to manage. I would use a NACL specifically when I need to explicitly deny traffic — security groups have no deny rules, they only allow. A common use case: if a specific IP range is attacking you, you can add a NACL deny rule with a lower rule number than the allow rule to block it at the subnet boundary before it even reaches instance-level security groups. Another use case: compliance requirements that mandate a firewall at the network perimeter.

**Q5: Your Lambda function works fine locally but times out in AWS. What do you check?**

First, check if the function is in a VPC. VPC-enabled Lambdas can time out if the subnet has no internet access and the function needs to reach an external service or AWS API without a VPC Endpoint. The function needs a NAT Gateway in the subnet's route table or VPC Endpoints for the AWS services it calls. Second, check the timeout setting — default is 3 seconds. Third, check if it's a cold start issue — the first invocation after idle spins up a new container. Check the INIT duration in CloudWatch Logs. If cold starts are the problem, reduce package size or use Provisioned Concurrency. Fourth, check for DB connection pool exhaustion — Lambda can have hundreds of concurrent invocations each holding a DB connection. Use RDS Proxy to pool connections. Fifth, check memory — Lambda CPU is proportional to memory allocation. A compute-heavy function with 128MB has very little CPU.

**Q6: How does IAM role assumption work for an EKS pod?**

With IRSA (IAM Roles for Service Accounts), each K8s ServiceAccount can be annotated with an IAM role ARN. When a pod using that ServiceAccount makes an AWS API call, the AWS SDK calls the EKS OIDC endpoint to exchange a K8s service account token for AWS temporary credentials via AssumeRoleWithWebIdentity. The IAM role's trust policy must trust the EKS OIDC provider. This gives each pod its own IAM identity with least-privilege permissions, rather than all pods sharing the node's IAM role. For example, a pod that needs S3 access gets a role with only S3 read permission for the specific bucket, while another pod that sends emails via SES gets a role with only SES send permission.
