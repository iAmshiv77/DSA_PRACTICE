# Infrastructure as Code

## Terraform

Terraform is a declarative IaC tool by HashiCorp. You describe the desired state of your infrastructure in HCL (HashiCorp Configuration Language), and Terraform computes the diff between current state and desired state, then applies only the changes needed.

### Core Concepts

#### Providers

Providers are plugins that translate Terraform resources into API calls for a specific platform.

```hcl
# versions.tf
terraform {
  required_version = ">= 1.7.0"

  required_providers {
    aws = {
      source  = "hashicorp/aws"
      version = "~> 5.0"
    }
    kubernetes = {
      source  = "hashicorp/kubernetes"
      version = "~> 2.0"
    }
  }

  # Remote state backend
  backend "s3" {
    bucket         = "company-terraform-state"
    key            = "production/main.tfstate"
    region         = "ap-south-1"
    encrypt        = true
    dynamodb_table = "terraform-state-lock"  # prevents concurrent applies
  }
}

provider "aws" {
  region = var.aws_region

  default_tags {
    tags = {
      Environment = var.environment
      Project     = "super-schools"
      ManagedBy   = "terraform"
    }
  }
}
```

#### Resources

Resources are the fundamental building blocks — each represents an infrastructure object.

```hcl
resource "aws_vpc" "main" {
  cidr_block           = "10.0.0.0/16"
  enable_dns_hostnames = true
  enable_dns_support   = true

  tags = {
    Name = "${var.environment}-vpc"
  }
}

# Reference another resource using resource_type.resource_name.attribute
resource "aws_subnet" "private" {
  count             = length(var.availability_zones)
  vpc_id            = aws_vpc.main.id      # implicit dependency
  cidr_block        = cidrsubnet("10.0.0.0/16", 8, count.index + 10)
  availability_zone = var.availability_zones[count.index]

  tags = {
    Name = "${var.environment}-private-${count.index + 1}"
    Tier = "private"
  }
}
```

#### State

Terraform state (`terraform.tfstate`) is the source of truth about what Terraform manages. It maps resource definitions to real infrastructure objects. Remote state allows teams to collaborate without conflicting.

State commands:
```bash
# List all resources in state
terraform state list

# Show details of a specific resource
terraform state show aws_vpc.main

# Remove a resource from state without destroying it (orphan it)
terraform state rm aws_s3_bucket.old

# Move a resource to a new address (after refactor)
terraform state mv aws_instance.web aws_instance.web_server
```

#### Import

Bring existing infrastructure under Terraform management without recreating it.

```bash
# Import existing VPC
terraform import aws_vpc.main vpc-0a1b2c3d4e5f

# Import existing RDS instance
terraform import aws_db_instance.postgres myapp-postgres

# Terraform 1.5+ — import block (preferred, declarative)
```

```hcl
# import.tf (Terraform 1.5+)
import {
  to = aws_vpc.main
  id = "vpc-0a1b2c3d4e5f"
}

import {
  to = aws_db_instance.postgres
  id = "myapp-postgres"
}
```

#### Modules

Modules are reusable, composable units of Terraform code. A module is just a directory with `.tf` files.

```
modules/
├── vpc/
│   ├── main.tf
│   ├── variables.tf
│   └── outputs.tf
├── rds/
│   ├── main.tf
│   ├── variables.tf
│   └── outputs.tf
└── ecs/
    ├── main.tf
    ├── variables.tf
    └── outputs.tf
```

```hcl
# Using a module
module "vpc" {
  source = "./modules/vpc"

  environment        = var.environment
  cidr_block         = "10.0.0.0/16"
  availability_zones = ["ap-south-1a", "ap-south-1b", "ap-south-1c"]
}

module "rds" {
  source = "./modules/rds"

  vpc_id          = module.vpc.vpc_id         # use output from vpc module
  subnet_ids      = module.vpc.private_subnet_ids
  instance_class  = "db.t3.medium"
  database_name   = "appdb"
}

# Module output values expose module internals to the caller
output "rds_endpoint" {
  value = module.rds.endpoint
}
```

#### Workspaces

Workspaces allow you to maintain multiple state files from the same configuration. Useful for managing dev/staging/prod with one codebase.

