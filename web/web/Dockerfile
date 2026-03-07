# Web app
FROM node:22-alpine AS builder

# Install bash and bun
RUN apk add --no-cache bash && npm install -g bun

WORKDIR /app

# Copy entire web directory (preserves workspace structure)
COPY . .

# Install dependencies
RUN bun install

# Build web project (type check + vite build)
RUN cd web && bun run type-check && bun x vite build

# Clean up node_modules and other projects
RUN rm -rf /app/node_modules /app/discovery /app/www /app/packages /app/bun.lock*

# Production stage with Caddy
FROM caddy:alpine

WORKDIR /app/web

# Copy built dist from builder
COPY --from=builder /app/web/dist ./dist

# Copy Caddyfile
COPY web/Caddyfile /etc/caddy/Caddyfile

# Expose port for serving
EXPOSE 3000

# Serve with Caddy
CMD ["caddy", "run", "--config", "/etc/caddy/Caddyfile"]
