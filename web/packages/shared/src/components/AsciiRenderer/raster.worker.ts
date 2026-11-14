import { CanvasRenderer, Options } from "ascii-chat";
import type { TerminalRendererModule } from "../../wasm/terminalRenderer";
import type { AsciiRendererBinding } from "../../wasm/generated/mirror-bindings";
import type { RasterWorkerRequest } from "./RasterWorker";
import { browserRasterFontSize } from "./rasterConfig";
import { createWasmModule, type ClientLikeKind } from "../../wasm/assetUrl";

let workerSession: { delete(): void } | undefined;
async function initialize(
  clientLikeKind: ClientLikeKind,
): Promise<TerminalRendererModule> {
  const module = await createWasmModule<TerminalRendererModule>(clientLikeKind);
  const Session =
    clientLikeKind === "mirror"
      ? module.MirrorSessionBinding
      : module.ClientSessionBinding;
  if (!Session)
    throw new Error(`Missing ${clientLikeKind} raster worker initializer`);
  const session = new Session({});
  if (!session.valid()) {
    const code = session.error();
    session.delete();
    throw new Error(`Cannot initialize raster worker (${code})`);
  }
  workerSession?.delete();
  workerSession = session;
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
        new Options(module).patch({ matrix_rain: matrix });
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
      const { width, height } = renderer.dimensions();
      const pixels = CanvasRenderer.rasterize(renderer, text, zalgo);
      previousText = text;
      (
        self.postMessage as (message: unknown, transfer: Transferable[]) => void
      )({ width, height, pixels, changed }, [pixels.buffer]);
    })
    .catch((error) => self.postMessage({ error: String(error) }));
};
