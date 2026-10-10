import { act, cleanup, renderHook, waitFor } from "@testing-library/react";
import { afterEach, beforeEach, expect, it, vi } from "vite-plus/test";
import {
  useVideoEncodings,
  VIDEO_ENCODING_STORAGE_KEY,
  VIDEO_ENCODING_CHANGED,
} from "../../src/hooks/useVideoEncodings";

const support = vi.fn(async (config: VideoEncoderConfig) => ({
  supported: true,
  config,
}));
beforeEach(() => {
  window.localStorage.clear();
  window.history.replaceState(null, "", "/client");
  support.mockImplementation(async (config) => ({ supported: true, config }));
  vi.stubGlobal(
    "VideoEncoder",
    class {
      static isConfigSupported = support;
    },
  );
  vi.stubGlobal("VideoFrame", class {});
});
afterEach(() => {
  cleanup();
  vi.unstubAllGlobals();
});

it("offers codecs in preference order and selects the best without writing preferences", async () => {
  const { result } = renderHook(() => useVideoEncodings());
  await waitFor(() => expect(result.current.loading).toBe(false));
  expect(result.current.supportedEncodings).toEqual(["hevc", "h264", "raw"]);
  expect(result.current.selectedEncoding).toBe("hevc");
  expect(window.localStorage.getItem(VIDEO_ENCODING_STORAGE_KEY)).toBeNull();
  expect(window.location.search).toBe("");
});
it("uses stored choices unless a URL overrides them, without overwriting storage", async () => {
  window.localStorage.setItem(VIDEO_ENCODING_STORAGE_KEY, "raw");
  window.history.replaceState(null, "", "/client?encoding=H.264");
  const { result } = renderHook(() => useVideoEncodings());
  await waitFor(() => expect(result.current.loading).toBe(false));
  expect(result.current.selectedEncoding).toBe("h264");
  expect(window.localStorage.getItem(VIDEO_ENCODING_STORAGE_KEY)).toBe("raw");
  act(() => {
    window.history.replaceState(null, "", "/client");
    window.dispatchEvent(new PopStateEvent("popstate"));
  });
  expect(result.current.selectedEncoding).toBe("raw");
});
it("updates the URL, storage and other hook consumers only on explicit selection", async () => {
  const first = renderHook(() => useVideoEncodings());
  const second = renderHook(() => useVideoEncodings());
  await waitFor(() => expect(first.result.current.loading).toBe(false));
  act(() => first.result.current.selectEncoding("h264"));
  expect(window.localStorage.getItem(VIDEO_ENCODING_STORAGE_KEY)).toBe("h264");
  expect(new URLSearchParams(window.location.search).get("encoding")).toBe(
    "H.264",
  );
  expect(second.result.current.selectedEncoding).toBe("h264");
});
it("filters unsupported codecs and defaults to H.264 then raw", async () => {
  support.mockImplementation(async (config) => ({
    supported: config.codec.startsWith("avc"),
    config,
  }));
  const { result, rerender } = renderHook(
    ({ fps }) => useVideoEncodings({ fps }),
    { initialProps: { fps: 30 } },
  );
  await waitFor(() => expect(result.current.loading).toBe(false));
  expect(result.current.supportedEncodings).toEqual(["h264", "raw"]);
  expect(result.current.bestEncoding).toBe("h264");
  support.mockImplementation(async (config) => ({ supported: false, config }));
  rerender({ fps: 60 });
  await waitFor(() =>
    expect(result.current.supportedEncodings).toEqual(["raw"]),
  );
  expect(result.current.selectedEncoding).toBe("raw");
  expect(() => result.current.selectEncoding("hevc")).toThrow(/unsupported/);
});
it("does not silently change an unsupported explicit preference", async () => {
  support.mockImplementation(async (config) => ({ supported: false, config }));
  window.localStorage.setItem(VIDEO_ENCODING_STORAGE_KEY, "h264");
  const { result } = renderHook(() => useVideoEncodings());
  await waitFor(() => expect(result.current.loading).toBe(false));
  expect(result.current.selectedEncoding).toBe("h264");
  await expect(
    result.current.createEncoder(320, 240, 30, vi.fn()),
  ).rejects.toThrow(/H.264.*unsupported/);
  expect(window.localStorage.getItem(VIDEO_ENCODING_STORAGE_KEY)).toBe("h264");
});
it("rejects invalid URL values but ignores obsolete stored values", async () => {
  window.localStorage.setItem(VIDEO_ENCODING_STORAGE_KEY, "obsolete");
  const { result } = renderHook(() => useVideoEncodings());
  await waitFor(() => expect(result.current.loading).toBe(false));
  expect(result.current.preference).toBe("auto");
  act(() => {
    window.history.replaceState(null, "", "/client?encoding=bad");
    window.dispatchEvent(new Event(VIDEO_ENCODING_CHANGED));
  });
  expect(() => result.current.validate()).toThrow(/Unknown encoding/);
  expect(window.localStorage.getItem(VIDEO_ENCODING_STORAGE_KEY)).toBe(
    "obsolete",
  );
});
