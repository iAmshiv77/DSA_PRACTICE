# Monitoring & Observability

## The Three Pillars of Observability

Observability is the ability to understand the internal state of a system by examining its external outputs. The three pillars are complementary — you need all three to diagnose production problems effectively.

### Pillar 1: Metrics

Numerical measurements aggregated over time. Metrics are cheap to store, fast to query, and ideal for dashboards and alerting. They tell you **what** is happening.

Types:
- **Counter**: monotonically increasing (total requests, total errors). Always goes up.
- **Gauge**: current value that can go up or down (memory usage, active connections, queue depth).
- **Histogram**: distribution of values across buckets (request duration bucketed by 10ms, 50ms, 100ms, 500ms, 1s). Enables percentile calculations.
- **Summary**: similar to histogram but percentiles calculated client-side. Less flexible for aggregation.

Example metrics for a NestJS API:
```
http_requests_total{method="POST",route="/auth/login",status="200"} 48291
http_request_duration_seconds_bucket{le="0.1"} 44000
http_request_duration_seconds_bucket{le="0.5"} 47800
http_request_duration_seconds_bucket{le="1.0"} 48100
http_errors_total{route="/auth/login",error="invalid_credentials"} 1204
db_query_duration_seconds_bucket{query="findUser",le="0.05"} 43000
redis_cache_hit_total 38000
redis_cache_miss_total 10291
```

### Pillar 2: Logs

Timestamped records of discrete events. Logs tell you **what happened** at a specific point in time. Essential for debugging but expensive to store and slow to query at scale.

Key attributes of good logs:
- Structured (JSON) — machine-parseable, not free-form strings
- Include context (request ID, user ID, trace ID) — correlate with other pillars
- Appropriate severity levels: DEBUG, INFO, WARN, ERROR, FATAL
- Include enough context to be actionable without being verbose

```json
{
  "level": "error",
  "time": "2024-01-15T10:23:45.123Z",
  "pid": 1,
  "hostname": "backend-pod-abc123",
  "context": "AuthService",
  "msg": "sendOtp failed for +919876543210",
  "traceId": "4bf92f3577b34da6a3ce929d0e0e4736",
  "spanId": "00f067aa0ba902b7",
  "userId": null,
  "error": {
    "message": "MSG91 API returned 500",
    "stack": "Error: ...",
    "code": "MSG91_UPSTREAM_ERROR"
  }
}
```

### Pillar 3: Traces

A trace represents the end-to-end journey of a single request through a distributed system. Each trace consists of spans — units of work within a single service. Spans are linked by a trace ID that propagates across service boundaries via HTTP headers.

```
Trace ID: 4bf92f3577b34da6a3ce929d0e0e4736

POST /auth/login  [0ms ─────────────────────── 243ms]
  ├── AuthService.login  [2ms ──────────────── 241ms]
  │   ├── Redis GET otp:+919876543210  [3ms ── 4ms]  (1ms)
  │   ├── DB findOne User  [5ms ─────────────── 62ms]  (57ms)  ← slow
  │   └── JWT sign  [63ms ─ 65ms]  (2ms)
  └── SanitizeInterceptor  [241ms ── 243ms]  (2ms)
```

Without distributed tracing, you cannot see cross-service latency. With tracing, you can immediately identify which service or DB query is the bottleneck.

---

## Prometheus + Grafana

### Architecture

```
Application pods → /metrics endpoint (pull model)
                              ↑
                    Prometheus scrapes every 15s
                              ↓
                    Prometheus TSDB (time-series storage)
                              ↓
                    Grafana queries via PromQL
                              ↓
                    Dashboards + Alertmanager
```

### Prometheus Configuration

