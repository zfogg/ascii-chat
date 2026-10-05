import { afterEach, expect, it, vi } from "vite-plus/test";
import { useRenderLoop } from "../../src/hooks/useRenderLoop";

vi.mock("react", () => ({
  useCallback: (callback: unknown) => callback,
  useEffect: () => {},
  useRef: (current: unknown) => ({ current }),
}));

afterEach(() => vi.unstubAllGlobals());

it("keeps every 60 Hz refresh despite rounded RAF timestamps", () => {
  let next: FrameRequestCallback = () => {};
  vi.stubGlobal("requestAnimationFrame", (callback: FrameRequestCallback) => {
    next = callback;
    return 1;
  });
  const render = vi.fn();
  const clock = { current: 0 };
  const loop = useRenderLoop(render, { current: 1000 / 60 }, clock);
  loop.startRenderLoop();
  const start = clock.current;
  for (let frame = 1; frame <= 600; frame++) {
    next(start + Math.round((frame * 1000) / 60));
  }
  expect(render).toHaveBeenCalledTimes(600);
  expect(clock.current - start).toBeCloseTo(10000);
});

it("limits 120 Hz refreshes to 30 FPS without a catch-up burst after a stall", () => {
  let next: FrameRequestCallback = () => {};
  vi.stubGlobal("requestAnimationFrame", (callback: FrameRequestCallback) => {
    next = callback;
    return 1;
  });
  const render = vi.fn();
  const clock = { current: 0 };
  useRenderLoop(render, { current: 1000 / 30 }, clock).startRenderLoop();
  const start = clock.current;
  for (let frame = 1; frame <= 120; frame++) next(start + (frame * 1000) / 120);
  expect(render).toHaveBeenCalledTimes(30);
  next(start + 5000);
  expect(render).toHaveBeenCalledTimes(31);
  next(start + 5001);
  expect(render).toHaveBeenCalledTimes(31);
});
