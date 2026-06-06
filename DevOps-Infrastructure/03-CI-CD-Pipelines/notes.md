# CI/CD Pipelines

## CI vs Continuous Delivery vs Continuous Deployment

### Continuous Integration (CI)
The practice of frequently merging developer code changes into a shared repository, with each merge triggering an automated build and test suite. The goal is to detect integration problems early.

Key activities in CI:
- Source code compilation
- Unit tests, integration tests
- Static code analysis (linting, SAST)
- Dependency vulnerability scanning
- Code coverage reporting

### Continuous Delivery (CD — Delivery)
Extends CI by automatically preparing every passing build for release to production. The deployment itself requires a **manual approval gate**. The artifact is always in a deployable state.

Flow: Code push → CI pipeline passes → artifact staged → **human clicks deploy**

### Continuous Deployment (CD — Deployment)
Every passing build is automatically deployed to production without manual intervention. Requires mature test coverage and strong monitoring/rollback capabilities.

Flow: Code push → CI pipeline passes → automatically live in production

### Comparison Table

| Property | CI | Continuous Delivery | Continuous Deployment |
|---|---|---|---|
| Automated build + test | Yes | Yes | Yes |
| Artifact always deployable | No guarantee | Yes | Yes |
| Deploy to prod | Manual, ad hoc | Manual trigger | Fully automatic |
| Human gate | None in pipeline | Before prod deploy | None |
| Risk | Higher (infrequent merge) | Medium | Low (if tests are mature) |
| Speed to prod | Slow | Days | Minutes |

---

## GitHub Actions — Complete Production Pipeline

### Project structure assumption
- NestJS app, Dockerized
- Push to ECR
- Deploy to Kubernetes via `kubectl`

