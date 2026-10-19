import { act, renderHook } from "@testing-library/react";
import { beforeEach, expect, test } from "vite-plus/test";
import { useRenderSettings } from "../src/hooks/useRenderSettings";
beforeEach(() => window.history.replaceState(null, "", "/mirror"));
test("bare Face Follow query flags persist independently and explicit false disables framing", () => {
  window.history.replaceState(null, "", "/mirror?faceFollow&faceFollowDebug");
  const hook = renderHook(useRenderSettings);
  expect(hook.result.current[0].faceFollow).toBe(true);
  expect(hook.result.current[0].faceFollowDebug).toBe(true);
  act(() =>
    hook.result.current[1]((settings) => ({ ...settings, faceFollow: false })),
  );
  expect(new URLSearchParams(window.location.search).has("faceFollow")).toBe(
    false,
  );
  expect(
    new URLSearchParams(window.location.search).get("faceFollowDebug"),
  ).toBe("true");
  hook.unmount();
  window.history.replaceState(
    null,
    "",
    "/mirror?faceFollow=false&faceFollowDebug",
  );
  const restored = renderHook(useRenderSettings);
  expect(restored.result.current[0].faceFollow).toBe(false);
  expect(restored.result.current[0].faceFollowDebug).toBe(true);
});
test("automatic defaults follow modes until an explicit choice, which survives switching and reload", () => {
  const hook = renderHook(useRenderSettings);
  const mode = (
    animation: "matrix" | "waveform" | "fft",
    animationEnabled = true,
  ) =>
    act(() =>
      hook.result.current[1]((s) => ({ ...s, animation, animationEnabled })),
    );
  const flip = () =>
    act(() => hook.result.current[1]((s) => ({ ...s, flipX: !s.flipX })));
  expect(hook.result.current[0].flipX).toBe(true);
  mode("waveform");
  expect(hook.result.current[0].flipX).toBe(false);
  mode("matrix");
  expect(hook.result.current[0].flipX).toBe(true);
  mode("waveform");
  flip();
  for (const animation of ["matrix", "fft", "waveform"] as const) {
    mode(animation);
    expect(hook.result.current[0].flipX).toBe(true);
  }
  mode("matrix", false);
  expect(hook.result.current[0].flipX).toBe(true);
  flip();
  mode("waveform");
  mode("matrix", false);
  expect(hook.result.current[0].flipX).toBe(false);
  hook.unmount();
  const restored = renderHook(useRenderSettings);
  expect(restored.result.current[0].flipX).toBe(false);
  act(() =>
    restored.result.current[1]((s) => ({
      ...s,
      animationEnabled: true,
      animation: "matrix",
    })),
  );
  expect(restored.result.current[0].flipX).toBe(false);
});
test("automatic waveform default remains automatic after reload", () => {
  window.history.replaceState(
    null,
    "",
    "/mirror?animationEnabled=true&animation=waveform",
  );
  const first = renderHook(useRenderSettings);
  expect(first.result.current[0].flipX).toBe(false);
  first.unmount();
  const second = renderHook(useRenderSettings);
  act(() =>
    second.result.current[1]((s) => ({ ...s, animationEnabled: false })),
  );
  expect(second.result.current[0].flipX).toBe(true);
});
test("existing explicit waveform links carry the choice into webcam", () => {
  window.history.replaceState(
    null,
    "",
    "/mirror?animationEnabled=true&animation=waveform&waveformFlipX=true",
  );
  const { result } = renderHook(useRenderSettings);
  act(() => result.current[1]((s) => ({ ...s, animationEnabled: false })));
  expect(result.current[0].flipX).toBe(true);
});
