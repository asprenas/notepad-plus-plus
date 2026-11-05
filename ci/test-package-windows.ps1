<#
.SYNOPSIS
  Checks the Windows packages in dist/ (docs/phase-6-packaging-stabilization.md section 5):
  1. every DLL the executable and the Qt plugins import is in the package or is a Windows DLL
  2. the portable zip runs with a bare PATH (no Qt / compiler on PATH) and keeps its settings
     in its own folder (doLocalConf.xml)
  3. the installer installs silently for the current user (with the Explorer menu and file
     association tasks), the installed program runs and opens a file, the registry entries
     exist; the uninstaller removes the files and the registry entries again

  Results go to build/package/test-windows/report.txt.
.PARAMETER Dist      folder with the packages (default <repo>\dist)
.PARAMETER Version   package version (default: from CMakeLists.txt)
#>
param([string]$Dist = '', [string]$Version = '')
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
if (-not $Dist) { $Dist = Join-Path $Root 'dist' }
if (-not $Version) {
    $m = Select-String -Path (Join-Path $Root 'CMakeLists.txt') -Pattern '^\s*VERSION\s+(\d+\.\d+\.\d+)' | Select-Object -First 1
    $Version = $m.Matches[0].Groups[1].Value
}
$base = "notepadpp-qt-$Version-windows-x64"
$Work = Join-Path $Root 'build\package\test-windows'
if (Test-Path $Work) { Remove-Item -Recurse -Force $Work }
New-Item -ItemType Directory -Force $Work | Out-Null
$report = New-Object System.Collections.Generic.List[string]
$failed = 0
function Check($name, [bool]$ok, $detail = '') {
    $line = '{0,-58} {1}' -f $name, $(if ($ok) { 'PASS' } else { 'FAIL' })
    if ($detail) { $line += "  ($detail)" }
    $report.Add($line); Write-Host $line
    if (-not $ok) { $script:failed++ }
}

# Bare environment for child processes: Windows folders only on PATH, no QT_* variables
$bareEnv = @{ PATH = "$env:SystemRoot\System32;$env:SystemRoot;$env:SystemRoot\System32\Wbem"; QT_PLUGIN_PATH = $null; QT_QPA_PLATFORM_PLUGIN_PATH = $null; QML2_IMPORT_PATH = $null }
function Run-Bare([string]$exe, [string[]]$arguments, [int]$timeoutSec = 60) {
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $exe
    $psi.Arguments = ($arguments | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } }) -join ' '
    $psi.UseShellExecute = $false
    $psi.RedirectStandardError = $true
    $psi.WorkingDirectory = Split-Path -Parent $exe
    foreach ($k in $bareEnv.Keys) {
        if ($null -eq $bareEnv[$k]) { $psi.EnvironmentVariables.Remove($k) } else { $psi.EnvironmentVariables[$k] = $bareEnv[$k] }
    }
    $psi.EnvironmentVariables['NPP_QT_INSTANCE_KEY'] = "npp-package-test-$PID"
    $p = [System.Diagnostics.Process]::Start($psi)
    $err = $p.StandardError.ReadToEndAsync()
    if (-not $p.WaitForExit($timeoutSec * 1000)) { $p.Kill(); return @{ Code = -1; Err = 'timeout' } }
    return @{ Code = $p.ExitCode; Err = $err.Result }
}

# --- 1. imports --------------------------------------------------------------------------------
$zip = Join-Path $Dist "$base-portable.zip"
Check 'portable zip exists' (Test-Path $zip) $zip
Expand-Archive -Path $zip -DestinationPath (Join-Path $Work 'portable')
$app = Join-Path $Work 'portable\Notepad++Qt'
$exe = Join-Path $app 'notepadpp-qt.exe'

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$dumpbin = Get-ChildItem "$vs\VC\Tools\MSVC\*\bin\Hostx64\x64\dumpbin.exe" | Select-Object -Last 1
$shipped = @{}
Get-ChildItem $app -Recurse -Filter *.dll | ForEach-Object { $shipped[$_.Name.ToLower()] = $true }
$missing = @()
foreach ($bin in @(Get-Item $exe) + (Get-ChildItem $app -Recurse -Filter *.dll)) {
    $deps = & $dumpbin.FullName /nologo /dependents $bin.FullName | Where-Object { $_ -match '^\s+\S+\.dll\s*$' } | ForEach-Object { $_.Trim().ToLower() }
    foreach ($d in $deps) {
        if ($shipped[$d]) { continue }
        if ($d -like 'api-ms-win-*' -or $d -like 'ext-ms-*') { continue }
        if (Test-Path (Join-Path "$env:SystemRoot\System32" $d)) { continue }
        $missing += "$($bin.Name) -> $d"
    }
}
Check 'all imported DLLs shipped or part of Windows' ($missing.Count -eq 0) ($missing -join ', ')
Check 'MSVC runtime shipped app-local' ((Test-Path "$app\vcruntime140.dll") -and (Test-Path "$app\msvcp140.dll"))
Check 'Qt platform plugin shipped' (Test-Path "$app\platforms\qwindows.dll")

