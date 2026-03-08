# Web app
FROM node:22-alpine AS builder

# Install bash and bun
RUN apk add --no-cache bash && npm install -g bun

WORKDIR /app

# Copy entire monorepo (preserves workspace structure)
COPY . .

# Install dependencies from web workspace root
RUN cd web && bun install

# Build web project (type check + vite build)
RUN cd web && bun x tsc --noEmit && bun run --filter './web' build

# Clean up node_modules and other projects
RUN rm -rf /app/node_modules /app/discovery /app/www /app/packages /app/bun.lock*

# Production stage with Caddy
FROM caddy:alpine

WORKDIR /app/web

# Copy built dist from builder
COPY --from=builder /app/web/dist ./dist

# Copy Caddyfile
COPY web/web/Caddyfile /etc/caddy/Caddyfile

# Expose port for serving
EXPOSE 3000

# Serve with Caddy
CMD ["caddy", "run", "--config", "/etc/caddy/Caddyfile"]