```bash
# List workspaces
terraform workspace list

# Create and switch
terraform workspace new staging
terraform workspace new production

# Switch
terraform workspace select production

# Current workspace available in config
resource "aws_db_instance" "postgres" {
  instance_class = terraform.workspace == "production" ? "db.r6g.xlarge" : "db.t3.micro"
}
```

Workspace caveats: workspaces share the same backend bucket but different state keys. They are not a substitute for separate AWS accounts (security isolation). Many teams prefer to use different directories or different Terragrunt configurations per environment instead.

---

## Complete Terraform Example: AWS VPC + ECS + RDS

### Directory Structure

```
infrastructure/
├── versions.tf
├── variables.tf
├── outputs.tf
├── vpc.tf
├── security-groups.tf
├── rds.tf
├── ecr.tf
├── ecs.tf
└── terraform.tfvars
```

### variables.tf

```hcl
variable "aws_region" {
  type    = string
  default = "ap-south-1"
}

variable "environment" {
  type = string
}

variable "availability_zones" {
  type    = list(string)
  default = ["ap-south-1a", "ap-south-1b"]
}

variable "app_image_tag" {
  type        = string
  description = "Docker image tag to deploy"
}

variable "db_password" {
  type      = string
  sensitive = true
}
```

### vpc.tf

```hcl
resource "aws_vpc" "main" {
  cidr_block           = "10.0.0.0/16"
  enable_dns_hostnames = true
  enable_dns_support   = true
  tags = { Name = "${var.environment}-vpc" }
}

resource "aws_internet_gateway" "main" {
  vpc_id = aws_vpc.main.id
  tags   = { Name = "${var.environment}-igw" }
}

resource "aws_subnet" "public" {
  count                   = length(var.availability_zones)
  vpc_id                  = aws_vpc.main.id
  cidr_block              = cidrsubnet("10.0.0.0/16", 8, count.index)
  availability_zone       = var.availability_zones[count.index]
  map_public_ip_on_launch = true
  tags = {
    Name = "${var.environment}-public-${count.index + 1}"
    Tier = "public"
    "kubernetes.io/role/elb" = "1"
  }
}

resource "aws_subnet" "private" {
  count             = length(var.availability_zones)
  vpc_id            = aws_vpc.main.id
  cidr_block        = cidrsubnet("10.0.0.0/16", 8, count.index + 10)
  availability_zone = var.availability_zones[count.index]
  tags = {
    Name = "${var.environment}-private-${count.index + 1}"
    Tier = "private"
  }
}

resource "aws_eip" "nat" {
  count  = length(var.availability_zones)
  domain = "vpc"
  tags   = { Name = "${var.environment}-nat-eip-${count.index + 1}" }
}

resource "aws_nat_gateway" "main" {
  count         = length(var.availability_zones)
  allocation_id = aws_eip.nat[count.index].id
  subnet_id     = aws_subnet.public[count.index].id
  tags          = { Name = "${var.environment}-nat-${count.index + 1}" }
  depends_on    = [aws_internet_gateway.main]
}

resource "aws_route_table" "public" {
  vpc_id = aws_vpc.main.id
  route {
    cidr_block = "0.0.0.0/0"
    gateway_id = aws_internet_gateway.main.id
  }
  tags = { Name = "${var.environment}-public-rt" }
}

resource "aws_route_table" "private" {
  count  = length(var.availability_zones)
  vpc_id = aws_vpc.main.id
  route {
    cidr_block     = "0.0.0.0/0"
    nat_gateway_id = aws_nat_gateway.main[count.index].id
  }
  tags = { Name = "${var.environment}-private-rt-${count.index + 1}" }
}

resource "aws_route_table_association" "public" {
  count          = length(var.availability_zones)
  subnet_id      = aws_subnet.public[count.index].id
  route_table_id = aws_route_table.public.id
}

resource "aws_route_table_association" "private" {
  count          = length(var.availability_zones)
  subnet_id      = aws_subnet.private[count.index].id
  route_table_id = aws_route_table.private[count.index].id
}
```

### security-groups.tf

