# MCP deployment instructions

- Keep this directory as the source of truth for the hosted MCP stack. Do not deploy production unless the user explicitly requests it.
- Keep `ascii-chat/config.toml`, the Compose mounts, Caddy routes, and documented URL aligned. Keep restricted MCP mode enabled and `max_clients` at 4 unless asked to change them.
- Keep the MCP process non-root with no capabilities or credentials. It must share the egress guard's network namespace and start only after the guard is healthy. Do not weaken the loopback-only default or present the deployment as public-ready without target-host isolation tests.
- Preserve Caddy's persistent certificate data. Never commit `.env`, keys, certificates, downloads, media, or test artifacts. Use Caddy's ACME client; do not add a competing renewal service without an explicit reason.
- For changes, validate both Compose configurations and the Caddyfile, run the guard probe and `smoke.py` against a local stack, and update the README with limits, security assumptions, and any untested behavior. Use the development override to build the application from the checkout; the production Compose file uses the configured GHCR image.
