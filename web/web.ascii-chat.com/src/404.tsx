import ReactDOM from 'react-dom/client'
import { StrictMode } from 'react'
import { HelmetProvider } from 'react-helmet-async'
import { NotFoundPage } from './pages/NotFound'
import './style.css'

ReactDOM.createRoot(document.getElementById('app')!).render(
  <StrictMode>
    <HelmetProvider>
      <NotFoundPage />
    </HelmetProvider>
  </StrictMode>
)
