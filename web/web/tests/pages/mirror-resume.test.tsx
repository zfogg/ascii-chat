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
import type { BinarySettingsConfig } from "../../src/components/shared/BinarySettings";
import { DEFAULT_SETTINGS } from "../../src/utils/defaultSettings";

const backend = vi.hoisted(() => ({
  applySettings: vi.fn(),
  ready: false,
  file: false,
  loading: false,
  cameraReady: null as Promise<void> | null,
  suspended: vi.fn(),
  start: vi.fn(),
  stop: vi.fn(),
}));
vi.mock("@ascii-chat/shared/hooks", () => ({
  useMirrorRenderLoop: vi.fn(),
  useTestPattern: () => ({ enabled: false }),
  useCanvasCapture: () => ({ captureFrame: vi.fn() }),
}));
vi.mock("@ascii-chat/shared/wasm", () => ({
  getMirrorModule: () => null,
  initMirrorWasm: vi.fn(),
  createWasmModule: vi.fn(),
  isWasmReady: () => true,
}));
vi.mock("../../src/components/shared/BinarySettings", () => ({
  BinarySettings: ({
    config,
    onChange,
  }: {
    config: BinarySettingsConfig;
    onChange: (config: BinarySettingsConfig) => void;
  }) => (
    <>
      <button
        aria-pressed={!!config.flipX}
        onClick={() => onChange({ ...config, flipX: !config.flipX })}
      >
        Horizontal
      </button>
      <button
        aria-pressed={!!config.flipY}
        onClick={() => onChange({ ...config, flipY: !config.flipY })}
      >
        Vertical
      </button>
    </>
  ),
}));
vi.mock("@ascii-chat/shared/components/AsciiRenderer", () => ({
  AsciiRenderer: ({ uiLoading }: { uiLoading: boolean }) => (
    <div data-testid="renderer">{uiLoading ? "splash" : "frame"}</div>
  ),
}));
vi.mock("../../src/components/AsciiChatWebHead", () => ({
  AsciiChatWebHead: () => null,
}));
vi.mock("../../src/components/VideoUploadModal", () => ({
  VideoUploadModal: () => null,
}));
vi.mock("../../src/components/PageLayout", () => ({
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
}));
vi.mock("../../src/components/ModeHeader", () => ({
  ModeHeader: ({
    settingsPanel,
    controlBar,
  }: {
    settingsPanel: React.ReactNode;
    controlBar: {
      onStartWebcam: () => void;
      onStopWebcam: () => void;
      statusControls: React.ReactNode;
      fps: number;
    };
  }) => (
    <>
      {settingsPanel}
      <button onClick={controlBar.onStartWebcam}>Start</button>
      <button onClick={controlBar.onStopWebcam}>Stop</button>
      {controlBar.statusControls}
      <span data-testid="fps">{controlBar.fps}</span>
    </>
  ),
}));
vi.mock("../../src/hooks/useWasmOptions", () => ({
  applyMirrorWasmSettings: backend.applySettings,
  setMirrorWasmDimensions: vi.fn(),
}));
vi.mock("../../src/hooks/useClientLike", async (importOriginal) => ({
  ...(await importOriginal<typeof import("../../src/hooks/useClientLike")>()),
  useClientLike: () => {
    const [running, setRunning] = useState(backend.file);
    const [source, setSource] = useState<symbol | null>(
      backend.file ? MediaSourceType.FILE : null,
    );
    const [settings, setSettings] = useState(DEFAULT_SETTINGS);
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
      settings,
      setSettings,
      terminalDimensions: {
        cols: backend.ready ? 80 : 0,
        rows: backend.ready ? 40 : 0,
      },
      wasmInitialized: backend.ready,
      setFps: vi.fn(),
      fps: 66,
      stopWebcam: useCallback(() => {
        backend.stop();
        setRunning(false);
      }, []),
    };
  },
}));
vi.mock("../../src/hooks/useMirrorWebcam", () => ({
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
    loading: backend.loading,
    setSuspended: backend.suspended,
    stop: stopWebcam,
  }),
}));
beforeEach(() => {
  vi.clearAllMocks();
  backend.ready = false;
  backend.file = false;
  backend.loading = false;
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
  const toggle = screen.getByRole("button", { name: "Toggle Media" });
  fireEvent.click(toggle);
  expect(backend.suspended).toHaveBeenLastCalledWith(true);
  expect(screen.getByTestId("renderer").textContent).toBe("frame");
  expect(toggle.getAttribute("aria-pressed")).toBe("true");
  await act(async () => ready());
  expect(toggle.getAttribute("aria-pressed")).toBe("false");
  expect(screen.getByTestId("renderer").textContent).toBe("frame");
});

test("loading media does not report splash animation frames as video FPS", () => {
  backend.ready = true;
  backend.loading = true;
  render(<MirrorPage />);
  expect(screen.getByTestId("fps").textContent).toBe("0");
});

test("media flips start off, reach WASM and remain independent of webcam flips", async () => {
  backend.ready = true;
  backend.file = true;
  render(<MirrorPage />);
  const horizontal = screen.getByRole("button", { name: "Horizontal" });
  const vertical = screen.getByRole("button", { name: "Vertical" });
  expect(horizontal.getAttribute("aria-pressed")).toBe("false");
  expect(vertical.getAttribute("aria-pressed")).toBe("false");
  fireEvent.click(vertical);
  expect(backend.applySettings).toHaveBeenLastCalledWith(
    expect.objectContaining({ flipX: false, flipY: true }),
  );
  fireEvent.click(horizontal);
  expect(backend.applySettings).toHaveBeenLastCalledWith(
    expect.objectContaining({ flipX: true, flipY: true }),
  );
  await act(async () =>
    fireEvent.click(screen.getByRole("button", { name: "Toggle Media" })),
  );
  expect(vertical.getAttribute("aria-pressed")).toBe("false");
  expect(backend.applySettings).toHaveBeenLastCalledWith(
    expect.objectContaining({
      flipX: DEFAULT_SETTINGS.flipX,
      flipY: DEFAULT_SETTINGS.flipY,
    }),
  );
  fireEvent.click(screen.getByRole("button", { name: "Toggle Media" }));
  expect(vertical.getAttribute("aria-pressed")).toBe("true");
  expect(horizontal.getAttribute("aria-pressed")).toBe("true");
  expect(backend.applySettings).toHaveBeenLastCalledWith(
    expect.objectContaining({ flipX: true, flipY: true }),
  );
});

test("playing=true navigation starts the existing Mirror page", async () => {
  backend.ready = true;
  render(<MirrorPage />);
  expect(backend.start).not.toHaveBeenCalled();
  act(() => {
    history.replaceState({}, "", "/mirror?playing=true");
    window.dispatchEvent(new PopStateEvent("popstate"));
  });
  await waitFor(() => expect(backend.start).toHaveBeenCalledOnce());
});