```hcl
resource "aws_security_group" "alb" {
  name        = "${var.environment}-alb-sg"
  description = "ALB inbound HTTPS"
  vpc_id      = aws_vpc.main.id

  ingress {
    from_port   = 443
    to_port     = 443
    protocol    = "tcp"
    cidr_blocks = ["0.0.0.0/0"]
  }
  ingress {
    from_port   = 80
    to_port     = 80
    protocol    = "tcp"
    cidr_blocks = ["0.0.0.0/0"]
  }
  egress {
    from_port   = 0
    to_port     = 0
    protocol    = "-1"
    cidr_blocks = ["0.0.0.0/0"]
  }
}

resource "aws_security_group" "app" {
  name        = "${var.environment}-app-sg"
  description = "App traffic from ALB only"
  vpc_id      = aws_vpc.main.id

  ingress {
    from_port       = 3000
    to_port         = 3000
    protocol        = "tcp"
    security_groups = [aws_security_group.alb.id]
  }
  egress {
    from_port   = 0
    to_port     = 0
    protocol    = "-1"
    cidr_blocks = ["0.0.0.0/0"]
  }
}

resource "aws_security_group" "rds" {
  name        = "${var.environment}-rds-sg"
  description = "PostgreSQL from app only"
  vpc_id      = aws_vpc.main.id

  ingress {
    from_port       = 5432
    to_port         = 5432
    protocol        = "tcp"
    security_groups = [aws_security_group.app.id]
  }
}
```

### rds.tf

```hcl
resource "aws_db_subnet_group" "main" {
  name       = "${var.environment}-db-subnet-group"
  subnet_ids = aws_subnet.private[*].id
}

resource "aws_db_instance" "postgres" {
  identifier        = "${var.environment}-postgres"
  engine            = "postgres"
  engine_version    = "16.1"
  instance_class    = var.environment == "production" ? "db.r6g.large" : "db.t3.micro"
  allocated_storage = 100
  storage_type      = "gp3"
  storage_encrypted = true

  db_name  = "appdb"
  username = "appuser"
  password = var.db_password

  db_subnet_group_name   = aws_db_subnet_group.main.name
  vpc_security_group_ids = [aws_security_group.rds.id]

  multi_az               = var.environment == "production"
  publicly_accessible    = false
  deletion_protection    = var.environment == "production"
  skip_final_snapshot    = var.environment != "production"
  final_snapshot_identifier = "${var.environment}-final-snapshot"

  backup_retention_period = 7
  backup_window           = "02:00-03:00"
  maintenance_window      = "sun:04:00-sun:05:00"

  performance_insights_enabled = true
}
```

### ecs.tf

