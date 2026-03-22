/** @type {import('tailwindcss').Config} */
export default {
  content: ["./src/**/*.{js,ts,jsx,tsx}"],
  theme: {
    extend: {
      colors: {
        terminal: {
          bg: "#0c0c0c",
          fg: "#cccccc",
          // Named colors
          black: "#0c0c0c",
          red: "#c50f1f",
          green: "#13a10e",
          yellow: "#c19c00",
          blue: "#0037da",
          magenta: "#881798",
          cyan: "#3a96dd",
          white: "#cccccc",
          brightBlack: "#767676",
          brightRed: "#e74856",
          brightGreen: "#16c60c",
          brightYellow: "#f9f1a5",
          brightBlue: "#3b78ff",
          brightMagenta: "#b4009e",
          brightCyan: "#61d6d6",
          brightWhite: "#f2f2f2",
          // Numbered aliases (ANSI 0-15)
          0: "#0c0c0c", // black
          1: "#c50f1f", // red
          2: "#13a10e", // green
          3: "#c19c00", // yellow
          4: "#0037da", // blue
          5: "#881798", // magenta
          6: "#3a96dd", // cyan
          7: "#cccccc", // white
          8: "#767676", // bright black
          9: "#e74856", // bright red
          10: "#16c60c", // bright green
          11: "#f9f1a5", // bright yellow
          12: "#3b78ff", // bright blue
          13: "#b4009e", // bright magenta
          14: "#61d6d6", // bright cyan
          15: "#f2f2f2", // bright white
        },
      },
      fontFamily: {
        mono: ["Hack", "monospace"],
        hack: ["Hack", "monospace"],
        aleo: ["Aleo", "serif"],
      },
    },
  },
  plugins: [
    function ({ addUtilities, theme: _theme }) {
      const scrollbarColors = {
        ".scrollbar-primary": {
          "--scrollbar-thumb": "rgb(34, 211, 238)",
          "--scrollbar-thumb-hover": "rgb(6, 182, 212)",
          "--scrollbar-thumb-active": "rgb(165, 243, 252)",
        },
        ".scrollbar-accent": {
          "--scrollbar-thumb": "rgb(168, 85, 247)",
          "--scrollbar-thumb-hover": "rgb(147, 51, 234)",
          "--scrollbar-thumb-active": "rgb(196, 181, 253)",
        },
        ".scrollbar-success": {
          "--scrollbar-thumb": "rgb(34, 197, 94)",
          "--scrollbar-thumb-hover": "rgb(22, 163, 74)",
          "--scrollbar-thumb-active": "rgb(134, 239, 172)",
        },
        ".scrollbar-warning": {
          "--scrollbar-thumb": "rgb(234, 179, 8)",
          "--scrollbar-thumb-hover": "rgb(202, 138, 4)",
          "--scrollbar-thumb-active": "rgb(253, 224, 71)",
        },
        ".scrollbar-error": {
          "--scrollbar-thumb": "rgb(239, 68, 68)",
          "--scrollbar-thumb-hover": "rgb(220, 38, 38)",
          "--scrollbar-thumb-active": "rgb(252, 165, 165)",
        },
        ".scrollbar-cyan": {
          "--scrollbar-thumb": "rgb(34, 211, 238)",
          "--scrollbar-thumb-hover": "rgb(6, 182, 212)",
          "--scrollbar-thumb-active": "rgb(165, 243, 252)",
        },
        ".scrollbar-purple": {
          "--scrollbar-thumb": "rgb(168, 85, 247)",
          "--scrollbar-thumb-hover": "rgb(147, 51, 234)",
          "--scrollbar-thumb-active": "rgb(196, 181, 253)",
        },
        ".scrollbar-teal": {
          "--scrollbar-thumb": "rgb(20, 184, 166)",
          "--scrollbar-thumb-hover": "rgb(13, 148, 136)",
          "--scrollbar-thumb-active": "rgb(45, 212, 191)",
        },
        ".scrollbar-pink": {
          "--scrollbar-thumb": "rgb(236, 72, 153)",
          "--scrollbar-thumb-hover": "rgb(219, 39, 119)",
          "--scrollbar-thumb-active": "rgb(249, 168, 212)",
        },
        ".scrollbar-green": {
          "--scrollbar-thumb": "rgb(34, 197, 94)",
          "--scrollbar-thumb-hover": "rgb(22, 163, 74)",
          "--scrollbar-thumb-active": "rgb(134, 239, 172)",
        },
        ".scrollbar-yellow": {
          "--scrollbar-thumb": "rgb(234, 179, 8)",
          "--scrollbar-thumb-hover": "rgb(202, 138, 4)",
          "--scrollbar-thumb-active": "rgb(253, 224, 71)",
        },
        ".scrollbar-red": {
          "--scrollbar-thumb": "rgb(239, 68, 68)",
          "--scrollbar-thumb-hover": "rgb(220, 38, 38)",
          "--scrollbar-thumb-active": "rgb(252, 165, 165)",
        },
      };
      addUtilities(scrollbarColors);
    },
  ],
};
