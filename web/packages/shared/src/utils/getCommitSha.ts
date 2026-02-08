import { execSync } from "child_process";

export function getCommitSha(): string {
  // Check for Vercel's built-in environment variable first
  if (process.env.VERCEL_GIT_COMMIT_SHA) {
    return process.env.VERCEL_GIT_COMMIT_SHA.substring(0, 8);
  }

  // Fall back to git command for local development
  try {
    return execSync("git rev-parse HEAD").toString().trim().substring(0, 8);
  } catch {
    return "unknown";
  }
}