```hcl
resource "aws_ecr_repository" "backend" {
  name                 = "super-schools-backend"
  image_tag_mutability = "IMMUTABLE"

  image_scanning_configuration {
    scan_on_push = true
  }
}

resource "aws_ecs_cluster" "main" {
  name = "${var.environment}-cluster"

  setting {
    name  = "containerInsights"
    value = "enabled"
  }
}

resource "aws_iam_role" "ecs_task_execution" {
  name = "${var.environment}-ecs-task-execution-role"
  assume_role_policy = jsonencode({
    Version = "2012-10-17"
    Statement = [{
      Action    = "sts:AssumeRole"
      Effect    = "Allow"
      Principal = { Service = "ecs-tasks.amazonaws.com" }
    }]
  })
}

resource "aws_iam_role_policy_attachment" "ecs_task_execution" {
  role       = aws_iam_role.ecs_task_execution.name
  policy_arn = "arn:aws:iam::aws:policy/service-role/AmazonECSTaskExecutionRolePolicy"
}

resource "aws_ecs_task_definition" "backend" {
  family                   = "${var.environment}-backend"
  network_mode             = "awsvpc"
  requires_compatibilities = ["FARGATE"]
  cpu                      = 512
  memory                   = 1024
  execution_role_arn       = aws_iam_role.ecs_task_execution.arn

  container_definitions = jsonencode([{
    name  = "backend"
    image = "${aws_ecr_repository.backend.repository_url}:${var.app_image_tag}"
    portMappings = [{ containerPort = 3000, protocol = "tcp" }]
    environment = [
      { name = "NODE_ENV",  value = var.environment },
      { name = "DB_HOST",   value = aws_db_instance.postgres.address },
      { name = "DB_NAME",   value = "appdb" },
    ]
    secrets = [
      { name = "DB_PASSWORD",  valueFrom = aws_secretsmanager_secret.db_password.arn },
      { name = "JWT_SECRET",   valueFrom = aws_secretsmanager_secret.jwt_secret.arn },
    ]
    logConfiguration = {
      logDriver = "awslogs"
      options = {
        "awslogs-group"         = "/ecs/${var.environment}-backend"
        "awslogs-region"        = var.aws_region
        "awslogs-stream-prefix" = "ecs"
      }
    }
    healthCheck = {
      command     = ["CMD-SHELL", "wget -qO- http://localhost:3000/health || exit 1"]
      interval    = 30
      timeout     = 10
      retries     = 3
      startPeriod = 60
    }
  }])
}

resource "aws_ecs_service" "backend" {
  name            = "${var.environment}-backend"
  cluster         = aws_ecs_cluster.main.id
  task_definition = aws_ecs_task_definition.backend.arn
  desired_count   = 2
  launch_type     = "FARGATE"

  network_configuration {
    subnets          = aws_subnet.private[*].id
    security_groups  = [aws_security_group.app.id]
    assign_public_ip = false
  }

  load_balancer {
    target_group_arn = aws_lb_target_group.backend.arn
    container_name   = "backend"
    container_port   = 3000
  }

  deployment_circuit_breaker {
    enable   = true
    rollback = true
  }

  lifecycle {
    ignore_changes = [task_definition, desired_count]
    # task_definition managed by CI/CD, desired_count by autoscaling
  }
}
```

---

## Helm

Helm is the package manager for Kubernetes. A Helm chart is a collection of K8s manifests templated with Go's `text/template`.

### Chart Structure

```
my-nestjs-app/
├── Chart.yaml          # chart metadata
├── values.yaml         # default configuration values
├── templates/
│   ├── deployment.yaml
│   ├── service.yaml
│   ├── ingress.yaml
│   ├── configmap.yaml
│   ├── hpa.yaml
│   ├── serviceaccount.yaml
│   └── _helpers.tpl    # template helper functions (partials)
└── charts/             # chart dependencies
```

### Chart.yaml

```yaml
apiVersion: v2
name: nestjs-backend
description: Super Schools NestJS Backend
type: application
version: 0.1.0        # chart version
appVersion: "1.0.0"   # app version being packaged
```

### values.yaml

```yaml
replicaCount: 2

image:
  repository: 123456789012.dkr.ecr.ap-south-1.amazonaws.com/super-schools-backend
  pullPolicy: IfNotPresent
  tag: ""  # overridden at deploy time

serviceAccount:
  create: true
  annotations:
    eks.amazonaws.com/role-arn: arn:aws:iam::123456789012:role/backend-pod-role

service:
  type: ClusterIP
  port: 80
  targetPort: 3000

ingress:
  enabled: true
  className: alb
  annotations:
    kubernetes.io/ingress.class: alb
    alb.ingress.kubernetes.io/scheme: internet-facing
    alb.ingress.kubernetes.io/target-type: ip
    alb.ingress.kubernetes.io/certificate-arn: arn:aws:acm:...
  hosts:
    - host: api.superschools.app
      paths:
        - path: /
          pathType: Prefix

resources:
  requests:
    memory: 256Mi
    cpu: 100m
  limits:
    memory: 512Mi
    cpu: 500m

autoscaling:
  enabled: true
  minReplicas: 2
  maxReplicas: 10
  targetCPUUtilizationPercentage: 60
  targetMemoryUtilizationPercentage: 75

env:
  NODE_ENV: production
  DB_PORT: "5432"
  REDIS_PORT: "6379"

externalSecrets:
  enabled: true
  secretStoreRef: aws-secrets-manager

livenessProbe:
  httpGet:
    path: /health/live
    port: 3000
  initialDelaySeconds: 10
  periodSeconds: 10

readinessProbe:
  httpGet:
    path: /health/ready
    port: 3000
  initialDelaySeconds: 15
  periodSeconds: 5
```

