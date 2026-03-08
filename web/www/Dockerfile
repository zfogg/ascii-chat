# WWW app (frontend + API)
FROM node:22-alpine AS builder

# Install bash and bun
RUN apk add --no-cache bash && npm install -g bun

WORKDIR /app

# Copy entire web directory (preserves workspace structure)
COPY . .

# Install dependencies
RUN bun install

# Build www project only
RUN bun run --filter './www' build

# Production stage - Caddy + API
FROM caddy:alpine AS caddy
FROM node:22-alpine

# Install bun and git (for commit SHA detection)
RUN apk add --no-cache git && npm install -g bun

# Copy caddy binary
COPY --from=caddy /usr/bin/caddy /usr/local/bin/caddy

WORKDIR /app/www

# Copy root node_modules to both root and www (for import resolution)
COPY --from=builder /app/node_modules /app/node_modules
COPY --from=builder /app/node_modules ./node_modules

# Copy only www dist and specific files from builder
COPY --from=builder /app/www/dist ./dist
COPY --from=builder /app/www/api.js ./api.js
COPY --from=builder /app/www/package.json ./package.json
COPY --from=builder /app/www/src ./src

# Copy Caddyfile for frontend serving + API proxying
COPY www/Caddyfile /etc/caddy/Caddyfile

# Environment variable for API port (default 3001)
ENV API_PORT=3001

# Expose ports
EXPOSE 5173 3001

# Start script that runs both API and Caddy
RUN echo '#!/bin/sh' > /start.sh && \
    echo 'bun api.js &' >> /start.sh && \
    echo 'caddy run --config /etc/caddy/Caddyfile' >> /start.sh && \
    chmod +x /start.sh

CMD ["/start.sh"]
