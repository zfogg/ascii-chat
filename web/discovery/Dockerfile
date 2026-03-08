# Discovery app - builds on shared
FROM node:22-slim AS base

RUN apt-get update && apt-get install -y --no-install-recommends bash && npm install -g bun && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .
RUN cd web && bun install && npm install --save-optional @rollup/rollup-linux-arm64-gnu 2>/dev/null || true

# Build discovery
FROM base AS builder

RUN cd web/discovery && bun run build
RUN rm -rf /app/node_modules /app/packages /app/www /app/bun.lock*

# Production stage
FROM caddy:alpine

WORKDIR /app/discovery

COPY --from=builder /app/web/discovery/dist ./dist
COPY web/discovery/Caddyfile /etc/caddy/Caddyfile

EXPOSE 5174

CMD ["caddy", "run", "--config", "/etc/caddy/Caddyfile"]
