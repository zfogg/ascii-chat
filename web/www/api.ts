import express from "express";
import type { Request, Response } from "express";
import rateLimit from "express-rate-limit";
import dotenv from "dotenv";
import fs from "fs";
import path from "path";
import { fileURLToPath } from "url";
import winston from "winston";
import morgan from "morgan";
import MiniSearch from "minisearch";
import { generateSessionStrings } from "./src/utils/strings.ts";

dotenv.config();

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const app = express();

// API server configuration
// When NODE_ENV != production, api runs on localhost:3001
// The vite dev servers (port 5173, 5174, 3000) proxy /api requests to this
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
          return `[${String(timestamp)}] ${String(level).toUpperCase()}: ${String(message)} ${metaStr}`;
        }),
  ),
  transports: [
    new winston.transports.Console(),
    new winston.transports.File({ filename: "api.log" }),
  ],
});

// Trust proxy headers (set by Caddy reverse proxy)
app.set("trust proxy", "loopback, linklocal, uniquelocal");

// HTTP request logging middleware
app.use(
  morgan(NODE_ENV === "production" ? "combined" : "dev", {
    stream: { write: (msg: string) => logger.info(msg.trim()) },
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

interface IndexEntry {
  name: string;
  title: string;
  file: string;
}

interface TextWithLineNumbers {
  text: string;
  lineNumbers: number[] | null;
}

interface Snippet {
  text: string;
  lineNumbers: (number | null)[];
  matchLineNumber: number | null;
}

// Cache for man3 files and index
let fileCache: Record<string, string> = {};
let indexCache: IndexEntry[] | null = null;
let miniSearch: MiniSearch | null = null;

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
      indexCache = JSON.parse(
        fs.readFileSync(indexPath, "utf-8"),
      ) as IndexEntry[];
      logger.info(
        `Index loaded successfully with ${indexCache.length} entries`,
      );

      // Build minisearch index with content from HTML files
      miniSearch = new MiniSearch({
        fields: ["name", "title", "content"],
        storeFields: ["name", "title", "file"],
        idField: "name",
      });

      // Add pages with their HTML content
      const docsWithContent = indexCache.map((page) => {
        const filePath = path.join(man3Dir, page.file);
        let content = "";
        if (fs.existsSync(filePath)) {
          try {
            const html = fs.readFileSync(filePath, "utf-8");
            // Extract text content from HTML
            content = extractTextContent(html).substring(0, 50000); // Limit to 50k chars per page
          } catch (err) {
            logger.debug(`Failed to read content for ${page.name}: ${(err as Error).message}`);
          }
        }
        return { ...page, content };
      });

      miniSearch.addAll(docsWithContent);
      logger.info(`MiniSearch index built with ${indexCache.length} documents and full-text search`);
    } catch (err) {
      logger.error(`Failed to parse index.json: ${(err as Error).message}`);
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
          logger.error(
            `Failed to cache ${page.name}: ${(err as Error).message}`,
          );
        }
      }
    }
    logger.info(`Cached ${Object.keys(fileCache).length} files`);
  }
}

