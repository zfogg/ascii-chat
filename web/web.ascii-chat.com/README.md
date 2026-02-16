# web.ascii-chat.com

Web-based clients for ascii-chat, providing browser access to mirror, client, and discovery modes.

## Overview

This is a React + Vite application that runs ascii-chat in the browser via WebAssembly. The core ascii-chat C code is compiled to WASM using Emscripten, enabling real-time video chat with ASCII rendering directly in your browser.

## Tech Stack

- **Bun** - JavaScript runtime and package manager
- **Vite** - Fast frontend build tool
- **React** - UI framework
- **Tailwind CSS** - Utility-first CSS
- **Emscripten** - C to WebAssembly compiler
- **TypeScript** - Type-safe JavaScript

## Development

### Setup

```bash
bun install
```

### Running Locally

```bash
bun run dev
```

This starts the Vite dev server at `http://localhost:5173`.

### Building

```bash
bun run build
```

Builds the application to `dist/` for production.

### WASM Building

The WASM modules (mirror-web and client-web) are built automatically during the main build process:

```bash
bun run wasm:build
```

This invokes Emscripten to compile `src/web/mirror.c` and `src/web/client.c` to WebAssembly modules and places them in `src/wasm/dist/`.

**Prerequisites:** Emscripten must be installed (`brew install emscripten` on macOS).

## Project Structure

```
src/
├── pages/           # Route pages (Mirror, Client, Discovery)
├── components/      # Reusable React components
├── wasm/           # WASM module integration and types
├── hooks/          # Custom React hooks
├── styles/         # Tailwind CSS + custom styles
└── App.tsx         # Main application component

scripts/            # Build and deployment scripts
build.sh           # Builds WASM, runs type checking, linting, and Vite
```

## Deployment

The application is deployed to Vercel and automatically triggers on git pushes to the main branch.

**Pre-commit checks:** TypeScript type checking, Prettier formatting, ESLint, and WASM compilation all run before commits are accepted.

## Shared Dependencies

This project uses the `@ascii-chat/shared` package from `../packages/shared` for common types and utilities.

## Browser Support

Modern browsers with WebAssembly support (Chrome, Firefox, Safari, Edge).

## Troubleshooting

**WASM build fails with "emcc: command not found"**
Install Emscripten: `brew install emscripten`

**Port 5173 already in use**
Vite will automatically use the next available port.

**Type errors after dependency updates**
Run `bun run type-check` to verify TypeScript compatibility.

## License

Same as ascii-chat (see root repository).
