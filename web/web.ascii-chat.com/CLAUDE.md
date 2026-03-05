# web.ascii-chat.com Development Guide for Claude

## Repository Information

- **Parent Repository**: ascii-chat (zfogg/ascii-chat)
- **Project**: web.ascii-chat.com - React + Vite web frontend
- **Description**: Browser-based client for ascii-chat with WebAssembly compilation of core C code, enabling video chat and ASCII rendering in the browser
- **Deployment**: Vercel (automatic on git push to main branch)

## Related Web Projects

Part of Bun monorepo in `web/`:

- **ascii-chat.com** - Marketing/documentation website
- **web.ascii-chat.com** (this) - Browser client for terminal chat
- **discovery.ascii-chat.com** - ACDS public key distribution
- **packages/shared** - Shared types and utilities (used by all three)

## Quick Start

### Prerequisites

- **Bun** - JavaScript runtime and package manager (not npm)
- **Emscripten** - C to WebAssembly compiler (for WASM builds)
  - macOS: `brew install emscripten`
  - Arch: `pacman -S emscripten`
  - Ubuntu: `apt-get install emscripten`
- **CMake** (already available if building root ascii-chat)

### Building

```bash
# Install dependencies (from web root, not web.ascii-chat.com root)
cd web
bun install

# Type checking
bun run type-check

# Formatting check
bun run format:check

# Linting
bun run lint

# WASM build (requires Emscripten)
bun run wasm:build

# Full production build (includes type check, format check, lint, WASM)
cd web/web.ascii-chat.com
bun run build
```

### Running Locally

```bash
# Development server with hot reload
cd web/web.ascii-chat.com
bun run dev

# Navigate to http://localhost:3000
# Changes to src/ files trigger live reload
```

### Testing

```bash
# Unit tests (Vitest)
bun run test:unit

# Watch mode for unit tests
bun run test

# E2E tests with Playwright
bun run test:e2e

# E2E tests in UI mode (interactive)
bun run test:e2e:ui

# E2E tests with visible browser (HEADED=true for real media testing)
HEADED=true bun run test:e2e
```

