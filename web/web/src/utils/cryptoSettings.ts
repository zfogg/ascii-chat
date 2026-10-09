const STORAGE_KEY = "ascii-chat.crypto-settings.v1";

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

export function loadCryptoSettings(): CryptoSettings {
  if (typeof localStorage === "undefined") return DEFAULT_CRYPTO_SETTINGS;
  try {
    const parsed = JSON.parse(localStorage.getItem(STORAGE_KEY) || "null") as
      | Partial<CryptoSettings>
      | null;
    if (!parsed) return DEFAULT_CRYPTO_SETTINGS;
    return {
      ...DEFAULT_CRYPTO_SETTINGS,
      ...parsed,
      privateKeys: Array.isArray(parsed.privateKeys) ? parsed.privateKeys : [],
      verificationKeys: Array.isArray(parsed.verificationKeys)
        ? parsed.verificationKeys
        : [],
      activeVerificationKeyIds: parsed.activeVerificationKeyIds || {},
    };
  } catch {
    return DEFAULT_CRYPTO_SETTINGS;
  }
}

export function saveCryptoSettings(settings: CryptoSettings): void {
  localStorage.setItem(STORAGE_KEY, JSON.stringify(settings));
}

export function getActivePrivateKey(settings: CryptoSettings): string | undefined {
  if (!settings.authenticationEnabled || !settings.activePrivateKeyId) return;
  const key = settings.privateKeys.find(
    (candidate) => candidate.id === settings.activePrivateKeyId,
  );
  if (!key) return;
  return key.contents;
}

export function getActivePassword(settings: CryptoSettings): string | undefined {
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
