import type { AnchorHTMLAttributes } from "react";

interface CommitLinkProps extends AnchorHTMLAttributes<HTMLAnchorElement> {
  commitSha: string;
}

export function CommitLink({
  commitSha,
  className = "",
  ...props
}: CommitLinkProps) {
  const isUnknown = commitSha === "unknown";
  const displayText = isUnknown ? "unknown commit" : commitSha.substring(0, 8);

  return (
    <a
      href={
        isUnknown
          ? undefined
          : `https://github.com/zfogg/ascii-chat/commit/${commitSha}`
      }
      target={isUnknown ? undefined : "_blank"}
      rel={isUnknown ? undefined : "noopener noreferrer"}
      className={className + (isUnknown ? " cursor-default" : "")}
      style={isUnknown ? { pointerEvents: "none" } : {}}
      {...props}
    >
      {displayText}
    </a>
  );
}
