import type { HTMLAttributes } from "react";

interface BadgeProps extends Omit<HTMLAttributes<HTMLSpanElement>, 'className'> {
  variant?: "primary" | "secondary" | "warning" | "error";
  className?: string;
}

export function Badge({ variant = "primary", children, className = "", ...props }: BadgeProps) {
  const baseClass = `badge-${variant}`;
  const combinedClassName = `${baseClass} ${className}`.trim();

  return (
    <span className={combinedClassName} {...props}>
      {children}
    </span>
  );
}
