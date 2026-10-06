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

1. Preserve a dirty checkout. Before every release, reconcile all three release branches into `master` from a clean worktree. A release branch may contain direct agent work, so never assume `master` already contains it. Merge each branch that is not already an ancestor of `origin/master`, resolve and verify any conflicts, then push `master`:

   ```bash
   git fetch origin
   git checkout --detach origin/master
   git merge origin/deploy-web-www
   git merge origin/deploy-web-web
   git merge origin/deploy-web-discovery
   git push origin HEAD:master
   ```

   Run each merge only when needed; preserve all release-branch commits rather than replacing the branch or force-pushing it.

2. From the reconciled `master`, bring the intended remote commit to the matching release branch:

   ```bash
   git fetch origin
   git push origin origin/master:deploy-web-<site>
   ```

   Replace `<site>` with `www`, `web`, or `discovery`. Do not force-push a release branch.

3. Confirm the matching GitHub Actions run succeeds before treating the release as deployed. The deployment must use pnpm; do not substitute npm, Docker, or a manual Vercel Git deployment.

4. Open the public URL directly and take a fresh screenshot/accessibility check. For the web client, test direct deep links such as `/mirror`, `/client`, and `/discovery`, not just in-app navigation.

## Build and routing notes

- `web/web` is a Vite single-page app. Its `vercel.json` rewrite is required so direct client routes do not receive Vercel's 404 page. Keep the rewrite when modifying deployment configuration.
- `web/discovery` requires `SSH_PUBLIC_KEY` and `GPG_PUBLIC_KEY` Vercel environment variables in both Production and Preview. Set them to the complete SSH public-key line and armored GPG public-key block, respectively; fingerprints are derived display values and are not environment variables. `scripts/generate-keys.sh` writes them to `public/key.pub` and `public/key.gpg`. Its Vercel build command must be `bash scripts/generate-keys.sh && pnpm run vite:build && cp dist/index.html dist/404.html`. Avoid the full `pnpm run build` in Vercel: its formatter can inspect Vercel-generated `.vercel` files and fail the build.
- `web/www/api/session-strings.ts` is the canonical Vercel function: `https://www.ascii-chat.com/api/session-strings?count=<1-20>`. It uses the shared C-derived word lists and Web Crypto rejection sampling. Both the www and discovery pages use this endpoint in production; keep its discovery-origin CORS response and do not replace it with a downloaded native binary or cache its responses.
- If a release includes C/C++/header/CMake changes that affect the browser modules (`src/web`, `lib`, `include`, `cmake`, or the root `CMakeLists.txt`), rebuild the checked-in WASM assets before releasing: `cd web/web && pnpm run wasm:build`. Commit the regenerated mirror and client JavaScript/WASM binaries plus `manifest.json` in both `web/web/src/wasm/dist/` and `web/web/public/wasm/`. The script verifies the source hash, so a stale prebuilt module must not be deployed.
- For a local web build, use pnpm. If the isolated worktree has no dependencies, report that limitation rather than installing unrelated dependencies into the user's checkout.

## Domains and DNS

Cloudflare manages `ascii-chat.com` DNS. Before changing records, obtain explicit confirmation naming the target hostname and the records being replaced.
- Never alter `discovery-service.ascii-chat.com`, `stun.ascii-chat.com`, or `turn.ascii-chat.com` while deploying the discovery website. Those are service endpoints, not the `discovery.ascii-chat.com` frontend.
