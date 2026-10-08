# Run the app, capture its window and compare it with a golden image.
#
#   ./scripts/screenshot_diff.ps1                        compare build/AlphaRenderer.exe against tests/golden/scene.png
#   ./scripts/screenshot_diff.ps1 -Update                write the golden image from this build
#   ./scripts/screenshot_diff.ps1 -Scene glass           run tests/scenes/glass instead, compare with tests/golden/glass.png
#   ./scripts/screenshot_diff.ps1 -BuildDir C:\path\to\other\build
#
# The scene has a fixed start camera, so a run is comparable with the golden image. The golden image is specific to
# the GPU, driver and resolution it was made on; regenerate it with -Update on a build you trust (for example main).
# -Scene <name> overlays the files of tests/scenes/<name>/ (Scene.json and extra materials, same layout as Assets/Scene)
# onto the build's Assets/Scene for the run and restores Scene.json afterwards, so one build can render several scenes.
# Needs a GPU and a display; it is not part of `just check`.
#
# Writes <BuildDir>/screenshot-diff/actual.png and diff.png (absolute difference, x4 for visibility). A pixel counts as
# different when any channel differs by more than -PixelTolerance (0-255). The check fails when the fraction of
# different pixels exceeds -MaxBadFraction or the mean absolute difference exceeds -MaxMeanDiff. Measured on the dev
# machine (RTX 3070 Laptop, 1920x1080): the same build run against its own golden image gives a mean difference of
# 0.00-0.10 and no different pixels, while a build with the GI sky flooding bug gives 80 and 100%. The defaults sit
# roughly 10x above the noise.
param(
    [string]$BuildDir = "build",
    [string]$Baseline = "",
    [string]$Scene = "",
    [switch]$Update,
    [int]$SettleSeconds = 14,
    [int]$TimeoutSeconds = 120,
    [int]$PixelTolerance = 24,
    [double]$MaxBadFraction = 0.005,
    [double]$MaxMeanDiff = 1.0
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

public static class ShotTool {
    [StructLayout(LayoutKind.Sequential)] struct POINT { public int X, Y; }
    [StructLayout(LayoutKind.Sequential)] struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] static extern bool ClientToScreen(IntPtr h, ref POINT p);
    [DllImport("user32.dll")] static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);

    // Bring the window to the top (so other windows cannot cover it) and copy its client area.
    public static Bitmap CaptureClient(IntPtr hwnd) {
        SetWindowPos(hwnd, new IntPtr(-1), 0, 0, 0, 0, 0x0001 | 0x0002 | 0x0040); // TOPMOST, NOSIZE|NOMOVE|SHOWWINDOW
        SetForegroundWindow(hwnd);
        System.Threading.Thread.Sleep(1500);
        RECT r; GetClientRect(hwnd, out r);
        POINT p = new POINT(); ClientToScreen(hwnd, ref p);
        int w = r.Right - r.Left, h = r.Bottom - r.Top;
        Bitmap bmp = new Bitmap(w, h, PixelFormat.Format32bppArgb);
        using (Graphics g = Graphics.FromImage(bmp)) g.CopyFromScreen(p.X, p.Y, 0, 0, new Size(w, h));
        return bmp;
    }

    // Compares two same-sized images outside the ignored rectangle. Returns {badFraction, meanAbsDiff, compared}
    // and writes an amplified absolute-difference image.
    public static double[] Compare(Bitmap a, Bitmap b, int tol, Rectangle ignore, string diffPath) {
        int w = a.Width, h = a.Height;
        Rectangle all = new Rectangle(0, 0, w, h);
        BitmapData da = a.LockBits(all, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
        BitmapData db = b.LockBits(all, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
        Bitmap diff = new Bitmap(w, h, PixelFormat.Format32bppArgb);
        BitmapData dd = diff.LockBits(all, ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
        long bad = 0, compared = 0; double sum = 0;
        CompareBuffers(da, db, dd, w, h, tol, ignore, ref bad, ref compared, ref sum);
        a.UnlockBits(da); b.UnlockBits(db); diff.UnlockBits(dd);
        diff.Save(diffPath, ImageFormat.Png);
        diff.Dispose();
        return new double[] { compared == 0 ? 0 : (double)bad / compared, compared == 0 ? 0 : sum / (compared * 3.0), compared };
    }

    static void CompareBuffers(BitmapData da, BitmapData db, BitmapData dd, int w, int h, int tol, Rectangle ignore,
                            ref long bad, ref long compared, ref double sum) {
        byte[] ra = new byte[da.Stride * h], rb = new byte[db.Stride * h], rd = new byte[dd.Stride * h];
        Marshal.Copy(da.Scan0, ra, 0, ra.Length);
        Marshal.Copy(db.Scan0, rb, 0, rb.Length);
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                int i = y * da.Stride + x * 4, o = y * dd.Stride + x * 4;
                rd[o + 3] = 255;
                if (ignore.Contains(x, y)) continue;
                int d0 = Math.Abs(ra[i] - rb[i]), d1 = Math.Abs(ra[i + 1] - rb[i + 1]), d2 = Math.Abs(ra[i + 2] - rb[i + 2]);
                rd[o] = (byte)Math.Min(255, d0 * 4); rd[o + 1] = (byte)Math.Min(255, d1 * 4); rd[o + 2] = (byte)Math.Min(255, d2 * 4);
                compared++; sum += d0 + d1 + d2;
                if (Math.Max(d0, Math.Max(d1, d2)) > tol) bad++;
            }
        }
        Marshal.Copy(rd, 0, dd.Scan0, rd.Length);
    }
}
"@

