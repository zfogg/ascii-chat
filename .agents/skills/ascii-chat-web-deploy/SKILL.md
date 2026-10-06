---
name: ascii-chat-web-deploy
description: Deploy or repair the three ascii-chat Vercel websites and their Cloudflare DNS. Use for releases to ascii-chat.com, web.ascii-chat.com, or discovery.ascii-chat.com; not for the ACDS server itself.
---

# Ascii Chat Web Deploy

Use this skill when publishing or diagnosing the public ascii-chat websites.

## Release topology

| Site | Vercel project | Root directory | Release branch |
| --- | --- | --- | --- |
| `ascii-chat.com` | `ascii-chat-www` | `web/www` | `deploy-web-www` |
| `web.ascii-chat.com` | `ascii-chat-web` | `web/web` | `deploy-web-web` |
| `discovery.ascii-chat.com` | `ascii-chat-discovery` | `web/discovery` | `deploy-web-discovery` |

Each project is intentionally disconnected from Vercel's Git integration. Do not reconnect it or use ordinary Git pushes as deployments. The repository GitHub Actions workflows `.github/workflows/deploy-web-{www,web,discovery}.yml` use `pnpm` and the `VERCEL_TOKEN` GitHub secret to make the sole production deployment when its matching release branch is pushed.

## Release procedure

1. Preserve a dirty checkout. From a clean worktree or without switching branches, bring the intended remote commit to the matching release branch:

   ```bash
   git fetch origin
   git push origin origin/master:deploy-web-<site>
   ```

   Replace `<site>` with `www`, `web`, or `discovery`. Do not force-push a release branch.

2. Confirm the matching GitHub Actions run succeeds before treating the release as deployed. The deployment must use pnpm; do not substitute npm, Docker, or a manual Vercel Git deployment.

3. Open the public URL directly and take a fresh screenshot/accessibility check. For the web client, test direct deep links such as `/mirror`, `/client`, and `/discovery`, not just in-app navigation.

## Build and routing notes

- `web/web` is a Vite single-page app. Its `vercel.json` rewrite is required so direct client routes do not receive Vercel's 404 page. Keep the rewrite when modifying deployment configuration.
- `web/discovery` requires the `SSH_PUBLIC_KEY` and `GPG_PUBLIC_KEY` Vercel environment variables. Its Vercel build command must generate those key files, run `pnpm run vite:build`, and copy `dist/index.html` to `dist/404.html`. Avoid the full `pnpm run build` in Vercel: its formatter can inspect Vercel-generated `.vercel` files and fail the build.
- `web/discovery` endpoint configuration is shared by the page and its status functions. Set these Vercel Production environment variables when hosting it elsewhere; omit any of them to use the bundled official defaults:

  | Variable | Format | Current default |
  | --- | --- | --- |
  | `DISCOVERY_WEBRTC_SERVERS` | Comma-separated `ws://` or `wss://` URLs; bare `host:port` defaults to `wss` except port 80, which defaults to `ws`. | `wss://discovery-service.ascii-chat.com:443` |
  | `DISCOVERY_STUN_SERVERS` | Comma-separated `host:port` values. Whitespace around commas is ignored. | `stun.ascii-chat.com:3478,stun.l.google.com:19302` |
  | `DISCOVERY_TURN_SERVERS` | Comma-separated `host:port` values. Whitespace around commas is ignored. | `turn.ascii-chat.com:3478` |

  The Vite build exposes these non-secret values to the frontend, and the Vercel functions use the same values at runtime. Set all three before production deployment when overriding defaults. `DISCOVERY_STATUS_TURN_USERNAME` and `DISCOVERY_STATUS_TURN_PASSWORD` remain server-only credentials for authenticated TURN probes.
- `web/discovery/api/session-strings.ts` is a same-origin Vercel Function. It uses the C-derived word lists and Web Crypto rejection sampling; do not replace it with a downloaded native binary or cache its responses.
- `web/www/api/session-strings.ts` uses the same shared generator. The main homepage fetches this same-origin function, so keep it deployed whenever changing session-string presentation.
- For a local web build, use pnpm. If the isolated worktree has no dependencies, report that limitation rather than installing unrelated dependencies into the user's checkout.

## Domains and DNS

Cloudflare manages `ascii-chat.com` DNS. Before changing records, obtain explicit confirmation naming the target hostname and the records being replaced. Use Vercel's currently displayed CNAME/TXT values; they are project-specific and can change.

- Set the Vercel CNAME and ownership TXT records to **DNS only** (not proxied).
- A hostname moving from the previous Sidechain host has both an A and AAAA record; remove the AAAA record before converting the A record to a CNAME.
- Multiple `_vercel` TXT records can coexist. Add the required verification value; never overwrite an existing Vercel verification record for another site.
- Never alter `discovery-service.ascii-chat.com`, `stun.ascii-chat.com`, or `turn.ascii-chat.com` while deploying the discovery website. Those are service endpoints, not the `discovery.ascii-chat.com` frontend.

After DNS propagation, wait for Vercel to show **Valid Configuration**, then directly load and visually verify the hostname. Leave the verified site open for the user when appropriate.
