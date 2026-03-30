import type { ReactNode } from "react";
import { Fragment } from "react";
import { CommitLink } from "./CommitLink";
import { Link } from "./Link";

interface FooterProps {
  links: Array<{
    href: string;
    label: string;
    color: string;
    onClick?: () => void;
    target?: string;
    rel?: string;
  }>;
  commitSha: string;
  onCommitClick?: () => void;
  extraLine?: ReactNode;
  authorLinkColor?: string;
  className?: string;
}

export function Footer({
  links,
  commitSha,
  onCommitClick,
  extraLine,
  authorLinkColor = "text-cyan-400 hover:text-cyan-300",
  className = "mt-8 pt-8 border-t-2 border-gray-700",
}: FooterProps) {
  return (
    <footer
      className={`${className} w-full flex flex-col text-center text-sm md:text-base`}
      data-commit-sha={commitSha}
    >
      <p className="mx-auto flex flex-row flex-wrap justify-center items-center gap-2">
        {links.map((link, index) => (
          <Fragment key={link.href}>
            {index > 0 && <span className="text-gray-400">·</span>}
            <Link
              href={link.href}
              onClick={link.onClick}
              target={link.target}
              rel={link.rel}
              className={`${link.color} transition-colors whitespace-nowrap`}
            >
              {link.label}
            </Link>
          </Fragment>
        ))}
      </p>
      {extraLine && (
        <p className="text-xs md:text-sm text-gray-400 mt-4">{extraLine}</p>
      )}
      <p className="text-xs md:text-sm text-gray-400 mt-2">
        made with ❤️ by{" "}
        <Link
          href="https://zfo.gg/"
          target="_blank"
          rel="noopener noreferrer"
          className={authorLinkColor}
        >
          zfo.gg/
        </Link>
        {" · "}
        <CommitLink
          commitSha={commitSha}
          onClick={onCommitClick}
          className={`${authorLinkColor} font-mono text-xs`}
        />
      </p>
    </footer>
  );
}
