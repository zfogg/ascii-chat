# Shared base and package build
FROM node:22-alpine AS base

# Install bash and bun
RUN apk add --no-cache bash && npm install -g bun

WORKDIR /app

# Copy entire monorepo (preserves workspace structure)
COPY . .

# Install dependencies for entire monorepo workspace
RUN bun install

# Build shared package
FROM base AS shared

RUN cd web && bun run --filter './packages/shared' build
