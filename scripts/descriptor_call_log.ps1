# Capture the structure of every descriptor call a run makes, so two revisions can be diffed.
#
#   ./scripts/descriptor_call_log.ps1 -Out build/descriptor-log-main.txt
#   (check out the other revision)
#   ./scripts/descriptor_call_log.ps1 -Out build/descriptor-log-branch.txt
#   git diff --no-index build/descriptor-log-main.txt build/descriptor-log-branch.txt   # must be empty
#
# The same script captures the image/view creation log (scripts/image_call_log/image_log.hpp):
#   ./scripts/descriptor_call_log.ps1 -Out build/image-log-main.txt -Header scripts/image_call_log/image_log.hpp -Prefix ILOG -CompleteLine '\.\s*$' -BuildDir build/image-log
#
# And the sampler log: -Header scripts/sampler_call_log/sampler_log.hpp -Prefix SLOG -CompleteLine '\.\s*$' -BuildDir build/sampler-log
#
# Builds a separate scratch tree (build/descriptor-log, git-ignored) with descriptor_log.hpp force-included, starts the
# app, waits until it has created every descriptor, stops it, and keeps only the DSLOG lines. The product build is not touched.
param(
    [Parameter(Mandatory = $true)][string]$Out,
    [int]$TimeoutSeconds = 120,
    [int]$SettleSeconds = 5,
    # Which log to capture: the descriptor log by default; scripts/image_call_log/image_log.hpp with -Prefix ILOG for images.
    [string]$Header = "scripts/descriptor_call_log/descriptor_log.hpp",
    [string]$Prefix = "DSLOG",
    # Matches the end of a complete last line, to detect a log truncated by killing the app mid-write.
    [string]$CompleteLine = "(\]|\}|from layout#\d+|count=\d+)\s*$",
    [string]$BuildDir = "build/descriptor-log"
)
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")

$headerPath = (Resolve-Path $Header).Path -replace "\\", "/"

cmake -S . -B $BuildDir -G Ninja -DCMAKE_BUILD_TYPE=Debug "-DCMAKE_CXX_FLAGS=-include $headerPath" | Out-Null
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }
cmake --build $BuildDir | Out-Null
if ($LASTEXITCODE -ne 0) { throw "build failed" }

# The exe loads libstdc++ and friends from msys64; Git's older copies on PATH make it exit with 0xC0000139.
$env:PATH = "C:\msys64\mingw64\bin;" + $env:PATH

$stderrFile = Join-Path $BuildDir "run.stderr.txt"
$stdoutFile = Join-Path $BuildDir "run.stdout.txt"
$proc = Start-Process -FilePath (Join-Path $BuildDir "main.exe") -WorkingDirectory $BuildDir `
    -RedirectStandardError $stderrFile -RedirectStandardOutput $stdoutFile -PassThru

# Startup time varies with machine load, so wait for the app's own "all descriptors created" message instead of a
# fixed delay, then give it a few frames. Killing it mid-startup would truncate the last logged call.
$marker = "RenderingResources created with"
$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
while ((Get-Date) -lt $deadline) {
    if ($proc.HasExited) { throw "app exited early with code $($proc.ExitCode); see $stderrFile" }
    if ((Test-Path $stdoutFile) -and (Select-String -Path $stdoutFile -Pattern $marker -Quiet)) { break }
    Start-Sleep -Milliseconds 500
}
if ((Get-Date) -ge $deadline) { Stop-Process -Id $proc.Id -Force; throw "timed out waiting for '$marker'" }
Start-Sleep -Seconds $SettleSeconds
Stop-Process -Id $proc.Id -Force

$lines = Get-Content $stderrFile | Where-Object { $_ -like "$Prefix *" }
if (-not $lines) { throw "no $Prefix lines captured; was the header force-included?" }
$last = $lines[-1]
if ($last -notmatch $CompleteLine) { throw "last $Prefix line looks truncated: $last" }
$lines | Set-Content $Out
Write-Host "captured $($lines.Count) $Prefix calls -> $Out"
