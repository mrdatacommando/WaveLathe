# WaveLathe - Copyright (C) 2026 Mark Van de Velde
# SPDX-License-Identifier: AGPL-3.0-or-later

# Assembles the Windows release package: both products' standalones and VST3
# bundles, the user guide and the terms, zipped with checksums, ready to attach
# to a new release on GitHub's Releases page.
#
# Run from the project root, AFTER a Release build:
#
#     powershell -ExecutionPolicy Bypass -File Tools\make-release.ps1
#
# It refuses rather than ships something wrong. Three things are checked before
# anything is copied, because each of them has actually been wrong at least once
# in this project's history:
#
#   - the binaries carry the version in Version.h, not whatever they were built
#     with last. The plugin reported 0.1.0 for a month while the source said
#     0.37.0, and nothing on screen disagreed.
#   - no binary imports the Visual C++ runtime DLLs. Those are not part of
#     Windows, so a binary that needs them runs on every machine that happens to
#     have them and fails on the one that does not - and when the PLUGIN is the
#     one missing them, the host just drops it from the scan and says nothing.
#   - the binaries are newer than the newest source file, so the package cannot
#     be built from a stale compile.
#
# Two products ship from this build now: the synthesiser, and the mastering
# chain as an effect plugin. They are listed once, in $products below, and every
# check and every copy walks that list - so a third one is added there and
# nowhere else. Four binaries get the same three checks the one used to.

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot

function New-Product($name, $artefactDir, $exeName, $vst3Name) {
    $stage = Join-Path $root $artefactDir
    return [pscustomobject]@{
        Name     = $name
        ExeName  = $exeName
        Vst3Name = $vst3Name
        Exe      = Join-Path $stage "Standalone\$exeName"
        Vst3Dir  = Join-Path $stage "VST3\$vst3Name"
        Vst3Bin  = Join-Path $stage "VST3\$vst3Name\Contents\x86_64-win\$vst3Name"
    }
}

$products = @(
    (New-Product "WaveLathe" "build\WaveLathe_artefacts\Release" `
                 "WaveLathe.exe" "WaveLathe.vst3"),
    (New-Product "WaveLathe Master" "build\WaveLatheMaster_artefacts\Release" `
                 "WaveLathe Master.exe" "WaveLathe Master.vst3")
)

# Every shipping binary, flat, for the checks below to walk.
$binaries = @()
foreach ($p in $products) { $binaries += $p.Exe; $binaries += $p.Vst3Bin }

# ---- Version ---------------------------------------------------------------

$vh = Get-Content (Join-Path $root "Source\Version.h") -Raw
$major = [regex]::Match($vh, 'constexpr int major = (\d+);').Groups[1].Value
$minor = [regex]::Match($vh, 'constexpr int minor = (\d+);').Groups[1].Value
$patch = [regex]::Match($vh, 'constexpr int patch = (\d+);').Groups[1].Value
$v = "$major.$minor.$patch"

Write-Host "WaveLathe $v  ($($products.Count) products)" -ForegroundColor Cyan
Write-Host ("-" * 52)

# ---- Check: the binaries exist ---------------------------------------------

foreach ($f in $binaries) {
    if (-not (Test-Path $f)) { throw "not built: $f  (run a Release build first)" }
}

# ---- Check: they carry this version ----------------------------------------
#
# Both products share one version number, because they are built together and
# ship together. If that ever stops being true this is the line that has to
# change, and it will fail loudly rather than quietly shipping a mismatch.

foreach ($f in $binaries) {
    $fv = [System.Diagnostics.FileVersionInfo]::GetVersionInfo($f).FileVersion
    $name = Split-Path $f -Leaf
    if ($fv -ne $v) { throw "$name reports version $fv but Version.h says $v" }
    Write-Host "  version  $name  $fv"
}

# ---- Check: nothing newer in Source ----------------------------------------

