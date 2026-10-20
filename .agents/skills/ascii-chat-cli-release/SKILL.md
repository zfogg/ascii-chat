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

Use the returned `body` in the published release body. Preserve useful download instructions and source-tarball checksums; replace the provisional tag-based changelog to avoid duplicate notes. Include Linux musl/static packaging and Windows/macOS system-library exceptions. GitHub may return only a comparison link; preserve its actual generated text and supplement only if useful or requested. Save the complete body to a UTF-8 file, use `gh release edit TAG --notes-file FILE`, and read the stored body back for an exact match.

Finish only when final assets are published, every runtime architecture passes static checks and `--version`, failed-attempt release objects are removed while tags remain, and GitHub-generated notes are stored. Report the release URL, exact commit/tag, validation evidence, and any material limitations honestly.
