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

# Production stage
FROM node:22-alpine

WORKDIR /app

# Copy built dist from builder
COPY --from=builder /app/web/dist ./dist

# Environment variable for API port
ENV API_PORT=3001

# Expose port for serving
EXPOSE 3000

# Serve static files
RUN npm install -g http-server
CMD ["http-server", "dist", "-p", "3000"]
