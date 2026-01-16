# Homebrew Core Submission Guide for ascii-chat

This guide walks you through submitting ascii-chat to official Homebrew (homebrew-core).

## Prerequisites

- [x] Stable, well-maintained open source project
- [x] MIT license
- [x] Active development and user base
- [x] Working formula in custom tap (zfogg/ascii-chat)
- [ ] Formula tested on latest macOS
- [ ] Formula tested on Linux (via Homebrew on Linux)

## Overview

You currently have:
- Custom tap: `homebrew-ascii-chat` with pre-built bottles
- Two formulas: `ascii-chat` (runtime) and `libasciichat` (development)

For homebrew-core:
- Submit only `ascii-chat` (not libasciichat - Homebrew prefers single packages)
- Build from source tarball (not pre-built bottles initially)
- Simplified formula without GPG verification

## Step-by-Step Submission Process

### 1. Prepare the Formula

I've created a simplified formula at:
```
Formula/ascii-chat-homebrew-core.rb
```

**Key differences from your tap formula:**
- Builds from GitHub source tarball (not pre-built bottles)
- No GPG signature verification (Homebrew CI handles security)
- Simplified installation process
- Better test coverage
- Uses only `depends_on` for dependencies

### 2. Test the Formula Locally

**On macOS:**

```bash
# Test building from source
brew install --build-from-source Formula/ascii-chat-homebrew-core.rb

# Run tests
brew test ascii-chat

# Try running the binary
ascii-chat --help
ascii-chat --version
ascii-chat client --snapshot --no-webcam

# Uninstall
brew uninstall ascii-chat
```

**On Linux (Homebrew on Linux):**

If you have access to a Linux machine with Homebrew:

```bash
# Same commands as macOS
brew install --build-from-source Formula/ascii-chat-homebrew-core.rb
brew test ascii-chat
```

**Using Docker for Linux testing:**

```bash
# Use Homebrew's official Linux Docker image
docker run -it --rm \
  -v "$PWD:/workdir" \
  -w /workdir \
  homebrew/brew:latest \
  bash

# Inside container:
brew install --build-from-source Formula/ascii-chat-homebrew-core.rb
brew test ascii-chat
```

### 3. Run Homebrew's Formula Audits

```bash
# Audit the formula for style and correctness
brew audit --strict --online Formula/ascii-chat-homebrew-core.rb

# Check formula style
brew style Formula/ascii-chat-homebrew-core.rb

# Verify dependencies are available in homebrew-core
brew info libsodium opus portaudio zstd mimalloc ca-certificates gnupg
```

**Common audit issues to fix:**
- Trailing whitespace
- Line length > 80 characters
- Incorrect dependency specifications
- Missing test coverage

### 4. Fork and Clone homebrew-core

```bash
# Fork homebrew-core on GitHub first
# Then clone your fork:
cd ~/src
git clone https://github.com/YOUR_USERNAME/homebrew-core.git
cd homebrew-core

# Add upstream
git remote add upstream https://github.com/Homebrew/homebrew-core.git
git fetch upstream
```

### 5. Create a New Branch

```bash
cd homebrew-core

# Update master
git checkout master
git pull upstream master

# Create feature branch
git checkout -b ascii-chat
```

### 6. Add the Formula

```bash
# Copy your formula to homebrew-core
cp ~/src/github.com/zfogg/ascii-chat/Formula/ascii-chat-homebrew-core.rb \
   Formula/a/ascii-chat.rb

# Note: Homebrew organizes formulas alphabetically by first letter
# ascii-chat goes in Formula/a/
```

### 7. Test Again in homebrew-core

```bash
# Test building from the formula in homebrew-core
brew install --build-from-source Formula/a/ascii-chat.rb

# Run tests
brew test ascii-chat

# Audit
brew audit --strict --online Formula/a/ascii-chat.rb
brew style Formula/a/ascii-chat.rb
```

### 8. Commit and Push

```bash
# Stage changes
git add Formula/a/ascii-chat.rb

# Commit with proper format (Homebrew is VERY strict about commit messages)
git commit -m "ascii-chat 0.6.12 (new formula)

Real-time terminal-based video chat with ASCII art conversion.

Closes Homebrew/homebrew-core#XXXXX"

# Push to your fork
git push origin ascii-chat
```

