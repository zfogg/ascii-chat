import { defineConfig } from "eslint/config";
import globals from "globals";
import js from "@eslint/js";
import reactPlugin from "eslint-plugin-react";
import reactHooks from "eslint-plugin-react-hooks";
import reactRefresh from "eslint-plugin-react-refresh";
import tseslint from "typescript-eslint";

function languageOptions(jsx = false, tseslint = false) {
  const opts = {
    ecmaVersion: 2024,
    sourceType: "module",
    globals: {
      ...globals.browser,
      ...globals.node,
      __COMMIT_SHA__: "readonly",
    },
    parserOptions: {
      ecmaVersion: 2024,
      sourceType: "module",
      ecmaFeatures: {
        jsx: true,
      },
      projectService: true,
    },
  };
  if (jsx) {
    opts.parserOptions.ecmaFeatures = {
      ...opts.parserOptions.ecmaFeatures,
      jsx: true,
    };
  }
  if (tseslint) {
    opts.parser = tseslint.parser;
  }
  return opts;
}

export default defineConfig([
  js.configs.recommended,

  {
    rules: {
      "no-var": "error",
    },
  },

  reactPlugin.configs.flat.recommended,
  {
    rules: {
      "react/react-in-jsx-scope": "off",
    },
    settings: {
      react: {
        version: "19.2",
      },
    },
    languageOptions: {
      ...reactPlugin.configs.flat.recommended.languageOptions,
    },
  },

  {
    ignores: [
      "**/node_modules/**",
      "**/.venv/**",
      "**/.next/**",
      "**/dist/**",
      "**/build/**",
      "**/.git/**",
      "**/.vscode/**",
      "**/coverage/**",
    ],
  },

  {
    files: ["**/*.js"],
    languageOptions: languageOptions(false, false),
  },

  {
    files: ["**/*.jsx"],
    languageOptions: languageOptions(true, false),
    plugins: {
      "react-hooks": reactHooks,
      "react-refresh": reactRefresh,
    },
    rules: {
      "no-var": "error",
      // React TSX specific configuration                                      │
      ...reactHooks.configs.recommended.rules,
      "react-refresh/only-export-components": "warn",
    },
  },

  {
    files: ["**/*.ts"],
    languageOptions: languageOptions(false, true),
    plugins: {
      "@typescript-eslint": tseslint.plugin,
    },
    rules: {
      "@typescript-eslint/no-explicit-any": "error",
      "@typescript-eslint/no-unused-vars": [
        "error",
        {
          argsIgnorePattern: "^_",
          caughtErrorsIgnorePattern: "^_",
        },
      ],
    },
  },

  {
    files: ["**/*.tsx"],
    languageOptions: languageOptions(true, true),
    plugins: {
      "@typescript-eslint": tseslint.plugin,
      "react-hooks": reactHooks,
      "react-refresh": reactRefresh,
    },
    rules: {
      "@typescript-eslint/no-explicit-any": "error",
      "@typescript-eslint/no-unused-vars": [
        "error",
        {
          argsIgnorePattern: "^_",
          caughtErrorsIgnorePattern: "^_",
        },
      ],
      // React TSX specific configuration                                      │
      ...reactHooks.configs.recommended.rules,
      "react-refresh/only-export-components": "warn",
    },
  },
]);
