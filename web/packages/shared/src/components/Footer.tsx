import type { ReactNode } from "react";
import { CommitLink } from "./CommitLink";
import { Link } from "./Link";

interface FooterProps {
  links: Array<{
    href: string;
    label: string;
    color: string;
    onClick?: () => void;
  }>;
  commitSha: string;
  onCommitClick?: () => void;
  extraLine?: ReactNode;
  authorLinkColor?: string;
  className?: string;
}

export function Footer({ links, commitSha, onCommitClick, extraLine, authorLinkColor = "text-cyan-400 hover:text-cyan-300", className = "mt-16 pt-8 border-t-2 border-gray-700" }: FooterProps) {
  return (
    <footer className={`${className} text-center text-sm md:text-base`}>
      <p className="mb-3">
        {links.map((link, index) => (
          <span key={link.href}>
            {index > 0 && <span className="text-gray-500">{' · '}</span>}
            <Link
              href={link.href}
              onClick={link.onClick}
              className={`${link.color} transition-colors`}
            >
              {link.label}
            </Link>
          </span>
        ))}
      </p>
      {extraLine && (
        <p className="text-xs md:text-sm text-gray-600 mt-4">
          {extraLine}
        </p>
      )}
      <p className="text-xs md:text-sm text-gray-600 mt-2">
        made with ❤️ by{' '}
        <Link href="https://zfo.gg" className={authorLinkColor}>
          @zfogg
        </Link>
        {' · '}
        <CommitLink
          commitSha={commitSha}
          onClick={onCommitClick}
          className={`${authorLinkColor} font-mono text-xs`}
        />
      </p>
    </footer>
  );
}
