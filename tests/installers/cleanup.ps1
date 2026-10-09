param([Parameter(Mandatory = $true)][string]$Installer)
$ErrorActionPreference = 'Stop'
# Exercise the installer's actual finally block with a real Windows file lock.
$tokens = $null
$errors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile((Resolve-Path $Installer).Path, [ref]$tokens, [ref]$errors)
if ($errors) { throw $errors }
$transaction = $ast.Find({ param($node) $node -is [System.Management.Automation.Language.TryStatementAst] -and $null -ne $node.Finally }, $true)
$block = $transaction.Finally.Extent.Text
$cleanup = [scriptblock]::Create($block.Substring(1, $block.Length - 2))
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('ascii-chat-cleanup-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot | Out-Null
$work = Join-Path $testRoot 'work'
$previous = Join-Path $work 'previous'
New-Item -ItemType Directory -Path $previous -Force | Out-Null
$lockedFile = Join-Path $previous 'locked.exe'
[IO.File]::WriteAllText($lockedFile, 'old executable')
$handle = [IO.File]::Open($lockedFile, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::None)
$replaced = $false
try {
    $committed = $true
    $warnings = @(& $cleanup 3>&1)
    if (-not ($warnings | Where-Object { $_ -is [System.Management.Automation.WarningRecord] -and $_.Message.Contains($work) })) {
        throw 'Committed cleanup failure must warn with the recovery directory'
    }
    if (-not (Test-Path -LiteralPath $lockedFile)) { throw 'Expected locked file to remain' }
    $committed = $false
    $failed = $false
    try { & $cleanup } catch { $failed = $true }
    if (-not $failed) { throw 'Uncommitted cleanup failure must propagate' }
    $handle.Dispose()
    & $cleanup
    if (Test-Path -LiteralPath $work) { throw 'Unlocked cleanup left temporary files' }
    Write-Host 'PASS: committed cleanup warns, uncommitted cleanup throws, unlocked cleanup succeeds'
}
finally {
    $handle.Dispose()
    # testRoot is the exact GUID directory created above beneath the OS temp directory.
    Remove-Item -LiteralPath $testRoot -Recurse -Force
}
