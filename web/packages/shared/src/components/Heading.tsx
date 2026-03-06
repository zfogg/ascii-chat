import type { HTMLAttributes } from "react";
import { createElement } from "react";

/**
 * Convert heading text to a URL-friendly ID
 */
function slugify(text: string): string {
  if (typeof text !== 'string') {
    // Handle React elements by extracting text content
    return text.toString().toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-+|-+$/g, '');
  }
  return text
    .toLowerCase()
    .trim()
    .replace(/[^\w\s-]/g, '')
    .replace(/[\s_]+/g, '-')
    .replace(/--+/g, '-')
    .replace(/^-+|-+$/g, '');
}

/**
 * Copy anchor link to clipboard
 */
function copyAnchorLink(id: string): void {
  const url = `${window.location.pathname}#${id}`;
  navigator.clipboard.writeText(url).then(() => {
    console.log(`Copied link: ${url}`);
  });
}

interface HeadingProps extends Omit<HTMLAttributes<HTMLHeadingElement>, 'className'> {
  level?: 1 | 2 | 3 | 4;
  className?: string;
  id?: string;
  anchorLink?: boolean;
}

export function Heading({ level = 1, children, className = "", id, anchorLink = true, ...props }: HeadingProps) {
  const baseClass = `heading-${level}`;

  // Auto-generate ID from children if not provided
  const headingId = id || (typeof children === 'string' ? slugify(children) : undefined);
  const combinedClassName = `${baseClass} ${className}`.trim();

  const content = anchorLink && headingId ? (
    <span className="group inline-flex items-center gap-2 cursor-pointer hover:opacity-75 transition-opacity" onClick={() => copyAnchorLink(headingId)} title="Click to copy anchor link">
      {children}
      <span className="opacity-0 group-hover:opacity-50 transition-opacity text-xs">🔗</span>
    </span>
  ) : (
    children
  );

  return createElement(
    `h${level}`,
    {
      className: combinedClassName,
      id: headingId,
      ...props
    },
    content
  );
}
