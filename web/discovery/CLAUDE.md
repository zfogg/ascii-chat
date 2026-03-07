# discovery.ascii-chat.com Development Guide for Claude

## Project Overview

**discovery.ascii-chat.com** is the official public key distribution website for ACDS (ASCII-Chat Discovery Service). It serves Ed25519 public keys over HTTPS that clients use to establish trusted connections to the ACDS server at `discovery-service.ascii-chat.com:27225`.

The site provides:

- SSH Ed25519 public key distribution (`/key.pub`)
- GPG Ed25519 public key distribution (`/key.gpg`)
- ACDS trust model documentation and usage guides

## Quick Start

```bash
# Install dependencies (uses bun monorepo)
bun install

# Run dev server with hot reload
bun run dev

# Build for production
bun run build

# Preview production build
bun run preview

# Format code
bun run format

# Lint code
bun run lint

# Type checking
bun run type-check
```

## Build System & Tooling

**Package Manager**: Bun (specified in `package.json` as `packageManager: "bun@1.3.9"`)

### Scripts

- **`bun run dev`** - Start Vite dev server (HMR enabled)
- **`bun run build`** - Run build.sh (type-check → format → lint → vite build)
- **`bun run format`** - Format code with prettier (`prettier --write .`)
- **`bun run format:check`** - Check formatting without writing
- **`bun run lint`** - Lint with eslint
- **`bun run type-check`** - Type-check with TypeScript (`tsc --noEmit`)
- **`bun run preview`** - Serve production build locally

### Build Pipeline (build.sh)

The `scripts/build.sh` script runs:

1. **Type checking** - `bun run tsc --noEmit`
2. **Formatting check** - Detect unformatted files, auto-fix with `bun run format`
3. **Linting** - Run eslint, fail on errors
4. **Vite build** - Compile React + generate sitemap
5. **404 handling** - Copy `dist/index.html` to `dist/404.html` for SPA routing

## Deployment

**Platform**: Vercel (GitHub push-to-deploy)

**Configuration**: `vercel.json`

- Build command: `bun run build`
- Output directory: `dist/`
- Install command: `cd .. && bun install` (monorepo root)
- SPA rewrites: All routes → `/index.html`
- Security headers: HSTS, X-Content-Type-Options, X-Frame-Options, CSP-like Permissions-Policy

### Monorepo Integration

Part of `/web` monorepo. Key dependency:

- Uses `@ascii-chat/shared` workspace package (from `packages/@ascii-chat/shared`)

## Technology Stack

**Frontend**:

- React 19.2.4
- React Router 7.13.0
- React Helmet (for SEO/meta tags)
- Vite 7.3.1 (build tool)
- Tailwind CSS 4.1.18 + PostCSS

**Dev Tools**:

- TypeScript 5.9.3
- ESLint 10 + TypeScript plugin
- Prettier 3.8.1
- Vite Sitemap plugin

**Monitoring**:

- Vercel Analytics (`@vercel/analytics`)

## Key Files

```
├── src/
│   ├── App.jsx          # Main component displaying public keys and ACDS docs
│   ├── App.css          # Styling
│   └── main.jsx         # React entry point
├── public/
│   ├── key.pub          # SSH Ed25519 public key (served at /key.pub)
│   ├── key.gpg          # GPG Ed25519 public key (served at /key.gpg)
│   └── favicon files
├── index.html           # HTML entry point (rewrites to SPA)
├── vite.config.js       # Vite + sitemap plugin config
├── tailwind.config.js   # Tailwind CSS config
├── tsconfig.json        # TypeScript config
├── eslint.config.js     # ESLint config (shared from monorepo root)
├── .prettierrc           # Prettier config (shared from monorepo root)
├── package.json         # Dependencies and scripts
└── vercel.json          # Vercel deployment config
```

## Key Management

**Public keys are generated from ACDS server identity** (`acds_identity` file, not in git):

- **export-keys.sh** - Extracts public key and converts to SSH/GPG formats
- **gpg-gen-key.batch** - GPG key generation template

**Served as static files**:

- `/public/key.pub` → Served at `/key.pub` (SSH format, base64-encoded)
- `/public/key.gpg` → Served at `/key.gpg` (GPG armor format)

Clients fetch keys via HTTPS (CA-verified), then use them to verify TCP connections to ACDS server.

## Testing & Development

**Browser Testing**: Use Claude in Chrome with Playwright

- Test key fetching (`/key.pub`, `/key.gpg`)
- Verify documentation display
- Test responsive design and accessibility

**Local Dev**:

```bash
bun run dev           # Watch mode with HMR
# Open http://localhost:5173 (or printed URL)
```

**Pre-commit**:

- Formatting is auto-fixed in build.sh
- Linting must pass before deployment
- Type checking must pass

## Project Links

- **ASCII-Chat** - Main C/C++ project: https://github.com/zfogg/ascii-chat
- **ascii-chat.com** - Homepage/docs: `../ascii-chat.com/`
- **web.ascii-chat.com** - Client app: `../web.ascii-chat.com/`
- **ACDS Server** - Discovery service at `discovery-service.ascii-chat.com:27225`

## Git & Commits

```bash
# Add specific files only (never git add . in web/)
bun run format        # Auto-format before commit
git add file1 file2   # Stage individually
git commit -m "message"

# Co-Author Claude changes with:
# Co-Authored-By: Claude Haiku 4.5 <noreply@anthropic.com>
```

## Development Tips

- **HMR works in dev mode** - Changes hot-reload without restart
- **build.sh auto-formats** - Don't worry about prettier in dev, build catches it
- **Monorepo commands** - Use `bun run --filter './discovery.ascii-chat.com'` from web root to run scripts in isolation
- **404 handling** - SPA rewrites + 404.html copy ensure routing works in Vercel
- **Keys are static files** - No API calls needed, just HTTP GET `/key.pub` and `/key.gpg`
