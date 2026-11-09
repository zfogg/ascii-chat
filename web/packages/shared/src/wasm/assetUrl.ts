declare const __WASM_ASSET_BASE__: string | undefined;

export type ClientLikeKind = "mirror" | "client";

export interface WasmCapabilityManifest {
  module: ClientLikeKind;
  protocolVersion: string;
  codecs: string[];
  transports: string[];
  rendererModes: string[];
  features: Record<string, boolean>;
}

/** Keep a page and its workers on the artifact set selected at build time. */
export function getWasmAssetUrl(file: string): string {
  const base =
    typeof __WASM_ASSET_BASE__ === "string" ? __WASM_ASSET_BASE__ : "/wasm/";
  return new URL(`${base}${file}`, globalThis.location.origin).href;
}

/** Load either mode's JavaScript and WASM from the same published artifact set. */
export async function createWasmModule<T>(
  clientLikeKind: ClientLikeKind,
  overrides: Record<string, unknown> = {},
): Promise<T> {
  const url = getWasmAssetUrl(`${clientLikeKind}.js`);
  const { default: factory } = (await import(/* @vite-ignore */ url)) as {
    default: (options: Record<string, unknown>) => Promise<T>;
  };
  return factory({
    getRandomValue: () => crypto.getRandomValues(new Uint32Array(1))[0],
    ...overrides,
    locateFile: getWasmAssetUrl,
  });
}

/** Read the capability declaration produced alongside the verified artifacts. */
export async function getWasmCapabilities(
  kind: ClientLikeKind,
): Promise<WasmCapabilityManifest> {
  const response = await fetch(getWasmAssetUrl("manifest.json"));
  if (!response.ok) throw new Error(`Unable to load ${kind} WASM manifest`);
  const manifest = (await response.json()) as {
    capabilities?: Partial<Record<ClientLikeKind, WasmCapabilityManifest>>;
  };
  const capabilities = manifest.capabilities?.[kind];
  if (!capabilities) throw new Error(`WASM manifest has no ${kind} capabilities`);
  return capabilities;
}
