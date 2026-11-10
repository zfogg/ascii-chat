import { afterEach, expect, it, vi } from "vite-plus/test";
import { RasterWorker } from "../../../packages/shared/src/components/AsciiRenderer/RasterWorker";
afterEach(() => vi.unstubAllGlobals());
it.each(["mirror", "client"] as const)(
  "%s worker renders the newest pending frame and terminates cleanly",
  (mode) => {
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
    const raster = new RasterWorker(mode, present, vi.fn());
    for (const text of ["first", "second", "stale", "newest"])
      raster.write({ text, cols: 80, rows: 40, matrix: false, zalgo: 0 });
    expect(sent.map((x) => x.text)).toEqual(["first", "second"]);
    expect(sent.every((x) => x.clientLikeKind === mode)).toBe(true);
    worker.onmessage({
      data: { width: 1, height: 1, pixels: new Uint8ClampedArray(4) },
    });
    expect(present).toHaveBeenCalledOnce();
    expect(sent.map((x) => x.text)).toEqual(["first", "second", "newest"]);
    raster.close();
    expect(worker.terminate).toHaveBeenCalledOnce();
    worker.onmessage({ data: {} });
    expect(present).toHaveBeenCalledOnce();
  },
);
