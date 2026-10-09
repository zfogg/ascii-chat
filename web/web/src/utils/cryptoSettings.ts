import { readUrlValue, writeUrlValue } from "./urlState";
const STORAGE_KEY = "ascii-chat.crypto-settings.v1";
export const MAX_CRYPTO_KEYS = 32;

export type VerificationKeyTarget = "client-server" | "discovery-service";

export interface StoredIdentityKey {
  id: string;
  name: string;
  contents: string;
}

export interface StoredVerificationKey {
  id: string;
  name: string;
  contents: string;
  target: VerificationKeyTarget;
}

export interface CryptoSettings {
  /** null follows the transport default: app crypto on for ws, off for wss. */
  customEncryption: boolean | null;
  authenticationEnabled: boolean;
  skipServerVerification: boolean;
  password: string;
  privateKeys: StoredIdentityKey[];
  activePrivateKeyId: string | null;
  verificationKeys: StoredVerificationKey[];
  activeVerificationKeyIds: Partial<Record<VerificationKeyTarget, string>>;
}

export const DEFAULT_CRYPTO_SETTINGS: CryptoSettings = {
  customEncryption: null,
  authenticationEnabled: true,
  skipServerVerification: false,
  password: "",
  privateKeys: [],
  activePrivateKeyId: null,
  verificationKeys: [],
  activeVerificationKeyIds: {},
};

function loadStoredCryptoSettings(): CryptoSettings {
  if (typeof localStorage === "undefined") return DEFAULT_CRYPTO_SETTINGS;
  try {
    const parsed = JSON.parse(
      localStorage.getItem(STORAGE_KEY) || "null",
    ) as Partial<CryptoSettings> | null;
    if (!parsed) return DEFAULT_CRYPTO_SETTINGS;
    const privateKeys = Array.isArray(parsed.privateKeys)
      ? parsed.privateKeys.slice(0, MAX_CRYPTO_KEYS)
      : [];
    const verificationKeys = Array.isArray(parsed.verificationKeys)
      ? parsed.verificationKeys.slice(0, MAX_CRYPTO_KEYS)
      : [];
    const activePrivateKeyId = privateKeys.some(
      (key) => key.id === parsed.activePrivateKeyId,
    )
      ? parsed.activePrivateKeyId || null
      : null;
    const activeVerificationKeyIds = {
      ...parsed.activeVerificationKeyIds,
    };
    for (const target of ["client-server", "discovery-service"] as const) {
      if (
        !verificationKeys.some(
          (key) =>
            key.id === activeVerificationKeyIds[target] &&
            key.target === target,
        )
      ) {
        delete activeVerificationKeyIds[target];
      }
    }
    return {
      ...DEFAULT_CRYPTO_SETTINGS,
      ...parsed,
      privateKeys,
      activePrivateKeyId,
      verificationKeys,
      activeVerificationKeyIds,
    };
  } catch {
    return DEFAULT_CRYPTO_SETTINGS;
  }
}

export function saveCryptoSettings(settings: CryptoSettings): void {
  writeCryptoUrl(settings);
  localStorage.setItem(
    STORAGE_KEY,
    JSON.stringify({
      ...settings,
      privateKeys: settings.privateKeys.slice(0, MAX_CRYPTO_KEYS),
      verificationKeys: settings.verificationKeys.slice(0, MAX_CRYPTO_KEYS),
    }),
  );
}

export function getActivePrivateKey(
  settings: CryptoSettings,
): string | undefined {
  if (!settings.authenticationEnabled || !settings.activePrivateKeyId) return;
  const key = settings.privateKeys.find(
    (candidate) => candidate.id === settings.activePrivateKeyId,
  );
  if (!key) return;
  return key.contents;
}

export function getActivePassword(
  settings: CryptoSettings,
): string | undefined {
  return settings.authenticationEnabled && settings.password
    ? settings.password
    : undefined;
}

export function getActiveVerificationKey(
  settings: CryptoSettings,
  target: VerificationKeyTarget,
): string | undefined {
  const activeId = settings.activeVerificationKeyIds[target];
  if (!activeId) return;
  const key = settings.verificationKeys.find(
    (candidate) => candidate.id === activeId && candidate.target === target,
  );
  if (!key) return;
  return key.contents;
}

// Key material stays in browser storage; URLs carry selections and controls.
export function readCryptoUrl(settings: CryptoSettings): CryptoSettings {
  const controls = readUrlValue("cryptoOptions", {
    customEncryption: settings.customEncryption,
    authenticationEnabled: settings.authenticationEnabled,
    skipServerVerification: settings.skipServerVerification,
    activePrivateKeyId: settings.activePrivateKeyId,
    activeVerificationKeyIds: settings.activeVerificationKeyIds,
  });
  const encryption = readUrlValue("cryptoOptions", {
    customEncryption: null as boolean | null,
  });
  if (
    new URLSearchParams(window.location.search).has("cryptoOptions") ||
    new URLSearchParams(window.location.search).has("customEncryption")
  ) {
    controls.customEncryption = encryption.customEncryption;
  }
  return {
    ...settings,
    ...controls,
    password: readUrlValue("password", settings.password, true),
  };
}
export function writeCryptoUrl(settings: CryptoSettings): void {
  const defaults = loadStoredCryptoSettings();
  writeUrlValue(
    "cryptoOptions",
    {
      customEncryption: settings.customEncryption,
      authenticationEnabled: settings.authenticationEnabled,
      skipServerVerification: settings.skipServerVerification,
      activePrivateKeyId: settings.activePrivateKeyId,
      activeVerificationKeyIds: settings.activeVerificationKeyIds,
    },
    false,
    defaults,
  );
  writeUrlValue("password", settings.password, true, defaults.password);
}
export function loadCryptoSettings(): CryptoSettings {
  let settings = loadStoredCryptoSettings();
  if (readUrlValue("crypto", false)) {
    try {
      const draft = JSON.parse(
        sessionStorage.getItem("ascii-chat.crypto-draft") || "null",
      ) as CryptoSettings | null;
      if (draft)
        settings = {
          ...settings,
          privateKeys: draft.privateKeys,
          verificationKeys: draft.verificationKeys,
        };
    } catch {
      // Ignore an unavailable or malformed draft; saved keys remain usable.
    }
  }
  return readCryptoUrl(settings);
}
