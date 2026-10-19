import type { WasmModule } from "./common/optionsWrapper";

/** Terminal exports shared by the mirror and client WASM builds. */
export interface TerminalRendererModule extends WasmModule {
  _mirror_init_with_args?(args: number): number;
  _client_init_with_args?(args: number): number;
  _term_renderer_create(config: number, output: number): number;
  _term_renderer_set_zalgo(renderer: number, level: number): number;
  _term_renderer_feed(renderer: number, text: number, length: number): number;
  _term_renderer_pixels(renderer: number): number;
  _term_renderer_width_px(renderer: number): number;
  _term_renderer_height_px(renderer: number): number;
  _term_renderer_pitch(renderer: number): number;
  _term_renderer_destroy(renderer: number): void;
  _get_font_default_ptr(selection: number): number;
  _get_font_default_size(selection: number): number;
}
