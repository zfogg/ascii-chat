# Sentry Commit Message Examples

## Simple Fix

```
fix(api): Handle null response in user endpoint

The user API could return null for deleted accounts, causing a crash
in the dashboard. Add null check before accessing user properties.

Fixes SENTRY-5678
```

**Breakdown:**

- **Type**: `fix` (bug fix)
- **Scope**: `api` (which system is affected)
- **Subject**: Clear, imperative mood, under 70 chars
- **Body**: Explains the problem and solution
- **Footer**: Uses `Fixes` to auto-close the Sentry issue

## Feature with Scope

```
feat(alerts): Add Slack thread replies for alert updates

When an alert is updated or resolved, post a reply to the original
Slack thread instead of creating a new message. This keeps related
notifications grouped together.

Refs GH-1234
```

**Breakdown:**

- **Type**: `feat` (new feature)
- **Scope**: `alerts` (feature area)
- **Body**: Describes the change and why it matters
- **Footer**: `Refs` (links without auto-closing since it's a GitHub issue)

## Refactoring

```
ref: Extract common validation logic to shared module

Move duplicate validation code from three endpoints into a shared
validator class. No behavior change.
```

**Breakdown:**

- **Type**: `ref` (refactoring)
- **No scope**: Scope is optional for project-wide changes
- **Body**: Clearly states this is a refactor with no behavior change

## Performance Improvement

```
perf(rendering): Optimize ANSI sequence generation

Cache computed color codes instead of regenerating on every frame.
Reduces rendering time by 40% in benchmark tests.

Refs SENTRY-9999
```

## Breaking Change

```
feat(api)!: Remove deprecated v1 endpoints

Remove all v1 API endpoints that were deprecated in version 23.1.
Clients should migrate to v2 endpoints.

BREAKING CHANGE: v1 endpoints no longer available
Fixes SENTRY-9999
```

**Breakdown:**

- **Breaking indicator**: `!` after scope
- **BREAKING CHANGE footer**: Describes impact
- **Clear migration path**: Tells users what to do

## Revert Commit

```
revert: feat(api): Add new endpoint

This reverts commit abc123def456.

Reason: Caused performance regression in production (50% latency increase on POST requests).
```

**When to use**: Only when reverting entire commits. For partial reverts, use `fix()` instead.

## Documentation Only

```
docs: Add API authentication examples

Add runnable examples for JWT and SSH key authentication to the
API documentation. Include error handling patterns.
```

## Test Addition

```
test(crypto): Add tests for X25519 key exchange

Add 15 new test cases covering edge cases in X25519 implementation:
- Zero keys
- Invalid input lengths
- Output validation
```

## Build System Update

```
build: Update libsodium dependency to 1.0.19

Fixes security vulnerability in argon2 hashing.

Fixes SENTRY-8765
```

## Checklist Before Committing

- [ ] Type is one of: feat, fix, ref, perf, docs, test, build, ci, chore, style, meta, license
- [ ] Subject line uses imperative mood and present tense
- [ ] Subject line is under 70 characters
- [ ] All lines are under 100 characters
- [ ] Body explains **why**, not just **what**
- [ ] Issue reference uses correct pattern (Fixes SENTRY-XXX, Refs GH-XXX, etc.)
- [ ] Breaking changes include `!` indicator and `BREAKING CHANGE:` footer
- [ ] Commit is a single, stable, independently reviewable change
