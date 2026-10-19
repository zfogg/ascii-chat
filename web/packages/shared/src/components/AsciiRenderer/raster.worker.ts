import type { TerminalRendererModule } from "../../wasm/terminalRenderer";
import type { RasterWorkerRequest } from "./RasterWorker";
import { browserRasterFontSize } from "./rasterConfig";
import { createWasmModule, type ClientLikeKind } from "../../wasm/assetUrl";

async function initialize(
  clientLikeKind: ClientLikeKind,
): Promise<TerminalRendererModule> {
  const module = await createWasmModule<TerminalRendererModule>(clientLikeKind);
  const initializeOptions =
    clientLikeKind === "mirror"
      ? module._mirror_init_with_args
      : module._client_init_with_args;
  if (!initializeOptions)
    throw new Error(`Missing ${clientLikeKind} raster worker initializer`);
  const size = module.lengthBytesUTF8(clientLikeKind) + 1;
  const args = module._malloc(size);
  try {
    module.stringToUTF8(clientLikeKind, args, size);
    if (initializeOptions(args))
      throw new Error("Cannot initialize raster worker options");
  } finally {
    module._free(args);
  }
  return module;
}
let initialized: Promise<TerminalRendererModule> | undefined;
let renderer = 0;
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
        if (renderer) module._term_renderer_destroy(renderer);
        renderer = 0;
        const config = module._malloc(544),
          out = module._malloc(4);
        try {
          if (module._set_matrix_rain?.(matrix ? 1 : 0) !== 0)
            throw new Error("Cannot select raster worker font");
          module.HEAPU8.fill(0, config, config + 544);
          const view = new DataView(module.HEAPU8.buffer, config, 544);
          view.setInt32(0, cols, true);
          view.setInt32(4, rows, true);
          view.setFloat64(8, browserRasterFontSize(matrix), true);
          if (matrix) module.stringToUTF8("Matrix", config + 20, 512);
          view.setInt32(536, module._get_font_default_ptr(0), true);
          view.setInt32(540, module._get_font_default_size(0), true);
          if (module._term_renderer_create(config, out))
            throw new Error("Cannot create raster worker renderer");
          renderer = new DataView(module.HEAPU8.buffer, out, 4).getUint32(
            0,
            true,
          );
          signature = nextSignature;
        } finally {
          module._free(config);
          module._free(out);
        }
      }
      const bytes = new TextEncoder().encode(text);
      const ptr = module._malloc(bytes.length || 1);
      try {
        module.HEAPU8.set(bytes, ptr);
        module._term_renderer_set_zalgo(renderer, zalgo);
        if (module._term_renderer_feed(renderer, ptr, bytes.length))
          throw new Error("Raster worker rejected frame");
      } finally {
        module._free(ptr);
      }
      const width = module._term_renderer_width_px(renderer);
      const height = module._term_renderer_height_px(renderer);
      const pitch = module._term_renderer_pitch(renderer);
      const source = module._term_renderer_pixels(renderer);
      const pixels = new Uint8ClampedArray(width * height * 4);
      for (let row = 0; row < height; row++)
        pixels.set(
          module.HEAPU8.subarray(
            source + row * pitch,
            source + row * pitch + width * 4,
          ),
          row * width * 4,
        );
      previousText = text;
      (
        self.postMessage as (message: unknown, transfer: Transferable[]) => void
      )({ width, height, pixels, changed }, [pixels.buffer]);
    })
    .catch((error) => self.postMessage({ error: String(error) }));
};
