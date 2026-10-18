import {
  act,
  cleanup,
  fireEvent,
  render,
  screen,
  waitFor,
} from "@testing-library/react";
import { useCallback, useRef, useState } from "react";
import { afterEach, beforeEach, expect, test, vi } from "vite-plus/test";
import { MirrorPage } from "../../src/pages/Mirror";
import { MediaSourceType } from "../../src/hooks/useClientLike";
import { DEFAULT_SETTINGS } from "../../src/utils/defaultSettings";

const backend = vi.hoisted(() => ({
  ready: false,
  file: false,
  cameraReady: null as Promise<void> | null,
  suspended: vi.fn(),
  start: vi.fn(),
  stop: vi.fn(),
}));
vi.mock("@ascii-chat/shared/hooks", () => ({
  useTestPattern: () => ({ enabled: false }),
  useCanvasCapture: () => ({ captureFrame: vi.fn() }),
}));
vi.mock("@ascii-chat/shared/wasm", () => ({
  getMirrorModule: () => null,
  initMirrorWasm: vi.fn(),
  isWasmReady: () => true,
}));
vi.mock("../../src/wasm/dist/mirror.js", () => ({ default: vi.fn() }));
vi.mock("../../src/components", () => ({
  BinarySettings: () => null,
  AsciiRenderer: ({ uiLoading }: { uiLoading: boolean }) => (
    <div data-testid="renderer">{uiLoading ? "splash" : "frame"}</div>
  ),
  AsciiChatWebHead: () => null,
  VideoUploadModal: () => null,
  PageLayout: ({
    header,
    renderer,
  }: {
    header: React.ReactNode;
    renderer: React.ReactNode;
  }) => (
    <>
      {header}
      {renderer}
    </>
  ),
  ModeHeader: ({
    controlBar,
  }: {
    controlBar: {
      onStartWebcam: () => void;
      onStopWebcam: () => void;
      statusControls: React.ReactNode;
    };
  }) => (
    <>
      <button onClick={controlBar.onStartWebcam}>Start</button>
      <button onClick={controlBar.onStopWebcam}>Stop</button>
      {controlBar.statusControls}
    </>
  ),
}));
vi.mock("../../src/hooks", () => ({
  applyMirrorWasmSettings: vi.fn(),
  setMirrorWasmDimensions: vi.fn(),
  useMirrorRenderLoop: vi.fn(),
  useClientLike: () => {
    const [running, setRunning] = useState(backend.file);
    const [source, setSource] = useState<symbol | null>(
      backend.file ? MediaSourceType.FILE : null,
    );
    const ref = useRef(null);
    return {
      videoRef: ref,
      canvasRef: ref,
      rendererRef: ref,
      streamRef: ref,
      objectUrlRef: ref,
      lastFrameTimeRef: ref,
      frameIntervalRef: ref,
      debugCountRef: ref,
      firstFrameTimeRef: ref,
      isWebcamRunning: running,
      setIsWebcamRunning: setRunning,
      mediaSource: source,
      setMediaSource: setSource,
      settings: DEFAULT_SETTINGS,
      terminalDimensions: {
        cols: backend.ready ? 80 : 0,
        rows: backend.ready ? 40 : 0,
      },
      wasmInitialized: backend.ready,
      setFps: vi.fn(),
      stopWebcam: useCallback(() => {
        backend.stop();
        setRunning(false);
      }, []),
    };
  },
  useMirrorWebcam: ({
    setIsWebcamRunning,
    setMediaSource,
  }: {
    setIsWebcamRunning: (value: boolean) => void;
    setMediaSource: (value: symbol) => void;
  }) => ({
    startWebcam: async () => {
      backend.start();
      await backend.cameraReady;
      setMediaSource(MediaSourceType.WEBCAM);
      setIsWebcamRunning(true);
      return true;
    },
    cancelStart: vi.fn(),
  }),
}));
vi.mock("../../src/hooks/useMirrorFile", () => ({
  useMirrorFile: ({ stopWebcam }: { stopWebcam: () => void }) => ({
    kind: backend.file ? "video" : null,
    setSuspended: backend.suspended,
    stop: stopWebcam,
  }),
}));
beforeEach(() => {
  vi.clearAllMocks();
  backend.ready = false;
  backend.file = false;
  backend.cameraReady = null;
  history.replaceState({}, "", "/mirror");
});
afterEach(cleanup);

test("refresh resumes once after WASM and dimensions are ready, and Stop clears intent", async () => {
  history.replaceState({}, "", "/mirror?playing=true");
  const view = render(<MirrorPage />);
  expect(backend.start).not.toHaveBeenCalled();
  backend.ready = true;
  view.rerender(<MirrorPage />);
  await waitFor(() => expect(backend.start).toHaveBeenCalledOnce());
  view.rerender(<MirrorPage />);
  expect(backend.start).toHaveBeenCalledOnce();
  fireEvent.click(screen.getByText("Stop"));
  expect(new URLSearchParams(location.search).has("playing")).toBe(false);
});

test("manual playback is recorded for the next refresh", async () => {
  backend.ready = true;
  const view = render(<MirrorPage />);
  expect(backend.start).not.toHaveBeenCalled();
  await act(async () => fireEvent.click(screen.getByText("Start")));
  expect(new URLSearchParams(location.search).get("playing")).toBe("true");
  view.unmount();
  backend.start.mockClear();
  render(<MirrorPage />);
  await waitFor(() => expect(backend.start).toHaveBeenCalledOnce());
});

test("switching from media retains its frame while the camera opens", async () => {
  backend.ready = true;
  backend.file = true;
  let ready!: () => void;
  backend.cameraReady = new Promise<void>((resolve) => {
    ready = resolve;
  });
  render(<MirrorPage />);
  const toggle = screen.getByRole("button", { name: "Toggle Media Playback" });
  fireEvent.click(toggle);
  expect(backend.suspended).toHaveBeenLastCalledWith(true);
  expect(screen.getByTestId("renderer").textContent).toBe("frame");
  expect(toggle.getAttribute("aria-pressed")).toBe("true");
  await act(async () => ready());
  expect(toggle.getAttribute("aria-pressed")).toBe("false");
  expect(screen.getByTestId("renderer").textContent).toBe("frame");
});
