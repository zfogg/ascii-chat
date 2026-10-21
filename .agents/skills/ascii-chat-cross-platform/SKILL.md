---
name: ascii-chat-cross-platform
description: Work on ascii-chat for Linux and macOS from Windows using persistent Docker environments and the Tailscale-connected Mac over SSH. Use for platform coding, tests, runtime checks, and artifact verification.
---

# Cross-platform ascii-chat development

Use Linux containers for Linux work and the user's Mac for native macOS work. These environments support development and investigation as well as tests and artifact checks; using them does not itself authorize a release or deployment. Read `AGENTS.md` and inspect current presets and Docker definitions before adapting commands.

## Environment and source

- Windows uses Scoop LLVM; macOS prefers Homebrew LLVM; test containers install LLVM and libraries through Linux package managers. Use Clang and Ninja. Verify the selected compiler and CMake cache when reusing a build.
- Criterion is unsupported on Windows. Run it in Linux Docker; on this Intel Mac, use Docker for Criterion as the repository guide directs. Native macOS builds and runtime checks still belong on the Mac.
- Record the checkout, branch, SHA, dirty changes, OS, and architecture tested. A Docker source bind mount includes local edits; an SSH checkout does not. Transfer the intended commit or scoped patch explicitly and verify it arrived before interpreting results.
- Preserve native `build/`, Docker `build_docker/`, dependency caches, and unrelated work. Reconfigure rather than deleting builds. Separate incompatible architectures, libc variants, and configurations into distinct build/cache trees.

## Linux from PowerShell

Inspect `tests/Dockerfile`, `tests/Dockerfile.ubuntu`, `tests/docker-compose.yml`, and `tests/scripts/run-docker-tests.ps1` for development and tests. For packaging, inspect `tests/Dockerfile.debian`, `tests/Dockerfile.fedora`, and `tests/docker-compose.packages.yml`; for production images, inspect `docker/Dockerfile` and release CI. Select the environment for the task rather than assuming all images build the same configuration.

Persist both `build_docker/` and `.deps-cache-docker/` when using direct Docker runs, and preferably ccache too. From the Windows repo root, build the test image when missing or its inputs changed:

```powershell
docker build -f tests/Dockerfile -t ascii-chat-tests .
# Check $LASTEXITCODE before continuing after each native command.
$repo = (Get-Location).Path
New-Item -ItemType Directory -Force build_docker, .deps-cache-docker | Out-Null
docker run --rm --cap-add SYS_PTRACE `
  --mount "type=bind,source=$repo,target=/app" `
  --mount "type=bind,source=$repo/build_docker,target=/app/build_docker" `
  --mount "type=bind,source=$repo/.deps-cache-docker,target=/app/.deps-cache" `
  --mount 'type=volume,source=ascii-chat-ccache,target=/ccache' `
  -e TESTING=1 -e CCACHE_DIR=/ccache -w /app ascii-chat-tests `
  bash -lc 'cmake --preset docker && cmake --build build_docker --target tests && ctest --test-dir build_docker --output-on-failure --parallel 0'
```

Replace the shell payload for focused builds, debugging, artifact inspection, or runtime checks. Use `ctest --test-dir build_docker -R PATTERN --output-on-failure` for targeted tests; a Criterion binary needs a filter such as `--filter '*test_name*'`. Configure with the Docker preset before building; use the default preset for native builds.

Compose is also suitable: `docker compose -f tests/docker-compose.yml run --rm ascii-chat-tests` preserves build, dependencies, and ccache in named volumes. Keep the same Compose project name to reuse them. Inspect `docker compose ... config` first: the current Arch service references a root `Dockerfile` that may be absent; use a temporary override pointing its build to `tests/Dockerfile` if needed. The Ubuntu service currently hardcodes `DOCKER_OS=arm64`; match that value to the actual container architecture before using it on x86_64. Do not share compiled caches across architectures.

Named volumes can replace bind mounts if timestamp behavior makes Ninja rebuild unnecessarily. Keep them across runs; avoid `down -v`, volume pruning, and the runner's `-Clean` for routine work. Inspect the PowerShell runner before concurrent use: it removes a fixed-name container. Package Compose defaults delete build/dependency directories, so override those commands for incremental work.

For Git worktrees, ensure the container can resolve `.git` and initialized submodules. Host-absolute gitdir pointers may be invalid inside `/app`; mount necessary Git metadata at usable paths or use a valid Linux checkout rather than modifying the host gitfile.

## macOS over Tailscale SSH

The user's Mac is normally available as `loomen@zachbook-pro`; Tailscale is normally kept running on all machines. Its checkout is `~/src/github.com/zfogg/ascii-chat`:

```powershell
ssh loomen@zachbook-pro 'cd ~/src/github.com/zfogg/ascii-chat && git status --short && git branch --show-current && git rev-parse HEAD && uname -m'
```

Prefer single-quoted PowerShell SSH payloads so remote variables and shell substitutions reach the Mac unchanged. SSH sessions may lack the interactive Homebrew PATH. Locate `brew` (including `/usr/local/bin/brew` on Intel or `/opt/homebrew/bin/brew` on Apple Silicon), use `brew --prefix llvm`, and prepend that prefix's `bin` directory to PATH. Set `CC=clang` and `CXX=clang++`; verify `command -v clang` and `clang --version`. Apple developer tools are the fallback if Homebrew LLVM is unavailable; report that difference.

Before switching or updating the checkout, inspect dirty files and running builds. If a clean tree is needed, create a descriptive named stash, for example `git stash push --include-untracked -m "ascii-chat: preserve local work before cross-platform verification"`. Record its object ID and original branch/SHA. Do not stash ignored build caches. Fetch and fast-forward to the intended ref; do not hard-reset someone else's checkout or overwrite divergent commits.

After selecting the compiler, a native Debug build is:

```sh
cd ~/src/github.com/zfogg/ascii-chat &&
cmake --preset default -B build &&
cmake --build build
```

Reuse the Mac's dependency caches and compatible builds. Choose release or task-specific presets from `CMakePresets.json`. Run native smoke checks on the actual executable; Docker on the Mac still proves Linux behavior, not macOS behavior.

If valuable task changes are made on the Mac, commit only intended files and push to the appropriate task branch before finishing; record the pushed SHA so Windows can fetch it. Do not leave valuable work only on the remote machine or in a stash. This instruction covers preserving task changes, not publishing unrelated work. Restore pre-existing stashed work to its original branch when safe. Use the recorded stash rather than an assumed `stash@{0}`; verify restoration before dropping it. If conflicts prevent restoration, retain the stash and report its ID and unresolved state.

If SSH is unavailable, check connectivity and Tailscale rather than repeatedly retrying or changing machine configuration. Continue independent Windows/Linux work and report missing macOS evidence.

## Verify the actual claim

Use relevant tests and runtime scenarios for the change. Report commands, exit status, tested SHA or patch state, OS/architecture, and gaps. Compilation alone does not prove networking, media, or release runtime behavior.

For static-release checks, inspect the actual packaged executable and run it in the target environment. `cmake/utils/check_static_linking.sh BINARY linux /path/to/llvm-readelf` checks Linux ELF dependencies and interpreter; use `macos` for macOS. Linux release executables must have neither `NEEDED` entries nor `INTERP`; macOS may retain Apple system libraries. Pair linkage inspection with successful `--version` execution and a relevant smoke check. Follow [ascii-chat-cli-release](../ascii-chat-cli-release/SKILL.md) for an actual CLI release.
