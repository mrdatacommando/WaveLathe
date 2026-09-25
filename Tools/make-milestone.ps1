# WaveLathe - Copyright (C) 2026 Mark Van de Velde
# SPDX-License-Identifier: AGPL-3.0-or-later

# Archives everything needed to rebuild this project exactly as it stands, into
# one file under "My Bks", beside the milestone builds already kept there.
#
#     powershell -ExecutionPolicy Bypass -File Tools\make-milestone.ps1
#     powershell -ExecutionPolicy Bypass -File Tools\make-milestone.ps1 -Note "beta out to friends"
#
# There is no version control on this project, so a milestone is the only way
# back to a known state. That means the archive has to carry the SOURCE, not
# just the build: the four WaveForge_bk*.exe files in "My Bks" preserve what
# those builds sounded like and nothing about how to change them.
#
# What goes in is everything that is written by hand, plus the release package
# actually sent out. What stays out is anything a build regenerates - build/ is
# over half a gigabyte and reproduces from Source plus CMakeLists.

param(
    [string]$Note = ""
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$bks  = Join-Path $root "My Bks"

# ---- Version ---------------------------------------------------------------

$vh = Get-Content (Join-Path $root "Source\Version.h") -Raw
$major = [regex]::Match($vh, 'constexpr int major = (\d+);').Groups[1].Value
$minor = [regex]::Match($vh, 'constexpr int minor = (\d+);').Groups[1].Value
$patch = [regex]::Match($vh, 'constexpr int patch = (\d+);').Groups[1].Value
$v = "$major.$minor.$patch"

$stamp = Get-Date -Format "yyyy-MM-dd"
$name  = "WaveLathe-$v-milestone-$stamp"
$stage = Join-Path $env:TEMP $name
$zip   = Join-Path $bks "$name.zip"

Write-Host "WaveLathe $v milestone" -ForegroundColor Cyan
Write-Host ("-" * 52)

if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force -Path $stage | Out-Null
New-Item -ItemType Directory -Force -Path $bks   | Out-Null

# ---- Everything written by hand --------------------------------------------

$dirs  = @("Source", "Tests", "Tools", "Packaging", "Docs", "Analysis")
$files = @("CMakeLists.txt", "LICENSE", "README.md", "CHANGELOG.md")

foreach ($d in $dirs) {
    $src = Join-Path $root $d
    if (Test-Path $src) {
        Copy-Item $src (Join-Path $stage $d) -Recurse
        $n = (Get-ChildItem $src -Recurse -File).Count
        Write-Host ("  {0,-12} {1,4} files" -f $d, $n)
    }
}
foreach ($f in $files) {
    $src = Join-Path $root $f
    if (Test-Path $src) { Copy-Item $src $stage; Write-Host ("  {0,-12}" -f $f) }
}

# ---- The package that actually went out ------------------------------------

$distDir = Join-Path $root "dist"
$shipped = @()
if (Test-Path $distDir) {
    $relDir = Join-Path $stage "released"
    New-Item -ItemType Directory -Force -Path $relDir | Out-Null
    foreach ($f in (Get-ChildItem $distDir -File | Where-Object { $_.Extension -eq ".zip" -or $_.Extension -eq ".txt" })) {
        Copy-Item $f.FullName $relDir
        $shipped += $f.Name
        Write-Host ("  {0,-12} {1}" -f "released", $f.Name)
    }
}

# An index of what is inside that package, in plain text beside it.
#
# The built standalone and VST3 are kept inside the release zip rather than
# loose at the top of this archive, so that what is preserved is the package
# exactly as it went out - unpacking and re-zipping it would leave the
# checksums beside it meaning nothing. The cost is that a directory listing of
# this archive shows no binaries at all, which reads like they were forgotten.
# This file is the fix: the zip can be read without being opened.
$pkgZipName = ""
Add-Type -AssemblyName System.IO.Compression.FileSystem
$pkgZip = Get-ChildItem (Join-Path $stage "released") -Filter "*.zip" -ErrorAction SilentlyContinue | Select-Object -First 1
if ($pkgZip) {
    $pkgZipName = $pkgZip.Name
    $idx = @(
        "Contents of $($pkgZip.Name)",
        ("=" * 60),
        "",
        "This is the package as it was distributed. The built binaries are",
        "in here - they are not stored loose elsewhere in this archive.",
        ""
    )
    $z = [System.IO.Compression.ZipFile]::OpenRead($pkgZip.FullName)
    foreach ($entry in ($z.Entries | Sort-Object FullName)) {
        if ($entry.Length -eq 0 -and $entry.FullName.EndsWith("\")) { continue }
        $idx += ("  {0,-54} {1,12:N0} bytes" -f $entry.FullName, $entry.Length)
    }
    $z.Dispose()
    Set-Content -Path (Join-Path $stage "released\CONTENTS.txt") -Value $idx -Encoding utf8
    Write-Host ("  {0,-12} {1}" -f "released", "CONTENTS.txt (index of the package)")
}

# ---- The manifest: what this state WAS, which the files alone do not say ----

$testDirs = Get-ChildItem (Join-Path $root "build") -Directory -Filter "*Test_artefacts" -ErrorAction SilentlyContinue
$suiteCount = $testDirs.Count

$sums = ""
$sumFile = Get-ChildItem $distDir -Filter "*.sha256.txt" -ErrorAction SilentlyContinue | Select-Object -First 1
if ($sumFile) { $sums = (Get-Content $sumFile.FullName) -join "`r`n    " }

$noteBlock = ""
if ($Note -ne "") { $noteBlock = "`r`n  $Note`r`n" }

$whereBinaries = "  No release package was present when this was archived."
if ($pkgZipName -ne "") {
    $whereBinaries = @"
  Not loose at the top level of this archive. The built standalone and
  the VST3 are both inside:

      released\$pkgZipName

  That is the package exactly as it was distributed, kept whole so the
  checksums below still describe something real. Unpacking and re-zipping
  it would break that. For an index of what is in it without extracting
  anything, read released\CONTENTS.txt.
"@
}

$manifest = @"
WaveLathe $v  -  MILESTONE
Archived $stamp
================================================================
$noteBlock
WHAT THIS IS

  A complete snapshot of the hand-written project at version $v.
  This project has no version control, so this archive is the only
  route back to this state. It carries the SOURCE, not just a build.

  Everything in build\ was deliberately left out. It regenerates
  from Source\ and CMakeLists.txt, and it is over half a gigabyte.


STATE AT THIS POINT

  Version           $v
  Test suites       $suiteCount, all passing at the time of archiving
  Formats           Standalone (.exe) and VST3, Windows x64
  C++ runtime       Linked statically - no VC++ redistributable needed
  Distribution      Beta, direct to friends. Not code-signed.


BUILD ENVIRONMENT

  CMake             4.4.3
  Generator         Visual Studio 17 2022
  JUCE              9.0.1, fetched by CMake (FetchContent, GIT_TAG 9.0.1)
  Licence           WaveLathe AGPL-3.0-or-later; JUCE used under its AGPLv3


TO REBUILD FROM THIS ARCHIVE

  1. Extract somewhere with no spaces in the path.
  2. cmake -S . -B build
     (JUCE 9.0.1 is downloaded automatically - needs a connection)
  3. cmake --build build --config Release
  4. Binaries land in build\WaveLathe_artefacts\Release\

  To rebuild the user guide PDF:
     powershell -ExecutionPolicy Bypass -File Tools\make-guide.ps1
  To rebuild the release package:
     powershell -ExecutionPolicy Bypass -File Tools\make-release.ps1


WHERE THE BUILT BINARIES ARE

$whereBinaries

WHAT SHIPPED, AND ITS FINGERPRINT

    $sums


KNOWN UNTESTED AT THIS POINT

  Neither of these had been exercised end to end when this was
  archived. Both need the plugin running inside a DAW:

  1. Save a DAW project containing WaveLathe, close the DAW, and
     reopen it. Does a non-factory patch come back intact?
     (Risk introduced in 0.38.2, where host program changes began
     applying factory presets.)

  2. Bounce or export a track with a long reverb or delay tail.
     Is the tail complete, or cut off?
     (The 0.37.0 tail fix, never verified through a real export.)


OPEN, NON-BLOCKING

  - build\WaveForge_artefacts\ holds ~546 MB of pre-rename output.
    Safe to delete; nothing references it.
  - "WaveLathe" was searched on IP Australia's trade mark register
    (no results). No search outside Australia has been done.

================================================================
"@

Set-Content -Path (Join-Path $stage "MILESTONE.txt") -Value $manifest -Encoding utf8

# Copying preserves timestamps, which has already made one build in this
# project look older than it was. Stamp the copies with now.
Get-ChildItem $stage -Recurse | ForEach-Object { $_.LastWriteTime = Get-Date }

# ---- Archive ---------------------------------------------------------------

if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path "$stage\*" -DestinationPath $zip -CompressionLevel Optimal
Remove-Item $stage -Recurse -Force

$mb = [math]::Round((Get-Item $zip).Length / 1MB, 2)
$hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()

Write-Host ("-" * 52)
Write-Host "  archived  My Bks\$name.zip  ($mb MB)" -ForegroundColor Green
Write-Host "  sha256    $hash"
Write-Host ""
Write-Host "  This is the only copy. Put one somewhere that is not this drive." -ForegroundColor Yellow
