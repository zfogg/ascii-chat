import { act, cleanup, renderHook } from "@testing-library/react";
import { afterEach, beforeEach, expect, it, vi } from "vite-plus/test";
import { useWebcamStream } from "../../src/hooks/useWebcamStream";
import { ConnectionState, PacketType } from "../../src/wasm/client";
import { DEFAULT_SETTINGS } from "../../src/utils/defaultSettings";

const media = vi.hoisted(() => ({
  cameraId: "selected-camera",
  pattern: vi.fn(),
}));
vi.mock("@ascii-chat/shared", async (importOriginal) => ({
  ...(await importOriginal<object>()),
  createTestPatternVideoSource: media.pattern,
}));
vi.mock("../../src/hooks/useVideoEncodings", () => ({
  useVideoEncodings: () => ({ preference: "raw", validate: () => {} }),
}));
vi.mock("../../src/utils/mediaDevicePreferences", () => ({
  getMediaDevicePreferences: () => ({ cameraId: media.cameraId }),
  resolveCameraDeviceId: async () => media.cameraId,
  MEDIA_DEVICE_PREFERENCES_CHANGED: "devices",
}));
vi.mock("../../src/wasm/AVState", () => ({
  AVState: class {
    generation = 0;
    active = false;
    begin() {
      return ++this.generation;
    }
    current(generation: number) {
      return generation > 0 && generation === this.generation;
    }
    complete(generation: number) {
      this.active = this.current(generation);
      return this.active;
    }
    stop(release: () => void) {
      ++this.generation;
      this.active = false;
      release();
    }
    dispose() {}
  },
}));

beforeEach(() => {
  vi.clearAllMocks();
  media.cameraId = "selected-camera";
  history.replaceState({}, "", "/client");
});

it("a failed display startup does not leave the recovered camera marked as display capture", async () => {
  const { result, options } = setup();
  const video = options.videoRef.current!;
  Object.defineProperties(video, {
    videoWidth: { value: 640 },
    videoHeight: { value: 480 },
  });
  video.play = vi
    .fn()
    .mockRejectedValueOnce(new Error("Playback denied"))
    .mockResolvedValue(undefined);
  Object.assign(options.clientRef.current!, {
    sendVideoFrame: vi.fn(),
    sendPacket: vi.fn(),
  });
  const makeStream = () => {
    const track = {
      kind: "video",
      readyState: "live",
      getSettings: () => ({ width: 640, height: 480, frameRate: 30 }),
      applyConstraints: vi.fn().mockResolvedValue(undefined),
      stop: vi.fn(),
    };
    return {
      track,
      stream: {
        getTracks: () => [track],
        getVideoTracks: () => [track],
      } as unknown as MediaStream,
    };
  };
  const display = makeStream();
  await act(async () =>
    expect(await result.current.startVideoStream(display.stream)).toBe(false),
  );
  expect(display.track.stop).toHaveBeenCalledOnce();
  const camera = makeStream();
  Object.defineProperty(navigator, "mediaDevices", {
    configurable: true,
    value: { getUserMedia: vi.fn().mockResolvedValue(camera.stream) },
  });
  await act(async () => expect(await result.current.startWebcam()).toBe(true));
  expect(camera.track.applyConstraints).toHaveBeenCalled();
  expect(result.current.isWebcamRunning).toBe(true);
});
afterEach(cleanup);

it("sends selected file pixels without acquiring a webcam and respects backpressure", () => {
  const { result, options } = setup();
  const sendPacket = vi.fn();
  let queued = 0;
  Object.assign(options.clientRef.current!, {
    sendPacket,
    getBufferedAmount: () => queued,
  });
  const frame = { data: new Uint8Array(16).fill(128), width: 2, height: 2 };
  result.current.sendFileFrame(frame, options.canvasRef.current!);
  expect(sendPacket).toHaveBeenCalledExactlyOnceWith(
    PacketType.IMAGE_FRAME,
    expect.any(Uint8Array),
  );
  expect(result.current.isWebcamRunning).toBe(false);
  queued = 1024 * 1024;
  result.current.sendFileFrame(frame, options.canvasRef.current!);
  expect(sendPacket).toHaveBeenCalledTimes(1);
});

