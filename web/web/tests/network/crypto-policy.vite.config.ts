import { defineConfig } from "vite-plus";
import { fileURLToPath } from "node:url";

// Keep dependency discovery and hot reload from navigating live handshake tests.
export default defineConfig({
  resolve: {
    alias: {
      "@ascii-chat/shared": fileURLToPath(
        new URL("../../../packages/shared/src", import.meta.url),
      ),
    },
  },
  optimizeDeps: { entries: ["tests/network/crypto-policy.html"] },
  server: {
    host: "127.0.0.1",
    port: 30831,
    strictPort: true,
    hmr: false,
    headers: {
      "Cross-Origin-Embedder-Policy": "require-corp",
      "Cross-Origin-Opener-Policy": "same-origin",
    },
  },
});
