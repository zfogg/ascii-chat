import { expect, test } from "vite-plus/test";
import { VideoFrameClock } from "../../../packages/shared/src/hooks/videoFrameClock";

for (const fps of [
  24000 / 1001,
  24,
  25,
  30000 / 1001,
  30,
  50,
  60000 / 1001,
  60,
  120,
]) {
  test(`preserves ${fps} FPS below the configured ceiling`, () => {
    const clock = new VideoFrameClock();
    let rendered = 0;
    for (let frame = 0; frame < fps * 10; frame++)
      if (clock.accept(frame / fps, 1000 / 60)) rendered++;
    expect(Math.abs(rendered - Math.min(fps, 60) * 10)).toBeLessThanOrEqual(1);
  });
}
test("a non-divisible FPS ceiling skips frames without slowing the movie", () => {
  const clock = new VideoFrameClock();
  let rendered = 0;
  for (let frame = 0; frame < 600; frame++)
    if (clock.accept(frame / 60, 1000 / 25)) rendered++;
  expect(rendered).toBe(250);
});
test("variable timestamps, gaps and backwards seeks do not trigger catch-up bursts", () => {
  const clock = new VideoFrameClock();
  for (const time of [0, 0.04, 0.1, 3, 0.02])
    expect(clock.accept(time, 1000 / 60)).toBe(true);
  expect(clock.accept(0.02, 1000 / 60)).toBe(false);
});
