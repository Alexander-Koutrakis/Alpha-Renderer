# Prove the build works from a fresh clone: clone a ref of the remote (default: origin's main) into a new folder, then
# configure, build, compile the shaders and run the unit tests there, with nothing from this checkout.
#
#   ./scripts/cold_clone_check.ps1
#   ./scripts/cold_clone_check.ps1 -Ref some_branch
#   ./scripts/cold_clone_check.ps1 -Source D:/repos/other-clone     clone from a path instead of origin
#
# The clone has only what git tracks, so it fails if the build needs an untracked or git-ignored file (the *.md docs,
# .claude/, a prebuilt build/). It uses the debug preset (-Werror) and the same shader and test steps as `just check`.
# The clone is left in build/cold-clone/<timestamp> (git-ignored) so a failure can be looked at; delete it by hand.
# Needs the toolchain from AGENTS.md and network access to the remote; not part of `just check`.
param(
    [string]$Source = "",
    [string]$Ref = "main"
)
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")

if ($Source -eq "") {
    $Source = (git remote get-url origin).Trim()
    if ($LASTEXITCODE -ne 0 -or $Source -eq "") { throw "no origin remote; pass -Source" }
}
$dir = Join-Path "build/cold-clone" (Get-Date -Format "yyyyMMdd-HHmmss")
New-Item -ItemType Directory -Path "build/cold-clone" -Force | Out-Null

function Invoke-Step([string]$name, [scriptblock]$body) {
    Write-Host "== $name"
    & $body
    if ($LASTEXITCODE -ne 0) { throw "cold clone: '$name' failed (exit $LASTEXITCODE); clone kept in $dir" }
}

Invoke-Step "clone $Ref from $Source" { git clone --quiet --branch $Ref --single-branch $Source $dir }
$commit = (git -C $dir rev-parse --short HEAD).Trim()
Write-Host "cloned $commit into $dir"
Set-Location $dir

Invoke-Step "configure (debug preset)" { cmake --preset debug }
Invoke-Step "build" { cmake --build --preset debug }
Invoke-Step "shaders" { ./scripts/check_shaders.ps1 }
Invoke-Step "block layouts" { ./scripts/dump_block_layouts.ps1 }
Invoke-Step "unit tests" { ctest --test-dir build --output-on-failure }

# The exe finds shaders and assets next to itself, so they must have been built or copied there.
$spvCount = @(Get-ChildItem build/shaders -Filter *.spv -ErrorAction SilentlyContinue).Count
$missing = @("build/AlphaRenderer.exe", "build/Assets/Scene/Scene.json") | Where-Object { -not (Test-Path $_) }
if ($missing -or $spvCount -eq 0) {
    throw "cold clone: missing next to the exe: $($missing -join ', ') (compiled shaders: $spvCount); clone kept in $dir"
}
Write-Host "cold clone OK: $commit builds, $spvCount shaders compiled, tests pass (clone in $dir)"
