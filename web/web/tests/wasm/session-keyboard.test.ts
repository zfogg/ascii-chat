import { describe, expect, it, vi } from "vite-plus/test";
import { SessionUi } from "../../../packages/shared/src/wasm/sessionUi";
import type { MirrorModule } from "../../../packages/shared/src/wasm/mirror";

function session() {
  const key = vi.fn(() => 1);
  const module = {
    _wasm_ui_create: () => 7,
    _wasm_ui_capture_logs: vi.fn(),
    _wasm_ui_key: key,
  } as unknown as MirrorModule;
  return { ui: new SessionUi(module), key };
}

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
      const { ui, key } = session();
      expect(ui.key(input)).toBe(1);
      expect(key).toHaveBeenCalledExactlyOnceWith(7, expected);
    },
  );
  it.each(["", "Dead", "Process", "ab", "e\u0301", "\ud800", "\udfff"])(
    "ignores incomplete or non-character key events: %j",
    (input) => {
      const { ui, key } = session();
      expect(ui.key(input)).toBe(0);
      expect(key).not.toHaveBeenCalled();
    },
  );
  it("keeps Shift+Enter distinct from Enter and PageUp", () => {
    const { ui, key } = session();
    ui.key("Enter", true);
    ui.key("Enter");
    ui.key("PageUp");
    expect(key.mock.calls).toEqual([
      [7, 0x11000f],
      [7, 13],
      [7, 0x11000d],
    ]);
  });
});
