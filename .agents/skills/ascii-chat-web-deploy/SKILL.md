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
   git switch master
   git merge --ff-only origin/master
   git merge origin/deploy-web-www
   git merge origin/deploy-web-web
   git merge origin/deploy-web-discovery
   git push origin HEAD:master
   ```

   Run each merge only when needed; preserve all release-branch commits rather than replacing the branch or force-pushing it.

2. Validate the browser artifacts using the conditional Release-build procedure below, commit any regenerated canonical artifacts, and push `master`. Do not advance release branches until validation passes.

3. From the reconciled `master`, bring the intended remote commit to the matching release branch:

   ```bash
   git fetch origin
   git push origin origin/master:deploy-web-<site>
   ```

   Replace `<site>` with `www`, `web`, or `discovery`. Do not force-push a release branch.

4. Confirm the matching GitHub Actions run succeeds before treating the release as deployed. The deployment must use pnpm; do not substitute npm, Docker, or a manual Vercel Git deployment.

5. Open the public URL directly and take a fresh screenshot/accessibility check. For the web client, test direct deep links such as `/mirror`, `/client`, and `/discovery`, not just in-app navigation.

## Build and routing notes

- `web/web` is a Vite single-page app. Its `vercel.json` rewrite is required so direct client routes do not receive Vercel's 404 page. Keep the rewrite when modifying deployment configuration.
- `web/discovery` requires `SSH_PUBLIC_KEY` and `GPG_PUBLIC_KEY` Vercel environment variables in both Production and Preview. Set them to the complete SSH public-key line and armored GPG public-key block, respectively; fingerprints are derived display values and are not environment variables. `scripts/generate-keys.sh` writes them to `public/key.pub` and `public/key.gpg`. Its Vercel build command must be `bash scripts/generate-keys.sh && pnpm run vite:build && cp dist/index.html dist/404.html`. Avoid the full `pnpm run build` in Vercel: its formatter can inspect Vercel-generated `.vercel` files and fail the build.
- `web/www/api/session-strings.ts` is the canonical Vercel function: `https://www.ascii-chat.com/api/session-strings?count=<1-20>`. It uses the shared C-derived word lists and Web Crypto rejection sampling. Both the www and discovery pages use this endpoint in production; keep its discovery-origin CORS response and do not replace it with a downloaded native binary or cache its responses.
- `web/discovery` endpoint configuration is shared by the page and its status functions. Set these Vercel Production environment variables when hosting it elsewhere; omit any of them to use the bundled official defaults:

  | Variable | Format | Current default |
  | --- | --- | --- |
  | `DISCOVERY_WEBRTC_SERVERS` | Comma-separated `ws://` or `wss://` URLs; bare `host:port` defaults to `wss` except port 80, which defaults to `ws`. | `wss://discovery-service.ascii-chat.com:443` |
  | `DISCOVERY_STUN_SERVERS` | Comma-separated `stun:host:port` URLs. Whitespace around commas is ignored; bare `host:port` remains accepted for compatibility. | `stun:stun.ascii-chat.com:3478,stun:stun.l.google.com:19302` |
  | `DISCOVERY_TURN_SERVERS` | Comma-separated `turn:host:port` URLs. Whitespace around commas is ignored; bare `host:port` remains accepted for compatibility. | `turn:turn.ascii-chat.com:3478` |

  STUN and TURN default to port `3478` over UDP when omitted. Use `stuns:host[:port]` or `turns:host[:port]` for TLS; status functions probe those endpoints over TLS and default omitted ports to `5349`. These non-secret values are exposed to the frontend at Vite build time, and Vercel functions use the same values at runtime. `DISCOVERY_STATUS_TURN_USERNAME` and `DISCOVERY_STATUS_TURN_PASSWORD` remain server-only credentials for authenticated TURN probes.

### Conditional WASM Release builds

- Compile `mirror-web` and `client-web` and generate their JavaScript glue **only when needed**: canonical artifacts are missing, source or artifact hash validation fails, relevant compiler/build settings changed, or their Release-build provenance cannot be established. A frontend-only, documentation, or skill change does not by itself require regenerating WASM or its JavaScript. Never rewrite the manifest to make stale binaries appear current.
- First run `vp exec node web/web/scripts/wasm-build.mjs --publish-only` from the repository root. This checks the current source hash and every canonical artifact hash, then copies the verified assets to runtime URLs; it does not compile WASM or generate JavaScript glue. Reuse passing artifacts when their Release provenance is known.
- When regeneration is necessary, use the **main worktree** and `./build/`, with the effective WASM CMake build type set to **Release**. Keep the main worktree on `master`; use `codex/` branches for separate agent worktrees. Dependency build/install directories are separated by build type, so a separate `build_release/` tree is unnecessary. Preserve the native configuration: a native Debug cache in `build/` can drive the independent Release WASM configuration in `build/web/`. Verify the nested cache rather than inferring the WASM build type from the native cache or directory name.
- If `build/web/CMakeCache.txt` already says `Release`, build all browser targets through the shared parent target. Parent WASM targets share a directory lock, including across separate build commands, so they cannot run competing Ninja processes in `build/web/`:

  ```bash
  cmake --build build --target web
  ```

- If the nested browser configuration is absent or needs its build type changed, configure it explicitly with Emscripten and Ninja without replacing the native cache:

  ```bash
  emcmake cmake -S src/web -B build/web -G Ninja -DCMAKE_BUILD_TYPE=Release -DPARENT_CMAKE_BINARY_DIR="<absolute-repository-path>/build"
  cmake --build build/web --config Release --target mirror-web client-web
  ```

  The parent build must contain `generated/data/fonts/default.c` and `matrix_resurrected.c`; reuse verified generated fonts or generate them with the repository font targets first. On the current Windows Emscripten SDK, `-DCMAKE_EXE_LINKER_FLAGS=-sMALLOC=emmalloc` avoids the bundled dlmalloc/Clang alias incompatibility. Use `-DFETCHCONTENT_FULLY_DISCONNECTED=ON` only when the dependency cache is already populated.

  Confirm `CMAKE_BUILD_TYPE:STRING=Release` in the effective WASM `CMakeCache.txt` before publishing. Never publish Debug artifacts. CMake's incremental build determines which objects and generated glue actually need regeneration.
- After the build succeeds, publish its outputs using `ASCII_CHAT_WASM_ARTIFACT_DIR=build/web vp exec node web/web/scripts/wasm-build.mjs` from the repository root (in PowerShell, set `$env:ASCII_CHAT_WASM_ARTIFACT_DIR = 'build/web'` first). This copies the completed outputs into the canonical artifact directory without invoking another build. Use this option only after verifying that both Release targets built successfully from the current sources. Re-run `--publish-only` afterward to verify the final source/artifact hashes.
- Commit regenerated `mirror.js`, `mirror.wasm`, `client.js`, `client.wasm`, and `manifest.json` only in `web/web/src/wasm/dist/`. `web/web/public/wasm/` contains ignored runtime copies; do not force-add them. Record the Release configuration and build directory in the artifact commit message so subsequent deployments can establish provenance and skip unnecessary compilation.

- For local installation, checks, tests, and builds, follow `web/AGENTS.md` and use the `vp` wrappers. GitHub deployment workflows use pnpm.

## Domains and DNS

Cloudflare manages `ascii-chat.com` DNS. Before changing records, obtain explicit confirmation naming the target hostname and the records being replaced.
- Never alter `discovery-service.ascii-chat.com`, `stun.ascii-chat.com`, or `turn.ascii-chat.com` while deploying the discovery website. Those are service endpoints, not the `discovery.ascii-chat.com` frontend.
