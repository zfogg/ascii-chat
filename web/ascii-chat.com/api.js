import express from "express";
import rateLimit from "express-rate-limit";
import dotenv from "dotenv";
import fs from "fs";
import path from "path";
import { fileURLToPath } from "url";
import winston from "winston";
import morgan from "morgan";

dotenv.config();

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const app = express();
const PORT = process.env.API_PORT || 3001;
const NODE_ENV = process.env.NODE_ENV || "development";

// Configure logger
const logger = winston.createLogger({
  level: NODE_ENV === "production" ? "info" : "debug",
  format: winston.format.combine(
    winston.format.timestamp({ format: "YYYY-MM-DD HH:mm:ss" }),
    winston.format.errors({ stack: true }),
    NODE_ENV === "production"
      ? winston.format.json()
      : winston.format.printf(({ timestamp, level, message, ...meta }) => {
          const metaStr =
            Object.keys(meta).length > 0 ? JSON.stringify(meta) : "";
          return `[${timestamp}] ${level.toUpperCase()}: ${message} ${metaStr}`;
        }),
  ),
  transports: [
    new winston.transports.Console(),
    new winston.transports.File({ filename: "api.log" }),
  ],
});

// HTTP request logging middleware
app.use(
  morgan(NODE_ENV === "production" ? "combined" : "dev", {
    stream: { write: (msg) => logger.info(msg.trim()) },
  }),
);

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
  let indexPath = path.join(__dirname, "public/man3/pages.json");
  logger.debug(`Initializing cache...`);
  logger.debug(`__dirname: ${__dirname}`);
  logger.debug(`Looking for index at: ${indexPath}`);

  // Try fallback path on Vercel (files might be in dist)
  if (!fs.existsSync(indexPath) && process.env.VERCEL) {
    const fallbackPath = path.join(__dirname, "dist/public/man3/pages.json");
    logger.debug(`Index not found, trying fallback: ${fallbackPath}`);
    if (fs.existsSync(fallbackPath)) {
      indexPath = fallbackPath;
    }
  }

  const man3Dir = path.dirname(indexPath);

  // Load index
  if (fs.existsSync(indexPath)) {
    logger.debug("Index file found, loading...");
    try {
      indexCache = JSON.parse(fs.readFileSync(indexPath, "utf-8"));
      logger.info(
        `Index loaded successfully with ${indexCache.length} entries`,
      );
    } catch (err) {
      logger.error(`Failed to parse index.json: ${err.message}`);
    }
  } else {
    logger.error(`Index file not found at ${indexPath}`);
  }

  // Pre-cache files in production
  if (NODE_ENV === "production" && indexCache) {
    logger.info(`Caching ${indexCache.length} man pages...`);
    for (const page of indexCache) {
      const filePath = path.join(man3Dir, page.file);
      if (fs.existsSync(filePath)) {
        try {
          const content = fs.readFileSync(filePath, "utf-8");
          fileCache[page.name] = extractTextContent(content);
        } catch (err) {
          logger.error(`Failed to cache ${page.name}: ${err.message}`);
        }
      }
    }
    logger.info(`Cached ${Object.keys(fileCache).length} files`);
  }
}

// Extract text from HTML
function extractTextContent(html) {
  let text = html
    .replace(/<script[^>]*>[\s\S]*?<\/script>/gi, "") // Remove scripts
    .replace(/<style[^>]*>[\s\S]*?<\/style>/gi, "") // Remove styles
    .replace(/<[^>]+>/g, "\n"); // Replace HTML tags with newlines

  // Decode all HTML entities in one pass
  text = text.replace(/&([a-zA-Z]+|#\d+|#x[0-9A-Fa-f]+);/g, (match) => {
    const entities = {
      lt: "<",
      gt: ">",
      amp: "&",
      quot: '"',
      apos: "'",
      nbsp: " ",
    };

    if (match.startsWith("&#x")) {
      // Hex entity: &#x00A0;
      try {
        return String.fromCharCode(parseInt(match.slice(3, -1), 16));
      } catch {
        return match;
      }
    } else if (match.startsWith("&#")) {
      // Decimal entity: &#160;
      try {
        return String.fromCharCode(parseInt(match.slice(2, -1), 10));
      } catch {
        return match;
      }
    } else {
      // Named entity: &nbsp; &lt; etc
      const name = match.slice(1, -1);
      return entities[name] || match;
    }
  });

  return text
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
  let totalMatches = 0; // Count all matches (not just displayed)

  try {
    const regex = new RegExp(query, "i");

    for (let i = 0; i < lines.length; i++) {
      if (regex.test(lines[i])) {
        totalMatches++; // Count this match

        // Only create snippet if we haven't reached max
        if (snippets.length < maxSnippets && !usedLines.has(i)) {
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
    }
  } catch (_e) {
    // Invalid regex
  }

  return { snippets, totalMatches };
}

// Search endpoint
app.get("/api/man3/search", limiter, (req, res) => {
  const query = req.query.q || "";

  if (!query || query.trim().length === 0) {
    return res.json({ results: [] });
  }

  try {
    const regex = new RegExp(query, "i");
    const results = [];

    if (!indexCache) {
      return res.status(500).json({ error: "Index not loaded" });
    }

    // Search in index (title/name), excluding source pages and metadata
    const titleMatches = indexCache.filter(
      (page) =>
        !page.name.endsWith("_source") &&
        (regex.test(page.title || page.name) || regex.test(page.name)),
    );

    // Search in content for title matches (get snippets)
    for (const page of titleMatches) {
      const content = getFileContent(page.name);
      const { snippets, totalMatches } = findSnippets(content, query, 3);

      results.push({
        name: page.name,
        title: page.title,
        matchType: "title",
        snippets: snippets,
        totalMatchesInFile: totalMatches,
      });
    }

    // Search content for non-title matches (excluding source pages)
    for (const page of indexCache) {
      if (
        page.name.endsWith("_source") ||
        titleMatches.find((p) => p.name === page.name)
      ) {
        continue; // Skip source pages and already added matches
      }

      const content = getFileContent(page.name);
      const { snippets, totalMatches } = findSnippets(content, query, 3);

      if (snippets.length > 0) {
        results.push({
          name: page.name,
          title: page.title,
          matchType: "content",
          snippets: snippets,
          totalMatchesInFile: totalMatches,
        });
      }
    }

    // Count total matches (snippets) before slicing
    const totalMatches = results.reduce(
      (sum, result) => sum + result.snippets.length,
      0,
    );

    const displayLimit = 30;
    const displayedResults = results.slice(0, displayLimit);
    const moreFilesCount = Math.max(0, results.length - displayLimit);

    res.json({
      query: query,
      filesMatched: results.length,
      displayedResults: displayedResults.length,
      moreFilesCount: moreFilesCount,
      totalMatches: totalMatches,
      results: displayedResults,
    });
  } catch (err) {
    logger.error("Search error:", err);
    res.status(400).json({ error: err.message });
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

// Initialize cache
initializeCache();

// Export handler for Vercel
export default app;

// Start server if running as standalone (not as Vercel Function)
// Check both VERCEL env var and if this file was imported as a module
if (
  process.env.VERCEL !== "1" &&
  import.meta.url === `file://${process.argv[1]}`
) {
  app.listen(PORT, () => {
    logger.info(`Man3 search API running on http://localhost:${PORT}`);
    logger.info(`Environment: ${NODE_ENV}`);
    logger.info(`Rate limit: 30 searches per minute per IP`);
  });
}
