# Run clang-tidy over every translation unit in src/ and fail on findings from our enabled checks
# (see .clang-tidy). Clang-only front-end diagnostics (clang-diagnostic-*) are reported but do not
# fail the gate: the project builds with GCC and a few diagnostics differ between compilers.
# Needs build/compile_commands.json, produced by `just build`.
$ErrorActionPreference = "Continue" # native stderr is not an error; failures are thrown explicitly below
Set-Location (Join-Path $PSScriptRoot "..")

$files = Get-ChildItem src -Recurse -Filter *.cpp | ForEach-Object { $_.FullName -replace "\\", "/" } # not git ls-files: a new untracked file must not be skipped
$failed = @()
$frontEnd = @()
foreach ($f in $files) {
    $out = clang-tidy -p build --quiet --extra-arg=--target=x86_64-w64-mingw32 --extra-arg=-IC:/msys64/mingw64/include $f 2>&1 | Out-String
    $ours = ($out -split "`n") | Where-Object { $_ -match "\[cppcoreguidelines-" -and $_ -match "src[\\/]" }
    if ($ours) { $failed += $ours }
    $frontEnd += ($out -split "`n") | Where-Object { $_ -match "\[clang-diagnostic-" -and $_ -match "src[\\/]" }
}
$frontEnd = $frontEnd | Sort-Object -Unique
if ($frontEnd) { Write-Host "clang-only diagnostics (not gated): $($frontEnd.Count)" }
if ($failed) {
    $failed | Sort-Object -Unique | ForEach-Object { Write-Host $_ }
    throw "clang-tidy: $(($failed | Sort-Object -Unique).Count) finding(s)"
}
Write-Host "clang-tidy OK: $($files.Count) files, 0 findings"
