import { afterEach, describe, expect, it, vi } from "vite-plus/test";
import {
  createBandwidthSampler,
  formatBandwidth,
  recordDownload,
  recordUpload,
} from "../../src/network/bandwidth";
import { WebRTCBridge } from "../../src/network/WebRTCBridge";

afterEach(() => vi.restoreAllMocks());
describe("bandwidth", () => {
  it("uses elapsed time and returns to zero when traffic stops", () => {
    const clock = vi.spyOn(performance, "now").mockReturnValue(0);
    const sample = createBandwidthSampler();
    recordUpload(4000);
    recordDownload(10000);
    clock.mockReturnValue(2000);
    expect(sample()).toEqual({ upload: 2000, download: 5000 });
    clock.mockReturnValue(3000);
    expect(sample()).toEqual({ upload: 0, download: 0 });
  });
  it("does not count queued or replaced WebRTC frames and counts received fragments once", () => {
    const clock = vi.spyOn(performance, "now").mockReturnValue(0);
    const sample = createBandwidthSampler();
    const channel = {
      readyState: "open",
      bufferedAmount: 300000,
      send: vi.fn(),
      close: vi.fn(),
      onmessage: null,
      onbufferedamountlow: null,
    } as unknown as RTCDataChannel;
    const bridge = new WebRTCBridge(channel, vi.fn(), vi.fn(), 16);
    const packet = new Uint8Array(22);
    bridge.send(packet, true);
    bridge.send(packet, true);
    clock.mockReturnValue(1000);
    expect(sample().upload).toBe(0);
    Object.assign(channel, { bufferedAmount: 0 });
    channel.onbufferedamountlow?.call(channel, new Event("bufferedamountlow"));
    channel.onmessage?.call(
      channel,
      new MessageEvent("message", { data: packet.slice(0, 10).buffer }),
    );
    channel.onmessage?.call(
      channel,
      new MessageEvent("message", { data: packet.slice(10).buffer }),
    );
    clock.mockReturnValue(2000);
    expect(sample()).toEqual({ upload: 22, download: 22 });
    expect(channel.send).toHaveBeenCalledTimes(2);
    bridge.close();
  });
  it("formats decimal byte rates", () => {
    expect(formatBandwidth(0)).toBe("0 B/s");
    expect(formatBandwidth(1500)).toBe("1.5 kB/s");
    expect(formatBandwidth(2500000)).toBe("2.5 MB/s");
  });
});