### templates/_helpers.tpl

```yaml
{{/*
Expand the name of the chart.
*/}}
{{- define "nestjs-backend.name" -}}
{{- default .Chart.Name .Values.nameOverride | trunc 63 | trimSuffix "-" }}
{{- end }}

{{/*
Full name combining release name and chart name.
*/}}
{{- define "nestjs-backend.fullname" -}}
{{- if .Values.fullnameOverride }}
{{- .Values.fullnameOverride | trunc 63 | trimSuffix "-" }}
{{- else }}
{{- $name := default .Chart.Name .Values.nameOverride }}
{{- printf "%s-%s" .Release.Name $name | trunc 63 | trimSuffix "-" }}
{{- end }}
{{- end }}

{{/*
Common labels
*/}}
{{- define "nestjs-backend.labels" -}}
helm.sh/chart: {{ .Chart.Name }}-{{ .Chart.Version }}
app.kubernetes.io/name: {{ include "nestjs-backend.name" . }}
app.kubernetes.io/instance: {{ .Release.Name }}
app.kubernetes.io/version: {{ .Values.image.tag | quote }}
app.kubernetes.io/managed-by: {{ .Release.Service }}
{{- end }}
```

### templates/deployment.yaml

```yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: {{ include "nestjs-backend.fullname" . }}
  labels:
    {{- include "nestjs-backend.labels" . | nindent 4 }}
spec:
  {{- if not .Values.autoscaling.enabled }}
  replicas: {{ .Values.replicaCount }}
  {{- end }}
  selector:
    matchLabels:
      app.kubernetes.io/name: {{ include "nestjs-backend.name" . }}
      app.kubernetes.io/instance: {{ .Release.Name }}
  strategy:
    type: RollingUpdate
    rollingUpdate:
      maxSurge: 1
      maxUnavailable: 0
  template:
    metadata:
      labels:
        {{- include "nestjs-backend.labels" . | nindent 8 }}
      annotations:
        prometheus.io/scrape: "true"
        prometheus.io/path: "/metrics"
        prometheus.io/port: "3000"
    spec:
      serviceAccountName: {{ include "nestjs-backend.fullname" . }}
      terminationGracePeriodSeconds: 30
      containers:
        - name: backend
          image: "{{ .Values.image.repository }}:{{ .Values.image.tag | default .Chart.AppVersion }}"
          imagePullPolicy: {{ .Values.image.pullPolicy }}
          ports:
            - name: http
              containerPort: 3000
              protocol: TCP
          env:
            {{- range $key, $value := .Values.env }}
            - name: {{ $key }}
              value: {{ $value | quote }}
            {{- end }}
          envFrom:
            - secretRef:
                name: {{ include "nestjs-backend.fullname" . }}-secrets
          resources:
            {{- toYaml .Values.resources | nindent 12 }}
          livenessProbe:
            {{- toYaml .Values.livenessProbe | nindent 12 }}
          readinessProbe:
            {{- toYaml .Values.readinessProbe | nindent 12 }}
          lifecycle:
            preStop:
              exec:
                command: ["/bin/sh", "-c", "sleep 5"]  # let LB drain connections
      topologySpreadConstraints:
        - maxSkew: 1
          topologyKey: kubernetes.io/hostname
          whenUnsatisfiable: DoNotSchedule
          labelSelector:
            matchLabels:
              app.kubernetes.io/name: {{ include "nestjs-backend.name" . }}
```

### templates/hpa.yaml

```yaml
{{- if .Values.autoscaling.enabled }}
apiVersion: autoscaling/v2
kind: HorizontalPodAutoscaler
metadata:
  name: {{ include "nestjs-backend.fullname" . }}
spec:
  scaleTargetRef:
    apiVersion: apps/v1
    kind: Deployment
    name: {{ include "nestjs-backend.fullname" . }}
  minReplicas: {{ .Values.autoscaling.minReplicas }}
  maxReplicas: {{ .Values.autoscaling.maxReplicas }}
  metrics:
    - type: Resource
      resource:
        name: cpu
        target:
          type: Utilization
          averageUtilization: {{ .Values.autoscaling.targetCPUUtilizationPercentage }}
    - type: Resource
      resource:
        name: memory
        target:
          type: Utilization
          averageUtilization: {{ .Values.autoscaling.targetMemoryUtilizationPercentage }}
{{- end }}
```

