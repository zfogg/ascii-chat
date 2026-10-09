# Windows PowerShell 5.1+ / PowerShell 7. Run with irm .../install.ps1 | iex.
# ASCII_CHAT_INSTALL_DIR overrides the managed installation directory.
# ASCII_CHAT_VERSION pins a release, e.g. 0.12.17 (v0.12.17 also works).
& {
    $ErrorActionPreference = 'Stop'
    $ProgressPreference = 'SilentlyContinue'
    if ($env:OS -ne 'Windows_NT') { throw 'Use install.sh on Linux and macOS.' }
    [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
    $nativeArch = $env:PROCESSOR_ARCHITEW6432
    if (-not $nativeArch) { $nativeArch = $env:PROCESSOR_ARCHITECTURE }
    $arch = switch ($nativeArch) {
        'AMD64' { 'amd64' }
        'ARM64' { 'arm64' }
        default { throw "Unsupported Windows architecture: $nativeArch" }
    }
    $admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    $scope = if ($admin) { 'Machine' } else { 'User' }
    $base = if ($admin) { $env:ProgramFiles } else { Join-Path $env:LOCALAPPDATA 'Programs' }
    $destination = if ($env:ASCII_CHAT_INSTALL_DIR) { $env:ASCII_CHAT_INSTALL_DIR } else { Join-Path $base 'ascii-chat' }
    if (-not [IO.Path]::IsPathRooted($destination)) { throw 'ASCII_CHAT_INSTALL_DIR must be absolute.' }
    $destination = [IO.Path]::GetFullPath($destination).TrimEnd('\')
    if ($destination -eq [IO.Path]::GetPathRoot($destination).TrimEnd('\')) { throw 'Cannot install into a drive root.' }
    if (Test-Path -LiteralPath $destination) {
        $existing = Get-Item -LiteralPath $destination
        if (($existing.Attributes -band [IO.FileAttributes]::ReparsePoint) -or -not (Test-Path -LiteralPath "$destination\.ascii-chat-installer" -PathType Leaf)) {
            throw "Refusing to replace unmanaged directory: $destination"
        }
    }
    $parent = Split-Path -Parent $destination
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
    $work = Join-Path $parent ('.ascii-chat-install-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $work | Out-Null
    $previous = Join-Path $work 'previous'
    $replaced = $false
    $committed = $false
    try {
        $gumZip = Join-Path $work 'gum.zip'
        Invoke-WebRequest -UseBasicParsing 'https://github.com/charmbracelet/gum/releases/download/v2.0.2/gum_2.0.2_Windows_x86_64.zip' -OutFile $gumZip -TimeoutSec 300
        if ((Get-FileHash $gumZip -Algorithm SHA256).Hash -ne '0397091dec9b4e8f00e02b90fd3eb07bf45acabbdb61d67437950f18e03a8b79') { throw 'Gum checksum mismatch.' }
        Expand-Archive -LiteralPath $gumZip -DestinationPath "$work\gum"
        $gum = @(Get-ChildItem "$work\gum" -Recurse -Filter gum.exe)[0].FullName
        & $gum style --border rounded --padding '1 2' --border-foreground 86 'ascii-chat' "Install for Windows / $arch"
        if ($LASTEXITCODE -ne 0) { throw 'Unable to run Gum (ARM64 requires Windows x64 emulation).' }
        $releaseApi = 'https://api.github.com/repos/zfogg/ascii-chat/releases/latest'
        if ($env:ASCII_CHAT_VERSION) {
            $requestedVersion = $env:ASCII_CHAT_VERSION
            if ($requestedVersion -match '\A[0-9]+\.[0-9]+\.[0-9]+\z') { $requestedVersion = "v$requestedVersion" }
            if ($requestedVersion -notmatch '^v[0-9][a-zA-Z0-9._-]*$') { throw 'Invalid release tag.' }
            $releaseApi = "https://api.github.com/repos/zfogg/ascii-chat/releases/tags/$requestedVersion"
        }
        $release = Invoke-RestMethod $releaseApi -TimeoutSec 60
        $tag = $release.tag_name
        $name = "ascii-chat-$($tag -replace '^v', '')-Windows-$arch.zip"
        $asset = @($release.assets | Where-Object name -eq $name)
        if ($asset.Count -ne 1) { throw "Release does not contain $name" }
        Write-Host "Downloading $name"
        $zip = Join-Path $work 'release.zip'
        Invoke-WebRequest -UseBasicParsing $asset[0].browser_download_url -OutFile $zip -TimeoutSec 300
        if ($asset[0].digest -match '^sha256:(.+)$') {
            if ((Get-FileHash $zip -Algorithm SHA256).Hash -ne $Matches[1]) { throw 'Release checksum mismatch.' }
        }
        $payload = Join-Path $work 'new'
        Expand-Archive -LiteralPath $zip -DestinationPath $payload
        $binary = Join-Path $payload 'bin\ascii-chat.exe'
        if (-not (Test-Path -LiteralPath $binary -PathType Leaf)) { throw 'Release is missing bin/ascii-chat.exe.' }
        & $binary --version
        if ($LASTEXITCODE -ne 0) { throw "Release executable failed: $LASTEXITCODE" }
        Set-Content -LiteralPath "$payload\.ascii-chat-installer" -Value $tag
        if (Test-Path -LiteralPath $destination) { Move-Item -LiteralPath $destination -Destination $previous }
        $replaced = $true
        Move-Item -LiteralPath $payload -Destination $destination
        $binDir = Join-Path $destination 'bin'
        $savedPath = [string][Environment]::GetEnvironmentVariable('Path', $scope)
        if ($binDir -notin ($savedPath -split ';')) {
            [Environment]::SetEnvironmentVariable('Path', ($savedPath.TrimEnd(';') + ';' + $binDir).TrimStart(';'), $scope)
        }
        if ($binDir -notin ($env:Path -split ';')) { $env:Path += ";$binDir" }
        $committed = $true
        & $gum style --foreground 86 "Installed $tag -> $binDir" 'Ready to chat: ascii-chat' 'If PATH changed, open a new Windows terminal to load the updated PATH. This PowerShell session is already updated.'
    }
    finally {
        # Only these exact installer-owned paths can be removed.
        if ($replaced -and -not $committed) {
            if (Test-Path -LiteralPath $destination) { Remove-Item -LiteralPath $destination -Recurse -Force }
            if (Test-Path -LiteralPath $previous) { Move-Item -LiteralPath $previous -Destination $destination }
        }
        if (Test-Path -LiteralPath $work) {
            try {
                Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction Stop
            }
            catch {
                if (-not $committed) { throw }
                Write-Warning "Installation succeeded, but cleanup is incomplete at $work. Close any running ascii-chat processes and remove that temporary directory."
            }
        }
    }
}
