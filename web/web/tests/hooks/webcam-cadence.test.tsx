import { act, cleanup, renderHook } from "@testing-library/react";
import { afterEach, expect, it, vi } from "vite-plus/test";
import { useWebcamStream } from "../../src/hooks/useWebcamStream";
import { ConnectionState, PacketType } from "../../src/wasm/client";
import { DEFAULT_SETTINGS } from "../../src/utils/defaultSettings";
import type { ClientSession } from "../../src/network/Transport";

const captureOptions = vi.hoisted(() => ({
  preference: "raw",
  createEncoder: async () => null,
  validate: () => {},
}));
const testPattern = vi.hoisted(() => ({ enabled: false }));
vi.mock("../../src/hooks/useVideoEncodings", () => ({
  useVideoEncodings: () => captureOptions,
}));
vi.mock("@ascii-chat/shared", () => ({
  useTestPattern: () => testPattern,
  createTestPatternVideoSource: vi.fn(),
}));
// These tests exercise capture scheduling without loading the C lifecycle runtime.
vi.mock("../../src/wasm/AVState", () => ({
  AVState: class {
    generation = 0;
    active = false;
    begin() {
      return ++this.generation;
    }
    current(generation: number) {
      return generation === this.generation;
    }
    complete() {
      this.active = true;
      return true;
    }
    stop(release: () => void) {
      this.generation++;
      this.active = false;
      release();
    }
    dispose() {}
  },
}));

afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
  vi.unstubAllGlobals();
  window.localStorage.clear();
});

type CapturePath = "track" | "video-callback" | "poll";
async function startCapture(
  path: CapturePath,
  targetFps: number,
  receiveFps = targetFps,
) {
  let wallTime = 1000;
  let mediaTime = 0;
  let callback:
    | ((now: number, metadata: VideoFrameCallbackMetadata) => void)
    | undefined;
  let raf: FrameRequestCallback | undefined;
  let controller: ReadableStreamDefaultController<VideoFrame> | undefined;
  vi.spyOn(performance, "now").mockImplementation(() => wallTime);
  vi.spyOn(console, "log").mockImplementation(() => {});
  vi.spyOn(console, "info").mockImplementation(() => {});
  const video = document.createElement("video");
  Object.defineProperties(video, {
    videoWidth: { value: 320 },
    videoHeight: { value: 180 },
    readyState: { value: 4 },
    paused: { value: false },
    currentTime: { get: () => mediaTime },
    requestVideoFrameCallback: {
      value:
        path === "video-callback"
          ? (next: typeof callback) => {
              callback = next;
              return 1;
            }
          : undefined,
    },
    cancelVideoFrameCallback: { value: vi.fn() },
  });
  vi.spyOn(video, "play").mockResolvedValue(undefined);
  vi.spyOn(video, "pause").mockImplementation(() => {});
  const canvas = document.createElement("canvas");
  vi.spyOn(canvas, "getContext").mockReturnValue({
    drawImage: vi.fn(),
    save: vi.fn(),
    restore: vi.fn(),
    translate: vi.fn(),
    scale: vi.fn(),
  } as unknown as CanvasRenderingContext2D);
  const track = {
    kind: "video",
    label: "60 FPS camera",
    readyState: "live",
    getSettings: () => ({ width: 320, height: 180, frameRate: 60 }),
    getCapabilities: () => ({ frameRate: { max: 60 } }),
    applyConstraints: vi.fn().mockResolvedValue(undefined),
    stop: vi.fn(),
  };
  const stream = {
    active: true,
    getVideoTracks: () => [track],
    getTracks: () => [track],
  };
  vi.stubGlobal("navigator", {
    mediaDevices: {
      getUserMedia: vi.fn().mockResolvedValue(stream),
      enumerateDevices: vi.fn().mockResolvedValue([]),
    },
  });
  vi.stubGlobal(
    "MediaStreamTrackProcessor",
    path === "track"
      ? class {
          readable = new ReadableStream<VideoFrame>({
            start(value) {
              controller = value;
            },
          });
        }
      : undefined,
  );
  vi.stubGlobal("requestAnimationFrame", (next: FrameRequestCallback) => {
    raf = next;
    return 1;
  });
  vi.stubGlobal("cancelAnimationFrame", vi.fn());
  const sendPacket = vi.fn();
  const setError = vi.fn();
  let transportState = ConnectionState.CONNECTED;
  const options = {
    terminalDimensions: { cols: 80, rows: 24 },
    clientRef: {
      current: {
        sendPacket,
        sendUnencryptedAcipPacket: vi.fn(),
        getState: () => transportState,
      } as unknown as ClientSession,
    },
    connectionState: ConnectionState.CONNECTED,
    settings: { ...DEFAULT_SETTINGS, targetFps, flipX: false },
    receiveFps,
    captureFrame: () => ({ data: new Uint8Array(16), width: 2, height: 2 }),
    canvasRef: { current: canvas },
    videoRef: { current: video },
    frameIntervalRef: { current: 1000 / targetFps },
    lastFrameTimeRef: { current: 0 },
    frameQueueRef: { current: [] },
    setError,
  };
  const hook = renderHook(() => useWebcamStream(options));
  await act(async () => {
    await hook.result.current.startWebcam();
  });
  expect(setError.mock.calls.filter(([message]) => message !== "")).toEqual([]);
  expect(hook.result.current.isWebcamRunning).toBe(true);
  const deliver = async (seconds: number, arrival: number) => {
    mediaTime = seconds;
    wallTime = arrival;
    await act(async () => {
      if (path === "track") {
        controller!.enqueue({
          timestamp: Math.round(seconds * 1e6),
          close: vi.fn(),
        } as unknown as VideoFrame);
      } else if (path === "video-callback") {
        callback!(arrival, {
          mediaTime: seconds,
        } as VideoFrameCallbackMetadata);
      } else raf!(arrival);
    });
  };
  return {
    setTransportState: (state: ConnectionState) => {
      transportState = state;
    },
    receiveFps: () =>
      sendPacket.mock.calls.find(
        ([type]) => type === PacketType.CLIENT_CAPABILITIES,
      )?.[1][157],
    deliver,
    sent: () =>
      sendPacket.mock.calls.filter(([type]) => type === PacketType.IMAGE_FRAME)
        .length,
  };
}

