---
name: ascii-chat-cli-release
description: Prepare, publish, repair, and verify ascii-chat CLI releases through immutable version tags and GitHub Releases. Use for native CLI releases, not website or standalone library releases.
---

# ascii-chat CLI release

Read [references/build-and-verify.md](references/build-and-verify.md) before building. Reinspect `.github/workflows/release.yml`, `.github/actions/install-deps/action.yml`, `CMakePresets.json`, and dependency modules before copying commands: they are the current configuration authority.

## Release contract

- Publish by pushing an annotated `vMAJOR.MINOR.PATCH` tag on a commit already pushed to `origin/master`. Confirm the requested bump against published releases and remote tags. CLI and `lib/v*` library versions are independent; do not bump the library incidentally.
- Tags are protected and immutable. Never delete, move, force-push, or reuse a failed release tag, including through manual workflow dispatch. If any code, CMake, or workflow edit is required, commit and push the fix to master, then publish the next unused patch tag. Record attempted tags and workflow URLs.
- Retry an unchanged tag only for an identified transient runner/upload problem using `gh run rerun RUN_ID --failed`, not manual dispatch. Diagnose and validate locally before consuming another tag. Stop for missing credentials, inaccessible required hosts, or an external blocker outside release scope; report outstanding checks. Do not repeatedly tag an unchanged failure.
- Every CLI release uses static third-party dependencies. Linux uses musl and must have neither ELF `NEEDED` entries nor an `INTERP` segment. Windows and macOS may link OS system libraries only; they are not completely static executables. Never disable static enforcement to get a release through.
- Windows vcpkg dependencies on GitHub commonly take about three times as long as other platforms (an operational estimate, not a timeout). Windows often determines total release duration; wait for it before claiming completion.

## Prepare and publish

1. Inspect dirty work, master/remote ancestry, published releases, remote CLI/library tags, and active workflows. Preserve unrelated changes. Capture the latest successful published release as the notes baseline and maintain an explicit failed-attempt list for this release session.
2. Inventory existing dependency installations and compatible built archives before configuring locally; a new application build directory does not mean rebuilding dependencies. Reuse the Mac's existing built dependencies. Build locally first using new application build directories and CI configurations as closely as possible. Use this Windows host, `ssh loomen@zachbook-pro` for Intel macOS, and Docker for Linux. Preserve existing checkouts, build trees, and running processes. Test packaging targets as well as the executable. Record any local dependency/configuration differences from CI.
3. Validate the skill if changed, review the scoped diff, commit only named paths, push master, confirm the remote SHA, then create and push the annotated tag. Do not create candidate tags just to test version detection; pass the candidate version to CMake/environment instead.
4. Monitor Release through all six platform/architecture jobs, publishing, and notification. The workflow creates a draft and publishes after packaging passes. Inspect other tag-triggered CLI artifact workflows that exist, including Docker. Repair failures using fresh patch tags as above.
5. Download final GitHub assets into a new directory. Inventory every platform/architecture and format against expected workflow outputs. Audit actual packaged executables, not just local outputs. Verify static linkage and run `--version` with successful exit and correct version for every runtime architecture using native hosts, CI runner evidence, or supported emulation. Check development archives contain the static library; an additional shared development library does not weaken the CLI contract. Inspect installer payloads too, or establish executable identity with an independently audited archive. Record asset names, hashes, linkage, execution host, and results.

## Finalize release notes and failed attempts

After the final candidate is published and verified, delete only GitHub Release objects belonging to failed attempts from this session. Use `gh release delete TAG --yes` without `--cleanup-tag`; retain every tag and previous successful release. Do not delete releases merely because they precede the new one.

List remaining published non-prerelease CLI releases and identify the newest and second-newest successful releases (exclude drafts, failed attempts, and `lib/v*` releases). Use GitHub's release-notes generation endpoint with explicit boundaries:

```sh
gh api --method POST repos/zfogg/ascii-chat/releases/generate-notes \
  -f tag_name="$FINAL_TAG" -f previous_tag_name="$PREVIOUS_SUCCESSFUL_TAG"
```

Use the returned `body` in the published release body. Preserve useful download instructions and source-tarball checksums; replace the provisional tag-based changelog to avoid duplicate notes. Include Linux musl/static packaging and Windows/macOS system-library exceptions. GitHub may return only a comparison link; preserve its actual generated text alongside the human explanation below.

### Improvements breakdown

Publish a section titled **`Improvements breakdown`** immediately above the GitHub-generated changelist for every release. Explain what changed for users since the previous successful published release; group related work into readable topics rather than rewriting commit subjects or listing PR titles.

- Review every commit in `PREVIOUS_SUCCESSFUL_TAG..FINAL_TAG`, including direct commits and follow-up fixes. Inspect relevant pinned source diffs and substantive commit messages. Exclude later master commits and uncommitted work.
- Review PRs and issues opened, closed, or otherwise active during the interval, including older issues closed then. Read descriptions, relevant discussions/reviews, and closure context. Verify landed work against tag ancestry and final code: a closed PR may be unmerged, an open issue may have shipped implementation, and an issue closure may merely recognize existing behavior. Do not turn proposals or review suggestions into release claims.
- Describe concrete capabilities, corrected behavior, and useful controls or examples. Cover native client-like/server-like modes, browser applications, WASM/shared C code, audio/video, networking/security, and build/SDK changes when they changed. Explain how related PRs fit together and link representative evidence. Distinguish tagged browser/website source changes from a verified live website deployment.
- Compare dependency manifests, CMake configuration, and source at both tags before calling a dependency or feature new. Distinguish new libraries from new uses, build fixes, and reorganized modules. For example, FFTW was already present in `v0.13.5`; its later issue closure did not introduce it. Include material compatibility changes, optional runtime tools, and unfinished scope without claiming an entire issue's original wish list shipped.
- Preserve the generated changelist verbatim, download guidance, checksums, and assets. When updating existing notes, replace the breakdown section instead of duplicating it. Save the complete body as UTF-8, publish with `gh release edit TAG --notes-file FILE`, and read it back to verify placement and exact saved-body equality. Account for GitHub's CRLF line endings when comparing bytes. Release-body or skill-only edits do not require a new release tag.

Finish only when final assets are published, every runtime architecture passes static checks and `--version`, failed-attempt release objects are removed while tags remain, and both the Improvements breakdown and GitHub-generated notes are stored. Report the release URL, exact commit/tag, validation evidence, and any material limitations honestly.
