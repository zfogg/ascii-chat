# Discovery web app
FROM node:22-alpine AS builder

# Install bash and bun
RUN apk add --no-cache bash && npm install -g bun

WORKDIR /app

# Copy entire web directory (preserves workspace structure)
COPY . .

# Install dependencies
RUN bun install

# Build discovery project only
RUN cd web && bun run --filter './discovery' build

# Clean up node_modules
RUN rm -rf /app/node_modules /app/packages /app/web/www /app/web/web /app/bun.lock*

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
