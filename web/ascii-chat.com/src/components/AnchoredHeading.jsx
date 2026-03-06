import { slugify, copyAnchorLink } from '../utils/anchors';

/**
 * AnchoredHeading component - creates headings with auto-generated IDs and clickable anchor links
 *
 * Usage:
 *   <AnchoredHeading level={2}>Getting Started</AnchoredHeading>
 *   <AnchoredHeading level={3} className="text-cyan-300">Installation</AnchoredHeading>
 */
export default function AnchoredHeading({ level = 2, children, className = '', ...props }) {
  const id = slugify(typeof children === 'string' ? children : '');
  const HeadingTag = `h${level}`;

  return (
    <HeadingTag
      id={id}
      className={`group cursor-pointer hover:opacity-75 transition-opacity ${className}`}
      onClick={() => copyAnchorLink(id)}
      title="Click to copy anchor link"
      {...props}
    >
      <span className="inline-flex items-center gap-2">
        {children}
        <span className="opacity-0 group-hover:opacity-50 transition-opacity text-xs">
          🔗
        </span>
      </span>
    </HeadingTag>
  );
}
