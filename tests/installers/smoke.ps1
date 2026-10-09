param([Parameter(Mandatory = $true)][string]$Installer)
$ErrorActionPreference = 'Stop'
$Installer = (Resolve-Path $Installer).Path
$sandbox = Join-Path ([IO.Path]::GetTempPath()) ('ascii-chat-smoke-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $sandbox | Out-Null
$oldDirectory = $env:ASCII_CHAT_INSTALL_DIR
$oldVersion = $env:ASCII_CHAT_VERSION
$processPath = $env:Path
$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
$scope = if ($admin) { 'Machine' } else { 'User' }
$savedPath = [Environment]::GetEnvironmentVariable('Path', $scope)
function Assert($Condition, $Message) { if (-not $Condition) { throw $Message } }
try {
    $env:ASCII_CHAT_INSTALL_DIR = Join-Path $sandbox 'install with spaces'
    $env:ASCII_CHAT_VERSION = $null
    & $Installer
    $binary = Join-Path $env:ASCII_CHAT_INSTALL_DIR 'bin\ascii-chat.exe'
    $version = & $binary --version
    Assert ($LASTEXITCODE -eq 0) 'Installed binary failed'
    $bin = Split-Path -Parent $binary
    Assert ($bin -in ($env:Path -split ';')) 'Session PATH missing'
    Assert ($bin -in ([Environment]::GetEnvironmentVariable('Path', $scope) -split ';')) 'Persistent PATH missing'
    Set-Content "$env:ASCII_CHAT_INSTALL_DIR\obsolete-file" 'obsolete'
    Set-Content "$sandbox\unrelated-file" 'keep'
    Get-Content -Raw $Installer | Invoke-Expression
    Assert (-not (Test-Path "$env:ASCII_CHAT_INSTALL_DIR\obsolete-file")) 'Upgrade left obsolete files'
    Assert (Test-Path "$sandbox\unrelated-file") 'Unrelated file deleted'
    Assert (@(Get-ChildItem $sandbox -Filter '.ascii-chat-install-*').Count -eq 0) 'Temporary files remain'
    Assert (@([Environment]::GetEnvironmentVariable('Path', $scope) -split ';' | Where-Object { $_ -eq $bin }).Count -eq 1) 'Duplicate PATH entry'
    $env:ASCII_CHAT_VERSION = 'v0.0.0-installer-missing-release'
    $failed = $false
    try { & $Installer } catch { Write-Host "Expected failure: $_"; $failed = $true }
    Assert $failed 'Missing release unexpectedly succeeded'
    Assert ((& $binary --version) -eq $version) 'Failed install changed existing binary'
    Assert (@(Get-ChildItem $sandbox -Filter '.ascii-chat-install-*').Count -eq 0) 'Failed install left temporary files'
    $env:ASCII_CHAT_VERSION = $null
    $env:ASCII_CHAT_INSTALL_DIR = Join-Path $sandbox 'unmanaged'
    New-Item -ItemType Directory $env:ASCII_CHAT_INSTALL_DIR | Out-Null
    Set-Content "$env:ASCII_CHAT_INSTALL_DIR\keep" 'keep'
    $failed = $false
    try { & $Installer } catch { Write-Host "Expected failure: $_"; $failed = $true }
    Assert $failed 'Unmanaged directory was overwritten'
    Assert ((Get-Content "$env:ASCII_CHAT_INSTALL_DIR\keep") -eq 'keep') 'Unmanaged file changed'
    Write-Host 'PASS: install, piped upgrade, PATH, cleanup, failed download, unmanaged file preservation'
}
finally {
    [Environment]::SetEnvironmentVariable('Path', $savedPath, $scope)
    $env:Path = $processPath
    $env:ASCII_CHAT_INSTALL_DIR = $oldDirectory
    $env:ASCII_CHAT_VERSION = $oldVersion
    # The sandbox is a newly created GUID directory beneath the OS temp directory.
    Remove-Item -LiteralPath $sandbox -Recurse -Force
}