```yaml
# prometheus.yml
global:
  scrape_interval: 15s
  evaluation_interval: 15s

alerting:
  alertmanagers:
    - static_configs:
        - targets: ['alertmanager:9093']

rule_files:
  - '/etc/prometheus/rules/*.yml'

scrape_configs:
  - job_name: 'nestjs-backend'
    metrics_path: /metrics
    static_configs:
      - targets: ['backend-service:3000']
    relabel_configs:
      - source_labels: [__address__]
        target_label: instance

  # Kubernetes service discovery (preferred over static_configs)
  - job_name: 'kubernetes-pods'
    kubernetes_sd_configs:
      - role: pod
    relabel_configs:
      - source_labels: [__meta_kubernetes_pod_annotation_prometheus_io_scrape]
        action: keep
        regex: 'true'
      - source_labels: [__meta_kubernetes_pod_annotation_prometheus_io_path]
        action: replace
        target_label: __metrics_path__
        regex: (.+)
      - source_labels: [__address__, __meta_kubernetes_pod_annotation_prometheus_io_port]
        action: replace
        regex: ([^:]+)(?::\d+)?;(\d+)
        replacement: $1:$2
        target_label: __address__
```

### PromQL Examples

```promql
# Request rate (per second over last 5 minutes)
rate(http_requests_total[5m])

# Error rate percentage
rate(http_requests_total{status=~"5.."}[5m])
/ rate(http_requests_total[5m])
* 100

# 95th percentile latency
histogram_quantile(0.95, sum(rate(http_request_duration_seconds_bucket[5m])) by (le, route))

# 99th percentile latency per route
histogram_quantile(0.99,
  sum(rate(http_request_duration_seconds_bucket[5m])) by (le, route)
)

# Apdex score (fraction of requests < 500ms)
(
  sum(rate(http_request_duration_seconds_bucket{le="0.5"}[5m]))
  +
  sum(rate(http_request_duration_seconds_bucket{le="1.0"}[5m]))
) / 2
/ sum(rate(http_request_duration_seconds_count[5m]))

# DB connection pool saturation
db_pool_active_connections / db_pool_max_connections

# Memory usage per pod
container_memory_working_set_bytes{namespace="production", container="backend"}

# Pod restart count in last hour
increase(kube_pod_container_status_restarts_total[1h]) > 0

# Redis cache hit rate
rate(redis_cache_hit_total[5m])
/ (rate(redis_cache_hit_total[5m]) + rate(redis_cache_miss_total[5m]))
```

### Alert Rules

```yaml
# /etc/prometheus/rules/backend.yml
groups:
  - name: backend-alerts
    interval: 30s
    rules:
      - alert: HighErrorRate
        expr: |
          rate(http_requests_total{status=~"5.."}[5m])
          / rate(http_requests_total[5m]) > 0.01
        for: 2m
        labels:
          severity: critical
          team: backend
        annotations:
          summary: "Error rate above 1% on {{ $labels.route }}"
          description: "Error rate is {{ $value | humanizePercentage }} for the last 2 minutes."
          runbook_url: "https://wiki.company.com/runbooks/high-error-rate"

      - alert: HighP99Latency
        expr: |
          histogram_quantile(0.99,
            sum(rate(http_request_duration_seconds_bucket[5m])) by (le, route)
          ) > 2.0
        for: 5m
        labels:
          severity: warning
        annotations:
          summary: "P99 latency above 2s on {{ $labels.route }}"

      - alert: PodRestarting
        expr: increase(kube_pod_container_status_restarts_total[30m]) > 3
        for: 0m
        labels:
          severity: critical
        annotations:
          summary: "Pod {{ $labels.pod }} has restarted {{ $value }} times in 30 minutes"
```

### NestJS Prometheus Integration

```typescript
// Install: npm install prom-client @willsoto/nestjs-prometheus

// app.module.ts
import { PrometheusModule } from '@willsoto/nestjs-prometheus';

@Module({
  imports: [
    PrometheusModule.register({
      defaultMetrics: { enabled: true },
      path: '/metrics',
    }),
  ],
})
export class AppModule {}

// http-metrics.interceptor.ts
import { makeHistogramProvider } from '@willsoto/nestjs-prometheus';
import { Histogram, Registry } from 'prom-client';

export const HTTP_REQUEST_DURATION = makeHistogramProvider({
  name: 'http_request_duration_seconds',
  help: 'Duration of HTTP requests in seconds',
  labelNames: ['method', 'route', 'status_code'],
  buckets: [0.005, 0.01, 0.05, 0.1, 0.5, 1, 2, 5],
});

@Injectable()
export class HttpMetricsInterceptor implements NestInterceptor {
  constructor(
    @InjectMetric('http_request_duration_seconds')
    private readonly histogram: Histogram<string>,
  ) {}

  intercept(context: ExecutionContext, next: CallHandler): Observable<any> {
    const req = context.switchToHttp().getRequest<FastifyRequest>();
    const end = this.histogram.startTimer({
      method: req.method,
      route: req.routerPath ?? req.url,
    });

    return next.handle().pipe(
      tap(() => {
        const res = context.switchToHttp().getResponse<FastifyReply>();
        end({ status_code: res.statusCode });
      }),
      catchError((error) => {
        end({ status_code: error.status ?? 500 });
        throw error;
      }),
    );
  }
}
```

