export type VideoEncoding = "auto" | "hevc" | "raw";

/** Explicit HEVC requests must never silently fall back to raw pixels. */
export function getVideoEncoding(
  search = window.location.search,
): VideoEncoding {
  const value = new URLSearchParams(search).get("encoding")?.toLowerCase();
  if (value === undefined || value === "auto") return "auto";
  if (value === "hevc" || value === "hvec" || value === "h265") return "hevc";
  if (value === "raw") return "raw";
  throw new Error(
    `Unknown encoding "${value}". Use ?encoding=hevc or ?encoding=raw.`,
  );
}
