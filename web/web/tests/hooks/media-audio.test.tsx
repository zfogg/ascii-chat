import { act, cleanup, renderHook } from "@testing-library/react";
import { afterEach, beforeEach, expect, test, vi } from "vite-plus/test";
import { AudioPipeline } from "../../src/audio/AudioPipeline";
import { useMediaAudio } from "../../src/hooks/useMediaAudio";

vi.mock("../../src/audio/AudioPipeline", () => ({ AudioPipeline: vi.fn() }));
const pipeline = {
  captureActive: false,
  startCapture: vi.fn(async () => {
    pipeline.captureActive = true;
  }),
  stopCapture: vi.fn(() => {
    pipeline.captureActive = false;
  }),
  enablePlayback: vi.fn(async () => {}),
  disablePlayback: vi.fn(),
  close: vi.fn(),
};
beforeEach(() => {
  vi.clearAllMocks();
  pipeline.captureActive = false;
  vi.mocked(AudioPipeline).mockImplementation(function () {
    return pipeline as unknown as AudioPipeline;
  });
});
afterEach(cleanup);

test("unmount cancels a microphone preparation granted later without reopening capture", async () => {
  let grant!: (value: boolean) => void;
  const { result, unmount } = renderHook(() => useMediaAudio(() => ({})));
  let pending!: Promise<void>;
  act(() => {
    pending = result.current.toggleMicrophone(
      () =>
        new Promise((resolve) => {
          grant = resolve;
        }),
    );
  });
  unmount();
  await act(async () => {
    grant(true);
    await pending;
  });
  expect(AudioPipeline).not.toHaveBeenCalled();
  expect(pipeline.startCapture).not.toHaveBeenCalled();
});

test("speaker toggles keep capture alive and close resets both shared controls", async () => {
  const { result } = renderHook(() => useMediaAudio(() => ({})));
  await act(() => result.current.toggleMicrophone());
  expect(result.current.micEnabled).toBe(true);
  await act(() => result.current.toggleSpeakers());
  expect(result.current.audioEnabled).toBe(true);
  await act(() => result.current.toggleSpeakers());
  expect(result.current.audioEnabled).toBe(false);
  expect(result.current.micEnabled).toBe(true);
  expect(pipeline.stopCapture).not.toHaveBeenCalled();
  expect(AudioPipeline).toHaveBeenCalledOnce();
  act(() => result.current.closeAudio());
  expect(pipeline.close).toHaveBeenCalledOnce();
  expect(result.current.audioRef.current).toBeNull();
  expect(result.current.micEnabled).toBe(false);
});
