<#
.SYNOPSIS
  Find in Files over 10,000 files with the ORIGINAL Notepad++ (docs/reference/performance.md).
  Same folder layout as the port's ui.find.responsiveness test: 20 folders x 500 files,
  "hit me" in every 1000th file. The dialog is driven with window messages:
  WM_COMMAND IDM_SEARCH_FINDINFILES, WM_SETTEXT on the Find what / Directory combos,
  BM_CLICK on "Find All"; done = the process used no CPU for 1 s.
#>
param([string]$Npp = '', [int]$Runs = 3)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path "$PSScriptRoot\..\..").Path
if (-not $Npp) { $Npp = Join-Path $Root 'PowerEditor\bin64\notepad++.exe' }
$Work = Join-Path $Root 'build\perf'
$dir = Join-Path $Work 'fif10k'
if (-not (Test-Path (Join-Path $dir 'd19\f9999.txt'))) {
    Write-Host 'creating 10,000 files...'
    for ($i = 0; $i -lt 10000; $i++) {
        $d = Join-Path $dir ('d{0}' -f [math]::Floor($i / 500))
        if ($i % 500 -eq 0) { New-Item -ItemType Directory -Force $d | Out-Null }
        [IO.File]::WriteAllText((Join-Path $d "f$i.txt"), $(if ($i % 1000 -eq 0) { "hit me`n" } else { "nothing here`n" }))
    }
}

Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class W {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr l);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, string l);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    public static IntPtr FindDialog(uint pid) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p);
            if (p == pid && GetDlgItem(h, 1656) != IntPtr.Zero && GetDlgItem(h, 1601) != IntPtr.Zero) { found = h; return false; }
            return true; }, IntPtr.Zero);
        return found;
    }
}
'@
$WM_COMMAND = 0x0111; $WM_SETTEXT = 0x000C; $BM_CLICK = 0x00F5

$times = @()
for ($r = 0; $r -lt $Runs; $r++) {
    $settings = Join-Path $Work "settings-npp-fif-$r"
    if (Test-Path $settings) { Remove-Item -Recurse -Force $settings }
    New-Item -ItemType Directory -Force $settings | Out-Null
    $p = Start-Process -FilePath $Npp -ArgumentList @('-multiInst', '-nosession', "`"-settingsDir=$settings`"") -PassThru
    while ($p.MainWindowHandle -eq 0) { Start-Sleep -Milliseconds 50; $p.Refresh() }
    Start-Sleep -Milliseconds 500
    [void][W]::SendMessage($p.MainWindowHandle, $WM_COMMAND, [IntPtr]43013, [IntPtr]::Zero) # IDM_SEARCH_FINDINFILES
    $dlg = [IntPtr]::Zero
    for ($i = 0; $i -lt 100 -and $dlg -eq [IntPtr]::Zero; $i++) { Start-Sleep -Milliseconds 50; $dlg = [W]::FindDialog([uint32]$p.Id) }
    if ($dlg -eq [IntPtr]::Zero) { Stop-Process -Id $p.Id -Force; throw 'Find in Files dialog not found' }
    [void][W]::SendMessage([W]::GetDlgItem($dlg, 1601), $WM_SETTEXT, [IntPtr]::Zero, 'hit')
    [void][W]::SendMessage([W]::GetDlgItem($dlg, 1653), $WM_SETTEXT, [IntPtr]::Zero, $dir)
    [void][W]::SendMessage([W]::GetDlgItem($dlg, 1652), $WM_SETTEXT, [IntPtr]::Zero, '*.*')
    Start-Sleep -Milliseconds 300
    $p.Refresh(); $cpu0 = $p.TotalProcessorTime
    $sw = [Diagnostics.Stopwatch]::StartNew()
    [void][W]::PostMessage([W]::GetDlgItem($dlg, 1656), $BM_CLICK, [IntPtr]::Zero, [IntPtr]::Zero)
    $last = $cpu0; $lastChange = 0
    while ($sw.ElapsedMilliseconds - $lastChange -lt 1000 -and $sw.ElapsedMilliseconds -lt 300000) {
        Start-Sleep -Milliseconds 25; $p.Refresh()
        if ($p.TotalProcessorTime -ne $last) { $last = $p.TotalProcessorTime; $lastChange = $sw.ElapsedMilliseconds }
    }
    $times += $lastChange
    Stop-Process -Id $p.Id -Force
    Write-Host "run $r : $lastChange ms"
}
$sorted = @($times | Sort-Object)
Write-Host ("Notepad++ Find in Files 10k files: median {0} ms [{1}]" -f $sorted[[int][math]::Floor($sorted.Count / 2)], ($times -join ' '))
