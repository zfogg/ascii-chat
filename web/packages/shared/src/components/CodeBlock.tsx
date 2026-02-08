import type { HTMLAttributes } from "react";
import { Prism as SyntaxHighlighter } from 'react-syntax-highlighter';
import { vscDarkPlus } from 'react-syntax-highlighter/dist/esm/styles/prism';

interface CodeBlockProps extends Omit<HTMLAttributes<HTMLPreElement>, 'className'> {
  inline?: boolean;
  className?: string;
  language?: string;
}

export function CodeBlock({
  inline = false,
  children,
  className = "",
  language = 'bash',
  ...props
}: CodeBlockProps) {
  if (inline) {
    return (
      <code className={`code-inline ${className}`.trim()} {...props}>
        {children}
      </code>
    );
  }

  // Extract text content from children
  const code = typeof children === 'string'
    ? children
    : String(children);

  return (
    <SyntaxHighlighter
      language={language}
      style={vscDarkPlus}
      customStyle={{
        backgroundColor: '#111827',
        border: '1px solid #1f2937',
        borderRadius: '0.5rem',
        padding: '1rem',
        margin: 0,
        fontSize: '0.875rem',
        lineHeight: '1.5',
      }}
      codeTagProps={{
        style: {
          fontFamily: 'ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, "Liberation Mono", "Courier New", monospace',
        }
      }}
    >
      {code}
    </SyntaxHighlighter>
  );
}
