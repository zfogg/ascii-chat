# Websites

- **`www/` — ascii-chat.com:** Project homepage with installation instructions,
  a live ASCII mirror demo, usage guides, and searchable CLI, configuration, and
  C API manual pages (`/man1`, `/man5`, `/man3`).
- **`web/` — web.ascii-chat.com:** Browser client using the C engine compiled to
  WebAssembly. `/mirror` renders webcam, file, or URL media locally; `/client`
  connects to servers over WebSocket; `/discovery` creates or joins named sessions
  through ACDS and WebRTC.
- **`discovery/` — discovery.ascii-chat.com:** Discovery-service information site
  with signaling/STUN/TURN status checks, SSH/GPG public keys and fingerprints,
  connection examples, security guidance, and self-hosting instructions.

All three are React/TypeScript apps and share components and utilities through
`packages/shared/` (`@ascii-chat/shared`).

## ChatGPT Sites

- **`chatgpt-sites/ascii-chat-code-atlas/` — code-guide.ascii-chat.com:**
  Interactive source map and guided walkthroughs of native C, TypeScript, and
  shared C/WASM implementations. This static Python/JavaScript project is hosted
  with the Sites plugin, separately from the three Vercel apps. Its dependencies
  use the shared pnpm workspace. Keep future ChatGPT Sites projects under
  `chatgpt-sites/`.
- Keep generators, guide definitions, dependency manifests/lockfiles, authored
  `dist/index.html`, `dist/app.js`, `dist/guides.js`, `dist/style.css`, and
  `.openai/hosting.json` in Git. Ignore generated source data, standalone HTML,
  QA screenshots, dependency caches, `cloudflare-dist/`, and `.sites-checkout/`.
  Do not ignore the entire `dist/` directory: it contains authored frontend code.
- From the atlas directory run `vp install --ignore-scripts`,
  `python generate.py`, `python package.py`, and `python prepare_cloudflare.py`.
  Packaging parses complete references, validates every guide checkpoint against
  current source, highlights code, and creates the standalone bundle. Compression
  verifies a lossless round trip for the hosting payload. These are the atlas's
  build/check commands; the React `vp check`/`vp test` workflow below applies to
  the three React apps.
- Python and JavaScript source scanners resolve the repository root three levels
  above the atlas directory. Update both when relocating a Site.
- Use the Sites skill to publish the compressed output through its isolated
  checkout. Preserve the recorded project ID, public access, and custom domain.
  Do not publish this Site through a Vercel release branch or commit credentials.

<!--VITE PLUS START-->

# Using Vite+, the Unified Toolchain for the Web

This project is using Vite+, a unified toolchain built on top of Vite, Rolldown, Vitest, tsdown, Oxlint, Oxfmt, and Vite Task. Vite+ wraps runtime management, package management, and frontend tooling in a single global CLI called `vp`. Vite+ is distinct from Vite, but it invokes Vite through `vp dev` and `vp build`.

## Vite+ Workflow

`vp` is a global binary that handles the full development lifecycle. Run `vp help` to print a list of commands and `vp <command> --help` for information about a specific command.

### Start

- create - Create a new project from a template
- migrate - Migrate an existing project to Vite+
- config - Configure hooks and agent integration
- staged - Run linters on staged files
- install (`i`) - Install dependencies
- env - Manage Node.js versions

### Develop

- dev - Run the development server
- check - Run format, lint, and TypeScript type checks
- lint - Lint code
- fmt - Format code
- test - Run tests

### Execute

- run - Run monorepo tasks
- exec - Execute a command from local `node_modules/.bin`
- dlx - Execute a package binary without installing it as a dependency
- cache - Manage the task cache

### Build

- build - Build for production
- pack - Build libraries
- preview - Preview production build

### Manage Dependencies

Vite+ automatically detects and wraps the underlying package manager such as pnpm, npm, or Yarn through the `packageManager` field in `package.json` or package manager-specific lockfiles.

- add - Add packages to dependencies
- remove (`rm`, `un`, `uninstall`) - Remove packages from dependencies
- update (`up`) - Update packages to latest versions
- dedupe - Deduplicate dependencies
- outdated - Check for outdated packages
- list (`ls`) - List installed packages
- why (`explain`) - Show why a package is installed
- info (`view`, `show`) - View package information from the registry
- link (`ln`) / unlink - Manage local package links
- pm - Forward a command to the package manager

### Maintain

- upgrade - Update `vp` itself to the latest version

These commands map to their corresponding tools. For example, `vp dev --port 3000` runs Vite's dev server and works the same as Vite. `vp test` runs JavaScript tests through the bundled Vitest. The version of all tools can be checked using `vp --version`. This is useful when researching documentation, features, and bugs.

## Common Pitfalls

- **Using the package manager directly:** Do not use pnpm, npm, or Yarn directly. Vite+ can handle all package manager operations.
- **Always use Vite commands to run tools:** Don't attempt to run `vp vitest` or `vp oxlint`. They do not exist. Use `vp test` and `vp lint` instead.
- **Running scripts:** Vite+ commands take precedence over `package.json` scripts. If there is a `test` script defined in `scripts` that conflicts with the built-in `vp test` command, run it using `vp run test`.
- **Do not install Vitest, Oxlint, Oxfmt, or tsdown directly:** Vite+ wraps these tools. They must not be installed directly. You cannot upgrade these tools by installing their latest versions. Always use Vite+ commands.
- **Use Vite+ wrappers for one-off binaries:** Use `vp dlx` instead of package-manager-specific `dlx`/`npx` commands.
- **Import JavaScript modules from `vite-plus`:** Instead of importing from `vite` or `vitest`, all modules should be imported from the project's `vite-plus` dependency. For example, `import { defineConfig } from 'vite-plus';` or `import { expect, test, vi } from 'vite-plus/test';`. You must not install `vitest` to import test utilities.
- **Type-Aware Linting:** There is no need to install `oxlint-tsgolint`, `vp lint --type-aware` works out of the box.

## Review Checklist for Agents

- [ ] Run `vp install` after pulling remote changes and before getting started.
- [ ] Run `vp check` and `vp test` to validate changes.

<!--VITE PLUS END-->
