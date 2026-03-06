/**
 * Fetch session strings from the API
 * @param count - Number of session strings to generate (default: 1)
 * @returns Promise<string[]> - Array of session strings
 * @throws Error if the API call fails or count is invalid
 */
export async function fetchSessionStrings(count: number = 1): Promise<string[]> {
  try {
    const response = await fetch(
      `https://ascii-chat.com/api/session-strings?count=${count}`,
    );

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
