# Publish Test Results Action

A composite GitHub Action that handles test result publishing for the ASCII-Chat project. This action encapsulates all the logic for validating JUnit XML, publishing test results to GitHub checks, and uploading both test results and coverage to Codecov.

## Features

- ✅ Validates codecov.yml configuration
- ✅ Validates JUnit XML files
- 📊 Publishes test results as GitHub check runs
- 💬 Comments on PRs with test summaries
- 📈 Uploads test results to Codecov
- 🔍 Uploads coverage data to Codecov (optional)
- 🎯 Supports different test types (unit, integration, performance)

## Usage

```yaml
- name: Publish Test Results and Coverage
  uses: ./.github/actions/publish-test-results
  if: always()
  with:
    test-type: unit                           # Required: unit, integration, or performance
    os-name: ubuntu                           # Required: ubuntu or macos
    build-type: debug                          # Optional: debug or release (for unit tests)
    junit-file: junit.xml                      # Optional: path to JUnit XML (default: junit.xml)
    coverage-files: './*.gcov'                 # Optional: coverage file pattern (default: ./*.gcov)
    codecov-token: ${{ secrets.CODECOV_TOKEN }} # Required: Codecov token
    upload-coverage: 'true'                    # Optional: whether to upload coverage (default: false)
```

## Inputs

| Input | Description | Required | Default |
|-------|-------------|----------|---------|
| `test-type` | Type of test (unit, integration, performance) | Yes | - |
| `os-name` | Operating system name (ubuntu, macos) | Yes | - |
| `build-type` | Build type (debug, release) | No | '' |
| `junit-file` | Path to JUnit XML file | No | 'junit.xml' |
| `coverage-files` | Coverage file pattern for Codecov | No | './*.gcov' |
| `codecov-token` | Codecov token | Yes | - |
| `upload-coverage` | Whether to upload coverage to Codecov | No | 'false' |

## Examples

### Unit Tests with Coverage (Debug Build)
```yaml
- name: Publish Test Results and Coverage
  uses: ./.github/actions/publish-test-results
  if: always()
  with:
    test-type: unit
    os-name: ${{ matrix.os-name }}
    build-type: ${{ matrix.build_type }}
    codecov-token: ${{ secrets.CODECOV_TOKEN }}
    upload-coverage: ${{ matrix.build_type == 'debug' && 'true' || 'false' }}
```

### Integration Tests
```yaml
- name: Publish Test Results and Coverage
  uses: ./.github/actions/publish-test-results
  if: always()
  with:
    test-type: integration
    os-name: ${{ matrix.os-name }}
    codecov-token: ${{ secrets.CODECOV_TOKEN }}
    upload-coverage: 'true'
```

### Performance Tests
```yaml
- name: Publish Test Results and Coverage
  uses: ./.github/actions/publish-test-results
  if: always()
  with:
    test-type: performance
    os-name: ${{ matrix.os-name }}
    codecov-token: ${{ secrets.CODECOV_TOKEN }}
    upload-coverage: 'true'
```

## What It Does

1. **Validates Codecov YAML**: Ensures codecov.yml configuration is valid
2. **Validates JUnit XML**: Checks if the file exists and is valid XML
3. **Publishes to GitHub**: Creates check runs with test results
4. **Comments on PRs**: Adds summary comments with test statistics
5. **Uploads to Codecov**: Sends test results for test analytics
6. **Uploads Coverage**: Optionally uploads coverage data (gcov files)

## Check Names

The action creates GitHub checks with names based on test type:
- Unit Tests: `Unit Tests (os-build)` e.g., "Unit Tests (ubuntu-debug)"
- Integration Tests: `Integration Tests (os)` e.g., "Integration Tests (macos)"
- Performance Tests: `Performance Tests (os)` e.g., "Performance Tests (ubuntu)"

## Codecov Flags

The action uses appropriate flags for Codecov organization:
- Unit Tests: `ascii-chat-tests-{os}-{build}`
- Integration Tests: `ascii-chat-integration-{os}`
- Performance Tests: `ascii-chat-performance-{os}`