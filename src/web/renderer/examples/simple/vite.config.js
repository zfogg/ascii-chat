import { defineConfig } from "vite";
import fs from "node:fs";
import path from "node:path";

export default defineConfig({
  plugins: [
    {
      name: "serve-ascii-chat-assets",
      configureServer(server) {
        server.middlewares.use(
          "/ascii-chat-assets",
          (request, response, next) => {
            const asset = path.basename(
              new URL(request.url ?? "/", "http://localhost").pathname,
            );
            const file = path.resolve("node_modules/ascii-chat/dist", asset);
            if (!fs.existsSync(file)) return next();
            response.setHeader(
              "Content-Type",
              asset.endsWith(".wasm") ? "application/wasm" : "text/javascript",
            );
            fs.createReadStream(file).pipe(response);
          },
        );
      },
    },
  ],
  server: {
    headers: {
      "Cross-Origin-Opener-Policy": "same-origin",
      "Cross-Origin-Embedder-Policy": "require-corp",
    },
  },
});
