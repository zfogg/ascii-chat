import type { MirrorModule } from "../../wasm/mirror";
import type { RasterRequest } from "./RasterWorker";
import { BROWSER_RASTER_FONT_SIZE_PT } from "./rasterConfig";

// The published client module supports workers and exports the same C terminal
// renderer as Mirror. UI composition and keyboard handling stay in SessionUi.
const initialized: Promise<MirrorModule> = (async () => {
  const url = new URL("/wasm/client.js", self.location.href).href;
  const { default: factory } = await import(/* @vite-ignore */ url);
  return factory({
    locateFile: (name: string) =>
      new URL(`/wasm/${name}`, self.location.href).href,
  });
})();
let renderer = 0;
let signature = "";
let queue = Promise.resolve();
self.onmessage = (event: MessageEvent<RasterRequest>) => {
  queue = queue
    .then(async () => {
      const module = await initialized;
      const { text, cols, rows, matrix, zalgo } = event.data;
      const nextSignature = `${cols}:${rows}:${matrix}`;
      if (signature !== nextSignature) {
        if (renderer) module._term_renderer_destroy(renderer);
        renderer = 0;
        const config = module._malloc(544),
          out = module._malloc(4);
        try {
          module.HEAPU8.fill(0, config, config + 544);
          const view = new DataView(module.HEAPU8.buffer, config, 544);
          view.setInt32(0, cols, true);
          view.setInt32(4, rows, true);
          view.setFloat64(8, BROWSER_RASTER_FONT_SIZE_PT, true);
          if (matrix) module.stringToUTF8("Matrix", config + 20, 512);
          view.setInt32(536, module._get_font_default_ptr(), true);
          view.setInt32(540, module._get_font_default_size(), true);
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
      (
        self.postMessage as (message: unknown, transfer: Transferable[]) => void
      )({ width, height, pixels }, [pixels.buffer]);
    })
    .catch((error) => self.postMessage({ error: String(error) }));
};
