import type { HTMLAttributes } from "react";
import { Prism as SyntaxHighlighter } from 'react-syntax-highlighter';
import { vscDarkPlus } from 'react-syntax-highlighter/dist/esm/styles/prism';
import { useRef, useEffect } from 'react';

interface CodeBlockProps extends Omit<HTMLAttributes<HTMLPreElement>, 'className'> {
  inline?: boolean;
  className?: string;
  language?: string;
  highlightLines?: { start: number; end: number };
  searchQuery?: string;
  showLineNumbers?: boolean;
}

export function CodeBlock({
  inline = false,
  children,
  className = "",
  language = 'bash',
  highlightLines,
  searchQuery,
  showLineNumbers = false,
  ...props
}: CodeBlockProps) {
  const codeRef = useRef<HTMLDivElement>(null);

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

  // Apply search highlighting after Prism renders
  useEffect(() => {
    if (!searchQuery || !codeRef.current) return;

    try {
      const codeBlock = codeRef.current.querySelector('code');
      if (!codeBlock) return;

      // Use regex to find matches and wrap them with spans
      const createRegex = (searchTerm: string) => {
        const escaped = searchTerm.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
        return new RegExp(`(${escaped})`, 'gi');
      };

      const regex = createRegex(searchQuery);

      // Walk through text nodes and apply highlighting
      const walker = document.createTreeWalker(
        codeBlock as Node,
        NodeFilter.SHOW_TEXT,
        null
      );

      const nodesToProcess: Node[] = [];
      let node: Node | null;
      while ((node = walker.nextNode())) {
        nodesToProcess.push(node);
      }

      // Process nodes in reverse to avoid invalidating references
      for (let i = nodesToProcess.length - 1; i >= 0; i--) {
        const textNode = nodesToProcess[i]!;
        const text = textNode.textContent || '';
        const parent = textNode.parentNode;
        if (!parent || !regex.test(text)) continue;

        regex.lastIndex = 0; // Reset regex after test
        let lastIndex = 0;
        let match;
        const fragment = document.createDocumentFragment();

        while ((match = regex.exec(text)) !== null) {
          // Add text before match
          if (match.index > lastIndex) {
            fragment.appendChild(document.createTextNode(text.slice(lastIndex, match.index)));
          }

          // Add highlighted match
          const span = document.createElement('span');
          span.style.backgroundColor = 'rgba(120, 53, 15, 0.5)';
          span.style.color = '#fef08a';
          span.textContent = match[0];
          fragment.appendChild(span);

          lastIndex = regex.lastIndex;
        }

        // Add remaining text
        if (lastIndex < text.length) {
          fragment.appendChild(document.createTextNode(text.slice(lastIndex)));
        }

        // Replace the text node with the fragment
        parent.replaceChild(fragment, textNode as Node);
      }
    } catch (e) {
      console.error('[CodeBlock] Error:', e);
    }
  }, [searchQuery, children]);

  return (
    <div ref={codeRef}>
      {/* @ts-expect-error - react-syntax-highlighter types incompatible with React 19 */}
      <SyntaxHighlighter
        language={language}
        style={vscDarkPlus}
        customStyle={{
          backgroundColor: '#111827',
          border: '1px solid #1f2937',
          borderRadius: '0.5rem',
          padding: '1rem',
          fontSize: '0.875rem',
          lineHeight: '1.5',
          margin: 0,
        }}
        codeTagProps={{
          style: {
            fontFamily: 'ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, "Liberation Mono", "Courier New", monospace',
          }
        }}
        showLineNumbers={showLineNumbers}
        lineProps={(lineNumber) => {
          const isHighlighted = highlightLines &&
            lineNumber >= highlightLines.start &&
            lineNumber <= highlightLines.end;

          return isHighlighted ? {
            style: {
              backgroundColor: '#fbbf24',
              display: 'block',
              padding: '0.125rem 0.5rem',
            }
          } : {};
        }}
      >
        {code}
      </SyntaxHighlighter>
    </div>
  );
}
