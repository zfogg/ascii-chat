import type { ButtonHTMLAttributes, AnchorHTMLAttributes } from "react";

type ButtonAsButtonProps = Omit<ButtonHTMLAttributes<HTMLButtonElement>, 'className'> & {
  variant?: "primary" | "secondary";
  className?: string;
  href?: never;
};

type ButtonAsLinkProps = Omit<AnchorHTMLAttributes<HTMLAnchorElement>, 'className'> & {
  variant?: "primary" | "secondary";
  className?: string;
  href: string;
};

type ButtonProps = ButtonAsButtonProps | ButtonAsLinkProps;

export function Button({ variant = "primary", children, className = "", ...props }: ButtonProps) {
  const baseClass = `btn-${variant}`;
  const combinedClassName = `${baseClass} ${className}`.trim();

  if ('href' in props && props.href) {
    const { ...anchorProps } = props as ButtonAsLinkProps;
    return (
      <a className={combinedClassName} {...anchorProps}>
        {children}
      </a>
    );
  }

  const { ...buttonProps } = props as ButtonAsButtonProps;
  return (
    <button className={combinedClassName} {...buttonProps}>
      {children}
    </button>
  );
}
