# ascii-chat.com

Official homepage for ascii-chat - Video chat in your terminal

## Tech Stack

- **Bun** - Runtime and package manager
- **React** - UI library
- **Vite** - Build tool
- **Tailwind CSS v4** - Styling with @tailwindcss/postcss
- **React Router** - Client-side routing

## Development

```bash
bun install
bun run dev
```

## Build

```bash
bun run build
```

The man page is automatically converted from the ascii-chat repository:

```bash
scripts/manpage-build.sh
```

## Deployment

Deploy to Vercel or any static hosting service. The site is built to `dist/`.

### Cloudflare Configuration

If using Cloudflare as a CDN/proxy, configure a **Page Rule** to bypass cache for video files:

- **URL Pattern:** `www.ascii-chat.com/assets/*.mp4`
- **Cache Level:** Bypass

This is required for iOS Safari support. Safari requires proper HTTP 206 Partial Content responses for video streaming, which Cloudflare's cache layer doesn't preserve. Bypassing Cloudflare's cache allows Caddy to handle range requests correctly while browsers still cache the video locally for 1 year.

## Pages

- **/** - Home page with features, installation, and quick start
- **/docs/** - ascii-chat executable documentation pages
- **/man1** - Complete man page reference (auto-generated)
- **/man5** - Files man page
- **/man3** - Searchable API docs

TODO:

- SSR
  test
