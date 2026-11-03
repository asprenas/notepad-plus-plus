<#
Extracts the expected regex results from the ORIGINAL Windows Notepad++ (phase 3 regex
compatibility test). Runs every case of app/tests/data/regex/cases.json in the Scintilla
editor of a separate Notepad++ instance (own temporary settings folder, no session), with
the same search flags and loops as npp-core (Search.cpp findAll / replaceAll), and writes
app/tests/data/regex/expected.json.

Usage (Windows PowerShell, from the repository root):
  powershell -ExecutionPolicy Bypass -File tools/regex-compat/extract-from-npp.ps1 [-Npp "C:\Program Files\Notepad++\notepad++.exe"]
#>
param([string]$Npp = "C:\Program Files\Notepad++\notepad++.exe")
$ErrorActionPreference = "Stop"
$root = (Resolve-Path "$PSScriptRoot\..\..").Path

Add-Type -TypeDefinition @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

public class SciRemote {
    [DllImport("user32.dll")] static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern IntPtr FindWindowEx(IntPtr p, IntPtr after, string cls, string title);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);
    [DllImport("kernel32.dll")] static extern IntPtr VirtualAllocEx(IntPtr p, IntPtr a, UIntPtr size, uint type, uint protect);
    [DllImport("kernel32.dll")] static extern bool WriteProcessMemory(IntPtr p, IntPtr a, byte[] b, UIntPtr n, out UIntPtr w);
    [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr p, IntPtr a, byte[] b, UIntPtr n, out UIntPtr r);

    IntPtr sci, proc, buf;
    const int BufSize = 1 << 20;

    public SciRemote(IntPtr mainWindow) {
        sci = FindWindowEx(mainWindow, IntPtr.Zero, "Scintilla", null);
        if (sci == IntPtr.Zero) throw new Exception("Scintilla window not found");
        uint pid; GetWindowThreadProcessId(sci, out pid);
        proc = OpenProcess(0x0008 | 0x0010 | 0x0020 | 0x0400, false, pid);
        buf = VirtualAllocEx(proc, IntPtr.Zero, (UIntPtr)BufSize, 0x3000, 0x04);
        if (buf == IntPtr.Zero) throw new Exception("VirtualAllocEx failed");
    }
    long S(uint m, long w = 0, long l = 0) { return (long)SendMessage(sci, m, (IntPtr)w, (IntPtr)l); }
    void Put(byte[] b) {
        var z = new byte[b.Length + 1]; Array.Copy(b, z, b.Length);
        UIntPtr n; WriteProcessMemory(proc, buf, z, (UIntPtr)z.Length, out n);
    }
    static byte[] U(string s) { return Encoding.UTF8.GetBytes(s); }

    const uint SETTEXT = 2181, GETTEXT = 2182, GETLENGTH = 2006, SETCODEPAGE = 2037, SETEOLMODE = 2031,
        SETSEARCHFLAGS = 2198, SETTARGETRANGE = 2686, SEARCHINTARGET = 2197, GETTARGETSTART = 2191, GETTARGETEND = 2193,
        REPLACETARGET = 2194, REPLACETARGETRE = 2195, POSITIONAFTER = 2418, SETREADONLY = 2171, EMPTYUNDO = 2175;

    void SetText(string t) { S(SETREADONLY, 0); S(SETCODEPAGE, 65001); Put(U(t)); S(SETTEXT, 0, (long)buf); }
    public string GetText() {
        long len = S(GETLENGTH); S(GETTEXT, len + 1, (long)buf);
        var b = new byte[len]; UIntPtr r; ReadProcessMemory(proc, buf, b, (UIntPtr)len, out r);
        return Encoding.UTF8.GetString(b);
    }
    long Search(byte[] pat, long from, long to) { Put(pat); S(SETTARGETRANGE, from, to); return S(SEARCHINTARGET, pat.Length, (long)buf); }

    // same loop as npp-core findAll
    public List<long[]> FindAll(string text, string find, int flags) {
        SetText(text);
        var pat = U(find); var outp = new List<long[]>();
        long end = S(GETLENGTH), pos = 0;
        S(SETSEARCHFLAGS, flags);
        while (pos <= end) {
            if (Search(pat, pos, end) < 0) break;
            long s = S(GETTARGETSTART), t = S(GETTARGETEND);
            outp.Add(new long[] { s, t });
            if (t > s) pos = t; else { long a = S(POSITIONAFTER, t); pos = a > t ? a : t + 1; }
            if (pos > end || (t == end && t == s)) break;
        }
        return outp;
    }
    // same loop as npp-core replaceAll
    public int ReplaceAll(string text, string find, string repl, int flags, bool regex) {
        SetText(text);
        var pat = U(find); var rep = U(repl); int count = 0;
        long end = S(GETLENGTH), pos = 0;
        S(SETSEARCHFLAGS, flags);
        while (pos <= end) {
            if (Search(pat, pos, end) < 0) break;
            long s = S(GETTARGETSTART), t = S(GETTARGETEND);
            Put(rep); S(SETTARGETRANGE, s, t);
            S(regex ? REPLACETARGETRE : REPLACETARGET, rep.Length, (long)buf);
            long newEnd = S(GETTARGETEND);
            ++count; end += (newEnd - s) - (t - s); pos = newEnd;
            if (t == s) { if (pos >= end) break; pos = S(POSITIONAFTER, pos); }
        }
        S(EMPTYUNDO);
        return count;
    }
}
"@

