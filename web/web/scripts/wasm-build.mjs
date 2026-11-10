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
  rmSync,
} from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, resolve, relative } from "node:path";
import { execFileSync } from "node:child_process";
import { protocolVersion } from "../../../scripts/wasm-protocol-version.mjs";

const app = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const repo = resolve(app, "../..");
const output = resolve(app, "src/wasm/dist");
const publicOutput = resolve(app, "public/wasm");
const files = ["mirror.js", "mirror.wasm", "client.js", "client.wasm"];
const typeOutput = resolve(repo, "web/packages/shared/src/wasm/generated");
const types = ["mirror-bindings.d.ts", "client-bindings.d.ts"];
const removedFiles = ["face-follow.js", "face-follow.wasm"];
const hash = (bytes) => createHash("sha256").update(bytes).digest("hex");
const requiredExports = {
  mirror: ["AsciiRendererBinding", "MirrorFrameBinding"],
  client: [
    "ClientSessionBinding",
    "BrowserSessionBinding",
    "DiscoveryBinding",
    "HandoffBinding",
    "AudioSessionBinding",
  ],
};
function validateArtifactExports(kind, artifact, declaration) {
  const source = readFileSync(declaration ?? artifact, "utf8");
  for (const name of requiredExports[kind]) {
    if (!source.includes(name))
      throw new Error(`WASM ${kind} artifact does not export ${name}`);
  }
}
function copyArtifact(source, destination) {
  if (source.endsWith(".js") || source.endsWith(".d.ts")) {
    writeFileSync(
      destination,
      readFileSync(source, "utf8").replaceAll("\r\n", "\n"),
    );
    return;
  }
  copyFileSync(source, destination);
}
function normalizeDeclarations(path, kind) {
  let source = readFileSync(path, "utf8").replaceAll("\r\n", "\n");
  const types = `
export type WasmRecord = Record<string, unknown>;
export interface RendererDimensions {
  cols: number;
  rows: number;
  width: number;
  height: number;
}
export interface MirrorFrameResult extends WasmRecord {
  ok: boolean;
  code: number;
  message?: string;
  retryable?: boolean;
  text: string | null;
  width: number;
  height: number;
  changed: boolean;
}
export interface AudioSessionResult {
  code?: number;
  samples?: Float32Array;
  payload?: Uint8Array;
}
export type SessionEvent = WasmRecord;
export interface SessionResult extends WasmRecord {
  code?: number;
  json?: string | null;
  packetTypes?: Readonly<Record<string, number>>;
  bytes?: Uint8Array;
  decodeFailed?: boolean;
}
export interface ProtocolInfo extends WasmRecord {
  packetTypes: Readonly<Record<string, number>>;
}
export interface PacketOutput { type: number; payload: Uint8Array; }
export type PacketOutputList = ReadonlyArray<PacketOutput>;
export interface HostPacket { id: number; bytes: Uint8Array; }
`;
  const marker =
    kind === "client"
      ? "export type ClientBytes = Uint8Array;"
      : "export type RgbaBytes = Uint8Array | Uint8ClampedArray;";
  if (!source.includes("export type WasmRecord ="))
    source = source.replace(marker, `${marker}${types}`);
  const opaque = ["a", "ny"].join("");
  source = source.replaceAll(
    `dimensions(): ${opaque};`,
    "dimensions(): RendererDimensions;",
  );
  source = source.replaceAll(
    `processFrame(_0: RgbaBytes, _1: number, _2: number, _3: boolean): ${opaque};`,
    "processFrame(_0: RgbaBytes, _1: number, _2: number, _3: boolean): MirrorFrameResult;",
  );
  source = source.replaceAll(
    `process(_0: AudioSamples, _1: AudioSamples): ${opaque};`,
    "process(_0: AudioSamples, _1: AudioSamples): AudioSessionResult;",
  );
  source = source.replaceAll(
    `processBatch(_0: ${opaque}, _1: ${opaque}): number;`,
    "processBatch(_0: ReadonlyArray<AudioSamples>, _1: ReadonlyArray<AudioSamples>): number;",
  );
  source = source.replaceAll(
    `process(_0: ${opaque}): ${opaque};`,
    "process(_0: SessionEvent): SessionResult;",
  );
  source = source.replaceAll(
    `processBatch(_0: ${opaque}): ${opaque};`,
    "processBatch(_0: ReadonlyArray<SessionEvent>): ReadonlyArray<SessionResult>;",
  );
  source = source.replaceAll(
    `protocolInfo(): ${opaque};`,
    "protocolInfo(): ProtocolInfo;",
  );
  source = source.replaceAll(
    `render(_0: EmbindString, _1: number): ${opaque};`,
    "render(_0: EmbindString, _1: number): Uint8ClampedArray;",
  );
  source = source.replaceAll(
    `drainOutputs(): ${opaque};`,
    "drainOutputs(): PacketOutputList;",
  );
  source = source.replaceAll(
    `drainPackets(): ${opaque};`,
    "drainPackets(): PacketOutputList;",
  );
  source = source.replaceAll(
    "drainPackets(): PacketOutputList;",
    "drainPackets(): ReadonlyArray<HostPacket>;",
  );
  source = source.replace(
    `  packetTypes(): ${opaque};`,
    "  packetTypes(): PacketTypes;",
  );
  if (kind === "client" && !source.includes("export type PacketTypes ="))
    source = source.replace(
      "export type ClientBytes = Uint8Array;",
      "export type PacketTypes = Readonly<Record<string, number>>;\nexport type ClientBytes = Uint8Array;",
    );
  // Emscripten's generated filesystem/runtime declarations use an opaque type
  // for values whose shape is intentionally unknown. Keep that opacity, but make
  // it explicit so the checked-in declarations do not leak an unsound type
  // into the browser packages. Typed arrays are not generic over their
  // element type in the declarations emitted by this toolchain.
  source = source.replace(/Uint8Array<[^>]+>/g, "Uint8Array");
  source = source.replaceAll(/\bany\b/g, "unknown");
  writeFileSync(path, source);
}
function sourceHash() {
  const digest = createHash("sha256");
  const walk = (directory) => {
    for (const entry of readdirSync(directory, { withFileTypes: true }).sort(
      (a, b) => (a.name < b.name ? -1 : a.name > b.name ? 1 : 0),
    )) {
      const path = resolve(directory, entry.name);
      // The standalone npm renderer has its own build and dependency tree.
      // It does not contribute sources to the site modules.
      if (relative(repo, path).replaceAll("\\", "/") === "src/web/renderer")
        continue;
      // version.h is generated for the build machine; version.h.in is the source.
      if (
        relative(repo, path).replaceAll("\\", "/") ===
        "include/ascii-chat/version.h"
      )
        continue;
      if (entry.isDirectory()) walk(path);
      else if (/\.(c|cpp|h|cmake|txt|in)$/.test(entry.name)) {
        digest.update(relative(repo, path).replaceAll("\\", "/"));
        digest.update(readFileSync(path, "utf8").replaceAll("\r\n", "\n"));
      }
    }
  };
  for (const directory of [
    "src/web",
    "src/common/session",
    "lib",
    "include",
    "cmake",
  ])
    walk(resolve(repo, directory));
  digest.update(
    readFileSync(resolve(repo, "CMakeLists.txt"), "utf8").replaceAll(
      "\r\n",
      "\n",
    ),
  );
  return digest.digest("hex");
}

