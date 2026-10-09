import { beforeEach, expect, test } from "vite-plus/test";
import { act, renderHook } from "@testing-library/react";
import { useUrlState } from "../src/hooks/useUrlState";
import {
  migrateLegacyUrlState,
  readUrlValue,
  writeUrlValue,
} from "../src/utils/urlState";
import {
  DEFAULT_CRYPTO_SETTINGS,
  readCryptoUrl,
  writeCryptoUrl,
} from "../src/utils/cryptoSettings";

beforeEach(() => {
  localStorage.clear();
  window.history.replaceState(
    { router: "preserved" },
    "",
    "/client?unrelated=keep",
  );
});

test("restores settings and dialog state after remount without losing adjacent updates", () => {
  const first = renderHook(() => ({
    settings: useUrlState("settings", false),
    devices: useUrlState("devices", false),
    render: useUrlState("render", {
      width: 640,
      flipX: true,
      paletteChars: "# & +",
    }),
  }));
  act(() => {
    first.result.current.settings[1](true);
    first.result.current.devices[1](true);
    first.result.current.render[1]({
      width: 800,
      flipX: false,
      paletteChars: "░ # & +",
    });
  });
  const params = new URLSearchParams(location.search);
  expect(params.has("render")).toBe(false);
  expect(params.get("width")).toBe("800");
  expect(params.get("flipX")).toBe("false");
  expect(params.get("paletteChars")).toBe("░ # & +");
  first.unmount();
  const restored = renderHook(() =>
    useUrlState("render", { width: 640, flipX: true, paletteChars: "" }),
  );
  expect(restored.result.current[0]).toEqual({
    width: 800,
    flipX: false,
    paletteChars: "░ # & +",
  });
  expect(readUrlValue("settings", false)).toBe(true);
  expect(readUrlValue("devices", false)).toBe(true);
  expect(new URLSearchParams(location.search).get("unrelated")).toBe("keep");
  expect(history.state).toEqual({ router: "preserved" });
  restored.unmount();
});

test("crypto controls round trip while passwords stay in the fragment", () => {
  const settings = {
    ...DEFAULT_CRYPTO_SETTINGS,
    customEncryption: false,
    password: "test #+& Unicode ░",
  };
  writeCryptoUrl(settings);
  expect(location.search).not.toContain("password");
  expect(new URLSearchParams(location.hash.slice(1)).get("password")).toBe(
    settings.password,
  );
  expect(readCryptoUrl(DEFAULT_CRYPTO_SETTINGS)).toEqual(settings);
  writeCryptoUrl({ ...settings, password: "" });
  expect(readCryptoUrl(DEFAULT_CRYPTO_SETTINGS).password).toBe("");
});

test("malformed values fall back and explicit false survives", () => {
  writeUrlValue("render", { width: "wrong", flipX: false, extra: true });
  expect(readUrlValue("render", { width: 640, flipX: true })).toEqual({
    width: 640,
    flipX: false,
  });
  writeUrlValue("settings", "broken");
  expect(readUrlValue("settings", false)).toBe(false);
});

test("history navigation restores URL state", () => {
  const hook = renderHook(() => useUrlState("settings", false));
  act(() => {
    writeUrlValue("settings", true);
    window.dispatchEvent(new PopStateEvent("popstate"));
  });
  expect(hook.result.current[0]).toBe(true);
  hook.unmount();
});

test("upgrades JSON links without losing settings or overriding explicit fields", () => {
  const url = new URL(location.href);
  url.searchParams.set("render", JSON.stringify({ width: 800, flipX: false }));
  url.searchParams.set(
    "mediaDevices",
    JSON.stringify({
      cameraId: "camera & one",
      microphoneId: "mic",
      speakerId: "default",
    }),
  );
  url.searchParams.set(
    "cryptoOptions",
    JSON.stringify({
      customEncryption: true,
      activeVerificationKeyIds: { "client-server": "key-one" },
    }),
  );
  url.searchParams.set("width", "900");
  url.hash = "password=test%23password";
  history.replaceState(history.state, "", url);
  const defaults = { width: 640, flipX: true };
  expect(readUrlValue("render", defaults)).toEqual({
    width: 900,
    flipX: false,
  });
  migrateLegacyUrlState();
  const params = new URLSearchParams(location.search);
  for (const group of ["render", "cryptoOptions", "mediaDevices"])
    expect(params.has(group)).toBe(false);
  expect(readUrlValue("render", defaults)).toEqual({
    width: 900,
    flipX: false,
  });
  expect(params.get("cameraId")).toBe("camera & one");
  expect(params.get("activeVerificationKeyIds.client-server")).toBe("key-one");
  expect(location.hash).toBe("#password=test%23password");
});

test("clearing or replacing selected keys does not restore saved selections", () => {
  const saved = {
    ...DEFAULT_CRYPTO_SETTINGS,
    activeVerificationKeyIds: { "client-server": "old" },
  };
  localStorage.setItem(
    "ascii-chat.crypto-settings.v1",
    JSON.stringify({
      ...saved,
      verificationKeys: [
        {
          id: "old",
          name: "Saved key",
          contents: "test public key",
          target: "client-server",
        },
      ],
    }),
  );
  writeCryptoUrl({
    ...saved,
    activeVerificationKeyIds: { "discovery-service": "new" },
  });
  expect(readCryptoUrl(saved).activeVerificationKeyIds).toEqual({
    "discovery-service": "new",
  });
  writeCryptoUrl({ ...saved, activeVerificationKeyIds: {} });
  expect(readCryptoUrl(saved).activeVerificationKeyIds).toEqual({});
  expect(new URLSearchParams(location.search).has("cryptoOptions")).toBe(false);
});

test("only changed settings are written and resetting them removes the params", () => {
  const hook = renderHook(() => ({
    render: useUrlState("render", { width: 640, height: 480, flipX: true }),
    settings: useUrlState("settings", false),
  }));
  expect(location.search).toBe("?unrelated=keep");
  act(() => {
    hook.result.current.render[1]({ width: 800, height: 480, flipX: true });
    hook.result.current.settings[1](true);
  });
  expect(new URLSearchParams(location.search).get("width")).toBe("800");
  expect(new URLSearchParams(location.search).has("height")).toBe(false);
  expect(new URLSearchParams(location.search).has("flipX")).toBe(false);
  act(() => {
    hook.result.current.render[1]({ width: 640, height: 480, flipX: true });
    hook.result.current.settings[1](false);
  });
  expect(location.search).toBe("?unrelated=keep");
  hook.unmount();
});

test("existing default params are removed on mount", () => {
  history.replaceState(
    null,
    "",
    "/client?width=640&height=480&flipX=true&settings=false&other=keep",
  );
  const hook = renderHook(() => ({
    render: useUrlState("render", { width: 640, height: 480, flipX: true }),
    settings: useUrlState("settings", false),
  }));
  expect(location.search).toBe("?other=keep");
  hook.unmount();
});
