# Run the app with the Vulkan validation layers on and compare the VUIDs it reports with scripts/validation_baseline.txt.
#
#   ./scripts/validation_check.ps1
#
# Builds a separate scratch tree (build/validation, git-ignored) with ALPHA_ENABLE_VALIDATION, runs the app until it has
# created its resources plus a settle period, stops it and diffs the unique VUID list against the baseline.
# Exits 1 on any difference: a new VUID is a regression, a missing one means the baseline file should shrink.
# Needs a GPU and the Vulkan SDK validation layer; it is not part of `just check`.
param(
    [int]$TimeoutSeconds = 120,
    [int]$SettleSeconds = 20
)
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")

$buildDir = "build/validation"
cmake --preset mingw -B $buildDir -DCMAKE_BUILD_TYPE=Debug -DALPHA_ENABLE_VALIDATION=ON | Out-Null
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }
cmake --build $buildDir | Out-Null
if ($LASTEXITCODE -ne 0) { throw "build failed" }

# The exe loads libstdc++ and friends from msys64; Git's older copies on PATH make it exit with 0xC0000139.
$env:PATH = "C:\msys64\mingw64\bin;" + $env:PATH

$stderrFile = Join-Path $buildDir "run.stderr.txt"
$stdoutFile = Join-Path $buildDir "run.stdout.txt"
$proc = Start-Process -FilePath (Join-Path $buildDir "AlphaRenderer.exe") -WorkingDirectory $buildDir `
    -RedirectStandardError $stderrFile -RedirectStandardOutput $stdoutFile -PassThru

$marker = "RenderingResources created with"
$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
while ((Get-Date) -lt $deadline) {
    if ($proc.HasExited) { throw "app exited early with code $($proc.ExitCode); see $stderrFile" }
    if ((Test-Path $stdoutFile) -and (Select-String -Path $stdoutFile -Pattern $marker -Quiet)) { break }
    Start-Sleep -Milliseconds 500
}
if ((Get-Date) -ge $deadline) { Stop-Process -Id $proc.Id -Force; throw "timed out waiting for '$marker'" }
Start-Sleep -Seconds $SettleSeconds
if ($proc.HasExited) { throw "app exited during the run with code $($proc.ExitCode); see $stderrFile" }
Stop-Process -Id $proc.Id -Force

# No output is a valid result (a clean run): the validation build makes Device throw at startup if the layers are
# missing, and the marker above is only reached after the device exists, so silence cannot mean "layers not loaded".
$layerLines = @(Select-String -Path $stderrFile -Pattern "validation layer:" -SimpleMatch)
$found = @($layerLines | ForEach-Object { [regex]::Matches($_.Line, "VUID-[A-Za-z0-9_-]+") } | ForEach-Object Value | Sort-Object -Unique)
$baseline = @(Get-Content "scripts/validation_baseline.txt" | Where-Object { $_ -and -not $_.StartsWith("#") } | Sort-Object -Unique)

$diff = Compare-Object -ReferenceObject $baseline -DifferenceObject $found
if ($diff) {
    $diff | ForEach-Object {
        $side = if ($_.SideIndicator -eq "=>") { "NEW    " } else { "MISSING" }
        Write-Host "$side $($_.InputObject)"
    }
    Write-Host "validation check FAILED; full output in $stderrFile"
    exit 1
}
Write-Host "validation OK: $($found.Count) VUIDs, all in the baseline ($($baseline.Count) entries)"
