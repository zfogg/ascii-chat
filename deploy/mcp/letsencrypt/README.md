# Let’s Encrypt

Caddy is the ACME client: the Caddy image installs and runs it, and Caddy obtains and renews Let’s Encrypt certificates. Do not install Certbot or run a competing renewal service. The Compose `caddy_data` volume persists `/data`; back it up securely, including the account key, and never commit it. `/config` stores Caddy runtime configuration. The ACME directory is configurable through `ACME_DIRECTORY` in `.env`.

For pre-production tests set `ACME_DIRECTORY=https://acme-staging-v02.api.letsencrypt.org/directory`. Staging certificates are intentionally untrusted. Before a production certificate, point Cloudflare DNS at the host, expose TCP 80 and 443, and use the production directory `https://acme-v02.api.letsencrypt.org/directory`. Use a real contact email. The default HTTP challenge does not need a Cloudflare API token or DNS plugin. If Cloudflare proxies the record, validate challenge routing and configure proxy trust and rate limiting as described in the deployment README.

Test certificate acquisition and renewal on the actual host; a local configuration validation is not proof that public ACME issuance works. See https://caddyserver.com/docs/automatic-https and https://letsencrypt.org/docs/staging-environment/.
