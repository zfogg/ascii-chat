import { useEffect, useState } from "react";
import "./SecuritySetupModal.css";
import {
  validateSshPrivateKey,
  validateSshPublicKey,
} from "../wasm/client";
import type {
  CryptoSettings,
  StoredIdentityKey,
  StoredVerificationKey,
  VerificationKeyTarget,
} from "../utils/cryptoSettings";

interface Props {
  open: boolean;
  settings: CryptoSettings;
  defaultEncryptionEnabled: boolean;
  onClose: () => void;
  onSave: (settings: CryptoSettings) => void;
}

async function sshEd25519Fingerprint(publicKey: Uint8Array): Promise<string> {
  const algorithm = new TextEncoder().encode("ssh-ed25519");
  const blob = new Uint8Array(4 + algorithm.length + 4 + publicKey.length);
  const view = new DataView(blob.buffer);
  view.setUint32(0, algorithm.length, false);
  blob.set(algorithm, 4);
  view.setUint32(4 + algorithm.length, publicKey.length, false);
  blob.set(publicKey, 8 + algorithm.length);
  const digest = new Uint8Array(
    await crypto.subtle.digest("SHA-256", blob.buffer as ArrayBuffer),
  );
  return `SHA256:${btoa(String.fromCharCode(...digest)).replace(/=+$/, "")}`;
}

function newId(): string {
  return globalThis.crypto?.randomUUID?.() || `${Date.now()}-${Math.random()}`;
}

