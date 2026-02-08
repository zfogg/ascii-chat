import { useState, useEffect } from 'react'
import { Analytics } from '@vercel/analytics/react'
import { Footer, CodeBlock, PreCode, Heading, Button, Link } from '@ascii-chat/shared/components'
import { ACDSHead } from '../components/ACDSHead'

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
    <>
      <ACDSHead />
      <div className="max-w-4xl mx-auto px-4 md:px-8 py-8 md:py-16">
        <header className="text-center mb-12 pb-8 border-b-2 border-gray-700">
        <Heading level={1} className="mb-2 text-blue-400 text-3xl md:text-4xl">🔍 ascii-chat Discovery Service</Heading>
        <p className="text-gray-400 text-lg md:text-xl m-0">Official Public Keys</p>
        <p className="text-gray-400 text-lg md:text-xl m-0">
          Session signalling for <Link href="https://ascii-chat.com" onClick={() => handleLinkClick('https://ascii-chat.com', 'ascii-chat website (header)')}>
            ascii-chat
          </Link>
        </p>
      </header>

      <section className="mb-12">
        <Heading level={2} className="text-blue-400 border-b border-gray-700 pb-2 mb-4 text-2xl md:text-3xl">📋 About ACDS</Heading>
        <p className="leading-relaxed mb-4 text-base md:text-lg">
          The <strong>ascii-chat Discovery Service (ACDS)</strong> is a core component of{' '}
          <Link href="https://ascii-chat.com" onClick={() => handleLinkClick('https://ascii-chat.com', 'ascii-chat website (about)')}>
            ascii-chat
          </Link>, a real-time terminal-based video chat application. ACDS enables session discovery using
          memorable three-word strings like <code className="bg-gray-800 px-1 rounded">happy-sunset-ocean</code> instead of IP addresses.
          It provides NAT traversal, WebRTC signaling, and peer-to-peer connection establishment.
        </p>
        <p className="leading-relaxed mb-4 text-base md:text-lg">
          <strong>Privacy-first:</strong> ACDS only exchanges connection metadata—your audio and video
          never pass through our servers. All media flows peer-to-peer with end-to-end encryption.
        </p>

        <Heading level={3} className="text-gray-200 mt-6 mb-2 text-xl md:text-2xl">🏗️ Official ACDS Infrastructure</Heading>
        <p className="leading-relaxed mb-4 text-base md:text-lg">The official ACDS deployment consists of two components:</p>
        <ul className="leading-relaxed ml-0 pl-4 space-y-2">
          <li>
            <strong>This website</strong> (<code className="bg-gray-800 px-1 rounded">{window.location.hostname}</code>) - Serves public keys over HTTPS
          </li>
          <li>
            <strong>ACDS server</strong> (<code className="bg-gray-800 px-1 rounded">discovery-service.ascii-chat.com:27225</code>) - Handles session management (TCP)
          </li>
        </ul>
        <p className="leading-relaxed mb-4 text-base md:text-lg">
          The ascii-chat client is programmed to automatically connect to{' '}
          <code className="bg-gray-800 px-1 rounded">discovery-service.ascii-chat.com:27225</code> and trust keys from this website.
        </p>
      </section>

      <section className="mb-12">
        <Heading level={2} className="text-blue-400 border-b border-gray-700 pb-2 mb-4 text-2xl md:text-3xl">🔑 Public Keys</Heading>
        <p className="leading-relaxed mb-4 text-base md:text-lg">
          These Ed25519 public keys are used to verify the identity of the official ACDS server at{' '}
          <code className="bg-gray-800 px-1 rounded">{window.location.hostname}</code>. Download and verify these keys before connecting.
        </p>
        <p className="leading-relaxed mb-4 text-base md:text-lg">Keys are available at:</p>
        <ul className="leading-relaxed ml-0 pl-4 space-y-2">
          <li>
            <Link href="/key.pub">
              <code className="bg-gray-800 px-1 rounded">{baseUrl}/key.pub</code>
            </Link> (SSH)
          </li>
          <li>
            <Link href="/key.gpg">
              <code className="bg-gray-800 px-1 rounded">{baseUrl}/key.gpg</code>
            </Link> (GPG)
          </li>
        </ul>

        <Heading level={3} className="text-gray-200 mt-6 mb-2 text-xl md:text-2xl">SSH Ed25519 Public Key</Heading>
        <p className="leading-relaxed mb-2 text-base md:text-lg"><strong>Fingerprint:</strong></p>
        <PreCode>SHA256:Uvr6k+9VjcC60gbVtcvwiVZDsIfB6jZvMuD4G2FME6w</PreCode>
        <p className="leading-relaxed mb-2 text-base md:text-lg"><strong>Public Key:</strong></p>
        <PreCode>{sshKey || 'Loading...'}</PreCode>
        <Button href="/key.pub" download onClick={handleSshDownload} className="inline-block mt-2 mb-4">⬇ Download SSH Public Key</Button>

        <Heading level={3} className="text-gray-200 mt-6 mb-2 text-xl md:text-2xl">GPG Ed25519 Public Key</Heading>
        <p className="leading-relaxed mb-2 text-base md:text-lg"><strong>Fingerprint:</strong></p>
        <PreCode>0AAE 7D67 D734 6959 74C3  6CEE C380 DA08 AF18 35B9</PreCode>
        <p className="leading-relaxed mb-2 text-base md:text-lg"><strong>Public Key:</strong></p>
        <PreCode>{gpgKey || 'Loading...'}</PreCode>
        <Button href="/key.gpg" download onClick={handleGpgDownload} className="inline-block mt-2 mb-4">⬇ Download GPG Public Key</Button>
      </section>

      <section className="mb-12">
        <Heading level={2} className="text-blue-400 border-b border-gray-700 pb-2 mb-4 text-2xl md:text-3xl">📖 Getting Help</Heading>
        <p className="leading-relaxed mb-4 text-base md:text-lg">
          For complete documentation and options, use the built-in help system:
        </p>
        <CodeBlock language="bash">{`# Read the full ascii-chat manual
man ascii-chat

# Get ACDS-specific help and options
ascii-chat discovery-service --help

# General ascii-chat help
ascii-chat --help`}</CodeBlock>
      </section>

      <section className="mb-12">
        <Heading level={2} className="text-blue-400 border-b border-gray-700 pb-2 mb-4 text-2xl md:text-3xl">💻 Usage Examples</Heading>

        <Heading level={3} className="text-gray-200 mt-6 mb-2 text-xl md:text-2xl">Server: Create a Session</Heading>
        <CodeBlock language="bash">{`# Start a server and register with ACDS (uses discovery-service.ascii-chat.com by default)
ascii-chat server --discovery

# ACDS will return a session string like:
# Session: happy-sunset-ocean`}</CodeBlock>

        <Heading level={3} className="text-gray-200 mt-6 mb-2 text-xl md:text-2xl">Client: Join a Session</Heading>
        <CodeBlock language="bash">{`# Connect using the session string (uses discovery-service.ascii-chat.com by default)
ascii-chat client happy-sunset-ocean

# That's it! No configuration needed - the client automatically:
# - Connects to discovery-service.ascii-chat.com:27225
# - Trusts keys from ${window.location.hostname}
# - Looks up the session and connects to the server`}</CodeBlock>

        <Heading level={3} className="text-gray-200 mt-6 mb-2 text-xl md:text-2xl">Manual Key Verification (Optional)</Heading>
        <CodeBlock language="bash">{`# Download and verify SSH public key
curl -O ${baseUrl}/key.pub
ssh-keygen -lf key.pub

# Verify fingerprint matches: SHA256:Uvr6k+9VjcC60gbVtcvwiVZDsIfB6jZvMuD4G2FME6w

# Connect with explicit key verification (optional - automatic by default)
ascii-chat client happy-sunset-ocean --discovery-service-key ./key.pub`}</CodeBlock>
      </section>

      <section className="mb-12">
        <Heading level={2} className="text-blue-400 border-b border-gray-700 pb-2 mb-4 text-2xl md:text-3xl">🔒 Security</Heading>
        <ul className="leading-relaxed ml-0 pl-4 space-y-2">
          <li><strong>Automatic Trust:</strong> The ascii-chat client automatically trusts keys from <code className="bg-gray-800 px-1 rounded">discovery.ascii-chat.com</code> downloaded over HTTPS (official server only)</li>
          <li><strong>Key Verification:</strong> You can manually verify keys using the fingerprints shown above</li>
          <li><strong>Identity Verification:</strong> ACDS supports optional Ed25519 identity verification for servers and clients</li>
          <li><strong>No Media Access:</strong> ACDS never sees your video or audio—only connection metadata</li>
          <li><strong>End-to-End Encryption:</strong> All media flows peer-to-peer with ACIP encryption</li>
        </ul>
      </section>

      <section className="mb-12">
        <Heading level={2} className="text-blue-400 border-b border-gray-700 pb-2 mb-4 text-2xl md:text-3xl">🏗️ Running Your Own ACDS Server</Heading>
        <p className="leading-relaxed mb-4 text-base md:text-lg">
          You can run a private ACDS server for your organization. Third-party ACDS servers require
          clients to explicitly configure your public key via the <code className="bg-gray-800 px-1 rounded">--discovery-service-key</code> flag.
        </p>
        <CodeBlock language="bash">{`# Start your own ACDS server with SSH and GPG keys
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
  --server-key gpg:SERVER_GPG_KEY_ID`}</CodeBlock>
        <p className="leading-relaxed mb-4 text-base md:text-lg">
          <strong>Important:</strong> You should share the public key with ascii-chatters in a safe way.
          We recommend pre-sharing them safely somehow or hosting them on a website at a domain you control and
          serving them over HTTPS like we do.
          See the <Link href="https://zfogg.github.io/ascii-chat/group__module__acds.html#acds_deployment" onClick={() => handleLinkClick('https://zfogg.github.io/ascii-chat/group__module__acds.html#acds_deployment', 'ACDS deployment documentation')}>ascii-chat documentation</Link> for details.
        </p>
      </section>

      <Footer
        links={[
          { href: 'https://github.com/zfogg/ascii-chat', label: '📦 GitHub', color: 'text-cyan-400 hover:text-cyan-300', onClick: () => handleLinkClick('https://github.com/zfogg/ascii-chat', 'GitHub (footer)') },
          { href: 'https://zfogg.github.io/ascii-chat/group__module__acds.html', label: '📚 ACDS Documentation', color: 'text-teal-400 hover:text-teal-300', onClick: () => handleLinkClick('https://zfogg.github.io/ascii-chat/group__module__acds.html', 'ACDS Documentation (footer)') },
          { href: 'https://github.com/zfogg/ascii-chat/issues', label: '🐛 Issues', color: 'text-purple-400 hover:text-purple-300', onClick: () => handleLinkClick('https://github.com/zfogg/ascii-chat/issues', 'Issues') },
          { href: 'https://github.com/zfogg/ascii-chat/releases', label: '📦 Releases', color: 'text-pink-400 hover:text-pink-300', onClick: () => handleLinkClick('https://github.com/zfogg/ascii-chat/releases', 'Releases') },
          { href: 'https://web.ascii-chat.com', label: '🌐 Web Client', color: 'text-yellow-400 hover:text-yellow-300', onClick: () => handleLinkClick('https://web.ascii-chat.com', 'Web Client') },
        ]}
        commitSha={__COMMIT_SHA__}
        onCommitClick={() => handleLinkClick(`https://github.com/zfogg/ascii-chat/commit/${__COMMIT_SHA__}`, 'Commit SHA')}
        extraLine={<>ascii-chat Discovery Service · Hosted at <code className="bg-gray-800 px-1 rounded">{window.location.hostname}</code></>}
      />
      <Analytics />
      </div>
    </>
  )
}

export default Home
