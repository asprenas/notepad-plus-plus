; Inno Setup script of the Windows installer (docs/phase-6-packaging-stabilization.md 3.1).
; Built by ci/package-windows.ps1:
;   ISCC /DAppVersion=0.2.0 /DStageDir=<staged app folder> /DOutputDir=<dist> notepadpp-qt.iss
;
; - per-user (no UAC) or all-users install, chosen in the first page / on the command line
; - optional: desktop icon, Explorer "Edit with Notepad++ Qt" menu, file associations
;   (registered as "Open with" + Default Apps capabilities: Windows keeps the user's choice)
; - settings in %APPDATA%\Notepad++Qt are kept on upgrade; uninstall asks whether to delete them

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef StageDir
  #error StageDir (staged application folder) is required
#endif
#ifndef IconFile
  #error IconFile (npp.ico) is required
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

#define AppName "Notepad++ Qt"
#define AppExe "notepadpp-qt.exe"
#define ProgId "NotepadPPQt.File"

[Setup]
AppId={{6E0B5B54-2C0E-4C55-9A1C-7A4E6D3F0B21}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=Notepad++ Qt port
AppPublisherURL=https://github.com/asprenas/notepad-plus-plus
VersionInfoVersion={#AppVersion}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog commandline
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#OutputDir}
OutputBaseFilename=notepadpp-qt-{#AppVersion}-windows-x64-setup
SetupIconFile={#IconFile}
UninstallDisplayIcon={app}\{#AppExe}
LicenseFile={#StageDir}\LICENSE.txt
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ChangesAssociations=yes
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "korean"; MessagesFile: "compiler:Languages\Korean.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "contextmenu"; Description: "Explorer context menu ""Edit with {#AppName}"""; GroupDescription: "Shell integration:"
Name: "associate"; Description: "Register {#AppName} for text and source files (""Open with"", Default Apps)"; GroupDescription: "Shell integration:"

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Registry]
; Explorer context menu for every file (classic menu; Windows 11: "Show more options")
Root: HKA; Subkey: "Software\Classes\*\shell\notepadpp-qt"; ValueType: string; ValueName: ""; ValueData: "Edit with {#AppName}"; Flags: uninsdeletekey; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\*\shell\notepadpp-qt"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\{#AppExe}"",0"; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\*\shell\notepadpp-qt\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: contextmenu

; Program class used by the associations
Root: HKA; Subkey: "Software\Classes\{#ProgId}"; ValueType: string; ValueName: ""; ValueData: "Text document ({#AppName})"; Flags: uninsdeletekey; Tasks: associate
Root: HKA; Subkey: "Software\Classes\{#ProgId}\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"",0"; Tasks: associate
Root: HKA; Subkey: "Software\Classes\{#ProgId}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: associate
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "{#AppName}"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"" ""%1"""

; Default Apps (Settings > Apps > Default apps)
Root: HKA; Subkey: "Software\RegisteredApplications"; ValueType: string; ValueName: "{#AppName}"; ValueData: "Software\NotepadPPQt\Capabilities"; Flags: uninsdeletevalue; Tasks: associate
Root: HKA; Subkey: "Software\NotepadPPQt"; Flags: uninsdeletekey; Tasks: associate
Root: HKA; Subkey: "Software\NotepadPPQt\Capabilities"; ValueType: string; ValueName: "ApplicationName"; ValueData: "{#AppName}"; Tasks: associate
Root: HKA; Subkey: "Software\NotepadPPQt\Capabilities"; ValueType: string; ValueName: "ApplicationDescription"; ValueData: "Source code editor"; Tasks: associate
#include "associations.iss"

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[Code]
// Uninstall: settings, session and backups are the user's; delete them only on request.
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Dir: String;
begin
  if CurUninstallStep = usPostUninstall then
  begin
    Dir := ExpandConstant('{userappdata}\Notepad++Qt');
    if DirExists(Dir) and not UninstallSilent then
      if MsgBox('Delete your {#AppName} settings, session and backups too?' #13#10 + Dir,
                mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
        DelTree(Dir, True, True, True);
  end;
end;
