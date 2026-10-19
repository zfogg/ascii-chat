import { afterEach, describe, expect, it, vi } from "vite-plus/test";
import {
  RuntimeOptionsStore,
  type RuntimeOptionsModule,
} from "@ascii-chat/shared/wasm";
import { RenderSettingsControl } from "../src/network/RenderSettings";
import type { ClientSession } from "../src/network/Transport";

function engine() {
  let revision = 1;
  let values = { fps: 30, flip_x: false };
  let nextPointer = 1;
  const strings = new Map<number, string>();
  const release = vi.fn((pointer: number) => strings.delete(pointer));
  const module: RuntimeOptionsModule = {
    _wasm_options_revision: () => revision,
    _wasm_options_values: () => {
      const pointer = nextPointer++;
      strings.set(pointer, JSON.stringify({ revision, values }));
      return pointer;
    },
    _wasm_options_patch: (pointer, expected) => {
      const patch = JSON.parse(strings.get(pointer)!) as Partial<typeof values>;
      if (
        (expected !== -1 && expected !== revision) ||
        (patch.fps !== undefined && patch.fps < 1)
      )
        return 1;
      values = { ...values, ...patch };
      revision++;
      return 0;
    },
    _wasm_options_release: release,
    _malloc: () => nextPointer++,
    _free: release,
    UTF8ToString: (pointer) => strings.get(pointer)!,
    stringToUTF8: (value, pointer) => {
      strings.set(pointer, value);
    },
    lengthBytesUTF8: (value) => new TextEncoder().encode(value).length,
  };
  return {
    store: new RuntimeOptionsStore(module),
    module,
    strings,
    change: () => {
      values = { ...values, flip_x: !values.flip_x };
      revision++;
    },
  };
}

afterEach(() => vi.unstubAllGlobals());
describe("C runtime settings bridge", () => {
  it("observes C changes and keeps independent WASM instances isolated", () => {
    let frame: FrameRequestCallback | undefined;
    vi.stubGlobal("requestAnimationFrame", (callback: FrameRequestCallback) => {
      frame = callback;
      return 1;
    });
    const cancel = vi.fn();
    vi.stubGlobal("cancelAnimationFrame", cancel);
    const first = engine(),
      second = engine();
    const initial = first.store.getSnapshot();
    expect(first.store.getSnapshot()).toBe(initial);
    const listener = vi.fn();
    const unsubscribe = first.store.subscribe(listener);
    first.change();
    frame!(0);
    expect(listener).toHaveBeenCalledOnce();
    expect(first.store.getSnapshot().values["flip_x"]).toBe(true);
    expect(second.store.getSnapshot().values["flip_x"]).toBe(false);
    unsubscribe();
    expect(cancel).toHaveBeenCalledOnce();
  });
  it("retains committed snapshots on rejection and releases all temporary memory", () => {
    const { store, strings } = engine();
    const initial = store.getSnapshot();
    expect(() => store.applyPatch({ fps: 0 })).toThrow("rejected");
    expect(store.getSnapshot()).toBe(initial);
    store.applyPatch({ fps: 60 }, initial.revision);
    expect(() => store.applyPatch({ fps: 20 }, initial.revision)).toThrow(
      "rejected",
    );
    expect(store.getSnapshot().values["fps"]).toBe(60);
    expect(strings.size).toBe(0);
  });
});

function reply(request: number, status = 0, revision = 7) {
  const json = new TextEncoder().encode('{"fps":30}');
  const payload = new Uint8Array(20 + json.length);
  const view = new DataView(payload.buffer);
  view.setUint32(0, 1);
  view.setUint32(4, request);
  view.setUint32(12, revision);
  view.setUint32(16, status);
  payload.set(json, 20);
  return payload;
}
describe("remote render request correlation", () => {
  it("accepts a new host's lower revision with a fresh connection control", () => {
    const connection = { sendPacket: vi.fn() } as unknown as ClientSession;
    const commit = vi.fn(() => ({ fps: 30 }));
    const previous = new RenderSettingsControl(connection, commit);
    previous.send({ fps: 30 });
    expect(previous.receive(reply(1, 0, 100))).not.toBeNull();
    const reconnected = new RenderSettingsControl(connection, commit);
    reconnected.send({ fps: 30 });
    expect(reconnected.receive(reply(1, 0, 1))).toMatchObject({ revision: 1 });
  });
  it("ignores old replies before they can replace C effective state", () => {
    const sendPacket = vi.fn();
    const commit = vi.fn(() => ({ fps: 30 }));
    const control = new RenderSettingsControl(
      { sendPacket } as unknown as ClientSession,
      commit,
    );
    control.send({ fps: 60 });
    control.send({ fps: 30 });
    expect(control.receive(reply(1))).toBeNull();
    expect(commit).not.toHaveBeenCalled();
    expect(control.receive(reply(2))).toMatchObject({
      request: 2,
      revision: 7,
      status: 0,
      values: { fps: 30 },
    });
    expect(commit).toHaveBeenCalledOnce();
    expect(control.receive(reply(2))).toBeNull();
    expect(
      new DataView(
        (sendPacket.mock.calls[1]![1] as Uint8Array).buffer,
      ).getUint32(8),
    ).toBe(0xffff_ffff);
  });
  it("reports rejection with effective values and rejects incompatible schemas", () => {
    const control = new RenderSettingsControl(
      { sendPacket: vi.fn() } as unknown as ClientSession,
      () => ({ fps: 30 }),
    );
    control.send({ fps: 0 });
    expect(control.receive(reply(1, 5))).toMatchObject({
      status: 5,
      values: { fps: 30 },
    });
    const invalid = reply(2);
    new DataView(invalid.buffer).setUint32(0, 2);
    expect(() => control.receive(invalid)).toThrow("schema");
  });
});