**Commit message format:**
- First line: `formula-name VERSION (new formula)`
- Blank line
- Brief description (1-2 sentences)
- Blank line
- `Closes Homebrew/homebrew-core#XXXXX` (if there's a related issue)

### 9. Create Pull Request

1. Go to https://github.com/Homebrew/homebrew-core
2. Click "New Pull Request"
3. Select your fork and `ascii-chat` branch
4. Fill out the PR template (Homebrew has a specific template)

**PR Title Format:**
```
ascii-chat 0.6.12 (new formula)
```

**PR Description Template:**
```markdown
## Description

Real-time terminal-based video chat with ASCII art conversion.

ascii-chat is a client-server application for video chat in the terminal.
It captures webcam video, converts it to ASCII art in real-time, and
transmits it over TCP/IP. Features include:

- Multi-client support with grid layout
- Audio streaming and mixing
- End-to-end encryption with libsodium
- SSH key authentication
- Cross-platform (macOS, Linux, Windows)

## Checklist

- [x] I have personally tested the formula on macOS
- [ ] I have personally tested the formula on Linux (if you haven't, mention it)
- [x] The formula builds without errors
- [x] All tests pass
- [x] `brew audit --strict --online` passes
- [x] `brew style` passes
- [x] I am the author/maintainer of this software

## Additional Notes

This project has been actively maintained since 2013 with continuous
development. The project has comprehensive test coverage (90+ tests)
and full API documentation.

Source: https://github.com/zfogg/ascii-chat
Documentation: https://zfogg.github.io/ascii-chat/
```

### 10. Respond to CI and Reviewers

Homebrew has automated CI that will:
- Build your formula on macOS (multiple versions)
- Build on Linux
- Run all tests
- Check for style violations

**Common CI failures:**
- Missing dependencies on Linux
- Build failures due to system differences
- Test failures
- Style violations

**Be prepared to:**
- Respond to reviewer feedback promptly
- Make requested changes
- Update your PR with fixes
- Rebase if requested

### 11. After Approval

Once approved and merged:
1. Your formula will be available in homebrew-core
2. Users can install with: `brew install ascii-chat`
3. Homebrew will build bottles automatically for popular platforms
4. You'll be notified of the merge

## Homebrew Submission Best Practices

### Do's:
✅ Test thoroughly on both macOS and Linux before submitting
✅ Run `brew audit --strict --online` and fix all issues
✅ Use minimal, focused descriptions
✅ Respond to feedback quickly and professionally
✅ Follow Homebrew's commit message format exactly
✅ Include meaningful tests
✅ Keep the formula simple and maintainable

### Don'ts:
❌ Submit untested formulas
❌ Include pre-built binaries (bottles come later, after merge)
❌ Add GPG verification (Homebrew doesn't support it)
❌ Submit formulas for unstable/alpha software
❌ Argue with reviewers (they're volunteers helping you)
❌ Submit multiple formulas at once (libasciichat should be separate, later)
❌ Use complex workarounds (keep it simple)

## Maintaining the Formula

After acceptance:
- Update the formula for new releases by submitting PRs
- Monitor issues related to your formula
- Help users with installation problems
- Keep dependencies up to date

## Alternative: Keep Your Tap

If homebrew-core submission is too complex or restrictive, consider:
- Keeping your custom tap with pre-built bottles
- Providing better user experience in your tap
- Submitting to homebrew-core later when ready

Your custom tap is already excellent with:
- Pre-built bottles (faster installation)
- GPG signature verification (extra security)
- Separate runtime and development packages
- Homebrew service integration

## Resources

- [Homebrew Formula Cookbook](https://docs.brew.sh/Formula-Cookbook)
- [How to Open a Homebrew Pull Request](https://docs.brew.sh/How-To-Open-a-Homebrew-Pull-Request)
- [Acceptable Formulae](https://docs.brew.sh/Acceptable-Formulae)
- [Homebrew Maintainer Guide](https://docs.brew.sh/Maintainer-Guidelines)
- [Formula Style Guide](https://docs.brew.sh/Formula-Cookbook#formula-style)

## Questions?

If you need help during the submission process:
- Ask in #general on Homebrew's Discussions: https://github.com/orgs/Homebrew/discussions
- Review existing PRs to see how others handled similar issues
- Check Homebrew's documentation thoroughly

## Timeline Expectations

- Initial review: 1-7 days
- Total time to merge: 1-4 weeks (depending on feedback cycles)
- CI builds take 10-30 minutes per run
- Expect 1-3 rounds of feedback before approval

Good luck with your submission! 🍺
