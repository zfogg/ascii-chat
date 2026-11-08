import { describe, expect, it, vi } from "vite-plus/test";
import { SessionUi } from "../../../packages/shared/src/wasm/sessionUi";
import type { MirrorModule } from "../../../packages/shared/src/wasm/mirror";

function session() {
  const key = vi.fn(() => 1);
  const module = {
    SessionUiBinding: class {
      valid() {
        return true;
      }
      key = key;
      delete() {}
    },
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
      expect(key).toHaveBeenCalledExactlyOnceWith(expected);
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
    expect(key.mock.calls).toEqual([[0x11000f], [13], [0x11000d]]);
  });
});

describe("session UI ownership", () => {
  it("disposes the binding once and rejects later use", () => {
    const dispose = vi.fn();
    const module = {
      SessionUiBinding: class {
        valid() {
          return true;
        }
        delete = dispose;
      },
    } as unknown as MirrorModule;
    const ui = new SessionUi(module);
    ui.destroy();
    ui.destroy();
    expect(dispose).toHaveBeenCalledOnce();
    expect(() => ui.resize(80, 24)).toThrow("destroyed");
  });

  it("disposes an unsuccessfully created binding", () => {
    const dispose = vi.fn();
    const module = {
      SessionUiBinding: class {
        valid() {
          return false;
        }
        delete = dispose;
      },
    } as unknown as MirrorModule;
    expect(() => new SessionUi(module)).toThrow("Cannot create session UI");
    expect(dispose).toHaveBeenCalledOnce();
  });
});
