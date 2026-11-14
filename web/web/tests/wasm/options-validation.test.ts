import { describe, expect, it, vi } from "vite-plus/test";
import {
  ColorMode,
  Options,
  RenderMode,
  paletteName,
  paletteTypes,
} from "@ascii-chat/shared/wasm/options";

describe("shared options validation", () => {
  const snapshot = { revision: 1, values: {} };
  function setup() {
    const patchOptions = vi.fn(() => 0);
    const options = new Options({
      OptionsBinding: {
        revision: () => 1,
        read: () => snapshot,
        patch: patchOptions,
      },
    });
    return { options, patchOptions };
  }
  it.each([
    { color_mode: 42 },
    { render_mode: -1 },
    { palette_type: 7 },
    { color_filter: 100 },
    { renderer: 2 },
    { audio_source: 4 },
    { palette_custom: "x".repeat(1025) },
    { palette_custom: "x".repeat(256) },
    { palette_custom: "🌈".repeat(64) },
  ])("rejects invalid patches before committing: %o", (patch) => {
    const { options, patchOptions } = setup();
    expect(() => options.patch({ width: 80, ...patch })).toThrow();
    expect(patchOptions).not.toHaveBeenCalled();
  });
  it("accepts valid modes and a palette of 255 UTF-8 bytes", () => {
    const { options, patchOptions } = setup();
    options.patch({
      color_mode: ColorMode.Truecolor,
      render_mode: RenderMode.HalfBlock,
      palette_type: paletteTypes.custom,
      palette_custom: "x".repeat(255),
    });
    expect(patchOptions).toHaveBeenCalledOnce();
    expect(() => paletteName(42)).toThrow();
  });
});