**Browser Automation Testing:**
Use Claude in Chrome and Playwright for testing UI interactions. See [Testing with Claude in Chrome](#testing-with-claude-in-chrome) for details.

## Development Workflow

### Code Quality Tools

All tools use **Bun** instead of npm:

```bash
# Format code (write)
bun run format

# Check formatting without writing
bun run format:check

# Lint with ESLint (0 warnings allowed)
bun run lint

# Type check with TypeScript
bun run type-check

# Run all checks (format, lint, type)
cd web/web.ascii-chat.com
bun run build  # Runs all checks + WASM + vite build
```

**Script Breakdown** (from package.json):

- `dev` - Start Vite dev server on port 3000
- `build` - Full production build via scripts/build.sh
- `preview` - Preview production build locally
- `type-check` - TypeScript type checking
- `format` - Prettier reformat (all files)
- `format:check` - Prettier check without changes
- `lint` - ESLint with max-warnings=0 (no warnings allowed)
- `test` - Vitest in watch mode
- `test:unit` - Vitest single run
- `test:e2e` - Playwright E2E tests
- `test:e2e:ui` - Playwright with interactive UI
- `wasm:build` - Build both mirror-web and client-web WASM modules
- `wasm:build:mirror` - Build only mirror-web
- `wasm:build:client` - Build only client-web
- `wasm:watch` - Watch C files and rebuild WASM on changes
- `wasm:clean` - Remove built WASM modules
- `prebuild` - Ensure CMake is configured before build

### Build Script (scripts/build.sh)

The `bun run build` command executes `scripts/build.sh` which:

1. Runs TypeScript type checking
2. Runs Prettier format check
3. Runs ESLint (0 warnings allowed)
4. Builds WASM modules (skipped if emscripten unavailable)
5. Runs Vite build to dist/

**Important**: All checks run before the build, so any failures will stop the build.

## WASM Module Building

### Background

The core ascii-chat C code (mirror and client modes) is compiled to WebAssembly using Emscripten. This allows:

- Video processing in the browser
- Crypto operations (X25519, XSalsa20-Poly1305)
- Packet protocol handling
- All with minimal JavaScript overhead

### WASM Build Process

```bash
# Build both mirror-web and client-web targets
bun run wasm:build

# Watch C files and rebuild automatically during development
bun run wasm:watch

# Build only one module
bun run wasm:build:mirror
bun run wasm:build:client

# Clean WASM build artifacts
bun run wasm:clean
```

**Build locations:**

- Source: `src/web/mirror.c`, `src/web/client.c`
- Output: `src/wasm/dist/mirror_web.js`, `src/wasm/dist/client_web.js`, `.wasm` files
- CMake config: Root `CMakeLists.txt` with `mirror-web` and `client-web` targets

**If WASM build fails:**

- Check that Emscripten is installed and in PATH: `emcc --version`
- Emscripten not available is non-fatal (build skips with warning)
- Check C compilation errors in output

### WASM Module TypeScript Bindings

Each WASM module has TypeScript wrappers:

- `src/wasm/mirror.ts` - Mirror mode wrapper
- `src/wasm/client.ts` - Client mode wrapper
- `src/wasm/types.ts` - Shared type definitions

These provide type-safe interfaces to the compiled C code.

## Testing with Claude in Chrome

### Using Claude in Chrome for Browser Testing

The project integrates with Claude in Chrome and Playwright for automated browser testing:

**Playwright Configuration:**

- Tests in `tests/e2e/` directory
- Chromium only (camera/microphone permissions granted)
- Base URL: `http://localhost:3000`
- Reporter: HTML (results in `playwright-report/`)
- Screenshots: Only on failure
- Traces: On first retry (CI environment)

**Running Tests:**

```bash
# Start dev server and run E2E tests
bun run test:e2e

# Interactive E2E testing UI
bun run test:e2e:ui

# For tests requiring real media (camera/mic), use HEADED mode
HEADED=true bun run test:e2e
```

**Using Claude in Chrome for Manual Testing:**

1. Start the dev server: `bun run dev`
2. Open Claude in Chrome
3. Use browser automation tools to interact with the web app
4. Test UI flows, WebSocket connections, WASM initialization, etc.

**Playwright Browser Permissions:**

- Camera access granted automatically
- Microphone access granted automatically
- Useful for testing media input in mirror and client modes

### Test Structure

**Unit tests** (`tests/unit/*.test.ts`):

- Vitest + jsdom
- Test individual components and utilities
- Run with `bun run test:unit`

**E2E tests** (`tests/e2e/*.spec.ts`):

- Playwright
- Test full application flows
- Run with `bun run test:e2e`

**Test setup:**

- `tests/setup.ts` - Vitest setup for unit tests
- `playwright.config.ts` - Playwright configuration

## Project Structure

```
web.ascii-chat.com/
├── src/                        # Frontend application source
│   ├── pages/                  # Route pages (Mirror, Client, Discovery)
│   ├── components/             # Reusable React components (UI, input, etc)
│   ├── hooks/                  # Custom React hooks for state/side effects
│   ├── wasm/                   # WASM module wrappers and type bindings
│   ├── network/                # Network layer (WebSocket, packet handling)
│   ├── styles/                 # Tailwind CSS and custom styling
│   ├── utils/                  # Utility functions
│   └── App.tsx                 # Main React application component
├── public/                     # Static assets
│   └── wasm/                   # WASM modules and supporting files
├── tests/                      # Test suites
│   ├── unit/                   # Vitest unit tests
│   ├── e2e/                    # Playwright E2E tests
│   ├── setup.ts                # Vitest setup
│   └── README.md               # Test documentation
├── scripts/                    # Build scripts
│   └── build.sh                # Production build script (type check, lint, WASM, vite)
├── dist/                       # Production build output (generated)
├── node_modules/               # Bun dependencies
├── playwright-report/          # Playwright test results (generated)
├── .playwright/                # Playwright browser binaries (generated)
├── vite.config.ts              # Vite configuration (React, Tailwind, sitemap)
├── vitest.config.ts            # Vitest configuration (unit tests)
├── playwright.config.ts        # Playwright configuration (E2E tests)
├── tsconfig.json               # TypeScript configuration
├── package.json                # Bun dependencies and scripts
├── bun.lock                    # Bun lockfile (like package-lock.json)
├── index.html                  # Main HTML entry point
├── 404.html                    # 404 fallback page
├── README.md                   # Project documentation
└── vercel.json                 # Vercel deployment configuration
```

## Configuration Files

### Vite (vite.config.ts)

- **Dev server**: Port 3000
- **Build target**: esnext
- **Minify**: terser
- **Plugins**:
  - React Fast Refresh
  - Sitemap generation (for SEO)
- **CORS headers** (COEP/COOP for SharedArrayBuffer/WebWorker support)
- **Module chunking**: Manual chunk for xterm library
- **Path aliases**: `@` → `./src`, `@ascii-chat/shared` → shared package

### Playwright (playwright.config.ts)

- **Test directory**: `tests/e2e`
- **Base URL**: `http://localhost:3000`
- **Browsers**: Chromium only
- **Workers**: 1 in CI, default in local
- **Retries**: 2 in CI, 0 locally
- **Permissions**: Camera and microphone auto-granted
- **Dev server**: Auto-start with `npm run dev` (note: should be `bun run dev`)
- **Report**: HTML report in `playwright-report/`

### TypeScript (tsconfig.json)

- **Extends**: `../tsconfig.base.json` (shared config from web root)
- **Paths**:
  - `@/*` → `./src/*`
  - `@wasm/*` → `./public/wasm/*`
- **Target**: Based on base config

### Prettier

- **Config**: Uses workspace root `.prettierrc`
- **Ignore file**: `.prettierignore` (ignores WASM dist)
- **Command**: `bun run format` or `bun run format:check`

### ESLint (eslint.config.js)

- **Location**: `../eslint.config.js` (web root)
- **Config**: Flat config with React, React Hooks, TypeScript plugins
- **Rules**:
  - No var (enforce const/let)
  - React hooks rules enforced
  - `@typescript-eslint/no-explicit-any`: error
  - React Refresh warning for non-component exports
  - Unused variables must start with `_` if intentionally unused
- **Special handling**: `src/OpusEncoder.ts` allows unused vars (generated code)

## Deployment

### Vercel Configuration (vercel.json)

```json
{
  "buildCommand": "bun run build",
  "outputDirectory": "dist",
  "installCommand": "cd .. && bun install",
  "rewrites": [
    { "source": "/", "destination": "/index.html" },
    { "source": "/mirror", "destination": "/index.html" },
    { "source": "/client", "destination": "/index.html" },
    { "source": "/discovery", "destination": "/index.html" }
  ],
  "headers": [
    {
      "source": "/(.*)",
      "headers": [
        { "key": "Cross-Origin-Embedder-Policy", "value": "require-corp" },
        { "key": "Cross-Origin-Opener-Policy", "value": "same-origin" }
      ]
    }
  ]
}
```

**Key points:**

- Runs `bun run build` from web.ascii-chat.com directory
- Outputs to `dist/`
- Installs dependencies from web root (parent directory)
- URL rewrites for SPA routing (all routes → index.html)
- CORS headers enable SharedArrayBuffer (required for WASM threads)

### Automatic Deployment

- Triggered on push to `main` branch (or configured branch)
- Deployment URL: https://web.ascii-chat.com
- Preview deployments for pull requests
- See Vercel dashboard for deployment history

## Shared Dependencies

The project uses `@ascii-chat/shared` from `../packages/shared`:

- Common types and interfaces
- Shared utilities
- Installed via Bun workspace (`workspace:*` in package.json)

## Stack Overview

- **Bun** - Package manager and JavaScript runtime (DO NOT USE NPM)
- **Vite** - Module bundler and dev server (ES modules, fast reload)
- **React 19** - UI framework with new compiler
- **Tailwind CSS** - Utility-first CSS styling
- **TypeScript** - Type-safe JavaScript
- **Emscripten** - C to WebAssembly compiler
- **Playwright** - E2E testing framework
- **Vitest** - Unit testing framework
- **xterm.js** - Terminal emulation for ASCII output
- **hls.js** - HLS stream playback support

## Environment Variables

All environment variables map to Vercel or build-time configuration:

- **`VERCEL_GIT_COMMIT_SHA`** - Git commit hash (set by Vercel, embedded in app)
- **`HEADED`** - Set to `true` for Playwright to show browser during tests
  - Example: `HEADED=true bun run test:e2e`

## Pre-commit Hooks

If using git hooks from `../../git-hooks/pre-commit`:

Hooks run before each commit:

- TypeScript type checking
- Prettier formatting check
- ESLint linting
- WASM compilation

To bypass (not recommended): `git commit --no-verify`

## Shared TypeScript Configuration

The project extends `../tsconfig.base.json` from the web root. This provides:

- Shared compiler options across all web projects
- Path aliases (may include others like `@discovery`, `@ascii-chat`, etc)
- Base library configuration

Check web root for full configuration details.

## Git Workflow

```bash
# Never use 'git add -A' or 'git add .'
git add file1.ts file2.tsx

# Commit with individual file additions
git add file1.ts file2.tsx && git commit -m "message"

# Verify changes before push
git diff --staged
git log --oneline -3
```

**Note**: Pre-commit hooks will run type check, format check, lint, and WASM build. Fix any failures before committing.

## Resources

- **README.md** - Project overview and quick reference
- **IMPLEMENTATION_STATUS.md** - WASM module implementation progress and features
- **tests/README.md** - Detailed testing documentation
- **Vercel Dashboard** - Deployment status and logs
- **Playwright Docs** - https://playwright.dev/
- **Vite Docs** - https://vitejs.dev/
- **React Docs** - https://react.dev/

## Development Tips

### Fast Iteration

```bash
# Terminal 1: Watch WASM changes and rebuild automatically
bun run wasm:watch

# Terminal 2: Run dev server with auto-reload
bun run dev

# Terminal 3: Watch tests (optional)
bun run test
```

Changes to C files trigger WASM rebuild, which triggers Vite rebuild, which hot-reloads the browser.

### Testing WASM Changes

```bash
# Full rebuild and test
bun run build

# If build passes, run tests
bun run test:e2e
```

### Debugging Browser Issues

Use Claude in Chrome browser automation tools:

1. Open the running dev server in Chrome
2. Use browser automation to interact with the UI
3. Check browser console for errors: `bun run dev` and look at the output
4. Use Playwright trace viewer: Look in `playwright-report/` after test failures

## License

Same as ascii-chat (see root repository).
