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
test("source switches wait for C defaults and explicit choices persist", () => {
  const hook = renderHook(useRenderSettings);
  expect(hook.result.current[0].flipXExplicit).toBe(false);
  act(() =>
    hook.result.current[1]((s) => ({
      ...s,
      animationEnabled: true,
      animation: "waveform",
    })),
  );
  // Source selection alone does not calculate an orientation in JavaScript.
  expect(hook.result.current[0].flipX).toBe(true);
  expect(hook.result.current[0].flipXExplicit).toBe(false);
  act(() => hook.result.current[1]((s) => ({ ...s, flipX: false })));
  expect(hook.result.current[0].flipXExplicit).toBe(true);
  act(() => hook.result.current[1]((s) => ({ ...s, animationEnabled: false })));
  expect(hook.result.current[0].flipX).toBe(false);
  hook.unmount();
  const restored = renderHook(useRenderSettings);
  expect(restored.result.current[0].flipX).toBe(false);
  expect(restored.result.current[0].flipXExplicit).toBe(true);
});

test("automatic source URLs do not serialize an explicit orientation", () => {
  window.history.replaceState(
    null,
    "",
    "/mirror?animationEnabled=true&animation=waveform",
  );
  const hook = renderHook(useRenderSettings);
  expect(hook.result.current[0].flipXExplicit).toBe(false);
  expect(new URLSearchParams(window.location.search).has("flipX")).toBe(false);
});

test("engine readbacks retain C selection metadata while user edits choose explicitly", () => {
  const hook = renderHook(useRenderSettings);
  act(() =>
    hook.result.current[2]({
      ...hook.result.current[0],
      flipX: false,
      flipXExplicit: false,
    }),
  );
  expect(hook.result.current[0].flipX).toBe(false);
  expect(hook.result.current[0].flipXExplicit).toBe(false);
  expect(
    new URLSearchParams(window.location.search).has("horizontalFlip"),
  ).toBe(false);
  act(() => hook.result.current[1]((s) => ({ ...s, flipX: true })));
  expect(hook.result.current[0].flipXExplicit).toBe(true);
  act(() =>
    hook.result.current[2]({
      ...hook.result.current[0],
      flipX: false,
      flipXExplicit: true,
    }),
  );
  expect(hook.result.current[0].flipX).toBe(false);
  expect(hook.result.current[0].flipXExplicit).toBe(true);
  expect(
    new URLSearchParams(window.location.search).get("horizontalFlip"),
  ).toBe("off");
});
