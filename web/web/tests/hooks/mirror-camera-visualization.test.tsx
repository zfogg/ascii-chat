import { act, renderHook, waitFor } from "@testing-library/react";
import { expect, test, vi } from "vite-plus/test";
import { useMirrorWebcam } from "../../src/hooks/useMirrorWebcam";
import { MediaSourceType } from "../../src/hooks/useClientLike";
import { DEFAULT_SETTINGS } from "../../src/utils/defaultSettings";
const { resolveCameraDeviceId } = vi.hoisted(() => ({
  resolveCameraDeviceId: vi.fn(async () => "selected-camera"),
}));
vi.mock("@ascii-chat/shared/hooks", () => ({
  useTestPattern: () => ({ enabled: false }),
}));
vi.mock("@ascii-chat/shared/wasm", () => ({
  isWasmReady: () => false,
  isOptionsInitialized: () => true,
}));
vi.mock("../../src/utils/mediaDevicePreferences", () => ({
  getMediaDevicePreferences: () => ({}),
  resolveCameraDeviceId,
  MEDIA_DEVICE_PREFERENCES_CHANGED: "devices",
}));
function track(kind: string) {
  return {
    kind,
    readyState: "live",
    stop: vi.fn(function (this: { readyState: string }) {
      this.readyState = "ended";
    }),
  };
}
function stream(tracks: ReturnType<typeof track>[]) {
  return {
    getTracks: () => tracks,
    getVideoTracks: () => tracks.filter((t) => t.kind === "video"),
    getAudioTracks: () => tracks.filter((t) => t.kind === "audio"),
    removeTrack: (t: ReturnType<typeof track>) => {
      tracks.splice(tracks.indexOf(t), 1);
    },
    addTrack: (t: ReturnType<typeof track>) => tracks.push(t),
  } as unknown as MediaStream;
}
function setup() {
  const mic = track("audio"),
    camera = track("video");
  const source = stream([mic, camera]);
  const video = {
    srcObject: source,
    pause: vi.fn(),
    play: vi.fn().mockResolvedValue(undefined),
  } as unknown as HTMLVideoElement;
  const params = {
    settings: DEFAULT_SETTINGS,
    videoRef: { current: video },
    canvasRef: { current: document.createElement("canvas") },
    streamRef: { current: source as MediaStream | null },
    lastFrameTimeRef: { current: 0 },
    setIsWebcamRunning: vi.fn(),
    setMediaSource: vi.fn(),
    setError: vi.fn(),
    wasmInitialized: false,
    isWebcamRunning: true,
    mediaSource: MediaSourceType.WEBCAM,
  };
  return { params, mic, camera, source };
}
test("audio visualization stops only camera and restores video without replacing microphone", async () => {
  const { params, mic, camera, source } = setup();
  const restored = track("video");
  const getUserMedia = vi.fn().mockResolvedValue(stream([restored]));
  Object.defineProperty(navigator, "mediaDevices", {
    configurable: true,
    value: { getUserMedia },
  });
  const { rerender } = renderHook((p) => useMirrorWebcam(p), {
    initialProps: params,
  });
  rerender({
    ...params,
    settings: {
      ...DEFAULT_SETTINGS,
      animationEnabled: true,
      animation: "waveform",
    },
  });
  expect(camera.stop).toHaveBeenCalledOnce();
  expect(mic.stop).not.toHaveBeenCalled();
  expect(source.getVideoTracks()).toHaveLength(0);
  rerender({
    ...params,
    settings: { ...DEFAULT_SETTINGS, animationEnabled: true, animation: "fft" },
  });
  expect(getUserMedia).not.toHaveBeenCalled();
  rerender(params);
  await waitFor(() => expect(source.getVideoTracks()).toHaveLength(1));
  expect(getUserMedia.mock.calls[0]![0].audio).toBe(false);
  expect(mic.stop).not.toHaveBeenCalled();
});
test("late camera acquisition is discarded after returning to visualization", async () => {
  const { params } = setup();
  const late = track("video");
  let finish!: (s: MediaStream) => void;
  Object.defineProperty(navigator, "mediaDevices", {
    configurable: true,
    value: {
      getUserMedia: vi.fn(
        () =>
          new Promise<MediaStream>((resolve) => {
            finish = resolve;
          }),
      ),
    },
  });
  const audio = {
    ...params,
    settings: {
      ...DEFAULT_SETTINGS,
      animationEnabled: true,
      animation: "fft" as const,
    },
  };
  const { rerender } = renderHook((p) => useMirrorWebcam(p), {
    initialProps: audio,
  });
  rerender({
    ...audio,
    settings: {
      ...DEFAULT_SETTINGS,
      animationEnabled: false,
      animation: "fft",
    },
  });
  await waitFor(() => expect(finish).toBeTypeOf("function"));
  rerender(audio);
  await act(async () => finish(stream([late])));
  expect(late.stop).toHaveBeenCalledOnce();
  expect(params.streamRef.current!.getVideoTracks()).toHaveLength(0);
});

test("starting in FFT requests microphone only and does not wait for video metadata", async () => {
  const { params } = setup();
  params.streamRef.current = null;
  const getUserMedia = vi.fn().mockResolvedValue(stream([track("audio")]));
  Object.defineProperty(navigator, "mediaDevices", {
    configurable: true,
    value: { getUserMedia },
  });
  const { result } = renderHook(() =>
    useMirrorWebcam({
      ...params,
      isWebcamRunning: false,
      settings: {
        ...DEFAULT_SETTINGS,
        animationEnabled: true,
        animation: "fft",
      },
    }),
  );
  await act(async () => expect(await result.current.startWebcam()).toBe(true));
  expect(getUserMedia.mock.calls[0]![0].video).toBe(false);
  expect(params.setIsWebcamRunning).toHaveBeenCalledWith(true);
  expect(params.videoRef.current.play).not.toHaveBeenCalled();
});

test("failed camera restoration requests only the selected camera", async () => {
  const { params, mic } = setup();
  const getUserMedia = vi
    .fn()
    .mockRejectedValue(new DOMException("Denied", "NotAllowedError"));
  Object.defineProperty(navigator, "mediaDevices", {
    configurable: true,
    value: { getUserMedia },
  });
  const audio = {
    ...params,
    settings: {
      ...DEFAULT_SETTINGS,
      animationEnabled: true,
      animation: "fft" as const,
    },
  };
  const { rerender } = renderHook((p) => useMirrorWebcam(p), {
    initialProps: audio,
  });
  rerender({
    ...audio,
    settings: {
      ...DEFAULT_SETTINGS,
      animationEnabled: false,
      animation: "fft",
    },
  });
  await waitFor(() =>
    expect(params.setError).toHaveBeenCalledWith(
      expect.stringContaining("Failed to restore webcam"),
    ),
  );
  expect(getUserMedia).toHaveBeenCalledTimes(1);
  expect(getUserMedia).toHaveBeenCalledWith({
    video: {
      width: { ideal: 640 },
      height: { ideal: 480 },
      frameRate: { ideal: DEFAULT_SETTINGS.targetFps },
      facingMode: "user",
      deviceId: { exact: "selected-camera" },
    },
    audio: false,
  });
  expect(mic.stop).not.toHaveBeenCalled();
});
