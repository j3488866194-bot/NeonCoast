param([Parameter(Mandatory=$true)][string]$Destination)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
# Rasterize the project's existing assets/icon.svg geometry without new artwork.
$sizes = @(16,32,48,64,128,256)
$images = @()
foreach ($size in $sizes) {
    $bitmap = [Drawing.Bitmap]::new($size,$size)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.ScaleTransform($size/256.0,$size/256.0)
    $background = [Drawing.SolidBrush]::new([Drawing.ColorTranslator]::FromHtml('#172f36'))
    $sun = [Drawing.SolidBrush]::new([Drawing.ColorTranslator]::FromHtml('#ff927c'))
    $sea = [Drawing.SolidBrush]::new([Drawing.ColorTranslator]::FromHtml('#77d7c8'))
    $letter = [Drawing.SolidBrush]::new([Drawing.ColorTranslator]::FromHtml('#fff2d9'))
    $rounded = [Drawing.Drawing2D.GraphicsPath]::new()
    $rounded.AddArc(0,0,104,104,180,90)
    $rounded.AddArc(152,0,104,104,270,90)
    $rounded.AddArc(152,152,104,104,0,90)
    $rounded.AddArc(0,152,104,104,90,90)
    $rounded.CloseFigure()
    $graphics.FillPath($background,$rounded)
    $graphics.FillEllipse($sun,61,41,134,134)
    $graphics.FillRectangle($sea,40,139,176,9)
    $graphics.FillRectangle($sea,48,159,160,7)
    $graphics.FillRectangle($sea,60,177,136,6)
    $points = [Drawing.PointF[]]@(
        [Drawing.PointF]::new(83,204),[Drawing.PointF]::new(83,91),
        [Drawing.PointF]::new(103,91),[Drawing.PointF]::new(153,167),
        [Drawing.PointF]::new(153,91),[Drawing.PointF]::new(173,91),
        [Drawing.PointF]::new(173,204),[Drawing.PointF]::new(153,204),
        [Drawing.PointF]::new(103,128),[Drawing.PointF]::new(103,204))
    $graphics.FillPolygon($letter,$points)
    $memory = [IO.MemoryStream]::new()
    $bitmap.Save($memory,[Drawing.Imaging.ImageFormat]::Png)
    $images += ,$memory.ToArray()
    $memory.Dispose(); $rounded.Dispose(); $background.Dispose(); $sun.Dispose()
    $sea.Dispose(); $letter.Dispose(); $graphics.Dispose(); $bitmap.Dispose()
}
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($Destination))) | Out-Null
$stream = [IO.File]::Create($Destination)
$writer = [IO.BinaryWriter]::new($stream)
try {
    $writer.Write([UInt16]0); $writer.Write([UInt16]1); $writer.Write([UInt16]$sizes.Count)
    $offset = 6 + 16 * $sizes.Count
    for ($i=0; $i -lt $sizes.Count; $i++) {
        $dimension = if ($sizes[$i] -eq 256) { 0 } else { $sizes[$i] }
        $writer.Write([byte]$dimension); $writer.Write([byte]$dimension)
        $writer.Write([byte]0); $writer.Write([byte]0)
        $writer.Write([UInt16]1); $writer.Write([UInt16]32)
        $writer.Write([UInt32]$images[$i].Length); $writer.Write([UInt32]$offset)
        $offset += $images[$i].Length
    }
    foreach ($bytes in $images) { $writer.Write([byte[]]$bytes) }
} finally { $writer.Dispose(); $stream.Dispose() }
