import { act, cleanup, renderHook } from "@testing-library/react";
import { afterEach, beforeEach, expect, test, vi } from "vite-plus/test";
import { useMirrorFile } from "../../src/hooks/useMirrorFile";

const backend = vi.hoisted(() => ({
  transcode: vi.fn(),
  play: vi.fn(),
  capture: vi.fn(),
}));
vi.mock("../../src/utils/transcodeMedia", () => ({
  transcodeMedia: backend.transcode,
}));
vi.mock("../../src/utils/playMedia", () => ({ playMedia: backend.play }));
vi.mock("@ascii-chat/shared/hooks", () => ({
  useCanvasCapture: () => ({ captureFrame: backend.capture }),
}));

beforeEach(() => {
  vi.clearAllMocks();
  vi.stubGlobal(
    "URL",
    class extends URL {
      static createObjectURL = vi.fn(() => `blob:${Math.random()}`);
      static revokeObjectURL = vi.fn();
    },
  );
  vi.spyOn(HTMLMediaElement.prototype, "pause").mockImplementation(() => {});
  vi.spyOn(HTMLMediaElement.prototype, "load").mockImplementation(() => {});
  backend.play.mockResolvedValue(undefined);
});
afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
  vi.unstubAllGlobals();
});

function setup() {
  const video = document.createElement("video");
  Object.defineProperties(video, {
    videoWidth: { value: 320 },
    videoHeight: { value: 240 },
  });
  const canvas = document.createElement("canvas");
  const options = {
    videoRef: { current: video },
    canvasRef: { current: canvas },
    stopWebcam: vi.fn(),
    cancelStart: vi.fn(),
    setIsWebcamRunning: vi.fn(),
    setMediaSource: vi.fn(),
    setError: vi.fn(),
  };
  return {
    ...renderHook(() => useMirrorFile(options)),
    options,
    video,
    canvas,
  };
}

test("video selection replaces camera, keeps audio enabled, and releases resources on Stop", async () => {
  const { result, options, video, canvas } = setup();
  video.srcObject = {} as MediaStream;
  await act(() =>
    result.current.start(
      new File(["video"], "tone.mp4", { type: "video/mp4" }),
    ),
  );
  expect(options.cancelStart).toHaveBeenCalled();
  expect(options.stopWebcam).toHaveBeenCalled();
  expect(video.srcObject).toBeNull();
  expect(video.muted).toBe(false);
  expect(video.loop).toBe(true);
  expect(canvas.width).toBe(320);
  expect(result.current.kind).toBe("video");
  expect(backend.transcode).not.toHaveBeenCalled();
  act(() => result.current.stop());
  expect(URL.revokeObjectURL).toHaveBeenCalledOnce();
  expect(video.getAttribute("src")).toBeNull();
  expect(result.current.kind).toBeNull();
});

test("AVI uses conversion with audio even if the browser might accept its video track", async () => {
  backend.transcode.mockResolvedValue(
    new Blob(["converted"], { type: "video/mp4" }),
  );
  const { result } = setup();
  const file = new File(["avi"], "tone.avi");
  await act(() => result.current.start(file));
  expect(backend.transcode).toHaveBeenCalledWith(
    file,
    expect.any(AbortSignal),
    expect.any(Function),
  );
  expect(backend.play).toHaveBeenCalledOnce();
  expect(URL.revokeObjectURL).toHaveBeenCalledOnce();
  expect(result.current.kind).toBe("video");
});

test("unsupported native codecs fall back to conversion", async () => {
  backend.play.mockRejectedValueOnce(
    new DOMException("Unsupported", "NotSupportedError"),
  );
  backend.transcode.mockResolvedValue(new Blob(["converted"]));
  const { result } = setup();
  await act(() => result.current.start(new File(["video"], "legacy.mp4")));
  expect(backend.transcode).toHaveBeenCalledOnce();
  expect(backend.play).toHaveBeenCalledTimes(2);
  expect(result.current.kind).toBe("video");
});

test("cancelled conversion cannot replace a newer selected video", async () => {
  let finish!: (blob: Blob) => void;
  backend.transcode.mockImplementation(
    () =>
      new Promise<Blob>((resolve) => {
        finish = resolve;
      }),
  );
  const { result } = setup();
  let old!: Promise<void>;
  await act(async () => {
    old = result.current.start(new File(["avi"], "old.avi"));
  });
  const signal = backend.transcode.mock.calls[0]![1] as AbortSignal;
  await act(() => result.current.start(new File(["video"], "new.mp4")));
  expect(signal.aborted).toBe(true);
  await act(async () => {
    finish(new Blob(["old"]));
    await old;
  });
  expect(result.current.fileName).toBe("new.mp4");
  expect(result.current.kind).toBe("video");
  expect(backend.play).toHaveBeenCalledOnce();
});

test("a failed file clears loading and permits selecting a working replacement", async () => {
  backend.transcode.mockRejectedValueOnce(new Error("Invalid data"));
  const { result, options } = setup();
  await act(() => result.current.start(new File(["broken"], "broken.avi")));
  expect(result.current.loading).toBe(false);
  expect(result.current.kind).toBeNull();
  expect(options.setError).toHaveBeenLastCalledWith(
    expect.stringContaining("Invalid data"),
  );
  await act(() => result.current.start(new File(["video"], "good.mp4")));
  expect(options.setError).toHaveBeenLastCalledWith("");
  expect(result.current.kind).toBe("video");
});

test("PNG uses image decoding and does not call video playback", async () => {
  vi.stubGlobal(
    "Image",
    class {
      naturalWidth = 1920;
      naturalHeight = 1080;
      src = "";
      decode = vi.fn().mockResolvedValue(undefined);
      removeAttribute = vi.fn();
    },
  );
  const { result, canvas } = setup();
  await act(() =>
    result.current.start(new File(["png"], "still.png", { type: "image/png" })),
  );
  expect(result.current.kind).toBe("image");
  expect(canvas.width).toBe(1280);
  expect(canvas.height).toBe(720);
  expect(backend.play).not.toHaveBeenCalled();
});

test("unmount cancels in-flight work and revokes the file URL", async () => {
  backend.play.mockImplementationOnce(() => new Promise(() => {}));
  const { result, unmount } = setup();
  act(() => {
    void result.current.start(new File(["video"], "pending.mp4"));
  });
  const signal = backend.play.mock.calls[0]![1] as AbortSignal;
  unmount();
  expect(signal.aborted).toBe(true);
  expect(URL.revokeObjectURL).toHaveBeenCalledOnce();
});
