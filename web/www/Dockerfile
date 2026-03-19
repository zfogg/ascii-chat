# WWW app - builds on shared
ARG SOURCE_COMMIT
ARG VITE_DISCOVERY_API_BASE
ARG NODE_ENV=development
FROM node:22-slim AS base

# Install bash, curl, git, wget, and pnpm
RUN apt-get update && apt-get install -y --no-install-recommends bash curl git wget && npm install -g pnpm@9.15.0 && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy dependency files first for layer caching
COPY web/pnpm-lock.yaml web/pnpm-lock.yaml
COPY web/pnpm-workspace.yaml web/pnpm-workspace.yaml
COPY web/package.json web/
COPY web/discovery/package.json web/discovery/
COPY web/web/package.json web/web/
COPY web/www/package.json web/www/
COPY web/packages web/packages

# Clean install dependencies with no cache (from web root where monorepo is)
RUN cd web && pnpm install --frozen-lockfile

# Copy entire monorepo (node_modules excluded via .dockerignore)
COPY . .

# Pass SOURCE_COMMIT from Coolify as environment variable for build-time use
ENV SOURCE_COMMIT=${SOURCE_COMMIT}

# Build www
FROM base AS builder

ENV VERCEL=1
ENV VITE_DISCOVERY_API_BASE=${VITE_DISCOVERY_API_BASE:-http://localhost:5173}
RUN cd web/www && pnpm run build && test -d dist || (echo "ERROR: dist directory not created during build" && ls -la && exit 1)

# Production stage
FROM caddy:alpine AS caddy
FROM node:22-slim

ARG NODE_ENV=development
ENV NODE_ENV=${NODE_ENV}

RUN apt-get update && apt-get install -y --no-install-recommends curl && npm install -g pnpm@9.15.0 && rm -rf /var/lib/apt/lists/*
COPY --from=caddy /usr/bin/caddy /usr/local/bin/caddy

WORKDIR /app/web

COPY --from=builder /app/web/node_modules ./node_modules
COPY --from=builder /app/web/pnpm-lock.yaml ./pnpm-lock.yaml
COPY --from=builder /app/web/package.json ./package.json
COPY --from=builder /app/web/www ./www
COPY --from=builder /app/web/packages ./packages

COPY --from=builder /app/web/www/Caddyfile /etc/caddy/Caddyfile

WORKDIR /app/web/www

ENV API_PORT=3001
EXPOSE 80 3001

CMD ["pnpm", "run", "start:caddy"]

# Health check for both Caddy and API server
HEALTHCHECK --interval=30s --timeout=1.5s --start-period=3s --retries=3 \
  CMD curl -f http://localhost/api/health || exit 1