```yaml
# .github/workflows/ci-cd.yml
name: CI/CD Pipeline

on:
  push:
    branches: [main, develop]
  pull_request:
    branches: [main]

env:
  AWS_REGION: ap-south-1
  ECR_REGISTRY: 123456789012.dkr.ecr.ap-south-1.amazonaws.com
  ECR_REPOSITORY: super-schools-backend
  K8S_NAMESPACE: production

jobs:
  # ─── Job 1: Lint and type-check (fast fail) ───────────────────────────────
  lint:
    name: Lint & Type Check
    runs-on: ubuntu-24.04
    steps:
      - name: Checkout
        uses: actions/checkout@v4

      - name: Setup Node
        uses: actions/setup-node@v4
        with:
          node-version: '20'
          cache: 'npm'

      - name: Install dependencies
        run: npm ci

      - name: Run ESLint
        run: npm run lint

      - name: Run TypeScript type check
        run: npm run typecheck

  # ─── Job 2: Unit tests ────────────────────────────────────────────────────
  test-unit:
    name: Unit Tests
    runs-on: ubuntu-24.04
    needs: lint
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-node@v4
        with:
          node-version: '20'
          cache: 'npm'

      - name: Install dependencies
        run: npm ci

      - name: Run unit tests with coverage
        run: npm run test:cov
        env:
          NODE_ENV: test

      - name: Upload coverage to Codecov
        uses: codecov/codecov-action@v4
        with:
          token: ${{ secrets.CODECOV_TOKEN }}
          files: ./coverage/lcov.info
          fail_ci_if_error: true

  # ─── Job 3: Integration tests (needs real services) ──────────────────────
  test-integration:
    name: Integration Tests
    runs-on: ubuntu-24.04
    needs: lint
    services:
      postgres:
        image: postgres:16-alpine
        env:
          POSTGRES_DB: test_db
          POSTGRES_USER: test_user
          POSTGRES_PASSWORD: test_pass
        ports:
          - 5432:5432
        options: >-
          --health-cmd pg_isready
          --health-interval 10s
          --health-timeout 5s
          --health-retries 5

      redis:
        image: redis:7-alpine
        ports:
          - 6379:6379
        options: >-
          --health-cmd "redis-cli ping"
          --health-interval 10s
          --health-timeout 5s
          --health-retries 5

    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-node@v4
        with:
          node-version: '20'
          cache: 'npm'

      - name: Install dependencies
        run: npm ci

      - name: Run database migrations
        run: npm run migration:run
        env:
          DB_HOST: localhost
          DB_PORT: 5432
          DB_NAME: test_db
          DB_USER: test_user
          DB_PASSWORD: test_pass

      - name: Run integration tests
        run: npm run test:e2e
        env:
          DB_HOST: localhost
          DB_PORT: 5432
          DB_NAME: test_db
          DB_USER: test_user
          DB_PASSWORD: test_pass
          REDIS_HOST: localhost
          REDIS_PORT: 6379
          JWT_SECRET: test-jwt-secret-not-for-production
          NODE_ENV: test

  # ─── Job 4: Security scanning ─────────────────────────────────────────────
  security-scan:
    name: Security Scan
    runs-on: ubuntu-24.04
    needs: lint
    steps:
      - uses: actions/checkout@v4

      - name: Run Trivy vulnerability scanner (filesystem)
        uses: aquasecurity/trivy-action@master
        with:
          scan-type: fs
          scan-ref: .
          severity: CRITICAL,HIGH
          exit-code: 1
          format: table

      - name: Audit npm dependencies
        run: npm audit --audit-level=high

  # ─── Job 5: Build Docker image and push to ECR ────────────────────────────
  build-and-push:
    name: Build & Push to ECR
    runs-on: ubuntu-24.04
    needs: [test-unit, test-integration, security-scan]
    if: github.ref == 'refs/heads/main' && github.event_name == 'push'
    outputs:
      image-tag: ${{ steps.meta.outputs.tags }}
      image-digest: ${{ steps.build.outputs.digest }}

    steps:
      - uses: actions/checkout@v4

      - name: Configure AWS credentials
        uses: aws-actions/configure-aws-credentials@v4
        with:
          aws-access-key-id: ${{ secrets.AWS_ACCESS_KEY_ID }}
          aws-secret-access-key: ${{ secrets.AWS_SECRET_ACCESS_KEY }}
          aws-region: ${{ env.AWS_REGION }}

      - name: Login to Amazon ECR
        id: login-ecr
        uses: aws-actions/amazon-ecr-login@v2

      - name: Extract metadata for Docker
        id: meta
        uses: docker/metadata-action@v5
        with:
          images: ${{ env.ECR_REGISTRY }}/${{ env.ECR_REPOSITORY }}
          tags: |
            type=sha,prefix=sha-,format=short
            type=ref,event=branch
            type=semver,pattern={{version}}

      - name: Set up Docker Buildx
        uses: docker/setup-buildx-action@v3

      - name: Build and push Docker image
        id: build
        uses: docker/build-push-action@v5
        with:
          context: .
          push: true
          tags: ${{ steps.meta.outputs.tags }}
          labels: ${{ steps.meta.outputs.labels }}
          cache-from: type=gha
          cache-to: type=gha,mode=max
          build-args: |
            NODE_ENV=production
            BUILD_DATE=${{ github.event.head_commit.timestamp }}
            GIT_COMMIT=${{ github.sha }}

      - name: Scan pushed image with Trivy
        uses: aquasecurity/trivy-action@master
        with:
          image-ref: ${{ env.ECR_REGISTRY }}/${{ env.ECR_REPOSITORY }}:sha-${{ github.sha }}
          severity: CRITICAL
          exit-code: 1

  # ─── Job 6: Deploy to Kubernetes ──────────────────────────────────────────
  deploy:
    name: Deploy to Kubernetes
    runs-on: ubuntu-24.04
    needs: build-and-push
    environment:
      name: production
      url: https://api.superschools.app

    steps:
      - uses: actions/checkout@v4

      - name: Configure AWS credentials
        uses: aws-actions/configure-aws-credentials@v4
        with:
          aws-access-key-id: ${{ secrets.AWS_ACCESS_KEY_ID }}
          aws-secret-access-key: ${{ secrets.AWS_SECRET_ACCESS_KEY }}
          aws-region: ${{ env.AWS_REGION }}

      - name: Update kubeconfig for EKS
        run: |
          aws eks update-kubeconfig \
            --region ${{ env.AWS_REGION }} \
            --name super-schools-cluster

      - name: Set image tag in kustomization
        run: |
          cd k8s/overlays/production
          kustomize edit set image \
            backend=${{ env.ECR_REGISTRY }}/${{ env.ECR_REPOSITORY }}:sha-${{ github.sha }}

      - name: Apply Kubernetes manifests
        run: |
          kubectl apply -k k8s/overlays/production

      - name: Wait for rollout
        run: |
          kubectl rollout status deployment/backend \
            -n ${{ env.K8S_NAMESPACE }} \
            --timeout=300s

      - name: Verify deployment health
        run: |
          kubectl get pods -n ${{ env.K8S_NAMESPACE }} -l app=backend
          kubectl run health-check \
            --image=curlimages/curl:latest \
            --restart=Never \
            --rm -it \
            -- curl -sf https://api.superschools.app/health
```

