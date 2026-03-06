export function getCommitSha(): string {
  // Check for Vercel's built-in environment variable first
  if (typeof process !== "undefined") {
    const proc = process as typeof process & { env?: Record<string, string | undefined> };
    if (proc.env?.["VERCEL_GIT_COMMIT_SHA"]) {
      return proc.env["VERCEL_GIT_COMMIT_SHA"].substring(0, 8);
    }

    // Fall back to git command for local development (Node.js only)
    const procWithVersions = process as typeof process & { versions?: { node?: string } };
    if (procWithVersions.versions?.node) {
      try {
        // Dynamic import to avoid bundling child_process in browser
        const { execSync } = require("child_process") as typeof import("child_process");
        return execSync("git rev-parse HEAD").toString().trim().substring(0, 8);
      } catch {
        return "unknown";
      }
    }
  }

  return "unknown";
}
