# Discovery app - builds on shared
FROM ./Dockerfile.base AS base

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
