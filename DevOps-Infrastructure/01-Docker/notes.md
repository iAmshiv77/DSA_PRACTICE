# Docker — Interview Deep Dive

## Core Concepts

### What is Docker?
```
Docker packages an application + its dependencies into a CONTAINER.
Container = isolated process on the host OS (NOT a full VM).
Uses: Linux namespaces (isolation) + cgroups (resource limits).

VM vs Container:
  VM:        App → OS → Hypervisor → Host OS → Hardware
             Each VM has full OS kernel (~1 GB)
  Container: App → Docker Engine → Host OS (shared kernel) → Hardware
             Container = isolated process (~10 MB)

Benefit: "Works on my machine" problem solved. Environment packaged with code.
```

### Docker Architecture
```
Docker Client (CLI) → Docker Daemon (dockerd) → containerd → runc

Docker Daemon: manages images, containers, volumes, networks
containerd: container runtime (pulls images, manages lifecycle)
runc: low-level OCI-compliant runtime (creates namespaces, cgroups)

Image: Read-only template (layers)
Container: Running instance of an image (adds writable layer on top)
Registry: Store of images (Docker Hub, ECR, GCR)
```

---

## Dockerfile Best Practices

```dockerfile
# ── GOOD Dockerfile (production-grade) ──────────────────────

# Use specific versions (not :latest) — reproducible builds
FROM node:20.11-alpine3.19

# Metadata
LABEL maintainer="team@company.com"
LABEL version="1.0.0"

# Create non-root user (security)
RUN addgroup -S appgroup && adduser -S appuser -G appgroup

WORKDIR /app

# Copy dependency files FIRST (cache optimization)
# If package.json doesn't change → this layer stays cached
COPY package.json package-lock.json ./

# Install dependencies
RUN npm ci --only=production && \
    npm cache clean --force

# Copy source after (this layer invalidated when code changes)
COPY --chown=appuser:appgroup . .

# Switch to non-root user
USER appuser

# Document exposed port (documentation only — doesn't publish)
EXPOSE 3000

# Healthcheck
HEALTHCHECK --interval=30s --timeout=10s --start-period=40s --retries=3 \
  CMD wget --no-verbose --tries=1 --spider http://localhost:3000/health || exit 1

# Use exec form (not shell form) — receives signals directly
CMD ["node", "dist/main.js"]
```

### Layer Caching — Critical for Fast Builds
```
Rule: Put LEAST frequently changing files FIRST, MOST changing LAST.

Wrong order (slow):
  COPY . .            ← code changes every build → invalidates ALL below
  RUN npm install     ← reinstalls every time

Right order (fast):
  COPY package.json .
  RUN npm install     ← cached unless package.json changes
  COPY . .            ← only this layer rebuilds on code change

Each RUN command = one layer. Combine related commands:
  RUN apt-get update && \
      apt-get install -y curl && \
      rm -rf /var/lib/apt/lists/*   ← clean apt cache IN SAME LAYER
```

### Multi-Stage Builds
```dockerfile
# Stage 1: Build (heavy — has build tools)
FROM node:20-alpine AS builder
WORKDIR /app
COPY package*.json ./
RUN npm ci
COPY . .
RUN npm run build          # compiles TypeScript

# Stage 2: Production (lightweight — no dev dependencies, no source)
FROM node:20-alpine AS production
WORKDIR /app
COPY --from=builder /app/dist ./dist
COPY --from=builder /app/node_modules ./node_modules
COPY package.json .
USER node
CMD ["node", "dist/main.js"]

# Result: 800MB build image → 150MB production image
```

---

## Docker Networking

```
Bridge (default):
  Containers get virtual IP (172.17.0.x)
  Can communicate with each other by name (on user-defined bridge)
  Port mapping: -p 8080:3000 (host:container)

Host:
  Container shares host network namespace
  No isolation, maximum performance
  Use: very latency-sensitive (monitoring agents)

None:
  No network access. Fully isolated.

Overlay:
  Multi-host networking (Docker Swarm, Kubernetes)
  Encrypted VXLAN tunnel between hosts

User-defined bridge (recommended):
  docker network create mynet
  docker run --network mynet --name api myimage
  docker run --network mynet --name db postgres
  # api can reach db by hostname "db"
```

---

## Docker Compose

