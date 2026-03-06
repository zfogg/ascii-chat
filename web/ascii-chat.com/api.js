import express from "express";
import rateLimit from "express-rate-limit";
import dotenv from "dotenv";
import fs from "fs";
import path from "path";
import { fileURLToPath } from "url";
import winston from "winston";
import morgan from "morgan";
import { generateSessionStrings } from "./src/utils/strings.js";

dotenv.config();

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
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

// Rate limiting: 100 session string requests per minute per IP
// Higher limit for dev server (React StrictMode runs effects twice)
const sessionStringLimiter = rateLimit({
  windowMs: 1 * 60 * 1000,
  max: 100,
  message: "Too many session string requests, please try again later",
  standardHeaders: true,
  legacyHeaders: false,
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

// Extract text with preserved line numbers from line number prefixes
function extractTextWithLineNumbers(html) {
  let text = html
    .replace(/<script[^>]*>[\s\S]*?<\/script>/gi, "")
    .replace(/<style[^>]*>[\s\S]*?<\/style>/gi, "")
    .replace(/<[^>]+>/g, "\n");

  // Decode entities
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
      try {
        return String.fromCharCode(parseInt(match.slice(3, -1), 16));
      } catch {
        return match;
      }
    } else if (match.startsWith("&#")) {
      try {
        return String.fromCharCode(parseInt(match.slice(2, -1), 10));
      } catch {
        return match;
      }
    } else {
      return entities[match.slice(1, -1)] || match;
    }
  });

  // Keep all lines, but extract line numbers from those that have them
  const allLines = text.split("\n");
  const lineNumbers = [];

  for (let i = 0; i < allLines.length; i++) {
    const trimmed = allLines[i].trim();
    const lineNumMatch = trimmed.match(/^(\d+)\s+/);

    if (lineNumMatch) {
      const lineNum = parseInt(lineNumMatch[1], 10);
      lineNumbers.push(lineNum);
    } else {
      // Line without number - mark as -1
      lineNumbers.push(-1);
    }
  }

  // If we found actual code lines, return with preserved line numbers
  if (lineNumbers.some((ln) => ln > 0)) {
    return {
      text: text, // Return text AS-IS with line number prefixes
      lineNumbers: lineNumbers, // Array aligned with split text
    };
  }

  // Fallback if no numbered lines found
  const plainLines = text
    .replace(/\n\s*\n/g, "\n")
    .split("\n")
    .map((l) => l.trim())
    .filter((l) => l.length > 0);
  return {
    text: plainLines.join("\n"),
    lineNumbers: plainLines.map((_, idx) => idx + 1),
  };
}

// Get file text content with line numbers (from cache or disk)
function getFileContentWithLineNumbers(pageName, fileName) {
  // In development, read from disk
  if (process.env.NODE_ENV !== "production") {
    const man3Dir = path.join(__dirname, "public/man3");
    // Use the actual filename from pages.json if provided, otherwise construct it
    const actualFileName = fileName || `${pageName}.html`;
    const filePath = path.join(man3Dir, actualFileName);

    if (fs.existsSync(filePath)) {
      const content = fs.readFileSync(filePath, "utf-8");
      return extractTextWithLineNumbers(content);
    }
  }

  // In production, use cached text without line numbers
  if (fileCache[pageName]) {
    return { text: fileCache[pageName], lineNumbers: null };
  }

  return { text: "", lineNumbers: null };
}

// Find snippets around matches (centered on match)
function findSnippets(text, query, maxSnippets = 3, lineNumbers = null) {
  const allLines = text.split("\n");
  let lines = allLines;
  let actualLineNumbers = lineNumbers;

  // If lineNumbers are NOT provided, filter and number sequentially
  if (!lineNumbers) {
    lines = [];
    actualLineNumbers = [];
    for (let i = 0; i < allLines.length; i++) {
      if (allLines[i].trim()) {
        lines.push(allLines[i]);
        actualLineNumbers.push(i + 1);
      }
    }
  }

  const snippets = [];
  const usedLines = new Set();
  let totalMatches = 0;

  try {
    const regex = new RegExp(query, "i");

    for (let i = 0; i < lines.length; i++) {
      if (regex.test(lines[i])) {
        totalMatches++;

        if (snippets.length < maxSnippets && !usedLines.has(i)) {
          const before = i > 0 ? lines[i - 1] : "";
          const match = lines[i];
          const after = i < lines.length - 1 ? lines[i + 1] : "";
          const snippet = [before, match, after].join("\n");

          if (i > 0) usedLines.add(i - 1);
          usedLines.add(i);
          if (i < lines.length - 1) usedLines.add(i + 1);

          // Get line numbers, skipping non-code lines (marked with -1)
          let beforeLineNum = null;
          if (i > 0) {
            // Find previous code line
            for (let j = i - 1; j >= 0; j--) {
              if (actualLineNumbers[j] > 0) {
                beforeLineNum = actualLineNumbers[j];
                break;
              }
            }
          }

          let matchLineNum = actualLineNumbers[i];
          if (matchLineNum <= 0) matchLineNum = null;

          let afterLineNum = null;
          if (i < lines.length - 1) {
            // Find next code line
            for (let j = i + 1; j < lines.length; j++) {
              if (actualLineNumbers[j] > 0) {
                afterLineNum = actualLineNumbers[j];
                break;
              }
            }
          }

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
      const { text: content, lineNumbers } = getFileContentWithLineNumbers(
        page.name,
        page.file,
      );
      logger.debug(
        `[search] Retrieved ${lineNumbers ? lineNumbers.length : 0} line numbers for ${page.name}`,
      );
      const { snippets, totalMatches } = findSnippets(
        content,
        query,
        3,
        lineNumbers,
      );

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

      const { text: content, lineNumbers } = getFileContentWithLineNumbers(
        page.name,
        page.file,
      );
      const { snippets, totalMatches } = findSnippets(
        content,
        query,
        3,
        lineNumbers,
      );

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

// Session strings endpoint
app.get("/api/session-strings", sessionStringLimiter, (req, res) => {
  // Enable CORS for session strings endpoint
  res.header("Access-Control-Allow-Origin", "*");
  res.header("Access-Control-Allow-Methods", "GET");
  res.header("Access-Control-Allow-Headers", "Content-Type");

  try {
    const count = parseInt(req.query.count || "1", 10);

    // Validate count
    if (isNaN(count) || count < 1) {
      return res
        .status(400)
        .json({ error: "count must be a positive integer" });
    }

    // Max limit check (2500 * 5000 * 5000 = 62,500,000,000)
    const MAX_COUNT = 62500000000;
    if (count > MAX_COUNT) {
      return res.status(400).json({
        error: `count exceeds maximum (${MAX_COUNT} unique combinations)`,
      });
    }

    // Generate session strings using JavaScript implementation
    const strings = generateSessionStrings(count);

    logger.info(`[session-strings] Generated ${strings.length} strings`);

    res.json({
      count: strings.length,
      strings: strings,
    });
  } catch (err) {
    logger.error(`Session strings error: ${err.message}`);
    res.status(500).json({ error: "Failed to generate session strings" });
  }
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
