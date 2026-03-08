# WWW app - builds on shared
FROM node:22-slim AS base

# Install bash and bun
RUN apt-get update && apt-get install -y --no-install-recommends bash && npm install -g bun && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .
RUN bun install && npm install --only=optional

# Build www
FROM base AS builder

RUN cd web/www && bun run build
RUN test -d web/www/dist || (echo "ERROR: dist directory not created during build" && ls -la && exit 1)

# Production stage
FROM caddy:alpine AS caddy
FROM node:22-slim

RUN apt-get update && apt-get install -y --no-install-recommends git && npm install -g bun && rm -rf /var/lib/apt/lists/*
COPY --from=caddy /usr/bin/caddy /usr/local/bin/caddy

WORKDIR /app/www

COPY --from=builder /app/node_modules ./node_modules
COPY --from=builder /app/www/dist ./dist
COPY --from=builder /app/www/api.js ./api.js
COPY --from=builder /app/www/package.json ./package.json
COPY --from=builder /app/www/src ./src
COPY web/www/Caddyfile /etc/caddy/Caddyfile

ENV API_PORT=3001
EXPOSE 5173 3001

RUN echo '#!/bin/sh' > /start.sh && \
    echo 'bun api.js &' >> /start.sh && \
    echo 'caddy run --config /etc/caddy/Caddyfile' >> /start.sh && \
    chmod +x /start.sh

CMD ["/start.sh"]
