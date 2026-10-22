import { act, cleanup, renderHook } from "@testing-library/react";
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

test.each([30, 33, 60, 120, 144])(
  "Mirror converts each %s FPS source frame once, with no duplicates on idle render ticks",
  async (fps) => {
    let controller: ReadableStreamDefaultController<VideoFrame>;
    const cancel = vi.fn();
    vi.stubGlobal(
      "MediaStreamTrackProcessor",
      class {
        readable = new ReadableStream<VideoFrame>({
          start(value) {
            controller = value;
          },
          cancel,
        });
      },
    );
    let now = 0;
    let tick: FrameRequestCallback = () => {};
    vi.spyOn(performance, "now").mockImplementation(() => now);
    vi.stubGlobal("requestAnimationFrame", (callback: FrameRequestCallback) => {
      tick = callback;
      return 1;
    });
    vi.stubGlobal("cancelAnimationFrame", vi.fn());
    const drawImage = vi.fn();
    const captureFrame = vi.fn((_drawVideo?: boolean) => ({
      data: new Uint8Array(4),
      width: 1,
      height: 1,
    }));
    const writeFrame = vi.fn(() => true);
    convert.mockReturnValue("camera frame");
    const cameraTrack = {
      getCapabilities: () => ({ frameRate: { max: fps } }),
      getSettings: () => ({ frameRate: fps }),
    };
    const hook = renderHook(() =>
      useMirrorRenderLoop({
        isWebcamRunning: true,
        terminalDimensions: { cols: 80, rows: 30 },
        captureFrame,
        canvasRef: {
          current: {
            width: 1,
            height: 1,
            getContext: () => ({ drawImage }),
          } as unknown as HTMLCanvasElement,
        },
        rendererRef: {
          current: {
            writeFrame,
            getCanvas: () => null,
            getDimensions: () => ({ cols: 80, rows: 30 }),
            clear: vi.fn(),
            recreateRenderer: vi.fn(),
          },
        },
        debugCountRef: { current: 0 },
        firstFrameTimeRef: { current: null },
        frameIntervalRef: { current: 1000 / fps },
        streamRef: {
          current: {
            getVideoTracks: () => [cameraTrack],
          } as unknown as MediaStream,
        },
      }),
    );
    const frames = Array.from({ length: 120 }, (_, index) => ({
      timestamp: Math.round((index * 1e6) / fps),
      close: vi.fn(),
    }));
    for (const frame of frames) {
      await act(async () => {
        controller!.enqueue(frame as unknown as VideoFrame);
      });
      // Idle host ticks between arrivals must not convert the same image twice.
      for (let repeat = 0; repeat < 2; repeat++) {
        now += 1000 / 60;
        await act(async () => tick(now));
      }
    }
    now += 1000 / 60;
    await act(async () => tick(now));
    expect(drawImage).toHaveBeenCalledTimes(120);
    expect(writeFrame).toHaveBeenCalledTimes(120);
    expect(convert).toHaveBeenCalledTimes(120);
    expect(
      captureFrame.mock.calls.every(([drawVideo]) => drawVideo === false),
    ).toBe(true);
    expect(frames.every((frame) => frame.close.mock.calls.length === 1)).toBe(
      true,
    );
    hook.unmount();
    expect(cancel).toHaveBeenCalledOnce();
  },
);

test("Mirror starts and replaces direct capture after script-driven track restoration", async () => {
  let tick: FrameRequestCallback = () => {};
  vi.stubGlobal("requestAnimationFrame", (callback: FrameRequestCallback) => {
    tick = callback;
    return 1;
  });
  vi.stubGlobal("cancelAnimationFrame", vi.fn());
  const readers: {
    track: MediaStreamTrack;
    controller: ReadableStreamDefaultController<VideoFrame>;
    cancel: ReturnType<typeof vi.fn>;
  }[] = [];
  vi.stubGlobal(
    "MediaStreamTrackProcessor",
    class {
      readable: ReadableStream<VideoFrame>;
      constructor({ track }: { track: MediaStreamTrack }) {
        const cancel = vi.fn();
        this.readable = new ReadableStream<VideoFrame>({
          start(controller) {
            readers.push({ track, controller, cancel });
          },
          cancel,
        });
      }
    },
  );
  let tracks: MediaStreamTrack[] = [];
  const stream = Object.assign(new EventTarget(), {
    getVideoTracks: () => tracks,
  });
  const drawImage = vi.fn();
  const captureFrame = vi.fn(() => ({
    data: new Uint8Array(4),
    width: 1,
    height: 1,
  }));
  const writeFrame = vi.fn(() => true);
  convert.mockReturnValue("restored camera frame");
  const hook = renderHook(() =>
    useMirrorRenderLoop({
      isWebcamRunning: true,
      terminalDimensions: { cols: 80, rows: 24 },
      captureFrame,
      canvasRef: {
        current: {
          width: 1,
          height: 1,
          getContext: () => ({ drawImage }),
        } as unknown as HTMLCanvasElement,
      },
      rendererRef: {
        current: {
          writeFrame,
          getCanvas: () => null,
          getDimensions: () => ({ cols: 80, rows: 24 }),
          clear: vi.fn(),
          recreateRenderer: vi.fn(),
        },
      },
      debugCountRef: { current: 0 },
      firstFrameTimeRef: { current: null },
      frameIntervalRef: { current: 1000 / 60 },
      streamRef: { current: stream as unknown as MediaStream },
    }),
  );
  expect(readers).toHaveLength(0);
  for (let index = 0; index < 2; index++) {
    // Like MediaStream.addTrack(), this mutation emits no addtrack event.
    tracks = [{} as MediaStreamTrack];
    await act(async () => tick(performance.now() + 20));
    expect(readers).toHaveLength(index + 1);
    expect(readers[index]!.track).toBe(tracks[0]);
    const frame = { timestamp: 0, close: vi.fn() };
    await act(async () => {
      readers[index]!.controller.enqueue(frame as unknown as VideoFrame);
    });
    await act(async () => tick(performance.now() + (index + 1) * 40));
    await act(async () => tick(performance.now() + (index + 1) * 40 + 20));
    expect(frame.close).toHaveBeenCalledOnce();
    expect(captureFrame).toHaveBeenLastCalledWith(false);
    expect(writeFrame).toHaveBeenCalledTimes(index + 1);
  }
  expect(readers[0]!.cancel).toHaveBeenCalledOnce();
  hook.unmount();
  expect(readers[1]!.cancel).toHaveBeenCalledOnce();
});

