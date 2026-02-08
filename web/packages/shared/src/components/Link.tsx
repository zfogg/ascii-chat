import type { AnchorHTMLAttributes } from "react";

interface LinkProps extends Omit<AnchorHTMLAttributes<HTMLAnchorElement>, 'className'> {
  underline?: boolean;
  className?: string;
}

export function Link({ underline = false, children, className = "", ...props }: LinkProps) {
  const baseClass = underline ? "link-underline" : "link-standard";
  const combinedClassName = `${baseClass} ${className}`.trim();

  return (
    <a className={combinedClassName} {...props}>
      {children}
    </a>
  );
}
