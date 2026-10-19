import { act, cleanup, renderHook } from "@testing-library/react";
import { afterEach, expect, it, vi } from "vite-plus/test";
import { useCameraExposure } from "../../src/hooks/useCameraExposure";

vi.mock("../../src/utils/cameraExposure", () => ({
  supportsFastExposure: () => true,
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
