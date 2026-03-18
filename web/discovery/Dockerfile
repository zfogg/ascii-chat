# Discovery app - builds on shared
ARG SOURCE_COMMIT
ARG VITE_DISCOVERY_API_BASE
ARG NODE_ENV=development
ARG SSH_PUBLIC_KEY
ARG GPG_PUBLIC_KEY
FROM node:22-slim AS base

RUN apt-get update && apt-get install -y --no-install-recommends bash git && npm install -g bun@1.3.10 && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy entire monorepo (node_modules excluded via .dockerignore)
COPY . .

# Pass SOURCE_COMMIT from Coolify as environment variable for build-time use
ENV SOURCE_COMMIT=${SOURCE_COMMIT}

# Clean install dependencies with no cache (run from web/ which is the monorepo root)
RUN cd web && bun install --no-cache

# Build discovery
FROM base AS builder

# Redeclare ARGs for this stage (they don't carry over from global scope)
ARG SSH_PUBLIC_KEY
ARG GPG_PUBLIC_KEY
ARG VITE_DISCOVERY_API_BASE

ENV VITE_DISCOVERY_API_BASE=${VITE_DISCOVERY_API_BASE:-http://localhost:5173}
ENV SSH_PUBLIC_KEY=${SSH_PUBLIC_KEY}
ENV GPG_PUBLIC_KEY=${GPG_PUBLIC_KEY}
RUN cd web/discovery && bun run build
RUN test -d /app/web/discovery/dist || (echo "ERROR: dist directory not created during build" && ls -la && exit 1)

# Production stage
FROM caddy:alpine

ARG NODE_ENV=development
ENV NODE_ENV=${NODE_ENV}

WORKDIR /app/discovery

COPY --from=builder /app/web/discovery/dist ./dist
COPY web/discovery/Caddyfile /etc/caddy/Caddyfile

EXPOSE 5174

CMD ["caddy", "run", "--config", "/etc/caddy/Caddyfile"]
