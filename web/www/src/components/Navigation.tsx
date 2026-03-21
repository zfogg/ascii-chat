import { useState, useEffect } from "react";
import { SITES } from "@ascii-chat/shared/utils";
import { useSafeLocation } from "../hooks/useSafeLocation";
import TrackedLink from "./TrackedLink";

export default function Navigation() {
  const [mobileMenuOpen, setMobileMenuOpen] = useState(false);
  const location = useSafeLocation();

  const isActive = (path: string): boolean => location.pathname === path;

  // Focus first nav link when mobile menu opens for accessibility
  useEffect(() => {
    if (mobileMenuOpen) {
      const firstLink = document.querySelector(
        ".md\\:hidden + .md\\:hidden a:first-of-type",
      ) as HTMLAnchorElement;
      if (firstLink) {
        setTimeout(() => firstLink.focus(), 0);
      }
    }
  }, [mobileMenuOpen]);

  const getDesktopNavLinkClass = (paths: string | string[]): string => {
    const active = Array.isArray(paths)
      ? paths.some((p) => isActive(p))
      : isActive(paths);
    return `transition-colors py-1 px-2 rounded ${
      active
        ? "text-fuchsia-400 bg-gray-800 font-semibold"
        : "text-gray-400 hover:text-fuchsia-300"
    }`;
  };

  const getMobileNavLinkClass = (paths?: string | string[]): string => {
    if (!paths) {
      return "block py-3 px-4 transition-colors rounded text-gray-400 hover:text-fuchsia-300 hover:bg-gray-800/50";
    }
    const active = Array.isArray(paths)
      ? paths.some((p) => isActive(p))
      : isActive(paths);
    return `block py-3 px-4 transition-colors rounded ${
      active
        ? "text-fuchsia-400 bg-gray-800 font-semibold"
        : "text-gray-400 hover:text-fuchsia-300 hover:bg-gray-800/50"
    }`;
  };

  const navItems = [
    { to: "/", label: "Home", paths: "/" },
    { to: "/docs", label: "Docs", paths: ["/docs", "/docs/", "/crypto"] },
    { to: "/man1", label: "ascii-chat(1)", paths: "/man1" },
    { to: "/man5", label: "ascii-chat(5)", paths: "/man5" },
    { to: "/man3", label: "ascii-chat-*(3)", paths: "/man3" },
  ];

  const handleNavClick = () => {
    // Scroll the main scrollable container to top
    const scrollContainer = document.querySelector("div.overflow-y-auto");
    if (scrollContainer) {
      scrollContainer.scrollTop = 0;
    }
    setMobileMenuOpen(false);
  };

  return (
    <>
      {/* Mobile menu backdrop */}
      {mobileMenuOpen && (
        <div
          className="fixed inset-0 z-40 md:hidden"
          onClick={() => setMobileMenuOpen(false)}
        />
      )}
      <nav className="border-b border-gray-800 bg-gray-950/50 backdrop-blur-sm fixed top-0 left-0 right-0 z-50 w-full">
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

            {/* Desktop Navigation */}
            <div className="hidden md:flex gap-6 text-sm items-center">
              {navItems.map(({ to, label, paths }) => {
                const active = Array.isArray(paths)
                  ? paths.some((p) => isActive(p))
                  : isActive(paths);
                return (
                  <TrackedLink
                    key={to}
                    to={to}
                    label={`Nav - ${label}`}
                    className={getDesktopNavLinkClass(paths)}
                    onClick={handleNavClick}
                    aria-current={active ? "page" : undefined}
                  >
                    {label}
                  </TrackedLink>
                );
              })}
              <TrackedLink
                href={SITES.WEB}
                label="Nav - Web Client"
                className="transition-colors py-1 px-2 rounded text-gray-400 hover:text-fuchsia-300"
                target="_blank"
                rel="noopener noreferrer"
              >
                Web Client
              </TrackedLink>
            </div>

            {/* Mobile Menu Button */}
            <button
              onClick={() => setMobileMenuOpen(!mobileMenuOpen)}
              className="md:hidden text-gray-400 hover:text-fuchsia-300 transition-colors p-2"
              aria-label="Toggle menu"
              aria-expanded={mobileMenuOpen}
            >
              <svg
                className="w-6 h-6"
                fill="none"
                stroke="currentColor"
                viewBox="0 0 24 24"
              >
                {mobileMenuOpen ? (
                  <path
                    strokeLinecap="round"
                    strokeLinejoin="round"
                    strokeWidth={2}
                    d="M6 18L18 6M6 6l12 12"
                  />
                ) : (
                  <path
                    strokeLinecap="round"
                    strokeLinejoin="round"
                    strokeWidth={2}
                    d="M4 6h16M4 12h16M4 18h16"
                  />
                )}
              </svg>
            </button>
          </div>

          {/* Mobile Navigation */}
          {mobileMenuOpen && (
            <div className="md:hidden mt-4 pb-2 border-t border-gray-800 pt-4">
              {navItems.map(({ to, label, paths }) => {
                const active = Array.isArray(paths)
                  ? paths.some((p) => isActive(p))
                  : isActive(paths);
                return (
                  <TrackedLink
                    key={to}
                    to={to}
                    label={`Nav - ${label}`}
                    className={getMobileNavLinkClass(paths)}
                    onClick={handleNavClick}
                    aria-current={active ? "page" : undefined}
                  >
                    {label}
                  </TrackedLink>
                );
              })}
              <TrackedLink
                href={SITES.WEB}
                label="Nav - Web Client"
                className={getMobileNavLinkClass()}
                target="_blank"
                rel="noopener noreferrer"
                onClick={() => setMobileMenuOpen(false)}
              >
                Web Client
              </TrackedLink>
            </div>
          )}
        </div>
      </nav>
    </>
  );
}
