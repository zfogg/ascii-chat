import { describe, expect, it } from "vite-plus/test";
import { UploadQuality } from "../../src/network/UploadQuality";

describe("upload feedback policy", () => {
  it("cuts network bitrate and spatial/temporal load, then recovers gradually to ceilings", () => {
    const quality = new UploadQuality();
    const ceiling = quality.target(1280, 720, 30);
    for (let t = 0; t < 10000; t += 500)
      quality.sample(t, { bufferedBytes: 300000 });
    const collapsed = quality.target(1280, 720, 30);
    expect(quality.reason).toBe("network");
    expect(collapsed.width).toBe(320);
    expect(collapsed.fps).toBe(6);
    expect(collapsed.bitrate).toBeLessThan(ceiling.bitrate / 10);
    for (let t = 10000; t < 15000; t += 500)
      quality.sample(t, { bufferedBytes: 0 });
    expect(quality.target(1280, 720, 30)).toEqual(collapsed);
    quality.sample(15000, { bufferedBytes: 0 });
    expect(quality.target(1280, 720, 30).bitrate).toBeGreaterThan(
      collapsed.bitrate,
    );
    expect(quality.target(1280, 720, 30).bitrate).toBeLessThan(ceiling.bitrate);
    for (let t = 15500; t < 150000; t += 500)
      quality.sample(t, { bufferedBytes: 0 });
    expect(quality.target(1280, 720, 30)).toEqual(ceiling);
    expect(quality.target(160, 120, 3).fps).toBeLessThanOrEqual(3);
  });
  it("distinguishes encoder pressure from bandwidth and observes stale/drop feedback", () => {
    const quality = new UploadQuality();
    const initial = quality.target(640, 480, 30);
    for (let t = 0; t < 2000; t += 500)
      quality.sample(t, { bufferedBytes: 0 }, 2);
    expect(quality.reason).toBe("encoder");
    expect(quality.target(640, 480, 30).bitrate).toBe(initial.bitrate);
    expect(quality.target(640, 480, 30).fps).toBeLessThan(initial.fps);
    quality.sample(2500, { bufferedBytes: 0, oldestFrameAgeMs: 80 });
    expect(quality.reason).toBe("network");
    const bitrate = quality.target(640, 480, 30).bitrate;
    quality.sample(3000, { bufferedBytes: 0, droppedFrames: 1 });
    expect(quality.target(640, 480, 30).bitrate).toBeLessThan(bitrate);
  });
});
