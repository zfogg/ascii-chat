import { useState } from 'react'

export function Header() {
  const [mobileMenuOpen, setMobileMenuOpen] = useState(false)
  const currentPath = window.location.pathname.replace(/\/$/, '') || '/'

  const isActive = (path: string) => {
    return currentPath === path
  }

  const navLinks = [
    { href: '/', label: 'Home' },
    { href: '/mirror', label: 'Mirror' },
    { href: '/client', label: 'Client' },
    { href: '/discovery', label: 'Discovery' },
  ]

  return (
    <header className="border-b border-terminal-8 bg-terminal-0 sticky top-0 z-50">
      <div className="px-4 py-3 md:px-6 md:py-4">
        <div className="flex items-center justify-between">
          <a
            href="/"
            className="text-xl md:text-2xl font-bold text-terminal-cyan hover:text-terminal-brightCyan transition-colors"
          >
            ascii-chat <span className="text-sm md:text-base text-terminal-8 font-normal">(web client)</span>
          </a>

          {/* Desktop Navigation */}
          <nav className="hidden md:flex gap-6 text-sm">
            {navLinks.map(({ href, label }) => (
              <a
                key={href}
                href={href}
                className={`transition-colors py-1 px-2 rounded ${
                  isActive(href)
                    ? 'text-terminal-brightCyan bg-terminal-8 font-semibold'
                    : 'text-terminal-fg hover:text-terminal-cyan'
                }`}
              >
                {label}
              </a>
            ))}
          </nav>

          {/* Mobile Menu Button */}
          <button
            onClick={() => setMobileMenuOpen(!mobileMenuOpen)}
            className="md:hidden text-terminal-fg hover:text-terminal-cyan transition-colors p-2"
            aria-label="Toggle menu"
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
          <nav className="md:hidden mt-4 pb-2 border-t border-terminal-8 pt-4">
            {navLinks.map(({ href, label }) => (
              <a
                key={href}
                href={href}
                className={`block py-3 px-4 transition-colors rounded ${
                  isActive(href)
                    ? 'text-terminal-brightCyan bg-terminal-8 font-semibold'
                    : 'text-terminal-fg hover:text-terminal-cyan hover:bg-terminal-8/50'
                }`}
                onClick={() => setMobileMenuOpen(false)}
              >
                {label}
              </a>
            ))}
          </nav>
        )}
      </div>
    </header>
  )
}
