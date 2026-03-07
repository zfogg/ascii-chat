# WWW app (frontend + API) - extends base Dockerfile
FROM node:22-alpine AS builder

# Install bun
RUN npm install -g bun

WORKDIR /app

# Copy root dependencies
COPY package.json package-lock.json ./
RUN npm ci --legacy-peer-deps

# Copy entire monorepo
COPY . .

# Build www project (Vite frontend + bun API)
RUN cd www && npm run build

# Production stage
FROM node:22-alpine

# Install bun in production image
RUN npm install -g bun

WORKDIR /app/www

# Copy built assets from builder
COPY --from=builder /app/www/dist ./dist
COPY --from=builder /app/www/api.js ./api.js
COPY --from=builder /app/www/package.json ./package.json
COPY --from=builder /app/www/package-lock.json ./package-lock.json

# Install production dependencies
RUN npm ci --legacy-peer-deps --only=production

# Environment variable for API port (default 3001)
ENV API_PORT=30001

# Expose ports
EXPOSE 5173 30001

# Start both frontend server and API
# Frontend on 5173, API on API_PORT (default 30001)
CMD ["bun", "api.js"]
