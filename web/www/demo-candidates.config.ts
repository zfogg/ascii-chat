import { createReadStream } from "node:fs";
import { fileURLToPath } from "node:url";
import { defineConfig, mergeConfig } from "vite-plus";
import base from "./vite.config.js";

// Serve the same generated module as the widget's factory. Git symlink stubs
// in public/wasm are ordinary text files on some Windows checkouts.
export default mergeConfig(
  base,
  defineConfig({
    plugins: [
      {
        name: "demo-candidate-wasm",
        configureServer(server) {
          server.middlewares.use((request, response, next) => {
            const name = request.url?.split("?")[0];
            if (name !== "/wasm/mirror.wasm" && name !== "/wasm/mirror.js")
              return next();
            response.setHeader("Cross-Origin-Embedder-Policy", "require-corp");
            response.setHeader("Cross-Origin-Opener-Policy", "same-origin");
            response.setHeader(
              "Content-Type",
              name.endsWith(".wasm") ? "application/wasm" : "text/javascript",
            );
            createReadStream(
              fileURLToPath(
                new URL(
                  `../web/src/wasm/dist/${name.split("/").pop()}`,
                  import.meta.url,
                ),
              ),
            )
              .on("error", next)
              .pipe(response);
          });
        },
      },
    ],
  }),
);