if ($Baseline -eq "") { $Baseline = if ($Scene -eq "") { "tests/golden/scene.png" } else { "tests/golden/$Scene.png" } }
$buildPath = Resolve-Path $BuildDir
$exe = Join-Path $buildPath "AlphaRenderer.exe"
if (-not (Test-Path $exe)) { throw "no AlphaRenderer.exe in $buildPath (run 'just build' first)" }
$outDir = Join-Path $buildPath "screenshot-diff"
New-Item -ItemType Directory -Force $outDir | Out-Null

# The exe loads libstdc++ and friends from msys64; Git's older copies on PATH make it exit with 0xC0000139.
$env:PATH = "C:\msys64\mingw64\bin;" + $env:PATH
$stdout = Join-Path $outDir "run.stdout.txt"
$stderr = Join-Path $outDir "run.stderr.txt"
$buildScene = Join-Path $buildPath "Assets/Scene"
if ($Scene -ne "") {
    $sceneSource = Join-Path $root "tests/scenes/$Scene"
    if (-not (Test-Path (Join-Path $sceneSource "Scene.json"))) { throw "no Scene.json in $sceneSource" }
    Copy-Item (Join-Path $sceneSource "*") $buildScene -Recurse -Force
}
$proc = Start-Process -FilePath $exe -WorkingDirectory $outDir -RedirectStandardOutput $stdout `
    -RedirectStandardError $stderr -PassThru
try {
    $marker = "RenderingResources created with"
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        if ($proc.HasExited) { $proc.WaitForExit(); throw "app exited early with code $($proc.ExitCode); see $stderr" }
        if ((Test-Path $stdout) -and (Select-String -Path $stdout -Pattern $marker -Quiet)) { break }
        Start-Sleep -Milliseconds 500
    }
    if ((Get-Date) -ge $deadline) { throw "timed out waiting for '$marker'" }
    Start-Sleep -Seconds $SettleSeconds
    if ($proc.HasExited) {
        $proc.WaitForExit()
        throw "app exited during the run with code $($proc.ExitCode); see $stderr (code 0 and empty stderr: the window was closed from outside, so do not type into or close windows while this runs)"
    }

    $proc.Refresh()
    $hwnd = $proc.MainWindowHandle
    for ($i = 0; $i -lt 20 -and $hwnd -eq [IntPtr]::Zero; $i++) { Start-Sleep -Milliseconds 500; $proc.Refresh(); $hwnd = $proc.MainWindowHandle }
    if ($hwnd -eq [IntPtr]::Zero) { throw "app has no window handle" }
    $actual = [ShotTool]::CaptureClient($hwnd)
}
finally {
    if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force }
    if ($Scene -ne "") { Copy-Item (Join-Path $root "Assets/Scene/Scene.json") $buildScene -Force }
}

$actualPath = Join-Path $outDir "actual.png"
$actual.Save($actualPath, [System.Drawing.Imaging.ImageFormat]::Png)
Write-Host "captured $($actual.Width)x$($actual.Height) -> $actualPath"

$baselinePath = Join-Path $root $Baseline
if ($Update) {
    New-Item -ItemType Directory -Force (Split-Path $baselinePath) | Out-Null
    Copy-Item $actualPath $baselinePath -Force
    Write-Host "golden image written: $baselinePath"
    exit 0
}
if (-not (Test-Path $baselinePath)) { throw "no golden image at $baselinePath (create it with -Update)" }

$golden = New-Object System.Drawing.Bitmap $baselinePath
if ($golden.Width -ne $actual.Width -or $golden.Height -ne $actual.Height) {
    throw "size differs: golden $($golden.Width)x$($golden.Height), actual $($actual.Width)x$($actual.Height)"
}
# The FPS / frame-time overlay in the bottom-left corner changes every frame.
$ignore = New-Object System.Drawing.Rectangle 0, ($actual.Height - 90), 220, 90
$diffPath = Join-Path $outDir "diff.png"
$r = [ShotTool]::Compare($golden, $actual, $PixelTolerance, $ignore, $diffPath)
$bad = $r[0]; $mean = $r[1]
Write-Host ("different pixels: {0:P3} (limit {1:P1}, tolerance {2}/255)   mean abs diff: {3:N3} (limit {4})" -f $bad, $MaxBadFraction, $PixelTolerance, $mean, $MaxMeanDiff)
Write-Host "diff image: $diffPath"
if ($bad -gt $MaxBadFraction -or $mean -gt $MaxMeanDiff) {
    Write-Host "screenshot check FAILED"
    exit 1
}
Write-Host "screenshot check OK"
exit 0
