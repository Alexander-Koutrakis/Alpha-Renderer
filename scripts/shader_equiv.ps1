# Compare every shader at a git ref against the working tree. Each shader is compiled with glslc, canonicalised with
# spirv-opt -O, disassembled, and reduced to its sorted instruction lines with all ids renamed to "%". Identical output
# means the same set of operations, constants and types: a refactor that only moves code is reported as unchanged.
# This ignores instruction order, so it proves "nothing added, removed or altered", not "same schedule".
# Usage: ./scripts/shader_equiv.ps1 [-BaseRef main] [-Expect shader1.frag,shader2.comp]
# Shaders listed in -Expect must differ and have their diff printed; any other difference fails.
param(
    [string]$BaseRef = "main",
    [string[]]$Expect = @()
)
$ErrorActionPreference = "Stop"

$sdk = $env:VULKAN_SDK
if (-not $sdk) { throw "VULKAN_SDK is not set" }
$glslc = Join-Path $sdk "Bin/glslc.exe"
$opt = Join-Path $sdk "Bin/spirv-opt.exe"
$dis = Join-Path $sdk "Bin/spirv-dis.exe"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$work = Join-Path $root "build/shader-equiv"
if (Test-Path $work) { Remove-Item $work -Recurse -Force }
$baseDir = Join-Path $work "base"
New-Item -ItemType Directory -Force $baseDir | Out-Null

# Extract the base ref's shaders without touching the working tree.
$zip = Join-Path $work "base.zip"
git -C $root archive --format=zip -o $zip $BaseRef shaders
if ($LASTEXITCODE -ne 0) { throw "could not extract shaders at $BaseRef" }
Expand-Archive $zip -DestinationPath $baseDir

function Get-Canonical($shaderRoot, $file, $tag) {
    $spv = Join-Path $work "$tag.$($file.Name).spv"
    $optSpv = Join-Path $work "$tag.$($file.Name).opt.spv"
    & $glslc --target-env=vulkan1.3 -I (Join-Path $shaderRoot "shaders/common") $file.FullName -o $spv
    if ($LASTEXITCODE -ne 0) { throw "glslc failed ($tag): $($file.Name)" }
    & $opt -O --target-env=vulkan1.3 $spv -o $optSpv
    if ($LASTEXITCODE -ne 0) { throw "spirv-opt failed ($tag): $($file.Name)" }
    & $dis --no-header $optSpv |
        ForEach-Object { ($_ -replace '%[0-9A-Za-z_]+', '%').Trim() -replace '\s+', ' ' } |
        Sort-Object
}

$shaders = Get-ChildItem (Join-Path $root "shaders") -Recurse -Include *.vert, *.frag, *.comp
$changed = @()
foreach ($s in $shaders) {
    $baseFile = Get-Item (Join-Path $baseDir "shaders/$($s.Name)")
    $a = @(Get-Canonical $baseDir $baseFile "base")
    $b = @(Get-Canonical $root $s "head")
    $diff = Compare-Object $a $b
    if ($diff) {
        $changed += $s.Name
        if ($Expect -contains $s.Name) {
            Write-Host "--- $($s.Name): <= only in $BaseRef, => only in working tree"
            $diff | ForEach-Object { Write-Host "$($_.SideIndicator) $($_.InputObject)" }
        }
    }
}

$unexpected = $changed | Where-Object { $Expect -notcontains $_ }
$expectedButSame = $Expect | Where-Object { $changed -notcontains $_ }
Write-Host "compared $($shaders.Count) shaders against $BaseRef"
if ($changed.Count -gt 0) { Write-Host "changed: $($changed -join ', ')" }
if ($expectedButSame.Count -gt 0) { throw "expected to change but identical: $($expectedButSame -join ', ')" }
if ($unexpected.Count -gt 0) { throw "unexpected changes: $($unexpected -join ', ')" }
Write-Host "shader equivalence OK"