---

## ELK / EFK Stack

### ELK: Elasticsearch + Logstash + Kibana

```
Application → Logstash (parse, transform, enrich) → Elasticsearch (index + store) → Kibana (search + visualize)
```

**Logstash** is a data pipeline: collect logs from files, beats, Kafka → parse with Grok patterns → enrich (add GeoIP, parse user agents) → output to Elasticsearch.

**Elasticsearch** is a distributed search and analytics engine. Stores logs as JSON documents, indexed for full-text search and aggregations.

**Kibana** provides UI for: searching logs (KQL queries), Discover view, Dashboards, APM, Alerting.

### EFK: Elasticsearch + Fluentd/Fluent Bit + Kibana

Fluentd is more memory-efficient than Logstash and better suited for Kubernetes. Fluent Bit is even lighter (runs as DaemonSet, one per node).

```
K8s Pod stdout/stderr
        ↓
Fluent Bit DaemonSet (reads /var/log/containers/*.log)
        ↓ (forward protocol)
Fluentd Aggregator (enrichment, routing)
        ↓
Elasticsearch
        ↓
Kibana
```

### Fluent Bit Configuration for NestJS

```yaml
# fluent-bit ConfigMap
apiVersion: v1
kind: ConfigMap
metadata:
  name: fluent-bit-config
data:
  fluent-bit.conf: |
    [SERVICE]
        Flush         1
        Log_Level     info
        Parsers_File  parsers.conf

    [INPUT]
        Name              tail
        Path              /var/log/containers/backend-*.log
        Parser            docker
        Tag               backend.*
        Refresh_Interval  5
        Mem_Buf_Limit     50MB

    [FILTER]
        Name         parser
        Match        backend.*
        Key_Name     log
        Parser       nestjs_json

    [FILTER]
        Name         record_modifier
        Match        backend.*
        Record       cluster super-schools-prod
        Record       env production

    [OUTPUT]
        Name            es
        Match           backend.*
        Host            elasticsearch.logging.svc.cluster.local
        Port            9200
        Index           nestjs-logs
        Type            _doc
        Logstash_Format On
        Logstash_Prefix nestjs

  parsers.conf: |
    [PARSER]
        Name        nestjs_json
        Format      json
        Time_Key    time
        Time_Format %Y-%m-%dT%H:%M:%S.%LZ
```

### Kibana Query Language (KQL) Examples

```
# Find all errors in last 1 hour
level: "error" AND @timestamp > now-1h

# Errors for specific user
level: "error" AND userId: "12345"

# All requests slower than 1 second
duration_ms > 1000

# Specific route errors
level: "error" AND context: "AuthService"

# Find trace across services
traceId: "4bf92f3577b34da6a3ce929d0e0e4736"
```

---

## Distributed Tracing: OpenTelemetry and Jaeger

### OpenTelemetry (OTel)

OpenTelemetry is the vendor-neutral CNCF standard for instrumentation. You instrument once with OTel, then export to any backend (Jaeger, Zipkin, Tempo, Datadog, etc.).

Components:
- **API**: language-specific interfaces for tracing, metrics, logs
- **SDK**: implementation of the API
- **Collector**: agent/gateway that receives, processes, and exports telemetry
- **Exporters**: send data to specific backends

### NestJS OpenTelemetry Setup

