import express from "express";
import rateLimit from "express-rate-limit";
import dotenv from "dotenv";
import fs from "fs";
import path from "path";
import { fileURLToPath } from "url";

dotenv.config();

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const app = express();
const PORT = process.env.API_PORT || 3001;

// Rate limiting: 30 searches per minute per IP
const limiter = rateLimit({
  windowMs: 1 * 60 * 1000, // 1 minute
  max: 30, // limit each IP to 30 requests per windowMs
  message: "Too many search requests, please try again later",
  standardHeaders: true, // Return rate limit info in the `RateLimit-*` headers
  legacyHeaders: false, // Disable the `X-RateLimit-*` headers
});

// Cache for man3 files and index
let fileCache = {};
let indexCache = null;

// Initialize cache
function initializeCache() {
  const man3Dir = path.join(__dirname, "public/man3");
  const indexPath = path.join(man3Dir, "index.json");

  // Load index
  if (fs.existsSync(indexPath)) {
    indexCache = JSON.parse(fs.readFileSync(indexPath, "utf-8"));
  }

  // Pre-cache files in production
  if (process.env.NODE_ENV === "production" && indexCache) {
    console.log(`Caching ${indexCache.length} man pages...`);
    for (const page of indexCache) {
      const filePath = path.join(man3Dir, page.file);
      if (fs.existsSync(filePath)) {
        try {
          const content = fs.readFileSync(filePath, "utf-8");
          fileCache[page.name] = extractTextContent(content);
        } catch (e) {
          console.error(`Failed to cache ${page.name}:`, e.message);
        }
      }
    }
    console.log(`Cached ${Object.keys(fileCache).length} files`);
  }
}

// Extract text from HTML
function extractTextContent(html) {
  return html
    .replace(/<script[^>]*>[\s\S]*?<\/script>/gi, "") // Remove scripts
    .replace(/<style[^>]*>[\s\S]*?<\/style>/gi, "") // Remove styles
    .replace(/<[^>]+>/g, "\n") // Replace HTML tags with newlines
    .replace(/&nbsp;/g, " ")
    .replace(/&lt;/g, "<")
    .replace(/&gt;/g, ">")
    .replace(/&amp;/g, "&")
    .replace(/\n\s*\n/g, "\n") // Remove multiple blank lines
    .split("\n")
    .map((line) => line.trim())
    .filter((line) => line.length > 0)
    .join("\n");
}

// Get file text content (from cache or disk)
function getFileContent(pageName) {
  if (fileCache[pageName]) {
    return fileCache[pageName];
  }

  // In development, read from disk
  if (process.env.NODE_ENV !== "production") {
    const man3Dir = path.join(__dirname, "public/man3");
    const filePath = path.join(man3Dir, `${pageName}.html`);

    if (fs.existsSync(filePath)) {
      const content = fs.readFileSync(filePath, "utf-8");
      const text = extractTextContent(content);
      // Cache in memory for this request cycle
      fileCache[pageName] = text;
      return text;
    }
  }

  return "";
}

// Find snippets around matches (centered on match)
function findSnippets(text, query, maxSnippets = 3) {
  const allLines = text.split("\n");
  // Map filtered lines to their original line numbers
  const lines = [];
  const lineNumbers = [];
  for (let i = 0; i < allLines.length; i++) {
    if (allLines[i].trim()) {
      lines.push(allLines[i]);
      lineNumbers.push(i + 1); // 1-indexed line numbers
    }
  }

  const snippets = [];
  const usedLines = new Set(); // Track which lines we've already used

  try {
    const regex = new RegExp(query, "i");

    for (let i = 0; i < lines.length && snippets.length < maxSnippets; i++) {
      // Skip if we already used this line in a previous snippet
      if (usedLines.has(i)) continue;

      if (regex.test(lines[i])) {
        // Always show exactly 3 lines with match in the middle
        const before = i > 0 ? lines[i - 1] : "";
        const match = lines[i];
        const after = i < lines.length - 1 ? lines[i + 1] : "";

        // Build snippet: exactly 3 lines with match in middle
        const snippet = [before, match, after].join("\n");

        // Track which lines are used in this snippet to avoid overlaps
        if (i > 0) usedLines.add(i - 1);
        usedLines.add(i);
        if (i < lines.length - 1) usedLines.add(i + 1);

        // Calculate line numbers for all 3 lines
        const beforeLineNum = i > 0 ? lineNumbers[i - 1] : null;
        const matchLineNum = lineNumbers[i];
        const afterLineNum = i < lines.length - 1 ? lineNumbers[i + 1] : null;

        snippets.push({
          text: snippet,
          lineNumbers: [beforeLineNum, matchLineNum, afterLineNum],
          matchLineNumber: matchLineNum,
        });
      }
    }
  } catch (e) {
    // Invalid regex
  }

  return snippets;
}

// Search endpoint
app.get("/api/man3/search", limiter, (req, res) => {
  const query = req.query.q || "";

  if (!query || query.length < 2) {
    return res.json({ results: [] });
  }

  try {
    const regex = new RegExp(query, "i");
    const results = [];

    if (!indexCache) {
      return res.status(500).json({ error: "Index not loaded" });
    }

    // Search in index (title/name)
    const titleMatches = indexCache.filter(
      (page) =>
        regex.test(page.title || page.name) || regex.test(page.name)
    );

    // Search in content for title matches (get snippets)
    for (const page of titleMatches) {
      const content = getFileContent(page.name);
      const snippets = findSnippets(content, query, 3);

      results.push({
        name: page.name,
        title: page.title,
        matchType: "title",
        snippets: snippets,
      });
    }

    // Search content for non-title matches
    for (const page of indexCache) {
      if (titleMatches.find((p) => p.name === page.name)) {
        continue; // Already added
      }

      const content = getFileContent(page.name);
      const snippets = findSnippets(content, query, 3);

      if (snippets.length > 0) {
        results.push({
          name: page.name,
          title: page.title,
          matchType: "content",
          snippets: snippets,
        });
      }
    }

    // Count total matches (snippets) before slicing
    const totalMatches = results.reduce((sum, result) => sum + result.snippets.length, 0);

    res.json({
      query: query,
      filesMatched: results.length,
      totalMatches: totalMatches,
      results: results.slice(0, 10), // Limit to 10 results
    });
  } catch (e) {
    res.status(400).json({ error: "Invalid regex pattern" });
  }
});

// Health check
app.get("/api/health", (req, res) => {
  res.json({
    status: "ok",
    cached: Object.keys(fileCache).length,
    indexSize: indexCache ? indexCache.length : 0,
  });
});

// Initialize and start
initializeCache();

app.listen(PORT, () => {
  console.log(`Man3 search API running on http://localhost:${PORT}`);
  console.log(`Environment: ${process.env.NODE_ENV || "development"}`);
  console.log(`Rate limit: 30 searches per minute per IP`);
});
