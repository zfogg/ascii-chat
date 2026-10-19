// After rebuilding WASM: node web/web/tests/network/host-render-wasm.smoke.cjs
const { chromium } = require("../../node_modules/@playwright/test");
const { decompress } = require("../../node_modules/fzstd");
const http = require("node:http");
const fs = require("node:fs");
const assert = require("node:assert/strict");
const path = require("node:path");
(async () => {
  const server = http.createServer((req, res) => {
    res.setHeader("Cross-Origin-Opener-Policy", "same-origin");
    res.setHeader("Cross-Origin-Embedder-Policy", "require-corp");
    if (req.url === "/")
      return res.end("<html><body>Host rendering test</body></html>");
    const file =
      req.url === "/client.js"
        ? "client.js"
        : req.url === "/client.wasm"
          ? "client.wasm"
          : null;
    if (!file) {
      res.statusCode = 404;
      return res.end();
    }
    res.setHeader(
      "Content-Type",
      file.endsWith(".js") ? "text/javascript" : "application/wasm",
    );
    fs.createReadStream(path.join(__dirname, "../../src/wasm/dist", file)).pipe(
      res,
    );
  });
  await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
  const browser = await chromium.launch();
  try {
    const page = await browser.newPage();
    page.on("pageerror", (error) => console.error("browser error:", error));
    await page.goto(`http://127.0.0.1:${server.address().port}`);
    const packets = await page.evaluate(async () => {
      const { default: createModule } = await import("/client.js");
      const m = await createModule({
        print: () => {},
        printErr: () => {},
        getRandomValue: () => crypto.getRandomValues(new Uint32Array(1))[0],
      });
      const args = "client --width 40 --height 12 --log-level fatal";
      const ptr = m._malloc(args.length + 1);
      m.stringToUTF8(args, ptr, args.length + 1);
      if (m._client_init_with_args(ptr)) throw new Error("client init failed");
      m._free(ptr);
      // Measuring an old Matrix renderer must not reset newer C preferences.
      const selection = '{"waveform":true}';
      const selectionPtr = m._malloc(selection.length + 1);
      m.stringToUTF8(selection, selectionPtr, selection.length + 1);
      if (m._wasm_options_patch(selectionPtr, -1))
        throw new Error("waveform selection failed");
      m._free(selectionPtr);
      const revision = m._wasm_options_revision();
      const normalFont = m._get_font_default_ptr(1);
      const matrixFont = m._get_font_default_ptr(2);
      if (
        !normalFont ||
        !matrixFont ||
        normalFont === matrixFont ||
        !m._get_font_default_size(1) ||
        !m._get_font_default_size(2) ||
        m._get_font_default_ptr() !== normalFont ||
        m._wasm_options_revision() !== revision
      )
        throw new Error("font selection changed engine preferences");
      if (m._browser_host_create()) throw new Error("host create failed");
      const ids = [m._browser_host_add_client(), m._browser_host_add_client()];
      if (m._browser_host_set_local_client(ids[0]))
        throw new Error("local target failed");
      const send = (id, type, bytes) => {
        const p = m._malloc(bytes.length || 1);
        m.HEAPU8.set(bytes, p);
        const result = m._browser_host_receive(id, type, p, bytes.length);
        m._free(p);
        if (result) throw new Error(`receive ${type}: ${result}`);
      };
      const caps = (color, custom) => {
        const bytes = new Uint8Array(168),
          v = new DataView(bytes.buffer);
        v.setUint32(4, color);
        v.setUint16(16, 40);
        v.setUint16(18, 12);
        v.setUint32(85, 1);
        v.setUint32(89, 6);
        bytes.set(new TextEncoder().encode(custom), 93);
        return bytes;
      };
      send(ids[0], 5000, caps(3, "XX"));
      send(ids[1], 5000, caps(0, "YY"));
      const image = new Uint8Array(24 + 8 * 4 * 3),
        v = new DataView(image.buffer);
      v.setUint32(0, 8);
      v.setUint32(4, 4);
      v.setUint32(8, 1);
      for (let i = 24; i < image.length; i += 3) {
        image[i] = 180;
        image[i + 1] = 30;
        image[i + 2] = 80;
      }
      send(ids[0], 3001, image);
      send(ids[0], 5005, new Uint8Array());
      const output = [];
      m.hostPacketCallback = (id, bytes) => {
        if (new DataView(bytes.buffer, bytes.byteOffset).getUint16(8) === 3000)
          output.push({ id, bytes: Array.from(bytes) });
      };
      if (m._browser_host_render()) throw new Error("render failed");
      const first = output.splice(0);
      const patch = (id, values) => {
        const json = new TextEncoder().encode(JSON.stringify(values));
        const payload = new Uint8Array(20 + json.length);
        const view = new DataView(payload.buffer);
        view.setUint32(0, 1);
        view.setUint32(4, 1);
        view.setUint32(8, 0xffffffff);
        payload.set(json, 20);
        send(id, 6063, payload);
      };
      patch(ids[0], { color_mode: 0, palette_custom: "ZZ" });
      patch(ids[1], { color_mode: 3 });
      await new Promise((resolve) => setTimeout(resolve, 20));
      if (m._browser_host_render()) throw new Error("updated render failed");
      const updated = output.splice(0);
      if (
        m._client_opus_encoder_init(48000, 1, 24000) ||
        m._client_opus_decoder_init(48000, 1)
      )
        throw new Error("Could not initialize test audio codecs");
      const pcm = m._malloc(1920),
        encoded = m._malloc(1000),
        decoded = m._malloc(1920);
      const ownPackets = [];
      const sourcePackets = [];
      const listenerPackets = [];
      let ownEnergy = 0;
      let audioEnergy = 0,
        audioSamples = 0;
      m.hostPacketCallback = (id, bytes) => {
        const view = new DataView(
          bytes.buffer,
          bytes.byteOffset,
          bytes.byteLength,
        );
        if (view.getUint16(8) !== 4001) return;
        if (id === ids[0]) {
          ownPackets.push(bytes.slice());
          return;
        }
        if (id !== ids[1]) return;
        listenerPackets.push(bytes.slice(22));
        const length = view.getUint16(22 + 16);
        m.HEAPU8.set(bytes.subarray(22 + 18, 22 + 18 + length), encoded);
        const count = m._client_opus_decode(encoded, length, decoded, 960, 0);
        if (count !== 960)
          throw new Error("Mixed Opus did not decode to 20 ms");
        for (let i = 0; i < count; i++) {
          const value = m.HEAP16[(decoded >> 1) + i];
          audioEnergy += value * value;
        }
        audioSamples += count;
      };
      try {
        for (let frame = 0; frame < 12; frame++) {
          for (let i = 0; i < 960; i++)
            m.HEAP16[(pcm >> 1) + i] = Math.round(
              8000 * Math.sin((2 * Math.PI * 440 * (frame * 960 + i)) / 48000),
            );
          const length = m._client_opus_encode(pcm, 960, encoded, 1000);
          if (length <= 0) throw new Error("Test Opus encoding failed");
          const batch = new Uint8Array(18 + length),
            view = new DataView(batch.buffer);
          view.setUint32(0, 48000);
          view.setUint32(4, 20);
          view.setUint32(8, 1);
          view.setUint16(16, length);
          batch.set(m.HEAPU8.slice(encoded, encoded + length), 18);
          sourcePackets.push(batch.slice());
          send(ids[0], 4001, batch);
          if (m._browser_host_render_audio())
            throw new Error("Host audio tick failed");
        }
        m._client_opus_decoder_cleanup();
        if (m._client_opus_decoder_init(48000, 1))
          throw new Error("self decoder init failed");
        for (const bytes of ownPackets) {
          const view = new DataView(
            bytes.buffer,
            bytes.byteOffset,
            bytes.byteLength,
          );
          const length = view.getUint16(38);
          m.HEAPU8.set(bytes.subarray(40, 40 + length), encoded);
          const count = m._client_opus_decode(encoded, length, decoded, 960, 0);
          if (count !== 960) throw new Error("self mix did not decode");
          for (let i = 0; i < count; i++) {
            const sample = m.HEAP16[(decoded >> 1) + i];
            ownEnergy += sample * sample;
          }
        }
        if (ownPackets.length !== 12 || Math.sqrt(ownEnergy / (12 * 960)) > 50)
          throw new Error("sender hears its own microphone");
        // A second, independent (silent) input stream must not contaminate
        // the first stream's decoder or the listener's predictive encoder.
        m._browser_host_destroy();
        if (m._browser_host_create()) throw new Error("second host failed");
        ids[0] = m._browser_host_add_client();
        ids[1] = m._browser_host_add_client();
        const concurrent = [];
        m.hostPacketCallback = (id, bytes) => {
          if (
            id === ids[1] &&
            new DataView(bytes.buffer, bytes.byteOffset).getUint16(8) === 4001
          )
            concurrent.push(bytes.slice(22));
        };
        for (let frame = 0; frame < sourcePackets.length; frame++) {
          send(ids[0], 4001, sourcePackets[frame]);
          send(ids[1], 4001, ownPackets[frame].subarray(22));
          if (m._browser_host_render_audio())
            throw new Error("concurrent audio tick failed");
        }
        if (
          concurrent.length !== listenerPackets.length ||
          concurrent.some(
            (packet, i) =>
              packet.length !== listenerPackets[i].length ||
              packet.some((value, j) => value !== listenerPackets[i][j]),
          )
        )
          throw new Error(
            "independent Opus stream contaminated listener output",
          );
      } finally {
        m._free(pcm);
        m._free(encoded);
        m._free(decoded);
        m._client_opus_encoder_cleanup();
        m._client_opus_decoder_cleanup();
        m._browser_host_destroy();
      }
      return {
        video: [first, updated],
        audioRms: Math.sqrt(audioEnergy / Math.max(1, audioSamples)),
        audioSamples,
      };
    });
    assert.ok(packets.audioSamples >= 9600);
    assert.ok(packets.audioRms > 1000, `Mixed audio RMS ${packets.audioRms}`);
    const frames = packets.video.map((batch) => {
      assert.equal(
        batch.length,
        2,
        "one frame per local/remote client per tick",
      );
      return batch
        .sort((a, b) => a.id - b.id)
        .map(({ bytes }) => {
          const b = Buffer.from(bytes),
            p = b.subarray(22),
            flags = p.readUInt32BE(20);
          assert.equal(p.readUInt32BE(0), 40);
          assert.equal(p.readUInt32BE(4), 12);
          const payload = p.subarray(24);
          return Buffer.from(
            flags & 1 ? decompress(payload) : payload,
          ).toString();
        });
    });
    const [text, updated] = frames;
    assert.match(text[0], /\x1b\[38;2;/);
    assert.doesNotMatch(text[1], /\x1b\[38;/);
    assert.match(text[0], /X/);
    assert.doesNotMatch(text[0], /Y/);
    assert.match(text[1], /Y/);
    assert.doesNotMatch(text[1], /X/);
    assert.doesNotMatch(updated[0], /\x1b\[38;/);
    assert.match(updated[0], /Z/);
    assert.doesNotMatch(updated[0], /X/);
    assert.match(updated[1], /\x1b\[38;2;/);
    assert.match(updated[1], /Y/);
    console.log(
      "PASS: WASM local and remote targets honor independent settings and live updates without duplicate frames",
    );
  } finally {
    await browser.close();
    server.close();
  }
})().catch((e) => {
  console.error(e);
  process.exit(1);
});
