export function getCommitSha(): string {
  // Check for Node.js process global via globalThis (cast to any to handle browser/node differences)
  const proc = (globalThis as any).process as any;

  // Check for Vercel's built-in environment variable first
  if (proc?.env?.["VERCEL_GIT_COMMIT_SHA"]) {
    return proc.env["VERCEL_GIT_COMMIT_SHA"].substring(0, 8);
  }

  // Fall back to git command for local development (Node.js only)
  if (proc?.versions?.node) {
    try {
      // Use indirect require to get execSync from child_process
      const mod = (globalThis as any).require as any;
      const { execSync } = mod("child_process");
      return execSync("git rev-parse HEAD").toString().trim().substring(0, 8);
    } catch {
      return "unknown";
    }
  }

  return "unknown";
}
