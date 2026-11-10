import { copyFile, readFile, writeFile } from "node:fs/promises";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { protocolVersion } from "../../../scripts/wasm-protocol-version.mjs";

const repo = fileURLToPath(new URL("../../../", import.meta.url));
const build = resolve(
  repo,
  // renderer-web is emitted beside the other CMake WASM targets. Keep an
  // override for release packaging, but make the repository build work with
  // the normal `cmake --build build/web --target renderer-web` layout.
  process.env.ASCII_CHAT_WASM_ARTIFACT_DIR ?? "build/web",
);
const dest = new URL("./dist/", import.meta.url);
for (const file of ["renderer.js", "renderer.wasm", "renderer-bindings.d.ts"]) {
  await copyFile(resolve(build, file), new URL(file, dest));
}
const rendererSource = await readFile(new URL("renderer.js", dest), "utf8");
for (const binding of ["AsciiRendererBinding", "FaceFollowBinding"]) {
  if (!rendererSource.includes(binding))
    throw new Error(`Renderer artifact does not export ${binding}`);
}
await copyFile(
  new URL("../../../LICENSE.txt", import.meta.url),
  new URL("LICENSE.txt", dest),
);
await writeFile(
  new URL("renderer-manifest.json", dest),
  `${JSON.stringify(
    {
      module: "renderer",
      protocolVersion,
      codecs: ["raw"],
      transports: [],
      rendererModes: ["luminance", "glyph"],
      features: {
        stabilization: true,
        frameStabilization: true,
        faceStabilization: true,
        renderLoop: true,
        pixelCallback: true,
      },
    },
    null,
    2,
  )}\n`,
);
