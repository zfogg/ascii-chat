import { afterEach, expect, it, vi } from "vite-plus/test";
import { FramePresentationScheduler } from "../../src/network/FramePresentationScheduler";
afterEach(() => vi.useRealTimers());
it.each([24, 30, 60])(
  "preserves %i FPS arrivals without a display-clock sampling step",
  async (fps) => {
    vi.useFakeTimers({ toFake: ["setTimeout", "clearTimeout", "performance"] });
    const render = vi.fn();
    const scheduler = new FramePresentationScheduler(() => 1000 / fps, render);
    for (let index = 0; index < fps; index++) {
      scheduler.request();
      await vi.advanceTimersByTimeAsync(1000 / fps);
    }
    expect(render).toHaveBeenCalledTimes(fps);
    scheduler.close();
  },
);
it("coalesces a burst, respects the selected FPS and cancels pending work", async () => {
  vi.useFakeTimers({ toFake: ["setTimeout", "clearTimeout", "performance"] });
  const render = vi.fn();
  const scheduler = new FramePresentationScheduler(() => 1000 / 30, render);
  for (let i = 0; i < 100; i++) scheduler.request();
  expect(render).toHaveBeenCalledOnce();
  await vi.advanceTimersByTimeAsync(34);
  expect(render).toHaveBeenCalledTimes(2);
  scheduler.request();
  scheduler.close();
  await vi.advanceTimersByTimeAsync(1000);
  expect(render).toHaveBeenCalledTimes(2);
});

it("does not accumulate timer lateness into a lower presentation rate", () => {
  let now = 0;
  let pending: { at: number; callback: () => void } | undefined;
  vi.spyOn(performance, "now").mockImplementation(() => now);
  vi.spyOn(globalThis, "setTimeout").mockImplementation(((
    callback: () => void,
    delay: number,
  ) => {
    pending = { at: now + delay + 2, callback };
    return 1;
  }) as typeof setTimeout);
  const render = vi.fn();
  const scheduler = new FramePresentationScheduler(() => 1000 / 60, render);
  for (let frame = 0; frame < 120; frame++) {
    const arrival = (frame * 1000) / 60 + (frame % 2 ? 0.7 : -0.7);
    if (pending && pending.at <= arrival) {
      now = pending.at;
      const callback = pending.callback;
      pending = undefined;
      callback();
    }
    now = arrival;
    scheduler.request();
  }
  expect(render.mock.calls.length).toBeGreaterThanOrEqual(118);
  scheduler.close();
  vi.restoreAllMocks();
});
