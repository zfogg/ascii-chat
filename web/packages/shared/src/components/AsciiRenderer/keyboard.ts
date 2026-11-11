/** Match platform/keyboard.h without colliding with Unicode scalar values. */
export function browserKeyCode(key: string, shift = false): number {
  const special: Record<string, number> = {
    Escape: 27,
    Enter: shift ? 0x11000f : 13,
    Tab: 9,
    PageUp: 0x11000d,
    PageDown: 0x11000e,
    F12: 0x11000c,
    Backspace: 127,
    Delete: 0x110004,
    Home: 0x110005,
    End: 0x110006,
    ArrowUp: 0x110000,
    ArrowDown: 0x110001,
    ArrowLeft: 0x110002,
    ArrowRight: 0x110003,
  };
  const scalar = Array.from(key).length === 1 ? key.codePointAt(0) : undefined;
  const text = scalar && !(scalar >= 0xd800 && scalar <= 0xdfff) ? scalar : 0;
  return special[key] ?? text;
}
