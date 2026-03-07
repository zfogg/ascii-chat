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
# Build discovery app (frontend on 5174)
docker build -f discovery/Dockerfile -t ascii-chat-discovery .

# Build web app (frontend on 3000)
docker build -f web/Dockerfile -t ascii-chat-web .

# Build www app (frontend on 5173, API on 30001)
docker build -f www/Dockerfile -t ascii-chat-www .
```

## Running Individual Projects

```bash
# Run discovery (frontend on port 5174)
docker run -p 5174:3000 ascii-chat-discovery

# Run web (frontend on port 3000)
docker run -p 3000:3000 ascii-chat-web

# Run www (frontend on port 5173, API on port 30001)
docker run -p 5173:5173 -p 30001:30001 -e API_PORT=30001 ascii-chat-www
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
- **Discovery Frontend**: http://localhost:5174
- **Web Frontend**: http://localhost:3000
- **WWW Frontend**: http://localhost:5173
- **WWW API**: http://localhost:30001

## Notes

- All builds require the monorepo root package.json and node_modules structure
- Each Dockerfile operates in the monorepo context (not in individual project directories)
- Production images are optimized with multi-stage builds
- node_modules are installed fresh in the builder stage
