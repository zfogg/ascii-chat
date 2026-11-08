import { copyFile, mkdir, readFile, rm, writeFile } from "node:fs/promises";
import { resolve } from "node:path";
const build = resolve(
  process.env.ASCII_CHAT_WASM_ARTIFACT_DIR ?? "../../../build-wasm",
);
const dest = new URL("./dist/src/web/renderer/src/", import.meta.url);
await mkdir(dest, { recursive: true });
for (const file of ["renderer.js", "renderer.wasm", "renderer-bindings.d.ts"]) {
  await copyFile(resolve(build, file), new URL(file, dest));
}
await copyFile(
  new URL("../../../LICENSE.txt", import.meta.url),
  new URL("./dist/LICENSE.txt", import.meta.url),
);

// The workspace source is compiled with the renderer and shipped locally, so
// published consumers do not depend on the private web package.
for (const extension of ["js", "d.ts"]) {
  await copyFile(
    new URL(
      `./dist/web/packages/shared/src/wasm/options.${extension}`,
      import.meta.url,
    ),
    new URL(`options.${extension}`, dest),
  );
  await rm(
    new URL(
      `./dist/web/packages/shared/src/wasm/options.${extension}`,
      import.meta.url,
    ),
  );
  const entry = new URL(`index.${extension}`, dest);
  const text = await readFile(entry, "utf8");
  await writeFile(
    entry,
    text
      .replaceAll("@ascii-chat/shared/wasm/options", "./options.js")
      .replaceAll(
        "@ascii-chat/shared/wasm/generated/mirror-bindings",
        "./renderer-bindings.js",
      ),
  );
}
