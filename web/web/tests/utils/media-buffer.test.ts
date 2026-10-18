import { expect, test } from "vite-plus/test";
import { startupBuffer, bufferedAhead } from "../../src/utils/mediaBuffer";
test("fast conversion starts after three seconds instead of the whole movie", () => {
  expect(startupBuffer(120, 4, 1)).toBe(3);
});
test("slow conversion reserves enough for the remaining timeline with headroom", () => {
  const required = startupBuffer(120, 10, 20);
  expect(required).toBe(78);
  expect((120 - required) / 0.5).toBeLessThan(120);
});
test("short media and stalled conversion are bounded by the file duration", () => {
  expect(startupBuffer(2, 1, 1)).toBe(2);
  expect(startupBuffer(120, 0, 10)).toBe(120);
});
test("buffer ahead counts only the contiguous range containing playback", () => {
  const ranges = {
    length: 2,
    start: (i: number) => [0, 8][i]!,
    end: (i: number) => [5, 12][i]!,
  };
  expect(bufferedAhead(ranges, 3)).toBe(2);
  expect(bufferedAhead(ranges, 6)).toBe(0);
  expect(bufferedAhead(ranges, 10)).toBe(2);
});
