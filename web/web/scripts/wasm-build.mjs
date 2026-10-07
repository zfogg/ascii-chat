import { createHash } from "node:crypto";
import process from "node:process";
import console from "node:console";
import {
  existsSync,
  mkdirSync,
  readFileSync,
  writeFileSync,
  copyFileSync,
  readdirSync,
} from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, resolve, relative } from "node:path";
import { execFileSync } from "node:child_process";

const app = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const repo = resolve(app, "../..");
const output = resolve(app, "src/wasm/dist");
const publicOutput = resolve(app, "public/wasm");
const files = ["mirror.js", "mirror.wasm", "client.js", "client.wasm"];
const hash = (bytes) => createHash("sha256").update(bytes).digest("hex");
function sourceHash() {
  const digest = createHash("sha256");
  const walk = (directory) => {
    for (const entry of readdirSync(directory, { withFileTypes: true }).sort(
      (a, b) => (a.name < b.name ? -1 : a.name > b.name ? 1 : 0),
    )) {
      const path = resolve(directory, entry.name);
      if (entry.isDirectory()) walk(path);
      else if (/\.(c|h|cmake|txt|in)$/.test(entry.name)) {
        digest.update(relative(repo, path).replaceAll("\\", "/"));
        digest.update(readFileSync(path));
      }
    }
  };
  for (const directory of ["src/web", "lib", "include", "cmake"])
    walk(resolve(repo, directory));
  digest.update(readFileSync(resolve(repo, "CMakeLists.txt")));
  return digest.digest("hex");
}

const manifestPath = resolve(output, "manifest.json");
if (process.env.ASCII_CHAT_WASM_USE_PREBUILT === "1") {
  if (!existsSync(manifestPath))
    throw new Error(
      "Prebuilt WASM requires a manifest from a successful WASM build.",
    );
  const manifest = JSON.parse(readFileSync(manifestPath, "utf8"));
  if (
    existsSync(resolve(repo, "src/web")) &&
    manifest.sourceHash !== sourceHash()
  ) {
    throw new Error(
      "Prebuilt WASM is stale. Rebuild mirror-web and client-web.",
    );
  }
  for (const file of files) {
    if (manifest.files[file] !== hash(readFileSync(resolve(output, file))))
      throw new Error(`WASM artifact mismatch: ${file}`);
  }
} else {
  const artifactDirectory = process.env.ASCII_CHAT_WASM_ARTIFACT_DIR;
  const build = resolve(repo, process.env.ASCII_CHAT_WASM_BUILD_DIR || "build");
  if (!artifactDirectory)
    execFileSync(
      "cmake",
      ["--build", build, "--target", "mirror-web", "client-web"],
      { stdio: "inherit" },
    );
  const source = artifactDirectory
    ? resolve(repo, artifactDirectory)
    : existsSync(resolve(build, "client.wasm"))
      ? build
      : resolve(build, "web");
  mkdirSync(output, { recursive: true });
  for (const file of files)
    copyFileSync(resolve(source, file), resolve(output, file));
  writeFileSync(
    manifestPath,
    JSON.stringify(
      {
        sourceHash: sourceHash(),
        files: Object.fromEntries(
          files.map((file) => [
            file,
            hash(readFileSync(resolve(output, file))),
          ]),
        ),
      },
      null,
      2,
    ) + "\n",
  );
}
mkdirSync(publicOutput, { recursive: true });
// JS imports and runtime WASM URLs are published from the same verified build.
for (const file of files)
  copyFileSync(resolve(output, file), resolve(publicOutput, file));
copyFileSync(manifestPath, resolve(publicOutput, "manifest.json"));
console.log("Verified and published mirror/client WASM artifacts.");
