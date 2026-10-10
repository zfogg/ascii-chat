import { afterEach, expect, it, vi } from "vite-plus/test";
import { cleanup, renderHook } from "@testing-library/react";
import { useAsciiRendererHandle } from "../../../packages/shared/src/components/AsciiRenderer/useAsciiRendererHandle";
import type { AsciiRendererHandle } from "../../../packages/shared/src/components/AsciiRenderer/types";

afterEach(() => {
  cleanup();
  vi.unstubAllGlobals();
});

it("pauses during resize debounce then renders server grids at the local canvas size", () => {
  vi.stubGlobal(
    "ImageData",
    class {
      data: Uint8ClampedArray;
      constructor(
        dataOrWidth: Uint8ClampedArray | number,
        widthOrHeight: number,
        maybeHeight?: number,
      ) {
        void maybeHeight;
        this.data =
          dataOrWidth instanceof Uint8ClampedArray
            ? dataOrWidth
            : new Uint8ClampedArray(dataOrWidth * widthOrHeight * 4);
      }
    },
  );
  const feed = vi.fn(),
    draw = vi.fn(),
    fps = vi.fn();
  const ref = { current: null as AsciiRendererHandle | null };
  const timeout = { current: null as ReturnType<typeof setTimeout> | null };
  const params = {
    ref,
    moduleRef: {
      current: {
        HEAPU8: new Uint8Array(4096),
        _malloc: () => 1,
        _free: vi.fn(),
        _term_renderer_feed: feed,
        _term_renderer_pixels: () => 1,
        _term_renderer_pitch: () => 40,
      },
    },
    setupDoneRef: { current: true },
    rendererPtrRef: { current: 1 },
    canvasRef: {
      current: {
        width: 10,
        height: 10,
        getContext: () => ({ putImageData: draw }),
      },
    },
    resizeTimeoutRef: timeout,
    showFps: true,
    onFpsChange: fps,
    onDimensionsChange: undefined,
  } as unknown as Parameters<typeof useAsciiRendererHandle>[0];
  const { result } = renderHook(() => useAsciiRendererHandle(params));
  const handle = result.current;
  handle.updateDimensions(80, 24);
  expect(ref.current!.writeFrame("old", { cols: 80, rows: 24 })).toBe(true);
  expect(draw).toHaveBeenCalledTimes(1);

  timeout.current = 1 as unknown as ReturnType<typeof setTimeout>;
  expect(ref.current!.writeFrame("during resize", { cols: 80, rows: 24 })).toBe(
    false,
  );
  expect(draw).toHaveBeenCalledTimes(1);
  handle.updateDimensions(60, 20);
  timeout.current = null;
  expect(ref.current!.writeFrame("previous grid", { cols: 80, rows: 24 })).toBe(
    true,
  );
  expect(
    ref.current!.writeFrame("different rows", { cols: 60, rows: 24 }),
  ).toBe(true);
  expect(feed).toHaveBeenCalledTimes(3);
  expect(draw).toHaveBeenCalledTimes(3);
  expect(ref.current!.writeFrame("matching", { cols: 60, rows: 20 })).toBe(
    true,
  );
  expect(draw).toHaveBeenCalledTimes(4);

  handle.updateDimensions(100, 30);
  expect(
    ref.current!.writeFrame("server grid after growing", {
      cols: 60,
      rows: 20,
    }),
  ).toBe(true);
  expect(draw).toHaveBeenCalledTimes(5);
  expect(
    ref.current!.writeFrame("matching again", { cols: 100, rows: 30 }),
  ).toBe(true);
  expect(draw).toHaveBeenCalledTimes(6);
  expect(ref.current!.getDimensions()).toEqual({ cols: 100, rows: 30 });
});
