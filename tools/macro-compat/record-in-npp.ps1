<#
Records a macro in the ORIGINAL Windows Notepad++ (phase 3 criterion "원본에서 녹화한 매크로가 그대로
재생됨"). A separate Notepad++ instance with its own temporary settings folder:
  1. gets an input text, starts macro recording,
  2. runs menu commands, Scintilla commands and a regex Replace All in its Replace dialog,
  3. stops recording, saves the macro with its own "Save Current Recorded Macro" dialog, exits.
Writes app/tests/data/macros/recorded-in-npp.json: input text, text after the recording
(= expected result of the macro) and the <Macros> block Notepad++ wrote to shortcuts.xml.

Usage (Windows PowerShell, from the repository root):
  powershell -ExecutionPolicy Bypass -File tools/macro-compat/record-in-npp.ps1 [-Npp "C:\Program Files\Notepad++\notepad++.exe"]
#>
param([string]$Npp = "C:\Program Files\Notepad++\notepad++.exe")
$ErrorActionPreference = "Stop"
$root = (Resolve-Path "$PSScriptRoot\..\..").Path

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
using System.Text;

public class NppRemote {
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, string l);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr p, IntPtr after, string cls, string title);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr dlg, int id);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);
    [DllImport("kernel32.dll")] static extern IntPtr VirtualAllocEx(IntPtr p, IntPtr a, UIntPtr size, uint type, uint protect);
    [DllImport("kernel32.dll")] static extern bool WriteProcessMemory(IntPtr p, IntPtr a, byte[] b, UIntPtr n, out UIntPtr w);
    [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr p, IntPtr a, byte[] b, UIntPtr n, out UIntPtr r);
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc f, IntPtr l);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);

    public IntPtr Main, Sci; IntPtr proc, buf; uint pid;
    public NppRemote(IntPtr main) {
        Main = main;
        Sci = FindWindowEx(main, IntPtr.Zero, "Scintilla", null);
        GetWindowThreadProcessId(Sci, out pid);
        proc = OpenProcess(0x0008 | 0x0010 | 0x0020 | 0x0400, false, pid);
        buf = VirtualAllocEx(proc, IntPtr.Zero, (UIntPtr)(1 << 20), 0x3000, 0x04);
    }
    public long S(uint m, long w = 0, long l = 0) { return (long)SendMessage(Sci, m, (IntPtr)w, (IntPtr)l); }
    public long Str(uint m, long w, string text) {
        var b = Encoding.UTF8.GetBytes(text); var z = new byte[b.Length + 1]; Array.Copy(b, z, b.Length);
        UIntPtr n; WriteProcessMemory(proc, buf, z, (UIntPtr)z.Length, out n);
        return S(m, w, (long)buf);
    }
    public string Text() {
        long len = S(2006); S(2182, len + 1, (long)buf);
        var b = new byte[len]; UIntPtr r; ReadProcessMemory(proc, buf, b, (UIntPtr)len, out r);
        return Encoding.UTF8.GetString(b);
    }
    public void Command(int id) { SendMessage(Main, 0x0111, (IntPtr)id, IntPtr.Zero); }
    // visible dialog (#32770) of the process, newest first
    public IntPtr Dialog() {
        IntPtr found = IntPtr.Zero;
        EnumWindows((h, l) => {
            uint p; GetWindowThreadProcessId(h, out p);
            var cls = new StringBuilder(64); GetClassName(h, cls, 64);
            if (p == pid && cls.ToString() == "#32770" && IsWindowVisible(h)) { found = h; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }
}
"@

$WM_SETTEXT = 0x000C; $BM_CLICK = 0x00F5; $WM_CLOSE = 0x0010; $WM_COMMAND = 0x0111
if (-not (Test-Path $Npp)) { throw "Notepad++ not found: $Npp" }
$version = (Get-Item $Npp).VersionInfo.ProductVersion
$settings = Join-Path ([IO.Path]::GetTempPath()) ("npp-macro-" + [guid]::NewGuid())
New-Item -ItemType Directory $settings | Out-Null
$p = Start-Process $Npp -ArgumentList "-multiInst", "-nosession", "-settingsDir=`"$settings`"" -PassThru
function WaitFor($cond, $what) {
    $deadline = (Get-Date).AddSeconds(15)
    while (-not (& $cond)) { if ((Get-Date) -gt $deadline) { throw "timeout: $what" }; Start-Sleep -Milliseconds 100 }
}
try {
    WaitFor { $p.Refresh(); $p.MainWindowHandle -ne 0 } "main window"
    Start-Sleep -Milliseconds 800
    $n = New-Object NppRemote($p.MainWindowHandle)

    $inText = "banana  `r`ncherry`r`napple  `r`nkiwi`r`n"
    $n.S(2037, 65001) | Out-Null                       # SCI_SETCODEPAGE UTF-8
    $n.Str(2181, 0, $inText) | Out-Null                 # SCI_SETTEXT
    $n.S(2025, 0) | Out-Null                           # SCI_GOTOPOS 0

    $n.Command(42018)                                  # Macro > Start Recording
    $n.Command(42024)                                  # Edit > Blank Operations > Trim Trailing Space
    $n.Command(42059)                                  # Edit > Line Operations > Sort Lines Lexicographically Ascending
    $n.S(2318) | Out-Null                              # SCI_DOCUMENTEND
    $n.Str(2170, 0, "zucchini") | Out-Null             # SCI_REPLACESEL (typed text)
    $n.S(2316) | Out-Null                              # SCI_DOCUMENTSTART
    $n.S(2314) | Out-Null                              # SCI_LINEEND
    $n.Str(2170, 0, " (first)") | Out-Null             # SCI_REPLACESEL
    # Search > Replace..., regular expression, Replace All
    $n.Command(43003)
    WaitFor { $n.Dialog() -ne 0 } "Replace dialog"
    $dlg = $n.Dialog()
    [NppRemote]::SendMessage([NppRemote]::GetDlgItem($dlg, 1605), $BM_CLICK, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null   # Regular expression
    [NppRemote]::SendMessage([NppRemote]::GetDlgItem($dlg, 1601), $WM_SETTEXT, [IntPtr]::Zero, "^(\w)(\w*)") | Out-Null
    [NppRemote]::SendMessage([NppRemote]::GetDlgItem($dlg, 1602), $WM_SETTEXT, [IntPtr]::Zero, "\U`$1\E`$2") | Out-Null
    [NppRemote]::SendMessage([NppRemote]::GetDlgItem($dlg, 1609), $BM_CLICK, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null   # Replace All
    [NppRemote]::SendMessage($dlg, $WM_CLOSE, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    $n.Command(42016)                                  # Edit > Convert Case to > UPPERCASE (no selection: no change, still recorded)
    $n.Command(42019)                                  # Macro > Stop Recording
    $result = $n.Text()

    # Macro > Save Current Recorded Macro... (modal: posted)
    [NppRemote]::PostMessage($n.Main, $WM_COMMAND, [IntPtr]42025, [IntPtr]::Zero) | Out-Null
    WaitFor { $n.Dialog() -ne 0 } "Save macro dialog"
    $dlg = $n.Dialog()
    [NppRemote]::SendMessage([NppRemote]::GetDlgItem($dlg, 5005), $WM_SETTEXT, [IntPtr]::Zero, "Recorded in Notepad++") | Out-Null
    [NppRemote]::SendMessage([NppRemote]::GetDlgItem($dlg, 1), $BM_CLICK, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 300

    $n.S(2014) | Out-Null                              # SCI_SETSAVEPOINT: no "save?" question on exit
    [NppRemote]::PostMessage($n.Main, $WM_CLOSE, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    if (-not $p.WaitForExit(15000)) { throw "Notepad++ did not exit" }

    $xml = [xml](Get-Content -Raw -Encoding UTF8 "$settings\shortcuts.xml")
    $macro = $xml.NotepadPlus.Macros.Macro | Where-Object { $_.name -eq "Recorded in Notepad++" }
    if (-not $macro) { throw "macro not found in shortcuts.xml" }
    $doc = [pscustomobject]@{
        source = "Recorded by tools/macro-compat/record-in-npp.ps1 in Notepad++ $version (Windows)"
        input = $inText
        expected = $result
        shortcutsXml = "<NotepadPlus><Macros>" + $macro.OuterXml + "</Macros></NotepadPlus>"
    }
    New-Item -ItemType Directory -Force "$root\app\tests\data\macros" | Out-Null
    [IO.File]::WriteAllText("$root\app\tests\data\macros\recorded-in-npp.json", ($doc | ConvertTo-Json -Depth 4) + "`n", (New-Object Text.UTF8Encoding($false)))
    Write-Host "macro recorded in Notepad++ $version"
    Write-Host $macro.OuterXml
    Write-Host "expected: $result"
} finally {
    if (-not $p.HasExited) { $p.Kill() }
    Remove-Item -Recurse -Force $settings -ErrorAction SilentlyContinue
}
