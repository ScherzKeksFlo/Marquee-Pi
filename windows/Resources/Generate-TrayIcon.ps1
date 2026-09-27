param(
    [Parameter(Mandatory=$true)][string]$SourcePng,
    [Parameter(Mandatory=$true)][string]$TargetIco,
    [Parameter(Mandatory=$true)][string]$StatusColor
)
Add-Type -AssemblyName System.Drawing
$source=[System.Drawing.Bitmap]::new($SourcePng)
try {
    $frames=[System.Collections.Generic.List[object]]::new()
    foreach($size in @(16,20,24,32,48,64,128,256)) {
        $bitmap=[System.Drawing.Bitmap]::new($size,$size,[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        try {
            $graphics=[System.Drawing.Graphics]::FromImage($bitmap)
            try {
                $graphics.Clear([System.Drawing.Color]::Transparent)
                $graphics.InterpolationMode=[System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
                $graphics.SmoothingMode=[System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
                $graphics.DrawImage($source,0,0,$size,$size)
                if($size -le 48) {
                    $diameter=[single]($size*0.46)
                    $x=[single]($size*0.53)
                    $y=[single]($size*0.51)
                    $border=[single][Math]::Max(1.2,$size*0.055)
                    $white=[System.Drawing.SolidBrush]::new([System.Drawing.Color]::White)
                    $color=[System.Drawing.SolidBrush]::new([System.Drawing.ColorTranslator]::FromHtml($StatusColor))
                    try {
                        $graphics.FillEllipse($white,$x,$y,$diameter,$diameter)
                        $graphics.FillEllipse($color,$x+$border,$y+$border,$diameter-2*$border,$diameter-2*$border)
                    } finally {
                        $white.Dispose()
                        $color.Dispose()
                    }
                }
            } finally { $graphics.Dispose() }
            $stream=[System.IO.MemoryStream]::new()
            try {
                $bitmap.Save($stream,[System.Drawing.Imaging.ImageFormat]::Png)
                $frames.Add([pscustomobject]@{Size=$size;Bytes=$stream.ToArray()})
            } finally { $stream.Dispose() }
        } finally { $bitmap.Dispose() }
    }
    $file=[System.IO.File]::Create($TargetIco)
    try {
        $writer=[System.IO.BinaryWriter]::new($file)
        $writer.Write([uint16]0)
        $writer.Write([uint16]1)
        $writer.Write([uint16]$frames.Count)
        $offset=6+16*$frames.Count
        foreach($frame in $frames) {
            $writer.Write([byte]($frame.Size % 256))
            $writer.Write([byte]($frame.Size % 256))
            $writer.Write([byte]0)
            $writer.Write([byte]0)
            $writer.Write([uint16]1)
            $writer.Write([uint16]32)
            $writer.Write([uint32]$frame.Bytes.Length)
            $writer.Write([uint32]$offset)
            $offset += $frame.Bytes.Length
        }
        foreach($frame in $frames) { $writer.Write([byte[]]$frame.Bytes) }
        $writer.Flush()
    } finally { $file.Dispose() }
} finally { $source.Dispose() }
