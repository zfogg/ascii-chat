declare const __WASM_ASSET_BASE__: string | undefined;

/** Keep a page and its workers on the artifact set selected at build time. */
export function getWasmAssetUrl(file: string): string {
  const base =
    typeof __WASM_ASSET_BASE__ === "string" ? __WASM_ASSET_BASE__ : "/wasm/";
  return new URL(`${base}${file}`, globalThis.location.origin).href;
}
