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
    .replace(/<[^>]+>/g, "") // Remove HTML tags
    .replace(/&nbsp;/g, " ")
    .replace(/&lt;/g, "<")
    .replace(/&gt;/g, ">")
    .replace(/&amp;/g, "&")
    .replace(/\s+/g, " ") // Collapse whitespace
    .trim();
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

// Find snippets around matches
function findSnippets(text, query, maxSnippets = 3) {
  const lines = text.split("\n").filter((l) => l.trim());
  const snippets = [];
  const snippetSet = new Set();

  try {
    const regex = new RegExp(query, "i");

    for (let i = 0; i < lines.length; i++) {
      if (snippets.length >= maxSnippets) break;

      if (regex.test(lines[i])) {
        const start = Math.max(0, i - 1);
        const end = Math.min(lines.length - 1, i + 1);
        const snippet = lines.slice(start, end + 1).join("\n").trim();

        if (!snippetSet.has(snippet) && snippet.length > 10) {
          snippetSet.add(snippet);
          snippets.push(snippet);
        }
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

    res.json({
      query: query,
      results: results.slice(0, 100), // Limit to 100 results
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
