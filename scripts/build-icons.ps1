$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$iconDirectory = Join-Path (Split-Path -Parent $PSScriptRoot) 'resources/icons'

function Write-Icon([string]$SourceName, [string]$DestinationName) {
    $source = [Drawing.Image]::FromFile((Join-Path $iconDirectory $SourceName))
    try {
        $frames = [Collections.Generic.List[object]]::new()
        foreach ($size in @(16, 20, 24, 32, 40, 48, 64, 128, 256)) {
            $bitmap = [Drawing.Bitmap]::new($size, $size, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
            $graphics = [Drawing.Graphics]::FromImage($bitmap)
            $stream = [IO.MemoryStream]::new()
            try {
                $graphics.CompositingMode = [Drawing.Drawing2D.CompositingMode]::SourceCopy
                $graphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
                $graphics.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::HighQuality
                $graphics.DrawImage($source, 0, 0, $size, $size)
                $bitmap.Save($stream, [Drawing.Imaging.ImageFormat]::Png)
                $frames.Add(@{ Size = $size; Bytes = $stream.ToArray() })
            } finally {
                $stream.Dispose()
                $graphics.Dispose()
                $bitmap.Dispose()
            }
        }
        $file = [IO.File]::Create((Join-Path $iconDirectory $DestinationName))
        $writer = [IO.BinaryWriter]::new($file)
        try {
            $writer.Write([uint16]0)
            $writer.Write([uint16]1)
            $writer.Write([uint16]$frames.Count)
            $offset = 6 + 16 * $frames.Count
            foreach ($frame in $frames) {
                $dimension = if ($frame.Size -eq 256) { 0 } else { $frame.Size }
                $writer.Write([byte]$dimension)
                $writer.Write([byte]$dimension)
                $writer.Write([byte]0)
                $writer.Write([byte]0)
                $writer.Write([uint16]1)
                $writer.Write([uint16]32)
                $writer.Write([uint32]$frame.Bytes.Length)
                $writer.Write([uint32]$offset)
                $offset += $frame.Bytes.Length
            }
            foreach ($frame in $frames) { $writer.Write([byte[]]$frame.Bytes) }
        } finally {
            $writer.Dispose()
            $file.Dispose()
        }
    } finally {
        $source.Dispose()
    }
}

# The pack includes matching PNG exports, so building/running needs no SVG library.
Write-Icon 'grayscale-viewer-256.png' 'grayscale-viewer.ico'
Write-Icon 'grayscale-viewer-symbol-256.png' 'grayscale-viewer-tray.ico'