function setup() {
  const send = vi.fn();
  const options = {
    terminalDimensions: { cols: 80, rows: 40 },
    clientRef: {
      current: {
        getState: () => ConnectionState.CONNECTED,
        sendUnencryptedAcipPacket: send,
      },
    },
    connectionState: ConnectionState.CONNECTED,
    settings: DEFAULT_SETTINGS,
    captureFrame: () => null,
    videoRef: { current: document.createElement("video") },
    canvasRef: { current: document.createElement("canvas") },
    frameIntervalRef: { current: 1000 / 60 },
    lastFrameTimeRef: { current: 0 },
    frameQueueRef: { current: [] },
    setError: vi.fn(),
  } as unknown as Parameters<typeof useWebcamStream>[0];
  return { ...renderHook(() => useWebcamStream(options)), options, send };
}

it.each(["NotReadableError", "NotFoundError", "NotAllowedError"])(
  "reports %s without changing cameras or generating a test pattern",
  async (name) => {
    const getUserMedia = vi
      .fn()
      .mockRejectedValue(new DOMException("Camera unavailable", name));
    Object.defineProperty(navigator, "mediaDevices", {
      configurable: true,
      value: { getUserMedia },
    });
    const { result, options, send } = setup();
    await act(async () => result.current.startWebcam());
    expect(getUserMedia).toHaveBeenCalledExactlyOnceWith({
      video: {
        width: { ideal: 1280 },
        height: { ideal: 720 },
        frameRate: { ideal: 60 },
        deviceId: { exact: "selected-camera" },
      },
      audio: false,
    });
    expect(options.setError).toHaveBeenLastCalledWith(
      `Failed to start webcam: ${name}: Camera unavailable`,
    );
    expect(media.pattern).not.toHaveBeenCalled();
    expect(result.current.isWebcamRunning).toBe(false);
    expect(options.videoRef.current!.srcObject).toBeNull();
    expect(send).toHaveBeenLastCalledWith(
      PacketType.STREAM_STOP,
      expect.any(Uint8Array),
    );
  },
);

it("rejects a camera stream with no video track and releases its tracks", async () => {
  const stop = vi.fn();
  Object.defineProperty(navigator, "mediaDevices", {
    configurable: true,
    value: {
      getUserMedia: vi.fn().mockResolvedValue({
        getTracks: () => [{ stop }],
        getVideoTracks: () => [],
      }),
    },
  });
  const { result, options } = setup();
  await act(async () => result.current.startWebcam());
  expect(options.setError).toHaveBeenLastCalledWith(
    "Failed to start webcam: Error: Selected camera did not provide a live video track",
  );
  expect(stop).toHaveBeenCalledOnce();
  expect(media.pattern).not.toHaveBeenCalled();
  expect(result.current.isWebcamRunning).toBe(false);
});

it.each(["test", "test2"])(
  "only uses synthetic video when ?%s is explicit",
  async (mode) => {
    history.replaceState({}, "", `/client?${mode}&videoDeviceIndex=99`);
    media.cameraId = "";
    const getUserMedia = vi.fn();
    const enumerateDevices = vi.fn();
    Object.defineProperty(navigator, "mediaDevices", {
      configurable: true,
      value: { getUserMedia, enumerateDevices },
    });
    // Stop at the synthetic-source boundary without emulating video playback in jsdom.
    media.pattern.mockImplementationOnce(() => {
      throw new Error("synthetic source reached");
    });
    const { result } = setup();
    await act(async () => result.current.startWebcam());
    expect(media.pattern).toHaveBeenCalledWith(
      60,
      expect.any(Number),
      expect.any(Number),
      mode,
      expect.any(HTMLCanvasElement),
      expect.any(Function),
    );
    expect(getUserMedia).not.toHaveBeenCalled();
    expect(enumerateDevices).not.toHaveBeenCalled();
  },
);
