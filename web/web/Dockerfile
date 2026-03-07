# Web app - extends base Dockerfile
FROM node:22-alpine AS builder

# Install bun
RUN npm install -g bun

WORKDIR /app

# Copy root dependencies
COPY package.json package-lock.json ./
RUN npm ci --legacy-peer-deps

# Copy entire monorepo
COPY . .

# Build web project
RUN cd web && npm run build

# Production stage
FROM node:22-alpine

WORKDIR /app/web

# Copy built dist from builder
COPY --from=builder /app/web/dist ./dist

# Environment variable for API port
ENV API_PORT=3001

# Expose port for serving
EXPOSE 3000

# Serve static files (requires a simple HTTP server)
RUN npm install -g http-server
CMD ["http-server", "dist", "-p", "3000"]