$newestSource = Get-ChildItem -Path (Join-Path $root "Source") -Recurse -Include *.cpp,*.h |
                Sort-Object LastWriteTime -Descending | Select-Object -First 1
$oldestBinary = Get-ChildItem -Path $binaries |
                Sort-Object LastWriteTime | Select-Object -First 1

if ($newestSource.LastWriteTime -gt $oldestBinary.LastWriteTime) {
    throw "$($newestSource.Name) is newer than $($oldestBinary.Name) - rebuild before packaging"
}
Write-Host "  build    newer than every source file"

# ---- Check: no Visual C++ runtime dependency -------------------------------

function Get-ImportedDlls($Path) {
    $b = [System.IO.File]::ReadAllBytes($Path)
    $pe = [BitConverter]::ToUInt32($b, 0x3C)
    $nSec = [BitConverter]::ToUInt16($b, $pe + 6)
    $optSize = [BitConverter]::ToUInt16($b, $pe + 20)
    $magic = [BitConverter]::ToUInt16($b, $pe + 24)
    if ($magic -eq 0x20b) { $ddBase = $pe + 24 + 112 } else { $ddBase = $pe + 24 + 96 }
    # Data directory entry 1 is the import table. Entry 0 is the exports, and
    # reading that one by mistake yields a plausible-looking list of the wrong
    # thing, which is exactly how it was got wrong the first time.
    $impRva = [BitConverter]::ToUInt32($b, $ddBase + 8)
    $secOff = $pe + 24 + $optSize
    $secs = @()
    for ($i = 0; $i -lt $nSec; $i++) {
        $s = $secOff + $i * 40
        $secs += [pscustomobject]@{
            VA      = [BitConverter]::ToUInt32($b, $s + 12)
            VSize   = [BitConverter]::ToUInt32($b, $s + 8)
            Raw     = [BitConverter]::ToUInt32($b, $s + 20)
            RawSize = [BitConverter]::ToUInt32($b, $s + 16)
        }
    }
    $toOff = {
        param($rva)
        foreach ($s in $secs) {
            $sz = [Math]::Max($s.VSize, $s.RawSize)
            if ($rva -ge $s.VA -and $rva -lt ($s.VA + $sz)) { return $s.Raw + ($rva - $s.VA) }
        }
        return -1
    }
    $o = & $toOff $impRva
    $names = @()
    while ($true) {
        $nameRva = [BitConverter]::ToUInt32($b, $o + 12)
        if ($nameRva -eq 0) { break }
        $n = & $toOff $nameRva
        $e = $n; while ($b[$e] -ne 0) { $e++ }
        $names += [Text.Encoding]::ASCII.GetString($b, $n, $e - $n)
        $o += 20
    }
    return $names
}

foreach ($f in $binaries) {
    $bad = @(Get-ImportedDlls $f | Where-Object { $_ -match '^(MSVCP|VCRUNTIME|MSVCR|api-ms-win-crt)' })
    $name = Split-Path $f -Leaf
    if ($bad.Count -gt 0) { throw "$name still imports the C++ runtime: $($bad -join ', ')" }
    Write-Host "  runtime  $name  self-contained"
}

# ---- Stage ------------------------------------------------------------------

$distRoot = Join-Path $root "dist"
$pkgName  = "WaveLathe-$v-win64"
$pkg      = Join-Path $distRoot $pkgName

# What gets attached to the GitHub release, under names that never change. The
# README's download link names the file, so a version number in the name would
# break that link at every release. The folder carries the version instead, so
# each release's copy stays where it is.
$upload = Join-Path $distRoot "WaveLathe-$v-release"
$zip    = Join-Path $upload "WaveLathe-win64.zip"
$sums   = Join-Path $upload "WaveLathe-win64.sha256.txt"

if (Test-Path $pkg) { Remove-Item $pkg -Recurse -Force }
if (Test-Path $upload) { Remove-Item $upload -Recurse -Force }
New-Item -ItemType Directory -Force -Path (Join-Path $pkg "VST3") | Out-Null
New-Item -ItemType Directory -Force -Path $upload | Out-Null

