/**
 * AnchoredHeading component - creates headings with auto-generated IDs and clickable anchor links
 *
 * Usage:
 *   <AnchoredHeading level={2}>Getting Started</AnchoredHeading>
 *   <AnchoredHeading level={3} className="text-cyan-300">Installation</AnchoredHeading>
 */
function slugify(text) {
  if (typeof text !== 'string') {
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

export function AnchoredHeading({ level = 2, children, className = '', ...props }) {
  const id = slugify(typeof children === 'string' ? children : '');
  const HeadingTag = `h${level}`;

  return (
    <HeadingTag
      id={id}
      className={className}
      {...props}
    >
      <a
        href={`#${id}`}
        className="group inline-flex items-center gap-2 hover:opacity-75 transition-opacity"
        title="Click to go to this section"
      >
        {children}
        <span className="opacity-0 group-hover:opacity-50 transition-opacity text-xs">
          🔗
        </span>
      </a>
    </HeadingTag>
  );
}
