# WaveLathe - Copyright (C) 2026 Mark Van de Velde
# SPDX-License-Identifier: AGPL-3.0-or-later

# Regenerates the user guide PDF from the HTML, and keeps the version number in
# the HTML in step with Source/Version.h so there is still only one place the
# version is actually decided.
#
# Run from the project root:
#
#     powershell -ExecutionPolicy Bypass -File Tools\make-guide.ps1
#
# The HTML is the master. Edit Docs\WaveLathe-User-Guide.html, run this, and the
# PDF beside it is rebuilt. Nothing here edits the prose - the only substitution
# is the version string, in the two places it appears.
#
# Chrome does the printing. It is the only HTML engine on this machine and its
# --print-to-pdf honours the @page rules and the print stylesheet, so the PDF
# and the on-screen document are the same document rather than two that have to
# be kept looking alike.

$ErrorActionPreference = "Stop"

$root    = Split-Path -Parent $PSScriptRoot
$html    = Join-Path $root "Docs\WaveLathe-User-Guide.html"
$pdf     = Join-Path $root "Docs\WaveLathe-User-Guide.pdf"
$version = Join-Path $root "Source\Version.h"

foreach ($f in @($html, $version)) {
    if (-not (Test-Path $f)) { throw "missing: $f" }
}

# ---- Version, read from the header rather than typed again -----------------

$vh = Get-Content $version -Raw
$major = [regex]::Match($vh, 'constexpr int major = (\d+);').Groups[1].Value
$minor = [regex]::Match($vh, 'constexpr int minor = (\d+);').Groups[1].Value
$patch = [regex]::Match($vh, 'constexpr int patch = (\d+);').Groups[1].Value

if (-not $major -or -not $minor -or -not $patch) {
    throw "could not read the version out of $version"
}

$v = "$major.$minor.$patch"
Write-Host "version from Version.h: $v"

# Read and written as UTF-8 by hand, never through Get-Content and Set-Content.
# Windows PowerShell reads a file with no byte-order mark in the ANSI code page,
# so every em dash, arrow and multiplication sign in the guide came back as
# two or three characters of mojibake - and Set-Content then wrote those out as
# UTF-8, which made the damage permanent. It hid for as long as the version
# never changed, because nothing was written back; the first real bump, to
# 0.48.2, rewrote 143 lines and printed them into the PDF.
$utf8 = New-Object System.Text.UTF8Encoding($false)
$doc = [System.IO.File]::ReadAllText($html, $utf8)
$before = $doc

# The cover, and the line in the colophon. Both carry the same string, and both
# are matched on their own marker so the substitution cannot wander into prose
# that happens to contain a number.
$doc = [regex]::Replace($doc, '(<dd id="guide-version">)[^<]*(</dd>)',       "`${1}$v`${2}")
$doc = [regex]::Replace($doc, '(<span class="guide-version">)[^<]*(</span>)', "`${1}$v`${2}")

if ($doc -ne $before) {
    [System.IO.File]::WriteAllText($html, $doc, $utf8)
    Write-Host "updated the version in the HTML"
} else {
    Write-Host "HTML version already current"
}

# ---- Print -----------------------------------------------------------------

$chrome = @(
    "C:\Program Files\Google\Chrome\Application\chrome.exe",
    "C:\Program Files (x86)\Google\Chrome\Application\chrome.exe",
    "C:\Program Files\Microsoft\Edge\Application\msedge.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1

if (-not $chrome) { throw "no Chrome or Edge found to print with" }

if (Test-Path $pdf) { Remove-Item $pdf -Force }

# file:/// with forward slashes, which is what Chrome wants on Windows.
$url = "file:///" + ($html -replace '\\', '/')

# virtual-time-budget gives the images and the layout time to settle before the
# page is captured; without it a long document can print half-laid-out.
#
# Start-Process -Wait rather than the call operator, for two reasons that both
# bit on the way here. Chrome is a GUI-subsystem binary, so "& chrome" hands
# back straight away and the check below runs before the PDF exists. And Chrome
# writes what it did to stderr even on success, which Windows PowerShell turns
# into error records - so redirecting that stream reports a working run as a
# failure. Waiting on the process and then looking for the file avoids both:
# the PDF being on disk is the only thing that actually settles it.
$chromeArgs = @(
    "--headless",
    "--disable-gpu",
    "--no-sandbox",
    "--no-pdf-header-footer",
    "--virtual-time-budget=20000",
    "--run-all-compositor-stages-before-draw",
    "--print-to-pdf=`"$pdf`"",
    "`"$url`""
)

Start-Process -FilePath $chrome -ArgumentList $chromeArgs -NoNewWindow -Wait

Start-Sleep -Milliseconds 500

if (-not (Test-Path $pdf)) { throw "Chrome did not write $pdf" }

$size = [math]::Round((Get-Item $pdf).Length / 1KB, 1)
Write-Host "wrote Docs\WaveLathe-User-Guide.pdf  ($size KB)"
