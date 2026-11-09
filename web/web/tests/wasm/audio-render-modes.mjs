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
assert.equal(m.initMirror("mirror"), 0);
m.patchOptions({ waveform: true }, -1);
const renderer = new m.MirrorFrameBinding();
for (let base = 0; base < 49152; base += 4096) {
  const pcm = new Float32Array(4096);
  for (let i = 0; i < pcm.length; i++) {
    const t = (base + i) / 48000;
    pcm[i] = Math.sin(2 * Math.PI * 440 * t) * (0.2 + 0.6 * t);
  }
  renderer.submitAudio(pcm, 1);
}
for (const fft of [0, 1])
  for (const mode of [0, 1, 2])
    for (const flip of [0, 1]) {
      m.patchOptions(
        {
          waveform: !fft,
          fft: !!fft,
          render_mode: mode,
          flip_x: !!flip,
          flip_y: !!flip,
        },
        -1,
      );
      const frame = renderer.renderAudio(80, 24, 1);
      assert.ok(frame);
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
renderer.delete();
m.mirror_cleanup();
