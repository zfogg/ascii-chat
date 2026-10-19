import { renderHook, cleanup } from "@testing-library/react";
import { afterEach, expect, test, vi } from "vite-plus/test";
import { useMirrorRenderLoop } from "../../../packages/shared/src/hooks/useMirrorRenderLoop";
import {
  convertFrameToAscii,
  renderAudioVisualizationFrame,
} from "../../../packages/shared/src/wasm/mirror";
vi.mock("../../../packages/shared/src/wasm/mirror", () => ({
  isWasmReady: () => true,
  convertFrameToAscii: vi.fn(() => "rain"),
  renderAudioVisualizationFrame: vi.fn(() => "silence"),
  renderAnalyserAudioVisualization: vi.fn(),
}));
vi.mock("../../../packages/shared/src/hooks/useTestPattern", () => ({
  useTestPattern: () => ({ enabled: false }),
}));
afterEach(() => {
  cleanup();
  vi.unstubAllGlobals();
  vi.clearAllMocks();
});
for (const animation of ["matrix", "waveform", "fft"] as const) {
  test(`${animation} paints without waiting for camera or microphone`, () => {
    let tick!: FrameRequestCallback;
    vi.stubGlobal("requestAnimationFrame", (callback: FrameRequestCallback) => {
      tick = callback;
      return 1;
    });
    vi.stubGlobal("cancelAnimationFrame", vi.fn());
    const writeFrame = vi.fn(() => true);
    const captureFrame = vi.fn(() => null);
    renderHook(() =>
      useMirrorRenderLoop({
        isWebcamRunning: true,
        terminalDimensions: { cols: 80, rows: 24 },
        captureFrame,
        canvasRef: { current: null },
        rendererRef: { current: { writeFrame } as never },
        debugCountRef: { current: 0 },
        firstFrameTimeRef: { current: null },
        frameIntervalRef: { current: 1000 / 60 },
        streamRef: { current: null },
        animation,
        animationEnabled: true,
      }),
    );
    tick(performance.now() + 20);
    expect(writeFrame).toHaveBeenCalledWith(
      animation === "matrix" ? "rain" : "silence",
    );
    if (animation === "matrix") expect(convertFrameToAscii).toHaveBeenCalled();
    else
      expect(renderAudioVisualizationFrame).toHaveBeenCalledWith(
        expect.any(Float32Array),
        80,
        24,
        { source: "microphone", mode: animation },
      );
  });
}
