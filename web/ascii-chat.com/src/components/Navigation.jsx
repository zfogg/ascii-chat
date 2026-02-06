import { useLocation } from "react-router-dom";
import TrackedLink from "./TrackedLink";

export default function Navigation() {
  const location = useLocation();

  const isActive = (path) => location.pathname === path;

  const getNavLinkClass = (paths) => {
    const active = Array.isArray(paths)
      ? paths.some((p) => isActive(p))
      : isActive(paths);
    return `transition-colors ${
      active ? "text-fuchsia-400" : "text-gray-400 hover:text-fuchsia-300"
    }`;
  };

  return (
    <nav className="border-b border-gray-800 bg-gray-950/50 backdrop-blur-sm sticky top-0 z-50">
      <div className="max-w-5xl mx-auto px-4 sm:px-6 py-3 sm:py-4">
        <div className="flex items-center justify-between">
          <TrackedLink
            to="/"
            label="Nav - Logo"
            className="text-xl sm:text-2xl font-bold"
          >
            <span className="text-cyan-400">ascii</span>
            <span className="text-purple-400">-</span>
            <span className="text-teal-400">chat</span>
          </TrackedLink>

          <div className="flex gap-3 sm:gap-6 text-sm sm:text-base">
            <TrackedLink
              to="/"
              label="Nav - Home"
              className={`transition-colors ${
                isActive("/")
                  ? "text-cyan-400"
                  : "text-gray-400 hover:text-cyan-300"
              }`}
            >
              Home
            </TrackedLink>
            <TrackedLink
              to="/docs"
              label="Nav - Docs"
              className={getNavLinkClass(["/docs", "/docs/", "/crypto"])}
            >
              Docs
            </TrackedLink>
            <div className="flex gap-3 sm:gap-4 text-sm sm:text-base">
              <TrackedLink
                to="/man1"
                label="Nav - Man Page (1)"
                className={getNavLinkClass("/man1")}
              >
                ascii-chat(1)
              </TrackedLink>
              <TrackedLink
                to="/man5"
                label="Nav - Man Page (5)"
                className={getNavLinkClass("/man5")}
              >
                ascii-chat(5)
              </TrackedLink>
            </div>
          </div>
        </div>
      </div>
    </nav>
  );
}
