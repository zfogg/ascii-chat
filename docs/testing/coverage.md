# Native coverage

Coverage measures the C/C++ product sources compiled by the native test builds.
It does not measure every platform or the browser build merely because their
files exist in `src/`. Tests and vendored dependencies do not contribute to the
product score. Keep untested product code in the report; do not exclude a module
to satisfy a target.

## Reading the numbers

The Codecov headline is fully covered lines divided by hits + misses + partials.
An executed line with untested conditional branches can count as partial, so this
score differs from gcovr's executed-line percentage. `partials_as_hits` stays
disabled. Original Cobertura branch information is preserved in uploads.

Coverage builds explicitly disable sanitizers. Clang's gcov instrumentation
otherwise counts sanitizer-generated guards as branches on plain assignments
(for example `settings->width = values->width`), even though these are not source
decisions. The separate Linux Debug sanitizer unit job retains ASan/UBSan checks
with coverage disabled; normal Debug builds still enable sanitizers by default.
Keep a matched measurement when distinguishing this reporting correction from
newly exercised behavior.

The complete-upload summary records executed-line coverage for production code
and separately for `src/` (`application`). It unions line identities across the
seven reports instead of averaging shard percentages or summing duplicate lines.
Use Codecov's processed report for the branch-aware score.

Always compare the exact commit and report inventory. On November 2, 2026,
Codecov's repository API still reported 39.62% from `237dce570` (October 22),
while `9bfcf8c74`, the then-current master, was pending without totals. That old
report contained 371 files; `src/` had 3,343 hits, 6,239 partials and 5,187 misses
(14,769 lines). Its 22.64% fully covered score and 64.88% executed-line score
describe different metrics. Neither establishes current-master coverage.

## Policy in codecov.yml

| Setting                  | Policy and reason                                                                           |
| ------------------------ | ------------------------------------------------------------------------------------------- |
| Default branch           | `master`, matching the repository                                                           |
| Upload completion        | One upload after all seven native CI reports pass inventory validation; no carryforward     |
| Report age               | 24 hours, allowing a slow full matrix without accepting arbitrarily old reports             |
| Notifications            | Publish after that upload without waiting for unrelated statuses; report processing errors  |
| Overall project          | Compare with the base, allowing at most one percentage point of regression                  |
| Application project      | At least 50% for `src/`, with zero threshold allowance                                      |
| Patch                    | At least 60%; security patches at least 70%, with zero allowance                            |
| Branch filters           | None: feature branches need checks and PR comments too                                      |
| Components               | Current module paths, baseline-relative informational project checks                        |
| Flags                    | Only `coverage` is a native coverage upload; per-job test analytics flags are independent   |
| Comments                 | One updated comment including components, even if base coverage is unavailable              |
| Ignored files            | Build output, dependencies, test scaffolding, generated version and musl compatibility code |
| Partials and annotations | Retain branch partials and source annotations                                               |

The previous configuration duplicated module gates as components, omitted newer
session/MCP/media/UI modules, had obsolete branch and upload flag lists, and
included redundant paths such as `lib/audio/audio.*` beneath `lib/audio/`.
The CI-success setting belongs directly under `codecov`, not under `notify`.
The separate changes status is disabled; the project, application and patch
checks provide the gates, and components diagnose module regressions.

Validate edits against Codecov's live schema:

```sh
curl --fail --data-binary @codecov.yml https://api.codecov.io/validate
python .github/scripts/test_generate_coverage.py
python .github/scripts/test_prepare_coverage_upload.py
python .github/scripts/test_criterion_to_junit.py
```

References: [YAML reference](https://docs.codecov.com/docs/codecovyml-reference),
[status checks](https://docs.codecov.com/docs/commit-status),
[components](https://docs.codecov.com/docs/components).

## Reproducing a local measurement

Use Clang, Ninja and a compatible glibc/Linux or macOS build tree. From Windows,
use the repository's Linux test container. Share the main worktree's `.deps-cache`
source/download cache, while keeping platform-specific compiled dependencies and
instrumented objects in a separate build tree.

```sh
GITHUB_CMAKE_BINARY_DIR=build_coverage cmake --preset github -B build_coverage -DBUILD_TESTS=ON \
  -DASCIICHAT_FORCE_CRITERION_TESTS=ON -DASCIICHAT_ENABLE_COVERAGE=ON \
  -DASCIICHAT_ENABLE_SANITIZERS=OFF -DUSE_MUSL=OFF
cmake --build build_coverage --target ascii-chat tests
# Remove only stale counters in this measurement's build tree before running.
find build_coverage -name '*.gcda' -delete
ctest --test-dir build_coverage -L '^(unit|integration|performance)$' \
  --no-tests=error --output-on-failure --parallel 2 \
  --output-junit Testing/coverage-tests.xml
python .github/scripts/generate-coverage.py \
  --build build_coverage --output build_coverage/coverage.xml
```

Record test failures and prerequisite skips alongside the report. Coverage
generation or an accepted upload does not imply tests passed. A local Linux
measurement is not evidence of native macOS/Windows execution or a substitute
for the seven-shard CI completeness gate. When publishing a local measurement,
identify its platform, build configuration, test scope and exact source commit.
