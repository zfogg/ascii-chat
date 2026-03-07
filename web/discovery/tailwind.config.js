import sharedConfig from "@ascii-chat/shared/tailwind.config.js";

/** @type {import('tailwindcss').Config} */
export default {
  ...sharedConfig,
  content: [
    "./index.html",
    "./src/**/*.{js,ts,jsx,tsx}",
    "../packages/shared/src/**/*.{js,ts,jsx,tsx}",
  ],
};
