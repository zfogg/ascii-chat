# ascii-chat.com

Official website for ascii-chat - Video chat in your terminal

## Tech Stack

- **Bun** - Runtime and package manager
- **React** - UI library
- **Vite** - Build tool
- **Tailwind CSS v4** - Styling with @tailwindcss/postcss
- **React Router** - Client-side routing

## Development

```bash
npm install
npm run dev
```

## Build

```bash
npm run build
```

The man page is automatically converted from the ascii-chat repository:

```bash
mandoc -Thtml ../ascii-chat/build/share/man/man1/ascii-chat.1 > public/ascii-chat-man.html
```

## Deployment

Deploy to Vercel or any static hosting service. The site is built to `dist/`.

## Pages

- **/** - Home page with features, installation, and quick start
- **/crypto** - Cryptography documentation and key authentication
- **/man** - Complete man page reference (auto-generated)

TODO:

- SSR
