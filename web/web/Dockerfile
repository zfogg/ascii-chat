# Web app - builds on shared
FROM node:22-slim AS base

RUN apt-get update && apt-get install -y --no-install-recommends bash && npm install -g bun && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .
RUN bun install --frozen-lockfile=false

# Build web
FROM base AS builder

RUN cd web/web && bun run build
RUN rm -rf /app/node_modules /app/discovery /app/www /app/packages /app/bun.lock*

# Production stage
FROM caddy:alpine

WORKDIR /app/web

COPY --from=builder /app/web/dist ./dist
COPY web/web/Caddyfile /etc/caddy/Caddyfile

EXPOSE 3000

CMD ["caddy", "run", "--config", "/etc/caddy/Caddyfile"]
