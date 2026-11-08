import type { MirrorModule } from "./mirror";
import type { MainModule, SessionUiBinding } from "./generated/mirror-bindings";

export interface SessionUiModule extends Pick<
  MainModule,
  | "SessionUiBinding"
  | "MirrorFrameBinding"
  | "TerminalRendererBinding"
  | "initMirror"
  | "optionHelp"
  | "TestPatternBinding"
> {
  _wasm_ui_volume(): number;
  _wasm_ui_fps(): number;
  _wasm_ui_audio(enabled: number): number;
}

export interface SessionUiEvent {
  action: number;
  key: string;
  module: MirrorModule;
}

/** Owns one C UI instance. It never owns the WASM module or terminal renderer. */
export class SessionUi {
  private binding: SessionUiBinding | null;
  constructor(readonly module: MirrorModule) {
    const binding = new module.SessionUiBinding();
    if (!binding.valid()) {
      binding.delete();
      throw new Error("Cannot create session UI");
    }
    this.binding = binding;
  }
  private getBinding(): SessionUiBinding {
    if (!this.binding) throw new Error("Session UI is destroyed");
    return this.binding;
  }
  resize(cols: number, rows: number) {
    this.getBinding().resize(cols, rows);
  }
  loading(loading: boolean, message: string) {
    this.getBinding().loading(loading, message);
  }
  log(message: string) {
    this.getBinding().log(message);
  }
  media(text: string) {
    const result = this.getBinding().media(text);
    if (result !== 0) throw new Error("Session UI rejected media frame");
  }
  key(key: string, shift = false): number {
    const binding = this.getBinding();
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
    const scalar =
      Array.from(key).length === 1 ? key.codePointAt(0) : undefined;
    // Match platform/keyboard.h: text is a scalar; special keys are above Unicode.
    const text = scalar && !(scalar >= 0xd800 && scalar <= 0xdfff) ? scalar : 0;
    const code = special[key] ?? text;
    return code ? binding.key(code) : 0;
  }
  tick(now: number): string | null {
    return this.getBinding().tick(now) ?? null;
  }
  redraw() {
    this.getBinding().redraw();
  }
  clear() {
    this.getBinding().clear();
  }
  destroy() {
    this.binding?.delete();
    this.binding = null;
  }
}
