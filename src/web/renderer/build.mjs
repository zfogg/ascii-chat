import { copyFile } from "node:fs/promises";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";

const repo = fileURLToPath(new URL("../../../", import.meta.url));
const build = resolve(
  repo,
  process.env.ASCII_CHAT_WASM_ARTIFACT_DIR ?? "build-wasm",
);
const dest = new URL("./dist/", import.meta.url);
for (const file of ["renderer.js", "renderer.wasm", "renderer-bindings.d.ts"]) {
  await copyFile(resolve(build, file), new URL(file, dest));
}
await copyFile(
  new URL("../../../LICENSE.txt", import.meta.url),
  new URL("LICENSE.txt", dest),
);