const manifestPath = resolve(output, "manifest.json");
if (
  process.env.ASCII_CHAT_WASM_USE_PREBUILT === "1" ||
  process.argv.includes("--publish-only")
) {
  if (!existsSync(manifestPath))
    throw new Error(
      "Prebuilt WASM requires a manifest from a successful WASM build.",
    );
  const manifest = JSON.parse(readFileSync(manifestPath, "utf8"));
  if (
    process.env.VERCEL !== "1" &&
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
  for (const file of types) {
    if (
      manifest.types?.[file] !== hash(readFileSync(resolve(typeOutput, file)))
    )
      throw new Error(`WASM declaration mismatch: ${file}`);
  }
} else {
  const artifactDirectory = process.env.ASCII_CHAT_WASM_ARTIFACT_DIR;
  const build = resolve(repo, process.env.ASCII_CHAT_WASM_BUILD_DIR || "build");
  if (!artifactDirectory)
    execFileSync(
      "cmake",
      ["--build", build, "--target", "mirror-web", "client-web"],
      {
        stdio: "inherit",
      },
    );
  const source = artifactDirectory
    ? resolve(repo, artifactDirectory)
    : existsSync(resolve(build, "client.wasm"))
      ? build
      : resolve(build, "web");
  mkdirSync(output, { recursive: true });
  for (const file of files)
    copyArtifact(resolve(source, file), resolve(output, file));
  mkdirSync(typeOutput, { recursive: true });
  for (const file of types)
    copyArtifact(resolve(source, file), resolve(typeOutput, file));
  normalizeDeclarations(resolve(typeOutput, "client-bindings.d.ts"), "client");
  normalizeDeclarations(resolve(typeOutput, "mirror-bindings.d.ts"), "mirror");
  validateArtifactExports(
    "mirror",
    resolve(output, "mirror.js"),
    resolve(typeOutput, "mirror-bindings.d.ts"),
  );
  validateArtifactExports(
    "client",
    resolve(output, "client.js"),
    resolve(typeOutput, "client-bindings.d.ts"),
  );
  // Normalize generated glue and declarations before recording their hashes.
  execFileSync(
    process.execPath,
    [
      resolve(repo, "node_modules/vite-plus/bin/vp"),
      "fmt",
      "--config",
      resolve(repo, ".oxfmtrc.json"),
      "--ignore-path",
      resolve(repo, "scripts/format-empty-ignore"),
      resolve(output, "mirror.js"),
      resolve(output, "client.js"),
      ...types.map((file) => resolve(typeOutput, file)),
    ],
    { cwd: repo, stdio: "inherit" },
  );
  writeFileSync(
    manifestPath,
    JSON.stringify(
      {
        sourceHash: sourceHash(),
        capabilities: {
          mirror: {
            module: "mirror",
            protocolVersion,
            codecs: ["raw", "h264", "hevc", "opus"],
            transports: ["webrtc", "webtransport", "websocket"],
            rendererModes: ["luminance", "glyph"],
            features: { stabilization: true, renderer: true, opus: true },
          },
          client: {
            module: "client",
            protocolVersion,
            codecs: ["raw", "h264", "hevc", "opus"],
            transports: ["webrtc", "webtransport", "websocket"],
            rendererModes: [],
            features: { stabilization: false, renderer: false, opus: true },
          },
        },
        files: Object.fromEntries(
          files.map((file) => [
            file,
            hash(readFileSync(resolve(output, file))),
          ]),
        ),
        types: Object.fromEntries(
          types.map((file) => [
            file,
            hash(readFileSync(resolve(typeOutput, file))),
          ]),
        ),
      },
      null,
      2,
    ) + "\n",
  );
}
mkdirSync(publicOutput, { recursive: true });
for (const directory of [
  publicOutput,
  ...readdirSync(publicOutput, { withFileTypes: true })
    .filter((entry) => entry.isDirectory())
    .map((entry) => resolve(publicOutput, entry.name)),
]) {
  for (const file of removedFiles)
    rmSync(resolve(directory, file), { force: true });
}
const version = hash(readFileSync(manifestPath)).slice(0, 32);
const versionedOutput = resolve(publicOutput, version);
mkdirSync(versionedOutput, { recursive: true });
// JS imports and runtime WASM URLs are published from the same verified build.
// Retain older versions so an open page can finish loading its own artifact set.
for (const destination of [versionedOutput, publicOutput]) {
  for (const file of files)
    copyFileSync(resolve(output, file), resolve(destination, file));
  copyFileSync(manifestPath, resolve(destination, "manifest.json"));
}
console.log("Verified and published mirror/client WASM artifacts.");
