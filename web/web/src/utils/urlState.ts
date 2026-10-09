function isRecord(value: unknown): value is Record<string, unknown> {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}

function readScalar(raw: string | null, fallback: unknown): unknown {
  if (raw === null) return fallback;
  if (typeof fallback === "string") return raw;
  if (fallback === null) {
    if (raw === "null") return null;
    if (raw === "true" || raw === "false") return raw === "true";
    return raw;
  }
  if (typeof fallback === "boolean") {
    return raw === "true" || raw === ""
      ? true
      : raw === "false"
        ? false
        : fallback;
  }
  if (typeof fallback === "number") {
    const number = Number(raw);
    return raw.trim() !== "" && Number.isFinite(number) ? number : fallback;
  }
  return fallback;
}

function readFields(
  params: URLSearchParams,
  defaults: Record<string, unknown>,
  prefix = "",
): Record<string, unknown> {
  const result: Record<string, unknown> = {};
  for (const [name, initial] of Object.entries(defaults)) {
    const key = prefix + name;
    if (isRecord(initial)) {
      result[name] =
        params.get(key) === ""
          ? {}
          : readFields(
              params,
              [...params.keys()].some((field) => field.startsWith(`${key}.`))
                ? {}
                : initial,
              `${key}.`,
            );
    } else {
      result[name] = readScalar(params.get(key), initial);
    }
  }
  // Selection maps use dynamic service names, with string IDs as their values.
  if (prefix && Object.keys(defaults).length === 0) {
    for (const [key, value] of params) {
      if (key.startsWith(prefix)) result[key.slice(prefix.length)] = value;
    }
  }
  return result;
}

function writeFields(
  params: URLSearchParams,
  values: Record<string, unknown>,
  prefix = "",
  defaults: Record<string, unknown> = {},
): void {
  for (const [name, value] of Object.entries(values)) {
    const key = prefix + name;
    if (isRecord(value)) {
      const previousKeys = Array.from(params.keys()).filter((existing) =>
        existing.startsWith(`${key}.`),
      );
      for (const existing of previousKeys) params.delete(existing);
      params.delete(key);
      const initial = defaults[name];
      if (JSON.stringify(value) === JSON.stringify(initial)) continue;
      if (Object.keys(value).length === 0) params.set(key, "");
      else writeFields(params, value, `${key}.`);
    } else if (
      value === null ||
      ["string", "number", "boolean"].includes(typeof value)
    ) {
      if (Object.is(value, defaults[name])) params.delete(key);
      else params.set(key, String(value));
    }
  }
}

/** Objects are represented by separate query fields, never JSON blobs. */
export function readUrlValue<T>(key: string, fallback: T, fragment = false): T {
  if (typeof window === "undefined") return fallback;
  const params = new URLSearchParams(
    fragment ? window.location.hash.slice(1) : window.location.search,
  );
  if (isRecord(fallback)) {
    // Read older links until the mount effect upgrades them in place.
    const legacy = params.get(key);
    if (legacy !== null) {
      try {
        const fields: unknown = JSON.parse(legacy);
        if (isRecord(fields)) {
          const migrated = new URLSearchParams();
          writeFields(migrated, fields);
          for (const [name, value] of params) migrated.set(name, value);
          return readFields(migrated, fallback) as T;
        }
      } catch {
        // Malformed legacy links fall back to the individual fields/defaults.
      }
    }
    return readFields(params, fallback) as T;
  }
  return readScalar(params.get(key), fallback) as T;
}

export function writeUrlValue(
  key: string,
  value: unknown,
  fragment = false,
  defaults?: unknown,
): void {
  const url = new URL(window.location.href);
  const params = fragment
    ? new URLSearchParams(url.hash.slice(1))
    : url.searchParams;
  if (isRecord(value)) {
    params.delete(key);
    writeFields(params, value, "", isRecord(defaults) ? defaults : {});
  } else {
    if (Object.is(value, defaults)) params.delete(key);
    else params.set(key, String(value));
  }
  if (fragment) url.hash = params.toString();
  window.history.replaceState(window.history.state, "", url);
}

export function migrateLegacyUrlState(): void {
  const params = new URLSearchParams(window.location.search);
  const groups: Record<string, string[]> = {
    render: [
      "width",
      "height",
      "targetFps",
      "colorMode",
      "colorFilter",
      "palette",
      "paletteChars",
      "matrixRain",
      "animationEnabled",
      "animation",
      "flipX",
    ],
    cryptoOptions: [
      "customEncryption",
      "authenticationEnabled",
      "skipServerVerification",
      "activePrivateKeyId",
      "activeVerificationKeyIds",
    ],
    mediaDevices: ["cameraId", "microphoneId", "speakerId"],
  };
  for (const [group, names] of Object.entries(groups)) {
    const raw = params.get(group);
    if (raw === null) continue;
    let fields: Record<string, unknown> = {};
    try {
      const parsed: unknown = JSON.parse(raw);
      if (isRecord(parsed))
        fields = Object.fromEntries(
          Object.entries(parsed).filter(([name]) => names.includes(name)),
        );
    } catch {
      // Discard malformed legacy groups while preserving all other URL fields.
    }
    const migrated = new URLSearchParams();
    writeFields(migrated, fields);
    const url = new URL(window.location.href);
    for (const [name, value] of migrated) {
      if (!url.searchParams.has(name)) url.searchParams.set(name, value);
    }
    url.searchParams.delete(group);
    window.history.replaceState(window.history.state, "", url);
  }
}
