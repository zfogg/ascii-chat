import { Link } from "react-router-dom";
import { trackLinkClick } from "../utils/analytics";

export default function TrackedLink({ to, href, label, children, ...props }) {
  const url = to || href;
  const isExternal = href && (href.startsWith("http") || href.startsWith("//"));

  const handleClick = () => {
    trackLinkClick(url, label);
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
