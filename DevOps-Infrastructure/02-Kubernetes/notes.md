# Kubernetes — Interview Deep Dive

## Core Concepts

### What is Kubernetes?
```
Container orchestration platform. Manages deployment, scaling,
self-healing, and networking of containerized applications.

Key benefits:
  - Auto-scaling (HPA/VPA/KEDA)
  - Self-healing (restart crashed pods)
  - Rolling deployments (zero downtime)
  - Service discovery + load balancing
  - Secret and config management
```

### Architecture

```
┌─────────────────────── Control Plane ───────────────────────┐
│                                                              │
│  API Server      ←→  etcd (cluster state)                   │
│  (all requests)       (key-value, distributed)              │
│                                                              │
│  Scheduler       ←  kube-controller-manager                  │
│  (place pods)        (ReplicaSet, Node, Endpoint controllers)│
│                                                              │
└──────────────────────────────────────────────────────────────┘
                          │ kubelet (agent on each node)
       ┌──────────────────┼──────────────────┐
       ▼                  ▼                  ▼
  ┌─────────┐       ┌─────────┐       ┌─────────┐
  │ Node 1  │       │ Node 2  │       │ Node 3  │
  │ kubelet │       │ kubelet │       │ kubelet │
  │ kube-   │       │ kube-   │       │ kube-   │
  │ proxy   │       │ proxy   │       │ proxy   │
  │ [Pods]  │       │ [Pods]  │       │ [Pods]  │
  └─────────┘       └─────────┘       └─────────┘

etcd: Source of truth. All cluster state stored here.
API Server: Only component that reads/writes etcd.
Scheduler: Watches for unscheduled pods, picks best node.
Controller Manager: Control loops that maintain desired state.
kubelet: Node agent. Manages pod lifecycle on its node.
kube-proxy: Network rules on each node (iptables/IPVS).
```

---

## Core Objects

### Pod
```yaml
# Smallest deployable unit. One or more containers.
apiVersion: v1
kind: Pod
spec:
  containers:
  - name: api
    image: myapp:1.0
    ports:
    - containerPort: 3000
    resources:
      requests:
        memory: "128Mi"
        cpu: "250m"       # 250 millicores = 0.25 CPU
      limits:
        memory: "256Mi"
        cpu: "500m"
    livenessProbe:
      httpGet:
        path: /health
        port: 3000
      initialDelaySeconds: 30
      periodSeconds: 10
    readinessProbe:
      httpGet:
        path: /ready
        port: 3000
      initialDelaySeconds: 5
      periodSeconds: 5
```

### Deployment
```yaml
# Manages ReplicaSet → manages Pods. Declarative updates.
apiVersion: apps/v1
kind: Deployment
metadata:
  name: nestjs-api
spec:
  replicas: 3
  selector:
    matchLabels:
      app: nestjs-api
  strategy:
    type: RollingUpdate
    rollingUpdate:
      maxSurge: 1           # create 1 extra pod before killing old
      maxUnavailable: 0     # zero downtime
  template:
    metadata:
      labels:
        app: nestjs-api
    spec:
      containers:
      - name: api
        image: myapp:1.0
        envFrom:
        - configMapRef:
            name: api-config
        - secretRef:
            name: api-secrets
```

### Service (Discovery + Load Balancing)
```yaml
# ClusterIP: internal only (default)
apiVersion: v1
kind: Service
metadata:
  name: api-service
spec:
  selector:
    app: nestjs-api
  ports:
  - port: 80
    targetPort: 3000
  type: ClusterIP     # NodePort | LoadBalancer | ExternalName

# LoadBalancer: provisions cloud LB (AWS ELB, GCP LB)
# NodePort: exposes on each node's IP:port (30000-32767)
# ClusterIP: internal only — use with Ingress for external
```

