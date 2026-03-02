# ascii-chat.com Development Guide for Claude

## Repository Information

- **Project**: ascii-chat.com
- **Description**: Official marketing and documentation website for ascii-chat terminal video chat
- **Type**: Monorepo workspace (part of larger ascii-chat web project)
- **Package Manager**: Bun (not npm)

## Quick Start

### Installation & Development

```bash
# Install dependencies (from web/ root)
bun install

# Start development server (HMR enabled, auto-refresh)
bun run dev

# Format code with Prettier
bun run format

# Check formatting
bun run format:check

# Lint with ESLint
bun run lint

# Type check with TypeScript
bun run type-check

# Build for production
bun run build
```

**Important**:
- Use `bun` commands exclusively, not `npm`
- Build script runs type-check, format check, lint, man page generation, and vite build
- All builds deploy to Vercel via GitHub pushes (no manual deployment)

## Project Structure

```
ascii-chat.com/
├── src/
│   ├── components/          # React components (AsciiChatHead, etc.)
│   ├── pages/               # Route pages (home, crypto, man, docs)
│   ├── styles/              # Tailwind CSS and PostCSS
│   └── vite-env.d.ts        # Vite environment types
├── public/                  # Static assets (favicon, man pages)
├── scripts/
│   ├── build.sh             # Pre-build checks and vite build
│   └── manpage-build.sh     # Generates HTML man pages from ascii-chat binary
├── package.json             # Project dependencies and scripts
├── tsconfig.json            # TypeScript configuration (extends tsconfig.base.json)
├── vite.config.js           # Vite bundler configuration with React plugin
├── eslint.config.js         # ESLint rules for React/TypeScript
├── tailwind.config.js       # Tailwind CSS v4 with PostCSS
├── postcss.config.js        # PostCSS plugins (Tailwind, Autoprefixer)
├── vercel.json              # Vercel deployment configuration
└── dist/                    # Build output (generated)
```

## Tech Stack

- **Bun** - Runtime and package manager (faster than npm/yarn)
- **React 19** - UI library with JSX
- **Vite 7** - Lightning-fast build tool with HMR
- **Tailwind CSS v4** - Utility-first CSS with @tailwindcss/postcss
- **React Router 7** - Client-side routing and navigation
- **TypeScript 5.9** - Type-safe JavaScript
- **ESLint 10** - Code quality with eslint-plugin-react-hooks, react-refresh
- **Prettier 3.8** - Code formatting (opinionated)

## Build & Deployment

### Local Build Process

The `bun run build` command executes `scripts/build.sh`:

1. TypeScript type-checking (`bun run tsc --noEmit`)
2. Prettier formatting check (auto-fixes if needed)
3. ESLint linting
4. Man page generation (skipped on Vercel with `$VERCEL` env var)
5. Vite bundling to `dist/`
6. Copies `dist/index.html` to `dist/404.html` for SPA routing

### Vercel Deployment

- **Trigger**: Every push to `master` branch
- **Build Command**: `bun run build` (from vercel.json)
- **Install Command**: `cd .. && bun install` (from web root for monorepo)
- **Output Directory**: `dist/`
- **Domain**: https://ascii-chat.com

### Vercel Configuration (vercel.json)

- **Rewrites**: All `/docs/*` and `/man*` routes rewrite to `/index.html` (SPA routing)
- **Security Headers**:
  - HSTS (strict HTTPS)
  - X-Content-Type-Options: nosniff
  - X-Frame-Options: SAMEORIGIN
  - Permissions-Policy restricts camera/microphone/geolocation

## Code Quality & Testing

### Pre-commit Checks

All code must pass before deployment:

```bash
bun run format      # Auto-fix with Prettier
bun run lint        # Check ESLint rules
bun run type-check  # TypeScript validation
```

### Testing with Claude in Chrome

Use Claude in Chrome browser automation for testing:

```bash
# Manual testing workflow
1. Start dev server: bun run dev
2. Use Claude in Chrome to navigate pages
3. Test interactive features (routing, forms, crypto docs)
4. Verify styling across breakpoints
5. Check console for errors
```

### Playwright Integration (Future)

Planned for E2E testing:
- Page navigation tests
- Form submission flows
- Responsive design verification

## Monorepo Workspace Structure

The `web/` directory is a Bun monorepo with three main sites:

### Sites
- **ascii-chat.com** (this directory) - Marketing & documentation
- **web.ascii-chat.com** - Web client for terminal chat
- **discovery.ascii-chat.com** - Discovery service web UI

### Shared Package
- **packages/@ascii-chat/shared** - Shared TypeScript utilities & types

### Root-level Commands (from web/)

```bash
# Development
bun run dev                    # Start all 3 sites simultaneously

# Building
bun run build                  # Build all workspaces
bun run build:shared           # Build shared package only
bun run build:websites         # Build all 3 sites only

# Code quality (runs on all 3 sites)
bun run format                 # Format all sites
bun run format:check           # Check formatting
bun run lint                   # Lint all sites
bun run type-check             # Type-check all sites
```

## Pages & Routes

- **`/`** - Home page with features, installation, quick start
- **`/crypto`** - Cryptography documentation and key authentication
- **`/man`** - Complete man page reference (auto-generated from ascii-chat binary)
- **`/docs`** - Documentation hub with subpages:
  - `/docs/configuration` - CLI options reference
  - `/docs/hardware` - Hardware requirements
  - `/docs/terminal` - Terminal compatibility
  - `/docs/snapshot` - Snapshot mode
  - `/docs/network` - Network protocols
  - `/docs/media` - Media streaming options

## Environment Variables

- `VERCEL` - Set automatically on Vercel (skips man page build)
- `VERCEL_GIT_COMMIT_SHA` - Git commit hash (embedded in page as `__COMMIT_SHA__`)
- All standard Node/Bun variables supported

## Related Resources

- **Main Repository**: `/home/zfogg/src/github.com/zfogg/ascii-chat/`
- **Web API Docs**: `/docs/crypto.md` in main repo
- **Build System**: CMake (main binary), Vite (web)
- **CI/CD**: GitHub Actions (triggers Vercel deployment on push)

## Troubleshooting

**Build fails with "prettier not found"**: Run `bun install` from web root
**Type errors on startup**: Run `bun run type-check` to see full TypeScript errors
**Vercel deploy fails**: Check `vercel.json` buildCommand references correct output directory
**HMR not working**: Ensure dev server is running and browser can access http://localhost:5173
