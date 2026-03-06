export function getCommitSha(): string {
  // Check for Vercel's built-in environment variable first
  if (typeof process !== "undefined" && process.env?.VERCEL_GIT_COMMIT_SHA) {
    return process.env.VERCEL_GIT_COMMIT_SHA.substring(0, 8);
  }

  // Fall back to git command for local development (Node.js only)
  if (typeof process !== "undefined" && process.versions?.node) {
    try {
      // Dynamic import to avoid bundling child_process in browser
      const { execSync } = require("child_process");
      return execSync("git rev-parse HEAD").toString().trim().substring(0, 8);
    } catch {
      return "unknown";
    }
  }

  return "unknown";
}