### Ingress
```yaml
# HTTP routing to services (layer 7 LB)
apiVersion: networking.k8s.io/v1
kind: Ingress
metadata:
  name: api-ingress
  annotations:
    nginx.ingress.kubernetes.io/ssl-redirect: "true"
    cert-manager.io/cluster-issuer: letsencrypt
spec:
  tls:
  - hosts:
    - api.myapp.com
    secretName: api-tls
  rules:
  - host: api.myapp.com
    http:
      paths:
      - path: /api
        pathType: Prefix
        backend:
          service:
            name: api-service
            port:
              number: 80
```

---

## Scaling

### Horizontal Pod Autoscaler (HPA)
```yaml
apiVersion: autoscaling/v2
kind: HorizontalPodAutoscaler
spec:
  scaleTargetRef:
    apiVersion: apps/v1
    kind: Deployment
    name: nestjs-api
  minReplicas: 2
  maxReplicas: 20
  metrics:
  - type: Resource
    resource:
      name: cpu
      target:
        type: Utilization
        averageUtilization: 70   # scale up when CPU > 70%
  - type: Resource
    resource:
      name: memory
      target:
        type: Utilization
        averageUtilization: 80

# HPA algorithm:
# desiredReplicas = ceil(currentReplicas × currentMetric/targetMetric)
# e.g., 3 pods at 90% CPU, target 70%: ceil(3 × 90/70) = ceil(3.86) = 4 pods
```

### KEDA (Event-Driven Autoscaling)
```yaml
# Scale based on Kafka lag, SQS queue depth, Redis list length, etc.
apiVersion: keda.sh/v1alpha1
kind: ScaledObject
spec:
  scaleTargetRef:
    name: worker-deployment
  triggers:
  - type: kafka
    metadata:
      topic: orders
      consumerGroup: order-processor
      lagThreshold: "100"   # scale up when lag > 100 messages
```

---

## ConfigMap & Secrets

```yaml
# ConfigMap — non-sensitive config
apiVersion: v1
kind: ConfigMap
metadata:
  name: api-config
data:
  NODE_ENV: production
  LOG_LEVEL: info
  DB_PORT: "5432"

# Secret — base64 encoded (NOT encrypted by default!)
# Use sealed-secrets or Vault for real encryption
apiVersion: v1
kind: Secret
metadata:
  name: api-secrets
type: Opaque
data:
  DB_PASSWORD: cGFzc3dvcmQ=   # base64("password")
  JWT_SECRET: c2VjcmV0         # base64("secret")
```

---

## Health Probes — Deep Dive

```
Liveness Probe: Is the app alive? If fails → restart container.
  Use for: deadlock detection, crash recovery.
  Don't: check DB connectivity (DB being down ≠ app should restart)

Readiness Probe: Is the app ready to receive traffic? If fails → remove from LB.
  Use for: warm-up period, DB connection established.
  This is how zero-downtime rolling updates work.

Startup Probe: Has the app started? Disables liveness until passes.
  Use for: slow-starting apps (JVM, Python).

Typical pattern:
  startupProbe:   httpGet /health, failureThreshold: 30, period: 10s  (300s max start)
  livenessProbe:  httpGet /health, period: 10s
  readinessProbe: httpGet /ready, period: 5s
```

---

## Namespaces & RBAC

```yaml
# Namespace — logical separation (dev, staging, prod)
# Resource quotas per namespace
apiVersion: v1
kind: ResourceQuota
metadata:
  name: dev-quota
  namespace: development
spec:
  hard:
    requests.cpu: "4"
    requests.memory: 4Gi
    limits.cpu: "8"
    limits.memory: 8Gi
    pods: "20"

# RBAC — Role-Based Access Control
apiVersion: rbac.authorization.k8s.io/v1
kind: Role
metadata:
  namespace: production
  name: pod-reader
rules:
- apiGroups: [""]
  resources: ["pods", "pods/logs"]
  verbs: ["get", "list", "watch"]
---
kind: RoleBinding
metadata:
  name: read-pods-binding
subjects:
- kind: ServiceAccount
  name: ci-pipeline
  namespace: production
roleRef:
  kind: Role
  name: pod-reader
  apiGroup: rbac.authorization.k8s.io
```