it("advertises the receive FPS independently of a saved low camera upload limit", async () => {
  const capture = await startCapture("track", 20, 60);
  expect(capture.receiveFps()).toBe(60);
  for (let frame = 0; frame < 120; frame++)
    await capture.deliver(frame / 60, 1000 + (frame * 1000) / 60);
  expect(capture.sent()).toBe(40);
});

for (const path of ["track", "video-callback", "poll"] as const) {
  it(`${path}: stops sending immediately when transport closes before React updates`, async () => {
    const capture = await startCapture(path, 60);
    await capture.deliver(0, 1000);
    expect(capture.sent()).toBe(1);
    capture.setTransportState(ConnectionState.DISCONNECTED);
    await capture.deliver(1 / 60, 1017);
    capture.setTransportState(ConnectionState.HANDSHAKE);
    await capture.deliver(2 / 60, 1034);
    expect(capture.sent()).toBe(1);
  });

  it(`${path}: preserves 60 FPS despite callback jitter and rounded arrival times`, async () => {
    const capture = await startCapture(path, 60);
    for (let frame = 0; frame < 120; frame++) {
      // Camera timestamps remain regular while main-thread callbacks arrive early/late.
      await capture.deliver(
        frame / 60,
        1000 + Math.round((frame * 1000) / 60) + (frame % 2 ? -0.7 : 0.7),
      );
    }
    expect(capture.sent()).toBe(120);
  });

  it(`${path}: respects a 30 FPS ceiling and ignores duplicate decoded frames`, async () => {
    const capture = await startCapture(path, 30);
    for (let frame = 0; frame < 120; frame++) {
      await capture.deliver(frame / 60, 1000 + (frame * 1000) / 60);
    }
    expect(capture.sent()).toBe(60);
    await capture.deliver(3, 5000);
    const afterGap = capture.sent();
    await capture.deliver(3, 5100);
    expect(capture.sent()).toBe(afterGap);
  });
}
