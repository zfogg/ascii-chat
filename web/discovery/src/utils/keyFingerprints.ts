/**
 * Extract fingerprints from SSH and GPG public keys
 * SSH: parsed and hashed without ssh-keygen
 * GPG: extracted using openpgpjs
 */

/**
 * Parse OpenSSH public key format and extract key material
 * Format: "ssh-rsa AAAAB3Nza... [comment]"
 */
function parseSshPublicKey(keyString: string): {
  algorithm: string;
  keyData: ArrayBuffer;
} | null {
  const parts = keyString.trim().split(/\s+/);
  if (parts.length < 2) return null;

  const algorithm = parts[0];
  const keyBase64 = parts[1];

  if (!algorithm || !keyBase64) return null;

  try {
    const binaryString = atob(keyBase64 ?? "");
    const buffer = new ArrayBuffer(binaryString.length);
    const view = new Uint8Array(buffer);
    for (let i = 0; i < binaryString.length; i++) {
      view[i] = binaryString.charCodeAt(i);
    }
    return { algorithm, keyData: buffer };
  } catch {
    return null;
  }
}

/**
 * Compute SHA256 fingerprint from SSH key data
 * Returns base64 format: "SHA256:..."
 */
export async function getSshFingerprint(
  keyString: string,
): Promise<string | null> {
  const parsed = parseSshPublicKey(keyString);
  if (!parsed) return null;

  try {
    const hashBuffer = await crypto.subtle.digest("SHA-256", parsed.keyData);
    const hashArray = Array.from(new Uint8Array(hashBuffer));
    const hashBase64 = btoa(String.fromCharCode(...hashArray));

    // Format: SHA256:base64 (OpenSSH standard)
    return `SHA256:${hashBase64.replace(/\+/g, "-").replace(/\//g, "_").replace(/=/g, "")}`;
  } catch {
    return null;
  }
}

/**
 * Extract fingerprint from GPG public key using openpgpjs
 * Returns long fingerprint (40 hex chars) formatted with spaces
 */
export async function getGpgFingerprint(
  keyString: string,
): Promise<string | null> {
  try {
    // Dynamic import to avoid loading openpgpjs if not needed
    const openpgp = await import("openpgp");

    // Fix missing blank line after BEGIN header (Coolify env var processing issue)
    let processedKey = keyString;
    if (
      keyString.includes("-----BEGIN PGP PUBLIC KEY BLOCK-----") &&
      !keyString.includes("-----BEGIN PGP PUBLIC KEY BLOCK-----\n\n")
    ) {
      processedKey = keyString.replace(
        "-----BEGIN PGP PUBLIC KEY BLOCK-----\n",
        "-----BEGIN PGP PUBLIC KEY BLOCK-----\n\n",
      );
    }

    const key = await openpgp.readKey({ armoredKey: processedKey });
    const fingerprint = key.getFingerprint().toUpperCase();

    // Format as: 0AAE 7D67 D734 6959 74C3 6CEE C380 DA08 AF18 35B9
    return fingerprint.match(/.{1,4}/g)?.join(" ") || fingerprint;
  } catch (error) {
    console.error("GPG fingerprint computation failed:", error);
    console.error("Key string length:", keyString?.length);
    console.error("Key starts with:", keyString?.substring(0, 50));
    return null;
  }
}

/**
 * Determine key type from ASCII content
 */
export function getKeyType(keyString: string): "ssh" | "gpg" | "unknown" {
  if (keyString.includes("-----BEGIN PGP PUBLIC KEY BLOCK-----")) {
    return "gpg";
  }
  if (keyString.trim().startsWith("ssh-")) {
    return "ssh";
  }
  return "unknown";
}