# --- 2. portable run --------------------------------------------------------------------------
$sample = Join-Path $Work 'sample.txt'
Set-Content -Path $sample -Value 'package test' -Encoding ascii
$shot = Join-Path $Work 'portable.png'
$r = Run-Bare $exe @('--screenshot', $shot, $sample)
Check 'portable: starts with a bare PATH and renders' ($r.Code -eq 0 -and (Test-Path $shot)) "exit $($r.Code) $($r.Err.Trim())"
$r = Run-Bare $exe @('--quit-after=2500', $sample)
Check 'portable: opens a file from the command line' ($r.Code -eq 0 -and $r.Err -match 'state: tabs=1 ') ($r.Err.Trim())
Check 'portable: settings written next to the executable' (Test-Path "$app\config.xml")

# --- 3. installer (current user, silent) --------------------------------------------------------
$setup = Join-Path $Dist "$base-setup.exe"
Check 'installer exists' (Test-Path $setup) $setup
$dir = Join-Path $Work 'installed'
$log = Join-Path $Work 'install.log'
$p = Start-Process -FilePath $setup -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/CURRENTUSER',
        "/DIR=`"$dir`"", '/TASKS="contextmenu,associate"', "/LOG=`"$log`"") -Wait -PassThru
Check 'installer: silent per-user install' ($p.ExitCode -eq 0 -and (Test-Path "$dir\notepadpp-qt.exe")) "exit $($p.ExitCode)"
$menu = Get-ItemProperty -Path 'HKCU:\Software\Classes\*\shell\notepadpp-qt\command' -ErrorAction SilentlyContinue
Check 'installer: Explorer "Edit with" menu registered' ($menu -and $menu.'(default)' -like "*$dir*")
$progid = Get-ItemProperty -Path 'HKCU:\Software\Classes\NotepadPPQt.File\shell\open\command' -ErrorAction SilentlyContinue
$openWith = Get-ItemProperty -Path 'HKCU:\Software\Classes\.txt\OpenWithProgids' -ErrorAction SilentlyContinue
Check 'installer: file association (ProgID + .txt Open with)' ($progid -and $openWith -and ($openWith.PSObject.Properties.Name -contains 'NotepadPPQt.File'))
$cap = Get-ItemProperty -Path 'HKCU:\Software\RegisteredApplications' -ErrorAction SilentlyContinue
Check 'installer: registered for Default Apps' ($cap -and ($cap.PSObject.Properties.Name -contains 'Notepad++ Qt'))
$uninst = Get-ChildItem 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall' | Where-Object { $_.PSChildName -like '*6E0B5B54-2C0E-4C55-9A1C-7A4E6D3F0B21*' }
Check 'installer: Apps & features entry' ($null -ne $uninst)

# The association command line is what Explorer runs: use it to open the sample
if ($progid) {
    $cmd = $progid.'(default)' -replace '%1', $sample
    $exeInstalled = ($cmd -split '"')[1]
    $settings = Join-Path $Work 'installed-settings'
    New-Item -ItemType Directory -Force $settings | Out-Null
    $r = Run-Bare $exeInstalled @("-settingsDir=$settings", '--quit-after=2500', $sample)
    Check 'installed: association command opens the file' ($r.Code -eq 0 -and $r.Err -match 'state: tabs=1 ') ($r.Err.Trim())
}

# Uninstall
$unins = Join-Path $dir 'unins000.exe'
$p = Start-Process -FilePath $unins -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART') -Wait -PassThru
Start-Sleep -Seconds 2   # the uninstaller finishes from a temporary copy
Check 'uninstall: exit code 0' ($p.ExitCode -eq 0) "exit $($p.ExitCode)"
Check 'uninstall: program files removed' (-not (Test-Path "$dir\notepadpp-qt.exe"))
Check 'uninstall: Explorer menu removed' (-not (Test-Path 'HKCU:\Software\Classes\*\shell\notepadpp-qt'))
Check 'uninstall: ProgID removed' (-not (Test-Path 'HKCU:\Software\Classes\NotepadPPQt.File'))
$openWith = Get-ItemProperty -Path 'HKCU:\Software\Classes\.txt\OpenWithProgids' -ErrorAction SilentlyContinue
Check 'uninstall: .txt Open with entry removed' (-not ($openWith -and ($openWith.PSObject.Properties.Name -contains 'NotepadPPQt.File')))
$cap = Get-ItemProperty -Path 'HKCU:\Software\RegisteredApplications' -ErrorAction SilentlyContinue
Check 'uninstall: Default Apps registration removed' (-not ($cap -and ($cap.PSObject.Properties.Name -contains 'Notepad++ Qt')))

$report.Insert(0, "Windows package test $base  $(Get-Date -Format s)  failed: $failed")
$report | Set-Content -Path (Join-Path $Work 'report.txt') -Encoding utf8
Write-Host "report: $(Join-Path $Work 'report.txt')"
exit $(if ($failed) { 1 } else { 0 })
