import type { MirrorModule } from "./mirror";

export interface SessionUiModule {
  _wasm_ui_volume(): number;
  _wasm_ui_fps(): number;
  _wasm_ui_audio(enabled: number): number;
  _wasm_ui_create(): number;
  _wasm_ui_destroy(session: number): void;
  _wasm_ui_resize(session: number, cols: number, rows: number): void;
  _wasm_ui_loading(session: number, loading: number, message: number): void;
  _wasm_ui_log(session: number, message: number): void;
  _wasm_ui_media(session: number, text: number, length: number): number;
  _wasm_ui_key(session: number, key: number): number;
  _wasm_ui_tick(session: number, milliseconds: number): number;
  _wasm_ui_clear(session: number): void;
  _wasm_ui_redraw(session: number): void;
  _wasm_ui_capture_logs(session: number): void;
}

export interface SessionUiEvent {
  action: number;
  key: string;
  module: MirrorModule;
}

/** Owns one C UI instance. It never owns the WASM module or terminal renderer. */
export class SessionUi {
  private pointer: number;
  constructor(readonly module: MirrorModule) {
    this.pointer = module._wasm_ui_create();
    if (!this.pointer) throw new Error("Cannot create session UI");
    module._wasm_ui_capture_logs(this.pointer);
  }
  private withText<T>(
    text: string,
    callback: (pointer: number, length: number) => T,
  ): T {
    const length = this.module.lengthBytesUTF8(text);
    const pointer = this.module._malloc(length + 1);
    if (!pointer) throw new Error("Cannot allocate UI text");
    try {
      this.module.stringToUTF8(text, pointer, length + 1);
      return callback(pointer, length);
    } finally {
      this.module._free(pointer);
    }
  }
  resize(cols: number, rows: number) {
    this.module._wasm_ui_resize(this.pointer, cols, rows);
  }
  loading(loading: boolean, message: string) {
    this.withText(message, (p) =>
      this.module._wasm_ui_loading(this.pointer, Number(loading), p),
    );
  }
  log(message: string) {
    this.withText(message, (p) => this.module._wasm_ui_log(this.pointer, p));
  }
  media(text: string) {
    const result = this.withText(text, (p, n) =>
      this.module._wasm_ui_media(this.pointer, p, n),
    );
    if (result !== 0) throw new Error("Session UI rejected media frame");
  }
  key(key: string): number {
    const special: Record<string, number> = {
      Escape: 27,
      Enter: 13,
      Backspace: 127,
      Delete: 260,
      Home: 261,
      End: 262,
      ArrowUp: 256,
      ArrowDown: 257,
      ArrowLeft: 258,
      ArrowRight: 259,
    };
    const code = special[key] ?? (key.length === 1 ? key.charCodeAt(0) : 0);
    return code ? this.module._wasm_ui_key(this.pointer, code) : 0;
  }
  tick(now: number): string | null {
    const p = this.module._wasm_ui_tick(this.pointer, now);
    return p ? this.module.UTF8ToString(p) : null;
  }
  redraw() {
    this.module._wasm_ui_redraw(this.pointer);
  }
  clear() {
    this.module._wasm_ui_clear(this.pointer);
  }
  destroy() {
    if (this.pointer) this.module._wasm_ui_destroy(this.pointer);
    this.pointer = 0;
  }
}
