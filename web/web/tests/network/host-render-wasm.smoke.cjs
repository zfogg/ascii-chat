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
    const file = [
      "/client.js",
      "/client.wasm",
      "/mirror.js",
      "/mirror.wasm",
    ].includes(req.url)
      ? req.url.slice(1)
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
  const browser = await chromium.launch({
    executablePath: process.env.CHROME_PATH,
  });
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
      const revision = m.OptionsBinding.revision();
      const normalProbe = new m.AsciiRendererBinding(1, 1, 24, false, 1);
      const matrixProbe = new m.AsciiRendererBinding(1, 1, 24, true, 2);
      try {
        if (
          !normalProbe.valid() ||
          !matrixProbe.valid() ||
          m.OptionsBinding.revision() !== revision
        )
          throw new Error("font selection changed engine preferences");
      } finally {
        normalProbe.delete();
        matrixProbe.delete();
      }
      // The bundled font omits Braille; all eight positions must still render.
      const brailleRenderer = new m.AsciiRendererBinding(1, 1, 24, false, 1);
      if (!brailleRenderer.valid())
        throw new Error("Braille terminal creation failed");
      const pixelsFor = (text) => {
        const pixels = brailleRenderer.render("\x1b[H" + text, 0);
        if (!pixels) throw new Error("Braille render failed");
        return Array.from(pixels);
      };
      try {
        const blank = pixelsFor(" ");
        const brailleBlank = pixelsFor("⠀");
        const patterns = ["⠁", "⠂", "⠄", "⡀", "⠈", "⠐", "⠠", "⢀", "⣿"].map(
          pixelsFor,
        );
        const same = (a, b) => a.every((value, i) => value === b[i]);
        if (!same(blank, brailleBlank))
          throw new Error("Blank Braille pattern was not blank");
        if (
          patterns.some((pixels) => same(blank, pixels)) ||
          patterns.some((pixels, i) =>
            patterns.some((other, j) => i !== j && same(pixels, other)),
          )
        )
          throw new Error("Braille dot patterns did not render distinctly");
        for (let i = 0; i < 2; i++) {
          let failed = false;
          try {
            pixelsFor("\u0378");
          } catch {
            failed = true;
          }
          if (!failed)
            throw new Error("Missing font glyph did not report an error");
        }
        pixelsFor("A");
      } finally {
        brailleRenderer.delete();
      }
      let host = new m.BrowserHostBinding();
      if (!host.valid()) throw new Error("host create failed");
      const ids = [host.addClient(), host.addClient()];
      if (host.setLocalClient(ids[0]))
        throw new Error("local target failed");
      let hostInput = new m.HostPacketBinding(host);
      const send = (id, type, bytes) => {
        const result = hostInput.receive(id, type, bytes);
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
        return m.ClientFrameBinding.validate(
          bytes,
          width,
          height,
          crc,
          flags,
          metadata,
          bytes.length,
        ).code;
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
        if (result && result !== 87)
          throw new Error(
            "Malformed frame did not return ERROR_INVALID_FRAME: " + result,
          );
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
        const encoded = payload.subarray(offset);
        const decoded = m.ClientFrameBinding.validate(
          encoded,
          view.getUint32(0),
          view.getUint32(4),
          view.getUint32(16),
          flags,
          metadata,
          original,
        );
        if (decoded.code)
          throw new Error("Real host frame rejected: " + decoded.code);
        if (
          flags & 2 &&
          !m.ClientFrameBinding.validate(
            encoded,
            view.getUint32(0),
            view.getUint32(4),
            view.getUint32(16),
            flags,
            metadata,
            original + 1,
          ).decodeFailed
        )
          throw new Error("Short zstd output accepted");
        const bytes = decoded.bytes;
        const wrongPalette = metadata.slice();
        wrongPalette.fill(33, 20);
        if (
          new DataView(metadata.buffer, metadata.byteOffset).getUint32(8) !==
            2 &&
          !verify(
            bytes,
            view.getUint32(0),
            view.getUint32(4),
            view.getUint32(16),
            flags & ~2,
            wrongPalette,
          )
        )
          throw new Error("Off-palette real frame accepted");
        return new TextDecoder().decode(bytes);
      };
      let rendered = false;
      const paint = (text) => {
        const renderer = new m.AsciiRendererBinding(40, 12, 12, false, 0);
        try {
          if (!renderer.valid()) throw new Error("C terminal render failed");
          const canvas = document.getElementById("frame");
          const dimensions = renderer.dimensions();
          canvas.width = dimensions.width;
          canvas.height = dimensions.height;
          const pixels = renderer.render(text, 0);
          if (!pixels) throw new Error("C terminal render failed");
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
          renderer.delete();
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
      if (host.render()) throw new Error("render failed");
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
      if (host.render()) throw new Error("updated render failed");
      const updated = output.splice(0);
      patch(ids[0], {
        renderer: 1,
        palette_custom: "@#",
        palette_explicit: true,
      });
      patch(ids[1], {
        renderer: 0,
        color_mode: 0,
        palette_custom: "Y",
        palette_explicit: true,
      });
      await new Promise((resolve) => setTimeout(resolve, 20));
      if (host.render())
        throw new Error("per-viewer renderer failed");
      const distinct = output.splice(0);
      const glyphFrame = distinct.find((frame) => frame.id === ids[0]);
      const luminanceFrame = distinct.find((frame) => frame.id === ids[1]);
      const characters = (frame) =>
        frame.text.replace(/\x1b\[[0-9;]*[a-zA-Z]/g, "");
      if (
        !glyphFrame ||
        !luminanceFrame ||
        /[^@#\s]/u.test(characters(glyphFrame)) ||
        /[^Y\s]/u.test(characters(luminanceFrame))
      )
        throw new Error("Per-viewer renderer palettes were not respected");
      patch(ids[1], { color_mode: 0, render_mode: 2 });
      await new Promise((resolve) => setTimeout(resolve, 20));
      if (host.render())
        throw new Error("monochrome half-block render failed");
      const halfBlock = output.splice(0).find((frame) => frame.id === ids[1]);
      if (!halfBlock || !/[░▒▓█]/u.test(halfBlock.text))
        throw new Error("Missing real monochrome half-block shade output");
      for (const id of ids)
        patch(id, {
          color_mode: 3,
          color_filter: 0,
          render_mode: 0,
          renderer: 0,
          stabilize: true,
        });
      const stabilizedFrame = async (value) => {
        for (let i = 24; i < image.length; i += 3) image.fill(value, i, i + 3);
        send(ids[0], 3001, image);
        await new Promise((resolve) => setTimeout(resolve, 20));
        if (host.render())
          throw new Error("stabilization render failed");
        return output.splice(0).map((frame) => ({
          id: frame.id,
          red: Number(frame.text.match(/\x1b\[38;2;(\d+);/)?.[1]),
        }));
      };
      const baseline = await stabilizedFrame(100);
      const smoothed = await stabilizedFrame(104);
      for (const id of ids) {
        const first = baseline.find((frame) => frame.id === id)?.red;
        const second = smoothed.find((frame) => frame.id === id)?.red;
        if (
          first !== 100 ||
          !Number.isFinite(second) ||
          second < 100 ||
          second >= 104
        )
          throw new Error(
            `Stabilization failed for viewer ${id}: ${first}, ${second}`,
          );
      }
      send(ids[0], 5006, new Uint8Array());
      send(ids[0], 5005, new Uint8Array());
      const restarted = await stabilizedFrame(104);
      for (const id of ids)
        if (restarted.find((frame) => frame.id === id)?.red !== 104)
          throw new Error(
            `Stabilizer history survived source restart for viewer ${id}`,
          );
      let codec = new m.OpusCodecBinding(48000, 1, 24000, 1000);
      if (!codec.valid())
        throw new Error("Could not initialize test audio codecs");
      const pcm = new Int16Array(960);
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
        const decoded = codec.decode(
          bytes.subarray(22 + 18, 22 + 18 + length),
          960,
        );
        if (!decoded || decoded.length !== 960)
          throw new Error("Mixed Opus did not decode to 20 ms");
        for (const value of decoded) audioEnergy += value * value;
        const count = decoded.length;
        audioSamples += count;
      };
      try {
        for (let frame = 0; frame < 12; frame++) {
          for (let i = 0; i < 960; i++)
            pcm[i] = Math.round(
              8000 * Math.sin((2 * Math.PI * 440 * (frame * 960 + i)) / 48000),
            );
          const encoded = codec.encode(pcm);
          if (!encoded || !encoded.length)
            throw new Error("Test Opus encoding failed");
          const batch = new Uint8Array(18 + encoded.length),
            view = new DataView(batch.buffer);
          view.setUint32(0, 48000);
          view.setUint32(4, 20);
          view.setUint32(8, 1);
          view.setUint16(16, encoded.length);
          batch.set(encoded, 18);
          sourcePackets.push(batch.slice());
          send(ids[0], 4001, batch);
          if (host.renderAudio())
            throw new Error("Host audio tick failed");
        }
        codec.delete();
        codec = new m.OpusCodecBinding(48000, 1, 24000, 1000);
        if (!codec.valid()) throw new Error("self decoder init failed");
        for (const bytes of ownPackets) {
          const view = new DataView(
            bytes.buffer,
            bytes.byteOffset,
            bytes.byteLength,
          );
          const length = view.getUint16(38);
          const decoded = codec.decode(bytes.subarray(40, 40 + length), 960);
          if (!decoded || decoded.length !== 960)
            throw new Error("self mix did not decode");
          for (const sample of decoded) ownEnergy += sample * sample;
        }
        if (ownPackets.length !== 12 || Math.sqrt(ownEnergy / (12 * 960)) > 50)
          throw new Error("sender hears its own microphone");
        // A second, independent (silent) input stream must not contaminate
        // the first stream's decoder or the listener's predictive encoder.
        hostInput.delete();
        host.close();
        host = new m.BrowserHostBinding();
        if (!host.valid()) throw new Error("second host failed");
        hostInput = new m.HostPacketBinding(host);
        ids[0] = host.addClient();
        ids[1] = host.addClient();
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
          if (host.renderAudio())
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
        codec.delete();
        hostInput.delete();
        host.close();
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
      const ackResult = m.ClientSettingsBinding.receive(ack).code;
      if (ackResult) throw new Error("settings ACK failed: " + ackResult);
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
      const { default: createMirror } = await import("/mirror.js");
      const mirror = await createMirror({
        print: () => {},
        printErr: () => {},
        getRandomValue: () => crypto.getRandomValues(new Uint32Array(1))[0],
      });
      const mirrorSession = new mirror.MirrorSessionBinding({
        width: 40,
        height: 12,
      });
      if (!mirrorSession.valid()) throw new Error("mirror init failed");
      const rgba = new Uint8Array(8 * 4 * 4);
      for (let i = 0; i < rgba.length; i += 4) {
        rgba[i] = 180;
        rgba[i + 1] = 30;
        rgba[i + 2] = 80;
        rgba[i + 3] = 255;
      }
      const mirrorFrame = new mirror.MirrorFrameBinding();
      let mirrorFrames = 0;
      try {
        for (const palette of ["XX", "界é"])
          for (let mode = 0; mode <= 2; ++mode)
            for (let color = 0; color <= 3; ++color) {
              const json = {
                palette_custom: palette,
                render_mode: mode,
                color_mode: color,
              };
              const patched = mirror.OptionsBinding.patch(json, -1);
              if (patched) throw new Error("mirror settings failed");
              const output = mirrorFrame.convert(rgba, 8, 4, false);
              if (!output)
                throw new Error(
                  "Mirror validator rejected mode " +
                    mode +
                    " color " +
                    color +
                    " palette " +
                    palette,
                );
              const bytes = new TextEncoder().encode(output);
              let checksum = 0xffffffff;
              for (const byte of bytes) {
                checksum ^= byte;
                for (let bit = 0; bit < 8; ++bit)
                  checksum = (checksum >>> 1) ^ (checksum & 1 ? 0x82f63b78 : 0);
              }
              if (verify(bytes, 40, 12, ~checksum >>> 0, 0))
                throw new Error("Malformed accepted mirror output");
              mirrorFrames++;
            }
        for (const fft of [false, true]) {
          const json = { fft };
          const result = mirror.OptionsBinding.patch(json, -1);
          if (result) throw new Error("mirror audio settings failed");
          const frame = mirrorFrame.renderAudio(40, 12, 0);
          if (!frame)
            throw new Error("Mirror audio validation rejected valid output");
        }
      } finally {
        mirrorFrame.delete();
        mirrorSession.close();
        mirrorSession.delete();
      }
      if (mirrorFrames !== 24) throw new Error("Missing mirror mode coverage");
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
      "PASS: shared C validates browser mirror modes/palettes/audio and local/remote host frames",
    );
  } finally {
    await browser.close();
    server.close();
  }
})().catch((e) => {
  console.error(e);
  process.exit(1);
});