### Dockerfile for the build above

```dockerfile
# Dockerfile
FROM node:20-alpine AS builder
WORKDIR /app
COPY package*.json ./
RUN npm ci --only=production=false
COPY . .
RUN npm run build

FROM node:20-alpine AS production
WORKDIR /app
ENV NODE_ENV=production
RUN addgroup -g 1001 -S nodejs && adduser -S nestjs -u 1001
COPY package*.json ./
RUN npm ci --only=production && npm cache clean --force
COPY --from=builder --chown=nestjs:nodejs /app/dist ./dist
USER nestjs
EXPOSE 3000
HEALTHCHECK --interval=30s --timeout=10s --start-period=30s \
  CMD wget -qO- http://localhost:3000/health || exit 1
CMD ["node", "dist/main.js"]
```

---

## GitLab CI Example

```yaml
# .gitlab-ci.yml
stages:
  - validate
  - test
  - build
  - deploy-staging
  - deploy-production

variables:
  DOCKER_DRIVER: overlay2
  DOCKER_TLS_CERTDIR: "/certs"
  IMAGE_TAG: $CI_REGISTRY_IMAGE:$CI_COMMIT_SHORT_SHA

# ─── Reusable anchors ─────────────────────────────────────────────────────
.node_template: &node_template
  image: node:20-alpine
  cache:
    key:
      files:
        - package-lock.json
    paths:
      - node_modules/
  before_script:
    - npm ci

# ─── Stage: validate ──────────────────────────────────────────────────────
lint:
  <<: *node_template
  stage: validate
  script:
    - npm run lint
    - npm run typecheck
  rules:
    - if: $CI_PIPELINE_SOURCE == "merge_request_event"
    - if: $CI_COMMIT_BRANCH == "main"

# ─── Stage: test ──────────────────────────────────────────────────────────
unit-tests:
  <<: *node_template
  stage: test
  script:
    - npm run test:cov
  coverage: '/Lines\s*:\s*(\d+\.\d+)%/'
  artifacts:
    reports:
      coverage_report:
        coverage_format: cobertura
        path: coverage/cobertura-coverage.xml
    expire_in: 1 week

integration-tests:
  <<: *node_template
  stage: test
  services:
    - name: postgres:16-alpine
      alias: postgres
    - name: redis:7-alpine
      alias: redis
  variables:
    POSTGRES_DB: test
    POSTGRES_USER: test
    POSTGRES_PASSWORD: test
    DB_HOST: postgres
    REDIS_HOST: redis
  script:
    - npm run migration:run
    - npm run test:e2e

# ─── Stage: build ─────────────────────────────────────────────────────────
build-image:
  stage: build
  image: docker:24
  services:
    - docker:24-dind
  script:
    - docker login -u $CI_REGISTRY_USER -p $CI_REGISTRY_PASSWORD $CI_REGISTRY
    - docker build -t $IMAGE_TAG .
    - docker push $IMAGE_TAG
    - docker tag $IMAGE_TAG $CI_REGISTRY_IMAGE:latest
    - docker push $CI_REGISTRY_IMAGE:latest
  only:
    - main

# ─── Stage: deploy-staging ────────────────────────────────────────────────
deploy-staging:
  stage: deploy-staging
  image: bitnami/kubectl:latest
  environment:
    name: staging
    url: https://staging-api.superschools.app
  script:
    - kubectl config use-context staging
    - kubectl set image deployment/backend backend=$IMAGE_TAG -n staging
    - kubectl rollout status deployment/backend -n staging --timeout=180s
  only:
    - main

# ─── Stage: deploy-production (manual gate) ───────────────────────────────
deploy-production:
  stage: deploy-production
  image: bitnami/kubectl:latest
  environment:
    name: production
    url: https://api.superschools.app
  when: manual
  script:
    - kubectl config use-context production
    - kubectl set image deployment/backend backend=$IMAGE_TAG -n production
    - kubectl rollout status deployment/backend -n production --timeout=300s
  only:
    - main
```

