# Homebrew Core Submission Checklist

Use this checklist to track your progress submitting ascii-chat to homebrew-core.

## Pre-Submission Testing

- [ ] Formula audits cleanly: `brew audit --strict --online Formula/ascii-chat-homebrew-core.rb`
- [ ] Formula style is correct: `brew style Formula/ascii-chat-homebrew-core.rb`
- [ ] Formula builds on macOS (arm64): `brew install --build-from-source Formula/ascii-chat-homebrew-core.rb`
- [ ] Formula builds on macOS (x86_64): Test on Intel Mac if available
- [ ] Formula builds on Linux: Test via Docker or Linux machine
- [ ] All tests pass: `brew test ascii-chat`
- [ ] Binary works: `ascii-chat --help`, `ascii-chat --version`
- [ ] Snapshot mode works: `ascii-chat client --snapshot --no-webcam`
- [ ] All dependencies are available in homebrew-core
- [ ] No warnings or errors in build logs

**Quick test script:**
```bash
./scripts/test_homebrew_formula.sh
```

## Formula Preparation

- [x] Create homebrew-core compatible formula at `Formula/ascii-chat-homebrew-core.rb`
- [x] Use source tarball URL: `https://github.com/zfogg/ascii-chat/archive/refs/tags/v0.6.12.tar.gz`
- [x] Calculate SHA256: `3498b09d9e8b645fe741e00ecd854afd2b3f273b70cfb714f5eea4259f4379a9`
- [x] Remove GPG verification (not supported in homebrew-core)
- [x] Simplify build process
- [x] Add comprehensive tests
- [x] Document with clear description

## Repository Setup

- [ ] Fork homebrew-core: https://github.com/Homebrew/homebrew-core/fork
- [ ] Clone your fork:
  ```bash
  git clone https://github.com/YOUR_USERNAME/homebrew-core.git
  cd homebrew-core
  ```
- [ ] Add upstream remote:
  ```bash
  git remote add upstream https://github.com/Homebrew/homebrew-core.git
  git fetch upstream
  ```
- [ ] Update master branch:
  ```bash
  git checkout master
  git pull upstream master
  ```

## Branch Creation

- [ ] Create feature branch:
  ```bash
  git checkout -b ascii-chat
  ```
- [ ] Copy formula to correct location:
  ```bash
  cp ~/src/github.com/zfogg/ascii-chat/Formula/ascii-chat-homebrew-core.rb \
     Formula/a/ascii-chat.rb
  ```

## Testing in homebrew-core

- [ ] Install from homebrew-core formula:
  ```bash
  brew install --build-from-source Formula/a/ascii-chat.rb
  ```
- [ ] Run tests again: `brew test ascii-chat`
- [ ] Audit again: `brew audit --strict --online Formula/a/ascii-chat.rb`
- [ ] Style check again: `brew style Formula/a/ascii-chat.rb`
- [ ] Verify binary works: `ascii-chat --help`

## Commit and Push

- [ ] Stage formula:
  ```bash
  git add Formula/a/ascii-chat.rb
  ```
- [ ] Commit with proper format:
  ```bash
  git commit -m "ascii-chat 0.6.12 (new formula)

  Real-time terminal-based video chat with ASCII art conversion."
  ```
- [ ] Push to your fork:
  ```bash
  git push origin ascii-chat
  ```

## Pull Request

- [ ] Create PR: https://github.com/Homebrew/homebrew-core/compare
- [ ] Select your fork and `ascii-chat` branch
- [ ] Use title: `ascii-chat 0.6.12 (new formula)`
- [ ] Fill out PR description template (see HOMEBREW_SUBMISSION_GUIDE.md)
- [ ] Mark relevant checkboxes in PR template:
  - [x] Tested on macOS
  - [ ] Tested on Linux (if applicable)
  - [x] Formula builds without errors
  - [x] All tests pass
  - [x] Audit passes
  - [x] Style passes
  - [x] I am the author/maintainer

## Post-Submission

- [ ] Monitor CI builds (macOS, Linux)
- [ ] Respond to CI failures within 24 hours
- [ ] Respond to reviewer feedback promptly
- [ ] Make requested changes
- [ ] Update PR as needed
- [ ] Thank reviewers

## After Merge

- [ ] Celebrate! 🎉
- [ ] Update your tap's README to mention homebrew-core availability
- [ ] Monitor issues related to your formula
- [ ] Help users with installation problems
- [ ] Submit updates for new releases

## Common Issues and Solutions

### Build Failure on macOS
- Check Xcode Command Line Tools are installed
- Verify all dependencies are declared
- Check for macOS-specific compilation issues

### Build Failure on Linux
- Verify Linux-specific dependencies (alsa-lib, etc.)
- Check for platform-specific code issues
- Test in Homebrew's Linux Docker image

### Test Failures
- Ensure tests don't require interactive input
- Don't rely on webcam availability
- Use `--no-webcam` and `--snapshot` modes for testing
- Check for network-dependent tests (CI has no internet)

### Audit Failures
- Fix style issues (whitespace, line length)
- Use proper dependency syntax
- Check formula structure matches Homebrew standards

### Dependency Not Found
- Verify dependency name matches homebrew-core
- Check if dependency needs to be added to homebrew-core first
- Consider alternative dependencies available in homebrew-core

## Resources

- [Homebrew Formula Cookbook](https://docs.brew.sh/Formula-Cookbook)
- [How to Open a Homebrew Pull Request](https://docs.brew.sh/How-To-Open-a-Homebrew-Pull-Request)
- [Acceptable Formulae](https://docs.brew.sh/Acceptable-Formulae)
- [Homebrew Maintainer Guide](https://docs.brew.sh/Maintainer-Guidelines)

## Notes

- Be patient - reviews can take 1-7 days
- Be professional and courteous with reviewers
- Expect 1-3 rounds of feedback
- CI builds take 10-30 minutes
- Total time to merge: 1-4 weeks typically

---

**Current Status**: Ready for pre-submission testing
**Next Action**: Run `./scripts/test_homebrew_formula.sh`
