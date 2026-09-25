# WaveLathe - Copyright (C) 2026 Mark Van de Velde
# SPDX-License-Identifier: AGPL-3.0-or-later

# Builds every test suite, runs it, and reports one verdict.
#
# Run from the project root:
#
#     powershell -ExecutionPolicy Bypass -File Tools\run-tests.ps1
#
# Each suite is a console app that prints its own detail and returns 0 for pass
# and 1 for any failure, so the exit code of this script is the whole answer:
# 0 means every suite built and passed, anything else means look at the output.
#
# The build is not a convenience step. A test executable that predates the
# source it claims to test passes cheerfully and describes code nobody is
# running any more. Building first makes the binaries current by construction,
# which is the only guarantee worth having and costs nothing when there is
# nothing to rebuild.
#
# An earlier version of this tried to prove the same thing by comparing each
# executable's timestamp against the newest file in the tree. That is cruder
# than the dependency graph MSBuild already keeps: a touched CMakeLists.txt
# that no test links against marked all fourteen suites stale when not one of
# them was. The build system knows what depends on what. Ask it rather than
# guessing, and do not reintroduce the clever version.

$ErrorActionPreference = "Stop"

$root  = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root "build"

# ---- Which suites exist ----------------------------------------------------

# Read from CMakeLists.txt rather than a list kept here, so adding a test means
# adding it in one place. The console apps that are NOT tests - MatchProbe,
# VerifyPreset, WavAnalyzer - are told apart by name: a suite ends in "Test".
# Name a new one to match or this will quietly skip it.

$cml = Get-Content (Join-Path $root "CMakeLists.txt") -Raw
$suites = [regex]::Matches($cml, 'juce_add_console_app\(\s*(\w+Test)\b') |
          ForEach-Object { $_.Groups[1].Value } |
          Sort-Object -Unique

if ($suites.Count -eq 0) {
    Write-Host "No test suites found in CMakeLists.txt." -ForegroundColor Red
    Write-Host "Either the naming convention changed or the parse is broken -"
    Write-Host "either way, running nothing and reporting green would be worse."
    exit 1
}

Write-Host "WaveLathe test run" -ForegroundColor Cyan
Write-Host ("-" * 64)
Write-Host ("  {0} suites declared in CMakeLists.txt" -f $suites.Count)

# ---- Build -----------------------------------------------------------------

Write-Host "  building..." -NoNewline
$buildLog = Join-Path $env:TEMP "wavelathe-testbuild.log"
cmake --build $build --config Release --target $suites | Out-File -FilePath $buildLog -Encoding utf8
if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "  BUILD FAILED - nothing was run." -ForegroundColor Red
    Get-Content $buildLog | Select-String -Pattern "error" -SimpleMatch |
        Select-Object -First 20 | ForEach-Object { Write-Host "    $_" }
    Write-Host "  full log: $buildLog"
    exit 1
}
Write-Host " ok"
Write-Host ""

# ---- Run -------------------------------------------------------------------

$passed  = @()
$failed  = @()
$missing = @()

foreach ($s in $suites) {
    $exe = Join-Path $build "$s`_artefacts\Release\$s.exe"

    if (-not (Test-Path $exe)) {
        $missing += $s
        Write-Host ("  {0,-20} NOT BUILT" -f $s) -ForegroundColor Red
        continue
    }

    $out  = & $exe
    $code = $LASTEXITCODE

    if ($code -eq 0) {
        $passed += $s
        Write-Host ("  {0,-20} PASS" -f $s) -ForegroundColor Green
    } else {
        $failed += $s
        Write-Host ("  {0,-20} FAIL (exit $code)" -f $s) -ForegroundColor Red
        # Only a failing suite gets its output shown. Passing detail is noise,
        # and noise is how a real failure goes unread.
        $out | Select-Object -Last 30 | ForEach-Object { Write-Host "        $_" }
    }
}

# ---- Verdict ---------------------------------------------------------------

Write-Host ""
Write-Host ("-" * 64)
Write-Host ("  passed   {0} / {1}" -f $passed.Count, $suites.Count)
if ($failed.Count  -gt 0) { Write-Host ("  FAILED   {0}" -f ($failed  -join ", ")) -ForegroundColor Red }
if ($missing.Count -gt 0) { Write-Host ("  MISSING  {0}" -f ($missing -join ", ")) -ForegroundColor Red }

if ($failed.Count -eq 0 -and $missing.Count -eq 0) {
    Write-Host ""
    Write-Host "  GREEN - every suite built and passed." -ForegroundColor Green
    exit 0
}
exit 1