### Helm Release Management

```bash
# Install (first time)
helm install backend ./nestjs-backend \
  --namespace production \
  --create-namespace \
  --values ./values-production.yaml \
  --set image.tag=sha-abc123

# Upgrade (subsequent deploys)
helm upgrade backend ./nestjs-backend \
  --namespace production \
  --values ./values-production.yaml \
  --set image.tag=sha-def456 \
  --atomic \          # rollback automatically if upgrade fails
  --timeout 5m \
  --wait              # wait for all pods to be ready

# Rollback to previous release
helm rollback backend 3 --namespace production

# List releases
helm list --namespace production

# Show release history
helm history backend --namespace production

# Render templates without applying (dry run)
helm template backend ./nestjs-backend --values values-production.yaml

# Diff plugin (shows what would change)
helm diff upgrade backend ./nestjs-backend --values values-production.yaml --set image.tag=new-tag
```

### Helm Chart Repositories

```bash
# Add a repository
helm repo add bitnami https://charts.bitnami.com/bitnami
helm repo update

# Search for charts
helm search repo bitnami/redis

# Install from repository
helm install my-redis bitnami/redis \
  --set auth.password=secret \
  --set replica.replicaCount=1
```

---

## Terraform vs Pulumi vs CDK

### Comparison

| Feature | Terraform | Pulumi | AWS CDK |
|---|---|---|---|
| Language | HCL (declarative DSL) | TypeScript, Python, Go, C# (general-purpose) | TypeScript, Python, Java, C# |
| Cloud support | All major clouds + 2000+ providers | All major clouds | AWS only |
| State management | Remote backend (S3, Terraform Cloud) | Pulumi Cloud or self-hosted | CloudFormation stacks (no separate state) |
| Abstraction | Resources + Modules | Classes + inheritance | Constructs (L1/L2/L3) |
| Testing | Terratest (Go), terraform validate | Native unit testing in host language | jest + CDK assertions |
| Maturity | Most mature, largest community | Growing, 2018+ | AWS-supported, 2019+ |
| Loops/conditionals | count, for_each, dynamic blocks | Native language loops | Native language |
| Secret handling | Sensitive vars (still in state) | Pulumi ESC, encrypted state | SSM Parameter Store, Secrets Manager |

### When to use which

**Terraform**: team is comfortable with HCL, multi-cloud or many non-AWS services, existing Terraform codebase, want maximum provider ecosystem.

**Pulumi**: team prefers real programming languages for logic-heavy infrastructure (complex loops, conditionals, reuse), want to test infrastructure code properly, fine with smaller community.

**AWS CDK**: AWS-only shop, team is experienced TypeScript/Python developers, want high-level constructs that handle best practices automatically (L2/L3 constructs configure VPCs, ALBs with sensible defaults).

### CDK Quick Example

```typescript
// lib/app-stack.ts
import * as ec2 from 'aws-cdk-lib/aws-ec2';
import * as ecs from 'aws-cdk-lib/aws-ecs';
import * as ecs_patterns from 'aws-cdk-lib/aws-ecs-patterns';

export class AppStack extends Stack {
  constructor(scope: Construct, id: string, props?: StackProps) {
    super(scope, id, props);

    const vpc = new ec2.Vpc(this, 'AppVpc', {
      maxAzs: 2,
      natGateways: 1,
    });

    const cluster = new ecs.Cluster(this, 'AppCluster', { vpc });

    // L3 construct — handles ALB, target group, ECS service, IAM roles
    const service = new ecs_patterns.ApplicationLoadBalancedFargateService(this, 'BackendService', {
      cluster,
      cpu: 512,
      memoryLimitMiB: 1024,
      desiredCount: 2,
      taskImageOptions: {
        image: ecs.ContainerImage.fromEcrRepository(repo, imageTag),
        containerPort: 3000,
      },
      publicLoadBalancer: true,
    });
  }
}
```

