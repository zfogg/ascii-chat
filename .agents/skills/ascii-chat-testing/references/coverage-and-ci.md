# Coverage, JUnit and Codecov

Authority: `.github/workflows/test.yml`, `.github/actions/{generate-coverage,publish-test-results}/action.yml`, `.github/scripts/{generate-coverage,criterion-to-junit,prepare-coverage-upload}.py`, and `codecov.yml`. Recheck these before changing reporting; matrix sizes and pins below describe the inspected setup.

## Generated files

| Artifact | Producer | Meaning / consumer |
| --- | --- | --- |
| `<build>/Testing/criterion-xml/*.xml` | Criterion `--xml` supplied by CMake | Individual cases; GitHub checks and Codecov test analytics |
| `<build>/Testing/ctest-results.xml` | CTest `--output-junit Testing/ctest-results.xml` | CTest-level executable/harness results; shard-completion evidence |
| `*.gcno` / `*.gcda` in build tree | Clang instrumentation / execution | Compile metadata / execution counters |
| `<build>/coverage.xml` | `generate-coverage.py` with gcovr and matching `llvm-cov gcov` | Cobertura line/branch data for Codecov coverage |
| `coverage-reports/summary.json` | `prepare-coverage-upload.py` | Validated shard inventory and union of executed lines |

The requested “junity.xml” means JUnit XML; this pipeline has no canonical file named `junity.xml`. Test-result XML and Cobertura coverage XML serve different uploads and cannot substitute for one another.

`criterion-to-junit.py INPUT_DIR [OUTPUT_DIR]` wraps a lone testsuite root, adds testcase `classname`, and removes Criterion-specific testcase `assertions`/`status`. CI converts **in place**, preserving filenames, after GitHub checks consume Criterion XML. The converter exits successfully on an empty directory; callers must verify nonzero case counts.

## Local generation

Use a separate compatible build tree when changing instrumentation, libc or optimization. Neither the default nor coverage preset guarantees `BUILD_TESTS=ON`. Example on native macOS/glibc Linux:

```sh
cmake --preset default -B build_coverage \
  -DBUILD_TESTS=ON -DASCIICHAT_FORCE_CRITERION_TESTS=ON \
  -DASCIICHAT_ENABLE_COVERAGE=ON -DUSE_MUSL=OFF
cmake --build build_coverage --target ascii-chat tests
ctest --test-dir build_coverage -L '^unit$' --no-tests=error \
  --output-junit Testing/ctest-results.xml --output-on-failure --parallel 1
python -m pip install gcovr==8.6
python .github/scripts/generate-coverage.py --build build_coverage --output build_coverage/coverage.xml
python .github/scripts/criterion-to-junit.py build_coverage/Testing/criterion-xml build_coverage/Testing/junit
```

Check each exit before continuing; PowerShell native commands do not automatically stop on failure. For Docker enable coverage explicitly with the docker preset and use `build_docker` paths inside the container. Compose named-volume reports must be copied out or uploaded from the mounted environment; they are not automatically host files.

For a new measurement in a reused tree, remove only that tree's stale `.gcda` counters and old reports, then rerun the intended suites. Retain `.gcno` and compiled dependencies. Do not combine counters from different revisions or substitute old results for a missing shard. Coverage generation success does not imply tests passed; inspect both result XML and test exits.

The generator reads `CMakeCache.txt`, selects `llvm-cov` matching the configured Clang major version, requires paired `.gcda/.gcno`, and runs gcovr across the selected build tree. It filters `lib|src|include|tests`, excludes dependency directories, and rejects zero-execution reports and invalid/non-relative source paths. Do not use GCC gcov, fabricate coverage XML, tolerate corrupt counters, or upload an arbitrary individual `.gcov` file. This is gcov-style coverage, not a `.profraw`/`llvm-profdata` pipeline.

## CI generation and publication

The current native workflow requires **seven coverage shards**:

- Unit: Linux Debug, Linux Release, macOS Release.
- Integration: Linux and macOS.
- Performance: Linux and macOS, using optimized Release plus instrumentation.

CI's `github` preset enables Clang/non-musl libraries, forced Criterion and coverage. Category CTest commands use `--no-tests=error`, JUnit output, and pipeline failure propagation for captured logs. Generation/publishing/artifact steps use `always()` to preserve failed-run diagnostics. Integration also invokes selected executable scenarios outside the main CTest command; inspect the workflow for scope. Contract-only labels are not automatically selected by unit/integration runs.

Each shard publishes Criterion XML to GitHub checks, converts to JUnit, and uploads explicit report filenames with `codecov/test-results-action@v1` and category/OS/build flags. The action installs pinned `codecov-cli==11.3.1`, passes its binary, and disables report search. Test analytics upload errors are nonfatal; CTest failures still matter. Optional per-shard coverage uploading is **disabled** (`upload-coverage: false`) in the native jobs.

The final `coverage` job downloads the seven artifact directories. `prepare-coverage-upload.py coverage-reports` requires exactly one valid coverage report, one nonempty CTest report and nonzero Criterion cases per expected artifact. It refuses partial or zero-test uploads. This gate establishes presence/execution, not that every case passed; inspect upstream results separately. Its summary unions covered line identities and reports production (`lib/src/include`) separately from all sources (including tests). Original XML retains branch data; Codecov's score can differ from executed-line percentages because of partial branches.

Only then does `codecov/codecov-action@v5` send the explicit seven coverage files in one upload: `disable_search: true`, `plugins: noop`, flag `coverage`, name `complete-native-suite`, `CODECOV_TOKEN`, and `fail_ci_if_error: true`. The `complete-coverage` artifact retains inputs/summary for seven days. Do not enable individual shard coverage uploads to bypass the completeness gate.

For an authorized commit/push, let the existing Test workflow generate and upload reports, then inspect coverage-job results/artifacts and the Codecov result for that SHA. Documentation validation alone does not require workflow dispatch or a manual upload. For explicitly requested manual publication, reproduce the workflow's prepared seven-file input and upload settings; a focused local report is diagnostic, not full-suite coverage. Never print tokens.

`codecov.yml` defines default branch, non-carryforward flags, components/paths, project/patch checks and notification after one complete upload (`after_n_builds: 1`). Coverage includes tests as well as production; distinguish them when discussing percentages. Read actual thresholds instead of inventing a universal target. Badges can lag: verify the SHA and upload result.

When changing reporting helpers, run:

```sh
python .github/scripts/test_generate_coverage.py
python .github/scripts/test_prepare_coverage_upload.py
python .github/scripts/test_criterion_to_junit.py
```
