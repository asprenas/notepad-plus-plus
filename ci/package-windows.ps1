<#
.SYNOPSIS
  Windows packages of the Qt port (docs/plan/phase-6-packaging-stabilization.md 3.1):
  portable zip and symbols, built locally (GitHub is a repository only).

.DESCRIPTION
  1. Release build with MSVC (QT docking backend, tests off) in build/package/windows-msvc
  2. cmake --install into a staging folder, windeployqt (Qt runtime), app-local MSVC runtime
  3. dist/notepadpp-qt-<ver>-windows-x64-portable.zip   (doLocalConf.xml: settings next to the exe)
     dist/notepadpp-qt-<ver>-windows-x64-symbols.zip    (PDB for crash dumps)

.PARAMETER QtDir      MSVC Qt kit (default $env:NPP_QT_MSVC_DIR or C:\Programs\Qt\6.12.0\msvc2022_64)
.PARAMETER QtTools    Qt Tools folder with Ninja (default $env:NPP_QT_TOOLS or C:\Programs\Qt\Tools)
.PARAMETER Iscc       ISCC.exe (default: Inno Setup 6 in the usual places)
.PARAMETER Dist       output folder (default <repo>\dist)
.PARAMETER SkipBuild  reuse the existing build folder
#>
param(
    [string]$QtDir = $(if ($env:NPP_QT_MSVC_DIR) { $env:NPP_QT_MSVC_DIR } else { 'C:\Programs\Qt\6.12.0\msvc2022_64' }),
    [string]$QtTools = $(if ($env:NPP_QT_TOOLS) { $env:NPP_QT_TOOLS } else { 'C:\Programs\Qt\Tools' }),
    [string]$Iscc = '',
    [string]$Dist = '',
    [switch]$SkipBuild
)
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
if (-not $Dist) { $Dist = Join-Path $Root 'dist' }
$Build = Join-Path $Root 'build\package\windows-msvc'
$Stage = Join-Path $Root 'build\package\stage-windows\Notepad++Qt'

function Fail($msg) { Write-Host "package-windows: $msg" -ForegroundColor Red; exit 1 }

# Zip a folder (with the folder itself, or -Contents only). Freshly written files can be held
# for a moment by a virus scanner / the search indexer: retry instead of failing.
Add-Type -AssemblyName System.IO.Compression.FileSystem
function New-Zip([string]$folder, [string]$zipPath, [switch]$Contents) {
    for ($try = 1; ; $try++) {
        try {
            if (Test-Path $zipPath) { Remove-Item $zipPath }
            [IO.Compression.ZipFile]::CreateFromDirectory($folder, $zipPath, [IO.Compression.CompressionLevel]::Optimal, -not $Contents)
            return
        } catch {
            if ($try -ge 5) { Fail "zip $zipPath failed: $($_.Exception.Message)" }
            Start-Sleep -Seconds 2
        }
    }
}

# --- version from the root CMakeLists.txt ------------------------------------------------------
$m = Select-String -Path (Join-Path $Root 'CMakeLists.txt') -Pattern '^\s*VERSION\s+(\d+\.\d+\.\d+)' | Select-Object -First 1
if (-not $m) { Fail 'project VERSION not found' }
$Version = $m.Matches[0].Groups[1].Value
Write-Host "== Notepad++ Qt $Version (Windows x64)"

