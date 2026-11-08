import type { TerminalRendererModule } from "../../wasm/terminalRenderer";
import type { TerminalRendererBinding } from "../../wasm/generated/mirror-bindings";
import type { RasterWorkerRequest } from "./RasterWorker";
import { browserRasterFontSize } from "./rasterConfig";
import { createWasmModule, type ClientLikeKind } from "../../wasm/assetUrl";

async function initialize(
  clientLikeKind: ClientLikeKind,
): Promise<TerminalRendererModule> {
  const module = await createWasmModule<TerminalRendererModule>(clientLikeKind);
  const initializeOptions =
    clientLikeKind === "mirror" ? module.initMirror : module.initClient;
  if (!initializeOptions)
    throw new Error(`Missing ${clientLikeKind} raster worker initializer`);
  if (initializeOptions(clientLikeKind))
    throw new Error("Cannot initialize raster worker options");
  return module;
}
let initialized: Promise<TerminalRendererModule> | undefined;
let renderer: TerminalRendererBinding | null = null;
let signature = "";
let queue = Promise.resolve();
let previousText: string | null = null;
self.onmessage = (event: MessageEvent<RasterWorkerRequest>) => {
  initialized ??= initialize(event.data.clientLikeKind);
  const ready = initialized;
  queue = queue
    .then(async () => {
      const module = await ready;
      const { text, cols, rows, matrix, zalgo } = event.data;
      const nextSignature = `${cols}:${rows}:${matrix}`;
      const changed = text !== previousText || nextSignature !== signature;
      if (signature !== nextSignature) {
        if (module._set_matrix_rain?.(matrix ? 1 : 0) !== 0)
          throw new Error("Cannot select raster worker font");
        const replacement = new module.TerminalRendererBinding(
          cols,
          rows,
          browserRasterFontSize(matrix),
          matrix,
          0,
        );
        if (!replacement.valid()) {
          const code = replacement.error();
          replacement.delete();
          throw new Error(`Cannot create raster worker renderer (${code})`);
        }
        renderer?.delete();
        renderer = replacement;
        signature = nextSignature;
      }
      if (!renderer) throw new Error("Raster worker renderer is unavailable");
      if (renderer.setZalgo(zalgo))
        throw new Error("Cannot set raster worker effect");
      if (renderer.feed(text)) throw new Error("Raster worker rejected frame");
      const width = renderer.width();
      const height = renderer.height();
      const pixels = new Uint8ClampedArray(width * height * 4);
      if (!renderer.copyPixels(pixels))
        throw new Error("Cannot copy raster worker pixels");
      previousText = text;
      (
        self.postMessage as (message: unknown, transfer: Transferable[]) => void
      )({ width, height, pixels, changed }, [pixels.buffer]);
    })
    .catch((error) => self.postMessage({ error: String(error) }));
};