---

## Persistent Storage

```yaml
# PersistentVolumeClaim
apiVersion: v1
kind: PersistentVolumeClaim
metadata:
  name: postgres-pvc
spec:
  accessModes:
    - ReadWriteOnce          # one pod at a time
    # ReadWriteMany: NFS, EFS (multiple pods)
    # ReadOnlyMany: multiple pods read
  storageClassName: gp3     # AWS gp3 SSD
  resources:
    requests:
      storage: 20Gi

# Use in Pod:
volumes:
- name: postgres-storage
  persistentVolumeClaim:
    claimName: postgres-pvc
containers:
- volumeMounts:
  - mountPath: /var/lib/postgresql/data
    name: postgres-storage
```

---

## StatefulSet (for Databases)

```yaml
# StatefulSet: stable pod identity (pod-0, pod-1, pod-2)
# Ordered creation/deletion. Stable persistent storage per pod.
# Use for: databases, Kafka, ZooKeeper — anything needing stable identity

apiVersion: apps/v1
kind: StatefulSet
metadata:
  name: postgres
spec:
  serviceName: postgres-headless   # headless service for DNS
  replicas: 3
  template:
    spec:
      containers:
      - name: postgres
        image: postgres:15
  volumeClaimTemplates:            # auto-creates PVC per pod
  - metadata:
      name: data
    spec:
      accessModes: ["ReadWriteOnce"]
      resources:
        requests:
          storage: 10Gi
```

---

## Useful kubectl Commands

```bash
# Deploy / update
kubectl apply -f deployment.yaml
kubectl set image deployment/api api=myapp:2.0    # update image
kubectl rollout status deployment/api             # watch rollout
kubectl rollout undo deployment/api               # rollback

# Debug
kubectl get pods -n production
kubectl describe pod api-7d9f4b-xyz -n production
kubectl logs api-7d9f4b-xyz --previous --tail=100    # previous container logs
kubectl exec -it api-7d9f4b-xyz -- bash
kubectl port-forward pod/api-7d9f4b-xyz 8080:3000   # local → pod

# Scale
kubectl scale deployment api --replicas=5
kubectl top pods                                     # CPU/memory usage

# Events (most useful for debugging)
kubectl get events --sort-by=.lastTimestamp -n production
```

---

## Interview Questions

**Q: What is the difference between Deployment and StatefulSet?**
A: Deployment: pods are interchangeable, random names, shared PVC or no PVC. StatefulSet: pods have stable names (pod-0, pod-1), stable network identity, each gets its own PVC. Use StatefulSet for databases (each replica needs unique storage), Kafka, Zookeeper.

**Q: How does a rolling update work?**
A: Kubernetes creates new pods (maxSurge), waits for readiness probe to pass, then terminates old pods (maxUnavailable). With maxSurge=1, maxUnavailable=0: always has N pods available, briefly has N+1. Readiness probe is the key — new pod must be healthy before old is killed.

**Q: What happens when a node goes down?**
A: (1) kubelet on that node stops sending heartbeats. (2) Node controller marks node NotReady after 40s. (3) After 5 minutes (pod-eviction-timeout), pods on dead node are rescheduled. (4) Scheduler places pods on healthy nodes. PDB (PodDisruptionBudget) ensures minimum pods always available.

**Q: How do you handle DB migrations in Kubernetes?**
A: Run migration as a Kubernetes Job (not in app startup — race condition with multiple replicas). initContainer pattern: `kubectl apply -f migration-job.yaml && kubectl wait job/migration`. Or use Helm hooks: pre-upgrade Job that runs migration before new app pods start.

**Q: How does kube-proxy work?**
A: Maintains iptables/IPVS rules on each node. Service IP (ClusterIP) → iptables rule → randomly selects one pod IP. With IPVS mode: kernel-level hash table, O(1) lookup vs O(N) iptables. For 1000+ services, use IPVS mode.
