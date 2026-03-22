import { defineConfig } from "vite-plus";

export default defineConfig({
  staged: {
    "*": "vp check --fix",
  },
  lint: {
    ignorePatterns: [
      "deps/**",
      "web/web/src/wasm/dist/**",
      ".deps-cache/**",
      "build*/**",
      "node_modules/**",
    ],
    options: { typeAware: true, typeCheck: true },
  },
});
