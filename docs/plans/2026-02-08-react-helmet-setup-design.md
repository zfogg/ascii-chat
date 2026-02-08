# react-helmet-async Setup for All Three Sites

**Date:** 2026-02-08
**Status:** Implemented

## Overview

Set up react-helmet-async across all three websites (ascii-chat.com, discovery.ascii-chat.com, web.ascii-chat.com) to enable per-route SEO meta tag management while keeping site-level structured data in index.html.

## Architecture

### Package Installation

- Installed `react-helmet-async@2.0.5` in all three sites
- Added as peer dependency in shared package
- Added as dev dependency in shared package (for build resolution)

### Component Hierarchy

```
Shared Package (@ascii-chat/shared)
├── Head.tsx (base component)
│   ├── Accepts all SEO props (title, description, keywords, OG tags, Twitter cards)
│   ├── Renders <Helmet> with all meta tags
│   └── Exports HeadProps interface

Site-Specific Wrappers
├── AsciiChatHead.tsx (ascii-chat.com)
│   └── Defaults: title, description, keywords, url, ogImage
├── ACDSHead.tsx (discovery.ascii-chat.com)
│   └── Defaults: ACDS-specific SEO
└── WebClientHead.tsx (web.ascii-chat.com)
    └── Defaults: web client SEO
```

### Provider Setup

Each site wraps its app root with `<HelmetProvider>`:

- **ascii-chat.com**: `App.jsx` wraps `<BrowserRouter>`
- **discovery.ascii-chat.com**: `main.jsx` wraps routing
- **web.ascii-chat.com**: `main.tsx` wraps `<App />`

### index.html Cleanup

**Kept in index.html:**
- Google Tag Manager / Analytics scripts
- Charset and viewport meta tags
- Favicon links
- DNS prefetch/preconnect hints
- Schema.org JSON-LD structured data (site-level)
- Default `<title>` tag (fallback)

**Moved to React/Helmet:**
- `<title>` tag (dynamic per route)
- Meta description
- Meta keywords
- Meta author
- All Open Graph tags (`og:*`)
- All Twitter card tags (`twitter:*`)

## Usage Examples

### Home Page (Default SEO)

```jsx
import { AsciiChatHead } from "../components/AsciiChatHead";

export default function Home() {
  return (
    <>
      <AsciiChatHead />
      {/* Page content */}
    </>
  );
}
```

### Custom Route SEO

```jsx
<WebClientHead
  title="Mirror Mode - ascii-chat"
  description="Test your webcam with real-time ASCII art rendering."
  url="https://web.ascii-chat.com/mirror"
/>
```

### Custom Meta Tags

```jsx
<AsciiChatHead title="Custom Page">
  <meta name="robots" content="noindex, nofollow" />
  <link rel="canonical" href="https://ascii-chat.com/custom" />
</AsciiChatHead>
```

## Implementation Details

### Files Created

1. `web/packages/shared/src/components/Head.tsx` - Base Head component
2. `web/ascii-chat.com/src/components/AsciiChatHead.tsx` - ascii-chat.com wrapper
3. `web/discovery.ascii-chat.com/src/components/ACDSHead.tsx` - ACDS wrapper
4. `web/web.ascii-chat.com/src/components/WebClientHead.tsx` - Web client wrapper

### Files Modified

1. `web/packages/shared/package.json` - Added peer + dev dependencies
2. `web/packages/shared/src/components/index.ts` - Export Head and HeadProps
3. `web/ascii-chat.com/src/App.jsx` - Added HelmetProvider
4. `web/discovery.ascii-chat.com/src/main.jsx` - Added HelmetProvider
5. `web/web.ascii-chat.com/src/main.tsx` - Added HelmetProvider
6. All three index.html files - Removed dynamic meta tags
7. Home pages - Added Head components
8. `web/web.ascii-chat.com/src/pages/Mirror.tsx` - Added WebClientHead with custom SEO

### Dependencies

```json
{
  "ascii-chat.com": "react-helmet-async@2.0.5",
  "discovery.ascii-chat.com": "react-helmet-async@2.0.5",
  "web.ascii-chat.com": "react-helmet-async@2.0.5",
  "shared": {
    "peerDependencies": "react-helmet-async@^2.0.5",
    "devDependencies": "react-helmet-async@^2.0.5"
  }
}
```

## Benefits

1. **Per-Route SEO**: Each page can customize title, description, and OG tags
2. **DRY Defaults**: Site wrappers reduce repetition across routes
3. **Type Safety**: TypeScript interfaces ensure correct prop usage
4. **Maintainability**: Centralized SEO management in shared package
5. **Flexibility**: Children prop allows route-specific custom tags
6. **Performance**: Static site-level data stays in HTML, dynamic data rendered by React

## Testing

All three sites build successfully:
- ✅ ascii-chat.com
- ✅ discovery.ascii-chat.com
- ✅ web.ascii-chat.com

## Future Enhancements

- Add more route-specific Head components for docs pages
- Consider adding canonical URL management
- Add OpenGraph image generator for dynamic content
- Add structured data generation for specific page types
