import { defineConfig } from "vite-plus";
import react from "@vitejs/plugin-react";
import path from "path";
import sitemap from "vite-plugin-sitemap";
import { execSync } from "child_process";

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

  try {
    return execSync("git rev-parse HEAD").toString().trim().substring(0, 8);
  } catch {
    return "unknown";
  }
};

export default defineConfig({
  define: {
    __COMMIT_SHA__: JSON.stringify(getCommitSha()),
  },
  plugins: [
    react(),
    sitemap({
      hostname: "https://web.ascii-chat.com",
      dynamicRoutes: ["/", "/mirror", "/client", "/discovery"],
      changefreq: "daily",
      priority: 1.0,
      lastmod: new Date(),
    }),
  ],
  resolve: {
    alias: {
      "@": path.resolve(__dirname, "./src"),
      "@ascii-chat/shared": path.resolve(__dirname, "../packages/shared/src"),
    },
    extensions: [".ts", ".tsx", ".js", ".jsx"],
    dedupe: ["react", "react-dom", "react-helmet-async"],
  },
  server: {
    // Development: http://localhost:3000
    // Production: https://web.ascii-chat.com
    // API proxy: /api requests route to localhost:3001 (via API_PORT env var)
    port: 3000,
    headers: {
      "Cross-Origin-Embedder-Policy": "require-corp",
      "Cross-Origin-Opener-Policy": "same-origin",
      "Access-Control-Allow-Origin": "*",
      "Cross-Origin-Resource-Policy": "cross-origin",
    },
    proxy: {
      "/api": {
        target: `http://localhost:${process.env.API_PORT || 3001}`,
        changeOrigin: true,
      },
      "/fonts": {
        target: "http://localhost:5173",
        changeOrigin: true,
      },
    },
  },
  build: {
    target: "esnext",
    minify: "terser",
    chunkSizeWarningLimit: 600,
    rollupOptions: {
      input: {
        main: "./index.html",
        404: "./404.html",
      },
      output: {
        manualChunks(id) {
          // Terminal library
          if (id.includes("xterm")) {
            return "xterm";
          }
          // Dependencies
          if (id.includes("node_modules")) {
            return "deps";
          }
        },
      },
    },
  },
  worker: {
    format: "es",
  },
  optimizeDeps: {
    exclude: ["@ffmpeg/ffmpeg", "@ffmpeg/util"],
  },
});
