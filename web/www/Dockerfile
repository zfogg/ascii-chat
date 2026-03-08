# WWW app - builds on shared
FROM node:22-slim AS base

# Install bash and bun (fresh install, no cache)
RUN apt-get update && apt-get install -y --no-install-recommends bash && npm install -g bun@1.3.10 && rm -rf /var/lib/apt/lists/*

WORKDIR /app
# Cache invalidator: rebuild because of stale lock
COPY . .
RUN rm -f bun.lock* && bun install

# Build www
FROM base AS builder

RUN cd web/www && bun run build && test -d dist || (echo "ERROR: dist directory not created during build" && ls -la && exit 1)

# Production stage
FROM caddy:alpine AS caddy
FROM node:22-slim

RUN apt-get update && apt-get install -y --no-install-recommends git && npm install -g bun@1.3.10 && rm -rf /var/lib/apt/lists/*
COPY --from=caddy /usr/bin/caddy /usr/local/bin/caddy

WORKDIR /app/www

COPY --from=builder /app/node_modules ./node_modules
COPY --from=builder /app/web/www/dist ./dist
COPY --from=builder /app/web/www/api.js ./api.js
COPY --from=builder /app/web/www/package.json ./package.json
COPY web/www/Caddyfile /etc/caddy/Caddyfile

ENV API_PORT=3001
EXPOSE 5173 3001

RUN echo '#!/bin/sh' > /start.sh && \
    echo 'bun api.js &' >> /start.sh && \
    echo 'caddy run --config /etc/caddy/Caddyfile' >> /start.sh && \
    chmod +x /start.sh

CMD ["/start.sh"]
