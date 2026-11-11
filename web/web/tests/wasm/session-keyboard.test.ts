import { describe, expect, it } from "vite-plus/test";
import { browserKeyCode } from "../../../packages/shared/src/components/AsciiRenderer/keyboard";

describe("shared keyboard representation", () => {
  it.each([
    ["a", 97],
    ["é", 0xe9],
    ["Ā", 0x100],
    ["界", 0x754c],
    ["😀", 0x1f600],
    ["ArrowUp", 0x110000],
    ["PageUp", 0x11000d],
    ["PageDown", 0x11000e],
    ["F12", 0x11000c],
    ["Tab", 9],
  ])(
    "normalizes %s without colliding with a special key",
    (input, expected) => {
      expect(browserKeyCode(input)).toBe(expected);
    },
  );
  it.each(["", "Dead", "Process", "ab", "e\u0301", "\ud800", "\udfff"])(
    "ignores incomplete or non-character key events: %j",
    (input) => {
      expect(browserKeyCode(input)).toBe(0);
    },
  );
  it("keeps Shift+Enter distinct from Enter and PageUp", () => {
    expect([
      browserKeyCode("Enter", true),
      browserKeyCode("Enter"),
      browserKeyCode("PageUp"),
    ]).toEqual([0x11000f, 13, 0x11000d]);
  });
});