---

## Pipeline Best Practices

### Fail Fast
Put the cheapest, most likely-to-fail checks first. A lint failure should be known in 30 seconds, not after a 10-minute Docker build.

```
Order: lint → type-check → unit tests → integration tests → build → security scan → deploy
```

Never run a Docker build if unit tests fail — that wastes compute and developer time.

### Parallel Jobs
Independent jobs should run concurrently. In GitHub Actions use `needs` to express the DAG explicitly:

```yaml
jobs:
  lint:           # no needs — runs immediately
  test-unit:
    needs: lint   # starts after lint
  test-integration:
    needs: lint   # also starts after lint (parallel with test-unit)
  security-scan:
    needs: lint   # also parallel
  build:
    needs: [test-unit, test-integration, security-scan]  # waits for all three
  deploy:
    needs: build
```

This cuts total pipeline time significantly vs sequential execution.

### Environment Promotion
Code must pass through environments in order: `dev → staging → production`. Never skip staging for features, only hotfixes with explicit override.

```
main branch push → auto deploy to staging → smoke tests → manual gate → production
```

Use GitHub Environments with protection rules:
- Production environment: require 1 reviewer approval
- Required status checks: all CI jobs must pass
- Deployment branches: only `main`

### Artifact Immutability
Build the Docker image once, promote the same digest through environments. Never rebuild for staging vs production — you must deploy exactly what you tested.

```yaml
# Store the digest, not just a tag
- name: Output digest
  run: echo "DIGEST=${{ steps.build.outputs.digest }}" >> $GITHUB_OUTPUT

# In deploy job, use the digest
image: $ECR_REGISTRY/$ECR_REPO@${{ needs.build.outputs.digest }}
```

### Caching
```yaml
# GitHub Actions — cache node_modules across runs
- uses: actions/setup-node@v4
  with:
    node-version: '20'
    cache: 'npm'

# Docker layer caching with Buildx
- uses: docker/build-push-action@v5
  with:
    cache-from: type=gha
    cache-to: type=gha,mode=max
```

---

## Deployment Strategies

### Blue-Green Deployment

Two identical production environments. "Blue" is live, "Green" is the new version. Traffic is switched instantly at the load balancer level.

```
           ┌─────────────┐
           │ Load Balancer│
           └──────┬───────┘
                  │ 100% traffic
         ┌────────▼────────┐
         │   Blue (v1.0)   │  ← currently live
         └─────────────────┘

         ┌─────────────────┐
         │  Green (v1.1)   │  ← new version, idle
         └─────────────────┘

After switch: Load Balancer points to Green. Blue kept warm for instant rollback.
```

Pros: instant rollback (flip back to blue), zero downtime, easy to test green before switch
Cons: requires 2x infrastructure cost at all times

In Kubernetes:

```yaml
# Two separate Deployments, one Service that switches selector
apiVersion: v1
kind: Service
metadata:
  name: backend
spec:
  selector:
    app: backend
    slot: green    # change to 'blue' to switch back
  ports:
    - port: 80
      targetPort: 3000
```

### Canary Deployment

New version receives a small percentage of traffic initially. Gradually increase as confidence grows.

```
100% traffic (before):  [v1.0][v1.0][v1.0][v1.0][v1.0]
Canary phase 1 (5%):    [v1.0][v1.0][v1.0][v1.0][v1.1]
Canary phase 2 (20%):   [v1.0][v1.0][v1.0][v1.1][v1.1]
Canary phase 3 (50%):   [v1.0][v1.0][v1.1][v1.1][v1.1]
Full rollout (100%):    [v1.1][v1.1][v1.1][v1.1][v1.1]
```

In Kubernetes with weight-based routing (using Argo Rollouts):

```yaml
apiVersion: argoproj.io/v1alpha1
kind: Rollout
metadata:
  name: backend
spec:
  strategy:
    canary:
      steps:
        - setWeight: 5
        - pause: { duration: 5m }   # observe error rates
        - setWeight: 20
        - pause: { duration: 10m }
        - setWeight: 50
        - pause: { duration: 10m }
        - setWeight: 100
      canaryMetadata:
        labels:
          role: canary
      stableMetadata:
        labels:
          role: stable
```