if (-not (Test-Path $Npp)) { throw "Notepad++ not found: $Npp" }
$version = (Get-Item $Npp).VersionInfo.ProductVersion
$settings = Join-Path ([IO.Path]::GetTempPath()) ("npp-regex-" + [guid]::NewGuid())
New-Item -ItemType Directory $settings | Out-Null
$p = Start-Process $Npp -ArgumentList "-multiInst", "-nosession", "-notabbar", "-settingsDir=`"$settings`"" -PassThru
try {
    $deadline = (Get-Date).AddSeconds(30)
    while ($p.MainWindowHandle -eq 0 -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 200; $p.Refresh() }
    if ($p.MainWindowHandle -eq 0) { throw "Notepad++ window did not appear" }
    Start-Sleep -Milliseconds 500
    $sci = New-Object SciRemote($p.MainWindowHandle)

    $cases = Get-Content -Raw -Encoding UTF8 "$root\app\tests\data\regex\cases.json" | ConvertFrom-Json
    $results = @()
    foreach ($c in $cases) {
        $flags = 0
        if ($c.wholeWord -and $c.mode -ne "regex") { $flags = $flags -bor 0x2 }
        if ($c.matchCase) { $flags = $flags -bor 0x4 }
        if ($c.mode -eq "regex") {
            $flags = $flags -bor 0x200000 -bor 0x400000
            if ($c.dotMatchesNewline) { $flags = $flags -bor 0x10000000 }
        }
        $findFlags = $flags -bor 0x40000000 -bor 0x08000000                      # EMPTYMATCH_ALL | SKIPCRLFASONE
        $replFlags = $flags -bor 0x20000000 -bor 0x08000000                      # EMPTYMATCH_NOTAFTERMATCH | SKIPCRLFASONE
        $matches = $sci.FindAll($c.text, $c.engineFind, [int]$findFlags)
        $count = $sci.ReplaceAll($c.text, $c.engineFind, $c.engineReplace, [int]$replFlags, $c.mode -eq "regex")
        $replaced = $sci.GetText()
        $results += [pscustomobject]@{ name = $c.name; matches = @($matches | ForEach-Object { , @($_[0], $_[1]) }); replaceCount = $count; replaced = $replaced }
    }
    $doc = [pscustomobject]@{ source = "Notepad++ $version (Windows), extracted by tools/regex-compat/extract-from-npp.ps1"; results = $results }
    $json = $doc | ConvertTo-Json -Depth 6
    [IO.File]::WriteAllText("$root\app\tests\data\regex\expected.json", $json + "`n", (New-Object Text.UTF8Encoding($false)))
    Write-Host "$($results.Count) cases extracted from Notepad++ $version"
} finally {
    if (-not $p.HasExited) { $p.Kill() }
    Remove-Item -Recurse -Force $settings -ErrorAction SilentlyContinue
}