# --- tools ---------------------------------------------------------------------------------
if (-not (Test-Path "$QtDir\bin\windeployqt.exe")) { Fail "Qt MSVC kit not found: $QtDir" }
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { Fail 'Visual Studio C++ tools not found' }
<# Installer generation is deferred to a future release.
if (-not $Iscc) {
    $Iscc = @("$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe", "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
              "$env:ProgramFiles\Inno Setup 6\ISCC.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $Iscc -or -not (Test-Path $Iscc)) { Fail 'Inno Setup 6 (ISCC.exe) not found: winget install JRSoftware.InnoSetup' }
#>

# --- build + install + deploy (inside the vcvars environment) ----------------------------------
if (Test-Path $Stage) { Remove-Item -Recurse -Force (Split-Path -Parent $Stage) }
$bat = Join-Path $env:TEMP "npp-package-windows-$PID.bat"
$lines = @(
    '@echo off',
    "set `"PATH=${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer;%PATH%`"",
    "call `"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul || exit /b 10",
    "set `"PATH=$QtTools\Ninja;$QtDir\bin;%PATH%`"",
    "cd /d `"$Root`""
)
if (-not $SkipBuild) {
    $lines += "cmake --preset windows-msvc-release -B `"$Build`" -DCMAKE_PREFIX_PATH=`"$QtDir`" -DNPP_DOCKING_BACKEND=QT -DBUILD_TESTING=OFF || exit /b 11"
    $lines += "cmake --build `"$Build`" -j || exit /b 12"
}
$lines += "cmake --install `"$Build`" --prefix `"$Stage`" || exit /b 13"
$lines += "windeployqt --release --no-system-d3d-compiler --no-opengl-sw --no-compiler-runtime --verbose 0 `"$Stage\notepadpp-qt.exe`" || exit /b 14"
$lines += "for /d %%d in (`"%VCToolsRedistDir%x64\Microsoft.VC*.CRT`") do copy /y `"%%d\*.dll`" `"$Stage`" >nul || exit /b 15"
Set-Content -Path $bat -Value $lines -Encoding ascii
$ErrorActionPreference = 'Continue' # native tools write progress to stderr
& cmd /c $bat 2>&1 | ForEach-Object { "$_" } | Where-Object { $_ -match 'error|warning C|FAILED' } | Select-Object -Last 40
$rc = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
Remove-Item $bat
switch ($rc) {
    0 { }
    10 { Fail 'vcvars64.bat failed' }
    11 { Fail 'configure failed' }
    12 { Fail 'build failed' }
    13 { Fail 'install failed' }
    14 { Fail 'windeployqt failed' }
    15 { Fail 'MSVC runtime DLLs not found (VCToolsRedistDir)' }
    default { Fail "failed ($rc)" }
}
if (-not (Test-Path "$Stage\vcruntime140.dll")) { Fail 'vcruntime140.dll missing in the staging folder' }
Copy-Item (Join-Path $Root 'LICENSE') "$Stage\LICENSE.txt"
Copy-Item (Join-Path $Root 'docs\CHANGELOG.md') "$Stage\CHANGELOG.md" -ErrorAction SilentlyContinue

New-Item -ItemType Directory -Force $Dist | Out-Null
$base = "notepadpp-qt-$Version-windows-x64"

# --- portable zip: doLocalConf.xml makes the folder hold its own settings ----------------------
$portStage = Join-Path $Root 'build\package\stage-windows-portable'
if (Test-Path $portStage) { Remove-Item -Recurse -Force $portStage }
New-Item -ItemType Directory -Force $portStage | Out-Null
Copy-Item -Recurse $Stage (Join-Path $portStage "Notepad++Qt")
Set-Content -Path (Join-Path $portStage 'Notepad++Qt\doLocalConf.xml') -Value '' -Encoding ascii
$zip = Join-Path $Dist "$base-portable.zip"
if (Test-Path $zip) { Remove-Item $zip }
New-Zip (Join-Path $portStage 'Notepad++Qt') $zip
Write-Host "  portable  $zip"

# --- symbols for crash dumps ------------------------------------------------------------------
$pdb = Join-Path $Build 'bin\notepadpp-qt.pdb'
if (Test-Path $pdb) {
    $sym = Join-Path $Dist "$base-symbols.zip"
    if (Test-Path $sym) { Remove-Item $sym }
    # the PDB may still be held by mspdbsrv after the build: copy it with shared read access
    $pdbCopy = Join-Path $env:TEMP "npp-pdb-$PID\notepadpp-qt.pdb"
    New-Item -ItemType Directory -Force (Split-Path $pdbCopy) | Out-Null
    $in = [IO.File]::Open($pdb, 'Open', 'Read', 'ReadWrite'); $out = [IO.File]::Create($pdbCopy)
    $in.CopyTo($out); $out.Close(); $in.Close()
    New-Zip (Split-Path $pdbCopy) $sym -Contents
    Remove-Item -Recurse -Force (Split-Path $pdbCopy)
    Write-Host "  symbols   $sym"
}

# --- installer --------------------------------------------------------------------------------
<# Deferred until installer releases resume.
$ErrorActionPreference = 'Continue'
& $Iscc /Q "/DAppVersion=$Version" "/DStageDir=$Stage" "/DOutputDir=$Dist" `
    "/DIconFile=$(Join-Path $Root 'PowerEditor\src\icons\npp.ico')" (Join-Path $Root 'packaging\windows\notepadpp-qt.iss')
if ($LASTEXITCODE -ne 0) { Fail 'ISCC failed' }
Write-Host "  installer $(Join-Path $Dist "$base-setup.exe")"
#>