Pros: real production traffic validation, gradual blast radius
Cons: complex, requires good metrics to know when to abort

### Rolling Deployment

Kubernetes default. Pods are replaced gradually. At any point during the rollout, both old and new versions are running.

```yaml
apiVersion: apps/v1
kind: Deployment
spec:
  replicas: 10
  strategy:
    type: RollingUpdate
    rollingUpdate:
      maxSurge: 2          # allow 2 extra pods during rollout
      maxUnavailable: 1    # at most 1 pod unavailable at a time
```

Timeline: replace 1 pod, wait for it to be Ready, replace next — until all pods run new version.

Pros: no extra infrastructure needed, built-in to Kubernetes
Cons: both versions run simultaneously (API must be backward compatible), slower rollback than blue-green

### Strategy Comparison

| Property | Blue-Green | Canary | Rolling |
|---|---|---|---|
| Infrastructure cost | 2x | ~1.1x | 1x |
| Rollback speed | Instant | Fast | Slow (re-roll) |
| Blast radius | All-or-nothing | Small initially | Grows gradually |
| API backward compat required | No | Yes | Yes |
| Traffic control | Binary | Percentage | Pod count |
| Complexity | Medium | High | Low |

---

## Feature Flags

Feature flags decouple deployment from release. Code ships to production in a disabled state; a flag enables it without a new deployment.

### Implementation Pattern (NestJS)

```typescript
// feature-flag.service.ts
@Injectable()
export class FeatureFlagService {
  constructor(private readonly redis: Redis) {}

  async isEnabled(flagName: string, userId?: string): Promise<boolean> {
    // Global flag check
    const globalValue = await this.redis.get(`feature:${flagName}`);
    if (globalValue === 'true') return true;
    if (globalValue === 'false') return false;

    // Per-user rollout (percentage-based)
    const percentage = await this.redis.get(`feature:${flagName}:rollout`);
    if (percentage && userId) {
      const hash = parseInt(userId, 10) % 100;
      return hash < parseInt(percentage, 10);
    }

    return false;
  }
}

// Usage in service
const newCheckoutEnabled = await this.featureFlags.isEnabled('new-checkout', user.id);
if (newCheckoutEnabled) {
  return this.newCheckoutFlow(cart);
}
return this.legacyCheckoutFlow(cart);
```

### LaunchDarkly / OpenFeature SDK approach

```typescript
import { OpenFeature } from '@openfeature/server-sdk';

const client = OpenFeature.getClient();

const showNewDashboard = await client.getBooleanValue(
  'new-dashboard',
  false,  // default if flag unreachable
  { targetingKey: user.id }
);
```

Use cases:
- A/B testing
- Gradual rollout to percentage of users
- Kill switch for a broken feature
- Beta access for specific user IDs
- Dark launching (code runs but output is hidden)

---

## Secrets Management in CI

### What never to do
- Never commit secrets to the repository, even in private repos
- Never put secrets in Dockerfile `ENV` instructions (they appear in `docker history`)
- Never log secrets (mask all secret env vars in CI)
- Never store secrets in CI job artifacts

### GitHub Actions secrets

```yaml
# Set in GitHub → Repository → Settings → Secrets and variables → Actions
# Reference in workflow:
env:
  AWS_ACCESS_KEY_ID: ${{ secrets.AWS_ACCESS_KEY_ID }}
  DB_PASSWORD: ${{ secrets.PRODUCTION_DB_PASSWORD }}

# For org-level secrets shared across repos:
  SHARED_TOKEN: ${{ secrets.ORG_SHARED_TOKEN }}
```

Secrets are masked in logs automatically. GitHub replaces the value with `***`.

### AWS Secrets Manager in CI

```yaml
- name: Load secrets from AWS Secrets Manager
  uses: aws-actions/aws-secretsmanager-get-secrets@v2
  with:
    secret-ids: |
      PROD_DB_CREDS, arn:aws:secretsmanager:ap-south-1:123456789:secret:prod/db
    parse-json-secrets: true
    # This creates env vars: PROD_DB_CREDS_DB_PASSWORD, PROD_DB_CREDS_DB_USER etc.
```

### HashiCorp Vault in CI

