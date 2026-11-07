# Hosted MCP deployment

This stack runs the restricted ascii-chat MCP server behind Caddy. The default ports bind to **loopback only**; it does not publish a production endpoint. The deployment PR depends on the MCP downloads PR and an image containing those changes. `ghcr.io/zfogg/ascii-chat:latest` must be published with that code before production use. Prefer an immutable image digest in production after confirming the release.

## Files and configuration

- `compose.yml`: production image, Caddy, an egress guard, resource limits and bounded ephemeral storage.
- `compose.dev.yml`: builds the MCP image from this checkout using `docker/Dockerfile`, tagged `ascii-chat-mcp:dev`.
- `ascii-chat/config.toml`: mounted read-only as `/tmp/ascii-chat-config.toml`; this location is allowed by ascii-chat's config path validator. It configures restricted HTTP mode, port, four jobs, and downloads. The production download URL comes from TOML. The development override sets `ASCII_CHAT_DOWNLOADS_URL_PREFIX` from `MCP_PUBLIC_URL` for local testing; environment values override TOML. The binary's `--config` argument precedes `mcp-server`.
- `caddy/`: Caddyfile and a Caddy image with a pinned rate-limit plugin.
- `egress/`: a firewall sidecar that owns the MCP network namespace.
- `letsencrypt/`: certificate management and staging notes. Caddy manages certificates itself; no Certbot service or systemd in a container is needed.

Copy `.env.example` to `.env`, set a real `ACME_EMAIL`, and edit the other values. `.env` is local configuration and must not contain secrets. For a real deployment, set `MCP_BLOCKED_PUBLIC_IPS` to a space-separated list of **all** public IPs of the origin host and protected infrastructure reachable from it. Include public addresses that route back into the host or into otherwise protected services. Private and special-use IPv4 ranges are blocked automatically, IPv6 is disabled, and DNS replies cannot bypass the destination firewall. See the security section below.

From this directory:

```sh
cp .env.example .env
# Check the effective configuration and test locally before publishing.
docker compose --env-file .env config
docker compose --env-file .env up -d --build
# Development: requires initialized recursive submodules and the Dockerfile's dependency image.
docker compose --env-file .env -f compose.yml -f compose.dev.yml up -d --build
```

For plain local HTTP testing, set `MCP_SITE=http://localhost`, `MCP_PUBLIC_URL=http://localhost:8080`, and use the development override with the default loopback ports. Requests must use the configured Host name. To switch back to GHCR, use only the base Compose file and run `docker compose --env-file .env pull mcp`, then `docker compose --env-file .env up -d --build`. `--build` builds the local guard and Caddy images; the base file does not build MCP. Compose environment variables override TOML, and CLI options override both.

The image runs `/usr/local/bin/ascii-chat` directly instead of its discovery-service entrypoint, which generates keys. A root-only tmpfs masks `/acds`; do not put credentials in the image or mount Docker's socket. The container is non-root with a read-only root filesystem, no added capabilities, and `no-new-privileges`. The guard has `NET_ADMIN` in its own shared network namespace; the MCP container has no such capability.

## Limits and storage

MCP allows four concurrent render jobs. Defaults are 4 CPUs, 4 GiB memory, 128 PIDs, 512 MiB `/tmp` scratch, and 1 GiB `/downloads`; adjust `.env` based on host capacity and measured workloads. Caddy has separate CPU, memory and PID limits. Logs use bounded Docker rotation. Temporary filesystem limits count toward the MCP container's memory limit and can use swap: the configured maximums are not reserved disk. Downloads and scratch disappear on container recreation or host reboot, so download links can stop working before their 30-minute expiry. For durable downloads, replace `/downloads` tmpfs with a dedicated, writable filesystem with an enforced host quota; a normal named volume has no portable size limit. Never share it with unrelated files. Validate disk-full, memory pressure and recovery before public use.

