# Dump the memory layout of every uniform block and push-constant block in the compiled shaders, for
# tests/shader_layout_test.cpp (which compares it with the C++ structs that fill those blocks).
# Reads build/shader-check/*.spv (produced by scripts/check_shaders.ps1) through `spirv-cross --reflect`
# and writes build/shader-check/block_layouts.txt, one line per fact:
#   block <shader> <BlockName> <ubo|push|struct> <size in bytes>
#   member <name> <offset> <array stride> <array length>   (stride and length are 0 if not an array)
# `struct` blocks are structs used as array elements of a uniform block (their size is the array stride).
# spirv-cross lists only blocks a shader actually uses, so an unused block is not dumped.
$ErrorActionPreference = "Stop"

$sdk = $env:VULKAN_SDK
if (-not $sdk) { throw "VULKAN_SDK is not set" }
$cross = Join-Path $sdk "Bin/spirv-cross.exe"
$dir = Join-Path $PSScriptRoot "../build/shader-check"
$out = Join-Path $dir "block_layouts.txt"

# Size in bytes of the scalar, vector and matrix types push-constant members can have (spirv-cross reports no
# push-constant block size). Anything else is an error: add it here rather than guessing.
function Get-TypeSize([string]$type) {
    switch -Regex ($type) {
        '^(int|uint|float)$' { return 4 }
        '^[iu]?vec([234])$' { return 4 * [int]$Matches[1] }
        '^mat([234])$' { return 16 * [int]$Matches[1] }
        default { throw "dump_block_layouts: no size known for type '$type'" }
    }
}

function Add-Members($lines, $members) {
    foreach ($m in $members) {
        if ($null -eq $m.offset) { throw "dump_block_layouts: member '$($m.name)' has no offset" }
        $stride = 0
        $count = 0
        if ($m.array) {
            if (@($m.array).Count -ne 1) { throw "dump_block_layouts: member '$($m.name)' is a multi-dimensional array" }
            $stride = $m.array_stride
            $count = @($m.array)[0]
        }
        $lines.Add("member $($m.name) $($m.offset) $stride $count")
    }
}

$lines = New-Object System.Collections.Generic.List[string]
$spvs = Get-ChildItem $dir -Filter *.spv | Sort-Object Name
if ($spvs.Count -eq 0) { throw "no .spv files in $dir (run scripts/check_shaders.ps1 first)" }

foreach ($spv in $spvs) {
    $shader = $spv.BaseName
    $json = (& $cross $spv.FullName --reflect | Out-String)
    if ($LASTEXITCODE -ne 0) { throw "spirv-cross failed: $($spv.Name)" }
    $reflect = $json | ConvertFrom-Json
    $types = $reflect.types

    $blocks = @()
    foreach ($b in @($reflect.ubos)) { if ($b) { $blocks += [pscustomobject]@{ Kind = "ubo"; Block = $b } } }
    foreach ($b in @($reflect.push_constants)) { if ($b) { $blocks += [pscustomobject]@{ Kind = "push"; Block = $b } } }

    foreach ($entry in $blocks) {
        $block = $entry.Block
        $members = @($types.($block.type).members)
        if ($entry.Kind -eq "ubo") {
            $size = $block.block_size
        } else {
            $last = $members[-1]
            if ($last.array) { throw "dump_block_layouts: push constant $($block.name) ends in an array" }
            $size = $last.offset + (Get-TypeSize $last.type)
        }
        $lines.Add("block $shader $($block.name) $($entry.Kind) $size")
        Add-Members $lines $members

        # A struct used as an array element: dump its layout too, sized by the array stride.
        foreach ($m in $members) {
            if ($m.array -and $types.PSObject.Properties[$m.type]) {
                $struct = $types.($m.type)
                $lines.Add("block $shader $($struct.name) struct $($m.array_stride)")
                Add-Members $lines $struct.members
            }
        }
    }
}

# LF line endings so the C++ test and diffs behave the same everywhere.
[System.IO.File]::WriteAllText($out, (($lines -join "`n") + "`n"))
Write-Host "block layouts: $(@($lines | Where-Object { $_ -like 'block *' }).Count) blocks from $($spvs.Count) shaders -> $out"
