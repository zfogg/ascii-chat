import React from 'react'
import ReactDOM from 'react-dom/client'
import { HelmetProvider } from 'react-helmet-async'
import { NotFoundPage } from './pages/NotFound'
import './style.css'

ReactDOM.createRoot(document.getElementById('app')!).render(
  <React.StrictMode>
    <HelmetProvider>
      <NotFoundPage />
    </HelmetProvider>
  </React.StrictMode>
)
