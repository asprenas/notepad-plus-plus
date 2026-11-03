<#
Settings compatibility with the ORIGINAL Windows Notepad++ in both directions (phase 4 criterion
"설정이 원본 Notepad++와 양방향으로 호환").

Notepad++ runs as a separate instance (-multiInst) with temporary settings folders (-settingsDir),
so a running Notepad++ and its settings are not touched.

  1. NPP -> port: Notepad++ writes its default config.xml; the port loads and re-saves it
     (settings_compat roundtrip); every element / attribute / text must be unchanged.
  2. port -> NPP: the port writes config.xml with every <GUIConfig> field set to a non-default
     value plus shortcuts.xml (user shortcut, macro, Scintilla key) (settings_compat mutate);
     Notepad++ loads and re-saves them; settings_compat check lists what Notepad++ did not keep.

Writes the report to app/tests/data/settings-compat/report.txt.

Usage (Windows PowerShell, from the repository root, after building settings_compat):
  powershell -ExecutionPolicy Bypass -File tools/settings-compat/run-with-npp.ps1 `
      [-Npp "C:\Program Files\Notepad++\notepad++.exe"] [-Tool build\windows-mingw-debug\bin\settings_compat.exe]
#>
param(
    [string]$Npp = "C:\Program Files\Notepad++\notepad++.exe",
    [string]$Tool = "build\windows-mingw-debug\bin\settings_compat.exe",
    [string]$QtBin = "C:\Programs\Qt\6.12.0\mingw_64\bin"
)
$ErrorActionPreference = "Stop"
$root = (Resolve-Path "$PSScriptRoot\..\..").Path
$Tool = (Resolve-Path (Join-Path $root $Tool)).Path
$env:PATH = "$QtBin;$env:PATH"
$work = Join-Path ([IO.Path]::GetTempPath()) ("npp-settings-compat-" + [Guid]::NewGuid().ToString("N").Substring(0, 8))
$report = New-Object System.Collections.Generic.List[string]
# Settings added after 8.8.8 (read and written by 8.9.8.1 Parameters.cpp, the version the port follows):
# an older Notepad++ drops them, which is expected.
$newerThan888 = @("zoomSync", "disableSelectedTextDragDrop", "isFawSymlinkAllowed", "fawSymlinkAllowed", "formFeedPageBreak",
                  "printFormFeedPageBreak", "FiF_ignoreunsavedChangesInOpenedFiles", "fifIgnoreUnsavedChanges",
                  "shortcutsXmlHMAC", "ColorPickerCustomColors")
function Note([string]$line) {
    foreach ($n in $newerThan888) { if ($line -match "\b$n\b") { return "$line   (8.9.x setting, unknown to 8.8.8)" } }
    if ($line -match "maintainIndent") { return "$line   (8.9.8.1 default is 2 = advanced)" }
    if ($line -match "FluentCustomColor") { return "$line   (0 = accent colour; Notepad++ stores the resolved accent)" }
    if ($line -match "autoUpdateNextDate") { return "$line   (written by Notepad++'s updater)" }
    return $line
}
$version = (Get-Item $Npp).VersionInfo.ProductVersion
$report.Add("Original Notepad++ $version ($Npp)")

function Run-Npp([string]$settingsDir) {
    $p = Start-Process -FilePath $Npp -ArgumentList @("-multiInst", "-nosession", "-notabbar", "-settingsDir=`"$settingsDir`"") -PassThru
    $deadline = (Get-Date).AddSeconds(30)
    while ($p.MainWindowHandle -eq 0 -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 200; $p.Refresh() }
    if ($p.MainWindowHandle -eq 0) { $p.Kill(); throw "Notepad++ window not found" }
    Start-Sleep -Milliseconds 1500
    [void]$p.CloseMainWindow()          # normal exit: Notepad++ saves its settings
    if (-not $p.WaitForExit(20000)) { $p.Kill(); throw "Notepad++ did not exit" }
}

# flattened "path/@attr=value" and "path=text" lines of an XML file (GUIConfig keyed by name)
function Flatten([string]$file) {
    $xml = New-Object Xml.XmlDocument
    $xml.PreserveWhitespace = $false
    $xml.Load($file)
    $lines = New-Object System.Collections.Generic.List[string]
    function Walk($node, [string]$path) {
        foreach ($c in $node.ChildNodes) {
            if ($c.NodeType -eq "Element") {
                $key = $c.Name
                $nameAttr = $c.GetAttribute("name")
                if ($nameAttr) { $key += "[$nameAttr]" }
                $p = "$path/$key"
                foreach ($a in $c.Attributes) { $lines.Add("$p/@$($a.Name)=$($a.Value)") }
                Walk $c $p
            } elseif ($c.NodeType -eq "Text" -or $c.NodeType -eq "CDATA") {
                $lines.Add("$path=$($c.Value.Trim())")
            }
        }
    }
    Walk $xml.DocumentElement "/NotepadPlus"
    return $lines
}

New-Item -ItemType Directory -Force "$work\npp-default", "$work\port-roundtrip", "$work\port-written" | Out-Null
try {
    # ---- 1. NPP -> port
    Run-Npp "$work\npp-default"
    $fresh = "$work\npp-default\config.xml"
    if (-not (Test-Path $fresh)) { throw "Notepad++ did not write $fresh" }
    $out = & $Tool roundtrip $fresh "$work\port-roundtrip\config.xml"
    $report.Add("")
    $report.Add("== 1. NPP -> port: Notepad++ default config.xml loaded and re-saved by the port")
    $out | ForEach-Object { $report.Add("  " + (Note $_)) }
    $a = Flatten $fresh
    $b = Flatten "$work\port-roundtrip\config.xml"
    $missing = $a | Where-Object { $b -notcontains $_ }
    $added = $b | Where-Object { $a -notcontains $_ }
    $report.Add("  lines in NPP file: $($a.Count), lost by the port: $(@($missing).Count), added by the port: $(@($added).Count)")
    $missing | ForEach-Object { $report.Add("  - $_") }
    $added | ForEach-Object { $report.Add("  + " + (Note $_)) }

    # ---- 2. port -> NPP
    & $Tool mutate $fresh "$work\port-written" | Out-Null
    Copy-Item "$work\port-written\config.xml" "$work\port-written.config.xml"
    Run-Npp "$work\port-written"
    $out = & $Tool check $fresh "$work\port-written"
    $report.Add("")
    $report.Add("== 2. port -> NPP: config.xml (all GUIConfig fields changed) + shortcuts.xml written by the port, loaded and re-saved by Notepad++")
    $out | ForEach-Object { $report.Add("  " + (Note $_)) }
    $unexpected = @($out | Where-Object { $_ -match "^not-kept|^shortcuts" -and (Note $_) -eq $_ })
    $report.Add("  unexpected differences: $($unexpected.Count)")
}
finally {
    $dest = Join-Path $root "app\tests\data\settings-compat"
    New-Item -ItemType Directory -Force $dest | Out-Null
    $report | Set-Content -Encoding utf8 (Join-Path $dest "report.txt")
    $report | ForEach-Object { Write-Output $_ }
    Write-Output "work dir: $work"
}
