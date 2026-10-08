# Compile every GLSL shader with glslc and validate the SPIR-V with spirv-val.
# Fails on the first error. Output goes to build/shader-check (git-ignored).
$ErrorActionPreference = "Stop"

$sdk = $env:VULKAN_SDK
if (-not $sdk) { throw "VULKAN_SDK is not set" }
$glslc = Join-Path $sdk "Bin/glslc.exe"
$val = Join-Path $sdk "Bin/spirv-val.exe"
$out = Join-Path $PSScriptRoot "../build/shader-check"
New-Item -ItemType Directory -Force $out | Out-Null

$shaderDir = Join-Path $PSScriptRoot "../shaders"
$shaders = Get-ChildItem $shaderDir -Recurse -Include *.vert, *.frag, *.comp
if ($shaders.Count -eq 0) { throw "no shaders found" }

foreach ($s in $shaders) {
    $spv = Join-Path $out ($s.Name + ".spv")
    & $glslc --target-env=vulkan1.3 -Werror -I (Join-Path $shaderDir "common") $s.FullName -o $spv
    if ($LASTEXITCODE -ne 0) { throw "glslc failed: $($s.Name)" }
    & $val --target-env vulkan1.3 $spv
    if ($LASTEXITCODE -ne 0) { throw "spirv-val failed: $($s.Name)" }
}
Write-Host "shaders OK: $($shaders.Count) compiled and validated"
