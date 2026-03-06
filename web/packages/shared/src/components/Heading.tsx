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
    createElement(
      'a',
      {
        href: `#${headingId}`,
        className: 'group inline-flex items-center gap-2 hover:opacity-75 transition-opacity',
        title: 'Click to go to this section'
      },
      children,
      createElement('span', { className: 'opacity-0 group-hover:opacity-50 transition-opacity text-xs' }, '🔗')
    )
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