export function SecuritySetupModal({
  open,
  settings,
  defaultEncryptionEnabled,
  onClose,
  onSave,
}: Props) {
  const [draft, setDraft] = useState(settings);
  const [privateName, setPrivateName] = useState("");
  const [privateContents, setPrivateContents] = useState("");
  const [privateError, setPrivateError] = useState("");
  const [authError, setAuthError] = useState("");
  const [verificationName, setVerificationName] = useState("");
  const [verificationContents, setVerificationContents] = useState("");
  const [verificationTarget, setVerificationTarget] =
    useState<VerificationKeyTarget>("client-server");
  const [verificationError, setVerificationError] = useState("");
  const [fingerprints, setFingerprints] = useState<Record<string, string>>({});
  const [privateEntryOpen, setPrivateEntryOpen] = useState(false);
  const [verificationEntryOpen, setVerificationEntryOpen] = useState(false);

  useEffect(() => {
    if (open) setDraft(settings);
  }, [open, settings]);

  useEffect(() => {
    let active = true;
    void Promise.all(
      settings.verificationKeys.map(async (key) => {
        try {
          const raw = await validateSshPublicKey(key.contents);
          return [key.id, await sshEd25519Fingerprint(raw)] as const;
        } catch {
          return [key.id, "Invalid key"] as const;
        }
      }),
    ).then((entries) => {
      if (active) setFingerprints(Object.fromEntries(entries));
    });
    return () => {
      active = false;
    };
  }, [settings.verificationKeys]);

  if (!open) return null;

  const effectiveEncryption =
    draft.customEncryption ?? defaultEncryptionEnabled;
  const update = (changes: Partial<CryptoSettings>) =>
    setDraft((current) => ({ ...current, ...changes }));

  const addPrivateKey = async () => {
    try {
      await validateSshPrivateKey(privateContents);
      const key: StoredIdentityKey = {
        id: newId(),
        name: privateName.trim() || `Identity ${draft.privateKeys.length + 1}`,
        contents: privateContents.trim(),
      };
      update({
        privateKeys: [...draft.privateKeys, key],
        activePrivateKeyId: key.id,
      });
      setPrivateName("");
      setPrivateContents("");
      setPrivateError("");
      setPrivateEntryOpen(false);
    } catch (error) {
      setPrivateError(error instanceof Error ? error.message : String(error));
    }
  };

  const addVerificationKey = async () => {
    try {
      const parsedKey = await validateSshPublicKey(verificationContents);
      const key: StoredVerificationKey = {
        id: newId(),
        name:
          verificationName.trim() ||
          `Verification key ${draft.verificationKeys.length + 1}`,
        contents: verificationContents.trim(),
        target: verificationTarget,
      };
      const fingerprint = await sshEd25519Fingerprint(parsedKey);
      setFingerprints((current) => ({ ...current, [key.id]: fingerprint }));
      update({
        verificationKeys: [...draft.verificationKeys, key],
        activeVerificationKeyIds: {
          ...draft.activeVerificationKeyIds,
          [verificationTarget]: key.id,
        },
      });
      setVerificationName("");
      setVerificationContents("");
      setVerificationError("");
      setVerificationEntryOpen(false);
    } catch (error) {
      setVerificationError(error instanceof Error ? error.message : String(error));
    }
  };

  const readFile = async (
    file: File | undefined,
    setContents: (value: string) => void,
    setName: (value: string) => void,
    validate: (value: string) => void | Promise<void>,
  ) => {
    if (!file) return;
    const contents = await file.text();
    setContents(contents);
    setName(file.name);
    await validate(contents);
  };

  const privateRows = draft.privateKeys.map((key) => {
    const active = key.id === draft.activePrivateKeyId;
    return (
      <div key={key.id} className={`crypto-key-row${active ? " is-active" : ""}`}>
        <span className="crypto-key-name">
          <strong>{key.name}</strong>
          <small>PRIVATE IDENTITY · OPENSSH ED25519</small>
        </span>
        <button
          type="button"
          className="crypto-row-button"
          disabled={active}
          onClick={() => update({ activePrivateKeyId: key.id })}
        >
          {active ? "Active" : "Set active"}
        </button>
        <button
          type="button"
          className="crypto-row-button is-delete"
          onClick={() =>
            update({
              privateKeys: draft.privateKeys.filter((item) => item.id !== key.id),
              activePrivateKeyId: active ? null : draft.activePrivateKeyId,
            })
          }
        >
          Delete key
        </button>
      </div>
    );
  });

  const verificationRows = draft.verificationKeys.map((key) => {
    const active = draft.activeVerificationKeyIds[key.target] === key.id;
    const targetLabel =
      key.target === "client-server" ? "CHAT SERVER" : "DISCOVERY SERVICE";
    return (
      <div key={key.id} className={`crypto-key-row${active ? " is-active" : ""}`}>
        <span className="crypto-key-name">
          <strong>{key.name}</strong>
          <small>
            {targetLabel} · {fingerprints[key.id] || "ED25519 PUBLIC KEY"}
          </small>
        </span>
        <button
          type="button"
          className="crypto-row-button"
          disabled={active}
          onClick={() =>
            update({
              activeVerificationKeyIds: {
                ...draft.activeVerificationKeyIds,
                [key.target]: key.id,
              },
            })
          }
        >
          {active ? "Active" : "Set active"}
        </button>
        <button
          type="button"
          className="crypto-row-button is-delete"
          onClick={() => {
            const activeVerificationKeyIds = {
              ...draft.activeVerificationKeyIds,
            };
            if (activeVerificationKeyIds[key.target] === key.id) {
              delete activeVerificationKeyIds[key.target];
            }
            update({
              verificationKeys: draft.verificationKeys.filter(
                (item) => item.id !== key.id,
              ),
              activeVerificationKeyIds,
            });
          }}
        >
          Delete key
        </button>
      </div>
    );
  });

  const passwordError =
    draft.authenticationEnabled &&
    draft.password.length > 0 &&
    (draft.password.length < 8 || draft.password.length > 255);

  return (
    <div
      className="crypto-scrim"
      onMouseDown={(event) => {
        if (event.target === event.currentTarget) onClose();
      }}
    >
      <section
        role="dialog"
        aria-modal="true"
        aria-labelledby="crypto-setup-title"
        className="crypto-modal"
      >
        <header className="crypto-modal-head">
          <div>
            <h2 id="crypto-setup-title">Crypto</h2>
            <p>Set up ascii-chat encryption, authentication, and server verification.</p>
          </div>
          <button
            type="button"
            aria-label="Close"
            className="crypto-close"
            onClick={onClose}
          >
            ×
          </button>
        </header>

        <div className="crypto-scope">
          <span className="crypto-scope-mark">●</span>
          <span>
            <b>Shared browser settings</b>
            &nbsp;Used by Client and Discovery modes on this site.
          </span>
        </div>

        <div className="crypto-modal-body">
          <section className="crypto-section">
            <div className="crypto-section-head">
              <h3>Encryption and authentication</h3>
              <p>Controls the ascii-chat security layer</p>
            </div>
            <div className="crypto-section-content">
              <div className="crypto-toggle-row">
                <div className="crypto-toggle-copy">
                  <strong>ascii-chat&apos;s custom end-to-end encryption</strong>
                  <small>Encrypt ACIP traffic between this browser and the peer.</small>
                  <small>
                    If you&apos;re using WebRTC or WSS, you probably won&apos;t need this
                    for privacy because those transports are already encrypted.
                    You can still use authentication to verify identities.
                  </small>
                </div>
                <button
                  type="button"
                  role="switch"
                  aria-checked={effectiveEncryption}
                  aria-label="ascii-chat's custom end-to-end encryption"
                  className="crypto-switch"
                  onClick={() =>
                    update({ customEncryption: !effectiveEncryption })
                  }
                />
              </div>

              <div className="crypto-toggle-row">
                <div className="crypto-toggle-copy">
                  <strong>Require authentication</strong>
                  <small>
                    Prove identity with a password, an identity key, or both when
                    requested.
                  </small>
                </div>
                <button
                  type="button"
                  role="switch"
                  aria-checked={draft.authenticationEnabled}
                  aria-label="Require authentication"
                  className="crypto-switch"
                  onClick={() =>
                    update({ authenticationEnabled: !draft.authenticationEnabled })
                  }
                />
              </div>

              <div className="crypto-field">
                <label htmlFor="crypto-password">Shared password</label>
                <input
                  id="crypto-password"
                  aria-label="Custom crypto password"
                  type="password"
                  autoComplete="new-password"
                  minLength={8}
                  maxLength={255}
                  placeholder="Enter shared password"
                  value={draft.password}
                  onChange={(event) => {
                    setAuthError("");
                    update({ password: event.target.value });
                  }}
                />
                <p className="crypto-help">
                  Optional. Use the same password as the server. Password plus
                  identity key enables both checks.
                </p>
                {(authError || passwordError) && (
                  <p role="alert" className="crypto-error">
                    {authError ||
                      "Use a password between 8 and 255 characters, or clear the field."}
                  </p>
                )}
              </div>

              <div className="crypto-field">
                <span className="crypto-field-label">Private keys</span>
                <p className="crypto-help">
                  Choose which identity this browser presents. Keep multiple keys
                  here and switch without importing again.
                </p>
                <div className="crypto-key-actions">
                  <span className="crypto-help">Active key is shared by Client and Discovery.</span>
                  <button
                    type="button"
                    className="crypto-add"
                    aria-expanded={privateEntryOpen}
                    onClick={() => setPrivateEntryOpen((value) => !value)}
                  >
                    ＋ Add private key
                  </button>
                </div>
                <div className="crypto-key-list">
                  {privateRows.length ? (
                    privateRows
                  ) : (
                    <p className="crypto-help">No private keys added yet.</p>
                  )}
                </div>
                {privateEntryOpen && (
                  <div className="crypto-key-entry">
                    <div className="crypto-field-label">
                      SSH Ed25519 private key
                    </div>
                    <div className="crypto-input-action">
                      <input
                        aria-label="Private key name"
                        type="text"
                        placeholder="Name this key"
                        value={privateName}
                        onChange={(event) => setPrivateName(event.target.value)}
                      />
                      <button
                        type="button"
                        className="crypto-add"
                        onClick={() =>
                          document.getElementById("private-key-file")?.click()
                        }
                      >
                        Choose file…
                      </button>
                      <input
                        id="private-key-file"
                        aria-label="Private key file"
                        type="file"
                        accept=".pem,.key,.asc,.gpg"
                        hidden
                        onChange={(event) =>
                          void readFile(
                            event.target.files?.[0],
                            setPrivateContents,
                            setPrivateName,
                            async (contents) => {
                              try {
                                await validateSshPrivateKey(contents);
                                setPrivateError("");
                              } catch (error) {
                                setPrivateError(
                                  error instanceof Error
                                    ? error.message
                                    : String(error),
                                );
                              }
                            },
                          )
                        }
                      />
                    </div>
                    <div className="crypto-field">
                      <label htmlFor="private-key-text">Or paste key contents</label>
                      <textarea
                        id="private-key-text"
                        aria-label="Private key text"
                        placeholder="-----BEGIN OPENSSH PRIVATE KEY-----"
                        value={privateContents}
                        onChange={(event) => setPrivateContents(event.target.value)}
                      />
                      {privateError && (
                        <p role="alert" className="crypto-error">{privateError}</p>
                      )}
                    </div>
                    <div className="crypto-key-actions">
                      <span className="crypto-help">Key contents stay in this browser profile.</span>
                      <button
                        type="button"
                        className="crypto-add"
                        onClick={() => void addPrivateKey()}
                      >
                        Add to list
                      </button>
                    </div>
                  </div>
                )}
              </div>

              <div className="crypto-warning">
                <span>◆</span>
                <span>
                  <b>Private key storage:</b> saving remembers the key contents
                  in this browser&apos;s local storage so both modes can use them.
                  Anyone with access to this browser profile can read them. Use a
                  dedicated key without a passphrase for this browser.
                </span>
              </div>
            </div>

            <div className="crypto-section-head is-subsection">
              <h3>Verify who you connect to</h3>
              <p>Public keys only</p>
            </div>
            <div className="crypto-section-content">
              <div className="crypto-field">
                <span className="crypto-field-label">Server verification keys</span>
                <p className="crypto-help">
                  Select the trusted public key for each service. An unmatched
                  identity is rejected.
                </p>
                <div className="crypto-key-actions is-verification-actions">
                  <span className="crypto-help">Active keys are shared by Client and Discovery.</span>
                  <button
                    type="button"
                    className="crypto-add"
                    aria-expanded={verificationEntryOpen}
                    onClick={() => setVerificationEntryOpen((value) => !value)}
                  >
                    ＋ Add verification key
                  </button>
                </div>
                <div className="crypto-key-list">
                  {verificationRows.length ? (
                    verificationRows
                  ) : (
                    <p className="crypto-help">No server verification keys added yet.</p>
                  )}
                </div>
                {verificationEntryOpen && (
                  <div className="crypto-key-entry">
                    <label className="crypto-field-label" htmlFor="verification-target">
                      This key verifies
                    </label>
                    <select
                      id="verification-target"
                      aria-label="Verification key target"
                      value={verificationTarget}
                      onChange={(event) =>
                        setVerificationTarget(
                          event.target.value as VerificationKeyTarget,
                        )
                      }
                    >
                      <option value="client-server">Chat server</option>
                      <option value="discovery-service">Discovery service</option>
                    </select>
                    <div className="crypto-field">
                      <label htmlFor="verification-key-name">Key name</label>
                      <input
                        id="verification-key-name"
                        aria-label="Verification key name"
                        type="text"
                        placeholder="e.g. Home server"
                        value={verificationName}
                        onChange={(event) => setVerificationName(event.target.value)}
                      />
                    </div>
                    <div className="crypto-field">
                      <div className="crypto-input-action">
                        <label className="crypto-field-label" htmlFor="verification-key-text">
                          Public key
                        </label>
                        <button
                          type="button"
                          className="crypto-add"
                          onClick={() =>
                            document.getElementById("verification-key-file")?.click()
                          }
                        >
                          Choose file…
                        </button>
                        <input
                          id="verification-key-file"
                          aria-label="Verification key file"
                          type="file"
                          accept=".pub,.asc,.gpg,.key,.txt"
                          hidden
                          onChange={(event) =>
                            void readFile(
                              event.target.files?.[0],
                              setVerificationContents,
                              setVerificationName,
                              async (contents) => {
                                try {
                                  await validateSshPublicKey(contents);
                                  setVerificationError("");
                                } catch (error) {
                                  setVerificationError(
                                    error instanceof Error
                                      ? error.message
                                      : String(error),
                                  );
                                }
                              },
                            )
                          }
                        />
                      </div>
                      <textarea
                        id="verification-key-text"
                        aria-label="Verification key text"
                        placeholder="Paste public-key contents"
                        value={verificationContents}
                        onChange={(event) =>
                          setVerificationContents(event.target.value)
                        }
                      />
                      {verificationError && (
                        <p role="alert" className="crypto-error">{verificationError}</p>
                      )}
                    </div>
                    <div className="crypto-key-actions">
                      <span className="crypto-help">Choose a file or paste text above.</span>
                      <button
                        type="button"
                        className="crypto-add"
                        onClick={() => void addVerificationKey()}
                      >
                        Add to list
                      </button>
                    </div>
                  </div>
                )}
              </div>

              <div className="crypto-toggle-row">
                <div className="crypto-toggle-copy">
                  <strong>Skip server identity verification</strong>
                  <small>
                    Allow connecting without a matching trusted server key. This
                    can expose you to impersonation.
                  </small>
                </div>
                <button
                  type="button"
                  role="switch"
                  aria-checked={draft.skipServerVerification}
                  aria-label="Skip server identity verification"
                  className="crypto-switch"
                  onClick={() =>
                    update({
                      skipServerVerification: !draft.skipServerVerification,
                    })
                  }
                />
              </div>
            </div>
          </section>
        </div>

        <footer className="crypto-modal-foot">
          <div className="crypto-storage">
            Saved on this browser · shared by Client and Discovery
            <br />
            Private key material is stored as plaintext in localStorage.
          </div>
          <div className="crypto-actions">
            <button type="button" className="crypto-secondary" onClick={onClose}>
              Cancel
            </button>
            <button
              type="button"
              className="crypto-primary"
              onClick={() => {
                if (passwordError) {
                  setAuthError(
                    "Use a password between 8 and 255 characters, or clear the field.",
                  );
                  return;
                }
                onSave(draft);
              }}
            >
              Save security settings
            </button>
          </div>
        </footer>
      </section>
    </div>
  );
}
