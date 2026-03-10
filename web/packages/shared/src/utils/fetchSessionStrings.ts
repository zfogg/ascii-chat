import { API_RELATIVE, DISCOVERY_API_BASE } from "./urls";

/**
 * Fetch session strings from the API
 * In production, fetches from discovery service (configured via VITE_DISCOVERY_API_BASE)
 * In development, fetches from local API
 * @param count - Number of session strings to generate (default: 1)
 * @returns Promise<string[]> - Array of session strings
 * @throws Error if the API call fails or count is invalid
 */
export async function fetchSessionStrings(count: number = 1): Promise<string[]> {
  try {
    // In production, use discovery service API if configured
    // In dev, use relative URL with local API
    const apiBase = DISCOVERY_API_BASE || "";
    const endpoint = apiBase
      ? `${apiBase}/api/session-strings`
      : API_RELATIVE.SESSION_STRINGS;

    const response = await fetch(`${endpoint}?count=${count}`);

    if (!response.ok) {
      const error = await response.json();
      throw new Error(error.error || "Failed to fetch session strings");
    }

    const data = await response.json();
    return data.strings || [];
  } catch (err) {
    console.error("Error fetching session strings:", err);
    throw err;
  }
}
