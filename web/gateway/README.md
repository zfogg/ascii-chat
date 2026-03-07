# Caddy Reverse Proxy Gateway

This is the reverse proxy gateway for all ascii-chat web services. It uses Caddy to:

1. **Route requests** to the correct backend service (www, web, discovery)
2. **Manage SSL certificates** automatically with Let's Encrypt
3. **Serve on domains** instead of separate ports

## Configuration

The gateway uses `Caddyfile` for configuration. Two modes are provided:

### Development Mode (Local)

Routes services by path prefix on `http://localhost`:
- `http://localhost/www/` → www service (port 5173)
- `http://localhost/web/` → web service (port 3000)
- `http://localhost/discovery/` → discovery service (port 3000)

### Production Mode (Domains with HTTPS)

Uncomment and configure domain-based routing in `Caddyfile`:

```caddyfile
ascii-chat.com, www.ascii-chat.com {
  reverse_proxy www:5173
}

discovery.ascii-chat.com {
  reverse_proxy discovery:3000
}

web.ascii-chat.com {
  reverse_proxy web:3000
}
```

Caddy will automatically obtain and renew Let's Encrypt certificates.

## Running

From the `web/` directory:

```bash
# Build and start all services with the gateway
docker-compose up -d

# View logs
docker-compose logs -f gateway

# Stop
docker-compose down
```

## Data Volumes

The gateway stores Let's Encrypt certificates in Docker volumes:
- `caddy_data:/data/caddy` - Certificate data
- `caddy_config:/config/caddy` - Configuration cache

These persist across container restarts.

## Customization

Edit `Caddyfile` to:
- Add custom domains
- Change proxy routes
- Add middleware (compression, headers, etc.)
- Configure caching
- Add authentication

See [Caddy documentation](https://caddyserver.com/docs/) for full syntax.
