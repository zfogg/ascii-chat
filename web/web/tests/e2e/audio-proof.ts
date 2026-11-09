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
    const binding = module.MirrorFrameBinding.prototype;
    const submit = binding.submitAudio;
    binding.submitAudio = function (
      this: unknown,
      samples: Float32Array,
      source: number,
    ) {
      proof.inputRms = Math.sqrt(
        samples.reduce(
          (sum: number, sample: number) => sum + sample * sample,
          0,
        ) / samples.length,
      );
      proof.samples += samples.length;
      return submit.call(this, samples, source);
    };
    const render = binding.renderAudio;
    binding.renderAudio = function (
      this: unknown,
      width: number,
      height: number,
      source: number,
    ) {
      const frame = render.call(this, width, height, source);
      if (frame !== undefined) {
        proof.lastFrame = frame;
        proof.lastMode = module.OptionsBinding.read()?.values.fft ? "fft" : "waveform";
        proof.renders++;
      }
      return frame;
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
  const animation = page.getByRole("button", { name: /^(🟢 )?Animation$/ });
  await expect(animation).toBeVisible();
  if (!(await selection.count())) await animation.click();
  await expect(selection).toBeVisible();
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