---

## GitOps with ArgoCD

### What is GitOps?

GitOps applies Git version control principles to infrastructure and application deployment. The Git repository is the single source of truth for the desired state of the system. An operator (ArgoCD) continuously reconciles the cluster state with the Git repository.

Core principles:
1. Declarative: system state described declaratively (K8s manifests, Helm charts)
2. Versioned: desired state stored in Git — full audit trail, rollback via revert
3. Pulled automatically: automated agents pull and apply changes (not pushed by CI)
4. Continuously reconciled: if cluster state drifts from Git, it is corrected

### ArgoCD Architecture

```
Git Repository (source of truth)
     ↑ watches
ArgoCD (runs in cluster)
     ↓ reconciles
Kubernetes Cluster

Developer pushes new image tag to Git
              ↓
ArgoCD detects diff
              ↓
ArgoCD applies new manifests to cluster
              ↓
Deployment updates rolling → new pods
```

### ArgoCD Application Definition

```yaml
# argocd-app.yaml
apiVersion: argoproj.io/v1alpha1
kind: Application
metadata:
  name: backend-production
  namespace: argocd
spec:
  project: default

  source:
    repoURL: https://github.com/company/k8s-manifests
    targetRevision: main
    path: apps/backend/overlays/production
    # For Helm chart:
    # chart: nestjs-backend
    # helm:
    #   valueFiles:
    #     - values-production.yaml
    #   parameters:
    #     - name: image.tag
    #       value: sha-abc123

  destination:
    server: https://kubernetes.default.svc
    namespace: production

  syncPolicy:
    automated:
      prune: true       # delete resources removed from Git
      selfHeal: true    # revert manual changes to cluster
      allowEmpty: false
    syncOptions:
      - CreateNamespace=true
      - PrunePropagationPolicy=foreground
      - RespectIgnoreDifferences=true
    retry:
      limit: 5
      backoff:
        duration: 5s
        factor: 2
        maxDuration: 3m

  # Don't sync certain fields (managed by HPA/VPA)
  ignoreDifferences:
    - group: apps
      kind: Deployment
      jsonPointers:
        - /spec/replicas
```

### CI/CD + GitOps Integration

The CI pipeline builds and tests, then updates the Git repository. ArgoCD handles the actual cluster deployment.

```yaml
# In GitHub Actions deploy job (instead of kubectl apply):
- name: Update image tag in GitOps repo
  run: |
    git clone https://x-access-token:${{ secrets.GITOPS_TOKEN }}@github.com/company/k8s-manifests
    cd k8s-manifests

    # Update the image tag in the values file
    yq e -i '.image.tag = "${{ github.sha }}"' \
      apps/backend/overlays/production/values.yaml

    git config user.email "ci@company.com"
    git config user.name "CI Pipeline"
    git add .
    git commit -m "backend: update image to sha-${{ github.sha }}"
    git push

# ArgoCD detects the commit and syncs the cluster
```

This separation means the CI pipeline has no direct access to the production cluster. Rollback is `git revert` followed by ArgoCD auto-sync. The full deployment history is the Git commit history.

### ArgoCD Image Updater

For fully automated GitOps without manual CI steps updating Git:

```yaml
# Annotation on ArgoCD Application
metadata:
  annotations:
    argocd-image-updater.argoproj.io/image-list: backend=123456789.dkr.ecr.region.amazonaws.com/backend
    argocd-image-updater.argoproj.io/backend.update-strategy: latest
    argocd-image-updater.argoproj.io/write-back-method: git
```

Image Updater polls ECR, detects new tags, updates the Git repository, and ArgoCD auto-syncs.

---

## Interview Q&A

**Q1: What is Terraform state, and what problems arise when multiple engineers run Terraform simultaneously?**

Terraform state is a JSON file that maps the resources declared in `.tf` files to real infrastructure objects and their current attributes. Terraform needs it to compute the diff between desired and actual state. Without state, Terraform would not know which AWS VPC corresponds to which `aws_vpc` resource block. When multiple engineers run `terraform apply` simultaneously, they can corrupt the state file — one engineer's apply reads the state before the other's write, causing stale reads and potentially destroying or duplicating resources. The solution is remote state with locking: store state in S3 (encrypted) and use a DynamoDB table for pessimistic locking. When one `terraform apply` starts, it acquires the lock. Other applies wait until the lock is released. This prevents concurrent state mutations.

