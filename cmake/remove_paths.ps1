# PowerShell script to remove embedded file paths from Windows PE binaries
# Replaces absolute paths with relative paths using byte-level replacement
# Usage: .\remove_paths.ps1 <binary_path> <source_dir> [build_dir]

param(
    [Parameter(Mandatory=$true)]
    [string]$BinaryPath,
    
    [Parameter(Mandatory=$true)]
    [string]$SourceDir,
    
    [Parameter(Mandatory=$false)]
    [string]$BuildDir = ""
)

# Check if binary exists
if (-not (Test-Path $BinaryPath)) {
    Write-Error "Binary not found: $BinaryPath"
    exit 1
}

# Validate source directory
if (-not (Test-Path $SourceDir)) {
    Write-Error "Source directory not found: $SourceDir"
    exit 1
}

Write-Host "Removing embedded paths from: $BinaryPath" -ForegroundColor Cyan

# Resolve source directory to absolute path
$sourcePath = (Resolve-Path $SourceDir).Path
Write-Host "Source directory: $sourcePath" -ForegroundColor Gray
if ($BuildDir) {
    Write-Host "Build directory: $BuildDir" -ForegroundColor Gray
}

# Create patterns based on the source directory
# Convert source path to both forward slash and backslash formats
$sourcePathForward = $sourcePath -replace '\\', '/'
$sourcePathBackslash = $sourcePath -replace '/', '\\'

# Get user's home directory
$homeDir = $env:USERPROFILE
if (-not $homeDir) {
    $homeDir = $env:HOME
}

$replacements = @()

# Patterns to match absolute paths based on source directory
if ($sourcePath) {
    $replacements += @(
        @{Old = $sourcePathBackslash + '\'; New = ''},
        @{Old = $sourcePathForward + '/'; New = ''}
    )
}

# Patterns to match paths in user's home directory
if ($homeDir -and (Test-Path $homeDir)) {
    $homePath = (Resolve-Path $homeDir).Path
    $homePathForward = $homePath -replace '\\', '/'
    $homePathBackslash = $homePath -replace '/', '\\'
    
    Write-Host "Home directory: $homePath" -ForegroundColor Gray
    
    # Add home directory patterns (if not already covered by source directory)
    if ($homePath -ne $sourcePath) {
        # For home directory, we want to replace with just the relative part
        # But we need to be careful - we'll match paths that start with home dir
        # and replace the home dir prefix with empty string
        $replacements += @(
            @{Old = $homePathBackslash + '\'; New = ''},
            @{Old = $homePathForward + '/'; New = ''}
        )
    }
}

if ($replacements.Count -eq 0) {
    Write-Warning "No replacement patterns generated - cannot remove paths"
    exit 1
}

Write-Host "Using $($replacements.Count) replacement patterns" -ForegroundColor Gray

# Read binary as bytes (DO NOT convert to string - that corrupts binary data)
$bytes = [System.IO.File]::ReadAllBytes($BinaryPath)
$originalSize = $bytes.Length
$modified = $false
$totalReplacements = 0

# Process each replacement pattern
foreach ($replacement in $replacements) {
    $oldStr = $replacement.Old
    $newStr = $replacement.New
    
    # Get byte representations
    $oldBytes = [System.Text.Encoding]::UTF8.GetBytes($oldStr)
    $newBytes = [System.Text.Encoding]::UTF8.GetBytes($newStr)
    
    # Find and replace byte sequences in-place
    $patternMatches = 0
    for ($i = 0; $i -le $bytes.Length - $oldBytes.Length; $i++) {
        $byteMatch = $true
        for ($j = 0; $j -lt $oldBytes.Length; $j++) {
            if ($bytes[$i + $j] -ne $oldBytes[$j]) {
                $byteMatch = $false
                break
            }
        }
        
        if ($byteMatch) {
            # Found match - replace bytes
            if ($newBytes.Length -le $oldBytes.Length) {
                # New path fits within old path length - pad with nulls
                # Copy new path bytes
                for ($k = 0; $k -lt $newBytes.Length; $k++) {
                    $bytes[$i + $k] = $newBytes[$k]
                }
                # Pad remaining with nulls (safer than leaving old bytes)
                for ($k = $newBytes.Length; $k -lt $oldBytes.Length; $k++) {
                    $bytes[$i + $k] = [byte]0x00
                }
                
                $patternMatches++
                $totalReplacements++
                $modified = $true
                $i += $oldBytes.Length - 1  # Skip past replaced bytes
            } else {
                # New path is longer - skip to avoid corruption
                Write-Warning "  Skipping: new path longer than old (would corrupt binary)"
                $i += $oldBytes.Length - 1
            }
        }
    }
    
    if ($patternMatches -gt 0) {
        Write-Host "Replaced $patternMatches occurrences of pattern" -ForegroundColor Yellow
    }
}

if ($modified) {
    # Verify binary size hasn't changed (important for PE format)
    if ($bytes.Length -eq $originalSize) {
        # Write modified binary back
        [System.IO.File]::WriteAllBytes($BinaryPath, $bytes)
        Write-Host "Made $totalReplacements replacements - successfully removed embedded paths from binary" -ForegroundColor Green
    } else {
        Write-Error "Binary size changed from $originalSize to $($bytes.Length) bytes - this would corrupt the binary!"
        exit 1
    }
} else {
    Write-Host "No embedded paths found to remove" -ForegroundColor Gray
}

exit 0
