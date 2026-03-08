# Shared package build
FROM node:22-alpine AS builder

# Install bash and bun
RUN apk add --no-cache bash && npm install -g bun

WORKDIR /app

# Copy entire monorepo (preserves workspace structure)
COPY . .

# Install dependencies for entire monorepo workspace
RUN bun install

# Build shared package only
RUN cd web && bun run --filter './packages/shared' build

# Final stage - just the built dist
FROM node:22-alpine

WORKDIR /app/web/packages/shared

# Copy built dist from builder
COPY --from=builder /app/web/packages/shared/dist ./dist
COPY --from=builder /app/web/packages/shared/package.json ./package.json

# Keep this image running for other services to reference
CMD ["sleep", "infinity"]