```typescript
// tracing.ts — must be imported BEFORE all other modules
import { NodeSDK } from '@opentelemetry/sdk-node';
import { OTLPTraceExporter } from '@opentelemetry/exporter-trace-otlp-http';
import { Resource } from '@opentelemetry/resources';
import { SemanticResourceAttributes } from '@opentelemetry/semantic-conventions';
import { HttpInstrumentation } from '@opentelemetry/instrumentation-http';
import { FastifyInstrumentation } from '@opentelemetry/instrumentation-fastify';
import { PgInstrumentation } from '@opentelemetry/instrumentation-pg';
import { IORedisInstrumentation } from '@opentelemetry/instrumentation-ioredis';

const sdk = new NodeSDK({
  resource: new Resource({
    [SemanticResourceAttributes.SERVICE_NAME]: 'super-schools-backend',
    [SemanticResourceAttributes.SERVICE_VERSION]: process.env.APP_VERSION,
    [SemanticResourceAttributes.DEPLOYMENT_ENVIRONMENT]: process.env.NODE_ENV,
  }),
  traceExporter: new OTLPTraceExporter({
    url: process.env.OTEL_EXPORTER_OTLP_ENDPOINT ?? 'http://jaeger-collector:4318/v1/traces',
  }),
  instrumentations: [
    new HttpInstrumentation(),
    new FastifyInstrumentation(),
    new PgInstrumentation(),
    new IORedisInstrumentation(),
  ],
});

sdk.start();

// main.ts
import './tracing';  // must be first import
import { NestFactory } from '@nestjs/core';
```

### Trace Context Propagation

```typescript
// Add trace ID to logs for correlation
import { trace, context } from '@opentelemetry/api';

@Injectable()
export class TraceContextInterceptor implements NestInterceptor {
  intercept(ctx: ExecutionContext, next: CallHandler): Observable<any> {
    const span = trace.getActiveSpan();
    if (span) {
      const spanContext = span.spanContext();
      const req = ctx.switchToHttp().getRequest<FastifyRequest>();
      req.traceId = spanContext.traceId;
      req.spanId = spanContext.spanId;
    }
    return next.handle();
  }
}
```

### Jaeger Architecture

```
Application (OTel SDK)
        ↓ OTLP gRPC/HTTP
OTel Collector (receive, batch, export)
        ↓ Jaeger format
Jaeger Collector
        ↓
Cassandra / Elasticsearch (trace storage)
        ↓
Jaeger Query + UI (trace search and visualization)
```

Jaeger UI allows you to:
- Search traces by service, operation, tags, duration
- Visualize the timeline of all spans in a trace
- Compare two traces side by side
- Find traces that are slow or erroring

---

## SLI, SLO, SLA

### SLI — Service Level Indicator

A specific, measurable metric that represents an aspect of service quality.

Common SLIs:
- **Availability**: `(successful requests / total requests) * 100`
- **Latency**: P99 of request duration < 500ms
- **Error rate**: `(5xx responses / total responses) * 100`
- **Throughput**: requests per second
- **Freshness**: age of data served (for data pipelines)

### SLO — Service Level Objective

An internal target for an SLI over a rolling time window. This is the threshold your team commits to maintaining.

Examples:
- Availability SLO: 99.9% of requests succeed over a 30-day rolling window
- Latency SLO: 95% of requests complete in under 200ms; 99% under 1 second
- Error SLO: error rate below 0.1% over a 7-day window

**Error Budget**: SLO headroom available for outages.
```
SLO: 99.9% availability (30 days)
Allowed downtime = 30 days * 24h * 60m * (1 - 0.999) = 43.2 minutes/month

If you've used 30 minutes of downtime this month:
Remaining error budget = 13.2 minutes
```

When the error budget is exhausted, stop new feature deployments and focus on reliability.

### SLA — Service Level Agreement

A contractual commitment to customers, with financial or legal consequences for violation. SLAs are always looser than SLOs to give internal teams a safety buffer.

```
Internal SLO: 99.9% availability
External SLA: 99.5% availability
Buffer:        0.4% (gives you room for SLO violations without SLA breach)
```

### Practical PromQL for SLO Tracking