Caddy caps `/mcp` request bodies at 24 MiB, allows 75 seconds for upstream response headers, bounds header/body reads and idle connections, and limits each direct client IP to 30 requests per minute by default. Tune `MCP_RATE_EVENTS` and `MCP_RATE_WINDOW` for real clients. The plugin is pinned in `caddy/Dockerfile`. No Caddy access log is enabled: download URLs are bearer credentials. Treat error logs as sensitive too, since they may contain request URLs. Caddy initially uses the direct peer IP; leave the Cloudflare DNS record DNS-only unless you configure trusted proxy ranges and client-IP handling correctly. If Cloudflare is proxied, enforce Cloudflare/origin firewall rules and test the client-IP limits; otherwise all Cloudflare users may share one bucket or direct traffic may bypass edge controls. Rate limits do not replace authentication if the service needs it.

## Network isolation and startup

The `egress` sidecar applies IPv4 rules **before** becoming healthy; MCP shares that namespace and cannot modify the firewall. The policy denies loopback except Docker DNS, private, carrier NAT, link-local, documentation, benchmark, multicast and reserved IPv4 ranges, plus `MCP_BLOCKED_PUBLIC_IPS`. Established replies and public IPv4 egress remain allowed. Docker's embedded DNS is allowed, but requests to addresses returned by DNS, redirects and nested media requests are subject to the same firewall. IPv6 is disabled. The proxy can initiate connections to MCP port 27228, but that port is not published on the host. The guard is Linux Docker-specific and needs NET_ADMIN; it is not equivalent to a per-job sandbox. Do not mount secrets, host directories, devices, or Docker sockets into MCP.

The MCP service intentionally has `restart: no`: a Docker daemon restart must not restart it ahead of its firewall. Start the stack with Compose, which waits for guard health, after host reboot or failure. If the guard is recreated, run `docker compose up -d --force-recreate mcp caddy` after the guard is healthy so MCP joins the new namespace. For automatic boot, adapt the example under `systemd/` to run Compose from a host systemd unit after Docker starts; do not put systemd inside the containers. Monitor all three services and alert on guard or MCP failure.

Run `docker compose --env-file .env exec -T egress /usr/local/bin/egress-guard probe` for rule, DNS, private-address and public-connectivity checks. A failed connection alone does not establish that a destination was blocked by policy. Before exposing the service, verify on the target host that the guard stays healthy, the container shares the guard network namespace, IPv6 is disabled, DNS and public URLs work, and direct, redirected, and nested requests cannot reach loopback, RFC1918, link-local/metadata, the host's public addresses, or protected networks. Configure host firewall rules as defense in depth and restrict direct origin access if using Cloudflare proxying. `MCP_BLOCKED_PUBLIC_IPS` is operator-supplied: the sample cannot know your host or infrastructure addresses. Do not expose the service if any check fails. A sandbox and egress controls cannot eliminate decoder vulnerabilities; deploy with no secrets and keep the image patched.

## Public HTTPS and checks

After the isolation checks pass, set `MCP_SITE=mcp.ascii-chat.com`, `MCP_BIND_ADDRESS=0.0.0.0`, `MCP_HTTP_PORT=80`, and `MCP_HTTPS_PORT=443`; open only TCP 80/443 at the host firewall. Set the Cloudflare A record to the host; only add AAAA when the host and proxy are deliberately configured for IPv6 ingress. Follow `letsencrypt/README.md` for staging then production certificates. Do not expose the backend port. Check `docker compose ps`, Caddy logs and MCP logs, then run:

```sh
python smoke.py --url https://mcp.ascii-chat.com --animated
```

The smoke test requests a generated pattern, retrieves its temporary PNG, checks the public URL and proxy body limit, and confirms local/metadata URL requests fail. With `--animated`, it also generates a video locally with FFmpeg, uploads it, and checks that the downloaded GIF has distinct frames; this option requires `ffmpeg` and `ffprobe` on the test machine. It does not prove every egress route is blocked; test redirect and nested-playlist cases and your actual protected public addresses separately. Monitor capacity, rejected requests, certificate renewal, and download expiry. CI and CodeQL must pass on the exact deployed revision.

For rollback, choose a previously verified image digest in `.env`, then run `docker compose --env-file .env up -d --build`; preserve the Caddy volumes. Do not use `down -v` for routine updates.

The backend network uses `172.30.253.0/24`; choose a non-overlapping subnet and update the static guard address and probes if this conflicts with your host or VPN. After changing the mounted Caddyfile, run `docker compose --env-file .env restart caddy` and validate the new configuration.
