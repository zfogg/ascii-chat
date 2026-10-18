import { getClientModule } from "./client";

/** Device APIs stay in the host; stream state and cancellation live in shared C. */
export class AVState {
  private module: ReturnType<typeof getClientModule> = null;
  private handle = 0;

  begin(): number {
    if (!this.handle) {
      this.module = getClientModule();
      if (!this.module) throw new Error("Initialize the client before capture");
      this.handle = this.module._web_av_state_create();
      if (!this.handle) throw new Error("Unable to allocate audio/video state");
    }
    return this.module!._session_av_state_begin(this.handle);
  }

  get generation(): number {
    return this.handle ? this.module!._web_av_state_generation(this.handle) : 0;
  }

  current(generation: number): boolean {
    return (
      !!this.handle &&
      !!this.module!._session_av_state_is_current(this.handle, generation)
    );
  }

  complete(generation: number): boolean {
    return (
      !!this.handle &&
      !!this.module!._session_av_state_complete(this.handle, generation)
    );
  }

  get active(): boolean {
    return (
      !!this.handle && !!this.module!._session_av_state_is_active(this.handle)
    );
  }

  stop(release: () => void): void {
    if (this.handle) this.module!._session_av_state_begin_stop(this.handle);
    release();
    if (this.handle) this.module!._session_av_state_finish_stop(this.handle);
  }

  /** Call only after pending host startup has settled and resources are stopped. */
  dispose(): void {
    if (this.handle) this.module!._web_av_state_destroy(this.handle);
    this.handle = 0;
    this.module = null;
  }
}