```yaml
# docker-compose.yml — NestJS app with PostgreSQL + Redis

version: '3.9'

services:
  api:
    build:
      context: .
      dockerfile: Dockerfile
      target: production       # multi-stage target
    image: myapp:latest
    container_name: nestjs_api
    ports:
      - "3000:3000"
    environment:
      NODE_ENV: production
      DB_HOST: postgres
      REDIS_HOST: redis
    env_file:
      - .env.production
    depends_on:
      postgres:
        condition: service_healthy
      redis:
        condition: service_started
    restart: unless-stopped
    networks:
      - app-network
    deploy:
      resources:
        limits:
          cpus: '1.0'
          memory: 512M
        reservations:
          memory: 256M

  postgres:
    image: postgres:15-alpine
    container_name: postgres_db
    environment:
      POSTGRES_USER: ${DB_USER}
      POSTGRES_PASSWORD: ${DB_PASS}
      POSTGRES_DB: ${DB_NAME}
    volumes:
      - postgres_data:/var/lib/postgresql/data
      - ./init.sql:/docker-entrypoint-initdb.d/init.sql  # auto-run on first start
    healthcheck:
      test: ["CMD-SHELL", "pg_isready -U ${DB_USER} -d ${DB_NAME}"]
      interval: 10s
      timeout: 5s
      retries: 5
    networks:
      - app-network

  redis:
    image: redis:7-alpine
    container_name: redis_cache
    command: redis-server --requirepass ${REDIS_PASS} --maxmemory 256mb --maxmemory-policy allkeys-lru
    volumes:
      - redis_data:/data
    networks:
      - app-network

volumes:
  postgres_data:
  redis_data:

networks:
  app-network:
    driver: bridge
```

---

## Container Internals

### Linux Namespaces (Isolation)
```
PID namespace:   Container sees its own PID 1 (not host's init)
Network ns:      Own network interfaces, routing tables, ports
Mount ns:        Own filesystem view
UTS ns:          Own hostname
IPC ns:          Own interprocess communication resources
User ns:         Map container root → unprivileged host user (rootless)
Cgroup ns:       Own cgroup hierarchy view
```

### cgroups (Resource Limits)
```
Control Groups limit what a container can USE:
  CPU:    --cpus="0.5"    → max 50% of one CPU
  Memory: --memory="256m" → OOM-killed if exceeds 256MB
  Disk:   --blkio-weight  → relative I/O weight
  Network: (tc, not cgroup) → rate limiting

docker run --cpus="2" --memory="1g" --memory-swap="1g" myimage
  # memory-swap = memory: no swap allowed (good for predictability)
```

---

## Security Best Practices

```
1. Never run as root
   USER nonroot in Dockerfile

2. Read-only filesystem
   docker run --read-only --tmpfs /tmp myimage

3. Drop capabilities
   docker run --cap-drop ALL --cap-add NET_BIND_SERVICE myimage

4. No privileged mode (gives root on host)
   Never: docker run --privileged (unless you really need it)

5. Scan images for vulnerabilities
   docker scout cves myimage
   trivy image myimage

6. Use specific image tags
   FROM node:20.11.1-alpine3.19  (not :latest)

7. Secret management
   Never ENV SECRET_KEY=value in Dockerfile (visible in image history)
   Use Docker secrets or external vault (Vault, AWS Secrets Manager)

8. Network segmentation
   Don't expose all containers to internet
   Only the load balancer should be publicly accessible
```

---

## Useful Commands

```bash
# Build
docker build -t myapp:1.0 --no-cache .
docker build --target production -t myapp:prod .

# Run
docker run -d --name myapp -p 3000:3000 --env-file .env myapp:1.0
docker run -it --rm ubuntu bash               # interactive + auto-remove

# Debug
docker exec -it myapp bash                    # enter running container
docker logs myapp --follow --tail 100         # live logs
docker inspect myapp                          # full container metadata
docker stats                                  # live resource usage

# Images
docker images
docker rmi myapp:1.0
docker image prune -f                         # remove dangling images

# Volumes
docker volume create mydata
docker volume ls
docker run -v mydata:/app/data myapp

# Cleanup
docker system prune -af --volumes            # WARNING: removes everything!
docker container prune                        # remove stopped containers
```

---

## Interview Questions

**Q: What is the difference between CMD and ENTRYPOINT?**
A: CMD: default command, overridden by `docker run ... <command>`. ENTRYPOINT: always runs, CMD is appended as arguments. Use ENTRYPOINT for the main executable, CMD for default args. `docker run myimage --port 8080` with `ENTRYPOINT ["node", "server.js"]` → runs `node server.js --port 8080`.

**Q: How does a container differ from a VM at the kernel level?**
A: VM has its own kernel (hypervisor provides virtual hardware). Container shares host kernel, isolated via namespaces and cgroups. No separate kernel → lighter, faster startup (ms vs seconds), less memory (MB vs GB). But: containers must be compatible with host kernel. Cannot run Windows container on Linux host (without a VM layer).

**Q: How would you handle secrets in Docker?**
A: Never in ENV vars (visible in `docker inspect`). Never baked into image (visible in `docker history`). Options: (1) Docker Secrets (Swarm), (2) Kubernetes Secrets (encrypted in etcd), (3) Vault sidecar pattern, (4) AWS Secrets Manager + IAM role. Runtime injection via environment-specific config, not image.

**Q: Container shows OOM (Out of Memory) — how do you investigate?**
A: (1) `docker stats` — live memory usage. (2) Check `--memory` limit set too low. (3) Application memory leak: check heap profile. (4) `dmesg | grep oom-killer` — kernel OOM log. Fix: increase memory limit OR profile app for memory leak.
