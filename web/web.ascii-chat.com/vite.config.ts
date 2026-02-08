import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import path from 'path'
import sitemap from 'vite-plugin-sitemap'
import { execSync } from 'child_process'

const getCommitSha = () => {
  if (process.env.VERCEL_GIT_COMMIT_SHA) {
    return process.env.VERCEL_GIT_COMMIT_SHA.substring(0, 8)
  }
  try {
    return execSync('git rev-parse HEAD').toString().trim().substring(0, 8)
  } catch {
    return 'unknown'
  }
}

export default defineConfig({
  define: {
    __COMMIT_SHA__: JSON.stringify(getCommitSha()),
  },
  plugins: [
    react(),
    sitemap({
      hostname: 'https://web.ascii-chat.com',
      routes: [
        '/',
        '/mirror',
        '/client',
        '/discovery',
      ],
      changefreq: 'daily',
      priority: 1.0,
      lastmod: new Date(),
    }),
  ],
  resolve: {
    alias: {
      '@': path.resolve(__dirname, './src'),
      '@ascii-chat/shared': path.resolve(__dirname, '../packages/shared/src'),
    },
    extensions: ['.ts', '.tsx', '.js', '.jsx'],
    dedupe: ['react', 'react-dom'],
  },
  server: {
    port: 3000,
    headers: {
      'Cross-Origin-Embedder-Policy': 'require-corp',
      'Cross-Origin-Opener-Policy': 'same-origin',
    },
  },
  build: {
    target: 'esnext',
    minify: 'terser',
    rollupOptions: {
      input: {
        main: './index.html',
        404: './404.html',
      },
      output: {
        manualChunks: {
          'xterm': ['xterm', '@xterm/addon-fit'],
        },
      },
    },
  },
  worker: {
    format: 'es',
  },
  optimizeDeps: {
    exclude: ['@ffmpeg/ffmpeg', '@ffmpeg/util'],
  },
})
