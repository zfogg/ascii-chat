# WWW app - builds on shared
FROM node:22-slim AS base

# Install bash, git, and bun
RUN apt-get update && apt-get install -y --no-install-recommends bash git && npm install -g bun@1.3.10 && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy .git directory explicitly (needed for getCommitSha.ts to use git rev-parse)
COPY .git .git

# Copy entire monorepo (node_modules excluded via .dockerignore)
COPY . .

# Clean install dependencies with no cache (from web root where monorepo is)
RUN cd web && bun install --no-cache

# Build www
FROM base AS builder

RUN cd web/www && bun run build && test -d dist || (echo "ERROR: dist directory not created during build" && ls -la && exit 1)

# Production stage
FROM caddy:alpine AS caddy
FROM node:22-slim

RUN apt-get update && apt-get install -y --no-install-recommends git && npm install -g bun@1.3.10 && rm -rf /var/lib/apt/lists/*
COPY --from=caddy /usr/bin/caddy /usr/local/bin/caddy

WORKDIR /app/www

COPY --from=builder /app/web/node_modules ./node_modules
COPY --from=builder /app/web/www/dist ./dist
COPY --from=builder /app/web/www/api.js ./api.js
COPY --from=builder /app/web/www/package.json ./package.json
COPY --from=builder /app/web/www/src ./src
COPY web/www/Caddyfile /etc/caddy/Caddyfile

ENV API_PORT=3001
EXPOSE 5173 3001

RUN echo '#!/bin/sh' > /start.sh && \
    echo 'bun api.js &' >> /start.sh && \
    echo 'caddy run --config /etc/caddy/Caddyfile' >> /start.sh && \
    chmod +x /start.sh

CMD ["/start.sh"]