```yaml
- name: Import secrets from Vault
  uses: hashicorp/vault-action@v3
  with:
    url: https://vault.internal.company.com
    method: jwt
    role: ci-pipeline
    secrets: |
      secret/data/production/db password | DB_PASSWORD ;
      secret/data/production/jwt secret | JWT_SECRET
```

Vault uses short-lived dynamic credentials — the secret is generated for each pipeline run and expires after the job completes.

### Injecting secrets into Kubernetes at deploy time

```yaml
# In CI: create/update K8s secret from Secrets Manager
- name: Sync secrets to K8s
  run: |
    DB_PASSWORD=$(aws secretsmanager get-secret-value \
      --secret-id prod/db \
      --query SecretString \
      --output text | jq -r .password)

    kubectl create secret generic backend-secrets \
      --from-literal=DB_PASSWORD="$DB_PASSWORD" \
      --namespace production \
      --dry-run=client -o yaml | kubectl apply -f -
```

Better long-term: use External Secrets Operator — it syncs AWS/Vault secrets into K8s secrets automatically without CI involvement.

---

## Interview Q&A

**Q1: What is the difference between Continuous Delivery and Continuous Deployment?**

Continuous Delivery means every successful build produces an artifact that is ready to deploy, but a human must approve the actual production deployment. Continuous Deployment takes this further by automatically deploying every passing build to production with no manual gate. Continuous Delivery is appropriate for teams that need change control processes or deploy on business schedules. Continuous Deployment requires extremely high test confidence and mature rollback capabilities.

**Q2: How would you design a pipeline to minimize time-to-feedback for developers?**

I would structure the pipeline as a DAG with fast-fail behavior. The fastest checks run first and in parallel: lint, type-check, and unit tests typically complete in under 2 minutes and are run simultaneously. These block everything downstream. Integration tests and security scans, which take longer, run in parallel with each other after lint passes. The Docker build only runs if all tests pass. This way a developer knows within 90 seconds if their code is broken at the syntax/logic level, rather than waiting 15 minutes to find out after a Docker build.

**Q3: Why should you build a Docker image once and promote it across environments rather than rebuilding per environment?**

Rebuilding creates the risk of non-determinism. Even with the same Dockerfile and source code, a rebuild at a different time could pull different base image layers, different npm package versions (if lockfiles are not strictly respected), or hit different system dependencies. You test staging's image but production runs a different binary. By building once and capturing the digest (`sha256:...`), you guarantee that the exact bytes tested in staging are what run in production. The environment-specific configuration comes from environment variables and K8s ConfigMaps, not from the image.

**Q4: You have a zero-downtime requirement. Which deployment strategy do you choose and why?**

All three major strategies (blue-green, canary, rolling) achieve zero downtime, but the right choice depends on constraints. If I need instant rollback and have budget for 2x infrastructure, blue-green is cleanest. If I need to validate the new version under real traffic before full rollout, canary is best — I can abort at 5% traffic if error rates spike. If I need the simplest implementation with no extra infrastructure and my APIs are backward compatible, rolling is fine. For a backend API that might have DB schema changes, I lean toward blue-green because running two API versions simultaneously against the same schema can cause problems.

**Q5: How do you handle database migrations in a zero-downtime deployment?**

The rule is: expand then contract, never both at once.

For a column rename (old_name → new_name):
1. Migration 1: add `new_name` column (both columns exist). Deploy app v1 that writes to both.
2. Migration 2: backfill `new_name` from `old_name` for existing rows.
3. Deploy app v2 that reads from `new_name` only, still writes to both.
4. Migration 3: drop `old_name` after verifying no reads.

This way, at every step, the running application version is compatible with the current schema. Rolling back is safe at any stage.

**Q6: What are the security risks of storing secrets in environment variables, and what is the alternative?**

Environment variables leak through: process listings (`/proc/pid/environ`), application error dumps that print the environment, container inspection (`docker inspect`), and debug logging. The proper alternative is to use a secrets manager (AWS Secrets Manager, HashiCorp Vault, GCP Secret Manager) and fetch secrets at startup over a secure API call using an IAM role or Vault agent. The secret is in memory only during the process lifetime and never stored on disk or in container metadata. Additionally, secrets managers provide audit logs, automatic rotation, and fine-grained access control.
