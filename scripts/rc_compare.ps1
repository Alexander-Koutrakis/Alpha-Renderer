# Compare the Radiance Cascades GI with the brute-force reference (shaders/rc_reference.comp).
#
#   ./scripts/rc_compare.ps1                  sample scene
#   ./scripts/rc_compare.ps1 -Scene glass     tests/scenes/glass
#
# Runs the app three times through screenshot_diff.ps1 -Update, with ALPHA_RC_MODE = off, cascades and reference, and
# stores the captures in build/rc-compare/<scene>/. The GI each image contributes is (image - off). It then reports,
# over the screen except the FPS overlay:
#   - mean GI of the cascades and of the reference (display units, 0-255, averaged over the colour channels);
#   - bias = cascades GI / reference GI (1.0 = same total light);
#   - mean absolute error |cascades - reference| and that error as a fraction of the reference GI;
#   - correlation of the two GI images.
# and writes diff.png (red = cascades brighter than the reference, blue = darker, x4).
# The captures are the composited, tone-mapped window, so the numbers are in display units, not linear radiance: read
# them as "direction and size of the error", not as exact photometry. The reference averages about 64 samples per pixel
# over time (a few percent noise), so differences below roughly 5% are not significant.
# Needs a GPU and a display, and nothing else on screen while it runs; not part of `just check`.
param(
    [string]$Scene = "",
    [string]$BuildDir = "build",
    [int]$SettleSeconds = 20
)
$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $root

Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

public static class GiStats {
    static byte[] Read(Bitmap b, out int stride) {
        BitmapData d = b.LockBits(new Rectangle(0, 0, b.Width, b.Height), ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
        byte[] raw = new byte[d.Stride * b.Height];
        Marshal.Copy(d.Scan0, raw, 0, raw.Length);
        stride = d.Stride;
        b.UnlockBits(d);
        return raw;
    }

    // Returns { meanRc, meanRef, meanAbsErr, correlation, compared }.
    public static double[] Compare(Bitmap off, Bitmap rc, Bitmap rf, Rectangle ignore, string diffPath) {
        int w = off.Width, h = off.Height, sOff, sRc, sRf;
        byte[] a = Read(off, out sOff), b = Read(rc, out sRc), c = Read(rf, out sRf);
        Bitmap diff = new Bitmap(w, h, PixelFormat.Format32bppArgb);
        BitmapData dd = diff.LockBits(new Rectangle(0, 0, w, h), ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
        byte[] rd = new byte[dd.Stride * h];
        double n = 0, sumRc = 0, sumRf = 0, sumAbs = 0, sumRcRc = 0, sumRfRf = 0, sumRcRf = 0;
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                int o = y * dd.Stride + x * 4;
                rd[o + 3] = 255;
                if (ignore.Contains(x, y)) continue;
                double gRc = 0, gRf = 0;
                for (int k = 0; k < 3; k++) {
                    gRc += b[y * sRc + x * 4 + k] - a[y * sOff + x * 4 + k];
                    gRf += c[y * sRf + x * 4 + k] - a[y * sOff + x * 4 + k];
                }
                gRc /= 3.0; gRf /= 3.0;
                n++; sumRc += gRc; sumRf += gRf; sumAbs += Math.Abs(gRc - gRf);
                sumRcRc += gRc * gRc; sumRfRf += gRf * gRf; sumRcRf += gRc * gRf;
                double d = gRc - gRf;
                if (d > 0) rd[o + 2] = (byte)Math.Min(255, d * 4); else rd[o] = (byte)Math.Min(255, -d * 4);
            }
        }
        Marshal.Copy(rd, 0, dd.Scan0, rd.Length);
        diff.UnlockBits(dd);
        diff.Save(diffPath, ImageFormat.Png);
        diff.Dispose();
        double mRc = sumRc / n, mRf = sumRf / n;
        double cov = sumRcRf / n - mRc * mRf;
        double vRc = sumRcRc / n - mRc * mRc, vRf = sumRfRf / n - mRf * mRf;
        double corr = (vRc > 0 && vRf > 0) ? cov / Math.Sqrt(vRc * vRf) : 0;
        return new double[] { mRc, mRf, sumAbs / n, corr, n };
    }
}
"@

$name = if ($Scene -eq "") { "scene" } else { $Scene }
$outDir = Join-Path $root "build/rc-compare/$name"
New-Item -ItemType Directory -Force $outDir | Out-Null
$sceneArgs = if ($Scene -eq "") { @() } else { @("-Scene", $Scene) }

$old = $env:ALPHA_RC_MODE
try {
    foreach ($mode in @("off", "cascades", "reference")) {
        $env:ALPHA_RC_MODE = $mode
        Write-Host "== ALPHA_RC_MODE=$mode"
        & (Join-Path $PSScriptRoot "screenshot_diff.ps1") -BuildDir $BuildDir -Update -SettleSeconds $SettleSeconds `
            -Baseline "build/rc-compare/$name/$mode.png" @sceneArgs
        if ($LASTEXITCODE -ne 0) { throw "capture failed for mode $mode" }
    }
}
finally {
    if ($null -eq $old) { Remove-Item Env:ALPHA_RC_MODE -ErrorAction SilentlyContinue } else { $env:ALPHA_RC_MODE = $old }
}

$off = New-Object System.Drawing.Bitmap (Join-Path $outDir "off.png")
$rc = New-Object System.Drawing.Bitmap (Join-Path $outDir "cascades.png")
$rf = New-Object System.Drawing.Bitmap (Join-Path $outDir "reference.png")
$ignore = New-Object System.Drawing.Rectangle 0, ($off.Height - 90), 220, 90   # FPS overlay
$r = [GiStats]::Compare($off, $rc, $rf, $ignore, (Join-Path $outDir "diff.png"))
Write-Host ""
Write-Host ("scene: {0}   pixels compared: {1}" -f $name, $r[4])
Write-Host ("mean GI, cascades : {0:N3}" -f $r[0])
Write-Host ("mean GI, reference: {0:N3}" -f $r[1])
if ($r[1] -gt 0) {
    Write-Host ("bias (cascades / reference): {0:N3}" -f ($r[0] / $r[1]))
    Write-Host ("mean abs error: {0:N3} = {1:P1} of the reference GI" -f $r[2], ($r[2] / $r[1]))
}
Write-Host ("correlation: {0:N3}" -f $r[3])
Write-Host "images: $outDir"