**Q2: What is the difference between `terraform plan` and `terraform apply`, and when would you run plan without apply?**

`terraform plan` performs a read-only diff: it reads the current state, calls AWS read APIs to check real resource attributes, and computes what changes would need to be made — additions, modifications, deletions. Nothing is changed. `terraform apply` executes the plan. In a CI/CD pipeline, I always run `plan` first and save the output as a plan file (`terraform plan -out=tfplan`), then require a human to review the plan and approve. The `apply` then executes exactly the saved plan, not a fresh plan — this prevents time-of-check-to-time-of-use issues where state changes between plan and apply. In pull request workflows, I run `terraform plan` and post the output as a PR comment (using tools like Atlantis or Terraform Cloud) so reviewers see the infrastructure change alongside the code change.

**Q3: What is Helm's role in Kubernetes, and how is it different from kustomize?**

Helm is a templating engine and package manager. It takes a chart (Go templates + default values) and renders it into K8s manifests using a values file, then manages the release lifecycle (install, upgrade, rollback, delete). It is good for packaging complex applications with many configurable parameters and for sharing charts (public chart repositories). Kustomize uses a patch-overlay model — you have a base manifest and environment-specific overlays that patch specific fields without templating. Kustomize is built into `kubectl` (`kubectl apply -k`). Kustomize is simpler for managing environment differences (dev vs prod) when the app is not meant to be shared as a package. Helm is better for distributable software with many configuration options. Many teams use both: Helm for third-party dependencies (Redis, cert-manager) and Kustomize for their own applications.

**Q4: How do you prevent Terraform from destroying production resources when refactoring?**

Several mechanisms. First, `lifecycle { prevent_destroy = true }` on critical resources like RDS instances and S3 buckets — Terraform will refuse to destroy them even if the plan calls for it. Second, always run `terraform plan` and review the output, looking for any `-` (destroy) lines on production resources before applying. Third, use `lifecycle { ignore_changes = [field] }` for fields managed outside Terraform (like ASG desired count managed by autoscaling or ECS task definition managed by CI/CD). Fourth, `terraform state mv` to rename resources in state when refactoring without destroying and recreating them. Fifth, for major refactors, import existing resources into the new structure before removing the old definitions, so Terraform sees them as the same resource.

**Q5: Explain GitOps and how it differs from traditional CI/CD push deployments.**

In traditional CI/CD, the pipeline has credentials to the production cluster and pushes changes directly: `kubectl apply` or `helm upgrade` runs in the CI job. In GitOps, the pipeline has no cluster credentials. Instead, it commits the desired state to a Git repository. An operator running inside the cluster (ArgoCD) continuously watches that repository and pulls changes. If the cluster state diverges from Git (someone runs `kubectl set image` manually, or a pod crashes and changes state), ArgoCD automatically corrects the drift. The key benefits: Git is the audit log for all deployments — every change is a commit with author, timestamp, and diff. Rollback is `git revert`. The blast radius of a compromised CI system is limited — it can only push to Git, not directly modify production. The cluster is continuously reconciled, so manual changes are not permanent.

**Q6: A Terraform plan shows it wants to destroy and recreate your RDS instance due to a change in a field. How do you handle this?**

First, identify why. Some RDS fields require replacement (like `engine_version` major upgrade, `identifier`), while others can be modified in-place. If the change is necessary and I need to preserve data, the approach depends on the field. For an identifier rename, I use `terraform state mv` to rename the resource in state without destroying it. For changes that truly require recreation, I first take a manual RDS snapshot, then let Terraform recreate, then restore from snapshot if needed — though for a production DB I would use the `final_snapshot_identifier` to ensure a snapshot is taken before destruction. The safest approach for critical schema or configuration changes that require replacement is a blue-green strategy at the DB level: create the new instance from a snapshot, run your app against it in staging, then swap. Never let Terraform destroy and recreate a production database in one apply without a manual review and backup step.
