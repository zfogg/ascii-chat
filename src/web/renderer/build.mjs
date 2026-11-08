import { copyFile, mkdir } from "node:fs/promises";
import { resolve } from "node:path";
const build = resolve(
  process.env.ASCII_CHAT_WASM_ARTIFACT_DIR ?? "../../../build-wasm",
);
const dest = new URL("./dist/renderer/src/", import.meta.url);
await mkdir(dest, { recursive: true });
for (const file of ["renderer.js", "renderer.wasm"]) {
  await copyFile(resolve(build, file), new URL(file, dest));
}
await copyFile(
  new URL("../../../LICENSE.txt", import.meta.url),
  new URL("./dist/LICENSE.txt", import.meta.url),
);
