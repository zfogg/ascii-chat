import { cleanup, renderHook } from "@testing-library/react";
import { afterEach, expect, test, vi } from "vite-plus/test";
import { useMirrorRenderLoop } from "../../../packages/shared/src/hooks/useMirrorRenderLoop";

const convert = vi.hoisted(() => vi.fn());
vi.mock("../../../packages/shared/src/wasm/mirror", () => ({
  isWasmReady: () => true,
  convertFrameToAscii: convert,
}));
vi.mock("../../../packages/shared/src/hooks/useTestPattern", () => ({
  useTestPattern: () => ({ enabled: false }),
}));
afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
  vi.unstubAllGlobals();
  convert.mockReset();
});

for (const fault of ["slow", "throw"] as const) {
  test(`Mirror continues rendering after one ${fault} conversion`, () => {
    let now = 0;
    let nextFrame: FrameRequestCallback | undefined;
    vi.spyOn(performance, "now").mockImplementation(() => now);
    vi.stubGlobal("requestAnimationFrame", (callback: FrameRequestCallback) => {
      nextFrame = callback;
      return 1;
    });
    vi.stubGlobal("cancelAnimationFrame", vi.fn());
    vi.spyOn(console, "warn").mockImplementation(() => {});
    convert
      .mockImplementationOnce(() => {
        if (fault === "throw") throw new Error("transient conversion failure");
        now += 150;
        return "slow frame";
      })
      .mockReturnValue("recovered frame");
    const writeFrame = vi.fn(() => true);
    renderHook(() =>
      useMirrorRenderLoop({
        isWebcamRunning: true,
        terminalDimensions: { cols: 80, rows: 24 },
        captureFrame: () => ({ data: new Uint8Array(4), width: 1, height: 1 }),
        canvasRef: { current: null },
        rendererRef: {
          current: {
            writeFrame,
            getDimensions: () => ({ cols: 80, rows: 24 }),
            clear: vi.fn(),
            recreateRenderer: vi.fn(),
          },
        },
        debugCountRef: { current: 0 },
        firstFrameTimeRef: { current: null },
        frameIntervalRef: { current: 1000 / 60 },
        streamRef: { current: null },
      }),
    );
    for (let frame = 0; frame < 5; frame++) {
      const callback = nextFrame;
      nextFrame = undefined;
      now += 17;
      callback?.(now);
    }
    expect(convert.mock.calls.length).toBeGreaterThan(1);
    expect(writeFrame).toHaveBeenCalledWith("recovered frame");
    expect(nextFrame).toBeDefined();
  });
}
