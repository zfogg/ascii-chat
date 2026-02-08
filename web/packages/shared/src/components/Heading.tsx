import type { HTMLAttributes } from "react";
import { createElement } from "react";

interface HeadingProps extends Omit<HTMLAttributes<HTMLHeadingElement>, 'className'> {
  level?: 1 | 2 | 3 | 4;
  className?: string;
}

export function Heading({ level = 1, children, className = "", ...props }: HeadingProps) {
  const baseClass = `heading-${level}`;
  const combinedClassName = `${baseClass} ${className}`.trim();

  return createElement(`h${level}`, { className: combinedClassName, ...props }, children);
}
