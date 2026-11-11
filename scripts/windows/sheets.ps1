# SPDX-License-Identifier: GPL-3.0-or-later
<#
.SYNOPSIS
Makes contact sheets of the pictures a run of a game saved.

.DESCRIPTION
A run with -FrameEvery (scripts\windows\game.ps1) saves the picture on
display every so many frames as frame-<number>.png in its folder. This puts
them side by side, small, each under its frame number, sixty to a sheet by
default, so that a whole run can be looked at in a few images: which screen
is up when, where a controller script has to press what.

The sheets are written beside the pictures as sheet-<nn>.png. Like the
pictures they are derived from the game and stay in the build tree, which git
ignores.

.EXAMPLE
scripts\windows\game.ps1 -Image <iso> -Config stock-jit -Frames 9000 -FrameEvery 50 -Pad tests\games\killzone\level1.pad -Name level1
scripts\windows\sheets.ps1 -Name level1 -Config stock-jit
scripts\windows\sheets.ps1 -Name level1 -Config stock-jit -From 6500 -To 9000
scripts\windows\sheets.ps1 -Dir build\windows-clang\ps2n\tests\game\level1\stock-jit
#>
param(
    # The run, as scripts\windows\game.ps1 names its folder...
    [string]$Name = 'game',
    [string]$Config = 'ps2n-static',
    # ...or the folder itself.
    [string]$Dir = '',
    # Only the pictures of these frames.
    [int]$From = 0,
    [int]$To = [int]::MaxValue,
    [int]$Columns = 10,
    [int]$Rows = 6,
    # The width of one small picture, in pixels.
    [int]$Width = 192,
    [string]$BuildDir = 'build\windows-clang'
)

$ErrorActionPreference = 'Stop'

$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$folder = if ($Dir) { (Resolve-Path -LiteralPath $Dir).Path } else { Join-Path $root "$BuildDir\ps2n\tests\game\$Name\$Config" }
if (-not (Test-Path -LiteralPath $folder)) { throw "There is no such folder: $folder" }

function FrameNumber($file) { [int]($file.BaseName -replace '^frame-', '') }

$pictures = @(Get-ChildItem -LiteralPath $folder -Filter 'frame-*.png' | Sort-Object Name |
    Where-Object { (FrameNumber $_) -ge $From -and (FrameNumber $_) -le $To })
if ($pictures.Count -eq 0) { throw "No frame-<number>.png pictures of those frames in $folder (run with -FrameEvery)." }

Add-Type -AssemblyName System.Drawing

# The console's picture is shown four to three whatever its size in pixels.
$height = [int]($Width * 3 / 4)
$label = 14
$perSheet = $Columns * $Rows
$sheet = 0
for ($first = 0; $first -lt $pictures.Count; $first += $perSheet) {
    $batch = @($pictures[$first..([Math]::Min($first + $perSheet, $pictures.Count) - 1)])
    $rowsHere = [int][Math]::Ceiling($batch.Count / $Columns)
    $bitmap = New-Object System.Drawing.Bitmap ($Columns * $Width), ($rowsHere * ($height + $label))
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $font = New-Object System.Drawing.Font 'Consolas', 9
    try {
        $graphics.Clear([System.Drawing.Color]::FromArgb(40, 40, 40))
        $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBilinear
        for ($k = 0; $k -lt $batch.Count; $k++) {
            $x = ($k % $Columns) * $Width
            $y = [int][Math]::Floor($k / $Columns) * ($height + $label)
            $picture = [System.Drawing.Image]::FromFile($batch[$k].FullName)
            try { $graphics.DrawImage($picture, $x, $y + $label, $Width - 2, $height) }
            finally { $picture.Dispose() }
            $graphics.DrawString([string](FrameNumber $batch[$k]), $font, [System.Drawing.Brushes]::White, $x + 2, $y)
        }
        $path = Join-Path $folder ('sheet-{0:D2}.png' -f $sheet)
        $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    }
    finally {
        $font.Dispose()
        $graphics.Dispose()
        $bitmap.Dispose()
    }
    Write-Host ("{0}: {1} pictures, frames {2} to {3}" -f $path, $batch.Count, (FrameNumber $batch[0]), (FrameNumber $batch[-1]))
    $sheet++
}