# Both standalones at the top level, both VST3 bundles in one VST3 folder, so
# the person receiving this copies one folder to one place.
foreach ($p in $products) {
    Copy-Item $p.Exe (Join-Path $pkg $p.ExeName)
    Copy-Item $p.Vst3Dir (Join-Path $pkg "VST3\$($p.Vst3Name)") -Recurse
}

Copy-Item (Join-Path $root "Docs\WaveLathe-User-Guide.pdf") $pkg
Copy-Item (Join-Path $root "LICENSE")      (Join-Path $pkg "LICENSE.txt")
Copy-Item (Join-Path $root "CHANGELOG.md") (Join-Path $pkg "CHANGELOG.txt")
# The AGPL travels with the binaries, and so do the licences of everything
# compiled into them - several of those require their notice in every copy.
Copy-Item (Join-Path $root "THIRD_PARTY_NOTICES.txt") $pkg

# The one document written for the person receiving this rather than for the
# repository. Version substituted so it cannot claim to be a build it is not.
#
# As UTF-8 by hand, for the reason make-guide.ps1 gives: Get-Content reads a
# file without a byte-order mark in the ANSI code page, and the round trip
# through Set-Content turns every non-ASCII character into mojibake. The file
# is plain ASCII today, which is the only reason this had never bitten.
$utf8 = New-Object System.Text.UTF8Encoding($false)
$readme = [System.IO.File]::ReadAllText((Join-Path $root "Packaging\READ-ME-FIRST.txt"), $utf8)
$readme = $readme -replace '\{\{VERSION\}\}', $v
[System.IO.File]::WriteAllText((Join-Path $pkg "READ ME FIRST.txt"), $readme, $utf8)

# Copying preserves timestamps, and a stale mtime on a fresh copy has already
# made one build in this project look older than it was. Stamp them now.
Get-ChildItem $pkg -Recurse | ForEach-Object { $_.LastWriteTime = Get-Date }

# ---- Zip and checksum -------------------------------------------------------

Compress-Archive -Path "$pkg\*" -DestinationPath $zip -CompressionLevel Optimal

# The zip first, then every binary inside it. Checksums for the parts as well
# as the whole, because a recipient who has already unpacked can still check
# what they have without needing the zip back.
$hashTargets = @($zip)
foreach ($p in $products) {
    $hashTargets += (Join-Path $pkg $p.ExeName)
    $hashTargets += (Join-Path $pkg "VST3\$($p.Vst3Name)\Contents\x86_64-win\$($p.Vst3Name)")
}

$lines = @()
foreach ($f in $hashTargets) {
    $h = Get-FileHash $f -Algorithm SHA256
    $lines += "{0}  {1}" -f $h.Hash.ToLower(), (Split-Path $f -Leaf)
}
Set-Content -Path $sums -Value $lines -Encoding ascii

# ---- Report -----------------------------------------------------------------

Write-Host ("-" * 46)
$zipKb = [math]::Round((Get-Item $zip).Length / 1MB, 2)
Write-Host "  package  dist\WaveLathe-$v-release\WaveLathe-win64.zip  ($zipKb MB)" -ForegroundColor Green
Write-Host "  sums     dist\WaveLathe-$v-release\WaveLathe-win64.sha256.txt"
Write-Host ""
Get-ChildItem $pkg | ForEach-Object {
    if ($_.PSIsContainer) { Write-Host "    $($_.Name)\" } else { Write-Host "    $($_.Name)" }
}
Write-Host ""

# Pre-release is the one box that can break something: the README's download
# link asks for the latest release, and GitHub never counts a pre-release as
# the latest, so ticking it would leave people with the version before.
Write-Host "To publish: commit, tag v$v, push both by name, then on GitHub choose Draft a"
Write-Host "new release for that tag and attach the two files above. Leave pre-release"
Write-Host "unticked, or the README's download link will not find it."