```promql
# 30-day availability (success rate)
1 - (
  sum(increase(http_requests_total{status=~"5.."}[30d]))
  / sum(increase(http_requests_total[30d]))
)

# Error budget burn rate (how fast are we consuming the budget?)
# If this exceeds 1.0, we're burning budget faster than it replenishes
(
  rate(http_requests_total{status=~"5.."}[1h])
  / rate(http_requests_total[1h])
) / (1 - 0.999)

# Multi-window burn rate alert (Google SRE book method)
# Alert if short-window burn rate is high AND long-window burn rate is high
(
  rate(http_requests_total{status=~"5.."}[5m]) / rate(http_requests_total[5m])
  > 14.4 * 0.001
)
and
(
  rate(http_requests_total{status=~"5.."}[1h]) / rate(http_requests_total[1h])
  > 14.4 * 0.001
)
```

---

## Alerting Best Practices

### Avoid Alert Fatigue

Alert fatigue happens when too many low-quality alerts desensitize the on-call engineer. They start ignoring pages. This is how real incidents get missed.

Rules for good alerts:
1. **Alert on symptoms, not causes**: alert on high error rate (user impact), not high CPU (may or may not affect users)
2. **Every alert must be actionable**: if the alert fires and the engineer cannot do anything about it, remove it
3. **Use the `for` duration**: require the condition to be true for 2-5 minutes before alerting. Eliminates spikes that self-resolve.
4. **Include the runbook link**: every alert annotation should have a URL to the investigation steps
5. **Severity tiers**: CRITICAL (page immediately), WARNING (notify during business hours), INFO (log only)

### Alert Routing with Alertmanager

```yaml
# alertmanager.yml
route:
  group_by: ['alertname', 'cluster', 'service']
  group_wait: 10s      # wait before sending first notification (group related alerts)
  group_interval: 5m   # wait before sending new notifications for ongoing alerts
  repeat_interval: 4h  # re-notify if alert still firing after this time
  receiver: 'slack-warnings'

  routes:
    - match:
        severity: critical
      receiver: 'pagerduty-critical'
      continue: true    # also send to default receiver

    - match:
        severity: warning
      receiver: 'slack-warnings'

receivers:
  - name: 'pagerduty-critical'
    pagerduty_configs:
      - service_key: '$PAGERDUTY_KEY'
        description: '{{ template "pagerduty.default.description" . }}'

  - name: 'slack-warnings'
    slack_configs:
      - api_url: '$SLACK_WEBHOOK'
        channel: '#alerts-backend'
        title: '[{{ .Status | toUpper }}] {{ .CommonAnnotations.summary }}'
        text: |
          {{ range .Alerts }}
          *Alert:* {{ .Annotations.summary }}
          *Description:* {{ .Annotations.description }}
          *Runbook:* {{ .Annotations.runbook_url }}
          {{ end }}

inhibit_rules:
  # If cluster is down, suppress all other alerts from it
  - source_match:
      alertname: 'ClusterDown'
    target_match_re:
      alertname: '.+'
    equal: ['cluster']
```

### Dead Man's Switch

An alert that fires when your monitoring system itself is broken or not receiving data.

```yaml
# Alert that always fires — proves alerting pipeline is working
# Alertmanager routes this to a heartbeat endpoint (e.g., healthchecks.io)
# If the alert STOPS firing, the heartbeat endpoint raises an incident
- alert: WatchdogHeartbeat
  expr: vector(1)
  labels:
    severity: none
  annotations:
    summary: "Watchdog — this alert proves the monitoring pipeline is working"
```

---

## Node.js / NestJS Specifics

### Structured Logging with Pino

Pino is the fastest Node.js logger, outputs JSON natively.

