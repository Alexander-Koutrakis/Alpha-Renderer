# Resize the app window under the validation layers and check that the destroy-and-recreate path is clean.
#
#   ./scripts/resize_check.ps1
#
# Renderer::handleWindowResize destroys and rebuilds RenderingResources, which the call-log harnesses never see (they
# only capture startup). This runs the validation build (build/validation, as validation_check.ps1 does), resizes the
# window several times (shrink, grow, minimise, restore), then closes it normally and requires: the app was still
# running before the close, RenderingResources was rebuilt at least once per size change ("RenderingResources created
# with" in stdout), the app shut down by itself with exit code 0, and no VUID outside scripts/validation_baseline.txt.
# The normal shutdown matters: vkDestroyDevice runs while the debug messenger is alive, so the validation layer reports
# every object that was not destroyed (validation_check.ps1 kills the app and never sees that). Needs a GPU and an
# untouched screen; it is not part of `just check`.
param(
    [int]$TimeoutSeconds = 120,
    [int]$StepSeconds = 3,
    [int]$SettleSeconds = 5,
    [int]$ShutdownSeconds = 30
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

Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class AlphaResizeWin32 {
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hWnd, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int cmd);
}
"@
$SWP_NOMOVE_NOZORDER = 0x0002 -bor 0x0004
$SW_MINIMIZE = 6
$SW_RESTORE = 9

$stderrFile = Join-Path $buildDir "resize.stderr.txt"
$stdoutFile = Join-Path $buildDir "resize.stdout.txt"
$proc = Start-Process -FilePath (Join-Path $buildDir "AlphaRenderer.exe") -WorkingDirectory $buildDir `
    -RedirectStandardError $stderrFile -RedirectStandardOutput $stdoutFile -PassThru
# Touching Handle keeps the process handle open, so ExitCode is still readable after the app has died.
$null = $proc.Handle

$marker = "RenderingResources created with"
$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
while ((Get-Date) -lt $deadline) {
    if ($proc.HasExited) { throw "app exited early with code $($proc.ExitCode); see $stderrFile" }
    $proc.Refresh()
    if ($proc.MainWindowHandle -ne [IntPtr]::Zero -and (Test-Path $stdoutFile) -and
        (Select-String -Path $stdoutFile -Pattern $marker -Quiet)) { break }
    Start-Sleep -Milliseconds 500
}
if ((Get-Date) -ge $deadline) { Stop-Process -Id $proc.Id -Force; throw "timed out waiting for '$marker' and a window" }
$hwnd = $proc.MainWindowHandle
Start-Sleep -Seconds $StepSeconds

$sizes = @(@(1280, 720), @(1600, 900), @(960, 540))
foreach ($size in $sizes) {
    [void][AlphaResizeWin32]::SetWindowPos($hwnd, [IntPtr]::Zero, 0, 0, $size[0], $size[1], $SWP_NOMOVE_NOZORDER)
    Start-Sleep -Seconds $StepSeconds
    if ($proc.HasExited) { throw "app exited after resizing to $($size -join 'x') with code $($proc.ExitCode); see $stderrFile" }
}
[void][AlphaResizeWin32]::ShowWindow($hwnd, $SW_MINIMIZE)
Start-Sleep -Seconds $StepSeconds
[void][AlphaResizeWin32]::ShowWindow($hwnd, $SW_RESTORE)
Start-Sleep -Seconds $SettleSeconds
if ($proc.HasExited) { throw "app exited after minimise/restore with code $($proc.ExitCode); see $stderrFile" }

# Close the window like a user would (WM_CLOSE -> glfwWindowShouldClose) so every destructor and vkDestroyDevice run.
[void]$proc.CloseMainWindow()
if (-not $proc.WaitForExit($ShutdownSeconds * 1000)) {
    Stop-Process -Id $proc.Id -Force
    throw "app did not shut down within $ShutdownSeconds s of closing its window; see $stderrFile"
}

$rebuilds = @(Select-String -Path $stdoutFile -Pattern $marker -SimpleMatch).Count - 1
$layerLines = @(Select-String -Path $stderrFile -Pattern "validation layer:" -SimpleMatch)
$found = @($layerLines | ForEach-Object { [regex]::Matches($_.Line, "VUID-[A-Za-z0-9_-]+") } | ForEach-Object Value | Sort-Object -Unique)
$baseline = @(Get-Content "scripts/validation_baseline.txt" | Where-Object { $_ -and -not $_.StartsWith("#") } | Sort-Object -Unique)
$diff = Compare-Object -ReferenceObject $baseline -DifferenceObject $found

$failed = $false
if ($proc.ExitCode -ne 0) {
    Write-Host "resize check FAILED: app exited with code $($proc.ExitCode) after closing its window; see $stderrFile"
    $failed = $true
}
if ($rebuilds -lt $sizes.Count) {
    Write-Host "resize check FAILED: RenderingResources rebuilt $rebuilds times for $($sizes.Count) size changes; see $stdoutFile"
    $failed = $true
}
if ($diff) {
    $diff | ForEach-Object {
        $side = if ($_.SideIndicator -eq "=>") { "NEW    " } else { "MISSING" }
        Write-Host "$side $($_.InputObject)"
    }
    Write-Host "resize check FAILED: VUIDs differ from the baseline; full output in $stderrFile"
    $failed = $true
}
if ($failed) { exit 1 }
Write-Host "resize OK: $rebuilds RenderingResources rebuilds, app alive after minimise/restore, clean shutdown (exit 0), $($found.Count) VUIDs (baseline $($baseline.Count))"
