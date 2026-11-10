/** The headers required to serve the renderer's WebAssembly module. */
export const wasmHeaders = {
  "Content-Type": "application/wasm",
  "Cache-Control": "public, max-age=31536000, immutable",
  "Cross-Origin-Resource-Policy": "cross-origin",
} as const;

export type RendererWasmAsset = {
  body: Uint8Array;
  headers: typeof wasmHeaders & { "Content-Length": string };
};

/** Load the packaged WASM binary and headers for an HTTP response. */
export async function getRendererWasm(): Promise<RendererWasmAsset> {
  const { readFile } = await import("node:fs/promises");
  const body: Uint8Array = await readFile(
    new URL("../renderer.wasm", import.meta.url),
  );
  return {
    body,
    headers: { ...wasmHeaders, "Content-Length": String(body.byteLength) },
  };
}

/** Return the asset as a standard Fetch API response. */
export async function rendererWasmResponse(): Promise<Response> {
  const { body, headers } = await getRendererWasm();
  return new Response(body as BodyInit, { headers });
}

/** Ready-to-register Fetch-style route callback. */
export const rendererWasmHandler = (): Promise<Response> =>
  rendererWasmResponse();

import type { ServerResponse } from "node:http";

/** Write the asset to node:http or Bun's compatible response. */
export async function writeRendererWasm(
  response: ServerResponse,
): Promise<void> {
  const { body, headers } = await getRendererWasm();
  for (const [name, value] of Object.entries(headers))
    response.setHeader(name, value);
  response.end(body);
}