```typescript
// Install: npm install nestjs-pino pino-http pino-pretty

// main.ts
import { Logger } from 'nestjs-pino';

async function bootstrap() {
  const app = await NestFactory.create(AppModule, { bufferLogs: true });
  app.useLogger(app.get(Logger));
  await app.listen(3000);
}

// app.module.ts
import { LoggerModule } from 'nestjs-pino';

@Module({
  imports: [
    LoggerModule.forRoot({
      pinoHttp: {
        level: process.env.LOG_LEVEL ?? 'info',
        transport: process.env.NODE_ENV !== 'production'
          ? { target: 'pino-pretty', options: { colorize: true } }
          : undefined,
        serializers: {
          req(req) {
            return {
              method: req.method,
              url: req.url,
              // never log Authorization header
            };
          },
        },
        redact: {
          paths: ['req.headers.authorization', 'req.headers.cookie', 'body.password'],
          censor: '[REDACTED]',
        },
        customProps: (req) => ({
          traceId: req.headers['x-trace-id'],
        }),
        autoLogging: {
          ignore: (req) => req.url === '/health',  // don't log health checks
        },
      },
    }),
  ],
})
export class AppModule {}
```

### Health Checks

```typescript
// Install: npm install @nestjs/terminus

// health.controller.ts
import { Controller, Get } from '@nestjs/common';
import { HealthCheck, HealthCheckService, TypeOrmHealthIndicator, MicroserviceHealthIndicator } from '@nestjs/terminus';
import { InjectConnection } from '@nestjs/typeorm';
import { Connection } from 'typeorm';

@Controller('health')
export class HealthController {
  private readonly logger = new Logger(HealthController.name);

  constructor(
    private readonly health: HealthCheckService,
    private readonly db: TypeOrmHealthIndicator,
  ) {}

  @Get()
  @HealthCheck()
  check() {
    return this.health.check([
      () => this.db.pingCheck('database', { timeout: 3000 }),
    ]);
  }

  // Liveness: is the process running?
  @Get('live')
  liveness() {
    return { status: 'ok', timestamp: new Date().toISOString() };
  }

  // Readiness: can the service handle requests?
  @Get('ready')
  @HealthCheck()
  readiness() {
    return this.health.check([
      () => this.db.pingCheck('database', { timeout: 3000 }),
    ]);
  }
}

// K8s manifest
// livenessProbe:  /health/live  (restart pod if fails)
// readinessProbe: /health/ready (remove from load balancer if fails)
// startupProbe:   /health/live  (give app 60s to start before liveness kicks in)
```

### Kubernetes Probe Configuration

```yaml
livenessProbe:
  httpGet:
    path: /health/live
    port: 3000
  initialDelaySeconds: 10
  periodSeconds: 10
  failureThreshold: 3

readinessProbe:
  httpGet:
    path: /health/ready
    port: 3000
  initialDelaySeconds: 15
  periodSeconds: 5
  failureThreshold: 3

startupProbe:
  httpGet:
    path: /health/live
    port: 3000
  failureThreshold: 30   # 30 * 2s = 60s startup window
  periodSeconds: 2
```

### Metrics Endpoint in NestJS (prom-client manual)

```typescript
import { collectDefaultMetrics, Registry, Histogram, Counter } from 'prom-client';

@Injectable()
export class MetricsService {
  private readonly logger = new Logger(MetricsService.name);
  private readonly registry: Registry;
  readonly httpDuration: Histogram;
  readonly httpErrors: Counter;

  constructor() {
    this.registry = new Registry();
    collectDefaultMetrics({ register: this.registry });

    this.httpDuration = new Histogram({
      name: 'http_request_duration_seconds',
      help: 'HTTP request duration in seconds',
      labelNames: ['method', 'route', 'status_code'],
      buckets: [0.005, 0.01, 0.05, 0.1, 0.25, 0.5, 1, 2.5, 5],
      registers: [this.registry],
    });

    this.httpErrors = new Counter({
      name: 'http_errors_total',
      help: 'Total HTTP errors',
      labelNames: ['route', 'error_code'],
      registers: [this.registry],
    });
  }

  async getMetrics(): Promise<string> {
    return this.registry.metrics();
  }
}

@Controller()
export class MetricsController {
  private readonly logger = new Logger(MetricsController.name);

  constructor(private readonly metricsService: MetricsService) {}

  @Get('/metrics')
  async metrics(@Res({ passthrough: true }) res: FastifyReply) {
    res.header('Content-Type', 'text/plain; version=0.0.4; charset=utf-8');
    return this.metricsService.getMetrics();
  }
}
```

---

## Interview Q&A

