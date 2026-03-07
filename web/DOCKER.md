# Docker Setup for Web Projects

This directory contains Docker configurations for all three web projects: discovery, web, and www.

## Architecture

- **Dockerfile.base** - Base configuration in `web/` (for reference/documentation)
- **discovery/Dockerfile** - Discovery app (SPA, served via http-server)
- **web/Dockerfile** - Web app (SPA, served via http-server)
- **www/Dockerfile** - WWW app (Frontend + Bun API backend)

Each Dockerfile uses a multi-stage build process:
1. **Builder stage**: Installs dependencies and builds the project
2. **Production stage**: Only includes built artifacts and runtime dependencies

## Building Individual Projects

```bash
# Build discovery app
docker build -f discovery/Dockerfile -t ascii-chat-discovery .

# Build web app
docker build -f web/Dockerfile -t ascii-chat-web .

# Build www app
docker build -f www/Dockerfile -t ascii-chat-www .
```

## Running Individual Projects

```bash
# Run discovery (listens on port 3001 → container 3000)
docker run -p 3001:3000 ascii-chat-discovery

# Run web (listens on port 3002 → container 3000)
docker run -p 3002:3000 ascii-chat-web

# Run www (listens on port 5173 for frontend, 3000 for API)
docker run -p 5173:5173 -p 3000:3000 ascii-chat-www
```

## Using Docker Compose

Run all three projects at once:

```bash
# Start all services
docker-compose up --build

# Run in background
docker-compose up -d --build

# View logs
docker-compose logs -f

# Stop all services
docker-compose down
```

Services will be available at:
- **Discovery**: http://localhost:3001
- **Web**: http://localhost:3002
- **WWW Frontend**: http://localhost:5173
- **WWW API**: http://localhost:3000

## Notes

- All builds require the monorepo root package.json and node_modules structure
- Each Dockerfile operates in the monorepo context (not in individual project directories)
- Production images are optimized with multi-stage builds
- node_modules are installed fresh in the builder stage
