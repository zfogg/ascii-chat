# Caddy reverse proxy gateway
FROM caddy:alpine

# Copy Caddyfile configuration
COPY web/gateway/Caddyfile /etc/caddy/Caddyfile

# Create data directory for Let's Encrypt certificates
RUN mkdir -p /data/caddy /config/caddy

# Expose HTTP and HTTPS ports
EXPOSE 80 443

# Caddy will automatically reload the config and manage certificates
CMD ["caddy", "run", "--config", "/etc/caddy/Caddyfile", "--adapter", "caddyfile"]
