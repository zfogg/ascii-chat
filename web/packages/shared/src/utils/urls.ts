/**
 * Shared URL constants and utilities for ascii-chat web ecosystem
 *
 * Handles environment-aware URLs (localhost in dev, production domains in prod)
 * Used for cross-site links and API routing across:
 * - ascii-chat.com (port 5173 / main domain)
 * - discovery.ascii-chat.com (port 5174 / discovery subdomain)
 * - web.ascii-chat.com (port 3000 / web subdomain)
 * - API server (port 3001 / api.ascii-chat.com or main domain)
 * - Discovery service (port 27226 ws:// / 443 wss:// / discovery-service.ascii-chat.com for WebSocket)
 */

declare global {
  interface ImportMetaEnv {
    DEV: boolean;
    NODE_ENV?: string;
  }

  interface ImportMeta {
    readonly env: ImportMetaEnv;
  }
}

const isDev = import.meta.env.DEV;
const baseUrl = isDev ? "http://localhost" : "https://ascii-chat.com";
const discoveryBaseUrl = isDev
  ? "http://localhost"
  : "https://discovery.ascii-chat.com";
const webBaseUrl = isDev ? "http://localhost" : "https://web.ascii-chat.com";
const mainPort = isDev ? 5173 : 443;
const discoveryPort = isDev ? 5174 : 443;
const webPort = isDev ? 3000 : 443;

/**
 * Site base URLs
 * In dev: localhost with ports
 * In prod: actual domains with port 443
 */
export const SITES = {
  MAIN: `${baseUrl}:${mainPort}`,
  DISCOVERY: `${discoveryBaseUrl}:${discoveryPort}`,
  WEB: `${webBaseUrl}:${webPort}`,
  CRYPTO_DOCS: `${baseUrl}:${mainPort}/docs/crypto`,
} as const;

/**
 * API base URL
 * In dev: localhost:3001 (Vite will proxy /api to this)
 * In prod: same domain (uses relative URLs)
 */
export const API_BASE = isDev ? "http://localhost:3001" : "";

/**
 * Discovery service API base URL (for fetching session strings)
 * In dev: localhost:3001 (API server, accessible across all sites)
 * In prod: set via VITE_DISCOVERY_API_BASE environment variable
 */
export const DISCOVERY_API_BASE = isDev
  ? "http://localhost:3001"
  : import.meta.env["VITE_DISCOVERY_API_BASE"] || "";

/**
 * API endpoints with full URLs (for cross-origin requests in dev)
 */
export const API = {
  /** Get session strings from discovery service */
  SESSION_STRINGS: `${API_BASE}/api/session-strings`,
  /** Search man3 documentation */
  MAN3_SEARCH: `${API_BASE}/api/man3/search`,
  /** Health check endpoint */
  HEALTH: `${API_BASE}/api/health`,
} as const;

/**
 * Relative API endpoints (use these within the same origin)
 */
export const API_RELATIVE = {
  SESSION_STRINGS: "/api/session-strings",
  MAN3_SEARCH: "/api/man3/search",
  HEALTH: "/api/health",
} as const;

/**
 * Discovery service WebSocket connection URL
 * In dev: ws://localhost:27226 (local ACDS server)
 * In prod: wss://discovery-service.ascii-chat.com (production ACDS, default port 443)
 */
export const DISCOVERY_SERVICE_URL = isDev
  ? "ws://localhost:27226"
  : "wss://discovery-service.ascii-chat.com";

export type SiteKey = keyof typeof SITES;
