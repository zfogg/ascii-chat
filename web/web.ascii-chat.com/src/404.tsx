import React from 'react'
import ReactDOM from 'react-dom/client'
import { NotFoundPage } from './pages/NotFound'
import './style.css'

ReactDOM.createRoot(document.getElementById('app')!).render(
  <React.StrictMode>
    <NotFoundPage />
  </React.StrictMode>
)
