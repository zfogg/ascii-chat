# Unified Dockerfile for all web applications (www, discovery, web)
# Builds all three apps in a single image with a single Caddy instance

ARG SOURCE_COMMIT
ARG VITE_DISCOVERY_API_BASE
ARG NODE_ENV=development
ARG SSH_PUBLIC_KEY
ARG GPG_PUBLIC_KEY

# ============================================
# Base stage - install dependencies
# ============================================
FROM node:22-slim AS base

ARG SOURCE_COMMIT

# Install bash, curl, git, wget, and pnpm
RUN apt-get update && apt-get install -y --no-install-recommends bash curl git wget ca-certificates && npm install -g pnpm@10.32.1 && rm -rf /var/lib/apt/lists/*

RUN curl -fsSL "https://vite.plus" | bash && cp ~/.vite-plus/bin/vp /usr/local/bin/vp

WORKDIR /app

# Copy dependency files first for layer caching
COPY pnpm-lock.yaml pnpm-lock.yaml
COPY pnpm-workspace.yaml pnpm-workspace.yaml
COPY package.json ./
COPY discovery/package.json discovery/
COPY web/package.json web/
COPY www/package.json www/
COPY packages packages

# Clean install dependencies
RUN pnpm install --frozen-lockfile

# Copy everything (node_modules excluded via .dockerignore)
COPY . .

# Pass SOURCE_COMMIT as environment variable for build-time use
ENV SOURCE_COMMIT=${SOURCE_COMMIT}

# ============================================
# Builder stage - build ALL three apps
# ============================================
FROM base AS builder

ARG VITE_DISCOVERY_API_BASE
ARG SSH_PUBLIC_KEY
ARG GPG_PUBLIC_KEY

ENV VERCEL=1
ENV VITE_DISCOVERY_API_BASE=${VITE_DISCOVERY_API_BASE:-http://localhost:5173}
ENV SSH_PUBLIC_KEY=${SSH_PUBLIC_KEY}
ENV GPG_PUBLIC_KEY=${GPG_PUBLIC_KEY}

# Run all checks
RUN vp check

# Build www app
RUN echo "Building www app" && cd www && pnpm run build && test -d dist || (echo "ERROR: dist directory not created for www" && ls -la && exit 1)

# Build discovery app
RUN echo "Building discovery app" && cd discovery && pnpm run build && test -d dist || (echo "ERROR: dist directory not created for discovery" && ls -la && exit 1)

# Build web app
RUN echo "Building web app" && cd web && pnpm run build && test -d dist || (echo "ERROR: dist directory not created for web" && ls -la && exit 1)

# ============================================
# Production stage - serve all apps with Caddy
# ============================================
FROM caddy:alpine AS caddy

FROM node:22-slim

ARG NODE_ENV=development

ENV NODE_ENV=${NODE_ENV}

RUN apt-get update && apt-get install -y --no-install-recommends curl && npm install -g pnpm@10.32.1 && rm -rf /var/lib/apt/lists/*

COPY --from=caddy /usr/bin/caddy /usr/local/bin/caddy

WORKDIR /app/web

# Copy built apps
COPY --from=builder /app/www ./www
COPY --from=builder /app/discovery ./discovery
COPY --from=builder /app/web ./web

# Copy node_modules for www API server runtime deps
COPY --from=builder /app/node_modules ./node_modules

# Copy unified Caddyfile
COPY Caddyfile /etc/caddy/Caddyfile

# Expose port 80 (Caddy serving all three apps)
EXPOSE 80

# Run API server and Caddy together
CMD ["sh", "-c", "node www/api.ts & caddy run --config /etc/caddy/Caddyfile"]

# Health check
HEALTHCHECK --interval=30s --timeout=1.5s --start-period=3s --retries=3 \
  CMD curl -f http://localhost/ || exit 1
