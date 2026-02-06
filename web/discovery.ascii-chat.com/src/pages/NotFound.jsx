import '../App.css'

function NotFound() {
  const handleLinkClick = (url, text) => {
    if (window.gtag) {
      window.gtag('event', 'link_click', {
        link_url: url,
        link_text: text
      })
    }
  }

  return (
    <div className="container">
      <header>
        <h1 style={{ color: '#ff4444', fontSize: '3em' }}>404</h1>
        <p className="subtitle">Page Not Found</p>
        <p className="subtitle">The page you're looking for doesn't exist.</p>
      </header>

      <section style={{ textAlign: 'center' }}>
        <h2>🏠 Go Back</h2>
        <p>
          <a href="/" className="download-link" onClick={() => handleLinkClick('/', '404 back to home')}>← Back to Home</a>
        </p>
      </section>

      <footer>
        <p>
          <a href="https://github.com/zfogg/ascii-chat" target="_blank" rel="noopener noreferrer" onClick={() => handleLinkClick('https://github.com/zfogg/ascii-chat', 'GitHub (footer 404)')}>📦 GitHub</a>
          {' · '}
          <a href="https://zfogg.github.io/ascii-chat/group__module__acds.html" target="_blank" rel="noopener noreferrer" onClick={() => handleLinkClick('https://zfogg.github.io/ascii-chat/group__module__acds.html', 'ACDS Documentation (footer 404)')}>📚 ACDS Documentation</a>
          {' · '}
          <a href="https://github.com/zfogg/ascii-chat/issues" target="_blank" rel="noopener noreferrer" onClick={() => handleLinkClick('https://github.com/zfogg/ascii-chat/issues', 'Issues (404)')}>🐛 Issues</a>
          {' · '}
          <a href="https://github.com/zfogg/ascii-chat/releases" target="_blank" rel="noopener noreferrer" onClick={() => handleLinkClick('https://github.com/zfogg/ascii-chat/releases', 'Releases (404)')}>📦 Releases</a>
        </p>
        <p className="legal">
          ascii-chat Discovery Service
        </p>
      </footer>
    </div>
  )
}

export default NotFound
