import { useState, useEffect } from 'react'
import { Analytics } from '@vercel/analytics/react'
import '../App.css'

function Home() {
  const [sshKey, setSshKey] = useState('')
  const [gpgKey, setGpgKey] = useState('')
  const [baseUrl, setBaseUrl] = useState('')

  useEffect(() => {
    // Get the current domain
    setBaseUrl(window.location.origin)

    // Fetch public keys
    fetch('/key.pub')
      .then(r => r.text())
      .then(text => setSshKey(text.trim()))
      .catch(e => console.error('Failed to load SSH key:', e))

    fetch('/key.gpg')
      .then(r => r.text())
      .then(text => setGpgKey(text.trim()))
      .catch(e => console.error('Failed to load GPG key:', e))
  }, [])

  const handleSshDownload = () => {
    if (window.gtag) {
      window.gtag('event', 'download_ssh_key')
    }
  }

  const handleGpgDownload = () => {
    if (window.gtag) {
      window.gtag('event', 'download_gpg_key')
    }
  }

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
        <h1>🔍 ascii-chat Discovery Service</h1>
        <p className="subtitle">Official Public Keys</p>
        <p className="subtitle">
          Session signalling for <a href="https://ascii-chat.com" target="_blank" rel="noopener noreferrer" onClick={() => handleLinkClick('https://ascii-chat.com', 'ascii-chat website (header)')}>
            ascii-chat
          </a>
        </p>
      </header>

      <section>
        <h2>📋 About ACDS</h2>
        <p>
          The <strong>ascii-chat Discovery Service (ACDS)</strong> is a core component of{' '}
          <a href="https://ascii-chat.com" target="_blank" rel="noopener noreferrer" onClick={() => handleLinkClick('https://ascii-chat.com', 'ascii-chat website (about)')}>
            ascii-chat
          </a>, a real-time terminal-based video chat application. ACDS enables session discovery using
          memorable three-word strings like <code>happy-sunset-ocean</code> instead of IP addresses.
          It provides NAT traversal, WebRTC signaling, and peer-to-peer connection establishment.
        </p>
        <p>
          <strong>Privacy-first:</strong> ACDS only exchanges connection metadata—your audio and video
          never pass through our servers. All media flows peer-to-peer with end-to-end encryption.
        </p>

        <h3>🏗️ Official ACDS Infrastructure</h3>
        <p>The official ACDS deployment consists of two components:</p>
        <ul>
          <li>
            <strong>This website</strong> (<code>{window.location.hostname}</code>) - Serves public keys over HTTPS
          </li>
          <li>
            <strong>ACDS server</strong> (<code>discovery-service.ascii-chat.com:27225</code>) - Handles session management (TCP)
          </li>
        </ul>
        <p>
          The ascii-chat client is programmed to automatically connect to{' '}
          <code>discovery-service.ascii-chat.com:27225</code> and trust keys from this website.
        </p>
      </section>

      <section>
        <h2>🔑 Public Keys</h2>
        <p>
          These Ed25519 public keys are used to verify the identity of the official ACDS server at{' '}
          <code>{window.location.hostname}</code>. Download and verify these keys before connecting.
        </p>
        <p>Keys are available at:</p>
        <ul>
          <li>
            <a href="/key.pub" target="_blank" rel="noopener noreferrer">
              <code>{baseUrl}/key.pub</code>
            </a> (SSH)
          </li>
          <li>
            <a href="/key.gpg" target="_blank" rel="noopener noreferrer">
              <code>{baseUrl}/key.gpg</code>
            </a> (GPG)
          </li>
        </ul>

        <h3>SSH Ed25519 Public Key</h3>
        <p><strong>Fingerprint:</strong></p>
        <div className="fingerprint">
          SHA256:Uvr6k+9VjcC60gbVtcvwiVZDsIfB6jZvMuD4G2FME6w
        </div>
        <p><strong>Public Key:</strong></p>
        <pre><code>{sshKey || 'Loading...'}</code></pre>
        <a href="/key.pub" download className="download-link" onClick={handleSshDownload}>⬇ Download SSH Public Key</a>

        <h3>GPG Ed25519 Public Key</h3>
        <p><strong>Fingerprint:</strong></p>
        <div className="fingerprint">
          0AAE 7D67 D734 6959 74C3  6CEE C380 DA08 AF18 35B9
        </div>
        <p><strong>Public Key:</strong></p>
        <pre><code>{gpgKey || 'Loading...'}</code></pre>
        <a href="/key.gpg" download className="download-link" onClick={handleGpgDownload}>⬇ Download GPG Public Key</a>
      </section>

      <section>
        <h2>📖 Getting Help</h2>
        <p>
          For complete documentation and options, use the built-in help system:
        </p>
        <pre><code>{`# Read the full ascii-chat manual
man ascii-chat

# Get ACDS-specific help and options
ascii-chat discovery-service --help

# General ascii-chat help
ascii-chat --help`}</code></pre>
      </section>

      <section>
        <h2>💻 Usage Examples</h2>

        <h3>Server: Create a Session</h3>
        <pre><code>{`# Start a server and register with ACDS (uses discovery-service.ascii-chat.com by default)
ascii-chat server --discovery

# ACDS will return a session string like:
# Session: happy-sunset-ocean`}</code></pre>

        <h3>Client: Join a Session</h3>
        <pre><code>{`# Connect using the session string (uses discovery-service.ascii-chat.com by default)
ascii-chat client happy-sunset-ocean

# That's it! No configuration needed - the client automatically:
# - Connects to discovery-service.ascii-chat.com:27225
# - Trusts keys from ${window.location.hostname}
# - Looks up the session and connects to the server`}</code></pre>

        <h3>Manual Key Verification (Optional)</h3>
        <pre><code>{`# Download and verify SSH public key
curl -O ${baseUrl}/key.pub
ssh-keygen -lf key.pub

# Verify fingerprint matches: SHA256:Uvr6k+9VjcC60gbVtcvwiVZDsIfB6jZvMuD4G2FME6w

# Connect with explicit key verification (optional - automatic by default)
ascii-chat client happy-sunset-ocean --discovery-service-key ./key.pub`}</code></pre>
      </section>

      <section>
        <h2>🔒 Security</h2>
        <ul>
          <li><strong>Automatic Trust:</strong> The ascii-chat client automatically trusts keys from <code>discovery.ascii-chat.com</code> downloaded over HTTPS (official server only)</li>
          <li><strong>Key Verification:</strong> You can manually verify keys using the fingerprints shown above</li>
          <li><strong>Identity Verification:</strong> ACDS supports optional Ed25519 identity verification for servers and clients</li>
          <li><strong>No Media Access:</strong> ACDS never sees your video or audio—only connection metadata</li>
          <li><strong>End-to-End Encryption:</strong> All media flows peer-to-peer with ACIP encryption</li>
        </ul>
      </section>

      <section>
        <h2>🏗️ Running Your Own ACDS Server</h2>
        <p>
          You can run a private ACDS server for your organization. Third-party ACDS servers require
          clients to explicitly configure your public key via the <code>--discovery-service-key</code> flag.
        </p>
        <pre><code>{`# Start your own ACDS server with SSH and GPG keys
ascii-chat discovery-service 0.0.0.0 :: --port 27225 \\
  --key ~/.ssh/id_ed25519 \\
  --key gpg:YOUR_GPG_KEY_ID

# Server with GPG key
ascii-chat server --key gpg:SERVER_GPG_KEY_ID

# Client connects with explicit ACDS trust and authenticates with SSH key
ascii-chat client session-name \\
  --discovery-service your-acds.example.com \\
  --discovery-service-key https://your-acds.example.com/key.pub \\
  --key ~/.ssh/id_ed25519 \\
  --server-key gpg:SERVER_GPG_KEY_ID`}</code></pre>
        <p>
          <strong>Important:</strong> You should share the public key with ascii-chatters in a safe way.
          We recommend pre-sharing them safely somehow or hosting them on a website at a domain you control and
          serving them over HTTPS like we do.
          See the <a href="https://zfogg.github.io/ascii-chat/group__module__acds.html#acds_deployment" target="_blank" rel="noopener noreferrer" onClick={() => handleLinkClick('https://zfogg.github.io/ascii-chat/group__module__acds.html#acds_deployment', 'ACDS deployment documentation')}>ascii-chat documentation</a> for details.
        </p>
      </section>

      <footer>
        <p>
          <a href="https://github.com/zfogg/ascii-chat" target="_blank" rel="noopener noreferrer" onClick={() => handleLinkClick('https://github.com/zfogg/ascii-chat', 'GitHub (footer)')}>📦 GitHub</a>
          {' · '}
          <a href="https://zfogg.github.io/ascii-chat/group__module__acds.html" target="_blank" rel="noopener noreferrer" onClick={() => handleLinkClick('https://zfogg.github.io/ascii-chat/group__module__acds.html', 'ACDS Documentation (footer)')}>📚 ACDS Documentation</a>
          {' · '}
          <a href="https://github.com/zfogg/ascii-chat/issues" target="_blank" rel="noopener noreferrer" onClick={() => handleLinkClick('https://github.com/zfogg/ascii-chat/issues', 'Issues')}>🐛 Issues</a>
          {' · '}
          <a href="https://github.com/zfogg/ascii-chat/releases" target="_blank" rel="noopener noreferrer" onClick={() => handleLinkClick('https://github.com/zfogg/ascii-chat/releases', 'Releases')}>📦 Releases</a>
          {' · '}
          <a href="https://web.ascii-chat.com" target="_blank" rel="noopener noreferrer" onClick={() => handleLinkClick('https://web.ascii-chat.com', 'Web Client')}>🌐 Web Client</a>
        </p>
        <p className="legal">
          ascii-chat Discovery Service · Hosted at <code>{window.location.hostname}</code>
          {' · '}
          <a
            href={`https://github.com/zfogg/ascii-chat/commit/${__COMMIT_SHA__}`}
            target="_blank"
            rel="noopener noreferrer"
            onClick={() => handleLinkClick(`https://github.com/zfogg/ascii-chat/commit/${__COMMIT_SHA__}`, 'Commit SHA')}
            style={{ fontFamily: 'monospace', fontSize: '0.875rem' }}
          >
            {__COMMIT_SHA__}
          </a>
        </p>
      </footer>
      <Analytics />
    </div>
  )
}

export default Home
