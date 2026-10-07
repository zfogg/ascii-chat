import { defineConfig } from "vite-plus";
import react from "@vitejs/plugin-react";
import path from "path";

export default defineConfig({
  define: {
    // Vite replaces this in the application build. Mirror it here so modules
    // shared with the application can be imported by Vitest.
    __COMMIT_SHA__: JSON.stringify("test"),
  },
  plugins: [react()],
  test: {
    globals: true,
    environment: "jsdom",
    setupFiles: "./tests/setup.ts",
    include: ["tests/**/*.test.ts", "tests/**/*.test.tsx"],
    exclude: ["tests/e2e/**/*"],
  },
  resolve: {
    alias: {
      "@": path.resolve(__dirname, "./src"),
    },
  },
});
