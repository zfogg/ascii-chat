import { defineConfig } from "vite-plus";
import react from "@vitejs/plugin-react";
import sitemap from "vite-plugin-sitemap";
import { execFileSync, execSync } from "child_process";
import { copyFileSync, mkdirSync, readFileSync } from "fs";
import { createHash } from "node:crypto";
import path from "path";
import { fileURLToPath } from "url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
execFileSync(
  process.execPath,
  [path.resolve(__dirname, "../web/scripts/wasm-build.mjs"), "--publish-only"],
  { stdio: "inherit" },
);
const wasmDirectory = path.resolve(__dirname, "../web/src/wasm/dist");
const wasmVersion = createHash("sha256")
  .update(readFileSync(path.join(wasmDirectory, "manifest.json")))
  .digest("hex")
  .slice(0, 32);
const wasmFiles = [
  "mirror.js",
  "mirror.wasm",
  "face-follow.js",
  "face-follow.wasm",
];

const getCommitSha = () => {
  // Check deployment platform environment variables in order
  const envVars = [
    process.env?.VERCEL_GIT_COMMIT_SHA, // Vercel
    process.env?.SOURCE_COMMIT, // Coolify
    process.env?.GITHUB_SHA, // GitHub Actions
    process.env?.CI_COMMIT_SHA, // GitLab
  ];

  for (const envVar of envVars) {
    if (envVar) {
      return envVar;
    }
  }

  // Fall back to git command for local development
  try {
    return execSync("git rev-parse HEAD").toString().trim();
  } catch {
    return "unknown";
  }
};

const commitSha = getCommitSha();
console.log("[vite.config.js] Commit SHA:", commitSha);

// https://vite.dev/config/
export default defineConfig({
  define: {
    __COMMIT_SHA__: JSON.stringify(commitSha),
    __WASM_ASSET_BASE__: JSON.stringify(`/wasm/${wasmVersion}/`),
  },
  resolve: {
    alias: {
      "@ascii-chat/shared": path.resolve(__dirname, "../packages/shared/src"),
      "mirror-wasm-factory": path.resolve(
        __dirname,
        "../web/src/wasm/dist/mirror.js",
      ),
    },
    extensions: [".ts", ".tsx", ".js", ".jsx"],
    dedupe: ["react", "react-dom"],
  },
  server: {
    host: "0.0.0.0",
    port: 5173,
    allowedHosts: ["manjaro-twopal"],
    headers: {
      // COEP/COOP needed for WASM threading in dev, but breaks video on production HTTPS
      // Only set in development - production must configure these via reverse proxy if needed
      "Cross-Origin-Embedder-Policy": "require-corp",
      "Cross-Origin-Opener-Policy": "same-origin",
      // Allow other dev servers (web:3000, discovery:5174) to load fonts
      "Access-Control-Allow-Origin": "*",
    },
    // Development: http://0.0.0.0:5173 (accessible from other devices)
    // Production: https://ascii-chat.com
    // API proxy: /api requests route to localhost:3001 (via API_PORT env var)
    proxy: {
      "/api": {
        target: `http://localhost:${process.env.API_PORT || 3001}`,
        changeOrigin: true,
      },
    },
  },
  preview: {
    headers: {
      "Cross-Origin-Embedder-Policy": "require-corp",
      "Cross-Origin-Opener-Policy": "same-origin",
      "Permissions-Policy": "camera=(self), microphone=(self), geolocation=()",
    },
  },
  plugins: [
    react(),
    {
      name: "ensure-outdir",
      configureServer(server) {
        server.middlewares.use((request, response, next) => {
          const pathname = new URL(request.url || "/", "http://localhost")
            .pathname;
          const name = pathname.replace(/^\/wasm\/(?:[a-f0-9]{32}\/)?/, "");
          if (!pathname.startsWith("/wasm/") || !wasmFiles.includes(name))
            return next();
          response.setHeader(
            "Content-Type",
            name.endsWith(".wasm") ? "application/wasm" : "text/javascript",
          );
          response.end(readFileSync(path.join(wasmDirectory, name)));
        });
      },
      closeBundle() {
        for (const destination of ["dist/wasm", `dist/wasm/${wasmVersion}`]) {
          mkdirSync(destination, { recursive: true });
          for (const name of wasmFiles)
            copyFileSync(
              path.join(wasmDirectory, name),
              path.join(destination, name),
            );
        }
      },
    },
    sitemap({
      hostname: "https://ascii-chat.com",
      dynamicRoutes: [
        "/crypto",
        "/man",
        "/docs",
        "/docs/configuration",
        "/docs/hardware",
        "/docs/terminal",
        "/docs/display",
        "/docs/snapshot",
        "/docs/network",
        "/docs/media",
        "/docs/architecture",
      ],
    }),
  ],
  build: {
    chunkSizeWarningLimit: 600,
    rollupOptions: {
      output: {
        manualChunks(id) {
          // React core — tiny, needed immediately
          if (
            id.includes("node_modules/react/") ||
            id.includes("node_modules/react-dom/")
          ) {
            return "vendor-react";
          }
          // React Router — only needed when routing activates
          if (
            id.includes("node_modules/react-router") ||
            id.includes("node_modules/@remix-run")
          ) {
            return "vendor-router";
          }
          // react-helmet-async — small, used on every page
          if (
            id.includes("node_modules/react-helmet-async") ||
            id.includes("node_modules/react-fast-compare") ||
            id.includes("node_modules/invariant")
          ) {
            return "vendor-helmet";
          }
          // Everything else in node_modules
          if (id.includes("node_modules")) {
            return "vendor-misc";
          }
          // Shared package
          if (id.includes("@ascii-chat/shared")) {
            return "shared";
          }
          // Lazy-loaded routes get their own chunks
          if (id.includes("/src/pages/")) {
            const match = id.match(/\/pages\/(.+?)\.jsx?$/);
            if (match) {
              return `page-${match[1].toLowerCase()}`;
            }
          }
        },
      },
    },
  },
});
