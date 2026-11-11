import type { MainModule } from "../wasm/generated/mirror-bindings";
type FaceModule = Pick<MainModule, "faceReset" | "faceDetect" | "faceTick">;

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
  module!.faceReset();
  return module!;
})();

// Detection and composition both run here; rendering never waits for inference.
setInterval(() => {
  if (!module || performance.now() - lastFrame > 2500) return;
  const now = performance.now() / 1000;
  const values = module.faceTick(now, enabled);
  self.postMessage({
    type: "state",
    tick: now,
    values: Array.from(values),
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
      wasm.faceReset();
    }
    const scale = Math.min(320 / bitmap.width, 240 / bitmap.height);
    const width = Math.max(1, Math.floor(bitmap.width * scale));
    const height = Math.max(1, Math.floor(bitmap.height * scale));
    canvas.width = width;
    canvas.height = height;
    context.drawImage(bitmap, 0, 0, width, height);
    const pixels = context.getImageData(0, 0, width, height).data;
    lastFrame = performance.now();
    if (wasm.faceDetect(pixels, width, height, lastFrame / 1000))
      throw new Error("Invalid face detection frame");
    self.postMessage({ type: "done" });
  } catch (error) {
    self.postMessage({ type: "error", message: String(error) });
  } finally {
    bitmap.close();
  }
};
