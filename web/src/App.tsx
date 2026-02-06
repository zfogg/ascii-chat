import { HomePage } from './pages/Home'
import { MirrorPage } from './pages/Mirror'
import { NotFoundPage } from './pages/NotFound'

export function App() {
  const path = window.location.pathname

  // Simple client-side routing
  if (path === '/' || path === '') {
    return <HomePage />
  }

  if (path === '/mirror' || path === '/mirror/') {
    return <MirrorPage />
  }

  if (path === '/client' || path === '/client/') {
    return (
      <div className="h-screen flex items-center justify-center">
        <div className="text-center">
          <h1 className="text-4xl font-bold text-terminal-green mb-4">ascii-chat | Client Mode</h1>
          <p className="text-terminal-fg mb-8">Coming soon: WebSocket client</p>
        </div>
      </div>
    )
  }

  if (path === '/discovery' || path === '/discovery/') {
    return (
      <div className="h-screen flex items-center justify-center">
        <div className="text-center">
          <h1 className="text-4xl font-bold text-terminal-magenta mb-4">ascii-chat | Discovery Mode</h1>
          <p className="text-terminal-fg mb-8">Coming soon: WebRTC P2P connections</p>
        </div>
      </div>
    )
  }

  return <NotFoundPage />
}
