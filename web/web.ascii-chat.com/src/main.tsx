import React from 'react'
import ReactDOM from 'react-dom/client'
import { App } from './App'
import './style.css'
import { inject } from '@vercel/analytics'

// Initialize Vercel Analytics
inject()

ReactDOM.createRoot(document.getElementById('app')!).render(
  <React.StrictMode>
    <App />
  </React.StrictMode>
)
