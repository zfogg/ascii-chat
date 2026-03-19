import { defineConfig } from "vite-plus";
import react from "@vitejs/plugin-react";
import sitemap from "vite-plugin-sitemap";
import { execSync } from "child_process";
import path from "path";
import { fileURLToPath } from "url";
import dotenv from "dotenv";

// Load .env file
dotenv.config();

const __dirname = path.dirname(fileURLToPath(import.meta.url));

const getCommitSha = () => {
  // Check deployment platform environment variables in order
  const envVars = [
    process.env.VERCEL_GIT_COMMIT_SHA, // Vercel
    process.env.SOURCE_COMMIT, // Coolify
    process.env.GITHUB_SHA, // GitHub Actions
    process.env.CI_COMMIT_SHA, // GitLab
  ];

  for (const envVar of envVars) {
    if (envVar) {
      return envVar.substring(0, 8);
    }
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
    __SSH_PUBLIC_KEY__: JSON.stringify(process.env.SSH_PUBLIC_KEY || ""),
    __GPG_PUBLIC_KEY__: JSON.stringify(process.env.GPG_PUBLIC_KEY || ""),
  },
  resolve: {
    alias: {
      "@ascii-chat/shared": path.resolve(__dirname, "../packages/shared/src"),
    },
    extensions: [".ts", ".tsx", ".js", ".jsx"],
    dedupe: ["react", "react-dom"],
  },
  server: {
    port: 5174,
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
            return "deps";
          }
        },
      },
    },
  },
});
