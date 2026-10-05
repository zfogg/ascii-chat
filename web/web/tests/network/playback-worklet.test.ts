import workletSource from "../../src/audio/playback-worklet.js?raw";
import { runInNewContext } from "node:vm";
import { describe, expect, it, vi } from "vitest";

function processor() {
  let create!: new () => {
    port: { onmessage: (event: { data: unknown }) => void };
    process: (inputs: unknown[], outputs: Float32Array[][]) => boolean;
    count: number;
    underruns: number;
  };
  runInNewContext(workletSource, {
    AudioWorkletProcessor: class {
      port = { postMessage: vi.fn() };
    },
    registerProcessor: (_name: string, implementation: typeof create) => {
      create = implementation;
    },
    sampleRate: 48000,
  });
  return new create();
}

describe("audio-thread playback", () => {
  it("buffers network bursts and plays consecutive samples on the audio clock", () => {
    const playback = processor();
    const render = () => {
      const output = [new Float32Array(128), new Float32Array(128)];
      expect(playback.process([], [output])).toBe(true);
      return output;
    };
    const send = () =>
      playback.port.onmessage({
        data: {
          samples: new Float32Array(960).fill(0.25),
          channels: 1,
          rate: 48000,
        },
      });
    send();
    send();
    expect(render()[0]!.every((sample) => sample === 0)).toBe(true);
    send();
    render(); // fade-in
    for (let frame = 0; frame < 20; frame++) {
      expect(render()[0]!.every((sample) => sample === 0.25)).toBe(true);
    }
    expect(playback.underruns).toBe(0);
    send();
    send(); // delivery can be bursty without interrupting playback
    expect(render()[0]!.every((sample) => sample === 0.25)).toBe(true);
    expect(playback.underruns).toBe(0);
  });

  it("bounds backlog to recent audio instead of growing playback delay", () => {
    const playback = processor();
    playback.port.onmessage({
      data: {
        samples: new Float32Array(48000).fill(0.25),
        channels: 1,
        rate: 48000,
      },
    });
    expect(playback.count).toBe(9600);
  });
});
