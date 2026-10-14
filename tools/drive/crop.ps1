# Usage: crop.ps1 <src.bmp> <out.png> <x> <y> <w> <h> [zoom]   (nearest-neighbour zoom, pixels stay visible)
param([string]$src, [string]$out, [int]$x, [int]$y, [int]$w, [int]$h, [int]$zoom = 4)
Add-Type -AssemblyName System.Drawing
$b = [System.Drawing.Bitmap]::FromFile($src)
$s = New-Object System.Drawing.Bitmap ($w * $zoom), ($h * $zoom)
$g = [System.Drawing.Graphics]::FromImage($s)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
$g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
$g.DrawImage($b, (New-Object System.Drawing.Rectangle 0, 0, ($w * $zoom), ($h * $zoom)), (New-Object System.Drawing.Rectangle $x, $y, $w, $h), [System.Drawing.GraphicsUnit]::Pixel)
$s.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $s.Dispose(); $b.Dispose()
