import { act, renderHook } from "@testing-library/react";
import { beforeEach, expect, test } from "vite-plus/test";
import { useRenderSettings } from "../src/hooks/useRenderSettings";
beforeEach(() => window.history.replaceState(null, "", "/mirror"));
test("remembers each mode across repeated flips and reload", () => {
  const hook = renderHook(useRenderSettings);
  const mode = (animation: "matrix" | "waveform" | "fft", enabled = true) =>
    act(() =>
      hook.result.current[1]((s) => ({
        ...s,
        animation,
        animationEnabled: enabled,
      })),
    );
  const flip = () =>
    act(() => hook.result.current[1]((s) => ({ ...s, flipX: !s.flipX })));
  expect(hook.result.current[0].flipX).toBe(true);
  flip();
  mode("matrix");
  expect(hook.result.current[0].flipX).toBe(true);
  flip();
  flip();
  flip();
  mode("waveform");
  expect(hook.result.current[0].flipX).toBe(false);
  flip();
  mode("fft");
  expect(hook.result.current[0].flipX).toBe(false);
  flip();
  flip();
  mode("matrix");
  expect(hook.result.current[0].flipX).toBe(false);
  mode("waveform");
  expect(hook.result.current[0].flipX).toBe(true);
  mode("matrix", false);
  expect(hook.result.current[0].flipX).toBe(false);
  hook.unmount();
  const restored = renderHook(useRenderSettings);
  expect(restored.result.current[0].flipX).toBe(false);
  act(() =>
    restored.result.current[1]((s) => ({
      ...s,
      animationEnabled: true,
      animation: "waveform",
    })),
  );
  expect(restored.result.current[0].flipX).toBe(true);
});
test("legacy flipX affects only the linked mode", () => {
  window.history.replaceState(
    null,
    "",
    "/mirror?animationEnabled=true&animation=waveform&flipX=true",
  );
  const { result } = renderHook(useRenderSettings);
  expect(result.current[0].flipX).toBe(true);
  act(() => result.current[1]((s) => ({ ...s, animation: "fft" })));
  expect(result.current[0].flipX).toBe(false);
});
