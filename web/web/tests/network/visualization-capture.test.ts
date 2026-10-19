import { describe, expect, it } from "vite-plus/test";
import { runInNewContext } from "node:vm";
import processorSource from "../../../packages/shared/src/audio/visualization-worklet.js?raw";

describe("continuous visualization PCM", () => {
  function processor() {
    const messages: Float32Array[] = [];
    let Processor: any;
    runInNewContext(processorSource, {
      Float32Array,
      AudioWorkletProcessor: class {
        port = {
          postMessage: (samples: Float32Array) =>
            messages.push(samples.slice()),
        };
      },
      registerProcessor: (_name: string, ctor: any) => {
        Processor = ctor;
      },
    });
    return { node: new Processor(), messages };
  }

  it("delivers every sample once in order without display callbacks", () => {
    const { node, messages } = processor();
    let cursor = 0;
    for (const size of [128, 256, 64, 512, 128, 32]) {
      for (let block = 0; block < 100; block++) {
        node.process([[Float32Array.from({ length: size }, () => cursor++)]]);
      }
    }
    const delivered = messages.flatMap((block) => [...block]);
    expect(messages.every((block) => block.length === 1024)).toBe(true);
    expect(delivered.length).toBe(Math.floor(cursor / 1024) * 1024);
    expect(delivered.every((value, index) => value === index)).toBe(true);
  });

  it("downmixes channels and retains partial blocks across quanta", () => {
    const { node, messages } = processor();
    node.process([
      [new Float32Array(512).fill(1), new Float32Array(512).fill(-0.5)],
    ]);
    expect(messages).toHaveLength(0);
    node.process([[]]);
    node.process([
      [new Float32Array(512).fill(0.5), new Float32Array(512).fill(-1)],
    ]);
    expect(messages).toHaveLength(1);
    expect([...messages[0]!]).toEqual([
      ...new Float32Array(512).fill(0.25),
      ...new Float32Array(512).fill(-0.25),
    ]);
  });
});
