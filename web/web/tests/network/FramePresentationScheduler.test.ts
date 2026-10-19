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
