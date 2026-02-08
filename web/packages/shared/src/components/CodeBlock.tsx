import type { HTMLAttributes } from "react";

interface CodeBlockProps extends Omit<HTMLAttributes<HTMLPreElement>, 'className'> {
  inline?: boolean;
  className?: string;
}

export function CodeBlock({ inline = false, children, className = "", ...props }: CodeBlockProps) {
  if (inline) {
    return (
      <code className={`code-inline ${className}`.trim()} {...props}>
        {children}
      </code>
    );
  }

  return (
    <pre className={`code-block ${className}`.trim()} {...props}>
      <code className="code-content">{children}</code>
    </pre>
  );
}
