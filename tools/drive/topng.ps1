# Usage: topng.ps1 <src.bmp> <out.png> [width]
param([string]$src, [string]$out, [int]$w = 960)
Add-Type -AssemblyName System.Drawing
$b = [System.Drawing.Bitmap]::FromFile($src)
$h = [int]($b.Height * $w / $b.Width)
$s = New-Object System.Drawing.Bitmap $w, $h
$g = [System.Drawing.Graphics]::FromImage($s)
$g.DrawImage($b, 0, 0, $w, $h)
$s.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $s.Dispose(); $b.Dispose()
