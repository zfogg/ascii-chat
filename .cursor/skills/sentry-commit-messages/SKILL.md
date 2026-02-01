---
name: sentry-commit-messages
description: Create commit messages following Sentry conventions with conventional commits and issue references. Use when committing code changes, writing commit messages, or formatting git history with Fixes/Refs patterns.
---

# Sentry Commit Messages

## Format

```
<type>(<scope>): <subject>

<body>

<footer>
```

**Requirements:**

- Header is required; scope is optional
- All lines must stay under 100 characters
- Subject line max 70 characters

## Commit Types

| Type      | Purpose                           |
| --------- | --------------------------------- |
| `feat`    | New feature                       |
| `fix`     | Bug fix                           |
| `ref`     | Refactoring (no behavior change)  |
| `perf`    | Performance improvement           |
| `docs`    | Documentation only                |
| `test`    | Test additions or corrections     |
| `build`   | Build system or dependencies      |
| `ci`      | CI configuration                  |
| `chore`   | Maintenance tasks                 |
| `style`   | Code formatting (no logic change) |
| `meta`    | Repository metadata               |
| `license` | License changes                   |

## Subject Line Checklist

When writing the subject line:

- [ ] Use imperative, present tense ("Add feature" not "Added feature")
- [ ] Capitalize the first letter
- [ ] No period at the end
- [ ] Maximum 70 characters

## Body Guidelines

Explain **what** and **why**, not how:

- Use imperative mood and present tense
- Include motivation for the change
- Contrast with previous behavior when relevant
- Wrap at 100 characters

## Footer: Issue References

Reference issues using these patterns:

```
Fixes GH-1234          # GitHub issue
Fixes #1234            # Shorthand GitHub
Fixes SENTRY-1234      # Sentry issue
Refs LINEAR-ABC-123    # Linear issue (no auto-close)
```

- `Fixes` closes the issue when merged
- `Refs` links without closing

## Breaking Changes

For breaking changes, add `!` after type/scope and include a footer:

```
feat(api)!: Remove deprecated v1 endpoints

BREAKING CHANGE: v1 endpoints no longer available
```

## Revert Format

```
revert: feat(api): Add new endpoint

This reverts commit abc123def456.

Reason: Caused performance regression in production.
```

## Examples

See [examples.md](examples.md) for detailed commit examples across different scenarios.
