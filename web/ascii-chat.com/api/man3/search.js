import fs from "fs";
import path from "path";

export default async function handler(req, res) {
  if (req.method !== "GET") {
    return res.status(405).json({ error: "Method not allowed" });
  }

  const { q } = req.query;

  if (!q || typeof q !== "string") {
    return res.status(400).json({ error: "Missing search query" });
  }

  try {
    // Load the man3 index
    const indexPath = path.join(process.cwd(), "public/man3/index.json");
    const indexData = fs.readFileSync(indexPath, "utf-8");
    const pages = JSON.parse(indexData);

    // Parse the query as regex with support for flags
    let regex;
    let flags = "i"; // case-insensitive by default

    // Check if query looks like /pattern/flags format
    const regexMatch = q.match(/^\/(.+)\/([gimuy]*)$/);
    if (regexMatch) {
      try {
        regex = new RegExp(regexMatch[1], regexMatch[2] || flags);
      } catch (_e) {
        return res.status(400).json({ error: "Invalid regex pattern" });
      }
    } else {
      // Escape special regex chars and search as literal string
      try {
        regex = new RegExp(q.replace(/[.*+?^${}()|[\]\\]/g, "\\$&"), flags);
      } catch (_e) {
        return res.status(400).json({ error: "Invalid search query" });
      }
    }

    // Search through pages
    const results = [];
    let totalMatches = 0;

    for (const page of pages) {
      const nameMatches = (page.name.match(regex) || []).length;
      const titleMatches = (page.title.match(regex) || []).length;
      const matches = nameMatches + titleMatches;

      if (matches > 0) {
        results.push({
          ...page,
          matchCount: matches,
        });
        totalMatches += matches;
      }
    }

    // Sort by match count (descending)
    results.sort((a, b) => b.matchCount - a.matchCount);

    return res.status(200).json({
      results: results.slice(0, 100), // Limit to 100 results
      filesMatched: results.length,
      totalMatches,
    });
  } catch (error) {
    console.error("Search error:", error);
    return res.status(500).json({ error: "Search failed" });
  }
}
