# Draws installer\installer.ico (16-256 px, PNG frames): dark leather tile with a gold rim, a parchment disc with
# a dark red "R" and a gold download badge. Own art, nothing taken from the game.
# Usage: powershell -File tools\make_installer_icon.ps1 [-Preview <png>]
param([string]$Preview)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$root = Split-Path $PSScriptRoot -Parent
$out = Join-Path $root 'installer\installer.ico'

function Color($hex, $a = 255) {
    [System.Drawing.Color]::FromArgb($a, [Convert]::ToInt32($hex.Substring(0, 2), 16),
        [Convert]::ToInt32($hex.Substring(2, 2), 16), [Convert]::ToInt32($hex.Substring(4, 2), 16))
}

function RoundRect($x, $y, $w, $h, $r) {
    $p = New-Object System.Drawing.Drawing2D.GraphicsPath
    $p.AddArc($x, $y, 2 * $r, 2 * $r, 180, 90)
    $p.AddArc($x + $w - 2 * $r, $y, 2 * $r, 2 * $r, 270, 90)
    $p.AddArc($x + $w - 2 * $r, $y + $h - 2 * $r, 2 * $r, 2 * $r, 0, 90)
    $p.AddArc($x, $y + $h - 2 * $r, 2 * $r, 2 * $r, 90, 90)
    $p.CloseFigure()
    $p
}

function Draw([int]$size) {
    $bmp = New-Object System.Drawing.Bitmap $size, $size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'
    $g.TextRenderingHint = 'AntiAliasGridFit'
    $g.InterpolationMode = 'HighQualityBicubic'
    $k = $size / 256.0

    # Leather tile with a gold rim.
    $tile = RoundRect (6 * $k) (6 * $k) (244 * $k) (244 * $k) (40 * $k)
    $grad = New-Object System.Drawing.Drawing2D.LinearGradientBrush ([System.Drawing.PointF]::new(0, 0)),
        ([System.Drawing.PointF]::new(0, $size)), (Color '4a2f1a'), (Color '1c120a')
    $g.FillPath($grad, $tile)
    $rim = New-Object System.Drawing.Pen (Color 'c9a14a'), ([Math]::Max(1.0, 9 * $k))
    $g.DrawPath($rim, $tile)

    # Parchment disc.
    $d = 176 * $k; $dx = (256 * $k - $d) / 2; $dy = 26 * $k
    $disc = New-Object System.Drawing.Drawing2D.GraphicsPath
    $disc.AddEllipse($dx, $dy, $d, $d)
    $pb = New-Object System.Drawing.Drawing2D.PathGradientBrush $disc
    $pb.CenterColor = Color 'f1e2bb'
    $pb.SurroundColors = @(Color 'c9a873')
    $g.FillPath($pb, $disc)
    $g.DrawEllipse((New-Object System.Drawing.Pen (Color '6b4422'), ([Math]::Max(1.0, 5 * $k))), $dx, $dy, $d, $d)

    # "R" in a serif face.
    $font = New-Object System.Drawing.Font 'Georgia', ([float](130 * $k)), ([System.Drawing.FontStyle]::Bold), ([System.Drawing.GraphicsUnit]::Pixel)
    $fmt = New-Object System.Drawing.StringFormat
    $fmt.Alignment = 'Center'; $fmt.LineAlignment = 'Center'
    $g.DrawString('R', $font, (New-Object System.Drawing.SolidBrush (Color '7a1a0c')),
        (New-Object System.Drawing.RectangleF $dx, ($dy + 6 * $k), $d, $d), $fmt)

    # Gold download badge, bottom right.
    $b = 96 * $k; $bx = 256 * $k - $b - 12 * $k; $by = 256 * $k - $b - 12 * $k
    $g.FillEllipse((New-Object System.Drawing.SolidBrush (Color 'd9b04f')), $bx, $by, $b, $b)
    $g.DrawEllipse((New-Object System.Drawing.Pen (Color '3a240f'), ([Math]::Max(1.0, 6 * $k))), $bx, $by, $b, $b)
    $cx = $bx + $b / 2; $cy = $by + $b / 2
    $arrow = New-Object System.Drawing.Pen (Color '3a240f'), ([Math]::Max(1.5, 12 * $k))
    $arrow.StartCap = 'Round'; $arrow.EndCap = 'Round'; $arrow.LineJoin = 'Round'
    $g.DrawLine($arrow, $cx, $cy - 26 * $k, $cx, $cy + 12 * $k)
    $g.DrawLines($arrow, [System.Drawing.PointF[]]@(
        [System.Drawing.PointF]::new($cx - 18 * $k, $cy - 4 * $k),
        [System.Drawing.PointF]::new($cx, $cy + 14 * $k),
        [System.Drawing.PointF]::new($cx + 18 * $k, $cy - 4 * $k)))
    $g.DrawLine($arrow, $cx - 22 * $k, $cy + 28 * $k, $cx + 22 * $k, $cy + 28 * $k)
    $g.Dispose()
    $bmp
}

# .ico with PNG frames (Windows Vista+).
$sizes = 16, 24, 32, 48, 64, 128, 256
$frames = foreach ($s in $sizes) {
    $bmp = Draw $s
    if ($Preview -and $s -eq 256) { $bmp.Save($Preview, [System.Drawing.Imaging.ImageFormat]::Png) }
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    , $ms.ToArray()
}
$file = New-Object System.IO.MemoryStream
$w = New-Object System.IO.BinaryWriter $file
$w.Write([uint16]0); $w.Write([uint16]1); $w.Write([uint16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $s = $sizes[$i]; $len = $frames[$i].Length
    $w.Write([byte]($s % 256)); $w.Write([byte]($s % 256)); $w.Write([byte]0); $w.Write([byte]0)
    $w.Write([uint16]1); $w.Write([uint16]32); $w.Write([uint32]$len); $w.Write([uint32]$offset)
    $offset += $len
}
foreach ($f in $frames) { $w.Write($f) }
[System.IO.File]::WriteAllBytes($out, $file.ToArray())
"written $out ($($file.Length) bytes)"
