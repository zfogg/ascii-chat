import { expect, type Page } from "@playwright/test";
import { fileURLToPath } from "node:url";

export const moduleUrl = `/@fs/${fileURLToPath(
  new URL("../../../packages/shared/src/wasm/mirror.ts", import.meta.url),
)
  .replaceAll("\\", "/")
  .replace(/^\/+/, "")}`;
declare global {
  interface Window {
    audioProof?: {
      inputRms: number;
      samples: number;
      renders: number;
      lastFrame: string;
      lastMode: string;
    };
    outputProof?: { rms: number; peak: number; graphs: number };
  }
}

/** Observe real PCM at the C boundary and copy real C output before it is freed. */
export async function observeAudioVisualization(page: Page) {
  await page.evaluate(async (url) => {
    const { getMirrorModule } = await import(url);
    const module = getMirrorModule();
    const proof = (window.audioProof = {
      inputRms: 0,
      samples: 0,
      renders: 0,
      lastFrame: "",
      lastMode: "",
    });
    const submit = module._mirror_submit_audio_samples;
    module._mirror_submit_audio_samples = (
      pointer: number,
      count: number,
      source: number,
    ) => {
      const samples = module.HEAPF32.subarray(
        pointer >> 2,
        (pointer >> 2) + count,
      );
      proof.inputRms = Math.sqrt(
        samples.reduce(
          (sum: number, sample: number) => sum + sample * sample,
          0,
        ) / count,
      );
      proof.samples += count;
      return submit(pointer, count, source);
    };
    let selectedMode = "";
    for (const [setter, mode] of [
      ["_set_waveform", "waveform"],
      ["_set_fft", "fft"],
    ] as const) {
      const original = module[setter];
      module[setter] = (enabled: number) => {
        if (enabled) selectedMode = mode;
        return original(enabled);
      };
    }
    const render = module._mirror_render_audio_visualization;
    module._mirror_render_audio_visualization = (
      width: number,
      height: number,
      source: number,
    ) => {
      const pointer = render(width, height, source);
      if (pointer) {
        proof.lastFrame = module.UTF8ToString(pointer);
        proof.lastMode = selectedMode;
        proof.renders++;
      }
      return pointer;
    };
  }, moduleUrl);
}

/** Measure every actual Web Audio graph output; no capture or playback path is replaced. */
export async function installOutputMeter(page: Page) {
  await page.addInitScript(() => {
    const proof = (window.outputProof = { rms: 0, peak: 0, graphs: 0 });
    const connect = AudioNode.prototype.connect;
    AudioNode.prototype.connect = function (
      this: AudioNode,
      ...args: Parameters<AudioNode["connect"]>
    ) {
      const result = Reflect.apply(connect, this, args);
      const destination = args[0];
      if (destination instanceof AudioDestinationNode) {
        const analyser = this.context.createAnalyser();
        analyser.fftSize = 2048;
        Reflect.apply(connect, this, [analyser]);
        proof.graphs++;
        const samples = new Float32Array(2048);
        setInterval(() => {
          analyser.getFloatTimeDomainData(samples);
          proof.rms = Math.sqrt(
            samples.reduce((sum, sample) => sum + sample * sample, 0) /
              samples.length,
          );
          proof.peak = Math.max(proof.peak, proof.rms);
        }, 50);
      }
      return result;
    } as AudioNode["connect"];
  });
}

export async function selectAudioVisualization(
  page: Page,
  mode: "waveform" | "fft",
) {
  const selection = page
    .locator("select")
    .filter({ has: page.locator('option[value="waveform"]') });
  const settings = page.getByRole("button", { name: "Settings", exact: true });
  await settings.click();
  if (!(await selection.isVisible()))
    await page.getByRole("button", { name: "Animation", exact: true }).click();
  const previousRenders = await page.evaluate(
    () => window.audioProof?.renders ?? 0,
  );
  await selection.selectOption(mode);
  await expect
    .poll(() => new URL(page.url()).searchParams.get("animation"))
    .toBe(mode);
  await settings.click();
  await expect
    .poll(() => page.evaluate(() => window.audioProof?.inputRms ?? 0), {
      timeout: 20000,
    })
    .toBeGreaterThan(0.03);
  await expect
    .poll(() => page.evaluate(() => window.audioProof?.renders ?? 0))
    .toBeGreaterThan(previousRenders + 2);
  await expect
    .poll(() => page.evaluate(() => window.audioProof?.lastMode))
    .toBe(mode);
  await expect
    .poll(() => page.evaluate(() => window.audioProof?.lastFrame.length ?? 0))
    .toBeGreaterThan(100);
}
