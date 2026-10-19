import { afterEach, expect, it, vi } from "vite-plus/test";
import { RasterWorker } from "../../../packages/shared/src/components/AsciiRenderer/RasterWorker";
afterEach(() => vi.unstubAllGlobals());
it("renders the newest pending frame and terminates cleanly", () => {
  let worker: any;
  const sent: any[] = [];
  vi.stubGlobal(
    "Worker",
    class {
      onmessage: any;
      onerror: any;
      constructor() {
        worker = this;
      }
      postMessage(message: unknown) {
        sent.push(message);
      }
      terminate = vi.fn();
    },
  );
  const present = vi.fn();
  const raster = new RasterWorker(present, vi.fn());
  for (const text of ["first", "stale", "newest"])
    raster.write({ text, cols: 80, rows: 40, matrix: false, zalgo: 0 });
  expect(sent.map((x) => x.text)).toEqual(["first"]);
  worker.onmessage({
    data: { width: 1, height: 1, pixels: new Uint8ClampedArray(4) },
  });
  expect(present).toHaveBeenCalledOnce();
  expect(sent.map((x) => x.text)).toEqual(["first", "newest"]);
  raster.close();
  expect(worker.terminate).toHaveBeenCalledOnce();
  worker.onmessage({ data: {} });
  expect(present).toHaveBeenCalledOnce();
});
