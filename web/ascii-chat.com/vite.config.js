import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
import sitemap from "vite-plugin-sitemap";
import { execSync } from "child_process";
import path from "path";
import { fileURLToPath } from "url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));

const getCommitSha = () => {
  // Check for Vercel's built-in environment variable first
  const vercelSha =
    typeof process !== "undefined" && process.env?.VERCEL_GIT_COMMIT_SHA;
  if (vercelSha) {
    return vercelSha.substring(0, 8);
  }

  // Fall back to git command for local development
  try {
    return execSync("git rev-parse HEAD").toString().trim().substring(0, 8);
  } catch {
    return "unknown";
  }
};

// https://vite.dev/config/
export default defineConfig({
  define: {
    __COMMIT_SHA__: JSON.stringify(getCommitSha()),
  },
  resolve: {
    alias: {
      "@ascii-chat/shared": path.resolve(__dirname, "../packages/shared/src"),
    },
    extensions: [".ts", ".tsx", ".js", ".jsx"],
    dedupe: ["react", "react-dom"],
  },
  server: {
    proxy: {
      "/api": {
        target: `http://localhost:${process.env.API_PORT || 3001}`,
        changeOrigin: true,
      },
    },
  },
  plugins: [
    react(),
    sitemap({
      hostname: "https://ascii-chat.com",
      dynamicRoutes: [
        "/crypto",
        "/man",
        "/docs",
        "/docs/configuration",
        "/docs/hardware",
        "/docs/terminal",
        "/docs/snapshot",
        "/docs/network",
        "/docs/media",
      ],
    }),
  ],
  build: {
    rollupOptions: {
      output: {
        manualChunks: {
          // Separate docs pages into their own chunks
          "docs-pages": [
            "./src/pages/docs/Configuration.jsx",
            "./src/pages/docs/Hardware.jsx",
            "./src/pages/docs/Terminal.jsx",
            "./src/pages/docs/Snapshot.jsx",
            "./src/pages/docs/Network.jsx",
            "./src/pages/docs/Media.jsx",
            "./src/pages/docs/Crypto.jsx",
            "./src/pages/docs/DocsHub.jsx",
          ],
          // Vendor libraries
          vendor: ["react", "react-dom", "react-router-dom"],
          // Shared components
          "shared-components": ["@ascii-chat/shared/components"],
        },
      },
    },
  },
});
