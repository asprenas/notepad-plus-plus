<#
.SYNOPSIS
  Performance comparison with the ORIGINAL Notepad++ on Windows (docs/phase-6 3.5, docs/performance.md).

  Both programs run as separate instances (-multiInst) with temporary settings folders
  (-settingsDir), so installed copies and their settings are not touched. Windows appear on
  screen briefly. Every case runs -Runs times; the median is reported.

  Cases
    startup20   session.xml with 20 source files -> ready
    empty       no session, one empty document -> ready, private memory
    large500    a 500 MB text file from the command line -> ready, private memory

  "ready" = main window exists and the process used no CPU for 500 ms (end time = last CPU use).

.PARAMETER Npp   original notepad++.exe (default: PowerEditor\bin64\notepad++.exe built from this repo)
.PARAMETER Port  notepadpp-qt.exe (default: build\package\stage-windows\Notepad++Qt\notepadpp-qt.exe)
#>
param(
    [string]$Npp = '',
    [string]$Port = '',
    [int]$Runs = 5,
    [string[]]$Cases = @('startup20', 'empty', 'large500')
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path "$PSScriptRoot\..\..").Path
if (-not $Npp) { $Npp = Join-Path $Root 'PowerEditor\bin64\notepad++.exe' }
if (-not $Port) { $Port = Join-Path $Root 'build\package\stage-windows\Notepad++Qt\notepadpp-qt.exe' }
foreach ($e in $Npp, $Port) { if (-not (Test-Path $e)) { throw "not found: $e" } }
$Work = Join-Path $Root 'build\perf'
New-Item -ItemType Directory -Force $Work | Out-Null

function Wait-Ready([System.Diagnostics.Process]$p, [System.Diagnostics.Stopwatch]$sw, [int]$timeoutMs = 120000) {
    while ($p.MainWindowHandle -eq 0 -and $sw.ElapsedMilliseconds -lt $timeoutMs) { Start-Sleep -Milliseconds 20; $p.Refresh() }
    $lastCpu = $p.TotalProcessorTime; $lastChange = $sw.ElapsedMilliseconds
    while ($sw.ElapsedMilliseconds - $lastChange -lt 500 -and $sw.ElapsedMilliseconds -lt $timeoutMs) {
        Start-Sleep -Milliseconds 50; $p.Refresh()
        if ($p.TotalProcessorTime -ne $lastCpu) { $lastCpu = $p.TotalProcessorTime; $lastChange = $sw.ElapsedMilliseconds }
    }
    return $lastChange
}

function Run-Case([string]$exe, [string[]]$arguments) {
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $p = Start-Process -FilePath $exe -ArgumentList $arguments -PassThru
    $ms = Wait-Ready $p $sw
    $p.Refresh()
    $mem = [math]::Round($p.PrivateMemorySize64 / 1MB, 1)
    $ws = [math]::Round($p.WorkingSet64 / 1MB, 1)
    Stop-Process -Id $p.Id -Force
    $p.WaitForExit()
    return [pscustomobject]@{ Ms = $ms; PrivateMB = $mem; WorkingSetMB = $ws }
}

function Median($values) { $s = @($values | Sort-Object); return $s[[int][math]::Floor($s.Count / 2)] }

function New-Settings([string]$name, [string]$sessionXml) {
    $d = Join-Path $Work "settings-$name"
    if (Test-Path $d) { Remove-Item -Recurse -Force $d }
    New-Item -ItemType Directory -Force $d | Out-Null
    if ($sessionXml) { Set-Content -Path (Join-Path $d 'session.xml') -Value $sessionXml -Encoding utf8 }
    return $d
}

# --- inputs ------------------------------------------------------------------------------------
$files = @(Get-ChildItem (Join-Path $Root 'PowerEditor\src') -Recurse -Include *.cpp, *.h | Sort-Object Length -Descending | Select-Object -First 20)
$sessionFiles = ($files | ForEach-Object { "            <File filename=`"$($_.FullName)`" />" }) -join "`r`n"
$session = @"
<?xml version="1.0" encoding="UTF-8" ?>
<NotepadPlus>
    <Session activeView="0">
        <mainView activeIndex="0">
$sessionFiles
        </mainView>
        <subView activeIndex="0" />
    </Session>
</NotepadPlus>
"@
$large = Join-Path $Work 'large-500mb.txt'
if (-not (Test-Path $large) -or (Get-Item $large).Length -lt 500MB) {
    Write-Host 'creating 500 MB test file...'
    $line = [Text.Encoding]::ASCII.GetBytes(('The quick brown fox jumps over the lazy dog 0123456789 ' * 2).TrimEnd() + "`r`n")
    $block = New-Object byte[] ($line.Length * 10000)
    for ($i = 0; $i -lt 10000; $i++) { [Array]::Copy($line, 0, $block, $i * $line.Length, $line.Length) }
    $fs = [IO.File]::Create($large)
    while ($fs.Length -lt 500MB) { $fs.Write($block, 0, $block.Length) }
    $fs.Close()
}

# --- runs --------------------------------------------------------------------------------------
$results = @()
foreach ($case in $Cases) {
    foreach ($app in @(@{ Name = 'Notepad++'; Exe = $Npp }, @{ Name = 'Qt port'; Exe = $Port })) {
        $samples = @()
        for ($i = 0; $i -lt $Runs; $i++) {
            switch ($case) {
                'startup20' { $s = New-Settings "$($app.Name -replace '\W','')-$case" $session; $a = @('-multiInst', "`"-settingsDir=$s`"") }
                'empty'     { $s = New-Settings "$($app.Name -replace '\W','')-$case" ''; $a = @('-multiInst', '-nosession', "`"-settingsDir=$s`"") }
                'large500'  { $s = New-Settings "$($app.Name -replace '\W','')-$case" ''; $a = @('-multiInst', '-nosession', "`"-settingsDir=$s`"", "`"$large`"") }
            }
            $samples += Run-Case $app.Exe $a
        }
        $r = [pscustomobject]@{
            Case = $case; App = $app.Name
            ReadyMs = Median ($samples | ForEach-Object Ms)
            PrivateMB = Median ($samples | ForEach-Object PrivateMB)
            WorkingSetMB = Median ($samples | ForEach-Object WorkingSetMB)
            Samples = ($samples | ForEach-Object Ms) -join ' '
        }
        $results += $r
        Write-Host ("{0,-10} {1,-10} ready {2,6} ms  private {3,7} MB  ws {4,7} MB   [{5}]" -f $r.Case, $r.App, $r.ReadyMs, $r.PrivateMB, $r.WorkingSetMB, $r.Samples)
    }
}
$out = Join-Path $Work 'results-windows.csv'
$results | Export-Csv -NoTypeInformation -Path $out -Encoding utf8
Write-Host "results: $out"
