import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
import sitemap from "vite-plugin-sitemap";

// https://vite.dev/config/
export default defineConfig({
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
});
