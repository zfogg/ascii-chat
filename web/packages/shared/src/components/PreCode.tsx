import type { HTMLAttributes } from "react";

interface PreCodeProps extends Omit<HTMLAttributes<HTMLPreElement>, 'className'> {
  className?: string;
  codeClassName?: string;
}

export function PreCode({ children, className = "", codeClassName = "", ...props }: PreCodeProps) {
  const preClass = `bg-gray-900 rounded-lg p-4 overflow-x-auto mb-4 ${className}`.trim();
  const codeClass = `text-teal-300 ${codeClassName}`.trim();

  return (
    <pre className={preClass} {...props}>
      <code className={codeClass}>{children}</code>
    </pre>
  );
}
