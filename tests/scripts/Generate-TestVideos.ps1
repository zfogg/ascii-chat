#!/usr/bin/env pwsh
# PowerShell script to generate test videos for ascii-chat
# Creates a simulated webcam video of a person at their computer

param(
    [int]$Duration = 10,
    [string]$Resolution = "640x480",
    [int]$FPS = 30
)

$ErrorActionPreference = "Stop"

# Check if ffmpeg is available
if (-not (Get-Command ffmpeg -ErrorAction SilentlyContinue)) {
    Write-Host "Error: ffmpeg is not installed or not in PATH" -ForegroundColor Red
    Write-Host "Please install ffmpeg from: https://ffmpeg.org/download.html" -ForegroundColor Yellow
    exit 1
}

# Setup paths
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$FixturesDir = Join-Path (Join-Path $ScriptDir "..") "fixtures"

# Create fixtures directory if it doesn't exist
if (-not (Test-Path $FixturesDir)) {
    New-Item -ItemType Directory -Path $FixturesDir -Force | Out-Null
}

Set-Location $FixturesDir

Write-Host "Generating test video for ascii-chat..." -ForegroundColor Cyan

# Generate webcam simulation video
Write-Host "Creating simulated webcam video..." -ForegroundColor Yellow

# Build the complex filter as a single string (PowerShell handles multiline strings differently)
$filter = @"
nullsrc=size=$Resolution:rate=$FPS:duration=$Duration,
format=rgb24,
geq=r='128 + 20*sin(X/100) + 10*sin(Y/100) + if(pow((X-320)/80,2)+pow((Y-140)/100,2)<1, 100 + 80*exp(-pow((X-320)/80,2)-pow((Y-140)/100,2)) + 10*sin(T*2+X/50), 0) + if(pow((X-290)/15,2)+pow((Y-130)/10,2)<1, -50, 0) + if(pow((X-350)/15,2)+pow((Y-130)/10,2)<1, -50, 0) + if(abs(X-320)<30*abs(Y-180)<5, -30, 0) + if(Y>240, if(abs(X-320)<150-pow((Y-240)/50,2)*50, 50 + 30*sin(T*1.5), 0), 0) + 5*sin(T*0.5) + 3*cos(T*0.3)':
g='120 + 20*sin(X/100) + 10*sin(Y/100) + if(pow((X-320)/80,2)+pow((Y-140)/100,2)<1, 80 + 60*exp(-pow((X-320)/80,2)-pow((Y-140)/100,2)) + 10*sin(T*2+X/50), 0) + if(pow((X-290)/15,2)+pow((Y-130)/10,2)<1, -40, 0) + if(pow((X-350)/15,2)+pow((Y-130)/10,2)<1, -40, 0) + if(abs(X-320)<30*abs(Y-180)<5, -25, 0) + if(Y>240, if(abs(X-320)<150-pow((Y-240)/50,2)*50, 40 + 30*sin(T*1.5), 0), 0) + 5*sin(T*0.5) + 3*cos(T*0.3)':
b='140 + 20*sin(X/100) + 10*sin(Y/100) + if(pow((X-320)/80,2)+pow((Y-140)/100,2)<1, 40 + 30*exp(-pow((X-320)/80,2)-pow((Y-140)/100,2)) + 10*sin(T*2+X/50), 0) + if(pow((X-290)/15,2)+pow((Y-130)/10,2)<1, -30, 0) + if(pow((X-350)/15,2)+pow((Y-130)/10,2)<1, -30, 0) + if(abs(X-320)<30*abs(Y-180)<5, -20, 0) + if(Y>240, if(abs(X-320)<150-pow((Y-240)/50,2)*50, 80 + 30*sin(T*1.5), 0), 0) + 5*sin(T*0.5) + 3*cos(T*0.3)',
noise=alls=20:allf=t,
drawtext=text='Test User':x=10:y=10:fontcolor=white:fontsize=16:box=1:boxcolor=black@0.5:boxborderw=5
"@

# Run ffmpeg to generate the webcam simulation
try {
    $ffmpegArgs = @(
        "-f", "lavfi",
        "-i", $filter,
        "-c:v", "libx264",
        "-preset", "ultrafast",
        "-crf", "28",
        "-pix_fmt", "yuv420p",
        "-y",
        "webcam_test.mp4"
    )

    & ffmpeg @ffmpegArgs 2>&1 | Out-Null

    Write-Host "Webcam simulation video created: webcam_test.mp4" -ForegroundColor Green

    # Create default test link/copy
    if (Test-Path "webcam_test.mp4") {
        Copy-Item "webcam_test.mp4" "default_test.mp4" -Force
        Write-Host "Default test video created: default_test.mp4" -ForegroundColor Green
    }
}
catch {
    Write-Host "Failed to generate webcam simulation, creating simple test pattern..." -ForegroundColor Yellow

    # Generate simple test pattern as fallback
    $simpleArgs = @(
        "-f", "lavfi",
        "-i", "testsrc2=duration=5:size=${Resolution}:rate=$FPS",
        "-vf", "drawtext=text='ascii-chat Test':x=(w-text_w)/2:y=(h-text_h)/2:fontcolor=white:fontsize=30",
        "-c:v", "libx264",
        "-preset", "ultrafast",
        "-crf", "28",
        "-pix_fmt", "yuv420p",
        "-y",
        "test_pattern.mp4"
    )

    & ffmpeg @simpleArgs 2>&1 | Out-Null

    Copy-Item "test_pattern.mp4" "default_test.mp4" -Force
    Write-Host "Simple test pattern created: test_pattern.mp4" -ForegroundColor Green
}

Write-Host "`nTest videos generated successfully!" -ForegroundColor Green
Write-Host "Available videos:" -ForegroundColor Cyan
Get-ChildItem -Filter "*.mp4" | ForEach-Object {
    Write-Host "  - $($_.Name)" -ForegroundColor Gray
}
