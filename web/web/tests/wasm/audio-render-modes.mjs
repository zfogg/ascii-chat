const assert = {
  ok(value) {
    if (!value) throw new Error("Assertion failed");
  },
  equal(actual, expected) {
    if (actual !== expected) throw new Error(`${actual} !== ${expected}`);
  },
};
import createModule from "../../src/wasm/dist/mirror.js";
const m = await createModule({
  print() {},
  printErr() {},
  getRandomValue: () => crypto.getRandomValues(new Uint32Array(1))[0],
});
const args = m._malloc(16);
m.stringToUTF8("mirror", args, 16);
assert.equal(m._mirror_init_with_args(args), 0);
m._free(args);
m._set_waveform(1);
const samples = m._malloc(4096 * 4);
for (let base = 0; base < 49152; base += 4096) {
  const pcm = new Float32Array(4096);
  for (let i = 0; i < pcm.length; i++) {
    const t = (base + i) / 48000;
    pcm[i] = Math.sin(2 * Math.PI * 440 * t) * (0.2 + 0.6 * t);
  }
  new Float32Array(m.HEAPU8.buffer).set(pcm, samples / 4);
  m._mirror_submit_audio_samples(samples, 4096, 1);
}
m._free(samples);
for (const fft of [0, 1])
  for (const mode of [0, 1, 2])
    for (const flip of [0, 1]) {
      m._set_waveform(1 - fft);
      m._set_fft(fft);
      m._set_render_mode(mode);
      m._set_flip_x(flip);
      m._set_flip_y(flip);
      const p = m._mirror_render_audio_visualization(80, 24, 1);
      assert.ok(p);
      const frame = m.UTF8ToString(p);
      m._mirror_free_string(p);
      // Preserve trailing spaces when checking the exact terminal dimensions.
      const allRows = frame.replace(/\x1b\[[0-9;]*m/g, "").split("\n");
      allRows.pop();
      assert.equal(allRows.length, 24);
      assert.ok(allRows.every((r) => Array.from(r).length === 80));
      assert.ok(frame.includes(mode === 0 ? "\x1b[38;2;" : "\x1b[48;2;"));
      if (mode === 2) assert.ok(frame.includes("▀"));
      if (mode === 1) assert.ok(allRows.every((r) => /^ *$/.test(r)));
      document.querySelector("pre").textContent +=
        `PASS ${fft ? "FFT" : "waveform"} mode=${mode} flipped=${flip}\n`;
    }
m._mirror_cleanup();