// Extract text from HTML
function extractTextContent(html: string): string {
  let text = html
    .replace(/<script[^>]*>[\s\S]*?<\/script>/gi, "") // Remove scripts
    .replace(/<style[^>]*>[\s\S]*?<\/style>/gi, "") // Remove styles
    .replace(/<[^>]+>/g, "\n"); // Replace HTML tags with newlines

  // Decode all HTML entities in one pass
  text = text.replace(/&([a-zA-Z]+|#\d+|#x[0-9A-Fa-f]+);/g, (match) => {
    const entities: Record<string, string> = {
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
function extractTextWithLineNumbers(html: string): TextWithLineNumbers {
  let text = html
    .replace(/<script[^>]*>[\s\S]*?<\/script>/gi, "")
    .replace(/<style[^>]*>[\s\S]*?<\/style>/gi, "")
    .replace(/<[^>]+>/g, "\n");

  // Decode entities
  text = text.replace(/&([a-zA-Z]+|#\d+|#x[0-9A-Fa-f]+);/g, (match) => {
    const entities: Record<string, string> = {
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
  const lineNumbers: number[] = [];

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
function getFileContentWithLineNumbers(
  pageName: string,
  fileName: string | undefined,
): TextWithLineNumbers {
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
function findSnippets(
  text: string,
  query: string,
  maxSnippets = 3,
  lineNumbers: number[] | null = null,
  flags = "i",
): { snippets: Snippet[]; totalMatches: number } {
  const allLines = text.split("\n");
  let lines = allLines;
  let actualLineNumbers: number[] = lineNumbers || [];

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

  const snippets: Snippet[] = [];
  const usedLines = new Set<number>();
  let totalMatches = 0;

  try {
    const regex = new RegExp(query, flags);

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
          let beforeLineNum: number | null = null;
          if (i > 0) {
            // Find previous code line
            for (let j = i - 1; j >= 0; j--) {
              if (actualLineNumbers[j] > 0) {
                beforeLineNum = actualLineNumbers[j];
                break;
              }
            }
          }

          let matchLineNum: number | null = actualLineNumbers[i];
          if (matchLineNum <= 0) matchLineNum = null;

          let afterLineNum: number | null = null;
          if (i < lines.length - 1) {
            // Find next code line
            for (let j = i + 1; j < lines.length; j++) {
              if (actualLineNumbers[j] > 0) {
                afterLineNum = actualLineNumbers[j];
                break;
              }
            }
          }

          // If the match line doesn't have a line number, use the nearest available one
          const effectiveMatchLineNum =
            matchLineNum || afterLineNum || beforeLineNum;

          snippets.push({
            text: snippet,
            lineNumbers: [beforeLineNum, matchLineNum, afterLineNum],
            matchLineNumber: effectiveMatchLineNum,
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
app.get("/api/man3/search", limiter, (req: Request, res: Response) => {
  const query = (req.query.q as string) || "";

  if (!query || query.trim().length === 0) {
    return res.json({ results: [] });
  }

  try {
    if (!miniSearch) {
      return res.status(500).json({ error: "Search index not loaded" });
    }

    // Search using minisearch - require word boundaries for more precise matches
    const searchResults = miniSearch.search(query, {
      combineWith: "AND",
      prefix: false,
      boost: {
        name: 2,
        title: 2,
      },
    });

    // Filter out source pages and results without meaningful matches
    const filteredResults = searchResults
      .filter((result) => !result.name.endsWith("_source"))
      .filter((result) => {
        // Only include if it's a name/title match or has reasonable score
        const isNameMatch =
          result.name.toLowerCase().includes(query.toLowerCase()) ||
          (result.title &&
            result.title.toLowerCase().includes(query.toLowerCase()));
        return isNameMatch || result.score > 1;
      });

    const results: {
      name: string;
      title: string;
      matchType: string;
      snippets: Snippet[];
      totalMatchesInFile: number;
    }[] = [];

    // For each matched page, get content and find snippets
    for (const result of filteredResults) {
      const page = indexCache?.find((p) => p.name === result.name);
      if (!page) continue;

      const { text: content, lineNumbers } = getFileContentWithLineNumbers(
        page.name,
        page.file,
      );

      // Create a regex from the query for snippet matching
      let snippetRegex: string;
      try {
        // Extract pattern from /pattern/flags format if present
        const regexMatch = query.match(/^\/(.+)\/([gimuy]*)$/);
        snippetRegex = regexMatch ? regexMatch[1] : query;
      } catch {
        snippetRegex = query;
      }

      const { snippets, totalMatches } = findSnippets(
        content,
        snippetRegex,
        3,
        lineNumbers,
        "i",
      );

      results.push({
        name: page.name,
        title: page.title,
        matchType: "title",
        snippets: snippets,
        totalMatchesInFile: totalMatches,
      });
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
    res.status(400).json({ error: (err as Error).message });
  }
});

// Health check
app.get("/api/health", (_req: Request, res: Response) => {
  res.json({
    status: "ok",
    cached: Object.keys(fileCache).length,
    indexSize: indexCache ? indexCache.length : 0,
  });
});

// Session strings endpoint
app.get(
  "/api/session-strings",
  sessionStringLimiter,
  (req: Request, res: Response) => {
    // Enable CORS for session strings endpoint
    if (NODE_ENV === "production") {
      // In production, only allow ascii-chat subdomains
      const origin = req.get("origin");
      const allowedOrigins = [
        "https://ascii-chat.com",
        "https://www.ascii-chat.com",
        "https://web.ascii-chat.com",
        "https://discovery.ascii-chat.com",
      ];

      if (origin && allowedOrigins.includes(origin)) {
        res.header("Access-Control-Allow-Origin", origin);
      }
    } else {
      // In development, allow all origins
      res.header("Access-Control-Allow-Origin", "*");
    }
    res.header("Access-Control-Allow-Methods", "GET");
    res.header("Access-Control-Allow-Headers", "Content-Type");

    try {
      const count = parseInt((req.query.count as string) || "1", 10);

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
      logger.error(`Session strings error: ${(err as Error).message}`);
      res.status(500).json({ error: "Failed to generate session strings" });
    }
  },
);

// Initialize cache
initializeCache();

// Export handler for Vercel
export default app;

// Start server on Coolify (or any non-serverless platform)
app.listen(PORT, () => {
  logger.info(`Man3 search API running on http://localhost:${PORT}`);
  logger.info(`Environment: ${NODE_ENV}`);
  logger.info(`Rate limit: 30 searches per minute per IP`);
});
