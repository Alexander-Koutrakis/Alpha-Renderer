# Where does the Radiance Cascades GI lose light? Runs scripts/rc_compare.ps1 with the pass limited to cascades 0..k and the
# reference limited to the distance they reach, for k = 0..5, and prints per distance band how much light each side adds.
#
#   ./scripts/rc_bands.ps1                  sample scene
#   ./scripts/rc_bands.ps1 -Scene glass
#
# Reach of cascade k: 0.32, 1.28, 5.12, 20.48, 81.92, 327.68 m (4^(k+1) * RC_BASE_INTERVAL_LENGTH).
# "added" is the mean GI with cascades 0..k minus the mean with 0..k-1 (display units, see rc_compare.ps1). A band where the
# cascades add much less than the reference is where light is lost: near bands point at tracing near the surface and the
# resolve, far bands at the far cascades' tracer and merging. 13 app runs, about 8 minutes; needs a GPU and an untouched screen.
param(
    [string]$Scene = "",
    [string]$BuildDir = "build",
    [ValidateRange(1, 300)][int]$SettleFrames = 100
)
$ErrorActionPreference = "Stop"
$reach = @(0.32, 1.28, 5.12, 20.48, 81.92, 327.68)
$rows = @()
for ($k = 0; $k -le 5; $k++) {
    $scenArgs = if ($Scene -eq "") { @() } else { @("-Scene", $Scene) }
    $out = & (Join-Path $PSScriptRoot "rc_compare.ps1") -MaxCascade $k -BuildDir $BuildDir -SettleFrames $SettleFrames @scenArgs
    if ($LASTEXITCODE -ne 0 -and $null -ne $LASTEXITCODE) { throw "rc_compare failed for k=$k" }
    $line = $out | Where-Object { $_ -is [string] -and $_ -like "RESULT*" } | Select-Object -Last 1
    if (-not $line) { throw "no RESULT line for k=$k" }
    $f = @{}
    foreach ($m in [regex]::Matches($line, '(\w+)=(-?[\d\.]+)')) { $f[$m.Groups[1].Value] = [double]$m.Groups[2].Value }
    $rows += [pscustomobject]@{ K = $k; Reach = $reach[$k]; Casc = $f["meanCascades"]; Ref = $f["meanReference"]; Err = $f["meanAbsError"]; Corr = $f["correlation"] }
}
Write-Host ""
Write-Host "cascades 0..k versus reference limited to the same distance (mean GI, display units)"
Write-Host "  k   reach m   cascades  reference   ratio   |err|/ref  corr"
foreach ($r in $rows) {
    $ratio = if ($r.Ref -gt 0) { $r.Casc / $r.Ref } else { 0 }
    $rel = if ($r.Ref -gt 0) { $r.Err / $r.Ref } else { 0 }
    Write-Host ("  {0}  {1,8}  {2,9:N3}  {3,9:N3}  {4,6:N3}  {5,8:P1}  {6,5:N3}" -f $r.K, $r.Reach, $r.Casc, $r.Ref, $ratio, $rel, $r.Corr)
}
Write-Host ""
Write-Host "light added by each distance band"
Write-Host "  band m              cascades  reference   cascades/reference"
$prevC = 0.0; $prevR = 0.0; $prevReach = 0.0
foreach ($r in $rows) {
    $dc = $r.Casc - $prevC; $dr = $r.Ref - $prevR
    $q = if ([math]::Abs($dr) -gt 0.01) { "{0,6:N2}" -f ($dc / $dr) } else { "     -" }
    Write-Host ("  {0,6} - {1,-8}   {2,9:N3}  {3,9:N3}   {4}" -f $prevReach, $r.Reach, $dc, $dr, $q)
    $prevC = $r.Casc; $prevR = $r.Ref; $prevReach = $r.Reach
}
