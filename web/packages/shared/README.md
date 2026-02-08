# @ascii-chat/shared

Shared components and utilities for ascii-chat web applications.

## Setup

### 1. Add dependency to your app's package.json

```json
{
  "dependencies": {
    "@ascii-chat/shared": "workspace:*"
  }
}
```

### 2. Add Vite alias for module resolution

```js
// vite.config.js/ts
import path from "path";
import { fileURLToPath } from "url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));

export default defineConfig({
  resolve: {
    alias: {
      "@ascii-chat/shared": path.resolve(__dirname, "../packages/shared/src"),
    },
    extensions: [".ts", ".tsx", ".js", ".jsx"],
  },
});
```

### 3. Install dependencies

```bash
cd web
bun install
```

## Usage

### Components

```tsx
import { CommitLink } from "@ascii-chat/shared/components";

// Basic usage
<CommitLink
  commitSha={__COMMIT_SHA__}
  className="text-cyan-400 hover:text-cyan-300 font-mono text-xs"
/>

// With additional props (style, onClick, etc.)
<CommitLink
  commitSha={__COMMIT_SHA__}
  onClick={() => console.log("clicked")}
  style={{ fontSize: "0.875rem" }}
/>
```

### Utilities

**Note:** Don't use utilities in `vite.config` due to ESM resolution issues. Keep `getCommitSha()` local in each config file.

```js
// In vite.config.js - keep this local per-site
const getCommitSha = () => {
  if (process.env.VERCEL_GIT_COMMIT_SHA) {
    return process.env.VERCEL_GIT_COMMIT_SHA.substring(0, 8);
  }
  try {
    return execSync("git rev-parse HEAD").toString().trim().substring(0, 8);
  } catch {
    return "unknown";
  }
};
```

## Adding new shared code

1. Add component to `packages/shared/src/components/YourComponent.tsx`
2. Export from `packages/shared/src/components/index.ts`
3. Import in any app: `import { YourComponent } from "@ascii-chat/shared/components"`

## Current shared components

- **CommitLink** - Link to a git commit with SHA display
