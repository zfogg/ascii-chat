import type { AnchorHTMLAttributes } from "react";

interface CommitLinkProps extends AnchorHTMLAttributes<HTMLAnchorElement> {
  commitSha: string;
}

export function CommitLink({ commitSha, className = "", ...props }: CommitLinkProps) {
  return (
    <a
      href={`https://github.com/zfogg/ascii-chat/commit/${commitSha}`}
      target="_blank"
      rel="noopener noreferrer"
      className={className}
      {...props}
    >
      {commitSha}
    </a>
  );
}
