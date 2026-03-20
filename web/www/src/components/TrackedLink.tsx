import { Link } from "react-router-dom";
import { trackLinkClick } from "../utils";
import type { ReactNode } from "react";

interface TrackedLinkProps {
  to?: string;
  href?: string;
  label: string;
  children: ReactNode;
  className?: string;
  target?: string;
  rel?: string;
  onClick?: () => void;
}

export default function TrackedLink({
  to,
  href,
  label,
  children,
  ...props
}: TrackedLinkProps) {
  const url = to || href;

  const handleClick = () => {
    trackLinkClick(url!, label);
    if (props.onClick) {
      props.onClick();
    }
  };

  if (to) {
    // Internal React Router link
    return (
      <Link to={to} onClick={handleClick} {...props}>
        {children}
      </Link>
    );
  }

  // External link
  return (
    <a href={href} onClick={handleClick} {...props}>
      {children}
    </a>
  );
}
