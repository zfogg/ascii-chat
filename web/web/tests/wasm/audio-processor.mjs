/** Real C/WASM parity check. Start the app, then pass its URL and Python fixture directory. */
import { chromium } from "@playwright/test";
import { readFileSync } from "node:fs";
import { resolve } from "node:path";

const [url = "http://127.0.0.1:3000", directory] = process.argv.slice(2);
if (!directory)
  throw new Error(
    "Usage: node tests/wasm/audio-processor.mjs URL FIXTURE_DIRECTORY",
  );
const floats = (name) => {
  const bytes = readFileSync(resolve(directory, name));
  return Array.from(
    new Float32Array(bytes.buffer, bytes.byteOffset, bytes.length / 4),
  );
};
const browser = await chromium.launch({ headless: true });
try {
  const page = await browser.newPage();
  await page.goto(url);
  const result = await page.evaluate(
    async ({ input, expected }) => {
      const { default: create } = await import("/wasm/client.js");
      const module = await create({
        locateFile: (file) => new URL(`/wasm/${file}`, location.origin).href,
      });
      const processor = module._wasm_audio_processor_create(48000, false);
      if (!processor)
        throw new Error("Shared audio processor initialization failed");
      const scratch = module._malloc(960 * 4);
      try {
        const blocks = [128, 960, 13, 1, 127];
        let position = 0,
          block = 0,
          maxError = 0;
        while (position < input.length) {
          const count = Math.min(
            blocks[block++ % blocks.length],
            input.length - position,
          );
          module.HEAPF32.set(
            input.slice(position, position + count),
            scratch / 4,
          );
          if (
            module._wasm_audio_processor_process(processor, scratch, count) !==
            0
          )
            throw new Error("Shared WASM processing failed");
          for (let i = 0; i < count; ++i)
            maxError = Math.max(
              maxError,
              Math.abs(
                module.HEAPF32[scratch / 4 + i] - expected[position + i],
              ),
            );
          position += count;
        }
        if (!Number.isFinite(maxError) || maxError > 0.00002)
          throw new Error(`Native/WASM PCM mismatch: ${maxError}`);
        return { samples: position, maxAbsoluteError: maxError };
      } finally {
        module._wasm_audio_processor_destroy(processor);
        module._free(scratch);
      }
    },
    { input: floats("input.f32"), expected: floats("native.f32") },
  );
  console.log(JSON.stringify(result));
} finally {
  await browser.close();
}
