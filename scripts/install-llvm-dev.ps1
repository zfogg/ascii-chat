[CmdletBinding()]
param(
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string]$Version = '23.1.3',
    [ValidateSet('x86_64', 'aarch64')]
    [string]$Architecture = $(if ([System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture -eq 'Arm64') { 'aarch64' } else { 'x86_64' }),
    [string]$Destination = '',
    [string]$ArchivePath = '',
    [string]$ExpectedSha256 = ''
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not $Destination) {
    $suffix = if ($Architecture -eq 'x86_64') { '' } else { '-aarch64' }
    $Destination = Join-Path $root ".deps-cache/llvm-dev-$version$suffix"
}
New-Item -ItemType Directory -Force -Path $Destination | Out-Null
$Destination = (Resolve-Path -LiteralPath $Destination).Path
$members = @('include/llvm', 'include/llvm-c', 'include/clang-c', 'lib/LLVMSupport.lib',
             'lib/LLVMDemangle.lib', 'lib/libclang.lib', 'bin/libclang.dll')
$required = @('include/llvm/Support/CommandLine.h', 'include/llvm-c/DataTypes.h',
              'include/clang-c/Index.h', 'lib/LLVMSupport.lib', 'lib/LLVMDemangle.lib',
              'lib/libclang.lib', 'bin/libclang.dll')
if (@($required | Where-Object { -not (Test-Path -LiteralPath (Join-Path $Destination $_)) }).Count -eq 0) {
    Write-Host "LLVM development files already present: $Destination"
    return
}

$prefix = "clang+llvm-$version-$Architecture-pc-windows-msvc"
$archive = if ($ArchivePath) { $ArchivePath } else { Join-Path $Destination "$prefix.tar.xz" }
$hash = $ExpectedSha256
if (-not $hash -and $Version -eq '23.1.3') {
    $hash = if ($Architecture -eq 'x86_64') {
        '9ee6a2192fbb30c776415af529927f76ca6b5d1d55699bb1315f9878e610b5b8'
    } else {
        '4a42903db59f990a10c245eb32999f1e3247c37ba66de87f8f8dc2338ed9a79b'
    }
}
if (-not $hash) { throw 'Provide -ExpectedSha256 for an LLVM archive other than 23.1.3.' }
if ($hash -notmatch '^[0-9a-fA-F]{64}$') { throw 'ExpectedSha256 must be a SHA-256 digest.' }
if (-not (Test-Path -LiteralPath $archive)) {
    if ($ArchivePath) { throw "LLVM archive not found: $archive" }
    $url = "https://github.com/llvm/llvm-project/releases/download/llvmorg-$version/$prefix.tar.xz"
    Write-Host "Downloading LLVM $version development archive for $Architecture"
    $partial = "$archive.download"
    Invoke-WebRequest -Uri $url -OutFile $partial
    if ((Get-FileHash -LiteralPath $partial -Algorithm SHA256).Hash -ne $hash) {
        throw "LLVM development archive SHA-256 mismatch: $partial"
    }
    Move-Item -LiteralPath $partial -Destination $archive
}
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $hash) {
    throw "LLVM development archive SHA-256 mismatch: $archive"
}
$tar = Join-Path $env:SystemRoot 'System32/tar.exe'
$archiveMembers = @($members | ForEach-Object { "$prefix/$_" })
& $tar -xf $archive -C $Destination --strip-components 1 @archiveMembers
if ($LASTEXITCODE -ne 0) { throw "LLVM development archive extraction failed: $LASTEXITCODE" }
$missing = @($required | Where-Object { -not (Test-Path -LiteralPath (Join-Path $Destination $_)) })
if ($missing.Count) { throw "LLVM development archive is missing: $($missing -join ', ')" }
Write-Host "LLVM development files installed: $Destination"
