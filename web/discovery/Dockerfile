# Discovery app - builds on shared
FROM node:22-slim AS base

RUN apt-get update && apt-get install -y --no-install-recommends bash && npm install -g bun@1.3.10 && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .
RUN rm -f bun.lock && bun install

# Build discovery
FROM base AS builder

RUN cd web/discovery && bun run build
RUN test -d /app/web/discovery/dist || (echo "ERROR: dist directory not created during build" && ls -la && exit 1)

# Production stage
FROM caddy:alpine

WORKDIR /app/discovery

COPY --from=builder /app/web/discovery/dist ./dist
COPY web/discovery/Caddyfile /etc/caddy/Caddyfile

EXPOSE 5174

CMD ["caddy", "run", "--config", "/etc/caddy/Caddyfile"]
