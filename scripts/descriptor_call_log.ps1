# Capture the structure of every descriptor call a run makes, so two revisions can be diffed.
#
#   ./scripts/descriptor_call_log.ps1 -Out build/descriptor-log-main.txt
#   (check out the other revision)
#   ./scripts/descriptor_call_log.ps1 -Out build/descriptor-log-branch.txt
#   git diff --no-index build/descriptor-log-main.txt build/descriptor-log-branch.txt   # must be empty
#
# Builds a separate scratch tree (build/descriptor-log, git-ignored) with descriptor_log.hpp force-included, starts the
# app, lets it reach the render loop, stops it, and keeps only the DSLOG lines. The product build is not touched.
param(
    [Parameter(Mandatory = $true)][string]$Out,
    [int]$Seconds = 20
)
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")

$header = (Resolve-Path "scripts/descriptor_call_log/descriptor_log.hpp").Path -replace "\\", "/"
$buildDir = "build/descriptor-log"

cmake -S . -B $buildDir -G Ninja -DCMAKE_BUILD_TYPE=Debug "-DCMAKE_CXX_FLAGS=-include $header" | Out-Null
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }
cmake --build $buildDir | Out-Null
if ($LASTEXITCODE -ne 0) { throw "build failed" }

# The exe loads libstdc++ and friends from msys64; Git's older copies on PATH make it exit with 0xC0000139.
$env:PATH = "C:\msys64\mingw64\bin;" + $env:PATH

$stderrFile = Join-Path $buildDir "run.stderr.txt"
$proc = Start-Process -FilePath (Join-Path $buildDir "main.exe") -WorkingDirectory $buildDir `
    -RedirectStandardError $stderrFile -RedirectStandardOutput (Join-Path $buildDir "run.stdout.txt") -PassThru
Start-Sleep -Seconds $Seconds
if ($proc.HasExited) { throw "app exited early with code $($proc.ExitCode); see $stderrFile" }
Stop-Process -Id $proc.Id -Force

$lines = Get-Content $stderrFile | Where-Object { $_ -like "DSLOG *" }
if (-not $lines) { throw "no DSLOG lines captured; was the header force-included?" }
$lines | Set-Content $Out
Write-Host "captured $($lines.Count) descriptor calls -> $Out"
