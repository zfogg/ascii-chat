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

# Production stage
FROM caddy:alpine AS caddy_base

# Production stage - combined
FROM node:22-alpine

# Install bun and caddy
RUN npm install -g bun
COPY --from=caddy_base /usr/bin/caddy /usr/local/bin/caddy

WORKDIR /app

# Copy monorepo structure from builder (needed for workspace resolution)
COPY --from=builder /app/package.json ./package.json
COPY --from=builder /app/packages ./packages
COPY --from=builder /app/www ./www

# Copy prebuilt node_modules from builder (skip reinstall)
COPY --from=builder /app/node_modules ./node_modules
COPY --from=builder /app/bun.lock* ./

WORKDIR /app/www

# Copy Caddyfile for frontend serving + API proxying
COPY www/Caddyfile /etc/caddy/Caddyfile

# Environment variable for API port (default 30001)
ENV API_PORT=30001

# Expose ports
EXPOSE 5173 30001

# Start script that runs both API and Caddy
RUN echo '#!/bin/sh' > /start.sh && \
    echo 'bun api.js &' >> /start.sh && \
    echo 'caddy run --config /etc/caddy/Caddyfile' >> /start.sh && \
    chmod +x /start.sh

CMD ["/start.sh"]
