/** @type {import('tailwindcss').Config} */
export default {
  content: [
    "./src/**/*.{js,ts,jsx,tsx}",
  ],
  theme: {
    extend: {
      colors: {
        terminal: {
          bg: '#0c0c0c',
          fg: '#cccccc',
          // Named colors
          black: '#0c0c0c',
          red: '#c50f1f',
          green: '#13a10e',
          yellow: '#c19c00',
          blue: '#0037da',
          magenta: '#881798',
          cyan: '#3a96dd',
          white: '#cccccc',
          brightBlack: '#767676',
          brightRed: '#e74856',
          brightGreen: '#16c60c',
          brightYellow: '#f9f1a5',
          brightBlue: '#3b78ff',
          brightMagenta: '#b4009e',
          brightCyan: '#61d6d6',
          brightWhite: '#f2f2f2',
          // Numbered aliases (ANSI 0-15)
          0: '#0c0c0c',    // black
          1: '#c50f1f',    // red
          2: '#13a10e',    // green
          3: '#c19c00',    // yellow
          4: '#0037da',    // blue
          5: '#881798',    // magenta
          6: '#3a96dd',    // cyan
          7: '#cccccc',    // white
          8: '#767676',    // bright black
          9: '#e74856',    // bright red
          10: '#16c60c',   // bright green
          11: '#f9f1a5',   // bright yellow
          12: '#3b78ff',   // bright blue
          13: '#b4009e',   // bright magenta
          14: '#61d6d6',   // bright cyan
          15: '#f2f2f2',   // bright white
        },
      },
      fontFamily: {
        mono: ['JetBrains Mono', 'Cascadia Code', 'Fira Code', 'monospace'],
      },
    },
  },
  plugins: [],
}
