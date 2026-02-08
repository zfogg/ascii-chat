import type { HTMLAttributes } from "react";

interface CardProps extends Omit<HTMLAttributes<HTMLDivElement>, 'className'> {
  variant?: "default" | "subtle" | "interactive" | "standard";
  accent?: "cyan" | "purple" | "teal" | "pink" | "green" | "yellow" | "red";
  className?: string;
}

export function Card({
  variant = "default",
  accent,
  children,
  className = "",
  ...props
}: CardProps) {
  const variantClass = variant === "default" ? "card" : `card-${variant}`;
  const accentClass = accent ? `accent-${accent}` : "";
  const combinedClassName = `${variantClass} ${accentClass} ${className}`.trim();

  return (
    <div className={combinedClassName} {...props}>
      {children}
    </div>
  );
}
