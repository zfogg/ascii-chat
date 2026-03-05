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
}

export function CodeBlock({
  inline = false,
  children,
  className = "",
  language = 'bash',
  highlightLines,
  searchQuery,
  ...props
}: CodeBlockProps) {
  const codeRef = useRef<HTMLElement>(null);

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

      // Get all text nodes
      const walker = document.createTreeWalker(
        codeBlock,
        NodeFilter.SHOW_TEXT,
        null,
        false
      );

      const allTextNodes: Node[] = [];
      let node: Node | null;
      while ((node = walker.nextNode())) {
        allTextNodes.push(node);
      }


      // Create regex for search term
      const searchRegex = searchQuery.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
      const regex = new RegExp(`(${searchRegex})`, 'gi');

      // Combine all text nodes to find matches across tokens
      const fullText = allTextNodes.map(n => n.textContent || '').join('');
      const matches: Array<{ text: string; startNodeIdx: number; startOffset: number }> = [];

      let match;
      while ((match = regex.exec(fullText)) !== null) {
        // Find which text node this match starts in
        let charCount = 0;
        let startNodeIdx = 0;
        let startOffset = 0;

        for (let i = 0; i < allTextNodes.length; i++) {
          const nodeLength = (allTextNodes[i].textContent || '').length;
          if (charCount + nodeLength > match.index) {
            startNodeIdx = i;
            startOffset = match.index - charCount;
            break;
          }
          charCount += nodeLength;
        }

        matches.push({
          text: match[0],
          startNodeIdx,
          startOffset,
        });
      }


      // Apply highlighting for each match
      matches.forEach((matchNum, matchIndex) => {
        const match = matchNum;
        let remaining = match.text;
        let nodeIdx = match.startNodeIdx;
        let offset = match.startOffset;

        while (remaining && nodeIdx < allTextNodes.length) {
          const node = allTextNodes[nodeIdx];
          const nodeText = node.textContent || '';
          const availableInNode = nodeText.length - offset;

          if (availableInNode <= 0) {
            nodeIdx++;
            offset = 0;
            continue;
          }

          const takeFromNode = Math.min(remaining.length, availableInNode);
          const matchPart = nodeText.substring(offset, offset + takeFromNode);

          if (offset === 0 && takeFromNode === nodeText.length) {
            // Replace entire node
            const parent = node.parentNode;
            if (parent) {
              const highlightSpan = document.createElement('span');
              highlightSpan.style.backgroundColor = 'rgba(120, 53, 15, 0.5)'; // yellow-900/50
              highlightSpan.style.color = '#fef08a'; // yellow-200
              highlightSpan.textContent = nodeText;
              parent.replaceChild(highlightSpan, node);
              allTextNodes[nodeIdx] = highlightSpan;
            }
          } else if (offset > 0 && takeFromNode === nodeText.length - offset) {
            // Highlight rest of node from offset
            const beforeText = nodeText.substring(0, offset);
            const beforeNode = document.createTextNode(beforeText);
            const highlightSpan = document.createElement('span');
            highlightSpan.style.backgroundColor = 'rgba(120, 53, 15, 0.5)'; // yellow-900/50
            highlightSpan.style.color = '#fef08a'; // yellow-200
            highlightSpan.textContent = matchPart;

            const parent = node.parentNode;
            if (parent) {
              parent.insertBefore(beforeNode, node);
              parent.replaceChild(highlightSpan, node);
              allTextNodes[nodeIdx] = highlightSpan;
            }
          } else {
            // Split node: before + highlight + after
            const beforeText = nodeText.substring(0, offset);
            const afterText = nodeText.substring(offset + takeFromNode);
            const beforeNode = document.createTextNode(beforeText);
            const highlightSpan = document.createElement('span');
            highlightSpan.style.backgroundColor = 'rgba(120, 53, 15, 0.5)'; // yellow-900/50
            highlightSpan.style.color = '#fef08a'; // yellow-200
            highlightSpan.textContent = matchPart;
            const afterNode = document.createTextNode(afterText);

            const parent = node.parentNode;
            if (parent) {
              parent.insertBefore(beforeNode, node);
              parent.insertBefore(highlightSpan, node);
              parent.replaceChild(afterNode, node);
            }
          }

          remaining = remaining.substring(takeFromNode);
          nodeIdx++;
          offset = 0;
        }
      });

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
