import { afterEach, describe, expect, it, vi } from "vite-plus/test";
import source from "../../src/audio/capture-worklet.js?raw";

type Frame = { microphone: Float32Array; media: Float32Array };
interface Capture {
  port: { onmessage: (event: { data: { port: object } }) => void };
  process(inputs: Float32Array[][], outputs?: Float32Array[][]): boolean;
}
function capture(channels: 1 | 2, firstChannel = 0) {
  let Processor!: new (options: object) => Capture;
  vi.stubGlobal(
    "AudioWorkletProcessor",
    class {
      port = { onmessage: null };
    },
  );
  vi.stubGlobal(
    "registerProcessor",
    (_name: string, constructor: typeof Processor) => {
      Processor = constructor;
    },
  );
  new Function(source)();
  const frames: Frame[] = [];
  const processor = new Processor({
    processorOptions: {
      microphoneChannels: channels,
      firstChannel,
      frameSamples: 960,
    },
  });
  processor.port.onmessage({
    data: {
      port: {
        start() {},
        postMessage(frame: Frame) {
          frames.push(frame);
        },
      },
    },
  });
  return { processor, frames };
}
afterEach(() => vi.unstubAllGlobals());

describe("stereo capture audio clock", () => {
  it("records the selected pair while keeping the speaker output silent", () => {
    const { processor } = capture(2, 1);
    const block = (value: number) => new Float32Array(128).fill(value);
    const outputs = [[block(0)], [block(0), block(0)]];
    processor.process(
      [
        [block(0.9), block(0.2), block(-0.4)],
        [block(0.1), block(-0.3)],
      ],
      outputs,
    );
    expect(outputs[0]![0]!.every((sample) => sample === 0)).toBe(true);
    expect(
      outputs[1]![0]!.every((sample) => Math.abs(sample - 0.3) < 1e-6),
    ).toBe(true);
    expect(
      outputs[1]![1]!.every((sample) => Math.abs(sample + 0.7) < 1e-6),
    ).toBe(true);
  });
  it("keeps the selected interface pair separate from stereo media in a 20 ms frame", () => {
    const { processor, frames } = capture(2, 1);
    const block = (value: number) => new Float32Array(128).fill(value);
    for (let i = 0; i < 8; i++)
      processor.process([
        [block(0.9), block(0.2), block(-0.4)],
        [block(0.1), block(-0.3)],
      ]);
    expect(frames).toHaveLength(1);
    expect(frames[0]!.microphone).toHaveLength(1920);
    for (let i = 0; i < 960; i++) {
      expect(frames[0]!.microphone[i * 2]).toBeCloseTo(0.2);
      expect(frames[0]!.microphone[i * 2 + 1]).toBeCloseTo(-0.4);
      expect(frames[0]!.media[i * 2]).toBeCloseTo(0.1);
      expect(frames[0]!.media[i * 2 + 1]).toBeCloseTo(-0.3);
    }
  });
  it("centers mono speech and falls back to centered mono for a one-channel stereo device", () => {
    for (const channels of [1, 2] as const) {
      const { processor, frames } = capture(channels);
      for (let i = 0; i < 8; i++)
        processor.process([[new Float32Array(128).fill(0.25)], []]);
      expect(frames).toHaveLength(1);
      expect(
        Array.from(frames[0]!.microphone).every((sample) => sample === 0.25),
      ).toBe(true);
      expect(Array.from(frames[0]!.media).every((sample) => sample === 0)).toBe(
        true,
      );
    }
  });
  it("captures stereo media while the microphone is off", () => {
    const { processor, frames } = capture(1);
    for (let i = 0; i < 8; i++)
      processor.process([
        [],
        [new Float32Array(128).fill(0.4), new Float32Array(128).fill(-0.2)],
      ]);
    expect(frames).toHaveLength(1);
    expect(frames[0]!.microphone.every((sample) => sample === 0)).toBe(true);
    expect(frames[0]!.media[0]).toBeCloseTo(0.4);
    expect(frames[0]!.media[1]).toBeCloseTo(-0.2);
  });
  it("rejects an unavailable input instead of capturing a different microphone", () => {
    const { processor } = capture(1, 2);
    expect(() => processor.process([[new Float32Array(128)], []])).toThrow(
      "unavailable",
    );
  });
  it("rejects an incomplete pair on a multichannel interface", () => {
    const { processor } = capture(2, 1);
    expect(() =>
      processor.process([[new Float32Array(128), new Float32Array(128)], []]),
    ).toThrow("stereo pair is unavailable");
  });
});
