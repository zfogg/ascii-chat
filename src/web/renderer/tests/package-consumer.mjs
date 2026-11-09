import { execFileSync } from "node:child_process";
import { mkdir, mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import { createRequire } from "node:module";
import { dirname, resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";

const packageDir = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const buildDir = resolve(packageDir, "../../../build-wasm");
await mkdir(buildDir, { recursive: true });
const sample = await mkdtemp(resolve(buildDir, "npm-consumer-"));
const { version } = JSON.parse(
  await readFile(resolve(packageDir, "package.json"), "utf8"),
);
const npm = process.env.npm_execpath;
if (!npm) throw new Error("Run this test with npm run test:package");
const runNpm = (args, cwd) =>
  execFileSync(process.execPath, [npm, ...args], { cwd, stdio: "inherit" });
try {
  runNpm(["pack", "--pack-destination", sample], packageDir);
  await writeFile(
    resolve(sample, "package.json"),
    JSON.stringify({ private: true, type: "module" }),
  );
  runNpm(
    [
      "install",
      "--offline",
      "--ignore-scripts",
      `./ascii-chat-${version}.tgz`,
    ],
    sample,
  );
  await writeFile(
    resolve(sample, "index.ts"),
    `import { AsciiRenderer, createRenderer, ColorMode, type RendererModule } from "ascii-chat";
import { paletteTypes } from "ascii-chat/options";
import type { MainModule } from "ascii-chat/bindings";
const factory: typeof AsciiRenderer.create = createRenderer;
const color: ColorMode = ColorMode.Truecolor;
const compatible = (module: MainModule): RendererModule => module;
void [factory, color, compatible, paletteTypes.standard];
`,
  );
  const require = createRequire(import.meta.url);
  execFileSync(
    process.execPath,
    [
      require.resolve("typescript/bin/tsc"),
      "--noEmit",
      "--target",
      "ES2022",
      "--module",
      "NodeNext",
      "--moduleResolution",
      "NodeNext",
      "index.ts",
    ],
    { cwd: sample, stdio: "inherit" },
  );
  execFileSync(
    process.execPath,
    [
      "--input-type=module",
      "-e",
      `import { ColorMode } from "ascii-chat"; import { paletteTypes } from "ascii-chat/options"; if (ColorMode.Truecolor !== 3 || paletteTypes.standard !== 1) process.exit(1); import.meta.resolve("ascii-chat/renderer.wasm");`,
    ],
    { cwd: sample, stdio: "inherit" },
  );
  console.log(
    "Packed renderer works in an isolated JavaScript and TypeScript project",
  );
} finally {
  if (!resolve(sample).startsWith(buildDir + sep))
    throw new Error("Refusing to clean outside build-wasm");
  await rm(sample, { recursive: true, force: true });
}