**Q1: What is the difference between monitoring and observability?**

Monitoring is asking predefined questions about a system — you set up dashboards and alerts for things you know you need to watch. Observability is the ability to ask arbitrary questions about why the system behaves the way it does. A monitored system tells you when something is wrong. An observable system lets you understand why it is wrong, even for failure modes you did not anticipate. Monitoring uses dashboards on known metrics. Observability uses logs, traces, and metrics in combination to reconstruct system state after the fact. In practice: monitoring catches the alert, observability helps you debug the root cause.

**Q2: You get paged at 2AM for high error rate. Walk me through your investigation.**

First, look at the error rate dashboard to confirm severity and scope — is it all routes or specific ones, all pods or one, all regions or one. Second, query Kibana for recent error logs in the same time window to get the actual error messages and stack traces. Third, check if there was a recent deployment — correlate the alert time with deployment history. Fourth, use the trace ID from the error log to find the full distributed trace in Jaeger — this shows exactly which service or database query is failing. Fifth, check the relevant infrastructure metrics: DB connection pool saturation, RDS CPU, Redis memory, pod OOMKilled events. If it is a bad deployment, roll back immediately. If it is a downstream service failure, implement circuit breaking. If it is a DB issue, check for slow queries, locks, or connection exhaustion.

**Q3: What is the difference between a histogram and a gauge in Prometheus?**

A gauge is a single numerical value that represents a current measurement — it can go up or down, like current memory usage or number of active HTTP connections. You query a gauge to know the current state. A histogram is a distribution measurement — it records observations into predefined buckets, allowing you to calculate percentiles. A gauge tells you "currently 450MB of memory is used." A histogram tells you "95% of requests complete in under 200ms." For latency, you always use a histogram because you need percentiles. `average latency` is misleading — a P99 of 5s is a serious problem even if the average is 50ms. For current resource usage, you use a gauge.

**Q4: How do you set an SLO, and what do you do when the error budget runs out?**

Start with your current actual performance as a baseline — don't set an aspirational SLO you cannot currently meet. Review business impact: what error rate is acceptable to users? For a school API, 99.9% availability means 43 minutes of downtime per month — is that acceptable? Define the SLI precisely (which requests count, what counts as an error). Set the SLO slightly below what you currently achieve to give room for incidents. Document what "good enough" means. When the error budget runs out, the engineering team agreement (from Google SRE model) is to freeze feature work and focus entirely on reliability until the budget replenishes. This creates a direct business incentive to maintain reliability — new features stop if the system is unreliable.

**Q5: What is structured logging and why does it matter at scale?**

Structured logging means emitting logs as JSON objects with consistent, well-known fields — level, timestamp, message, context, traceId, userId — rather than free-form strings. At scale, logs are aggregated from hundreds of pods into Elasticsearch or similar. With unstructured logs, searching requires regex on free text, which is slow, fragile, and expensive. With structured logs, you can query `level:"error" AND context:"AuthService" AND userId:"12345"` directly on indexed fields. This is orders of magnitude faster. Additionally, structured logs enable automated alerting on log patterns, easy aggregation of error counts per service, and correlation with traces via the traceId field. Tools like Pino in Node.js output JSON natively with minimal performance overhead.

**Q6: What is the difference between a liveness probe and a readiness probe in Kubernetes, and what happens when each fails?**

A liveness probe checks whether the application process is alive and functioning. If it fails, Kubernetes restarts the pod. Use it for deadlock detection — the process is running but stuck and cannot make progress. A readiness probe checks whether the pod is ready to serve traffic. If it fails, Kubernetes removes the pod from the Service endpoints — no new requests are routed to it, but the pod is not restarted. Use it for startup (before the app finishes loading) and for temporary unavailability (e.g., DB connection lost). The practical difference: a failing readiness probe means "don't send me traffic right now," while a failing liveness probe means "something is fundamentally wrong, kill and restart me." A common mistake is making the readiness probe depend on external services — if your DB is down and the readiness probe fails, Kubernetes will remove all pods from load balancing, making a partial outage into a total outage. Liveness probes should be very lightweight (process is alive), readiness probes can check critical dependencies.
