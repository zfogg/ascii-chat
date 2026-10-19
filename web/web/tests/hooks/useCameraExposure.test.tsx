import { act, cleanup, renderHook, waitFor } from "@testing-library/react";
import { afterEach, expect, it, vi } from "vite-plus/test";
import {
  setFastExposure,
  supportsFastExposure,
} from "../../src/utils/cameraExposure";
import { useCameraExposure } from "../../src/hooks/useCameraExposure";

vi.mock("../../src/utils/cameraExposure", () => ({
  supportsFastExposure: vi.fn(() => true),
  setFastExposure: vi.fn(async () => {}),
}));
afterEach(() => {
  cleanup();
  window.history.replaceState(null, "", "/");
});

it("defaults off, ignores the retired parameter, and shares the canonical device preference", () => {
  window.history.replaceState(null, "", "/mirror?fastExposure=true");
  const first = renderHook(() => useCameraExposure(null));
  const second = renderHook(() => useCameraExposure(null));
  expect(first.result.current.enabled).toBe(false);
  act(() => first.result.current.select(true));
  expect(second.result.current.enabled).toBe(true);
  expect(new URLSearchParams(location.search).get("fastCameraExposure")).toBe(
    "true",
  );
  act(() => second.result.current.select(false));
  expect(first.result.current.enabled).toBe(false);
  expect(new URLSearchParams(location.search).has("fastCameraExposure")).toBe(
    false,
  );
});

it("reads explicit true and false URL values", () => {
  window.history.replaceState(null, "", "/mirror?fastCameraExposure=true");
  const hook = renderHook(() => useCameraExposure(null));
  expect(hook.result.current.enabled).toBe(true);
  act(() => {
    window.history.replaceState(null, "", "/mirror?fastCameraExposure=false");
    window.dispatchEvent(new PopStateEvent("popstate"));
  });
  expect(hook.result.current.enabled).toBe(false);
});

it.each([null, {} as MediaStreamTrack])(
  "clears a stale error when the next track is absent or unsupported: %s",
  async (nextTrack) => {
    vi.mocked(supportsFastExposure).mockReturnValue(true);
    vi.mocked(setFastExposure).mockRejectedValueOnce(
      new Error("old camera failed"),
    );
    const hook = renderHook(
      ({ track }: { track: MediaStreamTrack | null }) =>
        useCameraExposure(track),
      { initialProps: { track: {} as MediaStreamTrack | null } },
    );
    await waitFor(() =>
      expect(hook.result.current.error).toBe("old camera failed"),
    );
    vi.mocked(supportsFastExposure).mockReturnValue(false);
    hook.rerender({ track: nextTrack });
    expect(hook.result.current.error).toBe("");
  },
);
