import { act, cleanup, renderHook } from "@testing-library/react";
import { afterEach, expect, test, vi } from "vite-plus/test";
import { useMirrorWebcam } from "../../src/hooks/useMirrorWebcam";
import { DEFAULT_SETTINGS } from "../../src/utils/defaultSettings";

vi.mock("@ascii-chat/shared/hooks", () => ({
  useTestPattern: () => ({ enabled: false }),
}));
vi.mock("@ascii-chat/shared/wasm", () => ({
  isWasmReady: () => false,
  isOptionsInitialized: () => true,
}));

afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
  vi.unstubAllGlobals();
  localStorage.clear();
});

for (const fps of [30, 60]) {
  test(`Mirror keeps camera acquisition at 60 FPS with a ${fps} FPS rendering limit`, async () => {
    vi.spyOn(console, "log").mockImplementation(() => {});
    vi.spyOn(console, "time").mockImplementation(() => {});
    vi.spyOn(console, "timeEnd").mockImplementation(() => {});
    const track = {
      stop: vi.fn(),
      getSettings: () => ({ width: 1280, height: 720 }),
    };
    const stream = { getTracks: () => [track], getVideoTracks: () => [track] };
    const canvas = document.createElement("canvas");
    const getUserMedia = vi.fn().mockResolvedValue(stream);
    vi.stubGlobal("navigator", {
      mediaDevices: {
        getUserMedia,
        enumerateDevices: vi.fn().mockResolvedValue([]),
      },
    });
    const video = {
      readyState: 1,
      muted: true,
      play: vi.fn().mockResolvedValue(undefined),
    } as unknown as HTMLVideoElement;
    const hook = renderHook(() =>
      useMirrorWebcam({
        settings: { ...DEFAULT_SETTINGS, targetFps: fps },
        videoRef: { current: video },
        canvasRef: { current: canvas },
        streamRef: { current: null },
        lastFrameTimeRef: { current: 0 },
        setIsWebcamRunning: vi.fn(),
        setMediaSource: vi.fn(),
        setError: vi.fn(),
        wasmInitialized: false,
        isWebcamRunning: false,
      }),
    );
    await act(async () => {
      expect(await hook.result.current.startWebcam()).toBe(true);
    });
    expect([canvas.width, canvas.height]).toEqual([320, 180]);
    expect(getUserMedia).toHaveBeenCalledWith(
      expect.objectContaining({
        video: expect.objectContaining({
          width: { ideal: 1280 },
          height: { ideal: 720 },
          frameRate: { ideal: 60 },
        }),
      }),
    );
  });
}
