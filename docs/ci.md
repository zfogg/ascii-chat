# Continuous integration

Pull requests and pushes run Linux native validation automatically. Build,
Test Suite, Code Analysis, Security and Markdown Linting
cancel superseded runs for the same workflow and PR or branch. Closing a PR
cancels its remaining validation runs; deployment and publication workflows are
excluded from that cleanup. Container publication finishes its active run while
keeping only the latest pending run.

Native compilation waits for workflow validation, patch whitespace and changed
C/C++ formatting checks. Browser tests wait for web type/lint and unit checks.
Web and container workflows use input-specific path filters; shared native
headers, dependencies and build configuration still trigger native checks.

## Full platform validation

In GitHub Actions, select **Build** or **Test Suite**, choose **Run workflow**,
select the branch to test and enable **Run the full native platform matrix**.
The default is off. These workflows can also be dispatched with the CLI:

```sh
gh workflow run build.yml --ref YOUR_BRANCH -f full_matrix=true
gh workflow run test.yml --ref YOUR_BRANCH -f full_matrix=true
```

Build includes all six existing Linux/macOS/Windows architecture combinations
when requested. Test Suite restores its existing macOS and Windows jobs where
supported. Automatic Test Suite runs keep Linux Debug/Release, integration,
performance, memory and functional checks. Complete cross-platform coverage
aggregation/publication runs only with the full Test Suite matrix, because its
validator requires all seven reports; automatic runs retain individual reports.

Release and website deployment workflows retain their separate publication
policies. Manual full validation is a user decision; agents do not need a new
instruction or special PR label.