for (const contentFit of [undefined, "cover"] as const) {
  for (const fault of ["slow", "throw"] as const) {
    test(`Mirror continues rendering after one ${fault} conversion (${contentFit ?? "default contain"})`, () => {
      let now = 0;
      let nextFrame: FrameRequestCallback | undefined;
      vi.spyOn(performance, "now").mockImplementation(() => now);
      vi.stubGlobal(
        "requestAnimationFrame",
        (callback: FrameRequestCallback) => {
          nextFrame = callback;
          return 1;
        },
      );
      vi.stubGlobal("cancelAnimationFrame", vi.fn());
      vi.spyOn(console, "warn").mockImplementation(() => {});
      convert
        .mockImplementationOnce(() => {
          if (fault === "throw")
            throw new Error("transient conversion failure");
          now += 150;
          return "slow frame";
        })
        .mockReturnValue("recovered frame");
      const writeFrame = vi.fn(() => true);
      renderHook(() =>
        useMirrorRenderLoop({
          isWebcamRunning: true,
          ...(contentFit ? { contentFit } : {}),
          terminalDimensions: { cols: 80, rows: 24 },
          captureFrame: () => ({
            data: new Uint8Array(4),
            width: 1,
            height: 1,
          }),
          canvasRef: { current: null },
          rendererRef: {
            current: {
              writeFrame,
              getCanvas: () => null,
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
      expect(
        convert.mock.calls.every(
          (call) => call[3] === (contentFit ?? "contain"),
        ),
      ).toBe(true);
      expect(writeFrame).toHaveBeenCalledWith("recovered frame");
      expect(nextFrame).toBeDefined();
    });
  }
}

test("camera video callbacks also convert once per arrival when direct capture is unavailable", async () => {
  vi.stubGlobal("MediaStreamTrackProcessor", undefined);
  let tick!: FrameRequestCallback;
  let delivered!: VideoFrameRequestCallback;
  vi.stubGlobal("requestAnimationFrame", (callback: FrameRequestCallback) => {
    tick = callback;
    return 1;
  });
  vi.stubGlobal("cancelAnimationFrame", vi.fn());
  const track = {
    getSettings: () => ({ frameRate: 30 }),
    getCapabilities: () => ({ frameRate: { max: 30 } }),
  };
  const video = {
    srcObject: { getVideoTracks: () => [track] },
    requestVideoFrameCallback: (callback: VideoFrameRequestCallback) => {
      delivered = callback;
      return 1;
    },
    cancelVideoFrameCallback: vi.fn(),
  };
  const writeFrame = vi.fn(() => true);
  convert.mockReturnValue("identical camera output");
  renderHook(() =>
    useMirrorRenderLoop({
      isWebcamRunning: true,
      terminalDimensions: { cols: 80, rows: 24 },
      captureFrame: () => ({ data: new Uint8Array(4), width: 1, height: 1 }),
      cameraVideoRef: { current: video as unknown as HTMLVideoElement },
      canvasRef: { current: null },
      rendererRef: { current: { writeFrame } as never },
      debugCountRef: { current: 0 },
      firstFrameTimeRef: { current: null },
      frameIntervalRef: { current: 1000 / 30 },
      streamRef: { current: null },
    }),
  );
  for (let frame = 0; frame < 10; frame++) {
    await act(async () =>
      delivered(performance.now(), {
        mediaTime: frame / 30,
      } as VideoFrameCallbackMetadata),
    );
    await act(async () => tick(performance.now() + 20));
    await act(async () => tick(performance.now() + 40));
  }
  expect(convert).toHaveBeenCalledTimes(10);
  expect(writeFrame).toHaveBeenCalledTimes(10);
});
