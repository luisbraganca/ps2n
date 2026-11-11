# SPDX-License-Identifier: GPL-3.0-or-later
<#
.SYNOPSIS
Serves the website on this machine, the way GitHub Pages serves it.

.DESCRIPTION
The website is docs\index.html, docs\404.html and docs\site
(docs\site\README.md). GitHub Pages serves the docs folder as it is, under
/ps2n/, and answers an address that holds no file with docs\404.html. This
does the same at http://localhost:<port>/ps2n/, so that the page can be
looked at before it is pushed. Nothing is built: a change to a file shows on
reload.

Opening docs\index.html in a browser as a file works as well, with two
exceptions: the logo does not put itself together (a browser does not let a
page read the pixels of a picture that came from a file), and docs\404.html
finds neither its picture nor the front page.

Only this machine can reach it. Ctrl+C ends it.

.EXAMPLE
scripts\windows\site.ps1
scripts\windows\site.ps1 -Port 8080 -Open
#>
param(
    [int]$Port = 8377,
    # Where the site is on its host: the repository's name, as GitHub Pages has it.
    [string]$Path = '/ps2n/',
    # Opens the page in the default browser.
    [switch]$Open
)

$ErrorActionPreference = 'Stop'

$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$docs = (Join-Path $root 'docs').TrimEnd('\') + '\'
if (-not (Test-Path (Join-Path $docs 'index.html'))) { throw "No docs\index.html under $root" }
if (-not $Path.StartsWith('/')) { $Path = '/' + $Path }
if (-not $Path.EndsWith('/')) { $Path += '/' }

$types = @{
    '.html' = 'text/html; charset=utf-8'
    '.css'  = 'text/css; charset=utf-8'
    '.js'   = 'text/javascript; charset=utf-8'
    '.png'  = 'image/png'
    '.ico'  = 'image/x-icon'
    '.svg'  = 'image/svg+xml'
    '.json' = 'application/json'
    '.md'   = 'text/markdown; charset=utf-8'
    '.txt'  = 'text/plain; charset=utf-8'
}

# The file an address names, or nothing: never one outside docs.
function Find-File([string]$relative) {
    if ($relative -eq '' -or $relative.EndsWith('/')) { $relative += 'index.html' }
    try { $full = [IO.Path]::GetFullPath((Join-Path $docs ($relative -replace '/', '\'))) } catch { return $null }
    if (-not $full.StartsWith($docs, [StringComparison]::OrdinalIgnoreCase)) { return $null }
    if (Test-Path -LiteralPath $full -PathType Leaf) { return $full }
    return $null
}

$listener = New-Object System.Net.HttpListener
$listener.Prefixes.Add("http://localhost:$Port/")
$listener.Start()
$address = "http://localhost:$Port$Path"
Write-Host "The website is at $address  (Ctrl+C ends this)"
if ($Open) { Start-Process $address }

try {
    while ($listener.IsListening) {
        # Waiting in short steps keeps Ctrl+C working.
        $pending = $listener.GetContextAsync()
        while (-not $pending.Wait(200)) { }
        $context = $pending.Result
        $response = $context.Response
        try {
            $asked = [Uri]::UnescapeDataString($context.Request.Url.AbsolutePath)
            $status = 200
            $file = $null
            if ($asked.StartsWith($Path)) { $file = Find-File $asked.Substring($Path.Length) }
            if (-not $file) { $status = 404; $file = Find-File '404.html' }
            if ($file) {
                $bytes = [IO.File]::ReadAllBytes($file)
                $type = $types[[IO.Path]::GetExtension($file).ToLowerInvariant()]
                if (-not $type) { $type = 'application/octet-stream' }
                $response.StatusCode = $status
                $response.ContentType = $type
                $response.Headers['Cache-Control'] = 'no-store'
                $response.ContentLength64 = $bytes.Length
                $response.OutputStream.Write($bytes, 0, $bytes.Length)
            } else {
                $response.StatusCode = 404
            }
            Write-Host "$status $asked"
        } catch {
            try { $response.StatusCode = 500 } catch { }
        } finally {
            try { $response.Close() } catch { }
        }
    }
} finally {
    $listener.Stop()
    $listener.Close()
}
