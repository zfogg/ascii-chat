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

## Pages

- **/** - Home page with features, installation, and quick start
- **/docs/** - ascii-chat executable documentation pages
- **/man1** - Complete man page reference (auto-generated)
- **/man5** - Files man page
- **/man3** - Searchable API docs

TODO:

- SSR
  test
