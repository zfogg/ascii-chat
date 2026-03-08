# Web app - builds on shared
FROM node:22-slim AS base

RUN apt-get update && apt-get install -y --no-install-recommends bash && npm install -g bun@1.3.10 && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .
RUN rm -f bun.lock && bun install

# Build web
FROM base AS builder

RUN cd web/web && bun run build
RUN test -d /app/web/web/dist || (echo "ERROR: dist directory not created during build" && ls -la && exit 1)

# Production stage
FROM caddy:alpine

WORKDIR /app/web

COPY --from=builder /app/web/web/dist ./dist
COPY web/web/Caddyfile /etc/caddy/Caddyfile

EXPOSE 3000

CMD ["caddy", "run", "--config", "/etc/caddy/Caddyfile"]
