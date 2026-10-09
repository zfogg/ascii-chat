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
  start: vi.fn(),
  stop: vi.fn(),
}));
vi.mock("@ascii-chat/shared/hooks", () => ({
  useTestPattern: () => ({ enabled: false }),
}));
vi.mock("@ascii-chat/shared/wasm", () => ({
  getMirrorModule: () => null,
  initMirrorWasm: vi.fn(),
  isWasmReady: () => true,
}));
vi.mock("../../src/wasm/dist/mirror.js", () => ({ default: vi.fn() }));
vi.mock("../../src/components", () => ({
  BinarySettings: () => null,
  AsciiRenderer: () => null,
  AsciiChatWebHead: () => null,
  VideoUploadModal: () => null,
  PageLayout: ({ header }: { header: React.ReactNode }) => header,
  ModeHeader: ({
    controlBar,
  }: {
    controlBar: { onStartWebcam: () => void; onStopWebcam: () => void };
  }) => (
    <>
      <button onClick={controlBar.onStartWebcam}>Start</button>
      <button onClick={controlBar.onStopWebcam}>Stop</button>
    </>
  ),
}));
vi.mock("../../src/hooks", () => ({
  applyMirrorWasmSettings: vi.fn(),
  setMirrorWasmDimensions: vi.fn(),
  useMirrorRenderLoop: vi.fn(),
  useClientLike: () => {
    const [running, setRunning] = useState(false);
    const [source, setSource] = useState<symbol | null>(null);
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
      setMediaSource(MediaSourceType.WEBCAM);
      setIsWebcamRunning(true);
    },
    startVideoFile: vi.fn(),
  }),
}));
beforeEach(() => {
  vi.clearAllMocks();
  backend.ready = false;
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
