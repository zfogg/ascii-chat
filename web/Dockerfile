# Unified Dockerfile for all web applications (www, discovery, web)
# Usage:
#   docker build -f web/Dockerfile --build-arg TARGET_APP=www -t ascii-chat-www .
#   docker build -f web/Dockerfile --build-arg TARGET_APP=discovery -t ascii-chat-discovery .
#   docker build -f web/Dockerfile --build-arg TARGET_APP=web -t ascii-chat-web .

ARG TARGET_APP=www
ARG SOURCE_COMMIT
ARG VITE_DISCOVERY_API_BASE
ARG NODE_ENV=development
ARG SSH_PUBLIC_KEY
ARG GPG_PUBLIC_KEY

# ============================================
# Build stage - shared for all apps
# ============================================
FROM node:22-slim AS base

ARG TARGET_APP
ARG SOURCE_COMMIT

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

# Pass SOURCE_COMMIT as environment variable for build-time use
ENV SOURCE_COMMIT=${SOURCE_COMMIT}

# ============================================
# Builder stage - build the specific app
# ============================================
FROM base AS builder

ARG TARGET_APP
ARG VITE_DISCOVERY_API_BASE
ARG SSH_PUBLIC_KEY
ARG GPG_PUBLIC_KEY

# Set environment variables based on target app
RUN if [ "$TARGET_APP" = "www" ]; then \
      echo "Building www app"; \
      export ENV_VARS="VERCEL=1 VITE_DISCOVERY_API_BASE=${VITE_DISCOVERY_API_BASE:-http://localhost:5173}"; \
    elif [ "$TARGET_APP" = "discovery" ]; then \
      echo "Building discovery app"; \
      export ENV_VARS="VITE_DISCOVERY_API_BASE=${VITE_DISCOVERY_API_BASE:-http://localhost:5173} SSH_PUBLIC_KEY=${SSH_PUBLIC_KEY} GPG_PUBLIC_KEY=${GPG_PUBLIC_KEY}"; \
    elif [ "$TARGET_APP" = "web" ]; then \
      echo "Building web app"; \
      export ENV_VARS="VITE_DISCOVERY_API_BASE=${VITE_DISCOVERY_API_BASE:-http://localhost:5173}"; \
    else \
      echo "ERROR: Unknown TARGET_APP=$TARGET_APP"; \
      exit 1; \
    fi

ENV VERCEL=1
ENV VITE_DISCOVERY_API_BASE=${VITE_DISCOVERY_API_BASE:-http://localhost:5173}
ENV SSH_PUBLIC_KEY=${SSH_PUBLIC_KEY}
ENV GPG_PUBLIC_KEY=${GPG_PUBLIC_KEY}

# Build the target app
RUN cd web/${TARGET_APP} && pnpm run build && test -d dist || (echo "ERROR: dist directory not created for ${TARGET_APP}" && ls -la && exit 1)

# ============================================
# Production stage - serve the built app
# ============================================
FROM caddy:alpine AS caddy

FROM node:22-slim

ARG TARGET_APP
ARG NODE_ENV=development

ENV NODE_ENV=${NODE_ENV}
ENV TARGET_APP=${TARGET_APP}

RUN apt-get update && apt-get install -y --no-install-recommends curl && npm install -g pnpm@9.15.0 && rm -rf /var/lib/apt/lists/*

COPY --from=caddy /usr/bin/caddy /usr/local/bin/caddy

WORKDIR /app/web

# Copy built app and dependencies
COPY --from=builder /app/web/node_modules ./node_modules
COPY --from=builder /app/web/pnpm-lock.yaml ./pnpm-lock.yaml
COPY --from=builder /app/web/package.json ./package.json

# Copy app-specific files
COPY --from=builder /app/web/${TARGET_APP} ./${TARGET_APP}
COPY --from=builder /app/web/packages ./packages

# Copy app-specific Caddyfile
COPY --from=builder /app/web/${TARGET_APP}/Caddyfile /etc/caddy/Caddyfile

WORKDIR /app/web/${TARGET_APP}

# Set port based on app
RUN if [ "$TARGET_APP" = "www" ]; then \
      echo "EXPOSE 80 3001" && export EXPOSE_PORTS="80 3001" && export API_PORT=3001; \
    elif [ "$TARGET_APP" = "discovery" ]; then \
      echo "EXPOSE 5174" && export EXPOSE_PORTS="5174"; \
    elif [ "$TARGET_APP" = "web" ]; then \
      echo "EXPOSE 3000" && export EXPOSE_PORTS="3000"; \
    fi

ENV API_PORT=${API_PORT:-3001}
EXPOSE 80 3000 3001 5174

CMD ["pnpm", "run", "start:caddy"]

# Health check
HEALTHCHECK --interval=30s --timeout=5s --start-period=3s --retries=3 \
  CMD curl -f http://localhost/ || exit 1
