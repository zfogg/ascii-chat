interface FaceModule {
  HEAPU8: Uint8Array;
  HEAPF32: Float32Array;
  _face_web_input(): number;
  _face_web_reset(): void;
  _face_web_detect(width: number, height: number, now: number): void;
  _face_web_tick(now: number, enabled: number): number;
}

declare const __WASM_ASSET_BASE__: string;

const canvas = new OffscreenCanvas(320, 240);
const context = canvas.getContext("2d", { willReadFrequently: true })!;
let enabled = false;
let module: FaceModule | undefined;
let dimensions = "";
let lastFrame = 0;
const ready = (async () => {
  const base = new URL(__WASM_ASSET_BASE__, self.location.origin).href;
  const factory = await import(/* @vite-ignore */ `${base}mirror.js`);
  module = await factory.default({
    locateFile: (file: string) => base + file,
    pthreadPoolSize: 0,
  });
  module!._face_web_reset();
  return module!;
})();

// Detection and composition both run here; rendering never waits for inference.
setInterval(() => {
  if (!module || performance.now() - lastFrame > 2500) return;
  const now = performance.now() / 1000;
  const pointer = module._face_web_tick(now, Number(enabled));
  self.postMessage({
    type: "state",
    tick: now,
    values: Array.from(module.HEAPF32.subarray(pointer / 4, pointer / 4 + 11)),
  });
}, 16);

self.onmessage = async (
  event: MessageEvent<{
    bitmap: ImageBitmap;
    enabled: boolean;
    reset: boolean;
  }>,
) => {
  const { bitmap, reset } = event.data;
  try {
    const wasm = await ready;
    enabled = event.data.enabled;
    const key = `${bitmap.width}x${bitmap.height}`;
    if (reset || dimensions !== key) {
      dimensions = key;
      wasm._face_web_reset();
    }
    const scale = Math.min(320 / bitmap.width, 240 / bitmap.height);
    const width = Math.max(1, Math.floor(bitmap.width * scale));
    const height = Math.max(1, Math.floor(bitmap.height * scale));
    canvas.width = width;
    canvas.height = height;
    context.drawImage(bitmap, 0, 0, width, height);
    const pixels = context.getImageData(0, 0, width, height).data;
    wasm.HEAPU8.set(pixels, wasm._face_web_input());
    lastFrame = performance.now();
    wasm._face_web_detect(width, height, lastFrame / 1000);
    self.postMessage({ type: "done" });
  } catch (error) {
    self.postMessage({ type: "error", message: String(error) });
  } finally {
    bitmap.close();
  }
};
