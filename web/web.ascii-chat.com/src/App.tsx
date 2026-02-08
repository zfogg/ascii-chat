import { HelmetProvider } from 'react-helmet-async'
import { HomePage } from './pages/Home'
import { MirrorPage } from './pages/Mirror'
import { NotFoundPage } from './pages/NotFound'
import { ClientPage } from './pages/Client'
import { Header } from './components/Header'
import { Footer } from './components/Footer'

export function App() {
  const path = window.location.pathname

  // Simple client-side routing
  if (path === '/' || path === '') {
    return (
      <HelmetProvider>
        <div className="min-h-screen flex flex-col">
          <Header />
          <HomePage />
          <Footer />
        </div>
      </HelmetProvider>
    )
  }

  if (path === '/mirror' || path === '/mirror/') {
    return (
      <HelmetProvider>
        <div className="min-h-screen flex flex-col">
          <Header />
          <MirrorPage />
          <Footer />
        </div>
      </HelmetProvider>
    )
  }

  if (path === '/client' || path === '/client/') {
    return (
      <HelmetProvider>
        <ClientPage />
      </HelmetProvider>
    )
  }

  if (path === '/discovery' || path === '/discovery/') {
    return (
      <HelmetProvider>
        <div className="min-h-screen flex flex-col">
          <Header />
          <div className="flex-1 flex items-center justify-center">
            <div className="text-center">
              <h1 className="text-4xl font-bold text-terminal-magenta mb-4">ascii-chat | Discovery Mode</h1>
              <p className="text-terminal-fg mb-8">Coming soon: WebRTC P2P connections</p>
            </div>
          </div>
          <Footer />
        </div>
      </HelmetProvider>
    )
  }

  return (
    <HelmetProvider>
      <div className="min-h-screen flex flex-col">
        <Header />
        <NotFoundPage />
        <Footer />
      </div>
    </HelmetProvider>
  )
}
