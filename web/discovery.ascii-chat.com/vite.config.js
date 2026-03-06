import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
import sitemap from "vite-plugin-sitemap";
import { execSync } from "child_process";
import path from "path";
import { fileURLToPath } from "url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));

const getCommitSha = () => {
  // Check for Vercel's built-in environment variable first
  if (process.env.VERCEL_GIT_COMMIT_SHA) {
    return process.env.VERCEL_GIT_COMMIT_SHA.substring(0, 8);
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
      hostname: "https://discovery.ascii-chat.com",
    }),
  ],
  build: {
    chunkSizeWarningLimit: 600,
    rollupOptions: {
      output: {
        manualChunks(id) {
          // Dependencies
          if (id.includes("node_modules")) {
            if (id.includes("react")) {
              return "deps-react";
            }
            return "deps";
          }
        },
      },
    },
  },
});
