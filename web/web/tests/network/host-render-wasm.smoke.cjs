// After rebuilding WASM: node web/web/tests/network/host-render-wasm.smoke.cjs
const { chromium } = require("../../node_modules/@playwright/test");
// ACIP uses CRC32-C (Castagnoli), not the IEEE CRC in node:zlib.
function crc32(bytes) {
  let value = 0xffffffff;
  for (const byte of bytes) {
    value ^= byte;
    for (let bit = 0; bit < 8; ++bit)
      value = (value >>> 1) ^ (value & 1 ? 0x82f63b78 : 0);
  }
  return ~value >>> 0;
}
const http = require("node:http");
const fs = require("node:fs");
const assert = require("node:assert/strict");
const path = require("node:path");
(async () => {
  const server = http.createServer((req, res) => {
    res.setHeader("Cross-Origin-Opener-Policy", "same-origin");
    res.setHeader("Cross-Origin-Embedder-Policy", "require-corp");
    if (req.url === "/")
      return res.end(
        `<html><style>body{background:#10141c;color:#e1e8ef;font:16px monospace;padding:24px}canvas{image-rendering:pixelated;border:1px solid #546479}</style><body><h1>Received frame validation</h1><p id="result">Browser host wire output validated and rendered by shared C</p><canvas id="frame"></canvas></body></html>`,
      );
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
    const fixtures = [
      { text: "    ", width: 4, height: 1, valid: true },
      { text: "界é ", width: 4, height: 1, valid: true },
      { text: "abc", width: 4, height: 1, valid: false },
      { text: "abcde", width: 4, height: 1, valid: false },
      { text: "abcd\nabc", width: 4, height: 2, valid: false },
      { text: "abcd\x1b]52;c;AAAA\x07", width: 4, height: 1, valid: false },
      { text: "abcd\x1b]0;title\x07", width: 4, height: 1, valid: false },
      { text: "abcd\x1b[6n", width: 4, height: 1, valid: false },
      { text: "abc\x1b[", width: 4, height: 1, valid: false },
      { text: "a\x1b[999999999999b", width: 4, height: 1, valid: false },
      { text: "\x1b[8mabcd", width: 4, height: 1, valid: false },
      { text: "\x1b[31mabcd\x1b[0m", width: 4, height: 1, valid: true },
    ].map((fixture) => ({ ...fixture, crc: crc32(Buffer.from(fixture.text)) }));
    const packets = await page.evaluate(async (fixtures) => {
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
        m._get_font_default_ptr(0) !== normalFont ||
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
      let rejected = 0;
      const verify = (
        bytes,
        width,
        height,
        crc,
        flags,
        metadata = new Uint8Array(),
      ) => {
        const text = m._malloc(bytes.length || 1),
          contract = m._malloc(metadata.length || 1);
        try {
          m.HEAPU8.set(bytes, text);
          m.HEAPU8.set(metadata, contract);
          return m._client_validate_frame(
            text,
            bytes.length,
            width,
            height,
            crc,
            flags,
            contract,
            metadata.length,
          );
        } finally {
          m._free(text);
          m._free(contract);
        }
      };
      for (const fixture of fixtures) {
        const result = verify(
          new TextEncoder().encode(fixture.text),
          fixture.width,
          fixture.height,
          fixture.crc,
          0,
        );
        if ((result === 0) !== fixture.valid)
          throw new Error("C frame corpus failed: " + JSON.stringify(fixture));
        if (result) rejected++;
      }
      const decodeFrame = (packet) => {
        const payload = packet.subarray(22),
          view = new DataView(
            payload.buffer,
            payload.byteOffset,
            payload.byteLength,
          );
        const flags = view.getUint32(20),
          original = view.getUint32(8);
        let offset = 24;
        if (!(flags & 16) || view.getUint32(offset) !== 1)
          throw new Error("Missing explicit render contract");
        const metadata = payload.subarray(
          offset,
          offset + 20 + view.getUint32(offset + 16),
        );
        offset += metadata.length;
        const encoded = payload.subarray(offset),
          input = m._malloc(encoded.length),
          output = m._malloc(original);
        try {
          m.HEAPU8.set(encoded, input);
          if (flags & 2) {
            if (
              m._client_decode_frame_data(
                input,
                encoded.length,
                output,
                original,
              )
            )
              throw new Error("C zstd failed");
            const oversized = m._malloc(original + 1);
            try {
              if (
                !m._client_decode_frame_data(
                  input,
                  encoded.length,
                  oversized,
                  original + 1,
                )
              )
                throw new Error("Short zstd output accepted");
            } finally {
              m._free(oversized);
            }
          } else m.HEAPU8.set(encoded, output);
          const bytes = m.HEAPU8.slice(output, output + original);
          if (
            verify(
              bytes,
              view.getUint32(0),
              view.getUint32(4),
              view.getUint32(16),
              flags,
              metadata,
            )
          )
            throw new Error("Real host frame rejected");
          const wrongPalette = metadata.slice();
          wrongPalette.fill(33, 20); // Same exact-length contract, truly off-palette for X/Y/Z frames.
          if (
            new DataView(metadata.buffer, metadata.byteOffset).getUint32(8) !==
              2 &&
            !verify(
              bytes,
              view.getUint32(0),
              view.getUint32(4),
              view.getUint32(16),
              flags,
              wrongPalette,
            )
          )
            throw new Error("Off-palette real frame accepted");
          return new TextDecoder().decode(bytes);
        } finally {
          m._free(input);
          m._free(output);
        }
      };
      let rendered = false;
      const paint = (text) => {
        const config = m._malloc(544),
          out = m._malloc(4),
          bytes = new TextEncoder().encode(text),
          input = m._malloc(bytes.length);
        let renderer = 0;
        try {
          m.HEAPU8.fill(0, config, config + 544);
          const view = new DataView(m.HEAPU8.buffer, config, 544);
          view.setInt32(0, 40, true);
          view.setInt32(4, 12, true);
          view.setFloat64(8, 12, true);
          view.setInt32(536, m._get_font_default_ptr(0), true);
          view.setInt32(540, m._get_font_default_size(0), true);
          if (m._term_renderer_create(config, out))
            throw new Error("C terminal create failed");
          renderer = new DataView(m.HEAPU8.buffer, out, 4).getUint32(0, true);
          m.HEAPU8.set(bytes, input);
          if (m._term_renderer_feed(renderer, input, bytes.length))
            throw new Error("C terminal feed failed");
          const canvas = document.getElementById("frame");
          canvas.width = m._term_renderer_width_px(renderer);
          canvas.height = m._term_renderer_height_px(renderer);
          const pixels = new Uint8ClampedArray(
              canvas.width * canvas.height * 4,
            ),
            source = m._term_renderer_pixels(renderer),
            pitch = m._term_renderer_pitch(renderer);
          for (let row = 0; row < canvas.height; row++)
            pixels.set(
              m.HEAPU8.subarray(
                source + row * pitch,
                source + row * pitch + canvas.width * 4,
              ),
              row * canvas.width * 4,
            );
          if (!pixels.some((value, i) => i % 4 !== 3 && value > 0))
            throw new Error("No visible pixels from real frame");
          canvas
            .getContext("2d")
            .putImageData(
              new ImageData(pixels, canvas.width, canvas.height),
              0,
              0,
            );
          document.getElementById("result").textContent +=
            "; " + rejected + " malformed frames rejected; live output below";
        } finally {
          if (renderer) m._term_renderer_destroy(renderer);
          m._free(config);
          m._free(out);
          m._free(input);
        }
      };
      const output = [];
      m.hostPacketCallback = (id, bytes) => {
        if (
          new DataView(bytes.buffer, bytes.byteOffset).getUint16(8) === 3000
        ) {
          const text = decodeFrame(bytes);
          output.push({ id, bytes: Array.from(bytes), text });
          if (!rendered) {
            paint(text);
            rendered = true;
          }
        }
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
      patch(ids[1], { color_mode: 0, render_mode: 2 });
      await new Promise((resolve) => setTimeout(resolve, 20));
      if (m._browser_host_render())
        throw new Error("monochrome half-block render failed");
      const halfBlock = output.splice(0).find((frame) => frame.id === ids[1]);
      if (!halfBlock || !/[░▒▓█]/u.test(halfBlock.text))
        throw new Error("Missing real monochrome half-block shade output");
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
      // A frame may carry an older palette, but may not contradict its acknowledged revision.
      const acknowledged = new TextEncoder().encode(
        JSON.stringify({
          width: 40,
          height: 12,
          fps: 30,
          color_mode: 0,
          palette_custom: "X",
        }),
      );
      const ack = new Uint8Array(20 + acknowledged.length);
      const ackView = new DataView(ack.buffer);
      ackView.setUint32(0, 1);
      ackView.setUint32(12, 90);
      ack.set(acknowledged, 20);
      const ackPtr = m._malloc(ack.length);
      m.HEAPU8.set(ack, ackPtr);
      try {
        const ackResult = m._client_settings_receive(ackPtr, ack.length);
        if (ackResult) throw new Error("settings ACK failed: " + ackResult);
      } finally {
        m._free(ackPtr);
      }
      const paletteContract = new Uint8Array(21);
      const paletteView = new DataView(paletteContract.buffer);
      paletteView.setUint32(0, 1);
      paletteView.setUint32(4, 90);
      paletteView.setUint32(16, 1);
      paletteContract[20] = 32;
      const blank = fixtures[0];
      if (
        !verify(
          new TextEncoder().encode(blank.text),
          4,
          1,
          blank.crc,
          16,
          paletteContract,
        )
      )
        throw new Error("Contradictory acknowledged palette accepted");
      paletteView.setUint32(4, 89);
      if (
        verify(
          new TextEncoder().encode(blank.text),
          4,
          1,
          blank.crc,
          16,
          paletteContract,
        )
      )
        throw new Error("Older palette revision rejected");
      return {
        video: [first, updated],
        audioRms: Math.sqrt(audioEnergy / Math.max(1, audioSamples)),
        audioSamples,
      };
    }, fixtures);
    if (process.env.FRAME_VALIDATION_SCREENSHOT)
      await page.screenshot({
        path: process.env.FRAME_VALIDATION_SCREENSHOT,
        fullPage: true,
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
        .map(({ bytes, text }) => {
          const payload = Buffer.from(bytes).subarray(22);
          assert.equal(payload.readUInt32BE(0), 40);
          assert.equal(payload.readUInt32BE(4), 12);
          assert.ok(payload.readUInt32BE(20) & 16);
          return text;
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
