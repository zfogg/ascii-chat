import { defineConfig } from 'vite-plus';

export default defineConfig({
  lint: {
    "plugins": [
      "oxc",
      "typescript",
      "unicorn",
      "react"
    ],
    "categories": {
      "correctness": "warn"
    },
    "env": {
      "builtin": true
    },
    "settings": {
      "react": {
        "version": "19.2"
      }
    },
    "ignorePatterns": [
      "**/node_modules/**",
      "**/.venv/**",
      "**/.next/**",
      "**/dist/**",
      "**/build/**",
      "**/.git/**",
      "**/.vscode/**",
      "**/coverage/**",
      "**/tests/**",
      "**/playwright.config.ts",
      "**/vite.config.ts",
      "**/vitest.config.ts"
    ],
    "rules": {
      "constructor-super": "error",
      "for-direction": "error",
      "getter-return": "error",
      "no-async-promise-executor": "error",
      "no-case-declarations": "error",
      "no-class-assign": "error",
      "no-compare-neg-zero": "error",
      "no-cond-assign": "error",
      "no-const-assign": "error",
      "no-constant-binary-expression": "error",
      "no-constant-condition": "error",
      "no-control-regex": "error",
      "no-debugger": "error",
      "no-delete-var": "error",
      "no-dupe-class-members": "error",
      "no-dupe-else-if": "error",
      "no-dupe-keys": "error",
      "no-duplicate-case": "error",
      "no-empty": "error",
      "no-empty-character-class": "error",
      "no-empty-pattern": "error",
      "no-empty-static-block": "error",
      "no-ex-assign": "error",
      "no-extra-boolean-cast": "error",
      "no-fallthrough": "error",
      "no-func-assign": "error",
      "no-global-assign": "error",
      "no-import-assign": "error",
      "no-invalid-regexp": "error",
      "no-irregular-whitespace": "error",
      "no-loss-of-precision": "error",
      "no-misleading-character-class": "error",
      "no-new-native-nonconstructor": "error",
      "no-nonoctal-decimal-escape": "error",
      "no-obj-calls": "error",
      "no-prototype-builtins": "error",
      "no-redeclare": "error",
      "no-regex-spaces": "error",
      "no-self-assign": "error",
      "no-setter-return": "error",
      "no-shadow-restricted-names": "error",
      "no-sparse-arrays": "error",
      "no-this-before-super": "error",
      "no-unassigned-vars": "error",
      "no-undef": "error",
      "no-unexpected-multiline": "error",
      "no-unreachable": "error",
      "no-unsafe-finally": "error",
      "no-unsafe-negation": "error",
      "no-unsafe-optional-chaining": "error",
      "no-unused-labels": "error",
      "no-unused-private-class-members": "error",
      "no-unused-vars": "error",
      "no-useless-backreference": "error",
      "no-useless-catch": "error",
      "no-useless-escape": "error",
      "no-with": "error",
      "preserve-caught-error": "error",
      "require-yield": "error",
      "use-isnan": "error",
      "valid-typeof": "error",
      "no-var": "error",
      "react/display-name": "error",
      "react/jsx-key": "error",
      "react/jsx-no-comment-textnodes": "error",
      "react/jsx-no-duplicate-props": "error",
      "react/jsx-no-target-blank": "error",
      "react/jsx-no-undef": "error",
      "react/no-children-prop": "error",
      "react/no-danger-with-children": "error",
      "react/no-direct-mutation-state": "error",
      "react/no-find-dom-node": "error",
      "react/no-is-mounted": "error",
      "react/no-render-return-value": "error",
      "react/no-string-refs": "error",
      "react/no-unescaped-entities": "error",
      "react/no-unknown-property": "error",
      "react/no-unsafe": "off",
      "react/react-in-jsx-scope": "error",
      "react/require-render-return": "error"
    },
    "overrides": [
      {
        "files": [
          "**/*.js"
        ],
        "globals": {
          "__COMMIT_SHA__": "readonly"
        },
        "env": {
          "es2024": true,
          "browser": true,
          "node": true
        }
      },
      {
        "files": [
          "**/*.jsx"
        ],
        "rules": {
          "react/no-unescaped-entities": "off",
          "react-hooks/rules-of-hooks": "error",
          "react-hooks/exhaustive-deps": "warn",
          "react/only-export-components": "warn"
        },
        "globals": {
          "__COMMIT_SHA__": "readonly"
        },
        "env": {
          "es2024": true,
          "browser": true,
          "node": true
        }
      },
      {
        "files": [
          "**/*.ts"
        ],
        "rules": {
          "@typescript-eslint/no-explicit-any": "error",
          "no-unused-vars": [
            "error",
            {
              "argsIgnorePattern": "^_",
              "caughtErrorsIgnorePattern": "^_",
              "varsIgnorePattern": "^_"
            }
          ]
        },
        "globals": {
          "__COMMIT_SHA__": "readonly"
        },
        "env": {
          "es2024": true,
          "browser": true,
          "node": true
        }
      },
      {
        "files": [
          "**/*.tsx"
        ],
        "rules": {
          "@typescript-eslint/no-explicit-any": "error",
          "react/no-unescaped-entities": "off",
          "react-hooks/rules-of-hooks": "error",
          "react-hooks/exhaustive-deps": "warn",
          "no-unused-vars": [
            "error",
            {
              "argsIgnorePattern": "^_",
              "caughtErrorsIgnorePattern": "^_",
              "varsIgnorePattern": "^_"
            }
          ],
          "react/only-export-components": "warn"
        },
        "globals": {
          "__COMMIT_SHA__": "readonly"
        },
        "env": {
          "es2024": true,
          "browser": true,
          "node": true
        }
      },
      {
        "files": [
          "**/OpusEncoder.ts"
        ],
        "rules": {
          "no-unused-vars": "off"
        }
      }
    ],
    "options": {
      "typeAware": true,
      "typeCheck": true
    }
  },
  fmt: {
    "printWidth": 80,
    "sortPackageJson": false,
    "ignorePatterns": []
  },
});
