import { AsciiRenderer } from "ascii-chat";
import type { TerminalRendererModule } from "../../wasm/terminalRenderer";
import type { AsciiRendererBinding } from "../../wasm/generated/mirror-bindings";
import type { RasterWorkerRequest } from "./RasterWorker";
import { browserRasterFontSize } from "./rasterConfig";
import { createWasmModule, type ClientLikeKind } from "../../wasm/assetUrl";

async function initialize(
  clientLikeKind: ClientLikeKind,
): Promise<TerminalRendererModule> {
  const module = await createWasmModule<TerminalRendererModule>(clientLikeKind);
  if (clientLikeKind === "mirror") {
    if (!module.MirrorSessionBinding)
      throw new Error("Missing mirror session binding");
    const mirrorSession = new module.MirrorSessionBinding({});
    if (!mirrorSession.valid()) {
      const code = mirrorSession.error();
      mirrorSession.delete();
      throw new Error(`Cannot initialize raster worker options (${code})`);
    }
  } else {
    if (!module.ClientSessionBinding)
      throw new Error("Missing client session binding");
    const clientSession = new module.ClientSessionBinding({});
    if (!clientSession.valid()) {
      const code = clientSession.error();
      clientSession.delete();
      throw new Error(`Cannot initialize raster worker options (${code})`);
    }
    clientSession.close();
    clientSession.delete();
  }
  return module;
}
let initialized: Promise<TerminalRendererModule> | undefined;
let renderer: AsciiRendererBinding | null = null;
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
        if (module.patchOptions({ matrix_rain: matrix }, -1) !== 0)
          throw new Error("Cannot select raster worker font");
        const replacement = new module.AsciiRendererBinding(
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
      const dimensions = renderer.dimensions();
      const width = dimensions.width;
      const height = dimensions.height;
      const pixels = AsciiRenderer.rasterize(renderer, text, zalgo);
      previousText = text;
      (
        self.postMessage as (message: unknown, transfer: Transferable[]) => void
      )({ width, height, pixels, changed }, [pixels.buffer]);
    })
    .catch((error) => self.postMessage({ error: String(error) }));
};
