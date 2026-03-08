# Discovery web app
FROM zfogg/ascii-chat-web-shared:latest AS builder

# Build discovery project only
RUN cd web && bun run --filter './discovery' build

# Clean up node_modules and other packages (keep discovery)
RUN rm -rf /app/node_modules /app/packages /app/www /app/bun.lock*

# Production stage with Caddy
FROM caddy:alpine

WORKDIR /app/discovery

# Copy built dist from builder
COPY --from=builder /app/discovery/dist ./dist

# Copy Caddyfile
COPY web/discovery/Caddyfile /etc/caddy/Caddyfile

# Expose port for serving
EXPOSE 5174

# Serve with Caddy
CMD ["caddy", "run", "--config", "/etc/caddy/Caddyfile"]
