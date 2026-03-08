# Discovery app - builds on shared
FROM node:22-alpine AS base

RUN apk add --no-cache bash && npm install -g bun

WORKDIR /app
COPY . .
RUN cd web && bun install

# Build discovery
FROM base AS builder

RUN cd web && bun run --filter './discovery' build
RUN rm -rf /app/node_modules /app/packages /app/www /app/bun.lock*

# Production stage
FROM caddy:alpine

WORKDIR /app/discovery

COPY --from=builder /app/discovery/dist ./dist
COPY web/discovery/Caddyfile /etc/caddy/Caddyfile

EXPOSE 5174

CMD ["caddy", "run", "--config", "/etc/caddy/Caddyfile"]
